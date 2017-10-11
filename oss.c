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

#include "clock.h"
#include "messenger.h"

#define DEFAULT_LOG_FILE_PATH "oss.log"
#define DEFAULT_MAX_SLAVE_COUNT 5
#define DEFAULT_TIME_LIMIT 20

/**
 * Total number of living or dead child processes launched.
 */
static int total_num_processes = 0;

static int launch_child()
{
    int child_pid = fork();
    if (child_pid == 0)
    {
        // Fork succeeded, now in child
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
        total_num_processes++;
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
    fprintf(dest, "    -s <num>    Set <num> as maximum number of slave processes (default 5)\n");
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

    // Create and start outgoing clock
    clock_s* clock = clock_new(CLOCK_MODE_OUT);

    // Create master messenger
    messenger_s* shm_msg = messenger_new(MESSENGER_SIDE_MASTER);

    // Get starting wall clock time in seconds
    time_t time_start = time(NULL);

    while (1)
    {
        // Update the simulated clock
        clock_lock(clock);
        clock_tick(clock);
        int seconds = clock_get_seconds(clock);
        clock_unlock(clock);

        // Rules for natural termination, as specified in the assignment:
        // 1. Two simulated seconds have passed
        // 2. One hundred processes total have been spawned
        // 3. Real time limit elapsed

        // See rule 1 above
        if (seconds >= 2)
        {
            fprintf(stderr, "rule 1\n");
            break;
        }

        // See rule 2 above
        if (total_num_processes >= 100)
        {
            fprintf(stderr, "rule 2\n");
            break;
        }

        // See rule 3 above
        if (time(NULL) - time_start > param_time_limit)
        {
            fprintf(stderr, "rule 3\n");
            break;
        }
    }

    // Clean stuff up
    messenger_delete(shm_msg);
    clock_delete(clock);
    free(param_log_file_path);

    return 0;
}
