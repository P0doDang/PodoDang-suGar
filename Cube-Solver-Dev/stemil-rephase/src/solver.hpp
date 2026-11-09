#pragma once
#include "repair.hpp"
#include "tables.hpp"
#include <optional>
#include <vector>

namespace rephase {
struct SolveConfig {
    double budget_ms=1000,repair_fraction=0.1;
    bool adaptive=false;
    std::optional<double> rephase_ms;
    TraceCallback trace;
    // Multi-orientation search: 1..12 equivalent orientations (3 URF rotations x inverse cube x LR mirror)
    // searched by parallel threads; the shortest solution wins.
    int orientations=1;
    // Number of distinct best candidate solutions (across orientations) that are
    // each repaired, in parallel; the shortest repaired one wins.
    int candidates=1;
    // Improvement search after the first solution: false = stemil prefix enumeration
    // (one greedy Phase-1 path per prefix), true = Kociemba-style enumeration of every
    // Phase-1 solution by increasing length, each followed by a bounded Phase 2.
    bool phase1_ida=false;
};
struct OrientationReport {
    int index=0,rotation=0;bool inverse=false,mirror=false;
    bool solved=false,timed_out=false;
    size_t htm=0;
    double ms=0;
    uint64_t nodes=0;
};
struct CandidateReport {
    int orientation=0;
    size_t before=0,after=0;
    double repair_ms=0;
};
struct SolveResult {
    bool solved=false,two_phase_timed_out=false;
    Algorithm initial_moves,moves;
    int initial_boundary=0,final_focal_index=0;
    double elapsed_ms=0,two_phase_ms=0,phase1_ms=0,phase2_ms=0,repair_ms=0,allocated_repair_fraction=0;
    double allocated_rephase_ms=0,two_phase_budget_ms=0;
    uint64_t search_nodes=0,candidates=0;
    RepairStats repair_stats;
    // Populated by the 6-way path (orientations>1 or candidates>1).
    int orientations=1,winning_orientation=0,repaired_candidate=-1;
    double initial_found_ms=0,final_found_ms=0,rephase_found_ms=0; /* rephase_found_ms: offset from rephase start of the last improvement, 0 if none */ // since solve start: when the pre-rephase / final answer was first found (6-way path)
    size_t single_orientation_htm=0; // orientation 0 alone, for comparison
    std::vector<OrientationReport> orientation_reports;
    std::vector<CandidateReport> candidate_reports;
};
class Solver {
public:
    Solver(const Tables& tables,const Repair* repair=nullptr):tables_(tables),repair_(repair){}
    SolveResult solve(const State& cube,SolveConfig config={}) const;
private:
    SolveResult solve_multi(const State& cube,SolveConfig config) const;
    const Tables& tables_;
    const Repair* repair_;
};
int phase_boundary(const State& cube,const Algorithm& moves);
}
