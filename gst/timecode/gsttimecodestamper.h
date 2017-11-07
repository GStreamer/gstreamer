/*
 * GStreamer
 * Copyright (C) 2016 Vivia Nikolaidou <vivia@toolsonair.com>
 *
 * gsttimecodestamper.h
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

#ifndef __GST_TIME_CODE_STAMPER_H__
#define __GST_TIME_CODE_STAMPER_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/audio/audio.h>

#if HAVE_LTC
#include <ltc.h>
#endif

#define GST_TYPE_TIME_CODE_STAMPER            (gst_timecodestamper_get_type())
#define GST_TIME_CODE_STAMPER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TIME_CODE_STAMPER,GstTimeCodeStamper))
#define GST_TIME_CODE_STAMPER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_TIME_CODE_STAMPER,GstTimeCodeStamperClass))
#define GST_TIME_CODE_STAMPER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_TIME_CODE_STAMPER,GstTimeCodeStamperClass))
#define GST_IS_TIME_CODE_STAMPER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TIME_CODE_STAMPER))
#define GST_IS_TIME_CODE_STAMPER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_TIME_CODE_STAMPER))

#define GST_TYPE_TIME_CODE_STAMPER_SOURCE (gst_timecodestamper_source_get_type())

typedef struct _GstTimeCodeStamper GstTimeCodeStamper;
typedef struct _GstTimeCodeStamperClass GstTimeCodeStamperClass;

typedef enum GstTimeCodeStamperSource
{
  GST_TIME_CODE_STAMPER_NOREPLACE,
  GST_TIME_CODE_STAMPER_INTERN,
  GST_TIME_CODE_STAMPER_EXISTING,
  GST_TIME_CODE_STAMPER_LTC,
  GST_TIME_CODE_STAMPER_NRZERO
} GstTimeCodeStamperSource;

/**
 * GstTimeCodeStamper:
 *
 * Opaque data structure.
 */
struct _GstTimeCodeStamper
{
  GstBaseTransform videofilter;

  GstPad *ltcpad;

  /* < private > */
  GstTimeCodeStamperSource tc_source;
  gboolean drop_frame;
  GstVideoTimeCode *current_tc;
  GstVideoTimeCode *first_tc;
  GstVideoTimeCode *ltc_current_tc;
  GstVideoTimeCode *ltc_intern_tc;
  GstClockTime ltc_max_offset;
  gint tc_add;
  GstSegment ltc_segment;
  GstVideoInfo vinfo;
  gboolean post_messages;
  gboolean first_tc_now;
  gboolean is_flushing;
  gboolean no_wait;
  GMutex mutex;

#if HAVE_LTC
  LTCDecoder *ltc_dec;
  ltc_off_t ltc_total;
  GstAudioInfo audio_info;
  GstClockTime ltc_first_runtime;
  GstClockTime ltc_audio_endtime;
  GCond ltc_cond_video;
  GCond ltc_cond_audio;
#endif
};

struct _GstTimeCodeStamperClass
{
  GstBaseTransformClass parent_class;
};

GType gst_timecodestamper_get_type (void);

GType gst_timecodestamper_source_get_type (void);

G_END_DECLS
#endif /* __GST_TIME_CODE_STAMPER_H__ */
