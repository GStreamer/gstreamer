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
        "rate = (int) [ 32000, 64000 ], "
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

GST_BOILERPLATE (GstOpusDec, gst_opus_dec, GstElement, GST_TYPE_ELEMENT);

static gboolean opus_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn opus_dec_chain (GstPad * pad, GstBuffer * buf);
static gboolean opus_dec_sink_setcaps (GstPad * pad, GstCaps * caps);
static GstStateChangeReturn opus_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean opus_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean opus_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean opus_dec_sink_query (GstPad * pad, GstQuery * query);
static const GstQueryType *opus_get_src_query_types (GstPad * pad);
static const GstQueryType *opus_get_sink_query_types (GstPad * pad);
static gboolean opus_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static GstFlowReturn opus_dec_chain_parse_data (GstOpusDec * dec,
    GstBuffer * buf, GstClockTime timestamp, GstClockTime duration);
static GstFlowReturn opus_dec_chain_parse_header (GstOpusDec * dec,
    GstBuffer * buf);
#if 0
static GstFlowReturn opus_dec_chain_parse_comments (GstOpusDec * dec,
    GstBuffer * buf);
#endif

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
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (opus_dec_change_state);

  GST_DEBUG_CATEGORY_INIT (opusdec_debug, "opusdec", 0,
      "opus decoding element");
}

static void
gst_opus_dec_reset (GstOpusDec * dec)
{
  gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
  dec->granulepos = -1;
  dec->packetno = 0;
  dec->frame_size = 0;
  dec->frame_samples = 960;
  dec->frame_duration = 0;
  if (dec->state) {
    opus_decoder_destroy (dec->state);
    dec->state = NULL;
  }
#if 0
  if (dec->mode) {
    opus_mode_destroy (dec->mode);
    dec->mode = NULL;
  }
#endif

  gst_buffer_replace (&dec->streamheader, NULL);
  gst_buffer_replace (&dec->vorbiscomment, NULL);
  g_list_foreach (dec->extra_headers, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->extra_headers);
  dec->extra_headers = NULL;

#if 0
  memset (&dec->header, 0, sizeof (dec->header));
#endif
}

static void
gst_opus_dec_init (GstOpusDec * dec, GstOpusDecClass * g_class)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&opus_dec_sink_factory, "sink");
  gst_pad_set_chain_function (dec->sinkpad, GST_DEBUG_FUNCPTR (opus_dec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (opus_dec_sink_event));
  gst_pad_set_query_type_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (opus_get_sink_query_types));
  gst_pad_set_query_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (opus_dec_sink_query));
  gst_pad_set_setcaps_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (opus_dec_sink_setcaps));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template (&opus_dec_src_factory, "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (opus_dec_src_event));
  gst_pad_set_query_type_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (opus_get_src_query_types));
  gst_pad_set_query_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (opus_dec_src_query));
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->sample_rate = 48000;
  dec->n_channels = 2;

  gst_opus_dec_reset (dec);
}

static gboolean
opus_dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstOpusDec *dec = GST_OPUS_DEC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  GstStructure *s;
  const GValue *streamheader;

  s = gst_caps_get_structure (caps, 0);
  if ((streamheader = gst_structure_get_value (s, "streamheader")) &&
      G_VALUE_HOLDS (streamheader, GST_TYPE_ARRAY) &&
      gst_value_array_get_size (streamheader) >= 2) {
    const GValue *header;
    GstBuffer *buf;
    GstFlowReturn res = GST_FLOW_OK;

    header = gst_value_array_get_value (streamheader, 0);
    if (header && G_VALUE_HOLDS (header, GST_TYPE_BUFFER)) {
      buf = gst_value_get_buffer (header);
      res = opus_dec_chain_parse_header (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->streamheader, buf);
    }
#if 0
    vorbiscomment = gst_value_array_get_value (streamheader, 1);
    if (vorbiscomment && G_VALUE_HOLDS (vorbiscomment, GST_TYPE_BUFFER)) {
      buf = gst_value_get_buffer (vorbiscomment);
      res = opus_dec_chain_parse_comments (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->vorbiscomment, buf);
    }
#endif

    g_list_foreach (dec->extra_headers, (GFunc) gst_mini_object_unref, NULL);
    g_list_free (dec->extra_headers);
    dec->extra_headers = NULL;

    if (gst_value_array_get_size (streamheader) > 2) {
      gint i, n;

      n = gst_value_array_get_size (streamheader);
      for (i = 2; i < n; i++) {
        header = gst_value_array_get_value (streamheader, i);
        buf = gst_value_get_buffer (header);
        dec->extra_headers =
            g_list_prepend (dec->extra_headers, gst_buffer_ref (buf));
      }
    }
  }

