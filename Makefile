CFLAGS ?= -Wall -Wextra -std=c99 -O2

# List all the final executables
TARGETS = test_level_1 test_level_2 test_level_3 test_level_4

all: $(TARGETS)

test_level_1: malloc.o test_level_1.o
	$(CC) $(CFLAGS) -o $@ $^

test_level_2: malloc.o test_level_2.o
	$(CC) $(CFLAGS) -o $@ $^

test_level_3: malloc.o test_level_3.o
	$(CC) $(CFLAGS) -o $@ $^

test_level_4: malloc.o test_level_4.o
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f *.o $(TARGETS)