/* Session and process group manipulation 
   Copyright (C) 1992, 1993, 1994 Free Software Foundation

This file is part of the GNU Hurd.

The GNU Hurd is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

The GNU Hurd is distributed in the hope that it will be useful, 
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with the GNU Hurd; see the file COPYING.  If not, write to
the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.  */

/* Written by Michael I. Bushnell.  */

#include <mach.h>
#include <sys/types.h>
#include <hurd/hurd_types.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <signal.h>

#include "proc.h"
#include "process_S.h"
#include "ourmsg_U.h"


/* Create and return a new process group with pgid PGID in session SESS. */
static inline struct pgrp *
new_pgrp (pid_t pgid,
	  struct session *sess)
{
  struct pgrp *pg;
  
  pg = malloc (sizeof (struct pgrp));
  pg->pg_plist = 0;
  pg->pg_pgid = pgid;
  pg->pg_orphcnt = 0;
  
  pg->pg_session = sess;
  pg->pg_next = sess->s_pgrps;
  if (pg->pg_next)
    pg->pg_next->pg_prevp = &pg->pg_next;
  sess->s_pgrps = pg;
  pg->pg_prevp = &sess->s_pgrps;
  
  add_pgrp_to_hash (pg);
  return pg;
}

/* Create and return a new session with session leader P. */
static inline struct session *
new_session (struct proc *p)
{
  struct session *sess;
  
  sess = malloc (sizeof (struct session));
  sess->s_sid = p->p_pid;
  sess->s_pgrps = 0;
  sess->s_sessionid = MACH_PORT_NULL;

  add_session_to_hash (sess);
  
  return sess;
}

/* Free an empty session */
static inline void
free_session (struct session *s)
{
  if (s->s_sessionid)
    mach_port_mod_refs (mach_task_self (), s->s_sessionid,
			MACH_PORT_RIGHT_RECEIVE, 1);
  remove_session_from_hash (s);
  free (s);
}

/* Free an empty process group. */
static inline void
free_pgrp (struct pgrp *pg)
{
  *pg->pg_prevp = pg->pg_next;
  if (pg->pg_next)
    pg->pg_next->pg_prevp = pg->pg_prevp;
  if (!pg->pg_session->s_pgrps)
    free_session (pg->pg_session);
  remove_pgrp_from_hash (pg);
  free (pg);
}
 
/* Implement proc_setsid as described in <hurd/proc.defs>. */
kern_return_t
S_proc_setsid (struct proc *p)
{
  struct session *sess;
  
  if (p->p_pgrp->pg_pgid == p->p_pid || pgrp_find (p->p_pid))
    return EPERM;
  
  leave_pgrp (p);
  
  sess = new_session (p);
  p->p_pgrp= new_pgrp (p->p_pid, sess);
  join_pgrp (p);

  return 0;
}

/* Used in bootstrapping to set the pgrp of processes 0 and 1. */
void
boot_setsid (struct proc *p)
{
  struct session *sess;
  
  sess = new_session (p);
  p->p_pgrp = new_pgrp (p->p_pid, sess);
  join_pgrp (p);
  return;
}

/* Implement proc_getsid as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getsid (struct proc *callerp,
	     pid_t pid,
	     pid_t *sid)
{
  struct proc *p = pid_find (pid);
  if (!p)
    return ESRCH;

  *sid = p->p_pgrp->pg_session->s_sid;
  return 0;
}

/* Implement proc_getsessionpids as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getsessionpids (struct proc *callerp,
		       pid_t sid,
		       pid_t **pids,
		       u_int *npidsp)
{
  int count;
  struct pgrp *pg;
  struct proc *p;
  struct session *s;
  pid_t *pp = *pids;
  u_int npids = *npidsp;
  
  s = session_find (sid);
  if (!s)
    return ESRCH;
  
  count = 0;
  for (pg = s->s_pgrps; pg; pg = pg->pg_next)
    for (p = pg->pg_plist; p; p = p->p_gnext)
      {
	if (++count <= npids)
	  *pp++ = p->p_pid;
      }
  
  if (count > npids)
    /* They didn't all fit */
    {
      vm_allocate (mach_task_self (), (vm_address_t *)pids, 
		   count * sizeof (pid_t), 1);
      pp = *pids;
      for (pg = s->s_pgrps; pg; pg = pg->pg_next)
	for (p = pg->pg_plist; p; p = p->p_gnext)
	  *pp++ = p->p_pid;
      /* Set dealloc XXX */
    }

  *npidsp = count;
  return 0;
}

/* Implement proc_getsessionpgids as described in <hurd/process.defs>. */
kern_return_t
S_proc_getsessionpgids (struct proc *callerp,
			pid_t sid,
			pid_t **pgids,
			u_int *npgidsp)
{
  int count;
  struct pgrp *pg;
  struct session *s;
  pid_t *pp = *pgids;
  int npgids = *npgidsp;
  
  s = session_find (sid);
  if (!s)
    return ESRCH;
  count = 0;

  for (pg = s->s_pgrps; pg; pg = pg->pg_next)
    if (++count <= npgids)
      *pp++ = pg->pg_pgid;
  
  if (count > npgids)
    /* They didn't all fit. */
    {
      vm_allocate (mach_task_self (), (vm_address_t *)pgids,
		   count * sizeof (pid_t), 1);
      pp = *pgids;
      for (pg = s->s_pgrps; pg; pg = pg->pg_next)
	*pp++ = pg->pg_pgid;
      /* Dealloc ? XXX */
    }
  *npgidsp = count;
  return 0;
}

