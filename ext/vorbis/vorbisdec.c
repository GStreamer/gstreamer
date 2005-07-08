/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
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

#include "vorbisdec.h"
#include <string.h>
#include <gst/tag/tag.h>
#include <gst/audio/multichannel.h>

GST_DEBUG_CATEGORY_EXTERN (vorbisdec_debug);
#define GST_CAT_DEFAULT vorbisdec_debug

static GstElementDetails vorbis_dec_details = {
  "VorbisDec",
  "Codec/Decoder/Audio",
  "decode raw vorbis streams to float audio",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
};

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate vorbis_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-float, "
        "rate = (int) [ 8000, 50000 ], "
        "channels = (int) [ 1, 6 ], " "endianness = (int) BYTE_ORDER, "
/* no ifdef in macros, please
#ifdef GST_VORBIS_DEC_SEQUENTIAL
      "layout = \"sequential\", "
#endif
*/
        "width = (int) 32, " "buffer-frames = (int) 0")
    );

static GstStaticPadTemplate vorbis_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstVorbisDec, gst_vorbis_dec, GstElement, GST_TYPE_ELEMENT);

static gboolean vorbis_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn vorbis_dec_chain (GstPad * pad, GstBuffer * buffer);
static GstElementStateReturn vorbis_dec_change_state (GstElement * element);

#if 0
static const GstFormat *vorbis_dec_get_formats (GstPad * pad);
#endif

static gboolean vorbis_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean vorbis_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static gboolean vorbis_dec_sink_query (GstPad * pad, GstQuery * query);

static void
gst_vorbis_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vorbis_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&vorbis_dec_sink_factory));
  gst_element_class_set_details (element_class, &vorbis_dec_details);
}

static void
gst_vorbis_dec_class_init (GstVorbisDecClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = vorbis_dec_change_state;
}

#if 0
static const GstFormat *
vorbis_dec_get_formats (GstPad * pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,         /* samples in the audio case */
    GST_FORMAT_TIME,
    0
  };
  static GstFormat sink_formats[] = {
    /*GST_FORMAT_BYTES, */
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,         /* granulepos or samples */
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}
#endif

#if 0
static const GstEventMask *
vorbis_get_event_masks (GstPad * pad)
{
  static const GstEventMask vorbis_dec_src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return vorbis_dec_src_event_masks;
}
#endif

static const GstQueryType *
vorbis_get_query_types (GstPad * pad)
{
  static const GstQueryType vorbis_dec_src_query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return vorbis_dec_src_query_types;
}

static void
gst_vorbis_dec_init (GstVorbisDec * dec)
{
  dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&vorbis_dec_sink_factory), "sink");
  gst_pad_set_event_function (dec->sinkpad, vorbis_dec_sink_event);
  gst_pad_set_chain_function (dec->sinkpad, vorbis_dec_chain);
  gst_pad_set_query_function (dec->sinkpad, vorbis_dec_sink_query);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&vorbis_dec_src_factory), "src");
  gst_pad_set_event_function (dec->srcpad, vorbis_dec_src_event);
  gst_pad_set_query_type_function (dec->srcpad, vorbis_get_query_types);
  gst_pad_set_query_function (dec->srcpad, vorbis_dec_src_query);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);
}

static gboolean
vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstVorbisDec *dec;
  guint64 scale = 1;

  dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  if (dec->packetno < 1)
    return FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  if (dec->sinkpad == pad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = sizeof (float) * dec->vi.channels;
        case GST_FORMAT_DEFAULT:
          *dest_value = scale * (src_value * dec->vi.rate / GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * sizeof (float) * dec->vi.channels;
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / dec->vi.rate;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (sizeof (float) * dec->vi.channels);
          break;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND /
              (dec->vi.rate * sizeof (float) * dec->vi.channels);
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
vorbis_dec_src_query (GstPad * pad, GstQuery * query)
{
  gint64 granulepos;
  GstVorbisDec *dec;
  gboolean res;

  dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 value, total;

      /* query peer for total length */
      if (!(res = gst_pad_query (GST_PAD_PEER (dec->sinkpad), query)))
        goto error;

      granulepos = dec->granulepos;

      gst_query_parse_position (query, &format, NULL, &total);

      /* and convert to the final format */
      if (!(res =
              vorbis_dec_convert (pad, GST_FORMAT_DEFAULT, granulepos, &format,
                  &value)))
        goto error;

      gst_query_set_position (query, format, value, total);

      GST_LOG_OBJECT (dec,
          "query %u: peer returned granulepos: %llu - we return %llu (format %u)",
          query, granulepos, value, format);

      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              vorbis_dec_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;

error:
  {
    GST_DEBUG ("error handling event");
    return res;
  }
}

static gboolean
vorbis_dec_sink_query (GstPad * pad, GstQuery * query)
{
  GstVorbisDec *dec;
  gboolean res;

  dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              vorbis_dec_convert (pad, src_fmt, src_val, &dest_fmt, &dest_val)))
        goto error;
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = FALSE;
      break;
  }
