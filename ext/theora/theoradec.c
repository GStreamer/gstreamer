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

#include <gst/gst.h>
#include <theora/theora.h>
#include <string.h>
#include <gst/tag/tag.h>

GST_DEBUG_CATEGORY (theoradec_debug);
#define GST_CAT_DEFAULT theoradec_debug

#define GST_TYPE_THEORA_DEC \
  (gst_theora_dec_get_type())
#define GST_THEORA_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_THEORA_DEC,GstTheoraDec))
#define GST_THEORA_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_THEORA_DEC,GstTheoraDec))
#define GST_IS_THEORA_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_THEORA_DEC))
#define GST_IS_THEORA_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_THEORA_DEC))

typedef struct _GstTheoraDec GstTheoraDec;
typedef struct _GstTheoraDecClass GstTheoraDecClass;

struct _GstTheoraDec
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  theora_state state;
  theora_info info;
  theora_comment comment;

  gboolean have_header;
  guint64 granulepos;
  guint64 granule_shift;

  GstClockTime last_timestamp;
  guint64 frame_nr;
  gboolean need_keyframe;
  gint width, height;
  gint offset_x, offset_y;

  gboolean crop;

  GList *queued;

  gdouble segment_rate;
  gint64 segment_start;
  gint64 segment_stop;
  gint64 segment_time;
};

struct _GstTheoraDecClass
{
  GstElementClass parent_class;
};

#define THEORA_DEF_CROP         TRUE
enum
{
  ARG_0,
  ARG_CROP
};

static GstElementDetails theora_dec_details = {
  "TheoraDec",
  "Codec/Decoder/Video",
  "decode raw theora streams to raw YUV video",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>, "
      "Wim Taymans <wim@fluendo.com>",
};

static GstStaticPadTemplate theora_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate theora_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

GST_BOILERPLATE (GstTheoraDec, gst_theora_dec, GstElement, GST_TYPE_ELEMENT);

static void theora_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void theora_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

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
static GstCaps *theora_dec_src_getcaps (GstPad * pad);

#if 0
static const GstFormat *theora_get_formats (GstPad * pad);
#endif
#if 0
static const GstEventMask *theora_get_event_masks (GstPad * pad);
#endif
static const GstQueryType *theora_get_query_types (GstPad * pad);


static void
gst_theora_dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_dec_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&theora_dec_sink_factory));
  gst_element_class_set_details (element_class, &theora_dec_details);
}

