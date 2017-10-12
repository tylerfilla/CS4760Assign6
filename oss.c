/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>
#include <sys/wait.h>

#include "clock.h"
#include "messenger.h"

#define DEFAULT_LOG_FILE_PATH "oss.log"
#define DEFAULT_MAX_SLAVE_COUNT 5
#define DEFAULT_TIME_LIMIT 20

static struct
{
    /** The open log file. */
    FILE* log_file;

    /** Total number of living or dead child processes launched. */
    int total_num_processes;

    /** Total number of currently living processes. */
    int current_num_processes;

    /** The outgoing clock instance. */
    clock_s* clock;

    /** The master messenger instance. */
    messenger_s* shm_msg;
} global;

static void handle_exit()
{
    // Clean up IPC components
    if (global.clock)
    {
        clock_delete(global.clock);
    }
    if (global.shm_msg)
    {
        messenger_delete(global.shm_msg);
    }

    // Close log file if it is open
    if (global.log_file)
    {
        fclose(global.log_file);
    }
}

static void handle_sigint(int sig)
{
    fprintf(stderr, "execution interrupted\n");

    // Let the normal exit handler take over
    exit(2);
}

static int launch_child()
{
    int child_pid = fork();
    if (child_pid == 0)
    {
        // Fork succeeded, now in child

        // Redirect child stdout to log file
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
        global.total_num_processes++;
        global.current_num_processes++;
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
    fprintf(dest, "    -s <num>    Set <num> as max number of concurrent user processes (default 5)\n");
    fprintf(dest, "    -t <time>   Terminate after <time> seconds (default 20)\n");
}

static void print_usage(FILE* dest, const char* executable_name)
{
    fprintf(dest, "Usage: %s [option..]\n", executable_name);
    fprintf(dest, "Try `%s -h' for more information.\n", executable_name);
}

int main(int argc, char* argv[])
{
    // Other supplied parameters
    char* param_log_file_path = strdup(DEFAULT_LOG_FILE_PATH);
    int param_max_slave_count = DEFAULT_MAX_SLAVE_COUNT;
    int param_time_limit = DEFAULT_TIME_LIMIT;

    if (!param_log_file_path)
    {
        perror("strdup(3) failed");
        return 1;
    }

    // Handle command-line options
    int opt;
    while ((opt = getopt(argc, argv, "hl:s:t:")) != -1)
    {
        switch (opt)
        {
        case 'h':
            print_help(stdout, argv[0]);
            return 0;
        case 'l':
            free(param_log_file_path);
            param_log_file_path = strdup(optarg);
            break;
        case 's':
        {
            char* end = NULL;
            param_max_slave_count = (int) strtoul(optarg, &end, 10);
            if (*end)
            {
                fprintf(stderr, "invalid max slave count: %s\n", optarg);
                return 1;
            }
            break;
        }
        case 't':
        {
            char* end = NULL;
            param_time_limit = (int) strtoul(optarg, &end, 10);
            if (*end)
            {
                fprintf(stderr, "invalid time limit: %s\n", optarg);
                return 1;
            }
            break;
        }
        default:
            fprintf(stderr, "invalid option: -%c\n", opt);
            print_usage(stderr, argv[0]);
            return 1;
        }
    }

    // Open log file for appending
    global.log_file = fopen(param_log_file_path, "a");
    if (!global.log_file)
    {
        perror("unable to open log file, logging will not occur");
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

    // Create master messenger
    global.shm_msg = messenger_new(MESSENGER_SIDE_MASTER);

    // Get starting wall clock time in seconds
    time_t time_start = time(NULL);

    // Launch first set of children
    // I understood the assignment to mean the -s option to specify the max # of children at a time (not total)
    for (int i = 0; i < param_max_slave_count; ++i)
    {
        launch_child();
    }

    // Whether the loop is terminating
    // While terminating, oss will still accept messages, but won't spawn children
    int terminating = 0;

    while (1)
    {
        // Update the simulated clock
        if (clock_lock(global.clock))
            break;
        clock_tick(global.clock);
        int seconds = clock_get_seconds(global.clock);
        if (clock_unlock(global.clock))
            break;

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
        // This uses System V semaphores under the hood (see messenger.c)
        if (messenger_lock(global.shm_msg))
            break;

        // If a message is waiting
        if (messenger_test(global.shm_msg))
        {
            // Get a copy of the message
            messenger_msg_s msg = messenger_poll(global.shm_msg);

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
            if (messenger_unlock(global.shm_msg))
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
            if (messenger_unlock(global.shm_msg))
                break;
        }

        // Stop after all children have terminated
        if (global.current_num_processes <= 0)
        {
            // Lock messenger to get access to log file
            if (global.log_file && !messenger_lock(global.shm_msg))
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

                // Unlock messenger after logging
                messenger_unlock(global.shm_msg);
            }

            break;
        }

        usleep(1);
    }

    // Created with strdup(3)
    free(param_log_file_path);

    return 0;
}
