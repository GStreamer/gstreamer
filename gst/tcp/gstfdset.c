/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Wim Taymans <wim@fluendo.com>
 *
 * gsttcpfdset.h: fdset datastructure
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

#define MIN_POLLFDS	4096
#define INIT_POLLFDS	MIN_POLLFDS

#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
/* OS/X needs this because of bad headers */
#include <string.h>

#include "gstfdset.h"

GType
gst_fdset_mode_get_type (void)
{
  static GType fdset_mode_type = 0;
  static GEnumValue fdset_mode[] = {
    {GST_FDSET_MODE_SELECT, "GST_FDSET_MODE_SELECT", "Select"},
    {GST_FDSET_MODE_POLL, "GST_FDSET_MODE_POLL", "Poll"},
    {GST_FDSET_MODE_EPOLL, "GST_FDSET_MODE_EPOLL", "EPoll"},
    {0, NULL, NULL},
  };

  if (!fdset_mode_type) {
    fdset_mode_type = g_enum_register_static ("GstFDSetModeType", fdset_mode);
  }
  return fdset_mode_type;
}

struct _GstFDSet
{
  GstFDSetMode mode;

  /* for poll */
  struct pollfd *testpollfds;
  gint last_testpollfds;
  gint testsize;

  struct pollfd *pollfds;
  gint size;
  gint free;
  gint last_pollfds;
  GMutex *poll_lock;

  /* for select */
  fd_set readfds, writefds;     /* input */
  fd_set testreadfds, testwritefds;     /* output */
};

static gint
nearest_pow (gint num)
{
  gint n = 1;

  while (n < num)
    n <<= 1;

  return n;
}

static void
ensure_size (GstFDSet * set, gint len)
{
  guint need = len * sizeof (struct pollfd);

  if (need > set->size) {
    need = nearest_pow (need);
    need = MAX (need, MIN_POLLFDS * sizeof (struct pollfd));

    set->pollfds = g_realloc (set->pollfds, need);

    set->size = need;
  }
}

GstFDSet *
gst_fdset_new (GstFDSetMode mode)
{
  GstFDSet *nset;

  nset = g_new0 (GstFDSet, 1);
  nset->mode = mode;

  switch (mode) {
    case GST_FDSET_MODE_SELECT:
      FD_ZERO (&nset->readfds);
      FD_ZERO (&nset->writefds);
      break;
    case GST_FDSET_MODE_POLL:
      nset->pollfds = NULL;
      nset->testpollfds = NULL;
      nset->free = 0;
      nset->last_pollfds = 0;
      nset->poll_lock = g_mutex_new ();
      ensure_size (nset, MIN_POLLFDS);
      break;
    case GST_FDSET_MODE_EPOLL:
      g_warning ("implement me");
      break;
    default:
      break;
  }
  return nset;
}

void
gst_fdset_free (GstFDSet * set)
{
  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      break;
    case GST_FDSET_MODE_POLL:
      g_free (set->pollfds);
      break;
    case GST_FDSET_MODE_EPOLL:
      g_warning ("implement me");
      break;
    default:
      break;
  }
  g_free (set);
}


void
gst_fdset_set_mode (GstFDSet * set, GstFDSetMode mode)
{
  g_warning ("implement me");
}

GstFDSetMode
gst_fdset_get_mode (GstFDSet * set)
{
  return set->mode;
}

