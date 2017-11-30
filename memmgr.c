/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
 */

#include <errno.h>
#include <stdio.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

#include "config.h"
#include "memmgr.h"

#define SEM_FTOK_CHAR 'M'
#define SHM_FTOK_CHAR 'N'

/**
 * The size, in bytes, of each memory page.
 * Assigned: 1 KiB
 */
#define PAGE_SIZE (1 * 1024)

/**
 * The size, in bytes, of the simulated system memory.
 * Assigned: 256 KiB
 */
#define SYSTEM_MEMORY_SIZE (256 * 1024)

/**
 * The size, in bytes, of each process's virtual address space.
 * Assigned: 32 KiB
 */
#define USER_PROCESS_VM_SIZE (32 * 1024)

/**
 * Page frame allocated bit mask. Indicates that a frame is allocated a page in memory.
 */
#define PAGE_FRAME_BIT_ALLOCATED 1

/**
 * Page frame dirty bit mask. Indicates that a page was modified since page-in.
 */
#define PAGE_FRAME_BIT_DIRTY 2

/**
 * Page from reference bit mask. Indicates that a page was modified recently.
 */
#define PAGE_FRAME_BIT_REFERENCE 4

/**
 * A page frame. Pages themselves are simulated as blocks of heap memory.
 */
typedef struct
{
    /** A bitfield of flags governing operation. */
    unsigned int bits;

    /** The time at which the page was paged in. */
    unsigned long time_page_in;
} __page_frame;

/**
 * A page table.
 */
typedef struct
{
    /** The page frames. */
    __page_frame frames[USER_PROCESS_VM_SIZE / PAGE_SIZE];
} __page_table;

/**
 * Internal memory for memory manager. Shared.
 */
struct __memmgr_mem_s
{
    /** The page tables. */
    __page_table page_tables[MAX_USER_PROCS];

    /** In lieu of a page table base register, this maps between real system pids and page table indices. */
    pid_t page_table_map[MAX_USER_PROCS];

    /** The number of processes mapped to page tables. */
    int num_procs_mapped;
};

static int memmgr_look_up_proc(memmgr_s* self, pid_t proc)
{
    int idx;
    for (idx = 0; idx < MAX_USER_PROCS; ++idx)
    {
        if (self->__mem->page_table_map[idx] == proc)
            return idx;
    }

    return -1;
}

static int memmgr_map_proc(memmgr_s* self, pid_t proc)
{
    if (self->__mem->num_procs_mapped == MAX_USER_PROCS)
        return 1;

    // Find first available page table
    int idx;
    for (idx = 0; idx < MAX_USER_PROCS; ++idx)
    {
        if (self->__mem->page_table_map[idx] == -1)
            break;
    }

    // Map the process
    self->__mem->page_table_map[idx] = proc;
    self->__mem->num_procs_mapped++;

    return 0;
}

static int memmgr_unmap_proc(memmgr_s* self, pid_t proc)
{
    // Look up the page table index for the process
    int idx = memmgr_look_up_proc(self, proc);

    // Unmap the process
    self->__mem->page_table_map[idx] = -1;
    self->__mem->num_procs_mapped--;

    return 0;
}

static __page_table* memmgr_get_page_table(memmgr_s* self, pid_t proc)
{
    // Loop up page table index
    int page_table_idx = memmgr_look_up_proc(self, proc);

    if (page_table_idx == -1)
        return NULL;

    // Get actual page table
    return &self->__mem->page_tables[page_table_idx];
}

/**
 * Start the memory manager user agent.
 */
static int memmgr_start_ua(memmgr_s* self)
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
        perror("start memory manager user agent: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Obtain existing shared memory segment
    int shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("start memory manager user agent: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment as read-only
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start memory manager user agent: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Obtain IPC key for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("start memory manager user agent: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    int semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("start memory manager user agent: unable to get sem: semget(3) failed");
        goto fail_sem;
    }

    self->running = MEMMGR_RUNNING;
    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    // Map the client process
    if (memmgr_map_proc(self, getpid()))
    {
        fprintf(stderr, "error: start memory manager user agent: unable to map client process %d\n", getpid());
        goto fail_map;
    }

    return 0;

fail_sem:
fail_shm:
fail_map:
    // Detach shared memory, if needed
    if (shm != NULL)
    {
        shmdt(shm);
        if (errno)
        {
            perror("start memory manager user agent: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

/**
 * Start the kernel memory manager.
 */
static int memmgr_start_kernel(memmgr_s* self)
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
        perror("start memory manager: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__memmgr_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start memory manager: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start memory manager: unable to attach shm: shmat(2) failed");
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
        perror("start memory manager: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start memory manager: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("start memory manager: unable to set sem value: semctl(2) failed");
        goto fail_sem;
    }

    self->running = MEMMGR_RUNNING;
    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    // Initialize page table map
    for (int i = 0; i < MAX_USER_PROCS; ++i)
    {
        self->__mem->page_table_map[i] = -1;
    }

    return 0;

fail_sem:
    // Remove semaphore set, if needed
    if (semid >= 0)
    {
        semctl(semid, 0, IPC_RMID);
        if (errno)
        {
            perror("start memory manager: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != NULL)
    {
        shmdt(shm);
        if (errno)
        {
            perror("start memory manager: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("start memory manager: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

/**
 * Stop the memory manager user agent.
 */
static int memmgr_stop_ua(memmgr_s* self)
{
    if (!self->running)
        return 1;

    // Unmap the client process
    if (memmgr_unmap_proc(self, getpid()))
    {
        fprintf(stderr, "error: stop memory manager user agent: unable to unmap client process %d\n", getpid());
        goto fail_unmap;
    }

    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("stop memory manager user agent: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->running = MEMMGR_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
fail_unmap:
    return 1;
}

/**
 * Stop the memory manager.
 */
static int memmgr_stop_kernel(memmgr_s* self)
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
        perror("stop memory manager: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    // Remove shared memory segment
    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("stop memory manager: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("stop memory manager: unable to remove sem: semctl(2) failed");
        goto fail_sem;
    }

    self->running = MEMMGR_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_sem:
fail_shm:
    return 1;
}

memmgr_s* memmgr_construct(memmgr_s* self, int mode)
{
    if (self == NULL)
        return NULL;

    self->mode = mode;
    self->running = MEMMGR_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    switch (mode)
    {
    case MEMMGR_MODE_UA:
        memmgr_start_ua(self);
        break;
    case MEMMGR_MODE_KERNEL:
        memmgr_start_kernel(self);
        break;
    default:
        return NULL;
    }

    return self;
}

memmgr_s* memmgr_destruct(memmgr_s* self)
{
    if (self == NULL)
        return NULL;

    switch (self->mode)
    {
    case MEMMGR_MODE_UA:
        memmgr_stop_ua(self);
        break;
    case MEMMGR_MODE_KERNEL:
        memmgr_stop_kernel(self);
        break;
    default:
        return NULL;
    }

    return self;
}

int memmgr_lock(memmgr_s* self)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("memory manager lock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int memmgr_unlock(memmgr_s* self)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("memory manager unlock: unable to increment sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int memmgr_read_ptr(memmgr_s* self, ptr_vm_t ptr)
{
    // Get user process pid
    pid_t proc = getpid();

    // Get page table
    __page_table* page_table = memmgr_get_page_table(self, proc);

    // TODO: Do read

    return 0;
}

int memmgr_write_ptr(memmgr_s* self, ptr_vm_t ptr)
{
    // Get user process pid
    pid_t proc = getpid();

    // Get page table
    __page_table* page_table = memmgr_get_page_table(self, proc);

    // TODO: Do write

    return 0;
}
