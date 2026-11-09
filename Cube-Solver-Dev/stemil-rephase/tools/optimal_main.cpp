// Optimal-solution oracle and corpus generator.
//   optimal gen   --count N --length L [--seed S] [--threads T] [--cap-sec C] --out corpus.jsonl
//   optimal solve (--scramble "R U F2" | --facelets <54>) [--cap-sec C]
//   optimal selftest
#include "optimal.hpp"
#include "shuffle.hpp"
#include "symmetry.hpp"
#include <atomic>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <optional>

using namespace rephase;
namespace {
struct Args { std::vector<std::string> v; std::string get(const std::string& k,const std::string& d="") const { for(size_t i=0;i+1<v.size();++i)if(v[i]==k)return v[i+1];return d; } };
Deadline in_seconds(double s){return Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double>(s));}
double secs(Deadline a,Deadline b){return std::chrono::duration<double>(b-a).count();}
struct Hash { size_t operator()(Key k) const { return hash_key(k); } };
int selftest(const Tables& tables,const CornerPdb& pdb) {
    OptimalSolver solver(tables,pdb);
    // Independent BFS oracle for distances <= 4.
    std::unordered_map<Key,int,Hash> distance;std::vector<State> queue{State::identity()};distance[queue[0].key()]=0;
    for(size_t head=0;head<queue.size();++head){State s=queue[head];int d=distance[s.key()];if(d==4)continue;for(int m=0;m<18;++m){State n=s.moved(m);if(distance.emplace(n.key(),d+1).second)queue.push_back(n);}}
    std::mt19937 rng(5);int checked=0;
    for(int i=0;i<300;++i) {
        State s=queue[rng()%queue.size()];int expected=distance[s.key()];
        auto r=solver.solve(s,in_seconds(20));
        if(!r.solved || r.length!=expected || !s.applied(r.moves).solved() || solver.lower_bound(s)>expected){std::cerr<<"selftest failed: expected "<<expected<<" got "<<r.length<<"\n";return 1;}
        ++checked;
    }
    // Lower bound never exceeds a known solution length; optimal never exceeds it either.
    for(int i=0;i<40;++i) {
        auto scramble=random_scramble(9,uint32_t(100+i));State s=State::identity().applied(scramble);
        auto r=solver.solve(s,in_seconds(60));
        if(!r.solved || int(r.moves.size())>9 || solver.lower_bound(s)>r.length || !s.applied(r.moves).solved()){std::cerr<<"selftest failed on scramble "<<i<<"\n";return 1;}
        auto parallel=solver.solve_parallel(s,in_seconds(60),20,8);
        if(!parallel.solved || parallel.length!=r.length || !s.applied(parallel.moves).solved()){std::cerr<<"parallel search disagrees on scramble "<<i<<"\n";return 1;}
        // Nothing shorter exists: re-run with a smaller cap.
        if(r.length>0 && solver.solve(s,in_seconds(60),r.length-1).solved){std::cerr<<"shorter solution found after optimal\n";return 1;}
        ++checked;
    }
    std::cout<<"selftest passed ("<<checked<<" cubes; BFS-oracle distances <= 4 and scramble lengths 9, serial and parallel search agree)\n";
    return 0;
}
}
int main(int argc,char** argv) {
    try {
        Args args;for(int i=2;i<argc;++i)args.v.push_back(argv[i]);
        std::string command=argc>1?argv[1]:"";
        std::string resources=args.get("--resources","../third_party/stemil/resources");
        int pdb_depth=std::stoi(args.get("--pdb-depth","11"));
        if(command!="gen" && command!="solve" && command!="selftest"){std::cerr<<"usage: optimal gen|solve|selftest ...\n";return 1;}
        auto t0=Clock::now();
        Tables tables(resources);
        std::cerr<<"stemil tables loaded in "<<secs(t0,Clock::now())<<" s\n";
        t0=Clock::now();CornerPdb pdb(pdb_depth);
        std::cerr<<"corner database (depth "<<pdb_depth<<", "<<pdb.bytes()/(1024*1024)<<" MiB) built in "<<secs(t0,Clock::now())<<" s\n";
        OptimalSolver solver(tables,pdb);
        if(command=="selftest")return selftest(tables,pdb);
        double cap=std::stod(args.get("--cap-sec","600"));
        if(command=="solve") {
            State cube=args.get("--facelets").empty()?State::identity().applied(parse_algorithm(args.get("--scramble"))):State::from_facelets(args.get("--facelets"));
            auto s=Clock::now();auto r=solver.solve(cube,in_seconds(cap));
            std::cout<<(r.solved?"optimal length "+std::to_string(r.length)+": "+format_algorithm(r.moves):std::string("timeout"))<<"  (root bound "<<r.root_bound<<", searched through bound "<<r.last_bound_completed<<", "<<r.nodes<<" nodes, "<<secs(s,Clock::now())<<" s)\n";
            return r.solved?0:2;
        }
        int count=std::stoi(args.get("--count","20")),length=std::stoi(args.get("--length","16")),threads=std::stoi(args.get("--threads","12"));
        uint32_t seed=uint32_t(std::stoul(args.get("--seed","1000")));std::string out_path=args.get("--out");
        if(out_path.empty())throw std::invalid_argument("--out required");
        std::vector<std::string> lines(count);std::atomic<int> next{0},done{0},timeouts{0};std::mutex mu;
        auto worker=[&]{for(int i;(i=next++)<count;){
            uint32_t s=seed+uint32_t(i);auto scramble=random_scramble(length,s);State cube=State::identity().applied(scramble);
            auto begin=Clock::now();auto r=solver.solve(cube,in_seconds(cap));double sec=secs(begin,Clock::now());
            std::ostringstream o;o<<std::fixed<<std::setprecision(2)<<"{\"index\":"<<i<<",\"seed\":"<<s<<",\"scramble_length\":"<<length<<",\"scramble\":\""<<format_algorithm(scramble)<<"\",\"facelets\":\""<<cube.facelets()<<"\",\"optimal\":";
            if(r.solved)o<<r.length<<",\"optimal_sequence\":\""<<format_algorithm(r.moves)<<"\"";else{o<<"null,\"lower_bound\":"<<(r.last_bound_completed+1);++timeouts;}
            o<<",\"root_bound\":"<<r.root_bound<<",\"nodes\":"<<r.nodes<<",\"seconds\":"<<sec<<"}";
            std::lock_guard<std::mutex> g(mu);lines[i]=o.str();std::cerr<<"["<<++done<<"/"<<count<<"] cube "<<i<<" optimal "<<(r.solved?std::to_string(r.length):"timeout")<<" ("<<sec<<" s)\n";
        }};
        std::vector<std::thread> pool;for(int i=0;i<threads;++i)pool.emplace_back(worker);for(auto& t:pool)t.join();
        std::ofstream out(out_path);for(auto& l:lines)out<<l<<"\n";
        std::cerr<<"wrote "<<out_path<<" ("<<timeouts<<" timeouts)\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<"\n";return 1;}
}
