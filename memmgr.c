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
 * Page frame allocated bit. Indicates that a page is allocated to a process.
 */
#define PAGE_FRAME_BIT_ALLOCATED (1ul << 0)

/**
 * Page frame dirty bit. Indicates that a page was modified since last page-in.
 */
#define PAGE_FRAME_BIT_DIRTY (1ul << 1)

/**
 * Page frame reference bit. Indicates that a page was recently referenced (read/written).
 */
#define PAGE_FRAME_BIT_REFERENCE (1ul << 2)

/**
 * A page number.
 */
typedef long page_t;

/**
 * A page frame. Pages themselves are simulated as blocks of heap memory.
 */
typedef struct
{
    /** A bitfield of flags governing operation. */
    unsigned int flags;

    /** The time at which the page was paged in. */
    unsigned long time_page_in;

    /** The process to which this page is allocated or -1 if not allocated. */
    int process;
} __page_frame;

/**
 * A process page table.
 */
typedef struct
{
    /** The page frames. This maps VM page numbers to system page numbers. */
    page_t map[USER_PROCESS_VM_SIZE / PAGE_SIZE];

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
    /** In lieu of a page table base register, this maps between real system pids and page table indices. */
    pid_t page_table_map[MAX_USER_PROCS];

    /** The number of processes mapped to page tables. */
    int num_procs_mapped;

    /** The total number of free frames. */
    unsigned long num_free_frames;

    /** The system memory page frames. */
    __page_frame frames[SYSTEM_MEMORY_SIZE / PAGE_SIZE];

    /** The process page tables. */
    __page_table page_tables[MAX_USER_PROCS];
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
    // Look up page table index
    int idx = memmgr_look_up_proc(self, proc);

    if (idx == -1)
        return 1;

    // Unmap the process
    self->__mem->page_table_map[idx] = -1;
    self->__mem->num_procs_mapped--;

    return 0;
}

static __page_table* memmgr_get_page_table(memmgr_s* self, pid_t proc)
{
    // Look up page table index
    int idx = memmgr_look_up_proc(self, proc);

    if (idx == -1)
        return NULL;

    // Get actual page table
    return &self->__mem->page_tables[idx];
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

    memset(self->__mem->frames, 0, sizeof(self->__mem->frames));
    memset(self->__mem->page_tables, 0, sizeof(self->__mem->page_tables));

    for (int i = 0; i < MAX_USER_PROCS; ++i)
    {
        self->__mem->page_table_map[i] = -1;

        for (page_t j = 0; j < USER_PROCESS_VM_SIZE / PAGE_SIZE; ++j)
        {
            self->__mem->page_tables[i].map[j] = -1;
            self->__mem->page_tables[i].waiting = 0;
        }
    }

    self->__mem->num_procs_mapped = 0;
    self->__mem->num_free_frames = SYSTEM_MEMORY_SIZE / PAGE_SIZE;

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

    __page_table* page_table = memmgr_get_page_table(self, getpid());

    if (page_table != NULL)
    {
        // Release all the process's pages
        for (page_t page = 0; page < USER_PROCESS_VM_SIZE / PAGE_SIZE; ++page)
        {
            page_t page_sys = page_table->map[page];

            // Skip unmapped pages
            if (page_sys == -1)
                continue;

            self->__mem->frames[page_sys].process = -1;
            self->__mem->num_free_frames++;
        }
    }

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

fail_unmap:
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

    if (page_table == NULL)
        return 0;

    // Get page number from VM address
    page_t page_num = TRANSLATE_PAGE(ptr);

    // If page frame is not allocated, we have a page fault
    if (page_table->map[page_num] == -1)
    {
        // Put process in I/O queue for page
        // This is simulated by storing the process's wait parameters in its page table
        // Schedule the page to become available in 15 milliseconds
        page_table->waiting = 1;
        page_table->wait_page = page_num;
        page_table->wait_done = read_time + 15000000ul;

        // Report page fault so process can suspend
        // The process will need to check back at a later time for the page
        return 2;
    }

    // Get page frame for VM pointer
    __page_frame* page_frame = &self->__mem->frames[page_table->map[page_num]];

    // Set reference bit
    page_frame->flags |= PAGE_FRAME_BIT_REFERENCE;

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

    if (page_table == NULL)
        return 0;

    // Get page number from VM address
    page_t page_num = TRANSLATE_PAGE(ptr);

    // If page frame is not allocated, we have a page fault
    if (page_table->map[page_num] == -1)
    {
        // Put process in I/O queue for page
        // This is simulated by storing the process's wait parameters in its page table
        // Schedule the page to become available in 15 milliseconds
        page_table->waiting = 1;
        page_table->wait_page = page_num;
        page_table->wait_done = write_time + 15000000ul;

        // Report page fault so process can suspend
        // The process will need to check back at a later time for the page
        return 2;
    }

    // Get page frame for VM pointer
    __page_frame* page_frame = &self->__mem->frames[page_table->map[page_num]];

    // Set reference and dirty bits
    page_frame->flags |= PAGE_FRAME_BIT_REFERENCE;
    page_frame->flags |= PAGE_FRAME_BIT_DIRTY;

    return 0;
}

