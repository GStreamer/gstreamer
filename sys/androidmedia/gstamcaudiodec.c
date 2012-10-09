/*
 * Initially based on gst-omx/omx/gstomxvideodec.c
 *
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/multichannel.h>
#include <string.h>

#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy memcpy
#endif

#include "gstamcaudiodec.h"
#include "gstamc-constants.h"

GST_DEBUG_CATEGORY_STATIC (gst_amc_audio_dec_debug_category);
#define GST_CAT_DEFAULT gst_amc_audio_dec_debug_category

/* prototypes */
static void gst_amc_audio_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_amc_audio_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_amc_audio_dec_open (GstAudioDecoder * decoder);
static gboolean gst_amc_audio_dec_close (GstAudioDecoder * decoder);
static gboolean gst_amc_audio_dec_start (GstAudioDecoder * decoder);
static gboolean gst_amc_audio_dec_stop (GstAudioDecoder * decoder);
static gboolean gst_amc_audio_dec_set_format (GstAudioDecoder * decoder,
    GstCaps * caps);
static void gst_amc_audio_dec_flush (GstAudioDecoder * decoder, gboolean hard);
static GstFlowReturn gst_amc_audio_dec_handle_frame (GstAudioDecoder * decoder,
    GstBuffer * buffer);

static GstFlowReturn gst_amc_audio_dec_drain (GstAmcAudioDec * self);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_amc_audio_dec_debug_category, "amcaudiodec", 0, \
      "Android MediaCodec audio decoder");

GST_BOILERPLATE_FULL (GstAmcAudioDec, gst_amc_audio_dec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER, DEBUG_INIT);

static GstCaps *
create_sink_caps (const GstAmcCodecInfo * codec_info)
{
  GstCaps *ret;
  gint i;

  ret = gst_caps_new_empty ();

  for (i = 0; i < codec_info->n_supported_types; i++) {
    const GstAmcCodecType *type = &codec_info->supported_types[i];

    if (strcmp (type->mime, "audio/mpeg") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1,
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "audio/3gpp") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/AMR",
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "audio/amr-wb") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/AMR-WB",
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "audio/mp4a-latm") == 0) {
      gint j;
      GstStructure *tmp, *tmp2;
      gboolean have_profile = FALSE;
      GValue va = { 0, };
      GValue v = { 0, };

      g_value_init (&va, GST_TYPE_LIST);
      g_value_init (&v, G_TYPE_STRING);
      g_value_set_string (&v, "raw");
      gst_value_list_append_value (&va, &v);
      g_value_set_string (&v, "adts");
      gst_value_list_append_value (&va, &v);
      g_value_unset (&v);

      tmp = gst_structure_new ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4,
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "framed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_structure_set_value (tmp, "stream-format", &va);
      g_value_unset (&va);

      for (j = 0; j < type->n_profile_levels; j++) {
        const gchar *profile;

        profile =
            gst_amc_aac_profile_to_string (type->profile_levels[j].profile);

        if (!profile) {
          GST_ERROR ("Unable to map AAC profile 0x%08x",
              type->profile_levels[j].profile);
          continue;
        }

        tmp2 = gst_structure_copy (tmp);
        gst_structure_set (tmp2, "profile", G_TYPE_STRING, profile, NULL);
        gst_caps_merge_structure (ret, tmp2);

        have_profile = TRUE;
      }

      if (!have_profile) {
        gst_caps_merge_structure (ret, tmp);
      } else {
        gst_structure_free (tmp);
      }
    } else if (strcmp (type->mime, "audio/g711-alaw") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/x-alaw",
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "audio/g711-mlaw") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/x-mulaw",
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "audio/vorbis") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/x-vorbis",
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "audio/flac") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/x-flac",
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "framed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else if (strcmp (type->mime, "audio/mpeg-L2") == 0) {
      GstStructure *tmp;

      tmp = gst_structure_new ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 1,
          "layer", G_TYPE_INT, 2,
          "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT,
          "parsed", G_TYPE_BOOLEAN, TRUE, NULL);
      gst_caps_merge_structure (ret, tmp);
    } else {
      GST_WARNING ("Unsupported mimetype '%s'", type->mime);
    }
  }

  return ret;
}

