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

#include <unistd.h>

#include "clock.h"
#include "scheduler.h"

#define DEFAULT_LOG_FILE_PATH "oss.log"

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
} global;

static void handle_exit()
{
    // Clean up IPC components
    if (global.clock)
    {
        clock_delete(global.clock);
    }
    if (global.scheduler)
    {
        scheduler_delete(global.scheduler);
    }

    // Close log file if it is open
    if (global.log_file)
    {
        fclose(global.log_file);
    }

    // The program is exiting, but this is The Right Way (TM)
    if (global.log_file_path)
    {
        free(global.log_file_path);
    }
}

static void handle_sigint(int sig)
{
    fprintf(stderr, "execution interrupted\n");

    // Exit the application with status 2
    // The exit handler should take over just like normal
    exit(2);
}

static pid_t launch_child()
{
    int child_pid = fork();
    if (child_pid == 0)
    {
        // Fork succeeded, now in child

        // Redirect child stderr and stdout to log file
        // This is a hack to allow logging from children without communicating the log fd
        dup2(fileno(global.log_file), STDERR_FILENO);
        dup2(fileno(global.log_file), STDOUT_FILENO);

        // Swap in the child image
        if (execv("./child", (char* []) { "./child", NULL }))
        {
            perror("launch child failed (in child): execv(2) failed");
            _Exit(1);
        }

        // This should never happen
        _Exit(42);
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
}

static void print_usage(FILE* dest, const char* executable_name)
{
    fprintf(dest, "Usage: %s [option..]\n", executable_name);
    fprintf(dest, "Try `%s -h' for more information.\n", executable_name);
}

int main(int argc, char* argv[])
{
    global.log_file_path = strdup(DEFAULT_LOG_FILE_PATH);

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
            free(global.log_file_path);
            global.log_file_path = strdup(optarg);
            if (!global.log_file_path)
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

    if (!global.log_file_path)
    {
        printf("global.log_file_path not allocated\n");
        return 1;
    }

    // Open log file for appending
    global.log_file = fopen(global.log_file_path, "w");
    if (!global.log_file)
    {
        perror("unable to open log file, so logging will not occur");
    }

    // Register handlers for process exit and interrupt signal (^C at terminal)
    atexit(&handle_exit);
    struct sigaction sigaction_sigint = {};
    sigaction_sigint.sa_handler = &handle_sigint;
    if (sigaction(SIGINT, &sigaction_sigint, NULL))
    {
        perror("cannot handle SIGINT, manual IPC cleanup may be needed: sigaction(2) failed");
    }

    // Create and start outgoing clock
    global.clock = clock_new(CLOCK_MODE_OUT);

    // Create master scheduler
    global.scheduler = scheduler_new(SCHEDULER_SIDE_MASTER);

    // Seed the RNG
    srand((unsigned int) time(NULL));

    while (1)
    {
        // Lock the clock
        if (clock_lock(global.clock))
            return 1;

        // Generate a time between 1 and 1.000001 seconds (1s + [0, 1000ns])
        // This duration of time passage will be simulated
        unsigned int dn = (unsigned int) (rand() % 1000); // NOLINT
        unsigned int ds = 1;

        // Advance the clock
        clock_advance(global.clock, dn, ds);

        // Get latest time from clock
        unsigned int now_nanos = clock_get_nanos(global.clock);
        unsigned int now_seconds = clock_get_seconds(global.clock);

        // Unlock the clock
        if (clock_unlock(global.clock))
            return 1;

        // Lock the scheduler
        if (scheduler_lock(global.scheduler))
            return 1;

        // If resources are available for another process to be spawned
        if (scheduler_m_available(global.scheduler))
        {
            // Launch a child
            pid_t child_pid = launch_child();

            // If doing so failed
            if (child_pid == -1)
            {
                // Unlock the scheduler
                if (scheduler_unlock(global.scheduler))
                    return 1;

                // Try again later
                continue;
            }

            // Complete spawning the child process
            // This allocates the process control block and whatnot
            if (scheduler_m_complete_spawn(global.scheduler, child_pid))
            {
                // Unlock the scheduler and die
                scheduler_unlock(global.scheduler);
                return 1;
            }
        }

        // Unlock the scheduler
        if (scheduler_unlock(global.scheduler))
            return 1;

        usleep(1);
    }

    return 0;
}

/*
        // Rules for natural termination, as specified in the assignment:
        // 1. Two simulated seconds have passed
        // 2. One hundred processes total have been spawned
        // 3. Real time limit elapsed

        // See rule 1
        if (seconds >= 2)
        {
            terminating = 1;
        }

        // See rule 2
        if (global.total_num_processes >= 100)
        {
            terminating = 2;
        }

        // See rule 3
        if (time(NULL) - time_start > param_time_limit)
        {
            terminating = 3;
        }

        // Enter critical section on shm_msg
        // This uses System V semaphores under the hood (see scheduler.c)
        if (scheduler_lock(global.shm_msg))
            break;

        // If a message is waiting
        if (scheduler_test(global.shm_msg))
        {
            // Get a copy of the message
            scheduler_msg_s msg = scheduler_poll(global.shm_msg);

            // Mark termination
            global.current_num_processes--;

            // Get the time of receipt
            if (clock_lock(global.clock))
                break;
            int nanos_recv = clock_get_nanos(global.clock);
            int seconds_recv = clock_get_seconds(global.clock);
            if (clock_unlock(global.clock))
                break;

            // Print requested log information from assignment
            if (global.log_file)
            {
                fprintf(global.log_file, "termination notice from child at %ds %dns, received at %ds %dns\n", msg.arg1,
                        msg.arg2, seconds_recv, nanos_recv);

                // NOTE: We're writing to a file "simultaneously" with two processes
                // Writes are guarded by semaphores, but there's still buffering going on with the FILE*
                // Sentences can still be interspersed, but in massive buffered blocks
                // Flushing immediately after writing here fixes that
                fflush(global.log_file);
            }

            // Leave critical section on shm_msg
            if (scheduler_unlock(global.shm_msg))
                break;

            // Fill in for that process with another child
            if (terminating == 0)
            {
                wait(NULL);
                launch_child();
            }
        }
        else
        {
            // Leave critical section on shm_msg
            if (scheduler_unlock(global.shm_msg))
                break;
        }

        // Stop after all children have terminated
        if (global.current_num_processes <= 0)
        {
            // Lock scheduler to get access to log file
            if (global.log_file && !scheduler_lock(global.shm_msg))
            {
                // Log message about why termination happened
                switch (terminating)
                {
                case 1:
                    fprintf(global.log_file, "terminated: 2 simulated seconds passed\n");
                    break;
                case 2:
                    fprintf(global.log_file, "terminated: 100 processes spawned in total\n");
                    break;
                case 3:
                    fprintf(global.log_file, "terminated: real time limit of %ds elapsed\n", param_time_limit);
                    break;
                default:
                    // ???
                    break;
                }
                fflush(global.log_file);

                // Unlock scheduler after logging
                scheduler_unlock(global.shm_msg);
            }

            break;
        }
 */
