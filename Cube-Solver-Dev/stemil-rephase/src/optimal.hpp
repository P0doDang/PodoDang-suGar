#pragma once
// Exact (optimal HTM) solver: IDA* over the full cube with an admissible lower bound
// max(corner pattern database, Phase-1 distance on each of the three face axes).
// The Phase-1 distance comes from the existing stemil Phase-1 table (mod-3 gradient),
// the corner database is built here in RAM by BFS (never written into the stemil tree).
#include "cube.hpp"
#include "repair.hpp"
#include "tables.hpp"
#include <cstdint>
#include <vector>

namespace rephase {
class CornerPdb {
public:
    // max_depth 11 gives the exact corner distance; smaller values keep the table
    // admissible by storing max_depth+1 (a lower bound) for unreached states.
    explicit CornerPdb(int max_depth=11);
    int operator()(int corner_permutation,int corner_orientation) const { return dist_[size_t(corner_permutation)*2187+size_t(corner_orientation)]; }
    int max_depth() const { return max_depth_; }
    size_t bytes() const { return dist_.size(); }
private:
    int max_depth_;
    std::vector<uint8_t> dist_;
};
struct OptimalResult {
    bool solved=false,timed_out=false;
    int length=-1,root_bound=0,last_bound_completed=-1;
    Algorithm moves;
    uint64_t nodes=0;
};
class OptimalSolver {
public:
    OptimalSolver(const Tables& tables,const CornerPdb& pdb):tables_(tables),pdb_(pdb) {}
    // Shortest solution of length <= max_length, or timed_out with the deepest fully
    // searched bound (every solution shorter than last_bound_completed+1 is excluded).
    OptimalResult solve(const State& cube,Deadline deadline,int max_length=20) const;
    // Same result, with each bound's subtrees (first two moves) shared by `threads` threads.
    OptimalResult solve_parallel(const State& cube,Deadline deadline,int max_length,int threads) const;
    // Admissible lower bound at the root.
    int lower_bound(const State& cube) const;
private:
    const Tables& tables_;
    const CornerPdb& pdb_;
};
}