static const gchar *
caps_to_mime (GstCaps * caps)
{
  GstStructure *s;
  const gchar *name;

  s = gst_caps_get_structure (caps, 0);
  if (!s)
    return NULL;

  name = gst_structure_get_name (s);

  if (strcmp (name, "audio/mpeg") == 0) {
    gint mpegversion;

    if (!gst_structure_get_int (s, "mpegversion", &mpegversion))
      return NULL;

    if (mpegversion == 1) {
      gint layer;

      if (!gst_structure_get_int (s, "layer", &layer) || layer == 3)
        return "audio/mpeg";
      else if (layer == 2)
        return "audio/mpeg-L2";
    } else if (mpegversion == 2 || mpegversion == 4) {
      return "audio/mp4a-latm";
    }
  } else if (strcmp (name, "audio/AMR") == 0) {
    return "audio/3gpp";
  } else if (strcmp (name, "audio/AMR-WB") == 0) {
    return "audio/amr-wb";
  } else if (strcmp (name, "audio/x-alaw") == 0) {
    return "audio/g711-alaw";
  } else if (strcmp (name, "audio/x-mulaw") == 0) {
    return "audio/g711-mlaw";
  } else if (strcmp (name, "audio/x-vorbis") == 0) {
    return "audio/vorbis";
  }

  return NULL;
}

static GstCaps *
create_src_caps (const GstAmcCodecInfo * codec_info)
{
  GstCaps *ret;

  ret = gst_caps_new_simple ("audio/x-raw-int",
      "rate", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "channels", GST_TYPE_INT_RANGE, 1, G_MAXINT,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);

  return ret;
}

static void
gst_amc_audio_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstAmcAudioDecClass *audiodec_class = GST_AMC_AUDIO_DEC_CLASS (g_class);
  const GstAmcCodecInfo *codec_info;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar *longname;

  codec_info =
      g_type_get_qdata (G_TYPE_FROM_CLASS (g_class), gst_amc_codec_info_quark);
  /* This happens for the base class and abstract subclasses */
  if (!codec_info)
    return;

  audiodec_class->codec_info = codec_info;

  /* Add pad templates */
  caps = create_sink_caps (codec_info);
  templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  caps = create_src_caps (codec_info);
  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  longname = g_strdup_printf ("Android MediaCodec %s", codec_info->name);
  gst_element_class_set_details_simple (element_class,
      codec_info->name,
      "Codec/Decoder/Audio",
      longname, "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
  g_free (longname);
}

static void
gst_amc_audio_dec_class_init (GstAmcAudioDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audiodec_class = GST_AUDIO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_amc_audio_dec_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_amc_audio_dec_change_state);

  audiodec_class->start = GST_DEBUG_FUNCPTR (gst_amc_audio_dec_start);
  audiodec_class->stop = GST_DEBUG_FUNCPTR (gst_amc_audio_dec_stop);
#if 0
  audiodec_class->open = GST_DEBUG_FUNCPTR (gst_amc_audio_dec_open);
  audiodec_class->close = GST_DEBUG_FUNCPTR (gst_amc_audio_dec_close);
#endif
  audiodec_class->flush = GST_DEBUG_FUNCPTR (gst_amc_audio_dec_flush);
  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_amc_audio_dec_set_format);
  audiodec_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_amc_audio_dec_handle_frame);
}

static void
gst_amc_audio_dec_init (GstAmcAudioDec * self, GstAmcAudioDecClass * klass)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (self), TRUE);

  self->drain_lock = g_mutex_new ();
  self->drain_cond = g_cond_new ();
}

static gboolean
gst_amc_audio_dec_open (GstAudioDecoder * decoder)
{
  GstAmcAudioDec *self = GST_AMC_AUDIO_DEC (decoder);
  GstAmcAudioDecClass *klass = GST_AMC_AUDIO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Opening decoder");

  self->codec = gst_amc_codec_new (klass->codec_info->name);
  if (!self->codec)
    return FALSE;
  self->started = FALSE;
  self->flushing = TRUE;

  GST_DEBUG_OBJECT (self, "Opened decoder");

  return TRUE;
}

