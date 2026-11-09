#include "tables.hpp"
#include "cube.hpp"
#include "moves.hpp"
#include "pruning_tables.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace rephase {
namespace {
struct FileCloser { void operator()(FILE* file) const { std::fclose(file); } };
template<size_t N> void read_moves(const std::filesystem::path& path,int (&table)[N][18],int maximum,bool phase2=false) {
    std::ifstream in(path);if(!in)throw std::runtime_error("missing existing stemil moving table: "+path.string());
    for(size_t s=0;s<N;++s)for(int m=0;m<18;++m) {
        if(phase2 && m<12 && m%3!=1)continue;
        if(!(in>>table[s][m]) || table[s][m]<0 || table[s][m]>=maximum)
            throw std::runtime_error("invalid existing stemil moving table: "+path.string());
    }
    std::string extra;if(in>>extra)throw std::runtime_error("extra data in moving table: "+path.string());
}
}
uint64_t read_stemil_ascii(const std::filesystem::path& path,uint8_t* output,size_t entries,bool phase1,bool progress) {
    auto error=[&](const std::string& message){return std::runtime_error(message+": "+path.string());};
    if(entries==0 || !output)throw error("invalid read-only table destination");
    std::unique_ptr<FILE,FileCloser> in(std::fopen(path.c_str(),"rb"));
    if(!in)throw error("missing existing stemil pruning table; no generation is performed");
    std::array<unsigned char,1024*1024> buffer{};
    size_t count=0;unsigned value=0;bool digits=false;uint64_t hash=14695981039346656037ULL;
    auto last=std::chrono::steady_clock::now();
    auto emit=[&] {
        if(count>=entries)throw error("extra pruning entries");
        if(phase1 ? (value & (value>>1) & 0x55)!=0 : value>18)
            throw error("unfinished or invalid pruning value");
        if(count==0 && (phase1?(value&3)!=0:value!=0))throw error("invalid pruning goal");
        output[count++]=uint8_t(value);hash^=value;hash*=1099511628211ULL;digits=false;value=0;
    };
    while(size_t n=std::fread(buffer.data(),1,buffer.size(),in.get())) {
        for(size_t i=0;i<n;++i) {
            unsigned char c=buffer[i];
            if(c>='0' && c<='9') {
                digits=true;value=value*10+c-'0';if(value>255)throw error("pruning byte outside 0..255");
            }else if(c==' ' || c=='\n' || c=='\r' || c=='\t') { if(digits)emit(); }
            else throw error("invalid pruning text");
        }
        auto now=std::chrono::steady_clock::now();
        if(progress && now-last>=std::chrono::seconds(10)) {
            std::cerr<<"Loading existing stemil "<<path.filename().string()<<": "<<count<<"/"<<entries<<" entries\n";last=now;
        }
    }
    if(std::ferror(in.get()))throw error("unable to read existing pruning table");
    if(digits)emit();
    if(count!=entries)throw error("truncated pruning table");
    if(progress)std::cerr<<"Loaded existing stemil "<<path.filename().string()<<": "<<count<<" entries, fingerprint "<<hash<<"\n";
    return hash;
}
Tables::Tables(std::filesystem::path resources,bool progress):resources_(std::filesystem::absolute(resources).lexically_normal()) {
    // Preflight every resource before reading. No upstream generation entry point
    // is called, even when files are missing. All outputs remain in this project.
    for(const auto& name:{"phase1_pruning","phase2_corner_UD_edge","phase2_corner_UDslice_edge","phase2_UD_edge_UDslice_edge"})
        if(!std::filesystem::is_regular_file(resources_/"pruning_tables"/name))
            throw std::runtime_error("missing existing stemil pruning table: "+(resources_/"pruning_tables"/name).string()+"; existing stemil files must be supplied; they are never regenerated");
    transformations();auto moving=resources_/"moving_tables";
    read_moves(moving/"move_corner_orientation",move_corner_orientation,2187);
    read_moves(moving/"move_edge_orientation",move_edge_orientation,2048);
    read_moves(moving/"move_UDslice_edge_position",move_UDslice_edge_position,495);
    read_moves(moving/"move_corner_permutation",move_corner_permutation,40320);
    read_moves(moving/"move_UD_edge_permutation",move_UD_edge_permutation,40320,true);
    read_moves(moving/"move_UDslice_edge_permutation",move_UDslice_edge_permutation,24,true);
    // Storage and pruning lookup functions are the original implementation,
    // compiled directly from ../third_party/stemil/src without any patch.
    auto pruning=resources_/"pruning_tables";
    read_stemil_ascii(pruning/"phase1_pruning",reinterpret_cast<uint8_t*>(::phase1_pruning),NUM_PHASE1_PRUNING_TABLE,true,progress);
    read_stemil_ascii(pruning/"phase2_corner_UD_edge",::phase2_corner_UD_edge,NUM_PHASE2_CORNER_UD_EDGE,false,progress);
    read_stemil_ascii(pruning/"phase2_corner_UDslice_edge",::phase2_corner_UDslice_edge,NUM_PHASE2_CORNER_UDSLICE_EDGE,false,progress);
    read_stemil_ascii(pruning/"phase2_UD_edge_UDslice_edge",::phase2_UD_edge_UDslice_edge,NUM_PHASE2_UD_EDGE_UDSLICE_EDGE,false,progress);
    // Touch every page once, sequentially. If the OS swapped part of a table out while the
    // later tables were loading, this pays the page-in cost during setup instead of as random
    // faults inside a timed search.
    volatile unsigned char sink=0;unsigned char sum=0;
    auto touch=[&](const void* data,size_t bytes){auto p=static_cast<const volatile unsigned char*>(data);for(size_t i=0;i<bytes;i+=4096)sum^=p[i];};
    touch(::phase1_pruning,sizeof(::phase1_pruning));touch(::phase2_corner_UD_edge,sizeof(::phase2_corner_UD_edge));
    touch(::phase2_corner_UDslice_edge,sizeof(::phase2_corner_UDslice_edge));touch(::phase2_UD_edge_UDslice_edge,sizeof(::phase2_UD_edge_UDslice_edge));
    sink=sum;(void)sink;
}
int Tables::phase1_lower_bound(int co,int eo,int slice) const {
    int depth=0;
    while(co || eo || slice) {
        if(depth>=12)throw std::runtime_error("invalid existing Phase-1 gradient");
        int value=phase1_mod3(co,eo,slice);bool found=false;
        for(int m=0;m<18 && !found;++m) {
            int c=move_corner_orientation[co][m],e=move_edge_orientation[eo][m],s=move_UDslice_edge_position[slice][m];
            if(phase1_mod3(c,e,s)==(value+2)%3) { co=c;eo=e;slice=s;found=true; }
        }
        if(!found)throw std::runtime_error("invalid existing Phase-1 gradient");
        ++depth;
    }
    return depth;
}
int Tables::phase1_mod3(int co,int eo,int slice) const {
    return ::get_phase1_pruning_value(::get_phase1_state(co,eo,slice));
}
int Tables::phase2_lower_bound(int cp,int ep,int slice) const {
    return ::get_phase2_pruning_value(cp,ep,slice);
}
size_t Tables::allocated_bytes() const {
    return sizeof(::phase1_pruning)+sizeof(::phase2_corner_UD_edge)+sizeof(::phase2_corner_UDslice_edge)+sizeof(::phase2_UD_edge_UDslice_edge);
}
size_t Tables::phase1_bytes() const {
    return sizeof(::phase1_pruning)+sizeof(::move_corner_orientation)+sizeof(::move_edge_orientation)+sizeof(::move_UDslice_edge_position);
}
size_t Tables::phase2_bytes() const {
    return sizeof(::phase2_corner_UD_edge)+sizeof(::phase2_corner_UDslice_edge)+sizeof(::phase2_UD_edge_UDslice_edge)+sizeof(::move_corner_permutation)+sizeof(::move_UD_edge_permutation)+sizeof(::move_UDslice_edge_permutation);
}
}
