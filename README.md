# HackRush Malloc

This is my C implementation of a memory allocator, built for HackRush — a hackathon at IIT Gandhinagar.

The organizers provided the starting files: fixed function signatures and a `64KB` simulated RAM array (`uint8_t ram[]`) standing in for a memory-constrained device, with no access to `sbrk` or `mmap`. Everything beyond those signatures is my own design: the header layout, the splitting and coalescing logic, task quotas, handle-based indirection, and heap compaction.

### Usage

A C compiler supporting C99/C11 and `make` are required.

```shell
git clone https://github.com/i-m-ag-e/hackrush_malloc.git
cd hackrush_malloc
make 

```

To run the consolidated test suite and benchmarks:

```shell
./test_allocator

```

## Architecture & Design

The allocator (`allocator.c`) is designed to handle severe fragmentation, track task-specific quotas, and recover from Out-Of-Memory (OOM) scenarios via heap compaction.

### Header Layout & Checksum Validation

Every allocated and free block is prefixed with an 8-byte packed header embedded directly in the `ram[]` array:

* **Bytes 0-3**: Usable block size (`uint32_t`)
* **Byte 4**: Status (`FREE=0`, `USED=1`)
* **Bytes 5-6**: Task ID (`uint16_t`)
* **Byte 7**: Checksum (`uint8_t`)

The checksum (an XOR of bytes 0-6) prevents silent memory corruption. If a user attempts to free an invalid pointer (e.g., pointing into the middle of a block), the checksum validation fails and the double-free or invalid-free is safely rejected.

### Block Splitting & Fragmentation Strategy

When a block larger than requested is found, it is split into a `USED` block and a new `FREE` block (provided the remaining space is large enough to hold a header + minimum allocation). Adjacent free blocks are coalesced automatically on every `mem_free()`.

The allocator supports both first-fit and best-fit strategies. Benchmarking against an alternating small/large workload yields the following:

```text
  First-Fit -> Ratio: 0.033 | Fragments: 2
  Best-Fit  -> Ratio: 0.000 | Fragments: 1
  Winner: best_fit

```

*Why best-fit wins here:* The benchmark heavily saturates the heap. When a small allocation is requested, first-fit carves it out of the first available large block, eroding the contiguous space needed for larger (4096 byte) requests. Best-fit instead fills the small holes left behind by previous frees, which minimizes unusable fragments.

### Per-Task Quotas & Leak Detection

To prevent runaway processes in a shared environment, the allocator enforces strict per-task memory quotas.

* Before any block is assigned, the requested size (plus header overhead) is checked against the active task's allowed maximum.
* If a task attempts to exceed its quota, the allocation is rejected (`watchdog triggered`).
* When a task exits, the allocator scans the heap. Any blocks still tagged with the task's ID are reported as memory leaks.

### Handles, Heap Compaction & OOM Eviction

To handle fragmentation where total free memory is sufficient but contiguous space is not, the allocator can compact the heap.

Compaction relies on handle-based indirection. Instead of returning raw pointers, `mem_alloc_handle` returns an integer index, and the actual pointer is resolved through a handle table. This lets blocks move without invalidating references the caller holds.

If an allocation fails:

1. **Compaction:** A two-pointer sweep (`read_head` and `write_head`) slides all `USED` blocks to the front of the heap, updating each handle-table entry as its block moves. The remaining `FREE` space is consolidated into a single block at the end.
2. **Eviction:** If compaction still does not free enough contiguous space, the allocator selects the idle task with the largest footprint, reclaims all of its blocks, invalidates its handles, and retries the request.

---

### Progression Note

The `progression/` directory contains the raw historical snapshots (`level_1.c` through `level_4.c`) from the hackathon, demonstrating how the allocator's design evolved incrementally from a basic fixed-block array to a handle-based compacting heap. You can build these legacy tests using `make progression`.

*Note: the test suite (`test_allocator.c`) started as Python test/benchmark scripts provided by the hackathon; I used AI to convert them to C and fix them up.*