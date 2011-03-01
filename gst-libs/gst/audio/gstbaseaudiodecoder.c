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

/**
 * SECTION:gstbaseaudiodecoder
 * @short_description: Base class for codec elements
 * @see_also: #GstBaseTransform, #GstBaseSource, #GstBaseSink
 *
 * #GstBaseAudioDecoder is the base class for codec elements ion GStreamer. It is
 * a layer on top of #GstElement that provides simplified interface to plugin
 * writers, hangling many details for you. Its way of operation is explained
 * below.
 *
 * Subclasses are responsible for specifying the codec's source pad caps. For
 * that purpose they should provide an implementation of ::negotiate_src_caps.
 * If the subclass provides an implementation of this method, it will be
 * invoked by #GstBaseAudioDecoder on its sink_setcaps function. Otherwise, if
 * the subclass does not provide an implementation of this method, the subclass
 * will be responsible for calling gst_base_audio_decoder_set_src_caps() to
 * complete the caps negotiation before any buffers are pushed out.
 *
 * Each buffer received on the codec's sink pad is pushed to its input 
 * adapter. When there is enough data present in the input adapter
 * (configured in the #GstBaseAudioDecoder:input-buffer-size
 * property), the method ::process_data is called on the subclass. Subclasses
 * must provide an implementation of this method, which would read from the
 * input adapter, encode or decode the data, and push it to the output adapter.
 * If #GstBaseAudioDecoder:input-buffer-size is set to 0 ::process_data will be
 * invoked as soon as there is any data on the input adapter.
 *
 * Similarly, when there is enough data present on the output adapter,
 * (configured in the #GstBaseAudioDecoder:output-buffer-size property),
 * buffers will be pushed out through the codec's source pad. If 
 * #GstBaseAudioDecoder:output-buffer-size is set to 0 a buffer will be pushed
 * out as soon as there is any data present on the output adapter. Notice
 * that if no implementation of ::negotiate_src_caps has been provided by the
 * subclass, it must call gst_base_audio_decoder_set_src_caps() to complete
 * the caps negotiation process or otherwise attempting to push buffers
 * through the codec's source pad will fail.
 *
 * It is possible for subclasses to take control on how and when buffers
 * are pushed out by overriding the ::push_data method. If subclasses 
 * provide an implementation of this method #GstBaseAudioDecoder will
 * not push buffers out by itself, instead, whenever there* is data present
 * in the output adapter, it will invoke ::push_data on subclass, which 
 * will implement there any logic necessary for pushing  buffers out when
 * appropriate. In this mode of operation, the property
 *  ::output_buffer_size is ignored in #GstBaseAudioDecoder. In any case,
 * buffers should be pushed using gst_base_audio_decoder_push_buffer().
 *
 * #GstBaseAudioDecoder checks for discontinuities and handles them
 * appropriately when pushing buffers out (setting the discontinuous 
 * flag on the output buffers when necessary). Subclasses can check if
 * the data present on the adapters represents a discontinuity by checking
 * the discont field of #GstBaseAudioDecoder. Also, subclasses can provide
 * an implementation for the ::handle_discont method, which will be invoked
 * whenever a discontinuity is detected on the source stream.
 *
 * Because data is not processed immediately and is stored in adapters,
 * depending on how the actual codec operates it may be possible to
 * receive an end-of-stream event before all the data in the adapters
 * has been processed and pushed out. If this can happen, the subclass
 * must provide implementation of the ::flush_input method, which should
 * then read the data present int the input adapter, process it and 
 * store the result in the output adapter. The subclass may also want
 * provide an implementation for the ::flush_output method, which would
 * take care of reading the data from the output adapter and push it 
 * out through the codec's source pad. If no implementation is provided
 * for the ::flush_out method, #GstBaseAudioDecoder will create a single
 * buffer with all the data present in the output adapter and push it
 * out. If a subclass needs to force a flush on the adapters for some
 * reason, it should call gst_base_audio_decoder_flush(), which will then
 * invoke ::flush_input and/or ::flush_output appropriately.
 *
 * Subclasses may provide an implementation for the ::start, ::stop
 * and ::reset methods when needed. This methods will be called
 * from #GstBaseAudioDecoder when needed (on state changes,
 * discontinuities, etc), so they must never invoke the
 * implementation on the parent class. When a subclass needs to
 * start, stop or reset the codec itself, it should use the public
 * functions gst_base_audio_decoder_{start,stop,reset}(), which call
 * the corresponding methods on the parent class, which will then
 * call the functions provided by the subclass (if any).
 *
 * #GstBaseAudioDecoder also provides an sink event handler.
 * Subclasses that want to be notified on these events, can provide
 * an implementation of the ::event function, which will be called after
 * #GstBaseAudioDecoder has processed the event itself.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstbaseaudiodecoder.h"
#include <gst/audio/audio.h>
#include <string.h>

/*
 * FIXME: maybe we need more work with the segments (see ac3 decoder)
 */

