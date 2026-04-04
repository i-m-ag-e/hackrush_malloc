## Level 1

### Usage
`make` and a C compiler supporting C17 is needed (this can be changed in the Makefile) for the following instructions

```shell
git clone https://github.com/i-m-ag-e/hackrush_malloc.git
git checkout level_1
make # creates an executable test_level_1
```

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
