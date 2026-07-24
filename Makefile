CFLAGS ?= -Wall -Wextra -std=c11 -O2

all: test_allocator

test_allocator: allocator.c test_allocator.c
	$(CC) $(CFLAGS) -o $@ $^

# Legacy target to build the historical hackathon snapshots
progression: progression/level_1.c progression/test_level_1.c \
             progression/level_2.c progression/test_level_2.c \
             progression/level_3.c progression/test_level_3.c \
             progression/level_4.c progression/test_level_4.c
	$(CC) $(CFLAGS) -o progression/test_level_1 progression/level_1.c progression/test_level_1.c
	$(CC) $(CFLAGS) -o progression/test_level_2 progression/level_2.c progression/test_level_2.c
	$(CC) $(CFLAGS) -o progression/test_level_3 progression/level_3.c progression/test_level_3.c
	$(CC) $(CFLAGS) -o progression/test_level_4 progression/level_4.c progression/test_level_4.c

clean:
	rm -f test_allocator progression/test_level_1 progression/test_level_2 progression/test_level_3 progression/test_level_4
