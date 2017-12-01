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

#include "clock.h"

#define SEM_FTOK_CHAR 'C'
#define SHM_FTOK_CHAR 'D'

/**
 * Internal memory for clock. Shared.
 */
struct __clock_mem_s
{
    /** Nanosecond counter. */
    unsigned int nanos;

    /** Second counter. */
    unsigned int seconds;
};

/**
 * Start the clock under IN mode. Leaves clock unstarted on failure.
 */
static int clock_start_in(clock_s* self)
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
        perror("start incoming clock: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Obtain existing shared memory segment
    int shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("start incoming clock: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start incoming clock: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Obtain IPC key for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("start incoming clock: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    int semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("start incoming clock: unable to get sem: semget(3) failed");
        goto fail_sem;
    }

    self->running = CLOCK_RUNNING;
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
            perror("start incoming clock: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

/**
 * Start the clock under OUT mode. Leaves clock unstarted on failure.
 */
static int clock_start_out(clock_s* self)
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
        perror("start outgoing clock: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__clock_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start outgoing clock: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start outgoing clock: unable to attach shm: shmat(2) failed");
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
        perror("start outgoing clock: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start outgoing clock: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("start outgoing clock: unable to set sem value: semctl(2) failed");
        goto fail_sem;
    }

    self->running = CLOCK_RUNNING;
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
            perror("start outgoing clock: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != NULL)
    {
        shmdt(shm);
        if (errno)
        {
            perror("start outgoing clock: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("start outgoing clock: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

/**
 * Stop the clock under IN mode. Leaves clock in indeterminate state on failure.
 */
static int clock_stop_in(clock_s* self)
{
    if (!self->running)
        return 1;

    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("stop incoming clock: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->running = CLOCK_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
    return 1;
}

/**
 * Stop the clock under OUT mode. Leaves clock in indeterminate state on failure.
 */
static int clock_stop_out(clock_s* self)
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
        perror("stop outgoing clock: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    // Remove shared memory segment
    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("stop outgoing clock: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("stop outgoing clock: unable to remove sem: semctl(2) failed");
        goto fail_sem;
    }

    self->running = CLOCK_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_sem:
fail_shm:
    return 1;
}

clock_s* clock_construct(clock_s* self, int mode)
{
    if (self == NULL)
        return NULL;

    self->mode = mode;
    self->running = CLOCK_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    switch (mode)
    {
    case CLOCK_MODE_IN:
        clock_start_in(self);
        break;
    case CLOCK_MODE_OUT:
        clock_start_out(self);
        break;
    default:
        return NULL;
    }

    return self;
}

clock_s* clock_destruct(clock_s* self)
{
    if (self == NULL)
        return NULL;

    switch (self->mode)
    {
    case CLOCK_MODE_IN:
        clock_stop_in(self);
        break;
    case CLOCK_MODE_OUT:
        clock_stop_out(self);
        break;
    default:
        return NULL;
    }

    return self;
}

int clock_lock(clock_s* self)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("clock lock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int clock_unlock(clock_s* self)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("clock unlock: unable to increment sem: semop(2) failed");
        return 1;
    }

    return 0;
}

void clock_advance(clock_s* self, unsigned int ds, unsigned int dn)
{
    // Current time
    unsigned int nanos = self->__mem->nanos;
    unsigned int seconds = self->__mem->seconds;

    // Advance time
    nanos += dn;
    seconds += ds;

    // Wrap nanoseconds
    if (nanos >= 1000000000)
    {
        seconds += nanos / 1000000000;
        nanos %= 1000000000;
    }

    // Update time
    self->__mem->nanos = nanos;
    self->__mem->seconds = seconds;
}

unsigned int clock_get_nanos(clock_s* self)
{
    return self->__mem->nanos;
}

unsigned int clock_get_seconds(clock_s* self)
{
    return self->__mem->seconds;
}

unsigned long clock_get_time(clock_s* self)
{
    return self->__mem->seconds * 1000000000ul + (unsigned long) self->__mem->nanos;
}
