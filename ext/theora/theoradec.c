/* GStreamer
 * Copyright (C) 2004 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *               2006 Michael Smith <msmith@fluendo.com>
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
 * SECTION:element-theoradecexp
 * @see_also: theoradec, theoraenc, oggdemux
 *
 * This element decodes theora streams into raw video using the theora-exp
 * decoder
 * <ulink url="http://www.theora.org/">Theora</ulink> is a royalty-free
 * video codec maintained by the <ulink url="http://www.xiph.org/">Xiph.org
 * Foundation</ulink>, based on the VP3 codec.
 * 
 * <refsect2>
 * <title>Example pipeline</title>
 * |[
 * gst-launch -v filesrc location=videotestsrc.ogg ! oggdemux ! theoraexpdec ! xvimagesink
 * ]| This example pipeline will demux an ogg stream and decode the theora video,
 * displaying it on screen.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "theoradec.h"
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY_STATIC (theoradecexp_debug);
#define GST_CAT_DEFAULT theoradecexp_debug

static GstStaticPadTemplate theora_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) { I420, YUY2, Y444 }, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate theora_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

GST_BOILERPLATE (GstTheoraExpDec, gst_theoradec, GstElement, GST_TYPE_ELEMENT);

static gboolean theora_dec_sink_event (GstPad * pad, GstEvent * event);
static GstFlowReturn theora_dec_chain (GstPad * pad, GstBuffer * buffer);
static GstStateChangeReturn theora_dec_change_state (GstElement * element,
    GstStateChange transition);
static gboolean theora_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean theora_dec_src_query (GstPad * pad, GstQuery * query);
static gboolean theora_dec_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static gboolean theora_dec_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static gboolean theora_dec_sink_query (GstPad * pad, GstQuery * query);

static const GstQueryType *theora_get_query_types (GstPad * pad);

static void
gst_theoradec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_dec_sink_factory));
  gst_element_class_set_details_simple (element_class, "Theora video decoder",
      "Codec/Decoder/Video",
      "decode raw theora streams to raw YUV video using libtheoradec",
      "Benjamin Otte <in7y118@public.uni-hamburg.de>, "
      "Wim Taymans <wim@fluendo.com>, " "Michael Smith <msmith@fluendo,com>");
}

static void
gst_theoradec_class_init (GstTheoraExpDecClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = theora_dec_change_state;

  GST_DEBUG_CATEGORY_INIT (theoradecexp_debug, "theoradecexp", 0,
      "Theora decoder");
}

static void
gst_theoradec_init (GstTheoraExpDec * dec, GstTheoraExpDecClass * g_class)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&theora_dec_sink_factory, "sink");
  gst_pad_set_query_function (dec->sinkpad, theora_dec_sink_query);
  gst_pad_set_event_function (dec->sinkpad, theora_dec_sink_event);
  gst_pad_set_chain_function (dec->sinkpad, theora_dec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_static_template (&theora_dec_src_factory, "src");
  gst_pad_set_event_function (dec->srcpad, theora_dec_src_event);
  gst_pad_set_query_type_function (dec->srcpad, theora_get_query_types);
  gst_pad_set_query_function (dec->srcpad, theora_dec_src_query);
  gst_pad_use_fixed_caps (dec->srcpad);

  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->queued = NULL;
}

static void
gst_theoradec_reset (GstTheoraExpDec * dec)
{
  GList *walk;

  dec->need_keyframe = TRUE;
  dec->last_timestamp = -1;
  dec->granulepos = -1;
  dec->discont = TRUE;
  dec->frame_nr = -1;
  gst_segment_init (&dec->segment, GST_FORMAT_TIME);

  GST_OBJECT_LOCK (dec);
  dec->proportion = 1.0;
  dec->earliest_time = -1;
  GST_OBJECT_UNLOCK (dec);

  for (walk = dec->queued; walk; walk = g_list_next (walk)) {
    gst_buffer_unref (GST_BUFFER_CAST (walk->data));
  }
  g_list_free (dec->queued);
  dec->queued = NULL;
}

static gint64
inc_granulepos (GstTheoraExpDec * dec, gint64 granulepos)
{
  gint framecount;

  if (granulepos == -1)
    return -1;

  framecount = th_granule_frame (dec->dec, granulepos);

  return (framecount + 1) << dec->info.keyframe_granule_shift;
}

static const GstQueryType *
theora_get_query_types (GstPad * pad)
{
  static const GstQueryType theora_src_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    GST_QUERY_CONVERT,
    0
  };

  return theora_src_query_types;
}

static GstClockTime
gst_theoradec_granule_clocktime (GstTheoraExpDec * dec, ogg_int64_t granulepos)
{
  /* Avoid using theora_granule_time, which returns a double (in seconds); not
   * what we want
   */
  if (granulepos >= 0) {
    guint64 framecount = th_granule_frame (dec->dec, granulepos);

    return gst_util_uint64_scale_int (framecount * GST_SECOND,
        dec->info.fps_denominator, dec->info.fps_numerator);
  }
  return -1;
}

