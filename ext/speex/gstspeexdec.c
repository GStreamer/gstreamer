/* GStreamer
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstspeexdec.h"
#include <string.h>
//#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY (speexdec_debug);
#define GST_CAT_DEFAULT speexdec_debug

static GstElementDetails speex_dec_details = {
  "SpeexDec",
  "Codec/Decoder/Audio",
  "decode speex streams to audio",
  "Wim Taymans <wim@fluendo.com>",
};

#define DEFAULT_ENH             TRUE

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

GST_BOILERPLATE (GstSpeexDec, gst_speex_dec, GstElement, GST_TYPE_ELEMENT);

static gboolean speex_dec_event (GstPad * pad, GstEvent * event);
static GstFlowReturn speex_dec_chain (GstPad * pad, GstBuffer * buf);
static GstStateChangeReturn speex_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean speex_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean speex_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean speex_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static void gst_speexdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_speexdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static void
gst_speex_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&speex_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&speex_dec_sink_factory));
  gst_element_class_set_details (element_class, &speex_dec_details);
}

static void
gst_speex_dec_class_init (GstSpeexDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_speexdec_set_property;
  gobject_class->get_property = gst_speexdec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ENH,
      g_param_spec_boolean ("enh", "Enh", "Enable perceptual enhancement",
          DEFAULT_ENH, G_PARAM_READWRITE));

  gstelement_class->change_state = speex_dec_change_state;

  GST_DEBUG_CATEGORY_INIT (speexdec_debug, "speexdec", 0,
      "speex decoding element");
}

static const GstQueryType *
speex_get_query_types (GstPad * pad)
{
  static const GstQueryType speex_dec_src_query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return speex_dec_src_query_types;
}

static void
gst_speex_dec_init (GstSpeexDec * dec, GstSpeexDecClass * g_class)
{
  dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&speex_dec_sink_factory), "sink");
  gst_pad_set_chain_function (dec->sinkpad, speex_dec_chain);
  gst_pad_set_event_function (dec->sinkpad, speex_dec_event);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&speex_dec_src_factory), "src");
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_pad_set_event_function (dec->srcpad, speex_dec_src_event);
  gst_pad_set_query_type_function (dec->srcpad, speex_get_query_types);
  gst_pad_set_query_function (dec->srcpad, speex_dec_src_query);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->enh = DEFAULT_ENH;
}

static gboolean
speex_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstSpeexDec *dec;
  guint64 scale = 1;

  dec = GST_SPEEXDEC (gst_pad_get_parent (pad));

  if (dec->packetno < 1)
    return FALSE;

  if (pad == dec->sinkpad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = sizeof (float) * dec->header->nb_channels;
        case GST_FORMAT_DEFAULT:
          *dest_value = scale * (src_value * dec->header->rate / GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * sizeof (float) * dec->header->nb_channels;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / dec->header->rate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (sizeof (float) * dec->header->nb_channels);
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND /
              (dec->header->rate * sizeof (float) * dec->header->nb_channels);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
speex_dec_src_query (GstPad * pad, GstQuery * query)
{
  gint64 samples_out = 0, total_samples;
  GstSpeexDec *dec = GST_SPEEXDEC (GST_OBJECT_PARENT (pad));
  GstFormat my_format = GST_FORMAT_TIME;
  GstPad *peer;

  if (GST_QUERY_TYPE (query) != GST_QUERY_POSITION)
    return FALSE;
  if (!(peer = gst_pad_get_peer (dec->sinkpad)))
    return FALSE;
  gst_pad_query_position (peer, &my_format, NULL, &total_samples);
  gst_object_unref (peer);
  samples_out = dec->samples_out;
  speex_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT, samples_out,
      &my_format, &samples_out);
  speex_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT, total_samples,
      &my_format, &total_samples);
  gst_query_set_position (query, GST_FORMAT_TIME, samples_out, total_samples);

  return TRUE;
}

static gboolean
speex_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstSpeexDec *dec = GST_SPEEXDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      gint64 cur, stop;
      GstFormat format, my_format = GST_FORMAT_DEFAULT;
      GstSeekType cur_type, stop_type;
      GstSeekFlags flags;

      gst_event_parse_seek (event, NULL, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);

      /* convert to samples_out */
      if (speex_dec_convert (pad, format, cur, &my_format, &cur) &&
          (stop == -1 ||
              speex_dec_convert (pad, format, stop, &my_format, &stop))) {
        GstEvent *real_seek = gst_event_new_seek (1.0, GST_FORMAT_DEFAULT,
            flags, cur_type, cur, stop_type, stop);

        res = gst_pad_send_event (GST_PAD_PEER (dec->sinkpad), real_seek);
      } else
        res = FALSE;
      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;
}

