/* New runner only: profile an unchanged Kociemba search except its totalDepth
 * definition is renamed to route phase transitions through the wrapper below.
 * The generated include comes from kociemba==1.2.1; originals are read-only.
 */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <time.h>
#include "search.h"
#include "coordcube.h"

typedef struct {
    uint64_t total_ns, phase1_ns, phase2_ns, phase2_calls;
    uint64_t baseline_rss, phase1_rss, phase2_rss, repair_rss, total_rss;
    uint64_t phase1_samples, phase2_samples, repair_samples, interval_ns;
} brp_metrics;

static _Atomic int active_phase = 0;
static _Atomic int monitoring = 0;
static _Atomic uint64_t peaks[4], samples[4], total_peak;
static uint64_t total_ns, phase2_ns, phase2_calls, baseline_rss;
static uint64_t interval_ns;
static long page_size;
static pthread_t observer;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static uint64_t rss_bytes(void) {
    char data[128];
    int fd = open("/proc/self/statm", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    ssize_t count = read(fd, data, sizeof(data) - 1);
    close(fd);
    if (count <= 0) return 0;
    data[count] = '\0';
    unsigned long long size, resident;
    if (sscanf(data, "%llu %llu", &size, &resident) != 2) return 0;
    return (uint64_t)resident * (uint64_t)page_size;
}

static void update_peak(_Atomic uint64_t* peak, uint64_t rss) {
    uint64_t previous = atomic_load(peak);
    while (previous < rss && !atomic_compare_exchange_weak(peak, &previous, rss)) {}
}

static void sample_once(void) {
    int phase = atomic_load(&active_phase);
    if (phase < 1 || phase > 3) return;
    uint64_t rss = rss_bytes();
    if (rss && phase == atomic_load(&active_phase)) {
        update_peak(&peaks[phase], rss);
        update_peak(&total_peak, rss);
        atomic_fetch_add(&samples[phase], 1);
    }
}

void brp_set_phase(int phase) {
    atomic_store(&active_phase, phase);
    if (phase >= 1 && phase <= 3 && atomic_load(&samples[phase]) == 0) sample_once();
}

void brp_end_phase(void) {
    sample_once();
    atomic_store(&active_phase, 0);
}

static void* observe(void* unused) {
    (void)unused;
    struct timespec interval = {(time_t)(interval_ns / 1000000000ULL),
                                (long)(interval_ns % 1000000000ULL)};
    while (atomic_load(&monitoring)) {
        sample_once();
        nanosleep(&interval, NULL);
    }
    return NULL;
}

int brp_start_monitor(uint64_t sampling_ns) {
    if (atomic_load(&monitoring)) return -1;
    page_size = sysconf(_SC_PAGESIZE);
    if (page_size <= 0) return -2;
    baseline_rss = rss_bytes();
    if (!baseline_rss) return -3;
    interval_ns = sampling_ns;
    total_ns = phase2_ns = phase2_calls = 0;
    atomic_store(&active_phase, 0);
    atomic_store(&total_peak, 0);
    for (int i = 0; i < 4; ++i) {
        atomic_store(&peaks[i], 0); atomic_store(&samples[i], 0);
    }
    atomic_store(&monitoring, 1);
    if (pthread_create(&observer, NULL, observe, NULL) != 0) {
        atomic_store(&monitoring, 0); return -4;
    }
    return 0;
}

void brp_stop_monitor(brp_metrics* out) {
    brp_end_phase();
    if (atomic_exchange(&monitoring, 0)) pthread_join(observer, NULL);
    *out = (brp_metrics){
        total_ns, total_ns >= phase2_ns ? total_ns - phase2_ns : 0,
        phase2_ns, phase2_calls, baseline_rss,
        atomic_load(&peaks[1]), atomic_load(&peaks[2]), atomic_load(&peaks[3]),
        atomic_load(&total_peak), atomic_load(&samples[1]), atomic_load(&samples[2]),
        atomic_load(&samples[3]), interval_ns
    };
}

int brp_phase2_impl(search_t*, int, int);

int totalDepth(search_t* search, int phase1_depth, int max_depth) {
    brp_set_phase(2);
    uint64_t start = now_ns();
    int result = brp_phase2_impl(search, phase1_depth, max_depth);
    phase2_ns += now_ns() - start;
    ++phase2_calls;
    brp_set_phase(1);
    return result;
}

#define solution brp_raw_solution
#include "search_profiled.inc"
#undef solution

void brp_prepare(const char* cache_dir) {
    if (!PRUNING_INITED) initPruning(cache_dir);
}

char* brp_solve(char* facelets, int max_depth, long timeout, const char* cache_dir) {
    brp_set_phase(1);
    uint64_t start = now_ns();
    char* result = brp_raw_solution(facelets, max_depth, timeout, 1, cache_dir);
    total_ns = now_ns() - start;
    brp_end_phase();
    return result;
}
