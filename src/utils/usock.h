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

#ifndef NN_USOCK_INCLUDED
#define NN_USOCK_INCLUDED

#include "worker.h"

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/socket.h>
#undef _GNU_SOURCE

/*  OS-level sockets. */

struct nn_iobuf {
    void *iov_base;
    size_t iov_len;
};

struct nn_usock_event;

struct nn_usock_event_vfptr {
    void (*event) (struct nn_usock_event *self);
};

struct nn_usock_event {
    const struct nn_usock_event_vfptr *vfptr;
    struct nn_worker_event worker_event;
    int error;
};

void nn_usock_event_init (struct nn_usock_event *self,
    const struct nn_usock_event_vfptr *vfptr);
void nn_usock_event_term (struct nn_usock_event *self);

struct nn_usock {

    /*  The worker thread the usock is associated with. */
    struct nn_worker *worker;

    /*  The underlying OS socket and handle that represents it in the poller. */
    int s;
    struct nn_worker_hndl hndl;

    /*  The state the usock's state machine is in. This value is accessed
        solely from the worker thread. */
    int state;

    /*  This event is used to signal that the connection is established, that
        we are waiting for a connection to be established or that a new
        connection can be accepted. We could have used three separate events,
        but given that the three modes are mutually exclusive, we can do with
        a single one. */
    struct nn_worker_event init;

    /*  When accepting a new connection, the pointer to the object to associate
        the new connection with is stored here. */
    struct nn_usock *newsock;
};

int nn_usock_init (struct nn_usock *self,
    int domain, int type, int protocol, struct nn_worker *worker);
void nn_usock_close (struct nn_usock *self, struct nn_usock_event *event);

int nn_usock_setsockopt (struct nn_usock *self, int level, int optname,
    const void *optval, size_t optlen);

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen);
int nn_usock_listen (struct nn_usock *self, int backlog);
void nn_usock_accept (struct nn_usock *self, struct nn_usock *newsock,
    struct nn_usock_event *event);
void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen, struct nn_usock_event *event);

void nn_usock_send (struct nn_usock *self, const struct nn_iobuf *iov,
    int iovcnt, struct nn_usock_event *event);
void nn_usock_recv (struct nn_usock *self, void *buf, size_t len,
    struct nn_usock_event *event);

#endif

