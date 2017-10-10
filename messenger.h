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

typedef struct __messenger_mem_s __messenger_mem_s;

typedef struct
{
    /** The local side of the messenger channel. */
    int side;

    /** The ID of the shared memory segment used. */
    int shmid;

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

#endif // #ifndef MESSENGER_H
