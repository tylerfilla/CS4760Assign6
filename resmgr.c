/*
 * Tyler Filla
 * CS 4760
 * Assignment 5
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include "resmgr.h"

#define SHM_FTOK_CHAR 'R'
#define SEM_FTOK_CHAR 'S'

/**
 * The maximum number of concurrent user processes.
 */
#define MAX_USER_PROCS 18

/**
 * The maximum number of instances per resource class.
 */
#define MAX_INSTANCES 10

/**
 * A process queue for a particular resource class.
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
} __res_queue_s;

/**
 * A resource descriptor for a particular resource class.
 */
typedef struct
{
    /** Nonzero if the resource is reusable, otherwise zero. */
    int reusable;

    /** The number of resources of this class remaining. */
    unsigned int remaining;

    /** The process wait queue. */
    __res_queue_s wait_queue;

    /** A list of all allocations made, duplication allowed. Inefficient, but it works. */
    int allocations[MAX_USER_PROCS * MAX_INSTANCES];

    /** The length of the allocations list. */
    unsigned int num_allocations;
} __rd_s;

struct __resmgr_mem_s
{
    /** A map from internally-recognized process indices to real world pids. */
    pid_t procs[MAX_USER_PROCS];

    /** The number of recognized processes. */
    unsigned int num_procs;

    /** The resource descriptors. */
    __rd_s resources[NUM_RESOURCE_CLASSES];

    /** Total number of allocations made. */
    unsigned int stat_num_allocations;
};

/**
 * Enqueue to a process queue.
 */
static void __res_queue_offer(__res_queue_s* queue, pid_t proc)
{
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
    queue->pids[queue->idx_tail] = proc;
}

/**
 * Dequeue from a process queue.
 */
static pid_t __res_queue_poll(__res_queue_s* queue)
{
    if (queue->length == 0)
        return -1;

    pid_t proc = queue->pids[queue->idx_head];

    queue->length--;
    queue->pids[queue->idx_head] = -1;

    queue->idx_head++;
    queue->idx_head %= MAX_USER_PROCS;

    return proc;
}

/**
 * Assign a process index to a new pid.
 */
static void resmgr_add_proc(resmgr_s* self, pid_t proc)
{
    self->__mem->procs[self->__mem->num_procs++] = proc;
}

/**
 * Remove the index mapping for the given pid.
 */
static void resmgr_remove_proc(resmgr_s* self, int proc_idx)
{
    // If at end of list, do nothing
    if (proc_idx >= self->__mem->num_procs - 1)
        return;

    // Move subsequent process mappings down one
    for (int i = proc_idx; i < self->__mem->num_procs; ++i)
    {
        self->__mem->procs[i] = self->__mem->procs[i + 1];
    }

    // Decrement process count
    self->__mem->num_procs--;
}

static pid_t resmgr_get_proc(resmgr_s* self, int proc_idx)
{
    return self->__mem->procs[proc_idx];
}

/**
 * Do a reverse lookup to get the process index for the given pid.
 */
static int resmgr_look_up_proc(resmgr_s* self, pid_t proc)
{
    for (int proc_idx = 0; proc_idx < self->__mem->num_procs; ++proc_idx)
    {
        if (self->__mem->procs[proc_idx] == proc)
        {
            return proc_idx;
        }
    }

    return -1;
}

/**
 * Enqueue the given process into the wait queue on the given resource.
 */
static void resmgr_wait_enqueue(resmgr_s* self, pid_t proc, int res)
{
    __res_queue_offer(&self->__mem->resources[res].wait_queue, proc);
}

/**
 * Dequeue the given process from the wait queue on the given resource.
 */
static pid_t resmgr_wait_dequeue(resmgr_s* self, int res)
{
    return __res_queue_poll(&self->__mem->resources[res].wait_queue);
}

/**
 * Immediately allocate a resource to a process. Not always legal, hence wait queues.
 */
