/* Internal sockets

   Copyright (C) 1995 Free Software Foundation, Inc.

   Written by Miles Bader <miles@gnu.ai.mit.edu>

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#ifndef __SOCK_H__
#define __SOCK_H__

#include <hurd/ports.h>
#include <cthreads.h>		/* For mutexes */

struct pipe;

/* A port on SOCK.  Multiple sock_user's can point to the same socket.  */ 
struct sock_user
{
  struct port_info pi;
  struct sock *sock;
};

/* An endpoint for a possible I/O stream.  */
struct sock
{
  int refs;
  struct mutex lock;

  /* Reads from this socket come from READ_PIPE, writes go to WRITE_PIPE.
     A sock always has a read pipe, and a write pipe when it's connected to
     another socket.  */
  struct pipe *read_pipe, *write_pipe;

  /* FLAGS from SOCK_*, below.  */
  unsigned flags;

  /* An identifying number for the socket.  */
  unsigned id;
  /* Last time the socket got frobbed.  */
  time_value_t change_time;

  /* This socket's local address.  Note that we don't hold any references on
     ADDR, and depend on the addr zeroing our pointer if it goes away (which
     is ok, as we can then just make up another address if necessary, and no
     one could tell anyway).  */
  struct addr *addr;

  /* If this sock has been connected to another sock, then WRITE_ADDR is the
     addr of that sock.  We *do* hold a reference to this addr.  */
  struct addr *write_addr;

  /* A connection queue to listen for incoming connections on.  Once a socket
     has one of these, it always does, and can never again be used for
     anything but accepting incoming connections.  */
  struct connq *connq;
};

/* Socket flags */
#define SOCK_CONNECTED		0x1 /* A connected connection-oriented sock. */
#define SOCK_NONBLOCK		0x2 /* Don't block on I/O.  */
#define SOCK_SHUTDOWN_READ	0x4 /* The read-half has been shutdown.  */
#define SOCK_SHUTDOWN_WRITE	0x8 /* The write-half has been shutdown.  */

/* Returns the pipe that SOCK is reading from in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  NULL may
   also be returned in PIPE with a 0 error, meaning that EOF should be
   returned.  SOCK mustn't be locked.  */
error_t sock_aquire_read_pipe (struct sock *sock, struct pipe **pipe);

/* Returns the pipe that SOCK is writing to in PIPE, locked and with an
   additional reference, or an error saying why it's not possible.  SOCK
   mustn't be locked.  */
error_t sock_aquire_write_pipe (struct sock *sock, struct pipe **pipe);

/* Connect together the previously unconnected sockets SOCK1 and SOCK2.  */
error_t sock_connect (struct sock *sock1, struct sock *sock2)

/* Return a new socket with the given pipe class in SOCK.  */
error_t sock_create (struct pipe_class *pipe_class, struct sock **sock);

/* Return a new socket just like TEMPLATE in SOCK.  */
error_t sock_create (struct sock *template, struct sock **sock);

/* Return a new user port on SOCK in PORT.  */
error_t sock_create_port (struct sock *sock, mach_port_t *port);

/* Bind SOCK to ADDR.  */
error_t sock_bind (struct sock *sock, struct addr *addr);

/* Returns SOCK's address in ADDR, with an additional reference added.  If
   SOCK doesn't currently have an address, one is fabricated first.  */
error_t sock_get_addr (struct sock *sock, struct addr *addr);

/* Returns a send right to SOCK's address in ADDR_PORT.  If SOCK doesn't
   currently have an address, one is fabricated first.  */
error_t sock_get_addr_port (struct sock *sock, mach_port_t *addr_port);

/* If SOCK is a connected socket, returns a send right to SOCK's peer's
   address in ADDR_PORT.  */
error_t sock_get_write_addr_port (struct sock *sock, mach_port_t *addr_port);

#endif /* __SOCK_H__ */
