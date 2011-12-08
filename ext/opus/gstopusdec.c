/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * Based on the speexdec element.
 */

/**
 * SECTION:element-opusdec
 * @see_also: opusenc, oggdemux
 *
 * This element decodes a OPUS stream to raw integer audio.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=opus.ogg ! oggdemux ! opusdec ! audioconvert ! audioresample ! alsasink
 * ]| Decode an Ogg/Opus file. To create an Ogg/Opus file refer to the documentation of opusenc.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>
#include "gstopusheader.h"
#include "gstopuscommon.h"
#include "gstopusdec.h"

GST_DEBUG_CATEGORY_STATIC (opusdec_debug);
#define GST_CAT_DEFAULT opusdec_debug

static GstStaticPadTemplate opus_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) { 48000, 24000, 16000, 12000, 8000 }, "
        "channels = (int) [ 1, 8 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate opus_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus")
    );

#define DB_TO_LINEAR(x) pow (10., (x) / 20.)

#define DEFAULT_USE_INBAND_FEC FALSE
#define DEFAULT_APPLY_GAIN TRUE

enum
{
  PROP_0,
  PROP_USE_INBAND_FEC,
  PROP_APPLY_GAIN
};

GST_BOILERPLATE (GstOpusDec, gst_opus_dec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER);

static GstFlowReturn gst_opus_dec_parse_header (GstOpusDec * dec,
    GstBuffer * buf);
static gboolean gst_opus_dec_start (GstAudioDecoder * dec);
static gboolean gst_opus_dec_stop (GstAudioDecoder * dec);
static GstFlowReturn gst_opus_dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);
static gboolean gst_opus_dec_set_format (GstAudioDecoder * bdec,
    GstCaps * caps);
static void gst_opus_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_opus_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);


static void
gst_opus_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &opus_dec_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &opus_dec_sink_factory);
  gst_element_class_set_details_simple (element_class, "Opus audio decoder",
      "Codec/Decoder/Audio",
      "decode opus streams to audio",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_opus_dec_class_init (GstOpusDecClass * klass)
{
  GObjectClass *gobject_class;
  GstAudioDecoderClass *adclass;

  gobject_class = (GObjectClass *) klass;
  adclass = (GstAudioDecoderClass *) klass;

  gobject_class->set_property = gst_opus_dec_set_property;
  gobject_class->get_property = gst_opus_dec_get_property;

  adclass->start = GST_DEBUG_FUNCPTR (gst_opus_dec_start);
  adclass->stop = GST_DEBUG_FUNCPTR (gst_opus_dec_stop);
  adclass->handle_frame = GST_DEBUG_FUNCPTR (gst_opus_dec_handle_frame);
  adclass->set_format = GST_DEBUG_FUNCPTR (gst_opus_dec_set_format);

  g_object_class_install_property (gobject_class, PROP_USE_INBAND_FEC,
      g_param_spec_boolean ("use-inband-fec", "Use in-band FEC",
          "Use forward error correction if available", DEFAULT_USE_INBAND_FEC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_APPLY_GAIN,
      g_param_spec_boolean ("apply-gain", "Apply gain",
          "Apply gain if any is specified in the header", DEFAULT_APPLY_GAIN,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (opusdec_debug, "opusdec", 0,
      "opus decoding element");
}

static void
gst_opus_dec_reset (GstOpusDec * dec)
{
  dec->packetno = 0;
  if (dec->state) {
    opus_multistream_decoder_destroy (dec->state);
    dec->state = NULL;
  }

  gst_buffer_replace (&dec->streamheader, NULL);
  gst_buffer_replace (&dec->vorbiscomment, NULL);
  gst_buffer_replace (&dec->last_buffer, NULL);
  dec->primed = FALSE;

  dec->pre_skip = 0;
  dec->r128_gain = 0;
}

static void
gst_opus_dec_init (GstOpusDec * dec, GstOpusDecClass * g_class)
{
  dec->sample_rate = 0;
  dec->n_channels = 0;
  dec->use_inband_fec = FALSE;
  dec->apply_gain = DEFAULT_APPLY_GAIN;

  gst_opus_dec_reset (dec);
}

static gboolean
gst_opus_dec_start (GstAudioDecoder * dec)
{
  GstOpusDec *odec = GST_OPUS_DEC (dec);

  gst_opus_dec_reset (odec);

  /* we know about concealment */
  gst_audio_decoder_set_plc_aware (dec, TRUE);

  if (odec->use_inband_fec) {
    gst_audio_decoder_set_latency (dec, 2 * GST_MSECOND + GST_MSECOND / 2,
        120 * GST_MSECOND);
  }

  return TRUE;
}

