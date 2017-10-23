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

/**
 * The maximum number of concurrent simulated user processes (SUPs).
 */
#define MAX_USER_PROCS 18

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
 * Determine if there are enough resources to spawn another SUp. Take care to keep the scheduler locked between a call
 * to this function and a call to scheduler_master_complete_spawn.
 *
 * Master only.
 *
 * @param scheduler The scheduler instance
 * @return Nonzero if such is the case, otherwise zero
 */
int scheduler_m_available(scheduler_s* scheduler);

/**
 * Complete the spawning of a new SUP. This allocates resources
 *
 * Master only.
 *
 * @param scheduler The scheduler instance
 * @param pid The new process's pid
 * @return Zero on success, otherwise nonzero
 */
int scheduler_m_complete_spawn(scheduler_s* scheduler, pid_t pid);

/**
 * On a master-side scheduler instance, select and schedule the next SUP to execute.
 *
 * Master only.
 *
 * @param scheduler The scheduler instance
 * @return The pid of the scheduled process, otherwise -1
 */
pid_t scheduler_m_select_and_schedule(scheduler_s* scheduler);

/**
 * On a slave-side scheduler instance, get the currently scheduled and dispatched process's pid.
 *
 * Slave only.
 *
 * @param scheduler The scheduler instance
 * @return Nonzero if such is the case, otherwise zero
 */
pid_t scheduler_s_get_dispatch_proc(scheduler_s* scheduler);

/**
 * On a slave-side scheduler instance, get the currently scheduled and dispatched process's quantum.
 *
 * Slave only.
 *
 * @param scheduler The scheduler instance
 * @return The scheduled time quantum in nanoseconds
 */
unsigned int scheduler_s_get_dispatch_quantum(scheduler_s* scheduler);

#endif // #ifndef SCHEDULER_H
