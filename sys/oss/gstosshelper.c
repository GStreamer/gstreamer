/* Evil evil evil hack to get OSS apps to cooperate with esd
 * Copyright (C) 1998, 1999 Manish Singh <yosh@gimp.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

//#define DSP_DEBUG 

/* This lets you run multiple instances of x11amp by setting the X11AMPNUM
   environment variable. Only works on glibc2.
 */
/* #define MULTIPLE_X11AMP */

#if defined(__GNUC__) && !defined(__STRICT_ANSI__)

#ifdef DSP_DEBUG
#define DPRINTF(format, args...)	printf(format, ## args)
#else
#define DPRINTF(format, args...)
#endif


#include "config.h"

#include <dlfcn.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>

#include <errno.h>

#ifdef HAVE_MACHINE_SOUNDCARD_H
#  include <machine/soundcard.h>
#else
#  ifdef HAVE_SOUNDCARD_H
#    include <soundcard.h>
#  else
#    include <sys/soundcard.h>
#  endif
#endif

#include "gstosshelper.h"

/* BSDI has this functionality, but not define :() */
#if defined(RTLD_NEXT)
#define REAL_LIBC RTLD_NEXT
#else
#define REAL_LIBC ((void *) -1L)
#endif

#if defined(__FreeBSD__) || defined(__bsdi__)
typedef unsigned long request_t;
#else
typedef int request_t;
#endif

static int sndfd = -1;
static int new_format = 1;
static int fmt = AFMT_S16_LE;
static int speed = 44100;
static int stereo = 1;

int
open (const char *pathname, int flags, ...)
{
  static int (*func) (const char *, int, mode_t) = NULL;
  va_list args;
  mode_t mode;

  if (!func)
    func = (int (*) (const char *, int, mode_t)) dlsym (REAL_LIBC, "open");

  va_start (args, flags);
  mode = va_arg (args, mode_t);
  va_end (args);

  if (!strcmp (pathname, "/dev/dsp")) {
      DPRINTF ("hijacking /dev/dsp open, and taking it to GStreamer...\n");
      return (sndfd = HELPER_MAGIC_SNDFD);
  }
  return (sndfd = (*func) (pathname, flags, mode));
}

static int
dspctl (int fd, request_t request, void *argp)
{
  int *arg = (int *) argp;

  DPRINTF ("hijacking /dev/dsp ioctl, and sending it to GStreamer "
	   "(%d : %x - %p)\n", fd, request, argp);
  
  switch (request)
  {
    case SNDCTL_DSP_RESET:
    case SNDCTL_DSP_POST:
      break;

    case SNDCTL_DSP_SETFMT:
      fmt = *arg;
      new_format = 1;
      break;

    case SNDCTL_DSP_SPEED:
      speed = *arg;
      new_format = 1;
      break;

    case SNDCTL_DSP_STEREO:
      stereo = *arg;
      new_format = 1;
      break;

    case SNDCTL_DSP_GETBLKSIZE:
      *arg = 4096;
      break;

    case SNDCTL_DSP_GETFMTS:
      *arg = 0x38;
      break;

#ifdef SNDCTL_DSP_GETCAPS
    case SNDCTL_DSP_GETCAPS:
      *arg = 0;
      break;
#endif

    case SNDCTL_DSP_GETOSPACE:
      {
	audio_buf_info *bufinfo = (audio_buf_info *) argp;
	bufinfo->bytes = 4096;
      }
      break;


    default:
      DPRINTF ("unhandled /dev/dsp ioctl (%x - %p)\n", request, argp);
      break;
  }

  return 0;
}

void *
mmap(void *start, size_t length, int prot , int flags, int fd, off_t offset)
{
  static void * (*func) (void *, size_t, int, int, int, off_t) = NULL;

  if (!func)
    func = (void * (*) (void *, size_t, int, int, int, off_t)) dlsym (REAL_LIBC, "mmap");

  if ((fd == sndfd) && (sndfd != -1))
  {
    DPRINTF("MMAP: oops... we're in trouble here. /dev/dsp mmap()ed. Not supported yet.\n");
    errno = EACCES;
    return (void *)-1; /* Better causing an error than silently not working, in this case */
  }

  return (*func) (start, length, prot, flags, fd, offset);
}

ssize_t
write (int fd, const void *buf, size_t len)
{
  static int (*func) (int, const void *, size_t) = NULL;
  command cmd;

  if (!func)
    func = (int (*) (int, const void *, size_t)) dlsym (REAL_LIBC, "write");

  if ((fd != sndfd) || (sndfd == -1))
  {
    return (*func) (fd, buf, len);
  }

  DPRINTF("WRITE: called for %d bytes\n", len);

  if (new_format) {
    new_format = 0;

    cmd.id = CMD_FORMAT;
    cmd.cmd.format.format = fmt;
    cmd.cmd.format.stereo = stereo;
    cmd.cmd.format.rate = speed;

    (*func) (HELPER_MAGIC_OUT, &cmd, sizeof(command));
  }
  cmd.id = CMD_DATA;
  cmd.cmd.length = len;

  (*func) (HELPER_MAGIC_OUT, &cmd, sizeof(command));
  (*func) (HELPER_MAGIC_OUT, buf, len);

  //return (*func) (fd, buf, len);

  return len;
}

