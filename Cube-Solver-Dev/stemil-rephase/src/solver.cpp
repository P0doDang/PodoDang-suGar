#include "solver.hpp"
#include "moves.hpp"
#include "search.hpp"
#include "symmetry.hpp"
#include "kernel.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <exception>
#include <thread>
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
struct Search {
    const Tables& tables;
    Deadline deadline;
    uint64_t nodes=0,candidates=0;
    double phase1_ms=0,phase2_ms=0;
    bool timed_out=false;
    TraceCallback trace;
    std::vector<Algorithm>* history=nullptr;
    std::vector<double>* history_ms=nullptr;Deadline origin{};
    void record(const Algorithm& a){if(history)history->push_back(a);if(history_ms)history_ms->push_back(std::chrono::duration<double,std::milli>(Clock::now()-origin).count());}
    bool expired() {
        if((++nodes&255)==1 && Clock::now()>=deadline)timed_out=true;
        return timed_out;
    }
    bool phase2(int cp,int ep,int slice,int exclusive_limit,Algorithm& out) {
        for(int depth=phase2_bound(cp,ep,slice);depth<exclusive_limit && depth<=18;++depth) {
            if(phase2_visit(*this,cp,ep,slice,depth,-1,out))return true;
            if(timed_out)return false;
        }
        return false;
    }
    bool candidate(Cube cube,int limit,Algorithm& moves) {
        ++candidates;
        Deadline t0=Clock::now();
        if(t0>=deadline) { timed_out=true;return false; }
        Algorithm p1;p1.reserve(24);
        phase1_descent(cube.corner_orientation,cube.edge_orientation,cube.UDslice_edge_position,p1);
        ++nodes;
        Deadline t1=Clock::now();phase1_ms+=milliseconds(t0,t1);
        if(t1>=deadline) { timed_out=true;return false; }
        for(int m:p1)cube=fast_move(cube,m);
        int ep,slice;
        cubie_to_UD_edge_permutation(cube.cubie_edge_permutation,&ep);
        cubie_to_UDslice_edge_permutation(cube.cubie_edge_permutation+8,&slice);
        Algorithm p2;p2.reserve(24);
        bool ok=phase2(cube.corner_permutation,ep,slice,limit-int(p1.size()),p2);
        phase2_ms+=milliseconds(t1,Clock::now());
        if(!ok)return false;
        moves=std::move(p1);moves.insert(moves.end(),p2.begin(),p2.end());return true;
    }
    void improved(Algorithm answer,Algorithm& best) {
        answer=normalize(answer);
        if(answer.size()>=best.size())return;
        if(trace){TraceEvent e;e.event="candidate_improved";e.stage="two_phase";e.before=int(best.size());e.after=int(answer.size());e.sequence=format_algorithm(answer);trace(e);}
        best=std::move(answer);
        record(best);
    }
    // Kociemba-style Phase 1: every path of exactly `length` moves that ends in G1,
    // skipping paths whose last move is itself a Phase-2 move (those equal a shorter
    // Phase-1 solution followed by Phase 2 and were covered at a smaller length).
    void phase1_paths(const Cube& cube,int distance,int togo,int previous,int length,Algorithm& path,Algorithm& best) {
        if(expired())return;
        if(togo==0) {
            if(!path.empty() && phase2_move(path.back()))return;
            ++candidates;
            Deadline t0=Clock::now();
            int ep,slice;
            cubie_to_UD_edge_permutation(const_cast<Edges*>(cube.cubie_edge_permutation),&ep);
            cubie_to_UDslice_edge_permutation(const_cast<Edges*>(cube.cubie_edge_permutation)+8,&slice);
            Algorithm p2;
            bool ok=phase2(cube.corner_permutation,ep,slice,int(best.size())-length,p2);
            phase2_ms+=milliseconds(t0,Clock::now());
            if(ok){Algorithm answer=path;answer.insert(answer.end(),p2.begin(),p2.end());improved(std::move(answer),best);}
            return;
        }
        for(int m=0;m<18;++m)if(canonical_next(previous,m)) {
            int c=move_corner_orientation[cube.corner_orientation][m],e=move_edge_orientation[cube.edge_orientation][m],sl=move_UDslice_edge_position[cube.UDslice_edge_position][m];
            int value=phase1_mod3(c,e,sl);
            int next=value==(distance+1)%3?distance+1:value==distance%3?distance:distance-1;
            if(next>togo-1)continue;
            path.push_back(m);
            phase1_paths(fast_move(cube,m),next,togo-1,m,length,path,best);
            path.pop_back();
            if(timed_out || int(best.size())<=length)return;
        }
    }
    void phase1_enumerate(const Cube& root,Algorithm& best) {
        Deadline t0=Clock::now();
        int distance=tables.phase1_lower_bound(root.corner_orientation,root.edge_orientation,root.UDslice_edge_position);
        phase1_ms+=milliseconds(t0,Clock::now());
        Algorithm path;
        for(int length=distance;length<int(best.size()) && !timed_out;++length) {
            double phase2_before=phase2_ms;Deadline began=Clock::now();
            phase1_paths(root,distance,length,-1,length,path,best);
            // Phase-1 time is the rest of this length's enumeration.
            phase1_ms+=milliseconds(began,Clock::now())-(phase2_ms-phase2_before);
        }
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
                    record(best);
                }
            }
            return;
        }
        if(size_t(left)+prefix.size()>=best.size())return;
        for(int m=0;m<18;++m)if(canonical_next(previous,m)) {
            prefix.push_back(m);prefixes(fast_move(cube,m),left-1,m,prefix,best);prefix.pop_back();
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
    if(config.orientations<1 || config.orientations>max_orientations || config.candidates<1 || config.candidates>64)
        throw std::invalid_argument("orientations must be 1..12 and candidates 1..64");
    if(config.orientations>1 || config.candidates>1) {
        if(config.adaptive)throw std::invalid_argument("--adaptive cannot be combined with --orientations/--candidates");
        return solve_multi(cube,std::move(config));
    }
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
    if(config.phase1_ida)search.phase1_enumerate(cube.coordinates(),best);
    else {
        Algorithm prefix;
        for(int depth=1;!best.empty() && depth<int(best.size()) && Clock::now()<search.deadline;++depth) {
            search.prefixes(cube.coordinates(),depth,-1,prefix,best);if(search.timed_out)break;
        }
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
SolveResult Solver::solve_multi(const State& cube,SolveConfig config) const {
    if(!std::isfinite(config.budget_ms) || config.budget_ms<=0 || config.budget_ms>60000 || !std::isfinite(config.repair_fraction) || config.repair_fraction<0 || config.repair_fraction>=1)
        throw std::invalid_argument("budget must be >0 and <=60000 ms, repair fraction must be in [0,1)");
    double rephase_budget=config.rephase_ms.value_or(config.budget_ms*config.repair_fraction);
    if(!std::isfinite(rephase_budget) || rephase_budget<0 || rephase_budget>=config.budget_ms)
        throw std::invalid_argument("rephase time must be >=0 and less than total budget");
    if(rephase_budget>0 && !repair_)throw std::invalid_argument("repair engine required for a nonzero rephase time");
    SolveResult result;Deadline start=Clock::now(),end=after(start,config.budget_ms);
    result.allocated_rephase_ms=rephase_budget;result.allocated_repair_fraction=rephase_budget/config.budget_ms;result.two_phase_budget_ms=config.budget_ms-rephase_budget;
    result.orientations=config.orientations;
    if(cube.solved()) {
        result.solved=true;result.elapsed_ms=milliseconds(start,Clock::now());return result;
    }
    transformations(); // initialise the shared move tables before any thread starts
    const Deadline two_phase_deadline=after(start,result.two_phase_budget_ms);
    struct Candidate { Algorithm moves;int boundary=0,orientation=0;double found_ms=0; };
    struct Worker {
        OrientationReport report;std::vector<Candidate> found;
        uint64_t candidates=0;double phase1_ms=0,phase2_ms=0;std::exception_ptr error;
    };
    std::vector<Worker> workers(config.orientations);
    if(config.trace){TraceEvent e;e.event="solve_start";e.stage="two_phase";e.budget_ms=result.two_phase_budget_ms;e.message="orientations="+std::to_string(config.orientations)+" candidates="+std::to_string(config.candidates);config.trace(e);}
    auto work=[&](int index) {
        Worker& worker=workers[index];Orientation orientation=orientation_at(index);
        worker.report.index=index;worker.report.rotation=orientation.rotation;worker.report.inverse=orientation.inverse;worker.report.mirror=orientation.mirror;
        try {
            Deadline began=Clock::now();
            State searched=search_state(cube,orientation);Cube coordinates=searched.coordinates();
            std::vector<Algorithm> history;std::vector<double> history_ms;
            Search search{tables_,two_phase_deadline,0,0,0,0,false,nullptr};search.history=&history;search.history_ms=&history_ms;search.origin=start;
            Algorithm best;
            if(search.candidate(coordinates,31,best)) {
                best=normalize(best);search.record(best);
                if(config.phase1_ida)search.phase1_enumerate(coordinates,best);
                else {
                    Algorithm prefix;
                    for(int depth=1;!best.empty() && depth<int(best.size()) && Clock::now()<search.deadline;++depth) {
                        search.prefixes(coordinates,depth,-1,prefix,best);if(search.timed_out)break;
                    }
                }
                if(!searched.applied(best).solved())throw std::logic_error("orientation solution failed cubie verification");
                worker.report.solved=true;worker.report.htm=best.size();
                for(size_t hi=0;hi<history.size();++hi) {const auto& item=history[hi];
                    Algorithm mapped=normalize(original_solution(item,orientation));
                    if(!cube.applied(mapped).solved())throw std::logic_error("mapped orientation solution failed verification");
                    int boundary=std::min(int(mapped.size()),original_boundary(phase_boundary(searched,item),item.size(),orientation));
                    worker.found.push_back({std::move(mapped),boundary,index,history_ms[hi]});
                }
            }
            worker.report.timed_out=search.timed_out;worker.report.nodes=search.nodes;worker.candidates=search.candidates;
            worker.phase1_ms=search.phase1_ms;worker.phase2_ms=search.phase2_ms;worker.report.ms=milliseconds(began,Clock::now());
        }catch(...){worker.error=std::current_exception();}
    };
    {
        std::vector<std::thread> threads;
        for(int i=1;i<config.orientations;++i)threads.emplace_back(work,i);
        work(0);
        for(auto& thread:threads)thread.join();
    }
    for(auto& worker:workers)if(worker.error)std::rethrow_exception(worker.error);
    std::vector<Candidate> pool;
    for(auto& worker:workers) {
        result.search_nodes+=worker.report.nodes;result.candidates+=worker.candidates;
        result.two_phase_timed_out=result.two_phase_timed_out || worker.report.timed_out;
        result.orientation_reports.push_back(worker.report);
        for(auto& item:worker.found)pool.push_back(std::move(item));
        if(config.trace){TraceEvent e;e.event="orientation_end";e.stage="two_phase";e.message="orientation="+std::to_string(worker.report.index)+" rotation="+std::to_string(worker.report.rotation)+(worker.report.inverse?" inverse":"");e.after=worker.report.solved?int(worker.report.htm):-1;e.nodes=worker.report.nodes;e.timed_out=worker.report.timed_out;e.budget_ms=worker.report.ms;config.trace(e);}
    }
    result.single_orientation_htm=workers[0].report.solved?workers[0].report.htm:0;
    result.two_phase_ms=milliseconds(start,Clock::now());
    if(pool.empty()) {
        result.elapsed_ms=result.two_phase_ms;
        if(config.trace){TraceEvent e;e.event="initial_timeout";e.stage="two_phase";e.timed_out=result.two_phase_timed_out;e.nodes=result.search_nodes;config.trace(e);}
        return result;
    }
    // Distinct candidates, shortest first (orientation order breaks ties).
    std::stable_sort(pool.begin(),pool.end(),[](const Candidate& a,const Candidate& b){return a.moves.size()<b.moves.size();});
    std::vector<Candidate> unique;
    for(auto& item:pool) {
        bool seen=std::any_of(unique.begin(),unique.end(),[&](const Candidate& c){return c.moves==item.moves;});
        if(!seen)unique.push_back(std::move(item));
    }
    if(unique.size()>size_t(config.candidates))unique.resize(config.candidates);
    const Candidate& base=unique.front();
    result.solved=true;result.initial_moves=result.moves=base.moves;result.initial_boundary=result.final_focal_index=base.boundary;
    result.winning_orientation=base.orientation;result.initial_found_ms=result.final_found_ms=base.found_ms;
    result.phase1_ms=workers[base.orientation].phase1_ms;result.phase2_ms=workers[base.orientation].phase2_ms;
    if(config.trace){TraceEvent e;e.event="initial_solution";e.stage="two_phase";e.after=int(base.moves.size());e.sequence=format_algorithm(base.moves);config.trace(e);}
    Deadline repair_begin=Clock::now();
    if(rephase_budget>0 && repair_begin<end) {
        Deadline repair_deadline=std::min(end,after(repair_begin,rephase_budget));
        std::vector<RepairResult> repaired(unique.size());std::vector<double> repair_ms(unique.size(),0);std::vector<std::exception_ptr> errors(unique.size());
        auto repair_one=[&](size_t i) {
            try {
                Deadline began=Clock::now();
                repaired[i]=repair_->optimize(unique[i].moves,unique[i].boundary,repair_deadline);
                repair_ms[i]=milliseconds(began,Clock::now());
            }catch(...){errors[i]=std::current_exception();}
        };
        {
            std::vector<std::thread> threads;
            for(size_t i=1;i<unique.size();++i)threads.emplace_back(repair_one,i);
            repair_one(0);
            for(auto& thread:threads)thread.join();
        }
        for(auto& error:errors)if(error)std::rethrow_exception(error);
        size_t winner=0;
        for(size_t i=0;i<unique.size();++i) {
            result.candidate_reports.push_back({unique[i].orientation,unique[i].moves.size(),repaired[i].moves.size(),repair_ms[i]});
            if(repaired[i].moves.size()<repaired[winner].moves.size())winner=i;
            if(config.trace){TraceEvent e;e.event="candidate_repair_end";e.stage="rephase";e.before=int(unique[i].moves.size());e.after=int(repaired[i].moves.size());e.budget_ms=repair_ms[i];e.message="candidate="+std::to_string(i)+" orientation="+std::to_string(unique[i].orientation);e.sequence=format_algorithm(repaired[i].moves);config.trace(e);}
        }
        result.repaired_candidate=int(winner);
        if(!repaired[winner].stats.events.empty()){result.rephase_found_ms=repaired[winner].stats.events.back().at_ms;result.final_found_ms=milliseconds(start,repair_begin)+result.rephase_found_ms;}
        result.moves=std::move(repaired[winner].moves);result.final_focal_index=repaired[winner].boundary;result.repair_stats=std::move(repaired[winner].stats);
    }
    if(result.moves.size()>base.moves.size() || !cube.applied(result.moves).solved())throw std::logic_error("final solution failed verification");
    if(config.trace){TraceEvent e;e.event="solve_end";e.stage="solve";e.before=int(base.moves.size());e.after=int(result.moves.size());e.sequence=format_algorithm(result.moves);e.complete=true;config.trace(e);}
    Deadline finish=Clock::now();result.repair_ms=milliseconds(repair_begin,finish);result.elapsed_ms=milliseconds(start,finish);
    return result;
}
}
