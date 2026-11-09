#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "coordinates.hpp"

namespace rephase {
using Algorithm = std::vector<int>;
struct Key {
    uint64_t corners = 0, edges = 0;
    bool operator==(const Key& other) const { return corners == other.corners && edges == other.edges; }
};
uint64_t hash_key(Key key);
struct State {
    std::array<uint8_t, 8> cp{}, co{};
    std::array<uint8_t, 12> ep{}, eo{};
    static State identity();
    State then(const State& rhs) const;
    State inverse() const;
    State moved(int move) const;
    State applied(const Algorithm& algorithm) const;
    Key key() const;
    static State from_key(Key key);
    bool operator==(const State& other) const { return key() == other.key(); }
    bool operator!=(const State& other) const { return !(*this == other); }
    bool solved() const { return *this == identity(); }
    void validate() const;
    Cube coordinates() const;
    std::string facelets() const;
    static State from_facelets(const std::string& facelets);
};
const std::array<State, 18>& transformations();
Algorithm parse_algorithm(const std::string& text);
std::string format_algorithm(const Algorithm& algorithm);
Algorithm inverse_algorithm(const Algorithm& algorithm);
Algorithm normalize(const Algorithm& algorithm);
// Opposite faces commute. Keep their lexicographically first ordering.
bool canonical_next(int previous, int move);
int inverse_move(int move);
}
