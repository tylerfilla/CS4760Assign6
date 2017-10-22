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

typedef struct scheduler_msg_s
{
    /** An integer argument. */
    int arg1;

    /** An integer argument. */
    int arg2;
} scheduler_msg_s;

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
#define scheduler_new(side) scheduler_construct(malloc(sizeof(scheduler_s)), side)

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

/*

/**
 * Determine if a scheduler has a message waiting.
 *
 * @param scheduler The scheduler instance
 * @return Nonzero if such is the case, otherwise zero
 * /
int scheduler_test(scheduler_s* scheduler);

/**
 * Remove and return a waiting message from a scheduler.
 *
 * @param scheduler The scheduler instance
 * @return The message
 * /
scheduler_msg_s scheduler_poll(scheduler_s* scheduler);

/**
 * Set the waiting message on a scheduler.
 *
 * @param scheduler The scheduler instance
 * @param msg The message
 * /
void scheduler_offer(scheduler_s* scheduler, scheduler_msg_s msg);

*/

#endif // #ifndef SCHEDULER_H
