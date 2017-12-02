/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
 */

#include <errno.h>
#include <stdarg.h>
#include <string.h>

#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <unistd.h>

#include "logger.h"

#define SEM_FTOK_CHAR 'L'
#define SHM_FTOK_CHAR 'O'

/**
 * The maximum length of a log record message.
 */
#define RECORD_MSG_MAX 256

/**
 * The size of the log record buffer.
 */
#define BUFFER_SIZE 16

/**
 * Internal memory for logger. Shared.
 */
struct __logger_mem_s
{
    /** The log record buffer. */
    char buffer[BUFFER_SIZE][RECORD_MSG_MAX];

    /** The number of buffered records. */
    int num_buffered_records;
};

/**
 * Start a client logger instance.
 */
static int logger_start_client(logger_s* self)
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
        perror("start client logger: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Obtain existing shared memory segment
    int shmid = shmget(shm_key, 0, 0);
    if (errno)
    {
        perror("start client logger: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start client logger: unable to attach shm: shmat(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Obtain IPC key for semaphore set
    key_t sem_key = ftok(".", SEM_FTOK_CHAR);
    if (errno)
    {
        perror("start client logger: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Obtain existing semaphore set
    int semid = semget(sem_key, 0, 0);
    if (errno)
    {
        perror("start client logger: unable to get sem: semget(3) failed");
        goto fail_sem;
    }

    self->running = LOGGER_RUNNING;
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
            perror("start client logger: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    return 1;
}

/**
 * Start a server logger instance.
 */
static int logger_start_server(logger_s* self)
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
        perror("start server logger: unable to obtain shm key: ftok(3) failed");
        goto fail_shm;
    }

    // Create shared memory segment
    shmid = shmget(shm_key, sizeof(__logger_mem_s), IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start server logger: unable to get shm: shmget(2) failed");
        goto fail_shm;
    }

    // Attach shared memory segment
    shm = shmat(shmid, NULL, 0);
    if (errno)
    {
        perror("start server logger: unable to attach shm: shmat(2) failed");
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
        perror("start server logger: unable to obtain sem key: ftok(3) failed");
        goto fail_sem;
    }

    // Create semaphore set with one element
    semid = semget(sem_key, 1, IPC_CREAT | IPC_EXCL | 0600);
    if (errno)
    {
        perror("start server logger: unable to get sem: semget(2) failed");
        goto fail_sem;
    }

    // Configure unlocked binary semaphore
    semctl(semid, 0, SETVAL, 1);
    if (errno)
    {
        perror("start server logger: unable to set sem value: semctl(2) failed");
        goto fail_sem;
    }

    self->running = LOGGER_RUNNING;
    self->shmid = shmid;
    self->semid = semid;
    self->__mem = shm;

    // Initialize record buffer
    memset(self->__mem->buffer, 0, BUFFER_SIZE * RECORD_MSG_MAX);
    self->__mem->num_buffered_records = 0;

    return 0;

fail_sem:
    // Remove semaphore set, if needed
    if (semid >= 0)
    {
        semctl(semid, 0, IPC_RMID);
        if (errno)
        {
            perror("start server logger: cleanup: unable to remove sem: semctl(2) failed");
        }
    }

fail_shm:
    // Detach shared memory, if needed
    if (shm != NULL)
    {
        shmdt(shm);
        if (errno)
        {
            perror("start server logger: cleanup: unable to detach shm: shmdt(2) failed");
        }
    }

    // Remove shared memory, if needed
    if (shmid >= 0)
    {
        shmctl(shmid, IPC_RMID, NULL);
        if (errno)
        {
            perror("start server logger: cleanup: unable to remove shm: shmctl(2) failed");
        }
    }

    return 1;
}

/**
 * Stop a client logger instance.
 */
static int logger_stop_client(logger_s* self)
{
    if (!self->running)
        return 1;

    errno = 0;

    // Detach shared memory segment
    shmdt(self->__mem);
    if (errno)
    {
        perror("stop client logger: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    self->running = LOGGER_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_shm:
    return 1;
}

/**
 * Stop a server logger instance.
 */
static int logger_stop_server(logger_s* self)
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
        perror("stop server logger: unable to detach shm: shmdt(2) failed");
        goto fail_shm;
    }

    // Remove shared memory segment
    shmctl(self->shmid, IPC_RMID, NULL);
    if (errno)
    {
        perror("stop server logger: unable to remove shm: shmctl(2) failed");
        goto fail_shm;
    }

    //
    // Semaphore
    //

    // Remove semaphore set
    semctl(self->semid, 0, IPC_RMID);
    if (errno)
    {
        perror("stop server logger: unable to remove sem: semctl(2) failed");
        goto fail_sem;
    }

    self->running = LOGGER_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    return 0;

fail_sem:
fail_shm:
    return 1;
}

logger_s* logger_construct(logger_s* self, int mode)
{
    if (self == NULL)
        return NULL;

    self->mode = mode;
    self->running = LOGGER_NOT_RUNNING;
    self->shmid = -1;
    self->semid = -1;
    self->__mem = NULL;

    switch (mode)
    {
    case LOGGER_MODE_CLIENT:
        logger_start_client(self);
        break;
    case LOGGER_MODE_SERVER:
        logger_start_server(self);
        break;
    default:
        return NULL;
    }

    return self;
}

logger_s* logger_destruct(logger_s* self)
{
    if (self == NULL)
        return NULL;

    switch (self->mode)
    {
    case LOGGER_MODE_CLIENT:
        logger_stop_client(self);
        break;
    case LOGGER_MODE_SERVER:
        logger_stop_server(self);
        break;
    default:
        return NULL;
    }

    return self;
}

int logger_lock(logger_s* self)
{
    errno = 0;

    // Try to decrement semaphore
    struct sembuf buf = { 0, -1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("logger lock: unable to decrement sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int logger_unlock(logger_s* self)
{
    errno = 0;

    // Try to increment semaphore
    struct sembuf buf = { 0, 1, 0 };
    semop(self->semid, &buf, 1);
    if (errno)
    {
        perror("logger unlock: unable to increment sem: semop(2) failed");
        return 1;
    }

    return 0;
}

int logger_dump(logger_s* self, FILE* file)
{
    // Print out and remove records line-by-line
    for (int i = 0; i < self->__mem->num_buffered_records; ++i)
    {
        // Log record message
        char* msg = self->__mem->buffer[i];

        // Skip empty messages
        if (msg[0] == '\0')
            continue;

        // Print the message, followed by a line break
        fprintf(file, "%s\n", msg);

        // Clear the message
        msg[0] = '\0';
    }

    // Flush the file buffer
    fflush(file);

    // All records removed from buffer
    self->__mem->num_buffered_records = 0;

    return 0;
}

int logger_submit(logger_s* self, const char* msg, size_t msg_len)
{
    if (self->__mem->num_buffered_records == BUFFER_SIZE)
        return 1;

    // Copy the log record message into the buffer
    //size_t len = msg_len < RECORD_MSG_MAX ? msg_len : RECORD_MSG_MAX;
    //strncpy(self->__mem->buffer[self->__mem->num_buffered_records], msg, len);
    //self->__mem->buffer[self->__mem->num_buffered_records][len - 1] = '\0';
    //self->__mem->num_buffered_records++;

    return 0;
}

int logger_log(logger_s* self, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    // Build the formatted message
    char msg[RECORD_MSG_MAX];
    vsnprintf(msg, RECORD_MSG_MAX, fmt, args);
    msg[RECORD_MSG_MAX - 1] = '\0';

    va_end(args);

    fprintf(stderr, "%s\n", msg);

    /*
    va_list args;
    va_start(args, fmt);

    // Build the formatted message
    char msg[RECORD_MSG_MAX];
    vsnprintf(msg, RECORD_MSG_MAX, fmt, args);
    msg[RECORD_MSG_MAX - 1] = '\0';

    va_end(args);

    if (logger_lock(self))
        return 1;

    // Submit the formatted message
    if (logger_submit(self, msg, RECORD_MSG_MAX))
        return 2;

    if (logger_unlock(self))
        return 3;
        */

    return 0;
}
