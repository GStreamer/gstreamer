/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * Tremor modifications <2006>:
 *   Chris Lord, OpenedHand Ltd. <chris@openedhand.com>, http://www.o-hand.com/
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
 * SECTION:element-ivorbisdec
 * @see_also: vorbisenc, oggdemux
 *
 * This element decodes a Vorbis stream to raw int audio.
 * <ulink url="http://www.vorbis.com/">Vorbis</ulink> is a royalty-free
 * audio codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>. The decoder uses integer math to be more suitable for
 * embedded devices.
 * 
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! ivorbisdec ! audioconvert ! alsasink
 * ]| Decode an Ogg/Vorbis. To create an Ogg/Vorbis file refer to the
 * documentation of vorbisenc.
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "vorbisdec.h"
#include <string.h>
#include <gst/audio/audio.h>
#include <gst/tag/tag.h>
#include <gst/audio/multichannel.h>

GST_DEBUG_CATEGORY_EXTERN (vorbisdec_debug);
#define GST_CAT_DEFAULT vorbisdec_debug

static const GstElementDetails vorbis_dec_details =
GST_ELEMENT_DETAILS ("Vorbis audio decoder",
    "Codec/Decoder/Audio",
    "decode raw vorbis streams to integer audio",
    "Benjamin Otte <in7y118@public.uni-hamburg.de>\n"
    "Chris Lord <chris@openedhand.com>");

static GstStaticPadTemplate vorbis_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) [ 1, MAX ], "
        "channels = (int) [ 1, 6 ], "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) { 16, 32 }, "
        "depth = (int) 16, " "signed = (boolean) true")
    );

static GstStaticPadTemplate vorbis_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GstIVorbisDec, gst_ivorbis_dec, GstElement, GST_TYPE_ELEMENT);

static void vorbis_dec_finalize (GObject * object);
static gboolean vorbis_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn vorbis_dec_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn vorbis_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean vorbis_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean vorbis_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);

static gboolean vorbis_dec_sink_query (GstPad * pad, GstQuery * query);

static void
gst_ivorbis_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstPadTemplate *src_template, *sink_template;

  src_template = gst_static_pad_template_get (&vorbis_dec_src_factory);
  gst_element_class_add_pad_template (element_class, src_template);

  sink_template = gst_static_pad_template_get (&vorbis_dec_sink_factory);
  gst_element_class_add_pad_template (element_class, sink_template);

  gst_element_class_set_details (element_class, &vorbis_dec_details);
}

static void
gst_ivorbis_dec_class_init (GstIVorbisDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = vorbis_dec_finalize;

  gstelement_class->change_state = GST_DEBUG_FUNCPTR (vorbis_dec_change_state);
}

static const GstQueryType *
vorbis_get_query_types (GstPad * pad)
{
  static const GstQueryType vorbis_dec_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return vorbis_dec_src_query_types;
}

static void
gst_ivorbis_dec_init (GstIVorbisDec * dec, GstIVorbisDecClass * g_class)
{
  dec->sinkpad = gst_pad_new_from_static_template (&vorbis_dec_sink_factory,
      "sink");

  gst_pad_set_event_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_sink_event));
  gst_pad_set_chain_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_chain));
  gst_pad_set_query_function (dec->sinkpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_sink_query));
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad = gst_pad_new_from_static_template (&vorbis_dec_src_factory,
      "src");

  gst_pad_set_event_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_src_event));
  gst_pad_set_query_type_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (vorbis_get_query_types));
  gst_pad_set_query_function (dec->srcpad,
      GST_DEBUG_FUNCPTR (vorbis_dec_src_query));
  gst_pad_use_fixed_caps (dec->srcpad);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->queued = NULL;
  dec->pendingevents = NULL;
  dec->taglist = NULL;
}

