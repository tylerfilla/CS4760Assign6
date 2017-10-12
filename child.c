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
#include "messenger.h"

static struct
{
    /** The incoming clock instance. */
    clock_s* clock;

    /** The slave messenger instance. */
    messenger_s* messenger;
} global;

static void handle_exit()
{
    clock_delete(global.clock);
    messenger_delete(global.messenger);
}

int main(int argc, char* argv[])
{
    atexit(&handle_exit);
    srand((unsigned int) time(NULL));

    // Create and start incoming (read-only) clock
    global.clock = clock_new(CLOCK_MODE_IN);

    // Create slave messenger
    global.messenger = messenger_new(MESSENGER_SIDE_SLAVE);

    // Get the starting simulated time
    clock_lock(global.clock);
    int nanos_start = clock_get_nanos(global.clock);
    int seconds_start = clock_get_seconds(global.clock);
    clock_unlock(global.clock);

    // Schedule a simulated time in the future until which to wait
    int nanos_target = nanos_start + (1 + rand() % 1000000); // NOLINT
    int seconds_target = seconds_start + (nanos_target / 1000000000);
    nanos_target %= 1000000000;

    while (1)
    {
        // Lock the simulated clock
        if (clock_lock(global.clock))
            break;

        // Get current simulated time
        int nanos_now = clock_get_nanos(global.clock);
        int seconds_now = clock_get_seconds(global.clock);

        // Unlock the simulated clock
        if (clock_unlock(global.clock))
            break;

        // If delay has elapsed
        if (seconds_now > seconds_target || (seconds_now == seconds_target && nanos_now > nanos_target))
        {
            // Lock the messenger
            if (messenger_lock(global.messenger))
                break;

            // Redirected to log file
            printf("child %d entered critical section at real time %ld (%ds %dns sim time)\n", getpid(), time(NULL),
                    seconds_now, nanos_now);

            // If there is no message waiting, we send our termination notice
            if (!messenger_test(global.messenger))
            {
                // Send current time
                messenger_msg_s msg = { seconds_now, nanos_now };
                messenger_offer(global.messenger, msg);

                // Redirected to log file
                printf("child %d leaving critical section at real time %ld (%ds %dns sim time)\n", getpid(), time(NULL),
                        seconds_now, nanos_now);

                // Unlock the messenger
                if (messenger_unlock(global.messenger))
                    break;

                // Exit normally
                exit(0);
            }

            // Unlock the messenger
            if (messenger_unlock(global.messenger))
                break;

            // Redirected to log file
            printf("child %d leaving critical section at real time %ld (%ds %dns sim time)\n", getpid(), time(NULL),
                    seconds_now, nanos_now);
        }

        usleep(1);
    }

    return 0;
}
