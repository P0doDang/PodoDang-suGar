#pragma once
#include "cube.hpp"
#include "trace.hpp"
#include <chrono>
#include <optional>
#include <vector>

namespace rephase {
using Clock=std::chrono::steady_clock;
using Deadline=Clock::time_point;
enum class RepairMode { BoundaryAndSliding,Boundary,Sliding };
const char* repair_mode_name(RepairMode mode);
struct RepairConfig {
    int table_depth=4;
    size_t memory_limit_bytes=64*1024*1024;
    std::vector<int> windows={4,6,8,10};
    double window_slice_ms=8;
    bool repeat=true;
    RepairMode mode=RepairMode::BoundaryAndSliding;
    double boundary_fraction=0.5;
};
void validate_repair_config(const RepairConfig& config);
struct ExactResult {
    std::optional<Algorithm> replacement;
    bool complete=false; // True only when shortest replacement/proof was established.
    bool timed_out=false;
    uint64_t nodes=0;
};
enum class RepairStage { Boundary,Sliding };
const char* repair_stage_name(RepairStage stage);
struct RepairEvent { int begin=0,end=0,old_length=0,new_length=0; RepairStage stage=RepairStage::Boundary; double at_ms=0; /* since optimize() start */ };
struct RepairStageStats {
    uint64_t nodes=0;
    int windows_tested=0,windows_completed=0,windows_timed_out=0,cache_hits=0,improvements=0,moves_saved=0;
    double elapsed_ms=0;
};
struct RepairStats {
    uint64_t nodes=0;
    int windows_tested=0,windows_completed=0,windows_timed_out=0;
    bool deadline_reached=false;
    std::vector<RepairEvent> events;
    RepairStageStats boundary,sliding;
};
struct RepairResult { Algorithm moves; int boundary=0; RepairStats stats; };
class Repair {
public:
    explicit Repair(RepairConfig config={});
    ExactResult exact(const State& target,int max_length,Deadline deadline) const;
    RepairResult optimize(const Algorithm& solution,int boundary,Deadline deadline,const TraceCallback& trace={}) const;
    RepairResult optimize_with(const Algorithm& solution,int boundary,const RepairConfig& search_config,Deadline deadline,const TraceCallback& trace={}) const;
    const RepairConfig& config() const { return config_; }
    size_t table_bytes() const { return table_.capacity()*sizeof(Entry); }
    size_t build_peak_bytes() const { return build_peak_bytes_; }
    size_t states() const { return states_; }
    int depth() const { return config_.table_depth; }
private:
    struct Entry { Key key; uint64_t path=0; uint8_t depth=255; };
    RepairConfig config_;
    std::vector<Entry> table_;
    size_t states_=0,build_peak_bytes_=0;
    const Entry* lookup(Key key) const;
    bool insert(Key key,uint64_t path,uint8_t depth);
    Algorithm decode(uint64_t path,int depth) const;
};
}
