#pragma once
#include "cube.hpp"
#include <vector>

namespace rephase {
// One of six equivalent search problems for the same cube: conjugation by the
// URF 3-fold rotation applied `rotation` times (0..2), optionally on the inverse
// cube. Each shares the same pruning tables but makes Phase 1 target a different
// face axis of the original cube, so the Two-Phase searches produce different
// candidate solutions.
struct Orientation {
    int rotation=0;
    bool inverse=false;
    // Left-right reflection after the rotation. It keeps the Phase-1 axis, so it does
    // not change the subgroup, but the mirrored search visits moves in a different
    // order and therefore produces different candidates within a time limit.
    bool mirror=false;
};
constexpr int max_orientations=12;
// Preferred order: identity, inverse, URF, URF inverse, URF^2, URF^2 inverse,
// then the same six mirrored (indices 6..11).
Orientation orientation_at(int index);
// Face permutation of move `move` under the URF rotation applied `times` times.
int rotate_move(int move,int times);
Algorithm rotate_algorithm(const Algorithm& algorithm,int times);
// Left-right reflection: R<->L, every turn direction reversed.
int mirror_move(int move);
Algorithm mirror_algorithm(const Algorithm& algorithm);
State mirror(const State& cube);
// Cube conjugated by the rotation (whole-cube relabelling of stickers).
State conjugate(const State& cube,int times);
// Cube that the Two-Phase search actually solves for this orientation.
State search_state(const State& cube,Orientation orientation);
// Solution for the original cube from a solution of `search_state`.
Algorithm original_solution(const Algorithm& found,Orientation orientation);
// Phase boundary index within original_solution() for a boundary in `found`.
int original_boundary(int found_boundary,size_t length,Orientation orientation);
}
