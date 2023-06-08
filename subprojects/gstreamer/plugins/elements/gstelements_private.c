/* GStreamer
 * Copyright (C) 2011 David Schleef <ds@schleef.org>
 * Copyright (C) 2011 Tim-Philipp Müller <tim.muller@collabora.co.uk>
 * Copyright (C) 2014 Tim-Philipp Müller <tim@centricular.com>
 * Copyright (C) 2014 Vincent Penquerc'h <vincent@collabora.co.uk>
 *
 * gstelements_private.c: Shared code for core elements
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <stdio.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif
#include <sys/types.h>
#include <errno.h>
#include <string.h>
#include <string.h>
#include "gst/gst.h"
#include "gstelements_private.h"

#ifdef G_OS_WIN32
#  include <io.h>               /* lseek, open, close, read */
#  undef lseek
#  define lseek _lseeki64
#  undef off_t
#  define off_t guint64
#  define WIN32_LEAN_AND_MEAN   /* prevents from including too many things */
#  include <windows.h>
#  undef WIN32_LEAN_AND_MEAN
#  ifndef EWOULDBLOCK
#  define EWOULDBLOCK EAGAIN
#  endif
#endif /* G_OS_WIN32 */

#define BUFFER_FLAG_SHIFT 4

G_STATIC_ASSERT ((1 << BUFFER_FLAG_SHIFT) == GST_MINI_OBJECT_FLAG_LAST);

/* Returns a newly allocated string describing the flags on this buffer */
gchar *
gst_buffer_get_flags_string (GstBuffer * buffer)
{
  static const char flag_strings[] =
      "\000\000\000\000live\000decode-only\000discont\000resync\000corrupted\000"
      "marker\000header\000gap\000droppable\000delta-unit\000tag-memory\000"
      "sync-after\000non-droppable\000FIXME";
  static const guint8 flag_idx[] = { 0, 1, 2, 3, 4, 9, 21, 29, 36, 46, 53,
    60, 64, 74, 85, 96, 107, 121,
  };
  int i, max_bytes;
  char *flag_str, *end;

  /* max size is all flag strings plus a space or terminator after each one */
  max_bytes = sizeof (flag_strings);
  flag_str = g_malloc (max_bytes);

  end = flag_str;
  end[0] = '\0';
  for (i = BUFFER_FLAG_SHIFT; i < G_N_ELEMENTS (flag_idx); i++) {
    if (GST_MINI_OBJECT_CAST (buffer)->flags & (1 << i)) {
      strcpy (end, flag_strings + flag_idx[i]);
      end += strlen (end);
      end[0] = ' ';
      end[1] = '\0';
      end++;
    }
  }

  return flag_str;
}

/* Returns a newly-allocated string describing the metas on this buffer, or NULL */
gchar *
gst_buffer_get_meta_string (GstBuffer * buffer)
{
  gpointer state = NULL;
  GstMeta *meta;
  GString *s = NULL;

  while ((meta = gst_buffer_iterate_meta (buffer, &state))) {
    const gchar *desc = g_type_name (meta->info->type);

    if (s == NULL)
      s = g_string_new (NULL);
    else
      g_string_append (s, ", ");

    g_string_append (s, desc);
  }

  return (s != NULL) ? g_string_free (s, FALSE) : NULL;
}

/* Define our own iovec structure here, so that we can use it unconditionally
 * in the code below and use almost the same code path for systems where
 * writev() is supported and those were it's not supported */
#ifndef HAVE_SYS_UIO_H
struct iovec
{
  gpointer iov_base;
  gsize iov_len;
};
#endif

/* completely arbitrary thresholds */
#define FDSINK_MAX_ALLOCA_SIZE (64 * 1024)      /* 64k */
#define FDSINK_MAX_MALLOC_SIZE ( 8 * 1024 * 1024)       /*  8M */

/* Adapted from GLib (gio/gioprivate.h)
 *
 * POSIX defines IOV_MAX/UIO_MAXIOV as the maximum number of iovecs that can
 * be sent in one go.  We define our own version of it here as there are two
 * possible names, and also define a fall-back value if none of the constants
 * are defined */
