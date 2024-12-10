/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#ifndef __GST_VTENC_H__
#define __GST_VTENC_H__

#include <gst/gst.h>
#include <gst/base/gstqueuearray.h>
#include <gst/codecparsers/gsth264parser.h>
#include <gst/video/video.h>
#include <VideoToolbox/VideoToolbox.h>

G_BEGIN_DECLS

#define GST_TYPE_VTENC            (gst_vtenc_get_type())
#define GST_VTENC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_VTENC, GstVTEnc))
#define GST_VTENC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_VTENC, GstVTEncClass))
#define GST_IS_VTENC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VTENC))
#define GST_IS_VTENC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VTENC))
#define GST_VTENC_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_VTENC, GstVTEncClass))
#define GST_VTENC_CAST(obj)       ((GstVTEnc *)obj)
typedef struct _GstVTEnc GstVTEnc;
typedef struct _GstVTEncClass GstVTEncClass;

#define GST_VTENC_CLASS_GET_CODEC_DETAILS(klass) \
  ((const GstVTEncoderDetails *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass), \
      GST_VTENC_CODEC_DETAILS_QDATA))

/**
 * GstVtencRateControl:
 * @GST_VTENC_RATE_CONTROL_ABR: average (variable) bitrate
 * @GST_VTENC_RATE_CONTROL_CBR: constant bitrate
 *
 * Since: 1.26
 */
typedef enum
{
  GST_VTENC_RATE_CONTROL_ABR,
  GST_VTENC_RATE_CONTROL_CBR,
} GstVtencRateControl;

typedef struct _GstVTEncoderDetails GstVTEncoderDetails;

struct _GstVTEncoderDetails
{
  const char * name;
  const char * element_name;
  const char * mimetype;
  const char * authors;
  CMVideoCodecType format_id;
  gboolean require_hardware;
};

struct _GstVTEncClass
{
  GstVideoEncoderClass parent_class;
};

struct _GstVTEnc
{
  GstVideoEncoder parent;

  const GstVTEncoderDetails * details;

  CMVideoCodecType specific_format_id;
  CFStringRef profile_level;
  GstH264Profile h264_profile;
  guint bitrate;
  guint max_bitrate;
  float bitrate_window;
  gboolean allow_frame_reordering;
  gboolean realtime;
  gdouble quality;
  gint max_keyframe_interval;
  GstClockTime max_keyframe_interval_duration;
  gint max_frame_delay;
  gint latency_frames;
  gboolean preserve_alpha;
  GstVtencRateControl rate_control;

  gboolean dump_properties;
  gboolean dump_attributes;

  gboolean have_field_order;
  GstVideoCodecState *input_state;
  GstVideoInfo video_info;
  VTCompressionSessionRef session;
  CFDictionaryRef keyframe_props;
  GstClockTime dts_offset;

  GstVecDeque * output_queue;
  /* Protects output_queue, is_flushing and pause_task */
  GMutex queue_mutex;
  GCond queue_cond;

  /* Temporary workaround for HEVCWithAlpha encoder not throttling input */
  GMutex encoding_mutex;
  GCond encoding_cond;

  /* downstream_ret is protected by the STREAM_LOCK */
  GstFlowReturn downstream_ret;
  gboolean negotiate_downstream;
  gboolean is_flushing;
  gboolean pause_task;

  /* If we get an EncoderMalfunctionErr or similar, we restart the session
   * before the next encode call */
  gboolean require_restart;
};

GType gst_vtenc_get_type (void);

void gst_vtenc_register_elements (GstPlugin * plugin);

G_END_DECLS

#endif /* __GST_VTENC_H__ */
