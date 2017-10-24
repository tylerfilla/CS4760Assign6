/*
 * Tyler Filla
 * CS 4760
 * Assignment 4
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "scheduler.h"

#define SHM_FTOK_CHAR 'S'
#define SEM_FTOK_CHAR 'T'

#define BASE_TIME_QUANTUM_NANOS 10000000

/**
 * Process control block for a SUP.
 */
typedef struct
{
    /** The process ID. */
    pid_t pid;
} __process_ctl_block_s;

/**
 * Internal memory for scheduler. Shared.
 */
struct __scheduler_mem_s
{
    //
    // Process Control Blocks
    //

    /** All process control blocks for SUPs. */
    __process_ctl_block_s procs[MAX_USER_PROCS];

    /** Number of SUPs currently running. */
    unsigned int num_procs;

    //
    // Queues
    //

    /** The first process queue. High priority. */
    pid_t queue_0[MAX_USER_PROCS];

    /** Number of SUPs in queue 0. */
    unsigned int queue_0_len;

    /** The rolling head of the first process queue. */
    unsigned int queue_0_head;

    /** The rolling tail of the first process queue. */
    unsigned int queue_0_tail;

    /** The second process queue. Medium priority. */
    pid_t queue_1[MAX_USER_PROCS];

    /** Number of SUPs in queue 1. */
    unsigned int queue_1_len;

    /** The rolling head of the second process queue. */
    unsigned int queue_1_head;

    /** The rolling tail of the second process queue. */
    unsigned int queue_1_tail;

    /** The third process queue. Low priority. */
    pid_t queue_2[MAX_USER_PROCS];

    /** Number of SUPs in queue 2. */
    unsigned int queue_2_len;

    /** The rolling head of the third process queue. */
    unsigned int queue_2_head;

    /** The rolling tail of the third process queue. */
    unsigned int queue_2_tail;

    //
    // Dispatch
    //

    /** The PID of the currently scheduled process, otherwise -1. */
    pid_t dispatch_proc;

    /** The time quantum, in nanoseconds, of the currently scheduled process. Valid iff dispatch_proc != -1. */
    unsigned int dispatch_quantum;
};

/**
 * Dequeue a SUP's pid from the desired scheduler queue. This implements a circular buffer.
 */
