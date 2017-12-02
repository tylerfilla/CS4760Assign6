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

#include <sys/wait.h>
#include <unistd.h>

#include "clock.h"
#include "config.h"
#include "memmgr.h"

#define DEFAULT_LOG_FILE_PATH "oss.log"

static struct
{
    /** The desired path to the log file. */
    char* log_file_path;

    /** The open log file. */
    FILE* log_file;

    /** The outgoing clock instance. */
    clock_s* clock;

    /** The kernel memory manager. */
    memmgr_s* memmgr;

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
        }
    }

    // Clean up IPC-heavy components
    if (g.clock)
    {
        clock_delete(g.clock);
    }
    if (g.memmgr)
    {
        memmgr_delete(g.memmgr);
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

    // printf(3) is not signal-safe, but POSIX allows write(2)
    // Using sizeof on a char array initialized via string literal includes the null terminator
    // Printing the null terminator is undesired, so subtract 1 from the size
    // This does not go through the logger, since SysV semaphores are not POSIX signal-safe
    char death_notice_msg[] = "oss: received a child process death notice\n";
    write(STDOUT_FILENO, death_notice_msg, sizeof(death_notice_msg) - 1);

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
    pid_t child_pid = fork();
    if (child_pid == 0)
    {
        // Fork succeeded, now in child

        // Redirect STDOUT to log file
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
        fprintf(stderr, "global.log_file_path not allocated\n");
        return 1;
    }

    // Open log file for appending
    g.log_file = fopen(g.log_file_path, "w");
    if (!g.log_file)
    {
        perror("unable to open log file, so logging will not occur");
    }

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

    // Create kernel memory manager
    g.memmgr = memmgr_new(MEMMGR_MODE_KERNEL, g.clock);

    fprintf(stderr, "press ^C to stop the simulation\n");

    // Redirect STDOUT to log file
    dup2(fileno(g.log_file), STDOUT_FILENO);

    unsigned long next_spawn_time = 0;

    while (1)
    {
        //
        // Simulate Clock
        //

        // Block SIGCHLD to prevent lock interference
        sigprocmask(SIG_BLOCK, &sigset_sigchld, NULL);

        if (clock_lock(g.clock))
            return 1;

        // Advance the clock by 0 to 250 milliseconds
        clock_advance(g.clock, 0, rand() % 250000000u);

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(g.clock);
        unsigned int now_seconds = clock_get_seconds(g.clock);
        unsigned long now_time = clock_get_time(g.clock);

        if (clock_unlock(g.clock))
            return 1;

        // Unblock SIGCHLD
        sigprocmask(SIG_UNBLOCK, &sigset_sigchld, NULL);

        //
        // Simulate OS Duties
        //

        // Report child process deaths
        // In practice, everything is so fast that this reports all deaths
        if (g.last_child_proc_dead)
        {
            printf("oss: user process %d has died\n", g.last_child_proc_dead);
            g.last_child_proc_dead = 0;
        }

        // If we should try to spawn a child process
        // We do so on first iteration or on subsequent iterations after small delays
        if (now_time >= next_spawn_time)
        {
            // If there is room for another process
            if (g.num_child_procs < MAX_USER_PROCS)
            {
                if (g.interrupted)
                    return 0;

                // Launch a child process
                pid_t child = launch_child();

                printf("oss: spawned a new user process %d (%us %uns)\n", child, now_seconds, now_nanos);
                printf("oss: there are now %d processes in the system\n", g.num_child_procs);
            }

            // Schedule next spawn time
            // Select a random time between now and 500 milliseconds from now
            next_spawn_time = now_time + (rand() % 500) * 1000000ul;
        }

        // Block SIGCHLD to prevent lock interference
        sigprocmask(SIG_BLOCK, &sigset_sigchld, NULL);

        if (memmgr_lock(g.memmgr))
            return 1;

        // Update the state of the memory manager
        // This may internally involve locking/unlocking the clock semaphore, so leave SIGCHLD blocked
        if (memmgr_update(g.memmgr))
            return 1;

        if (memmgr_unlock(g.memmgr))
            return 1;

        // Unblock SIGCHLD
        sigprocmask(SIG_UNBLOCK, &sigset_sigchld, NULL);

        // Break loop on interrupt
        if (g.interrupted)
            break;

        // Add some real time to the simulation
        usleep(100000);
    }

    return 0;
}