int memmgr_is_waiting(memmgr_s* self)
{
    // Get user process pid
    pid_t proc = getpid();

    // Get page table for user process
    __page_table* page_table = memmgr_get_page_table(self, proc);

    if (page_table == NULL)
        return 0;

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
                // To simulate page-in, we just pick an unallocated frame from system memory
                // There is no actual data that needs to be read from disk
                // If no frames are available, we do the second-chance page replacement algorithm

                // The number of pages in the system
                // This should probably get #define'd out
                unsigned long num_system_pages = SYSTEM_MEMORY_SIZE / PAGE_SIZE;

                // Find first unallocated system frame
                page_t page_num;
                for (page_num = 0; page_num < num_system_pages; ++page_num)
                {
                    if ((self->__mem->frames[page_num].flags & PAGE_FRAME_BIT_ALLOCATED) == 0)
                        break;
                }

                // If no frames are unallocated
                if (page_num == num_system_pages)
                {
                    // Find oldest allocated page, preferably with reference bit unset
                    page_t oldest_unref = -1;
                    unsigned long oldest_unref_time_page_in = 0xfffffffffffffffful;
                    page_t oldest_absolute = -1;
                    unsigned long oldest_absolute_time_page_in = 0xfffffffffffffffful;
                    for (page_t num = 0; num < num_system_pages; ++num)
                    {
                        __page_frame* page_frame = &self->__mem->frames[num];

                        // Find oldest unreferenced frame
                        if (page_frame->time_page_in < oldest_unref_time_page_in
                                && (page_frame->flags & PAGE_FRAME_BIT_REFERENCE) == 0)
                        {
                            oldest_unref = num;
                            oldest_unref_time_page_in = page_frame->time_page_in;
                        }

                        // Find absolute oldest frame
                        if (page_frame->time_page_in < oldest_absolute_time_page_in)
                        {
                            oldest_absolute = num;
                            oldest_absolute_time_page_in = page_frame->time_page_in;
                        }
                    }

                    // If an oldest unreferenced frame exists, use it
                    // Otherwise, use the absolute oldest frame
                    if (oldest_unref != -1)
                    {
                        page_num = oldest_unref;
                    }
                    else
                    {
                        page_num = oldest_absolute;
                    }

                    // Get victim page frame
                    __page_frame* page_frame = &self->__mem->frames[page_num];

                    // If victim page is modified, simulate a disk write
                    if ((page_frame->flags & PAGE_FRAME_BIT_DIRTY) != 0)
                    {
                        if (clock_lock(self->clock))
                            return 1;

                        // Advance by 15ms to simulate writing page to disk
                        clock_advance(self->clock, 0, 15000000);

                        if (clock_unlock(self->clock))
                            return 1;
                    }

                    // Steal victim page from its process
                    if (page_frame->process != -1)
                    {
                        self->__mem->page_table_map[page_frame->process] = -1;
                    }
                }
                else
                {
                    // A frame was unallocated
                    // We will claim it, so decrement free frame count
                    self->__mem->num_free_frames--;
                }

                // Allocate selected page frame to process
                page_table->map[page_table->wait_page] = page_num;
                __page_frame* page_frame = &self->__mem->frames[page_num];
                page_frame->flags = PAGE_FRAME_BIT_ALLOCATED;
                page_frame->time_page_in = update_time;
                page_frame->process = idx;

                // End the process's wait
                page_table->waiting = 0;
            }
        }
    }

    // If less than 10% pages remain, do sweep
    if (self->__mem->num_free_frames < (SYSTEM_MEMORY_SIZE / PAGE_SIZE) / 10)
    {
        for (page_t page = 0; page < SYSTEM_MEMORY_SIZE / PAGE_SIZE; ++page)
        {
            __page_frame* page_frame = &self->__mem->frames[page];

            // If reference bit is not set, page it out
            if ((page_frame->flags & PAGE_FRAME_BIT_REFERENCE) == 0)
            {
                // If page is modified, simulate a disk write
                if ((page_frame->flags & PAGE_FRAME_BIT_DIRTY) != 0)
                {
                    if (clock_lock(self->clock))
                        return 1;

                    // Advance by 15ms to simulate writing page to disk
                    clock_advance(self->clock, 0, 15000000);

                    if (clock_unlock(self->clock))
                        return 1;
                }

                // Remove page from its process
                self->__mem->page_table_map[page_frame->process] = -1;
            }

            // Clear reference bit
            page_frame->flags &= ~PAGE_FRAME_BIT_REFERENCE;
        }
    }

    return 0;
}

void memmgr_dump_frames(memmgr_s* self, FILE* dest)
{
    fprintf(dest, "===== BEGIN PAGE FRAME DUMP =====\n");
    for (page_t page = 0; page < SYSTEM_MEMORY_SIZE / PAGE_SIZE; ++page)
    {
        int proc_idx = self->__mem->frames[page].process;

        // Skip never allocated frames
        if (proc_idx == -1)
        {
            fprintf(dest, ". ");
            continue;
        }

        pid_t proc_pid = self->__mem->page_table_map[proc_idx];

        // Skip unallocated frames
        if (proc_pid == -1)
        {
            fprintf(dest, ". ");
            continue;
        }

        fprintf(dest, "%d ", proc_pid);
    }
    fprintf(dest, "\n===== END PAGE FRAME DUMP =====\n");
}