#define scheduler_dequeue_proc_pid(self, queue) \
            ((pid_t) (self)->__mem->queue_##queue[((self)->__mem->queue_##queue##_head++) % MAX_USER_PROCS])

/**
 * Enqueue a SUP's pid to the desired scheduler queue. This implements a circular buffer.
 */
#define scheduler_enqueue_proc_pid(self, pid, queue) \
            ((self)->__mem->queue_##queue[(++(self)->__mem->queue_##queue##_tail) % MAX_USER_PROCS] = (pid_t) (pid))

/**
 * Allocate a process control block for a newly spawned SUP with the given pid.
 */
static void scheduler_allocate_pcb(scheduler_s* self, pid_t pid)
{
    // Find index of first unallocated block
    int block;
    for (block = 0; block < MAX_USER_PROCS; ++block)
    {
        if (self->__mem->procs[block].pid == -1)
            break;
    }

    if (block == MAX_USER_PROCS)
    {
        return;
    }

    self->__mem->procs[block].pid = pid;
    self->__mem->num_procs++;
}

/**
 * Clear all SUP process control blocks.
 */
static void scheduler_clear_pcbs(scheduler_s* self)
{
    // Zero out all memory for all blocks
    memset(self->__mem->procs, 0, MAX_USER_PROCS * sizeof(__process_ctl_block_s));

    // Add pids of -1 to indicate cleared blocks
    for (int i = 0; i < MAX_USER_PROCS; ++i)
    {
        self->__mem->procs[i].pid = -1;
    }
}

/**
 * Open a master side scheduler.
 */
static int scheduler_open_master(scheduler_s* self)
{
    errno = 0;

    //
    // Shared Memory
    //

    int shmid = -1;
    void* shm = (void*) -1;

    // Obtain IPC key for shared memory
    key_t shm_key = ftok(".", SHM_FTOK_CHAR);
    if (errno)
    {
        perror("open master scheduler: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__scheduler_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("open master scheduler: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("open master scheduler: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    int semid = -1;

    // Obtain IPC key for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("open master scheduler: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("open master scheduler: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("open master scheduler: unable to set sem value: semctl(2) failed");
        goto fail_sem;
    }

    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    return 0;

fail_sem:
    // Remove semaphore set, if needed
    if (semid >= 0)
    {
        semctl(semid, 0, IPC_RMID);
        if (errno)
        {
            perror("open master scheduler: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != (void*) -1)
    {
        shmdt(shm);
        if (errno)
        {
            perror("open master scheduler: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("open master scheduler: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

/**
 * Open a slave side scheduler.
 */
static int scheduler_open_slave(scheduler_s* self)
{
    errno = 0;

    //
    // Shared Memory
    //

    void* shm = (void*) -1;

    // Obtain IPC key for shared memory
    key_t shm_key = ftok(".", SHM_FTOK_CHAR);
    if (errno)
    {
        perror("open slave scheduler: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Get ID of the shared memory segment
    int shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("open slave scheduler: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("open slave scheduler: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Obtain IPC for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("open slave scheduler: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    int semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("open slave scheduler: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    return 0;

fail_sem:
fail_shm:
    // Detach shared memory, if needed
    if (shm != (void*) -1)
    {
        shmdt(shm);
        if (errno)
        {
            perror("open slave scheduler: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

/**
 * Close a master side scheduler.
 */
static int scheduler_close_master(scheduler_s* self)
{
    errno = 0;

    //
    // Shared Memory
    //

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("close master scheduler: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("close master scheduler: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("close master scheduler: unable to remove sem: semctl(2) failed");
        goto fail_sem;
    }

    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_sem:
fail_shm:
    return 1;
}

/**
 * Close a slave side scheduler.
 */
static int scheduler_close_slave(scheduler_s* self)
{
    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("close slave scheduler: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
    return 1;
}

scheduler_s* scheduler_construct(scheduler_s* self, int side)
{
    if (self == NULL)
        return NULL;

    self->side = side;
    self->shmid = -1;
    self->semid = -1;

    // Open the scheduler
    switch (side)
    {
    case SCHEDULER_SIDE_MASTER:
        if (scheduler_open_master(self))
            return NULL;
        scheduler_clear_pcbs(self);
        break;
    case SCHEDULER_SIDE_SLAVE:
        if (scheduler_open_slave(self))
            return NULL;
        break;
    default:
        break;
    }

    return self;
}

scheduler_s* scheduler_destruct(scheduler_s* self)
{
    if (self == NULL)
        return NULL;

    // Close the scheduler
    switch (self->side)
    {
    case SCHEDULER_SIDE_MASTER:
        if (scheduler_close_master(self))
            return NULL;
        break;
    case SCHEDULER_SIDE_SLAVE:
        if (scheduler_close_slave(self))
            return NULL;
        break;
    default:
        break;
    }

    return self;
}

int scheduler_lock(scheduler_s* self)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("scheduler lock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int scheduler_unlock(scheduler_s* self)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("scheduler unlock: unable to increment sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int scheduler_m_available(scheduler_s* self)
{
    // Only run on master side
    if (self->side != SCHEDULER_SIDE_MASTER)
        return 0;

    return self->__mem->num_procs < MAX_USER_PROCS;
}

int scheduler_m_complete_spawn(scheduler_s* self, pid_t pid)
{
    // Only run on master side
    if (self->side != SCHEDULER_SIDE_MASTER)
        return 1;

    // Create process control block
    scheduler_allocate_pcb(self, pid);

    // Start the process in queue 0
    scheduler_enqueue_proc_pid(self, pid, 0);

    return 0;
}

pid_t scheduler_m_select_and_schedule(scheduler_s* self)
{
    // Only run on master side
    if (self->side != SCHEDULER_SIDE_MASTER)
        return 1;

    return 0;
}

pid_t scheduler_s_get_dispatch_proc(scheduler_s* self)
{
    // Only run on slave side
    if (self->side != SCHEDULER_SIDE_SLAVE)
        return 0;

    return self->__mem->dispatch_proc;
}

unsigned int scheduler_s_get_dispatch_quantum(scheduler_s* self)
{
    // Only run on slave side
    if (self->side != SCHEDULER_SIDE_SLAVE)
        return 0;

    return self->__mem->dispatch_quantum;
}