static gboolean
gst_amc_audio_dec_close (GstAudioDecoder * decoder)
{
  GstAmcAudioDec *self = GST_AMC_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (self->codec)
    gst_amc_codec_free (self->codec);
  self->codec = NULL;

  self->started = FALSE;
  self->flushing = TRUE;

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_amc_audio_dec_finalize (GObject * object)
{
  GstAmcAudioDec *self = GST_AMC_AUDIO_DEC (object);

  g_mutex_free (self->drain_lock);
  g_cond_free (self->drain_cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_amc_audio_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstAmcAudioDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_AMC_AUDIO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_AMC_AUDIO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;
      self->draining = FALSE;
      self->started = FALSE;
      if (!gst_amc_audio_dec_open (GST_AUDIO_DECODER (self)))
        return GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->flushing = TRUE;
      gst_amc_codec_flush (self->codec);
      g_mutex_lock (self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (self->drain_cond);
      g_mutex_unlock (self->drain_lock);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_amc_audio_dec_close (GST_AUDIO_DECODER (self)))
        return GST_STATE_CHANGE_FAILURE;
      self->downstream_flow_ret = GST_FLOW_WRONG_STATE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_amc_audio_dec_set_src_caps (GstAmcAudioDec * self, GstAmcFormat * format)
{
  GstCaps *caps;
  gint rate, channels;
  guint32 channel_mask = 0;

  if (!gst_amc_format_get_int (format, "sample-rate", &rate) ||
      !gst_amc_format_get_int (format, "channel-count", &channels)) {
    GST_ERROR_OBJECT (self, "Failed to get output format metadata");
    return FALSE;
  }

  if (rate == 0 || channels == 0) {
    GST_ERROR_OBJECT (self, "Rate or channels not set");
    return FALSE;
  }

  /* Not always present */
  if (gst_amc_format_contains_key (format, "channel-mask"))
    gst_amc_format_get_int (format, "channel-mask", (gint *) & channel_mask);

  if (self->positions)
    g_free (self->positions);
  self->positions =
      gst_amc_audio_channel_mask_to_positions (channel_mask, channels);


  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels,
      "width", G_TYPE_INT, 16,
      "depth", G_TYPE_INT, 16,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, G_BYTE_ORDER, NULL);

  if (self->positions)
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0),
        self->positions);

  self->channels = channels;
  self->rate = rate;
  gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (self), caps);
  gst_caps_unref (caps);

  self->input_caps_changed = FALSE;

  return TRUE;
}

