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

/**
 * SECTION:element-vorbisdec
 * @see_also: vorbisenc, oggdemux
 *
 * This element decodes a Vorbis stream to raw float audio.
 * <ulink url="http://www.vorbis.com/">Vorbis</ulink> is a royalty-free
 * audio codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch -v filesrc location=sine.ogg ! oggdemux ! vorbisdec ! audioconvert ! alsasink
 * ]| Decode an Ogg/Vorbis. To create an Ogg/Vorbis file refer to the documentation of vorbisenc.
 * </refsect2>
 *
 * Last reviewed on 2006-03-01 (0.10.4)
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstvorbisdec.h"
#include <string.h>
#include <gst/audio/audio.h>
#include <gst/tag/tag.h>
#include <gst/audio/multichannel.h>

#include "gstvorbiscommon.h"

GST_DEBUG_CATEGORY_EXTERN (vorbisdec_debug);
#define GST_CAT_DEFAULT vorbisdec_debug

static GstStaticPadTemplate vorbis_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_VORBIS_DEC_SRC_CAPS);

static GstStaticPadTemplate vorbis_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-vorbis")
    );

GST_BOILERPLATE (GST_VORBIS_DEC_GLIB_TYPE_NAME, gst_vorbis_dec, GstElement,
    GST_TYPE_ELEMENT);

static void vorbis_dec_finalize (GObject * object);
static gboolean vorbis_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn vorbis_dec_chain (GstPad * pad, GstBuffer * buffer);
static GstFlowReturn vorbis_dec_chain_forward (GstVorbisDec * vd,
    gboolean discont, GstBuffer * buffer);
static GstStateChangeReturn vorbis_dec_change_state (GstElement * element,
    GstStateChange transition);

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
  GstPadTemplate *src_template, *sink_template;

  src_template = gst_static_pad_template_get (&vorbis_dec_src_factory);
  gst_element_class_add_pad_template (element_class, src_template);

  sink_template = gst_static_pad_template_get (&vorbis_dec_sink_factory);
  gst_element_class_add_pad_template (element_class, sink_template);

  gst_element_class_set_details_simple (element_class,
      "Vorbis audio decoder", "Codec/Decoder/Audio",
      GST_VORBIS_DEC_DESCRIPTION,
      "Benjamin Otte <otte@gnome.org>, Chris Lord <chris@openedhand.com>");
}

static void
gst_vorbis_dec_class_init (GstVorbisDecClass * klass)
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
gst_vorbis_dec_init (GstVorbisDec * dec, GstVorbisDecClass * g_class)
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
  GstVorbisDec *vd = GST_VORBIS_DEC (object);

  vorbis_block_clear (&vd->vb);
  vorbis_dsp_clear (&vd->vd);
  vorbis_comment_clear (&vd->vc);
  vorbis_info_clear (&vd->vi);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vorbis_dec_reset (GstVorbisDec * dec)
{
  dec->last_timestamp = GST_CLOCK_TIME_NONE;
  dec->discont = TRUE;
  dec->seqnum = gst_util_seqnum_next ();
  gst_segment_init (&dec->segment, GST_FORMAT_TIME);

  g_list_foreach (dec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->queued);
  dec->queued = NULL;
  g_list_foreach (dec->gather, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->gather);
  dec->gather = NULL;
  g_list_foreach (dec->decode, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (dec->decode);
  dec->decode = NULL;
  g_list_foreach (dec->pendingevents, (GFunc) gst_mini_object_unref, NULL);
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
  GstVorbisDec *dec;
  guint64 scale = 1;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

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
  GstVorbisDec *dec;
  gboolean res = FALSE;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 value;
      GstFormat format;
      gint64 time;

      gst_query_parse_position (query, &format, NULL);

      /* we start from the last seen time */
      time = dec->last_timestamp;
      /* correct for the segment values */
      time = gst_segment_to_stream_time (&dec->segment, GST_FORMAT_TIME, time);

      GST_LOG_OBJECT (dec,
          "query %p: our time: %" GST_TIME_FORMAT, query, GST_TIME_ARGS (time));

      /* and convert to the final format */
      if (!(res =
              vorbis_dec_convert (pad, GST_FORMAT_TIME, time, &format, &value)))
        goto error;

      gst_query_set_position (query, format, value);

      GST_LOG_OBJECT (dec,
          "query %p: we return %" G_GINT64_FORMAT " (format %u)", query, value,
          format);

      break;
    }
    case GST_QUERY_DURATION:
    {
      res = gst_pad_peer_query (dec->sinkpad, query);
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
  GstVorbisDec *dec;
  gboolean res;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

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
  GstVorbisDec *dec;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

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
      guint32 seqnum;

      gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
          &stop_type, &stop);
      seqnum = gst_event_get_seqnum (event);
      gst_event_unref (event);

      /* First bring the requested format to time */
      tformat = GST_FORMAT_TIME;
      if (!(res = vorbis_dec_convert (pad, format, cur, &tformat, &tcur)))
        goto convert_error;
      if (!(res = vorbis_dec_convert (pad, format, stop, &tformat, &tstop)))
        goto convert_error;

      /* then seek with time on the peer */
      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);
      gst_event_set_seqnum (real_seek, seqnum);

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
  GstVorbisDec *dec;

  dec = GST_VORBIS_DEC (gst_pad_get_parent (pad));

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
      gst_vorbis_dec_reset (dec);
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

      /* we need time for now */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      GST_DEBUG_OBJECT (dec,
          "newsegment: update %d, rate %g, arate %g, start %" GST_TIME_FORMAT
          ", stop %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT,
          update, rate, arate, GST_TIME_ARGS (start), GST_TIME_ARGS (stop),
          GST_TIME_ARGS (time));

      /* now configure the values */
      gst_segment_set_newsegment_full (&dec->segment, update,
          rate, arate, format, start, stop, time);
      dec->seqnum = gst_event_get_seqnum (event);

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
    case GST_EVENT_TAG:
    {
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
}

