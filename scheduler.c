/*
 * Tyler Filla
 * CS 4760
 * Assignment 4
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>

#include "scheduler.h"

#define SHM_FTOK_CHAR 'M'
#define SEM_FTOK_CHAR 'N'

struct __scheduler_mem_s
{
    /** Whether a message is waiting. */
    int waiting;

    /** The message data. */
    scheduler_msg_s msg;
};

/**
 * Open a master side scheduler.
 */
static int scheduler_open_master(scheduler_s* self)
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
        perror("open master scheduler: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__scheduler_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("open master scheduler: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("open master scheduler: unable to attach shm: shmat(2) failed");
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
        perror("open master scheduler: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("open master scheduler: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("open master scheduler: unable to set sem value: semctl(2) failed");
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
            perror("open master scheduler: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != (void*) -1)
    {
        shmdt(shm);
        if (errno)
        {
            perror("open master scheduler: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("open master scheduler: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

/**
 * Open a slave side scheduler.
 */
static int scheduler_open_slave(scheduler_s* self)
{
    errno = 0;

    //
    // Shared Memory
    //

    void* shm = (void*) -1;

    // Obtain IPC key for shared memory
    key_t shm_key = ftok(".", SHM_FTOK_CHAR);
    if (errno)
    {
        perror("open slave scheduler: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Get ID of the shared memory segment
    int shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("open slave scheduler: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("open slave scheduler: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Obtain IPC for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("open slave scheduler: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    int semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("open slave scheduler: unable to get sem: semget(2) failed");
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
            perror("open slave scheduler: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

/**
 * Close a master side scheduler.
 */
static int scheduler_close_master(scheduler_s* self)
{
    errno = 0;

    //
    // Shared Memory
    //

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("close master scheduler: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("close master scheduler: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("close master scheduler: unable to remove sem: semctl(2) failed");
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
 * Close a slave side scheduler.
 */
static int scheduler_close_slave(scheduler_s* self)
{
    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("close slave scheduler: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
    return 1;
}

scheduler_s* scheduler_construct(scheduler_s* self, int side)
{
    if (self == NULL)
        return NULL;

    self->side = side;
    self->shmid = -1;
    self->semid = -1;

    // Open the scheduler
    switch (side)
    {
    case SCHEDULER_SIDE_MASTER:
        scheduler_open_master(self);
        break;
    case SCHEDULER_SIDE_SLAVE:
        scheduler_open_slave(self);
        break;
    default:
        break;
    }

    return self;
}

scheduler_s* scheduler_destruct(scheduler_s* self)
{
    if (self == NULL)
        return NULL;

    // Close the scheduler
    switch (self->side)
    {
    case SCHEDULER_SIDE_MASTER:
        scheduler_close_master(self);
        break;
    case SCHEDULER_SIDE_SLAVE:
        scheduler_close_slave(self);
        break;
    default:
        break;
    }

    return self;
}

int scheduler_lock(scheduler_s* self)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("scheduler lock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int scheduler_unlock(scheduler_s* self)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("scheduler unlock: unable to increment sem: semop(2) failed");
        return 1;
    }

    return 0;
}

/*

int scheduler_test(scheduler_s* self)
{
    return self->__mem->waiting;
}

scheduler_msg_s scheduler_poll(scheduler_s* self)
{
    scheduler_msg_s msg = self->__mem->msg;
    self->__mem->waiting = 0;
    return msg;
}

void scheduler_offer(scheduler_s* self, scheduler_msg_s msg)
{
    self->__mem->msg = msg;
    self->__mem->waiting = 1;
}

*/
