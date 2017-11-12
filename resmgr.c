/*
 * Tyler Filla
 * CS 4760
 * Assignment 5
 */

#include <errno.h>
#include <stdio.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include "resmgr.h"

#define SHM_FTOK_CHAR 'R'
#define SEM_FTOK_CHAR 'S'

struct __resmgr_mem_s
{
    int not_empty;
};

static int resmgr_start_client(resmgr_s* self)
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
        perror("start client resource manager: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Obtain existing shared memory segment
    int shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("start client resource manager: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment as read-only
    shm = shmat(shmid, NULL, SHM_RDONLY);
    if (errno)
    {
        perror("start client resource manager: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Obtain IPC key for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("start client resource manager: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    int semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("start client resource manager: unable to get sem: semget(3) failed");
        goto fail_sem;
    }

    self->running = RESMGR_RUNNING;
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
            perror("start client resource manager: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

static int resmgr_stop_client(resmgr_s* self)
{
    if (!self->running)
        return 1;

    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("stop client resource manager: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->running = RESMGR_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
    return 1;
}

static int resmgr_start_server(resmgr_s* self)
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
        perror("start server resource manger: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__resmgr_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start server resource manger: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start server resource manger: unable to attach shm: shmat(2) failed");
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
        perror("start server resource manger: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start server resource manger: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("start server resource manger: unable to set sem value: semctl(2) failed");
        goto fail_sem;
    }

    self->running = RESMGR_RUNNING;
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
            perror("start server resource manger: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != NULL)
    {
        shmdt(shm);
        if (errno)
        {
            perror("start server resource manger: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("start server resource manger: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

static int resmgr_stop_server(resmgr_s* self)
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
        perror("stop server resource manager: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    // Remove shared memory segment
    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("stop server resource manager: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("stop server resource manager: unable to remove sem: semctl(2) failed");
        goto fail_sem;
    }

    self->running = RESMGR_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_sem:
fail_shm:
    return 1;
}

resmgr_s* resmgr_construct(resmgr_s* self, int side)
{
    if (self == NULL)
        return NULL;

    self->side = side;

    switch (side)
    {
    case RESMGR_SIDE_CLIENT:
        resmgr_start_client(self);
        break;
    case RESMGR_SIDE_SERVER:
        resmgr_start_server(self);
        break;
    default:
        return NULL;
    }

    return self;
}

resmgr_s* resmgr_destruct(resmgr_s* self)
{
    if (self == NULL)
        return NULL;

    switch (self->side)
    {
    case RESMGR_SIDE_CLIENT:
        resmgr_stop_client(self);
        break;
    case RESMGR_SIDE_SERVER:
        resmgr_stop_server(self);
        break;
    default:
        return NULL;
    }

    return self;
}

int resmgr_lock(resmgr_s* resmgr)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(resmgr->semid, &buf, 1);
    if (errno)
    {
        perror("resource manager lock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int resmgr_unlock(resmgr_s* resmgr)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(resmgr->semid, &buf, 1);
    if (errno)
    {
        perror("resource manager unlock: unable to increment sem: semop(2) failed");
        return 1;
    }

    return 0;
}

void resmgr_resolve_deadlocks(resmgr_s* self)
{
}
