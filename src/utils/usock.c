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

/*  Private functions. */
static int nn_usock_geterr (struct nn_usock *self);
static void nn_usock_callback_handler (struct nn_callback *self, void *source,
    int type);
static const struct nn_callback_vfptr nn_usock_vfptr =
    {nn_usock_callback_handler};

static int nn_usock_init_from_fd (struct nn_usock *self,
    int fd, struct nn_worker *worker, struct nn_callback *callback)
{
    int rc;
    int opt;

    /*  Set up the callback pointers. */
    nn_callback_init (&self->in_callback, &nn_usock_vfptr);
    self->out_callback = callback;

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

    self->state = NN_USOCK_STATE_STARTING;

    /*  Initialise sources of callbacks. */
    nn_worker_fd_init (&self->wfd, &self->in_callback);
    nn_worker_task_init (&self->connect_task, &self->in_callback);
    nn_worker_task_init (&self->connected_task, &self->in_callback);
    nn_worker_task_init (&self->accept_task, &self->in_callback);

    /*  We are not accepting a connection at the moment. */
    self->newsock = NULL;
    self->newcallback = NULL;

    return 0;
}

int nn_usock_init (struct nn_usock *self,
    int domain, int type, int protocol,
    struct nn_worker *worker,
    struct nn_callback *callback)
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

    return nn_usock_init_from_fd (self, s, worker, callback);
}

void nn_usock_close (struct nn_usock *self)
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
    struct nn_callback *newcallback)
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
        nn_usock_init_from_fd (newsock, s, self->worker, newcallback);
        self->out_callback->vfptr->callback (self->out_callback, self,
            NN_USOCK_ACCEPTED);
        return;
    }

    /*  Unexpected failure. */
    if (nn_slow (errno != EAGAIN && errno != EWOULDBLOCK &&
          errno != ECONNABORTED)) {
        self->out_callback->vfptr->callback (self->out_callback, self,
            NN_USOCK_ERROR);
        return;
    }

    /*  Ask the worker thread to wait for the new connection. */
    self->newsock = newsock;
    self->newcallback = newcallback;
    nn_worker_post (self->worker, &self->accept_task);    
}

void nn_usock_connect (struct nn_usock *self, const struct sockaddr *addr,
    size_t addrlen)
{
    int rc;

    /* Do the connect itself. */
    rc = connect (self->s, addr, (socklen_t) addrlen);

    /* Immediate success. */
    if (nn_fast (rc == 0)) {

        /*  Ask worker thread to start polling on the socket. */
        nn_worker_post (self->worker, &self->connected_task);

        /*  Notify the user that the connection is established. */
        self->out_callback->vfptr->callback (self->out_callback, self,
            NN_USOCK_CONNECTED);
        return;
    }

    /* Return unexpected errors to the caller. Notify the user about it. */
    if (nn_slow (errno != EINPROGRESS)) {
        self->out_callback->vfptr->callback (self->out_callback, self,
            NN_USOCK_ERROR);
        return;
    }

    /*  Ask worker thread to start waiting for connection establishment. */
    nn_worker_post (self->worker, &self->connect_task);
}

void nn_usock_send (struct nn_usock *self, const struct nn_iovec *iov,
    int iovcnt)
{
    nn_assert (0);
}

void nn_usock_recv (struct nn_usock *self, void *buf, size_t len)
{
    nn_assert (0);
}

static int nn_usock_geterr (struct nn_usock *self)
{
    int rc;
    int opt;
#if defined NN_HAVE_HPUX
    int optsz;
#else
    socklen_t optsz;
#endif

    opt = 0;
    optsz = sizeof (opt);
    rc = getsockopt (self->s, SOL_SOCKET, SO_ERROR, &opt, &optsz);

    /*  The following should handle both Solaris and UNIXes derived from BSD. */
    if (rc == -1) {
        opt = errno;
    }
    else {
        errno_assert (rc == 0);
        nn_assert (optsz == sizeof (opt));
    }

    return opt;
}

static void nn_usock_callback_handler (struct nn_callback *self, void *source,
    int type)
{
    int rc;
    struct nn_usock *usock;
    int s;

    usock = nn_cont (self, struct nn_usock, in_callback);

    switch (usock->state) {

/******************************************************************************/
/*  STARTING                                                                  */
/******************************************************************************/
    case NN_USOCK_STATE_STARTING:
        if (source == &usock->connected_task) {
            nn_assert (type == NN_WORKER_TASK_POSTED);
            nn_worker_add (usock->worker, usock->s, &usock->wfd);
            usock->state = NN_USOCK_STATE_CONNECTED;
            return;
        }
        if (source == &usock->connect_task) {
            nn_assert (type == NN_WORKER_TASK_POSTED);
            nn_worker_add (usock->worker, usock->s, &usock->wfd);
            nn_worker_set_out (usock->worker, &usock->wfd);
            usock->state = NN_USOCK_STATE_CONNECTING;
            return;
        }
        if (source == &usock->accept_task) {
            nn_assert (type == NN_WORKER_TASK_POSTED);
            nn_worker_add (usock->worker, usock->s, &usock->wfd);
            nn_worker_set_in (usock->worker, &usock->wfd);
            usock->state = NN_USOCK_STATE_ACCEPTING;
            return;
        }
        nn_assert (0);

/******************************************************************************/
/*  CONNECTING                                                                */
/******************************************************************************/ 
    case NN_USOCK_STATE_CONNECTING:
        if (source == &usock->wfd) {
            switch (type) {
            case NN_WORKER_FD_OUT:
                nn_worker_reset_out (usock->worker, &usock->wfd);
                usock->state = NN_USOCK_STATE_CONNECTED;
                usock->out_callback->vfptr->callback (usock->out_callback,
                    usock, NN_USOCK_CONNECTED);
                return;
            case NN_WORKER_FD_ERR:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  ACCEPTING                                                                 */
/******************************************************************************/ 
    case NN_USOCK_STATE_ACCEPTING:
        if (source == &usock->wfd) {
            switch (type) {
            case NN_WORKER_FD_IN:
                nn_assert (usock->newsock);
#if NN_HAVE_ACCEPT4
                s = accept4 (usock->s, NULL, NULL, SOCK_CLOEXEC);
#else
                s = accept (usock->s, NULL, NULL);
#endif
                /*  ECONNABORTED is an valid error. If it happens do nothing
                    and wait for next incoming connection to accept. */
                if (s < 0) {
                    if (errno == ECONNABORTED)
                        return;
                    errno_assert (0);
                }

                nn_usock_init_from_fd (usock->newsock, s, usock->worker,
                    usock->newcallback);
                usock->out_callback->vfptr->callback (usock->out_callback,
                    usock, NN_USOCK_ACCEPTED);
                usock->newsock = NULL;
                usock->newcallback = NULL;
                return;
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  CONNECTED                                                                 */
/******************************************************************************/ 
    case NN_USOCK_STATE_CONNECTED:
        if (source == &usock->wfd) {
            switch (type) {
            case NN_WORKER_FD_IN:
                nn_assert (0);
            case NN_WORKER_FD_OUT:
                nn_assert (0);
            case NN_WORKER_FD_ERR:
                nn_assert (0);
            default:
                nn_assert (0);
            }
        }
        nn_assert (0);

/******************************************************************************/
/*  Invalid state                                                             */
/******************************************************************************/ 
    default:
        nn_assert (0);
    }
}

