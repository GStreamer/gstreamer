/*
 * DASH demux plugin for GStreamer
 *
 * gstdashdemux.h
 *
 * Copyright (C) 2012 Orange
 * 
 * Authors:
 *   David Corvoysier <david.corvoysier@orange.com>
 *   Hamid Zakari <hamid.zakari@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library (COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_DASH_DEMUX_H__
#define __GST_DASH_DEMUX_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include "gstmpdparser.h"
#include "gstfragmented.h"
#include "gsturidownloader.h"

G_BEGIN_DECLS
#define GST_TYPE_DASH_DEMUX \
        (gst_dash_demux_get_type())
#define GST_DASH_DEMUX(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DASH_DEMUX,GstDashDemux))
#define GST_DASH_DEMUX_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DASH_DEMUX,GstDashDemuxClass))
#define GST_IS_DASH_DEMUX(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DASH_DEMUX))
#define GST_IS_DASH_DEMUX_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DASH_DEMUX))
//
typedef struct _GstDashDemux GstDashDemux;
typedef struct _GstDashDemuxClass GstDashDemuxClass;
#define MAX_LANGUAGES 20
/**
 * GstDashDemux:
 *
 * Opaque #GstDashDemux data structure.
 */
struct _GstDashDemux
{
  GstElement parent;
  GstPad *sinkpad;
  GstPad *srcpad[MAX_LANGUAGES];   /*Video/Audio/Application src pad*/
  GstCaps *output_caps[MAX_LANGUAGES]; /*Video/Audio/Application output buf caps*/
  GstCaps *input_caps[MAX_LANGUAGES]; /*Video/Audio/Application input caps*/

  GstBuffer *playlist;
  GstUriDownloader *downloader;
  GstMpdClient *client;         /* MPD client */
  GQueue *queue;                /*Video/Audio/Application List of fragment storing the fetched fragments */
  gboolean end_of_playlist;

  /* Properties */
  GstClockTime min_buffering_time;    /* Minimum buffering time accumulated before playback */
  GstClockTime max_buffering_time;    /* Maximum buffering time accumulated during playback */
  gfloat bandwidth_usage;             /* Percentage of the available bandwidth to use       */
  guint64 max_bitrate;              /* max of bitrate supported by target decoder         */

  /* Streaming task */
  GstTask *stream_task;
  GStaticRecMutex stream_lock;
  gboolean stop_stream_task;
  GMutex *stream_timed_lock;
  GTimeVal next_stream;         /* Time of the next push */

  /* Download task */
  GstTask *download_task;
  GStaticRecMutex download_lock;
  gboolean cancelled;
  GMutex *download_timed_lock;
  GTimeVal next_download;       /* Time of the next download */

  /* Position in the stream */
  GstClockTime position;
  GstClockTime position_shift;
  gboolean need_segment;
  /* Download rate */
  guint64 dnl_rate;
};

struct _GstDashDemuxClass
{
  GstElementClass parent_class;
};

GType gst_dash_demux_get_type (void);

G_END_DECLS
#endif /* __GST_DASH_DEMUX_H__ */

