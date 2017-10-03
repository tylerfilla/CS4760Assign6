#
# Tyler Filla
# CS 4760
# Assignment 3
#

AR=ar
CC=gcc
CFLAGS=-Wall -std=gnu99
LDFLAGS=

%.o: %.c
	$(CC) -c -o $@ $? $(CFLAGS)

#
# Components
#

all: oss

# clock static library
clock.a: clock.o
	$(AR) rcs $@ $?

# oss executable
oss: oss.o clock.a
	$(CC) -o $@ $? $(LDFLAGS)

#
# Cleanup
#

clean:
	rm *.a *.o oss

.PHONY: clean
