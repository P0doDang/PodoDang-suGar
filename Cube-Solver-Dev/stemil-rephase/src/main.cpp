#include "solver.hpp"
#include "shuffle.hpp"
#include "optimal.hpp"
#include <algorithm>
#include <charconv>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <thread>
#include <stdexcept>
#include <sys/resource.h>
#include <unistd.h>

using namespace rephase;
namespace {
std::atomic<bool> stop_requested{false};
void request_stop(int) { stop_requested=true; }
struct ShortenedCase {
    size_t index=0;std::optional<uint32_t> seed;std::string scramble,initial,final_sequence;
    size_t before=0,after=0;int window=0;
};
struct RunSummary {
    std::vector<ShortenedCase> cases;
    size_t cubes=0,solved=0,measured=0,shortened=0,saved=0,initial_sum=0,final_sum=0,single_sum=0,multi_measured=0;
    double elapsed_sum=0;bool lengths=false;
    // Optimal-solution verification (--verify-optimal).
    bool verify=false;
    size_t optimal=0,not_optimal=0,unknown=0;
    size_t htm_optimal=0,htm_not_optimal=0,htm_unknown=0,optimal_length_sum=0,seconds_count=0;
    double verify_seconds=0;
};
struct VerifyInfo {
    bool done=false;
    int status=2;          // 0 proven optimal, 1 not optimal, 2 undecided
    int optimal_length=-1; // known when status==1 (shorter solution exists) or 0
    int lower_bound=0;     // proven lower bound when undecided
    double seconds=0;
};
void print_summary(const RunSummary& s,bool interrupted,std::ostream& out) {
    out<<std::fixed<<std::setprecision(2)<<"\n[반복 요약] 큐브 "<<s.cubes<<"개, 해결 "<<s.solved<<"개"<<(interrupted?" (중단됨)":"")<<'\n';
    if(s.lengths && s.measured) {
        double n=double(s.measured);
        out<<"  평균 stemil HTM "<<(double(s.initial_sum)/n)<<" -> 평균 최종 HTM "<<(double(s.final_sum)/n)
                 <<(s.multi_measured?"\n  방향 0 단독 평균 HTM "+std::to_string(double(s.single_sum)/double(s.multi_measured)).substr(0,5)+" (6방향 탐색 이득 = 이 값 - 위 stemil 평균)":std::string())
                 <<"\n  rephase로 단축된 큐브 "<<s.shortened<<"개, 줄인 수 합계 "<<s.saved
                 <<"\n  평균 탐색 시간 "<<(s.elapsed_sum/n)<<" ms\n";
    }
    if(s.verify) {
        size_t decided=s.optimal+s.not_optimal,total=decided+s.unknown;
        out<<"\n[최적해 검증] 검증한 큐브 "<<total<<"개: 최적 "<<s.optimal<<"개, 최적 아님 "<<s.not_optimal<<"개, 미확정 "<<s.unknown<<"개\n";
        if(decided)out<<"  최적해 비율: 판정된 "<<decided<<"개 중 "<<(100.0*double(s.optimal)/double(decided))<<"%";
        if(total)out<<", 전체 "<<total<<"개 중 "<<(100.0*double(s.optimal)/double(total))<<"% (미확정은 최적으로 세지 않음)";
        out<<"\n  평균 solve move 수: 전체 "<<(total?double(s.htm_optimal+s.htm_not_optimal+s.htm_unknown)/double(total):0.0)
           <<", 최적 사례 "<<(s.optimal?double(s.htm_optimal)/double(s.optimal):0.0)
           <<", 최적 아닌 사례 "<<(s.not_optimal?double(s.htm_not_optimal)/double(s.not_optimal):0.0)
           <<", 미확정 사례 "<<(s.unknown?double(s.htm_unknown)/double(s.unknown):0.0)<<'\n';
        if(decided)out<<"  판정된 큐브의 평균 최적 길이 "<<(double(s.optimal_length_sum)/double(decided))
                      <<", 평균 solve 길이 "<<(double(s.htm_optimal+s.htm_not_optimal)/double(decided))
                      <<" (평균 gap "<<((double(s.htm_optimal+s.htm_not_optimal)-double(s.optimal_length_sum))/double(decided))<<")\n";
        out<<"  검증 소요 시간 합계 "<<s.verify_seconds<<" 초\n";
    }
    out<<"\n[rephase로 수를 줄인 사례] "<<s.cases.size()<<"건\n";
    if(s.cases.empty())out<<"  없음\n";
    for(const auto& c:s.cases) {
        out<<"  큐브 "<<(c.index+1);if(c.seed)out<<" (seed "<<*c.seed<<')';
        if(c.window)out<<" window "<<c.window;
        out<<": "<<c.before<<"수 -> "<<c.after<<"수 (-"<<(c.before-c.after)<<")\n"
                 <<"    셔플:   "<<c.scramble<<"\n    stemil: "<<c.initial<<"\n    rephase: "<<c.final_sequence<<'\n';
    }
    out<<std::flush;
}
std::string quote(const std::string& text) {
    std::string result="\"";
    for(unsigned char c:text) {
        if(c=='\"' || c=='\\') { result+='\\';result+=c; }
        else if(c=='\n')result+="\\n";
        else if(c=='\r')result+="\\r";
        else if(c=='\t')result+="\\t";
        else if(c<32) { std::ostringstream out;out<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c);result+=out.str(); }
        else result+=c;
    }
    return result+'\"';
}