#if defined(IOV_MAX)
#define GST_IOV_MAX IOV_MAX
#elif defined(UIO_MAXIOV)
#define GST_IOV_MAX UIO_MAXIOV
#elif defined(__APPLE__)
/* For osx/ios, UIO_MAXIOV is documented in writev(2), but <sys/uio.h>
 * only declares it if defined(KERNEL) */
#define GST_IOV_MAX 512
#else
/* 16 is the minimum value required by POSIX */
#define GST_IOV_MAX 16
#endif

static gssize
gst_writev (gint fd, const struct iovec *iov, gint iovcnt, gsize total_bytes)
{
  gssize written;

#ifdef HAVE_SYS_UIO_H
  if (iovcnt <= GST_IOV_MAX) {
    do {
      written = writev (fd, iov, iovcnt);
    } while (written < 0 && errno == EINTR);
  } else
#endif
  {
    gint i;

    /* We merge the memories here because technically write()/writev() is
     * supposed to be atomic, which it's not if we do multiple separate
     * write() calls. It's very doubtful anyone cares though in our use
     * cases, and it's not clear how that can be reconciled with the
     * possibility of short writes, so in any case we might want to
     * simplify this later or just remove it. */
    if (iovcnt > 1 && total_bytes <= FDSINK_MAX_MALLOC_SIZE) {
      gchar *mem, *p;

      if (total_bytes <= FDSINK_MAX_ALLOCA_SIZE)
        mem = g_alloca (total_bytes);
      else
        mem = g_malloc (total_bytes);

      p = mem;
      for (i = 0; i < iovcnt; ++i) {
        memcpy (p, iov[i].iov_base, iov[i].iov_len);
        p += iov[i].iov_len;
      }

      do {
        written = write (fd, mem, total_bytes);
      } while (written < 0 && errno == EINTR);

      if (total_bytes > FDSINK_MAX_ALLOCA_SIZE)
        g_free (mem);
    } else {
      gssize ret;

      written = 0;
      for (i = 0; i < iovcnt; ++i) {
        do {
          ret = write (fd, iov[i].iov_base, iov[i].iov_len);
        } while (ret < 0 && errno == EINTR);
        if (ret > 0)
          written += ret;
        if (ret != iov[i].iov_len)
          break;
      }
    }
  }

  return written;
}

static GstFlowReturn
gst_writev_iovecs (GstObject * sink, gint fd, GstPoll * fdset,
    struct iovec *vecs, guint n_vecs, gsize bytes_to_write,
    guint64 * bytes_written, gint max_transient_error_timeout,
    guint64 current_position, gboolean * flushing)
{
  GstFlowReturn flow_ret;
  gint64 start_time = 0;

  *bytes_written = 0;
  max_transient_error_timeout *= 1000;
  if (max_transient_error_timeout)
    start_time = g_get_monotonic_time ();

  GST_LOG_OBJECT (sink, "%u iovecs", n_vecs);

  /* now write it all out! */
  {
    gssize ret, left;

    left = bytes_to_write;

    do {
      if (flushing != NULL && g_atomic_int_get (flushing)) {
        GST_DEBUG_OBJECT (sink, "Flushing, exiting loop");
        flow_ret = GST_FLOW_FLUSHING;
        goto out;
      }
#ifndef G_OS_WIN32
      if (fdset != NULL) {
        do {
          GST_DEBUG_OBJECT (sink, "going into select, have %" G_GSSIZE_FORMAT
              " bytes to write", left);
          ret = gst_poll_wait (fdset, GST_CLOCK_TIME_NONE);
        } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

        if (ret == -1) {
          if (errno == EBUSY)
            goto stopped;
          else
            goto select_error;
        }
      }
#endif

      ret = gst_writev (fd, vecs, n_vecs, left);

      if (ret > 0) {
        /* Wrote something, allow the caller to update the vecs passed here */
        *bytes_written = ret;
        break;
      }

      if (errno == EAGAIN || errno == EWOULDBLOCK || ret == 0) {
        /* do nothing, try again */
        if (max_transient_error_timeout)
          start_time = g_get_monotonic_time ();
      } else if (errno == EACCES && max_transient_error_timeout > 0) {
        /* seek back to where we started writing and try again after sleeping
         * for 10ms.
         *
         * Some network file systems report EACCES spuriously, presumably
         * because at the same time another client is reading the file.
         * It happens at least on Linux and macOS on SMB/CIFS and NFS file
         * systems.
         *
         * Note that NFS does not check access permissions during open()
         * but only on write()/read() according to open(2), so we would
         * loop here in case of NFS.
         */
        if (g_get_monotonic_time () > start_time + max_transient_error_timeout) {
          GST_ERROR_OBJECT (sink, "Got EACCES for more than %dms, failing",
              max_transient_error_timeout);
          goto write_error;
        }
        GST_DEBUG_OBJECT (sink, "got EACCES, retry after 10ms sleep");
        g_assert (current_position != -1);
        g_usleep (10000);

        /* Seek back to the current position, sometimes a partial write
         * happened and we have no idea how much and if what was written
         * is actually correct (it sometimes isn't)
         */
        ret = lseek (fd, current_position, SEEK_SET);
        if (ret < 0 || ret != current_position) {
          GST_ERROR_OBJECT (sink,
              "failed to seek back to current write position");
          goto write_error;
        }
      } else {
        goto write_error;
      }
#ifdef G_OS_WIN32
      /* do short sleep on windows where we don't use gst_poll(),
       * to avoid excessive busy looping */
      if (fdset != NULL)
        g_usleep (1000);
#endif
    }
    while (left > 0);
  }

  flow_ret = GST_FLOW_OK;

out:

  return flow_ret;

/* ERRORS */
#ifndef G_OS_WIN32
select_error:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, READ, (NULL),
        ("select on file descriptor: %s", g_strerror (errno)));
    GST_DEBUG_OBJECT (sink, "Error during select: %s", g_strerror (errno));
    flow_ret = GST_FLOW_ERROR;
    goto out;
  }
