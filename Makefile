CFLAGS ?= -Wall -Wextra -std=c11 -O2

# List all the final executables
TARGETS = test_level_1 test_level_2 test_level_3 test_level_4

all: $(TARGETS)

test_level_1: level_1.c test_level_1.c
	$(CC) $(CFLAGS) -o $@ $^

test_level_2: level_2.c test_level_2.c
	$(CC) $(CFLAGS) -o $@ $^

test_level_3: level_3.c test_level_3.c
	$(CC) $(CFLAGS) -o $@ $^

test_level_4: level_4.c test_level_4.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(TARGETS)