CFLAGS ?= -Wall -Wextra -std=c11

TARGET = test_level_4
OBJS = malloc.o test_level_4.o

all: $(TARGET)

$(TARGET): $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)

