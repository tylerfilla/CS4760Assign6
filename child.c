/*
 * Tyler Filla
 * CS 4760
 * Assignment 4
 */

#include <signal.h>
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

    /** Nonzero once SIGINT received. */
    volatile sig_atomic_t interrupted;
} g;

static void handle_exit()
{
    // Clean up IPC-heavy components
    if (g.clock)
    {
        clock_delete(g.clock);
    }
    if (g.scheduler)
    {
        scheduler_delete(g.scheduler);
    }
}

static void handle_sigint(int sig)
{
    // Set interrupted flag
    g.interrupted = 1;
}

int main(int argc, char* argv[])
{
    atexit(&handle_exit);
    srand((unsigned int) time(NULL));

    // Handle SIGINT
    struct sigaction sigaction_sigint = {};
    sigaction_sigint.sa_handler = &handle_sigint;
    if (sigaction(SIGINT, &sigaction_sigint, NULL))
    {
        perror("cannot handle SIGINT: sigaction(2) failed");
        return 1;
    }

    // Create and start incoming (read-only) clock
    g.clock = clock_new(CLOCK_MODE_IN);

    // Create slave scheduler
    // This connects to the existing master scheduler
    g.scheduler = scheduler_new(SCHEDULER_SIDE_SLAVE);

    int terminate = 0;

    // This loop represents part of the operating system control
    while (1)
    {
        // Lock the scheduler
        if (scheduler_lock(g.scheduler))
            return 1;

        // If this SUP is dispatched
        // It is at this point that the SUP is considered to be running
        if (scheduler_get_dispatch_proc(g.scheduler) == getpid())
        {
            // Unlock the scheduler
            if (scheduler_unlock(g.scheduler))
                return 1;

            //
            // Beginning Time
            //

            // Lock the clock
            if (clock_lock(g.clock))
                return 1;

            // Get latest time from clock
            unsigned int start_nanos = clock_get_nanos(g.clock);
            unsigned int start_seconds = clock_get_seconds(g.clock);

            // Unlock the clock
            if (clock_unlock(g.clock))
                return 1;

            // Absolute simulated beginning time
            unsigned long start_time = (unsigned long) start_nanos + (unsigned long) start_seconds * 1000000000l;

            printf("user proc %d: resume (sim time %ds, %dns)\n", getpid(), start_seconds, start_nanos);

            //
            // Event Simulation
            //
            // Randomly choose one of the following:
            //  a. Terminate immediately
            //  b. Terminate after time quantum
            //  c. Block on a simulated I/O event
            //  d. Run for some time and yield
            //

            // TODO: Don't forget to keep track of times to calculate statistics and move priorities around

            switch (rand() % 4)
            {
            case 0:
                // Terminate immediately
                printf("user proc %d: rolled a 0: terminating immediately\n", getpid());
                terminate = 1;
                break;
            case 1:
                // Terminate after time quantum
                printf("user proc %d: rolled a 1: terminating after time quantum\n", getpid());
                printf("user proc %d: 1 NOT YET IMPLEMENTED! Yield and retry...\n", getpid());
                break;
            case 2:
                // Block on a simulated I/O event
                printf("user proc %d: rolled a 2: waiting on an event\n", getpid());
                printf("user proc %d: 2 NOT YET IMPLEMENTED! Yield and retry...\n", getpid());
                break;
            case 3:
                // Run for some time and yield
                printf("user proc %d: rolled a 3: running for some time interval\n", getpid());
                printf("user proc %d: 3 NOT YET IMPLEMENTED! Yield and retry...\n", getpid());
                break;
            default:
                break;
            }

            // FIXME
            if (!terminate)
            {
                sleep(1);
            }

            //
            // Ending Time
            //

            // Lock the clock
            if (clock_lock(g.clock))
                return 1;

            // Get latest time from clock
            unsigned int stop_nanos = clock_get_nanos(g.clock);
            unsigned int stop_seconds = clock_get_seconds(g.clock);

            // Unlock the clock
            if (clock_unlock(g.clock))
                return 1;

            // Absolute simulated ending time
            unsigned long stop_time = (unsigned long) stop_nanos + (unsigned long) stop_seconds * 1000000000l;

            // CPU burst length
            unsigned long cpu_time = stop_time - start_time;

            // Lock the scheduler
            if (scheduler_lock(g.scheduler))
                return 1;

            printf("user proc %d: yield (sim time %ds, %dns)\n", getpid(), stop_seconds, stop_nanos);

            // Yield control back to the system after next unlock
            // Timing details are provided to the scheduler so that it can reevaluate process priority
            // In a perfect world, this wouldn't be controllable by the child
            if (scheduler_yield(g.scheduler, stop_nanos, stop_seconds, cpu_time))
                return 1;

            printf("user proc %d: summary: used %ldns cpu time\n", getpid(), cpu_time);

            if (!terminate)
            {
                printf("user proc %d: state is now READY\n", getpid());
            }
        }

        fflush(stdout);

        // Unlock the scheduler
        if (scheduler_unlock(g.scheduler))
            return 1;

        if (terminate)
            return 0;

        if (g.interrupted)
        {
            printf("user proc %d: interrupted\n", getpid());
            return 2;
        }
    }
}
