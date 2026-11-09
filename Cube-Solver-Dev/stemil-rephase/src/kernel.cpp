#include "kernel.hpp"
#include <array>

namespace rephase {
EdgeSteps::EdgeSteps() {
    for(int m=0;m<18;++m) {
        int base=(m/3)*3,turns=m%3+1;
        for(int k=0;k<12;++k) { int s=k;for(int i=0;i<turns;++i)s=move_edge[base][s];step[m][k]=s; }
    }
}
const EdgeSteps& edge_steps() { static const EdgeSteps steps; return steps; }
const Phase2Moves& phase2_moves_after(int previous) {
    static const std::array<Phase2Moves,19> table=[] {
        std::array<Phase2Moves,19> result;
        for(int p=-1;p<18;++p) {
            auto& list=result[p+1];
            for(int m=0;m<18;++m)if((m>=12 || m%3==1) && canonical_next(p,m))list.move[list.count++]=m;
        }
        return result;
    }();
    return table[previous+1];
}
}