done:
  gst_object_unref (dec);
  return ret;
}

static gboolean
opus_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstOpusDec *dec;
  guint64 scale = 1;

  dec = GST_OPUS_DEC (gst_pad_get_parent (pad));

  if (dec->packetno < 1) {
    res = FALSE;
    goto cleanup;
  }

  if (src_format == *dest_format) {
    *dest_value = src_value;
    res = TRUE;
    goto cleanup;
  }

  if (pad == dec->sinkpad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES)) {
    res = FALSE;
    goto cleanup;
  }

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = sizeof (gint16) * dec->n_channels;
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (scale * src_value,
              dec->sample_rate, GST_SECOND);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * sizeof (gint16) * dec->n_channels;
          break;
        case GST_FORMAT_TIME:
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND,
              dec->sample_rate);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (sizeof (gint16) * dec->n_channels);
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              dec->sample_rate * sizeof (gint16) * dec->n_channels);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }

cleanup:
  gst_object_unref (dec);
  return res;
}

static const GstQueryType *
opus_get_sink_query_types (GstPad * pad)
{
  static const GstQueryType opus_dec_sink_query_types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return opus_dec_sink_query_types;
}

static gboolean
opus_dec_sink_query (GstPad * pad, GstQuery * query)
{
  GstOpusDec *dec;
  gboolean res;

  dec = GST_OPUS_DEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = opus_dec_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val);
      if (res) {
        gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (dec);
  return res;
}

static const GstQueryType *
opus_get_src_query_types (GstPad * pad)
{
  static const GstQueryType opus_dec_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return opus_dec_src_query_types;
}

static gboolean
opus_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstOpusDec *dec;
  gboolean res = FALSE;

  dec = GST_OPUS_DEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:{
      GstSegment segment;
      GstFormat format;
      gint64 cur;

      gst_query_parse_position (query, &format, NULL);

      GST_PAD_STREAM_LOCK (dec->sinkpad);
      segment = dec->segment;
      GST_PAD_STREAM_UNLOCK (dec->sinkpad);

      if (segment.format != GST_FORMAT_TIME) {
        GST_DEBUG_OBJECT (dec, "segment not initialised yet");
        break;
      }

      if ((res = opus_dec_convert (dec->srcpad, GST_FORMAT_TIME,
                  segment.last_stop, &format, &cur))) {
        gst_query_set_position (query, format, cur);
      }
      break;
    }
    case GST_QUERY_DURATION:{
      GstFormat format = GST_FORMAT_TIME;
      gint64 dur;

      /* get duration from demuxer */
      if (!gst_pad_query_peer_duration (dec->sinkpad, &format, &dur))
        break;

      gst_query_parse_duration (query, &format, NULL);

      /* and convert it into the requested format */
      if ((res = opus_dec_convert (dec->srcpad, GST_FORMAT_TIME,
                  dur, &format, &dur))) {
        gst_query_set_duration (query, format, dur);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (dec);
  return res;
}

static gboolean
opus_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstOpusDec *dec = GST_OPUS_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstFormat format, tformat;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);

      /* we have to ask our peer to seek to time here as we know
       * nothing about how to generate a granulepos from the src
       * formats or anything.
       *
       * First bring the requested format to time
       */
      tformat = GST_FORMAT_TIME;
      if (!(res = opus_dec_convert (pad, format, cur, &tformat, &tcur)))
        break;
      if (!(res = opus_dec_convert (pad, format, stop, &tformat, &tstop)))
        break;

      /* then seek with time on the peer */
      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      GST_LOG_OBJECT (dec, "seek to %" GST_TIME_FORMAT, GST_TIME_ARGS (tcur));

      res = gst_pad_push_event (dec->sinkpad, real_seek);
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (dec);
  return res;
}