stopped:
  {
    GST_DEBUG_OBJECT (sink, "Select stopped");
    flow_ret = GST_FLOW_FLUSHING;
    goto out;
  }
#endif
write_error:
  {
    switch (errno) {
      case ENOSPC:
        GST_ELEMENT_ERROR (sink, RESOURCE, NO_SPACE_LEFT, (NULL), (NULL));
        break;
      default:{
        GST_ELEMENT_ERROR (sink, RESOURCE, WRITE, (NULL),
            ("Error while writing to file descriptor %d: %s",
                fd, g_strerror (errno)));
      }
    }
    flow_ret = GST_FLOW_ERROR;
    goto out;
  }
}

GstFlowReturn
gst_writev_buffer (GstObject * sink, gint fd, GstPoll * fdset,
    GstBuffer * buffer,
    guint64 * bytes_written, guint64 skip,
    gint max_transient_error_timeout, guint64 current_position,
    gboolean * flushing)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  struct iovec *vecs;
  GstMapInfo *maps;
  guint i, num_mem, num_vecs;
  gsize left = 0;

  /* Buffers can contain up to 16 memories, so we can safely directly call
   * writev() here without splitting up */
  g_assert (gst_buffer_get_max_memory () <= GST_IOV_MAX);

  num_mem = num_vecs = gst_buffer_n_memory (buffer);

  GST_DEBUG ("Writing buffer %p with %u memories and %" G_GSIZE_FORMAT " bytes",
      buffer, num_mem, gst_buffer_get_size (buffer));

  vecs = g_newa (struct iovec, num_mem);
  maps = g_newa (GstMapInfo, num_mem);

  /* Map all memories */
  {
    GstMemory *mem;
    guint i;

    for (i = 0; i < num_mem; ++i) {
      mem = gst_buffer_peek_memory (buffer, i);
      if (gst_memory_map (mem, &maps[i], GST_MAP_READ)) {
        vecs[i].iov_base = maps[i].data;
        vecs[i].iov_len = maps[i].size;
      } else {
        GST_WARNING ("Failed to map memory %p for reading", mem);
        vecs[i].iov_base = (void *) "";
        vecs[i].iov_len = 0;
      }
      left += vecs[i].iov_len;
    }
  }

  do {
    guint64 bytes_written_local = 0;

    flow_ret =
        gst_writev_iovecs (sink, fd, fdset, vecs, num_vecs, left,
        &bytes_written_local, max_transient_error_timeout, current_position,
        flushing);

    GST_DEBUG ("Wrote %" G_GUINT64_FORMAT " bytes of %" G_GSIZE_FORMAT ": %s",
        bytes_written_local, left, gst_flow_get_name (flow_ret));

    if (flow_ret != GST_FLOW_OK) {
      g_assert (bytes_written_local == 0);
      break;
    }

    if (bytes_written)
      *bytes_written += bytes_written_local;

    /* Done, no need to do bookkeeping */
    if (bytes_written_local == left)
      break;

    /* skip vectors that have been written in full */
    while (bytes_written_local >= vecs[0].iov_len) {
      bytes_written_local -= vecs[0].iov_len;
      left -= vecs[0].iov_len;
      ++vecs;
      --num_vecs;
    }
    g_assert (num_vecs > 0);
    /* skip partially written vector data */
    if (bytes_written_local > 0) {
      vecs[0].iov_len -= bytes_written_local;
      vecs[0].iov_base = ((guint8 *) vecs[0].iov_base) + bytes_written_local;
      left -= bytes_written_local;
    }
  } while (left > 0);

  for (i = 0; i < num_mem; i++)
    gst_memory_unmap (maps[i].memory, &maps[i]);

  return flow_ret;
}