static void
gst_amc_audio_dec_loop (GstAmcAudioDec * self)
{
  GstFlowReturn flow_ret = GST_FLOW_OK;
  gboolean is_eos;
  GstAmcBufferInfo buffer_info;
  gint idx;

  GST_AUDIO_DECODER_STREAM_LOCK (self);

retry:
  /*if (self->input_caps_changed) {
     idx = INFO_OUTPUT_FORMAT_CHANGED;
     } else { */
  GST_DEBUG_OBJECT (self, "Waiting for available output buffer");
  GST_AUDIO_DECODER_STREAM_UNLOCK (self);
  /* Wait at most 100ms here, some codecs don't fail dequeueing if
   * the codec is flushing, causing deadlocks during shutdown */
  idx = gst_amc_codec_dequeue_output_buffer (self->codec, &buffer_info, 100000);
  GST_AUDIO_DECODER_STREAM_LOCK (self);
  /*} */

  if (idx < 0) {
    if (self->flushing)
      goto flushing;

    switch (idx) {
      case INFO_OUTPUT_BUFFERS_CHANGED:{
        GST_DEBUG_OBJECT (self, "Output buffers have changed");
        if (self->output_buffers)
          gst_amc_codec_free_buffers (self->output_buffers,
              self->n_output_buffers);
        self->output_buffers =
            gst_amc_codec_get_output_buffers (self->codec,
            &self->n_output_buffers);
        if (!self->output_buffers)
          goto get_output_buffers_error;
        break;
      }
      case INFO_OUTPUT_FORMAT_CHANGED:{
        GstAmcFormat *format;
        gchar *format_string;

        GST_DEBUG_OBJECT (self, "Output format has changed");

        format = gst_amc_codec_get_output_format (self->codec);
        if (!format)
          goto format_error;

        format_string = gst_amc_format_to_string (format);
        GST_DEBUG_OBJECT (self, "Got new output format: %s", format_string);
        g_free (format_string);

        if (!gst_amc_audio_dec_set_src_caps (self, format)) {
          gst_amc_format_free (format);
          goto format_error;
        }
        gst_amc_format_free (format);

        if (self->output_buffers)
          gst_amc_codec_free_buffers (self->output_buffers,
              self->n_output_buffers);
        self->output_buffers =
            gst_amc_codec_get_output_buffers (self->codec,
            &self->n_output_buffers);
        if (!self->output_buffers)
          goto get_output_buffers_error;

        goto retry;
        break;
      }
      case INFO_TRY_AGAIN_LATER:
        GST_DEBUG_OBJECT (self, "Dequeueing output buffer timed out");
        goto retry;
        break;
      case G_MININT:
        GST_ERROR_OBJECT (self, "Failure dequeueing output buffer");
        goto dequeue_error;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    goto retry;
  }

  GST_DEBUG_OBJECT (self,
      "Got output buffer at index %d: size %d time %" G_GINT64_FORMAT
      " flags 0x%08x", idx, buffer_info.size, buffer_info.presentation_time_us,
      buffer_info.flags);

  is_eos = ! !(buffer_info.flags & BUFFER_FLAG_END_OF_STREAM);
  self->n_buffers++;

  if (buffer_info.size > 0) {
    GstAmcAudioDecClass *klass = GST_AMC_AUDIO_DEC_GET_CLASS (self);
    GstBuffer *outbuf;
    GstAmcBuffer *buf;

    /* This sometimes happens at EOS or if the input is not properly framed,
     * let's handle it gracefully by allocating a new buffer for the current
     * caps and filling it
     */
    if (idx >= self->n_output_buffers)
      goto invalid_buffer_index;

    if (strcmp (klass->codec_info->name, "OMX.google.mp3.decoder") == 0) {
      /* Google's MP3 decoder outputs garbage in the first output buffer
       * so we just drop it here */
      if (self->n_buffers == 1) {
        GST_DEBUG_OBJECT (self,
            "Skipping first buffer of Google MP3 decoder output");
        goto done;
      }
    }

    outbuf = gst_buffer_try_new_and_alloc (buffer_info.size);
    if (!outbuf)
      goto failed_allocate;

    buf = &self->output_buffers[idx];
    orc_memcpy (GST_BUFFER_DATA (outbuf), buf->data + buffer_info.offset,
        buffer_info.size);

    /* FIXME: We should get one decoded input frame here for
     * every buffer. If this is not the case somewhere, we will
     * error out at some point and will need to add workarounds
     */
    flow_ret =
        gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (self), outbuf, 1);
  }

done:
  if (!gst_amc_codec_release_output_buffer (self->codec, idx))
    goto failed_release;

  if (is_eos || flow_ret == GST_FLOW_UNEXPECTED) {
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    g_mutex_lock (self->drain_lock);
    if (self->draining) {
      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (self->drain_cond);
    } else if (flow_ret == GST_FLOW_OK) {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_UNEXPECTED;
    }
    g_mutex_unlock (self->drain_lock);
    GST_AUDIO_DECODER_STREAM_LOCK (self);
  } else {
    GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));
  }

  self->downstream_flow_ret = flow_ret;

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_AUDIO_DECODER_STREAM_UNLOCK (self);

  return;

dequeue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to dequeue output buffer"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

get_output_buffers_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to get output buffers"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

format_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to handle format"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }
failed_release:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to release output buffer index %d", idx));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_WRONG_STATE;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (self, "EOS");
      gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    } else
        if (flow_ret == GST_FLOW_NOT_LINKED || flow_ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (flow_ret)));
      gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    }
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

invalid_buffer_index:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Invalid input buffer index %d of %d", idx, self->n_input_buffers));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

failed_allocate:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to allocate output buffer"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }
}

static gboolean
gst_amc_audio_dec_start (GstAudioDecoder * decoder)
{
  GstAmcAudioDec *self;

  self = GST_AMC_AUDIO_DEC (decoder);
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->started = FALSE;
  self->flushing = TRUE;

  return TRUE;
}

