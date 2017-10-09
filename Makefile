#
# Tyler Filla
# CS 4760
# Assignment 3
#

AR=ar
CC=gcc
CFLAGS=-Wall -std=gnu99
LDFLAGS=

#
# Patterns
#

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.a: %.o
	$(AR) rcs $@ $<

#
# Components
#

all: oss

# 'oss' executable
oss: oss.o clock.a
	$(CC) -o $@ $^ $(LDFLAGS)

#
# Cleanup
#

clean:
	rm *.a *.o oss

.PHONY: clean
