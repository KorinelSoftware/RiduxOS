# Makefile for RiduxUI
# Compiles for FreeBSD (can be adapted for Linux)

CC = clang
CFLAGS = -Wall -Wextra -O2 -I./include
LDFLAGS = 

# Source files
SRCS = src/main.c src/flush.c
OBJS = $(SRCS:.c=.o)
TARGET = ridux-ui

# Default target
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
