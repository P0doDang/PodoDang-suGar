#pragma once
#include "repair.hpp"
#include "tables.hpp"
#include <optional>

namespace rephase {
struct SolveConfig {
    double budget_ms=1000,repair_fraction=0.1;
    bool adaptive=false;
    std::optional<double> rephase_ms;
    TraceCallback trace;
};
struct SolveResult {
    bool solved=false,two_phase_timed_out=false;
    Algorithm initial_moves,moves;
    int initial_boundary=0,final_focal_index=0;
    double elapsed_ms=0,two_phase_ms=0,phase1_ms=0,phase2_ms=0,repair_ms=0,allocated_repair_fraction=0;
    double allocated_rephase_ms=0,two_phase_budget_ms=0;
    uint64_t search_nodes=0,candidates=0;
    RepairStats repair_stats;
};
class Solver {
public:
    Solver(const Tables& tables,const Repair* repair=nullptr):tables_(tables),repair_(repair){}
    SolveResult solve(const State& cube,SolveConfig config={}) const;
private:
    const Tables& tables_;
    const Repair* repair_;
};
int phase_boundary(const State& cube,const Algorithm& moves);
}
