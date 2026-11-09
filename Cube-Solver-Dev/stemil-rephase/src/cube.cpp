#include "cube.hpp"
#include "moves.hpp"
#include <algorithm>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace rephase {
namespace {
constexpr char colors[] = "URFDLB";
constexpr int cf[8][3] = {{8,9,20},{6,18,38},{0,36,47},{2,45,11},
                         {29,26,15},{27,44,24},{33,53,42},{35,17,51}};
constexpr int ef[12][2] = {{5,10},{7,19},{3,37},{1,46},{32,16},{28,25},
                          {30,43},{34,52},{23,12},{21,41},{50,39},{48,14}};
constexpr char cc[8][3] = {{'U','R','F'},{'U','F','L'},{'U','L','B'},{'U','B','R'},
                          {'D','F','R'},{'D','L','F'},{'D','B','L'},{'D','R','B'}};
constexpr char ec[12][2] = {{'U','R'},{'U','F'},{'U','L'},{'U','B'},{'D','R'},{'D','F'},
                           {'D','L'},{'D','B'},{'F','R'},{'F','L'},{'B','L'},{'B','R'}};
template<size_t N> int parity(const std::array<uint8_t,N>& p) {
    int result=0;
    for (size_t i=0;i<N;++i) for(size_t j=i+1;j<N;++j) result ^= p[i]>p[j];
    return result;
}
template<size_t N> void permutation(const std::array<uint8_t,N>& p) {
    unsigned seen=0;
    for(auto item:p) {
        if(item>=N || (seen & (1u<<item))) throw std::invalid_argument("duplicate or invalid cubie");
        seen |= 1u<<item;
    }
}
uint64_t mix(uint64_t x) {
    x ^= x>>30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x>>27; x *= 0x94d049bb133111ebULL; return x^(x>>31);
}
}
uint64_t hash_key(Key k) { return mix(k.corners) ^ mix(k.edges + 0x9e3779b97f4a7c15ULL); }
State State::identity() {
    State s; std::iota(s.cp.begin(),s.cp.end(),0); std::iota(s.ep.begin(),s.ep.end(),0); return s;
}
State State::then(const State& b) const {
    State out;
    for(int i=0;i<8;++i) { out.cp[i]=cp[b.cp[i]]; out.co[i]=(co[b.cp[i]]+b.co[i])%3; }
    for(int i=0;i<12;++i) { out.ep[i]=ep[b.ep[i]]; out.eo[i]=eo[b.ep[i]]^b.eo[i]; }
    return out;
}
State State::inverse() const {
    State out;
    for(int i=0;i<8;++i) { out.cp[cp[i]]=i; out.co[cp[i]]=(3-co[i])%3; }
    for(int i=0;i<12;++i) { out.ep[ep[i]]=i; out.eo[ep[i]]=eo[i]; }
    return out;
}
const std::array<State,18>& transformations() {
    static const auto all=[] {
        initialize_moves(); std::array<State,18> result;
        for(int face=0;face<6;++face) {
            State q;
            for(int i=0;i<8;++i) { q.cp[i]=move_corner[face*3][i]; q.co[i]=corner_orient[face*3][i][0]; }
            for(int i=0;i<12;++i) { q.ep[i]=move_edge[face*3][i]; q.eo[i]=edge_orient[face*3][i][0]; }
            result[face*3]=q; result[face*3+1]=q.then(q); result[face*3+2]=q.then(q).then(q);
        }
        return result;
    }();
    return all;
}
State State::moved(int m) const {
    if(m<0 || m>=18) throw std::invalid_argument("move must be in [0,18)");
    return then(transformations()[m]);
}
State State::applied(const Algorithm& a) const { State s=*this; for(int m:a)s=s.moved(m); return s; }
Key State::key() const {
    Key k;
    for(int i=0;i<8;++i) k.corners |= uint64_t(cp[i] | (co[i]<<3))<<(5*i);
    for(int i=0;i<12;++i) k.edges |= uint64_t(ep[i] | (eo[i]<<4))<<(5*i);
    return k;
}
State State::from_key(Key k) {
    State s;
    for(int i=0;i<8;++i) { auto x=(k.corners>>(5*i))&31; s.cp[i]=x&7; s.co[i]=x>>3; }
    for(int i=0;i<12;++i) { auto x=(k.edges>>(5*i))&31; s.ep[i]=x&15; s.eo[i]=x>>4; }
    return s;
}
void State::validate() const {
    permutation(cp); permutation(ep);
    for(auto o:co) if(o>=3)throw std::invalid_argument("invalid corner orientation");
    for(auto o:eo) if(o>=2)throw std::invalid_argument("invalid edge orientation");
    if(std::accumulate(co.begin(),co.end(),0)%3 || std::accumulate(eo.begin(),eo.end(),0)%2)
        throw std::invalid_argument("impossible orientation sum");
    if(parity(cp)!=parity(ep))throw std::invalid_argument("impossible permutation parity");
}
Cube State::coordinates() const {
    Cube cube{}; Corners corners[8]; int corient[8],eorient[12]; bool slice[12];
    for(int i=0;i<8;++i) { corners[i]=Corners(cp[i]); corient[i]=co[i]; }
    for(int i=0;i<12;++i) { cube.cubie_edge_permutation[i]=Edges(ep[i]); eorient[i]=eo[i]; slice[i]=ep[i]>=8; }
    cubie_to_corner_permutation(corners,&cube.corner_permutation);
    cubie_to_corner_orientation(corient,&cube.corner_orientation);
    cubie_to_edge_orientation(eorient,&cube.edge_orientation);
    cubie_to_UDslice_edge_position(slice,&cube.UDslice_edge_position); return cube;
}
std::string State::facelets() const {
    validate(); std::string out(54,'?'); for(int i=0;i<6;++i)out[i*9+4]=colors[i];
    for(int i=0;i<8;++i) for(int j=0;j<3;++j) out[cf[i][(j+co[i])%3]]=cc[cp[i]][j];
    for(int i=0;i<12;++i) for(int j=0;j<2;++j) out[ef[i][(j+eo[i])%2]]=ec[ep[i]][j];
    return out;
}
State State::from_facelets(const std::string& in) {
    if(in.size()!=54)throw std::invalid_argument("facelets need 54 characters in URFDLB order");
    for(int i=0;i<6;++i) if(in[i*9+4]!=colors[i] || std::count(in.begin(),in.end(),colors[i])!=9)
        throw std::invalid_argument("facelet colors/centers must follow URFDLB, nine of each");
    State s;
    for(int i=0;i<8;++i) {
        int o=0; while(o<3 && in[cf[i][o]]!='U' && in[cf[i][o]]!='D')++o;
        if(o==3)throw std::invalid_argument("invalid corner colors");
        int p=0; while(p<8 && !(in[cf[i][(o+1)%3]]==cc[p][1] && in[cf[i][(o+2)%3]]==cc[p][2] && in[cf[i][o]]==cc[p][0]))++p;
        if(p==8)throw std::invalid_argument("invalid corner colors");
        s.cp[i]=p; s.co[i]=o;
    }
    for(int i=0;i<12;++i) {
        bool found=false;
        for(int p=0;p<12 && !found;++p)for(int o=0;o<2 && !found;++o)
            if(in[ef[i][o]]==ec[p][0] && in[ef[i][1-o]]==ec[p][1]) { s.ep[i]=p; s.eo[i]=o; found=true; }
        if(!found)throw std::invalid_argument("invalid edge colors");
    }
    s.validate(); return s;
}
Algorithm parse_algorithm(const std::string& text) {
    Algorithm a; std::istringstream stream(text); std::string token; const std::string faces="RFLBUD";
    while(stream>>token) {
        auto f=faces.find(token[0]);
        if(f==std::string::npos || token.size()>2 || (token.size()==2 && token[1]!='2' && token[1]!='\''))
            throw std::invalid_argument("invalid move: "+token);
        int p=token.size()==1?0:token[1]=='2'?1:2; a.push_back(3*f+p);
    }
    return a;
}
std::string format_algorithm(const Algorithm& a) {
    std::string out; const std::string faces="RFLBUD";
    for(int m:a) { if(m<0 || m>=18)throw std::invalid_argument("invalid move"); if(!out.empty())out+=' '; out+=faces[m/3]; if(m%3==1)out+='2'; if(m%3==2)out+='\''; }
    return out;
}
int inverse_move(int m) { return 3*(m/3)+2-m%3; }
Algorithm inverse_algorithm(const Algorithm& a) { Algorithm b; for(auto i=a.rbegin();i!=a.rend();++i)b.push_back(inverse_move(*i)); return b; }
bool canonical_next(int prev,int m) {
    if(prev<0)return true;
    int p=prev/3,f=m/3;
    if(p==f)return false;
    bool opposite=(p<4 && f<4 && (p+2)%4==f) || (p>=4 && f>=4);
    return !(opposite && f<p);
}
Algorithm normalize(const Algorithm& a) {
    Algorithm out=a; bool changed=true;
    while(changed) {
        changed=false;
        for(size_t i=1;i<out.size();++i) {
            int p=out[i-1]/3,f=out[i]/3;
            if(p==f) {
                int turn=(out[i-1]%3+out[i]%3+2)%4;
                out.erase(out.begin()+i);
                if(turn)out[i-1]=3*f+turn-1; else out.erase(out.begin()+i-1);
                changed=true;break;
            }
            if(!canonical_next(out[i-1],out[i])) { std::swap(out[i-1],out[i]);changed=true;break; }
        }
    }
    return out;
}
}