static gboolean
theora_dec_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstTheoraExpDec *dec;
  guint64 scale = 1;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  dec = GST_THEORA_DEC_EXP (gst_pad_get_parent (pad));

  /* we need the info part before we can done something */
  if (!dec->have_header)
    goto no_header;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value, 8,
              dec->info.pic_height * dec->info.pic_width * dec->output_bpp);
          break;
        case GST_FORMAT_TIME:
          /* seems like a rather silly conversion, implement me if you like */
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = dec->output_bpp *
              (dec->info.pic_width * dec->info.pic_height) / 8;
        case GST_FORMAT_DEFAULT:
          if (dec->info.fps_numerator && dec->info.fps_denominator)
            *dest_value = scale * gst_util_uint64_scale (src_value,
                dec->info.fps_numerator,
                dec->info.fps_denominator * GST_SECOND);
          else
            res = FALSE;
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          if (dec->info.fps_numerator && dec->info.fps_denominator)
            *dest_value = gst_util_uint64_scale (src_value,
                GST_SECOND * dec->info.fps_denominator,
                dec->info.fps_numerator);
          else
            res = FALSE;
          break;
        case GST_FORMAT_BYTES:
          *dest_value = gst_util_uint64_scale_int (src_value,
              dec->output_bpp * dec->info.pic_width * dec->info.pic_height, 8);
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
    GST_DEBUG_OBJECT (dec, "no header yet, cannot convert");
    res = FALSE;
    goto done;
  }
}

static gboolean
theora_dec_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstTheoraExpDec *dec;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  dec = GST_THEORA_DEC_EXP (gst_pad_get_parent (pad));

  /* we need the info part before we can done something */
  if (!dec->have_header)
    goto no_header;

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_theoradec_granule_clocktime (dec, src_value);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
        {
          guint rest;

          if (!dec->info.fps_numerator || !dec->info.fps_denominator) {
            res = FALSE;
            break;
          }

          /* framecount */
          *dest_value = gst_util_uint64_scale (src_value,
              dec->info.fps_numerator, GST_SECOND * dec->info.fps_denominator);

          /* funny way of calculating granulepos in theora */
          rest = *dest_value / (1 << dec->info.keyframe_granule_shift);
          *dest_value -= rest;
          *dest_value <<= dec->info.keyframe_granule_shift;
          *dest_value += rest;
          break;
        }
        default:
          res = FALSE;
          break;
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
    GST_DEBUG_OBJECT (dec, "no header yet, cannot convert");
    res = FALSE;
    goto done;
  }
}

static gboolean
theora_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstTheoraExpDec *dec;

  gboolean res = FALSE;

  dec = GST_THEORA_DEC_EXP (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      gint64 granulepos, value;
      GstFormat my_format, format;
      gint64 time;

      /* we can convert a granule position to everything */
      granulepos = dec->granulepos;

      GST_LOG_OBJECT (dec,
          "query %p: we have current granule: %lld", query, granulepos);

      /* parse format */
      gst_query_parse_position (query, &format, NULL);

      /* and convert to the final format in two steps with time as the 
       * intermediate step */
      my_format = GST_FORMAT_TIME;
      if (!(res =
              theora_dec_sink_convert (dec->sinkpad, GST_FORMAT_DEFAULT,
                  granulepos, &my_format, &time)))
        goto error;

      time = gst_segment_to_stream_time (&dec->segment, GST_FORMAT_TIME, time);

      GST_LOG_OBJECT (dec,
          "query %p: our time: %" GST_TIME_FORMAT, query, GST_TIME_ARGS (time));

      if (!(res =
              theora_dec_src_convert (pad, my_format, time, &format, &value)))
        goto error;

      gst_query_set_position (query, format, value);

      GST_LOG_OBJECT (dec,
          "query %p: we return %lld (format %u)", query, value, format);

      break;
    }
    case GST_QUERY_DURATION:
    {
      /* forward to peer for total */
      GstPad *peer;

      if (!(peer = gst_pad_get_peer (dec->sinkpad)))
        goto error;

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
              theora_dec_src_convert (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
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
    GST_DEBUG_OBJECT (dec, "query failed");
    goto done;
  }
}

