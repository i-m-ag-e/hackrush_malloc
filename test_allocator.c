/*
 * MEMSIM — Consolidated Test Suite (HackRush)
 * ===========================================
 * Includes:
 *   - Phase 1: Random alloc/free stress test & bounds checking
 *   - Phase 2: Fragmentation benchmark (first-fit vs best-fit)
 *   - Phase 3: Task quota & leak scenarios
 *   - Phase 4: Compaction & Handle benchmark
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>

#define SEED 42
#define DEVICE_RAM (64 * 1024)
#define MIN_ALLOC 4
#define MAX_ALLOC 4096
#define ALIGNMENT 4
#define HEADER_SIZE 8

/* ── Extern declarations from allocator.c ── */
extern uint8_t ram[DEVICE_RAM];
extern void mem_init(void);
extern int mem_alloc(int n_bytes, int task_id, int strategy);
extern bool mem_free(int ptr);

typedef struct {
    int total_free_bytes;
    int largest_free_block;
    int num_free_fragments;
    float fragmentation_ratio;
} MemStats;
extern MemStats mem_stats(void);

typedef void (*TaskFn)(void);
typedef struct {
    int task_id;
    int quota_bytes;
    int bytes_allocated;
    bool quota_exceeded;
} TaskReport;
extern TaskReport task_spawn(int task_id, int quota_bytes, TaskFn fn);

extern void handles_init(void);
extern int mem_alloc_handle(int n_bytes, int task_id);
extern bool mem_free_handle(int handle);
extern int mem_compact(void);
extern int deref_handle(int handle);


/* =========================================================================
 * PHASE 1: STRESS TEST & BOUNDS
 * ========================================================================= */
typedef struct {
    int ptr;
    int size;
    int canary_len;
} LivePtr;

bool run_stress_test(int n_calls, int seed) {
    srand(seed);
    mem_init();

    // Bumped array capacity to 2000 for headroom
    LivePtr live_ptrs[2000]; 
    int live_count = 0;
    int alloc_count = 0;
    int free_count = 0;
    bool passed = true;

    for (int step = 0; step < n_calls; step++) {
        int action = rand() % 3;

        if (action < 2 || live_count == 0) {
            int size = MIN_ALLOC + (rand() % (MAX_ALLOC - MIN_ALLOC + 1));
            int ptr = mem_alloc(size, 1, 0);

            if (ptr == -1) continue;

            if (ptr < HEADER_SIZE) {
                printf("  -> Step %d: mem_alloc returned ptr=%d below HEADER_SIZE\n", step, ptr);
                passed = false;
            } else if (ptr >= DEVICE_RAM) {
                printf("  -> Step %d: mem_alloc returned ptr=%d >= DEVICE_RAM\n", step, ptr);
                passed = false;
            } else if (ptr % ALIGNMENT != 0) {
                printf("  -> Step %d: mem_alloc returned unaligned ptr=%d\n", step, ptr);
                passed = false;
            } else {
                int canary_len = (size < 8) ? size : 8;
                for (int i = 0; i < canary_len; i++) {
                    ram[ptr + i] = (ptr + i) & 0xFF;
                }
                live_ptrs[live_count++] = (LivePtr){ptr, size, canary_len};
                alloc_count++;
            }
        } else if (live_count > 0) {
            int idx = rand() % live_count;
            LivePtr lp = live_ptrs[idx];
            live_ptrs[idx] = live_ptrs[--live_count];

            for (int i = 0; i < lp.canary_len; i++) {
                uint8_t expected = (lp.ptr + i) & 0xFF;
                uint8_t actual = ram[lp.ptr + i];
                if (actual != expected) {
                    printf("  -> Step %d: canary corrupted at ram[%d] expected 0x%02X got 0x%02X\n",
                           step, lp.ptr + i, expected, actual);
                    passed = false;
                }
            }

            if (!mem_free(lp.ptr)) {
                printf("  -> Step %d: mem_free(%d) returned False for a live pointer\n", step, lp.ptr);
                passed = false;
            }
            free_count++;
        }
    }

    for (int i = 0; i < live_count; i++) {
        if (!mem_free(live_ptrs[i].ptr)) {
            printf("  -> Cleanup: mem_free(%d) returned False\n", live_ptrs[i].ptr);
            passed = false;
        }
    }

    MemStats stats = mem_stats();
    if (stats.total_free_bytes < DEVICE_RAM - HEADER_SIZE * 2) {
        printf("  -> After full cleanup, only %d bytes free. Possible leak or coalescing bug.\n", stats.total_free_bytes);
        passed = false;
    }

    return passed;
}