static gboolean
speex_dec_event (GstPad * pad, GstEvent * event)
{
  GstSpeexDec *dec = GST_SPEEXDEC (GST_OBJECT_PARENT (pad));
  gint64 value, time;
  GstFormat fmt;

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      gst_event_parse_newsegment (event, NULL, NULL, &fmt, &value, NULL, NULL);
      if (fmt == GST_FORMAT_DEFAULT) {
        dec->samples_out = value;
        GST_DEBUG_OBJECT (dec,
            "setting samples_out to %" G_GUINT64_FORMAT " after discont",
            value);
      } else {
        GST_WARNING_OBJECT (dec,
            "discont event didn't include offset, we might set it wrong now");
        value = 0;
      }
      if (dec->packetno < 2) {
        if (dec->samples_out != 0)
          GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
              ("can't handle discont before parsing first 2 packets"));
        dec->packetno = 0;
        gst_pad_push_event (dec->srcpad,
            gst_event_new_newsegment (FALSE, 1.0, GST_FORMAT_TIME,
                0, GST_CLOCK_TIME_NONE, 0));
      } else {
        GstFormat time_format = GST_FORMAT_TIME;

        dec->packetno = 2;
        /* if one of them works, all of them work */
        if (speex_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT,
                dec->samples_out, &time_format, &time)) {
          gst_pad_push_event (dec->srcpad,
              gst_event_new_newsegment (FALSE, 1.0, GST_FORMAT_TIME,
                  time, GST_CLOCK_TIME_NONE, 0));
        } else {
          GST_ERROR_OBJECT (dec,
              "failed to parse data for DISCONT event, not sending any");
        }
      }
      gst_event_unref (event);
      break;
    default:
      return gst_pad_event_default (dec->sinkpad, event);
  }

  return TRUE;
}

