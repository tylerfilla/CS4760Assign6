/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "clock.h"
#include "messenger.h"

#define DEFAULT_LOG_FILE_PATH "oss.log"
#define DEFAULT_MAX_SLAVE_COUNT 5
#define DEFAULT_TIME_LIMIT 20

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
    // If called without arguments, print usage and exit abnormally
    if (argc == 1)
    {
        print_usage(stderr, argv[0]);
        return 1;
    }

    // Flag indicating the program should print help text and exit normally
    int flag_print_help = 0;

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
            flag_print_help = 1;
            break;
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
            return 1;
        }
    }

    if (flag_print_help)
    {
        print_help(stdout, argv[0]);
        return 0;
    }

    // Create outgoing clock
    clock_s* clock = clock_new(CLOCK_MODE_OUT);

    // Create master messenger
    messenger_s* shm_msg = messenger_new(MESSENGER_SIDE_MASTER);

    // Start clock
    clock_start(clock);

    while (1)
    {
        // Update clock
        clock_tick(clock);

        // FIXME
        if (clock_get_seconds(clock) >= 2)
            break;
    }

    // Clean stuff up
    messenger_delete(shm_msg);
    clock_delete(clock);
    free(param_log_file_path);

    return 0;
}