GST_DEBUG_CATEGORY (baseaudiodecoder_debug);
#define GST_CAT_DEFAULT baseaudiodecoder_debug

/* ----- Signals and properties ----- */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_INPUT_BUFFER_SIZE,
  PROP_OUTPUT_BUFFER_SIZE
};

/* ----- Function prototypes ----- */

static void gst_base_audio_decoder_finalize (GObject * object);
static void gst_base_audio_decoder_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_base_audio_decoder_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_base_audio_decoder_change_state (GstElement *
    element, GstStateChange transition);
static gboolean gst_base_audio_decoder_sink_event (GstPad * pad,
    GstEvent * event);
static gboolean gst_base_audio_decoder_sink_setcaps (GstPad * pad,
    GstCaps * caps);
static GstFlowReturn gst_base_audio_decoder_chain (GstPad * pad,
    GstBuffer * buf);
static void gst_base_audio_decoder_handle_discont (GstBaseAudioDecoder * codec,
    GstBuffer * buf);

/* ----- GObject setup ----- */

GST_BOILERPLATE (GstBaseAudioDecoder, gst_base_audio_decoder, GstElement,
    GST_TYPE_ELEMENT);

static void
gst_base_audio_decoder_base_init (gpointer g_class)
{
  GST_DEBUG_CATEGORY_INIT (baseaudiodecoder_debug, "baseaudiodecoder", 0,
      "Base Audio Codec Classes");
}

static void
gst_base_audio_decoder_class_init (GstBaseAudioDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_base_audio_decoder_set_property;
  gobject_class->get_property = gst_base_audio_decoder_get_property;
  gobject_class->finalize = gst_base_audio_decoder_finalize;

  element_class->change_state = gst_base_audio_decoder_change_state;

  klass->start = NULL;
  klass->stop = NULL;
  klass->reset = NULL;
  klass->event = NULL;
  klass->handle_discont = NULL;
  klass->flush_input = NULL;
  klass->flush_output = NULL;
  klass->process_data = NULL;
  klass->push_data = NULL;
  klass->negotiate_src_caps = NULL;

  /* Properties */
  g_object_class_install_property (gobject_class, PROP_INPUT_BUFFER_SIZE,
      g_param_spec_uint ("input-buffer-size", "Input buffer size",
          "Size of the input buffers in bytes (0 for not setting a "
          "particular size)", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class, PROP_OUTPUT_BUFFER_SIZE,
      g_param_spec_uint ("output-buffer-size", "Output buffer size",
          "Size of the output buffers in bytes (0 for not setting a "
          "particular size)", 0, G_MAXUINT, 0, G_PARAM_READWRITE));
}

static void
gst_base_audio_decoder_init (GstBaseAudioDecoder * codec,
    GstBaseAudioDecoderClass * klass)
{
  GstPadTemplate *pad_template;

  GST_DEBUG ("gst_base_audio_decoder_init");

  /* Setup sink pad */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "sink");
  g_return_if_fail (pad_template != NULL);

  codec->sinkpad = gst_pad_new_from_template (pad_template, "sink");
  gst_pad_set_event_function (codec->sinkpad,
      gst_base_audio_decoder_sink_event);
  gst_pad_set_setcaps_function (codec->sinkpad,
      gst_base_audio_decoder_sink_setcaps);
  gst_pad_set_chain_function (codec->sinkpad, gst_base_audio_decoder_chain);
  gst_element_add_pad (GST_ELEMENT (codec), codec->sinkpad);

  /* Setup source pad */
  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  codec->srcpad = gst_pad_new_from_template (pad_template, "src");
  gst_pad_use_fixed_caps (codec->srcpad);
  gst_element_add_pad (GST_ELEMENT (codec), codec->srcpad);

  /* Setup adapters */
  codec->input_adapter = gst_adapter_new ();
  codec->output_adapter = gst_adapter_new ();
  codec->input_buffer_size = 0;
  codec->output_buffer_size = 0;

  /* Setup state */
  memset (&codec->state, 0, sizeof (GstAudioState));
  gst_segment_init (&codec->state.segment, GST_FORMAT_TIME);

  codec->started = FALSE;
  codec->bytes_in = 0;
  codec->bytes_out = 0;
  codec->discont = TRUE;
  codec->caps_set = FALSE;
  codec->first_ts = -1;
  codec->last_ts = -1;
}

