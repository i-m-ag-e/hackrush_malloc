### Usage

`make` and a C compiler supporting C99 is needed (this can be changed in the Makefile) for the following instructions

```shell
git clone https://github.com/i-m-ag-e/hackrush_malloc.git
make 
# this creates 4 executables
# - test_level_1
# - test_level_2
# - test_level_3
# - test_level_4

# specific target can be made by running `make <target_name>`, e.g., `make test_level_1`
```

To run the tests
```
./test_level_1
./test_level_2
./test_level_3
./test_level_4
```

PS: I have comments in the code to explain where I felt explanation was necessaey. The comments were not added by AI. I have explicitly mentioned what has been written by AI in the functions or in the README (specifically, the benchmarks and the conversion of the stress tests to C has been done by AI). I have used AI to learn and to see how to do specific things. 

## Level 1

`test_level_1.c` is the `level1_stress_test.py` file converted to C (using GenAI). For this, the `main()` function in the initial starter file has been commented out. 

### Description

For level 1, I have declared a global `used_blocks` array of `int16_t`. I have used `int16_t` instead of `bool` as suggested, because for requested bytes >64, we will need to free more than 1 block at a time (at the time of freeing). So, `used_blocks[i]` will store the number of blocks that are currently being used starting from block `i`. For example,
- `used_blocks[0] = 3` means that blocks 0, 1 and 2 are currently being used. (further, `used_blocks[1]` and `used_blocks[2]` are set to -1 to denote that they are being used as part of a larger allocation)
- `used_blocks[0] = 0` means that block 0 is currently free

Below I have highlighted what I have for implementing each function: 

- `mem_init`: Sets all `used_blocks[i]` to 0, indicating that all blocks are free at the start.
- `mem_alloc`:
    - First, I find the number of blocks needed for the required bytes.
    - Then, I iterate through the `used_blocks` array to find a contiguous sequence of free blocks that can accommodate the required number of blocks.
    - If such a sequence is found, I do what I have described in the example above.
    - Else, I return -1  
- `mem_free`:
    - I first check if the given pointer is valid (i.e., it is less than `MAX_ALLOC` and greater than `MIN_ALLOC` and is divisible by 64).
    - If the pointer is valid, I find the `block_index` corresponding to the pointer. I get the amount of blocks to "deallocate" from `used_blocks[block_index]` (`n`). I set `used_blocks[block_index]` and subsequent `n - 1` blocks to 0.
- `mem_stats`: I iterate through the `used_blocks` array and count the number of free blocks. Using that I set the `free_bytes` accordingly and `largest_free_block` to 64. I have ignored the other fields for now. 

## Level 2

For level 2, I have implemented fragmentation of the heap and coalescing of free blocks. I have also implemented the `mem_stats` function to return the correct values for all fields. The `used_block` array has been removed.

- `mem_init`: Initially, the RAM is one big free block with a header.
- `mem_alloc`:
    - I iterate through the heap to find a free block that can accommodate the required bytes. If such a block is found, I split it into an allocated block and a free block (if there is enough space left for the free block). I update the headers accordingly (Strategy 0). For Strategy 1, I find the best fit block instead of the first fit block.
    - **One important detail**: In case after splitting the block, the remaining free block is smaller than the header size, I will not split the block and will give the entire block to the user. This is to avoid creating unusable tiny free blocks.
- `mem_free`:
    - I perform the bounds check as in level 1. Then I read the header of the block to be freed. 
    - If the checksum of the header is not correct, it means that user is trying to free a memory that does not contain a header (probably trying to free a pointer that is not the start of an allocated block). If that is the case, or if the block is already free, return false.
    - Else, I mark the block as free and coalesce it with adjacent free blocks if they exist (see `sweep_and_merge`).
- `mem_stats`: I iterate through the heap and count the number of free blocks, the total free bytes, and the largest free block. I set the `free_bytes` and `largest_free_block` accordingly. The other fields are set to their expected values now.
- `sweep_and_merge`: I iterate through the heap and coalesce adjacent free blocks into a single free block. I update the header of the previous free block accordingly. The merged free block becomes the "previous free block" for the next iteration.

### Test output
I have tested my implementation using the provided test files (converted to C using GenAI). The output of `test_level_2` is as follows:

```
============================================================
MEMSIM — Level 2 Fragmentation Benchmark (C Version)
============================================================

  Strategy: first_fit
    Alloc calls succeeded : 576
    Alloc failed (OOM)    : 424
    Free calls            : 300
    Final free bytes      : 364
    Largest free block    : 352
    Free fragments        : 2
    Fragmentation ratio   : 0.033

  Strategy: best_fit
    Alloc calls succeeded : 575
    Alloc failed (OOM)    : 425
    Free calls            : 300
    Final free bytes      : 352
    Largest free block    : 352
    Free fragments        : 1
    Fragmentation ratio   : 0.000

------------------------------------------------------------
  Winner: best_fit  (delta = 0.0330)
```

