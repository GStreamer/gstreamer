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
#  include "config.h"
#endif

#include "gstopusdec.h"
#include <string.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (opusdec_debug);
#define GST_CAT_DEFAULT opusdec_debug

#define DEC_MAX_FRAME_SIZE 2000

static GstStaticPadTemplate opus_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) { 8000, 12000, 16000, 24000, 48000 }, "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate opus_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-opus")
    );

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

static void
gst_opus_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&opus_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&opus_dec_sink_factory));
  gst_element_class_set_details_simple (element_class, "Opus audio decoder",
      "Codec/Decoder/Audio",
      "decode opus streams to audio",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");
}

static void
gst_opus_dec_class_init (GstOpusDecClass * klass)
{
  GstAudioDecoderClass *adclass;
  GstElementClass *gstelement_class;

  adclass = (GstAudioDecoderClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  adclass->start = GST_DEBUG_FUNCPTR (gst_opus_dec_start);
  adclass->stop = GST_DEBUG_FUNCPTR (gst_opus_dec_stop);
  adclass->handle_frame = GST_DEBUG_FUNCPTR (gst_opus_dec_handle_frame);
  adclass->set_format = GST_DEBUG_FUNCPTR (gst_opus_dec_set_format);

  GST_DEBUG_CATEGORY_INIT (opusdec_debug, "opusdec", 0,
      "opus decoding element");
}

static void
gst_opus_dec_reset (GstOpusDec * dec)
{
  dec->packetno = 0;
  dec->frame_size = 0;
  dec->frame_samples = 960;
  dec->frame_duration = 0;
  if (dec->state) {
    opus_decoder_destroy (dec->state);
    dec->state = NULL;
  }

  gst_buffer_replace (&dec->streamheader, NULL);
  gst_buffer_replace (&dec->vorbiscomment, NULL);
}

static void
gst_opus_dec_init (GstOpusDec * dec, GstOpusDecClass * g_class)
{
  dec->sample_rate = 48000;
  dec->n_channels = 2;

  gst_opus_dec_reset (dec);
}

static gboolean
gst_opus_dec_start (GstAudioDecoder * dec)
{
  GstOpusDec *odec = GST_OPUS_DEC (dec);

  gst_opus_dec_reset (odec);

  /* we know about concealment */
  gst_audio_decoder_set_plc_aware (dec, TRUE);

  return TRUE;
}

static gboolean
gst_opus_dec_stop (GstAudioDecoder * dec)
{
  GstOpusDec *odec = GST_OPUS_DEC (dec);

  gst_opus_dec_reset (odec);

  return TRUE;
}

static GstFlowReturn
gst_opus_dec_parse_header (GstOpusDec * dec, GstBuffer * buf)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_opus_dec_parse_comments (GstOpusDec * dec, GstBuffer * buf)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
opus_dec_chain_parse_data (GstOpusDec * dec, GstBuffer * buf,
    GstClockTime timestamp, GstClockTime duration)
{
  GstFlowReturn res = GST_FLOW_OK;
  gint size;
  guint8 *data;
  GstBuffer *outbuf;
  gint16 *out_data;
  int n, err;

  if (dec->state == NULL) {
    GstCaps *caps;

    dec->state = opus_decoder_create (dec->sample_rate, dec->n_channels, &err);
    if (!dec->state || err != OPUS_OK)
      goto creation_failed;

    /* set caps */
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->sample_rate,
        "channels", G_TYPE_INT, dec->n_channels,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);

    GST_DEBUG_OBJECT (dec, "rate=%d channels=%d frame-size=%d",
        dec->sample_rate, dec->n_channels, dec->frame_size);

    if (!gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec), caps))
      GST_ERROR ("nego failure");

    gst_caps_unref (caps);
  }

  if (buf) {
    data = GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf);

    GST_DEBUG_OBJECT (dec, "received buffer of size %u", size);

    /* copy timestamp */
  } else {
    /* concealment data, pass NULL as the bits parameters */
    GST_DEBUG_OBJECT (dec, "creating concealment data");
    data = NULL;
    size = 0;
  }

  GST_DEBUG ("frames %d", opus_packet_get_nb_frames (data, size));
  GST_DEBUG ("bandwidth %d", opus_packet_get_bandwidth (data));
  GST_DEBUG ("samples_per_frame %d", opus_packet_get_samples_per_frame (data,
          48000));
  GST_DEBUG ("channels %d", opus_packet_get_nb_channels (data));

  res = gst_pad_alloc_buffer_and_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec),
      GST_BUFFER_OFFSET_NONE, dec->frame_samples * dec->n_channels * 2,
      GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (dec)), &outbuf);

  if (res != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "buf alloc flow: %s", gst_flow_get_name (res));
    return res;
  }

  out_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  GST_LOG_OBJECT (dec, "decoding %d sample frame", dec->frame_samples);

  n = opus_decode (dec->state, data, size, out_data, dec->frame_samples, 0);
  if (n < 0) {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Decoding error: %d", n), (NULL));
    return GST_FLOW_ERROR;
  }

  if (!GST_CLOCK_TIME_IS_VALID (timestamp)) {
    GST_WARNING_OBJECT (dec, "No timestamp in -> no timestamp out");
  }

  GST_DEBUG_OBJECT (dec, "timestamp=%" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  GST_LOG_OBJECT (dec, "pushing buffer with ts=%" GST_TIME_FORMAT ", dur=%"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (dec->frame_duration));

  res = gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (dec), outbuf, 1);

  if (res != GST_FLOW_OK)
    GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (res));

  return res;

