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
#include <errno.h>
#include <string.h>
#include <string.h>
#include "gst/gst.h"
#include "gstelements_private.h"

#ifdef G_OS_WIN32
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
      "FIXME";
  static const guint8 flag_idx[] = { 0, 1, 2, 3, 4, 9, 21, 29, 36, 46, 53,
    60, 64, 74, 85, 96
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

/* UIO_MAXIOV is documented in writev(2), but <sys/uio.h> only
 * declares it on osx/ios if defined(KERNEL) */
#ifndef UIO_MAXIOV
#define UIO_MAXIOV 512
#endif

static gssize
gst_writev (gint fd, const struct iovec *iov, gint iovcnt, gsize total_bytes)
{
  gssize written;

#ifdef HAVE_SYS_UIO_H
  if (iovcnt <= UIO_MAXIOV) {
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
    if (total_bytes <= FDSINK_MAX_MALLOC_SIZE) {
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

static gsize
fill_vectors (struct iovec *vecs, GstMapInfo * maps, guint n, GstBuffer * buf)
{
  GstMemory *mem;
  gsize size = 0;
  guint i;

  g_assert (gst_buffer_n_memory (buf) == n);

  for (i = 0; i < n; ++i) {
    mem = gst_buffer_peek_memory (buf, i);
    if (gst_memory_map (mem, &maps[i], GST_MAP_READ)) {
      vecs[i].iov_base = maps[i].data;
      vecs[i].iov_len = maps[i].size;
    } else {
      GST_WARNING ("Failed to map memory %p for reading", mem);
      vecs[i].iov_base = (void *) "";
      vecs[i].iov_len = 0;
    }
    size += vecs[i].iov_len;
  }

  return size;
}

GstFlowReturn
gst_writev_buffers (GstObject * sink, gint fd, GstPoll * fdset,
    GstBuffer ** buffers, guint num_buffers, guint8 * mem_nums,
    guint total_mem_num, guint64 * bytes_written, guint64 skip)
{
  struct iovec *vecs;
  GstMapInfo *map_infos;
  GstFlowReturn flow_ret;
  gsize size = 0;
  guint i, j;

  GST_LOG_OBJECT (sink, "%u buffers, %u memories", num_buffers, total_mem_num);

  vecs = g_newa (struct iovec, total_mem_num);
  map_infos = g_newa (GstMapInfo, total_mem_num);

  /* populate output vectors */
  for (i = 0, j = 0; i < num_buffers; ++i) {
    size += fill_vectors (&vecs[j], &map_infos[j], mem_nums[i], buffers[i]);
    j += mem_nums[i];
  }

  /* now write it all out! */
  {
    gssize ret, left;
    guint n_vecs = total_mem_num;

    left = size;

    if (skip) {
      ret = skip;
      errno = 0;
      goto skip_first;
    }

    do {
#ifndef HAVE_WIN32
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
        if (bytes_written)
          *bytes_written += ret;
      }

    skip_first:

      if (ret == left)
        break;

      if (ret < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        /* do nothing, try again */
      } else if (ret < 0) {
        goto write_error;
      } else if (ret < left) {
        /* skip vectors that have been written in full */
        while (ret >= vecs[0].iov_len) {
          ret -= vecs[0].iov_len;
          left -= vecs[0].iov_len;
          ++vecs;
          --n_vecs;
        }
        g_assert (n_vecs > 0);
        /* skip partially written vector data */
        if (ret > 0) {
          vecs[0].iov_len -= ret;
          vecs[0].iov_base = ((guint8 *) vecs[0].iov_base) + ret;
          left -= ret;
        }
      }
#ifdef HAVE_WIN32
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

  for (i = 0; i < total_mem_num; ++i)
    gst_memory_unmap (map_infos[i].memory, &map_infos[i]);

  return flow_ret;

/* ERRORS */
#ifndef HAVE_WIN32
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
