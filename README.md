## Level 1

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