static gboolean
gst_amc_audio_dec_stop (GstAudioDecoder * decoder)
{
  GstAmcAudioDec *self;

  self = GST_AMC_AUDIO_DEC (decoder);
  GST_DEBUG_OBJECT (self, "Stopping decoder");
  self->flushing = TRUE;
  if (self->started) {
    gst_amc_codec_flush (self->codec);
    gst_amc_codec_stop (self->codec);
    self->started = FALSE;
    if (self->input_buffers)
      gst_amc_codec_free_buffers (self->input_buffers, self->n_input_buffers);
    self->input_buffers = NULL;
    if (self->output_buffers)
      gst_amc_codec_free_buffers (self->output_buffers, self->n_output_buffers);
    self->output_buffers = NULL;
  }
  gst_pad_stop_task (GST_AUDIO_DECODER_SRC_PAD (decoder));

  g_free (self->positions);
  self->positions = NULL;

  g_list_foreach (self->codec_datas, (GFunc) gst_buffer_unref, NULL);
  g_list_free (self->codec_datas);
  self->codec_datas = NULL;

  self->downstream_flow_ret = GST_FLOW_WRONG_STATE;
  self->eos = FALSE;
  g_mutex_lock (self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (self->drain_cond);
  g_mutex_unlock (self->drain_lock);
  gst_buffer_replace (&self->codec_data, NULL);
  GST_DEBUG_OBJECT (self, "Stopped decoder");
  return TRUE;
}

static gboolean
gst_amc_audio_dec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  GstAmcAudioDec *self;
  GstStructure *s;
  GstAmcFormat *format;
  const gchar *mime;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;
  gchar *format_string;
  gint rate, channels;

  self = GST_AMC_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, caps);

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  is_format_change |= (!self->input_caps
      || !gst_caps_is_equal (self->input_caps, caps));

  needs_disable = self->started;

  /* If the component is not started and a real format change happens
   * we have to restart the component. If no real format change
   * happened we can just exit here.
   */
  if (needs_disable && !is_format_change) {
    /* Framerate or something minor changed */
    self->input_caps_changed = TRUE;
    GST_DEBUG_OBJECT (self,
        "Already running and caps did not change the format");
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    gst_amc_audio_dec_drain (self);
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    gst_amc_audio_dec_stop (GST_AUDIO_DECODER (self));
    GST_AUDIO_DECODER_STREAM_LOCK (self);
    gst_amc_audio_dec_close (GST_AUDIO_DECODER (self));
    if (!gst_amc_audio_dec_open (GST_AUDIO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to open codec again");
      return FALSE;
    }

    if (!gst_amc_audio_dec_start (GST_AUDIO_DECODER (self))) {
      GST_ERROR_OBJECT (self, "Failed to start codec again");
    }
  }
  /* srcpad task is not running at this point */

  mime = caps_to_mime (caps);
  if (!mime) {
    GST_ERROR_OBJECT (self, "Failed to convert caps to mime");
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);
  if (!gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Failed to get rate/channels");
    return FALSE;
  }

  format = gst_amc_format_new_audio (mime, rate, channels);
  if (!format) {
    GST_ERROR_OBJECT (self, "Failed to create audio format");
    return FALSE;
  }

  /* FIXME: These buffers needs to be valid until the codec is stopped again */
  g_list_foreach (self->codec_datas, (GFunc) gst_buffer_unref, NULL);
  g_list_free (self->codec_datas);
  self->codec_datas = NULL;
  if (gst_structure_has_field (s, "codec_data")) {
    const GValue *h = gst_structure_get_value (s, "codec_data");
    GstBuffer *codec_data = gst_value_get_buffer (h);

    self->codec_datas =
        g_list_prepend (self->codec_datas, gst_buffer_ref (codec_data));
    gst_amc_format_set_buffer (format, "csd-0", codec_data);
  } else if (gst_structure_has_field (s, "streamheader")) {
    const GValue *sh = gst_structure_get_value (s, "streamheader");
    gint nsheaders = gst_value_array_get_size (sh);
    GstBuffer *buf;
    const GValue *h;
    gint i, j;
    gchar *fname;

    for (i = 0, j = 0; i < nsheaders; i++) {
      h = gst_value_array_get_value (sh, i);
      buf = gst_value_get_buffer (h);

      if (strcmp (mime, "audio/vorbis") == 0) {
        guint8 header_type = GST_BUFFER_DATA (buf)[0];

        /* Only use the identification and setup packets */
        if (header_type != 0x01 && header_type != 0x05)
          continue;
      }

      fname = g_strdup_printf ("csd-%d", j);
      self->codec_datas =
          g_list_prepend (self->codec_datas, gst_buffer_ref (buf));
      gst_amc_format_set_buffer (format, fname, buf);
      g_free (fname);
      j++;
    }
  }

  format_string = gst_amc_format_to_string (format);
  GST_DEBUG_OBJECT (self, "Configuring codec with format: %s", format_string);
  g_free (format_string);

  self->n_buffers = 0;
  if (!gst_amc_codec_configure (self->codec, format, 0)) {
    GST_ERROR_OBJECT (self, "Failed to configure codec");
    return FALSE;
  }

  gst_amc_format_free (format);

  if (!gst_amc_codec_start (self->codec)) {
    GST_ERROR_OBJECT (self, "Failed to start codec");
    return FALSE;
  }

  if (self->input_buffers)
    gst_amc_codec_free_buffers (self->input_buffers, self->n_input_buffers);
  self->input_buffers =
      gst_amc_codec_get_input_buffers (self->codec, &self->n_input_buffers);
  if (!self->input_buffers) {
    GST_ERROR_OBJECT (self, "Failed to get input buffers");
    return FALSE;
  }

  self->started = TRUE;
  self->input_caps_changed = TRUE;

  /* Start the srcpad loop again */
  self->flushing = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_AUDIO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_amc_audio_dec_loop, decoder);

  return TRUE;
}

