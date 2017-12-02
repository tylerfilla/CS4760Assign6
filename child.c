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
#include "logger.h"
#include "memmgr.h"

static struct
{
    /** The client logger instance. */
    logger_s* logger;

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
    if (g.logger)
    {
        logger_delete(g.logger);
    }
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

    // Create client logger
    g.logger = logger_new(LOGGER_MODE_CLIENT);

    // Create and start incoming (read-only) clock
    g.clock = clock_new(CLOCK_MODE_IN);

    // Create memory manager user agent
    g.memmgr = memmgr_new(MEMMGR_MODE_UA, g.clock);

    while (1)
    {
        //
        // Get Clock Time
        //

        if (clock_lock(g.clock))
            return 1;

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(g.clock);
        unsigned int now_seconds = clock_get_seconds(g.clock);
        unsigned long now_time = clock_get_time(g.clock);

        if (clock_unlock(g.clock))
            return 1;

        //
        // User Process Duties
        //

        if (memmgr_lock(g.memmgr))
            return 1;

        // Choose a random virtual memory address to reference
        ptr_vm_t ptr = rand() % memmgr_get_vm_high_ptr(g.memmgr);

        // Choose a random action to take on the pointer (read or write, 50/50)
        int act = rand() % 2;

        // The result of the memory reference
        int ref_result;

        // Perform the memory reference
    do_mem_ref:
        switch (act)
        {
        case 0:
            logger_log(g.logger, "child %d: reading from virtual memory address %#06lx", getpid(), ptr);
            ref_result = memmgr_read_ptr(g.memmgr, ptr);
            break;
        case 1:
            logger_log(g.logger, "child %d: writing to virtual memory address %#06lx", getpid(), ptr);
            ref_result = memmgr_write_ptr(g.memmgr, ptr);
            break;
        }

        if (memmgr_unlock(g.memmgr))
            return 1;

        // If recovering from a page fault
        // Ideally, this functionality would be part of the OS, but this makes things cleaner in the simulation
        if (ref_result == 2)
        {
            logger_log(g.logger, "child %d: page fault on reference to nonresident VM address %#06lx", getpid(), ptr);
            logger_log(g.logger, "child %d: suspending until memory is resident", getpid());

            // Suspend the process until the address is available
            while (1)
            {
                if (memmgr_lock(g.memmgr))
                    return 1;
                logger_log(g.logger, "child %d: locked in suspension", getpid());

                // If process is no longer waiting for the page
                if (!memmgr_is_waiting(g.memmgr))
                {
                    logger_log(g.logger, "child %d: resuming: address %#06lx is now resident", getpid(), ptr);

                    // Break suspension and retry the memory reference
                    // Leave the memory manager locked
                    goto do_mem_ref;
                }

                if (memmgr_unlock(g.memmgr))
                    return 1;

                // Sleep for real
                usleep(100000);
            }
        }
        else
        {
            logger_log(g.logger, "child %d: read/write successful: virtual memory address was resident", getpid());
        }

        // Break loop on interrupt
        if (g.interrupted)
        {
            fprintf(stderr, "%d: interrupted\n", getpid());
            break;
        }
    }
}
