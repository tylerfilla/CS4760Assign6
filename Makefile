#
# Tyler Filla
# CS 4760
# Assignment 6
#

AR=ar
CC=gcc
CFLAGS=-Wall -std=gnu99
LDFLAGS=

EXECUTABLES=child oss
LIBRARIES=clock.a resmgr.a

#
# Patterns
#

%.o: %.c
	$(CC) -c $(CFLAGS) $< -o $@

%.a: %.o
	$(AR) rcs $@ $<

# One part of the project
%.part: %.o $(LIBRARIES)
	$(CC) -o $@ $^ $(LDFLAGS)

#
# Components
#

# 'child' and 'oss' executables
all: child.part oss.part
	mv child.part child
	mv oss.part oss

#
# Cleanup
#

clean:
	rm *.a *.o $(EXECUTABLES)

.PHONY: clean
.SECONDARY:
