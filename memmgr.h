/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
 */

#ifndef MEMMGR_H
#define MEMMGR_H

/**
 * Mode indicating a memory manager is operating as a user agent.
 */
#define MEMMGR_MODE_UA 0

/**
 * Mode indicating a memory manager is operating in kernel mode.
 */
#define MEMMGR_MODE_KERNEL 1

/**
 * State indicating a memory manager is not running.
 */
#define MEMMGR_NOT_RUNNING 0

/**
 * State indicating a memory manager is running.
 */
#define MEMMGR_RUNNING 1

typedef struct __memmgr_mem_s __memmgr_mem_s;

typedef struct
{
    /** Memory manager mode. */
    int mode;

    /** Whether the memory manager is currently running. */
    int running;

    /** The ID of the shared memory segment used. */
    int shmid;

    /** The ID of the semaphore set protecting the internal memory. */
    int semid;

    /** Internal shared memory structure. */
    __memmgr_mem_s* __mem;
} memmgr_s;

/**
 * A simulated virtual memory pointer.
 */
typedef unsigned long ptr_vm_t;

/**
 * Create a memory manager instance.
 */
#define memmgr_new(mode) memmgr_construct(malloc(sizeof(memmgr_s)), (mode))

/**
 * Destroy a memory manager instance.
 */
#define memmgr_delete(memmgr) free(memmgr_destruct(memmgr))

/**
 * Construct a memory manager instance.
 *
 * @param memmgr The memory manager instance
 * @param mode The memory manager mode
 * @return The memory manager instance, constructed
 */
memmgr_s* memmgr_construct(memmgr_s* memmgr, int mode);

/**
 * Destruct a memory manager instance.
 *
 * @param memmgr The memory manager instance
 * @return The memory manager instance, destructed
 */
memmgr_s* memmgr_destruct(memmgr_s* memmgr);

/**
 * Lock a memory manager for exclusive access. This blocks if already locked.
 *
 * @param memmgr The memory manager instance
 * @return Zero on success, otherwise nonzero
 */
int memmgr_lock(memmgr_s* memmgr);

/**
 * Unlock a locked memory manager.
 *
 * @param memmgr The memory manager instance
 * @return Zero on success, otherwise nonzero
 */
int memmgr_unlock(memmgr_s* memmgr);

/**
 * Get the highest pointer of the VM address space of the calling user process.
 *
 * User mode.
 *
 * @param memmgr The memory manager instance
 * @return The topmost pointer in the VM address space
 */
ptr_vm_t memmgr_get_vm_high_ptr(memmgr_s* memmgr);

/**
 * Simulate a read at the given virtual memory address.
 *
 * User mode.
 *
 * @param memmgr The memory manager instance
 * @param ptr The virtual memory pointer
 * @return Zero on success, two on page fault, or otherwise nonzero
 */
int memmgr_read_ptr(memmgr_s* memmgr, ptr_vm_t ptr);

/**
 * Simulate a write at the given virtual memory address. The written value is not simulated.
 *
 * User mode.
 *
 * @param memmgr The memory manager instance
 * @param ptr The virtual memory pointer
 * @return Zero on success, two on page fault, or otherwise nonzero
 */
int memmgr_write_ptr(memmgr_s* memmgr, ptr_vm_t ptr);

/**
 * Check if the given pointer is resident in the calling process's virtual memory.
 *
 * User mode.
 *
 * @param memmgr The memory manager instance
 * @param ptr The virtual memory pointer
 * @return Nonzero if such is the case, otherwise zero
 */
int memmgr_is_resident(memmgr_s* memmgr, ptr_vm_t ptr);

#endif // #ifndef MEMMGR_H
