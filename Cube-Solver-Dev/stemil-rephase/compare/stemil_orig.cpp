// Original stemil improved_search, upstream sources untouched, plus a timestamp for
// "when the final-length solution was found". Same loop as upstream improved_search():
//   search(cube,31) first, then random_moves_search with growing prefix depth until the
//   time limit (the limit clock starts after the first search, as upstream does).
// stdin : "<seed>\t<scramble>" per line      stdout: one JSON object per line
// Tables are loaded before any cube is timed (load time excluded).
#include "coordinates.hpp"
#include "moves.hpp"
#include "pruning_tables.hpp"
#include "search.hpp"
#include "cube.hpp"
#include <sys/resource.h>
#include <unistd.h>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
using Clock = std::chrono::steady_clock;
double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

struct Run {
    int limit;
    std::vector<int> best;
    double found_ms = 0;
    Clock::time_point origin, begin;
    int time_limit;
};
// Identical to upstream random_moves_search() except it records the answer and its time instead of printing.
void prefix_search(Cube cube, int depth_now, int depth, int prev, std::vector<int>& prefix, Run& run) {
    if (depth_now == depth) {
        std::vector<int> solution;
        if (search(cube, run.limit - depth, &solution)) {
            run.best = prefix;
            run.best.insert(run.best.end(), solution.begin(), solution.end());
            run.limit = int(solution.size()) + depth;
            run.found_ms = ms_since(run.origin);
        }
        return;
    }
    for (int move = 0; move < 18; move++)
        if (move / 3 != prev / 3 && ms_since(run.begin) < run.time_limit) {
            prefix.push_back(move);
            prefix_search(move_cube(cube, move), depth_now + 1, depth, move, prefix, run);
            prefix.pop_back();
        }
}
}  // namespace

int main(int argc, char** argv) {
    int time_limit = 500;
    std::filesystem::path stemil = "../third_party/stemil", work = "compare/stemil_work";
    bool regen = false;  // integrity check: regenerate moving tables in the private work dir only, then exit
    for (int i = 1; i + 1 < argc; i += 2) {
        std::string k = argv[i];
        if (k == "--time-limit-ms") time_limit = std::stoi(argv[i + 1]);
        else if (k == "--stemil-dir") stemil = argv[i + 1];
        else if (k == "--work-dir") work = argv[i + 1];
        else if (k == "--regen-moving-check") { regen = true; --i; }  // flag without value
    }
    // Upstream generate_moving_tables() REWRITES resources/moving_tables/* in the cwd. Never run it inside
    // the real stemil tree: use a private work dir with its own moving tables and a read-only link to the
    // big pruning tables.
    stemil = std::filesystem::canonical(stemil);
    std::filesystem::create_directories(work / "resources/moving_tables");
    work = std::filesystem::canonical(work);
    if (work == stemil || std::filesystem::equivalent(work / "resources", stemil / "resources")) { std::cerr << "work dir must differ from stemil dir\n"; return 1; }
    auto link = work / "resources/pruning_tables";
    if (!std::filesystem::exists(link)) std::filesystem::create_directory_symlink(stemil / "resources/pruning_tables", link);
    if (!regen) {  // normal run: LOAD the original moving tables through a link (answer "n" => never written)
        std::filesystem::remove_all(work / "resources/moving_tables");
        std::filesystem::create_directory_symlink(stemil / "resources/moving_tables", work / "resources/moving_tables");
    }
    for (const char* t : {"phase1_pruning", "phase2_corner_UD_edge", "phase2_corner_UDslice_edge", "phase2_UD_edge_UDslice_edge"})
        if (!std::filesystem::is_regular_file(stemil / "resources/pruning_tables" / t)) { std::cerr << "missing table " << t << " (not generating)\n"; return 1; }
    std::filesystem::current_path(work);
    // The loaders ask "regenerate table? [y/N]" via cin: answer "n" from a private buffer, keep real stdin for cubes.
    std::istringstream no_answers(regen ? "ynnnnnnnnnnnnnnn" : "nnnnnnnnnnnnnnnn");  // moving-table prompt defaults to GENERATE, so "n" is essential
    auto* real_in = std::cin.rdbuf(no_answers.rdbuf());
    std::cout.setstate(std::ios::failbit);  // silence upstream loader chatter
    generate_moving_tables();
    generate_phase1_pruning_table();
    generate_phase2_pruning_tables();
    std::cout.clear();
    std::cin.rdbuf(real_in);
    if (regen) { std::cerr << "moving tables regenerated in " << work << "\n"; return 0; }
    std::cout << std::fixed << std::setprecision(3);

    std::string line;
    while (std::getline(std::cin, line)) {
        auto tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string seed = line.substr(0, tab), scramble = line.substr(tab + 1);
        rephase::State state = rephase::State::identity().applied(rephase::parse_algorithm(scramble));
        Cube cube = state.coordinates();

        Run run;
        run.time_limit = time_limit;
        run.origin = Clock::now();
        std::vector<int> first;
        search(cube, 31, &first);
        run.best = first;
        run.limit = int(first.size());
        run.found_ms = ms_since(run.origin);
        run.begin = Clock::now();
        std::vector<int> prefix;
        for (int depth = 1; ms_since(run.begin) < time_limit; ++depth) prefix_search(cube, 0, depth, -1, prefix, run);
        bool ok = state.applied(run.best).solved();  // raw sequence, as upstream reports it
        struct rusage u{};
        getrusage(RUSAGE_SELF, &u);
        std::cout << "{\"seed\":" << seed << ",\"solved\":" << (ok ? "true" : "false") << ",\"htm\":" << run.best.size()
                  << ",\"first_htm\":" << first.size() << ",\"found_ms\":" << run.found_ms << ",\"total_ms\":" << ms_since(run.origin)
                  << ",\"peak_rss_kib\":" << u.ru_maxrss << ",\"moves\":\"" << rephase::format_algorithm(run.best) << "\"}\n"
                  << std::flush;
    }
}
