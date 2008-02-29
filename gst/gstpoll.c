/* GStreamer
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2004 Wim Taymans <wim.taymans@gmail.com>
 * Copyright (C) 2007 Peter Kjellerstedt <pkj@axis.com>
 *
 * gstpoll.c: File descriptor set
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
/**
 * SECTION:gstpoll
 * @short_description: Keep track of file descriptors and make it possible
 *                     to wait on them in a cancelable way
 *
 * A #GstPoll keeps track of file descriptors much like fd_set (used with
 * select()) or a struct pollfd array (used with poll()). Once created with
 * gst_poll_new(), the set can be used to wait for file descriptors to be
 * readable and/or writeable. It is possible to make this wait be controlled
 * by specifying %TRUE for the @controllable flag when creating the set (or
 * later calling gst_poll_set_controllable()).
 *
 * New file descriptors are added to the set using gst_poll_add_fd(), and
 * removed using gst_poll_remove_fd(). Controlling which file descriptors
 * should be waited for to become readable and/or writeable are done using
 * gst_poll_fd_ctl_read() and gst_poll_fd_ctl_write().
 *
 * Use gst_poll_wait() to wait for the file descriptors to actually become
 * readable and/or writeable, or to timeout if no file descriptor is available
 * in time. The wait can be controlled by calling gst_poll_restart() and
 * gst_poll_set_flushing().
 *
 * Once the file descriptor set has been waited for, one can use
 * gst_poll_fd_has_closed() to see if the file descriptor has been closed,
 * gst_poll_fd_has_error() to see if it has generated an error,
 * gst_poll_fd_can_read() to see if it is possible to read from the file
 * descriptor, and gst_poll_fd_can_write() to see if it is possible to
 * write to it.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define _GNU_SOURCE 1
#include <sys/poll.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>

#ifdef G_OS_WIN32
#include <winsock2.h>
#define EINPROGRESS WSAEINPROGRESS
#else
#include <sys/socket.h>
#endif

/* OS/X needs this because of bad headers */
#include <string.h>

#include "gst_private.h"

#include "gstpoll.h"

/* the poll/select call is also performed on a control socket, that way
 * we can send special commands to control it
 */
#define SEND_COMMAND(set, command)                   \
G_STMT_START {                                       \
  unsigned char c = command;                         \
  write (set->control_write_fd.fd, &c, 1);           \
} G_STMT_END

#define READ_COMMAND(set, command, res)              \
G_STMT_START {                                       \
  res = read (set->control_read_fd.fd, &command, 1); \
} G_STMT_END

#define GST_POLL_CMD_WAKEUP  'W'        /* restart the poll/select call */

#ifdef G_OS_WIN32
#define CLOSE_SOCKET(sock) closesocket (sock)
#else
#define CLOSE_SOCKET(sock) close (sock)
#endif

struct _GstPoll
{
  GstPollMode mode;

  GMutex *lock;

  GArray *fds;
  GArray *active_fds;
  gboolean controllable;
  gboolean new_controllable;
  gboolean waiting;
  gboolean flushing;

  GstPollFD control_read_fd;
  GstPollFD control_write_fd;
};

static gint
find_index (GArray * array, GstPollFD * fd)
{
  struct pollfd *pfd;
  guint i;

  /* start by assuming the index found in the fd is still valid */
  if (fd->idx >= 0 && fd->idx < array->len) {
    pfd = &g_array_index (array, struct pollfd, fd->idx);

    if (pfd->fd == fd->fd) {
      return fd->idx;
    }
  }

  /* the pollfd array has changed and we need to lookup the fd again */
  for (i = 0; i < array->len; i++) {
    pfd = &g_array_index (array, struct pollfd, i);

    if (pfd->fd == fd->fd) {
      fd->idx = (gint) i;
      return fd->idx;
    }
  }

  fd->idx = -1;
  return fd->idx;
}

#if !defined(HAVE_PPOLL) && defined(HAVE_POLL)
/* check if all file descriptors will fit in an fd_set */
static gboolean
selectable_fds (const GstPoll * set)
{
  guint i;

  for (i = 0; i < set->fds->len; i++) {
    struct pollfd *pfd = &g_array_index (set->fds, struct pollfd, i);

    if (pfd->fd >= FD_SETSIZE)
      return FALSE;
  }

  return TRUE;
}