The benchmark has alternating small/large that heavily saturates the heap. So, `best_fit` slightly outperforms `first_fit` because leaves smaller gaps than `first_fit`. When a small allocation (e.g., 10 bytes) is requested, `first_fit` will get that space out of the first available large block, slowly taking away the contiguous spaces needed for the large (4096 byte) requests. `best_fit`, however, searches the entire heap to find the tightest possible gap left behind by previous frees. It perfectly plugs the small holes with small allocations and minimizes the creation of tiny, unusable pieces of memory (leaving only 1 fragment compared to First-Fit's 2).


### Level 3

For level 3, I have added a new struct `TaskStatus`, that contains a `TaskReport` struct and two additional fields - `is_active` and `current_alloc`, to keep track of if the task is active and how many bytes it has currently allocated. 
I have declared a global array `tasks` to keep track of the status of each task.

**Note**: I have currently imposed the limit of 255 maximum tasks (since the global array has to be static because we are the memory allocator). This can be changed by changing the value of the `MAX_TASKS` macro.

- `task_spawn`: 
    - I set `tasks[task_id]` to a `TaskStatus` with the corresponding fields filled as arguments (`task_id`, `quota_bytes`) or as default values and set it as active. 
    - Then I call the function, everything else is handled by the allocationn and free functions themselves.
    - After the function exists, I check if the task still has memory allocated to it (i.e., `current_alloc` is not 0). If it does, I report that this task has leaked memory.
    - The `TaskReport` has been filled by the alloc/free functions, so I just return that.

- `mem_alloc`: 
    - I first check if the `task_id` is valid and active. If not, I return -1.
    - Then I check if the requested allocation would exceed the task's quota. If it does, I mark `task.quota_exceeded` to `true` and return -1.
    - After searching for a big enough free block for the requested allocation, I check for the tasks's quota again, but this time against the actual size we would be allocating (which can be bigger than the requested size in case we give it the entire block, due to not having enough space for a header after splitting (see Level 2)). If it exceeds the quota, I mark `task.quota_exceeded` to `true` and return -1.
    - Else, I proceed with the allocation as before. If the allocation is successful, I update the `current_alloc` for that task (and the `bytes_allocated` in the `TaskReport`).

- `mem_free`:
    - Same as Level 2 except - I check that the task is active before freeing, and subtract the freed bytes from `current_alloc` for that task.

- I have included 3 functions that each demonstrate a different scenario of memory leaks and quota exceedance. These are run in the `main()` function. The "tasks" are supposed to:
    - Task 1: Allocate 100 bytes with a quota of 8192 and free it. (should succeed)
    - Task 2: Allocate 200 bytes with a quota of 8192 and not free it. (should leak memory)
    - Task 3: Allocate 9000 bytes in 3 allocations, with a quota of 8192. (should exceed quota and leak memory because it's not freed)
    - Task 4: Allocates some memory, frees some memory, in a loop. In the end, it exceeds quota due to not freeing memory before. (should exceed quota and leak memory)

The output of the `main()` function is as follows:

```
Task 2 leaked 208 bytes
Task 3 leaked 6016 bytes
Report Task 1: Quota 8192 bytes, Peak Allocated 108 bytes, Quota Exceeded: NO
Report Task 2: Quota 8192 bytes, Peak Allocated 208 bytes, Quota Exceeded: NO
Report Task 3: Quota 8192 bytes, Peak Allocated 6016 bytes, Quota Exceeded: YES

--- Starting Complex Worker (Task 4) ---
  [Task 4] Attempting final large allocation of 3500 bytes...
  [Task 4] Allocation failed! Watchdog caught us.
Task 4 leaked 1040 bytes
Report Task 4: Quota 4096 bytes, Peak Allocated 1840 bytes, Quota Exceeded: YES
```

### Level 4

For level 4, I have implemented the `handle` interface and heap compaction. 

- `mem_alloc_handle`:
    - I find throught the handles to find a free handle. 
    - Then, I allocate memory and map the `handle` to the allocated pointer. If allocation fails or I can't find a handle, I return -1.
    - Otherwise, I return the handle index.

- `mem_free_handle`:
    - I check if the handle is valid and mapped to an allocated pointer.
    - If it is valid, I free the corresponding pointer and mark the handle as free. I return true if successful, false otherwise.

- `mem_compact`:
    - I use two pointers to compact the heap - `read_head` and `write_head`. 
    - The `read_head` iterates through the heap as normal. The `write_head` only moves when we encounter an allocated block. 
    - When we encounter an allocated block, if `read_head` and `write_head` are not the same, we move the allocated block to the `write_head` position and update the corresponding handle to point to the new location. We also update the header of the moved block.
    - This way, by the end, we will have all the allocated blocks moved to the beginning of the heap, and all the free space will be consolidated into one big free block at the end of the heap.
    - After iterating through the heap, we write a new free block at the `write_head` position with the remaining free space and update the corresponding header.
    - This compaction should help in salvaging blocks of memory that have been put in between allocated blocks and are not large enough themselves to be used for new allocations. 

- `mem_alloc_or_compact`:
    - This function logs the eviction as asked.
    - This function first tries to allocate memory as normal. 
    - If allocation fails, it triggers compaction and then tries to allocate again.
    - If it still fails after compaction, we try to evict the task with the largest current allocation and claim all its memory. For that, I loop through the tasks to find the active task with the largest `current_alloc`. If such a task is found, I free all its allocated blocks, invalidate all its handles, and mark it as evicted (i.e., set `is_active` to false and `current_alloc` to 0). 
    - After evicting the largest task, I try to allocate again. If it still fails, then I return -1.

- I have tested this against the provided `test_level_4.c` (converted from `test_level_4.py` using GenAI) file. The output is as follows:

```
============================================================
MEMSIM — Level 4 Compaction Benchmark (C Version)
============================================================

  Phase 1 — Fill to ~90%
    Allocated 819 handles, ~58968 bytes used

  Phase 2 — Free every other handle (create fragmentation)
    Freed 410 handles — heap is now fragmented

  Phase 3 — Request large contiguous block (8192 bytes)
    Direct alloc: FAILED (fragmented, as expected)
    Compacting... recovered 36088 bytes  (0.46 ms)
    Post-compact alloc: SUCCESS (handle=0)
    Total time (compact + alloc): 0.51 ms

  Phase 4 — Validate surviving handle data integrity
    All 409 surviving handles valid: YES

------------------------------------------------------------
  Compaction benchmark: PASSED
```

