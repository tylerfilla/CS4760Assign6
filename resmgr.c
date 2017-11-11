/*
 * Tyler Filla
 * CS 4760
 * Assignment 5
 */

#include <stdlib.h>
#include "resmgr.h"

#define SHM_FTOK_CHAR 'R'
#define SEM_FTOK_CHAR 'S'

static int resmgr_start_client(resmgr_s* self)
{
    if (self->running)
        return 1;

    return 0;
}

static int resmgr_stop_client(resmgr_s* self)
{
    if (!self->running)
        return 1;

    return 0;
}

static int resmgr_start_server(resmgr_s* self)
{
    if (self->running)
        return 1;

    return 0;
}

static int resmgr_stop_server(resmgr_s* self)
{
    if (!self->running)
        return 1;

    return 0;
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