static void
vorbis_dec_finalize (GObject * object)
{
  /* Release any possibly allocated libvorbis data.
   * _clear functions can safely be called multiple times
   */
  GstIVorbisDec *vd = GST_IVORBIS_DEC (object);

  vorbis_block_clear (&vd->vb);
  vorbis_dsp_clear (&vd->vd);
  vorbis_comment_clear (&vd->vc);
  vorbis_info_clear (&vd->vi);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ivorbis_dec_reset (GstIVorbisDec * dec)
{
  GList *walk;

  dec->cur_timestamp = GST_CLOCK_TIME_NONE;
  dec->prev_timestamp = GST_CLOCK_TIME_NONE;
  dec->granulepos = -1;
  dec->discont = TRUE;
  gst_segment_init (&dec->segment, GST_FORMAT_TIME);

  for (walk = dec->queued; walk; walk = g_list_next (walk)) {
    gst_buffer_unref (GST_BUFFER_CAST (walk->data));
  }
  g_list_free (dec->queued);
  dec->queued = NULL;

  for (walk = dec->pendingevents; walk; walk = g_list_next (walk)) {
    gst_event_unref (GST_EVENT_CAST (walk->data));
  }
  g_list_free (dec->pendingevents);
  dec->pendingevents = NULL;

  if (dec->taglist)
    gst_tag_list_free (dec->taglist);
  dec->taglist = NULL;
}


static gboolean
vorbis_dec_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstIVorbisDec *dec;
  guint64 scale = 1;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  dec = GST_IVORBIS_DEC (gst_pad_get_parent (pad));

  if (!dec->initialized)
    goto no_header;

  if (dec->sinkpad == pad &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    goto no_format;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = dec->width * dec->vi.channels;
        case GST_FORMAT_DEFAULT:
          *dest_value =
              scale * gst_util_uint64_scale_int (src_value, dec->vi.rate,
              GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * dec->width * dec->vi.channels;
          break;
        case GST_FORMAT_TIME:
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, dec->vi.rate);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value / (dec->width * dec->vi.channels);
          break;
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale_int (src_value, GST_SECOND,
              dec->vi.rate * dec->width * dec->vi.channels);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }
done:
  gst_object_unref (dec);

  return res;

  /* ERRORS */
no_header:
  {
    GST_DEBUG_OBJECT (dec, "no header packets received");
    res = FALSE;
    goto done;
  }
no_format:
  {
    GST_DEBUG_OBJECT (dec, "formats unsupported");
    res = FALSE;
    goto done;
  }
}

static gboolean
vorbis_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstIVorbisDec *dec;
  gboolean res = FALSE;

  dec = GST_IVORBIS_DEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 granulepos, value;
      GstFormat my_format, format;
      gint64 time;

      /* we start from the last seen granulepos */
      granulepos = dec->granulepos;

      gst_query_parse_position (query, &format, NULL);

      /* and convert to the final format in two steps with time as the
       * intermediate step */
      my_format = GST_FORMAT_TIME;
      if (!(res =
              vorbis_dec_convert (pad, GST_FORMAT_DEFAULT, granulepos,
                  &my_format, &time)))
        goto error;

      /* correct for the segment values */
      time = gst_segment_to_stream_time (&dec->segment, GST_FORMAT_TIME, time);

      GST_LOG_OBJECT (dec,
          "query %p: our time: %" GST_TIME_FORMAT, query, GST_TIME_ARGS (time));

      /* and convert to the final format */
      if (!(res = vorbis_dec_convert (pad, my_format, time, &format, &value)))
        goto error;

      gst_query_set_position (query, format, value);

      GST_LOG_OBJECT (dec,
          "query %p: we return %lld (format %u)", query, value, format);

      break;
    }
    case GST_QUERY_DURATION:
    {
      GstPad *peer;

      if (!(peer = gst_pad_get_peer (dec->sinkpad))) {
        GST_WARNING_OBJECT (dec, "sink pad %" GST_PTR_FORMAT " is not linked",
            dec->sinkpad);
        goto error;
      }

      res = gst_pad_query (peer, query);
      gst_object_unref (peer);
      if (!res)
        goto error;

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
      res = gst_pad_query_default (pad, query);
      break;
  }