GstFlowReturn
gst_writev_mem (GstObject * sink, gint fd, GstPoll * fdset,
    const guint8 * data, guint size,
    guint64 * bytes_written, guint64 skip,
    gint max_transient_error_timeout, guint64 current_position,
    gboolean * flushing)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  struct iovec vec;
  gsize left;

  GST_DEBUG ("Writing memory %p with %u bytes", data, size);

  vec.iov_len = size;
  vec.iov_base = (guint8 *) data;
  left = size;

  do {
    guint64 bytes_written_local = 0;

    flow_ret =
        gst_writev_iovecs (sink, fd, fdset, &vec, 1, left,
        &bytes_written_local, max_transient_error_timeout, current_position,
        flushing);

    GST_DEBUG ("Wrote %" G_GUINT64_FORMAT " bytes of %" G_GSIZE_FORMAT ": %s",
        bytes_written_local, left, gst_flow_get_name (flow_ret));

    if (flow_ret != GST_FLOW_OK) {
      g_assert (bytes_written_local == 0);
      break;
    }

    if (bytes_written)
      *bytes_written += bytes_written_local;

    /* All done, no need for bookkeeping */
    if (bytes_written_local == left)
      break;

    /* skip partially written vector data */
    if (bytes_written_local < left) {
      vec.iov_len -= bytes_written_local;
      vec.iov_base = ((guint8 *) vec.iov_base) + bytes_written_local;
      left -= bytes_written_local;
    }
  } while (left > 0);

  return flow_ret;
}