static gboolean
gst_opus_dec_stop (GstAudioDecoder * dec)
{
  GstOpusDec *odec = GST_OPUS_DEC (dec);

  gst_opus_dec_reset (odec);

  return TRUE;
}

static double
gst_opus_dec_get_r128_gain (gint16 r128_gain)
{
  return r128_gain / (double) (1 << 8);
}

static double
gst_opus_dec_get_r128_volume (gint16 r128_gain)
{
  return DB_TO_LINEAR (gst_opus_dec_get_r128_gain (r128_gain));
}

static GstCaps *
gst_opus_dec_negotiate (GstOpusDec * dec)
{
  GstCaps *caps = gst_pad_get_allowed_caps (GST_AUDIO_DECODER_SRC_PAD (dec));
  GstStructure *s;

  caps = gst_caps_make_writable (caps);
  gst_caps_truncate (caps);

  s = gst_caps_get_structure (caps, 0);
  gst_structure_fixate_field_nearest_int (s, "rate", 48000);
  gst_structure_get_int (s, "rate", &dec->sample_rate);
  gst_structure_fixate_field_nearest_int (s, "channels", dec->n_channels);
  gst_structure_get_int (s, "channels", &dec->n_channels);

  GST_INFO_OBJECT (dec, "Negotiated %d channels, %d Hz", dec->n_channels,
      dec->sample_rate);

  return caps;
}

static GstFlowReturn
gst_opus_dec_parse_header (GstOpusDec * dec, GstBuffer * buf)
{
  const guint8 *data = GST_BUFFER_DATA (buf);
  GstCaps *caps;
  const GstAudioChannelPosition *pos = NULL;

  g_return_val_if_fail (gst_opus_header_is_id_header (buf), GST_FLOW_ERROR);
  g_return_val_if_fail (dec->n_channels == 0
      || dec->n_channels == data[9], GST_FLOW_ERROR);

  dec->n_channels = data[9];
  dec->pre_skip = GST_READ_UINT16_LE (data + 10);
  dec->r128_gain = GST_READ_UINT16_LE (data + 14);
  dec->r128_gain_volume = gst_opus_dec_get_r128_volume (dec->r128_gain);
  GST_INFO_OBJECT (dec,
      "Found pre-skip of %u samples, R128 gain %d (volume %f)",
      dec->pre_skip, dec->r128_gain, dec->r128_gain_volume);

  dec->channel_mapping_family = data[18];
  if (dec->channel_mapping_family == 0) {
    /* implicit mapping */
    GST_INFO_OBJECT (dec, "Channel mapping family 0, implicit mapping");
    dec->n_streams = dec->n_stereo_streams = 1;
    dec->channel_mapping[0] = 0;
    dec->channel_mapping[1] = 1;
  } else {
    dec->n_streams = data[19];
    dec->n_stereo_streams = data[20];
    memcpy (dec->channel_mapping, data + 21, dec->n_channels);

    if (dec->channel_mapping_family == 1) {
      GST_INFO_OBJECT (dec, "Channel mapping family 1, Vorbis mapping");
      switch (dec->n_channels) {
        case 1:
        case 2:
          /* nothing */
          break;
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
          pos = gst_opus_channel_positions[dec->n_channels - 1];
          break;
        default:{
          gint i;
          GstAudioChannelPosition *posn =
              g_new (GstAudioChannelPosition, dec->n_channels);

          GST_ELEMENT_WARNING (GST_ELEMENT (dec), STREAM, DECODE,
              (NULL), ("Using NONE channel layout for more than 8 channels"));

          for (i = 0; i < dec->n_channels; i++)
            posn[i] = GST_AUDIO_CHANNEL_POSITION_NONE;

          pos = posn;
        }
      }
    } else {
      GST_INFO_OBJECT (dec, "Channel mapping family %d",
          dec->channel_mapping_family);
    }
  }

  caps = gst_opus_dec_negotiate (dec);

  if (pos) {
    GST_DEBUG_OBJECT (dec, "Setting channel positions on caps");
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  }

  if (dec->n_channels > 8) {
    g_free ((GstAudioChannelPosition *) pos);
  }

  GST_INFO_OBJECT (dec, "Setting src caps to %" GST_PTR_FORMAT, caps);
  gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec), caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_opus_dec_parse_comments (GstOpusDec * dec, GstBuffer * buf)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
