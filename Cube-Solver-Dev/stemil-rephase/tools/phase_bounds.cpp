// stdin: "facelets<TAB>phase1_seq<TAB>phase2_seq" per line -> "root lb1 lb2" per line.
//   root = optimal-solver root bound (max(corner PDB, 3-axis Phase-1 distance)), whole cube
//   lb1  = Phase-1 pruning distance of the scrambled cube
//   lb2  = Phase-2 pruning bound of the G1 state reached after phase1_seq
#include "optimal.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace rephase;
int main(int argc,char** argv) {
    std::string resources=argc>1?argv[1]:"../third_party/stemil/resources";
    Tables tables(resources);CornerPdb pdb(11);OptimalSolver solver(tables,pdb);
    std::cerr<<"ready\n";
    std::string line;
    while(std::getline(std::cin,line)) {
        std::istringstream in(line);std::string f,p1,p2;std::getline(in,f,'\t');std::getline(in,p1,'\t');std::getline(in,p2,'\t');
        State s=State::from_facelets(f);Cube c0=s.coordinates();
        int lb1=tables.phase1_lower_bound(c0.corner_orientation,c0.edge_orientation,c0.UDslice_edge_position);
        State g=s.applied(parse_algorithm(p1));Cube c=g.coordinates();
        if(c.corner_orientation||c.edge_orientation||c.UDslice_edge_position)throw std::runtime_error("phase 1 did not reach G1");
        Edges ud[8],sl[4];int udp,slp;
        for(int i=0;i<8;++i)ud[i]=Edges(g.ep[i]);for(int i=0;i<4;++i)sl[i]=Edges(g.ep[i+8]);
        cubie_to_UD_edge_permutation(ud,&udp);cubie_to_UDslice_edge_permutation(sl,&slp);
        int lb2=tables.phase2_lower_bound(c.corner_permutation,udp,slp);
        if(!g.applied(parse_algorithm(p2)).solved())throw std::runtime_error("phase 2 does not solve");
        std::cout<<solver.lower_bound(s)<<' '<<lb1<<' '<<lb2<<'\n';
    }
}
