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
int scheduler_available(scheduler_s* scheduler);

/**
 * Complete the spawning of a new SUP. This allocates resources for it.
 *
 * Master only.
 *
 * @param scheduler The scheduler instance
 * @param pid The new process's pid
 * @param time_nanos The nanosecond part of the simulated time
 * @param time_seconds The second part of the simulated time
 * @return Zero on success, otherwise nonzero
 */
int scheduler_complete_spawn(scheduler_s* scheduler, pid_t pid, unsigned int time_nanos, unsigned int time_seconds);

/**
 * Complete the death of an old SUP. This releases resources for it.
 *
 * @param scheduler The scheduler instance
 * @param pid The old process's pid
 * @return Zero on success, otherwise nonzero
 */
int scheduler_complete_death(scheduler_s* scheduler, pid_t pid, unsigned int, unsigned int);

/**
 * On a master-side scheduler instance, select and schedule the next SUP to execute.
 *
 * Master only.
 *
 * @param scheduler The scheduler instance
 * @param time_nanos The nanosecond time part
 * @param time_seconds The second time part
 * @return The pid of the scheduled process, otherwise -1
 */
pid_t scheduler_select_and_schedule(scheduler_s* scheduler, unsigned int time_nanos, unsigned int time_seconds);

/**
 * On a slave-side scheduler instance, get the currently scheduled and dispatched process's pid.
 *
 * @param scheduler The scheduler instance
 * @return Nonzero if such is the case, otherwise zero
 */
pid_t scheduler_get_dispatch_proc(scheduler_s* scheduler);

/**
 * On a slave-side scheduler instance, get the currently scheduled and dispatched process's quantum.
 *
 * @param scheduler The scheduler instance
 * @return The scheduled time quantum in nanoseconds
 */
unsigned int scheduler_get_dispatch_quantum(scheduler_s* scheduler);

/**
 * On a slave-side scheduler instance, indicate a yield of control back to the system. Also useful for indicating when
 * a waited-on event has finished.
 *
 * @param scheduler The scheduler instance
 * @param time_nanos The nanosecond part of the current simulated time
 * @param time_seconds The second part of the current simulated time
 * @param cpu_time The simulated time taken, in nanoseconds, by the last burst
 * @return Zero on success, otherwise nonzero
 */
int scheduler_yield(scheduler_s* scheduler, unsigned int time_nanos, unsigned int time_seconds, unsigned long cpu_time);

/**
 * On a slave-side scheduler instance, indicate the SUP is waiting on an event.
 *
 * @param scheduler The scheduler instance
 * @return Zero on success, otherwise nonzero
 */
int scheduler_wait(scheduler_s* scheduler);

/**
 * Dump a human-readable summary of scheduler state to the given file.
 *
 * @param scheduler The scheduler instance
 * @param dest The destination file
 */
void scheduler_dump_summary(scheduler_s* scheduler, FILE* dest);

#endif // #ifndef SCHEDULER_H