/* check if the timeout will convert to a timeout value used for poll()
 * without a loss of precision
 */
static gboolean
pollable_timeout (GstClockTime timeout)
{
  if (timeout == GST_CLOCK_TIME_NONE)
    return TRUE;

  /* not a nice multiple of milliseconds */
  if (timeout % 1000000)
    return FALSE;

  return TRUE;
}
#endif

static GstPollMode
choose_mode (const GstPoll * set, GstClockTime timeout)
{
  GstPollMode mode;

  if (set->mode == GST_POLL_MODE_AUTO) {
#ifdef HAVE_PPOLL
    mode = GST_POLL_MODE_PPOLL;
#elif defined(HAVE_POLL)
    if (!selectable_fds (set) || pollable_timeout (timeout)) {
      mode = GST_POLL_MODE_POLL;
    } else {
#ifdef HAVE_PSELECT
      mode = GST_POLL_MODE_PSELECT;
#else
      mode = GST_POLL_MODE_SELECT;
#endif
    }
#elif defined(HAVE_PSELECT)
    mode = GST_POLL_MODE_PSELECT;
#else
    mode = GST_POLL_MODE_SELECT;
#endif
  } else {
    mode = set->mode;
  }
  return mode;
}

static gint
pollfd_to_fd_set (GstPoll * set, fd_set * readfds, fd_set * writefds)
{
  gint max_fd = -1;
  guint i;

  FD_ZERO (readfds);
  FD_ZERO (writefds);

  g_mutex_lock (set->lock);

  for (i = 0; i < set->active_fds->len; i++) {
    struct pollfd *pfd = &g_array_index (set->fds, struct pollfd, i);

    if (pfd->fd < FD_SETSIZE) {
      if (pfd->events & POLLIN)
        FD_SET (pfd->fd, readfds);
      if (pfd->events & POLLOUT)
        FD_SET (pfd->fd, writefds);
      if (pfd->fd > max_fd)
        max_fd = pfd->fd;
    }
  }

  g_mutex_unlock (set->lock);

  return max_fd;
}

static void
fd_set_to_pollfd (GstPoll * set, fd_set * readfds, fd_set * writefds)
{
  guint i;

  g_mutex_lock (set->lock);

  for (i = 0; i < set->active_fds->len; i++) {
    struct pollfd *pfd = &g_array_index (set->active_fds, struct pollfd, i);

    if (pfd->fd < FD_SETSIZE) {
      if (FD_ISSET (pfd->fd, readfds))
        pfd->revents |= POLLIN;
      if (FD_ISSET (pfd->fd, writefds))
        pfd->revents |= POLLOUT;
    }
  }

  g_mutex_unlock (set->lock);
}

/**
 * gst_poll_new:
 * @mode: the mode of the file descriptor set.
 * @controllable: whether it should be possible to control a wait.
 *
 * Create a new file descriptor set with the given @mode. If @controllable, it
 * is possible to restart or flush a call to gst_poll_wait() with
 * gst_poll_restart() and gst_poll_set_flushing() respectively.
 *
 * Returns: a new #GstPoll, or %NULL in case of an error. Free with
 * gst_poll_free().
 *
 * Since: 0.10.18
 */
GstPoll *
gst_poll_new (GstPollMode mode, gboolean controllable)
{
  GstPoll *nset;

  nset = g_new0 (GstPoll, 1);
  nset->mode = mode;
  nset->lock = g_mutex_new ();
  nset->fds = g_array_new (FALSE, FALSE, sizeof (struct pollfd));
  nset->active_fds = g_array_new (FALSE, FALSE, sizeof (struct pollfd));
  nset->control_read_fd.fd = -1;
  nset->control_write_fd.fd = -1;

  if (!gst_poll_set_controllable (nset, controllable))
    goto not_controllable;

  return nset;

  /* ERRORS */
not_controllable:
  {
    gst_poll_free (nset);
    return NULL;
  }
}

/**
 * gst_poll_free:
 * @set: a file descriptor set.
 *
 * Free a file descriptor set.
 *
 * Since: 0.10.18
 */
