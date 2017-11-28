/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include "clock.h"
#include "resmgr.h"

#define PARAM_B 3000

static struct
{
    /** Nonzero if in verbose mode, otherwise zero. */
    int verbose;

    /** The incoming clock instance. */
    clock_s* clock;

    /** The client resource manager instance. */
    resmgr_s* resmgr;

    /** The resources acquired. Used for random releasing. */
    int acquired_resources[NUM_RESOURCE_CLASSES];

    /** The number of resources acquired. The length of the above list. */
    int num_acquired_resources;

    /** The resource on which the process is waiting, else -1. Used for simulated sleeping. */
    int resource_waiting;

    /** The number of instances of the resource being waited on to expect before the request is fulfilled. */
    int resource_waiting_count;

    /** Nonzero once SIGINT received. */
    volatile sig_atomic_t interrupted;
} g;

static void handle_exit()
{
    // If some allocated resources remain
    if (g.num_acquired_resources > 0)
    {
        if (resmgr_lock(g.resmgr))
            return;

        // Release all acquired resources
        for (int ri = 0; ri < g.num_acquired_resources; ++ri)
        {
            int res = g.acquired_resources[ri];

            if (g.verbose)
            {
                printf("%d: exiting: releasing resource %d\n", getpid(), res);
            }

            resmgr_release(g.resmgr, res);
        }

        if (resmgr_unlock(g.resmgr))
            return;
    }

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
    srand((unsigned int) time(NULL) ^ getpid());

    // Handle rudimentary command-line arguments
    for (int argi = 0; argi < argc; ++argi)
    {
        char* arg = argv[argi];

        // Verbosity control argument
        if (strcmp(arg, "-v") == 0)
        {
            g.verbose = 1;
        }
    }

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
    g.resmgr->verbose = g.verbose;

    unsigned long next_resource_thing_time = 0;
    unsigned long next_death_check_time = 1000000000;

    // Prepare resource state
    g.resource_waiting = -1;

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

        // Work with resources every now and again
        if (now_time >= next_resource_thing_time)
        {
            if (resmgr_lock(g.resmgr))
                return 1;

            // If no resources are acquired, take one
            // Otherwise, take a 50% chance of claiming v. releasing
            if (g.num_acquired_resources == 0 || rand() % 2 == 0)
            {
                // Choose a random resource to acquire
                int res = rand() % NUM_RESOURCE_CLASSES;

                if (g.verbose)
                {
                    printf("%d: requesting resource %d\n", getpid(), res);
                }

                // Count allocations before requesting
                int before_count = resmgr_count(g.resmgr, res);

                // Request the resource
                // We just assume this will result in a wait (waits are resolved later on)
                // For now we just need to unlock the resource manager and regroup
                if (resmgr_request(g.resmgr, res))
                {
                    if (resmgr_unlock(g.resmgr))
                        return 1;

                    return 1;
                }

                if (resmgr_count(g.resmgr, res) > before_count)
                {
                    if (g.verbose)
                    {
                        printf("%d: acquired resource %d\n", getpid(), res);
                    }

                    // Mark resource as acquired
                    g.acquired_resources[res] = 1;
                    g.num_acquired_resources++;
                    g.resource_waiting = -1;
                }
                else
                {
                    if (g.verbose)
                    {
                        printf("%d: resource %d not acquired immediately\n", getpid(), res);
                    }

                    // Mark waiting on resource
                    g.resource_waiting = res;
                    g.resource_waiting_count = before_count + 1;
                }
            }
            else
            {
                // Find a resource to release
                int res = -1;
                for (int ri = 0; ri < NUM_RESOURCE_CLASSES; ++ri)
                {
                    if (g.acquired_resources[ri])
                    {
                        res = ri;
                    }
                }

                // If no resources are found, there is a fatal inconsistency error in this process
                if (res == -1)
                    return 1;

                if (g.verbose)
                {
                    printf("%d: releasing resource %d\n", getpid(), res);
                }

                // Release the resource
                if (resmgr_release(g.resmgr, res))
                {
                    if (resmgr_unlock(g.resmgr))
                        return 1;

                    return 1;
                }

                if (g.verbose)
                {
                    printf("%d: released resource %d\n", getpid(), res);
                }

                // Mark as released
                g.acquired_resources[res] = 0;
                g.num_acquired_resources--;
            }

            if (resmgr_unlock(g.resmgr))
                return 1;

            // If we're now waiting on a resource
            if (g.resource_waiting != -1)
            {
                int res = g.resource_waiting;

                if (g.verbose)
                {
                    printf("%d: waiting for resource %d\n", getpid(), res);
                }

                while (1)
                {
                    if (resmgr_lock(g.resmgr))
                        return 1;

                    // Break if we're finally granted the whole request
                    if (resmgr_count(g.resmgr, res) >= g.resource_waiting_count)
                    {
                        if (resmgr_unlock(g.resmgr))
                            return 1;

                        break;
                    }

                    if (resmgr_unlock(g.resmgr))
                        return 1;

                    usleep(100000);
                }

                if (g.verbose)
                {
                    printf("%d: acquired resource %d\n", getpid(), res);
                }

                // Mark resource as acquired
                g.acquired_resources[res] = 1;
                g.num_acquired_resources++;
                g.resource_waiting = -1;
            }

            // Schedule next resource thing time
            next_resource_thing_time = now_time + (rand() % PARAM_B) * 1000000ul;
        }

        // Periodically, but after the first simulated second, randomize natural death
        if (now_time >= next_death_check_time)
        {
            // Take a 5% chance of dying now
            // FIXME: Was this specified anywhere in the assignment?
            if (rand() % 20 == 0)
            {
                if (g.verbose)
                {
                    printf("%d: dying of natural causes\n", getpid());
                }
                return 0;
            }

            // Schedule next death check time
            next_death_check_time = now_time + (rand() % 250) * 1000000ul;
        }

        // Break loop on interrupt
        if (g.interrupted)
        {
            fprintf(stderr, "%d: interrupted\n", getpid());
            break;
        }
    }
}
