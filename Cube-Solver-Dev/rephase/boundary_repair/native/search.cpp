// Full-state, level-synchronous bidirectional BFS in the unrestricted HTM graph.
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <new>
#include <unordered_map>
#include <vector>

using State = std::array<uint8_t, 20>;
using Clock = std::chrono::steady_clock;
struct Hash {
    size_t operator()(const State& s) const noexcept {
        uint64_t h = 14695981039346656037ULL;
        for (uint8_t v : s) { h ^= v; h *= 1099511628211ULL; }
        return static_cast<size_t>(h);
    }
};
struct Path { uint64_t code; uint8_t depth; };
using Visited = std::unordered_map<State, Path, Hash>;
extern "C" {
struct SearchResult {
    int status; // 0 FOUND, 1 EXHAUSTED, 2 TIMEOUT, 3 STATE_LIMIT
    int length;
    uint8_t moves[16];
    uint64_t generated;
    uint64_t states;
};
}

static State apply_move(const State& a, const State& b) {
    State out;
    for (int i = 0; i < 8; ++i) {
        uint8_t av = a[b[i] / 3];
        out[i] = 3 * (av / 3) + (av % 3 + b[i] % 3) % 3;
    }
    for (int i = 8; i < 20; ++i) {
        uint8_t av = a[8 + b[i] / 2];
        out[i] = (av & ~1) | ((av ^ b[i]) & 1);
    }
    return out;
}

static void write_path(SearchResult* out, Path forward, Path backward) {
    out->status = 0;
    out->length = forward.depth + backward.depth;
    for (int i = forward.depth - 1; i >= 0; --i) {
        out->moves[i] = forward.code & 31;
        forward.code >>= 5;
    }
    // Backward paths go target -> meeting; reverse and invert each turn.
    for (int i = 0; i < backward.depth; ++i) {
        int m = backward.code & 31;
        out->moves[forward.depth + i] = 3 * (m / 3) + 2 - m % 3;
        backward.code >>= 5;
    }
}

static void run_search(const uint8_t* target_bytes, const uint8_t* map_bytes,
                       int max_depth, uint64_t timeout_ns, uint64_t max_states,
                       SearchResult* out) {
    std::memset(out, 0, sizeof(*out));
    out->status = 1;
    const auto deadline = Clock::now() + std::chrono::nanoseconds(timeout_ns);
    State identity{}, target;
    for (int i = 0; i < 8; ++i) identity[i] = 3 * i;
    for (int i = 8; i < 20; ++i) identity[i] = 2 * (i - 8);
    std::memcpy(target.data(), target_bytes, 20);
    if (identity == target) { out->status = 0; out->states = 1; return; }
    if (Clock::now() >= deadline) { out->status = 2; return; }
    if (max_depth == 0) return;
    if (max_states < 2) { out->status = 3; return; }
    std::array<State, 18> maps;
    for (int m = 0; m < 18; ++m) std::memcpy(maps[m].data(), map_bytes + 20 * m, 20);
    Visited visited[2];
    std::vector<State> frontier[2];
    visited[0].reserve(1024); visited[1].reserve(1024);
    visited[0].emplace(identity, Path{0, 0});
    visited[1].emplace(target, Path{0, 0});
    frontier[0].push_back(identity); frontier[1].push_back(target);
    out->states = 2;
    uint64_t processed = 0;
    for (int coverage = 1; coverage <= max_depth; ++coverage) {
        int side = (coverage - 1) % 2;
        auto& own = visited[side];
        auto& other = visited[1 - side];
        std::vector<State> next;
        for (const State& state : frontier[side]) {
            if ((processed++ & 15) == 0 && Clock::now() >= deadline) {
                out->status = 2; return;
            }
            Path path = own.find(state)->second;
            for (int m = 0; m < 18; ++m) {
                State child = apply_move(state, maps[m]);
                ++out->generated;
                if (own.find(child) != own.end()) continue;
                Path child_path{(path.code << 5) | static_cast<uint64_t>(m),
                                static_cast<uint8_t>(path.depth + 1)};
                auto meeting = other.find(child);
                if (meeting != other.end()) {
                    // Earlier complete layers ruled out every shorter distance.
                    if (Clock::now() >= deadline) { out->status = 2; return; }
                    if (side == 0) write_path(out, child_path, meeting->second);
                    else write_path(out, meeting->second, child_path);
                    return;
                }
                if (out->states >= max_states) { out->status = 3; return; }
                own.emplace(child, child_path);
                ++out->states;
                next.push_back(child);
            }
        }
        frontier[side] = std::move(next);
        if (Clock::now() >= deadline) { out->status = 2; return; }
    }
}

extern "C" void br_search(const uint8_t* target_bytes, const uint8_t* map_bytes,
                           int max_depth, uint64_t timeout_ns, uint64_t max_states,
                           SearchResult* out) {
    try {
        run_search(target_bytes, map_bytes, max_depth, timeout_ns, max_states, out);
    } catch (const std::bad_alloc&) {
        out->status = 3; // Preserve the original solution on memory exhaustion.
    }
}