creation_failed:
  GST_ERROR_OBJECT (dec, "Failed to create Opus decoder: %d", err);
  return GST_FLOW_ERROR;
}

static gint
gst_opus_dec_get_frame_samples (GstOpusDec * dec)
{
  gint frame_samples = 0;
  switch (dec->frame_size) {
    case 2:
      frame_samples = dec->sample_rate / 400;
      break;
    case 5:
      frame_samples = dec->sample_rate / 200;
      break;
    case 10:
      frame_samples = dec->sample_rate / 100;
      break;
    case 20:
      frame_samples = dec->sample_rate / 50;
      break;
    case 40:
      frame_samples = dec->sample_rate / 25;
      break;
    case 60:
      frame_samples = 3 * dec->sample_rate / 50;
      break;
    default:
      GST_WARNING_OBJECT (dec, "Unsupported frame size: %d", dec->frame_size);
      frame_samples = 0;
      break;
  }
  return frame_samples;
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
  if (!gst_structure_get_int (s, "frame-size", &dec->frame_size)) {
    GST_WARNING_OBJECT (dec, "Frame size not included in caps");
  }
  if (!gst_structure_get_int (s, "channels", &dec->n_channels)) {
    GST_WARNING_OBJECT (dec, "Number of channels not included in caps");
  }
  if (!gst_structure_get_int (s, "rate", &dec->sample_rate)) {
    GST_WARNING_OBJECT (dec, "Sample rate not included in caps");
  }

  dec->frame_samples = gst_opus_dec_get_frame_samples (dec);
  dec->frame_duration = gst_util_uint64_scale_int (dec->frame_samples,
      GST_SECOND, dec->sample_rate);
  GST_INFO_OBJECT (dec,
      "Got frame size %d, %d channels, %d Hz, giving %d samples per frame, frame duration %"
      GST_TIME_FORMAT, dec->frame_size, dec->n_channels, dec->sample_rate,
      dec->frame_samples, GST_TIME_ARGS (dec->frame_duration));

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, dec->sample_rate,
      "channels", G_TYPE_INT, dec->n_channels,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);
  gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec), caps);
  gst_caps_unref (caps);

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
      res = opus_dec_chain_parse_data (dec, buf, GST_BUFFER_TIMESTAMP (buf),
          GST_BUFFER_DURATION (buf));
    }
  } else {
    /* Otherwise fall back to packet counting and assume that the
     * first two packets are the headers. */
    switch (dec->packetno) {
      case 0:
        GST_DEBUG_OBJECT (dec, "counted streamheader");
        res = gst_opus_dec_parse_header (dec, buf);
        gst_audio_decoder_finish_frame (adec, NULL, 1);
        break;
      case 1:
        GST_DEBUG_OBJECT (dec, "counted vorbiscomments");
        res = gst_opus_dec_parse_comments (dec, buf);
        gst_audio_decoder_finish_frame (adec, NULL, 1);
        break;
      default:
      {
        res = opus_dec_chain_parse_data (dec, buf, GST_BUFFER_TIMESTAMP (buf),
            GST_BUFFER_DURATION (buf));
        break;
      }
    }
  }

  dec->packetno++;

  return res;
}
