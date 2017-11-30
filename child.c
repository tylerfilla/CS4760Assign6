/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <unistd.h>

#include "clock.h"
#include "memmgr.h"

static struct
{
    /** The incoming clock instance. */
    clock_s* clock;

    /** The memory manager user agent. */
    memmgr_s* memmgr;

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
    if (g.memmgr)
    {
        memmgr_delete(g.memmgr);
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
    srand((unsigned int) time(NULL) ^ getpid());

    // Handle SIGINT
    struct sigaction sigaction_sigint = {};
    sigaction_sigint.sa_handler = &handle_sigint;
    if (sigaction(SIGINT, &sigaction_sigint, NULL))
    {
        perror("cannot handle SIGINT: sigaction(2) failed, so manual IPC cleanup possible");
    }

    // Create and start incoming (read-only) clock
    g.clock = clock_new(CLOCK_MODE_IN);

    // Create memory manager user agent
    g.memmgr = memmgr_new(MEMMGR_MODE_UA);

    while (1)
    {
        if (clock_lock(g.clock))
            return 1;

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(g.clock);
        unsigned int now_seconds = clock_get_seconds(g.clock);
        unsigned long now_time = clock_get_time(g.clock);

        if (clock_unlock(g.clock))
            return 1;

        // TODO: Do client stuff somehow

        // Break loop on interrupt
        if (g.interrupted)
        {
            fprintf(stderr, "%d: interrupted\n", getpid());
            break;
        }
    }
}
