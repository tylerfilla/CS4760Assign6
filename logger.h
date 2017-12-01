/*
 * Tyler Filla
 * CS 4760
 * Assignment 6
 */

#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>

/**
 * Mode indicating a logger is operating as a server.
 */
#define LOGGER_MODE_SERVER 0

/**
 * Mode indicating a logger is operating as a client.
 */
#define LOGGER_MODE_CLIENT 1

/**
 * State indicating a logger is not running.
 */
#define LOGGER_NOT_RUNNING 0

/**
 * State indicating a logger is running.
 */
#define LOGGER_RUNNING 1

typedef struct __logger_mem_s __logger_mem_s;

typedef struct
{
    /** Logger mode. */
    int mode;

    /** Whether the logger is currently running. */
    int running;

    /** The ID of the shared memory segment used. */
    int shmid;

    /** The ID of the semaphore set protecting the internal memory. */
    int semid;

    /** Internal shared memory structure. */
    __logger_mem_s* __mem;

    /** A convenient log submit function (equal to lock-submit-unlock). */
    int (* log)(const char* msg);
} logger_s;

/**
 * Create a logger instance.
 */
#define logger_new(mode) logger_construct(malloc(sizeof(logger_s)), (mode))

/**
 * Destroy a logger instance.
 */
#define logger_delete(memmgr) free(logger_destruct(memmgr))

/**
 * Construct a logger instance.
 *
 * @param logger The logger instance
 * @param mode The logger mode
 * @return The logger instance, constructed
 */
logger_s* logger_construct(logger_s* logger, int mode);

/**
 * Destruct a logger instance.
 *
 * @param logger The logger instance
 * @return The logger instance, destructed
 */
logger_s* logger_destruct(logger_s* logger);

/**
 * Lock a logger for exclusive access. This blocks if already locked.
 *
 * @param logger The logger instance
 * @return Zero on success, otherwise nonzero
 */
int logger_lock(logger_s* logger);

/**
 * Unlock a locked logger.
 *
 * @param logger The logger instance
 * @return Zero on success, otherwise nonzero
 */
int logger_unlock(logger_s* logger);

/**
 * Dump the contents of a logger's buffer into the given file.
 *
 * @param logger The logger instance
 * @param file The destination file
 * @return Zero on success, otherwise nonzero
 */
int logger_dump(logger_s* logger, FILE* file);

/**
 * Submit a log record to a logger.
 *
 * @param logger The logger instance
 * @param msg The record message
 * @param msg_len The length of the record message
 * @return Zero on success, otherwise nonzero
 */
int logger_submit(logger_s* logger, const char* msg, size_t msg_len);

/**
 * Conveniently submit a log record to a logger with optional formatting.
 *
 * @param logger The logger instance
 * @param fmt The null-terminated record message
 * @param ... The format arguments
 * @return Zero on success, otherwise nonzero
 */
int logger_log(logger_s* logger, const char* fmt, ...);

#endif // #ifndef LOGGER_H
