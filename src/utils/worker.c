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

#include "worker.h"
#include "err.h"
#include "fast.h"
#include "cont.h"

/*  Private functions. */
static void nn_worker_routine (void *arg);

void nn_worker_hndl_init (struct nn_worker_hndl *self,
    const struct nn_worker_hndl_vfptr *vfptr)
{
    self->vfptr = vfptr;
}

void nn_worker_hndl_term (struct nn_worker_hndl *self)
{
}

void nn_worker_event_init (struct nn_worker_event *self,
    const struct nn_worker_event_vfptr *vfptr)
{
    self->vfptr = vfptr;
    nn_queue_item_init (&self->item);
}

void nn_worker_event_term (struct nn_worker_event *self)
{
    nn_queue_item_term (&self->item);
}

void nn_worker_init (struct nn_worker *self)
{
    nn_mutex_init (&self->sync);
    nn_queue_init (&self->events);
    nn_worker_event_init (&self->stop, NULL);
    nn_efd_init (&self->efd);
    nn_poller_init (&self->poller);
    nn_poller_add (&self->poller, nn_efd_getfd (&self->efd), &self->efd_hndl);
    nn_poller_set_in (&self->poller, &self->efd_hndl);
    nn_thread_init (&self->thread, nn_worker_routine, self);
}

void nn_worker_term (struct nn_worker *self)
{
    /*  Ask worker thread to terminate. */
    nn_worker_post (self, &self->stop);

    /*  Wait till worker thread terminates. */
    nn_thread_term (&self->thread);

    /*  Clean up. */
    nn_poller_term (&self->poller);
    nn_efd_term (&self->efd);
    nn_worker_event_term (&self->stop);
    nn_queue_term (&self->events);
    nn_mutex_term (&self->sync);
}

void nn_worker_post (struct nn_worker *self, struct nn_worker_event *event)
{
    nn_mutex_lock (&self->sync);
    nn_queue_push (&self->events, &event->item);
    nn_efd_signal (&self->efd);
    nn_mutex_unlock (&self->sync);
}

void nn_worker_add (struct nn_worker *self, int fd, struct nn_worker_hndl *hndl)
{
    nn_poller_add (&self->poller, fd, &hndl->phndl);
}

void nn_worker_rm (struct nn_worker *self, struct nn_worker_hndl *hndl)
{
    nn_poller_rm (&self->poller, &hndl->phndl);
}

void nn_worker_set_in (struct nn_worker *self, struct nn_worker_hndl *hndl)
{
    nn_poller_set_in (&self->poller, &hndl->phndl);
}

void nn_worker_reset_in (struct nn_worker *self, struct nn_worker_hndl *hndl)
{
    nn_poller_reset_in (&self->poller, &hndl->phndl);
}

void nn_worker_set_out (struct nn_worker *self, struct nn_worker_hndl *hndl)
{
    nn_poller_set_out (&self->poller, &hndl->phndl);
}

void nn_worker_reset_out (struct nn_worker *self, struct nn_worker_hndl *hndl)
{
    nn_poller_reset_out (&self->poller, &hndl->phndl);
}

static void nn_worker_routine (void *arg)
{
    int rc;
    struct nn_worker *self;
    int pevent;
    struct nn_poller_hndl *phndl;
    struct nn_queue_item *item;
    struct nn_worker_event *worker_event;
    struct nn_worker_hndl *chndl;

    self = (struct nn_worker*) arg;

    /*  Infinite loop. It will be interrupted only when the object is
        shut down. */
    while (1) {

        /*  Wait for any activity. */
        rc = nn_poller_wait (&self->poller, -1);
        errnum_assert (rc == 0, -rc);

        /*  Process all events from the poller. */
        while (1) {

            /*  Get next poller event, such as IN or OUT. */
            rc = nn_poller_event (&self->poller, &pevent, &phndl);
            if (nn_slow (rc == -EAGAIN))
                break;

            /*  If there are any new incoming worker events, process them. */
            if (phndl == &self->efd_hndl) {
                nn_mutex_lock (&self->sync);
                nn_assert (pevent == NN_POLLER_IN);
                nn_efd_unsignal (&self->efd);
                while (1) {

                    /*  Next worker event. */
                    item = nn_queue_pop (&self->events);
                    if (nn_slow (!item))
                        break;
                    worker_event = nn_cont (item, struct nn_worker_event, item);

                    /*  If the worker thread is asked to stop, do so. */
                    if (nn_slow (worker_event == &self->stop)) {
                        nn_mutex_unlock (&self->sync);
                        return;
                    }

                    /*  It's a standard event. Notify it that it has arrived
                        in the worker thread. */
                    worker_event->vfptr->start (worker_event);
                }
                nn_mutex_unlock (&self->sync);
                continue;
            }

            /*  It's a true I/O event. Invoke the handler. */
            chndl = nn_cont (phndl, struct nn_worker_hndl, phndl);
            chndl->vfptr->event (chndl, pevent);
        }
    }
}

