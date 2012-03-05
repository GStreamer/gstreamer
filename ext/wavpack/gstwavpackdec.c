/* GStreamer Wavpack plugin
 * Copyright (c) 2005 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (c) 2006 Edward Hervey <bilboed@gmail.com>
 * Copyright (c) 2006 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstwavpackdec.c: raw Wavpack bitstream decoder
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
 * SECTION:element-wavpackdec
 *
 * WavpackDec decodes framed (for example by the WavpackParse element)
 * Wavpack streams and decodes them to raw audio.
 * <ulink url="http://www.wavpack.com/">Wavpack</ulink> is an open-source
 * audio codec that features both lossless and lossy encoding.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch filesrc location=test.wv ! wavpackparse ! wavpackdec ! audioconvert ! audioresample ! autoaudiosink
 * ]| This pipeline decodes the Wavpack file test.wv into raw audio buffers and
 * tries to play it back using an automatically found audio sink.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/audio/audio.h>
#include <gst/audio/multichannel.h>

#include <math.h>
#include <string.h>

#include <wavpack/wavpack.h>
#include "gstwavpackdec.h"
#include "gstwavpackcommon.h"
#include "gstwavpackstreamreader.h"


GST_DEBUG_CATEGORY_STATIC (gst_wavpack_dec_debug);
#define GST_CAT_DEFAULT gst_wavpack_dec_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-wavpack, "
        "width = (int) [ 1, 32 ], "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], " "framed = (boolean) true")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) 8, depth = (int) 8, "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true; "
        "audio/x-raw-int, "
        "width = (int) 16, depth = (int) 16, "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true; "
        "audio/x-raw-int, "
        "width = (int) 32, depth = (int) 32, "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true; ")
    );

static gboolean gst_wavpack_dec_start (GstAudioDecoder * dec);
static gboolean gst_wavpack_dec_stop (GstAudioDecoder * dec);
static gboolean gst_wavpack_dec_set_format (GstAudioDecoder * dec,
    GstCaps * caps);
static GstFlowReturn gst_wavpack_dec_handle_frame (GstAudioDecoder * dec,
    GstBuffer * buffer);

static void gst_wavpack_dec_finalize (GObject * object);
static void gst_wavpack_dec_post_tags (GstWavpackDec * dec);

GST_BOILERPLATE (GstWavpackDec, gst_wavpack_dec, GstAudioDecoder,
    GST_TYPE_AUDIO_DECODER);

static void
gst_wavpack_dec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_set_details_simple (element_class, "Wavpack audio decoder",
      "Codec/Decoder/Audio",
      "Decodes Wavpack audio data",
      "Arwed v. Merkatz <v.merkatz@gmx.net>, "
      "Sebastian Dröge <slomo@circular-chaos.org>");
}

static void
gst_wavpack_dec_class_init (GstWavpackDecClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstAudioDecoderClass *base_class = (GstAudioDecoderClass *) (klass);

  gobject_class->finalize = gst_wavpack_dec_finalize;

  base_class->start = GST_DEBUG_FUNCPTR (gst_wavpack_dec_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_wavpack_dec_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_wavpack_dec_set_format);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_wavpack_dec_handle_frame);
}

static void
gst_wavpack_dec_reset (GstWavpackDec * dec)
{
  dec->wv_id.buffer = NULL;
  dec->wv_id.position = dec->wv_id.length = 0;

  dec->channels = 0;
  dec->channel_mask = 0;
  dec->sample_rate = 0;
  dec->depth = 0;
}

static void
gst_wavpack_dec_init (GstWavpackDec * dec, GstWavpackDecClass * gklass)
{
  dec->context = NULL;
  dec->stream_reader = gst_wavpack_stream_reader_new ();

  gst_wavpack_dec_reset (dec);
}

static void
gst_wavpack_dec_finalize (GObject * object)
{
  GstWavpackDec *dec = GST_WAVPACK_DEC (object);

  g_free (dec->stream_reader);
  dec->stream_reader = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_wavpack_dec_start (GstAudioDecoder * dec)
{
  GST_DEBUG_OBJECT (dec, "start");

  /* never mind a few errors */
  gst_audio_decoder_set_max_errors (dec, 16);
  /* don't bother us with flushing */
  gst_audio_decoder_set_drainable (dec, FALSE);
  /* aim for some perfect timestamping */
  gst_audio_decoder_set_tolerance (dec, 10 * GST_MSECOND);

  return TRUE;
}

static gboolean
gst_wavpack_dec_stop (GstAudioDecoder * dec)
{
  GstWavpackDec *wpdec = GST_WAVPACK_DEC (dec);

  GST_DEBUG_OBJECT (dec, "stop");

  if (wpdec->context) {
    WavpackCloseFile (wpdec->context);
    wpdec->context = NULL;
  }

  gst_wavpack_dec_reset (wpdec);

  return TRUE;
}