static gboolean
theora_dec_sink_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      if (!(res =
              theora_dec_sink_convert (pad, src_fmt, src_val, &dest_fmt,
                  &dest_val)))
        goto error;

      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

error:
  return res;
}

static gboolean
theora_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstTheoraExpDec *dec;

  dec = GST_THEORA_DEC_EXP (gst_pad_get_parent (pad));

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
      if (!(res = theora_dec_src_convert (pad, format, cur, &tformat, &tcur)))
        goto convert_error;
      if (!(res = theora_dec_src_convert (pad, format, stop, &tformat, &tstop)))
        goto convert_error;

      /* then seek with time on the peer */
      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      res = gst_pad_push_event (dec->sinkpad, real_seek);

      gst_event_unref (event);
      break;
    }
    case GST_EVENT_QOS:
    {
      gdouble proportion;
      GstClockTimeDiff diff;
      GstClockTime timestamp;

      gst_event_parse_qos (event, &proportion, &diff, &timestamp);

      /* we cannot randomly skip frame decoding since we don't have
       * B frames. we can however use the timestamp and diff to not
       * push late frames. This would let us save the time for copying and
       * cropping the frame. */
      GST_OBJECT_LOCK (dec);
      dec->proportion = proportion;
      dec->earliest_time = timestamp + diff;
      GST_OBJECT_UNLOCK (dec);

      res = gst_pad_event_default (pad, event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }
done:
  gst_object_unref (dec);

  return res;

  /* ERRORS */
convert_error:
  {
    GST_DEBUG_OBJECT (dec, "could not convert format");
    goto done;
  }
}

static gboolean
theora_dec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstTheoraExpDec *dec;

  dec = GST_THEORA_DEC_EXP (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      /* TODO: Call appropriate func with OC_DECCTL_SET_GRANPOS? */
      gst_theoradec_reset (dec);
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_EOS:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      gboolean update;
      GstFormat format;
      gdouble rate, arate;
      gint64 start, stop, time;

      gst_event_parse_new_segment_full (event, &update, &rate, &arate, &format,
          &start, &stop, &time);

      /* we need TIME and a positive rate */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      /* now configure the values */
      gst_segment_set_newsegment_full (&dec->segment, update,
          rate, arate, format, start, stop, time);

      /* and forward */
      ret = gst_pad_push_event (dec->srcpad, event);
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
    gst_event_unref (event);
    goto done;
  }
newseg_wrong_rate:
  {
    GST_DEBUG_OBJECT (dec, "negative rates not supported yet");
    gst_event_unref (event);
    goto done;
  }
}

static GstFlowReturn
theora_handle_comment_packet (GstTheoraExpDec * dec, ogg_packet * packet)
{
  gchar *encoder = NULL;
  GstBuffer *buf;
  GstTagList *list;

  GST_DEBUG_OBJECT (dec, "parsing comment packet");

  buf = gst_buffer_new_and_alloc (packet->bytes);
  memcpy (GST_BUFFER_DATA (buf), packet->packet, packet->bytes);

  list =
      gst_tag_list_from_vorbiscomment_buffer (buf, (guint8 *) "\201theora", 7,
      &encoder);

  gst_buffer_unref (buf);

  if (!list) {
    GST_ERROR_OBJECT (dec, "couldn't decode comments");
    list = gst_tag_list_new ();
  }
  if (encoder) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER, encoder, NULL);
    g_free (encoder);
  }
  gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
      GST_TAG_ENCODER_VERSION, dec->info.version_major,
      GST_TAG_VIDEO_CODEC, "Theora", NULL);

  if (dec->info.target_bitrate > 0) {
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_BITRATE, dec->info.target_bitrate,
        GST_TAG_NOMINAL_BITRATE, dec->info.target_bitrate, NULL);
  }

  gst_element_found_tags_for_pad (GST_ELEMENT_CAST (dec), dec->srcpad, list);

  return GST_FLOW_OK;
}

