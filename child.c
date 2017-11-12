/*
 * Tyler Filla
 * CS 4760
 * Assignment 5
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>

#include "clock.h"
#include "resmgr.h"

static struct
{
    /** The incoming clock instance. */
    clock_s* clock;

    /** The client resource manager instance. */
    resmgr_s* resmgr;

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
    if (g.resmgr)
    {
        resmgr_delete(g.resmgr);
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
        perror("cannot handle SIGINT: sigaction(2) failed, so manual IPC cleanup possible");
    }

    // Create and start incoming (read-only) clock
    g.clock = clock_new(CLOCK_MODE_IN);

    // Create client resource manager instance
    // This connects to the existing server resource manager
    g.resmgr = resmgr_new(RESMGR_SIDE_CLIENT);

    unsigned long next_death_check_time = 1000000000u;

    while (1)
    {
        if (clock_lock(g.clock))
            return 1;

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(g.clock);
        unsigned int now_seconds = clock_get_seconds(g.clock);
        unsigned long now_time = now_seconds * 1000000000ul + now_nanos;

        if (clock_unlock(g.clock))
            return 1;

        // Periodically, after the first simulated second, randomize natural death
        if (now_time >= next_death_check_time)
        {
            // Take a 20% chance of dying now
            // FIXME: Was this specified anywhere in the assignment?
            if (rand() % 5 == 0) // NOLINT
            {
                printf("%d: dying of natural causes\n", getpid()); // FIXME: Don't actually say this
                return 0;
            }

            // Schedule next death check time
            next_death_check_time = now_time + (rand() % 250) * 1000000ul; // NOLINT
        }

        // Break loop on interrupt
        if (g.interrupted)
        {
            printf("%d: interrupted\n", getpid());
            fflush(stdout);
            break;
        }
    }
}