static void
gst_theora_dec_class_init (GstTheoraDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = theora_dec_set_property;
  gobject_class->get_property = theora_dec_get_property;

  g_object_class_install_property (gobject_class, ARG_CROP,
      g_param_spec_boolean ("crop", "Crop",
          "Crop the image to the visible region", THEORA_DEF_CROP,
          (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->change_state = theora_dec_change_state;

  GST_DEBUG_CATEGORY_INIT (theoradec_debug, "theoradec", 0, "Theora decoder");
}

static void
gst_theora_dec_init (GstTheoraDec * dec, GstTheoraDecClass * g_class)
{
  dec->sinkpad =
      gst_pad_new_from_static_template (&theora_dec_sink_factory, "sink");
  gst_pad_set_query_function (dec->sinkpad, theora_dec_sink_query);
  gst_pad_set_event_function (dec->sinkpad, theora_dec_sink_event);
  gst_pad_set_chain_function (dec->sinkpad, theora_dec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_static_template (&theora_dec_src_factory, "src");
  gst_pad_set_getcaps_function (dec->srcpad, theora_dec_src_getcaps);
  gst_pad_set_event_function (dec->srcpad, theora_dec_src_event);
  gst_pad_set_query_type_function (dec->srcpad, theora_get_query_types);
  gst_pad_set_query_function (dec->srcpad, theora_dec_src_query);

  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  dec->crop = THEORA_DEF_CROP;
  dec->queued = NULL;
}

/* FIXME: copy from libtheora, theora should somehow make this available for seeking */
static int
_theora_ilog (unsigned int v)
{
  int ret = 0;

  while (v) {
    ret++;
    v >>= 1;
  }
  return (ret);
}

static gint64
_theora_granule_frame (GstTheoraDec * dec, gint64 granulepos)
{
  guint ilog;
  gint framecount;

  if (granulepos == -1)
    return -1;

  ilog = dec->granule_shift;

  /* granulepos is last ilog bits for counting pframes since last iframe and 
   * bits in front of that for the framenumber of the last iframe. */
  framecount = granulepos >> ilog;
  framecount += granulepos - (framecount << ilog);

  GST_DEBUG_OBJECT (dec, "framecount=%d, ilog=%u", framecount, ilog);

  return framecount;
}

static GstClockTime
_theora_granule_time (GstTheoraDec * dec, gint64 granulepos)
{
  gint framecount;

  if (granulepos == -1)
    return -1;

  framecount = _theora_granule_frame (dec, granulepos);

  return gst_util_uint64_scale_int (framecount * GST_SECOND,
      dec->info.fps_denominator, dec->info.fps_numerator);
}

static gint64
_inc_granulepos (GstTheoraDec * dec, gint64 granulepos)
{
  gint framecount;

  if (granulepos == -1)
    return -1;

  framecount = _theora_granule_frame (dec, granulepos);

  return (framecount + 1) << dec->granule_shift;
}

#if 0
static const GstFormat *
theora_get_formats (GstPad * pad)
{
  static GstFormat src_formats[] = {
    GST_FORMAT_DEFAULT,         /* frames in this case */
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    0
  };
  static GstFormat sink_formats[] = {
    GST_FORMAT_DEFAULT,
    GST_FORMAT_TIME,
    0
  };

  return (GST_PAD_IS_SRC (pad) ? src_formats : sink_formats);
}
#endif

#if 0
static const GstEventMask *
theora_get_event_masks (GstPad * pad)
{
  static const GstEventMask theora_src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return theora_src_event_masks;
}
#endif

static const GstQueryType *
theora_get_query_types (GstPad * pad)
{
  static const GstQueryType theora_src_query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return theora_src_query_types;
}


static gboolean
theora_dec_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstTheoraDec *dec;
  guint64 scale = 1;

  dec = GST_THEORA_DEC (GST_PAD_PARENT (pad));

  /* we need the info part before we can done something */
  if (!dec->have_header)
    return FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value = gst_util_uint64_scale_int (src_value, 2,
              dec->info.height * dec->info.width * 3);
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
          scale = 3 * (dec->info.width * dec->info.height) / 2;
        case GST_FORMAT_DEFAULT:
          *dest_value = scale * gst_util_uint64_scale (src_value,
              dec->info.fps_numerator, dec->info.fps_denominator * GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = gst_util_uint64_scale (src_value,
              GST_SECOND * dec->info.fps_denominator, dec->info.fps_numerator);
          break;
        case GST_FORMAT_BYTES:
          *dest_value =
              src_value * 3 * (dec->info.width * dec->info.height) / 2;
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
theora_dec_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  GstTheoraDec *dec;

  dec = GST_THEORA_DEC (GST_PAD_PARENT (pad));

  /* we need the info part before we can done something */
  if (!dec->have_header)
    return FALSE;

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
    {
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = _theora_granule_time (dec, src_value);
          break;
        default:
          res = FALSE;
      }
      break;
    }
    case GST_FORMAT_TIME:
    {
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
        {
          guint rest;

          /* framecount */
          *dest_value = gst_util_uint64_scale (src_value,
              dec->info.fps_numerator, GST_SECOND * dec->info.fps_denominator);

          /* funny way of calculating granulepos in theora */
          rest = *dest_value / dec->info.keyframe_frequency_force;
          *dest_value -= rest;
          *dest_value <<= dec->granule_shift;
          *dest_value += rest;
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    }
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
theora_dec_src_query (GstPad * pad, GstQuery * query)
{
  GstTheoraDec *dec = GST_THEORA_DEC (GST_PAD_PARENT (pad));
  gboolean res = FALSE;

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

      time = (time - dec->segment_start) + dec->segment_time;

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
      /* forward to peer for total */
      if (!(res = gst_pad_query (GST_PAD_PEER (dec->sinkpad), query)))
        goto error;
      break;
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
      res = FALSE;
      break;
  }
  return res;

error:
  GST_DEBUG ("query failed");
  return res;
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
      res = FALSE;
      break;
  }

error:
  return res;
}

static gboolean
theora_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstTheoraDec *dec;

  dec = GST_THEORA_DEC (GST_PAD_PARENT (pad));

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
      if (!(res = theora_dec_src_convert (pad, format, cur, &tformat, &tcur)))
        goto error;
      if (!(res = theora_dec_src_convert (pad, format, stop, &tformat, &tstop)))
        goto error;

      /* then seek with time on the peer */
      real_seek = gst_event_new_seek (rate, GST_FORMAT_TIME,
          flags, cur_type, tcur, stop_type, tstop);

      res = gst_pad_push_event (dec->sinkpad, real_seek);

      gst_event_unref (event);
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      break;
  }

  return res;

error:
  gst_event_unref (event);
  return res;
}

static GstCaps *
theora_dec_src_getcaps (GstPad * pad)
{
  GstCaps *caps;

  GST_OBJECT_LOCK (pad);
  if (!(caps = GST_PAD_CAPS (pad)))
    caps = (GstCaps *) gst_pad_get_pad_template_caps (pad);
  caps = gst_caps_ref (caps);
  GST_OBJECT_UNLOCK (pad);

  return caps;
}

static gboolean
theora_dec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = FALSE;
  GstTheoraDec *dec;

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      ret = gst_pad_push_event (dec->srcpad, event);
      break;
    case GST_EVENT_NEWSEGMENT:
    {
      GstFormat format;
      gdouble rate;
      gint64 start, stop, time;

      gst_event_parse_new_segment (event, NULL, &rate, &format, &start, &stop,
          &time);

      /* we need TIME and a positive rate */
      if (format != GST_FORMAT_TIME)
        goto newseg_wrong_format;

      if (rate <= 0.0)
        goto newseg_wrong_rate;

      /* now copy over the values */
      dec->segment_rate = rate;
      dec->segment_start = start;
      dec->segment_stop = stop;
      dec->segment_time = time;

      dec->need_keyframe = TRUE;
      dec->granulepos = -1;
      dec->last_timestamp = -1;
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
    GST_DEBUG ("received non TIME newsegment");
    goto done;
  }
newseg_wrong_rate:
  {
    GST_DEBUG ("negative rates not supported yet");
    goto done;
  }
}

