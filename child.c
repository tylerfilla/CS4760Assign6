/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>

#include "clock.h"

int main(int argc, char* argv[])
{
    srand((unsigned int) time(NULL));

    // Create and start incoming (read-only) clock
    clock_s* clock = clock_new(CLOCK_MODE_IN);

    // Get the current simulated time
    clock_lock(clock);
    int nanos = clock_get_nanos(clock);
    int seconds = clock_get_seconds(clock);
    clock_unlock(clock);

    // Schedule a simulated time in the future to wait until
    int nanos_target = nanos + (rand() % 1000000); // NOLINT
    int seconds_target = seconds + (nanos_target / 1000000000);
    nanos_target %= 1000000000;

    // Perform simulated delay using process sleep
    while (1)
    {
        // Lock the simulated clock
        if (clock_lock(clock))
            break;

        // Get current simulated time
        int nanos_now = clock_get_nanos(clock);
        int seconds_now = clock_get_seconds(clock);

        // Unlock the simulated clock
        if (clock_unlock(clock))
            break;

        // Test if delay has elapsed
        if (seconds_now > seconds_target || (seconds_now == seconds_target && nanos_now > nanos_target))
            break;

        usleep(1);
    }
/*
    // Create slave messenger
    messenger_s* messenger = messenger_new(MESSENGER_SIDE_SLAVE);



    // Clean stuff up
    clock_delete(clock);
    messenger_delete(messenger);
*/
    return 0;
}