int
select (int n, fd_set *readfds, fd_set *writefds,
		        fd_set *exceptfds, struct timeval *timeout)
{
  static int (*func) (int, fd_set *, fd_set *, fd_set *, struct timeval *) = NULL;

  if (!func)
    func = (int (*) (int, fd_set *, fd_set *, fd_set *, struct timeval *)) dlsym (REAL_LIBC, "select");

  if (n == sndfd) {
    DPRINTF ("audiooss: hijacking /dev/dsp select() [output]\n");
  }

  return (*func) (n, readfds, writefds, exceptfds, timeout);
}

int
dup2 (int oldfd, int newfd)
{
  static int (*func) (int, int) = NULL;

  if (!func)
    func = (int (*) (int, int)) dlsym (REAL_LIBC, "dup2");

  if ((oldfd == sndfd) && (oldfd != -1) && (newfd != -1))
  {
    DPRINTF("dup2(%d,%d) (oldfd == sndfd) called\n", oldfd, newfd);

    /* Do not close(newfd) as that would mark it available for reuse by the system -
     *        just tell the program that yes, we got the fd you asked for. Hackish. */
    sndfd = newfd;
    return newfd;
  }
  return (*func) (oldfd, newfd);
}

int
ioctl (int fd, request_t request, ...)
{
  static int (*func) (int, request_t, void *) = NULL;
  va_list args;
  void *argp;

  if (!func)                                                                    
    func = (int (*) (int, request_t, void *)) dlsym (REAL_LIBC, "ioctl");             

  va_start (args, request);
  argp = va_arg (args, void *);
  va_end (args);

  if (fd == sndfd)
    return dspctl (fd, request, argp);

  return (*func) (fd, request, argp); 
}

int
fcntl(int fd, int cmd, ...)
{
  static int (*func) (int, int, void *) = NULL;
  va_list args;
  void *argp;

  if (!func)
    func = (int (*) (int, int, void *)) dlsym (REAL_LIBC, "fcntl");

  va_start (args, cmd);
  argp = va_arg (args, void *);
  va_end (args);

  if ((fd != -1) && (fd == sndfd))
  {
    DPRINTF ("hijacking /dev/dsp fcntl() "
             "(%d : %x - %p)\n", fd, cmd, argp);
    if (cmd == F_GETFL) return O_RDWR;
    if (cmd == F_GETFD) return sndfd;
    return 0;
  }
  else
  {
    return (*func) (fd, cmd, argp);
  }
  return 0;
}

int
close (int fd)
{
  static int (*func) (int) = NULL;

  if (!func)
    func = (int (*) (int)) dlsym (REAL_LIBC, "close");

  if (fd == sndfd)
    sndfd = -1;
 
  return (*func) (fd);
}

#ifdef MULTIPLE_X11AMP

#include <socketbits.h>
#include <sys/param.h>
#include <sys/un.h>

#define ENVSET "X11AMPNUM"

int
unlink (const char *filename)
{
  static int (*func) (const char *) = NULL;
  char *num;

  if (!func)
    func = (int (*) (const char *)) dlsym (REAL_LIBC, "unlink");

  if (!strcmp (filename, "/tmp/X11Amp_CTRL") && (num = getenv (ENVSET)))
    {
      char buf[PATH_MAX] = "/tmp/X11Amp_CTRL";
      strcat (buf, num);
      return (*func) (buf); 
    }
  else
    return (*func) (filename);
}

typedef int (*sa_func_t) (int, struct sockaddr *, int);

static int
sockaddr_mangle (sa_func_t func, int fd, struct sockaddr *addr, int len)
{
  char *num;

  if (!strcmp (((struct sockaddr_un *) addr)->sun_path, "/tmp/X11Amp_CTRL")
      && (num = getenv(ENVSET)))
    {
      int ret;
      char buf[PATH_MAX] = "/tmp/X11Amp_CTRL";

      struct sockaddr_un *new_addr = malloc (len);

      strcat (buf, num);
      memcpy (new_addr, addr, len);
      strcpy (new_addr->sun_path, buf);

      ret = (*func) (fd, (struct sockaddr *) new_addr, len);

      free (new_addr);
      return ret;
    } 
  else
    return (*func) (fd, addr, len);
}

int
bind (int fd, struct sockaddr *addr, int len)
{
  static sa_func_t func = NULL;

  if (!func)
    func = (sa_func_t) dlsym (REAL_LIBC, "bind");
  return sockaddr_mangle (func, fd, addr, len);
}

int
connect (int fd, struct sockaddr *addr, int len)
{
  static sa_func_t func = NULL;

  if (!func)
    func = (sa_func_t) dlsym (REAL_LIBC, "connect");
  return sockaddr_mangle (func, fd, addr, len);
}

#endif /* MULTIPLE_X11AMP */

#else /* __GNUC__ */
static char *ident = NULL;

void
nogcc (void)
{
  ident = NULL;
}

#endif /* __GNUC__ */
