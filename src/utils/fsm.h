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

#ifndef NN_FSM_INCLUDED
#define NN_FSM_INCLUDED

#include "queue.h"

/*  Base class for state machines. */

struct nn_fsm_event {
    struct nn_queue_item item;
};

void nn_fsm_event_init (struct nn_fsm_event *self);
void nn_fsm_event_init (struct nn_fsm_event *term);

struct nn_fsm;

struct nn_fsm_vfptr {
    void (*event) (struct nn_fsm_event *self);
};

#define NN_FSM_FLAG_PROCESSING 1

struct nn_fsm {
    const struct nn_fsm_vfptr *vfptr;
    int flags;
    struct nn_queue events;
};

void nn_fsm_init (struct nn_fsm *self, const struct nn_fsm_vfptr *vfptr);
void nn_fsm_term (struct nn_fsm *self);

/*  Send an event to the state machine. The event will be processed once
    nn_fsm_run() is called. This function can be called even from the inside
    of the event handler. */
void nn_fsm_process (struct nn_fsm *self, struct nn_fsm_event *event);

#endif

