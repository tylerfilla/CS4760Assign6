/*
 * Tyler Filla
 * CS 4760
 * Assignment 4
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <unistd.h>

#include "scheduler.h"

/**
 * The constant coefficient alpha.
 */
#define MULTI_LEVEL_ALPHA 1

/**
 * The constant coefficient beta.
 */
#define MULTI_LEVEL_BETA 1

/**
 * The base time quantum before priority is taken into account.
 */
#define BASE_TIME_QUANTUM_NANOS 10000000

#define QUANTUM_DIVISOR_PRIO_HIGH 1
#define QUANTUM_DIVISOR_PRIO_MED 2
#define QUANTUM_DIVISOR_PRIO_LOW 3

#define SHM_FTOK_CHAR 'S'
#define SEM_FTOK_CHAR 'T'

#define STATE_READY 0
#define STATE_RUN 1
#define STATE_WAIT 2

#define PRIO_HIGH 0
#define PRIO_MED 1
#define PRIO_LOW 2

/**
 * Process control block for a SUP.
 */
typedef struct
{
    /** The process ID. */
    pid_t pid;

    /** The process state. */
    int state;

    /** The process priority. */
    int prio;

    /** The second part of the simulated time at which the process spawned. */
    unsigned int spawn_time_seconds;

    /** The nanosecond part of the simulated time at which the process spawned. */
    unsigned int spawn_time_nanos;

    /** The total simulated CPU time this process has accumulated (in nanoseconds). */
    unsigned int total_cpu_time;

    /** The total simulated wait time this process has accumulated (in nanoseconds). */
    unsigned int total_wait_time;
} __process_ctl_block_s;

/**
 * A ready queue for a particular priority level.
 */
typedef struct
{
    /** The stored pids, structured as a circular buffer. */
    pid_t pids[MAX_USER_PROCS];

    /** The number of elements in the queue. */
    unsigned int length;

    /** The rolling head of the queue. */
    unsigned int idx_head;

    /** The rolling tail of the queue. */
    unsigned int idx_tail;
} __process_queue_s;

/**
 * Internal memory for scheduler. Shared.
 */
struct __scheduler_mem_s
{
    /** All process control blocks for SUPs. */
    __process_ctl_block_s procs[MAX_USER_PROCS];

    /** Number of SUPs currently running. */
    unsigned int num_procs;

    /** Three ready queues for 3 priority levels: 0 = high, 1 = medium, and 2 = low. */
    __process_queue_s ready_queues[3];

    /** The PID of the currently scheduled process, otherwise -1. */
    pid_t dispatch_proc;

    /** The time quantum, in nanoseconds, of the currently scheduled process. Valid iff dispatch_proc != -1. */
    unsigned int dispatch_quantum;
};

/**
 * Find the process control block for the given SUP.
 */
static __process_ctl_block_s* scheduler_find_pcb(scheduler_s* self, pid_t pid)
{
    for (int i = 0; i < MAX_USER_PROCS; ++i)
    {
        __process_ctl_block_s* block = &self->__mem->procs[i];

        if (block->pid == pid)
            return block;
    }

    return NULL;
}

/**
 * Create a process control block for a newly spawned SUP with the given pid.
 */
static __process_ctl_block_s* scheduler_create_pcb(scheduler_s* self, pid_t pid)
{
    // Find first unused process control block (pid of -1)
    __process_ctl_block_s* block = scheduler_find_pcb(self, -1);

    if (block == NULL)
        return NULL;

    memset(block, 0, sizeof(__process_ctl_block_s));
    block->pid = pid;

    return block;
}

/**
 * Destroy a process control block for a running SUP with the given pid.
 */
static void scheduler_destroy_pcb(scheduler_s* self, pid_t pid)
{
    // Find process control block
    __process_ctl_block_s* block = scheduler_find_pcb(self, pid);

    if (block == NULL)
        return;

    // Mark block as unused
    block->pid = -1;
}

/**
 * Clear all SUP process control blocks.
 */
static void scheduler_clear_all_pcbs(scheduler_s* self)
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
 * Enqueue the SUP by pid into the appropriate ready queue with the given priority.
 */
static void scheduler_ready_enqueue(scheduler_s* self, pid_t pid, int prio)
{
    __process_queue_s* queue = &self->__mem->ready_queues[prio];

    if (queue->length == 0)
    {
        queue->idx_head = 0;
        queue->idx_tail = 0;
    }
    else
    {
        queue->idx_tail++;
        queue->idx_tail %= MAX_USER_PROCS;
    }

    queue->length++;
    queue->pids[queue->idx_tail] = pid;
}

/**
 * Dequeue a SUP from the ready queue with the given priority.
 */