static void
gst_amc_audio_dec_flush (GstAudioDecoder * decoder, gboolean hard)
{
  GstAmcAudioDec *self;

  self = GST_AMC_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Codec not started yet");
    return;
  }

  self->flushing = TRUE;
  gst_amc_codec_flush (self->codec);

  /* Wait until the srcpad loop is finished,
   * unlock GST_AUDIO_DECODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_AUDIO_DECODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_AUDIO_DECODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_AUDIO_DECODER_SRC_PAD (self));
  GST_AUDIO_DECODER_STREAM_LOCK (self);
  self->flushing = FALSE;

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_AUDIO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_amc_audio_dec_loop, decoder);

  GST_DEBUG_OBJECT (self, "Reset decoder");
}

static GstFlowReturn
gst_amc_audio_dec_handle_frame (GstAudioDecoder * decoder, GstBuffer * inbuf)
{
  GstAmcAudioDec *self;
  gint idx;
  GstAmcBuffer *buf;
  GstAmcBufferInfo buffer_info;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;

  self = GST_AMC_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  /* Make sure to keep a reference to the input here,
   * it can be unreffed from the other thread if
   * finish_frame() is called */
  if (inbuf)
    inbuf = gst_buffer_ref (inbuf);

  if (!self->started) {
    GST_ERROR_OBJECT (self, "Codec not started yet");
    if (inbuf)
      gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (self->eos) {
    GST_WARNING_OBJECT (self, "Got frame after EOS");
    if (inbuf)
      gst_buffer_unref (inbuf);
    return GST_FLOW_UNEXPECTED;
  }

  if (self->flushing)
    goto flushing;

  if (self->downstream_flow_ret != GST_FLOW_OK)
    goto downstream_error;

  if (!inbuf)
    return gst_amc_audio_dec_drain (self);

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);

  while (offset < GST_BUFFER_SIZE (inbuf)) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    /* Wait at most 100ms here, some codecs don't fail dequeueing if
     * the codec is flushing, causing deadlocks during shutdown */
    idx = gst_amc_codec_dequeue_input_buffer (self->codec, 100000);
    GST_AUDIO_DECODER_STREAM_LOCK (self);

    if (idx < 0) {
      if (self->flushing)
        goto flushing;
      switch (idx) {
        case INFO_TRY_AGAIN_LATER:
          GST_DEBUG_OBJECT (self, "Dequeueing input buffer timed out");
          continue;             /* next try */
          break;
        case G_MININT:
          GST_ERROR_OBJECT (self, "Failed to dequeue input buffer");
          goto dequeue_error;
        default:
          g_assert_not_reached ();
          break;
      }

      continue;
    }

    if (idx >= self->n_input_buffers)
      goto invalid_buffer_index;

    if (self->flushing)
      goto flushing;

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      memset (&buffer_info, 0, sizeof (buffer_info));
      gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info);
      goto downstream_error;
    }

    /* Now handle the frame */

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf = &self->input_buffers[idx];

    memset (&buffer_info, 0, sizeof (buffer_info));
    buffer_info.offset = 0;
    buffer_info.size = MIN (GST_BUFFER_SIZE (inbuf) - offset, buf->size);

    orc_memcpy (buf->data, GST_BUFFER_DATA (inbuf) + offset, buffer_info.size);

    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset =
          gst_util_uint64_scale (offset, duration, GST_BUFFER_SIZE (inbuf));
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buffer_info.presentation_time_us =
          gst_util_uint64_scale (timestamp + timestamp_offset, 1, GST_USECOND);
      self->last_upstream_ts = timestamp + timestamp_offset;
    }
    if (duration != GST_CLOCK_TIME_NONE)
      self->last_upstream_ts += duration;

    if (offset == 0) {
      if (!GST_BUFFER_FLAG_IS_SET (inbuf, GST_BUFFER_FLAG_DELTA_UNIT))
        buffer_info.flags |= BUFFER_FLAG_SYNC_FRAME;
    }

    offset += buffer_info.size;
    GST_DEBUG_OBJECT (self,
        "Queueing buffer %d: size %d time %" G_GINT64_FORMAT " flags 0x%08x",
        idx, buffer_info.size, buffer_info.presentation_time_us,
        buffer_info.flags);
    if (!gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info))
      goto queue_error;
  }

  gst_buffer_unref (inbuf);

  return self->downstream_flow_ret;

