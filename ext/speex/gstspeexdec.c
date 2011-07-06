/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
 * SECTION:element-speexdec
 * @see_also: speexenc, oggdemux
 *
 * This element decodes a Speex stream to raw integer audio.
 * <ulink url="http://www.speex.org/">Speex</ulink> is a royalty-free
 * audio codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=speex.ogg ! oggdemux ! speexdec ! audioconvert ! audioresample ! alsasink
 * ]| Decode an Ogg/Speex file. To create an Ogg/Speex file refer to the
 * documentation of speexenc.
 * </refsect2>
 *
 * Last reviewed on 2006-04-05 (0.10.2)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstspeexdec.h"
#include <stdlib.h>
#include <string.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (speexdec_debug);
#define GST_CAT_DEFAULT speexdec_debug

#define DEFAULT_ENH   TRUE

enum
{
  ARG_0,
  ARG_ENH
};

static GstStaticPadTemplate speex_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 6000, 48000 ], "
        "channels = (int) [ 1, 2 ], "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) true, " "width = (int) 16, " "depth = (int) 16")
    );

static GstStaticPadTemplate speex_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-speex")
    );

#define gst_speex_dec_parent_class parent_class
G_DEFINE_TYPE (GstSpeexDec, gst_speex_dec, GST_TYPE_ELEMENT);

static gboolean speex_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn speex_dec_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn speex_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean speex_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean speex_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean speex_dec_sink_query (GstPad * pad, GstQuery * query);
static const GstQueryType *speex_get_src_query_types (GstPad * pad);
static const GstQueryType *speex_get_sink_query_types (GstPad * pad);
static gboolean speex_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static void gst_speex_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_speex_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstFlowReturn speex_dec_chain_parse_data (GstSpeexDec * dec,
    GstBuffer * buf, GstClockTime timestamp, GstClockTime duration);

static GstFlowReturn speex_dec_chain_parse_header (GstSpeexDec * dec,
    GstBuffer * buf);
static GstFlowReturn speex_dec_chain_parse_comments (GstSpeexDec * dec,
    GstBuffer * buf);

static void
gst_speex_dec_class_init (GstSpeexDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_speex_dec_set_property;
  gobject_class->get_property = gst_speex_dec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ENH,
      g_param_spec_boolean ("enh", "Enh", "Enable perceptual enhancement",
          DEFAULT_ENH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (speex_dec_change_state);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&speex_dec_src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&speex_dec_sink_factory));
  gst_element_class_set_details_simple (gstelement_class, "Speex audio decoder",
      "Codec/Decoder/Audio",
      "decode speex streams to audio", "Wim Taymans <wim@fluendo.com>");

  GST_DEBUG_CATEGORY_INIT (speexdec_debug, "speexdec", 0,
      "speex decoding element");
}

static void
gst_speex_dec_reset (GstSpeexDec * dec)
{
  gst_segment_init (&dec->segment, GST_FORMAT_UNDEFINED);
  dec->packetno = 0;
  dec->frame_size = 0;
  dec->frame_duration = 0;
  dec->mode = NULL;
  free (dec->header);
  dec->header = NULL;
  speex_bits_destroy (&dec->bits);

  gst_buffer_replace (&dec->streamheader, NULL);
  gst_buffer_replace (&dec->vorbiscomment, NULL);

  if (dec->stereo) {
    speex_stereo_state_destroy (dec->stereo);
    dec->stereo = NULL;
  }

  if (dec->state) {
    speex_decoder_destroy (dec->state);
    dec->state = NULL;
  }
}

static void
gst_speex_dec_init (GstSpeexDec * dec)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&speex_dec_sink_factory, "sink");
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (speex_dec_chain));
  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (speex_dec_sink_event));
  gst_pad_set_query_type_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (speex_get_sink_query_types));
  gst_pad_set_query_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (speex_dec_sink_query));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_static_template (&speex_dec_src_factory, "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (speex_dec_src_event));
  gst_pad_set_query_type_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (speex_get_src_query_types));
  gst_pad_set_query_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (speex_dec_src_query));
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->enh = DEFAULT_ENH;

  gst_speex_dec_reset (dec);
}