static GstFlowReturn
speex_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstFlowReturn res;
  GstSpeexDec *dec;

  dec = GST_SPEEXDEC (gst_pad_get_parent (pad));

  if (dec->packetno == 0) {
    GstCaps *caps;

    /* get the header */
    dec->header = speex_packet_to_header ((char *) GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    gst_buffer_unref (buf);
    if (!dec->header) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
          (NULL), ("couldn't read header"));
      return GST_FLOW_ERROR;
    }
    if (dec->header->mode >= SPEEX_NB_MODES) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
          (NULL),
          ("Mode number %d does not (yet/any longer) exist in this version",
              dec->header->mode));
      return GST_FLOW_ERROR;
    }

    dec->mode = (SpeexMode *) speex_mode_list[dec->header->mode];

    /* initialize the decoder */
    dec->state = speex_decoder_init (dec->mode);
    if (!dec->state) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
          (NULL), ("couldn't initialize decoder"));
      gst_buffer_unref (buf);
      return GST_FLOW_ERROR;
    }

    speex_decoder_ctl (dec->state, SPEEX_SET_ENH, &dec->enh);
    speex_decoder_ctl (dec->state, SPEEX_GET_FRAME_SIZE, &dec->frame_size);

    if (dec->header->nb_channels != 1) {
      dec->callback.callback_id = SPEEX_INBAND_STEREO;
      dec->callback.func = speex_std_stereo_request_handler;
      dec->callback.data = &dec->stereo;
      speex_decoder_ctl (dec->state, SPEEX_SET_HANDLER, &dec->callback);
    }

    speex_decoder_ctl (dec->state, SPEEX_SET_SAMPLING_RATE, &dec->header->rate);

    speex_bits_init (&dec->bits);

    /* set caps */
    caps = gst_caps_new_simple ("audio/x-raw-int",
        "rate", G_TYPE_INT, dec->header->rate,
        "channels", G_TYPE_INT, dec->header->nb_channels,
        "signed", G_TYPE_BOOLEAN, TRUE,
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "width", G_TYPE_INT, 16, "depth", G_TYPE_INT, 16, NULL);

    if (!gst_pad_set_caps (dec->srcpad, caps)) {
      gst_caps_unref (caps);
      return GST_FLOW_NOT_NEGOTIATED;
    }
    gst_caps_unref (caps);
    gst_pad_push_event (dec->srcpad,
        gst_event_new_newsegment (FALSE, 1.0, GST_FORMAT_TIME,
            0, GST_CLOCK_TIME_NONE, 0));
  } else if (dec->packetno == 1) {
    gchar *encoder = NULL;

    /* FIXME parse comments */
    GstTagList *list = gst_tag_list_new ();     //gst_tag_list_from_vorbiscomment_buffer (buf, "", 1,

//        &encoder);

    gst_buffer_unref (buf);

    if (!list) {
      GST_WARNING_OBJECT (dec, "couldn't decode comments");
      list = gst_tag_list_new ();
    }
    if (encoder) {
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_ENCODER, encoder, NULL);
      g_free (encoder);
    }
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_AUDIO_CODEC, "Speex", NULL);
    /*
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_ENCODER_VERSION, dec->vi.version, NULL);

       if (dec->vi.bitrate_upper > 0)
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
       if (vd->vi.bitrate_nominal > 0)
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_NOMINAL_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
       if (vd->vi.bitrate_lower > 0)
       gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
       GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);
     */
    gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, list);
  } else {
    gint i;

    /* send data to the bitstream */
    speex_bits_read_from (&dec->bits, (char *) GST_BUFFER_DATA (buf),
        GST_BUFFER_SIZE (buf));
    gst_buffer_unref (buf);

    /* now decode each frame */
    for (i = 0; i < dec->header->frames_per_packet; i++) {
      gint ret;
      GstBuffer *outbuf;
      gint16 *out_data;

      ret = speex_decode (dec->state, &dec->bits, dec->output);
      if (ret == -1) {
        /* uh? end of stream */
        GST_WARNING_OBJECT (dec, "Unexpected end of stream found");
        break;
      } else if (ret == -2) {
        GST_WARNING_OBJECT (dec, "Decoding error: corrupted stream?");
        break;
      }
      if (speex_bits_remaining (&dec->bits) < 0) {
        GST_WARNING_OBJECT (dec, "Decoding overflow: corrupted stream?");
        break;
      }
      if (dec->header->nb_channels == 2)
        speex_decode_stereo (dec->output, dec->frame_size, &dec->stereo);

      if ((res = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE,
                  dec->frame_size * dec->header->nb_channels * 2,
                  GST_PAD_CAPS (dec->srcpad), &outbuf)) != GST_FLOW_OK)
        return res;
      out_data = (gint16 *) GST_BUFFER_DATA (outbuf);

      /*PCM saturation (just in case) */
      for (i = 0; i < dec->frame_size * dec->header->nb_channels; i++) {
        if (dec->output[i] > 32767.0)
          out_data[i] = 32767;
        else if (dec->output[i] < -32768.0)
          out_data[i] = -32768;
        else
          out_data[i] = (gint16) dec->output[i];
      }

      GST_BUFFER_OFFSET (outbuf) = dec->samples_out;
      GST_BUFFER_OFFSET_END (outbuf) = dec->samples_out + dec->frame_size;
      GST_BUFFER_TIMESTAMP (outbuf) =
          dec->samples_out * GST_SECOND / dec->header->rate;
      GST_BUFFER_DURATION (outbuf) =
          dec->frame_size * GST_SECOND / dec->header->rate;
      res = gst_pad_push (dec->srcpad, outbuf);
      if (res != GST_FLOW_OK && res != GST_FLOW_NOT_LINKED)
        return res;
      dec->samples_out += dec->frame_size;
    }
  }
  dec->packetno++;

  return GST_FLOW_OK;
}

static void
gst_speexdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSpeexDec *speexdec;

  g_return_if_fail (GST_IS_SPEEXDEC (object));

  speexdec = GST_SPEEXDEC (object);

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
gst_speexdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSpeexDec *speexdec;

  g_return_if_fail (GST_IS_SPEEXDEC (object));

  speexdec = GST_SPEEXDEC (object);

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
  GstSpeexDec *vd = GST_SPEEXDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      vd->packetno = 0;
      vd->samples_out = 0;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return parent_class->change_state (element, transition);
}