opus_dec_chain_parse_data (GstOpusDec * dec, GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;
  gint size;
  guint8 *data;
  GstBuffer *outbuf;
  gint16 *out_data;
  int n, err;
  int samples;
  unsigned int packet_size;
  GstBuffer *buf;

  if (dec->state == NULL) {
    /* If we did not get any headers, default to 2 channels */
    if (dec->n_channels == 0) {
      GstCaps *caps;
      GST_INFO_OBJECT (dec, "No header, assuming single stream");
      dec->n_channels = 2;
      dec->sample_rate = 48000;
      caps = gst_opus_dec_negotiate (dec);
      GST_INFO_OBJECT (dec, "Setting src caps to %" GST_PTR_FORMAT, caps);
      gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec), caps);
      gst_caps_unref (caps);
      /* default stereo mapping */
      dec->channel_mapping_family = 0;
      dec->channel_mapping[0] = 0;
      dec->channel_mapping[1] = 1;
      dec->n_streams = 1;
      dec->n_stereo_streams = 1;
    }

    GST_DEBUG_OBJECT (dec, "Creating decoder with %d channels, %d Hz",
        dec->n_channels, dec->sample_rate);
#ifndef GST_DISABLE_DEBUG
    gst_opus_common_log_channel_mapping_table (GST_ELEMENT (dec), opusdec_debug,
        "Mapping table", dec->n_channels, dec->channel_mapping);
