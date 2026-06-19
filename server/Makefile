.PHONY: all clean

CC=$(CROSS_COMPILE)cc
CFLAGS=-Wall -Wextra

all: aesdsocket

aesdsocket: aesdsocket.c
	$(CC) -o aesdsocket aesdsocket.c $(CFLAGS)

clean:
	rm -rf aesdsocket
