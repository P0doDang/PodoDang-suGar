#include "symmetry.hpp"
#include <array>
#include <stdexcept>

namespace rephase {
namespace {
struct Vec { int x,y,z; };
// Rotation by the URF 3-fold axis: (x,y,z) -> (y,z,x); U->R->F->U.
Vec rotate(Vec v,int times) {
    for(int i=0;i<times;++i)v={v.y,v.z,v.x};
    return v;
}
// Sticker layout: U 0-8, R 9-17, F 18-26, D 27-35, L 36-44, B 45-53,
// each face read row-major as seen from outside with U on top (U: B on top;
// D: F on top).
struct Sticker { Vec position,normal; };
Sticker sticker(int index) {
    int face=index/9,r=(index%9)/3,c=index%3;
    switch(face) {
        case 0:return {{c-1,1,r-1},{0,1,0}};
        case 1:return {{1,1-r,1-c},{1,0,0}};
        case 2:return {{c-1,1-r,1},{0,0,1}};
        case 3:return {{c-1,-1,1-r},{0,-1,0}};
        case 4:return {{-1,1-r,c-1},{-1,0,0}};
        default:return {{1-c,1-r,-1},{0,0,-1}};
    }
}
int sticker_index(Vec position,Vec normal) {
    for(int i=0;i<54;++i) {
        auto s=sticker(i);
        if(s.position.x==position.x && s.position.y==position.y && s.position.z==position.z &&
           s.normal.x==normal.x && s.normal.y==normal.y && s.normal.z==normal.z)return i;
    }
    throw std::logic_error("sticker not found");
}
int facelet_face(Vec normal) {
    if(normal.y==1)return 0;
    if(normal.x==1)return 1;
    if(normal.z==1)return 2;
    if(normal.y==-1)return 3;
    if(normal.x==-1)return 4;
    return 5;
}
// Move faces are ordered R F L B U D (see parse_algorithm).
constexpr Vec move_normal[6]={{1,0,0},{0,0,1},{-1,0,0},{0,0,-1},{0,1,0},{0,-1,0}};
int move_face(Vec normal) {
    for(int f=0;f<6;++f)
        if(move_normal[f].x==normal.x && move_normal[f].y==normal.y && move_normal[f].z==normal.z)return f;
    throw std::logic_error("invalid normal");
}
}
Orientation orientation_at(int index) {
    if(index<0 || index>=max_orientations)throw std::invalid_argument("orientation index must be 0..11");
    return {(index%6)/2,index%2==1,index>=6};
}
int mirror_move(int move) {
    if(move<0 || move>=18)throw std::invalid_argument("move must be in [0,18)");
    Vec n=move_normal[move/3];
    return 3*move_face({-n.x,n.y,n.z})+2-move%3;
}
Algorithm mirror_algorithm(const Algorithm& algorithm) {
    Algorithm out;out.reserve(algorithm.size());
    for(int m:algorithm)out.push_back(mirror_move(m));
    return out;
}
State mirror(const State& cube) {
    static const std::string letters="URFDLB";
    std::string in=cube.facelets(),out(54,'?');
    auto reflect=[](Vec v){return Vec{-v.x,v.y,v.z};};
    for(int i=0;i<54;++i) {
        auto s=sticker(i);
        int target=sticker_index(reflect(s.position),reflect(s.normal));
        Vec normal=sticker(int(letters.find(in[i]))*9+4).normal;
        out[target]=letters[facelet_face(reflect(normal))];
    }
    return State::from_facelets(out);
}
int rotate_move(int move,int times) {
    if(move<0 || move>=18)throw std::invalid_argument("move must be in [0,18)");
    times=((times%3)+3)%3;
    return 3*move_face(rotate(move_normal[move/3],times))+move%3;
}
Algorithm rotate_algorithm(const Algorithm& algorithm,int times) {
    Algorithm out;out.reserve(algorithm.size());
    for(int m:algorithm)out.push_back(rotate_move(m,times));
    return out;
}
State conjugate(const State& cube,int times) {
    times=((times%3)+3)%3;
    if(times==0)return cube;
    static const std::string letters="URFDLB";
    std::string in=cube.facelets(),out(54,'?');
    for(int i=0;i<54;++i) {
        auto s=sticker(i);
        int target=sticker_index(rotate(s.position,times),rotate(s.normal,times));
        int color=letters.find(in[i]);
        // The colour of a sticker is the face whose centre carries it.
        Vec normal=sticker(color*9+4).normal;
        out[target]=letters[facelet_face(rotate(normal,times))];
    }
    return State::from_facelets(out);
}
State search_state(const State& cube,Orientation orientation) {
    State s=conjugate(orientation.inverse?cube.inverse():cube,orientation.rotation);
    return orientation.mirror?mirror(s):s;
}
Algorithm original_solution(const Algorithm& found,Orientation orientation) {
    Algorithm unmirrored=orientation.mirror?mirror_algorithm(found):found;
    Algorithm back=rotate_algorithm(unmirrored,3-orientation.rotation);
    return orientation.inverse?inverse_algorithm(back):back;
}
int original_boundary(int found_boundary,size_t length,Orientation orientation) {
    return orientation.inverse?int(length)-found_boundary:found_boundary;
}
}
