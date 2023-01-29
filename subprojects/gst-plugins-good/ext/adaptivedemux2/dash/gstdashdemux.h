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
 * Copyright (C) 2021 Centricular Ltd
 *   Author: Edward Hervey <edward@centricular.com>
 *   Author: Jan Schmidt <jan@centricular.com>
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

#include "gstisoff.h"
#include "gstadaptivedemux.h"

#include "gstmpdclient.h"

G_BEGIN_DECLS
#define GST_TYPE_DASH_DEMUX2 \
        (gst_dash_demux2_get_type())
#define GST_DASH_DEMUX(obj) \
        (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DASH_DEMUX2,GstDashDemux2))
#define GST_DASH_DEMUX_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_DASH_DEMUX2,GstDashDemux2Class))
#define GST_IS_DASH_DEMUX(obj) \
        (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_DASH_DEMUX2))
#define GST_IS_DASH_DEMUX_CLASS(klass) \
        (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_DASH_DEMUX2))
#define GST_DASH_DEMUX_CAST(obj) \
	((GstDashDemux2 *)obj)

#define GST_TYPE_DASH_DEMUX_STREAM \
  (gst_dash_demux_stream_get_type())
#define GST_DASH_DEMUX_STREAM(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_DASH_DEMUX_STREAM,GstDashDemux2Stream))
#define GST_DASH_DEMUX_STREAM_CAST(obj) ((GstDashDemux2Stream *)obj)

typedef struct _GstDashDemux2Stream GstDashDemux2Stream;
typedef GstAdaptiveDemux2StreamClass GstDashDemux2StreamClass;

typedef struct _GstDashDemux2ClockDrift GstDashDemux2ClockDrift;
typedef struct _GstDashDemux2 GstDashDemux2;
typedef struct _GstDashDemux2Class GstDashDemux2Class;

struct _GstDashDemux2Stream
{
  GstAdaptiveDemux2Stream parent;

  gint index;
  GstActiveStream *active_stream;

  /* Track provided by this stream */
  GstAdaptiveDemuxTrack *track;

  GstMediaFragmentInfo current_fragment;

  /* index parsing */
  GstSidxParser sidx_parser;
  GstClockTime sidx_position;
  gint64 sidx_base_offset;
  gboolean allow_sidx;
  GstClockTime pending_seek_ts;

  GstAdapter *adapter;
  /* current offset of the first byte in the adapter / last byte we pushed or
   * dropped*/
  guint64 current_offset;
  /* index = 1, header = 2, data = 3 */
  guint current_index_header_or_data;

  /* ISOBMFF box parsing */
  gboolean is_isobmff;
  struct {
    /* index = 1, header = 2, data = 3 */
    guint32 current_fourcc;
    guint64 current_start_offset;
    guint64 current_size;
  } isobmff_parser;

  GstMoofBox *moof;
  guint64 moof_offset, moof_size;
  GArray *moof_sync_samples;
  guint current_sync_sample;

  guint64 moof_average_size;
  guint64 keyframe_average_size;
  guint64 keyframe_average_distance;
  gboolean first_sync_sample_after_moof, first_sync_sample_always_after_moof;

  /* Internal position value, at the keyframe/entry level */
  GstClockTime actual_position;
  /* Timestamp of the beginning of the current fragment */
  GstClockTime current_fragment_timestamp;
  GstClockTime current_fragment_duration;
  GstClockTime current_fragment_keyframe_distance;

  /* Average keyframe download time (only in trickmode-key-units) */
  GstClockTime average_download_time;
  /* Cached target time (only in trickmode-key-units) */
  GstClockTime target_time;
  /* Average skip-ahead time (only in trickmode-key-units) */
  GstClockTime average_skip_size;

  gchar *last_representation_id;
};

/**
 * GstDashDemux2:
 *
 * Opaque #GstDashDemux2 data structure.
 */
struct _GstDashDemux2
{
  GstAdaptiveDemux parent;

  GSList *next_periods;

  GstMPDClient2 *client;         /* MPD client */
  GMutex client_lock;

  GstDashDemux2ClockDrift *clock_drift;

  gboolean end_of_period;
  gboolean end_of_manifest;

  /* Properties */
  gint max_video_width, max_video_height;
  gint max_video_framerate_n, max_video_framerate_d;
  gchar* default_presentation_delay; /* presentation time delay if MPD@suggestedPresentationDelay is not present */
  guint start_bitrate; /* Initial bitrate to use before any bandwidth measurement */

  gboolean allow_trickmode_key_units;
};

struct _GstDashDemux2Class
{
  GstAdaptiveDemuxClass parent_class;
};

GType gst_dash_demux2_get_type (void);
GType gst_dash_demux_stream_get_type (void);

G_END_DECLS
#endif /* __GST_DASH_DEMUX_H__ */

