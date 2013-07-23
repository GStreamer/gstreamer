/*
 * DASH demux plugin for GStreamer
 *
 * gstdashdemux.h
 *
 * Copyright (C) 2012 Orange
 * Authors:
 *   David Corvoysier <david.corvoysier@orange.com>
 *   Hamid Zakari <hamid.zakari@gmail.com>
 *
 * Copyright (C) 2013 Smart TV Alliance
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>, Collabora Ltd.
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
#include <gst/base/gstdataqueue.h>
#include "gstmpdparser.h"
#include "gstdownloadrate.h"
#include <gst/uridownloader/gsturidownloader.h>

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

typedef struct _GstDashDemuxStream GstDashDemuxStream;
typedef struct _GstDashDemux GstDashDemux;
typedef struct _GstDashDemuxClass GstDashDemuxClass;

struct _GstDashDemuxStream
{
  GstPad *pad;

  gint index;

  GstCaps *input_caps;

  /*
   * Need to store the status for the download and
   * stream tasks separately as they are working at
   * different points of the stream timeline.
   * The download task is ahead of the stream.
   *
   * The download_end_of_period is set when a stream
   * has already downloaded all fragments for the current
   * period.
   *
   * The stream_end_of_period is set when a stream
   * has pushed all fragments for the current period
   */
  gboolean download_end_of_period;
  gboolean stream_end_of_period;

  gboolean stream_eos;
  gboolean need_header;

  /* tracks if a stream has enqueued data
   * after a pad switch.
   * This is required to prevent pads being
   * added to the demuxer and having no data
   * pushed to it before another pad switch
   * as this might make downstream elements
   * unhappy and error out if they get
   * an EOS without receiving any input
   */
  gboolean has_data_queued;

  GstDataQueue *queue;

  GstDownloadRate dnl_rate;
};

/**
 * GstDashDemux:
 *
 * Opaque #GstDashDemux data structure.
 */
struct _GstDashDemux
{
  GstElement parent;
  GstPad *sinkpad;

  gboolean have_group_id;
  guint group_id;

  GSList *streams;
  GSList *next_periods;
  GMutex streams_lock;

  GstSegment segment;
  gboolean need_segment;
  GstClockTime timestamp_offset;

  GstBuffer *manifest;
  GstUriDownloader *downloader;
  GstMpdClient *client;         /* MPD client */
  gboolean end_of_period;
  gboolean end_of_manifest;

  /* Properties */
  GstClockTime max_buffering_time;      /* Maximum buffering time accumulated during playback */
  gfloat bandwidth_usage;       /* Percentage of the available bandwidth to use       */
  guint64 max_bitrate;          /* max of bitrate supported by target decoder         */

  /* Streaming task */
  GstTask *stream_task;
  GRecMutex stream_task_lock;

  /* Download task */
  GstTask *download_task;
  GRecMutex download_task_lock;
  GMutex download_mutex;
  GCond download_cond;
  gboolean cancelled;

  /* Manifest update */
  GstClockTime last_manifest_update;
};

struct _GstDashDemuxClass
{
  GstElementClass parent_class;
};

GType gst_dash_demux_get_type (void);

G_END_DECLS
#endif /* __GST_DASH_DEMUX_H__ */

