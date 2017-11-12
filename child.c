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

#define PARAM_B 3000

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

    // The resources claimed by this process
    int claimed_resources[NUM_RESOURCE_CLASSES];
    int num_claimed_resources = 0;
    int resource_waiting = -1;

    unsigned long next_resource_thing_time = 0;
    unsigned long next_death_check_time = 1000000000;

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

            // If no resources are claimed, claim one
            // Otherwise, take a 50% chance of claiming v. releasing
            if (num_claimed_resources == 0 || rand() % 2 == 0)
            {
                // Choose a random resource to claim
                int res = rand() % NUM_RESOURCE_CLASSES;

                // Claim the resource
                // We assume this will result in a wait (waits are resolved later on)
                if (resmgr_claim(g.resmgr, res))
                    return 1;

                // Mark waiting on resource
                resource_waiting = res;
            }
            else
            {
                // Find a resource to release
                int res = -1;
                for (int ri = 0; ri < NUM_RESOURCE_CLASSES; ++ri)
                {
                    if (claimed_resources[ri])
                    {
                        res = ri;
                    }
                }

                // If no resources found, there is a fatal consistency error in this process
                if (res == -1)
                    return 1;

                // Release the resource
                if (resmgr_release(g.resmgr, res))
                    return 1;

                // Mark as released
                claimed_resources[res] = 0;
                num_claimed_resources--;
                resource_waiting = 0;
            }

            if (resmgr_unlock(g.resmgr))
                return 1;

            // If we're now waiting on a resource
            if (resource_waiting)
            {
                int res = resource_waiting;

                while (1)
                {
                    if (resmgr_lock(g.resmgr))
                        return 1;

                    // Break if we're finally allocated the resource
                    if (resmgr_has(g.resmgr, res))
                    {
                        if (resmgr_unlock(g.resmgr))
                            return 1;

                        break;
                    }

                    if (resmgr_unlock(g.resmgr))
                        return 1;

                    usleep(100000);
                }

                // Mark resource as claimed
                claimed_resources[res] = 1;
                num_claimed_resources++;
                resource_waiting = 0;
            }

            // Schedule next resource thing time
            next_resource_thing_time = now_time + (rand() % PARAM_B) * 1000000ul;
        }

        // Periodically, but after the first simulated second, randomize natural death
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
            next_death_check_time = now_time + (rand() % 250) * 1000000ul;
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