void
gst_poll_free (GstPoll * set)
{
  g_return_if_fail (set != NULL);

  if (set->control_write_fd.fd >= 0)
    CLOSE_SOCKET (set->control_write_fd.fd);
  if (set->control_read_fd.fd >= 0)
    CLOSE_SOCKET (set->control_read_fd.fd);

  g_array_free (set->active_fds, TRUE);
  g_array_free (set->fds, TRUE);
  g_mutex_free (set->lock);
  g_free (set);
}

/**
 * gst_poll_set_mode:
 * @set: a file descriptor set.
 * @mode: the mode of the file descriptor set.
 *
 * Set the mode to use to determine how to wait for the file descriptor set.
 *
 * Since: 0.10.18
 */
void
gst_poll_set_mode (GstPoll * set, GstPollMode mode)
{
  g_return_if_fail (set != NULL);

  g_mutex_lock (set->lock);
  set->mode = mode;
  g_mutex_unlock (set->lock);
}

/**
 * gst_poll_get_mode:
 * @set: a file descriptor set.
 *
 * Get the mode used to determine how to wait for the file descriptor set.
 *
 * Returns: the currently used mode.
 *
 * Since: 0.10.18
 */
GstPollMode
gst_poll_get_mode (const GstPoll * set)
{
  GstPollMode mode;

  g_return_val_if_fail (set != NULL, GST_POLL_MODE_AUTO);

  g_mutex_lock (set->lock);
  mode = set->mode;
  g_mutex_unlock (set->lock);

  return mode;
}

/**
 * gst_poll_fd_init:
 * @fd: a #GstPollFD
 *
 * Initializes @fd. Alternatively you can initialize it with
 * #GST_POLL_FD_INIT.
 */
void
gst_poll_fd_init (GstPollFD * fd)
{
  g_return_if_fail (fd != NULL);

  fd->fd = -1;
  fd->idx = -1;
}

static gboolean
gst_poll_add_fd_unlocked (GstPoll * set, GstPollFD * fd)
{
  gint idx;

  idx = find_index (set->fds, fd);
  if (idx < 0) {
    struct pollfd nfd;

    nfd.fd = fd->fd;
    nfd.events = POLLERR | POLLNVAL | POLLHUP;
    nfd.revents = 0;

    g_array_append_val (set->fds, nfd);
    fd->idx = set->fds->len - 1;
  }

  return TRUE;
}

/**
 * gst_poll_add_fd:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 *
 * Add a file descriptor to the file descriptor set.
 *
 * Returns: %TRUE if the file descriptor was successfully added to the set.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_add_fd (GstPoll * set, GstPollFD * fd)
{
  gboolean ret;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  ret = gst_poll_add_fd_unlocked (set, fd);

  g_mutex_unlock (set->lock);

  return ret;
}

/**
 * gst_poll_remove_fd:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 *
 * Remove a file descriptor from the file descriptor set.
 *
 * Returns: %TRUE if the file descriptor was successfully removed from the set.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_remove_fd (GstPoll * set, GstPollFD * fd)
{
  gint idx;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  /* get the index, -1 is an fd that is not added */
  idx = find_index (set->fds, fd);
  if (idx >= 0) {
    /* remove the fd at index, we use _remove_index_fast, which copies the last
     * element of the array to the freed index */
    g_array_remove_index_fast (set->fds, idx);

    /* mark fd as removed by setting the index to -1 */
    fd->idx = -1;
  }

  g_mutex_unlock (set->lock);

  return idx >= 0;
}