GstFlowReturn
gst_writev_buffer_list (GstObject * sink, gint fd, GstPoll * fdset,
    GstBufferList * buffer_list,
    guint64 * bytes_written, guint64 skip,
    gint max_transient_error_timeout, guint64 current_position,
    gboolean * flushing)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  struct iovec *vecs;
  GstMapInfo *maps;
  guint num_bufs, current_buf_idx = 0, current_buf_mem_idx = 0;
  guint i, num_vecs;
  gsize left = 0;

  num_bufs = gst_buffer_list_length (buffer_list);
  num_vecs = 0;

  GST_DEBUG ("Writing buffer list %p with %u buffers", buffer_list, num_bufs);

  vecs = g_newa (struct iovec, GST_IOV_MAX);
  maps = g_newa (GstMapInfo, GST_IOV_MAX);

  /* Map the first GST_IOV_MAX memories */
  {
    GstBuffer *buf;
    GstMemory *mem;
    guint j = 0;

    for (i = 0; i < num_bufs && num_vecs < GST_IOV_MAX; i++) {
      guint num_mem;

      buf = gst_buffer_list_get (buffer_list, i);
      num_mem = gst_buffer_n_memory (buf);

      for (j = 0; j < num_mem && num_vecs < GST_IOV_MAX; j++) {
        mem = gst_buffer_peek_memory (buf, j);
        if (gst_memory_map (mem, &maps[num_vecs], GST_MAP_READ)) {
          vecs[num_vecs].iov_base = maps[num_vecs].data;
          vecs[num_vecs].iov_len = maps[num_vecs].size;
        } else {
          GST_WARNING ("Failed to map memory %p for reading", mem);
          vecs[num_vecs].iov_base = (void *) "";
          vecs[num_vecs].iov_len = 0;
        }
        left += vecs[num_vecs].iov_len;
        num_vecs++;
      }
      current_buf_mem_idx = j;
      if (j == num_mem)
        current_buf_mem_idx = 0;
    }
    current_buf_idx = i;
    if (current_buf_mem_idx != 0) {
      g_assert (current_buf_idx > 0);
      current_buf_idx--;
    }
  }

  do {
    guint64 bytes_written_local = 0;
    guint vecs_written = 0;

    flow_ret =
        gst_writev_iovecs (sink, fd, fdset, vecs, num_vecs, left,
        &bytes_written_local, max_transient_error_timeout, current_position,
        flushing);

    GST_DEBUG ("Wrote %" G_GUINT64_FORMAT " bytes of %" G_GSIZE_FORMAT ": %s",
        bytes_written_local, left, gst_flow_get_name (flow_ret));

    if (flow_ret != GST_FLOW_OK) {
      g_assert (bytes_written_local == 0);
      break;
    }

    if (flow_ret != GST_FLOW_OK) {
      g_assert (bytes_written_local == 0);
      break;
    }

    if (bytes_written)
      *bytes_written += bytes_written_local;

    /* All done, no need for bookkeeping */
    if (bytes_written_local == left && current_buf_idx == num_bufs)
      break;

    /* skip vectors that have been written in full */
    while (vecs_written < num_vecs
        && bytes_written_local >= vecs[vecs_written].iov_len) {
      bytes_written_local -= vecs[vecs_written].iov_len;
      left -= vecs[vecs_written].iov_len;
      vecs_written++;
    }
    g_assert (vecs_written < num_vecs || bytes_written_local == 0);
    /* skip partially written vector data */
    if (bytes_written_local > 0) {
      vecs[vecs_written].iov_len -= bytes_written_local;
      vecs[vecs_written].iov_base =
          ((guint8 *) vecs[0].iov_base) + bytes_written_local;
      left -= bytes_written_local;
    }

    /* If we have buffers left, fill them in now */
    if (current_buf_idx < num_bufs) {
      GstBuffer *buf;
      GstMemory *mem;
      guint j = current_buf_mem_idx;

      /* Unmap the first vecs_written memories now */
      for (i = 0; i < vecs_written; i++)
        gst_memory_unmap (maps[i].memory, &maps[i]);
      /* Move upper remaining vecs and maps back to the beginning */
      memmove (vecs, &vecs[vecs_written],
          (num_vecs - vecs_written) * sizeof (vecs[0]));
      memmove (maps, &maps[vecs_written],
          (num_vecs - vecs_written) * sizeof (maps[0]));
      num_vecs -= vecs_written;

      /* And finally refill */
      for (i = current_buf_idx; i < num_bufs && num_vecs < GST_IOV_MAX; i++) {
        guint num_mem;

        buf = gst_buffer_list_get (buffer_list, i);
        num_mem = gst_buffer_n_memory (buf);

        for (j = current_buf_mem_idx; j < num_mem && num_vecs < GST_IOV_MAX;
            j++) {
          mem = gst_buffer_peek_memory (buf, j);
          if (gst_memory_map (mem, &maps[num_vecs], GST_MAP_READ)) {
            vecs[num_vecs].iov_base = maps[num_vecs].data;
            vecs[num_vecs].iov_len = maps[num_vecs].size;
          } else {
            GST_WARNING ("Failed to map memory %p for reading", mem);
            vecs[num_vecs].iov_base = (void *) "";
            vecs[num_vecs].iov_len = 0;
          }
          left += vecs[num_vecs].iov_len;
          num_vecs++;
        }
        current_buf_mem_idx = j;
        if (current_buf_mem_idx == num_mem)
          current_buf_mem_idx = 0;
      }
      current_buf_idx = i;
      if (current_buf_mem_idx != 0) {
        g_assert (current_buf_idx > 0);
        current_buf_idx--;
      }
    }
  } while (left > 0);

  for (i = 0; i < num_vecs; i++)
    gst_memory_unmap (maps[i].memory, &maps[i]);

  return flow_ret;
}
