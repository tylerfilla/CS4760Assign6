#
# Tyler Filla
# CS 4760
# Assignment 3
#

CC=gcc
CFLAGS=-Wall -std=gnu99
LDFLAGS=

%.o: %.c
	$(CC) -c -o $@ $? $(CFLAGS)

oss: oss.o
	$(CC) -o $@ $? $(LDFLAGS)

all: oss

#
# Cleanup
#

clean:
	rm *.o oss

.PHONY: clean
