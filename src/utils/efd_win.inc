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

#define NN_EFD_PORT 5907

#include "err.h"
#include "fast.h"

#include <string.h>
#include <stdint.h>

int nn_efd_init (struct nn_efd *self)
{
    SECURITY_ATTRIBUTES sa = {0};
    SECURITY_DESCRIPTOR sd;
    BOOL brc;
    HANDLE sync;
    DWORD dwrc;
    SOCKET listener;
    int rc;
    struct sockaddr_in addr;
    int addrlen;
    BOOL reuseaddr;
    BOOL nodelay;

    /*  Make the following critical section accessible to everyone. */
    sa.nLength = sizeof (sa);
    sa.bInheritHandle = FALSE;
    brc = InitializeSecurityDescriptor (&sd, SECURITY_DESCRIPTOR_REVISION);
    win_assert (brc);
    brc = SetSecurityDescriptorDacl(&sd, TRUE, (PACL) NULL, FALSE);
    win_assert (brc);
    sa.lpSecurityDescriptor = &sd;

    /*  This function has to be enclosed in a system-wide critical section
        so that two instances of the library don't accidentally create an efd
        crossing the process boundary. 
        CAUTION: This critical section has machine-wide scope. Thus, it must
        be properly exited even before crashing the process by an assertion. */
    sync = CreateEvent (&sa, FALSE, TRUE, "Global\\nanomsg-port-sync");
    win_assert (sync != NULL);

    /*  Enter the critical section. */
    dwrc = WaitForSingleObject (sync, INFINITE);
    nn_assert (dwrc == WAIT_OBJECT_0);

    /*  Unfortunately, on Windows the only way to send signal to a file
        descriptor (SOCKET) is to create a full-blown TCP connecting on top of
        the loopback interface. */
    self->w = INVALID_SOCKET;
    self->r = INVALID_SOCKET;

    /*  Create listening socket. */
    listener = socket (AF_INET, SOCK_STREAM, 0);
    if (nn_slow (listener == SOCKET_ERROR))
        goto wsafail;
    brc = SetHandleInformation ((HANDLE) listener, HANDLE_FLAG_INHERIT, 0);
    win_assert (brc);

    /*  This prevents subsequent attempts to create a signaler to fail bacause
        of "TCP port in use" problem. */
    reuseaddr = 1;
    rc = setsockopt (listener, SOL_SOCKET, SO_REUSEADDR,
        (char*) &reuseaddr, sizeof (reuseaddr));
    if (nn_slow (rc == SOCKET_ERROR))
        goto wsafail;

    /*  Bind the listening socket to the local port. */
    memset (&addr, 0, sizeof (addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
    addr.sin_port = htons (NN_EFD_PORT);
    rc = bind (listener, (const struct sockaddr*) &addr, sizeof (addr));
    if (nn_slow (rc == SOCKET_ERROR))
        goto wsafail;

    /*  Start listening for the incomming connections. In normal case we are
        going to accept just a single connection, so backlog buffer of size
        1 is sufficient. */
    rc = listen (listener, 1);
    if (nn_slow (rc == SOCKET_ERROR))
        goto wsafail;

    /*  Create the writer socket. */
    self->w = socket (AF_INET, SOCK_STREAM, 0);
    if (nn_slow (listener == SOCKET_ERROR))
        goto wsafail;
    brc = SetHandleInformation ((HANDLE) self->w, HANDLE_FLAG_INHERIT, 0);
    win_assert (brc);

    /*  Set TCP_NODELAY on the writer socket to make efd as fast as possible.
        There's only one byte going to be written, so batching would not make
        sense anyway. */
    nodelay = 1;
    rc = setsockopt (self->w, IPPROTO_TCP, TCP_NODELAY, (char*) &nodelay,
        sizeof (nodelay));
    if (nn_slow (rc == SOCKET_ERROR))
        goto wsafail;

    /*  Connect the writer socket to the listener socket. */
    rc = connect (self->w, (struct sockaddr*) &addr, sizeof (addr));
    if (nn_slow (rc == SOCKET_ERROR))
        goto wsafail;

    while (1) {

        /*  Accept new incoming connection. */
        addrlen = sizeof (addr);
        self->r = accept (listener, (struct sockaddr*) &addr, &addrlen);
        if (nn_slow (self->r == INVALID_SOCKET || addrlen != sizeof (addr)))
            goto wsafail;

        /*  Check that the connection actually comes from the localhost. */
        if (nn_fast (addr.sin_addr.s_addr == htonl (INADDR_LOOPBACK)))
            break;

        /*  If not so, close the connection and try again. */
        rc = closesocket (self->r);
        if (nn_slow (rc == INVALID_SOCKET))
            goto wsafail;
    }

    /*  Listener socket can be closed now as no more connections for this efd
        are going to be established anyway. */
    rc = closesocket (listener);
    if (nn_slow (rc == INVALID_SOCKET))
        goto wsafail;

    /*  Leave the critical section. */
    brc = SetEvent (sync);
    win_assert (brc != 0);
    brc = CloseHandle (sync);
    win_assert (brc != 0);

    /* Initialise the pre-allocated pollset. */
    FD_ZERO (&self->fds);

    return 0;

wsafail:
    rc = nn_err_wsa_to_posix (WSAGetLastError ());
    brc = SetEvent (sync);
    win_assert (brc != 0);
    brc = CloseHandle (sync);
    win_assert (brc != 0);
    errnum_assert (0, rc);
}

void nn_efd_term (struct nn_efd *self)
{
    int rc;

    rc = closesocket (self->w);
    wsa_assert (rc != INVALID_SOCKET);
    rc = closesocket (self->r);
    wsa_assert (rc != INVALID_SOCKET);
}

nn_fd nn_efd_getfd (struct nn_efd *self)
{
    return self->r;
}

void nn_efd_signal (struct nn_efd *self)
{
    int rc;
    unsigned char c = 0xec;

    rc = send (self->w, (char*) &c, 1, 0);
    wsa_assert (rc != SOCKET_ERROR);
    nn_assert (rc == 1);
}

void nn_efd_unsignal (struct nn_efd *self)
{
    int rc;
    uint8_t buf [16];

    while (1) {
        rc = recv (self->r, (char*) buf, sizeof (buf), 0);
        wsa_assert (rc != SOCKET_ERROR);
        if (nn_fast (rc < sizeof (buf)))
            break;
    }
}

