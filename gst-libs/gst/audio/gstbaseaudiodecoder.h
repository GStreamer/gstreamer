/* GStreamer
 * Copyright (C) 2009 Igalia S.L.
 * Author: Iago Toral Quiroga <itoral@igalia.com>
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

#ifndef _GST_BASE_AUDIO_DECODER_H_
#define _GST_BASE_AUDIO_DECODER_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstBaseAudioDecoder is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/audio/gstbaseaudioutils.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_AUDIO_DECODER \
  (gst_base_audio_decoder_get_type())
#define GST_BASE_AUDIO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_AUDIO_DECODER,GstBaseAudioDecoder))
#define GST_BASE_AUDIO_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_AUDIO_DECODER,GstBaseAudioDecoderClass))
#define GST_BASE_AUDIO_DECODER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_AUDIO_DECODER,GstBaseAudioDecoderClass))
#define GST_IS_BASE_AUDIO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_AUDIO_DECODER))
#define GST_IS_BASE_AUDIO_DECODER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_AUDIO_DECODER))

/**
 * GST_BASE_AUDIO_DECODER_SINK_NAME:
 *
 * The name of the templates for the sink pad.
 */
#define GST_BASE_AUDIO_DECODER_SINK_NAME    "sink"
/**
 * GST_BASE_AUDIO_DECODER_SRC_NAME:
 *
 * The name of the templates for the source pad.
 */
#define GST_BASE_AUDIO_DECODER_SRC_NAME     "src"

/**
 * GST_BASE_AUDIO_DECODER_SRC_PAD:
 * @obj: base audio codec instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 */
#define GST_BASE_AUDIO_DECODER_SRC_PAD(obj)         (((GstBaseAudioDecoder *) (obj))->srcpad)

/**
 * GST_BASE_AUDIO_DECODER_SINK_PAD:
 * @obj: base audio codec instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 */
#define GST_BASE_AUDIO_DECODER_SINK_PAD(obj)        (((GstBaseAudioDecoder *) (obj))->sinkpad)

typedef struct _GstBaseAudioDecoder GstBaseAudioDecoder;
typedef struct _GstBaseAudioDecoderClass GstBaseAudioDecoderClass;

typedef struct _GstBaseAudioDecoderPrivate GstBaseAudioDecoderPrivate;
typedef struct _GstBaseAudioDecoderContext GstBaseAudioDecoderContext;

/**
 * GstBaseAudioDecoderContext:
 * @state: a #GstAudioState describing input audio format
 * @eos: no (immediate) subsequent data in stream
 * @sync: stream parsing in sync
 * @delay: number of frames pending decoding (typically at least 1 for current)
 * @do_plc: whether subclass is prepared to handle (packet) loss concealment
 * @min_latency: min latency of element
 * @max_latency: max latency of element
 * @lookahead: decoder lookahead (in units of input rate samples)
 *
 * Transparent #GstBaseAudioEncoderContext data structure.
 */
struct _GstBaseAudioDecoderContext {
  /* input */
  /* (output) audio format */
  GstAudioState state;

  /* parsing state */
  gboolean eos;
  gboolean sync;

  /* misc */
  gint delay;

  /* output */
  gboolean do_plc;
  /* MT-protected (with LOCK) */
  GstClockTime min_latency;
  GstClockTime max_latency;
};

/**
 * GstBaseAudioDecoder:
 *
 * The opaque #GstBaseAudioDecoder data structure.
 */
struct _GstBaseAudioDecoder
{
  GstElement element;

  /*< protected >*/
  /* source and sink pads */
  GstPad         *sinkpad;
  GstPad         *srcpad;

  /* MT-protected (with STREAM_LOCK) */
  GstSegment      segment;
  GstBaseAudioDecoderContext *ctx;

  /* properties */
  GstClockTime    latency;
  GstClockTime    tolerance;
  gboolean        plc;

  /*< private >*/
  GstBaseAudioDecoderPrivate *priv;
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

/**
 * GstBaseAudioDecoderClass:
 * @start:          Optional.
 *                  Called when the element starts processing.
 *                  Allows opening external resources.
 * @stop:           Optional.
 *                  Called when the element stops processing.
 *                  Allows closing external resources.
 * @set_format:     Notifies subclass of incoming data format (caps).
 * @parse:          Optional.
 *                  Allows chopping incoming data into manageable units (frames)
 *                  for subsequent decoding.  This division is at subclass
 *                  discretion and may or may not correspond to 1 (or more)
 *                  frames as defined by audio format.
 * @handle_frame:   Provides input data (or NULL to clear any remaining data)
 *                  to subclass.  Input data ref management is performed by
 *                  base class, subclass should not care or intervene.
 * @flush:          Optional.
 *                  Instructs subclass to clear any codec caches and discard
 *                  any pending samples and not yet returned encoded data.
 *                  @hard indicates whether a FLUSH is being processed,
 *                  or otherwise a DISCONT (or conceptually similar).
 * @event:          Optional.
 *                  Event handler on the sink pad. This function should return
 *                  TRUE if the event was handled and should be discarded
 *                  (i.e. not unref'ed).
 * @pre_push:       Optional.
 *                  Called just prior to pushing (encoded data) buffer downstream.
 *                  Subclass has full discretionary access to buffer,
 *                  and a not OK flow return will abort downstream pushing.
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @handle_frame (and likely @set_format) needs to be
 * overridden.
 */
struct _GstBaseAudioDecoderClass
{
  GstElementClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */

  gboolean      (*start)              (GstBaseAudioDecoder *dec);

  gboolean      (*stop)               (GstBaseAudioDecoder *dec);

  gboolean      (*set_format)         (GstBaseAudioDecoder *dec,
                                       GstCaps *caps);

  GstFlowReturn (*parse)              (GstBaseAudioDecoder *dec,
                                       GstAdapter *adapter,
                                       gint *offset, gint *length);

  GstFlowReturn (*handle_frame)       (GstBaseAudioDecoder *dec,
                                       GstBuffer *buffer);

  void          (*flush)              (GstBaseAudioDecoder *dec, gboolean hard);

  GstFlowReturn (*pre_push)           (GstBaseAudioDecoder *dec,
                                       GstBuffer **buffer);

  gboolean      (*event)              (GstBaseAudioDecoder *dec,
                                       GstEvent *event);

  /*< private >*/
  gpointer       _gst_reserved[GST_PADDING_LARGE];
};

GstFlowReturn     gst_base_audio_decoder_finish_frame (GstBaseAudioDecoder * dec,
                                                       GstBuffer * buf, gint frames);

GType gst_base_audio_decoder_get_type (void);

G_END_DECLS

#endif

