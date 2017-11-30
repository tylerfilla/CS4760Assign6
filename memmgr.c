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

#include "memmgr.h"

#define SEM_FTOK_CHAR 'M'
#define SHM_FTOK_CHAR 'N'

/**
 * The size, in bytes, of each memory page.
 * Assigned: 1 KiB
 */
#define PAGE_SIZE 1024

/**
 * The size, in bytes, of the simulated system memory.
 * Assigned: 256 KiB
 */
#define SYSTEM_MEMORY_SIZE (256 * 1024)

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
 * Page request result bit mask. Indicates the request succeeded and the requested page allocated.
 */
#define PAGE_REQUEST_SUCCESS 1

/**
 * Page request result bit mask. Indicates a page fault occurred.
 */
#define PAGE_REQUEST_FAULT 2

/**
 * A page frame. Pages themselves are simulated as blocks of heap memory.
 */
typedef struct __page_frame
{
    /** A bitfield of flags governing operation. */
    unsigned int bits;

    /** The time at which the page was paged in. */
    unsigned long time_page_in;
};

/**
 * A page table.
 */
typedef struct __page_table
{
};

/**
 * Request a specific page to be allocated.
 *
 * @param page The page ??? (FIXME)
 * @return A bitfield of result information
 */
static unsigned int __page_request(int page)
{
}

/**
 * Internal memory for memory manager. Shared.
 */
struct __memmgr_mem_s
{
    /** The number of free page frames. */
    int free_frames;
};

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

    return 0;

fail_sem:
fail_shm:
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
        if (errno == EINTR || errno == EINVAL || errno == EIDRM)
            return 1;

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
