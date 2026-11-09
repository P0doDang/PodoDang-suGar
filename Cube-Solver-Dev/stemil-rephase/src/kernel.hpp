#pragma once
// Allocation-free, inlinable Two-Phase kernels. They reproduce the search order of
// the original stemil functions exactly (same greedy Phase-1 descent, same Phase-2
// move order) but avoid per-node overhead: no cubie conversions in the Phase-1
// descent, no std::function recursion in Phase 2, no redundant table validation.
#include "cube.hpp"
#include "coordinates.hpp"
#include "moves.hpp"
#include "pruning_tables.hpp"
#include <stdexcept>

namespace rephase {
inline int phase1_mod3(int co,int eo,int slice) {
    unsigned state=unsigned(co)*(NUM_EDGE_ORIENTATION*NUM_UDSLICE_EDGE_POSITION)+unsigned(eo)*NUM_UDSLICE_EDGE_POSITION+unsigned(slice);
    int index=int(state>>2),position=int(state&3),mask=3<<2*position;
    return (::phase1_pruning[index]&mask)>>2*position;
}
inline int phase2_bound(int cp,int ep,int slice) {
    int a=::phase2_corner_UD_edge[size_t(cp)*NUM_UD_EDGE_PERMUTATION+ep];
    int b=::phase2_corner_UDslice_edge[size_t(cp)*NUM_UDSLICE_EDGE_PERMUTATION+slice];
    int c=::phase2_UD_edge_UDslice_edge[size_t(ep)*NUM_UDSLICE_EDGE_PERMUTATION+slice];
    return a>b?(a>c?a:c):(b>c?b:c);
}
// Same result as the original phase1_search(): follow the mod-3 gradient, first
// improving move in index order.
inline void phase1_descent(int co,int eo,int slice,Algorithm& out) {
    int depth=phase1_mod3(co,eo,slice);
    while(co || eo || slice) {
        bool found=false;
        for(int m=0;m<18;++m) {
            int c=move_corner_orientation[co][m],e=move_edge_orientation[eo][m],s=move_UDslice_edge_position[slice][m];
            int value=phase1_mod3(c,e,s);
            if(value==(depth+2)%3){co=c;eo=e;slice=s;depth=value;out.push_back(m);found=true;break;}
        }
        if(!found)throw std::runtime_error("invalid existing Phase-1 gradient");
    }
}
// Edge permutation of a whole move (quarter, half or three-quarter turn), built
// from the same composition move_cube() performs.
struct EdgeSteps { int step[18][12]; EdgeSteps(); };
const EdgeSteps& edge_steps();
inline Cube fast_move(const Cube& cube,int move) {
    Cube out;const auto& steps=edge_steps();
    out.corner_orientation=move_corner_orientation[cube.corner_orientation][move];
    out.edge_orientation=move_edge_orientation[cube.edge_orientation][move];
    out.UDslice_edge_position=move_UDslice_edge_position[cube.UDslice_edge_position][move];
    out.corner_permutation=move_corner_permutation[cube.corner_permutation][move];
    for(int k=0;k<12;++k)out.cubie_edge_permutation[k]=cube.cubie_edge_permutation[steps.step[move][k]];
    return out;
}
struct Phase2Moves { int count=0;int move[10]; };
const Phase2Moves& phase2_moves_after(int previous); // previous in -1..17
// ctx supplies bool expired() and bool timed_out.
template<class Ctx> bool phase2_visit(Ctx& ctx,int c,int e,int s,int left,int previous,Algorithm& out) {
    if(ctx.expired())return false;
    if(c==0 && e==0 && s==0)return true;
    if(left==0 || phase2_bound(c,e,s)>left)return false;
    const Phase2Moves& list=phase2_moves_after(previous);
    for(int i=0;i<list.count;++i) {
        int m=list.move[i];
        out.push_back(m);
        if(phase2_visit(ctx,move_corner_permutation[c][m],move_UD_edge_permutation[e][m],move_UDslice_edge_permutation[s][m],left-1,m,out))return true;
        out.pop_back();
        if(ctx.timed_out)return false;
    }
    return false;
}
}