static void
gst_wavpack_dec_negotiate (GstWavpackDec * dec)
{
  GstCaps *caps;

  /* arrange for 1, 2 or 4-byte width == depth output */
  if (dec->depth == 24)
    dec->width = 32;
  else
    dec->width = dec->depth;

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, dec->sample_rate,
      "channels", G_TYPE_INT, dec->channels,
      "depth", G_TYPE_INT, dec->width,
      "width", G_TYPE_INT, dec->width,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  /* Only set the channel layout for more than two channels
   * otherwise things break unfortunately */
  if (dec->channel_mask != 0 && dec->channels > 2)
    if (!gst_wavpack_set_channel_layout (caps, dec->channel_mask))
      GST_WARNING_OBJECT (dec, "Failed to set channel layout");

  GST_DEBUG_OBJECT (dec, "setting caps %" GST_PTR_FORMAT, caps);

  /* should always succeed */
  gst_pad_set_caps (GST_AUDIO_DECODER_SRC_PAD (dec), caps);
  gst_caps_unref (caps);
}

static gboolean
gst_wavpack_dec_set_format (GstAudioDecoder * bdec, GstCaps * caps)
{
  GstWavpackDec *dec = GST_WAVPACK_DEC (bdec);
  GstStructure *structure = gst_caps_get_structure (caps, 0);

  /* Check if we can set the caps here already */
  if (gst_structure_get_int (structure, "channels", &dec->channels) &&
      gst_structure_get_int (structure, "rate", &dec->sample_rate) &&
      gst_structure_get_int (structure, "width", &dec->depth)) {
    GstAudioChannelPosition *pos;

    /* If we already have the channel layout set from upstream
     * take this */
    if (gst_structure_has_field (structure, "channel-positions")) {
      pos = gst_audio_get_channel_positions (structure);
      if (pos != NULL && dec->channels > 2) {
        GstStructure *new_str = gst_caps_get_structure (caps, 0);

        gst_audio_set_channel_positions (new_str, pos);
        dec->channel_mask =
            gst_wavpack_get_channel_mask_from_positions (pos, dec->channels);
      }

      if (pos != NULL)
        g_free (pos);
    }

    gst_wavpack_dec_negotiate (dec);

    /* send GST_TAG_AUDIO_CODEC and GST_TAG_BITRATE tags before something
     * is decoded or after the format has changed */
    gst_wavpack_dec_post_tags (dec);
  }

  return TRUE;
}

static void
gst_wavpack_dec_post_tags (GstWavpackDec * dec)
{
  GstTagList *list;
  GstFormat format_time = GST_FORMAT_TIME, format_bytes = GST_FORMAT_BYTES;
  gint64 duration, size;

  /* try to estimate the average bitrate */
  if (gst_pad_query_peer_duration (GST_AUDIO_DECODER_SINK_PAD (dec),
          &format_bytes, &size) &&
      gst_pad_query_peer_duration (GST_AUDIO_DECODER_SINK_PAD (dec),
          &format_time, &duration) && size > 0 && duration > 0) {
    guint64 bitrate;

    list = gst_tag_list_new ();

    bitrate = gst_util_uint64_scale (size, 8 * GST_SECOND, duration);
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_BITRATE,
        (guint) bitrate, NULL);

    gst_element_post_message (GST_ELEMENT (dec),
        gst_message_new_tag (GST_OBJECT (dec), list));
  }
}

