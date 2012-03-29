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
#include <gst/video/gstvideopool.h>
#include <gst/video/gstvideometa.h>

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
 * Returned while parsing to indicate more data is needed.
 */
#define GST_BASE_VIDEO_CODEC_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

/**
 * GST_BASE_VIDEO_CODEC_STREAM_LOCK:
 * @codec: video codec instance
 *
 * Obtain a lock to protect the codec function from concurrent access.
 *
 * Since: 0.10.22
 */
#define GST_BASE_VIDEO_CODEC_STREAM_LOCK(codec) g_rec_mutex_lock (&GST_BASE_VIDEO_CODEC (codec)->stream_lock)
/**
 * GST_BASE_VIDEO_CODEC_STREAM_UNLOCK:
 * @codec: video codec instance
 *
 * Release the lock that protects the codec function from concurrent access.
 *
 * Since: 0.10.22
 */
#define GST_BASE_VIDEO_CODEC_STREAM_UNLOCK(codec) g_rec_mutex_unlock (&GST_BASE_VIDEO_CODEC (codec)->stream_lock)

typedef struct _GstVideoState GstVideoState;
typedef struct _GstVideoFrameState GstVideoFrameState;
typedef struct _GstBaseVideoCodec GstBaseVideoCodec;
typedef struct _GstBaseVideoCodecClass GstBaseVideoCodecClass;

/* GstVideoState is only used on the compressed video pad */
/**
 * GstVideoState:
 * @width: Width in pixels (including borders)
 * @height: Height in pixels (including borders)
 * @fps_n: Numerator of framerate
 * @fps_d: Denominator of framerate
 * @par_n: Numerator of Pixel Aspect Ratio
 * @par_d: Denominator of Pixel Aspect Ratio
 * @have_interlaced: The content of the @interlaced field is present and valid
 * @interlaced: %TRUE if the stream is interlaced
 * @top_field_first: %TRUE if the interlaced frame is top-field-first
 * @clean_width: Useful width of video in pixels (i.e. without borders)
 * @clean_height: Useful height of video in pixels (i.e. without borders)
 * @clean_offset_left: Horizontal offset (from the left) of useful region in pixels
 * @clean_offset_top: Vertical offset (from the top) of useful region in pixels
 * @bytes_per_picture: Size in bytes of each picture
 * @codec_data: Optional Codec Data for the stream
 *
 * Information about compressed video stream.
 * FIXME: Re-use GstVideoInfo for more fields.
 */
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

/**
 * GstVideoFrameState:
 * @decode_timestamp: Decoding timestamp (aka DTS)
 * @presentation_timestamp: Presentation timestamp (aka PTS)
 * @presentation_duration: Duration of frame
 * @system_frame_number: unique ID attributed when #GstVideoFrameState is
 *        created
 * @decode_frame_number: Decoded frame number, increases in decoding order
 * @presentation_frame_number: Presentation frame number, increases in
 *        presentation order.
 * @distance_from_sync: Distance of the frame from a sync point, in number
 *        of frames.
 * @is_sync_point: #TRUE if the frame is a synchronization point (like a
 *        keyframe)
 * @is_eos: #TRUE if the frame is the last one of a segment.
 * @decode_only: If #TRUE, the frame is only meant to be decoded but not
 *        pushed downstream
 * @sink_buffer: input buffer
 * @src_buffer: output buffer
 * @field_index: Number of fields since beginning of stream
 * @n_fields: Number of fields present in frame (default 2)
 * @coder_hook: Private data called with @coder_hook_destroy_notify
 * @coder_hook_destroy_notify: Called when frame is destroyed
 * @deadline: Target clock time for display (running time)
 * @force_keyframe: For encoders, if #TRUE a keyframe must be generated
 * @force_keyframe_headers: For encoders, if #TRUE new headers must be generated
 * @events: List of #GstEvent that must be pushed before the next @src_buffer
 *
 * State of a video frame going through the codec
 **/

struct _GstVideoFrameState
{
  /*< private >*/
  gint ref_count;

  /*< public >*/
  GstClockTime decode_timestamp;
  GstClockTime presentation_timestamp;
  GstClockTime presentation_duration;

  gint system_frame_number;
  gint decode_frame_number;
  gint presentation_frame_number;

  int distance_from_sync;
  gboolean is_sync_point;
  gboolean is_eos;

  /* Frames that should not be pushed downstream and are
   * not meant for display */
  gboolean decode_only;

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

/**
 * GstBaseVideoCodec:
 *
 * The opaque #GstBaseVideoCodec data structure.
 */
struct _GstBaseVideoCodec
{
  /*< private >*/
  GstElement      element;

  /*< protected >*/
  GstPad         *sinkpad;
  GstPad         *srcpad;

  /* protects all data processing, i.e. is locked
   * in the chain function, finish_frame and when
   * processing serialized events */
  GRecMutex stream_lock;

  guint64         system_frame_number;

  GList *frames;  /* Protected with OBJECT_LOCK */
  GstVideoState state;		/* Compressed video pad */
  GstVideoInfo info;		/* Raw video pad */
  GstSegment segment;

  /* QoS properties */
  gdouble         proportion;
  GstClockTime    earliest_time;
  gboolean        discont;

  gint64          bytes;
  gint64          time;

  /* FIXME before moving to base */
  void           *padding[GST_PADDING_LARGE];
};

/**
 * GstBaseVideoCodecClass:
 *
 * The opaque #GstBaseVideoCodecClass data structure.
 */
struct _GstBaseVideoCodecClass
{
  /*< private >*/
  GstElementClass element_class;

  /* FIXME before moving to base */
  void *padding[GST_PADDING_LARGE];
};

GType gst_video_frame_state_get_type (void);
GType gst_base_video_codec_get_type (void);

void gst_base_video_codec_append_frame (GstBaseVideoCodec *codec, GstVideoFrameState *frame);
void gst_base_video_codec_remove_frame (GstBaseVideoCodec *codec, GstVideoFrameState *frame);

GstVideoFrameState * gst_base_video_codec_new_frame (GstBaseVideoCodec *base_video_codec);

GstVideoFrameState * gst_video_frame_state_ref (GstVideoFrameState * frame);
void                 gst_video_frame_state_unref (GstVideoFrameState * frame);

G_END_DECLS

#endif

