/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#ifndef MESSENGER_H
#define MESSENGER_H

/**
 * Indicates a messenger is a master messenger.
 */
#define MESSENGER_SIDE_MASTER 0

/**
 * Indicates a messenger is a slave messenger.
 */
#define MESSENGER_SIDE_SLAVE 1

typedef struct messenger_msg_s
{
    /** An integer argument. */
    int arg1;

    /** An integer argument. */
    int arg2;
} messenger_msg_s;

typedef struct __messenger_mem_s __messenger_mem_s;

typedef struct
{
    /** The local side of the messenger channel. */
    int side;

    /** Whether the messenger is currently locked. */
    int locked;

    /** The ID of the shared memory segment used. */
    int shmid;

    /** The ID of the semaphore set protecting the internal memory. */
    int semid;

    /** Internal shared memory structure. */
    __messenger_mem_s* __mem;
} messenger_s;

/**
 * Create a messenger instance.
 */
#define messenger_new(side) messenger_construct(malloc(sizeof(messenger_s)), side)

/**
 * Destroy a messenger instance.
 */
#define messenger_delete(messenger) free(messenger_destruct(messenger))

/**
 * Construct a messenger instance.
 *
 * @param messenger The messenger instance
 * @param side The local messenger side
 * @return The messenger instance, constructed
 */
messenger_s* messenger_construct(messenger_s* messenger, int side);

/**
 * Destruct a messenger instance.
 *
 * @param messenger The messenger instance
 * @return The messenger instance, destructed
 */
messenger_s* messenger_destruct(messenger_s* messenger);

/**
 * Lock the messenger for exclusive access. This blocks if already locked.
 *
 * @param messenger The messenger instance
 * @return Zero on success, otherwise nonzero
 */
int messenger_lock(messenger_s* messenger);

/**
 * Unlock a locked messenger.
 *
 * @param messenger The messenger instance
 * @return Zero on success, otherwise nonzero
 */
int messenger_unlock(messenger_s* messenger);

/**
 * Determine if a messenger has a message waiting.
 *
 * @param messenger The messenger instance
 * @return Nonzero if such is the case, otherwise zero
 */
int messenger_test(messenger_s* messenger);

/**
 * Remove and return a waiting message from a messenger.
 *
 * @param messenger The messenger instance
 * @return The message
 */
messenger_msg_s messenger_poll(messenger_s* messenger);

/**
 * Set the waiting message on a messenger.
 *
 * @param messenger The messenger instance
 * @param msg The message
 */
void messenger_offer(messenger_s* messenger, messenger_msg_s msg);

#endif // #ifndef MESSENGER_H
