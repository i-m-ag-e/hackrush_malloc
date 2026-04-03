/*
 * MEMSIM — Level 1 Stress Test (C Version)
 * ========================================
 * Compile with:
 * gcc -std=c99 -Wall -o stress_test starter_harness.c level1_stress_test.c
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SEED 42
#define DEVICE_RAM (64 * 1024)
#define MIN_ALLOC 4
#define MAX_ALLOC 4096
#define ALIGNMENT 4
#define HEADER_SIZE 8

/* ── Extern declarations from starter_harness.c ── */
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

/* ── Tracker for live allocations ── */
typedef struct {
    int ptr;
    int size;
    int canary_len;
} LivePtr;

/* ── Tests ── */

bool run_stress_test(int n_calls, int seed) {
    srand(seed);
    mem_init();

    LivePtr live_ptrs[1000];
    int live_count = 0;
    int alloc_count = 0;
    int free_count = 0;
    bool passed = true;

    for (int step = 0; step < n_calls; step++) {
        // Bias towards alloc: 0, 1 = alloc; 2 = free
        int action = rand() % 3;

        if (action < 2 || live_count == 0) {
            int size = MIN_ALLOC + (rand() % (MAX_ALLOC - MIN_ALLOC + 1));
            int ptr = mem_alloc(size, 1, 0);

            if (ptr == -1) {
                continue;  // OOM is acceptable
            }

            if (ptr < HEADER_SIZE) {
                printf(
                    "  -> Step %d: mem_alloc returned ptr=%d below "
                    "HEADER_SIZE\n",
                    step, ptr);
                passed = false;
            } else if (ptr >= DEVICE_RAM) {
                printf(
                    "  -> Step %d: mem_alloc returned ptr=%d >= DEVICE_RAM\n",
                    step, ptr);
                passed = false;
            } else if (ptr % ALIGNMENT != 0) {
                printf("  -> Step %d: mem_alloc returned unaligned ptr=%d\n",
                       step, ptr);
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

            // Remove from live list by swapping with the last element
            live_ptrs[idx] = live_ptrs[--live_count];

            // Verify canary before freeing
            for (int i = 0; i < lp.canary_len; i++) {
                uint8_t expected = (lp.ptr + i) & 0xFF;
                uint8_t actual = ram[lp.ptr + i];
                if (actual != expected) {
                    printf(
                        "  -> Step %d: canary corrupted at ram[%d] expected "
                        "0x%02X got 0x%02X\n",
                        step, lp.ptr + i, expected, actual);
                    passed = false;
                }
            }

            bool ok = mem_free(lp.ptr);
            if (!ok) {
                printf(
                    "  -> Step %d: mem_free(%d) returned False for a live "
                    "pointer\n",
                    step, lp.ptr);
                passed = false;
            }
            free_count++;
        }
    }

    // Free remaining pointers
    for (int i = 0; i < live_count; i++) {
        if (!mem_free(live_ptrs[i].ptr)) {
            printf("  -> Cleanup: mem_free(%d) returned False\n",
                   live_ptrs[i].ptr);
            passed = false;
        }
    }

    MemStats stats = mem_stats();
    if (stats.total_free_bytes < DEVICE_RAM - HEADER_SIZE * 2) {
        printf(
            "  -> After full cleanup, only %d bytes free (expected close to "
            "%d). Possible leak or coalescing bug.\n",
            stats.total_free_bytes, DEVICE_RAM);
        passed = false;
    }

    return passed;
}

bool run_double_free_test(void) {
    mem_init();
    bool passed = true;
    int ptr = mem_alloc(64, 0, 0);

    if (ptr == -1) {
        printf("  -> mem_alloc returned -1 on fresh heap\n");
        return false;
    }

    mem_free(ptr);
    bool result = mem_free(ptr);  // Double free

    if (result != false) {
        printf("  -> Double-free of ptr=%d returned true instead of false\n",
               ptr);
        passed = false;
    }
    return passed;
}

bool run_invalid_ptr_test(void) {
    mem_init();
    bool passed = true;
    int bad_ptrs[] = {-1, 0, 3, DEVICE_RAM, DEVICE_RAM + 100};
    int n_bad = sizeof(bad_ptrs) / sizeof(bad_ptrs[0]);

    for (int i = 0; i < n_bad; i++) {
        bool result = mem_free(bad_ptrs[i]);
        if (result != false) {
            printf("  -> mem_free(%d) returned true instead of false\n",
                   bad_ptrs[i]);
            passed = false;
        }
    }
    return passed;
}

bool run_alignment_test(void) {
    mem_init();
    bool passed = true;
    int sizes[] = {4, 5, 7, 8, 13, 64, 100, 255, 256, 1000, 4096};
    int n_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int i = 0; i < n_sizes; i++) {
        int ptr = mem_alloc(sizes[i], 0, 0);
        if (ptr != -1 && ptr % ALIGNMENT != 0) {
            printf("  -> mem_alloc(%d) returned unaligned ptr=%d\n", sizes[i],
                   ptr);
            passed = false;
        }
        if (ptr != -1) {
            mem_free(ptr);
        }
    }
    return passed;
}

/* ── Main Runner ── */

int main(void) {
    printf("============================================================\n");
    printf("MEMSIM — Level 1 Stress Test\n");
    printf("============================================================\n");

    int passed_tests = 0;
    int total_tests = 4;
    bool all_ok = true;

    if (run_stress_test(500, SEED)) {
        printf("  [PASS] 500-call random stress test\n");
        passed_tests++;
    } else {
        printf("  [FAIL] 500-call random stress test\n");
        all_ok = false;
    }

    if (run_double_free_test()) {
        printf("  [PASS] Double-free protection\n");
        passed_tests++;
    } else {
        printf("  [FAIL] Double-free protection\n");
        all_ok = false;
    }

    if (run_invalid_ptr_test()) {
        printf("  [PASS] Invalid pointer rejection\n");
        passed_tests++;
    } else {
        printf("  [FAIL] Invalid pointer rejection\n");
        all_ok = false;
    }

    if (run_alignment_test()) {
        printf("  [PASS] Alignment guarantee\n");
        passed_tests++;
    } else {
        printf("  [FAIL] Alignment guarantee\n");
        all_ok = false;
    }

    printf("------------------------------------------------------------\n");
    printf("Results: %d/%d tests passed\n", passed_tests, total_tests);

    if (!all_ok) {
        printf("\nErrors found. Fix them before submitting.\n");
        return 1;
    } else {
        printf("\nAll tests passed. Level 1 stress test complete.\n");
        return 0;
    }
}
