.PHONY: all clean

CC=$(CROSS_COMPILE)cc
CFLAGS=-Wall -Wextra
LDFLAGS=-lpthread

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) -o aesdsocket aesdsocket.c $(CFLAGS) $(LDFLAGS)

clean:
	rm -rf aesdsocket