static gboolean
opus_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstOpusDec *dec;
  gboolean ret = FALSE;

  dec = GST_OPUS_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:{
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      if (update) {
        /* time progressed without data, see if we can fill the gap with
         * some concealment data */
        if (dec->segment.last_stop < start) {
          GstClockTime duration;

          duration = start - dec->segment.last_stop;
          opus_dec_chain_parse_data (dec, NULL, dec->segment.last_stop,
              duration);
        }
      }

      /* now configure the values */
      gst_segment_set_newsegment_full (&dec->segment, update,
          rate, arate, GST_FORMAT_TIME, start, stop, time);

      dec->granulepos = -1;

      GST_DEBUG_OBJECT (dec, "segment now: cur = %" GST_TIME_FORMAT " [%"
          GST_TIME_FORMAT " - %" GST_TIME_FORMAT "]",
          GST_TIME_ARGS (dec->segment.last_stop),
          GST_TIME_ARGS (dec->segment.start),
          GST_TIME_ARGS (dec->segment.stop));

      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (dec);
  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (dec, "received non TIME newsegment");
    gst_object_unref (dec);
    return FALSE;
  }
newseg_wrong_rate:
  {
    GST_DEBUG_OBJECT (dec, "negative rates not supported yet");
    gst_object_unref (dec);
    return FALSE;
  }
}

static GstFlowReturn
opus_dec_chain_parse_header (GstOpusDec * dec, GstBuffer * buf)
{
  GstCaps *caps;
  //gint error = OPUS_OK;

#if 0
  dec->samples_per_frame = opus_packet_get_samples_per_frame (
      (const unsigned char *) GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
#endif

#if 0
  if (memcmp (dec->header.codec_id, "OPUS    ", 8) != 0)
    goto invalid_header;
#endif

#if 0
#ifdef HAVE_OPUS_0_7
  dec->mode =
      opus_mode_create (dec->sample_rate, dec->header.frame_size, &error);
#else
  dec->mode =
      opus_mode_create (dec->sample_rate, dec->header.nb_channels,
      dec->header.frame_size, &error);
#endif
  if (!dec->mode)
    goto mode_init_failed;

  /* initialize the decoder */
#ifdef HAVE_OPUS_0_11
  dec->state =
      opus_decoder_create_custom (dec->mode, dec->header.nb_channels, &error);
#else
#ifdef HAVE_OPUS_0_7
  dec->state = opus_decoder_create (dec->mode, dec->header.nb_channels, &error);
#else
  dec->state = opus_decoder_create (dec->mode);
#endif
#endif
#endif
  dec->state = opus_decoder_create (dec->sample_rate, dec->n_channels);
  if (!dec->state)
    goto init_failed;

#if 0
#ifdef HAVE_OPUS_0_8
  dec->frame_size = dec->header.frame_size;
#else
  opus_mode_info (dec->mode, OPUS_GET_FRAME_SIZE, &dec->frame_size);
#endif
#endif

  dec->frame_duration = gst_util_uint64_scale_int (dec->frame_size,
      GST_SECOND, dec->sample_rate);

  /* set caps */
  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, dec->sample_rate,
      "channels", G_TYPE_INT, dec->n_channels,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);

  GST_DEBUG_OBJECT (dec, "rate=%d channels=%d frame-size=%d",
      dec->sample_rate, dec->n_channels, dec->frame_size);

  if (!gst_pad_set_caps (dec->srcpad, caps))
    goto nego_failed;

  gst_caps_unref (caps);
  return GST_FLOW_OK;

  /* ERRORS */
#if 0
invalid_header:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("Invalid header"));
    return GST_FLOW_ERROR;
  }
mode_init_failed:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("Mode initialization failed: %d", error));
    return GST_FLOW_ERROR;
  }
#endif
init_failed:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't initialize decoder"));
    return GST_FLOW_ERROR;
  }
nego_failed:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't negotiate format"));
    gst_caps_unref (caps);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