static GstFlowReturn
theora_handle_type_packet (GstTheoraExpDec * dec, ogg_packet * packet)
{
  GstCaps *caps;
  gint par_num, par_den;
  guint32 fourcc;

  GST_DEBUG_OBJECT (dec, "fps %d/%d, PAR %d/%d",
      dec->info.fps_numerator, dec->info.fps_denominator,
      dec->info.aspect_numerator, dec->info.aspect_denominator);

  /* calculate par
   * the info.aspect_* values reflect PAR;
   * 0 for either is undefined; we're told to assume 1:1 */
  par_num = dec->info.aspect_numerator;
  par_den = dec->info.aspect_denominator;
  if (par_num == 0 || par_den == 0) {
    par_num = par_den = 1;
  }
  /* theora has:
   *
   *  frame_width/frame_height : dimension of the encoded frame 
   *  pic_width/pic_height : dimension of the visible part
   *  pic_x/pic_y : offset in encoded frame where visible part starts
   */
  GST_DEBUG_OBJECT (dec, "dimension %dx%d, PAR %d/%d", dec->info.frame_width,
      dec->info.frame_height, par_num, par_den);
  GST_DEBUG_OBJECT (dec, "pic dimension %dx%d, offset %d:%d",
      dec->info.pic_width, dec->info.pic_height,
      dec->info.pic_x, dec->info.pic_y);

  /* add black borders to make width/height/offsets even. we need this because
   * we cannot express an offset to the peer plugin. */
  dec->width = GST_ROUND_UP_2 (dec->info.pic_width + (dec->info.pic_x & 1));
  dec->height = GST_ROUND_UP_2 (dec->info.pic_height + (dec->info.pic_y & 1));
  dec->offset_x = dec->info.pic_x & ~1;
  dec->offset_y = dec->info.pic_y & ~1;

  GST_DEBUG_OBJECT (dec, "after fixup frame dimension %dx%d, offset %d:%d",
      dec->width, dec->height, dec->offset_x, dec->offset_y);

  if (dec->info.pixel_fmt == TH_PF_420) {
    dec->output_bpp = 12;       /* Average bits per pixel. */

    /* This is bad. I420 is very specific, and has implicit stride. This means
     * we can't use the native output of the decoder, we have to memcpy it to
     * an I420 buffer. For now, GStreamer gives us no better alternative.
     */
    fourcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
  } else if (dec->info.pixel_fmt == TH_PF_422) {
    dec->output_bpp = 16;
    /* Unfortunately, we don't have a planar 'fourcc' value, which means we
     * can't represent the output format of the decoder at all in gstreamer.
     * So, we convert to a widely-supported packed format.
     */
    fourcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
  } else if (dec->info.pixel_fmt == TH_PF_444) {
    dec->output_bpp = 24;
    /* As for I420, we can't define the stride for this, so we need to memcpy,
     * though at least this is a planar format...
     */
    fourcc = GST_MAKE_FOURCC ('Y', '4', '4', '4');
  } else {
    GST_ERROR_OBJECT (dec, "Invalid pixel format %d", dec->info.pixel_fmt);
    return GST_FLOW_ERROR;
  }

  dec->dec = th_decode_alloc (&dec->info, dec->setup);

  th_setup_free (dec->setup);
  dec->setup = NULL;

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, fourcc,
      "framerate", GST_TYPE_FRACTION,
      dec->info.fps_numerator, dec->info.fps_denominator,
      "pixel-aspect-ratio", GST_TYPE_FRACTION, par_num, par_den,
      "width", G_TYPE_INT, dec->width, "height", G_TYPE_INT, dec->height, NULL);
  gst_pad_set_caps (dec->srcpad, caps);
  gst_caps_unref (caps);

  dec->have_header = TRUE;

  return GST_FLOW_OK;
}

static GstFlowReturn
theora_handle_header_packet (GstTheoraExpDec * dec, ogg_packet * packet)
{
  GstFlowReturn res;
  int ret;

  GST_DEBUG_OBJECT (dec, "parsing header packet");

  ret = th_decode_headerin (&dec->info, &dec->comment, &dec->setup, packet);
  if (ret < 0)
    goto header_read_error;

  /* We can never get here unless we have at least a one-byte packet */
  switch (packet->packet[0]) {
    case 0x81:
      res = theora_handle_comment_packet (dec, packet);
      break;
    case 0x82:
      res = theora_handle_type_packet (dec, packet);
      break;
    default:
      /* ignore */
      g_warning ("unknown theora header packet found");
    case 0x80:
      /* nothing special, this is the identification header */
      res = GST_FLOW_OK;
      break;
  }
  return res;

  /* ERRORS */
header_read_error:
  {
    GST_WARNING_OBJECT (dec, "Header parsing failed: %d", ret);
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't read header packet"));
    return GST_FLOW_ERROR;
  }
}

/* returns TRUE if buffer is within segment, else FALSE.
 * if buffer is on segment border, its timestamp and duration will be clipped */
