#include "optimal.hpp"
#include "kernel.hpp"
#include "symmetry.hpp"
#include <algorithm>
#include <atomic>
#include <mutex>
#include <thread>
#include <stdexcept>

namespace rephase {
CornerPdb::CornerPdb(int max_depth):max_depth_(max_depth) {
    if(max_depth<1 || max_depth>11)throw std::invalid_argument("corner database depth must be 1..11");
    transformations();
    const uint8_t unseen=uint8_t(max_depth+1);
    dist_.assign(size_t(40320)*2187,unseen);
    dist_[0]=0;
    std::vector<uint32_t> frontier{0},next;
    for(int depth=0;depth<max_depth;++depth) {
        next.clear();
        for(uint32_t index:frontier) {
            int cp=int(index/2187),co=int(index%2187);
            for(int m=0;m<18;++m) {
                size_t target=size_t(move_corner_permutation[cp][m])*2187+size_t(move_corner_orientation[co][m]);
                if(dist_[target]==unseen){dist_[target]=uint8_t(depth+1);next.push_back(uint32_t(target));}
            }
        }
        frontier.swap(next);
    }
}
namespace {
struct Node { int co[3],eo[3],sl[3],d[3],cp; };
struct Search {
    const Tables& tables;const CornerPdb& pdb;Deadline deadline;
    int rot[3][18];
    State root;uint64_t nodes=0;bool timed_out=false;Algorithm path,found;
    const std::atomic<bool>* stop=nullptr;
    int h(const Node& n) const { return std::max(std::max(n.d[0],std::max(n.d[1],n.d[2])),pdb(n.cp,n.co[0])); }
    Node start() const {
        Node n{};
        for(int a=0;a<3;++a) {
            Cube c=conjugate(root,a).coordinates();
            n.co[a]=c.corner_orientation;n.eo[a]=c.edge_orientation;n.sl[a]=c.UDslice_edge_position;
            n.d[a]=tables.phase1_lower_bound(n.co[a],n.eo[a],n.sl[a]);
            if(a==0)n.cp=c.corner_permutation;
        }
        return n;
    }
    Node child(const Node& n,int m) const {
        Node c;
        for(int a=0;a<3;++a) {
            int mm=rot[a][m];
            c.co[a]=move_corner_orientation[n.co[a]][mm];c.eo[a]=move_edge_orientation[n.eo[a]][mm];c.sl[a]=move_UDslice_edge_position[n.sl[a]][mm];
            int value=phase1_mod3(c.co[a],c.eo[a],c.sl[a]),d=n.d[a];
            c.d[a]=value==(d+1)%3?d+1:value==d%3?d:d-1;
        }
        c.cp=move_corner_permutation[n.cp][m];
        return c;
    }
    bool dfs(const Node& n,int g,int bound,int previous) {
        if((++nodes&8191)==0 && (Clock::now()>=deadline || (stop && stop->load(std::memory_order_relaxed))))timed_out=true;
        if(timed_out)return false;
        int estimate=h(n);
        if(g+estimate>bound)return false;
        if(estimate==0 && root.applied(path).solved()){found=path;return true;}
        if(g==bound)return false;
        for(int m=0;m<18;++m)if(canonical_next(previous,m)) {
            path.push_back(m);
            if(dfs(child(n,m),g+1,bound,m))return true;
            path.pop_back();
            if(timed_out)return false;
        }
        return false;
    }
};
}
int OptimalSolver::lower_bound(const State& cube) const {
    Search s{tables_,pdb_,Deadline{},{},cube};
    for(int a=0;a<3;++a)for(int m=0;m<18;++m)s.rot[a][m]=rotate_move(m,a);
    return s.h(s.start());
}
OptimalResult OptimalSolver::solve(const State& cube,Deadline deadline,int max_length) const {
    cube.validate();
    OptimalResult result;
    Search s{tables_,pdb_,deadline,{},cube};
    for(int a=0;a<3;++a)for(int m=0;m<18;++m)s.rot[a][m]=rotate_move(m,a);
    Node start=s.start();
    result.root_bound=s.h(start);
    for(int bound=result.root_bound;bound<=max_length;++bound) {
        s.path.clear();
        if(s.dfs(start,0,bound,-1)){result.solved=true;result.length=bound;result.moves=s.found;break;}
        if(s.timed_out){result.timed_out=true;break;}
        result.last_bound_completed=bound;
    }
    result.nodes=s.nodes;
    return result;
}
OptimalResult OptimalSolver::solve_parallel(const State& cube,Deadline deadline,int max_length,int threads) const {
    if(threads<=1)return solve(cube,deadline,max_length);
    cube.validate();
    OptimalResult result;
    Search base{tables_,pdb_,deadline,{},cube};
    for(int a=0;a<3;++a)for(int m=0;m<18;++m)base.rot[a][m]=rotate_move(m,a);
    Node start=base.start();
    result.root_bound=base.h(start);
    std::vector<std::pair<int,int>> tasks;
    for(int a=0;a<18;++a)for(int b=0;b<18;++b)if(canonical_next(a,b))tasks.emplace_back(a,b);
    for(int bound=result.root_bound;bound<=max_length;++bound) {
        if(bound<=2) {
            // Tiny bounds: the serial search is immediate.
            base.path.clear();base.timed_out=false;
            if(base.dfs(start,0,bound,-1)){result.solved=true;result.length=bound;result.moves=base.found;break;}
            if(base.timed_out){result.timed_out=true;break;}
            result.last_bound_completed=bound;continue;
        }
        std::atomic<size_t> next{0};std::atomic<bool> stop{false},timed_out{false};std::atomic<uint64_t> nodes{0};
        std::mutex mu;Algorithm found;
        auto worker=[&] {
            Search s{tables_,pdb_,deadline,{},cube};
            for(int a=0;a<3;++a)for(int m=0;m<18;++m)s.rot[a][m]=base.rot[a][m];
            s.stop=&stop;
            for(size_t i;(i=next++)<tasks.size() && !stop;) {
                auto [a,b]=tasks[i];
                Node first=s.child(start,a);if(1+s.h(first)>bound)continue;
                Node second=s.child(first,b);
                s.path={a,b};
                if(s.dfs(second,2,bound,b)){std::lock_guard<std::mutex> g(mu);if(!stop){found=s.found;stop=true;}break;}
                if(s.timed_out){if(!stop)timed_out=true;stop=true;break;}
            }
            nodes+=s.nodes;
        };
        std::vector<std::thread> pool;for(int i=0;i<threads;++i)pool.emplace_back(worker);for(auto& t:pool)t.join();
        result.nodes+=nodes;
        if(!found.empty()){result.solved=true;result.length=bound;result.moves=found;break;}
        if(timed_out || Clock::now()>=deadline){result.timed_out=true;break;}
        result.last_bound_completed=bound;
    }
    result.nodes+=base.nodes;
    return result;
}
}
