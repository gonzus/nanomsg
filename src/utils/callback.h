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

#ifndef NN_ASYNC_INCLUDED
#define NN_ASYNC_INCLUDED

/*  Base class objects accepting callbacks. */

/*  Types of callback events. */
#define NN_ASYNC_OK 0
#define NN_ASYNC_ERR 1
#define NN_ASYNC_IN 2
#define NN_ASYNC_OUT 3

struct nn_callback;

struct nn_callback_vfptr {
    void (*event) (struct nn_callback *self, void *source, int type);
};

struct nn_callback {
    const struct nn_callback_vfptr *vfptr;
};

void nn_callback_init (struct nn_callback *self,
    const struct nn_callback_vfptr *vfptr);
void nn_callback_term (struct nn_callback *self);

#endif

