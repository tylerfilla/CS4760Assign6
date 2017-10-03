/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <stdlib.h>
#include "clock.h"

int main(int argc, char* argv[])
{
    a_clock_t* clock = a_clock_new();
    a_clock_delete(clock);
    return 0;
}
