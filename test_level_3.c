#include <stdbool.h>
#include <stdio.h>

typedef struct {
    int task_id;
    int quota_bytes;
    int bytes_allocated; /* peak allocated */
    bool quota_exceeded;
    /* leaks: scan after fn() returns for blocks still tagged with task_id */
} TaskReport;
typedef void (*TaskFn)(void);

extern void mem_init();
extern int mem_alloc(int n_bytes, int task_id, int strategy);
extern bool mem_free(int ptr);
extern TaskReport task_spawn(int task_id, int quota_bytes, TaskFn fn);

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
