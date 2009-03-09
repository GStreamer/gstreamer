/* GStreamer
 * Copyright (C) <2006> Mark Nauwelaerts <manauw@skynet.be>
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


#ifndef __GST_FFMPEGPIPE_H__
#define __GST_FFMPEGPIPE_H__

#include <gst/base/gstadapter.h>
#include "gstffmpeg.h"

G_BEGIN_DECLS

/* pipe protocol helpers */
#define GST_FFMPEG_PIPE_MUTEX_LOCK(m) G_STMT_START {                    \
  GST_LOG_OBJECT (m, "locking tlock from thread %p", g_thread_self ()); \
  g_mutex_lock (m->tlock);                                              \
  GST_LOG_OBJECT (m, "locked tlock from thread %p", g_thread_self ());  \
} G_STMT_END

#define GST_FFMPEG_PIPE_MUTEX_UNLOCK(m) G_STMT_START {                    \
  GST_LOG_OBJECT (m, "unlocking tlock from thread %p", g_thread_self ()); \
  g_mutex_unlock (m->tlock);                                              \
} G_STMT_END

#define GST_FFMPEG_PIPE_WAIT(m) G_STMT_START {                          \
  GST_LOG_OBJECT (m, "thread %p waiting", g_thread_self ());            \
  g_cond_wait (m->cond, m->tlock);                                      \
} G_STMT_END

#define GST_FFMPEG_PIPE_SIGNAL(m) G_STMT_START {                        \
  GST_LOG_OBJECT (m, "signalling from thread %p", g_thread_self ());    \
  g_cond_signal (m->cond);                                              \
} G_STMT_END

typedef struct _GstFFMpegPipe GstFFMpegPipe;

struct _GstFFMpegPipe
{
  /* lock for syncing */
  GMutex *tlock;
  /* with TLOCK */
  /* signals counterpart thread to have a look */
  GCond *cond;
  /* seen eos */
  gboolean eos;
  /* flowreturn obtained by src task */
  GstFlowReturn srcresult;
  /* adpater collecting data */
  GstAdapter *adapter;
  /* amount needed in adapter by src task */
  guint needed;
};

G_END_DECLS

#endif /* __GST_FFMPEGPIPE_H__ */
