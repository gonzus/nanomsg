/*
    Copyright (c) 2012-2013 250bpm s.r.o.

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

#if !defined NN_HAVE_WINDOWS

#include "ipc.h"

#include "../../ipc.h"

#include "../utils/bstream.h"
#include "../utils/cstream.h"

#include "../../utils/err.h"
#include "../../utils/alloc.h"
#include "../../utils/fast.h"
#include "../../utils/list.h"

#include <string.h>
#include <sys/un.h>
#include <unistd.h>

#define NN_IPC_BACKLOG 10

/*  Implementation of virtual functions from bstream. */
static int nn_ipc_bstream_open (const char *addr, struct nn_usock *usock,
    struct nn_fsm *owner);
const struct nn_bstream_vfptr nn_ipc_bstream_vfptr = {
    nn_ipc_bstream_open
};

/*  Implementation of virtual functions from cstream. */
static int nn_ipc_cstream_open (struct nn_usock *usock, struct nn_fsm *owner);
static int nn_ipc_cstream_resolve (const char *addr,
    struct sockaddr_storage *local, socklen_t *locallen,
    struct sockaddr_storage *remote, socklen_t *remotelen);
const struct nn_cstream_vfptr nn_ipc_cstream_vfptr = {
    nn_ipc_cstream_open,
    nn_ipc_cstream_resolve
};

/*  nn_transport interface. */
static void nn_ipc_init (void);
static void nn_ipc_term (void);
static int nn_ipc_bind (const char *addr, void *hint,
    struct nn_epbase **epbase);
static int nn_ipc_connect (const char *addr, void *hint,
    struct nn_epbase **epbase);

static struct nn_transport nn_ipc_vfptr = {
    "ipc",
    NN_IPC,
    nn_ipc_init,
    nn_ipc_term,
    nn_ipc_bind,
    nn_ipc_connect,
    NULL,
    NN_LIST_ITEM_INITIALIZER
};

struct nn_transport *nn_ipc = &nn_ipc_vfptr;

static void nn_ipc_init (void)
{
}

static void nn_ipc_term (void)
{
}

static int nn_ipc_bind (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    struct nn_bstream *bstream;

    bstream = nn_alloc (sizeof (struct nn_bstream), "bstream (ipc)");
    alloc_assert (bstream);
    rc = nn_bstream_init (bstream, addr, hint, &nn_ipc_bstream_vfptr);
    if (nn_slow (rc != 0)) {
        nn_free (bstream);
        return rc;
    }
    *epbase = &bstream->epbase;

    return 0;
}

static int nn_ipc_connect (const char *addr, void *hint,
    struct nn_epbase **epbase)
{
    int rc;
    struct nn_cstream *cstream;

    /*  TODO: Check the syntax of the address here! */

    cstream = nn_alloc (sizeof (struct nn_cstream), "cstream (ipc)");
    alloc_assert (cstream);
    rc = nn_cstream_init (cstream, addr, hint, &nn_ipc_cstream_vfptr);
    if (nn_slow (rc != 0)) {
        nn_free (cstream);
        return rc;
    }
    *epbase = &cstream->epbase;

    return 0;
}

static int nn_ipc_bstream_open (const char *addr, struct nn_usock *usock,
    struct nn_fsm *owner)
{
    int rc;
    struct sockaddr_storage ss;
    socklen_t sslen;
    struct sockaddr_un *un;

    /*  Create the AF_UNIX address. */
    memset (&ss, 0, sizeof (ss));
    un = (struct sockaddr_un*) &ss;
    if (strlen (addr) >= sizeof (un->sun_path))
        return -ENAMETOOLONG;
    ss.ss_family = AF_UNIX;
    strncpy (un->sun_path, addr, sizeof (un->sun_path));
    sslen = sizeof (struct sockaddr_un);

    /*  Delete the ipc file left over by eventual previous runs of
        the application. */
    rc = unlink (addr);
    errno_assert (rc == 0 || errno == ENOENT);

    /*  Open a listening socket. */
    rc = nn_usock_init (usock, AF_UNIX, SOCK_STREAM, 0, owner);
    if (nn_slow (rc < 0))
        return rc;
    rc = nn_usock_bind (usock, (struct sockaddr*) &ss, sslen);
    errnum_assert (rc == 0, -rc);
    rc = nn_usock_listen (usock, NN_IPC_BACKLOG);
    errnum_assert (rc == 0, -rc);
}

static int nn_ipc_cstream_open (struct nn_usock *usock, struct nn_fsm *owner)
{
    return nn_usock_init (usock, AF_UNIX, SOCK_STREAM, 0, owner);
}

static int nn_ipc_cstream_resolve (const char *addr,
    struct sockaddr_storage *local, socklen_t *locallen,
    struct sockaddr_storage *remote, socklen_t *remotelen)
{
    struct sockaddr_un *un;

    /*  Make sure we're working from a clean slate. Required on Mac OS X. */
    memset (remote, 0, sizeof (struct sockaddr_storage));

    /*  Fill in the address. */
    un = (struct sockaddr_un*) remote;
    if (strlen (addr) >= sizeof (un->sun_path))
        return -ENAMETOOLONG;
    remote->ss_family = AF_UNIX;
    strncpy (un->sun_path, addr, sizeof (un->sun_path));
    *remotelen = sizeof (struct sockaddr_un);

    return 0;
}

#endif

