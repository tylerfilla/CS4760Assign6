/*
 * Tyler Filla
 * CS 4760
 * Assignment 3
 */

typedef struct
{
    int shmid;
} a_clock_t;

/**
 * Create a clock instance.
 */
#define a_clock_new() a_clock_construct(malloc(sizeof(a_clock_t)))

/**
 * Destroy a clock instance.
 */
#define a_clock_delete(clock) free(a_clock_destruct(clock))

/**
 * Construct a clock instance.
 *
 * @param clock The clock instance
 * @return The clock instance, constructed
 */
a_clock_t* a_clock_construct(a_clock_t* clock);

/**
 * Destruct a clock instance.
 *
 * @param clock The clock instance
 * @return The clock instance, destructed
 */
a_clock_t* a_clock_destruct(a_clock_t* clock);