#define ROUND_UP_2(x) (((x) + 1) & ~1)
#define ROUND_UP_4(x) (((x) + 3) & ~3)
#define ROUND_UP_8(x) (((x) + 7) & ~7)

static GstFlowReturn
theora_handle_comment_packet (GstTheoraDec * dec, ogg_packet * packet)
{
  gchar *encoder = NULL;
  GstBuffer *buf;
  GstTagList *list;

  GST_DEBUG ("parsing comment packet");

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

  gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, list);

  return GST_FLOW_OK;
}

static GstFlowReturn
theora_handle_type_packet (GstTheoraDec * dec, ogg_packet * packet)
{
  GstCaps *caps;
  gint par_num, par_den;

  GST_DEBUG_OBJECT (dec, "fps %d/%d, PAR %d/%d",
      dec->info.fps_numerator, dec->info.fps_denominator,
      dec->info.aspect_numerator, dec->info.aspect_denominator);

  /* calculate par
   * the info.aspect_* values reflect PAR;
   * 0:0 is allowed and can be interpreted as 1:1, so correct for it */
  par_num = dec->info.aspect_numerator;
  par_den = dec->info.aspect_denominator;
  if (par_num == 0 && par_den == 0) {
    par_num = par_den = 1;
  }
  /* theora has:
   *
   *  width/height : dimension of the encoded frame 
   *  frame_width/frame_height : dimension of the visible part
   *  offset_x/offset_y : offset in encoded frame where visible part starts
   */
  GST_DEBUG_OBJECT (dec, "dimension %dx%d, PAR %d/%d", dec->info.width,
      dec->info.height, par_num, par_den);
  GST_DEBUG_OBJECT (dec, "frame dimension %dx%d, offset %d:%d",
      dec->info.frame_width, dec->info.frame_height,
      dec->info.offset_x, dec->info.offset_y);

  if (dec->crop) {
    /* add black borders to make width/height/offsets even. we need this because
     * we cannot express an offset to the peer plugin. */
    dec->width = ROUND_UP_2 (dec->info.frame_width + (dec->info.offset_x & 1));
    dec->height =
        ROUND_UP_2 (dec->info.frame_height + (dec->info.offset_y & 1));
    dec->offset_x = dec->info.offset_x & ~1;
    dec->offset_y = dec->info.offset_y & ~1;
  } else {
    /* no cropping, use the encoded dimensions */
    dec->width = dec->info.width;
    dec->height = dec->info.height;
    dec->offset_x = 0;
    dec->offset_y = 0;
  }

  dec->granule_shift = _theora_ilog (dec->info.keyframe_frequency_force - 1);

  GST_DEBUG_OBJECT (dec, "after fixup frame dimension %dx%d, offset %d:%d",
      dec->width, dec->height, dec->offset_x, dec->offset_y);

  /* done */
  theora_decode_init (&dec->state, &dec->info);

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
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
theora_handle_header_packet (GstTheoraDec * dec, ogg_packet * packet)
{
  GstFlowReturn res;

  GST_DEBUG ("parsing header packet");

  if (theora_decode_header (&dec->info, &dec->comment, packet))
    goto header_read_error;

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
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't read header packet"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
theora_dec_push (GstTheoraDec * dec, GstBuffer * buf)
{
  GstFlowReturn result;
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

        time = outtime - ((size * GST_SECOND * dec->info.fps_denominator)
            / dec->info.fps_numerator);

        GST_DEBUG_OBJECT (dec, "patch buffer %lld %lld", size, time);
        GST_BUFFER_TIMESTAMP (buffer) = time;
        /* ignore the result */
        gst_pad_push (dec->srcpad, buffer);
        size--;
      }
      g_list_free (dec->queued);
      dec->queued = NULL;
    }
    result = gst_pad_push (dec->srcpad, buf);
  }

  return result;
}

