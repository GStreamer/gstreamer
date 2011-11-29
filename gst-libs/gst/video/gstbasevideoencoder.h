/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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

#ifndef _GST_BASE_VIDEO_ENCODER_H_
#define _GST_BASE_VIDEO_ENCODER_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstBaseVideoEncoder is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/video/gstbasevideocodec.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_VIDEO_ENCODER \
  (gst_base_video_encoder_get_type())
#define GST_BASE_VIDEO_ENCODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_VIDEO_ENCODER,GstBaseVideoEncoder))
#define GST_BASE_VIDEO_ENCODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_VIDEO_ENCODER,GstBaseVideoEncoderClass))
#define GST_BASE_VIDEO_ENCODER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_VIDEO_ENCODER,GstBaseVideoEncoderClass))
#define GST_IS_BASE_VIDEO_ENCODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_VIDEO_ENCODER))
#define GST_IS_BASE_VIDEO_ENCODER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_VIDEO_ENCODER))

/**
 * GST_BASE_VIDEO_ENCODER_SINK_NAME:
 *
 * The name of the templates for the sink pad.
 */
#define GST_BASE_VIDEO_ENCODER_SINK_NAME    "sink"
/**
 * GST_BASE_VIDEO_ENCODER_SRC_NAME:
 *
 * The name of the templates for the source pad.
 */
#define GST_BASE_VIDEO_ENCODER_SRC_NAME     "src"

/**
 * GST_BASE_VIDEO_ENCODER_FLOW_DROPPED:
 *
 * Returned when the event/buffer should be dropped.
 */
#define GST_BASE_VIDEO_ENCODER_FLOW_DROPPED GST_FLOW_CUSTOM_SUCCESS_1

typedef struct _GstBaseVideoEncoder GstBaseVideoEncoder;
typedef struct _GstBaseVideoEncoderClass GstBaseVideoEncoderClass;

/**
 * GstBaseVideoEncoder:
 * @element: the parent element.
 *
 * The opaque #GstBaseVideoEncoder data structure.
 */
struct _GstBaseVideoEncoder
{
  GstBaseVideoCodec base_video_codec;

  /*< protected >*/
  gboolean          sink_clipping;

  guint64           presentation_frame_number;
  int               distance_from_sync;

  /*< private >*/
  /* FIXME move to real private part ?
   * (and introduce a context ?) */
  gboolean          drained;
  gboolean          at_eos;

  gint64            min_latency;
  gint64            max_latency;

  GList            *current_frame_events;

  GstBuffer        *headers;

  GList            *force_key_unit; /* List of pending forced keyunits */

  void             *padding[GST_PADDING_LARGE];
};

/**
 * GstBaseVideoEncoderClass:
 * @start:          Optional.
 *                  Called when the element starts processing.
 *                  Allows opening external resources.
 * @stop:           Optional.
 *                  Called when the element stops processing.
 *                  Allows closing external resources.
 * @set_format:     Optional.
 *                  Notifies subclass of incoming data format.
 *                  GstVideoState fields have already been
 *                  set according to provided caps.
 * @handle_frame:   Provides input frame to subclass.
 * @finish:         Optional.
 *                  Called to request subclass to dispatch any pending remaining
 *                  data (e.g. at EOS).
 * @shape_output:   Optional.
 *                  Allows subclass to push frame downstream in whatever
 *                  shape or form it deems appropriate.  If not provided,
 *                  provided encoded frame data is simply pushed downstream.
 * @event:          Optional.
 *                  Event handler on the sink pad. This function should return
 *                  TRUE if the event was handled and should be discarded
 *                  (i.e. not unref'ed).
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @handle_frame needs to be overridden, and @set_format
 * and @get_caps are likely needed as well.
 */
struct _GstBaseVideoEncoderClass
{
  GstBaseVideoCodecClass              base_video_codec_class;

  /*< public >*/
  /* virtual methods for subclasses */

  gboolean      (*start)              (GstBaseVideoEncoder *coder);

  gboolean      (*stop)               (GstBaseVideoEncoder *coder);

  gboolean      (*set_format)         (GstBaseVideoEncoder *coder,
                                       GstVideoState *state);

  GstFlowReturn (*handle_frame)       (GstBaseVideoEncoder *coder,
                                       GstVideoFrame *frame);

  gboolean      (*reset)              (GstBaseVideoEncoder *coder);
  GstFlowReturn (*finish)             (GstBaseVideoEncoder *coder);

  GstFlowReturn (*shape_output)       (GstBaseVideoEncoder *coder,
                                       GstVideoFrame *frame);

  gboolean      (*event)              (GstBaseVideoEncoder *coder,
                                       GstEvent *event);

  /*< private >*/
  /* FIXME before moving to base */
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

GType                  gst_base_video_encoder_get_type (void);

const GstVideoState*   gst_base_video_encoder_get_state (GstBaseVideoEncoder *coder);

GstVideoFrame*         gst_base_video_encoder_get_oldest_frame (GstBaseVideoEncoder *coder);
GstFlowReturn          gst_base_video_encoder_finish_frame (GstBaseVideoEncoder *base_video_encoder,
                                                            GstVideoFrame *frame);

void                   gst_base_video_encoder_set_latency (GstBaseVideoEncoder *base_video_encoder,
                                                           GstClockTime min_latency, GstClockTime max_latency);
void                   gst_base_video_encoder_set_latency_fields (GstBaseVideoEncoder *base_video_encoder,
                                                                  int n_fields);
void                   gst_base_video_encoder_set_headers (GstBaseVideoEncoder *base_video_encoder,
                                                                  GstBuffer *headers);
G_END_DECLS

#endif