error:
  return res;
}

static gboolean
vorbis_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstVorbisDec *dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      gint64 value;
      GstFormat my_format = GST_FORMAT_TIME;

      /* convert to time */
      res = vorbis_dec_convert (pad, GST_EVENT_SEEK_FORMAT (event),
          GST_EVENT_SEEK_OFFSET (event), &my_format, &value);
      if (res) {
        GstEvent *real_seek = gst_event_new_seek (
            (GST_EVENT_SEEK_TYPE (event) & ~GST_SEEK_FORMAT_MASK) |
            GST_FORMAT_TIME, value);

        res = gst_pad_send_event (GST_PAD_PEER (dec->sinkpad), real_seek);
      }
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
vorbis_dec_sink_event (GstPad * pad, GstEvent * event)
{
  gint64 start_value, end_value, time, bytes;
  gboolean ret = TRUE;
  GstVorbisDec *dec;

  dec = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      GST_STREAM_LOCK (pad);
      if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT,
              (gint64 *) & start_value, &end_value)) {
        dec->granulepos = start_value;
        GST_DEBUG_OBJECT (dec,
            "setting granuleposition to %" G_GUINT64_FORMAT " after discont",
            start_value);
      } else {
        if (gst_event_discont_get_value (event, GST_FORMAT_TIME,
                (gint64 *) & start_value, &end_value)) {
          dec->granulepos = start_value * dec->vi.rate / GST_SECOND;
          GST_DEBUG_OBJECT (dec,
              "setting granuleposition to %" G_GUINT64_FORMAT " after discont",
              dec->granulepos);
        } else {
          GST_WARNING_OBJECT (dec,
              "discont event didn't include offset, we might set it wrong now");
          dec->granulepos = -1;
        }
      }
      GST_DEBUG ("vd: discont %" G_GUINT64_FORMAT, dec->granulepos);

      dec->granulepos = -1;

      if (dec->packetno < 3) {
        if (dec->granulepos != 0)
          GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
              ("can't handle discont before parsing first 3 packets"));
        dec->packetno = 0;
#if 0
        gst_pad_push_event (dec->srcpad, gst_event_new_discontinuous (FALSE,
                GST_FORMAT_TIME, (guint64) 0, GST_FORMAT_DEFAULT,
                (guint64) 0, GST_FORMAT_BYTES, (guint64) 0, 0));
#endif
        gst_event_ref (event);
        gst_pad_push_event (dec->srcpad, event);
      } else {
        GstFormat time_format, default_format, bytes_format;

        time_format = GST_FORMAT_TIME;
        default_format = GST_FORMAT_DEFAULT;
        bytes_format = GST_FORMAT_BYTES;

        dec->packetno = 3;
        /* if one of them works, all of them work */
        if (vorbis_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT,
                dec->granulepos, &time_format, &time)
            && vorbis_dec_convert (dec->srcpad, GST_FORMAT_DEFAULT,
                dec->granulepos, &bytes_format, &bytes)) {

          gst_event_ref (event);
          gst_pad_push_event (dec->srcpad, event);
          /*
             gst_pad_push_event (dec->srcpad,
             gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
             time, GST_FORMAT_DEFAULT, dec->granulepos,
             GST_FORMAT_BYTES, bytes, 0));
           */
        } else {
          GST_ERROR_OBJECT (dec,
              "failed to parse data for DISCONT event, not sending any");
          gst_event_ref (event);
          gst_pad_push_event (dec->srcpad, event);
        }
