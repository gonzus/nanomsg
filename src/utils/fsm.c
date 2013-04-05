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

#include "fsm.h"
#include "err.h"
#include "cont.h"

void nn_fsm_event_init (struct nn_fsm_event *self)
{
    nn_queue_item_init (&self->item);
}

void nn_fsm_event_term (struct nn_fsm_event *self)
{
    nn_queue_item_term (&self->item);
}

void nn_fsm_init (struct nn_fsm *self, const struct nn_fsm_vfptr *vfptr)
{
    self->vfptr = vfptr;
    nn_queue_init (&self->events);
}

void nn_fsm_term (struct nn_fsm *self)
{
    nn_queue_term (&self->events);
}

void nn_fsm_process (struct nn_fsm *self, struct nn_fsm_event *event)
{
    struct nn_fsm_event *ev;

    nn_queue_push (&self->events, &event->item);

    /*  If the processing loop is already running, do nothing. Event will
        be processed in one of the subsequent iterations of the loop. */
    if (self->flags & NN_FSM_FLAG_PROCESSING)
        return;

    self->flags |= NN_FSM_FLAG_PROCESSING;
    while (1) {

        /*  Get next event to process. If there is none, exit. */
        ev = nn_cont (nn_queue_pop (&self->events), struct nn_fsm_event, item);
        if (!ev)
            break;

        /*  Ask derived class to process the event. */
        self->vfptr->event (ev);
    }
    self->flags &= ~NN_FSM_FLAG_PROCESSING;
}