static GstFlowReturn
theora_handle_data_packet (GstTheoraDec * dec, ogg_packet * packet,
    GstClockTime outtime)
{
  /* normal data packet */
  yuv_buffer yuv;
  GstBuffer *out;
  guint i;
  gboolean keyframe;
  gint out_size;
  gint stride_y, stride_uv;
  gint width, height;
  gint cwidth, cheight;
  GstFlowReturn result;

  if (!dec->have_header)
    goto not_initialized;

  /* the second most significant bit of the first data byte is cleared 
   * for keyframes */
  keyframe = (packet->packet[0] & 0x40) == 0;
  if (keyframe) {
    dec->need_keyframe = FALSE;
  } else if (dec->need_keyframe) {
    goto dropping;
  }

  if (theora_decode_packetin (&dec->state, packet))
    goto could_not_read;

  if (theora_decode_YUVout (&dec->state, &yuv) < 0)
    goto decode_error;

  if ((yuv.y_width != dec->info.width) || (yuv.y_height != dec->info.height))
    goto wrong_dimensions;


  width = dec->width;
  height = dec->height;
  cwidth = width / 2;
  cheight = height / 2;

  /* should get the stride from the caps, for now we round up to the nearest
   * multiple of 4 because some element needs it. chroma needs special 
   * treatment, see videotestsrc. */
  stride_y = ROUND_UP_4 (width);
  stride_uv = ROUND_UP_8 (width) / 2;

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

    dest_y = GST_BUFFER_DATA (out);
    dest_u = dest_y + stride_y * height;
    dest_v = dest_u + stride_uv * cheight;

    src_y = yuv.y + dec->offset_x + dec->offset_y * yuv.y_stride;

    for (i = 0; i < height; i++) {
      memcpy (dest_y, src_y, width);

      dest_y += stride_y;
      src_y += yuv.y_stride;
    }

    src_u = yuv.u + dec->offset_x / 2 + dec->offset_y / 2 * yuv.uv_stride;
    src_v = yuv.v + dec->offset_x / 2 + dec->offset_y / 2 * yuv.uv_stride;

    for (i = 0; i < cheight; i++) {
      memcpy (dest_u, src_u, cwidth);
      memcpy (dest_v, src_v, cwidth);

      dest_u += stride_uv;
      src_u += yuv.uv_stride;
      dest_v += stride_uv;
      src_v += yuv.uv_stride;
    }
  }

  GST_BUFFER_OFFSET (out) = dec->frame_nr;
  dec->frame_nr++;
  GST_BUFFER_OFFSET_END (out) = dec->frame_nr;
  GST_BUFFER_DURATION (out) =
      gst_util_uint64_scale_int (GST_SECOND, dec->info.fps_denominator,
      dec->info.fps_numerator);
  GST_BUFFER_TIMESTAMP (out) = outtime;

  result = theora_dec_push (dec, out);

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
    return GST_FLOW_OK;
  }
