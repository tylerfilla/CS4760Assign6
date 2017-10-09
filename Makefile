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

# One part of the project
%.part: %.o clock.a
	$(CC) -o $@ $^ $(LDFLAGS)

#
# Components
#

all: child.part oss.part
	mv child.part child
	mv oss.part oss

#
# Cleanup
#

clean:
	rm *.a *.o child oss

.PHONY: clean
.SECONDARY:
