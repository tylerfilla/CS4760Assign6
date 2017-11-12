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
        perror("cannot handle SIGINT: sigaction(2) failed");
        return 1;
    }

    // Create and start incoming (read-only) clock
    g.clock = clock_new(CLOCK_MODE_IN);

    // Create client resource manager instance
    // This connects to the existing server resource manager
    g.resmgr = resmgr_new(RESMGR_SIDE_CLIENT);

    int terminate = 0;
    int suppress = 0;

    // This loop represents part of the operating system control
    while (1)
    {
        /*
        // Lock the scheduler
        if (scheduler_lock(g.resmgr))
            return 1;

        // If this SUP is dispatched
        // It is at this point that the SUP is considered to be running
        if (scheduler_get_dispatch_proc(g.resmgr) == getpid())
        {
            // Get assigned time quantum
            unsigned int quantum = scheduler_get_dispatch_quantum(g.resmgr);

            // Unlock the scheduler
            if (scheduler_unlock(g.resmgr))
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
            fflush(stdout);

            //
            // Event Simulation
            //
            // Randomly choose one of the following:
            //  a. Terminate immediately
            //  b. Terminate after time quantum
            //  c. Block on a simulated I/O event
            //  d. Run for some time and yield
            //

            // Time spent waiting on an event
            unsigned long evt_duration_time = 0;

            switch (rand() % 4)
            {
            case 0:
                // Terminate immediately
                printf("user proc %d: rolled a 0: terminating immediately\n", getpid());
                fflush(stdout);
                terminate = 1;
                break;
            case 1:
                // Terminate after time quantum
                printf("user proc %d: rolled a 1: terminating after full time quantum\n", getpid());
                fflush(stdout);
                while (1)
                {
                    // Lock the clock
                    if (clock_lock(g.clock))
                        return 1;

                    // Get latest time from clock
                    unsigned int now_nanos = clock_get_nanos(g.clock);
                    unsigned int now_seconds = clock_get_seconds(g.clock);

                    // Unlock the clock
                    if (clock_unlock(g.clock))
                        return 1;

                    // Absolute latest time
                    unsigned long now_time = (unsigned long) now_nanos + (unsigned long) now_seconds * 1000000000l;

                    // Break once quantum is used
                    if (now_time - start_time >= quantum)
                        break;

                    usleep(100);
                }
                terminate = 1;
                break;
            case 2:
                // Block on a simulated I/O event
                printf("user proc %d: rolled a 2: waiting on an event\n", getpid());
                fflush(stdout);
                {
                    // Randomize duration of "event"
                    unsigned int evt_duration_nanos = (unsigned int) (rand() % 1000);
                    unsigned int evt_duration_seconds = (unsigned int) (rand() % 5);

                    evt_duration_time = (unsigned long) evt_duration_nanos
                            + (unsigned long) evt_duration_seconds * 1000000000l;

                    // Compute end of event
                    unsigned int evt_done_nanos = start_nanos + evt_duration_nanos;
                    unsigned int evt_done_seconds = start_seconds + evt_duration_seconds;

                    // Absolute event end time
                    unsigned long evt_done_time = (unsigned long) evt_done_nanos
                            + (unsigned long) evt_done_seconds * 1000000000l;

                    printf("user proc %d: info: event will complete after %ldns\n", getpid(), evt_duration_time);
                    fflush(stdout);

                    // Put SUP into WAIT
                    if (scheduler_lock(g.resmgr))
                        return 1;
                    if (scheduler_wait(g.resmgr))
                        return 1;
                    if (scheduler_unlock(g.resmgr))
                        return 1;

                    //
                    // This SUP is now considered to be in the WAIT state. It may continue to run as a real process on
                    // the system, but the corresponding SUP will not account for any time taken. This case only sets a
                    // time in the future to resume and physically waits for it, which shouldn't be accounted for by
                    // definition in the assignment. Another SUP should be scheduled at this time.
                    //

                    while (1)
                    {
                        // Lock the clock
                        if (clock_lock(g.clock))
                            return 1;

                        // Get latest time from clock
                        unsigned int now_nanos = clock_get_nanos(g.clock);
                        unsigned int now_seconds = clock_get_seconds(g.clock);

                        // Unlock the clock
                        if (clock_unlock(g.clock))
                            return 1;

                        // Absolute latest time
                        unsigned long now_time = (unsigned long) now_nanos + (unsigned long) now_seconds * 1000000000l;

                        // Break once event "occurs"
                        if (now_time >= evt_done_time)
                            break;

                        usleep(100);
                    }

                    printf("user proc %d: info: event has occurred\n", getpid());
                    fflush(stdout);
                    suppress = 1;
                }
                break;
            case 3:
                // Run for some time (a percent of the quantum) and yield
                printf("user proc %d: rolled a 3: running for some percentage of quantum\n", getpid());
                fflush(stdout);
                {
                    unsigned int quantum_fraction = quantum / (1 + rand() % 99);

                    printf("user proc %d: info: will get preempted after %dns\n", getpid(), quantum_fraction);
                    fflush(stdout);

                    while (1)
                    {
                        // Lock the clock
                        if (clock_lock(g.clock))
                            return 1;

                        // Get latest time from clock
                        unsigned int now_nanos = clock_get_nanos(g.clock);
                        unsigned int now_seconds = clock_get_seconds(g.clock);

                        // Unlock the clock
                        if (clock_unlock(g.clock))
                            return 1;

                        // Absolute latest time
                        unsigned long now_time = (unsigned long) now_nanos + (unsigned long) now_seconds * 1000000000l;

                        // Break once quantum is used
                        if (now_time - start_time >= quantum_fraction)
                            break;

                        usleep(100);
                    }
                }
                break;
            default:
                break;
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
            unsigned long cpu_time = stop_time - start_time - evt_duration_time;

            // Lock the scheduler
            if (scheduler_lock(g.resmgr))
                return 1;

            if (!suppress)
            {
                printf("user proc %d: yield (sim time %ds, %dns)\n", getpid(), stop_seconds, stop_nanos);
                fflush(stdout);
            }

            // Yield control back to the system after next unlock
            // Timing details are provided to the scheduler so that it can reevaluate process priority
            // In a perfect world, this wouldn't be controllable by the child
            // This is always done, regardless of the child's fate
            if (scheduler_yield(g.resmgr, stop_nanos, stop_seconds, cpu_time))
                return 1;

            if (!suppress)
            {
                printf("user proc %d: summary: used %ldns cpu time (subject to clock granularity)\n", getpid(),
                        cpu_time);
                fflush(stdout);
            }
        }

        // Unlock the scheduler
        if (scheduler_unlock(g.resmgr))
            return 1;

        if (terminate)
            return 0;

        */

        if (g.interrupted)
        {
            printf("user proc %d: interrupted\n", getpid());
            fflush(stdout);
            return 2;
        }
    }
}
