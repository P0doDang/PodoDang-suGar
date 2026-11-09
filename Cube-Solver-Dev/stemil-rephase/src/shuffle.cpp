#include "shuffle.hpp"
#include <chrono>
#include <random>
#include <stdexcept>
#include <unistd.h>

namespace rephase {
uint32_t new_shuffle_seed() {
    uint64_t value=uint64_t(std::chrono::high_resolution_clock::now().time_since_epoch().count())^uint64_t(getpid());
    std::random_device entropy;value^=uint64_t(entropy())<<32;value^=entropy();
    value^=value>>30;value*=0xbf58476d1ce4e5b9ULL;value^=value>>27;value*=0x94d049bb133111ebULL;value^=value>>31;
    return uint32_t(value);
}
Algorithm random_scramble(int length,uint32_t seed) {
    if(length<1 || length>1000)throw std::invalid_argument("scramble length must be 1..1000");
    std::mt19937 random(seed);std::uniform_int_distribution<int> move(0,17);Algorithm result;
    result.reserve(length);
    while(int(result.size())<length) {
        int m=move(random);
        if(result.empty() || canonical_next(result.back(),m))result.push_back(m);
    }
    return result;
}
}
