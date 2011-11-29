/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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

#ifndef _GST_BASE_VIDEO_CODEC_H_
#define _GST_BASE_VIDEO_CODEC_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstBaseVideoCodec is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_VIDEO_CODEC \
  (gst_base_video_codec_get_type())
#define GST_BASE_VIDEO_CODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_VIDEO_CODEC,GstBaseVideoCodec))
#define GST_BASE_VIDEO_CODEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_VIDEO_CODEC,GstBaseVideoCodecClass))
#define GST_BASE_VIDEO_CODEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_VIDEO_CODEC,GstBaseVideoCodecClass))
#define GST_IS_BASE_VIDEO_CODEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_VIDEO_CODEC))
#define GST_IS_BASE_VIDEO_CODEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_VIDEO_CODEC))

/**
 * GST_BASE_VIDEO_CODEC_SINK_NAME:
 *
 * The name of the templates for the sink pad.
 */
#define GST_BASE_VIDEO_CODEC_SINK_NAME    "sink"
/**
 * GST_BASE_VIDEO_CODEC_SRC_NAME:
 *
 * The name of the templates for the source pad.
 */
#define GST_BASE_VIDEO_CODEC_SRC_NAME     "src"

/**
 * GST_BASE_VIDEO_CODEC_SRC_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 */
#define GST_BASE_VIDEO_CODEC_SRC_PAD(obj)         (((GstBaseVideoCodec *) (obj))->srcpad)

/**
 * GST_BASE_VIDEO_CODEC_SINK_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 */
#define GST_BASE_VIDEO_CODEC_SINK_PAD(obj)        (((GstBaseVideoCodec *) (obj))->sinkpad)

/**
 * GST_BASE_VIDEO_CODEC_FLOW_NEED_DATA:
 *
 */
#define GST_BASE_VIDEO_CODEC_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

#define GST_BASE_VIDEO_CODEC_STREAM_LOCK(codec) g_static_rec_mutex_lock (&GST_BASE_VIDEO_CODEC (codec)->stream_lock)
#define GST_BASE_VIDEO_CODEC_STREAM_UNLOCK(codec) g_static_rec_mutex_unlock (&GST_BASE_VIDEO_CODEC (codec)->stream_lock)

typedef struct _GstVideoState GstVideoState;
typedef struct _GstVideoFrame GstVideoFrame;
typedef struct _GstBaseVideoCodec GstBaseVideoCodec;
typedef struct _GstBaseVideoCodecClass GstBaseVideoCodecClass;

struct _GstVideoState
{
  GstCaps *caps;
  GstVideoFormat format;
  int width, height;
  int fps_n, fps_d;
  int par_n, par_d;

  gboolean have_interlaced;
  gboolean interlaced;
  gboolean top_field_first;

  int clean_width, clean_height;
  int clean_offset_left, clean_offset_top;

  int bytes_per_picture;

  GstBuffer *codec_data;

};

struct _GstVideoFrame
{
  GstClockTime decode_timestamp;
  GstClockTime presentation_timestamp;
  GstClockTime presentation_duration;

  gint system_frame_number;
  gint decode_frame_number;
  gint presentation_frame_number;

  int distance_from_sync;
  gboolean is_sync_point;
  gboolean is_eos;

  GstBuffer *sink_buffer;
  GstBuffer *src_buffer;

  int field_index;
  int n_fields;

  void *coder_hook;
  GDestroyNotify coder_hook_destroy_notify;

  GstClockTime deadline;

  gboolean force_keyframe;
  gboolean force_keyframe_headers;

  /* Events that should be pushed downstream *before*
   * the next src_buffer */
  GList *events;
};

struct _GstBaseVideoCodec
{
  GstElement element;

  /*< private >*/
  GstPad *sinkpad;
  GstPad *srcpad;

  /* protects all data processing, i.e. is locked
   * in the chain function, finish_frame and when
   * processing serialized events */
  GStaticRecMutex stream_lock;

  guint64 system_frame_number;

  GList *frames;  /* Protected with OBJECT_LOCK */
  GstVideoState state;
  GstSegment segment;

  gdouble proportion;
  GstClockTime earliest_time;
  gboolean discont;

  gint64 bytes;
  gint64 time;

  /* FIXME before moving to base */
  void *padding[GST_PADDING_LARGE];
};

struct _GstBaseVideoCodecClass
{
  GstElementClass element_class;

  /* FIXME before moving to base */
  void *padding[GST_PADDING_LARGE];
};

GType gst_base_video_codec_get_type (void);

GstVideoFrame * gst_base_video_codec_new_frame (GstBaseVideoCodec *base_video_codec);
void gst_base_video_codec_free_frame (GstVideoFrame *frame);

G_END_DECLS

#endif