static pid_t scheduler_ready_dequeue(scheduler_s* self, int prio)
{
    __process_queue_s* queue = &self->__mem->ready_queues[prio];

    if (queue->length == 0)
    {
        fprintf(stderr, "attempt to dequeue from ready queue %d when empty\n", prio);
        exit(1);
    }

    pid_t pid = queue->pids[queue->idx_head];

    queue->length--;
    queue->pids[queue->idx_head] = -1;

    queue->idx_head++;
    queue->idx_head %= MAX_USER_PROCS;

    return pid;
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

    // Initialize ready queues
    for (int q = 0; q < 3; ++q)
    {
        for (int p = 0; p < MAX_USER_PROCS; ++p)
        {
            self->__mem->ready_queues[q].pids[p] = -1;
        }
    }

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
        scheduler_clear_all_pcbs(self);
        self->__mem->dispatch_proc = -1;
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

int scheduler_available(scheduler_s* self)
{
    // Only run on master side
    if (self->side != SCHEDULER_SIDE_MASTER)
        return 0;

    return self->__mem->num_procs < MAX_USER_PROCS;
}

int scheduler_complete_spawn(scheduler_s* self, pid_t pid)
{
    // Only run on master side
    if (self->side != SCHEDULER_SIDE_MASTER)
        return 1;

    // Create process control block
    scheduler_create_pcb(self, pid);

    // Start the process with high priority
    scheduler_ready_enqueue(self, pid, PRIO_HIGH);

    self->__mem->num_procs++;

    for (int q = 0; q < 3; ++q)
    {
        printf("queue %d (head: %d, tail: %d, length: %d): ", q, self->__mem->ready_queues[q].idx_head,
                self->__mem->ready_queues[q].idx_tail, self->__mem->ready_queues[q].length);
        for (int i = 0; i < MAX_USER_PROCS; ++i)
        {
            printf("%d, ", self->__mem->ready_queues[q].pids[i]);
        }
        printf("\n");
    }

    return 0;
}

int scheduler_complete_death(scheduler_s* self, pid_t pid)
{
    // Only run on master side
    if (self->side != SCHEDULER_SIDE_MASTER)
        return 1;

    // TODO: Add process's stats to global stats for final readout

    // Destroy process control block
    scheduler_destroy_pcb(self, pid);

    self->__mem->num_procs--;

    if (self->__mem->dispatch_proc == pid)
    {
        self->__mem->dispatch_proc = -1;
    }

    return 0;
}

pid_t scheduler_select_and_schedule(scheduler_s* self)
{
    // Only run on master side
    if (self->side != SCHEDULER_SIDE_MASTER)
        return 1;

    //
    // Selection
    //

    // The pid of the selected process
    pid_t pid;

    // Pull from high priority first, then medium priority, then low priority
    if (self->__mem->ready_queues[PRIO_HIGH].length > 0)
    {
        pid = scheduler_ready_dequeue(self, PRIO_HIGH);
    }
    else if (self->__mem->ready_queues[PRIO_MED].length > 0)
    {
        pid = scheduler_ready_dequeue(self, PRIO_MED);
    }
    else if (self->__mem->ready_queues[PRIO_LOW].length > 0)
    {
        pid = scheduler_ready_dequeue(self, PRIO_LOW);
    }
    else
    {
        // There are no ready processes in the system
        return -1;
    }

    //
    // Scheduling
    //

    // Get PCB of process
    __process_ctl_block_s* block = scheduler_find_pcb(self, pid);

    // Set process state to RUN
    block->state = STATE_RUN;

    // The time quantum allocated for this process
    unsigned int quantum = BASE_TIME_QUANTUM_NANOS;

    // Use a QUANTUM DIVISOR!!! to manipulate time quantum based on priority
    // Lower priorities should get less time, which forms a negative feedback loop
    // Hopefully, each process should fit into a groove at just the right priority for the system
    // This is just a simulation, though, so it's quite granular (not the most optimal configuration)
    switch (block->prio)
    {
    case PRIO_HIGH:
        quantum /= QUANTUM_DIVISOR_PRIO_HIGH;
        break;
    case PRIO_MED:
        quantum /= QUANTUM_DIVISOR_PRIO_MED;
        break;
    case PRIO_LOW:
        quantum /= QUANTUM_DIVISOR_PRIO_LOW;
        break;
    }

    // Configure the currently dispatched process
    // Once the scheduler is unlocked by the master, the slave scheduler(s) will pick this up
    self->__mem->dispatch_proc = pid;
    self->__mem->dispatch_quantum = quantum;

    return pid;
}

pid_t scheduler_get_dispatch_proc(scheduler_s* self)
{
    return self->__mem->dispatch_proc;
}

unsigned int scheduler_get_dispatch_quantum(scheduler_s* self)
{
    return self->__mem->dispatch_quantum;
}

int scheduler_yield(scheduler_s* self)
{
    // Only run on slave side
    if (self->side != SCHEDULER_SIDE_SLAVE)
        return 1;

    pid_t pid = getpid();

    // Make sure pids match
    if (self->__mem->dispatch_proc != pid)
        return 1;

    // Transition SUP from RUN to READY
    __process_ctl_block_s* block = scheduler_find_pcb(self, pid);
    block->state = STATE_READY;

    // TODO: Re-evaluate priority of SUP
    int prio = block->prio;

    // Enqueue the SUP as READY with the new priority
    scheduler_ready_enqueue(self, pid, prio);

    // Signal master scheduler that previously dispatched process has yielded
    // After the slave unlocks the scheduler, the CPU will be considered idle
    self->__mem->dispatch_proc = -1;

    return 0;
}