/* Implement proc_getpgrppids as described in <hurd/process.defs>. */
kern_return_t
S_proc_getpgrppids (struct proc *callerp,
		    pid_t pgid,
		    pid_t **pids,
		    u_int *npidsp)
{
  int count;
  struct proc *p;
  struct pgrp *pg;
  pid_t *pp = *pids;
  int npids = *npidsp;
  
  pg = pgrp_find (pgid);
  if (!pg)
    return ESRCH;

  count = 0;
  for (p = pg->pg_plist; p; p = p->p_gnext)
    if (++count <= npids)
      *pp++ = p->p_pid;
  
  if (count > npids)
    /* They didn't all fit. */
    {
      vm_allocate (mach_task_self (), (vm_address_t *)pids,
		   count * sizeof (pid_t), 1);
      pp = *pids;
      for (p = pg->pg_plist; p; p = p->p_gnext)
	*pp++ = p->p_pid;
      /* Dealloc ? XXX */
    }
  *npidsp = count;
  return 0;
}

/* Implement proc_getsidport as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getsidport (struct proc *p,
		 mach_port_t *sessport)
{
  if (!p->p_pgrp)
    *sessport = MACH_PORT_NULL;
  else
    {
      if (!p->p_pgrp->pg_session->s_sessionid)
	mach_port_allocate (mach_task_self (), MACH_PORT_RIGHT_RECEIVE,
			    &p->p_pgrp->pg_session->s_sessionid);
      *sessport = p->p_pgrp->pg_session->s_sessionid;
    }
  return 0;
}

/* Implement proc_setpgrp as described in <hurd/proc.defs>. */
kern_return_t
S_proc_setpgrp (struct proc *callerp,
	      pid_t pid,
	      pid_t pgid)
{
  struct proc *p;
  struct pgrp *pg;
  
  p = pid ? pid_find (pid) : callerp;

  if (!p || (p != callerp && p->p_parent != callerp))
    return ESRCH;
  
  if (p->p_parent == callerp && p->p_exec)
    return EACCES;
  
  if (!pgid)
    pgid = p->p_pid;
  pg = pgrp_find (pgid);

  if (p->p_pgrp->pg_session->s_sid == p->p_pid
      || p->p_pgrp->pg_session != callerp->p_pgrp->pg_session
      || ((pgid != p->p_pid
	   && (!pg || pg->pg_session != callerp->p_pgrp->pg_session))))
    return EPERM;
  
  if (p->p_pgrp != pg)
    {
      leave_pgrp (p);
      p->p_pgrp = pg ? pg : new_pgrp (pgid, p->p_pgrp->pg_session);
      join_pgrp (p);
    }
  
  return 0;
}

/* Implement proc_getpgrp as described in <hurd/proc.defs>. */
kern_return_t
S_proc_getpgrp (struct proc *callerp,
	      pid_t pid,
	      pid_t *pgid)
{
  struct proc *p = pid_find (pid);
  
  if (!p)
    return ESRCH;
  
  if (p->p_pgrp)
    *pgid = p->p_pgrp->pg_pgid;
  
  return 0;
}

/* Implement proc_mark_exec as described in <hurd/proc.defs>. */
kern_return_t
S_proc_mark_exec (struct proc *p)
{
  p->p_exec = 1;
  return 0;
}

/* Make process P no longer a member of its process group.
   Note that every process is always a member of some process group;
   this must be followed by setting P->p_pgrp and then calling
   join_pgrp. */
void
leave_pgrp (struct proc *p)
{
  struct pgrp *pg = p->p_pgrp;

  *p->p_gprevp = p->p_gnext;
  if (p->p_gnext)
    p->p_gnext->p_gprevp = p->p_gprevp;
  
  /* If we were the last member of our pgrp, free it */
  if (!pg->pg_plist)
    free_pgrp (pg);
  else if (p->p_parent->p_pgrp != pg
	   && p->p_parent->p_pgrp->pg_session == pg->pg_session
	   && !--pg->pg_orphcnt)
    {
      /* We were the last process keeping this from being
	 an orphaned process group -- do the orphaning gook */
      struct proc *ip;
      int dosignal = 0;
      
      for (ip = pg->pg_plist; ip; ip = ip->p_gnext)
	{
	  if (ip->p_stopped)
	    dosignal = 1;
	  nowait_proc_newids (ip->p_msgport, ip->p_task, ip->p_pid,
		       ip->p_parent->p_pid, 1);
	}
      if (dosignal)
	for (ip = pg->pg_plist; ip; ip = ip->p_gnext)
	  {
	    send_signal (ip->p_msgport, SIGHUP, ip->p_task);
	    send_signal (ip->p_msgport, SIGCONT, ip->p_task);
	  }
    }
}

/* Cause process P to join its process group. */
void
join_pgrp (struct proc *p)
{
  struct pgrp *pg = p->p_pgrp;

  p->p_gnext = pg->pg_plist;
  p->p_gprevp = &pg->pg_plist;
  if (pg->pg_plist)
    pg->pg_plist->p_gprevp = &p->p_gnext;
  pg->pg_plist = p;
  
  if (p->p_parent->p_pgrp != pg
      && p->p_parent->p_pgrp->pg_session == pg->pg_session)
    pg->pg_orphcnt++;
}