could_not_read:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("theora decoder did not read data packet"));
    return GST_FLOW_ERROR;
  }
decode_error:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("couldn't read out YUV image"));
    return GST_FLOW_ERROR;
  }
wrong_dimensions:
  {
    GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
        (NULL), ("dimensions of image do not match header"));
    return GST_FLOW_ERROR;
  }
no_buffer:
  {
    return result;
  }
}

static GstFlowReturn
theora_dec_chain (GstPad * pad, GstBuffer * buf)
{
  GstTheoraDec *dec;
  ogg_packet packet;
  GstFlowReturn result = GST_FLOW_OK;

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));

  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buf);
  packet.bytes = GST_BUFFER_SIZE (buf);
  packet.granulepos = GST_BUFFER_OFFSET_END (buf);
  packet.packetno = 0;          /* we don't really care */
  packet.b_o_s = dec->have_header ? 0 : 1;
  packet.e_o_s = 0;

  if (dec->have_header) {
    if (packet.granulepos != -1) {
      dec->granulepos = packet.granulepos;
      dec->last_timestamp = _theora_granule_time (dec, packet.granulepos);
    } else if (dec->last_timestamp != -1) {
      dec->last_timestamp = _theora_granule_time (dec, dec->granulepos);
    }
  } else {
    dec->last_timestamp = -1;
  }

  GST_DEBUG_OBJECT (dec, "header=%d packetno=%lld, outtime=%" GST_TIME_FORMAT,
      packet.packet[0], packet.packetno, GST_TIME_ARGS (dec->last_timestamp));

  /* switch depending on packet type */
  if (packet.packet[0] & 0x80) {
    if (dec->have_header) {
      GST_WARNING_OBJECT (GST_OBJECT (dec), "Ignoring header");
      goto done;
    }
    result = theora_handle_header_packet (dec, &packet);
  } else {
    result = theora_handle_data_packet (dec, &packet, dec->last_timestamp);
  }

done:
  dec->granulepos = _inc_granulepos (dec, dec->granulepos);

  gst_object_unref (dec);

  gst_buffer_unref (buf);

  return result;
}

static GstStateChangeReturn
theora_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstTheoraDec *dec = GST_THEORA_DEC (element);
  GstStateChangeReturn ret;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      theora_info_init (&dec->info);
      theora_comment_init (&dec->comment);
      dec->have_header = FALSE;
      dec->need_keyframe = TRUE;
      dec->last_timestamp = -1;
      dec->granulepos = -1;
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
      theora_clear (&dec->state);
      theora_comment_clear (&dec->comment);
      theora_info_clear (&dec->info);
      dec->have_header = FALSE;
      dec->granulepos = -1;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
theora_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTheoraDec *dec = GST_THEORA_DEC (object);

  switch (prop_id) {
    case ARG_CROP:
      dec->crop = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
theora_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTheoraDec *dec = GST_THEORA_DEC (object);

  switch (prop_id) {
    case ARG_CROP:
      g_value_set_boolean (value, dec->crop);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