static GstFlowReturn
gst_wavpack_dec_handle_frame (GstAudioDecoder * bdec, GstBuffer * buf)
{
  GstWavpackDec *dec;
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  WavpackHeader wph;
  int32_t decoded, unpacked_size;
  gboolean format_changed;
  gint width, depth, i, max;
  gint32 *dec_data = NULL;
  guint8 *out_data;

  dec = GST_WAVPACK_DEC (bdec);

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  /* check input, we only accept framed input with complete chunks */
  if (GST_BUFFER_SIZE (buf) < sizeof (WavpackHeader))
    goto input_not_framed;

  if (!gst_wavpack_read_header (&wph, GST_BUFFER_DATA (buf)))
    goto invalid_header;

  if (GST_BUFFER_SIZE (buf) < wph.ckSize + 4 * 1 + 4)
    goto input_not_framed;

  if (!(wph.flags & INITIAL_BLOCK))
    goto input_not_framed;

  dec->wv_id.buffer = GST_BUFFER_DATA (buf);
  dec->wv_id.length = GST_BUFFER_SIZE (buf);
  dec->wv_id.position = 0;

  /* create a new wavpack context if there is none yet but if there
   * was already one (i.e. caps were set on the srcpad) check whether
   * the new one has the same caps */
  if (!dec->context) {
    gchar error_msg[80];

    dec->context = WavpackOpenFileInputEx (dec->stream_reader,
        &dec->wv_id, NULL, error_msg, OPEN_STREAMING, 0);

    /* expect this to work */
    if (!dec->context) {
      GST_WARNING_OBJECT (dec, "Couldn't decode buffer: %s", error_msg);
      goto context_failed;
    }
  }

  g_assert (dec->context != NULL);

  format_changed =
      (dec->sample_rate != WavpackGetSampleRate (dec->context)) ||
      (dec->channels != WavpackGetNumChannels (dec->context)) ||
      (dec->depth != WavpackGetBytesPerSample (dec->context) * 8) ||
#ifdef WAVPACK_OLD_API
      (dec->channel_mask != dec->context->config.channel_mask);
#else
      (dec->channel_mask != WavpackGetChannelMask (dec->context));
#endif

  if (!GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (dec)) || format_changed) {
    gint channel_mask;

    dec->sample_rate = WavpackGetSampleRate (dec->context);
    dec->channels = WavpackGetNumChannels (dec->context);
    dec->depth = WavpackGetBytesPerSample (dec->context) * 8;

#ifdef WAVPACK_OLD_API
    channel_mask = dec->context->config.channel_mask;
#else
    channel_mask = WavpackGetChannelMask (dec->context);
#endif
    if (channel_mask == 0)
      channel_mask = gst_wavpack_get_default_channel_mask (dec->channels);

    dec->channel_mask = channel_mask;

    gst_wavpack_dec_negotiate (dec);

    /* send GST_TAG_AUDIO_CODEC and GST_TAG_BITRATE tags before something
     * is decoded or after the format has changed */
    gst_wavpack_dec_post_tags (dec);
  }

  /* alloc output buffer */
  unpacked_size = (dec->width / 8) * wph.block_samples * dec->channels;
  ret = gst_pad_alloc_buffer (GST_AUDIO_DECODER_SRC_PAD (dec),
      GST_BUFFER_OFFSET (buf), unpacked_size,
      GST_PAD_CAPS (GST_AUDIO_DECODER_SRC_PAD (dec)), &outbuf);

  if (ret != GST_FLOW_OK)
    goto out;

  dec_data = g_malloc (4 * wph.block_samples * dec->channels);
  out_data = GST_BUFFER_DATA (outbuf);

  /* decode */
  decoded = WavpackUnpackSamples (dec->context, dec_data, wph.block_samples);
  if (decoded != wph.block_samples)
    goto decode_error;

  width = dec->width;
  depth = dec->depth;
  max = dec->channels * wph.block_samples;
  if (width == 8) {
    gint8 *outbuffer = (gint8 *) out_data;

    for (i = 0; i < max; i++) {
      *outbuffer++ = (gint8) (dec_data[i]);
    }
  } else if (width == 16) {
    gint16 *outbuffer = (gint16 *) out_data;

    for (i = 0; i < max; i++) {
      *outbuffer++ = (gint16) (dec_data[i]);
    }
  } else if (dec->width == 32) {
    gint32 *outbuffer = (gint32 *) out_data;

    if (width != depth) {
      for (i = 0; i < max; i++) {
        *outbuffer++ = (gint32) (dec_data[i] << (width - depth));
      }
    } else {
      for (i = 0; i < max; i++) {
        *outbuffer++ = (gint32) dec_data[i];
      }
    }
  } else {
    g_assert_not_reached ();
  }

  g_free (dec_data);

  ret = gst_audio_decoder_finish_frame (bdec, outbuf, 1);

out:

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (ret));
  }

  return ret;

/* ERRORS */
input_not_framed:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Expected framed input"));
    return GST_FLOW_ERROR;
  }
invalid_header:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Invalid wavpack header"));
    return GST_FLOW_ERROR;
  }
context_failed:
  {
    GST_AUDIO_DECODER_ERROR (bdec, 1, LIBRARY, INIT, (NULL),
        ("error creating Wavpack context"), ret);
    goto out;
  }
decode_error:
  {
    const gchar *reason = "unknown";

    if (dec->context) {
#ifdef WAVPACK_OLD_API
      reason = dec->context->error_message;
#else
      reason = WavpackGetErrorMessage (dec->context);
#endif
    } else {
      reason = "couldn't create decoder context";
    }
    GST_AUDIO_DECODER_ERROR (bdec, 1, STREAM, DECODE, (NULL),
        ("decoding error: %s", reason), ret);
    g_free (dec_data);
    if (outbuf)
      gst_buffer_unref (outbuf);
    if (ret == GST_FLOW_OK)
      gst_audio_decoder_finish_frame (bdec, NULL, 1);
    return ret;
  }
}

gboolean
gst_wavpack_dec_plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "wavpackdec",
          GST_RANK_PRIMARY, GST_TYPE_WAVPACK_DEC))
    return FALSE;
  GST_DEBUG_CATEGORY_INIT (gst_wavpack_dec_debug, "wavpack_dec", 0,
      "Wavpack decoder");
  return TRUE;
}