#if 0
static GstFlowReturn
opus_dec_chain_parse_comments (GstOpusDec * dec, GstBuffer * buf)
{
  GstTagList *list;
  gchar *encoder = NULL;

  list = gst_tag_list_from_vorbiscomment_buffer (buf, NULL, 0, &encoder);

  if (!list) {
    GST_WARNING_OBJECT (dec, "couldn't decode comments");
    list = gst_tag_list_new ();
  }

  if (encoder) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER, encoder, NULL);
  }

  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, "Opus", NULL);

  if (dec->header.bytes_per_packet > 0) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, (guint) dec->header.bytes_per_packet * 8, NULL);
  }

  GST_INFO_OBJECT (dec, "tags: %" GST_PTR_FORMAT, list);

  gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, list);

  g_free (encoder);
  g_free (ver);

  return GST_FLOW_OK;
}
#endif

static GstFlowReturn
opus_dec_chain_parse_data (GstOpusDec * dec, GstBuffer * buf,
    GstClockTime timestamp, GstClockTime duration)
{
  GstFlowReturn res = GST_FLOW_OK;
  gint size;
  guint8 *data;
  GstBuffer *outbuf;
  gint16 *out_data;
  int n;

  if (timestamp != -1) {
    dec->segment.last_stop = timestamp;
    dec->granulepos = -1;
  }

  if (dec->state == NULL) {
    GstCaps *caps;

    dec->state = opus_decoder_create (dec->sample_rate, dec->n_channels);

    /* set caps */
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->sample_rate,
        "channels", G_TYPE_INT, dec->n_channels,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);

    GST_DEBUG_OBJECT (dec, "rate=%d channels=%d frame-size=%d",
        dec->sample_rate, dec->n_channels, dec->frame_size);

    if (!gst_pad_set_caps (dec->srcpad, caps))
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

  GST_DEBUG ("bandwidth %d", opus_packet_get_bandwidth (data));
  GST_DEBUG ("samples_per_frame %d", opus_packet_get_samples_per_frame (data,
          48000));
  GST_DEBUG ("channels %d", opus_packet_get_nb_channels (data));

  res = gst_pad_alloc_buffer_and_set_caps (dec->srcpad,
      GST_BUFFER_OFFSET_NONE, dec->frame_samples * dec->n_channels * 2,
      GST_PAD_CAPS (dec->srcpad), &outbuf);

  if (res != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (dec, "buf alloc flow: %s", gst_flow_get_name (res));
    return res;
  }

  out_data = (gint16 *) GST_BUFFER_DATA (outbuf);

  GST_LOG_OBJECT (dec, "decoding frame");

  n = opus_decode (dec->state, data, size, out_data, dec->frame_samples, TRUE);
  if (n < 0) {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Decoding error: %d", n), (NULL));
    return GST_FLOW_ERROR;
  }

  if (!GST_CLOCK_TIME_IS_VALID (timestamp)) {
    timestamp = gst_util_uint64_scale_int (dec->granulepos - dec->frame_size,
        GST_SECOND, dec->sample_rate);
  }

  GST_DEBUG_OBJECT (dec, "timestamp=%" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);
  if (dec->discont) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
    dec->discont = 0;
  }

  dec->segment.last_stop += dec->frame_duration;

  GST_LOG_OBJECT (dec, "pushing buffer with ts=%" GST_TIME_FORMAT ", dur=%"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
      GST_TIME_ARGS (dec->frame_duration));

  res = gst_pad_push (dec->srcpad, outbuf);

  if (res != GST_FLOW_OK)
    GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (res));

  return res;
}

static GstFlowReturn
opus_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res;
  GstOpusDec *dec;

  dec = GST_OPUS_DEC (gst_pad_get_parent (pad));

  if (GST_BUFFER_IS_DISCONT (buf)) {
    dec->discont = TRUE;
  }

  res = opus_dec_chain_parse_data (dec, buf, GST_BUFFER_TIMESTAMP (buf),
      GST_BUFFER_DURATION (buf));

//done:
  dec->packetno++;

  gst_buffer_unref (buf);
  gst_object_unref (dec);

  return res;
}

static GstStateChangeReturn
opus_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstOpusDec *dec = GST_OPUS_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_opus_dec_reset (dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
