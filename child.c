/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "clock.h"
#include "messenger.h"

int main(int argc, char* argv[])
{
    printf("Hello, world! This is the child.\n");

    sleep(1);

    // Create and start incoming clock
    clock_s* clock = clock_new(CLOCK_MODE_IN);

    // Create slave messenger
    messenger_s* messenger = messenger_new(MESSENGER_SIDE_SLAVE);

    printf("%d nanos\n", clock_get_nanos(clock));

    // Clean stuff up
    clock_delete(clock);
    messenger_delete(messenger);

    return 0;
}
