.PHONY: all clean

CC=$(CROSS_COMPILE)gcc
CFLAGS=-Wall -Wextra -ggdb

all: writer

writer: writer.c
	$(CC) $(CFLAGS) -o writer writer.c

clean:
	rm -rf writer
