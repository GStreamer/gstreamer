/* GStreamer
 * Copyright (C) 2012 Smart TV Alliance
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>, Collabora Ltd.
 *
 * gstmssdemux.h:
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

#ifndef __GST_MSSDEMUX_H__
#define __GST_MSSDEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/base/gstdataqueue.h>
#include "gstmssmanifest.h"
#include <gst/uridownloader/gsturidownloader.h>

G_BEGIN_DECLS

GST_DEBUG_CATEGORY_EXTERN (mssdemux_debug);
#define GST_CAT_DEFAULT mssdemux_debug

#define GST_TYPE_MSS_DEMUX \
  (gst_mss_demux_get_type())
#define GST_MSS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_MSS_DEMUX,GstMssDemux))
#define GST_MSS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_MSS_DEMUX,GstMssDemuxClass))
#define GST_IS_MSS_DEMUX(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_MSS_DEMUX))
#define GST_IS_MSS_DEMUX_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_MSS_DEMUX))

#define GST_MSS_DEMUX_CAST(obj) ((GstMssDemux *)(obj))

typedef struct _GstMssDemuxStream GstMssDemuxStream;
typedef struct _GstMssDemux GstMssDemux;
typedef struct _GstMssDemuxClass GstMssDemuxClass;

struct _GstMssDemuxStream {
  GstPad *pad;

  GstCaps *caps;

  GstMssDemux *parent;

  GstMssStream *manifest_stream;

#if 0
  GstUriDownloader *downloader;
#endif

  GstEvent *pending_segment;

  GstSegment segment;

  /* Downloading task */
  GstTask *download_task;
  GRecMutex download_lock;

  GstFlowReturn last_ret;
  gboolean eos;
  gboolean have_data;
  gboolean cancelled;
  gboolean restart_download;

  guint download_error_count;

  /* download tooling */
  GstElement *src;
  GstPad *src_srcpad;
  GMutex fragment_download_lock;
  GCond fragment_download_cond;
  gboolean starting_fragment;
  gint64 download_start_time;
  gint64 download_total_time;
  gint64 download_total_bytes;
  gint current_download_rate;
};

struct _GstMssDemux {
  GstBin bin;

  /* pads */
  GstPad *sinkpad;

  gboolean have_group_id;
  guint group_id;

  GstBuffer *manifest_buffer;

  GstMssManifest *manifest;
  gchar *base_url;
  gchar *manifest_uri;

  GSList *streams;
  guint n_videos;
  guint n_audios;

  gboolean update_bitrates;

  /* properties */
  guint64 connection_speed; /* in bps */
  guint data_queue_max_size;
  gfloat bitrate_limit;
};

struct _GstMssDemuxClass {
  GstBinClass parent_class;
};

GType gst_mss_demux_get_type (void);

G_END_DECLS

#endif /* __GST_MSSDEMUX_H__ */