bool run_double_free_test(void) {
    mem_init();
    int ptr = mem_alloc(64, 0, 0);
    if (ptr == -1) return false;
    mem_free(ptr);
    if (mem_free(ptr) != false) {
        printf("  -> Double-free of ptr=%d returned true instead of false\n", ptr);
        return false;
    }
    return true;
}

bool run_invalid_ptr_test(void) {
    mem_init();
    int bad_ptrs[] = {-1, 0, 3, DEVICE_RAM, DEVICE_RAM + 100};
    int n_bad = sizeof(bad_ptrs) / sizeof(bad_ptrs[0]);
    for (int i = 0; i < n_bad; i++) {
        if (mem_free(bad_ptrs[i]) != false) {
            printf("  -> mem_free(%d) returned true instead of false\n", bad_ptrs[i]);
            return false;
        }
    }
    return true;
}

bool run_alignment_test(void) {
    mem_init();
    int sizes[] = {4, 5, 7, 8, 13, 64, 100, 255, 256, 1000, 4096};
    int n_sizes = sizeof(sizes) / sizeof(sizes[0]);
    for (int i = 0; i < n_sizes; i++) {
        int ptr = mem_alloc(sizes[i], 0, 0);
        if (ptr != -1 && ptr % ALIGNMENT != 0) {
            printf("  -> mem_alloc(%d) returned unaligned ptr=%d\n", sizes[i], ptr);
            return false;
        }
        if (ptr != -1) mem_free(ptr);
    }
    return true;
}

/* =========================================================================
 * PHASE 2: FRAGMENTATION BENCHMARK
 * ========================================================================= */
typedef struct {
    const char* strategy_name;
    int alloc_succeeded;
    int alloc_failed;
    int free_calls;
    int total_free_bytes;
    int largest_free_block;
    int num_free_fragments;
    float fragmentation_ratio;
} BenchmarkResult;

BenchmarkResult run_frag_benchmark(int strategy, const char* name, int seed) {
    srand(seed);
    mem_init();

    // Bumped array capacity to 2000 for headroom
    int live_ptrs[2000];
    int head = 0, tail = 0;
    int alloc_ok = 0, alloc_fail = 0, free_count = 0;

    for (int step = 0; step < 1000; step++) {
        int size = (step % 2 == 1) ? (4 + (rand() % 125)) : (512 + (rand() % 3585));
        int ptr = mem_alloc(size, 0, strategy);
        
        if (ptr == -1) alloc_fail++;
        else {
            alloc_ok++;
            live_ptrs[tail++] = ptr;
        }

        if (step > 100 && step % 3 == 0 && head < tail) {
            mem_free(live_ptrs[head++]);
            free_count++;
        }
    }

    MemStats stats = mem_stats();
    return (BenchmarkResult){name, alloc_ok, alloc_fail, free_count, stats.total_free_bytes, stats.largest_free_block, stats.num_free_fragments, stats.fragmentation_ratio};
}

/* =========================================================================
 * PHASE 3: TASK QUOTAS & LEAKS
 * ========================================================================= */
void good_task() { mem_free(mem_alloc(100, 1, 0)); }
void leaky_task() { mem_alloc(200, 2, 0); }
void greedy_task() {
    mem_alloc(3000, 3, 0);
    mem_alloc(3000, 3, 0);
    mem_alloc(3000, 3, 0);
}
void complex_worker() {
    int live_cache_ptrs[5];
    for (int i = 0; i < 5; i++) {
        int temp_ptr = mem_alloc(1000, 4, 0);
        if (temp_ptr != -1) mem_free(temp_ptr);
        
        int cache_ptr = mem_alloc(200, 4, 0);
        if (cache_ptr != -1) live_cache_ptrs[i] = cache_ptr;
    }
    mem_alloc(3500, 4, 0); // Will trigger watchdog
    (void) live_cache_ptrs;
}

