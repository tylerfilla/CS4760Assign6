/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "messenger.h"

#define SHM_FTOK_CHAR 'M'
#define SEM_FTOK_CHAR 'N'

struct __messenger_mem_s
{
    /** Whether a message is waiting. */
    int waiting;

    /** The message data. */
    messenger_msg_s msg;
};

/**
 * Open a master side messenger.
 */
static int messenger_open_master(messenger_s* self)
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
        perror("open master messenger: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__messenger_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("open master messenger: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("open master messenger: unable to attach shm: shmat(2) failed");
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
        perror("open master messenger: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("open master messenger: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("open master messenger: unable to set sem value: semctl(2) failed");
        goto fail_sem;
    }

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
            perror("open master messenger: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != (void*) -1)
    {
        shmdt(shm);
        if (errno)
        {
            perror("open master messenger: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("open master messenger: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

/**
 * Open a slave side messenger.
 */
static int messenger_open_slave(messenger_s* self)
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
        perror("open slave messenger: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Get ID of the shared memory segment
    shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("open slave messenger: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("open slave messenger: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    int semid = -1;

    // Obtain IPC for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("open slave messenger: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("open slave messenger: unable to get sem: semget(2) failed");
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
            perror("open slave messenger: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

/**
 * Close a master side messenger.
 */
static int messenger_close_master(messenger_s* self)
{
    errno = 0;

    //
    // Shared Memory
    //

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("close master messenger: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("close master messenger: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("close master messenger: unable to remove sem: semctl(2) failed");
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
 * Close a slave side messenger.
 */
static int messenger_close_slave(messenger_s* self)
{
    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("close slave messenger: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
    return 1;
}

messenger_s* messenger_construct(messenger_s* self, int side)
{
    self->side = side;
    self->shmid = -1;
    self->semid = -1;

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

int messenger_lock(messenger_s* self)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("messenger lock: unable to increment sem: semop(2) failed");
        return 1;
    }

    self->locked = 1;

    return 0;
}

int messenger_unlock(messenger_s* self)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(self->semid, &buf, 0);
    if (errno)
    {
        perror("messenger unlock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    self->locked = 0;

    return 0;
}

int messenger_test(messenger_s* self)
{
    return self->__mem->waiting;
}

messenger_msg_s messenger_poll(messenger_s* self)
{
    messenger_msg_s msg = self->__mem->msg;
    self->__mem->waiting = 0;
    return msg;
}

void messenger_offer(messenger_s* self, messenger_msg_s msg)
{
    self->__mem->msg = msg;
    self->__mem->waiting = 1;
}
