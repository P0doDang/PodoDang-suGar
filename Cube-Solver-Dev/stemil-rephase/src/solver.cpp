#include "solver.hpp"
#include "moves.hpp"
#include "search.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>

namespace rephase {
namespace {
bool subgroup(const State& s) {
    return std::all_of(s.co.begin(),s.co.end(),[](int x){return x==0;}) &&
           std::all_of(s.eo.begin(),s.eo.end(),[](int x){return x==0;}) &&
           std::all_of(s.ep.begin(),s.ep.begin()+8,[](int x){return x<8;});
}
bool phase2_move(int m) { return m>=12 || m%3==1; }
double milliseconds(Deadline from,Deadline to) { return std::chrono::duration<double,std::milli>(to-from).count(); }
Deadline after(Deadline start,double ms) { return start+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(ms)); }
struct AccumulateTime {
    double& total;Deadline started=Clock::now();
    ~AccumulateTime(){total+=milliseconds(started,Clock::now());}
};
struct Search {
    const Tables& tables;
    Deadline deadline;
    uint64_t nodes=0,candidates=0;
    double phase1_ms=0,phase2_ms=0;
    bool timed_out=false;
    TraceCallback trace;
    bool expired() {
        if((++nodes&255)==1 && Clock::now()>=deadline)timed_out=true;
        return timed_out;
    }
    bool phase1(Cube cube,Algorithm& out) {
        if(Clock::now()>=deadline) { timed_out=true;return false; }
        AccumulateTime timer{phase1_ms};
        // Validate the queried gradient, then reuse the existing stemil Phase-1
        // function directly. Original search.cpp is compiled without modification.
        tables.phase1_lower_bound(cube.corner_orientation,cube.edge_orientation,cube.UDslice_edge_position);
        ::phase1_search(cube.corner_orientation,cube.edge_orientation,cube.UDslice_edge_position,&out);
        ++nodes;
        if(Clock::now()>=deadline) { timed_out=true;return false; }
        return true;
    }
    bool phase2(int cp,int ep,int slice,int exclusive_limit,Algorithm& out) {
        AccumulateTime timer{phase2_ms};
        std::function<bool(int,int,int,int,int)> visit=[&](int c,int e,int s,int left,int previous) {
            if(expired())return false;
            if(c==0 && e==0 && s==0)return true;
            if(left==0 || tables.phase2_lower_bound(c,e,s)>left)return false;
            for(int m=0;m<18;++m)if(phase2_move(m) && canonical_next(previous,m)) {
                out.push_back(m);
                if(visit(move_corner_permutation[c][m],move_UD_edge_permutation[e][m],move_UDslice_edge_permutation[s][m],left-1,m))return true;
                out.pop_back();if(timed_out)return false;
            }
            return false;
        };
        for(int depth=tables.phase2_lower_bound(cp,ep,slice);depth<exclusive_limit && depth<=18;++depth) {
            if(visit(cp,ep,slice,depth,-1))return true;
            if(timed_out)return false;
        }
        return false;
    }
    bool candidate(Cube cube,int limit,Algorithm& moves) {
        ++candidates;Algorithm p1;
        if(!phase1(cube,p1))return false;
        for(int m:p1)cube=move_cube(cube,m);
        int ep,slice;
        cubie_to_UD_edge_permutation(cube.cubie_edge_permutation,&ep);
        cubie_to_UDslice_edge_permutation(cube.cubie_edge_permutation+8,&slice);
        Algorithm p2;
        if(!phase2(cube.corner_permutation,ep,slice,limit-int(p1.size()),p2))return false;
        moves=std::move(p1);moves.insert(moves.end(),p2.begin(),p2.end());return true;
    }
    void prefixes(Cube cube,int left,int previous,Algorithm& prefix,Algorithm& best) {
        if(expired())return;
        if(left==0) {
            Algorithm suffix;
            if(candidate(cube,int(best.size())-int(prefix.size()),suffix)) {
                Algorithm answer=prefix;answer.insert(answer.end(),suffix.begin(),suffix.end());answer=normalize(answer);
                if(answer.size()<best.size()) {
                    if(trace){TraceEvent e;e.event="candidate_improved";e.stage="two_phase";e.before=int(best.size());e.after=int(answer.size());e.sequence=format_algorithm(answer);trace(e);}
                    best=std::move(answer);
                }
            }
            return;
        }
        if(size_t(left)+prefix.size()>=best.size())return;
        for(int m=0;m<18;++m)if(canonical_next(previous,m)) {
            prefix.push_back(m);prefixes(move_cube(cube,m),left-1,m,prefix,best);prefix.pop_back();
            if(timed_out || best.empty())return;
        }
    }
};
}
int phase_boundary(const State& cube,const Algorithm& moves) {
    std::vector<bool> allowed(moves.size()+1,true);
    for(size_t i=moves.size();i>0;--i)allowed[i-1]=allowed[i] && phase2_move(moves[i-1]);
    State s=cube;
    for(size_t i=0;i<=moves.size();++i) {
        if(allowed[i] && subgroup(s))return i;
        if(i<moves.size())s=s.moved(moves[i]);
    }
    throw std::logic_error("no valid Phase-1/Phase-2 boundary");
}
SolveResult Solver::solve(const State& cube,SolveConfig config) const {
    cube.validate();
    if(!std::isfinite(config.budget_ms) || config.budget_ms<=0 || config.budget_ms>60000 || !std::isfinite(config.repair_fraction) || config.repair_fraction<0 || config.repair_fraction>=1)
        throw std::invalid_argument("budget must be >0 and <=60000 ms, repair fraction must be in [0,1)");
    double rephase_budget=config.rephase_ms.value_or(config.budget_ms*config.repair_fraction);
    if(!std::isfinite(rephase_budget) || rephase_budget<0 || rephase_budget>=config.budget_ms)
        throw std::invalid_argument("rephase time must be >=0 and less than total budget");
    if(config.rephase_ms && config.adaptive)throw std::invalid_argument("fixed rephase time cannot be combined with adaptive");
    if(rephase_budget>0 && !repair_)throw std::invalid_argument("repair engine required for a nonzero rephase time");
    SolveResult result;Deadline start=Clock::now(),end=after(start,config.budget_ms);
    result.allocated_rephase_ms=rephase_budget;result.allocated_repair_fraction=rephase_budget/config.budget_ms;result.two_phase_budget_ms=config.budget_ms-rephase_budget;
    Search search{tables_,after(start,result.two_phase_budget_ms),0,0,0,0,false,config.trace};
    if(config.trace){TraceEvent e;e.event="solve_start";e.stage="two_phase";e.budget_ms=result.two_phase_budget_ms;config.trace(e);}
    Algorithm best;
    if(cube.solved()) {
        result.solved=true;result.elapsed_ms=milliseconds(start,Clock::now());
        if(config.trace){TraceEvent e;e.event="solve_end";e.stage="solve";e.after=0;e.complete=true;config.trace(e);}
        return result;
    }
    if(!search.candidate(cube.coordinates(),31,best)) {
        result.two_phase_timed_out=search.timed_out;result.search_nodes=search.nodes;result.candidates=search.candidates;result.phase1_ms=search.phase1_ms;result.phase2_ms=search.phase2_ms;
        if(config.trace){TraceEvent e;e.event="initial_timeout";e.stage="two_phase";e.timed_out=search.timed_out;e.nodes=search.nodes;config.trace(e);}
        result.two_phase_ms=result.elapsed_ms=milliseconds(start,Clock::now());return result;
    }
    best=normalize(best);
    if(config.trace){TraceEvent e;e.event="initial_solution";e.stage="two_phase";e.after=int(best.size());e.sequence=format_algorithm(best);config.trace(e);}
    if(config.adaptive) {
        double f=config.repair_fraction;
        if(best.size()>=21)f*=0.5;else if(best.size()<=18)f=std::min(0.5,f*2);
        result.allocated_repair_fraction=f;result.allocated_rephase_ms=config.budget_ms*f;result.two_phase_budget_ms=config.budget_ms*(1-f);search.deadline=after(start,result.two_phase_budget_ms);
        if(config.trace){TraceEvent e;e.event="adaptive_budget";e.stage="two_phase";e.budget_ms=result.two_phase_budget_ms;config.trace(e);}
    }
    Algorithm prefix;
    for(int depth=1;!best.empty() && depth<int(best.size()) && Clock::now()<search.deadline;++depth) {
        search.prefixes(cube.coordinates(),depth,-1,prefix,best);if(search.timed_out)break;
    }
    if(!cube.applied(best).solved())throw std::logic_error("two-phase solution failed cubie verification");
    result.solved=true;result.initial_moves=result.moves=best;
    result.initial_boundary=result.final_focal_index=phase_boundary(cube,best);
    result.two_phase_timed_out=search.timed_out;result.search_nodes=search.nodes;result.candidates=search.candidates;result.phase1_ms=search.phase1_ms;result.phase2_ms=search.phase2_ms;
    if(config.trace){TraceEvent e;e.event="two_phase_end";e.stage="two_phase";e.after=int(best.size());e.begin=result.initial_boundary;e.sequence=format_algorithm(best);e.nodes=search.nodes;e.timed_out=search.timed_out;config.trace(e);}
    Deadline repair_begin=Clock::now();result.two_phase_ms=milliseconds(start,repair_begin);
    if(result.allocated_rephase_ms>0 && repair_begin<end) {
        // An early global search finish never silently expands the rephase cap.
        auto repaired=repair_->optimize(best,result.initial_boundary,std::min(end,after(repair_begin,result.allocated_rephase_ms)),config.trace);
        result.moves=std::move(repaired.moves);result.final_focal_index=repaired.boundary;result.repair_stats=std::move(repaired.stats);
    }
    if(result.moves.size()>best.size() || !cube.applied(result.moves).solved())throw std::logic_error("final solution failed verification");
    if(config.trace){TraceEvent e;e.event="solve_end";e.stage="solve";e.before=int(best.size());e.after=int(result.moves.size());e.sequence=format_algorithm(result.moves);e.complete=true;config.trace(e);}
    Deadline finish=Clock::now();result.repair_ms=milliseconds(repair_begin,finish);result.elapsed_ms=milliseconds(start,finish);
    return result;
}
}
