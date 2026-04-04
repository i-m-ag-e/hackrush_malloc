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

#define MAX_TASKS 256

typedef struct {
    int task_id;
    int quota_bytes;
    int bytes_allocated; /* peak allocated */
    bool quota_exceeded;
    /* leaks: scan after fn() returns for blocks still tagged with task_id */
} TaskReport;

typedef struct {
    TaskReport report;
    int current_alloc;
    bool active;
} TaskStatus;

static TaskStatus tasks[MAX_TASKS];

static int align_bytes(uint32_t n_bytes);
static int find_free_block(uint32_t aligned_n_byte, int strategy);
static void sweep_and_merge();
static bool task_is_active(int task_id);

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

    // check if this allocation would put us over quota before we do any work
    // we do this check later as well, but this is a quick early check to avoid
    // doing the work of finding a free block
    if (task_is_active(task_id) &&
        tasks[task_id].current_alloc + aligned_n_bytes + HEADER_SIZE >
            tasks[task_id].report.quota_bytes) {
        tasks[task_id].report.quota_exceeded = true;
        return -1;
    }

    int free_block = find_free_block(aligned_n_bytes, strategy);
    if (free_block == -1)
        return -1;

    bool checksum_ok;
    BlockHeader header = _read_header(free_block, &checksum_ok);

    int remaining_space = header.size - aligned_n_bytes;

    // after the split, do we have enough space left to fit another block with
    // its header and minimum alloc if not, we won't split and will just give
    // the whole block to the user
    bool will_split = remaining_space >= HEADER_SIZE + MIN_ALLOC;
    int given_size = will_split ? aligned_n_bytes : header.size;

    // this time, check against the actual size we would be allocating
    if (task_is_active(task_id) &&
        tasks[task_id].current_alloc + given_size + HEADER_SIZE >=
            tasks[task_id].report.quota_bytes) {
        tasks[task_id].report.quota_exceeded = true;
        return -1;
    }

    if (will_split) {
        _write_header(free_block, aligned_n_bytes, STATUS_USED, task_id);

        // split the current block and write a new header for the remaining free
        // space
        int next_free_location = free_block + HEADER_SIZE + aligned_n_bytes;
        _write_header(next_free_location, remaining_space - HEADER_SIZE,
                      STATUS_FREE, 0);
    } else {
        // give the whole block to the user without splitting
        _write_header(free_block, header.size, STATUS_USED, task_id);
    }

    if (task_is_active(task_id)) {
        tasks[task_id].current_alloc += given_size + HEADER_SIZE;

        if (tasks[task_id].current_alloc >
            tasks[task_id].report.bytes_allocated) {
            tasks[task_id].report.bytes_allocated =
                tasks[task_id].current_alloc;
        }
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

    // if checksum is bad, the user is probably trying to free a pointer that is
    // in the middle of a block, or just a random number otherwise, if the block
    // is already free, then it is a double free
    if (!checksum_ok || header.status == STATUS_FREE)
        return false;

    _write_header(ptr - HEADER_SIZE, header.size, STATUS_FREE, header.task_id);
    sweep_and_merge();

    if (header.task_id > 0 && task_is_active(header.task_id)) {
        tasks[header.task_id].current_alloc -= header.size + HEADER_SIZE;
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

typedef void (*TaskFn)(void);

TaskReport task_spawn(int task_id, int quota_bytes, TaskFn fn) {
    tasks[task_id] = (TaskStatus){.report = {.task_id = task_id,
                                             .bytes_allocated = 0,
                                             .quota_bytes = quota_bytes,
                                             .quota_exceeded = false},
                                  .current_alloc = 0,
                                  .active = true};

    fn();

    // if task still has allocated blocks, report them as leaks
    if (tasks[task_id].current_alloc != 0) {
        printf("Task %d leaked %d bytes\n", task_id,
               tasks[task_id].current_alloc);
    }

    tasks[task_id].active = false;

    return tasks[task_id].report;
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

void good_task() {
    int p = mem_alloc(100, 1, 0);  // Task 1 asks for 100
    mem_free(p);                   // Cleans up after itself
}

void leaky_task() {
    mem_alloc(200, 2, 0);  // Task 2 allocates, but never calls free()
}

void greedy_task() {
    // Task 3 tries to allocate 3000 bytes three times (9000 total)
    mem_alloc(3000, 3, 0);  // 1st call: Success (Usage: ~3000)
    mem_alloc(3000, 3, 0);  // 2nd call: Success (Usage: ~6000)
    mem_alloc(3000, 3, 0);  // 3rd call: Watchdog triggers! (9000 > 8192)
}

void complex_worker() {
    // THIS TEST WAS GENERATED USING GenAI

    printf("\n--- Starting Complex Worker (Task 4) ---\n");
    int live_cache_ptrs[5];

    // Loop 5 times, simulating processing 5 chunks of data
    for (int i = 0; i < 5; i++) {
        // 1. Allocate a large temporary buffer
        int temp_ptr = mem_alloc(1000, 4, 0);
        if (temp_ptr != -1) {
            // ... (Imagine we process data in this buffer) ...

            // Immediately free the temp buffer.
            // This keeps our "current_allocated" math bouncing up and down!
            mem_free(temp_ptr);
        }

        // 2. Allocate a smaller "cache" block to save the results
        int cache_ptr = mem_alloc(200, 4, 0);
        if (cache_ptr != -1) {
            // We store the pointer but NEVER call mem_free on it.
            // This creates a slow, creeping memory leak.
            live_cache_ptrs[i] = cache_ptr;
        }
    }

    // At this point, the loop is done.
    // We allocated and freed 1000 bytes 5 times successfully.
    // But we left behind FIVE 200-byte cache blocks.

    // 3. The greedy finish.
    // Let's ask for 3500 bytes. Because we leaked so much cache memory,
    // this request will push us over our 4096-byte quota!
    printf("  [Task 4] Attempting final large allocation of 3500 bytes...\n");
    int greedy_ptr = mem_alloc(3500, 4, 0);

    if (greedy_ptr == -1) {
        printf("  [Task 4] Allocation failed! Watchdog caught us.\n");
    }
}

int main(void) {
    mem_init();

    TaskReport r1 = task_spawn(1, 8192, good_task);
    TaskReport r2 = task_spawn(2, 8192, leaky_task);
    TaskReport r3 = task_spawn(3, 8192, greedy_task);

    printf(
        "Report Task 1: Quota %d bytes, Peak Allocated %d bytes, Quota "
        "Exceeded: %s\n",
        r1.quota_bytes, r1.bytes_allocated, r1.quota_exceeded ? "YES" : "NO");
    printf(
        "Report Task 2: Quota %d bytes, Peak Allocated %d bytes, Quota "
        "Exceeded: %s\n",
        r2.quota_bytes, r2.bytes_allocated, r2.quota_exceeded ? "YES" : "NO");
    printf(
        "Report Task 3: Quota %d bytes, Peak Allocated %d bytes, Quota "
        "Exceeded: %s\n",
        r3.quota_bytes, r3.bytes_allocated, r3.quota_exceeded ? "YES" : "NO");
    // Run the complex simulation with a 4KB Quota
    TaskReport r4 = task_spawn(4, 4096, complex_worker);

    printf(
        "Report Task 4: Quota %d bytes, Peak Allocated %d bytes, Quota "
        "Exceeded: %s\n",
        r4.quota_bytes, r4.bytes_allocated, r4.quota_exceeded ? "YES" : "NO");
    return 0;
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

        // if block is free and large enough
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
        // go to next block
        offset += header.size + HEADER_SIZE;
    }

    // if strategy was 0 and something was found, we would have returned already
    // else if didn't find anything, we return -1, which is correct for both
    // strategies the only case left is strategy 1 and we found something, so we
    // return best_fit
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
                // reflect the merge in the header of the previous free block
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

static bool task_is_active(int task_id) {
    return task_id > 0 && task_id < MAX_TASKS && tasks[task_id].active;
}
