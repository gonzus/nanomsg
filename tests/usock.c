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

#include "../src/utils/worker.c"
#include "../src/utils/callback.c"
#include "../src/utils/usock.c"
#include "../src/utils/timer.c"
#include "../src/utils/sleep.c"
#include "../src/utils/thread.c"
#include "../src/utils/queue.c"
#include "../src/utils/mutex.c"
#include "../src/utils/efd.c"
#include "../src/utils/poller.c"
#include "../src/utils/timerset.c"
#include "../src/utils/clock.c"
#include "../src/utils/list.c"
#include "../src/utils/alloc.c"
#include "../src/utils/err.c"

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>

static void handler (struct nn_callback *self, void *source, int type)
{
    printf ("%p ", source);
    switch (type) {
    case NN_USOCK_CONNECTED:
        printf ("connected\n");
        break;
    case NN_USOCK_ACCEPTED:
        printf ("accepted\n");
        break;
    case NN_USOCK_SENT:
        printf ("sent\n");
        break;
    case NN_USOCK_RECEIVED:
        printf ("received\n");
        break;
    case NN_USOCK_ERROR:
        printf ("error\n");
        break;
    case NN_USOCK_CLOSED:
        printf ("closed\n");
        break;

    default:
        nn_assert (0);
    }
}

static const struct nn_callback_vfptr vfptr = {handler};

int main ()
{
    int rc;
    struct sockaddr_in addr;
    struct nn_worker worker;
    struct nn_usock bs;
    struct nn_usock cs;
    struct nn_usock as;
    struct nn_callback callback;
    struct nn_iovec iovec;
    char buf [4];
    struct nn_timer timer;

    nn_callback_init (&callback, &vfptr);

    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (0x7f000001);
    addr.sin_port = htons (5555);

    nn_worker_init (&worker);

    rc = nn_usock_init (&bs, AF_INET, SOCK_STREAM, 0, &worker, &callback);
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_bind (&bs, (struct sockaddr*) &addr, sizeof (addr));
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_listen (&bs, 10);
    errnum_assert (rc == 0, -rc);
    nn_usock_accept (&bs, &as, &callback);

    nn_sleep (100);

    rc = nn_usock_init (&cs, AF_INET, SOCK_STREAM, 0, &worker, &callback);
    errnum_assert (rc == 0, -rc);
    nn_usock_connect (&cs, (struct sockaddr*) &addr, sizeof (addr));

    nn_sleep (500);

    iovec.iov_base = "ABCD";
    iovec.iov_len = 4;
    nn_usock_send (&cs, &iovec, 1);

    nn_usock_recv (&as, buf, sizeof (buf));

    nn_sleep (500);

    nn_usock_close (&cs);
    nn_usock_close (&as);
    nn_usock_close (&bs);

    nn_sleep (500);

    nn_timer_init (&timer, &worker, &callback);
    nn_timer_start (&timer, 100);
    nn_sleep (500);
    nn_timer_close (&timer);

    nn_worker_term (&worker);
    nn_callback_term (&callback);


    return 0;
}

