/*
 * Tyler Filla
 * CS 4760
 * Assignment 5
 */

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sys/wait.h>
#include <unistd.h>

#include "clock.h"
#include "resmgr.h"

#define DEFAULT_LOG_FILE_PATH "oss.log"

static struct
{
    /** The desired path to the log file. */
    char* log_file_path;

    /** Nonzero to indicate verbose mode, otherwise zero. */
    int verbose;

    /** The open log file. */
    FILE* log_file;

    /** The outgoing clock instance. */
    clock_s* clock;

    /** The resource manager instance. */
    resmgr_s* resmgr;

    /** Nonzero once SIGINT received. */
    volatile sig_atomic_t interrupted;
} g;

static void handle_exit()
{
    if (g.clock)
    {
        // Get stop time
        unsigned int stop_nanos = 0;
        unsigned int stop_seconds = 0;
        if (clock_lock(g.clock) == 0)
        {
            // Get stop time
            stop_nanos = clock_get_nanos(g.clock);
            stop_seconds = clock_get_seconds(g.clock);

            // Unlock the clock
            clock_unlock(g.clock);
        }

        if (g.interrupted)
        {
            fprintf(stderr, "\n--- interrupted; dumping information about last run ---\n");
            fprintf(stderr, "log file: %s\n", g.log_file_path);
            fprintf(stderr, "time now: %ds, %dns\n", stop_seconds, stop_nanos);
        }
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
        dup2(fileno(g.log_file), STDERR_FILENO);
        dup2(fileno(g.log_file), STDOUT_FILENO);

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
    fprintf(dest, "    -v          Verbose mode\n");
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
    while ((opt = getopt(argc, argv, "hlv:")) != -1)
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
        case 'v':
            break;
        default:
            fprintf(stderr, "invalid option: -%c\n", opt);
            print_usage(stderr, argv[0]);
            return 1;
        }
    }

    if (!g.log_file_path)
    {
        fprintf(stderr, "global.log_file_path not allocated\n");
        return 1;
    }

    // Open log file for appending
    g.log_file = fopen(g.log_file_path, "w");
    if (!g.log_file)
    {
        perror("unable to open log file, so logging will not occur");
    }

    // Redirect stdout to the log file
    dup2(fileno(g.log_file), STDOUT_FILENO);

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

    // Create server-side resource manager instance
    g.resmgr = resmgr_new(RESMGR_SIDE_SERVER);

    fprintf(stderr, "press ^C at a terminal or send SIGINT to stop the simulation\n");

    // Scheduled time to spawn another SUP
    unsigned int next_proc_nanos = 0;
    unsigned int next_proc_seconds = 0;

    while (1)
    {
        // Lock the clock
        if (clock_lock(g.clock))
            return 1;

        // Generate a time between 1 and 500 milliseconds
        // This duration of time passage will be simulated
        unsigned int dn = (unsigned int) (rand() % 500000000); // NOLINT

        // Advance the clock
        clock_advance(g.clock, dn, 0);

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(g.clock);
        unsigned int now_seconds = clock_get_seconds(g.clock);

        // Unlock the clock
        if (clock_unlock(g.clock))
            return 1;

        /*

        // Lock the scheduler
        if (resmgr_lock(g.resmgr))
            return 1;

        // If it is time to try to create a new process
        if (now_nanos >= next_proc_nanos && now_seconds >= next_proc_seconds)
        {
            // If resources are available for another process to be spawned
            if (scheduler_available(g.resmgr))
            {
                // Lock the clock
                if (clock_lock(g.clock))
                    return 1;

                // Get latest time from clock
                unsigned int spawn_nanos = clock_get_nanos(g.clock);
                unsigned int spawn_seconds = clock_get_seconds(g.clock);

                // Unlock the clock
                if (clock_unlock(g.clock))
                    return 1;

                // Launch a child
                pid_t child_pid = launch_child();

                // If doing so failed
                if (child_pid == -1)
                {
                    // Unlock the scheduler and die
                    scheduler_unlock(g.resmgr);
                    return 1;
                }

                // Complete spawning the child process
                // This allocates the process control block and whatnot
                // This does not dispatch the process, however
                if (scheduler_complete_spawn(g.resmgr, child_pid, spawn_nanos, spawn_seconds))
                {
                    // Unlock the scheduler and die
                    scheduler_unlock(g.resmgr);
                    return 1;
                }

                fprintf(g.log_file, "spawned user proc %d (sim time %ds, %dns)\n", child_pid, spawn_seconds,
                        spawn_nanos);
                fflush(g.log_file);
            }

            // Schedule next process spawn
            int next_proc_delay = rand() % 3;
            next_proc_nanos = now_nanos;
            next_proc_seconds = now_seconds + next_proc_delay;
        }

        // Handle dead SUPs
        if (g.dead_proc)
        {
            pid_t pid = g.dead_proc;
            g.dead_proc = 0;

            // Lock the clock
            if (clock_lock(g.clock))
                return 1;

            // Get latest time from clock
            unsigned int death_nanos = clock_get_nanos(g.clock);
            unsigned int death_seconds = clock_get_seconds(g.clock);

            // Unlock the clock
            if (clock_unlock(g.clock))
                return 1;

            // Need to handle death
            scheduler_complete_death(g.resmgr, pid, death_nanos, death_seconds);

            fprintf(g.log_file, "user proc %d: dead\n", pid);
            fflush(g.log_file);
        }

        // If a process is not currently scheduled
        if (scheduler_get_dispatch_proc(g.resmgr) == -1)
        {
            // Select and schedule (dispatch) the next SUP
            pid_t pid = scheduler_select_and_schedule(g.resmgr, now_nanos, now_seconds);

            // If a process couldn't be scheduled
            // TODO: This is part of CPU idle time
            if (pid == -1)
            {
                // Unlock the scheduler
                if (scheduler_unlock(g.resmgr))
                    return 1;

                // Try again later
                sleep(1);
                continue;
            }

            fprintf(g.log_file, "dispatched user proc %d (sim time %ds, %dns)\n", pid, now_seconds, now_nanos);
            fflush(g.log_file);
        }

        // Unlock the scheduler
        if (scheduler_unlock(g.resmgr))
            return 1;

        */

        // Break loop on interrupt
        if (g.interrupted)
            break;

        usleep(100000);
    }

    return 0;
}