#endif

    GST_DEBUG_OBJECT (dec, "%d streams, %d stereo", dec->n_streams,
        dec->n_stereo_streams);
    dec->state =
        opus_multistream_decoder_create (dec->sample_rate, dec->n_channels,
        dec->n_streams, dec->n_stereo_streams, dec->channel_mapping, &err);
    if (!dec->state || err != OPUS_OK)
      goto creation_failed;
  }

  if (buffer) {
    GST_DEBUG_OBJECT (dec, "Received buffer of size %u",
        GST_BUFFER_SIZE (buffer));
  } else {
    GST_DEBUG_OBJECT (dec, "Received missing buffer");
  }

  /* if using in-band FEC, we introdude one extra frame's delay as we need
     to potentially wait for next buffer to decode a missing buffer */
  if (dec->use_inband_fec && !dec->primed) {
    GST_DEBUG_OBJECT (dec, "First buffer received in FEC mode, early out");
    goto done;
  }

  /* That's the buffer we'll be sending to the opus decoder. */
  buf = dec->use_inband_fec && dec->last_buffer ? dec->last_buffer : buffer;

  if (buf) {
    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);
    GST_DEBUG_OBJECT (dec, "Using buffer of size %u", size);
  } else {
    /* concealment data, pass NULL as the bits parameters */
    GST_DEBUG_OBJECT (dec, "Using NULL buffer");
    data = NULL;
    size = 0;
  }

  /* use maximum size (120 ms) as the number of returned samples is
     not constant over the stream. */
  samples = 120 * dec->sample_rate / 1000;
  packet_size = samples * dec->n_channels * 2;

  res = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec),
      GST_BUFFER_OFFSET_NONE, packet_size,
      GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (dec)), &outbuf);

  if (res != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "buf alloc flow: %s", gst_flow_get_name (res));
    return res;
  }

  out_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  if (dec->use_inband_fec) {
    if (dec->last_buffer) {
      /* normal delayed decode */
      n = opus_multistream_decode (dec->state, data, size, out_data, samples,
          0);
    } else {
      /* FEC reconstruction decode */
      n = opus_multistream_decode (dec->state, data, size, out_data, samples,
          1);
    }
  } else {
    /* normal decode */
    n = opus_multistream_decode (dec->state, data, size, out_data, samples, 0);
  }

  if (n < 0) {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Decoding error: %d", n), (NULL));
    return GST_FLOW_ERROR;
  }
  GST_DEBUG_OBJECT (dec, "decoded %d samples", n);
  GST_BUFFER_SIZE (outbuf) = n * 2 * dec->n_channels;

  /* Skip any samples that need skipping */
  if (dec->pre_skip > 0) {
    guint scaled_pre_skip = dec->pre_skip * dec->sample_rate / 48000;
    guint skip = scaled_pre_skip > n ? n : scaled_pre_skip;
    guint scaled_skip = skip * 48000 / dec->sample_rate;
    GST_BUFFER_SIZE (outbuf) -= skip * 2 * dec->n_channels;
    GST_BUFFER_DATA (outbuf) += skip * 2 * dec->n_channels;
    dec->pre_skip -= scaled_skip;
    GST_INFO_OBJECT (dec,
        "Skipping %u samples (%u at 48000 Hz, %u left to skip)", skip,
        scaled_skip, dec->pre_skip);
  }

  if (GST_BUFFER_SIZE (outbuf) == 0) {
    gst_buffer_unref (outbuf);
    outbuf = NULL;
  }

  /* Apply gain */
  /* Would be better off leaving this to a volume element, as this is
     a naive conversion that does too many int/float conversions.
     However, we don't have control over the pipeline...
     So make it optional if the user program wants to use a volume,
     but do it by default so the correct volume goes out by default */
  if (dec->apply_gain && outbuf && dec->r128_gain) {
    unsigned int i, nsamples = GST_BUFFER_SIZE (outbuf) / 2;
    double volume = dec->r128_gain_volume;
    gint16 *samples = (gint16 *) GST_BUFFER_DATA (outbuf);
    GST_DEBUG_OBJECT (dec, "Applying gain: volume %f", volume);
    for (i = 0; i < nsamples; ++i) {
      int sample = (int) (samples[i] * volume + 0.5);
      samples[i] = sample < -32768 ? -32768 : sample > 32767 ? 32767 : sample;
    }
  }

  res = gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (dec), outbuf, 1);

  if (res != GST_FLOW_OK)
    GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (res));

done:
  if (dec->use_inband_fec) {
    gst_buffer_replace (&dec->last_buffer, buffer);
    dec->primed = TRUE;
  }

  return res;

creation_failed:
  GST_ERROR_OBJECT (dec, "Failed to create Opus decoder: %d", err);
  return GST_FLOW_ERROR;
}

static gboolean
gst_opus_dec_set_format (GstAudioDecoder * bdec, GstCaps * caps)
{
  GstOpusDec *dec = GST_OPUS_DEC (bdec);
  gboolean ret = TRUE;
  GstStructure *s;
  const GValue *streamheader;

  GST_DEBUG_OBJECT (dec, "set_format: %" GST_PTR_FORMAT, caps);

  s = gst_caps_get_structure (caps, 0);
  if ((streamheader = gst_structure_get_value (s, "streamheader")) &&
      G_VALUE_HOLDS (streamheader, GST_TYPE_ARRAY) &&
      gst_value_array_get_size (streamheader) >= 2) {
    const GValue *header, *vorbiscomment;
    GstBuffer *buf;
    GstFlowReturn res = GST_FLOW_OK;

    header = gst_value_array_get_value (streamheader, 0);
    if (header && G_VALUE_HOLDS (header, GST_TYPE_BUFFER)) {
      buf = gst_value_get_buffer (header);
      res = gst_opus_dec_parse_header (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->streamheader, buf);
    }

    vorbiscomment = gst_value_array_get_value (streamheader, 1);
    if (vorbiscomment && G_VALUE_HOLDS (vorbiscomment, GST_TYPE_BUFFER)) {
      buf = gst_value_get_buffer (vorbiscomment);
      res = gst_opus_dec_parse_comments (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->vorbiscomment, buf);
    }
  }

done:
  return ret;
}

