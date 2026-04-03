CFLAGS ?= -Wall -Wextra -std=c11

TARGET = test_level_1
OBJS = level_1.o test_level_1.o

all: $(TARGET)

$(TARGET): $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)

