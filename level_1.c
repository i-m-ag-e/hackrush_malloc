/*
 * MEMSIM — HackRush Starter Harness (C)
 * ======================================
 * Fill in every function marked TODO.
 * Do NOT change any function signatures — the hidden test suite links against
 * this translation unit.
 *
 * Header layout (8 bytes per block, embedded in ram[]):
 *   Bytes 0-3 : size     (uint32_t) — usable bytes (excluding header)
 *   Byte  4   : status   (uint8_t)  — FREE=0, USED=1
 *   Bytes 5-6 : task_id  (uint16_t) — 0 if unowned
 *   Byte  7   : checksum (uint8_t)  — XOR of bytes 0..6
 *
 * Compile:  gcc -std=c99 -Wall -o memsim starter_harness.c
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Device constants (do not change) ────────────────────────────────────────
 */
#define DEVICE_RAM (64 * 1024)
#define MIN_ALLOC 4
#define MAX_ALLOC 4096
#define ALIGNMENT 4
#define HEADER_SIZE 8

#define STATUS_FREE 0
#define STATUS_USED 1

/* ── Global RAM ───────────────────────────────────────────────────────────────
 */
uint8_t ram[DEVICE_RAM];

/* ── Header layout (packed, no compiler padding) ──────────────────────────────
 */
#pragma pack(push, 1)
typedef struct {
    uint32_t size;  /* usable bytes */
    uint8_t status; /* FREE or USED */
    uint16_t task_id;
    uint8_t checksum; /* XOR of bytes 0..6 */
} BlockHeader;
#pragma pack(pop)

/* ── Header helpers
 * ──────────────────────────────────────────────────────────── */

static uint8_t _compute_checksum(uint32_t size, uint8_t status,
                                 uint16_t task_id) {
    uint8_t cs = 0;
    uint8_t buf[7];
    memcpy(buf, &size, 4);
    buf[4] = status;
    memcpy(buf + 5, &task_id, 2);
    for (int i = 0; i < 7; i++)
        cs ^= buf[i];
    return cs;
}

static void _write_header(int offset, uint32_t size, uint8_t status,
                          uint16_t task_id) {
    BlockHeader *h = (BlockHeader *)(ram + offset);
    h->size = size;
    h->status = status;
    h->task_id = task_id;
    h->checksum = _compute_checksum(size, status, task_id);
}

static BlockHeader _read_header(int offset, bool *checksum_ok) {
    BlockHeader h;
    memcpy(&h, ram + offset, HEADER_SIZE);
    uint8_t expected = _compute_checksum(h.size, h.status, h.task_id);
    if (checksum_ok)
        *checksum_ok = (h.checksum == expected);
    return h;
}

#define BLOCK_SIZE 64
#define N_BLOCKS ((DEVICE_RAM) / (BLOCK_SIZE))

int16_t used_blocks[N_BLOCKS];

static int required_blocks(int n_bytes);
static int find_free_blocks(int n_blocks);

/* ════════════════════════════════════════════════════════════════════════════
 * LEVEL 1 + 2 — Core allocator
 * ════════════════════════════════════════════════════════════════════════════
 */

void mem_init(void) {
    /*
     * Initialise the heap.
     * Write a single free block spanning the entire RAM minus its header.
     * TODO
     */
    memset(ram, 0, DEVICE_RAM);
    memset(used_blocks, 0, sizeof(used_blocks));
}

/*
 * Allocate n_bytes of usable memory.
 * strategy: 0 = first_fit, 1 = best_fit
 * Returns offset to first usable byte (header_offset + HEADER_SIZE), or -1.
 */
int mem_alloc(int n_bytes, int task_id, int strategy) {
    (void)task_id;
    (void)strategy;

    if (n_bytes < MIN_ALLOC || n_bytes > MAX_ALLOC)
        return -1;

    int blocks_needed = required_blocks(n_bytes);
    int free_blocks_offset = find_free_blocks(blocks_needed);

    if (free_blocks_offset == -1)
        return -1;

    // set the first unused block to tell the number of blocks used in a single
    // allocation
    used_blocks[free_blocks_offset] = blocks_needed;
    for (int i = 1; i < blocks_needed; ++i) {
        // set all (n - 1) blocks after that to -1
        // we don't necessarily need to do this. It just makes things simpler
        used_blocks[free_blocks_offset + i] = -1;
    }

    return free_blocks_offset * 64;
}

/*
 * Free the block whose usable area starts at ptr.
 * Coalesce with the immediately following free block (Level 2).
 * Returns true on success.
 */
bool mem_free(int ptr) {
    /* TODO */
    (void)ptr;

    if (ptr < 0 || ptr > DEVICE_RAM)
        return false;

    // since we only allocate blocks of 64, we can only free blocks of 64
    if (ptr % 64 != 0)
        return false;

    int block_index = ptr / 64;

    int blocks_to_free = used_blocks[block_index];
    // cannot free from the middle of an allocated block (-1), or an unallocated
    // block (0)
    if (blocks_to_free <= 0)
        return false;

    for (int i = 0; i < blocks_to_free; ++i) {
        used_blocks[block_index + i] = 0;
    }

    return true;
}