#ifdef HAVE_VORBIS_SYNTHESIS_RESTART
        vorbis_synthesis_restart (&dec->vd);
#endif
      }
      GST_STREAM_UNLOCK (pad);
      gst_event_unref (event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }
  return ret;
}

static GstFlowReturn
vorbis_handle_comment_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  gchar *encoder = NULL;
  GstMessage *message;
  GstTagList *list;
  GstBuffer *buf;

  GST_DEBUG ("parsing comment packet");

  buf = gst_buffer_new_and_alloc (packet->bytes);
  GST_BUFFER_DATA (buf) = packet->packet;

  list =
      gst_tag_list_from_vorbiscomment_buffer (buf, (guint8 *) "\003vorbis", 7,
      &encoder);

  gst_buffer_unref (buf);

  if (!list) {
    GST_ERROR_OBJECT (vd, "couldn't decode comments");
    list = gst_tag_list_new ();
  }
  if (encoder) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER, encoder, NULL);
    g_free (encoder);
  }
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_ENCODER_VERSION, vd->vi.version,
      GST_TAG_AUDIO_CODEC, "Vorbis", NULL);
  if (vd->vi.bitrate_upper > 0)
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
  if (vd->vi.bitrate_nominal > 0)
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_NOMINAL_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
  if (vd->vi.bitrate_lower > 0)
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);

  message = gst_message_new_tag ((GstObject *) vd, list);
  gst_element_post_message (GST_ELEMENT (vd), message);

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_type_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  GstCaps *caps;
  const GstAudioChannelPosition *pos = NULL;

  /* done */
  vorbis_synthesis_init (&vd->vd, &vd->vi);
  vorbis_block_init (&vd->vd, &vd->vb);
  caps = gst_caps_new_simple ("audio/x-raw-float",
      "rate", G_TYPE_INT, vd->vi.rate,
      "channels", G_TYPE_INT, vd->vi.channels,
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, 32, "buffer-frames", G_TYPE_INT, 0, NULL);

  switch (vd->vi.channels) {
    case 1:
    case 2:
      /* nothing */
      break;
    case 3:{
      static GstAudioChannelPosition pos3[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
      };
      pos = pos3;
      break;
    }
    case 4:{
      static GstAudioChannelPosition pos4[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
      };
      pos = pos4;
      break;
    }
    case 5:{
      static GstAudioChannelPosition pos5[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
      };
      pos = pos5;
      break;
    }
    case 6:{
      static GstAudioChannelPosition pos6[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_LFE
      };
      pos = pos6;
      break;
    }
    default:
      goto channel_count_error;
  }
  if (pos) {
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  }
  gst_pad_set_caps (vd->srcpad, caps);
  gst_caps_unref (caps);

  vd->initialized = TRUE;

  return GST_FLOW_OK;

  /* ERROR */
channel_count_error:
  {
    gst_caps_unref (caps);
    GST_ELEMENT_ERROR (vd, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported channel count %d", vd->vi.channels));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_handle_header_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  GstFlowReturn res;

  GST_DEBUG ("parsing header packet");

  /* Packetno = 0 if the first byte is exactly 0x01 */
  packet->b_o_s = (packet->packet[0] == 0x1) ? 1 : 0;

  if (vorbis_synthesis_headerin (&vd->vi, &vd->vc, packet))
    goto header_read_error;

  switch (packet->packetno) {
    case 1:
      res = vorbis_handle_comment_packet (vd, packet);
      break;
    case 2:
      res = vorbis_handle_type_packet (vd, packet);
      break;
    default:
      /* ignore */
      res = GST_FLOW_OK;
      break;
  }
  return res;

  /* ERRORS */
header_read_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't read header packet"));
    return GST_FLOW_ERROR;
  }
}

static void
copy_samples (float *out, float **in, guint samples, gint channels)
{
  gint i, j;

#ifdef GST_VORBIS_DEC_SEQUENTIAL
  for (i = 0; i < channels; i++) {
    memcpy (out, in[i], samples * sizeof (float));
    out += samples;
  }
#else
  for (j = 0; j < samples; j++) {
    for (i = 0; i < channels; i++) {
      *out++ = in[i][j];
    }
  }
#endif
}

