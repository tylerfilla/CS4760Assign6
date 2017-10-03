/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include "clock.h"
#include <stdio.h>

a_clock_t* a_clock_construct(a_clock_t* clock)
{
    printf("construct\n");
    return clock;
}

a_clock_t* a_clock_destruct(a_clock_t* clock)
{
    printf("destruct\n");
    return clock;
}