done:
  gst_object_unref (dec);

  return res;

  /* ERRORS */
error:
  {
    GST_WARNING_OBJECT (dec, "error handling query");
    goto done;
  }
}

static gboolean
vorbis_dec_sink_query (GstPad * pad, GstQuery * query)
{
  GstIVorbisDec *dec;
  gboolean res;

  dec = GST_IVORBIS_DEC (gst_pad_get_parent (pad));

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
      res = gst_pad_query_default (pad, query);
      break;
  }

done:
  gst_object_unref (dec);

  return res;

  /* ERRORS */
error:
  {
    GST_DEBUG_OBJECT (dec, "error converting value");
    goto done;
  }
}

static gboolean
vorbis_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstIVorbisDec *dec;

  dec = GST_IVORBIS_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstFormat format, tformat;
      gdouble rate;
      GstEvent *real_seek;
      GstSeekFlags flags;
      GstSeekType cur_type, stop_type;
      gint64 cur, stop;
      gint64 tcur, tstop;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);
      gst_event_unref (event);

      /* we have to ask our peer to seek to time here as we know
       * nothing about how to generate a granulepos from the src
       * formats or anything.
       *
       * First bring the requested format to time
       */
      tformat = GST_FORMAT_TIME;
      if (!(res = vorbis_dec_convert (pad, format, cur, &tformat, &tcur)))
        goto convert_error;
      if (!(res = vorbis_dec_convert (pad, format, stop, &tformat, &tstop)))
        goto convert_error;

      /* then seek with time on the peer */
      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      res = gst_pad_push_event (dec->sinkpad, real_seek);

      break;
    }
    default:
      res = gst_pad_push_event (dec->sinkpad, event);
      break;
  }
done:
  gst_object_unref (dec);

  return res;

  /* ERRORS */
convert_error:
  {
    GST_DEBUG_OBJECT (dec, "cannot convert start/stop for seek");
    goto done;
  }
}

static gboolean
vorbis_dec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstIVorbisDec *dec;

  dec = GST_IVORBIS_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* here we must clean any state in the decoder */
#ifdef HAVE_VORBIS_SYNTHESIS_RESTART
      vorbis_synthesis_restart (&dec->vd);
#endif
      gst_ivorbis_dec_reset (dec);
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;
      gboolean update;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need time and a positive rate for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      GST_DEBUG_OBJECT (dec,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values */
      gst_segment_set_newsegment_full (&dec->segment, update,
          rate, arate, format, start, stop, time);

      if (dec->initialized)
        /* and forward */
        ret = gst_pad_push_event (dec->srcpad, event);
      else {
        /* store it to send once we're initialized */
        dec->pendingevents = g_list_append (dec->pendingevents, event);
        ret = TRUE;
      }
      break;
    }
    default:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
  }
done:
  gst_object_unref (dec);

  return ret;

  /* ERRORS */
newseg_wrong_format:
  {
    GST_DEBUG_OBJECT (dec, "received non TIME newsegment");
    goto done;
  }
newseg_wrong_rate:
  {
    GST_DEBUG_OBJECT (dec, "negative rates not supported yet");
    goto done;
  }
}

