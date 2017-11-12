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

    while (1)
    {
        // Break loop on interrupt
        if (g.interrupted)
        {
            printf("user proc %d: interrupted\n", getpid());
            fflush(stdout);
            break;
        }
    }
}