long rss_kib(){rusage u{};getrusage(RUSAGE_SELF,&u);return u.ru_maxrss;}
double ms(Deadline a,Deadline b){return std::chrono::duration<double,std::milli>(b-a).count();}
int integer(const std::string& text,const std::string& name) {
    int value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw std::invalid_argument("invalid integer for "+name);
    return value;
}
uint32_t seed_number(const std::string& text) {
    uint32_t value=0;auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
    if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())throw std::invalid_argument("seed must be an integer in 0..4294967295");
    return value;
}
double number(const std::string& text,const std::string& name) {
    size_t used=0;double value=std::stod(text,&used);
    if(used!=text.size() || !std::isfinite(value))throw std::invalid_argument("invalid finite number for "+name);
    return value;
}
std::vector<double> number_list(const std::string& text,const std::string& name) {
    std::vector<double> values;size_t begin=0;
    while(true) {
        auto end=text.find(',',begin);
        values.push_back(number(text.substr(begin,end==std::string::npos?end:end-begin),name));
        if(end==std::string::npos)break;
        begin=end+1;
    }
    return values;
}
std::vector<int> integer_list(const std::string& text,const std::string& name) {
    std::vector<int> values;size_t begin=0;
    while(true) {
        auto end=text.find(',',begin);
        values.push_back(integer(text.substr(begin,end==std::string::npos?end:end-begin),name));
        if(end==std::string::npos)break;
        begin=end+1;
    }
    return values;
}
bool under(const std::filesystem::path& path,const std::filesystem::path& root) {
    auto p=path.begin();for(auto r=root.begin();r!=root.end();++r,++p)if(p==path.end() || *p!=*r)return false;
    return true;
}
class TraceWriter {
    bool enabled_;FILE* file_=nullptr;Deadline started_=Clock::now();
public:
    TraceWriter(bool enabled,const std::filesystem::path& path,const std::filesystem::path& resources):enabled_(enabled) {
        if(path.empty())return;
        auto destination=std::filesystem::weakly_canonical(std::filesystem::absolute(path));
        for(const auto& protected_root:{std::filesystem::path("../third_party/stemil"),std::filesystem::absolute(resources).parent_path()}) {
            auto root=std::filesystem::weakly_canonical(std::filesystem::absolute(protected_root));
            if(under(destination,root))throw std::invalid_argument("trace output cannot write into the existing stemil directory");
        }
        if(!destination.parent_path().empty())std::filesystem::create_directories(destination.parent_path());
        int descriptor=open(destination.c_str(),O_WRONLY|O_CREAT|O_EXCL,0644);
        if(descriptor<0)throw std::runtime_error("unable to create new trace file (existing files are preserved): "+destination.string()+": "+std::strerror(errno));
        file_=fdopen(descriptor,"w");if(!file_){close(descriptor);throw std::runtime_error("unable to open trace stream");}
    }
    ~TraceWriter(){if(file_)std::fclose(file_);}
    void write(const TraceEvent& e,int index) {
        if(!enabled_)return;
        std::ostringstream out;out<<std::fixed<<std::setprecision(3)<<"{\"event\":"<<quote(e.event)<<",\"stage\":"<<quote(e.stage)<<",\"index\":"<<index<<",\"elapsed_ms\":"<<ms(started_,Clock::now());
        if(e.budget_ms>=0)out<<",\"budget_ms\":"<<e.budget_ms;
        if(e.begin>=0)out<<",\"begin\":"<<e.begin;
        if(e.end>=0)out<<",\"end\":"<<e.end;
        if(e.before>=0)out<<",\"before\":"<<e.before;
        if(e.after>=0)out<<",\"after\":"<<e.after;
        if(!e.message.empty())out<<",\"message\":"<<quote(e.message);
        if(!e.sequence.empty())out<<",\"sequence\":"<<quote(e.sequence);
        if(e.event=="repair_applied")out<<",\"replacement\":"<<quote(e.replacement);
        out<<",\"nodes\":"<<e.nodes<<",\"complete\":"<<(e.complete?"true":"false")<<",\"timed_out\":"<<(e.timed_out?"true":"false")<<",\"cached\":"<<(e.cached?"true":"false")<<"}\n";
        auto line=out.str();
        if(file_){if(std::fwrite(line.data(),1,line.size(),file_)!=line.size() || std::fflush(file_))throw std::runtime_error("unable to write trace file");}
        else {std::cerr<<line<<std::flush;if(!std::cerr)throw std::runtime_error("unable to write trace to stderr");}
    }
};
void stage_stats(const RepairStageStats& s) {
    std::cout<<"{\"windows_tested\":"<<s.windows_tested<<",\"windows_completed\":"<<s.windows_completed<<",\"windows_timed_out\":"<<s.windows_timed_out<<",\"cache_hits\":"<<s.cache_hits<<",\"nodes\":"<<s.nodes<<",\"improvements\":"<<s.improvements<<",\"moves_saved\":"<<s.moves_saved<<",\"elapsed_ms\":"<<s.elapsed_ms<<'}';
}
void stats(const RepairStats& s) {
    std::cout<<"\"repair_nodes\":"<<s.nodes<<",\"windows_tested\":"<<s.windows_tested<<",\"windows_completed\":"<<s.windows_completed<<",\"windows_timed_out\":"<<s.windows_timed_out<<",\"repair_deadline_reached\":"<<(s.deadline_reached?"true":"false")<<",\"repairs\":[";
    bool first=true;for(auto e:s.events){if(!first)std::cout<<',';first=false;std::cout<<"{\"stage\":"<<quote(repair_stage_name(e.stage))<<",\"begin\":"<<e.begin<<",\"end\":"<<e.end<<",\"old_length\":"<<e.old_length<<",\"new_length\":"<<e.new_length<<'}';}
    std::cout<<"],\"boundary_repair\":";stage_stats(s.boundary);std::cout<<",\"sliding_repair\":";stage_stats(s.sliding);
}
void settings(const RepairConfig& c) {
    std::cout<<"\"repair_mode\":"<<quote(repair_mode_name(c.mode))<<",\"windows\":[";
    for(size_t i=0;i<c.windows.size();++i){if(i)std::cout<<',';std::cout<<c.windows[i];}
    std::cout<<"],\"window_slice_ms\":"<<c.window_slice_ms<<",\"boundary_fraction\":"<<c.boundary_fraction;
}
void multi_fields(const SolveResult& r) {
    std::cout<<"\"orientations\":"<<r.orientations<<",\"winning_orientation\":"<<r.winning_orientation<<",\"single_orientation_htm\":"<<r.single_orientation_htm<<",\"orientation_htm\":[";
    for(size_t i=0;i<r.orientation_reports.size();++i)std::cout<<(i?",":"")<<(r.orientation_reports[i].solved?std::to_string(r.orientation_reports[i].htm):"null");
    std::cout<<"],\"candidate_htm\":[";
    for(size_t i=0;i<r.candidate_reports.size();++i)std::cout<<(i?",":"")<<"{\"orientation\":"<<r.candidate_reports[i].orientation<<",\"before\":"<<r.candidate_reports[i].before<<",\"after\":"<<r.candidate_reports[i].after<<'}';
    std::cout<<"],\"repaired_candidate\":"<<r.repaired_candidate<<",\"initial_found_ms\":"<<r.initial_found_ms<<",\"final_found_ms\":"<<r.final_found_ms<<",\"rephase_found_ms\":"<<r.rephase_found_ms<<',';
}
double rephase_search_ms(const SolveResult& result) {
    return result.repair_stats.boundary.elapsed_ms+result.repair_stats.sliding.elapsed_ms;
}
double rephase_search_ms(const RepairResult& result) {
    return result.stats.boundary.elapsed_ms+result.stats.sliding.elapsed_ms;
}
struct RephasePlan { int window=0;double minimum_ms=0,maximum_ms=0; };
struct RephaseRun { RephasePlan plan;RepairResult result;double elapsed_ms=0; };
struct StemilRun { double maximum_ms=0;SolveResult result; };
std::string shown_algorithm(const Algorithm& moves,bool solved) {
    if(!solved)return "(해를 찾지 못함)";
    auto text=format_algorithm(moves);return text.empty()?"(없음)":text;
}
Algorithm phase_moves(const SolveResult& result,bool phase1) {
    if(!result.solved)return {};
    auto middle=result.initial_moves.begin()+result.initial_boundary;
    return phase1?Algorithm(result.initial_moves.begin(),middle):Algorithm(middle,result.initial_moves.end());
}
void human_input_header(const std::optional<std::string>& scramble,size_t index,size_t total) {
    if(total>1)std::cout<<"[큐브 "<<(index+1)<<'/'<<total<<"]\n";
    else if(total==0)std::cout<<"[큐브 "<<(index+1)<<"]\n";
    std::string shuffle=scramble && !scramble->empty()?*scramble:"(없음)";
    std::cout<<"셔플 move\n  "<<shuffle<<'\n'
             <<"셔플 move 수: "<<(scramble?parse_algorithm(*scramble).size():0)<<"수\n"<<std::flush;
}
void human_stemil_run(const SolveResult& result,double maximum_ms,size_t run_index,size_t run_total,
                      long stemil_peak_rss_kib,size_t phase1_bytes,size_t phase2_bytes,bool heading=true) {
    auto p1=phase_moves(result,true),p2=phase_moves(result,false);
    if(heading)std::cout<<"\n[stemil "<<(run_index+1)<<'/'<<run_total<<"]\n";
    std::cout<<std::fixed<<std::setprecision(3)
             <<"stemil 최대 탐색 시간: "<<maximum_ms<<" ms\n"
             <<"stemil phase 1 move\n  "<<shown_algorithm(p1,result.solved)<<'\n'
             <<"stemil phase 1 move 수: "<<p1.size()<<"수\n"
             <<"stemil phase 1 탐색 시간: "<<result.phase1_ms<<" ms\n"
             <<std::setprecision(2)
             <<"stemil phase 1 메모리: "<<(double(phase1_bytes)/(1024.0*1024.0))<<" MiB\n"
             <<std::setprecision(3)
             <<"stemil phase 2 move\n  "<<shown_algorithm(p2,result.solved)<<'\n'
             <<"stemil phase 2 move 수: "<<p2.size()<<"수\n"
             <<"stemil phase 2 탐색 시간: "<<result.phase2_ms<<" ms\n"
             <<std::setprecision(2)
             <<"stemil phase 2 메모리: "<<(double(phase2_bytes)/(1024.0*1024.0))<<" MiB\n"
             <<std::setprecision(3)
             <<"stemil 전체 solve move\n  "<<shown_algorithm(result.initial_moves,result.solved)<<'\n'
             <<"stemil 전체 solve move 수: "<<(result.solved?result.initial_moves.size():0)<<"수\n"
             <<"stemil 전체 solve 시간: "<<result.two_phase_ms<<" ms\n"
             <<std::setprecision(2)
             <<"stemil 전체 solve 메모리: "<<(double(stemil_peak_rss_kib)/1024.0)<<" MiB\n"<<std::flush;
}
void human_rephase_run(const SolveResult& initial,const RephaseRun& run,size_t run_index,size_t run_total,size_t rephase_peak_bytes) {
    size_t saved=initial.solved?initial.initial_moves.size()-run.result.moves.size():0;
    std::cout<<"\n[rephase "<<(run_index+1)<<'/'<<run_total<<"]\n"
             <<"window: "<<run.plan.window<<"\n"
             <<std::fixed<<std::setprecision(3)
             <<"최소 탐색 배정 시간: "<<run.plan.minimum_ms<<" ms\n"
             <<"최대 탐색 시간: "<<run.plan.maximum_ms<<" ms\n"
             <<"rephase solve move 수: "<<(initial.solved?run.result.moves.size():0)<<"수\n"
             <<"rephase로 수를 줄였는지: "<<(saved?"예":"아니오")<<'\n'
             <<"rephase로 줄인 수: "<<saved<<"수\n"
             <<"rephase 탐색 시간: "<<rephase_search_ms(run.result)<<" ms\n"
             <<std::setprecision(2)
             <<"rephase solve 메모리: "<<(double(rephase_peak_bytes)/(1024.0*1024.0))<<" MiB\n"<<std::flush;
}
void human_result(const SolveResult& result,const std::optional<std::string>& scramble,
                  long stemil_peak_rss_kib,size_t phase1_bytes,size_t phase2_bytes,size_t rephase_peak_bytes,
                  size_t index,size_t total) {
    human_input_header(scramble,index,total);
    human_stemil_run(result,result.two_phase_budget_ms,0,1,stemil_peak_rss_kib,phase1_bytes,phase2_bytes,false);
    size_t saved=result.solved?result.initial_moves.size()-result.moves.size():0;
    if(result.orientations>1 || !result.candidate_reports.empty()) {
        std::cout<<"6방향 탐색: "<<result.orientations<<"방향, 방향별 move 수:";
        for(const auto& o:result.orientation_reports){std::cout<<' '<<o.index<<(o.inverse?"i":"")<<(o.mirror?"m":"")<<"(rot"<<o.rotation<<")=";if(o.solved)std::cout<<o.htm;else std::cout<<'-';}
        std::cout<<"\n방향 0 단독 move 수: "<<result.single_orientation_htm<<"수, 6방향 최선: "<<(result.solved?result.initial_moves.size():0)<<"수, 선택된 방향: "<<result.winning_orientation<<'\n';
        if(!result.candidate_reports.empty()) {
            std::cout<<"후보 rephase: "<<result.candidate_reports.size()<<"개, 후보별(방향 전->후):";
            for(const auto& c:result.candidate_reports)std::cout<<" o"<<c.orientation<<' '<<c.before<<"->"<<c.after;
            std::cout<<", 선택된 후보: "<<result.repaired_candidate<<'\n';
        }
    }
    std::cout<<std::fixed<<std::setprecision(3)
             <<"rephase solve move 수: "<<(result.solved?result.moves.size():0)<<"수\n"
             <<"rephase로 수를 줄였는지: "<<(saved?"예":"아니오")<<'\n'
             <<"rephase로 줄인 수: "<<saved<<"수\n"
             <<"rephase 탐색 시간: "<<rephase_search_ms(result)<<" ms\n"
             <<std::setprecision(2)
             <<"rephase solve 메모리: "<<(double(rephase_peak_bytes)/(1024.0*1024.0))<<" MiB\n";
    if(total!=1)std::cout<<'\n';
    std::cout<<std::flush;
}
void help() {
    std::cout<<R"(stemil-rephase — existing stemil Two-Phase + boundary and sliding repair (HTM)

Commands:
  [solve]                   automatically shuffle and solve (also the no-argument default)
  solve --scramble "R U F2" | --facelets <54 URFDLB characters>
  batch                     one URFDLB facelet string per stdin line
  repair --sequence "R U R' ..." --boundary <move index>
  verify --facelets <54 characters> --sequence "solution"

Automatic shuffle:
  --scramble-length 25       random move count (1..1000)
  --seed N                   reproducible shuffle seed (0..4294967295); omitted: new seed
  --runs 1                   automatic cubes per process (1..10000), tables loaded once
                             cube i uses seed + i, modulo 2^32
                             0 repeats until Ctrl+C; the current cube finishes first
                             runs != 1 prints a summary to stderr at the end

6-way search (this directory):
  --orientations 1           search 1..12 equivalent orientations in parallel threads
                             (URF rotation x inverse cube, 7..12 add the LR mirror);
                             the shortest solution wins
  --candidates 1             repair up to N distinct best candidates in parallel threads
                             (across orientations); the shortest repaired one wins
                             both need CPU cores: threads = orientations, then candidates
                             1/1 reproduces the original single-thread behavior
  --phase1-mode prefix       improvement search: prefix = stemil prefixes with one greedy
                             Phase-1 path each; ida = every Phase-1 solution by length

Time and rephase:
  --budget-ms 1000           total warmed solve time; setup is separate
  --stemil-max-ms A,B,C      independently run stemil with each search maximum
  --rephase-ms N             reserve N ms for one combined rephase run
  --rephase-max-ms A,B,C     maximum list used in every window/time combination
  --rephase-min-ms X,Y       minimum list used in every window/time combination
  --repair-fraction 0.1      reserve a fraction if --rephase-ms is not set
  --adaptive                 adjust the fraction from first solution length
                             fixed --rephase-ms conflicts with fraction/adaptive options
  --windows 4,6,8,10         chosen HTM lengths (2..10); multi mode keeps input order
  --window 8                 choose one length instead of --windows
  --window-slice-ms 8        maximum time per exact window attempt
  --repair-mode boundary+sliding | boundary | sliding
  --boundary-fraction 0.5    maximum boundary share when using both stages
                             sliding uses the remaining rephase time
  --no-repeat                stop after the first successful replacement
  --table-depth 4            short exact transformation DB radius (0..5)
  --repair-memory-mib 64     cap DB plus construction queue allocations

Trace and resources:
  --json                     detailed machine-readable JSON result
  --trace                    JSONL progress to stderr; concise result stays on stdout
  --trace-file PATH          enable trace to a NEW file, preserving existing files
  --verify-optimal           after each solve, try to prove the found length optimal (or find
                             a shorter solution) with the IDA* oracle; records the optimal
                             ratio and average solve length (stderr summary + --stats-file).
                             Needs +20-30 s setup; hard for 18+ moves (see --verify-cap-sec)
  --verify-cap-sec 60        per-cube proof time limit; on timeout the result is undecided
  --verify-threads N         threads for one proof (default: all cores)
  --stats-file stats.txt     append per-cube verification rows and the final summary here
  --output-file output.txt   append every rephase-shortened case here as it is found
                             (file is created only when a case exists)
  --resources ../third_party/stemil/resources

Examples:
  ./build/stemil-rephase
  ./build/stemil-rephase solve --stemil-max-ms 500,1000,2000
  ./build/stemil-rephase solve --windows 4,6,8 --rephase-ms 100 --trace
  ./build/stemil-rephase solve --stemil-max-ms 500,1000 --windows 6,8 --rephase-max-ms 100,200
  ./build/stemil-rephase solve --windows 6,7,8 --rephase-max-ms 100,200,300 --rephase-min-ms 10
  ./build/stemil-rephase solve --seed 2026 --runs 3 --window 8 --rephase-ms 200
  ./build/stemil-rephase solve --runs 0 --rephase-ms 100
  ./build/stemil-rephase solve --runs 0 --orientations 6 --candidates 6 --rephase-ms 100
  ./build/stemil-rephase repair --sequence "R R' U F L B D" --boundary 7 --rephase-ms 100

Build only this upgrade executable with make. Existing stemil sources and all
four pruning tables are reused read-only, without regeneration or conversion.
Trace elapsed_ms starts at program launch; result elapsed_ms is per warmed cube.
Repair alone uses --rephase-ms or, if omitted, --budget-ms as its repair cap.
Multi-rephase gives --budget-ms to stemil, then applies every window independently
to that same stemil solution. A minimum is an exact-attempt allocation, not sleep;
a completed shortest-path proof may therefore finish sooner.
Multi-stemil runs every time condition independently on the same input cube. Only
the largest stemil time result is rephased. Rephase runs windows x maxima x minima.
Stemil and rephase results are flushed as soon as each run completes.
Timeout is not a negative proof. Final solutions retain full-transform validation.
)";
}
}
int main(int argc,char** argv) {
    try {
        if(argc>1 && (std::string(argv[1])=="--help" || std::string(argv[1])=="help")){help();return 0;}
        bool implicit=argc==1 || argv[1][0]=='-';std::string command=implicit?"solve":argv[1];
        std::string scramble,facelets,sequence;bool has_scramble=false,has_facelets=false,has_sequence=false;
        bool has_seed=false,shuffle_length_set=false,runs_set=false,fraction_set=false,windows_set=false,trace_enabled=false,json_output=false;
        bool budget_set=false,rephase_ms_set=false,rephase_max_set=false,rephase_min_set=false,stemil_max_set=false;
        uint32_t seed=0;int scramble_length=25,runs=1,boundary=-1;SolveConfig solve_config;RepairConfig repair_config;
        std::vector<int> requested_windows=repair_config.windows;
        std::vector<double> rephase_max_values,rephase_min_values,stemil_max_values;
        std::filesystem::path resources="../third_party/stemil/resources",trace_file,output_file="output.txt",stats_file="stats.txt";
        bool verify_optimal=false;double verify_cap_sec=60;int verify_threads=int(std::max(1u,std::thread::hardware_concurrency()));
        for(int i=implicit?1:2;i<argc;++i) {
            std::string option=argv[i];
            if(option=="--help"){help();return 0;}
            if(option=="--trace"){trace_enabled=true;continue;}
            if(option=="--json"){json_output=true;continue;}
            if(option=="--verify-optimal"){verify_optimal=true;continue;}
            if(option=="--adaptive"){solve_config.adaptive=true;continue;}
            if(option=="--no-repeat"){repair_config.repeat=false;continue;}
            if(i+1>=argc)throw std::invalid_argument("missing value for "+option);
            std::string value=argv[++i];
            if(option=="--scramble"){scramble=value;has_scramble=true;}
            else if(option=="--facelets"){facelets=value;has_facelets=true;}
            else if(option=="--sequence"){sequence=value;has_sequence=true;}
            else if(option=="--boundary")boundary=integer(value,option);
            else if(option=="--budget-ms"){solve_config.budget_ms=number(value,option);budget_set=true;}
            else if(option=="--stemil-max-ms" || option=="--stemil-times-ms"){stemil_max_values=number_list(value,option);stemil_max_set=true;}
            else if(option=="--rephase-ms"){solve_config.rephase_ms=number(value,option);rephase_ms_set=true;}
            else if(option=="--rephase-max-ms"){rephase_max_values=number_list(value,option);rephase_max_set=true;}
            else if(option=="--rephase-min-ms"){rephase_min_values=number_list(value,option);rephase_min_set=true;}
            else if(option=="--repair-fraction"){solve_config.repair_fraction=number(value,option);fraction_set=true;}
            else if(option=="--seed"){seed=seed_number(value);has_seed=true;}
            else if(option=="--scramble-length"){scramble_length=integer(value,option);shuffle_length_set=true;}
            else if(option=="--runs"){runs=integer(value,option);runs_set=true;}
            else if(option=="--orientations")solve_config.orientations=integer(value,option);
            else if(option=="--candidates")solve_config.candidates=integer(value,option);
            else if(option=="--phase1-mode") {
                if(value=="prefix")solve_config.phase1_ida=false;
                else if(value=="ida")solve_config.phase1_ida=true;
                else throw std::invalid_argument("--phase1-mode must be prefix or ida");
            }
            else if(option=="--table-depth")repair_config.table_depth=integer(value,option);
            else if(option=="--repair-memory-mib") {
                double mib=number(value,option);if(mib<=0 || mib>1024)throw std::invalid_argument("repair memory limit must be >0 and <=1024 MiB");repair_config.memory_limit_bytes=size_t(mib*1024*1024);
            }
            else if(option=="--window-slice-ms")repair_config.window_slice_ms=number(value,option);
            else if(option=="--boundary-fraction")repair_config.boundary_fraction=number(value,option);
            else if(option=="--resources")resources=value;
            else if(option=="--output-file")output_file=value;
            else if(option=="--stats-file")stats_file=value;
            else if(option=="--verify-cap-sec")verify_cap_sec=number(value,option);
            else if(option=="--verify-threads")verify_threads=integer(value,option);
            else if(option=="--trace-file"){trace_file=value;trace_enabled=true;}
            else if(option=="--repair-mode") {
                if(value=="boundary+sliding")repair_config.mode=RepairMode::BoundaryAndSliding;
                else if(value=="boundary")repair_config.mode=RepairMode::Boundary;
                else if(value=="sliding")repair_config.mode=RepairMode::Sliding;
                else throw std::invalid_argument("repair mode must be boundary+sliding, boundary, or sliding");
            }
            else if(option=="--windows" || option=="--window") {
                if(windows_set)throw std::invalid_argument("choose either --window or --windows once");
                windows_set=true;
                requested_windows=option=="--window"?std::vector<int>{integer(value,option)}:integer_list(value,option);
                repair_config.windows=requested_windows;
            }
            else throw std::invalid_argument("unknown option: "+option);
        }
        if(solve_config.budget_ms<=0 || solve_config.budget_ms>60000)throw std::invalid_argument("budget must be >0 and <=60000 ms");
        if(solve_config.repair_fraction<0 || solve_config.repair_fraction>=1)throw std::invalid_argument("repair fraction must be in [0,1)");
        if(solve_config.orientations<1 || solve_config.orientations>12)throw std::invalid_argument("--orientations must be 1..12");
        if(solve_config.candidates<1 || solve_config.candidates>64)throw std::invalid_argument("--candidates must be 1..64");
        if((solve_config.orientations>1 || solve_config.candidates>1) && solve_config.adaptive)throw std::invalid_argument("--adaptive cannot be combined with --orientations/--candidates");
        if(solve_config.rephase_ms && (fraction_set || solve_config.adaptive))throw std::invalid_argument("--rephase-ms cannot be combined with --repair-fraction or --adaptive");
        if(rephase_min_set && !rephase_max_set)throw std::invalid_argument("--rephase-min-ms requires --rephase-max-ms");
        if(rephase_max_set && (rephase_ms_set || fraction_set || solve_config.adaptive))throw std::invalid_argument("--rephase-max-ms cannot be combined with --rephase-ms, --repair-fraction, or --adaptive");
        if(verify_optimal && (command=="repair" || command=="verify" || rephase_max_set || stemil_max_set))throw std::invalid_argument("--verify-optimal applies to plain solve/batch runs, not repair, verify, --stemil-max-ms, or --rephase-max-ms");
        if(verify_cap_sec<=0 || verify_cap_sec>86400 || verify_threads<1 || verify_threads>256)throw std::invalid_argument("--verify-cap-sec must be >0 and <=86400, --verify-threads 1..256");
        if(stemil_max_set && (budget_set || rephase_ms_set || fraction_set || solve_config.adaptive))throw std::invalid_argument("--stemil-max-ms cannot be combined with --budget-ms, --rephase-ms, --repair-fraction, or --adaptive");
        for(double value:stemil_max_values)if(value<=0 || value>60000)throw std::invalid_argument("each stemil maximum must be >0 and <=60000 ms");
        if(scramble_length<1 || scramble_length>1000 || runs<0 || runs>10000)throw std::invalid_argument("scramble length must be 1..1000; runs must be 0..10000 (0 repeats until Ctrl+C)");
        validate_repair_config(repair_config);
        std::vector<RephasePlan> rephase_plans;
        if(rephase_max_set) {
            const std::vector<double> minimums=rephase_min_set?rephase_min_values:std::vector<double>{0};
            for(int window:requested_windows)for(double maximum:rephase_max_values)for(double minimum:minimums) {
                if(maximum<=0 || maximum>60000)throw std::invalid_argument("each rephase maximum must be >0 and <=60000 ms");
                if(minimum<0 || minimum>maximum)throw std::invalid_argument("each rephase minimum must be >=0 and <= its maximum");
                rephase_plans.push_back({window,minimum,maximum});
            }
        }
        std::sort(repair_config.windows.begin(),repair_config.windows.end());repair_config.windows.erase(std::unique(repair_config.windows.begin(),repair_config.windows.end()),repair_config.windows.end());
        bool automatic=command=="solve" && !has_scramble && !has_facelets;
        if(!automatic && (has_seed || shuffle_length_set || runs_set))throw std::invalid_argument("shuffle seed, length, and runs require automatic solve");
        if(command=="verify") {
            if(!has_facelets || !has_sequence)throw std::invalid_argument("verify requires --facelets and --sequence");
            auto cube=State::from_facelets(facelets);auto algorithm=parse_algorithm(sequence);bool valid=cube.applied(algorithm).solved();std::cout<<"{\"valid\":"<<(valid?"true":"false")<<",\"htm\":"<<algorithm.size()<<"}\n";return valid?0:1;
        }
        if(command!="repair" && command!="solve" && command!="batch")throw std::invalid_argument("unknown command: "+command);
        if(command=="repair" && (has_scramble || has_facelets || fraction_set || solve_config.adaptive || rephase_max_set || rephase_min_set || stemil_max_set))throw std::invalid_argument("repair takes sequence/boundary and one time cap, not cube inputs or multi-run options");
        if(command=="batch" && (has_scramble || has_facelets || has_sequence || boundary>=0))throw std::invalid_argument("batch reads cube inputs from stdin");
        if(command=="solve" && (has_sequence || boundary>=0 || (has_scramble && has_facelets)))throw std::invalid_argument("solve takes either scramble or facelets, or automatic shuffle");
        double requested_rephase=solve_config.rephase_ms.value_or(solve_config.budget_ms*solve_config.repair_fraction);
        if(!rephase_max_set && !stemil_max_set && command!="repair" && (requested_rephase<0 || requested_rephase>=solve_config.budget_ms))throw std::invalid_argument("rephase time must be >=0 and less than total budget");
        Algorithm input_algorithm;State cube;std::optional<uint32_t> cube_seed;
        if(command=="solve") {
            if(automatic){if(!has_seed)seed=new_shuffle_seed();cube_seed=seed;input_algorithm=random_scramble(scramble_length,seed);scramble=format_algorithm(input_algorithm);cube=State::identity().applied(input_algorithm);}
            else if(has_facelets)cube=State::from_facelets(facelets);
            else {input_algorithm=parse_algorithm(scramble);cube=State::identity().applied(input_algorithm);}
        }
        Algorithm repair_input;
        if(command=="repair"){if(!has_sequence || boundary<0)throw std::invalid_argument("repair requires --sequence and --boundary");repair_input=parse_algorithm(sequence);if(size_t(boundary)>repair_input.size())throw std::invalid_argument("boundary outside sequence");}
        TraceWriter writer(trace_enabled,trace_file,resources);int current_index=-1;
        TraceCallback trace;if(trace_enabled)trace=[&](const TraceEvent& e){writer.write(e,current_index);};solve_config.trace=trace;
        if(automatic && trace_enabled){TraceEvent e;e.event="shuffle";e.stage="input";e.sequence=scramble;e.message="seed="+std::to_string(seed)+" length="+std::to_string(scramble_length);writer.write(e,0);}
        if(command=="repair") {
            double cap=solve_config.rephase_ms.value_or(solve_config.budget_ms);if(cap<0 || cap>60000)throw std::invalid_argument("repair cap must be 0..60000 ms");
            auto setup=Clock::now();Repair engine(repair_config);auto start=Clock::now();current_index=0;
            auto duration=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(cap));auto result=engine.optimize(repair_input,boundary,start+duration,trace);auto finish=Clock::now();
            std::cout<<std::fixed<<std::setprecision(3)<<"{\"equivalent\":"<<(State::identity().applied(repair_input)==State::identity().applied(result.moves)?"true":"false")<<",\"initial_sequence\":"<<quote(sequence)<<",\"sequence\":"<<quote(format_algorithm(result.moves))<<",\"initial_htm\":"<<repair_input.size()<<",\"htm\":"<<result.moves.size()<<",\"initial_boundary\":"<<boundary<<",\"final_focal_index\":"<<result.boundary<<",\"allocated_rephase_ms\":"<<cap<<",\"elapsed_ms\":"<<ms(start,finish)<<",\"setup_ms\":"<<ms(setup,start)<<",\"repair_table_bytes\":"<<engine.table_bytes()<<",\"repair_build_peak_bytes\":"<<engine.build_peak_bytes()<<",\"repair_table_states\":"<<engine.states()<<",\"peak_rss_kib\":"<<rss_kib()<<',';
            settings(repair_config);std::cout<<',';stats(result.stats);std::cout<<"}\n";return 0;
        }
        if(trace){TraceEvent e;e.event="setup_start";e.stage="setup";e.message=resources.string();trace(e);}
        auto setup=Clock::now();Tables tables(resources);long stemil_peak_rss_kib=rss_kib();
        size_t phase1_bytes=tables.phase1_bytes(),phase2_bytes=tables.phase2_bytes();
        std::unique_ptr<Repair> repair;if(rephase_max_set || (!stemil_max_set && requested_rephase>0))repair=std::make_unique<Repair>(repair_config);
        size_t rephase_peak_bytes=repair?repair->build_peak_bytes():0;double setup_ms=ms(setup,Clock::now());
        Solver solver(tables,(rephase_max_set || stemil_max_set)?nullptr:repair.get());
        std::unique_ptr<CornerPdb> corner_pdb;std::unique_ptr<OptimalSolver> verifier;
        if(verify_optimal) {
            std::cerr<<"최적해 검증용 corner database 구성 중 (약 20~30초)...\n";
            corner_pdb=std::make_unique<CornerPdb>();verifier=std::make_unique<OptimalSolver>(tables,*corner_pdb);
        }
        std::ofstream stats_out;
        auto stats_line=[&](const std::string& text){
            if(!verify_optimal)return;
            if(!stats_out.is_open())stats_out.open(stats_file,std::ios::app);
            if(stats_out){stats_out<<text;stats_out.flush();}
        };
        if(verify_optimal) {
            char stamp[32];std::time_t now=std::time(nullptr);std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S",std::localtime(&now));
            std::ostringstream header;header<<"\n===== "<<stamp<<" 실행 시작: budget "<<solve_config.budget_ms<<" ms, rephase "<<requested_rephase<<" ms, orientations "<<solve_config.orientations<<", candidates "<<solve_config.candidates<<", phase1-mode "<<(solve_config.phase1_ida?"ida":"prefix")<<", 검증 상한 "<<verify_cap_sec<<" 초 =====\n";
            stats_line(header.str());
        }
        SolveConfig initial_config=solve_config;
        if(rephase_max_set || stemil_max_set){initial_config.rephase_ms=0;initial_config.repair_fraction=0;initial_config.adaptive=false;}
        if(trace){TraceEvent e;e.event="setup_end";e.stage="setup";e.budget_ms=setup_ms;trace(e);}
        auto apply_rephases=[&](const State& state,const SolveResult& initial,int stemil_run_index,
                                const std::function<void(const RephaseRun&,size_t,size_t)>& emit) {
            std::vector<RephaseRun> multi_runs;multi_runs.reserve(rephase_plans.size());
            for(const auto& plan:rephase_plans) {
                RephaseRun run;run.plan=plan;run.result.moves=initial.initial_moves;run.result.boundary=initial.initial_boundary;
                if(initial.solved) {
                    RepairConfig search_config=repair_config;search_config.windows={plan.window};
                    search_config.window_slice_ms=std::max(search_config.window_slice_ms,plan.minimum_ms);
                    std::string context="window="+std::to_string(plan.window)+" minimum_ms="+std::to_string(plan.minimum_ms);
                    if(stemil_run_index>=0)context+=" stemil_run="+std::to_string(stemil_run_index);
                    if(trace){TraceEvent e;e.event="rephase_run_start";e.stage="rephase";e.budget_ms=plan.maximum_ms;e.message=context;trace(e);}
                    auto start=Clock::now();auto duration=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(plan.maximum_ms));
                    run.result=repair->optimize_with(initial.initial_moves,initial.initial_boundary,search_config,start+duration,trace);run.elapsed_ms=ms(start,Clock::now());
                    if(run.result.moves.size()>initial.initial_moves.size() || !state.applied(run.result.moves).solved())throw std::logic_error("multi-rephase solution failed verification");
                    if(trace){TraceEvent e;e.event="rephase_run_end";e.stage="rephase";e.before=int(initial.initial_moves.size());e.after=int(run.result.moves.size());e.budget_ms=run.elapsed_ms;e.message=context;e.sequence=format_algorithm(run.result.moves);e.complete=!run.result.stats.deadline_reached;e.timed_out=run.result.stats.deadline_reached;trace(e);}
                }
                if(emit)emit(run,multi_runs.size(),rephase_plans.size());
                multi_runs.push_back(std::move(run));
            }
            return multi_runs;
        };
        RunSummary summary;
        auto append_output=[&](const ShortenedCase& c) {
            std::ofstream file(output_file,std::ios::app);
            if(!file){std::cerr<<"warning: cannot open "<<output_file.string()<<" for appending\n";return;}
            char stamp[32];std::time_t now=std::time(nullptr);std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M:%S",std::localtime(&now));
            file<<"["<<stamp<<"] 큐브 "<<(c.index+1);if(c.seed)file<<" (seed "<<*c.seed<<')';
            file<<": "<<c.before<<"수 -> "<<c.after<<"수 (-"<<(c.before-c.after)<<")";if(c.window)file<<", window "<<c.window;
            file<<"\n  셔플:    "<<c.scramble<<"\n  stemil:  "<<c.initial<<"\n  rephase: "<<c.final_sequence
                <<"\n  설정: budget "<<solve_config.budget_ms<<" ms, windows ";
            for(size_t i=0;i<requested_windows.size();++i)file<<(i?",":"")<<requested_windows[i];
            file<<"\n\n";file.flush();
        };
        auto record_shortened=[&](size_t index,const std::optional<std::string>& scramble_text,std::optional<uint32_t> input_seed,
                                  const Algorithm& before,const Algorithm& after,int window) {
            if(after.size()>=before.size())return;
            summary.cases.push_back({index,input_seed,scramble_text.value_or(""),format_algorithm(before),format_algorithm(after),before.size(),after.size(),window});
            append_output(summary.cases.back());
        };
        auto output=[&](const State& state,size_t index,const std::optional<std::string>& input_scramble,std::optional<uint32_t> input_seed) {
            current_index=int(index);
            size_t total=command=="batch"?0:(automatic?size_t(runs):1); // 0: open-ended
            auto json_stemil=[&](const SolveResult& result,double maximum_ms,size_t stemil_index,size_t stemil_total,bool selected_for_rephase) {
                auto p1=phase_moves(result,true),p2=phase_moves(result,false);
                std::cout<<std::fixed<<std::setprecision(3)<<"{\"result_type\":\"stemil\",\"index\":"<<index<<",\"stemil_index\":"<<stemil_index<<",\"stemil_total\":"<<stemil_total<<",\"selected_for_rephase\":"<<(selected_for_rephase?"true":"false")<<",\"input_mode\":"<<quote(input_seed?"automatic":input_scramble?"scramble":"facelets")<<",\"seed\":"<<(input_seed?std::to_string(*input_seed):"null")<<",\"scramble\":"<<(input_scramble?quote(*input_scramble):"null")<<",\"scramble_length\":"<<(input_scramble?std::to_string(parse_algorithm(*input_scramble).size()):"null")<<",\"facelets\":"<<quote(state.facelets())<<",\"solved\":"<<(result.solved?"true":"false")<<",\"tables\":\"expanded\",\"stemil_resources\":"<<quote(tables.resources().string())<<",\"maximum_ms\":"<<maximum_ms<<",\"phase1_sequence\":"<<quote(format_algorithm(p1))<<",\"phase1_htm\":"<<(result.solved?std::to_string(p1.size()):"null")<<",\"phase1_ms\":"<<result.phase1_ms<<",\"phase1_memory_bytes\":"<<phase1_bytes<<",\"phase2_sequence\":"<<quote(format_algorithm(p2))<<",\"phase2_htm\":"<<(result.solved?std::to_string(p2.size()):"null")<<",\"phase2_ms\":"<<result.phase2_ms<<",\"phase2_memory_bytes\":"<<phase2_bytes<<",\"sequence\":"<<quote(format_algorithm(result.initial_moves))<<",\"initial_sequence\":"<<quote(format_algorithm(result.initial_moves))<<",\"htm\":"<<(result.solved?std::to_string(result.initial_moves.size()):"null")<<",\"initial_htm\":"<<(result.solved?std::to_string(result.initial_moves.size()):"null")<<",\"solve_ms\":"<<result.two_phase_ms<<",\"solve_peak_rss_kib\":"<<stemil_peak_rss_kib<<",\"search_nodes\":"<<result.search_nodes<<",\"candidates\":"<<result.candidates<<",\"timed_out\":"<<(result.two_phase_timed_out?"true":"false")<<",\"setup_ms\":"<<setup_ms<<"}\n"<<std::flush;
            };
            auto json_rephase=[&](const SolveResult& initial,const RephaseRun& run,size_t run_index,size_t run_total,size_t stemil_index,double stemil_maximum_ms) {
                size_t saved=initial.solved?initial.initial_moves.size()-run.result.moves.size():0;
                std::cout<<std::fixed<<std::setprecision(3)<<"{\"result_type\":\"rephase\",\"index\":"<<index<<",\"rephase_index\":"<<run_index<<",\"rephase_total\":"<<run_total<<",\"base_stemil_index\":"<<stemil_index<<",\"base_stemil_maximum_ms\":"<<stemil_maximum_ms<<",\"input_mode\":"<<quote(input_seed?"automatic":input_scramble?"scramble":"facelets")<<",\"seed\":"<<(input_seed?std::to_string(*input_seed):"null")<<",\"scramble\":"<<(input_scramble?quote(*input_scramble):"null")<<",\"facelets\":"<<quote(state.facelets())<<",\"solved\":"<<(initial.solved?"true":"false")<<",\"tables\":\"expanded\",\"window\":"<<run.plan.window<<",\"minimum_ms\":"<<run.plan.minimum_ms<<",\"maximum_ms\":"<<run.plan.maximum_ms<<",\"effective_window_slice_ms\":"<<std::max(repair_config.window_slice_ms,run.plan.minimum_ms)<<",\"initial_sequence\":"<<quote(format_algorithm(initial.initial_moves))<<",\"initial_htm\":"<<(initial.solved?std::to_string(initial.initial_moves.size()):"null")<<",\"sequence\":"<<quote(format_algorithm(run.result.moves))<<",\"htm\":"<<(initial.solved?std::to_string(run.result.moves.size()):"null")<<",\"shortened\":"<<(saved?"true":"false")<<",\"moves_saved\":"<<saved<<",\"elapsed_ms\":"<<run.elapsed_ms<<",\"search_ms\":"<<rephase_search_ms(run.result)<<",\"repair_build_peak_bytes\":"<<rephase_peak_bytes<<',';stats(run.result.stats);std::cout<<"}\n"<<std::flush;
            };
            if(stemil_max_set) {
                std::vector<StemilRun> stemil_runs;stemil_runs.reserve(stemil_max_values.size());bool all_solved=true;
                size_t base_index=size_t(std::max_element(stemil_max_values.begin(),stemil_max_values.end())-stemil_max_values.begin());
                if(!json_output)human_input_header(input_scramble,index,total);
                for(size_t i=0;i<stemil_max_values.size();++i) {
                    SolveConfig config=initial_config;config.budget_ms=stemil_max_values[i];
                    if(trace){TraceEvent e;e.event="stemil_run_start";e.stage="two_phase";e.budget_ms=config.budget_ms;e.message="stemil_run="+std::to_string(i)+" maximum_ms="+std::to_string(config.budget_ms);trace(e);}
                    StemilRun run;run.maximum_ms=config.budget_ms;run.result=solver.solve(state,config);
                    if(trace){TraceEvent e;e.event="stemil_run_end";e.stage="two_phase";e.budget_ms=run.result.two_phase_ms;e.after=run.result.solved?int(run.result.initial_moves.size()):-1;e.message="stemil_run="+std::to_string(i);e.sequence=format_algorithm(run.result.initial_moves);e.complete=run.result.solved;e.timed_out=run.result.two_phase_timed_out;trace(e);}
                    if(json_output)json_stemil(run.result,run.maximum_ms,i,stemil_max_values.size(),i==base_index);
                    else human_stemil_run(run.result,run.maximum_ms,i,stemil_max_values.size(),stemil_peak_rss_kib,phase1_bytes,phase2_bytes);
                    all_solved=run.result.solved && all_solved;stemil_runs.push_back(std::move(run));
                }
                const auto& base=stemil_runs[base_index];
                if(rephase_max_set) {
                    auto emit=[&](const RephaseRun& run,size_t run_index,size_t run_total) {
                        if(base.result.solved)record_shortened(index,input_scramble,input_seed,base.result.initial_moves,run.result.moves,run.plan.window);
                        if(json_output)json_rephase(base.result,run,run_index,run_total,base_index,base.maximum_ms);
                        else human_rephase_run(base.result,run,run_index,run_total,rephase_peak_bytes);
                    };
                    apply_rephases(state,base.result,int(base_index),emit);
                }
                return all_solved;
            }
            auto r=solver.solve(state,initial_config);
            if(rephase_max_set) {
                if(!json_output){human_input_header(input_scramble,index,total);human_stemil_run(r,initial_config.budget_ms,0,1,stemil_peak_rss_kib,phase1_bytes,phase2_bytes,false);}
                else json_stemil(r,initial_config.budget_ms,0,1,true);
                auto emit=[&](const RephaseRun& run,size_t run_index,size_t run_total) {
                    if(r.solved)record_shortened(index,input_scramble,input_seed,r.initial_moves,run.result.moves,run.plan.window);
                    if(json_output)json_rephase(r,run,run_index,run_total,0,initial_config.budget_ms);
                    else human_rephase_run(r,run,run_index,run_total,rephase_peak_bytes);
                };
                apply_rephases(state,r,-1,emit);return r.solved;
            }
            if(r.solved) {
                summary.lengths=true;++summary.measured;summary.initial_sum+=r.initial_moves.size();summary.final_sum+=r.moves.size();
                if(r.moves.size()<r.initial_moves.size()){++summary.shortened;summary.saved+=r.initial_moves.size()-r.moves.size();record_shortened(index,input_scramble,input_seed,r.initial_moves,r.moves,0);}
                summary.elapsed_sum+=r.elapsed_ms;
                if(r.orientations>1 || !r.candidate_reports.empty()){++summary.multi_measured;summary.single_sum+=r.single_orientation_htm?r.single_orientation_htm:r.initial_moves.size();}
            }
            VerifyInfo verify_info;
            if(verifier && r.solved) {
                // Is there a solution strictly shorter than ours? A completed search says no (optimal).
                auto began=Clock::now();
                auto deadline=began+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(verify_cap_sec));
                int ours=int(r.moves.size());
                auto proof=ours>0?verifier->solve_parallel(state,deadline,ours-1,verify_threads):OptimalResult{};
                verify_info.done=true;verify_info.seconds=ms(began,Clock::now())/1000.0;
                if(proof.solved){verify_info.status=1;verify_info.optimal_length=proof.length;}
                else if(!proof.timed_out){verify_info.status=0;verify_info.optimal_length=ours;}
                else{verify_info.status=2;verify_info.lower_bound=proof.last_bound_completed+1;}
                summary.verify=true;summary.verify_seconds+=verify_info.seconds;
                if(verify_info.status==0){++summary.optimal;summary.htm_optimal+=ours;summary.optimal_length_sum+=ours;}
                else if(verify_info.status==1){++summary.not_optimal;summary.htm_not_optimal+=ours;summary.optimal_length_sum+=verify_info.optimal_length;}
                else{++summary.unknown;summary.htm_unknown+=ours;}
                std::ostringstream row;row<<"큐브 "<<(index+1);if(input_seed)row<<" (seed "<<*input_seed<<')';
                row<<": solve "<<ours<<"수, ";
                if(verify_info.status==0)row<<"최적해 (증명됨)";
                else if(verify_info.status==1)row<<"최적 아님 (최적 "<<verify_info.optimal_length<<"수, +"<<(ours-verify_info.optimal_length)<<")";
                else row<<"미확정 (하한 "<<verify_info.lower_bound<<"수, 시간 상한 도달)";
                row<<", 검증 "<<std::fixed<<std::setprecision(1)<<verify_info.seconds<<"초";
                if(input_scramble && !input_scramble->empty())row<<"\n  셔플: "<<*input_scramble;
                row<<"\n  solve: "<<format_algorithm(r.moves)<<"\n";
                stats_line(row.str());
            }
            if(!json_output && (command=="solve" || command=="batch")) {
                human_result(r,input_scramble,stemil_peak_rss_kib,phase1_bytes,phase2_bytes,rephase_peak_bytes,index,total);
                if(verify_info.done) {
                    std::cout<<"최적해 검증: "<<(verify_info.status==0?"최적해 (증명됨)":verify_info.status==1?"최적 아님 (최적 "+std::to_string(verify_info.optimal_length)+"수)":"미확정 (하한 "+std::to_string(verify_info.lower_bound)+"수)")
                             <<", 검증 시간 "<<std::fixed<<std::setprecision(1)<<verify_info.seconds<<" 초\n"<<std::flush;
                }
                return r.solved;
            }
            auto p1=phase_moves(r,true),p2=phase_moves(r,false);
            std::cout<<std::fixed<<std::setprecision(3)<<"{\"index\":"<<index<<",\"input_mode\":"<<quote(input_seed?"automatic":input_scramble?"scramble":"facelets")<<",\"seed\":"<<(input_seed?std::to_string(*input_seed):"null")<<",\"scramble\":"<<(input_scramble?quote(*input_scramble):"null")<<",\"scramble_length\":"<<(input_scramble?std::to_string(parse_algorithm(*input_scramble).size()):"null")<<",\"facelets\":"<<quote(state.facelets())<<",\"solved\":"<<(r.solved?"true":"false")<<",\"tables\":\"expanded\",\"stemil_resources\":"<<quote(tables.resources().string())<<",\"sequence\":"<<quote(format_algorithm(r.moves))<<",\"initial_sequence\":"<<quote(format_algorithm(r.initial_moves))<<",\"htm\":"<<(r.solved?std::to_string(r.moves.size()):"null")<<",\"initial_htm\":"<<(r.solved?std::to_string(r.initial_moves.size()):"null")<<",\"phase1_sequence\":"<<quote(format_algorithm(p1))<<",\"phase1_htm\":"<<(r.solved?std::to_string(p1.size()):"null")<<",\"phase1_ms\":"<<r.phase1_ms<<",\"phase1_memory_bytes\":"<<phase1_bytes<<",\"phase2_sequence\":"<<quote(format_algorithm(p2))<<",\"phase2_htm\":"<<(r.solved?std::to_string(p2.size()):"null")<<",\"phase2_ms\":"<<r.phase2_ms<<",\"phase2_memory_bytes\":"<<phase2_bytes<<",\"initial_boundary\":"<<r.initial_boundary<<",\"final_focal_index\":"<<r.final_focal_index<<",\"elapsed_ms\":"<<r.elapsed_ms<<",\"two_phase_ms\":"<<r.two_phase_ms<<",\"repair_ms\":"<<r.repair_ms<<",\"rephase_ms\":"<<r.repair_ms<<",\"rephase_search_ms\":"<<rephase_search_ms(r)<<",\"budget_ms\":"<<solve_config.budget_ms<<",\"allocated_repair_fraction\":"<<r.allocated_repair_fraction<<",\"allocated_rephase_ms\":"<<r.allocated_rephase_ms<<",\"two_phase_budget_ms\":"<<r.two_phase_budget_ms<<",\"search_nodes\":"<<r.search_nodes<<",\"candidates\":"<<r.candidates<<",\"two_phase_timed_out\":"<<(r.two_phase_timed_out?"true":"false")<<",\"setup_ms\":"<<setup_ms<<",\"heuristic_bytes\":"<<tables.allocated_bytes()<<",\"stemil_peak_rss_kib\":"<<stemil_peak_rss_kib<<",\"repair_table_bytes\":"<<(repair?repair->table_bytes():0)<<",\"repair_build_peak_bytes\":"<<(repair?repair->build_peak_bytes():0)<<",\"rephase_peak_bytes\":"<<rephase_peak_bytes<<",\"repair_table_states\":"<<(repair?repair->states():0)<<",\"peak_rss_kib\":"<<rss_kib()<<',';
            if(verify_info.done)std::cout<<"\"optimal_status\":\""<<(verify_info.status==0?"optimal":verify_info.status==1?"not_optimal":"undecided")<<"\",\"optimal_htm\":"<<(verify_info.status==2?std::string("null"):std::to_string(verify_info.optimal_length))<<",\"optimal_lower_bound\":"<<verify_info.lower_bound<<",\"verify_seconds\":"<<verify_info.seconds<<',';
            multi_fields(r);settings(repair_config);std::cout<<',';stats(r.repair_stats);std::cout<<"}\n"<<std::flush;return r.solved;
        };
        if(command=="solve") {
            bool all=true;const bool repeating=automatic && runs!=1;
            if(repeating) {
                struct sigaction action{};action.sa_handler=request_stop;sigemptyset(&action.sa_mask);
                sigaction(SIGINT,&action,nullptr);sigaction(SIGTERM,&action,nullptr);
            }
            for(uint64_t i=0;automatic?(runs==0 || i<uint64_t(runs)):i<1;++i) {
                if(i>0 && stop_requested)break;
                if(automatic && i>0){cube_seed=uint32_t(seed+uint32_t(i));input_algorithm=random_scramble(scramble_length,*cube_seed);scramble=format_algorithm(input_algorithm);cube=State::identity().applied(input_algorithm);if(trace_enabled){TraceEvent e;e.event="shuffle";e.stage="input";e.sequence=scramble;e.message="seed="+std::to_string(*cube_seed);writer.write(e,int(i));}}
                bool ok=output(cube,size_t(i),automatic || has_scramble?std::optional<std::string>(scramble):std::nullopt,cube_seed);
                ++summary.cubes;if(ok)++summary.solved;all=ok && all;
            }
            if(repeating || summary.verify)print_summary(summary,stop_requested,std::cerr);
            if(summary.verify) {
                std::ostringstream text;print_summary(summary,stop_requested,text);
                stats_line("\n----- 실행 요약 -----"+text.str());
            }
            return all?0:2;
        }
        std::string line;size_t index=0;bool all=true;
        while(std::getline(std::cin,line)){if(!line.empty() && line.back()=='\r')line.pop_back();if(line.empty())continue;all=output(State::from_facelets(line),index++,std::nullopt,std::nullopt) && all;}
        if(summary.verify){print_summary(summary,false,std::cerr);std::ostringstream text;print_summary(summary,false,text);stats_line("\n----- 실행 요약 -----"+text.str());}
        return all?0:2;
    }catch(const std::exception& e){std::cerr<<"{\"error\":"<<quote(e.what())<<"}\n";return 1;}
}
