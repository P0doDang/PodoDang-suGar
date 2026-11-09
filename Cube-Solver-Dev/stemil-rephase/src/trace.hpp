#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace rephase {
struct TraceEvent {
    std::string event,stage,message,sequence,replacement;
    double budget_ms=-1;
    int begin=-1,end=-1,before=-1,after=-1;
    uint64_t nodes=0;
    bool complete=false,timed_out=false,cached=false;
};
using TraceCallback=std::function<void(const TraceEvent&)>;
}