/**
 * gst_poll_fd_ctl_write:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 * @active: a new status.
 *
 * Control whether the descriptor @fd in @set will be monitored for
 * writability.
 *
 * Returns: %TRUE if the descriptor was successfully updated.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_fd_ctl_write (GstPoll * set, GstPollFD * fd, gboolean active)
{
  gint idx;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  idx = find_index (set->fds, fd);
  if (idx >= 0) {
    struct pollfd *pfd = &g_array_index (set->fds, struct pollfd, idx);

    if (active)
      pfd->events |= POLLOUT;
    else
      pfd->events &= ~POLLOUT;
  }

  g_mutex_unlock (set->lock);

  return idx >= 0;
}

static gboolean
gst_poll_fd_ctl_read_unlocked (GstPoll * set, GstPollFD * fd, gboolean active)
{
  gint idx;

  idx = find_index (set->fds, fd);
  if (idx >= 0) {
    struct pollfd *pfd = &g_array_index (set->fds, struct pollfd, idx);

    if (active)
      pfd->events |= (POLLIN | POLLPRI);
    else
      pfd->events &= ~(POLLIN | POLLPRI);
  }

  return idx >= 0;
}

/**
 * gst_poll_fd_ctl_read:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 * @active: a new status.
 *
 * Control whether the descriptor @fd in @set will be monitored for
 * readability.
 *
 * Returns: %TRUE if the descriptor was successfully updated.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_fd_ctl_read (GstPoll * set, GstPollFD * fd, gboolean active)
{
  gboolean ret;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  ret = gst_poll_fd_ctl_read_unlocked (set, fd, active);

  g_mutex_unlock (set->lock);

  return ret;
}

/**
 * gst_poll_fd_has_closed:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 *
 * Check if @fd in @set has closed the connection.
 *
 * Returns: %TRUE if the connection was closed.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_fd_has_closed (const GstPoll * set, GstPollFD * fd)
{
  gboolean res = FALSE;
  gint idx;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  idx = find_index (set->active_fds, fd);
  if (idx >= 0) {
    struct pollfd *pfd = &g_array_index (set->active_fds, struct pollfd, idx);

    res = (pfd->revents & POLLHUP) != 0;
  }

  g_mutex_unlock (set->lock);

  return res;
}

/**
 * gst_poll_fd_has_error:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 *
 * Check if @fd in @set has an error.
 *
 * Returns: %TRUE if the descriptor has an error.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_fd_has_error (const GstPoll * set, GstPollFD * fd)
{
  gboolean res = FALSE;
  gint idx;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  idx = find_index (set->active_fds, fd);
  if (idx >= 0) {
    struct pollfd *pfd = &g_array_index (set->active_fds, struct pollfd, idx);

    res = (pfd->revents & (POLLERR | POLLNVAL)) != 0;
  }

  g_mutex_unlock (set->lock);

  return res;
}

static gboolean
gst_poll_fd_can_read_unlocked (const GstPoll * set, GstPollFD * fd)
{
  gboolean res = FALSE;
  gint idx;

  idx = find_index (set->active_fds, fd);
  if (idx >= 0) {
    struct pollfd *pfd = &g_array_index (set->active_fds, struct pollfd, idx);

    res = (pfd->revents & (POLLIN | POLLPRI)) != 0;
  }

  return res;
}

/**
 * gst_poll_fd_can_read:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 *
 * Check if @fd in @set has data to be read.
 *
 * Returns: %TRUE if the descriptor has data to be read.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_fd_can_read (const GstPoll * set, GstPollFD * fd)
{
  gboolean res = FALSE;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  res = gst_poll_fd_can_read_unlocked (set, fd);

  g_mutex_unlock (set->lock);

  return res;
}

/**
 * gst_poll_fd_can_write:
 * @set: a file descriptor set.
 * @fd: a file descriptor.
 *
 * Check if @fd in @set can be used for writing.
 *
 * Returns: %TRUE if the descriptor can be used for writing.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_fd_can_write (const GstPoll * set, GstPollFD * fd)
{
  gboolean res = FALSE;
  gint idx;

  g_return_val_if_fail (set != NULL, FALSE);
  g_return_val_if_fail (fd != NULL, FALSE);
  g_return_val_if_fail (fd->fd >= 0, FALSE);

  g_mutex_lock (set->lock);

  idx = find_index (set->active_fds, fd);
  if (idx >= 0) {
    struct pollfd *pfd = &g_array_index (set->active_fds, struct pollfd, idx);

    res = (pfd->revents & POLLOUT) != 0;
  }

  g_mutex_unlock (set->lock);

  return res;
}

/**
 * gst_poll_wait:
 * @set: a #GstPoll.
 * @timeout: a timeout in nanoseconds.
 *
 * Wait for activity on the file descriptors in @set. This function waits up to
 * the specified @timeout.  A timeout of #GST_CLOCK_TIME_NONE waits forever.
 *
 * When this function is called from multiple threads, -1 will be returned with
 * errno set to EPERM.
 *
 * Returns: The number of #GstPollFD in @set that have activity or 0 when no
 * activity was detected after @timeout. If an error occurs, -1 is returned
 * and errno is set.
 *
 * Since: 0.10.18
 */
