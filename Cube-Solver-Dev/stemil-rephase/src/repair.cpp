#include "repair.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
#include <unordered_map>

namespace rephase {
const Repair::Entry* Repair::lookup(Key k) const {
    size_t i=hash_key(k)&(table_.size()-1);
    while(table_[i].depth!=255) { if(table_[i].key==k)return &table_[i]; i=(i+1)&(table_.size()-1); }
    return nullptr;
}
bool Repair::insert(Key k,uint64_t path,uint8_t d) {
    size_t i=hash_key(k)&(table_.size()-1);
    while(table_[i].depth!=255) { if(table_[i].key==k)return false; i=(i+1)&(table_.size()-1); }
    table_[i]={k,path,d};++states_;return true;
}
Algorithm Repair::decode(uint64_t path,int d) const {
    Algorithm a; for(int i=0;i<d;++i)a.push_back((path>>(5*i))&31);return a;
}
const char* repair_mode_name(RepairMode mode) {
    switch(mode){case RepairMode::BoundaryAndSliding:return "boundary+sliding";case RepairMode::Boundary:return "boundary";case RepairMode::Sliding:return "sliding";}
    throw std::invalid_argument("invalid repair mode");
}
const char* repair_stage_name(RepairStage stage) { return stage==RepairStage::Boundary?"boundary":"sliding"; }
void validate_repair_config(const RepairConfig& config) {
    if(config.table_depth<0 || config.table_depth>5)throw std::invalid_argument("repair table depth must be 0..5");
    if(config.windows.empty() || config.window_slice_ms<=0 || !std::isfinite(config.window_slice_ms))
        throw std::invalid_argument("repair windows/slice must be positive");
    for(int w:config.windows)if(w<2 || w>10)throw std::invalid_argument("window lengths must be 2..10");
    if(!std::isfinite(config.boundary_fraction) || config.boundary_fraction<=0 || config.boundary_fraction>=1)
        throw std::invalid_argument("boundary fraction must be >0 and <1");
    repair_mode_name(config.mode);
}
Repair::Repair(RepairConfig config):config_(std::move(config)) {
    validate_repair_config(config_);
    std::sort(config_.windows.begin(),config_.windows.end());
    config_.windows.erase(std::unique(config_.windows.begin(),config_.windows.end()),config_.windows.end());
    // Upper bound by canonical word count, before allocating either structure.
    std::array<size_t,18> count{};count.fill(1);size_t words=1;
    for(int d=1;d<=config_.table_depth;++d) {
        for(auto c:count)words+=c;
        std::array<size_t,18> next{};
        for(int p=0;p<18;++p)for(int m=0;m<18;++m)if(canonical_next(p,m))next[m]+=count[p];
        count=next;
    }
    size_t slots=2;while(slots<words*2)slots*=2;
    struct Item { State state; uint64_t path; int previous; uint8_t depth; };
    size_t maximum_bytes=slots*sizeof(Entry)+words*sizeof(Item);
    if(maximum_bytes>config_.memory_limit_bytes)throw std::invalid_argument("repair table construction exceeds memory cap; lower --table-depth or increase --repair-memory-mib");
    table_.resize(slots);std::vector<Item> queue;queue.reserve(words);
    State identity=State::identity();insert(identity.key(),0,0);queue.push_back({identity,0,-1,0});
    // Enumerate every canonical word. Expanding only one representative per state
    // would interact with previous-face pruning and could omit valid successors.
    for(size_t i=0;i<queue.size();++i) {
        auto item=queue[i];if(item.depth==config_.table_depth)continue;
        for(int m=0;m<18;++m)if(canonical_next(item.previous,m)) {
            State next=item.state.moved(m);auto path=item.path|(uint64_t(m)<<(5*item.depth));
            auto d=uint8_t(item.depth+1);insert(next.key(),path,d);queue.push_back({next,path,m,d});
        }
    }
    build_peak_bytes_=table_bytes()+queue.capacity()*sizeof(Item);
}
ExactResult Repair::exact(const State& target,int max_length,Deadline deadline) const {
    if(max_length<0 || max_length>9)throw std::invalid_argument("exact replacement bound must be 0..9");
    ExactResult result;
    if(Clock::now()>=deadline) { result.timed_out=true;return result; }
    if(const auto* e=lookup(target.key());e && e->depth<=max_length) {
        result.replacement=decode(e->path,e->depth);result.complete=true;return result;
    }
    // All distances <= table radius were ruled out by the lookup above.
    Algorithm query;
    for(int length=config_.table_depth+1;length<=max_length;++length) {
        int reverse_depth=length-config_.table_depth;
        std::function<bool(const State&,int,int)> visit=[&](const State& current,int left,int previous) {
            ++result.nodes;
            if((result.nodes&63)==1 && Clock::now()>=deadline) { result.timed_out=true;return false; }
            if(const auto* e=lookup(current.key());e && int(e->depth)+int(query.size())<=length) {
                Algorithm answer=decode(e->path,e->depth); auto tail=inverse_algorithm(query);
                answer.insert(answer.end(),tail.begin(),tail.end());answer=normalize(answer);
                if(State::identity().applied(answer)!=target)throw std::logic_error("exact search produced an invalid transform");
                result.replacement=std::move(answer);return true;
            }
            if(!left)return false;
            for(int m=0;m<18;++m)if(canonical_next(previous,m)) {
                query.push_back(m);bool found=visit(current.moved(m),left-1,m);query.pop_back();
                if(found || result.timed_out)return found;
            }
            return false;
        };
        if(visit(target,reverse_depth,-1)) { result.complete=true;return result; }
        if(result.timed_out)return result;
    }
    result.complete=true;return result;
}
RepairResult Repair::optimize(const Algorithm& solution,int boundary,Deadline deadline,const TraceCallback& trace) const {
    return optimize_with(solution,boundary,config_,deadline,trace);
}
RepairResult Repair::optimize_with(const Algorithm& solution,int boundary,const RepairConfig& search_config,Deadline deadline,const TraceCallback& trace) const {
    validate_repair_config(search_config);
    if(boundary<0 || size_t(boundary)>solution.size())throw std::invalid_argument("boundary outside sequence");
    RepairResult result{solution,boundary,{}};const State original=State::identity().applied(solution);const auto optimize_began=Clock::now();
    int focal=boundary;
    struct Query {Key key;int bound;bool operator==(const Query& other) const{return key==other.key && bound==other.bound;}};
    struct Hash {size_t operator()(const Query& q) const{return hash_key(q.key)^uint64_t(q.bound)*0x9e3779b97f4a7c15ULL;}};
    std::unordered_map<Query,ExactResult,Hash> completed;
    auto stage_run=[&](RepairStage stage,Deadline stage_deadline) {
        auto started=Clock::now();auto& counts=stage==RepairStage::Boundary?result.stats.boundary:result.stats.sliding;
        const std::string name=repair_stage_name(stage);
        if(trace){TraceEvent e;e.event="stage_start";e.stage=name;e.budget_ms=std::max(0.0,std::chrono::duration<double,std::milli>(stage_deadline-started).count());trace(e);}
        while(Clock::now()<stage_deadline) {
            const int n=int(result.moves.size());std::vector<std::pair<int,int>> windows;int previous_width=-1;
            for(int requested:search_config.windows) {
                int w=std::min(n,requested);if(w<2 || w==previous_width)continue;previous_width=w;
                std::vector<int> starts;
                if(stage==RepairStage::Sliding)for(int start=0;start<=n-w;++start)starts.push_back(start);
                else {
                    // At the sequence end, the window ending exactly at the
                    // focal index is the only boundary-adjacent window.
                    int first=focal==n?n-w:std::max(0,focal-w+1);
                    for(int start=first;start<=std::min(focal,n-w);++start)
                        if((focal==0 || start<focal) && (focal==n || start+w>focal))starts.push_back(start);
                    std::stable_sort(starts.begin(),starts.end(),[&](int a,int b){return std::abs(2*a+w-2*focal)<std::abs(2*b+w-2*focal);});
                }
                for(int start:starts)windows.emplace_back(start,start+w);
            }
            bool changed=false;
            for(auto [start,end]:windows) {
                if(Clock::now()>=stage_deadline)break;
                Algorithm window(result.moves.begin()+start,result.moves.begin()+end);
                State target=State::identity().applied(window);Query query{target.key(),end-start-1};
                if(trace){TraceEvent e;e.event="window_start";e.stage=name;e.begin=start;e.end=end;e.before=end-start;e.sequence=format_algorithm(window);trace(e);}
                ExactResult found;bool cached=false;
                auto entry=completed.find(query);
                if(entry!=completed.end()){found=entry->second;found.nodes=0;cached=true;++counts.cache_hits;}
                else {
                    auto slice=std::chrono::duration_cast<Clock::duration>(std::chrono::duration<double,std::milli>(search_config.window_slice_ms));
                    found=exact(target,query.bound,std::min(stage_deadline,Clock::now()+slice));
                    // A timeout is never cached as a negative proof. A small fixed
                    // cache bounds additional memory while sharing completed work.
                    if(found.complete){if(completed.size()>=256)completed.clear();completed.emplace(query,found);}
                }
                ++counts.windows_tested;counts.nodes+=found.nodes;counts.windows_completed+=found.complete;counts.windows_timed_out+=found.timed_out;
                ++result.stats.windows_tested;result.stats.nodes+=found.nodes;result.stats.windows_completed+=found.complete;result.stats.windows_timed_out+=found.timed_out;
                if(trace){TraceEvent e;e.event="window_end";e.stage=name;e.begin=start;e.end=end;e.nodes=found.nodes;e.complete=found.complete;e.timed_out=found.timed_out;e.cached=cached;e.after=found.replacement?int(found.replacement->size()):-1;trace(e);}
                if(!found.replacement)continue;
                const auto& replacement=*found.replacement;
                Algorithm next(result.moves.begin(),result.moves.begin()+start);
                next.insert(next.end(),replacement.begin(),replacement.end());next.insert(next.end(),result.moves.begin()+end,result.moves.end());
                if(next.size()>=result.moves.size() || State::identity().applied(next)!=original)
                    throw std::logic_error("repair splice failed verification");
                result.stats.events.push_back({start,end,end-start,int(replacement.size()),stage,std::chrono::duration<double,std::milli>(Clock::now()-optimize_began).count()});++counts.improvements;counts.moves_saved+=end-start-int(replacement.size());
                if(trace){TraceEvent e;e.event="repair_applied";e.stage=name;e.begin=start;e.end=end;e.before=int(result.moves.size());e.after=int(next.size());e.sequence=format_algorithm(window);e.replacement=format_algorithm(replacement);e.complete=found.complete;trace(e);}
                result.moves=std::move(next);
                if(stage==RepairStage::Boundary)focal=start+int(replacement.size())/2;
                else if(focal>=end)focal-=end-start-int(replacement.size());
                else if(focal>=start)focal=start+int(replacement.size())/2;
                result.boundary=focal;changed=true;break;
            }
            if(!changed || !search_config.repeat)break;
        }
        counts.elapsed_ms=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
        if(trace){TraceEvent e;e.event="stage_end";e.stage=name;e.before=int(solution.size());e.after=int(result.moves.size());e.nodes=counts.nodes;e.timed_out=Clock::now()>=stage_deadline;trace(e);}
    };
    auto started=Clock::now();
    if(started>=deadline){result.stats.deadline_reached=true;return result;}
    if(search_config.mode!=RepairMode::Sliding) {
        auto boundary_deadline=deadline;
        if(search_config.mode==RepairMode::BoundaryAndSliding)boundary_deadline=started+std::chrono::duration_cast<Clock::duration>((deadline-started)*search_config.boundary_fraction);
        stage_run(RepairStage::Boundary,boundary_deadline);
    }
    if(search_config.mode!=RepairMode::Boundary && Clock::now()<deadline && (search_config.repeat || result.stats.events.empty()))stage_run(RepairStage::Sliding,deadline);
    result.stats.deadline_reached=Clock::now()>=deadline;
    return result;
}
}
