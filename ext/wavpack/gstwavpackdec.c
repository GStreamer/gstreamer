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


#define WAVPACK_DEC_MAX_ERRORS 16

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
        "width = (int) 32, "
        "depth = (int) [ 1, 32 ], "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) [ 6000, 192000 ], "
        "endianness = (int) BYTE_ORDER, " "signed = (boolean) true")
    );

static GstFlowReturn gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_wavpack_dec_sink_set_caps (GstPad * pad, GstCaps * caps);
static gboolean gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event);
static void gst_wavpack_dec_finalize (GObject * object);
static GstStateChangeReturn gst_wavpack_dec_change_state (GstElement * element,
    GstStateChange transition);
static void gst_wavpack_dec_post_tags (GstWavpackDec * dec);

GST_BOILERPLATE (GstWavpackDec, gst_wavpack_dec, GstElement, GST_TYPE_ELEMENT);

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
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_change_state);
  gobject_class->finalize = gst_wavpack_dec_finalize;
}

static void
gst_wavpack_dec_reset (GstWavpackDec * dec)
{
  dec->wv_id.buffer = NULL;
  dec->wv_id.position = dec->wv_id.length = 0;

  dec->error_count = 0;

  dec->channels = 0;
  dec->channel_mask = 0;
  dec->sample_rate = 0;
  dec->depth = 0;

  gst_segment_init (&dec->segment, GST_FORMAT_TIME);
  dec->next_block_index = 0;
}

static void
gst_wavpack_dec_init (GstWavpackDec * dec, GstWavpackDecClass * gklass)
{
  dec->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_chain));
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_sink_set_caps));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_wavpack_dec_sink_event));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

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
gst_wavpack_dec_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstWavpackDec *dec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);

  /* Check if we can set the caps here already */
  if (gst_structure_get_int (structure, "channels", &dec->channels) &&
      gst_structure_get_int (structure, "rate", &dec->sample_rate) &&
      gst_structure_get_int (structure, "width", &dec->depth)) {
    GstCaps *caps;
    GstAudioChannelPosition *pos;

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->sample_rate,
        "channels", G_TYPE_INT, dec->channels,
        "depth", G_TYPE_INT, dec->depth,
        "width", G_TYPE_INT, 32,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);

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

    GST_DEBUG_OBJECT (dec, "setting caps %" GST_PTR_FORMAT, caps);

    /* should always succeed */
    gst_pad_set_caps (dec->srcpad, caps);
    gst_caps_unref (caps);

    /* send GST_TAG_AUDIO_CODEC and GST_TAG_BITRATE tags before something
     * is decoded or after the format has changed */
    gst_wavpack_dec_post_tags (dec);
  }

  gst_object_unref (dec);

  return TRUE;
}

static void
gst_wavpack_dec_post_tags (GstWavpackDec * dec)
{
  GstTagList *list;
  GstFormat format_time = GST_FORMAT_TIME, format_bytes = GST_FORMAT_BYTES;
  gint64 duration, size;

  list = gst_tag_list_new ();

  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, "Wavpack", NULL);

  /* try to estimate the average bitrate */
  if (gst_pad_query_peer_duration (dec->sinkpad, &format_bytes, &size) &&
      gst_pad_query_peer_duration (dec->sinkpad, &format_time, &duration) &&
      size > 0 && duration > 0) {
    guint64 bitrate;

    bitrate = gst_util_uint64_scale (size, 8 * GST_SECOND, duration);
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE, GST_TAG_BITRATE,
        (guint) bitrate, NULL);
  }

  gst_element_post_message (GST_ELEMENT (dec),
      gst_message_new_tag (GST_OBJECT (dec), list));
}

static GstFlowReturn
gst_wavpack_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstWavpackDec *dec;
  GstBuffer *outbuf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  WavpackHeader wph;
  int32_t decoded, unpacked_size;
  gboolean format_changed;

  dec = GST_WAVPACK_DEC (GST_PAD_PARENT (pad));

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

    if (!dec->context) {
      GST_WARNING ("Couldn't decode buffer: %s", error_msg);
      dec->error_count++;
      if (dec->error_count <= WAVPACK_DEC_MAX_ERRORS) {
        goto out;               /* just return OK for now */
      } else {
        goto decode_error;
      }
    }
  }

  g_assert (dec->context != NULL);

  dec->error_count = 0;

  format_changed =
      (dec->sample_rate != WavpackGetSampleRate (dec->context)) ||
      (dec->channels != WavpackGetNumChannels (dec->context)) ||
      (dec->depth != WavpackGetBitsPerSample (dec->context)) ||