void
gst_fdset_add_fd (GstFDSet * set, GstFD * fd)
{
  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      /* nothing */
      break;
    case GST_FDSET_MODE_POLL:
    {
      struct pollfd *nfd;
      gint idx;

      g_mutex_lock (set->poll_lock);

      ensure_size (set, set->last_pollfds + 1);

      idx = set->free;
      if (idx == -1) {
        /* find free space */
        while (idx < set->last_pollfds) {
          idx++;
          if (set->pollfds[idx].fd == -1)
            break;
        }
      }
      nfd = &set->pollfds[idx];

      nfd->fd = fd->fd;
      nfd->events = POLLERR | POLLNVAL | POLLHUP;
      nfd->revents = 0;

      /* see if we have one fd more */
      set->last_pollfds = MAX (idx + 1, set->last_pollfds);
      fd->idx = idx;
      set->free = -1;
      g_mutex_unlock (set->poll_lock);

      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
}

void
gst_fdset_remove_fd (GstFDSet * set, GstFD * fd)
{
  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      /* nothing */
      FD_CLR (fd->fd, &set->writefds);
      FD_CLR (fd->fd, &set->readfds);
      break;
    case GST_FDSET_MODE_POLL:
    {
      g_mutex_lock (set->poll_lock);

      set->pollfds[fd->idx].fd = -1;
      set->pollfds[fd->idx].events = 0;
      set->pollfds[fd->idx].revents = 0;

      /* if we removed the last fd, we can lower the last_pollfds */
      if (fd->idx + 1 == set->last_pollfds) {
        set->last_pollfds--;
      }
      fd->idx = -1;

      if (set->free == -1) {
        set->free = fd->idx;
      } else {
        set->free = MIN (set->free, fd->idx);
      }
      g_mutex_unlock (set->poll_lock);
      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
}

void
gst_fdset_fd_ctl_write (GstFDSet * set, GstFD * fd, gboolean active)
{
  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      if (active)
        FD_SET (fd->fd, &set->writefds);
      else
        FD_CLR (fd->fd, &set->writefds);
      break;
    case GST_FDSET_MODE_POLL:
    {
      gint events = set->pollfds[fd->idx].events;

      if (active)
        events |= POLLOUT;
      else
        events &= ~POLLOUT;

      set->pollfds[fd->idx].events = events;
      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
}

void
gst_fdset_fd_ctl_read (GstFDSet * set, GstFD * fd, gboolean active)
{
  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      if (active)
        FD_SET (fd->fd, &set->readfds);
      else
        FD_CLR (fd->fd, &set->readfds);
      break;
    case GST_FDSET_MODE_POLL:
    {
      gint events = set->pollfds[fd->idx].events;

      if (active)
        events |= (POLLIN | POLLPRI);
      else
        events &= ~(POLLIN | POLLPRI);

      set->pollfds[fd->idx].events = events;
      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
}

gboolean
gst_fdset_fd_has_closed (GstFDSet * set, GstFD * fd)
{
  gboolean res = FALSE;

  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      res = FALSE;
      break;
    case GST_FDSET_MODE_POLL:
    {
      gint idx = fd->idx;

      g_mutex_lock (set->poll_lock);
      if (idx >= 0 && idx < set->last_testpollfds)
        res = (set->testpollfds[idx].revents & POLLHUP) != 0;
      g_mutex_unlock (set->poll_lock);
      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
  return res;
}

gboolean
gst_fdset_fd_has_error (GstFDSet * set, GstFD * fd)
{
  gboolean res = FALSE;

  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      res = FALSE;
      break;
    case GST_FDSET_MODE_POLL:
    {
      gint idx = fd->idx;

      g_mutex_lock (set->poll_lock);
      if (idx >= 0 && idx < set->last_testpollfds)
        res = (set->testpollfds[idx].revents & (POLLERR | POLLNVAL)) != 0;
      g_mutex_unlock (set->poll_lock);
      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
  return res;
}

gboolean
gst_fdset_fd_can_read (GstFDSet * set, GstFD * fd)
{
  gboolean res = FALSE;

  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      res = FD_ISSET (fd->fd, &set->testreadfds);
      break;
    case GST_FDSET_MODE_POLL:
    {
      gint idx = fd->idx;

      g_mutex_lock (set->poll_lock);
      if (idx >= 0 && idx < set->last_testpollfds)
        res = (set->testpollfds[idx].revents & (POLLIN | POLLPRI)) != 0;
      g_mutex_unlock (set->poll_lock);
      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
  return res;
}

gboolean
gst_fdset_fd_can_write (GstFDSet * set, GstFD * fd)
{
  gboolean res = FALSE;

  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
      res = FD_ISSET (fd->fd, &set->testwritefds);
      break;
    case GST_FDSET_MODE_POLL:
    {
      gint idx = fd->idx;

      g_mutex_lock (set->poll_lock);
      if (idx >= 0 && idx < set->last_testpollfds)
        res = (set->testpollfds[idx].revents & POLLOUT) != 0;
      g_mutex_unlock (set->poll_lock);
      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }
  return res;
}

int
gst_fdset_wait (GstFDSet * set, int timeout)
{
  int res = -1;

  switch (set->mode) {
    case GST_FDSET_MODE_SELECT:
    {
      struct timeval tv;
      struct timeval *tvptr = NULL;

      set->testreadfds = set->readfds;
      set->testwritefds = set->writefds;

      if (timeout > 0) {
        tv.tv_sec = timeout / 1000;
        tv.tv_usec = timeout % 1000;

        tvptr = &tv;
      }
      res =
          select (FD_SETSIZE, &set->testreadfds, &set->testwritefds,
          (fd_set *) 0, tvptr);
      break;
    }
    case GST_FDSET_MODE_POLL:
    {
      g_mutex_lock (set->poll_lock);
      if (set->testsize != set->size) {
        set->testpollfds = g_realloc (set->testpollfds, set->size);
        set->testsize = set->size;
      }
      set->last_testpollfds = set->last_pollfds;
      memcpy (set->testpollfds, set->pollfds,
          sizeof (struct pollfd) * set->last_testpollfds);
      g_mutex_unlock (set->poll_lock);

      res = poll (set->testpollfds, set->last_testpollfds, timeout);

      break;
    }
    case GST_FDSET_MODE_EPOLL:
      break;
  }

  return res;
}