static gboolean
speex_dec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstSpeexDec *dec = GST_SPEEX_DEC (gst_pad_get_parent (pad));
  gboolean ret = TRUE;
  GstStructure *s;
  const GValue *streamheader;

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
      res = speex_dec_chain_parse_header (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->streamheader, buf);
    }

    vorbiscomment = gst_value_array_get_value (streamheader, 1);
    if (vorbiscomment && G_VALUE_HOLDS (vorbiscomment, GST_TYPE_BUFFER)) {
      buf = gst_value_get_buffer (vorbiscomment);
      res = speex_dec_chain_parse_comments (dec, buf);
      if (res != GST_FLOW_OK)
        goto done;
      gst_buffer_replace (&dec->vorbiscomment, buf);
    }
  }

done:
  gst_object_unref (dec);
  return ret;
}

static gboolean
speex_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstSpeexDec *dec;
  guint64 scale = 1;

  dec = GST_SPEEX_DEC (gst_pad_get_parent (pad));

  if (src_format == *dest_format) {
    *dest_value = src_value;
    res = TRUE;
    goto cleanup;
  }

  if (dec->packetno < 1) {
    res = FALSE;
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
          scale = 2 * dec->header->nb_channels;
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (scale * src_value, dec->header->rate,
              GST_SECOND);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * 2 * dec->header->nb_channels;
          break;
        case GST_FORMAT_TIME:
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND,
              dec->header->rate);
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (2 * dec->header->nb_channels);
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              dec->header->rate * 2 * dec->header->nb_channels);
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
speex_get_sink_query_types (GstPad * pad)
{
  static const GstQueryType speex_dec_sink_query_types[] = {
    GST_QUERY_CONVERT,
    0
  };

  return speex_dec_sink_query_types;
}

static gboolean
speex_dec_sink_query (GstPad * pad, GstQuery * query)
{
  GstSpeexDec *dec;
  gboolean res;

  dec = GST_SPEEX_DEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res = speex_dec_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val);
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
speex_get_src_query_types (GstPad * pad)
{
  static const GstQueryType speex_dec_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return speex_dec_src_query_types;
}

static gboolean
speex_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstSpeexDec *dec;
  gboolean res = FALSE;

  dec = GST_SPEEX_DEC (gst_pad_get_parent (pad));

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

      if ((res = speex_dec_convert (dec->srcpad, GST_FORMAT_TIME,
                  segment.position, &format, &cur))) {
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
      if ((res = speex_dec_convert (dec->srcpad, GST_FORMAT_TIME,
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
speex_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstSpeexDec *dec = GST_SPEEX_DEC (gst_pad_get_parent (pad));

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
      if (!(res = speex_dec_convert (pad, format, cur, &tformat, &tcur)))
        break;
      if (!(res = speex_dec_convert (pad, format, stop, &tformat, &tstop)))
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
speex_dec_sink_event (GstPad * pad, GstEvent * event)
{
  GstSpeexDec *dec;
  gboolean ret = FALSE;

  dec = GST_SPEEX_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);
      ret = speex_dec_sink_setcaps (pad, caps);
      gst_event_unref (event);
      break;
    }
    case GST_EVENT_SEGMENT:{
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      if (segment.format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (segment.rate <= 0.0)
        goto newseg_wrong_rate;

#if 0
      if (update) {
        /* time progressed without data, see if we can fill the gap with
         * some concealment data */
        if (dec->segment.position < start) {
          GstClockTime duration;

          duration = start - dec->segment.position;
          speex_dec_chain_parse_data (dec, NULL, dec->segment.position,
              duration);
        }
      }
#endif

      /* now configure the values */
      dec->segment = segment;

      GST_DEBUG_OBJECT (dec, "segment now: %" GST_SEGMENT_FORMAT, &segment);
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
speex_dec_chain_parse_header (GstSpeexDec * dec, GstBuffer * buf)
{
  GstCaps *caps;
  char *data;
  gsize size;

  /* get the header */
  data = gst_buffer_map (buf, &size, NULL, GST_MAP_READ);
  dec->header = speex_packet_to_header (data, size);
  gst_buffer_unmap (buf, data, size);

  if (!dec->header)
    goto no_header;

  if (dec->header->mode >= SPEEX_NB_MODES || dec->header->mode < 0)
    goto mode_too_old;

  dec->mode = speex_lib_get_mode (dec->header->mode);

  /* initialize the decoder */
  dec->state = speex_decoder_init (dec->mode);
  if (!dec->state)
    goto init_failed;

  speex_decoder_ctl (dec->state, SPEEX_SET_ENH, &dec->enh);
  speex_decoder_ctl (dec->state, SPEEX_GET_FRAME_SIZE, &dec->frame_size);

  if (dec->header->nb_channels != 1) {
    dec->stereo = speex_stereo_state_init ();
    dec->callback.callback_id = SPEEX_INBAND_STEREO;
    dec->callback.func = speex_std_stereo_request_handler;
    dec->callback.data = dec->stereo;
    speex_decoder_ctl (dec->state, SPEEX_SET_HANDLER, &dec->callback);
  }

  speex_decoder_ctl (dec->state, SPEEX_SET_SAMPLING_RATE, &dec->header->rate);

  dec->frame_duration = gst_util_uint64_scale_int (dec->frame_size,
      GST_SECOND, dec->header->rate);

  speex_bits_init (&dec->bits);

  /* set caps */
  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, dec->header->rate,
      "channels", G_TYPE_INT, dec->header->nb_channels,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);

  if (!gst_pad_set_caps (dec->srcpad, caps))
    goto nego_failed;

  gst_caps_unref (caps);
  return GST_FLOW_OK;

  /* ERRORS */
no_header:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't read header"));
    return GST_FLOW_ERROR;
  }
mode_too_old:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL),
        ("Mode number %d does not (yet/any longer) exist in this version",
            dec->header->mode));
    return GST_FLOW_ERROR;
  }
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

static GstFlowReturn
speex_dec_chain_parse_comments (GstSpeexDec * dec, GstBuffer * buf)
{
  GstTagList *list;
  gchar *ver, *encoder = NULL;

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
      GST_TAG_AUDIO_CODEC, "Speex", NULL);

  ver = g_strndup (dec->header->speex_version, SPEEX_HEADER_VERSION_LENGTH);
  g_strstrip (ver);

  if (ver != NULL && *ver != '\0') {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER_VERSION, ver, NULL);
  }

  if (dec->header->bitrate > 0) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, (guint) dec->header->bitrate, NULL);
  }

  GST_INFO_OBJECT (dec, "tags: %" GST_PTR_FORMAT, list);

  gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, list);

  g_free (encoder);
  g_free (ver);

  return GST_FLOW_OK;
}