static GstFlowReturn
vorbis_handle_identification_packet (GstVorbisDec * vd)
{
  GstCaps *caps;
  const GstAudioChannelPosition *pos = NULL;
  gint width = GST_VORBIS_DEC_DEFAULT_SAMPLE_WIDTH;

  switch (vd->vi.channels) {
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
      pos = gst_vorbis_channel_positions[vd->vi.channels - 1];
      break;
    default:{
      gint i;
      GstAudioChannelPosition *posn =
          g_new (GstAudioChannelPosition, vd->vi.channels);

      GST_ELEMENT_WARNING (GST_ELEMENT (vd), STREAM, DECODE,
          (NULL), ("Using NONE channel layout for more than 8 channels"));

      for (i = 0; i < vd->vi.channels; i++)
        posn[i] = GST_AUDIO_CHANNEL_POSITION_NONE;

      pos = posn;
    }
  }

  /* negotiate width with downstream */
  caps = gst_pad_get_allowed_caps (vd->srcpad);
  if (caps) {
    if (!gst_caps_is_empty (caps)) {
      GstStructure *s;

      s = gst_caps_get_structure (caps, 0);
      /* template ensures 16 or 32 */
      gst_structure_get_int (s, "width", &width);

      GST_INFO_OBJECT (vd, "using %s with %d channels and %d bit audio depth",
          gst_structure_get_name (s), vd->vi.channels, width);
    }
    gst_caps_unref (caps);
  }
  vd->width = width >> 3;

  /* select a copy_samples function, this way we can have specialized versions
   * for mono/stereo and avoid the depth switch in tremor case */
  vd->copy_samples = get_copy_sample_func (vd->vi.channels, vd->width);

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (vd->srcpad));
  gst_caps_set_simple (caps, "rate", G_TYPE_INT, vd->vi.rate,
      "channels", G_TYPE_INT, vd->vi.channels,
      "width", G_TYPE_INT, width, NULL);

  if (pos) {
    gst_audio_set_channel_positions (gst_caps_get_structure (caps, 0), pos);
  }

  if (vd->vi.channels > 8) {
    g_free ((GstAudioChannelPosition *) pos);
  }

  gst_pad_set_caps (vd->srcpad, caps);
  gst_caps_unref (caps);

  return GST_FLOW_OK;
}

static GstFlowReturn
vorbis_handle_comment_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  guint bitrate = 0;
  gchar *encoder = NULL;
  GstTagList *list, *old_list;
  GstBuffer *buf;

  GST_DEBUG_OBJECT (vd, "parsing comment packet");

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = gst_ogg_packet_data (packet);
  GST_BUFFER_SIZE (buf) = gst_ogg_packet_size (packet);

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
    if (encoder[0])
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
vorbis_handle_type_packet (GstVorbisDec * vd)
{
  GList *walk;
  gint res;

  g_assert (vd->initialized == FALSE);

  if (G_UNLIKELY ((res = vorbis_synthesis_init (&vd->vd, &vd->vi))))
    goto synthesis_init_error;

  if (G_UNLIKELY ((res = vorbis_block_init (&vd->vd, &vd->vb))))
    goto block_init_error;

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

  /* ERRORS */
synthesis_init_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't initialize synthesis (%d)", res));
    return GST_FLOW_ERROR;
  }
