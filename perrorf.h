/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

//
// perrorf.h
// A utility for formatting perror-style error messages.
//

#ifndef PERRORF_H
#define PERRORF_H

#define PERRORF_MSG_SIZE 512

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>

/**
 * Print a formatted string to stderr explaining the meaning of errno.
 *
 * @param format Format string
 * @param ... Format parameters
 */
static inline void perrorf(const char* format, ...)
{
    // Make sure perror(3) will actually print something
    if (!errno)
        return;

    // Format message into a buffer
    va_list args;
    va_start(args, format);
    char msg[PERRORF_MSG_SIZE + 1];
    vsnprintf(msg, PERRORF_MSG_SIZE * sizeof(char), format, args);
    va_end(args);

    // Terminate the string
    msg[256] = '\0';

    perror(msg);
}

#endif // #ifndef PERRORF_H
