/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

//
// clock.c
// This is the main file for the clock library.
//

#include <errno.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "clock.h"
#include "perrorf.h"

/**
 * Inner memory structure for clock function. This gets shared among processes.
 */
struct __clock_mem_s
{
    /** Second counter. */
    long seconds;

    /** Nanosecond counter. */
    int nanoseconds;
};

clock_s* clock_construct(clock_s* self, int mode)
{
    self->mode = mode;
    self->running = CLOCK_NOT_RUNNING;
    self->shmid = -1;
    self->__mem = NULL;

    return self;
}

clock_s* clock_destruct(clock_s* self)
{
    return self;
}

/**
 * Start the clock under IN mode.
 */
static int clock_start_in(clock_s* self)
{
    if (self->running)
        return -100;

    return -2;
}

/**
 * Start the clock under OUT mode.
 */
static int clock_start_out(clock_s* self)
{
    if (self->running)
        return -100;

    // Obtain the IPC key
    key_t key = ftok("/bin/bash", 'Q');
    if (errno)
    {
        perrorf("unable to obtain key: ftok(3) failed");
        return 1;
    }

    // Create a shared memory segment
    int shmid = shmget(key, sizeof(__clock_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perrorf("unable to create shm: shmget(2) failed");
        return 2;
    }

    // Attach shared memory segment
    __clock_mem_s* mem = shmat(shmid, NULL, 0);
    if (errno)
    {
        perrorf("unable to attach shm: shmat(2) failed");

        // Destroy segment
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perrorf("unable to clean up shm: shmctl(2) failed");
            return 4;
        }

        return 3;
    }

    self->running = CLOCK_RUNNING;
    self->shmid = shmid;
    self->__mem = mem;

    return -2;
}

int clock_start(clock_s* self)
{
    switch (self->mode)
    {
    case CLOCK_MODE_IN:
        return clock_start_in(self);
    case CLOCK_MODE_OUT:
        return clock_start_out(self);
    default:
        return -1;
    }
}

/**
 * Stop the clock under IN mode.
 */
static int clock_stop_in(clock_s* self)
{
    if (self->running)
        return -100;

    return -2;
}

/**
 * Stop the clock under OUT mode.
 */
static int clock_stop_out(clock_s* self)
{
    if (!self->running)
        return -100;

    //shmdt(self->shmid

    return -2;
}

int clock_stop(clock_s* self)
{
    switch (self->mode)
    {
    case CLOCK_MODE_IN:
        return clock_stop_in(self);
    case CLOCK_MODE_OUT:
        return clock_stop_out(self);
    default:
        return -1;
    }
}

void clock_tick(clock_s* self)
{
}

long clock_get_seconds(clock_s* self)
{
    return 0l;
}

int clock_get_nanos(clock_s* self)
{
    return 0;
}