gint
gst_poll_wait (GstPoll * set, GstClockTime timeout)
{
  gboolean restarting;
  int res;

  g_return_val_if_fail (set != NULL, -1);

  g_mutex_lock (set->lock);

  /* we cannot wait from multiple threads */
  if (set->waiting)
    goto already_waiting;

  /* flushing, exit immediatly */
  if (set->flushing)
    goto flushing;

  set->waiting = TRUE;

  do {
    GstPollMode mode;

    res = -1;
    restarting = FALSE;

    mode = choose_mode (set, timeout);

    g_array_set_size (set->active_fds, set->fds->len);
    memcpy (set->active_fds->data, set->fds->data,
        set->fds->len * sizeof (struct pollfd));
    g_mutex_unlock (set->lock);

    switch (mode) {
      case GST_POLL_MODE_AUTO:
        g_assert_not_reached ();
        break;
      case GST_POLL_MODE_PPOLL:
      {
#ifdef HAVE_PPOLL
        struct timespec ts;
        struct timespec *tsptr;

        if (timeout != GST_CLOCK_TIME_NONE) {
          GST_TIME_TO_TIMESPEC (timeout, ts);
          tsptr = &ts;
        } else {
          tsptr = NULL;
        }

        res =
            ppoll ((struct pollfd *) set->active_fds->data,
            set->active_fds->len, tsptr, NULL);
#else
        g_assert_not_reached ();
        errno = ENOSYS;
#endif
        break;
      }
      case GST_POLL_MODE_POLL:
      {
#ifdef HAVE_POLL
        gint t;

        if (timeout != GST_CLOCK_TIME_NONE) {
          t = GST_TIME_AS_MSECONDS (timeout);
        } else {
          t = -1;
        }

        res =
            poll ((struct pollfd *) set->active_fds->data,
            set->active_fds->len, t);
#else
        g_assert_not_reached ();
        errno = ENOSYS;
#endif
        break;
      }
      case GST_POLL_MODE_PSELECT:
#ifndef HAVE_PSELECT
      {
        g_assert_not_reached ();
        errno = ENOSYS;
        break;
      }
#endif
      case GST_POLL_MODE_SELECT:
      {
        fd_set readfds;
        fd_set writefds;
        gint max_fd;

        max_fd = pollfd_to_fd_set (set, &readfds, &writefds);

        if (mode == GST_POLL_MODE_SELECT) {
          struct timeval tv;
          struct timeval *tvptr;

          if (timeout != GST_CLOCK_TIME_NONE) {
            GST_TIME_TO_TIMEVAL (timeout, tv);
            tvptr = &tv;
          } else {
            tvptr = NULL;
          }

          res = select (max_fd + 1, &readfds, &writefds, NULL, tvptr);
        } else {
#ifdef HAVE_PSELECT
          struct timespec ts;
          struct timespec *tsptr;

          if (timeout != GST_CLOCK_TIME_NONE) {
            GST_TIME_TO_TIMESPEC (timeout, ts);
            tsptr = &ts;
          } else {
            tsptr = NULL;
          }

          res = pselect (max_fd + 1, &readfds, &writefds, NULL, tsptr, NULL);
#endif
        }

        if (res > 0) {
          fd_set_to_pollfd (set, &readfds, &writefds);
        }

        break;
      }
    }

    g_mutex_lock (set->lock);

    /* check if the poll/select was aborted due to a command */
    if (res > 0 && set->controllable) {
      while (TRUE) {
        guchar cmd;
        gint result;

        /* we do not check the read status of the control socket here because
         * there may have been a write to the socket between the time the
         * poll/select finished and before we got the mutex back, and we need
         * to clear out the control socket before leaving */
        READ_COMMAND (set, cmd, result);
        if (result <= 0) {
          /* no more commands, quit the loop */
          break;
        }

        /* if the control socket is the only socket with activity when we get
         * here, we restart the _wait operation, else we allow the caller to
         * process the other file descriptors */
        if (res == 1 &&
            gst_poll_fd_can_read_unlocked (set, &set->control_read_fd))
          restarting = TRUE;
      }
    }

    /* update the controllable state if needed */
    set->controllable = set->new_controllable;

    if (set->flushing) {
      /* we got woken up and we are flushing, we need to stop */
      errno = EBUSY;
      res = -1;
      break;
    }
  } while (restarting);

  set->waiting = FALSE;

  g_mutex_unlock (set->lock);

  return res;

  /* ERRORS */
already_waiting:
  {
    g_mutex_unlock (set->lock);
    errno = EPERM;
    return -1;
  }
flushing:
  {
    g_mutex_unlock (set->lock);
    errno = EBUSY;
    return -1;
  }
}

