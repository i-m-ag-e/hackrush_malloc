CFLAGS ?= -Wall -Wextra -std=c11

TARGET = malloc
OBJS = malloc.o 

all: $(TARGET)

$(TARGET): $(OBJS)

clean:
	rm -f $(OBJS) $(TARGET)