/* =========================================================================
 * PHASE 4: COMPACTION BENCHMARK
 * ========================================================================= */
#define COMPACT_BLOCK_SIZE 64
#define COMPACT_LARGE_REQUEST 8192
#define COMPACT_TARGET_FILL 0.90

bool run_compaction_benchmark() {
    mem_init();
    handles_init();

    int handles[4096];
    int handle_count = 0;
    int target_bytes = (int)(DEVICE_RAM * COMPACT_TARGET_FILL);
    int used = 0;

    while (used + COMPACT_BLOCK_SIZE + 8 < target_bytes) {
        int h = mem_alloc_handle(COMPACT_BLOCK_SIZE, 1);
        if (h == -1) break;
        handles[handle_count++] = h;
        used += COMPACT_BLOCK_SIZE + 8;
    }

    int keep[4096];
    int keep_count = 0;
    for (int i = 0; i < handle_count; i++) {
        if (i % 2 == 0) mem_free_handle(handles[i]);
        else keep[keep_count++] = handles[i];
    }

    int direct = mem_alloc_handle(COMPACT_LARGE_REQUEST, 2);
    if (direct != -1) return true; // Did not need compaction

    mem_compact();
    
    for (int i = 0; i < keep_count; i++) {
        if (deref_handle(keep[i]) == -1) return false;
    }

    int post = mem_alloc_handle(COMPACT_LARGE_REQUEST, 2);
    
    bool all_valid = true;
    for (int i = 0; i < keep_count; i++) {
        if (deref_handle(keep[i]) == -1) {
            all_valid = false;
            break;
        }
    }

    return (post != -1 && all_valid);
}

/* =========================================================================
 * MAIN EXECUTOR
 * ========================================================================= */
int main(void) {
    printf("============================================================\n");
    printf("MEMSIM — Consolidated Test Suite\n");
    printf("============================================================\n\n");

    printf("--- PHASE 1: Stress Test & Bounds ---\n");
    printf("  [PASS] 500-call random stress test: %s\n", run_stress_test(500, SEED) ? "YES" : "NO");
    printf("  [PASS] Double-free protection: %s\n", run_double_free_test() ? "YES" : "NO");
    printf("  [PASS] Invalid pointer rejection: %s\n", run_invalid_ptr_test() ? "YES" : "NO");
    printf("  [PASS] Alignment guarantee: %s\n\n", run_alignment_test() ? "YES" : "NO");

    printf("--- PHASE 2: Fragmentation Benchmark ---\n");
    BenchmarkResult ff = run_frag_benchmark(0, "first_fit", 99);
    BenchmarkResult bf = run_frag_benchmark(1, "best_fit", 99);
    printf("  First-Fit -> Ratio: %.3f | Fragments: %d\n", ff.fragmentation_ratio, ff.num_free_fragments);
    printf("  Best-Fit  -> Ratio: %.3f | Fragments: %d\n", bf.fragmentation_ratio, bf.num_free_fragments);
    printf("  Winner: %s\n\n", (ff.fragmentation_ratio <= bf.fragmentation_ratio) ? "first_fit" : "best_fit");

    printf("--- PHASE 3: Task Quotas & Leaks ---\n");
    mem_init();
    TaskReport r1 = task_spawn(1, 8192, good_task);
    TaskReport r2 = task_spawn(2, 8192, leaky_task);
    TaskReport r3 = task_spawn(3, 8192, greedy_task);
    TaskReport r4 = task_spawn(4, 4096, complex_worker);
    printf("  Task 1 (Good): Quota Exceeded: %s\n", r1.quota_exceeded ? "YES" : "NO");
    printf("  Task 2 (Leaky): Quota Exceeded: %s\n", r2.quota_exceeded ? "YES" : "NO");
    printf("  Task 3 (Greedy): Quota Exceeded: %s\n", r3.quota_exceeded ? "YES" : "NO");
    printf("  Task 4 (Complex): Quota Exceeded: %s\n\n", r4.quota_exceeded ? "YES" : "NO");

    printf("--- PHASE 4: Compaction & Handles ---\n");
    bool phase4_pass = run_compaction_benchmark();
    printf("  [PASS] Compaction benchmark: %s\n", phase4_pass ? "YES" : "NO");

    printf("\n============================================================\n");
    return 0;
}
