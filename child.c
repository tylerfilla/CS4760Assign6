/*
 * Tyler Filla
 * CS 4760
 * Assignment 4
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>

#include "clock.h"
#include "scheduler.h"

static struct
{
    /** The incoming clock instance. */
    clock_s* clock;

    /** The slave scheduler instance. */
    scheduler_s* scheduler;
} global;

static void handle_exit()
{
    clock_delete(global.clock);
    scheduler_delete(global.scheduler);
}

int main(int argc, char* argv[])
{
    atexit(&handle_exit);
    srand((unsigned int) time(NULL));

    // Create and start incoming (read-only) clock
    global.clock = clock_new(CLOCK_MODE_IN);

    // Create slave scheduler
    // This connects to the existing master scheduler
    global.scheduler = scheduler_new(SCHEDULER_SIDE_SLAVE);

    while (1)
    {
        // Lock the scheduler
        if (scheduler_lock(global.scheduler))
            return 1;

        if (scheduler_s_get_dispatch_proc(global.scheduler) == getpid())
        {
            // Determine if the process should run to completion
            int completion = rand() % 2; // NOLINT

            if (completion)
            {
            }
        }

        // Unlock the scheduler
        if (scheduler_unlock(global.scheduler))
            return 1;

        usleep(1);
    }

    return 0;
}

/*
        // Lock the simulated clock
        if (clock_lock(global.clock))
            break;

        // Get current simulated time
        int now_nanos = clock_get_nanos(global.clock);
        int now_seconds = clock_get_seconds(global.clock);

        // Unlock the simulated clock
        if (clock_unlock(global.clock))
            break;

        // If delay has elapsed
        if (seconds_now > seconds_target || (seconds_now == seconds_target && nanos_now > nanos_target))
        {
            // Lock the scheduler
            if (scheduler_lock(global.scheduler))
                break;

            // Redirected to log file
            printf("child %d entered critical section at real time %ld (%ds %dns sim time)\n", getpid(), time(NULL),
                    seconds_now, nanos_now);

            // If there is no message waiting, we send our termination notice
            if (!scheduler_test(global.scheduler))
            {
                // Send current time
                scheduler_msg_s msg = { seconds_now, nanos_now };
                scheduler_offer(global.scheduler, msg);

                // Redirected to log file
                printf("child %d leaving critical section at real time %ld (%ds %dns sim time)\n", getpid(), time(NULL),
                        seconds_now, nanos_now);

                fflush(stdout);

                // Unlock the scheduler
                if (scheduler_unlock(global.scheduler))
                    break;

                // Exit normally
                exit(0);
            }

            fflush(stdout);

            // Redirected to log file
            printf("child %d leaving critical section at real time %ld (%ds %dns sim time)\n", getpid(), time(NULL),
                    seconds_now, nanos_now);

            // Unlock the scheduler
            if (scheduler_unlock(global.scheduler))
                break;
        }
 */