static GstFlowReturn
vorbis_handle_identification_packet (GstIVorbisDec * vd)
{
  GstCaps *caps;
  const GstAudioChannelPosition *pos = NULL;
  gint width = 16;

  switch (vd->vi.channels) {
    case 1:
    case 2:
      /* nothing */
      break;
    case 3:{
      static const GstAudioChannelPosition pos3[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT
      };
      pos = pos3;
      break;
    }
    case 4:{
      static const GstAudioChannelPosition pos4[] = {
        GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
        GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
        GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
        GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT
      };
      pos = pos4;
      break;
    }
    case 5:{
      static const GstAudioChannelPosition pos5[] = {
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
      static const GstAudioChannelPosition pos6[] = {
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

  /* negotiate with downstream */
  caps = gst_pad_get_allowed_caps (vd->srcpad);
  if (caps) {
    if (!gst_caps_is_empty (caps)) {
      GstStructure *s;

      s = gst_caps_get_structure (caps, 0);
      /* template ensures 16 or 32 */
      gst_structure_get_int (s, "width", &width);
    }
    gst_caps_unref (caps);
  }
  vd->width = width >> 3;

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "rate", G_TYPE_INT, vd->vi.rate,
      "channels", G_TYPE_INT, vd->vi.channels,
      "endianness", G_TYPE_INT, G_BYTE_ORDER, "width", G_TYPE_INT, width,
      "depth", G_TYPE_INT, 16, "signed", G_TYPE_BOOLEAN, TRUE, NULL);

  if (pos) {
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  }
  gst_pad_set_caps (vd->srcpad, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;

  /* ERROR */
channel_count_error:
  {
    GST_ELEMENT_ERROR (vd, STREAM, NOT_IMPLEMENTED, (NULL),
        ("Unsupported channel count %d", vd->vi.channels));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_handle_comment_packet (GstIVorbisDec * vd, ogg_packet * packet)
{
  guint bitrate = 0;
  gchar *encoder = NULL;
  GstTagList *list, *old_list;
  GstBuffer *buf;

  GST_DEBUG_OBJECT (vd, "parsing comment packet");

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = packet->packet->buffer->data;
  GST_BUFFER_SIZE (buf) = packet->packet->buffer->size;

  list =
      gst_tag_list_from_vorbiscomment_buffer (buf, (guint8 *) "\003vorbis", 7,
      &encoder);

  old_list = vd->taglist;
  vd->taglist = gst_tag_list_merge (vd->taglist, list, GST_TAG_MERGE_REPLACE);

  if (old_list)
    gst_tag_list_free (old_list);
  gst_tag_list_free (list);
  gst_buffer_unref (buf);

  if (!vd->taglist) {
    GST_ERROR_OBJECT (vd, "couldn't decode comments");
    vd->taglist = gst_tag_list_new ();
  }
  if (encoder) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER, encoder, NULL);
    g_free (encoder);
  }
  gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_ENCODER_VERSION, vd->vi.version,
      GST_TAG_AUDIO_CODEC, "Vorbis", NULL);
  if (vd->vi.bitrate_nominal > 0 && vd->vi.bitrate_nominal <= 0x7FFFFFFF) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_NOMINAL_BITRATE, (guint) vd->vi.bitrate_nominal, NULL);
    bitrate = vd->vi.bitrate_nominal;
  }
  if (vd->vi.bitrate_upper > 0 && vd->vi.bitrate_upper <= 0x7FFFFFFF) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_MAXIMUM_BITRATE, (guint) vd->vi.bitrate_upper, NULL);
    if (!bitrate)
      bitrate = vd->vi.bitrate_upper;
  }
  if (vd->vi.bitrate_lower > 0 && vd->vi.bitrate_lower <= 0x7FFFFFFF) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_MINIMUM_BITRATE, (guint) vd->vi.bitrate_lower, NULL);
    if (!bitrate)
      bitrate = vd->vi.bitrate_lower;
  }
  if (bitrate) {
    gst_tag_list_add (vd->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, (guint) bitrate, NULL);
  }

  if (vd->initialized) {
    gst_element_found_tags_for_pad (GST_ELEMENT_CAST (vd), vd->srcpad,
        vd->taglist);
    vd->taglist = NULL;
  } else {
    /* Only post them as messages for the time being. *
     * They will be pushed on the pad once the decoder is initialized */
    gst_element_post_message (GST_ELEMENT_CAST (vd),
        gst_message_new_tag (GST_OBJECT (vd), gst_tag_list_copy (vd->taglist)));
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_type_packet (GstIVorbisDec * vd)
{
  GList *walk;

  g_assert (vd->initialized == FALSE);

  vorbis_synthesis_init (&vd->vd, &vd->vi);
  vorbis_block_init (&vd->vd, &vd->vb);
  vd->initialized = TRUE;

  if (vd->pendingevents) {
    for (walk = vd->pendingevents; walk; walk = g_list_next (walk))
      gst_pad_push_event (vd->srcpad, GST_EVENT_CAST (walk->data));
    g_list_free (vd->pendingevents);
    vd->pendingevents = NULL;
  }

  if (vd->taglist) {
    /* The tags have already been sent on the bus as messages. */
    gst_pad_push_event (vd->srcpad, gst_event_new_tag (vd->taglist));
    vd->taglist = NULL;
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_header_packet (GstIVorbisDec * vd, ogg_packet * packet)
{
  GstFlowReturn res;

  GST_DEBUG_OBJECT (vd, "parsing header packet");

  /* Packetno = 0 if the first byte is exactly 0x01 */
  packet->b_o_s = packet->packet->length ?
      ((packet->packet->buffer->data[0] == 0x1) ? 1 : 0) : 0;

  if (vorbis_synthesis_headerin (&vd->vi, &vd->vc, packet))
    goto header_read_error;

  switch (packet->packet->length ? packet->packet->buffer->data[0] : 0x0) {
    case 0x01:
      res = vorbis_handle_identification_packet (vd);
      break;
    case 0x03:
      res = vorbis_handle_comment_packet (vd, packet);
      break;
    case 0x05:
      res = vorbis_handle_type_packet (vd);
      break;
    default:
      /* ignore */
      g_warning ("unknown vorbis header packet found");
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

/* Taken from Tremor, misc.h */
#ifdef _ARM_ASSEM_
static inline ogg_int32_t
CLIP_TO_15 (ogg_int32_t x)
{
  int tmp;
  asm volatile ("subs	%1, %0, #32768\n\t"
      "movpl	%0, #0x7f00\n\t"
      "orrpl	%0, %0, #0xff\n"
      "adds	%1, %0, #32768\n\t"
      "movmi	%0, #0x8000":"+r" (x), "=r" (tmp)
      ::"cc");

  return (x);
}
#else
static inline ogg_int32_t
CLIP_TO_15 (ogg_int32_t x)
{
  int ret = x;

  ret -= ((x <= 32767) - 1) & (x - 32767);
  ret -= ((x >= -32768) - 1) & (x + 32768);
  return (ret);
}
#endif

static void
copy_samples (gint32 * out, ogg_int32_t ** in, guint samples, gint channels)
{
  gint i, j;

  for (j = 0; j < samples; j++) {
    for (i = 0; i < channels; i++) {
      *out++ = CLIP_TO_15 (in[i][j] >> 9);
    }
  }
}

static void
copy_samples_16 (gint16 * out, ogg_int32_t ** in, guint samples, gint channels)
{
  gint i, j;

  for (j = 0; j < samples; j++) {
    for (i = 0; i < channels; i++) {
      *out++ = CLIP_TO_15 (in[i][j] >> 9);
    }
  }
}

/* clip output samples to the segment boundaries
 */
static gboolean
vorbis_do_clip (GstIVorbisDec * dec, GstBuffer * buf)
{
  gint64 start, stop, cstart, cstop, diff;

  start = GST_BUFFER_TIMESTAMP (buf);
  stop = start + GST_BUFFER_DURATION (buf);

  if (!gst_segment_clip (&dec->segment, GST_FORMAT_TIME,
          start, stop, &cstart, &cstop))
    goto clipped;

  /* see if some clipping happened */
  diff = cstart - start;
  if (diff > 0) {
    GST_BUFFER_TIMESTAMP (buf) = cstart;
    GST_BUFFER_DURATION (buf) -= diff;

    /* bring clipped time to samples */
    diff = gst_util_uint64_scale_int (diff, dec->vi.rate, GST_SECOND);
    /* samples to bytes */
    diff *= (dec->width * dec->vi.channels);
    GST_DEBUG_OBJECT (dec, "clipping start to %" GST_TIME_FORMAT " %"
        G_GUINT64_FORMAT " bytes", GST_TIME_ARGS (cstart), diff);
    GST_BUFFER_DATA (buf) += diff;
    GST_BUFFER_SIZE (buf) -= diff;
  }
  diff = stop - cstop;
  if (diff > 0) {
    GST_BUFFER_DURATION (buf) -= diff;

    /* bring clipped time to samples and then to bytes */
    diff = gst_util_uint64_scale_int (diff, dec->vi.rate, GST_SECOND);
    diff *= (dec->width * dec->vi.channels);
    GST_DEBUG_OBJECT (dec, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GUINT64_FORMAT " bytes", GST_TIME_ARGS (cstop), diff);
    GST_BUFFER_SIZE (buf) -= diff;
  }

  return FALSE;

  /* dropped buffer */
clipped:
  {
    GST_DEBUG_OBJECT (dec, "clipped buffer");
    gst_buffer_unref (buf);
    return TRUE;
  }
}

static GstFlowReturn
vorbis_dec_push (GstIVorbisDec * dec, GstBuffer * buf)
{
  GstFlowReturn result;
  gint64 outoffset = GST_BUFFER_OFFSET (buf);

  if (outoffset == -1) {
    dec->queued = g_list_append (dec->queued, buf);
    GST_DEBUG_OBJECT (dec, "queued buffer");
    result = GST_FLOW_OK;
  } else {
    if (G_UNLIKELY (dec->queued)) {
      gint64 size;
      GList *walk;

      GST_DEBUG_OBJECT (dec, "first buffer with offset %lld", outoffset);

      size = g_list_length (dec->queued);
      for (walk = g_list_last (dec->queued); walk;
          walk = g_list_previous (walk)) {
        GstBuffer *buffer = GST_BUFFER (walk->data);

        outoffset -= GST_BUFFER_SIZE (buffer) / (dec->width * dec->vi.channels);

        GST_BUFFER_OFFSET (buffer) = outoffset;
        GST_BUFFER_TIMESTAMP (buffer) =
            gst_util_uint64_scale_int (outoffset, GST_SECOND, dec->vi.rate);
        GST_DEBUG_OBJECT (dec, "patch buffer %" G_GUINT64_FORMAT
            " offset %" G_GUINT64_FORMAT, size, outoffset);
        size--;
      }
      for (walk = dec->queued; walk; walk = g_list_next (walk)) {
        GstBuffer *buffer = GST_BUFFER (walk->data);

        /* clips or returns FALSE with buffer unreffed when completely
         * clipped */
        if (vorbis_do_clip (dec, buffer))
          continue;

        if (dec->discont) {
          GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
          dec->discont = FALSE;
        }
        /* ignore the result */
        gst_pad_push (dec->srcpad, buffer);
      }
      g_list_free (dec->queued);
      dec->queued = NULL;
    }

    /* clip */
    if (vorbis_do_clip (dec, buf))
      return GST_FLOW_OK;

    if (dec->discont) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      dec->discont = FALSE;
    }
    result = gst_pad_push (dec->srcpad, buf);
  }

  return result;
}

static GstFlowReturn
vorbis_handle_data_packet (GstIVorbisDec * vd, ogg_packet * packet)
{
  ogg_int32_t **pcm;
  guint sample_count;
  GstBuffer *out;
  GstFlowReturn result;
  gint size;

  if (!vd->initialized)
    goto not_initialized;

  /* FIXME, we should queue undecoded packets here until we get
   * a timestamp, then we reverse timestamp the queued packets and
   * clip them, then we decode only the ones we want and don't
   * keep decoded data in memory.
   * Ideally, of course, the demuxer gives us a valid timestamp on
   * the first packet.
   */

  /* normal data packet */
  /* FIXME, we can skip decoding if the packet is outside of the
   * segment, this is however not very trivial as we need a previous
   * packet to decode the current one so we must be carefull not to
   * throw away too much. For now we decode everything and clip right
   * before pushing data. */
  if (G_UNLIKELY (vorbis_synthesis (&vd->vb, packet, 1)))
    goto could_not_read;

  if (G_UNLIKELY (vorbis_synthesis_blockin (&vd->vd, &vd->vb) < 0))
    goto not_accepted;

  /* assume all goes well here */
  result = GST_FLOW_OK;

  /* count samples ready for reading */
  if ((sample_count = vorbis_synthesis_pcmout (&vd->vd, NULL)) == 0)
    goto done;

  size = sample_count * vd->vi.channels * vd->width;

  /* alloc buffer for it */
  result =
      gst_pad_alloc_buffer_and_set_caps (vd->srcpad, GST_BUFFER_OFFSET_NONE,
      size, GST_PAD_CAPS (vd->srcpad), &out);
  if (G_UNLIKELY (result != GST_FLOW_OK))
    goto done;

  /* get samples ready for reading now, should be sample_count */
  if (G_UNLIKELY ((vorbis_synthesis_pcmout (&vd->vd, &pcm)) != sample_count))
    goto wrong_samples;

  /* copy samples in buffer */
  if (vd->width == 4) {
    copy_samples ((gint32 *) GST_BUFFER_DATA (out), pcm, sample_count,
        vd->vi.channels);
  } else if (vd->width == 2) {
    copy_samples_16 ((gint16 *) GST_BUFFER_DATA (out), pcm, sample_count,
        vd->vi.channels);
  } else {
    g_assert_not_reached ();
  }

  GST_BUFFER_SIZE (out) = size;
  GST_BUFFER_OFFSET (out) = vd->granulepos;
  if (vd->granulepos != -1) {
    GST_BUFFER_OFFSET_END (out) = vd->granulepos + sample_count;
    GST_BUFFER_TIMESTAMP (out) =
        gst_util_uint64_scale_int (vd->granulepos, GST_SECOND, vd->vi.rate);
  } else {
    GST_BUFFER_TIMESTAMP (out) = -1;
  }
  /* this should not overflow */
  GST_BUFFER_DURATION (out) = sample_count * GST_SECOND / vd->vi.rate;

  if (vd->cur_timestamp != GST_CLOCK_TIME_NONE) {
    GST_BUFFER_TIMESTAMP (out) = vd->cur_timestamp;
    GST_DEBUG_OBJECT (vd,
        "cur_timestamp: %" GST_TIME_FORMAT " + %" GST_TIME_FORMAT " = % "
        GST_TIME_FORMAT, GST_TIME_ARGS (vd->cur_timestamp),
        GST_TIME_ARGS (GST_BUFFER_DURATION (out)),
        GST_TIME_ARGS (vd->cur_timestamp + GST_BUFFER_DURATION (out)));
    vd->cur_timestamp += GST_BUFFER_DURATION (out);
    GST_BUFFER_OFFSET (out) = GST_CLOCK_TIME_TO_FRAMES (vd->cur_timestamp,
        vd->vi.rate);
    GST_BUFFER_OFFSET_END (out) = GST_BUFFER_OFFSET (out) + sample_count;
  }

  if (vd->granulepos != -1)
    vd->granulepos += sample_count;

  result = vorbis_dec_push (vd, out);

done:
  vorbis_synthesis_read (&vd->vd, sample_count);

  /* granulepos is the last sample in the packet */
  if (packet->granulepos != -1)
    vd->granulepos = packet->granulepos;

  return result;

  /* ERRORS */
not_initialized:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("no header sent yet"));
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
wrong_samples:
  {
    gst_buffer_unref (out);
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("vorbis decoder reported wrong number of samples"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstIVorbisDec *vd;
  ogg_packet packet;
  ogg_reference ref;
  ogg_buffer buf;
  GstFlowReturn result = GST_FLOW_OK;
  GstClockTime timestamp;
  guint64 offset_end;

  vd = GST_IVORBIS_DEC (gst_pad_get_parent (pad));

  /* resync on DISCONT */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (vd, "received DISCONT buffer");
    vd->granulepos = -1;
    vd->cur_timestamp = GST_CLOCK_TIME_NONE;
    vd->prev_timestamp = GST_CLOCK_TIME_NONE;
#ifdef HAVE_VORBIS_SYNTHESIS_RESTART
    vorbis_synthesis_restart (&vd->vd);
#endif
    vd->discont = TRUE;
  }

  timestamp = GST_BUFFER_TIMESTAMP (buffer);
  offset_end = GST_BUFFER_OFFSET_END (buffer);

  /* only ogg has granulepos, demuxers of other container formats 
   * might provide us with timestamps instead (e.g. matroskademux) */
  if (offset_end == GST_BUFFER_OFFSET_NONE && timestamp != GST_CLOCK_TIME_NONE) {
    /* we might get multiple consecutive buffers with the same timestamp */
    if (timestamp != vd->prev_timestamp) {
      vd->cur_timestamp = timestamp;
      vd->prev_timestamp = timestamp;
    }
  } else {
    vd->cur_timestamp = GST_CLOCK_TIME_NONE;
    vd->prev_timestamp = GST_CLOCK_TIME_NONE;
  }

  /* make ogg_packet out of the buffer */
  buf.data = GST_BUFFER_DATA (buffer);
  buf.size = GST_BUFFER_SIZE (buffer);
  buf.refcount = 1;
  buf.ptr.owner = NULL;
  buf.ptr.next = NULL;

  ref.buffer = &buf;
  ref.begin = 0;
  ref.length = buf.size;
  ref.next = NULL;

  packet.packet = &ref;
  packet.bytes = ref.length;
  packet.granulepos = offset_end;
  packet.packetno = 0;          /* we don't care */
  /*
   * FIXME. Is there anyway to know that this is the last packet and
   * set e_o_s??
   * Yes there is, keep one packet at all times and only push out when
   * you receive a new one.  Implement this.
   */
  packet.e_o_s = 0;

  if (G_UNLIKELY (packet.bytes < 1))
    goto wrong_size;

  GST_DEBUG_OBJECT (vd, "vorbis granule: %" G_GINT64_FORMAT,
      (gint64) packet.granulepos);

  /* switch depending on packet type */
  if (buf.data[0] & 1) {
    if (vd->initialized) {
      GST_WARNING_OBJECT (vd, "Already initialized, so ignoring header packet");
      goto done;
    }
    result = vorbis_handle_header_packet (vd, &packet);
  } else {
    result = vorbis_handle_data_packet (vd, &packet);
  }

  GST_DEBUG_OBJECT (vd, "offset end: %" G_GUINT64_FORMAT, offset_end);

done:
  gst_buffer_unref (buffer);
  gst_object_unref (vd);

  return result;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_ERROR (vd, STREAM, DECODE, (NULL), ("empty buffer received"));
    result = GST_FLOW_ERROR;
    vd->discont = TRUE;
    goto done;
  }
}

static GstStateChangeReturn
vorbis_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstIVorbisDec *vd = GST_IVORBIS_DEC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      vorbis_info_init (&vd->vi);
      vorbis_comment_init (&vd->vc);
      vd->initialized = FALSE;
      vd->width = 2;
      gst_ivorbis_dec_reset (vd);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  res = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_DEBUG_OBJECT (vd, "PAUSED -> READY, clearing vorbis structures");
      vorbis_block_clear (&vd->vb);
      vorbis_dsp_clear (&vd->vd);
      vorbis_comment_clear (&vd->vc);
      vorbis_info_clear (&vd->vi);
      gst_ivorbis_dec_reset (vd);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return res;
}
