/*
 * Tyler Filla
 * CS 4760
 * Assignment 4
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/wait.h>
#include <unistd.h>

#include "clock.h"
#include "scheduler.h"

#define DEFAULT_LOG_FILE_PATH "oss.log"

// TODO: Current task: handling SIGCHLD and calling complete_death

static struct
{
    /** The desired path to the log file. */
    char* log_file_path;

    /** The open log file. */
    FILE* log_file;

    /** The outgoing clock instance. */
    clock_s* clock;

    /** The master scheduler instance. */
    scheduler_s* scheduler;

    /** The number of spawned child processes. */
    volatile sig_atomic_t num_children;

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

    // Close log file
    if (g.log_file)
    {
        fclose(g.log_file);
    }
    if (g.log_file_path)
    {
        free(g.log_file_path);
    }
}

static void handle_sigchld(int sig)
{
    g.num_children--;

    // Obtain the zombie's pid
    pid_t pid = wait(NULL);

    printf("got death notice for %d\n", pid); // FIXME: Not safe
}

static void handle_sigint(int sig)
{
    // Set interrupted flag
    g.interrupted = 1;
}

static pid_t launch_child()
{
    int child_pid = fork();
    if (child_pid == 0)
    {
        // Fork succeeded, now in child

        // Redirect child stderr and stdout to log file
        // This is a hack to allow logging from children without communicating the log fd
        //dup2(fileno(g.log_file), STDERR_FILENO);
        //dup2(fileno(g.log_file), STDOUT_FILENO);
        // TODO: Uncomment the above lines

        // Swap in the child image
        if (execv("./child", (char* []) { "./child", NULL }))
        {
            perror("launch child failed (in child): execv(2) failed");
        }

        _Exit(1);
    }
    else if (child_pid > 0)
    {
        // Fork succeeded, now in parent
        g.num_children++;
        return child_pid;
    }
    else
    {
        // Fork failed, still in parent
        perror("launch child failed (in parent): fork(2) failed");
        return -1;
    }
}

static void print_help(FILE* dest, const char* executable_name)
{
    fprintf(dest, "Usage: %s [option...]\n\n", executable_name);
    fprintf(dest, "Supported options:\n");
    fprintf(dest, "    -h          Display this information\n");
    fprintf(dest, "    -l <file>   Log events to <file> (default oss.log)\n");
}

static void print_usage(FILE* dest, const char* executable_name)
{
    fprintf(dest, "Usage: %s [option..]\n", executable_name);
    fprintf(dest, "Try `%s -h' for more information.\n", executable_name);
}

int main(int argc, char* argv[])
{
    atexit(&handle_exit);
    srand((unsigned int) time(NULL));

    g.log_file_path = strdup(DEFAULT_LOG_FILE_PATH);

    // Handle command-line options
    int opt;
    while ((opt = getopt(argc, argv, "hl:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            print_help(stdout, argv[0]);
            return 0;
        case 'l':
            free(g.log_file_path);
            g.log_file_path = strdup(optarg);
            if (!g.log_file_path)
            {
                perror("global.log_file_path not allocated: strdup(3) failed");
                return 1;
            }
            break;
        default:
            fprintf(stderr, "invalid option: -%c\n", opt);
            print_usage(stderr, argv[0]);
            return 1;
        }
    }

    if (!g.log_file_path)
    {
        printf("global.log_file_path not allocated\n");
        return 1;
    }

    // Open log file for appending
    g.log_file = fopen(g.log_file_path, "w");
    if (!g.log_file)
    {
        perror("unable to open log file, so logging will not occur");
    }

    // Register handler for SIGCHLD signal
    struct sigaction sigaction_sigchld = {};
    sigaction_sigchld.sa_handler = &handle_sigchld;
    if (sigaction(SIGCHLD, &sigaction_sigchld, NULL))
    {
        perror("cannot handle SIGCHLD: sigaction(2) failed");
        return 1;
    }

    // Register handler for SIGINT signal (^C at terminal)
    struct sigaction sigaction_sigint = {};
    sigaction_sigint.sa_handler = &handle_sigint;
    if (sigaction(SIGINT, &sigaction_sigint, NULL))
    {
        perror("cannot handle SIGINT: sigaction(2) failed");
        return 1;
    }

    // Create and start outgoing clock
    g.clock = clock_new(CLOCK_MODE_OUT);

    // Create master scheduler
    g.scheduler = scheduler_new(SCHEDULER_SIDE_MASTER);

    while (1)
    {
        // Lock the clock
        if (clock_lock(g.clock))
            return 1;

        // Generate a time between 1 and 1.000001 seconds (1s + [0, 1000ns])
        // This duration of time passage will be simulated
        unsigned int dn = (unsigned int) (rand() % 1000);
        unsigned int ds = 1;

        // Advance the clock
        clock_advance(g.clock, dn, ds);

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(g.clock);
        unsigned int now_seconds = clock_get_seconds(g.clock);

        // Unlock the clock
        if (clock_unlock(g.clock))
            return 1;

        // Lock the scheduler
        if (scheduler_lock(g.scheduler))
            return 1;

        // If resources are available for another process to be spawned
        if (scheduler_available(g.scheduler))
        {
            // Launch a child
            pid_t child_pid = launch_child();

            // If doing so failed
            if (child_pid == -1)
            {
                // Unlock the scheduler and die
                scheduler_unlock(g.scheduler);
                return 1;
            }

            // Complete spawning the child process
            // This allocates the process control block and whatnot
            // This does not dispatch the process, however
            if (scheduler_complete_spawn(g.scheduler, child_pid))
            {
                // Unlock the scheduler and die
                scheduler_unlock(g.scheduler);
                return 1;
            }

            printf("spawned user proc %d\n", child_pid);
        }

        // If a process is not currently scheduled
        if (scheduler_get_dispatch_proc(g.scheduler) == -1)
        {
            // Select and schedule (dispatch) the next SUP
            pid_t pid = scheduler_select_and_schedule(g.scheduler);

            printf("dispatched user proc %d\n", pid);
            printf("user proc %d: state is now RUN\n", pid);
        }

        fflush(stdout);

        // Unlock the scheduler
        if (scheduler_unlock(g.scheduler))
            return 1;

        if (g.interrupted)
        {
            printf("execution interrupted\n");
            break;
        }

        sleep(1); // TODO: Does this limit rate enough to provide approximately 1 spawn on average?
        // This also lets the simulated clock somewhat sync up to real-ish time for a bit
    }

    // Wait for remaining children to die
    while (g.num_children > 0)
    {
        killpg(getpgrp(), SIGINT);
        usleep(1000);
    }
}
