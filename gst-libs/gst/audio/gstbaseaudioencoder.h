/* GStreamer
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

#ifndef __GST_BASE_AUDIO_ENCODER_H__
#define __GST_BASE_AUDIO_ENCODER_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>
#include <gst/audio/multichannel.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_AUDIO_ENCODER		   (gst_base_audio_encoder_get_type())
#define GST_BASE_AUDIO_ENCODER(obj)		   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_AUDIO_ENCODER,GstBaseAudioEncoder))
#define GST_BASE_AUDIO_ENCODER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_AUDIO_ENCODER,GstBaseAudioEncoderClass))
#define GST_BASE_AUDIO_ENCODER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_AUDIO_ENCODER,GstBaseAudioEncoderClass))
#define GST_IS_BASE_AUDIO_ENCODER(obj)	   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_AUDIO_ENCODER))
#define GST_IS_BASE_AUDIO_ENCODER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_AUDIO_ENCODER))
#define GST_BASE_AUDIO_ENCODER_CAST(obj)	((GstBaseAudioEncoder *)(obj))

/**
 * GST_BASE_AUDIO_ENCODER_SINK_NAME:
 *
 * the name of the templates for the sink pad
 */
#define GST_BASE_AUDIO_ENCODER_SINK_NAME	"sink"
/**
 * GST_BASE_AUDIO_ENCODER_SRC_NAME:
 *
 * the name of the templates for the source pad
 */
#define GST_BASE_AUDIO_ENCODER_SRC_NAME	        "src"

/**
 * GST_BASE_AUDIO_ENCODER_SRC_PAD:
 * @obj: base parse instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 *
 * Since: 0.10.x
 */
#define GST_BASE_AUDIO_ENCODER_SRC_PAD(obj)	(GST_BASE_AUDIO_ENCODER_CAST (obj)->srcpad)

/**
 * GST_BASE_AUDIO_ENCODER_SINK_PAD:
 * @obj: base parse instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 *
 * Since: 0.10.x
 */
#define GST_BASE_AUDIO_ENCODER_SINK_PAD(obj)	(GST_BASE_AUDIO_ENCODER_CAST (obj)->sinkpad)

/**
 * GST_BASE_AUDIO_ENCODER_SEGMENT:
 * @obj: base parse instance
 *
 * Gives the segment of the element.
 *
 * Since: 0.10.x
 */
#define GST_BASE_AUDIO_ENCODER_SEGMENT(obj)     (GST_BASE_AUDIO_ENCODER_CAST (obj)->segment)


typedef struct _GstBaseAudioEncoder GstBaseAudioEncoder;
typedef struct _GstBaseAudioEncoderClass GstBaseAudioEncoderClass;

typedef struct _GstBaseAudioEncoderPrivate GstBaseAudioEncoderPrivate;
typedef struct _GstBaseAudioEncoderContext GstBaseAudioEncoderContext;

/**
 * GstAudioState:
 * @xint: whether sample data is int or float
 * @rate: rate of sample data
 * @channels: number of channels in sample data
 * @width: width (in bits) of sample data
 * @depth: used bits in sample data (if integer)
 * @sign: rate of sample data (if integer)
 * @endian: endianness of sample data
 * @bpf: bytes per audio frame
 */
typedef struct _GstAudioState {
  gboolean xint;
  gint  rate;
  gint  channels;
  gint  width;
  gint  depth;
  gboolean sign;
  gint  endian;
  GstAudioChannelPosition *channel_pos;

  gint  bpf;
} GstAudioState;

/**
 * GstBaseAudioEncoderContext:
 * @state: a #GstAudioState describing input audio format
 * @frame_samples: number of samples (per channel) subclass needs to be handed,
 *   or will be handed all available if 0.
 * @frame_max: max number of frames of size @frame_bytes accepted at once
 *  (assumed minimally 1)
 * @latency: latency of element; should only be changed during configure
 * @lookahead: encoder lookahead (in units of input rate samples)
 *
 * Transparent #GstBaseAudioEncoderContext data structure.
 */
struct _GstBaseAudioEncoderContext {
  /* input */
  GstAudioState state;

  /* output */
  gint  frame_samples;
  gint  frame_max;
  GstClockTime min_latency;
  GstClockTime max_latency;
  gint  lookahead;
};

/**
 * GstBaseAudioEncoder:
 * @element: the parent element.
 *
 * The opaque #GstBaseAudioEncoder data structure.
 */
struct _GstBaseAudioEncoder {
  GstElement     element;

  /*< protected >*/
  /* source and sink pads */
  GstPad         *sinkpad;
  GstPad         *srcpad;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment      segment;
  GstBaseAudioEncoderContext *ctx;

  /* properties */
  gint64          tolerance;
  gboolean        perfect_ts;
  gboolean        hard_resync;
  gboolean        granule;

  /*< private >*/
  GstBaseAudioEncoderPrivate *priv;
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstBaseAudioEncoderClass:
 * @start:          Optional.
 *                  Called when the element starts processing.
 *                  Allows opening external resources.
 * @stop:           Optional.
 *                  Called when the element stops processing.
 *                  Allows closing external resources.
 * @set_format:     Notifies subclass of incoming data format.
 *                  GstBaseAudioEncoderContext fields have already been
 *                  set according to provided caps.
 * @handle_frame:   Provides input samples (or NULL to clear any remaining data)
 *                  according to directions as provided by subclass in the
 *                  #GstBaseAudioEncoderContext.  Input data ref management
 *                  is performed by base class, subclass should not care or
 *                  intervene.
 * @flush:          Optional.
 *                  Instructs subclass to clear any codec caches and discard
 *                  any pending samples and not yet returned encoded data.
 * @event:          Optional.
 *                  Event handler on the sink pad. This function should return
 *                  TRUE if the event was handled and should be discarded
 *                  (i.e. not unref'ed).
 * @pre_push:       Optional.
 *                  Called just prior to pushing (encoded data) buffer downstream.
 * @getcaps:        Optional.
 *                  Allows for a custom sink getcaps implementation (e.g.
 *                  for multichannel input specification).  If not implemented,
 *                  default returns gst_base_audio_encoder_proxy_getcaps
 *                  applied to sink template caps.
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @set_format and @handle_frame needs to be overridden.
 */
struct _GstBaseAudioEncoderClass {
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  gboolean      (*start)              (GstBaseAudioEncoder *enc);

  gboolean      (*stop)               (GstBaseAudioEncoder *enc);

  gboolean      (*set_format)         (GstBaseAudioEncoder *enc,
                                       GstAudioState *state);

  GstFlowReturn (*handle_frame)       (GstBaseAudioEncoder *enc,
                                       GstBuffer *buffer);

  void          (*flush)              (GstBaseAudioEncoder *enc);

  GstFlowReturn (*pre_push)           (GstBaseAudioEncoder *enc,
                                       GstBuffer *buffer);

  gboolean      (*event)              (GstBaseAudioEncoder *enc,
                                       GstEvent *event);

  GstCaps *     (*getcaps)            (GstBaseAudioEncoder *enc);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

GType           gst_base_audio_encoder_get_type         (void);

GstFlowReturn   gst_base_audio_encoder_finish_frame (GstBaseAudioEncoder * enc,
                                        GstBuffer *buffer, gint samples);

GstCaps *       gst_base_audio_encoder_proxy_getcaps (GstBaseAudioEncoder * enc,
                                                      GstCaps * caps);

GstCaps *       gst_base_audio_encoder_add_streamheader (GstCaps * caps,
                                                         GstBuffer * buf, ...);

G_END_DECLS

#endif /* __GST_BASE_AUDIO_ENCODER_H__ */
