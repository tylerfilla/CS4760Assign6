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
#define MAX_PROCESSES 18

static struct
{
    /** The desired path to the log file. */
    char* log_file_path;

    /** Nonzero if in verbose mode, otherwise zero. */
    volatile sig_atomic_t verbose;

    /** The open log file. */
    FILE* log_file;

    /** The outgoing clock instance. */
    clock_s* clock;

    /** The resource manager instance. */
    resmgr_s* resmgr;

    /** The current number of child processes. */
    volatile sig_atomic_t num_child_procs;

    /** The pid of the last dead child process. */
    volatile sig_atomic_t last_child_proc_dead;

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

            // Print resource statistics
            resmgr_dump(g.resmgr);
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

static void handle_sigchld(int sig)
{
    // Decrement number of child processes
    g.num_child_procs--;

    if (g.verbose)
    {
        // printf(3) is not signal-safe
        // POSIX allows the use of write(2), but this is unformatted
        char death_notice_msg[] = "oss: received a child process death notice\n";
        write(STDOUT_FILENO, death_notice_msg, sizeof(death_notice_msg));
    }

    // Get and record the pid
    // Hopefully we can report it in time
    pid_t pid = wait(NULL);
    g.last_child_proc_dead = pid;
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

        // Redirect child stdout to log file
        // This is a hack to allow logging from children without communicating the log fd
        dup2(fileno(g.log_file), STDOUT_FILENO);

        // Swap in the child image
        // Pass along verbosity control argument, if applicable
        if (execv("./child", (char* []) { "./child", g.verbose ? "-v" : NULL, NULL }))
        {
            perror("launch child failed (in child): execv(2) failed");
        }

        _Exit(1);
    }
    else if (child_pid > 0)
    {
        // Increment number of child processes
        g.num_child_procs++;

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
    while ((opt = getopt(argc, argv, "hl:v")) != -1)
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
            g.verbose = 1;
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
    // We will communicate on the terminal using stderr
    dup2(fileno(g.log_file), STDOUT_FILENO);

    // Register handler for SIGCHLD signal (to know when children die)
    struct sigaction sigaction_sigchld = {};
    sigaction_sigchld.sa_handler = &handle_sigchld;
    if (sigaction(SIGCHLD, &sigaction_sigchld, NULL))
    {
        perror("cannot handle SIGCHLD: sigaction(2) failed, this is a fatal error");
        return 2;
    }

    // Signal set for blocking SIGCHLD
    sigset_t sigset_sigchld;
    sigemptyset(&sigset_sigchld);
    sigaddset(&sigset_sigchld, SIGCHLD);

    // Register handler for SIGINT signal (^C at terminal)
    struct sigaction sigaction_sigint = {};
    sigaction_sigint.sa_handler = &handle_sigint;
    if (sigaction(SIGINT, &sigaction_sigint, NULL))
    {
        perror("cannot handle SIGINT: sigaction(2) failed, so manual IPC cleanup possible");
    }

    // Create and start outgoing clock
    g.clock = clock_new(CLOCK_MODE_OUT);

    // Create server-side resource manager instance
    g.resmgr = resmgr_new(RESMGR_SIDE_SERVER);
    g.resmgr->verbose = g.verbose;

    fprintf(stderr, "press ^C to stop the simulation\n");

    unsigned long next_spawn_time = 0;
    unsigned long next_deadlock_detect_time = 0;

    while (1)
    {
        //
        // Simulate Clock
        //

        // Block SIGCHLD
        sigprocmask(SIG_BLOCK, &sigset_sigchld, NULL);

        if (clock_lock(g.clock))
            return 1;

        // Advance the clock by 0 to 250 milliseconds
        clock_advance(g.clock, rand() % 250000000u, 0);

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(g.clock);
        unsigned int now_seconds = clock_get_seconds(g.clock);
        unsigned long now_time = now_seconds * 1000000000ul + now_nanos;

        if (clock_unlock(g.clock))
            return 1;

        // Unblock SIGCHLD
        sigprocmask(SIG_UNBLOCK, &sigset_sigchld, NULL);

        //
        // Simulate OS Duties
        //

        // Block SIGCHLD
        sigprocmask(SIG_BLOCK, &sigset_sigchld, NULL);

        if (resmgr_lock(g.resmgr))
            break;

        // Update the resource manager
        // This must be done frequently to keep everything consistent
        resmgr_update(g.resmgr);

        if (resmgr_unlock(g.resmgr))
            return 1;

        // Unblock SIGCHLD
        sigprocmask(SIG_UNBLOCK, &sigset_sigchld, NULL);

        // Report child process deaths
        if (g.last_child_proc_dead)
        {
            if (g.verbose)
            {
                printf("oss: process %d has died\n", g.last_child_proc_dead);
            }

            g.last_child_proc_dead = 0;
        }

        // If we should try to spawn a child process
        // We do so on first iteration or on subsequent iterations spaced out by 1 to 500 milliseconds
        if (now_time >= next_spawn_time)
        {
            // If there is room for another process
            if (g.num_child_procs < MAX_PROCESSES)
            {
                if (g.interrupted)
                    return 0;

                // Launch a child process
                pid_t child = launch_child();

                if (g.verbose)
                {
                    printf("oss: spawned a new process %d (%ds %dns)\n", child, now_seconds, now_nanos);
                    printf("oss: there are now %d processes in the system\n", g.num_child_procs);
                }
            }

            // Schedule next spawn time
            next_spawn_time = now_time + (rand() % 500) * 1000000ul;
        }

        // Run deadlock detection and resolution once per simulated second
        if (now_time >= next_deadlock_detect_time)
        {
            // Block SIGCHLD
            sigprocmask(SIG_BLOCK, &sigset_sigchld, NULL);

            if (resmgr_lock(g.resmgr))
                return 1;

            printf("oss: running deadlock detection and resolution (%ds %dns)\n", now_seconds, now_nanos);

            // Detect and resolve deadlocks
            resmgr_resolve_deadlocks(g.resmgr);

            if (resmgr_unlock(g.resmgr))
                return 1;

            // Unblock SIGCHLD
            sigprocmask(SIG_UNBLOCK, &sigset_sigchld, NULL);

            // Schedule next deadlock detection time
            next_deadlock_detect_time = now_time + 1000000000u;
        }

        // Break loop on interrupt
        if (g.interrupted)
            break;

        usleep(100000);
    }

    return 0;
}
