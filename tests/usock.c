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
#include "../src/utils/sleep.c"
#include "../src/utils/thread.c"
#include "../src/utils/queue.c"
#include "../src/utils/mutex.c"
#include "../src/utils/efd.c"
#include "../src/utils/poller.c"
#include "../src/utils/timerset.c"
#include "../src/utils/clock.c"
#include "../src/utils/list.c"
#include "../src/utils/err.c"

#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>

int main ()
{
    int rc;
    struct sockaddr_in addr;
    struct nn_worker worker;
    struct nn_usock bs;
    struct nn_usock cs;
    struct nn_usock as;
    struct nn_usock_event cev;
    struct nn_usock_event aev;

    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (0x7f000001);

    nn_worker_init (&worker);

    rc = nn_usock_init (&bs, AF_INET, SOCK_STREAM, 0, &worker);
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_bind (&bs, (struct sockaddr*) &addr, sizeof (addr));
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_listen (&bs, 10);
    errnum_assert (rc == 0, -rc);
    nn_usock_accept (&bs, &as, &aev);

    nn_sleep (100);

    rc = nn_usock_init (&cs, AF_INET, SOCK_STREAM, 0, &worker);
    errnum_assert (rc == 0, -rc);
    nn_usock_connect (&cs, (struct sockaddr*) &addr, sizeof (addr), &cev);

    nn_sleep (10000000);

    return 0;
}

