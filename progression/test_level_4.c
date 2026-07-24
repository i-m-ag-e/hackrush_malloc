/*
 * MEMSIM — Level 4 Compaction Benchmark (C Version)
 * ====================================================
 * Compile:
 * gcc -std=c99 -Wall -O2 -o benchmark4 malloc.c level4_benchmark.c
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define DEVICE_RAM (64 * 1024)
#define BLOCK_SIZE 64
#define LARGE_REQUEST 8192
#define TARGET_FILL 0.90

/* ── Extern declarations from your allocator ── */
extern void mem_init(void);
extern void handles_init(void);
extern int mem_alloc_handle(int n_bytes, int task_id);
extern bool mem_free_handle(int handle);
extern int mem_compact(void);
extern int deref_handle(int handle);

bool run_compaction_benchmark() {
    mem_init();
    handles_init();

    // ── Phase 1: Fill to ~90 %
    // ────────────────────────────────────────────────
    printf("\n  Phase 1 — Fill to ~90%%\n");
    int handles[4096];
    int handle_count = 0;

    int target_bytes = (int)(DEVICE_RAM * TARGET_FILL);
    int used = 0;

    while (used + BLOCK_SIZE + 8 < target_bytes) {
        int h = mem_alloc_handle(BLOCK_SIZE, 1);
        if (h == -1)
            break;

        handles[handle_count++] = h;
        used += BLOCK_SIZE + 8;  // include header
    }
    printf("    Allocated %d handles, ~%d bytes used\n", handle_count, used);

    // ── Phase 2: Free every other handle ─────────────────────────────────────
    printf("\n  Phase 2 — Free every other handle (create fragmentation)\n");
    int freed = 0;

    int keep[4096];
    int keep_count = 0;

    for (int i = 0; i < handle_count; i++) {
        if (i % 2 == 0) {
            mem_free_handle(handles[i]);
            freed++;
        } else {
            keep[keep_count++] = handles[i];
        }
    }
    printf("    Freed %d handles — heap is now fragmented\n", freed);

    // ── Phase 3: Request large block ─────────────────────────────────────────
    printf("\n  Phase 3 — Request large contiguous block (%d bytes)\n",
           LARGE_REQUEST);
    int direct = mem_alloc_handle(LARGE_REQUEST, 2);

    if (direct != -1) {
        printf("    Direct alloc: SUCCESS without compaction (handle=%d)\n",
               direct);
        printf(
            "    NOTE: Your allocator coalesced enough; compaction not "
            "triggered.\n");
        return true;
    }

    printf("    Direct alloc: FAILED (fragmented, as expected)\n");

    // ── Phase 3: Compact and retry ───────────────────────────────────────────
    clock_t t0 = clock();
    int recovered = mem_compact();
    clock_t t1 = clock();

    double compact_ms = ((double)(t1 - t0) / CLOCKS_PER_SEC) * 1000.0;
    printf("    Compacting... recovered %d bytes  (%.2f ms)\n", recovered,
           compact_ms);

    // Verify existing handles didn't break during compaction
    int bad_handles = 0;
    for (int i = 0; i < keep_count; i++) {
        if (deref_handle(keep[i]) == -1) {
            bad_handles++;
        }
    }

    if (bad_handles > 0) {
        printf("    ERROR: %d handles became invalid after compaction!\n",
               bad_handles);
        return false;
    }

    int post = mem_alloc_handle(LARGE_REQUEST, 2);
    clock_t t2 = clock();
    double elapsed_ms = ((double)(t2 - t0) / CLOCKS_PER_SEC) * 1000.0;

    if (post == -1) {
        printf(
            "    Post-compact alloc: FAILED — not enough contiguous space "
            "after compaction.\n");
        return false;
    }

    printf("    Post-compact alloc: SUCCESS (handle=%d)\n", post);
    printf("    Total time (compact + alloc): %.2f ms\n", elapsed_ms);

    // ── Phase 4: Validate data integrity of surviving handles ────────────────
    printf("\n  Phase 4 — Validate surviving handle data integrity\n");
    bool all_valid = true;
    for (int i = 0; i < keep_count; i++) {
        if (deref_handle(keep[i]) == -1) {
            all_valid = false;
            break;
        }
    }
    printf("    All %d surviving handles valid: %s\n", keep_count,
           all_valid ? "YES" : "NO");

    return (post != -1 && all_valid);
}

int main(void) {
    printf("============================================================\n");
    printf("MEMSIM — Level 4 Compaction Benchmark (C Version)\n");
    printf("============================================================\n");

    bool passed = run_compaction_benchmark();

    printf("\n------------------------------------------------------------\n");
    if (passed) {
        printf("  Compaction benchmark: PASSED\n\n");
        printf("  Copy the Phase output above into your README.\n");
        printf(
            "  Include the timing from YOUR machine (not the example "
            "output).\n");
    } else {
        printf("  Compaction benchmark: FAILED\n");
        printf(
            "  Check: does mem_compact() move all USED blocks to the front?\n");
        printf(
            "  Check: does it patch handle_table[] after moving each block?\n");
    }

    return 0;
}