#ifdef WAVPACK_OLD_API
      (dec->channel_mask != dec->context->config.channel_mask);
#else
      (dec->channel_mask != WavpackGetChannelMask (dec->context));
#endif

  if (!GST_PAD_CAPS (dec->srcpad) || format_changed) {
    GstCaps *caps;
    gint channel_mask;

    dec->sample_rate = WavpackGetSampleRate (dec->context);
    dec->channels = WavpackGetNumChannels (dec->context);
    dec->depth = WavpackGetBitsPerSample (dec->context);

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->sample_rate,
        "channels", G_TYPE_INT, dec->channels,
        "depth", G_TYPE_INT, dec->depth,
        "width", G_TYPE_INT, 32,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "signed", G_TYPE_BOOLEAN, TRUE, NULL);

#ifdef WAVPACK_OLD_API
    channel_mask = dec->context->config.channel_mask;
#else
    channel_mask = WavpackGetChannelMask (dec->context);
#endif
    if (channel_mask == 0)
      channel_mask = gst_wavpack_get_default_channel_mask (dec->channels);

    dec->channel_mask = channel_mask;

    /* Only set the channel layout for more than two channels
     * otherwise things break unfortunately */
    if (channel_mask != 0 && dec->channels > 2)
      if (!gst_wavpack_set_channel_layout (caps, channel_mask))
        GST_WARNING_OBJECT (dec, "Failed to set channel layout");

    GST_DEBUG_OBJECT (dec, "setting caps %" GST_PTR_FORMAT, caps);

    /* should always succeed */
    gst_pad_set_caps (dec->srcpad, caps);
    gst_caps_unref (caps);

    /* send GST_TAG_AUDIO_CODEC and GST_TAG_BITRATE tags before something
     * is decoded or after the format has changed */
    gst_wavpack_dec_post_tags (dec);
  }

  /* alloc output buffer */
  unpacked_size = 4 * wph.block_samples * dec->channels;
  ret = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET (buf),
      unpacked_size, GST_PAD_CAPS (dec->srcpad), &outbuf);

  if (ret != GST_FLOW_OK)
    goto out;

  gst_buffer_copy_metadata (outbuf, buf, GST_BUFFER_COPY_TIMESTAMPS);

  /* If we got a DISCONT buffer forward the flag. Nothing else
   * has to be done as libwavpack doesn't store state between
   * Wavpack blocks */
  if (GST_BUFFER_IS_DISCONT (buf) || dec->next_block_index != wph.block_index)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  dec->next_block_index = wph.block_index + wph.block_samples;

  /* decode */
  decoded = WavpackUnpackSamples (dec->context,
      (int32_t *) GST_BUFFER_DATA (outbuf), wph.block_samples);
  if (decoded != wph.block_samples)
    goto decode_error;

  if ((outbuf = gst_audio_buffer_clip (outbuf, &dec->segment,
              dec->sample_rate, 4 * dec->channels))) {
    GST_LOG_OBJECT (dec, "pushing buffer with time %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)));
    ret = gst_pad_push (dec->srcpad, outbuf);
  }

out:

  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (ret));
  }

  gst_buffer_unref (buf);

  return ret;

/* ERRORS */
input_not_framed:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Expected framed input"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
invalid_header:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL), ("Invalid wavpack header"));
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
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
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
        ("Failed to decode wavpack stream: %s", reason));
    if (outbuf)
      gst_buffer_unref (outbuf);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_wavpack_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstWavpackDec *dec = GST_WAVPACK_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "Received %s event", GST_EVENT_TYPE_NAME (event));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat fmt;
      gboolean is_update;
      gint64 start, end, base;
      gdouble rate;

      gst_event_parse_new_segment (event, &is_update, &rate, &fmt, &start,
          &end, &base);
      if (fmt == GST_FORMAT_TIME) {
        GST_DEBUG ("Got NEWSEGMENT event in GST_FORMAT_TIME, passing on (%"
            GST_TIME_FORMAT " - %" GST_TIME_FORMAT ")", GST_TIME_ARGS (start),
            GST_TIME_ARGS (end));
        gst_segment_set_newsegment (&dec->segment, is_update, rate, fmt,
            start, end, base);
      } else {
        gst_segment_init (&dec->segment, GST_FORMAT_TIME);
      }
      break;
    }
    default:
      break;
  }

  gst_object_unref (dec);
  return gst_pad_event_default (pad, event);
}

static GstStateChangeReturn
gst_wavpack_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstWavpackDec *dec = GST_WAVPACK_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (dec->context) {
        WavpackCloseFile (dec->context);
        dec->context = NULL;
      }

      gst_wavpack_dec_reset (dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
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
