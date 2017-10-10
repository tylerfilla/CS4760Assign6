/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#ifndef CLOCK_H
#define CLOCK_H

/**
 * Mode indicating a clock only reads from its internal memory structures.
 */
#define CLOCK_MODE_IN 0

/**
 * Mode indicating a clock writes to its internal memory structures.
 */
#define CLOCK_MODE_OUT 1

/**
 * State indicating a clock is not running.
 */
#define CLOCK_NOT_RUNNING 0

/**
 * State indicating a clock is running.
 */
#define CLOCK_RUNNING 1

typedef struct __clock_mem_s __clock_mem_s;

typedef struct
{
    /** Clock mode. */
    int mode;

    /** Whether the clock is currently running. */
    int running;

    /** The ID of the shared memory segment used. */
    int shmid;

    /** Internal shared memory structure. */
    __clock_mem_s* __mem;
} clock_s;

/**
 * Create a clock instance.
 */
#define clock_new(mode) clock_construct(malloc(sizeof(clock_s)), mode)

/**
 * Destroy a clock instance.
 */
#define clock_delete(clock) free(clock_destruct(clock))

/**
 * Construct a clock instance.
 *
 * @param self The clock instance
 * @param mode The clock mode
 * @return The clock instance, constructed
 */
clock_s* clock_construct(clock_s* clock, int mode);

/**
 * Destruct a clock instance.
 *
 * @param clock The clock instance
 * @return The clock instance, destructed
 */
clock_s* clock_destruct(clock_s* clock);

/**
 * Tick a clock instance.
 *
 * @param clock The clock instance
 */
void clock_tick(clock_s* clock);

/**
 * Retrieve the current number of nanoseconds elapsed since the last second since a clock started.
 *
 * @param clock The clock instance
 * @return Its nanosecond count
 */
int clock_get_nanos(clock_s* clock);

/**
 * Retrieve the current number of seconds elapsed since a clock started.
 *
 * @param clock The clock instance
 * @return Its second count
 */
int clock_get_seconds(clock_s* clock);

#endif // #ifndef CLOCK_H
