/*
 * MEMSIM — Level 2 Fragmentation Benchmark (C Version)
 * ====================================================
 * Compile:
 * gcc -std=c99 -Wall -o benchmark level_1.c level2_benchmark.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define SEED 99

/* ── Extern declarations from your allocator ── */
extern void mem_init(void);
extern int mem_alloc(int n_bytes, int task_id, int strategy);
extern bool mem_free(int ptr);

typedef struct {
    int   total_free_bytes;
    int   largest_free_block;
    int   num_free_fragments;
    float fragmentation_ratio;
} MemStats;
extern MemStats mem_stats(void);

/* ── Result Struct ── */
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

/* ── Test Runner ── */
BenchmarkResult run_benchmark(int strategy, const char* name, int seed) {
    // Note: C's rand() produces a different sequence than Python's Mersenne Twister,
    // but the statistical distribution of the workload remains identical.
    srand(seed);
    mem_init();

    // FIFO queue for live pointers
    int live_ptrs[1000];
    int head = 0;
    int tail = 0;

    int alloc_ok = 0;
    int alloc_fail = 0;
    int free_count = 0;

    for (int step = 0; step < 1000; step++) {
        int size;
        if (step % 2 == 1) {
            // Small block: 4 to 128 bytes
            size = 4 + (rand() % (128 - 4 + 1));
        } else {
            // Large block: 512 to 4096 bytes
            size = 512 + (rand() % (4096 - 512 + 1));
        }

        int ptr = mem_alloc(size, 0, strategy);
        if (ptr == -1) {
            alloc_fail++;
        } else {
            alloc_ok++;
            live_ptrs[tail++] = ptr;
        }

        // Free the oldest allocation every third step after step 100
        if (step > 100 && step % 3 == 0 && head < tail) {
            int ptr_to_free = live_ptrs[head++];
            mem_free(ptr_to_free);
            free_count++;
        }
    }

    MemStats stats = mem_stats();

    BenchmarkResult r;
    r.strategy_name = name;
    r.alloc_succeeded = alloc_ok;
    r.alloc_failed = alloc_fail;
    r.free_calls = free_count;
    r.total_free_bytes = stats.total_free_bytes;
    r.largest_free_block = stats.largest_free_block;
    r.num_free_fragments = stats.num_free_fragments;
    r.fragmentation_ratio = stats.fragmentation_ratio;

    return r;
}

void print_result(BenchmarkResult r) {
    printf("\n  Strategy: %s\n", r.strategy_name);
    printf("    Alloc calls succeeded : %d\n", r.alloc_succeeded);
    printf("    Alloc failed (OOM)    : %d\n", r.alloc_failed);
    printf("    Free calls            : %d\n", r.free_calls);
    printf("    Final free bytes      : %d\n", r.total_free_bytes);
    printf("    Largest free block    : %d\n", r.largest_free_block);
    printf("    Free fragments        : %d\n", r.num_free_fragments);
    printf("    Fragmentation ratio   : %.3f\n", r.fragmentation_ratio);
}

int main(void) {
    printf("============================================================\n");
    printf("MEMSIM — Level 2 Fragmentation Benchmark (C Version)\n");
    printf("============================================================\n");

    // 0 = first_fit, 1 = best_fit
    BenchmarkResult ff = run_benchmark(0, "first_fit", SEED);
    BenchmarkResult bf = run_benchmark(1, "best_fit", SEED);

    print_result(ff);
    print_result(bf);

    printf("\n------------------------------------------------------------\n");
    
    const char* winner = (ff.fragmentation_ratio <= bf.fragmentation_ratio) ? "first_fit" : "best_fit";
    float diff = fabs(ff.fragmentation_ratio - bf.fragmentation_ratio);
    
    printf("  Winner: %s  (delta = %.4f)\n\n", winner, diff);
    printf("  Copy the table above into your README and add a paragraph\n");
    printf("  explaining WHY the winning strategy performed better on\n");
    printf("  this alternating small/large workload.\n");

    return 0;
}