block_init_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (vd), STREAM, DECODE,
        (NULL), ("couldn't initialize block (%d)", res));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_handle_header_packet (GstVorbisDec * vd, ogg_packet * packet)
{
  GstFlowReturn res;
  gint ret;

  GST_DEBUG_OBJECT (vd, "parsing header packet");

  /* Packetno = 0 if the first byte is exactly 0x01 */
  packet->b_o_s = ((gst_ogg_packet_data (packet))[0] == 0x1) ? 1 : 0;

  if ((ret = vorbis_synthesis_headerin (&vd->vi, &vd->vc, packet)))
    goto header_read_error;

  switch ((gst_ogg_packet_data (packet))[0]) {
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
        (NULL), ("couldn't read header packet (%d)", ret));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
vorbis_dec_push_forward (GstVorbisDec * dec, GstBuffer * buf)
{
  GstFlowReturn result;

  /* clip */
  if (!(buf = gst_audio_buffer_clip (buf, &dec->segment, dec->vi.rate,
              dec->vi.channels * dec->width))) {
    GST_LOG_OBJECT (dec, "clipped buffer");
    return GST_FLOW_OK;
  }

  if (dec->discont) {
    GST_LOG_OBJECT (dec, "setting DISCONT");
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    dec->discont = FALSE;
  }

  GST_DEBUG_OBJECT (dec,
      "pushing time %" GST_TIME_FORMAT ", dur %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  result = gst_pad_push (dec->srcpad, buf);

  return result;
}

static GstFlowReturn
vorbis_dec_push_reverse (GstVorbisDec * dec, GstBuffer * buf)
{
  GstFlowReturn result = GST_FLOW_OK;

  dec->queued = g_list_prepend (dec->queued, buf);

  return result;
}

static void
vorbis_do_timestamps (GstVorbisDec * vd, GstBuffer * buf, gboolean reverse,
    GstClockTime timestamp, GstClockTime duration)
{
  /* interpolate reverse */
  if (vd->last_timestamp != -1 && reverse)
    vd->last_timestamp -= duration;

  /* take buffer timestamp, use interpolated timestamp otherwise */
  if (timestamp != -1)
    vd->last_timestamp = timestamp;
  else
    timestamp = vd->last_timestamp;

  /* interpolate forwards */
  if (vd->last_timestamp != -1 && !reverse)
    vd->last_timestamp += duration;

  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;
}

static GstFlowReturn
vorbis_handle_data_packet (GstVorbisDec * vd, ogg_packet * packet,
    GstClockTime timestamp, GstClockTime duration)
{
  vorbis_sample_t **pcm;
  guint sample_count;
  GstBuffer *out;
  GstFlowReturn result;
  gint size;

  if (G_UNLIKELY (!vd->initialized))
    goto not_initialized;

  /* normal data packet */
  /* FIXME, we can skip decoding if the packet is outside of the
   * segment, this is however not very trivial as we need a previous
   * packet to decode the current one so we must be carefull not to
   * throw away too much. For now we decode everything and clip right
   * before pushing data. */
  if (G_UNLIKELY (vorbis_synthesis (&vd->vb, packet)))
    goto could_not_read;

  if (G_UNLIKELY (vorbis_synthesis_blockin (&vd->vd, &vd->vb) < 0))
    goto not_accepted;

  /* assume all goes well here */
  result = GST_FLOW_OK;

  /* count samples ready for reading */
  if ((sample_count = vorbis_synthesis_pcmout (&vd->vd, NULL)) == 0)
    goto done;

  size = sample_count * vd->vi.channels * vd->width;
  GST_LOG_OBJECT (vd, "%d samples ready for reading, size %d", sample_count,
      size);

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
  vd->copy_samples ((vorbis_sample_t *) GST_BUFFER_DATA (out), pcm,
      sample_count, vd->vi.channels, vd->width);

  GST_LOG_OBJECT (vd, "setting output size to %d", size);
  GST_BUFFER_SIZE (out) = size;

  /* this should not overflow */
  if (duration == -1)
    duration = sample_count * GST_SECOND / vd->vi.rate;

  vorbis_do_timestamps (vd, out, FALSE, timestamp, duration);

  if (vd->segment.rate >= 0.0)
    result = vorbis_dec_push_forward (vd, out);
  else
    result = vorbis_dec_push_reverse (vd, out);

done:
  vorbis_synthesis_read (&vd->vd, sample_count);

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
vorbis_dec_decode_buffer (GstVorbisDec * vd, GstBuffer * buffer)
{
  ogg_packet *packet;
  ogg_packet_wrapper packet_wrapper;
  GstFlowReturn result = GST_FLOW_OK;

  /* make ogg_packet out of the buffer */
  gst_ogg_packet_wrapper_from_buffer (&packet_wrapper, buffer);
  packet = gst_ogg_packet_from_wrapper (&packet_wrapper);
  /* set some more stuff */
  packet->granulepos = -1;
  packet->packetno = 0;         /* we don't care */
  /* EOS does not matter, it is used in vorbis to implement clipping the last
   * block of samples based on the granulepos. We clip based on segments. */
  packet->e_o_s = 0;

  GST_LOG_OBJECT (vd, "decode buffer of size %ld", packet->bytes);

  /* error out on empty header packets, but just skip empty data packets */
  if (G_UNLIKELY (packet->bytes == 0)) {
    if (vd->initialized)
      goto empty_buffer;
    else
      goto empty_header;
  }

  /* switch depending on packet type */
  if ((gst_ogg_packet_data (packet))[0] & 1) {
    if (vd->initialized) {
      GST_WARNING_OBJECT (vd, "Already initialized, so ignoring header packet");
      goto done;
    }
    result = vorbis_handle_header_packet (vd, packet);
  } else {
    GstClockTime timestamp, duration;

    timestamp = GST_BUFFER_TIMESTAMP (buffer);
    duration = GST_BUFFER_DURATION (buffer);

    result = vorbis_handle_data_packet (vd, packet, timestamp, duration);
  }

done:
  return result;

empty_buffer:
  {
    /* don't error out here, just ignore the buffer, it's invalid for vorbis
     * but not fatal. */
    GST_WARNING_OBJECT (vd, "empty buffer received, ignoring");
    result = GST_FLOW_OK;
    goto done;
  }

/* ERRORS */
empty_header:
  {
    GST_ELEMENT_ERROR (vd, STREAM, DECODE, (NULL), ("empty header received"));
    result = GST_FLOW_ERROR;
    vd->discont = TRUE;
    goto done;
  }
}

/*
 * Input:
 *  Buffer decoding order:  7  8  9  4  5  6  3  1  2  EOS
 *  Discont flag:           D        D        D  D
 *
 * - Each Discont marks a discont in the decoding order.
 *
 * for vorbis, each buffer is a keyframe when we have the previous
 * buffer. This means that to decode buffer 7, we need buffer 6, which
 * arrives out of order.
 *
 * we first gather buffers in the gather queue until we get a DISCONT. We
 * prepend each incomming buffer so that they are in reversed order.
 *
 *    gather queue:    9  8  7
 *    decode queue:
 *    output queue:
 *
 * When a DISCONT is received (buffer 4), we move the gather queue to the
 * decode queue. This is simply done be taking the head of the gather queue
 * and prepending it to the decode queue. This yields:
 *
 *    gather queue:
 *    decode queue:    7  8  9
 *    output queue:
 *
 * Then we decode each buffer in the decode queue in order and put the output
 * buffer in the output queue. The first buffer (7) will not produce any output
 * because it needs the previous buffer (6) which did not arrive yet. This
 * yields:
 *
 *    gather queue:
 *    decode queue:    7  8  9
 *    output queue:    9  8
 *
 * Then we remove the consumed buffers from the decode queue. Buffer 7 is not
 * completely consumed, we need to keep it around for when we receive buffer
 * 6. This yields:
 *
 *    gather queue:
 *    decode queue:    7
 *    output queue:    9  8
 *
 * Then we accumulate more buffers:
 *
 *    gather queue:    6  5  4
 *    decode queue:    7
 *    output queue:
 *
 * prepending to the decode queue on DISCONT yields:
 *
 *    gather queue:
 *    decode queue:    4  5  6  7
 *    output queue:
 *
 * after decoding and keeping buffer 4:
 *
 *    gather queue:
 *    decode queue:    4
 *    output queue:    7  6  5
 *
 * Etc..
 */
static GstFlowReturn
vorbis_dec_flush_decode (GstVorbisDec * dec)
{
  GstFlowReturn res = GST_FLOW_OK;
  GList *walk;

  walk = dec->decode;

  GST_DEBUG_OBJECT (dec, "flushing buffers to decoder");

  while (walk) {
    GList *next;
    GstBuffer *buf = GST_BUFFER_CAST (walk->data);

    GST_DEBUG_OBJECT (dec, "decoding buffer %p, ts %" GST_TIME_FORMAT,
        buf, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)));

    next = g_list_next (walk);

    /* decode buffer, prepend to output queue */
    res = vorbis_dec_decode_buffer (dec, buf);

    /* if we generated output, we can discard the buffer, else we
     * keep it in the queue */
    if (dec->queued) {
      GST_DEBUG_OBJECT (dec, "decoded buffer to %p", dec->queued->data);
      dec->decode = g_list_delete_link (dec->decode, walk);
      gst_buffer_unref (buf);
    } else {
      GST_DEBUG_OBJECT (dec, "buffer did not decode, keeping");
    }
    walk = next;
  }
  while (dec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (dec->queued->data);
    GstClockTime timestamp, duration;

    timestamp = GST_BUFFER_TIMESTAMP (buf);
    duration = GST_BUFFER_DURATION (buf);

    vorbis_do_timestamps (dec, buf, TRUE, timestamp, duration);
    res = vorbis_dec_push_forward (dec, buf);

    dec->queued = g_list_delete_link (dec->queued, dec->queued);
  }
  return res;
}

static GstFlowReturn
vorbis_dec_chain_reverse (GstVorbisDec * vd, gboolean discont, GstBuffer * buf)
{
  GstFlowReturn result = GST_FLOW_OK;

  /* if we have a discont, move buffers to the decode list */
  if (G_UNLIKELY (discont)) {
    GST_DEBUG_OBJECT (vd, "received discont");
    while (vd->gather) {
      GstBuffer *gbuf;

      gbuf = GST_BUFFER_CAST (vd->gather->data);
      /* remove from the gather list */
      vd->gather = g_list_delete_link (vd->gather, vd->gather);
      /* copy to decode queue */
      vd->decode = g_list_prepend (vd->decode, gbuf);
    }
    /* flush and decode the decode queue */
    result = vorbis_dec_flush_decode (vd);
  }

  GST_DEBUG_OBJECT (vd, "gathering buffer %p of size %u, time %" GST_TIME_FORMAT
      ", dur %" GST_TIME_FORMAT, buf, GST_BUFFER_SIZE (buf),
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  /* add buffer to gather queue */
  vd->gather = g_list_prepend (vd->gather, buf);

  return result;
}

static GstFlowReturn
vorbis_dec_chain_forward (GstVorbisDec * vd, gboolean discont,
    GstBuffer * buffer)
{
  GstFlowReturn result;

  result = vorbis_dec_decode_buffer (vd, buffer);

  gst_buffer_unref (buffer);

  return result;
}

static GstFlowReturn
vorbis_dec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstVorbisDec *vd;
  GstFlowReturn result = GST_FLOW_OK;
  gboolean discont;

  vd = GST_VORBIS_DEC (gst_pad_get_parent (pad));

  discont = GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_FLAG_DISCONT);

  /* resync on DISCONT */
  if (G_UNLIKELY (discont)) {
    GST_DEBUG_OBJECT (vd, "received DISCONT buffer");
    vd->last_timestamp = GST_CLOCK_TIME_NONE;
#ifdef HAVE_VORBIS_SYNTHESIS_RESTART
    vorbis_synthesis_restart (&vd->vd);
#endif
    vd->discont = TRUE;
  }

  if (vd->segment.rate >= 0.0)
    result = vorbis_dec_chain_forward (vd, discont, buffer);
  else
    result = vorbis_dec_chain_reverse (vd, discont, buffer);

  gst_object_unref (vd);

  return result;
}

static GstStateChangeReturn
vorbis_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstVorbisDec *vd = GST_VORBIS_DEC (element);
  GstStateChangeReturn res;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      vorbis_info_init (&vd->vi);
      vorbis_comment_init (&vd->vc);
      vd->initialized = FALSE;
      gst_vorbis_dec_reset (vd);
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
      vd->initialized = FALSE;
      vorbis_block_clear (&vd->vb);
      vorbis_dsp_clear (&vd->vd);
      vorbis_comment_clear (&vd->vc);
      vorbis_info_clear (&vd->vi);
      gst_vorbis_dec_reset (vd);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return res;
}
