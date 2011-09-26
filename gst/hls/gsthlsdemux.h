/* GStreamer
 * Copyright (C) 2010 Marc-Andre Lureau <marcandre.lureau@gmail.com>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * gsthlsdemux.h:
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


#ifndef __GST_HLS_DEMUX_H__
#define __GST_HLS_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "m3u8.h"

G_BEGIN_DECLS
#define GST_TYPE_HLS_DEMUX \
  (gst_hls_demux_get_type())
#define GST_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_HLS_DEMUX,GstHLSDemux))
#define GST_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_HLS_DEMUX,GstHLSDemuxClass))
#define GST_IS_HLS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_HLS_DEMUX))
#define GST_IS_HLS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_HLS_DEMUX))
typedef struct _GstHLSDemux GstHLSDemux;
typedef struct _GstHLSDemuxClass GstHLSDemuxClass;

/**
 * GstHLSDemux:
 *
 * Opaque #GstHLSDemux data structure.
 */
struct _GstHLSDemux
{
  GstElement parent;

  GstTask *task;
  GStaticRecMutex task_lock;
  GstPad *srcpad;
  GstPad *sinkpad;
  GstBuffer *playlist;
  GstCaps *input_caps;
  GstM3U8Client *client;        /* M3U8 client */
  GQueue *queue;                /* Queue storing the fetched fragments */
  gboolean need_cache;          /* Wheter we need to cache some fragments before starting to push data */
  gboolean end_of_playlist;
  gboolean do_typefind;		/* Whether we need to typefind the next buffer */

  /* Properties */
  guint fragments_cache;        /* number of fragments needed to be cached to start playing */
  gfloat bitrate_switch_tol;    /* tolerance with respect to the fragment duration to switch the bitarate*/

  /* Updates thread */
  GThread *updates_thread;      /* Thread handling the playlist and fragments updates */
  GMutex *thread_lock;          /* Thread lock */
  GCond *thread_cond;           /* Signals the thread to quit */
  gboolean thread_return;       /* Instructs the thread to return after the thread_quit condition is meet */
  GTimeVal next_update;         /* Time of the next update */
  gint64 accumulated_delay;     /* Delay accumulated fetching fragments, used to decide a playlist switch */

  /* Fragments fetcher */
  GstElement *fetcher;
  GstBus *fetcher_bus;
  GstPad *fetcherpad;
  GMutex *fetcher_lock;
  GCond *fetcher_cond;
  GTimeVal *timeout;
  gboolean fetcher_error;
  gboolean stopping_fetcher;
  gboolean cancelled;
  GstAdapter *download;

  /* Position in the stream */
  GstClockTime position;
  GstClockTime position_shift;
  gboolean need_segment;
};

struct _GstHLSDemuxClass
{
  GstElementClass parent_class;
};

GType gst_hls_demux_get_type (void);

G_END_DECLS
#endif /* __GST_HLS_DEMUX_H__ */
