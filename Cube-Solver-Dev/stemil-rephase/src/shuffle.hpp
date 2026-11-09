#pragma once
#include "cube.hpp"
namespace rephase {
uint32_t new_shuffle_seed();
Algorithm random_scramble(int length,uint32_t seed);
}
