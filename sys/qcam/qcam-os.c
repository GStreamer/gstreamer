/* qcam-Linux.c -- Linux-specific routines for accessing QuickCam */

/* Version 0.1, January 2, 1996 */
/* Version 0.5, August 24, 1996 */


/******************************************************************

Copyright (C) 1996 by Scott Laird

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL SCOTT LAIRD BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

******************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#ifdef TESTING
#include <errno.h>
#endif
#include <sys/io.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "qcam.h"
#include "qcam-Linux.h"

int __inline__
read_lpstatus (const struct qcam *q)
{
  return inb (q->port + 1);
}

int
read_lpcontrol (const struct qcam *q)
{
  return inb (q->port + 2);
}

int
read_lpdata (const struct qcam *q)
{
  return inb (q->port);
}

void
write_lpdata (const struct qcam *q, int d)
{
  outb (d, q->port);
}

void
write_lpcontrol (const struct qcam *q, int d)
{
  outb (d, q->port + 2);
}

int
enable_ports (const struct qcam *q)
{
  if (q->port < 0x278)
    return 1;			/* Better safe than sorry */
  if (q->port > 0x3bc)
    return 1;
  return (ioperm (q->port, 3, 1));
}

int
disable_ports (const struct qcam *q)
{
  return (ioperm (q->port, 3, 0));
}

/* Lock port.  This is currently sub-optimal, and is begging to be
   fixed.  It should check for dead locks.  Any takers? */

/* qc_lock_wait
 * This function uses POSIX fcntl-style locking on a file created in the
 * /tmp directory.  Because it uses the Unix record locking facility, locks
 * are relinquished automatically on process termination, so "dead locks"
 * are not a problem.  (FYI, the lock file will remain after process
 * termination, but this is actually desired so that the next process need
 * not re-creat(2)e it... just lock it.)
 * The wait argument indicates whether or not this funciton should "block"
 * waiting for the previous lock to be relinquished.  This is ideal so that
 * multiple processes (eg. qcam) taking "snapshots" can peacefully coexist.
 * - Dave Plonka (plonka@carroll1.cc.edu)
 */
int
qc_lock_wait (struct qcam *q, int wait)
{
#if 1
  static struct flock sfl;

  if (-1 == q->fd) {		/* we've yet to open the lock file */
    static char lockfile[128];

    sprintf (lockfile, "/var/run/LOCK.qcam.0x%x", q->port);
    if (-1 == (q->fd = open (lockfile, O_WRONLY | O_CREAT, 0666))) {
      perror ("open");
      return 1;
    }
#ifdef TESTING
    fprintf (stderr, "%s - %d: %s open(2)ed\n", __FILE__, __LINE__, lockfile);
#endif

    /* initialize the l_type memver to lock the file exclusively */
    sfl.l_type = F_WRLCK;
  }
#ifdef TESTING
  if (0 != fcntl (q->fd, F_SETLK, &sfl))	/* non-blocking set lock */
#else
  if (0 != fcntl (q->fd, wait ? F_SETLKW : F_SETLK, &sfl))
#endif
  {
#ifdef TESTING
    perror ("fcntl");
    if (EAGAIN != errno || !wait)
      return 1;

    fprintf (stderr, "%s - %d: waiting for exclusive lock on fd %d...\n",
	__FILE__, __LINE__, q->fd);

    if (0 != fcntl (q->fd, F_SETLKW, &sfl))	/* "blocking" set lock */
#endif
    {
      perror ("fcntl");
      return 1;
    }
  }
#ifdef TESTING
  fprintf (stderr, "%s - %d: fd %d locked exclusively\n", __FILE__, __LINE__,
      q->fd);
#endif

#else
  char lockfile[128], tmp[128];
  struct stat statbuf;

  sprintf (lockfile, "/var/run/LOCK.qcam.0x%x", q->port);
  sprintf (tmp, "%s-%d", lockfile, getpid ());

  if ((creat (tmp, 0) == -1) ||
      (link (tmp, lockfile) == -1) ||
      (stat (tmp, &statbuf) == -1) || (statbuf.st_nlink == 1)) {
#ifdef DEBUGQC
    perror ("QuickCam Locked");
    if (unlink (tmp) == -1)
      perror ("Error unlinking temp file.");
#else
    unlink (tmp);
#endif
    return 1;
  }

  unlink (tmp);
  if (chown (lockfile, getuid (), getgid ()) == -1)
    perror ("Chown problems");
#endif

  return 0;
}

int
qc_lock (struct qcam *q)
{
#if 1
  return qc_lock_wait (q, 1 /*wait */ );
#else
  return qc_lock_wait (q, 0 /*don't wait */ );
#endif
}

/* Unlock port */

int
qc_unlock (struct qcam *q)
{
  static struct flock sfl;

#if 1
  if (-1 == q->fd) {		/* port was not locked */
    return 1;
  }

  /* clear the exclusive lock */
  sfl.l_type = F_UNLCK;
  if (0 != fcntl (q->fd, F_SETLK, &sfl)) {
    perror ("fcntl");
    return 1;
  }
#ifdef TESTING
  fprintf (stderr, "%s - %d: fd %d unlocked\n", __FILE__, __LINE__, q->fd);
#endif

#else
  char lockfile[128];

  sprintf (lockfile, "/var/run/LOCK.qcam.0x%x", q->port);
  unlink (lockfile);		/* What would I do with an error? */
#endif

  return 0;
}


/* Probe for camera.  Returns 0 if found, 1 if not found, sets
   q->port.*/

int
qc_probe (struct qcam *q)
{
  int ioports[] = { 0x378, 0x278, 0x3bc, 0 };
  int i = 0;

  /* Attempt to get permission to access IO ports.  Must be root */

  while (ioports[i] != 0) {
    q->port = ioports[i++];

    if (qc_open (q)) {
      perror ("Can't get I/O permission");
      exit (1);
    }

    if (qc_detect (q)) {
      fprintf (stderr, "QuickCam detected at 0x%x\n", q->port);
      qc_close (q);
      return (0);
    } else
      qc_close (q);
  }

  return 1;
}


/* THIS IS UGLY.  I need a short delay loop -- somthing well under a
millisecond.  Unfortunately, adding 2 usleep(1)'s to qc_command slowed
it down by a factor of over 1000 over the same loop with 2
usleep(0)'s, and that's too slow -- qc_start was taking over a second
to run.  This seems to help, but if anyone has a good
speed-independent pause routine, please tell me. -- Scott */

void
qc_wait (int val)
{
  int i;

  while (val--)
    for (i = 0; i < 50000; i++);
}