static gboolean
clip_buffer (GstTheoraExpDec * dec, GstBuffer * buf)
{
  gboolean res = TRUE;
  GstClockTime in_ts, in_dur, stop;
  gint64 cstart, cstop;

  in_ts = GST_BUFFER_TIMESTAMP (buf);
  in_dur = GST_BUFFER_DURATION (buf);

  GST_LOG_OBJECT (dec,
      "timestamp:%" GST_TIME_FORMAT " , duration:%" GST_TIME_FORMAT,
      GST_TIME_ARGS (in_ts), GST_TIME_ARGS (in_dur));

  /* can't clip without TIME segment */
  if (dec->segment.format != GST_FORMAT_TIME)
    goto beach;

  /* we need a start time */
  if (!GST_CLOCK_TIME_IS_VALID (in_ts))
    goto beach;

  /* generate valid stop, if duration unknown, we have unknown stop */
  stop =
      GST_CLOCK_TIME_IS_VALID (in_dur) ? (in_ts + in_dur) : GST_CLOCK_TIME_NONE;

  /* now clip */
  if (!(res = gst_segment_clip (&dec->segment, GST_FORMAT_TIME,
              in_ts, stop, &cstart, &cstop)))
    goto beach;

  /* update timestamp and possibly duration if the clipped stop time is valid */
  GST_BUFFER_TIMESTAMP (buf) = cstart;
  if (GST_CLOCK_TIME_IS_VALID (cstop))
    GST_BUFFER_DURATION (buf) = cstop - cstart;

beach:
  GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
  return res;
}


