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

#include "../protocol.h"

#include "sock.h"

#include "../utils/err.h"

void nn_sockbase_init (struct nn_sockbase *self,
    const struct nn_sockbase_vfptr *vfptr, void *hint)
{
    self->vfptr = vfptr;
    self->sock = (struct nn_sock*) hint;
    nn_fsm_event_init (&self->event_closed);
}

void nn_sockbase_term (struct nn_sockbase *self)
{
}

void nn_sockbase_closed (struct nn_sockbase *self)
{
    /*  TODO: Do the following in a more sane way. */
    self->event_closed.fsm = &self->sock->fsm;
    self->event_closed.source = self;
    self->event_closed.type = NN_SOCKBASE_CLOSED;
    nn_ctx_raise (self->sock->fsm.ctx, &self->event_closed);
}

struct nn_ctx *nn_sockbase_getctx (struct nn_sockbase *self)
{
    return nn_sock_getctx (self->sock);
}

int nn_sockbase_getopt (struct nn_sockbase *self, int option,
    void *optval, size_t *optvallen)
{
    return nn_sock_getopt_inner (self->sock, NN_SOL_SOCKET, option,
        optval, optvallen);
}

