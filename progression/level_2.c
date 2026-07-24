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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

static int align_bytes(uint32_t n_bytes);
static int find_free_block(uint32_t aligned_n_byte, int strategy);
static void sweep_and_merge();

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
    _write_header(0, DEVICE_RAM - HEADER_SIZE, STATUS_FREE, 0);
}

/*
 * Allocate n_bytes of usable memory.
 * strategy: 0 = first_fit, 1 = best_fit
 * Returns offset to first usable byte (header_offset + HEADER_SIZE), or -1.
 */
int mem_alloc(int n_bytes, int task_id, int strategy) {
    (void)strategy;

    if (n_bytes < MIN_ALLOC || n_bytes > MAX_ALLOC)
        return -1;

    int aligned_n_bytes = align_bytes(n_bytes);
    int free_block = find_free_block(aligned_n_bytes, strategy);

    if (free_block == -1)
        return -1;

    bool checksum_ok;
    BlockHeader header = _read_header(free_block, &checksum_ok);

    int remaining_space = header.size - aligned_n_bytes;

    if (remaining_space >= HEADER_SIZE + MIN_ALLOC) {
        _write_header(free_block, aligned_n_bytes, STATUS_USED, task_id);

        int next_free_location = free_block + HEADER_SIZE + aligned_n_bytes;
        _write_header(next_free_location, remaining_space - HEADER_SIZE,
                      STATUS_FREE, 0);
    } else {
        _write_header(free_block, header.size, STATUS_USED, task_id);
    }

    return free_block + HEADER_SIZE;
}

/*
 * Free the block whose usable area starts at ptr.
 * Coalesce with the immediately following free block (Level 2).
 * Returns true on success.
 */
bool mem_free(int ptr) {
    /* TODO */

    if (ptr < HEADER_SIZE || ptr > DEVICE_RAM - HEADER_SIZE)
        return false;

    bool checksum_ok;
    BlockHeader header = _read_header(ptr - HEADER_SIZE, &checksum_ok);

    if (!checksum_ok || header.status == STATUS_FREE)
        return false;

    _write_header(ptr - HEADER_SIZE, header.size, STATUS_FREE, header.task_id);
    sweep_and_merge();

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

    int offset = 0;
    int free_blocks = 0, used_blocks = 0;
    int free_bytes = 0;

    while (DEVICE_RAM - HEADER_SIZE > offset) {
        bool checksum_ok;
        BlockHeader header = _read_header(offset, &checksum_ok);

        printf(
            "[  offset=%04d  size=%-5d  status=%-s  task=%-3d  csum=%-3s  ]\n",
            offset, header.size, header.status == STATUS_FREE ? "FREE" : "USED",
            header.task_id, checksum_ok ? "OK" : "BAD");
        offset += header.size + HEADER_SIZE;

        if (header.status == STATUS_FREE) {
            free_blocks++;
            free_bytes += header.size;
        } else
            used_blocks++;
    }

    printf("HEAP SUMMARY: %d blocks | %d used | %d free | %d free bytes\n",
           used_blocks + free_blocks, used_blocks, free_blocks, free_bytes);
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

    int offset = 0;
    while (DEVICE_RAM - HEADER_SIZE > offset) {
        bool checksum_ok;
        BlockHeader header = _read_header(offset, &checksum_ok);
        offset += header.size + HEADER_SIZE;

        if (header.status == STATUS_FREE) {
            s.total_free_bytes += header.size;
            if (s.largest_free_block < (int)header.size)
                s.largest_free_block = header.size;
            s.num_free_fragments++;
        }
    }

    s.fragmentation_ratio =
        s.total_free_bytes == 0
            ? 0
            : 1 - s.largest_free_block / (double)s.total_free_bytes;
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

static int align_bytes(uint32_t n_bytes) {
    // align to multiples of 4
    return (n_bytes + 3) & ~3;
}

static int find_free_block(uint32_t aligned_n_bytes, int strategy) {
    assert(aligned_n_bytes % 4 == 0);

    int offset = 0;
    int best_fit = -1, best_fit_diff = 0;
    while (offset < DEVICE_RAM) {
        bool checksum_ok;
        BlockHeader header = _read_header(offset, &checksum_ok);

        if (header.status == STATUS_FREE && header.size >= aligned_n_bytes) {
            if (strategy == 0 || header.size == aligned_n_bytes)
                return offset;
            else if (strategy == 1 &&
                     (best_fit == -1 || header.size - aligned_n_bytes <
                                            (uint32_t)best_fit_diff)) {
                best_fit_diff = header.size - aligned_n_bytes;
                best_fit = offset;
            }
        }
        offset += header.size + HEADER_SIZE;
    }

    return best_fit;
}

static void sweep_and_merge() {
    int offset = 0;

    int prev_free = -1;
    BlockHeader prev_free_header;
    while (DEVICE_RAM - HEADER_SIZE > offset) {
        bool checksum_ok;
        BlockHeader header = _read_header(offset, &checksum_ok);

        if (header.status == STATUS_FREE) {
            if (prev_free != -1) {
                prev_free_header.size += header.size + HEADER_SIZE;
                _write_header(prev_free, prev_free_header.size, STATUS_FREE, 0);
            } else {
                prev_free = offset;
                prev_free_header = header;
            }
        } else {
            prev_free = -1;
        }

        offset += header.size + HEADER_SIZE;
    }
}
