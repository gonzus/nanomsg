/*
    Copyright (c) 2013 250bpm s.r.o.

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"),
    to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense,
    and/or sell copies of the Software, and to permit persons to whom
    the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
    THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
    IN THE SOFTWARE.
*/

#ifndef NN_WORKER_INCLUDED
#define NN_WORKER_INCLUDED

#include "queue.h"
#include "mutex.h"
#include "thread.h"
#include "efd.h"
#include "poller.h"

/*  Asynchronous I/O completion port. */

#define NN_WORKER_HNDL_IN NN_POLLER_IN
#define NN_WORKER_HNDL_OUT NN_POLLER_OUT
#define NN_WORKER_HNDL_ERR NN_POLLER_ERR

/*  This class represents a file descriptor registered with the poller. */
struct nn_worker_hndl;

struct nn_worker_hndl_vfptr {
    void (*event) (struct nn_worker_hndl *self, int event);
};

struct nn_worker_hndl {
    const struct nn_worker_hndl_vfptr *vfptr;
    struct nn_poller_hndl phndl;
};

void nn_worker_hndl_init (struct nn_worker_hndl *self,
    const struct nn_worker_hndl_vfptr *vfptr);
void nn_worker_hndl_term (struct nn_worker_hndl *self);

/*  This class represents and event sent to the completion port, such as
    request for an asynchronous operation. */
struct nn_worker_event;

struct nn_worker_event_vfptr {
    void (*start) (struct nn_worker_event *self);
};

struct nn_worker_event {
    const struct nn_worker_event_vfptr *vfptr;
    struct nn_queue_item item;
};

void nn_worker_event_init (struct nn_worker_event *self,
    const struct nn_worker_event_vfptr *vfptr);
void nn_worker_event_term (struct nn_worker_event *self);


/*  This is the completion port per se. It handles the event loop and passes
    events between the poller and async objects such as nn_usock. */
struct nn_worker {
    struct nn_mutex sync;
    struct nn_queue events;
    struct nn_worker_event stop;
    struct nn_efd efd;
    struct nn_poller poller;
    struct nn_poller_hndl efd_hndl;
    struct nn_thread thread;
};

void nn_worker_init (struct nn_worker *self);
void nn_worker_term (struct nn_worker *self);

void nn_worker_post (struct nn_worker *self, struct nn_worker_event *event);

void nn_worker_add (struct nn_worker *self, int fd, struct nn_worker_hndl *hndl);
void nn_worker_rm (struct nn_worker *self, struct nn_worker_hndl *hndl);
void nn_worker_set_in (struct nn_worker *self, struct nn_worker_hndl *hndl);
void nn_worker_reset_in (struct nn_worker *self, struct nn_worker_hndl *hndl);
void nn_worker_set_out (struct nn_worker *self, struct nn_worker_hndl *hndl);
void nn_worker_reset_out (struct nn_worker *self, struct nn_worker_hndl *hndl);

#endif