/* FIXME, this needs to be moved to the demuxer */
static GstFlowReturn
theora_dec_push (GstTheoraExpDec * dec, GstBuffer * buf)
{
  GstFlowReturn result = GST_FLOW_OK;
  GstClockTime outtime = GST_BUFFER_TIMESTAMP (buf);

  if (outtime == GST_CLOCK_TIME_NONE) {
    dec->queued = g_list_append (dec->queued, buf);
    GST_DEBUG_OBJECT (dec, "queued buffer");
    result = GST_FLOW_OK;
  } else {
    if (dec->queued) {
      gint64 size;
      GList *walk;

      GST_DEBUG_OBJECT (dec, "first buffer with time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (outtime));

      size = g_list_length (dec->queued);
      for (walk = dec->queued; walk; walk = g_list_next (walk)) {
        GstBuffer *buffer = GST_BUFFER (walk->data);
        GstClockTime time;

        time = outtime - gst_util_uint64_scale_int (size * GST_SECOND,
            dec->info.fps_denominator, dec->info.fps_numerator);

        GST_DEBUG_OBJECT (dec, "patch buffer %lld %lld", size, time);
        GST_BUFFER_TIMESTAMP (buffer) = time;

        if (dec->discont) {
          GST_BUFFER_FLAG_SET (buffer, GST_BUFFER_FLAG_DISCONT);
          dec->discont = FALSE;
        }

        /* ignore the result.. */
        if (clip_buffer (dec, buffer))
          gst_pad_push (dec->srcpad, buffer);
        else
          gst_buffer_unref (buffer);
        size--;
      }
      g_list_free (dec->queued);
      dec->queued = NULL;
    }

    if (dec->discont) {
      GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
      dec->discont = FALSE;
    }

    if (clip_buffer (dec, buf))
      result = gst_pad_push (dec->srcpad, buf);
    else
      gst_buffer_unref (buf);
  }

  return result;
}

/* Create a packed 'YUY2' image, push it.
 */
static GstFlowReturn
theora_handle_422_image (GstTheoraExpDec * dec, th_ycbcr_buffer yuv,
    GstClockTime outtime)
{
  int i, j;
  gint width, height;
  gint out_size;
  gint stride;
  GstBuffer *out;
  GstFlowReturn result;

  width = dec->width;
  height = dec->height;

  stride = GST_ROUND_UP_2 (width) * 2;

  out_size = stride * height;

  /* now copy over the area contained in offset_x,offset_y,
   * frame_width, frame_height */
  result =
      gst_pad_alloc_buffer_and_set_caps (dec->srcpad, GST_BUFFER_OFFSET_NONE,
      out_size, GST_PAD_CAPS (dec->srcpad), &out);
  if (result != GST_FLOW_OK)
    goto no_buffer;

  /* The output pixels look like:
   *   YUYVYUYV....
   *
   * Do the interleaving... Note that this is kinda messed up if our width is
   * odd. In that case, we can't represent it properly in YUY2, so we just
   * pad out to even in that case (this is why we have GST_ROUND_UP_2() above).
   */
  {
    guchar *src_y;
    guchar *src_cb;
    guchar *src_cr;
    guchar *dest;
    guchar *curdest;
    guchar *src;

    dest = GST_BUFFER_DATA (out);

    src_y = yuv[0].data + dec->offset_x + dec->offset_y * yuv[0].ystride;
    src_cb = yuv[1].data + dec->offset_x / 2 + dec->offset_y * yuv[1].ystride;
    src_cr = yuv[2].data + dec->offset_x / 2 + dec->offset_y * yuv[2].ystride;

    for (i = 0; i < height; i++) {
      /* Y first */
      curdest = dest;
      src = src_y;
      for (j = 0; j < width; j++) {
        *curdest = *src++;
        curdest += 2;
      }
      src_y += yuv[0].ystride;

      curdest = dest + 1;
      src = src_cb;
      for (j = 0; j < width; j++) {
        *curdest = *src++;
        curdest += 4;
      }
      src_cb += yuv[1].ystride;

      curdest = dest + 3;
      src = src_cr;
      for (j = 0; j < width; j++) {
        *curdest = *src++;
        curdest += 4;
      }
      src_cr += yuv[2].ystride;

      dest += stride;
    }
  }

  GST_BUFFER_OFFSET (out) = dec->frame_nr;
  if (dec->frame_nr != -1)
    dec->frame_nr++;
  GST_BUFFER_OFFSET_END (out) = dec->frame_nr;
  GST_BUFFER_DURATION (out) =
      gst_util_uint64_scale_int (GST_SECOND, dec->info.fps_denominator,
      dec->info.fps_numerator);
  GST_BUFFER_TIMESTAMP (out) = outtime;

  return theora_dec_push (dec, out);

no_buffer:
  {
    GST_DEBUG_OBJECT (dec, "could not get buffer, reason: %s",
        gst_flow_get_name (result));
    return result;
  }
}

/* Get buffer, populate with original data (we must memcpy to get things to
 * have the expected strides, etc...), and push.
 */
static GstFlowReturn
theora_handle_444_image (GstTheoraExpDec * dec, th_ycbcr_buffer yuv,
    GstClockTime outtime)
{
  int i, plane;
  gint width, height;
  gint out_size;
  gint stride;
  GstBuffer *out;
  GstFlowReturn result;

  width = dec->width;
  height = dec->height;

  /* TODO: Check if we have any special alignment requirements for the planes,
   * or for each line within a plane. */
  stride = width;
  out_size = stride * height * 3;

  /* now copy over the area contained in offset_x,offset_y,
   * frame_width, frame_height */
  result =
      gst_pad_alloc_buffer_and_set_caps (dec->srcpad, GST_BUFFER_OFFSET_NONE,
      out_size, GST_PAD_CAPS (dec->srcpad), &out);
  if (result != GST_FLOW_OK)
    goto no_buffer;

  {
    guchar *dest, *src;

    for (plane = 0; plane < 3; plane++) {
      /* TODO: do we have to use something different here? */
      dest = GST_BUFFER_DATA (out) + plane * stride * height;

      src = yuv[plane].data + dec->offset_x +
          dec->offset_y * yuv[plane].ystride;

      for (i = 0; i < height; i++) {
        memcpy (dest, src, width);

        dest += stride;
        src += yuv[plane].ystride;
      }

    }
  }

  /* FIXME, frame_nr not correct */
  GST_BUFFER_OFFSET (out) = dec->frame_nr;
  dec->frame_nr++;
  GST_BUFFER_OFFSET_END (out) = dec->frame_nr;
  GST_BUFFER_DURATION (out) =
      gst_util_uint64_scale_int (GST_SECOND, dec->info.fps_denominator,
      dec->info.fps_numerator);
  GST_BUFFER_TIMESTAMP (out) = outtime;

  return theora_dec_push (dec, out);

no_buffer:
  {
    GST_DEBUG_OBJECT (dec, "could not get buffer, reason: %s",
        gst_flow_get_name (result));
    return result;
  }
}

/* Create a (planar, but with special alignment and stride requirements) 'I420'
 * buffer, populate, push.
 */
static GstFlowReturn
theora_handle_420_image (GstTheoraExpDec * dec, th_ycbcr_buffer yuv,
    GstClockTime outtime)
{
  int i;
  gint width, height, cwidth, cheight;
  gint out_size;
  gint stride_y, stride_uv;
  GstBuffer *out;
  GstFlowReturn result;

  width = dec->width;
  height = dec->height;
  cwidth = width / 2;
  cheight = height / 2;

  /* should get the stride from the caps, for now we round up to the nearest
   * multiple of 4 because some element needs it. chroma needs special 
   * treatment, see videotestsrc. */
  stride_y = GST_ROUND_UP_4 (width);
  stride_uv = GST_ROUND_UP_8 (width) / 2;

  out_size = stride_y * height + stride_uv * cheight * 2;

  /* now copy over the area contained in offset_x,offset_y,
   * frame_width, frame_height */
  result =
      gst_pad_alloc_buffer_and_set_caps (dec->srcpad, GST_BUFFER_OFFSET_NONE,
      out_size, GST_PAD_CAPS (dec->srcpad), &out);
  if (result != GST_FLOW_OK)
    goto no_buffer;

  /* copy the visible region to the destination. This is actually pretty
   * complicated and gstreamer doesn't support all the needed caps to do this
   * correctly. For example, when we have an odd offset, we should only combine
   * 1 row/column of luma samples with one chroma sample in colorspace conversion. 
   * We compensate for this by adding a black border around the image when the
   * offset or size is odd (see above).
   */
  {
    guchar *dest_y, *src_y;
    guchar *dest_u, *src_u;
    guchar *dest_v, *src_v;
    gint offset_u, offset_v;

    dest_y = GST_BUFFER_DATA (out);
    dest_u = dest_y + stride_y * height;
    dest_v = dest_u + stride_uv * cheight;

    src_y = yuv[0].data + dec->offset_x + dec->offset_y * yuv[0].ystride;

    for (i = 0; i < height; i++) {
      memcpy (dest_y, src_y, width);

      dest_y += stride_y;
      src_y += yuv[0].ystride;
    }

    offset_u = dec->offset_x / 2 + dec->offset_y / 2 * yuv[1].ystride;
    offset_v = dec->offset_x / 2 + dec->offset_y / 2 * yuv[2].ystride;

    src_u = yuv[1].data + offset_u;
    src_v = yuv[2].data + offset_v;

    for (i = 0; i < cheight; i++) {
      memcpy (dest_u, src_u, cwidth);
      memcpy (dest_v, src_v, cwidth);

      dest_u += stride_uv;
      src_u += yuv[1].ystride;
      dest_v += stride_uv;
      src_v += yuv[2].ystride;
    }
  }

  /* FIXME, frame_nr not correct */
  GST_BUFFER_OFFSET (out) = dec->frame_nr;
  dec->frame_nr++;
  GST_BUFFER_OFFSET_END (out) = dec->frame_nr;
  GST_BUFFER_DURATION (out) =
      gst_util_uint64_scale_int (GST_SECOND, dec->info.fps_denominator,
      dec->info.fps_numerator);
  GST_BUFFER_TIMESTAMP (out) = outtime;

  return theora_dec_push (dec, out);

no_buffer:
  {
    GST_DEBUG_OBJECT (dec, "could not get buffer, reason: %s",
        gst_flow_get_name (result));
    return result;
  }
}

static GstFlowReturn
theora_handle_data_packet (GstTheoraExpDec * dec, ogg_packet * packet,
    GstClockTime outtime)
{
  /* normal data packet */
  th_ycbcr_buffer yuv;
  GstFlowReturn result;
  ogg_int64_t gp;

  if (G_UNLIKELY (!dec->have_header))
    goto not_initialized;

  if (th_packet_iskeyframe (packet)) {
    dec->need_keyframe = FALSE;
  } else if (G_UNLIKELY (dec->need_keyframe)) {
    goto dropping;
  }

  /* this does the decoding */
  if (G_UNLIKELY (th_decode_packetin (dec->dec, packet, &gp)))
    goto decode_error;

  if (outtime != -1) {
    gboolean need_skip;
    GstClockTime qostime;

    /* QoS is done on running time */
    qostime = gst_segment_to_running_time (&dec->segment, GST_FORMAT_TIME,
        outtime);

    GST_OBJECT_LOCK (dec);
    /* check for QoS, don't perform the last steps of getting and
     * pushing the buffers that are known to be late. */
    /* FIXME, we can also entirely skip decoding if the next valid buffer is 
     * known to be after a keyframe (using the granule_shift) */
    need_skip = dec->earliest_time != -1 && qostime <= dec->earliest_time;
    GST_OBJECT_UNLOCK (dec);

    if (need_skip)
      goto dropping_qos;
  }

  /* this does postprocessing and set up the decoded frame
   * pointers in our yuv variable */
  if (G_UNLIKELY (th_decode_ycbcr_out (dec->dec, yuv) < 0))
    goto no_yuv;

  if (G_UNLIKELY ((yuv[0].width != dec->info.frame_width) ||
          (yuv[0].height != dec->info.frame_height)))
    goto wrong_dimensions;

  if (dec->info.pixel_fmt == TH_PF_420) {
    result = theora_handle_420_image (dec, yuv, outtime);
  } else if (dec->info.pixel_fmt == TH_PF_422) {
    result = theora_handle_422_image (dec, yuv, outtime);
  } else if (dec->info.pixel_fmt == TH_PF_444) {
    result = theora_handle_444_image (dec, yuv, outtime);
  } else {
    g_assert_not_reached ();
  }

  return result;

  /* ERRORS */
not_initialized:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("no header sent yet"));
    return GST_FLOW_ERROR;
  }
