CFLAGS ?= -Wall -Wextra -std=c11

TARGET = test_level_2
OBJS = malloc.o test_level_2.o

all: $(TARGET)

$(TARGET): $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)

