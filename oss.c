/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <stdlib.h>
#include "clock.h"

int main(int argc, char* argv[])
{
    // Create outgoing clock
    clock_s* clock = clock_new(CLOCK_MODE_OUT);

    // Start clock
    clock_start(clock);

    while (1)
    {
        // Update clock
        clock_tick(clock);
        break;
    }

    // Stop clock
    clock_stop(clock);

    // Destroy clock
    clock_delete(clock);

    return 0;
}
