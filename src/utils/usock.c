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

#include "usock.h"
#include "cont.h"
#include "err.h"

#include <unistd.h>
#include <fcntl.h>

#define NN_USOCK_STATE_STARTING 1
#define NN_USOCK_STATE_CONNECTING 2
#define NN_USOCK_STATE_CONNECTED 3
#define NN_USOCK_STATE_ACCEPTING 4

#define NN_USOCK_EVENT_IN 1
#define NN_USOCK_EVENT_OUT 2
#define NN_USOCK_EVENT_ERR 3
#define NN_USOCK_EVENT_CONNECTED 4
#define NN_USOCK_EVENT_CONNECT 5
#define NN_USOCK_EVENT_ACCEPT 6

/*  Make sure that we can forward the poller events to the user without
    converting them. */
CT_ASSERT (NN_USOCK_EVENT_IN == NN_POLLER_IN);
CT_ASSERT (NN_USOCK_EVENT_OUT == NN_POLLER_OUT);
CT_ASSERT (NN_USOCK_EVENT_ERR == NN_POLLER_ERR);

static void nn_usock_process (struct nn_usock *self, int event);
static void nn_usock_callback_handler (struct nn_callback *self, void *source,
    int type)
{
    struct nn_usock *usock;

    usock = nn_cont (self, struct nn_usock, callback);

    /*  This function coverts callback events into native usock events. */
    if (source == &usock->wfd) {
        nn_usock_process (usock, type);
        return;
    }
    nn_assert (type == NN_WORKER_TASK_POSTED);
    if (source == &usock->connected_task) {
        nn_usock_process (usock, NN_USOCK_EVENT_CONNECTED);
        return;
    }
    if (source == &usock->connect_task) {
        nn_usock_process (usock, NN_USOCK_EVENT_CONNECT);
        return;
    }
    if (source == &usock->accept_task) {
        nn_usock_process (usock, NN_USOCK_EVENT_ACCEPT);
        return;
    }
    nn_assert (0);
}
static const struct nn_callback_vfptr nn_usock_vfptr =
    {nn_usock_callback_handler};

#if 0
void nn_usock_event_init (struct nn_usock_event *self,
    const struct nn_usock_event_vfptr *vfptr)
{
    self->vfptr = vfptr;
    nn_worker_event_init (&self->worker_event, &nn_usock_worker_event_vfptr);
    self->error = 0;
}

void nn_usock_event_term (struct nn_usock_event *self)
{
    nn_worker_event_term (&self->worker_event);
}
#endif

static int nn_usock_init_from_fd (struct nn_usock *self,
    int fd, struct nn_worker *worker)
{
    int rc;
    int opt;

    nn_callback_init (&self->callback, &nn_usock_vfptr);

    /*  Store the reference to the worker the socket is associated with. */
    self->worker = worker;

    /*  Store the file descriptor of the underlying socket. */
    self->s = fd;

    /* Setting FD_CLOEXEC option immediately after socket creation is the
        second best option after using SOCK_CLOEXEC. There is a race condition
        here (if process is forked between socket creation and setting
        the option) but the problem is pretty unlikely to happen. */
#if defined FD_CLOEXEC
    rc = fcntl (self->s, F_SETFD, FD_CLOEXEC);
#if defined NN_HAVE_OSX
    errno_assert (rc != -1 || errno == EINVAL);
#else
    errno_assert (rc != -1);
#endif
#endif

    /* If applicable, prevent SIGPIPE signal when writing to the connection
        already closed by the peer. */
#ifdef SO_NOSIGPIPE
    opt = 1;
    rc = setsockopt (self, SOL_SOCKET, SO_NOSIGPIPE, &opt, sizeof (opt));
#if defined NN_HAVE_OSX
    errno_assert (rc == 0 || errno == EINVAL);
#else
    errno_assert (rc == 0);
#endif
#endif

    /* Switch the socket to the non-blocking mode. All underlying sockets
        are always used in the callbackhronous mode. */
    opt = fcntl (self->s, F_GETFL, 0);
    if (opt == -1)
        opt = 0;
    if (!(opt & O_NONBLOCK)) {
        rc = fcntl (self->s, F_SETFL, opt | O_NONBLOCK);
#if defined NN_HAVE_OSX
        errno_assert (rc != -1 || errno == EINVAL);
#else
        errno_assert (rc != -1);
#endif
    }

    nn_worker_fd_init (&self->wfd, &self->callback);

    self->state = NN_USOCK_STATE_STARTING;

    /*  Initialise the events to be sent to the worker thread. */
    nn_worker_task_init (&self->connect_task, &self->callback);
    nn_worker_task_init (&self->connected_task, &self->callback);
    nn_worker_task_init (&self->accept_task, &self->callback);

    /*  We are not accepting a connection at the moment. */
    self->newsock = NULL;

    return 0;
}

int nn_usock_init (struct nn_usock *self,
    int domain, int type, int protocol, struct nn_worker *worker)
{
    int s;

    /*  If the operating system allows to directly open the socket with CLOEXEC
        flag, do so. That way there are no race conditions. */
#ifdef SOCK_CLOEXEC
    type |= SOCK_CLOEXEC;
#endif

    /* Open the underlying socket. */
    s = socket (domain, type, protocol);
    if (s < 0)
       return -errno;

    return nn_usock_init_from_fd (self, s, worker);
}