static GstFlowReturn
vorbis_handle_data_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  float **pcm;
  guint sample_count;
  GstFlowReturn result;

  if (!vd->initialized)
    goto not_initialized;

  /* normal data packet */
  if (vorbis_synthesis (&vd->vb, packet))
    goto could_not_read;

  if (vorbis_synthesis_blockin (&vd->vd, &vd->vb) < 0)
    goto not_accepted;

  sample_count = vorbis_synthesis_pcmout (&vd->vd, &pcm);
  if (sample_count > 0) {
    GstBuffer *out;

    result = gst_pad_alloc_buffer (vd->srcpad, GST_BUFFER_OFFSET_NONE,
        sample_count * vd->vi.channels * sizeof (float),
        GST_PAD_CAPS (vd->srcpad), &out);

    if (result == GST_FLOW_OK) {
      float *out_data = (float *) GST_BUFFER_DATA (out);

      copy_samples (out_data, pcm, sample_count, vd->vi.channels);

      GST_BUFFER_OFFSET (out) = vd->granulepos;
      GST_BUFFER_OFFSET_END (out) = vd->granulepos + sample_count;
      if (vd->granulepos != -1)
        GST_BUFFER_TIMESTAMP (out) = vd->granulepos * GST_SECOND / vd->vi.rate;
      else
        GST_BUFFER_TIMESTAMP (out) = -1;
      GST_BUFFER_DURATION (out) = sample_count * GST_SECOND / vd->vi.rate;

      result = gst_pad_push (vd->srcpad, out);

      if (vd->granulepos != -1)
        vd->granulepos += sample_count;
    } else {
      /* no buffer.. */
      result = GST_FLOW_OK;
    }
    vorbis_synthesis_read (&vd->vd, sample_count);
  } else {
    /* no samples.. */
    result = GST_FLOW_OK;
  }

  return result;

  /* ERRORS */
not_initialized:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("no header sent yet (packet no is %d)", packet->packetno));
    return GST_FLOW_ERROR;
  }
could_not_read:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't read data packet"));
    return GST_FLOW_ERROR;
  }
not_accepted:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("vorbis decoder did not accept data packet"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVorbisDec *vd;
  ogg_packet packet;
  GstFlowReturn result = GST_FLOW_OK;

  vd = GST_VORBIS_DEC (GST_PAD_PARENT (pad));

  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buffer);
  packet.bytes = GST_BUFFER_SIZE (buffer);
  packet.granulepos = vd->granulepos;
  packet.packetno = vd->packetno++;
  /* 
   * FIXME. Is there anyway to know that this is the last packet and
   * set e_o_s??
   */
  packet.e_o_s = 0;

  GST_DEBUG ("vorbis granule: %" G_GUINT64_FORMAT, packet.granulepos);

  /* switch depending on packet type */
  if (packet.packet[0] & 1) {
    if (vd->initialized) {
      GST_WARNING_OBJECT (vd, "Ignoring header");
      goto done;
    }
    result = vorbis_handle_header_packet (vd, &packet);
  } else {
    result = vorbis_handle_data_packet (vd, &packet);
  }

  GST_DEBUG ("offset end: %" G_GUINT64_FORMAT, GST_BUFFER_OFFSET_END (buffer));

  /* granulepos is the last sample in the packet */
  if (GST_BUFFER_OFFSET_END_IS_VALID (buffer))
    vd->granulepos = GST_BUFFER_OFFSET_END (buffer);;

done:
  gst_buffer_unref (buffer);

  return result;
}

static GstElementStateReturn
vorbis_dec_change_state (GstElement * element)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (element);
  GstElementState transition;
  GstElementStateReturn res;

  transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      vorbis_info_init (&vd->vi);
      vorbis_comment_init (&vd->vc);
      vd->initialized = FALSE;
      vd->granulepos = -1;
      vd->packetno = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = parent_class->change_state (element);

  switch (transition) {
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (vd, "PAUSED -> READY, clearing vorbis structures");
      vorbis_block_clear (&vd->vb);
      vorbis_dsp_clear (&vd->vd);
      vorbis_comment_clear (&vd->vc);
      vorbis_info_clear (&vd->vi);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return res;
}