void mem_dump(void) {
    /*
     * Required output format:
     *   [offset=0000  size=65528  status=FREE  task=0   csum=OK ]
     *   ...
     *   HEAP SUMMARY: N blocks | M used | K free | B free bytes
     * TODO
     */

    int free_blocks = 0;
    for (int i = 0; i < N_BLOCKS; ++i) {
        if (used_blocks[i] == 0)
            free_blocks++;
    }

    for (int i = 0; i < N_BLOCKS; ++i) {
        printf("[offset=%04d  size=64  task=0  csum=OK  ]\n", i * 64);
    }

    printf("HEAP SUMMARY: %d blocks | %d used | %d free | %d free bytes\n",
           N_BLOCKS, N_BLOCKS - free_blocks, free_blocks, free_blocks * 64);
}

typedef struct {
    int total_free_bytes;
    int largest_free_block;
    int num_free_fragments;
    float fragmentation_ratio; /* 1 - (largest_free / total_free); 0 if total==0
                                */
} MemStats;

MemStats mem_stats(void) {
    /* TODO */
    MemStats s = {0, 0, 0, 0.0f};

    int free_blocks = 0;
    for (int i = 0; i < N_BLOCKS; ++i) {
        if (used_blocks[i] == 0)
            free_blocks++;
    }

    s.total_free_bytes = free_blocks * 64;
    s.largest_free_block = 64;
    return s;
}

/* ════════════════════════════════════════════════════════════════════════════
 * LEVEL 3 — Multi-task sandbox
 * ════════════════════════════════════════════════════════════════════════════
 */

typedef struct {
    int task_id;
    int quota_bytes;
    int bytes_allocated; /* peak allocated */
    bool quota_exceeded;
    /* leaks: scan after fn() returns for blocks still tagged with task_id */
} TaskReport;

typedef void (*TaskFn)(void);

TaskReport task_spawn(int task_id, int quota_bytes, TaskFn fn) {
    /* TODO */
    (void)task_id;
    (void)quota_bytes;
    (void)fn;
    TaskReport r = {task_id, quota_bytes, 0, false};
    return r;
}

/* ════════════════════════════════════════════════════════════════════════════
 * LEVEL 4 — Heap compaction + OOM recovery
 * ════════════════════════════════════════════════════════════════════════════
 */

#define MAX_HANDLES 4096

static int handle_table[MAX_HANDLES]; /* handle -> offset; -1 = invalid */
static int next_handle = 0;

void handles_init(void) {
    for (int i = 0; i < MAX_HANDLES; i++)
        handle_table[i] = -1;
    next_handle = 0;
}

/* Returns handle (>= 0) or -1 on failure. */
int mem_alloc_handle(int n_bytes, int task_id) {
    /* TODO */
    (void)n_bytes;
    (void)task_id;
    return -1;
}

bool mem_free_handle(int handle) {
    /* TODO */
    (void)handle;
    return false;
}

/* Returns current raw offset for handle, or -1. */
int deref_handle(int handle) {
    if (handle < 0 || handle >= MAX_HANDLES)
        return -1;
    return handle_table[handle];
}

/* Compact heap, patch handle_table. Returns bytes recovered. */
int mem_compact(void) {
    /* TODO */
    return 0;
}

/* Alloc with compaction + OOM eviction fallback. Returns handle or -1. */
int mem_alloc_or_compact(int n_bytes, int task_id) {
    /* TODO */
    (void)n_bytes;
    (void)task_id;
    return -1;
}

/* ════════════════════════════════════════════════════════════════════════════
 * Entry point — quick sanity check
 * ════════════════════════════════════════════════════════════════════════════
 */

// int main(void) {
//     mem_init();
//     int p1 = mem_alloc(100, 1, 0);
//     int p2 = mem_alloc(200, 2, 0);
//     int p3 = mem_alloc(50, 1, 0);
//     printf("Allocated: p1=%d  p2=%d  p3=%d\n", p1, p2, p3);
//     mem_free(p2);
//     mem_dump();
//     MemStats s = mem_stats();
//     printf("free=%d  largest=%d  frags=%d  ratio=%.3f\n", s.total_free_bytes,
//            s.largest_free_block, s.num_free_fragments,
//            s.fragmentation_ratio);
//     return 0;
// }

static int required_blocks(int n_bytes) {
    return (n_bytes / BLOCK_SIZE) + (bool)(n_bytes % BLOCK_SIZE);
}

static int find_free_blocks(int n_blocks) {
    int current_free_blocks = 0;

    for (int i = 0; i < N_BLOCKS; ++i) {
        if (used_blocks[i] == 0)
            current_free_blocks++;
        else
            current_free_blocks = 0;

        if (current_free_blocks == n_blocks)
            return i - current_free_blocks + 1;
    }

    return -1;
}