void nn_usock_close (struct nn_usock *self, struct nn_usock_event *event)
{
    nn_assert (0);
}

int nn_usock_setsockopt (struct nn_usock *self, int level, int optname,
    const void *optval, size_t optlen)
{
    int rc;

    /*  EINVAL errors are ignored on OSX platform. The reason for that is buggy
        OSX behaviour where setsockopt returns EINVAL if the peer have already
        disconnected. Thus, nn_usock_setsockopt() can succeed on OSX even though
        the option value was invalid, but the peer have already closed the
        connection. This behaviour should be relatively harmless. */
    rc = setsockopt (self->s, level, optname, optval, (socklen_t) optlen);
#if defined NN_HAVE_OSX
    if (nn_slow (rc != 0 && errno != EINVAL))
        return -errno;
#else
    if (nn_slow (rc != 0))
        return -errno;
#endif

    return 0;
}

int nn_usock_bind (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;

    rc = bind (self->s, addr, (socklen_t) addrlen);
    if (nn_slow (rc != 0))
        return -errno;

    return 0;
}

int nn_usock_listen (struct nn_usock *self, int backlog)
{
    int rc;

    /*  Start listening for incoming connections. */
    rc = listen (self->s, backlog);
    if (nn_slow (rc != 0))
        return -errno;

    return 0;
}

void nn_usock_accept (struct nn_usock *self, struct nn_usock *newsock,
    struct nn_usock_event *event)
{
    int s;

    /*  If newsock is not NULL, other accept is in progress. That should never
        happen. */
    nn_assert (!self->newsock);

#if NN_HAVE_ACCEPT4
    s = accept4 (self->s, NULL, NULL, SOCK_CLOEXEC);
#else
    s = accept (self->s, NULL, NULL);
#endif

    /*  Immediate success. */
    if (nn_fast (s >= 0)) {
        nn_usock_init_from_fd (newsock, s, self->worker);
        event->vfptr->event (event);
        return;
    }

    /*  Unexpected failure. */
    if (nn_slow (errno != EAGAIN && errno != EWOULDBLOCK &&
          errno != ECONNABORTED)) {
        event->error = errno;
        event->vfptr->event (event);
        return;
    }

    /*  Ask the worker thread to wait for the new connection. */
    self->newsock = newsock;
    nn_worker_post (self->worker, &self->accept_task);    
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen, struct nn_usock_event *event)
{
    int rc;

    /* Do the connect itself. */
    rc = connect (self->s, addr, (socklen_t) addrlen);

    /* Immediate success. */
    if (nn_fast (rc == 0)) {

        /*  Ask worker thread to start polling on the socket. */
        nn_worker_post (self->worker, &self->connected_task);

        /*  Notify the user that the connection is established. */
        event->vfptr->event (event);

        return;
    }

    /* Return unexpected errors to the caller. Notify the user about it. */
    if (nn_slow (errno != EINPROGRESS)) {
        event->error = errno;
        event->vfptr->event (event);
        return;
    }

    /*  Ask worker thread to start waiting for connection establishment. */
    nn_worker_post (self->worker, &self->connect_task);
}

void nn_usock_send (struct nn_usock *self, const struct nn_iobuf *iov,
    int iovcnt, struct nn_usock_event *event)
{
    nn_assert (0);
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len,
    struct nn_usock_event *event)
{
    nn_assert (0);
}

static void nn_usock_process (struct nn_usock *self, int event)
{
    switch (self->state) {
    case NN_USOCK_STATE_STARTING:
        switch (event) {
        case NN_USOCK_EVENT_CONNECTED:
            nn_worker_add (self->worker, self->s, &self->wfd);
            self->state = NN_USOCK_STATE_CONNECTED;
            break;
        case NN_USOCK_EVENT_CONNECT:
            nn_worker_add (self->worker, self->s, &self->wfd);
            nn_worker_set_out (self->worker, &self->wfd);
            self->state = NN_USOCK_STATE_CONNECTING;
            break;
        case NN_USOCK_EVENT_ACCEPT:
            nn_worker_add (self->worker, self->s, &self->wfd);
            nn_worker_set_in (self->worker, &self->wfd);
            self->state = NN_USOCK_STATE_ACCEPTING;
            break;
        default:
            nn_assert (0);
        }
        break;
    case NN_USOCK_STATE_CONNECTING:
        switch (event) {
        case NN_USOCK_EVENT_OUT:
            nn_worker_reset_out (self->worker, &self->wfd);
            self->state = NN_USOCK_STATE_CONNECTED;
            break;
        case NN_USOCK_EVENT_ERR:
            nn_assert (0);
        default:
            nn_assert (0);
        }
        break;
    case NN_USOCK_STATE_CONNECTED:
        switch (event) {
        case NN_USOCK_EVENT_IN:
            nn_assert (0);
        case NN_USOCK_EVENT_OUT:
            nn_assert (0);
        case NN_USOCK_EVENT_ERR:
            nn_assert (0);
        default:
            nn_assert (0);
        }
        break;
    case NN_USOCK_STATE_ACCEPTING:
        switch (event) {
        case NN_USOCK_EVENT_IN:
            nn_assert (0);
        default:
            nn_assert (0);
        }
        break;
    default:
        nn_assert (0);
    }
}