static GstFlowReturn
speex_dec_chain_parse_data (GstSpeexDec * dec, GstBuffer * buf,
    GstClockTime timestamp, GstClockTime duration)
{
  GstFlowReturn res = GST_FLOW_OK;
  gint i, fpp;
  SpeexBits *bits;
  gsize size;
  char *data;

  if (!dec->frame_duration)
    goto not_negotiated;

  if (timestamp != -1) {
    dec->segment.position = timestamp;
  } else {
    timestamp = dec->segment.position;
  }

  if (buf) {
    /* send data to the bitstream */
    data = gst_buffer_map (buf, &size, NULL, GST_MAP_READ);
    speex_bits_read_from (&dec->bits, data, size);
    gst_buffer_unmap (buf, data, size);

    fpp = dec->header->frames_per_packet;
    bits = &dec->bits;

    GST_DEBUG_OBJECT (dec, "received buffer of size %u, fpp %d, %d bits", size,
        fpp, speex_bits_remaining (bits));
  } else {
    /* concealment data, pass NULL as the bits parameters */
    GST_DEBUG_OBJECT (dec, "creating concealment data");
    fpp = dec->header->frames_per_packet;
    bits = NULL;
  }


  /* now decode each frame, catering for unknown number of them (e.g. rtp) */
  for (i = 0; i < fpp; i++) {
    GstBuffer *outbuf;
    gint16 *out_data;
    gint ret;

    GST_LOG_OBJECT (dec, "decoding frame %d/%d, %d bits remaining", i, fpp,
        bits ? speex_bits_remaining (bits) : -1);
#if 0
    res = gst_pad_alloc_buffer_and_set_caps (dec->srcpad,
        GST_BUFFER_OFFSET_NONE, dec->frame_size * dec->header->nb_channels * 2,
        GST_PAD_CAPS (dec->srcpad), &outbuf);

    if (res != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (dec, "buf alloc flow: %s", gst_flow_get_name (res));
      return res;
    }
#endif
    /* FIXME, we can use a bufferpool because we have fixed size buffers. We
     * could also use an allocator */
    outbuf =
        gst_buffer_new_allocate (NULL,
        dec->frame_size * dec->header->nb_channels * 2, 0);

    out_data = gst_buffer_map (outbuf, &size, NULL, GST_MAP_WRITE);
    ret = speex_decode_int (dec->state, bits, out_data);
    gst_buffer_unmap (outbuf, out_data, size);

    if (ret == -1) {
      /* uh? end of stream */
      if (fpp == 0 && speex_bits_remaining (bits) < 8) {
        /* if we did not know how many frames to expect, then we get this
           at the end if there are leftover bits to pad to the next byte */
      } else {
        GST_WARNING_OBJECT (dec, "Unexpected end of stream found");
      }
      gst_buffer_unref (outbuf);
      outbuf = NULL;
      break;
    } else if (ret == -2) {
      GST_WARNING_OBJECT (dec, "Decoding error: corrupted stream?");
      gst_buffer_unref (outbuf);
      outbuf = NULL;
      break;
    }

    if (bits && speex_bits_remaining (bits) < 0) {
      GST_WARNING_OBJECT (dec, "Decoding overflow: corrupted stream?");
      gst_buffer_unref (outbuf);
      outbuf = NULL;
      break;
    }
    if (dec->header->nb_channels == 2)
      speex_decode_stereo_int (out_data, dec->frame_size, dec->stereo);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    GST_BUFFER_DURATION (outbuf) = dec->frame_duration;

    dec->segment.position += dec->frame_duration;
    timestamp = dec->segment.position;

    GST_LOG_OBJECT (dec, "pushing buffer with ts=%" GST_TIME_FORMAT ", dur=%"
        GST_TIME_FORMAT, GST_TIME_ARGS (timestamp),
        GST_TIME_ARGS (dec->frame_duration));

    res = gst_pad_push (dec->srcpad, outbuf);

    if (res != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (dec, "flow: %s", gst_flow_get_name (res));
      break;
    }
  }

  return res;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (dec, CORE, NEGOTIATION, (NULL),
        ("decoder not initialized"));
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
memcmp_buffers (GstBuffer * buf1, GstBuffer * buf2)
{
  gsize size1, size2;
  gpointer data1;
  gboolean res;

  size1 = gst_buffer_get_size (buf1);
  size2 = gst_buffer_get_size (buf2);

  if (size1 != size2)
    return FALSE;

  data1 = gst_buffer_map (buf1, NULL, NULL, GST_MAP_READ);
  res = gst_buffer_memcmp (buf2, 0, data1, size1) == 0;
  gst_buffer_unmap (buf1, data1, size1);

  return res;
}

static GstFlowReturn
speex_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res;
  GstSpeexDec *dec;

  dec = GST_SPEEX_DEC (gst_pad_get_parent (pad));

  /* If we have the streamheader and vorbiscomment from the caps already
   * ignore them here */
  if (dec->streamheader && dec->vorbiscomment) {
    if (memcmp_buffers (dec->streamheader, buf)) {
      res = GST_FLOW_OK;
    } else if (memcmp_buffers (dec->vorbiscomment, buf)) {
      res = GST_FLOW_OK;
    } else {
      res =
          speex_dec_chain_parse_data (dec, buf, GST_BUFFER_TIMESTAMP (buf),
          GST_BUFFER_DURATION (buf));
    }
  } else {
    /* Otherwise fall back to packet counting and assume that the
     * first two packets are the headers. */
    switch (dec->packetno) {
      case 0:
        res = speex_dec_chain_parse_header (dec, buf);
        break;
      case 1:
        res = speex_dec_chain_parse_comments (dec, buf);
        break;
      default:
        res =
            speex_dec_chain_parse_data (dec, buf, GST_BUFFER_TIMESTAMP (buf),
            GST_BUFFER_DURATION (buf));
        break;
    }
  }

  dec->packetno++;

  gst_buffer_unref (buf);
  gst_object_unref (dec);

  return res;
}

static void
gst_speex_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSpeexDec *speexdec;

  speexdec = GST_SPEEX_DEC (object);

  switch (prop_id) {
    case ARG_ENH:
      g_value_set_boolean (value, speexdec->enh);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_speex_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpeexDec *speexdec;

  speexdec = GST_SPEEX_DEC (object);

  switch (prop_id) {
    case ARG_ENH:
      speexdec->enh = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstStateChangeReturn
speex_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstSpeexDec *dec = GST_SPEEX_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_speex_dec_reset (dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}