static void
gst_base_audio_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstBaseAudioDecoder *codec;

  codec = GST_BASE_AUDIO_DECODER (object);

  switch (prop_id) {
    case PROP_INPUT_BUFFER_SIZE:
      g_value_set_uint (value, codec->input_buffer_size);
      break;
    case PROP_OUTPUT_BUFFER_SIZE:
      g_value_set_uint (value, codec->output_buffer_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_decoder_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstBaseAudioDecoder *codec;

  codec = GST_BASE_AUDIO_DECODER (object);

  switch (prop_id) {
    case PROP_INPUT_BUFFER_SIZE:
      codec->input_buffer_size = g_value_get_uint (value);
      break;
    case PROP_OUTPUT_BUFFER_SIZE:
      codec->output_buffer_size = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_base_audio_decoder_finalize (GObject * object)
{
  GstBaseAudioDecoder *codec;

  g_return_if_fail (GST_IS_BASE_AUDIO_DECODER (object));
  codec = GST_BASE_AUDIO_DECODER (object);

  if (codec->input_adapter) {
    g_object_unref (codec->input_adapter);
  }
  if (codec->output_adapter) {
    g_object_unref (codec->output_adapter);
  }
  if (codec->codec_data) {
    gst_buffer_unref (codec->codec_data);
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* ----- Private element implementation ----- */

static void
gst_base_audio_decoder_read_state_from_caps (GstBaseAudioDecoder * codec,
    GstCaps * caps)
{
  GstStructure *structure;
  const GValue *codec_data;

  structure = gst_caps_get_structure (caps, 0);

  if (codec->codec_data) {
    gst_buffer_unref (codec->codec_data);
    codec->codec_data = NULL;
  }

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data && G_VALUE_TYPE (codec_data) == GST_TYPE_BUFFER) {
    codec->codec_data = gst_value_get_buffer (codec_data);
  }

  gst_structure_get_int (structure, "channels", &codec->state.channels);
  gst_structure_get_int (structure, "rate", &codec->state.rate);
  gst_structure_get_int (structure, "depth", &codec->state.sample_depth);
  gst_structure_get_int (structure, "width", &codec->state.bytes_per_sample);
  codec->state.bytes_per_sample /= 8;
  codec->state.frame_size =
      codec->state.bytes_per_sample * codec->state.channels;
}

static gboolean
gst_base_audio_decoder_sink_event (GstPad * pad, GstEvent * event)
{
  GstBaseAudioDecoder *codec;
  GstBaseAudioDecoderClass *codec_class;
  gboolean ret = FALSE;

  codec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      /* Flush any data still present in the adapters */
      gst_base_audio_decoder_flush (codec);
      ret = gst_pad_push_event (codec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_base_audio_decoder_reset (codec);
      ret = gst_pad_push_event (codec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      GST_DEBUG ("news egment %lld %lld", start, time);
      gst_segment_set_newsegment_full (&codec->state.segment,
          update, rate, arate, format, start, stop, time);
      ret = gst_pad_push_event (codec->srcpad, event);
      break;
    }
    default:
      ret = gst_pad_push_event (codec->srcpad, event);
      break;
  }

  /* Let the subclass see the event too */
  if (codec_class->event) {
    if (!codec_class->event (codec, event)) {
      ret = FALSE;
      goto subclass_event_error;
    }
  }

done:
  gst_object_unref (codec);
  return ret;

newseg_wrong_format:
  GST_DEBUG ("received non TIME newsegment");
  gst_event_unref (event);
  goto done;

newseg_wrong_rate:
  GST_DEBUG ("negative rates not supported");
  gst_event_unref (event);
  goto done;

subclass_event_error:
  GST_DEBUG ("codec implementation failed to proces event");
  gst_event_unref (event);
  goto done;
}

static gboolean
gst_base_audio_decoder_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstBaseAudioDecoder *codec;
  GstBaseAudioDecoderClass *codec_class;

  codec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  GST_DEBUG ("gst_base_audio_decoder_sink_setcaps %" GST_PTR_FORMAT, caps);

  /* Let the subclass provide the source caps and we will set them
     on the codec's source pad */
  if (codec_class->negotiate_src_caps) {
    GstCaps *src_caps;
    src_caps = codec_class->negotiate_src_caps (codec, caps);
    if (!gst_base_audio_decoder_set_src_caps (codec, src_caps)) {
      GST_DEBUG ("Caps negotiation failed!");
      g_object_unref (codec);
      gst_caps_unref (src_caps);
      return FALSE;
    }
    gst_caps_unref (src_caps);
  } else {
    /* If the subclass does not provide a negotiate_src_caps method, then
       it will be responsible for calling gst_base_audio_decoder_set_src_caps
       with appropriate caps before we try to push buffers out */
    GST_DEBUG ("Subclass does not provide negotiate_src_caps, is that ok?");
  }

  gst_base_audio_decoder_start (codec);

  g_object_unref (codec);

  return TRUE;
}

static GstStateChangeReturn
gst_base_audio_decoder_change_state (GstElement * element,
    GstStateChange transition)
{
  GstBaseAudioDecoder *codec;
  GstBaseAudioDecoderClass *codec_class;
  GstStateChangeReturn ret;

  codec = GST_BASE_AUDIO_DECODER (element);
  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_base_audio_decoder_start (codec)) {
        goto start_failed;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_base_audio_decoder_reset (codec)) {
        goto reset_failed;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_base_audio_decoder_stop (codec)) {
        goto stop_failed;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;

start_failed:
  {
    GST_ELEMENT_ERROR (codec, LIBRARY, INIT, (NULL), ("Failed to start codec"));
    return GST_STATE_CHANGE_FAILURE;
  }
reset_failed:
  {
    GST_ELEMENT_ERROR (codec, LIBRARY, INIT, (NULL), ("Failed to reset codec"));
    return GST_STATE_CHANGE_FAILURE;
  }
stop_failed:
  {
    GST_ELEMENT_ERROR (codec, LIBRARY, INIT, (NULL), ("Failed to stop codec"));
    return GST_STATE_CHANGE_FAILURE;
  }
}

static void
gst_base_audio_decoder_handle_discont (GstBaseAudioDecoder * codec,
    GstBuffer * buffer)
{
  GstBaseAudioDecoderClass *codec_class;

  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  /* Reset codec on discont */
  if (codec->started) {
    gst_base_audio_decoder_reset (codec);
  }

  codec->discont = TRUE;

  /* Let the subclass do its stuff too if that is needed */
  if (codec_class->handle_discont) {
    codec_class->handle_discont (codec, buffer);
  }
}

static GstFlowReturn
gst_base_audio_decoder_chain (GstPad * pad, GstBuffer * buf)
{
  GstBaseAudioDecoder *codec;
  GstBaseAudioDecoderClass *codec_class;
  GstBuffer *outbuf;
  GstFlowReturn ret;
  guint bytes_ready;
  guint64 timestamp;

  codec = GST_BASE_AUDIO_DECODER (gst_pad_get_parent (pad));
  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  GST_DEBUG ("gst_base_audio_decoder_chain");

  /* Make sure we have started our codec */
  if (G_UNLIKELY (!codec->started)) {
    if (G_UNLIKELY (!gst_base_audio_decoder_start (codec))) {
      GST_ELEMENT_ERROR (codec, LIBRARY, INIT, (NULL),
          ("Failed to start codec"));
      gst_object_unref (codec);
      return GST_FLOW_ERROR;
    }
  }

  /* Handle timestamps */
  timestamp = GST_BUFFER_TIMESTAMP (buf);
  if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GST_DEBUG ("buffer timestamp %" GST_TIME_FORMAT " duration:%"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));
    if (gst_adapter_available (codec->input_adapter) == 0) {
      codec->first_ts = timestamp;
    }
    codec->last_ts = timestamp;
  }

  /* Check for discontinuity */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG ("received DISCONT buffer");
    gst_base_audio_decoder_handle_discont (codec, buf);
  }

  /* Push buffer to the input adapter so the codec can
     take data from it as needed */
  codec->bytes_in += GST_BUFFER_SIZE (buf);
  gst_adapter_push (codec->input_adapter, buf);

  GST_DEBUG ("Input buffer size: %ld bytes", GST_BUFFER_SIZE (buf));

  /* Check if we have enough data to be processed. While we have
     enough data on the input adapter, instruct the element to
     process it */
  ret = GST_FLOW_OK;
  bytes_ready = gst_adapter_available (codec->input_adapter);
  while (ret == GST_FLOW_OK && bytes_ready > 0 &&
      bytes_ready >= codec->input_buffer_size) {
    GST_DEBUG ("Processing data");
    ret = codec_class->process_data (codec);
    bytes_ready = gst_adapter_available (codec->input_adapter);
    GST_DEBUG ("%ld bytes remaining on the input", bytes_ready);
  }

  /* FIXME: is it possible that we have enough data in the output
     adapter but we have to wait for more data before we can
     push buffers out? In that case we need a custom GST_FLOW.
     Not sure if we could handle pushing buffers here in that
     case though, since we always push in output_buffer_size
     blocks. */

  /* If no error was raised, check if we can push buffers out */
  if (G_LIKELY (ret == GST_FLOW_OK)) {
    bytes_ready = gst_adapter_available (codec->output_adapter);
    GST_DEBUG ("Processed input correctly");
    GST_DEBUG ("%ld bytes on the output", bytes_ready);

    /* If the subclass wants to control how buffers are pushed out
       let it do it */
    if (bytes_ready > 0 && codec_class->push_data) {
      GST_DEBUG ("Calling push_data on the subclass");
      codec_class->push_data (codec);
    } else if (bytes_ready > 0 && bytes_ready >= codec->output_buffer_size) {
      /* We have enough data in the output adapter, so take a buffer, apply
         clipping, push it out and repeat while we have enough data */
      guint bytes_to_push;

      bytes_to_push =
          codec->output_buffer_size ? codec->output_buffer_size : bytes_ready;

      do {
        GST_DEBUG ("Pushing a buffer out (%ld bytes)", bytes_to_push);

        outbuf = gst_adapter_take_buffer (codec->output_adapter, bytes_to_push);

        /* Set buffer timestamp/duration if needed (and possible) */
        if (!GST_BUFFER_TIMESTAMP_IS_VALID (outbuf) && codec->first_ts != -1) {
          GST_DEBUG ("Computing output buffer timestamp");
          GST_BUFFER_TIMESTAMP (outbuf) = codec->first_ts;
        }

        if (!GST_BUFFER_DURATION_IS_VALID (outbuf) && codec->state.frame_size) {
          guint nsamples;
          GST_DEBUG ("Computing output buffer duration");
          nsamples = GST_BUFFER_SIZE (outbuf) / codec->state.frame_size;
          GST_BUFFER_DURATION (outbuf) =
              gst_util_uint64_scale_int (GST_SECOND, nsamples,
              codec->state.rate);
        }

        if (codec->first_ts != -1) {
          codec->first_ts += GST_BUFFER_DURATION (outbuf);
          if (codec->first_ts > codec->last_ts) {
            codec->last_ts = codec->first_ts;
          }
        }

        GST_DEBUG ("out buffer timestamp %" GST_TIME_FORMAT " duration:%"
            GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
            GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

        /* Clip buffer */
        if (codec->state.segment.format == GST_FORMAT_TIME ||
            codec->state.segment.format == GST_FORMAT_DEFAULT) {
          GST_DEBUG ("Clipping buffer");
          outbuf = gst_audio_buffer_clip (outbuf, &codec->state.segment,
              codec->state.rate, codec->state.frame_size);
        }

        /* Set DISCONT flag on the output buffer if needed */
        if (G_LIKELY (outbuf)) {
          if (G_UNLIKELY (codec->discont)) {
            GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
            codec->discont = FALSE;
            GST_DEBUG ("Buffer is discont");
          }

          ret = gst_base_audio_decoder_push_buffer (codec, outbuf);
        }

        /* See if we can push another buffer */
        bytes_ready = gst_adapter_available (codec->output_adapter);
        GST_DEBUG ("%ld bytes left on the output", bytes_ready);
      } while (ret == GST_FLOW_OK && bytes_ready >= bytes_to_push);
    } else {
      /* We need more data before we can push a buffer out */
      GST_DEBUG ("Not pushing out, need more data");
      ret = GST_FLOW_OK;
    }
  } else {
    /* We got an error */
    GST_DEBUG ("Got error while processing data");
  }

  GST_DEBUG ("chain-done");

  return ret;
}

/* ----- Element public API ----- */

/**
 * gst_base_audio_decoder_reset:
 * @codec: The #GstBaseAudioDecoder instance.
 *
 * Resets the codec.
 *
 * This method will also invoke the subclass's reset virtual method
 * if available. Niotice that reseting the codec will clear the
 * input and output adapters.
 * 
 * Returns: TRUE if the start operation was successful.
 */
gboolean
gst_base_audio_decoder_reset (GstBaseAudioDecoder * codec)
{
  GstBaseAudioDecoderClass *codec_class;

  GST_DEBUG ("gst_base_audio_decoder_reset");

  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  gst_adapter_clear (codec->input_adapter);
  gst_adapter_clear (codec->output_adapter);

  /* FIXME: is this needed? */
  gst_segment_init (&codec->state.segment, GST_FORMAT_TIME);

  codec->first_ts = -1;
  codec->last_ts = -1;

  if (codec_class->reset) {
    codec_class->reset (codec);
  }

  return TRUE;
}

/**
 * gst_base_audio_decoder_stop:
 * @codec: The #GstBaseAudioDecoder instance.
 *
 * Stop the codec. Normally this will be used for closing resource.
 *
 * This method will also invoke the subclass's stop virtual method
 * if available.
 * 
 * Returns: TRUE if the start operation was successful.
 */
gboolean
gst_base_audio_decoder_stop (GstBaseAudioDecoder * codec)
{
  GstBaseAudioDecoderClass *codec_class;

  GST_DEBUG ("gst_base_audio_decoder_stop");

  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  gst_base_audio_decoder_reset (codec);

  codec->bytes_in = 0;
  codec->bytes_out = 0;

  if (codec_class->stop) {
    codec_class->stop (codec);
  }

  codec->started = FALSE;

  return TRUE;
}

/**
 * gst_base_audio_decoder_start:
 * @codec: The #GstBaseAudioDecoder instance.
 *
 * Setup the codec so it can start processing data. Normally
 * this will be used for opening resources needed for operation.
 *
 * This method will also invoke the subclass's start virtual method
 * if available.
 * 
 * Returns: TRUE if the start operation was successful.
 */
gboolean
gst_base_audio_decoder_start (GstBaseAudioDecoder * codec)
{
  GstBaseAudioDecoderClass *codec_class;

  GST_DEBUG ("gst_base_audio_decoder_start");

  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  gst_base_audio_decoder_reset (codec);

  codec->bytes_in = 0;
  codec->bytes_out = 0;

  if (codec_class->start) {
    codec_class->start (codec);
  }

  codec->started = TRUE;

  return TRUE;
}

/**
 * gst_base_audio_decoder_flush:
 * @codec: The #GstBaseAudioDecoder instance.
 *
 * Flushes the input and output adapters. Subclasses should provide
 * a flush_input implementation to allow flushing the input adapter.
 * For the output adapter subclasses should provide a flush_output
 * implementation. If no flush_output implementation is provided
 * the output adapter will be flushed by pushing a single buffer
 * containing all the data present in the output adapter.
 * 
 * It is guaranteed that any data present in the adapters will be cleared
 * after calling this method even if the operation flush 
 * operation was not successfull.
 *
 * Returns: TRUE if the flush operation was successful (any data present in
 * the adapters was properly processed).
 */
gboolean
gst_base_audio_decoder_flush (GstBaseAudioDecoder * codec)
{
  GstFlowReturn ret_i = GST_FLOW_OK;
  GstFlowReturn ret_o = GST_FLOW_OK;
  guint bytes;
  GstBaseAudioDecoderClass *codec_class;

  GST_DEBUG ("gst_base_audio_decoder_flush");

  codec_class = GST_BASE_AUDIO_DECODER_GET_CLASS (codec);

  /* Flush input adapter */
  bytes = gst_adapter_available (codec->input_adapter);
  if (bytes > 0) {
    GST_DEBUG ("Flushing input adapter");
    /* If the subclass provides a flush_input implementation, use that.
       Otherwise we will clear the adapter and lose the data */
    if (codec_class->flush_input) {
      ret_i = codec_class->flush_input (codec);
      if (ret_i != GST_FLOW_OK) {
        GST_DEBUG ("failed to flush input");
      }
    } else {
      GST_DEBUG ("Received EOS but cannot flush input, data will be lost");
      ret_i = GST_FLOW_ERROR;
    }
    gst_adapter_clear (codec->input_adapter);
  }

  /* Flush output adapter */
  bytes = gst_adapter_available (codec->output_adapter);
  if (bytes > 0) {
    /* If the subclass provides a flush_output implementation, use that.
       Otherwise just push a single buffer with the adapter contents */
    GST_DEBUG ("Flushing output adapter");
    if (codec_class->flush_output) {
      ret_o = codec_class->flush_output (codec);
      if (ret_o != GST_FLOW_OK) {
        GST_DEBUG ("failed to flush output (flush_output)");
      }
    } else {
      GstBuffer *outbuf =
          gst_adapter_take_buffer (codec->output_adapter, bytes);
      ret_o = gst_base_audio_decoder_push_buffer (codec, outbuf);
      gst_buffer_unref (outbuf);
      if (ret_o != GST_FLOW_OK) {
        GST_DEBUG ("Forced output flush failed");
      }
    }
    gst_adapter_clear (codec->output_adapter);
  }

  return (ret_i == GST_FLOW_OK && ret_o == GST_FLOW_OK);
}

/**
 * gst_base_audio_decoder_set_src_caps:
 * @codec: #GstBaseAudioDecoder instance
 * @caps: The caps to set on the source pad of @codec.
 *
 * Attempts to set @caps as the source caps of @codec. If the new caps
 * are accepted on the source pad, this will issue a flush on the adapters
 * to ensure that any data received with the old caps is processed first
 * and a reset of the codec.
 *
 * Returns: TRUE if caps were set successfully.
 */
gboolean
gst_base_audio_decoder_set_src_caps (GstBaseAudioDecoder * codec,
    GstCaps * caps)
{
  gboolean ret;

  GST_DEBUG ("gst_base_audio_decoder_set_src_caps %" GST_PTR_FORMAT, caps);

  /* First, check if the pad accepts the new caps */
  if (!gst_pad_accept_caps (codec->srcpad, caps)) {
    GST_DEBUG ("pad does not accept new caps");
    return FALSE;
  }

  /* If we have data in our adapters we should probably flush first */
  gst_base_audio_decoder_flush (codec);

  /* Set the caps on the pad  */
  ret = gst_pad_set_caps (codec->srcpad, caps);

  /* And update the state of the codec from the caps */
  if (ret) {
    gst_base_audio_decoder_read_state_from_caps (codec, caps);
    codec->caps_set = TRUE;
  }

  return ret;
}

/**
 * gst_base_audio_decoder_push_buffer:
 * @codec: #GstBaseAudioDecoder instance
 * @buffer: a #GstBuffer.
 *
 * Pushes a buffer through the source pad.
 *
 * Returns: a #GstFlowReturn indicating the result of the push operation.
 */
GstFlowReturn
gst_base_audio_decoder_push_buffer (GstBaseAudioDecoder * codec,
    GstBuffer * buffer)
{
  codec->bytes_out += GST_BUFFER_SIZE (buffer);
  return gst_pad_push (codec->srcpad, buffer);
}
