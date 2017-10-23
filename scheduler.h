/*
 * Tyler Filla
 * CS 4760
 * Assignment 4
 */

#ifndef SCHEDULER_H
#define SCHEDULER_H

/**
 * Indicates a scheduler instance is a master.
 */
#define SCHEDULER_SIDE_MASTER 0

/**
 * Indicates a scheduler instance is a slave.
 */
#define SCHEDULER_SIDE_SLAVE 1

typedef struct __scheduler_mem_s __scheduler_mem_s;

typedef struct
{
    /** The local side of the scheduler channel. */
    int side;

    /** The ID of the shared memory segment used. */
    int shmid;

    /** The ID of the semaphore set protecting the internal memory. */
    int semid;

    /** Internal shared memory structure. */
    __scheduler_mem_s* __mem;
} scheduler_s;

/**
 * Create a scheduler instance.
 */
#define scheduler_new(side) scheduler_construct(malloc(sizeof(scheduler_s)), (side))

/**
 * Destroy a scheduler instance.
 */
#define scheduler_delete(scheduler) free(scheduler_destruct(scheduler))

/**
 * Construct a scheduler instance.
 *
 * @param scheduler The scheduler instance
 * @param side The local scheduler side
 * @return The scheduler instance, constructed
 */
scheduler_s* scheduler_construct(scheduler_s* scheduler, int side);

/**
 * Destruct a scheduler instance.
 *
 * @param scheduler The scheduler instance
 * @return The scheduler instance, destructed
 */
scheduler_s* scheduler_destruct(scheduler_s* scheduler);

/**
 * Lock the scheduler for exclusive access. This blocks if already locked.
 *
 * @param scheduler The scheduler instance
 * @return Zero on success, otherwise nonzero
 */
int scheduler_lock(scheduler_s* scheduler);

/**
 * Unlock a locked scheduler.
 *
 * @param scheduler The scheduler instance
 * @return Zero on success, otherwise nonzero
 */
int scheduler_unlock(scheduler_s* scheduler);

/**
 * On a master-side scheduler instance, select and schedule the next process to execute.
 *
 * Master only.
 *
 * @param scheduler The scheduler instance
 * @return Zero on success, otherwise nonzero
 */
int scheduler_select_and_schedule(scheduler_s* scheduler);

/**
 * On a slave-side scheduler instance, test if the calling process should execute.
 *
 * Slave only.
 *
 * @param scheduler The scheduler instance
 * @return Nonzero if such is the case, otherwise zero
 */
int scheduler_slave_ismyturn(scheduler_s* scheduler);

#endif // #ifndef SCHEDULER_H
