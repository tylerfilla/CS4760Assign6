/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

#include "clock.h"
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
 * A macro to translate a virtual memory address to a VM page number. If you modify any of the above parameters, this
 * macro will need to be adjusted.
 */
#define TRANSLATE_PAGE(ptr) ((0x7c00ul & (ptr)) >> 10)

/**
 * Page frame dirty bit. Indicates that a page was modified since last page-in.
 */
#define PAGE_FRAME_BIT_DIRTY (1ul << 0)

/**
 * Page frame reference bit. Indicates that a page was recently referenced (read/written).
 */
#define PAGE_FRAME_BIT_REFERENCE (1ul << 1)

/**
 * A page number.
 */
typedef unsigned long page_t;

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
 * A process page table.
 */
typedef struct
{
    /** The page frames. */
    __page_frame* frames[USER_PROCESS_VM_SIZE / PAGE_SIZE];

    /** Nonzero if the process is waiting on a page. */
    int waiting;

    /** The page on which the process is currently waiting. */
    page_t wait_page;

    /** The clock time at which the simulated page wait will be lifted. */
    unsigned long wait_done;
} __page_table;

/**
 * Internal memory for memory manager. Shared.
 */
struct __memmgr_mem_s
{
    /** The system memory page frames. */
    __page_frame system_frames[SYSTEM_MEMORY_SIZE / PAGE_SIZE];

    /** The process page tables. */
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

    // Attach shared memory segment
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

    //
    // Initialization
    //

    self->running = MEMMGR_RUNNING;
    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    memset(self->__mem->system_frames, 0, sizeof(self->__mem->system_frames));
    memset(self->__mem->page_tables, 0, sizeof(self->__mem->page_tables));

    for (int i = 0; i < MAX_USER_PROCS; ++i)
    {
        self->__mem->page_table_map[i] = -1;
    }

    self->__mem->num_procs_mapped = 0;

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

memmgr_s* memmgr_construct(memmgr_s* self, int mode, clock_s* clock)
{
    if (self == NULL)
        return NULL;

    self->mode = mode;
    self->clock = clock;
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

ptr_vm_t memmgr_get_vm_high_ptr(memmgr_s* memmgr)
{
    return USER_PROCESS_VM_SIZE - 1;
}

int memmgr_read_ptr(memmgr_s* self, ptr_vm_t ptr)
{
    if (clock_lock(self->clock))
        return 1;

    // Advance by 10ns to simulate read
    clock_advance(self->clock, 0, 10);

    unsigned long read_time = clock_get_time(self->clock);

    if (clock_unlock(self->clock))
        return 1;

    // Get user process pid
    pid_t proc = getpid();

    // Get page table for user process
    __page_table* page_table = memmgr_get_page_table(self, proc);

    // Get page number from VM address
    page_t page_num = TRANSLATE_PAGE(ptr);

    // Get page frame for VM pointer
    __page_frame* page_frame = page_table->frames[page_num];

    // If page frame is not allocated, we have a page fault
    if (page_frame == NULL)
    {
        // Put process in I/O queue for page
        // This is simulated by storing the process's wait parameters in its page table
        // Schedule the page to become available in 15 milliseconds (FIXME: about 15ms)
        page_table->waiting = 1;
        page_table->wait_page = page_num;
        page_table->wait_done = read_time + 15000000ul;

        // Report page fault so process can suspend
        // The process will need to check back at a later time for the page
        return 2;
    }

    // Set reference bit
    page_frame->bits |= PAGE_FRAME_BIT_REFERENCE;

    return 0;
}

int memmgr_write_ptr(memmgr_s* self, ptr_vm_t ptr)
{
    if (clock_lock(self->clock))
        return 1;

    // Advance by 10ns to simulate write
    clock_advance(self->clock, 0, 10);

    unsigned long write_time = clock_get_time(self->clock);

    if (clock_unlock(self->clock))
        return 1;

    // Get user process pid
    pid_t proc = getpid();

    // Get page table for user process
    __page_table* page_table = memmgr_get_page_table(self, proc);

    // Get page number from VM address
    page_t page_num = TRANSLATE_PAGE(ptr);

    // Get page frame for VM pointer
    __page_frame* page_frame = page_table->frames[page_num];

    // If page frame is not allocated, we have a page fault
    if (page_frame == NULL)
    {
        // Put process in I/O queue for page
        // This is simulated by storing the process's wait parameters in its page table
        // Schedule the page to become available in 15 milliseconds (FIXME: about 15ms?)
        page_table->waiting = 1;
        page_table->wait_page = page_num;
        page_table->wait_done = write_time + 15000000ul;

        // Report page fault so process can suspend
        // The process will need to check back at a later time for the page
        return 2;
    }

    // Set reference bit
    page_frame->bits |= PAGE_FRAME_BIT_REFERENCE;

    // Set dirty bit
    page_frame->bits |= PAGE_FRAME_BIT_DIRTY;

    return 0;
}

int memmgr_is_waiting(memmgr_s* self)
{
    // Get user process pid
    pid_t proc = getpid();

    // Get page table for user process
    __page_table* page_table = memmgr_get_page_table(self, proc);

    return page_table->waiting;
}

int memmgr_update(memmgr_s* self)
{
    if (clock_lock(self->clock))
        return 1;

    unsigned long update_time = clock_get_time(self->clock);

    if (clock_unlock(self->clock))
        return 1;

    // Iterate over valid process indices
    int idx;
    for (idx = 0; idx < MAX_USER_PROCS; ++idx)
    {
        // Get pid for process index
        pid_t proc = self->__mem->page_table_map[idx];

        // If process is mapped here
        if (proc != -1)
        {
            // Get page table for process
            __page_table* page_table = &self->__mem->page_tables[idx];

            // If process is waiting on a page that should now be available
            if (page_table->waiting && update_time >= page_table->wait_done)
            {
                // TODO: Bring the page in

                // End the process's wait
                page_table->waiting = 0;
            }
        }
    }

    return 0;
}