downstream_error:
  {
    GST_ERROR_OBJECT (self, "Downstream returned %s",
        gst_flow_get_name (self->downstream_flow_ret));
    if (inbuf)
      gst_buffer_unref (inbuf);
    return self->downstream_flow_ret;
  }
invalid_buffer_index:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Invalid input buffer index %d of %d", idx, self->n_input_buffers));
    if (inbuf)
      gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }
dequeue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to dequeue input buffer"));
    if (inbuf)
      gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }
queue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Failed to queue input buffer"));
    if (inbuf)
      gst_buffer_unref (inbuf);
    return GST_FLOW_ERROR;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- returning WRONG_STATE");
    if (inbuf)
      gst_buffer_unref (inbuf);
    return GST_FLOW_WRONG_STATE;
  }
}

static GstFlowReturn
gst_amc_audio_dec_drain (GstAmcAudioDec * self)
{
  GstFlowReturn ret;
  gint idx;

  GST_DEBUG_OBJECT (self, "Draining codec");
  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Codec not started yet");
    return GST_FLOW_OK;
  }

  /* Don't send EOS buffer twice, this doesn't work */
  if (self->eos) {
    GST_DEBUG_OBJECT (self, "Codec is EOS already");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_AUDIO_DECODER_STREAM_UNLOCK (self);
  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port.
   * Wait at most 0.5s here. */
  idx = gst_amc_codec_dequeue_input_buffer (self->codec, 500000);
  GST_AUDIO_DECODER_STREAM_LOCK (self);

  if (idx >= 0 && idx < self->n_input_buffers) {
    GstAmcBufferInfo buffer_info;

    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    g_mutex_lock (self->drain_lock);
    self->draining = TRUE;

    memset (&buffer_info, 0, sizeof (buffer_info));
    buffer_info.size = 0;
    buffer_info.presentation_time_us =
        gst_util_uint64_scale (self->last_upstream_ts, 1, GST_USECOND);
    buffer_info.flags |= BUFFER_FLAG_END_OF_STREAM;

    if (gst_amc_codec_queue_input_buffer (self->codec, idx, &buffer_info)) {
      GST_DEBUG_OBJECT (self, "Waiting until codec is drained");
      g_cond_wait (self->drain_cond, self->drain_lock);
      GST_DEBUG_OBJECT (self, "Drained codec");
      ret = GST_FLOW_OK;
    } else {
      GST_ERROR_OBJECT (self, "Failed to queue input buffer");
      ret = GST_FLOW_ERROR;
    }

    g_mutex_unlock (self->drain_lock);
    GST_AUDIO_DECODER_STREAM_LOCK (self);
  } else if (idx >= self->n_input_buffers) {
    GST_ERROR_OBJECT (self, "Invalid input buffer index %d of %d",
        idx, self->n_input_buffers);
    ret = GST_FLOW_ERROR;
  } else {
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for EOS: %d", idx);
    ret = GST_FLOW_ERROR;
  }

  return ret;
}
