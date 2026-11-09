// Optimal (IDA*) solver in batch form: tables + corner PDB are built once (excluded from timing).
// stdin: "<seed>\t<scramble>" per line   stdout: one JSON per line.
// Unfinished cubes (cap reached) report optimal=null and the proven lower bound.
#include "optimal.hpp"
#include <sys/resource.h>
#include <iomanip>
#include <iostream>
#include <string>

using namespace rephase;
int main(int argc, char** argv) {
    try {
        double cap = 60;
        int threads = 6;
        std::string resources = "../third_party/stemil/resources";
        for (int i = 1; i + 1 < argc; i += 2) {
            std::string k = argv[i];
            if (k == "--cap-sec") cap = std::stod(argv[i + 1]);
            else if (k == "--threads") threads = std::stoi(argv[i + 1]);
            else if (k == "--resources") resources = argv[i + 1];
        }
        Tables tables(resources);
        CornerPdb pdb(11);
        OptimalSolver solver(tables, pdb);
        std::cout << std::fixed << std::setprecision(3);
        std::string line;
        while (std::getline(std::cin, line)) {
            auto tab = line.find('\t');
            if (tab == std::string::npos) continue;
            State cube = State::identity().applied(parse_algorithm(line.substr(tab + 1)));
            auto begin = Clock::now();
            auto r = solver.solve_parallel(cube, begin + std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(cap)), 20, threads);
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
            struct rusage u{};
            getrusage(RUSAGE_SELF, &u);
            std::cout << "{\"seed\":" << line.substr(0, tab) << ",\"solved\":" << (r.solved ? "true" : "false") << ",\"ms\":" << ms
                      << ",\"peak_rss_kib\":" << u.ru_maxrss;
            if (r.solved && cube.applied(r.moves).solved())
                std::cout << ",\"htm\":" << r.length << ",\"moves\":\"" << format_algorithm(r.moves) << "\"";
            else
                std::cout << ",\"htm\":null,\"lower_bound\":" << (r.last_bound_completed + 1);
            std::cout << "}\n" << std::flush;
        }
    } catch (const std::exception& e) { std::cerr << "error: " << e.what() << "\n"; return 1; }
}