static int resmgr_allocate_resource(resmgr_s* self, pid_t proc, int res)
{
    // Get resource descriptor
    __rd_s* rd = &self->__mem->resources[res];

    printf("resmgr: allocating resource %d to process %d\n", res, proc);

    if (rd->num_allocations >= MAX_USER_PROCS)
    {
        printf("resmgr: cannot allocate resource %d to process %d: acquisition list full\n", res, proc);
        return 1;
    }

    // Add entry to acquisition list
    rd->allocations[rd->num_allocations++] = proc;
    rd->num_allocations++;

    // Decrement remaining count
    // This is ultimately what limits resources and causes DEADLOCKS
    rd->remaining--;

    printf("resmgr: %d instances of resource %d now remain\n", rd->remaining, res);

    self->__mem->stat_num_allocations++;

    // Print allocation table after every 20 allocations
    if (self->__mem->stat_num_allocations > 0 && self->__mem->stat_num_allocations % 20 == 0)
    {
        // Header
        printf("PID      Index ");
        for (int p_res = 0; p_res < NUM_RESOURCE_CLASSES; ++p_res)
        {
            printf("%3d ", p_res);
        }
        printf("\n");

        // Body
        for (int proc_idx = 0; proc_idx < self->__mem->num_procs; ++proc_idx)
        {
            pid_t p_proc = self->__mem->procs[proc_idx];

            printf("%8d %5d", p_proc, proc_idx);
            for (int p_res = 0; p_res < NUM_RESOURCE_CLASSES; ++p_res)
            {
                // Get resource descriptor
                __rd_s* p_rd = &self->__mem->resources[p_res];

                // Count corresponding allocations
                int num_instances_allocated = 0;
                for (int i = 0; i < p_rd->num_allocations; ++i)
                {
                    // If calling process is found
                    if (p_rd->allocations[i] == p_proc)
                    {
                        // Increment count
                        num_instances_allocated++;
                    }
                }

                printf("%3d ", num_instances_allocated);
            }
            printf("\n");
        }
    }

    return 0;
}

/**
 * Remove the given process from the wait queue on the given resource.
 * /
static void resmgr_wait_remove(resmgr_s* self, pid_t proc, int res)
{
    // This is a very inefficient hack
    // Take out all pids and put back uninteresting ones, ouch...
    for (int p = 0; p < self->__mem->resources[res].wait_queue.length; ++p)
    {
        pid_t p_proc = resmgr_wait_dequeue(self, res);

        // Skip matching pid
        // Should this also increment p?
        if (p_proc == proc)
            continue;

        resmgr_wait_enqueue(self, p_proc, res);
    }
}
*/

