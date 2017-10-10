/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <errno.h>
#include <stdlib.h>

#include <sys/ipc.h>
#include <sys/shm.h>

#include "messenger.h"
#include "perrorf.h"

#define FTOK_PATH "/bin/echo"
#define FTOK_CHAR 'M'


struct __messenger_mem_s
{
    /** The message data. */
    int message;
};

/**
 * Open a master side messenger.
 */
static void messenger_open_master(messenger_s* self)
{
    errno = 0;

    // Obtain the IPC key
    key_t key = ftok(FTOK_PATH, FTOK_CHAR);
    if (errno)
    {
        perrorf("open master messenger: unable to obtain key: ftok(3) failed");
        exit(1);
    }

    // Create a shared memory segment
    int shmid = shmget(key, sizeof(__messenger_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perrorf("open master messenger: unable to create shm: shmget(2) failed");
        exit(2);
    }

    // Attach shared memory segment
    void* shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perrorf("open master messenger: unable to attach shm: shmat(2) failed");

        // Destroy segment
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perrorf("open master messenger: unable to remove shm: shmctl(2) failed");
            exit(4);
        }

        exit(3);
    }

    self->shmid = shmid;
    self->__mem = shm;
}

/**
 * Open a slave side messenger.
 */
static void messenger_open_slave(messenger_s* self)
{
    errno = 0;

    // Obtain the IPC key
    key_t key = ftok(FTOK_PATH, FTOK_CHAR);
    if (errno)
    {
        perrorf("open slave messenger: unable to obtain key: ftok(3) failed");
        exit(1);
    }

    // Get ID of the shared memory segment
    int shmid = shmget(key, 0, 0);
    if (errno)
    {
        perrorf("open slave messenger: unable to get shm: shmget(2) failed");
        exit(2);
    }

    // Attach shared memory segment
    void* shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perrorf("open slave messenger: unable to attach shm: shmat(2) failed");
        exit(3);
    }

    self->shmid = shmid;
    self->__mem = shm;
}

/**
 * Close a master side messenger.
 */
static void messenger_close_master(messenger_s* self)
{
    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perrorf("close master messenger: unable to detach shm: shmdt(2) failed");
        exit(1);
    }

    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perrorf("close master messenger: unable to remove shm: shmctl(2) failed");
        exit(2);
    }

    self->shmid = 0;
    self->__mem = NULL;
}

/**
 * Close a slave side messenger.
 */
static void messenger_close_slave(messenger_s* self)
{
    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perrorf("close slave messenger: unable to detach shm: shmdt(2) failed");
        exit(1);
    }

    self->shmid = 0;
    self->__mem = NULL;
}

messenger_s* messenger_construct(messenger_s* self, int side)
{
    self->side = side;
    self->shmid = -1;

    // Open the messenger
    switch (side)
    {
    case MESSENGER_SIDE_MASTER:
        messenger_open_master(self);
        break;
    case MESSENGER_SIDE_SLAVE:
        messenger_open_slave(self);
        break;
    default:
        break;
    }

    return self;
}

messenger_s* messenger_destruct(messenger_s* self)
{
    // Close the messenger
    switch (self->side)
    {
    case MESSENGER_SIDE_MASTER:
        messenger_close_master(self);
        break;
    case MESSENGER_SIDE_SLAVE:
        messenger_close_slave(self);
        break;
    default:
        break;
    }

    return self;
}
