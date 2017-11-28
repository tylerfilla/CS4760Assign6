/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
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

    /** The ID of the semaphore set protecting the internal memory. */
    int semid;

    /** Internal shared memory structure. */
    __clock_mem_s* __mem;
} clock_s;

/**
 * Create a clock instance.
 */
#define clock_new(mode) clock_construct(malloc(sizeof(clock_s)), (mode))

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
 * Advance a clock instance by the given time difference.
 *
 * @param clock The clock instance
 * @param dn Additional nanoseconds
 * @param ds Additional seconds
 */
void clock_advance(clock_s* clock, unsigned int dn, unsigned int ds);

/**
 * Lock a clock for exclusive access. This blocks if already locked.
 *
 * @param clock The clock instance
 * @return Zero on success, otherwise nonzero
 */
int clock_lock(clock_s* clock);

/**
 * Unlock a locked clock.
 *
 * @param clock The clock instance
 * @return Zero on success, otherwise nonzero
 */
int clock_unlock(clock_s* clock);

/**
 * Retrieve the current number of nanoseconds elapsed since the last second since a clock started.
 *
 * @param clock The clock instance
 * @return Its nanosecond count or -1 on failure
 */
unsigned int clock_get_nanos(clock_s* clock);

/**
 * Retrieve the current number of seconds elapsed since a clock started.
 *
 * @param clock The clock instance
 * @return Its second count or -1 on failure
 */
unsigned int clock_get_seconds(clock_s* clock);

#endif // #ifndef CLOCK_H