static gboolean
memcmp_buffers (GstBuffer * buf1, GstBuffer * buf2)
{
  gsize size1, size2;

  size1 = GST_BUFFER_SIZE (buf1);
  size2 = GST_BUFFER_SIZE (buf2);

  if (size1 != size2)
    return FALSE;

  return !memcmp (GST_BUFFER_DATA (buf1), GST_BUFFER_DATA (buf2), size1);
}

static GstFlowReturn
gst_opus_dec_handle_frame (GstAudioDecoder * adec, GstBuffer * buf)
{
  GstFlowReturn res;
  GstOpusDec *dec;

  /* no fancy draining */
  if (G_UNLIKELY (!buf))
    return GST_FLOW_OK;

  dec = GST_OPUS_DEC (adec);
  GST_LOG_OBJECT (dec,
      "Got buffer ts %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  /* If we have the streamheader and vorbiscomment from the caps already
   * ignore them here */
  if (dec->streamheader && dec->vorbiscomment) {
    if (memcmp_buffers (dec->streamheader, buf)) {
      GST_DEBUG_OBJECT (dec, "found streamheader");
      gst_audio_decoder_finish_frame (adec, NULL, 1);
      res = GST_FLOW_OK;
    } else if (memcmp_buffers (dec->vorbiscomment, buf)) {
      GST_DEBUG_OBJECT (dec, "found vorbiscomments");
      gst_audio_decoder_finish_frame (adec, NULL, 1);
      res = GST_FLOW_OK;
    } else {
      res = opus_dec_chain_parse_data (dec, buf);
    }
  } else {
    /* Otherwise fall back to packet counting and assume that the
     * first two packets might be the headers, checking magic. */
    switch (dec->packetno) {
      case 0:
        if (gst_opus_header_is_header (buf, "OpusHead", 8)) {
          GST_DEBUG_OBJECT (dec, "found streamheader");
          res = gst_opus_dec_parse_header (dec, buf);
          gst_audio_decoder_finish_frame (adec, NULL, 1);
        } else {
          res = opus_dec_chain_parse_data (dec, buf);
        }
        break;
      case 1:
        if (gst_opus_header_is_header (buf, "OpusTags", 8)) {
          GST_DEBUG_OBJECT (dec, "counted vorbiscomments");
          res = gst_opus_dec_parse_comments (dec, buf);
          gst_audio_decoder_finish_frame (adec, NULL, 1);
        } else {
          res = opus_dec_chain_parse_data (dec, buf);
        }
        break;
      default:
      {
        res = opus_dec_chain_parse_data (dec, buf);
        break;
      }
    }
  }

  dec->packetno++;

  return res;
}

static void
gst_opus_dec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOpusDec *dec = GST_OPUS_DEC (object);

  switch (prop_id) {
    case PROP_USE_INBAND_FEC:
      g_value_set_boolean (value, dec->use_inband_fec);
      break;
    case PROP_APPLY_GAIN:
      g_value_set_boolean (value, dec->apply_gain);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opus_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpusDec *dec = GST_OPUS_DEC (object);

  switch (prop_id) {
    case PROP_USE_INBAND_FEC:
      dec->use_inband_fec = g_value_get_boolean (value);
      break;
    case PROP_APPLY_GAIN:
      dec->apply_gain = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