static int resmgr_start_client(resmgr_s* self)
{
    if (self->running)
        return 1;

    errno = 0;

    //
    // Shared Memory
    //

    void* shm = (void*) -1;

    // Obtain IPC key for shared memory
    key_t shm_key = ftok(".", SHM_FTOK_CHAR);
    if (errno)
    {
        perror("start client resource manager: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Obtain existing shared memory segment
    int shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("start client resource manager: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment as read-only
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start client resource manager: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Obtain IPC key for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("start client resource manager: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    int semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("start client resource manager: unable to get sem: semget(3) failed");
        goto fail_sem;
    }

    self->running = RESMGR_RUNNING;
    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    // Map the calling process into the resource data
    resmgr_add_proc(self, getpid());

    return 0;

fail_sem:
fail_shm:
    // Detach shared memory, if needed
    if (shm != NULL)
    {
        shmdt(shm);
        if (errno)
        {
            perror("start client resource manager: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

static int resmgr_stop_client(resmgr_s* self)
{
    if (!self->running)
        return 1;

    // Remove mapping for current process in resource data
    int proc_idx = resmgr_look_up_proc(self, getpid());
    if (proc_idx != -1)
    {
        resmgr_remove_proc(self, proc_idx);
    }

    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("stop client resource manager: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->running = RESMGR_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
    return 1;
}

static int resmgr_start_server(resmgr_s* self)
{
    if (self->running)
        return 1;

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
        perror("start server resource manger: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__resmgr_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start server resource manger: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start server resource manger: unable to attach shm: shmat(2) failed");
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
        perror("start server resource manger: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start server resource manger: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("start server resource manger: unable to set sem value: semctl(2) failed");
        goto fail_sem;
    }

    self->running = RESMGR_RUNNING;
    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    //
    // Resource Initialization
    //

    // Calculate minimum and maximum bounds on reusable resources
    // This computes 20% +/- 5% to be so
    int res_min_reusable = (int) (0.15 * NUM_RESOURCE_CLASSES);
    int res_max_reusable = (int) (0.25 * NUM_RESOURCE_CLASSES);

    // Compute number of reusable resources
    int res_num_reusable = res_min_reusable + rand() % (res_max_reusable - res_min_reusable + 1);

    // Configure each resource class
    for (int ri = 0; ri < NUM_RESOURCE_CLASSES; ++ri)
    {
        // Get resource descriptor
        __rd_s* rd = &self->__mem->resources[ri];

        // Mark reusable if there are more reusable resources to assign
        // The reusable flag just needs to be nonzero to be true, so this shortcut checks out
        rd->reusable = res_max_reusable > 0 ? res_num_reusable-- : 0;

        // Generate an initial number of instances between 1 and 10, inclusive
        rd->remaining = 1 + (unsigned int) (rand() % MAX_INSTANCES);

        // Reset allocations list
        for (int ai = 0; ai < MAX_USER_PROCS * MAX_INSTANCES; ++ai)
        {
            rd->allocations[ai] = -1;
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
            perror("start server resource manger: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != NULL)
    {
        shmdt(shm);
        if (errno)
        {
            perror("start server resource manger: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("start server resource manger: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

static int resmgr_stop_server(resmgr_s* self)
{
    if (!self->running)
        return 1;

    errno = 0;

    //
    // Shared Memory
    //

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("stop server resource manager: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    // Remove shared memory segment
    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("stop server resource manager: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("stop server resource manager: unable to remove sem: semctl(2) failed");
        goto fail_sem;
    }

    self->running = RESMGR_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_sem:
fail_shm:
    return 1;
}

resmgr_s* resmgr_construct(resmgr_s* self, int side)
{
    if (self == NULL)
        return NULL;

    self->side = side;

    switch (side)
    {
    case RESMGR_SIDE_CLIENT:
        resmgr_start_client(self);
        break;
    case RESMGR_SIDE_SERVER:
        resmgr_start_server(self);
        break;
    default:
        return NULL;
    }

    return self;
}

resmgr_s* resmgr_destruct(resmgr_s* self)
{
    if (self == NULL)
        return NULL;

    switch (self->side)
    {
    case RESMGR_SIDE_CLIENT:
        resmgr_stop_client(self);
        break;
    case RESMGR_SIDE_SERVER:
        resmgr_stop_server(self);
        break;
    default:
        return NULL;
    }

    return self;
}

int resmgr_lock(resmgr_s* resmgr)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(resmgr->semid, &buf, 1);
    if (errno)
    {
        perror("resource manager lock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int resmgr_unlock(resmgr_s* resmgr)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(resmgr->semid, &buf, 1);
    if (errno)
    {
        perror("resource manager unlock: unable to increment sem: semop(2) failed");
        return 1;
    }

    return 0;
}

void resmgr_update(resmgr_s* self)
{
    if (self->side != RESMGR_SIDE_SERVER)
        return;

    // Scan over each resource class
    for (int res = 0; res < NUM_RESOURCE_CLASSES; ++res)
    {
        // Get resource descriptor
        __rd_s* rd = &self->__mem->resources[res];

        // While more instances remain and wait queue is nonempty
        while (rd->remaining > 0 && rd->wait_queue.length > 0)
        {
            // Dequeue process from resource wait queue
            pid_t proc = resmgr_wait_dequeue(self, res);

            // Allocate resource
            resmgr_allocate_resource(self, proc, res);
        }
    }
}

int request_less_than_available(int* request_matrix, int* avail_vector, int proc)
{
    int i = 0;
    for (; i < NUM_RESOURCE_CLASSES; i++)
    {
        // If resource is over-requested
        if (request_matrix[proc * NUM_RESOURCE_CLASSES + i] > avail_vector[i])
            break;
    }

    // True if no resource is over-requested
    return i == NUM_RESOURCE_CLASSES;
}

int deadlock(int* avail_vector, int* request_matrix, int* alloc_matrix)
{
    //
    // This algo runs a mini simulation to reduce the resource dependency graph. I think.
    //

    // Possible scratch space?
    int work[NUM_RESOURCE_CLASSES];

    // Processes that finished in simulation
    int finish[MAX_USER_PROCS];

    // Copy available into work
    for (int i = 0; i < NUM_RESOURCE_CLASSES; ++i)
    {
        work[i] = avail_vector[i];
    }

    // Clear finish
    for (int i = 0; i < MAX_USER_PROCS; ++i)
    {
        finish[i] = 0;
    }

    // Iterate over processes (by index, not pid)
    for (int proc_idx = 0; proc_idx < MAX_USER_PROCS; ++proc_idx)
    {
        // If process finished, go to next process
        if (finish[proc_idx])
            continue;

        // If process's requests are satisfiable
        if (request_less_than_available(request_matrix, work, proc_idx))
        {
            // Mark the process as finishing
            finish[proc_idx] = 1;

            // Add to work vector the process's row from the allocation matrix
            for (int res = 0; res < NUM_RESOURCE_CLASSES; ++res)
            {
                work[res] += alloc_matrix[proc_idx * NUM_RESOURCE_CLASSES + res];
            }

            // Perform another scan over the processes
            // This gets incremented to zero for the next loop iteration
            proc_idx = -1;
        }
    }

    // Find first process that can't be finished
    int p;
    for (p = 0; p < MAX_USER_PROCS; p++)
    {
        if (!finish[p])
        {
            printf("OH NO: %d IS NOT GOING TO FINISH\n", p);
            break;
        }
    }

    // True if any process failed to finish
    return p != MAX_USER_PROCS;
}

void resmgr_resolve_deadlocks(resmgr_s* self)
{
    if (self->side != RESMGR_SIDE_SERVER)
        return;

    //
    // Collect Data
    //

    // Required data: allocation matrix, request matrix, and availability vector
    int alloc_matrix[MAX_USER_PROCS * NUM_RESOURCE_CLASSES];
    int request_matrix[MAX_USER_PROCS * NUM_RESOURCE_CLASSES];
    int avail_vector[NUM_RESOURCE_CLASSES];

    // Populate allocation and request matrices
    for (int res = 0; res < NUM_RESOURCE_CLASSES; ++res)
    {
        // Get resource descriptor
        __rd_s* rd = &self->__mem->resources[res];

        for (int proc_idx = 0; proc_idx < MAX_USER_PROCS; ++proc_idx)
        {
            pid_t proc = resmgr_get_proc(self, proc_idx);

            // Count corresponding allocations
            int num_instances_allocated = 0;
            for (int i = 0; i < rd->num_allocations; ++i)
            {
                // If calling process is found
                if (rd->allocations[i] == proc)
                {
                    // Increment count
                    num_instances_allocated++;
                }
            }

            // Populate allocation matrix
            alloc_matrix[proc_idx * NUM_RESOURCE_CLASSES + res] = num_instances_allocated;

            // Count corresponding waiting requests
            int num_instances_requested = 0;
            for (int i = 0; i < rd->wait_queue.length; ++i)
            {
                // If calling process is found
                if (rd->wait_queue.pids[i] == proc)
                {
                    // Increment count
                    num_instances_requested++;
                }
            }

            // Populate request matrix
            request_matrix[proc_idx * NUM_RESOURCE_CLASSES + res] = num_instances_requested;
        }
    }

    // Populate availability vector
    for (int res = 0; res < NUM_RESOURCE_CLASSES; ++res)
    {
        avail_vector[res] = self->__mem->resources[res].remaining;
    }

    //
    // Detect Deadlocked Processes
    //

    if (deadlock(avail_vector, request_matrix, alloc_matrix))
    {
        printf("DEADLOCK ALERT OH NO TAKE COVER\n");
    }

    //
    // Resolve Deadlocks
    //

    // TODO: Resolve all deadlocks
}

int resmgr_request(resmgr_s* self, int res)
{
    if (res >= NUM_RESOURCE_CLASSES)
        return 1;

    pid_t proc = getpid();
    __rd_s* rd = &self->__mem->resources[res];

    // If we can allocate immediately, do so
    // Otherwise, put process in wait queue
    if (rd->remaining > 0)
    {
        return resmgr_allocate_resource(self, proc, res);
    }
    else
    {
        // Add process to wait queue for desired resource
        // The server-side resource manager will come through soon enough to resolve waits
        resmgr_wait_enqueue(self, proc, res);
    }

    return 0;
}

int resmgr_release(resmgr_s* self, int res)
{
    if (res >= NUM_RESOURCE_CLASSES)
        return 1;

    pid_t proc = getpid();
    __rd_s* rd = &self->__mem->resources[res];

    // Scan through all allocations
    for (int i = 0; i < rd->num_allocations; ++i)
    {
        // If calling process is listed, splice out the first occurrence
        if (rd->allocations[i] == proc)
        {
            // If not the end of the list
            if (i < rd->num_allocations - 1)
            {
                // Move the subsequent allocations down one
                for (int j = i; j < rd->num_allocations; ++j)
                {
                    rd->allocations[i] = rd->allocations[j + 1];
                }
            }

            // Decrement acquisition count
            rd->num_allocations--;

            // Increment remaining count if resource is reusable
            if (rd->reusable)
            {
                rd->remaining++;
            }

            break;
        }
    }

    printf("resmgr: %d instances of resource %d now remain\n", rd->remaining, res);

    return 0;
}

int resmgr_count(resmgr_s* self, int res)
{
    if (res >= NUM_RESOURCE_CLASSES)
        return 0;

    pid_t proc = getpid();
    __rd_s* rd = &self->__mem->resources[res];

    int count = 0;

    // Scan through all allocations
    for (int i = 0; i < rd->num_allocations; ++i)
    {
        // If calling process is found
        if (rd->allocations[i] == proc)
        {
            // Increment count
            count++;
        }
    }

    return count;
}