dropping:
  {
    GST_WARNING_OBJECT (dec, "dropping frame because we need a keyframe");
    dec->discont = TRUE;
    return GST_FLOW_OK;
  }
dropping_qos:
  {
    if (dec->frame_nr != -1)
      dec->frame_nr++;
    dec->discont = TRUE;
    GST_WARNING_OBJECT (dec, "dropping frame because of QoS");
    return GST_FLOW_OK;
  }
decode_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("theora decoder did not decode data packet"));
    return GST_FLOW_ERROR;
  }
no_yuv:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't read out YUV image"));
    return GST_FLOW_ERROR;
  }
wrong_dimensions:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, FORMAT,
        (NULL), ("dimensions of image do not match header"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
theora_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstTheoraExpDec *dec;
  ogg_packet packet;
  GstFlowReturn result = GST_FLOW_OK;
  gboolean isheader;

  dec = GST_THEORA_DEC_EXP (gst_pad_get_parent (pad));

  /* resync on DISCONT */
  if (G_UNLIKELY (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DISCONT))) {
    GST_DEBUG_OBJECT (dec, "Received DISCONT buffer");
    dec->need_keyframe = TRUE;
    dec->last_timestamp = -1;
    dec->granulepos = -1;
    dec->discont = TRUE;
  }

  GST_DEBUG ("Offset end is %d, k-g-s %d", (int) (GST_BUFFER_OFFSET_END (buf)),
      dec->info.keyframe_granule_shift);
  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buf);
  packet.bytes = GST_BUFFER_SIZE (buf);
  packet.granulepos = GST_BUFFER_OFFSET_END (buf);
  packet.packetno = 0;          /* we don't really care */
  packet.b_o_s = dec->have_header ? 0 : 1;
  /* EOS does not matter for the decoder */
  packet.e_o_s = 0;

  if (dec->have_header) {
    if (packet.granulepos != -1) {
      GST_DEBUG_OBJECT (dec, "Granulepos from packet: %lld", packet.granulepos);
      dec->granulepos = packet.granulepos;
      dec->last_timestamp =
          gst_theoradec_granule_clocktime (dec, packet.granulepos);
    } else if (dec->last_timestamp != -1) {
      GST_DEBUG_OBJECT (dec, "Granulepos inferred?: %lld", dec->granulepos);
      dec->last_timestamp =
          gst_theoradec_granule_clocktime (dec, dec->granulepos);
    } else {
      GST_DEBUG_OBJECT (dec, "Granulepos unknown");
      dec->last_timestamp = GST_CLOCK_TIME_NONE;
    }
    if (dec->last_timestamp == GST_CLOCK_TIME_NONE &&
        GST_BUFFER_TIMESTAMP_IS_VALID (buf))
      dec->last_timestamp = GST_BUFFER_TIMESTAMP (buf);

  } else {
    GST_DEBUG_OBJECT (dec, "Granulepos not usable: no headers seen");
    dec->last_timestamp = -1;
  }

  /* A zero-byte packet is a valid data packet, meaning 'duplicate frame' */
  if (packet.bytes > 0 && packet.packet[0] & 0x80)
    isheader = TRUE;
  else
    isheader = FALSE;

  GST_DEBUG_OBJECT (dec, "header=%d packetno=%lld, outtime=%" GST_TIME_FORMAT,
      packet.bytes ? packet.packet[0] : -1, packet.packetno,
      GST_TIME_ARGS (dec->last_timestamp));

  /* switch depending on packet type */
  if (isheader) {
    if (dec->have_header) {
      GST_WARNING_OBJECT (GST_OBJECT (dec), "Ignoring header");
      goto done;
    }
    result = theora_handle_header_packet (dec, &packet);
  } else {
    result = theora_handle_data_packet (dec, &packet, dec->last_timestamp);
  }

done:
  /* interpolate granule pos */
  dec->granulepos = inc_granulepos (dec, dec->granulepos);

  gst_object_unref (dec);

  gst_buffer_unref (buf);

  return result;
}

static GstStateChangeReturn
theora_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstTheoraExpDec *dec = GST_THEORA_DEC_EXP (element);
  GstStateChangeReturn ret;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      th_info_init (&dec->info);
      th_comment_init (&dec->comment);
      dec->have_header = FALSE;
      gst_theoradec_reset (dec);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = parent_class->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      th_decode_free (dec->dec);
      dec->dec = NULL;

      th_comment_clear (&dec->comment);
      th_info_clear (&dec->info);
      gst_theoradec_reset (dec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "theoradecexp", GST_RANK_PRIMARY,
          gst_theoradec_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "theoradec",
    "Theora dec (exp) plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
