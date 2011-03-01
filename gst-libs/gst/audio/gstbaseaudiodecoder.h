/* GStreamer
 * Copyright (C) 2009 Igalia S.L.
 * Author: Iago Toral Quiroga <itoral@igalia.com>
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

/**
 * GST_BASE_AUDIO_DECODER_INPUT_ADAPTER:
 * @obj: base audio codec instance
 *
 * Gives the pointer to the input #GstAdapter object of the element.
 */
#define GST_BASE_AUDIO_DECODER_INPUT_ADAPTER(obj)   (((GstBaseAudioDecoder *) (obj))->input_adapter)

/**
 * GST_BASE_AUDIO_DECODER_OUTPUT_ADAPTER:
 * @obj: base audio codec instance
 *
 * Gives the pointer to the output #GstAdapter object of the element.
 */
#define GST_BASE_AUDIO_DECODER_OUTPUT_ADAPTER(obj)   (((GstBaseAudioDecoder *) (obj))->output_adapter)

typedef struct _GstBaseAudioDecoder GstBaseAudioDecoder;
typedef struct _GstBaseAudioDecoderClass GstBaseAudioDecoderClass;
typedef struct _GstAudioState GstAudioState;

struct _GstAudioState
{
  gint channels;
  gint rate;
  gint bytes_per_sample;
  gint sample_depth;
  gint frame_size;
  GstSegment segment;
};

/**
 * GstBaseAudioDecoder:
 * @element: the parent element.
 * @caps_set: whether caps have been set on the codec's source pad.
 * @sinkpad: the sink pad.
 * @srcpad: the source pad.
 * @input_adapter: the input adapter that will be filled with the input buffers.
 * @output_adapter: the output adapter. Subclasses will read from the input
 *    adapter, process the data and fill the output adapter with the result.
 * @input_buffer_size: The minimum amount of data that should be present on the
 *    input adapter for the codec to process it.
 * @output_buffer_size: The minimum amount of data that should be present on the
 *    output adapter for the codec to push buffers out.
 * @bytes_in: total bytes that have been received.
 * @bytes_out: total bytes that have been pushed out.
 * @discont: whether the next buffer to push represents a discontinuity in the
 *     stream.
 * @state: Audio stream information. See #GstAudioState for details.
 * @codec_data: The codec data.
 * @started: Whether the codec has been started and is ready to process data
 *     or not.
 * @first_ts: timestamp of the first buffer in the input adapter.
 * @last_ts: timestamp of the last buffer in the input adapter.
 *
 * The opaque #GstBaseAudioDecoder data structure.
 */
struct _GstBaseAudioDecoder
{
  GstElement element;

  /*< private >*/
  gboolean caps_set;

  /*< protected >*/
  GstPad *sinkpad;
  GstPad *srcpad;
  GstAdapter *input_adapter;
  GstAdapter *output_adapter;
  guint input_buffer_size;
  guint output_buffer_size;
  guint64 bytes_in;
  guint64 bytes_out;
  gboolean discont;
  GstAudioState state;
  GstBuffer *codec_data;
  gboolean started;

  guint64 first_ts;
  guint64 last_ts;
};

/**
 * GstBaseAudioDecoderClass:
 * @parent_class: Element parent class
 * @start: Start processing. Ideal for opening resources in the subclass
 * @stop: Stop processing. Subclasses should use this to close resources.
 * @reset: Resets the codec. Called on discontinuities, etc.
 * @event: Override this to handle events arriving on the sink pad.
 * @handle_discont: Override to be notified on discontinuities.
 * @flush_input: Subclasses may implement this to flush the input adapter,
 *    processing any data present in it and filling the output adapter with the
 *    result. This could be necessary if it is possible for the codec to
 *    receive an end-of-stream event before all the data in the input
 *    adapter has been processed.
 * @flush_output: Subclasses may implement this to flush the output adapter,
 *    pushing buffers out through the codec's source pad when the end-of-stream
 *    event is received and there is data waiting to be processed in the
 *    adapters.
 * @process_data: Subclasses must implement this. They should read from the
 *    input adapter, encode/decode the data present in it and fill the 
 *    output adapter with the result.
 * @push_data: Normally, #GstBaseAudioDecoder will handle pushing buffers out.
 *     However, it is possible for developers to take control of when and how
 *     buffers are pushed out by overriding this method. If subclasses provide
 *     an implementation, #GstBaseAudioDecoder will not push any buffers,
 *     instead, whenever there is data on the output adapter, it will call this
 *     method on the subclass, which would be the sole responsible for 
 *     pushing the buffers out when appropriate.
 * @negotiate_src_caps: Subclasses can implement this method to provide
 * appropriate caps to be set on the codec's source pad. If they don't
 * provide this, they will be responsible for calling 
 * gst_base_audio_decoder_set_src_caps when appropriate.
 */
struct _GstBaseAudioDecoderClass
{
  GstElementClass parent_class;

  gboolean (*start) (GstBaseAudioDecoder *codec);
  gboolean (*stop) (GstBaseAudioDecoder *codec);
  gboolean (*reset) (GstBaseAudioDecoder *codec);

  GstFlowReturn (*event) (GstBaseAudioDecoder *codec, GstEvent *event);
  void (*handle_discont) (GstBaseAudioDecoder *codec, GstBuffer *buffer);
  gboolean (*flush_input) (GstBaseAudioDecoder *codec);
  gboolean (*flush_output) (GstBaseAudioDecoder *codec);
  GstFlowReturn (*process_data) (GstBaseAudioDecoder *codec);
  GstFlowReturn (*push_data) (GstBaseAudioDecoder *codec);
  GstCaps * (*negotiate_src_caps) (GstBaseAudioDecoder *codec,
      GstCaps *sink_caps);
};

GType gst_base_audio_decoder_get_type (void);
gboolean gst_base_audio_decoder_reset (GstBaseAudioDecoder *codec);
gboolean gst_base_audio_decoder_stop (GstBaseAudioDecoder *codec);
gboolean gst_base_audio_decoder_start (GstBaseAudioDecoder *codec);
gboolean gst_base_audio_decoder_flush (GstBaseAudioDecoder *codec);
gboolean gst_base_audio_decoder_set_src_caps (GstBaseAudioDecoder *codec,
    GstCaps *caps);
GstFlowReturn gst_base_audio_decoder_push_buffer (GstBaseAudioDecoder *codec,
    GstBuffer *buffer);

G_END_DECLS

#endif

