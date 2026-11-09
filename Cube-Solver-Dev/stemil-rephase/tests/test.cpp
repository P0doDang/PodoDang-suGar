#include "solver.hpp"
#include "shuffle.hpp"
#include "symmetry.hpp"
#include "kernel.hpp"
#include "search.hpp"
#include "moves.hpp"
#include "pruning_tables.hpp"
#include <algorithm>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <unistd.h>

using namespace rephase;
namespace {
void require(bool ok,const std::string& reason) { if(!ok)throw std::runtime_error(reason); }
Deadline later(double ms=5000) { return Clock::now()+std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(ms)); }
template<class F> void rejects(F fn) { bool threw=false;try{fn();}catch(const std::invalid_argument&){threw=true;}require(threw,"invalid input accepted"); }
struct Hash { size_t operator()(Key k) const { return hash_key(k); } };
void symmetry_test() {
    std::mt19937 rng(6);const auto id=State::identity();
    for(int i=0;i<300;++i) {
        Algorithm a;int n=1+rng()%25;for(int j=0;j<n;++j)a.push_back(rng()%18);
        State cube=id.applied(a);
        for(int k=0;k<3;++k)require(conjugate(cube,k)==id.applied(rotate_algorithm(a,k)),"conjugation equals relabelled moves");
        require(conjugate(conjugate(cube,1),2)==cube && conjugate(conjugate(cube,2),1)==cube,"rotation inverse");
        require(mirror(cube)==id.applied(mirror_algorithm(a)),"mirror equals reflected moves");
        require(mirror(mirror(cube))==cube,"mirror is an involution");
        for(int index=0;index<max_orientations;++index) {
            Orientation o=orientation_at(index);
            State searched=search_state(cube,o);searched.validate();
            Algorithm rotated=rotate_algorithm(o.inverse?inverse_algorithm(a):a,o.rotation);
            Algorithm found=inverse_algorithm(o.mirror?mirror_algorithm(rotated):rotated);
            require(searched.applied(found).solved(),"constructed search-state solution");
            Algorithm back=original_solution(found,o);
            require(cube.applied(back).solved() && back.size()==found.size(),"solution maps back to the original cube");
            require(original_boundary(int(found.size())/2,found.size(),o)>=0,"boundary maps");
        }
    }
    require(rotate_move(0,1)!=0 && rotate_move(rotate_move(rotate_move(5,1),1),1)==5,"move rotation has order three");
    rejects([&]{orientation_at(12);});rejects([&]{rotate_move(18,0);});
}
void kernel_test(const Tables& tables) {
    std::mt19937 rng(99);const auto id=State::identity();
    // Phase-1 descent must equal the original phase1_search for every cube.
    for(int i=0;i<400;++i) {
        Algorithm a;int n=1+rng()%25;for(int j=0;j<n;++j)a.push_back(rng()%18);
        Cube c=id.applied(a).coordinates();
        std::vector<int> original;::phase1_search(c.corner_orientation,c.edge_orientation,c.UDslice_edge_position,&original);
        Algorithm fast;phase1_descent(c.corner_orientation,c.edge_orientation,c.UDslice_edge_position,fast);
        require(Algorithm(original.begin(),original.end())==fast,"phase-1 descent equals original phase1_search");
        require(phase1_mod3(c.corner_orientation,c.edge_orientation,c.UDslice_edge_position)==tables.phase1_mod3(c.corner_orientation,c.edge_orientation,c.UDslice_edge_position),"inline phase-1 lookup");
    }
    // fast_move must equal upstream move_cube on all fields.
    Cube c=id.coordinates(),d=c;
    for(int i=0;i<2000;++i) {
        int m=rng()%18;c=move_cube(c,m);d=fast_move(d,m);
        bool same=c.corner_orientation==d.corner_orientation && c.edge_orientation==d.edge_orientation && c.UDslice_edge_position==d.UDslice_edge_position && c.corner_permutation==d.corner_permutation;
        for(int k=0;k<12;++k)same=same && c.cubie_edge_permutation[k]==d.cubie_edge_permutation[k];
        require(same,"fast_move equals upstream move_cube");
    }
    // Phase 2: compare against the previous std::function implementation.
    struct Ctx { bool expired(){return false;} bool timed_out=false; } ctx;
    auto p2move=[](int m){return m>=12 || m%3==1;};
    for(int i=0;i<300;++i) {
        int cp=0,ep=0,sl=0;Algorithm scramble;int prev=-1;
        for(int j=0,n=2+rng()%8;j<n;) { int m=rng()%18;if(p2move(m) && canonical_next(prev,m)){cp=move_corner_permutation[cp][m];ep=move_UD_edge_permutation[ep][m];sl=move_UDslice_edge_permutation[sl][m];prev=m;++j;} }
        std::function<bool(int,int,int,int,int,Algorithm&)> visit=[&](int cc,int e,int s,int left,int previous,Algorithm& out) {
            if(cc==0 && e==0 && s==0)return true;
            if(left==0 || tables.phase2_lower_bound(cc,e,s)>left)return false;
            for(int m=0;m<18;++m)if(p2move(m) && canonical_next(previous,m)) {
                out.push_back(m);
                if(visit(move_corner_permutation[cc][m],move_UD_edge_permutation[e][m],move_UDslice_edge_permutation[s][m],left-1,m,out))return true;
                out.pop_back();
            }
            return false;
        };
        Algorithm expected,actual;bool found=false;
        for(int depth=tables.phase2_lower_bound(cp,ep,sl);depth<=18 && !found;++depth){expected.clear();found=visit(cp,ep,sl,depth,-1,expected);}
        found=false;
        for(int depth=phase2_bound(cp,ep,sl);depth<=18 && !found;++depth){actual.clear();found=phase2_visit(ctx,cp,ep,sl,depth,-1,actual);}
        require(found && expected==actual,"phase-2 kernel equals the previous implementation");
        require(phase2_bound(cp,ep,sl)==tables.phase2_lower_bound(cp,ep,sl),"inline phase-2 bound");
    }
}
void existing_loader_test() {
    char temporary[]="/tmp/rephase-read-only-loader-XXXXXX";
    char* created=mkdtemp(temporary);require(created!=nullptr,"create loader fixture");
    std::filesystem::path directory(created),file=directory/"table";
    struct Cleanup { std::filesystem::path p;~Cleanup(){std::filesystem::remove(p/"table");std::filesystem::remove(p);} } cleanup{directory};
    auto write=[&](const std::string& text){std::ofstream out(file);out<<text;};
    uint8_t bytes[4]{};write("0\n1 2\r\n18");
    auto before=std::filesystem::last_write_time(file);
    read_stemil_ascii(file,bytes,4,false);
    require(bytes[0]==0 && bytes[1]==1 && bytes[2]==2 && bytes[3]==18,"existing ASCII table loads exactly");
    require(std::filesystem::last_write_time(file)==before,"read-only loader preserves source");
    auto rejected=[&](const std::string& text,bool packed=false){
        write(text);bool threw=false;try{read_stemil_ascii(file,bytes,4,packed);}catch(const std::runtime_error&){threw=true;}
        require(threw,"malformed or incomplete existing table must be rejected");
    };
    rejected("0 1 2");rejected("0 1 2 3 4");rejected("1 1 2 3");
    rejected("0 1 2 255");rejected("0 1 2 -1");rejected("0 1 2 9999999999");rejected("0 1 2 a");
    rejected("0 0 0 255",true);write("0 85 170 84");read_stemil_ascii(file,bytes,4,true);
    bool missing=false;try{Tables unavailable(directory);}catch(const std::runtime_error&){missing=true;}
    require(missing && !std::filesystem::exists(directory/"pruning_tables"),"missing tables never trigger generation or fallback");
}
void existing_pruning_test(const Tables& tables) {
    require(tables.allocated_bytes()==2181911040ULL,"four complete original pruning tables used");
    // Independent shallow BFS proves the exact distances/modulo-3 values of
    // queried projections. It does not create or alter any resource table.
    struct Item {int a,b,c,d;};
    auto verify=[&](bool phase1,int projection) {
        std::vector<Item> queue={{0,0,0,0}};std::unordered_map<uint64_t,int> distances={{0,0}};
        auto rank=[&](int a,int b,int c){return phase1?(uint64_t(a)*2048+b)*495+c:uint64_t(a)*(projection==0?40320:24)+b;};
        for(size_t head=0;head<queue.size();++head) {
            auto x=queue[head];int actual;
            if(phase1)actual=tables.phase1_mod3(x.a,x.b,x.c);
            else if(projection==0)actual=::phase2_corner_UD_edge[uint64_t(x.a)*40320+x.b];
            else if(projection==1)actual=::phase2_corner_UDslice_edge[x.a*24+x.b];
            else actual=::phase2_UD_edge_UDslice_edge[x.a*24+x.b];
            require(actual==(phase1?x.d%3:x.d),"existing pruning data agrees with independent shallow projection BFS");
            if(phase1)require(tables.phase1_lower_bound(x.a,x.b,x.c)==x.d,"original Phase-1 gradient gives exact shallow distance");
            if(x.d==4)continue;
            for(int m=0;m<18;++m) {
                if(!phase1 && m<12 && m%3!=1)continue;
                int a,b,c=0;
                if(phase1){a=move_corner_orientation[x.a][m];b=move_edge_orientation[x.b][m];c=move_UDslice_edge_position[x.c][m];}
                else {a=projection==2?move_UD_edge_permutation[x.a][m]:move_corner_permutation[x.a][m];b=projection==0?move_UD_edge_permutation[x.b][m]:move_UDslice_edge_permutation[x.b][m];}
                if(distances.emplace(rank(a,b,c),x.d+1).second)queue.push_back({a,b,c,x.d+1});
            }
        }
    };
    verify(true,0);for(int projection=0;projection<3;++projection)verify(false,projection);
    std::mt19937 random(241);
    for(int i=0;i<1000;++i) {
        int c=random()%40320,e=random()%40320,s=random()%24;
        int expected=std::max({int(::phase2_corner_UD_edge[uint64_t(c)*40320+e]),int(::phase2_corner_UDslice_edge[c*24+s]),int(::phase2_UD_edge_UDslice_edge[e*24+s])});
        require(tables.phase2_lower_bound(c,e,s)==expected,"original max combines all three full Phase-2 projections");
    }
}

}
int main() {
    try {
        const auto id=State::identity();require(id.facelets()=="UUUUUUUUURRRRRRRRRFFFFFFFFFDDDDDDDDDLLLLLLLLLBBBBBBBBB","identity facelets");
        for(int m=0;m<18;++m) {
            auto s=id.moved(m);s.validate();require(s.moved(inverse_move(m))==id,"move/inverse");
            require(s.then(s.inverse())==id && s.inverse().then(s)==id,"group inverse");
            require(State::from_facelets(s.facelets())==s,"single move facelet roundtrip");
        }
        // Independent expected sticker examples for the clockwise U and F moves.
        auto u=id.moved(12).facelets();require(u.substr(9,3)=="BBB" && u.substr(18,3)=="RRR" && u.substr(36,3)=="FFF","U sticker convention");
        auto f=id.moved(3).facelets();require(f.substr(6,3)=="LLL" && f[9]=='U' && f[12]=='U' && f[15]=='U',"F sticker convention");
        rejects([&]{parse_algorithm("R3");});rejects([&]{parse_algorithm("X");});rejects([&]{State::from_facelets("bad");});
        auto invalid=id;std::swap(invalid.ep[0],invalid.ep[1]);rejects([&]{invalid.validate();});
        invalid=id;invalid.co[0]=1;rejects([&]{invalid.validate();});
        invalid=id;invalid.eo[0]=1;rejects([&]{invalid.validate();});
        invalid=id;invalid.cp[0]=invalid.cp[1];rejects([&]{invalid.validate();});
        std::mt19937 rng(20260917);State state=id;
        for(int i=0;i<1000;++i) {
            state=state.moved(rng()%18);state.validate();
            require(State::from_key(state.key())==state,"lossless key");
            require(State::from_facelets(state.facelets())==state,"facelet roundtrip");
            require(state.then(state.inverse())==id,"random state inverse");
            Algorithm a,b;for(int j=0;j<12;++j){a.push_back(rng()%18);b.push_back(rng()%18);}
            require(id.applied(a).then(id.applied(b))==id.applied(a).applied(b),"composition convention");
            auto normalized=normalize(a);require(normalized.size()<=a.size() && id.applied(normalized)==id.applied(a),"normalization equivalence");
        }
        auto generated=random_scramble(25,2026);
        require(generated==random_scramble(25,2026) && generated!=random_scramble(25,2027),"shuffle seed reproduces a scramble");
        require(generated.size()==25,"automatic scramble length");
        for(size_t i=1;i<generated.size();++i)require(canonical_next(generated[i-1],generated[i]),"automatic scramble move constraints");
        id.applied(generated).validate();rejects([&]{random_scramble(0,1);});rejects([&]{random_scramble(1001,1);});
        symmetry_test();
        existing_loader_test();
        Tables tables("../third_party/stemil/resources");
        existing_pruning_test(tables);
        kernel_test(tables);
        state=id;
        for(int i=0;i<1000;++i) {
            int m=rng()%18;auto coordinate_move=move_cube(state.coordinates(),m);state=state.moved(m);auto expected=state.coordinates();
            require(coordinate_move.corner_orientation==expected.corner_orientation && coordinate_move.edge_orientation==expected.edge_orientation && coordinate_move.corner_permutation==expected.corner_permutation && coordinate_move.UDslice_edge_position==expected.UDslice_edge_position,"upstream coordinate moves agree with group state");
            for(int j=0;j<12;++j)require(coordinate_move.cubie_edge_permutation[j]==expected.cubie_edge_permutation[j],"upstream edge permutation");
        }
        Repair repair;require(repair.states()==46741,"complete radius-4 HTM database count");
        require(tables.phase1_bytes()>0 && tables.phase2_bytes()>0 && tables.phase1_bytes()+tables.phase2_bytes()>tables.allocated_bytes(),"phase-specific pruning and move-table memory accounting");
        // Independent unpruned breadth-first oracle, including duplicate paths.
        std::unordered_map<Key,int,Hash> distances;std::vector<State> queue={id};distances[id.key()]=0;
        for(size_t head=0;head<queue.size();++head) {
            State s=queue[head];int d=distances[s.key()];if(d==3)continue;
            for(int m=0;m<18;++m) { auto next=s.moved(m);if(distances.emplace(next.key(),d+1).second)queue.push_back(next); }
        }
        require(distances.size()==3502,"independent radius-3 state count");
        for(auto [k,d]:distances) {
            auto found=repair.exact(State::from_key(k),3,later());require(found.complete && found.replacement && int(found.replacement->size())==d,"exact search vs BFS oracle");
            if(d>0) { auto none=repair.exact(State::from_key(k),d-1,later());require(none.complete && !none.replacement,"exact lower-bound proof"); }
        }
        for(int radius: {0,2}) {
            RepairConfig alternate;alternate.table_depth=radius;Repair other(alternate);
            for(int i=0;i<20;++i) {
                Algorithm a;for(int j=0;j<4;++j)a.push_back(rng()%18);State t=id.applied(a);
                auto expected=repair.exact(t,4,later());auto actual=other.exact(t,4,later());
                require(actual.complete && actual.replacement && actual.replacement->size()==expected.replacement->size(),"bidirectional search agrees across database radii");
            }
        }
        for(int i=0;i<80;++i) {
            Algorithm a;for(int j=0;j<5;++j)a.push_back(rng()%18);
            auto t=id.applied(a);auto found=repair.exact(t,5,later());
            require(found.complete && found.replacement && found.replacement->size()<=a.size() && id.applied(*found.replacement)==t,"bidirectional depth-5 replacement");
        }
        auto timed=repair.exact(id.moved(0),9,Clock::now());require(timed.timed_out && !timed.complete,"timeout must not be a negative proof");
        RepairConfig capped;capped.memory_limit_bytes=1024;rejects([&]{Repair small(capped);});
        auto redundant=parse_algorithm("R U D U' R' F2");auto optimized=repair.optimize(redundant,3,later());
        require(id.applied(optimized.moves)==id.applied(redundant) && optimized.moves.size()<redundant.size() && !optimized.stats.events.empty(),"verified shortening splice");
        auto unchanged=repair.optimize(redundant,3,Clock::now());require(unchanged.moves==redundant && unchanged.stats.deadline_reached,"deadline retains input");
        rejects([&]{repair.optimize(redundant,99,later());});
        // Inefficiency is far from the supplied phase boundary. Only whole-
        // sequence sliding can reach it with the requested four-move window.
        RepairConfig narrow;narrow.windows={4};narrow.repeat=false;narrow.mode=RepairMode::Boundary;
        auto distant=parse_algorithm("R R' U F L B D");Repair boundary_only(narrow);
        auto boundary_result=boundary_only.optimize(distant,7,later());require(boundary_result.moves==distant && boundary_result.stats.boundary.windows_tested==1,"boundary-only checks end window and leaves distant window untouched");
        RepairConfig end_config;end_config.windows={2};end_config.mode=RepairMode::Boundary;
        Repair end_repair(end_config);auto end_redundant=parse_algorithm("U F R R'");
        auto end_result=end_repair.optimize(end_redundant,4,later());
        require(end_result.moves==parse_algorithm("U F") && end_result.stats.boundary.moves_saved==2 && id.applied(end_result.moves)==id.applied(end_redundant),"boundary at sequence end can shorten its adjacent window");
        narrow.mode=RepairMode::BoundaryAndSliding;Repair both(narrow);std::vector<TraceEvent> captured;
        auto full_result=both.optimize(distant,7,later(),[&](const TraceEvent& e){captured.push_back(e);});
        require(full_result.moves==parse_algorithm("U F L B D") && id.applied(full_result.moves)==id.applied(distant),"sliding repairs outside boundary and preserves transform");
        require(full_result.stats.events.size()==1 && full_result.stats.events[0].stage==RepairStage::Sliding && full_result.stats.sliding.moves_saved==2,"no-repeat and stage accounting");
        require(std::count_if(captured.begin(),captured.end(),[](const TraceEvent& e){return e.event=="repair_applied" && e.stage=="sliding";})==1,"trace records actual sliding replacement");
        narrow.repeat=true;Repair cached(narrow);auto irreducible=parse_algorithm("R U F L");
        auto cached_result=cached.optimize(irreducible,2,later());
        require(cached_result.moves==irreducible && cached_result.stats.sliding.cache_hits==1,"completed work shared across stages");
        narrow.mode=RepairMode::Sliding;Repair sliding_only(narrow);
        auto sliding_result=sliding_only.optimize(distant,7,later());require(sliding_result.stats.boundary.windows_tested==0 && sliding_result.moves==full_result.moves,"sliding-only mode");
        RepairConfig override_config=end_repair.config();override_config.windows={4};override_config.mode=RepairMode::Sliding;override_config.repeat=false;
        auto override_result=end_repair.optimize_with(distant,7,override_config,later());
        require(override_result.moves==parse_algorithm("U F L B D") && override_result.stats.sliding.windows_tested>0,"per-run window override reuses one repair database");
        for(int i=0;i<20;++i){Algorithm a;for(int j=0;j<14;++j)a.push_back(rng()%18);auto result=both.optimize(a,rng()%15,later(50));require(result.moves.size()<=a.size() && id.applied(result.moves)==id.applied(a),"random sliding splice invariants");}
        RepairConfig sorted;sorted.windows={8,4,6,4};Repair ordered(sorted);require(ordered.config().windows==std::vector<int>({4,6,8}),"window priority sorted and deduplicated");
        sorted.windows={11};rejects([&]{Repair invalid_window(sorted);});
        Solver solver(tables,&repair);
        auto solved=solver.solve(id);require(solved.solved && solved.moves.empty(),"solved cube");
        auto r2=solver.solve(id.moved(1),{100,0,false,std::nullopt,{}});require(r2.solved && r2.moves==parse_algorithm("R2"),"first R2 phase-2 move is permitted");
        for(int i=0;i<30;++i) {
            Algorithm a;for(int j=0;j<6;++j)a.push_back(rng()%18);state=id.applied(a);
            auto found=solver.solve(state,{100,0.1,false,std::nullopt,{}});
            require(found.solved && state.applied(found.moves).solved() && found.moves.size()<=found.initial_moves.size(),"end-to-end solution verification");
            require(found.elapsed_ms<120,"warmed budget overrun");
            require(phase_boundary(state,found.initial_moves)==found.initial_boundary,"subgroup boundary");
        }
        auto almost_no_time=solver.solve(id.applied(parse_algorithm("R U F B L D R U F2")),{0.00001,0,false,std::nullopt,{}});
        require(!almost_no_time.solved && almost_no_time.two_phase_timed_out,"initial search honors deadline");
        {
            // 6-way path: every orientation subset and candidate count must give a verified solution
            // no longer than its own initial solution, and 1/1 must reproduce the single-thread path.
            std::mt19937 multi_rng(77);
            for(int i=0;i<6;++i) {
                auto scramble=random_scramble(20,uint32_t(500+i));State state=id.applied(scramble);
                for(auto [orientations,candidates]:{std::pair<int,int>{1,1},{2,1},{6,1},{6,4},{3,6},{12,1},{8,3}}) {
                    SolveConfig config{150,0.2,false,std::nullopt,{}};config.orientations=orientations;config.candidates=candidates;
                    auto found=solver.solve(state,config);
                    require(found.solved && state.applied(found.moves).solved() && state.applied(found.initial_moves).solved(),"6-way solution verification");
                    require(found.moves.size()<=found.initial_moves.size(),"6-way repair never lengthens");
                    if(orientations>1 || candidates>1) {
                        require(int(found.orientation_reports.size())==orientations && found.orientations==orientations,"per-orientation reports");
                        size_t best=99;for(const auto& o:found.orientation_reports)if(o.solved)best=std::min(best,o.htm);
                        require(found.initial_moves.size()<=best,"6-way base is at most the best orientation");
                        require(found.candidate_reports.size()<=size_t(candidates) && !found.candidate_reports.empty(),"candidate reports");
                    } else require(found.orientation_reports.empty(),"1/1 uses the single-thread path");
                }
            }
            // Kociemba-style Phase-1 enumeration: verified, never longer than its first solution,
            // single- and multi-orientation.
            for(int i=0;i<8;++i) {
                auto scramble=random_scramble(i<4?8:22,uint32_t(900+i));State state=id.applied(scramble);
                for(int orientations:{1,6}) {
                    SolveConfig config{150,0.1,false,std::nullopt,{}};config.orientations=orientations;config.phase1_ida=true;
                    auto found=solver.solve(state,config);
                    require(found.solved && state.applied(found.moves).solved() && found.moves.size()<=found.initial_moves.size(),"phase-1 IDA solution verification");
                    if(i<4)require(found.moves.size()<=8,"phase-1 IDA finds a solution no longer than a short scramble");
                }
            }
            rejects([&]{SolveConfig bad{100,0,false,std::nullopt,{}};bad.orientations=13;solver.solve(id.moved(1),bad);});
            rejects([&]{SolveConfig bad{100,0.1,true,std::nullopt,{}};bad.orientations=2;solver.solve(id.moved(1),bad);});
        }
        auto adaptive=solver.solve(id.moved(1),{100,0.1,true,std::nullopt,{}});
        require(adaptive.solved && adaptive.allocated_repair_fraction==0.2,"adaptive short-solution allocation");
        SolveConfig fixed;fixed.budget_ms=100;fixed.rephase_ms=3;captured.clear();fixed.trace=[&](const TraceEvent& e){captured.push_back(e);};
        auto fixed_result=solver.solve(id.moved(1),fixed);
        require(fixed_result.solved && fixed_result.allocated_rephase_ms==3 && fixed_result.two_phase_budget_ms==97,"fixed rephase allocation");
        require(fixed_result.phase1_ms>=0 && fixed_result.phase2_ms>=0 && fixed_result.phase1_ms+fixed_result.phase2_ms<=fixed_result.two_phase_ms+1,"phase-specific search timing");
        for(const auto& e:captured)if(e.event=="stage_start")require(e.budget_ms<=3.1,"early global finish does not enlarge rephase cap");
        fixed.rephase_ms=100;rejects([&]{solver.solve(id.moved(1),fixed);});
        fixed.rephase_ms=3;fixed.adaptive=true;rejects([&]{solver.solve(id.moved(1),fixed);});
        fixed.adaptive=false;fixed.rephase_ms=0;fixed.trace={};Solver plain(tables);require(plain.solve(id.moved(1),fixed).solved,"fixed zero disables rephase without DB");
        std::cout<<"All tests passed: existing full stemil pruning tables, read-only ASCII loader, independent projection BFS, cube group, facelets, coordinates, exact repair, sliding windows, trace, shuffle, fixed time caps, deadlines, memory cap, end-to-end solve, and 6-way orientation/candidate solving.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"TEST FAILURE: "<<e.what()<<'\n';return 1;}
}