/**
 * gst_poll_set_controllable:
 * @set: a #GstPoll.
 * @controllable: new controllable state.
 *
 * When @controllable is %TRUE, this function ensures that future calls to
 * gst_poll_wait() will be affected by gst_poll_restart() and
 * gst_poll_set_flushing().
 *
 * Returns: %TRUE if the controllability of @set could be updated.
 *
 * Since: 0.10.18
 */
gboolean
gst_poll_set_controllable (GstPoll * set, gboolean controllable)
{
  g_return_val_if_fail (set != NULL, FALSE);

  g_mutex_lock (set->lock);

  if (controllable && set->control_read_fd.fd < 0) {
    gint control_sock[2];

#ifdef G_OS_WIN32
    gulong flags = 1;

    if (_pipe (control_sock, 4096, _O_BINARY) < 0)
      goto no_socket_pair;

    ioctlsocket (control_sock[0], FIONBIO, &flags);
    ioctlsocket (control_sock[1], FIONBIO, &flags);
#else
    if (socketpair (PF_UNIX, SOCK_STREAM, 0, control_sock) < 0)
      goto no_socket_pair;

    fcntl (control_sock[0], F_SETFL, O_NONBLOCK);
    fcntl (control_sock[1], F_SETFL, O_NONBLOCK);
#endif
    set->control_read_fd.fd = control_sock[0];
    set->control_write_fd.fd = control_sock[1];

    gst_poll_add_fd_unlocked (set, &set->control_read_fd);
  }

  if (set->control_read_fd.fd >= 0)
    gst_poll_fd_ctl_read_unlocked (set, &set->control_read_fd, controllable);

  /* delay the change of the controllable state if we are waiting */
  set->new_controllable = controllable;
  if (!set->waiting)
    set->controllable = controllable;

  g_mutex_unlock (set->lock);

  return TRUE;

  /* ERRORS */
no_socket_pair:
  {
    g_mutex_unlock (set->lock);
    return FALSE;
  }
}

/**
 * gst_poll_restart:
 * @set: a #GstPoll.
 *
 * Restart any gst_poll_wait() that is in progress. This function is typically
 * used after adding or removing descriptors to @set.
 *
 * If @set is not controllable, then this call will have no effect.
 *
 * Since: 0.10.18
 */
void
gst_poll_restart (GstPoll * set)
{
  g_return_if_fail (set != NULL);

  g_mutex_lock (set->lock);

  if (set->controllable && set->waiting) {
    /* if we are waiting, we can send the command, else we do not have to
     * bother, future calls will automatically pick up the new fdset */
    SEND_COMMAND (set, GST_POLL_CMD_WAKEUP);
  }

  g_mutex_unlock (set->lock);
}

/**
 * gst_poll_set_flushing:
 * @set: a #GstPoll.
 * @flushing: new flushing state.
 *
 * When @flushing is %TRUE, this function ensures that current and future calls
 * to gst_poll_wait() will return -1, with errno set to EBUSY.
 *
 * Unsetting the flushing state will restore normal operation of @set.
 *
 * Since: 0.10.18
 */
void
gst_poll_set_flushing (GstPoll * set, gboolean flushing)
{
  g_return_if_fail (set != NULL);

  g_mutex_lock (set->lock);

  /* update the new state first */
  set->flushing = flushing;

  if (flushing && set->controllable && set->waiting) {
    /* we are flushing, controllable and waiting, wake up the waiter */
    SEND_COMMAND (set, GST_POLL_CMD_WAKEUP);
  }

  g_mutex_unlock (set->lock);
}
