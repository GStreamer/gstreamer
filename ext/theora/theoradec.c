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

  guint packetno;
  guint64 granulepos;
};

struct _GstTheoraDecClass
{
  GstElementClass parent_class;
};

static GstElementDetails theora_dec_details = {
  "TheoraDec",
  "Filter/Decoder/Video",
  "decode raw theora streams to raw YUV video",
  "Benjamin Otte <in7y118@public.uni-hamburg.de>",
};

static GstStaticPadTemplate theora_dec_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
	"format = (fourcc) I420, "
	"framerate = (double) [0, MAX], "
	"width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate theora_dec_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-theora")
    );

GST_BOILERPLATE (GstTheoraDec, gst_theora_dec, GstElement, GST_TYPE_ELEMENT);

static void theora_dec_chain (GstPad * pad, GstData * data);
static GstElementStateReturn theora_dec_change_state (GstElement * element);
static gboolean theora_dec_src_event (GstPad * pad, GstEvent * event);
static gboolean theora_dec_src_query (GstPad * pad,
    GstQueryType query, GstFormat * format, gint64 * value);


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
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = theora_dec_change_state;
}

static void
gst_theora_dec_init (GstTheoraDec * dec)
{
  dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&theora_dec_sink_factory), "sink");
  gst_pad_set_chain_function (dec->sinkpad, theora_dec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&theora_dec_src_factory), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
  gst_pad_set_event_function (dec->srcpad, theora_dec_src_event);
  gst_pad_set_query_function (dec->srcpad, theora_dec_src_query);
  gst_element_add_pad (GST_ELEMENT (dec), dec->srcpad);

  GST_FLAG_SET (dec, GST_ELEMENT_EVENT_AWARE);
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

static gboolean
theora_dec_from_granulepos (GstTheoraDec * dec, GstFormat format, guint64 from,
    guint64 * to)
{
  guint64 framecount;
  guint ilog = _theora_ilog (dec->info.keyframe_frequency_force);

  if (dec->packetno < 1)
    return FALSE;

  /* granulepos is last ilog bits for counting pframes since last iframe and 
   * bits in front of that for the framenumber of the last iframe. */
  framecount = from >> ilog;
  framecount += from - (framecount << ilog);

  switch (format) {
    case GST_FORMAT_TIME:
      *to = framecount =
	  from * GST_SECOND * dec->info.fps_denominator /
	  dec->info.fps_numerator;
      break;
    case GST_FORMAT_DEFAULT:
      *to = framecount;
      break;
    case GST_FORMAT_BYTES:
      *to = framecount * dec->info.height * dec->info.width * 12 / 8;
      break;
    default:
      return FALSE;
  }
  return TRUE;
}

/* FIXME: we can only seek to keyframes... */
static gboolean
theora_dec_to_granulepos (GstTheoraDec * dec, GstFormat format, guint64 from,
    guint64 * to)
{
  guint64 framecount;

  if (dec->packetno < 1)
    return FALSE;

  switch (format) {
    case GST_FORMAT_TIME:
      framecount =
	  from * dec->info.fps_numerator / (GST_SECOND *
	  dec->info.fps_denominator);
      break;
    case GST_FORMAT_DEFAULT:
      framecount = from;
      break;
    case GST_FORMAT_BYTES:
      framecount = from * 8 / (dec->info.height * dec->info.width * 12);
      break;
    default:
      return FALSE;
  }
  *to = framecount << _theora_ilog (dec->info.keyframe_frequency_force - 1);
  return TRUE;
}

static gboolean
theora_dec_src_query (GstPad * pad, GstQueryType query, GstFormat * format,
    gint64 * value)
{
  gint64 granulepos;
  GstTheoraDec *dec = GST_THEORA_DEC (gst_pad_get_parent (pad));
  GstFormat my_format = GST_FORMAT_DEFAULT;

  if (!gst_pad_query (GST_PAD_PEER (dec->sinkpad), query, &my_format,
	  &granulepos))
    return FALSE;

  if (!theora_dec_from_granulepos (dec, *format, granulepos, value))
    return FALSE;

  GST_LOG_OBJECT (dec,
      "query %u: peer returned granulepos: %llu - we return %llu (format %u)\n",
      query, granulepos, *value, *format);
  return TRUE;
}

static gboolean
theora_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstTheoraDec *dec;

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      guint64 value;

      res = theora_dec_to_granulepos (dec, GST_EVENT_SEEK_FORMAT (event),
	  GST_EVENT_SEEK_OFFSET (event), &value);
      if (res) {
	GstEvent *real_seek = gst_event_new_seek (
	    (GST_EVENT_SEEK_TYPE (event) & ~GST_SEEK_FORMAT_MASK) |
	    GST_FORMAT_DEFAULT,
	    value);

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

static void
theora_dec_event (GstTheoraDec * dec, GstEvent * event)
{
  guint64 value, time, bytes;

  GST_LOG_OBJECT (dec, "handling event");
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      if (gst_event_discont_get_value (event, GST_FORMAT_DEFAULT, &value)) {
	dec->granulepos = value;
	GST_DEBUG_OBJECT (dec,
	    "setting granuleposition to %" G_GUINT64_FORMAT " after discont\n",
	    value);
      } else {
	GST_WARNING_OBJECT (dec,
	    "discont event didn't include offset, we might set it wrong now");
      }
      if (dec->packetno < 3) {
	if (dec->granulepos != 0)
	  GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
	      ("can't handle discont before parsing first 3 packets"));
	dec->packetno = 0;
	gst_pad_push (dec->srcpad, GST_DATA (gst_event_new_discontinuous (FALSE,
		    GST_FORMAT_TIME, (guint64) 0, GST_FORMAT_DEFAULT,
		    (guint64) 0, GST_FORMAT_BYTES, (guint64) 0, 0)));
      } else {
	dec->packetno = 3;
	/* if one of them works, all of them work */
	if (theora_dec_from_granulepos (dec, GST_FORMAT_TIME, dec->granulepos,
		&time)
	    && theora_dec_from_granulepos (dec, GST_FORMAT_DEFAULT,
		dec->granulepos, &value)
	    && theora_dec_from_granulepos (dec, GST_FORMAT_BYTES,
		dec->granulepos, &bytes)) {
	  gst_pad_push (dec->srcpad,
	      GST_DATA (gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
		      time, GST_FORMAT_DEFAULT, value, GST_FORMAT_BYTES, bytes,
		      0)));
	} else {
	  GST_ERROR_OBJECT (dec,
	      "failed to parse data for DISCONT event, not sending any");
	}
      }
      break;
    default:
      break;
  }
  gst_pad_event_default (dec->sinkpad, event);
}

static void
theora_dec_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf;
  GstTheoraDec *dec;
  ogg_packet packet;

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (data)) {
    theora_dec_event (dec, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);
  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buf);
  packet.bytes = GST_BUFFER_SIZE (buf);
  packet.granulepos = GST_BUFFER_OFFSET_END (buf);
  packet.packetno = dec->packetno++;
  packet.b_o_s = (packet.packetno == 0) ? 1 : 0;
  packet.e_o_s = 0;
  /* switch depending on packet type */
  if (packet.packet[0] & 0x80) {
    /* header packet */
    if (theora_decode_header (&dec->info, &dec->comment, &packet)) {
      GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
	  (NULL), ("couldn't read header packet"));
      gst_data_unref (data);
      return;
    }
    if (packet.packetno == 1) {
      gchar *encoder = NULL;
      GstTagList *list =
	  gst_tag_list_from_vorbiscomment_buffer (buf, "\201theora", 7,
	  &encoder);

      if (!list) {
	GST_ERROR_OBJECT (dec, "failed to parse tags");
	list = gst_tag_list_new ();
      }
      if (encoder) {
	gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
	    GST_TAG_ENCODER, encoder, NULL);
	g_free (encoder);
      }
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
	  GST_TAG_ENCODER_VERSION, dec->info.version_major, NULL);
      gst_element_found_tags_for_pad (GST_ELEMENT (dec), dec->srcpad, 0, list);
    } else if (packet.packetno == 2) {
      GstCaps *caps;

      /* done */
      theora_decode_init (&dec->state, &dec->info);
      caps = gst_caps_new_simple ("video/x-raw-yuv",
	  "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
	  "framerate", G_TYPE_DOUBLE,
	  ((gdouble) dec->info.fps_numerator) / dec->info.fps_denominator,
	  "width", G_TYPE_INT, dec->info.width, "height", G_TYPE_INT,
	  dec->info.height, NULL);
      gst_pad_set_explicit_caps (dec->srcpad, caps);
      gst_caps_free (caps);
    }
  } else {
    yuv_buffer yuv;
    GstBuffer *out;
    guint8 *y, *v, *u;
    guint i;

    /* normal data packet */
#if 0
    {
      GTimeVal tv;
      guint64 time;

      g_get_current_time (&tv);
      time = GST_TIMEVAL_TO_TIME (tv);
#endif
      if (theora_decode_packetin (&dec->state, &packet)) {
	GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
	    (NULL), ("theora decoder did not read data packet"));
	gst_data_unref (data);
	return;
      }
      if (theora_decode_YUVout (&dec->state, &yuv) < 0) {
	GST_ELEMENT_ERROR (GST_ELEMENT (dec), STREAM, DECODE,
	    (NULL), ("couldn't read out YUV image"));
	gst_data_unref (data);
	return;
      }
      g_return_if_fail (yuv.y_width == dec->info.width);
      g_return_if_fail (yuv.y_height == dec->info.height);
      out = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE,
	  yuv.y_width * yuv.y_height * 12 / 8);
      y = GST_BUFFER_DATA (out);
      u = y + yuv.y_width * yuv.y_height;
      v = u + yuv.y_width * yuv.y_height / 4;
      for (i = 0; i < yuv.y_height; i++) {
	memcpy (y + i * yuv.y_width, yuv.y + i * yuv.y_stride, yuv.y_width);
      }
      for (i = 0; i < yuv.y_height / 2; i++) {
	memcpy (u + i * yuv.uv_width, yuv.u + i * yuv.uv_stride, yuv.uv_width);
	memcpy (v + i * yuv.uv_width, yuv.v + i * yuv.uv_stride, yuv.uv_width);
      }
      GST_BUFFER_OFFSET (out) = dec->packetno - 4;
      GST_BUFFER_OFFSET_END (out) = dec->packetno - 3;
      GST_BUFFER_DURATION (out) =
	  GST_SECOND * ((gdouble) dec->info.fps_denominator) /
	  dec->info.fps_numerator;
      GST_BUFFER_TIMESTAMP (out) =
	  GST_BUFFER_OFFSET (out) * GST_BUFFER_DURATION (out);
#if 0
      g_get_current_time (&tv);
      time = GST_TIMEVAL_TO_TIME (tv) - time;
      if (time > 10000000)
	g_print ("w00t, you're sl0000w!! - %llu\n", time);
    }
#endif
    gst_pad_push (dec->srcpad, GST_DATA (out));
  }
  gst_data_unref (data);
}

static GstElementStateReturn
theora_dec_change_state (GstElement * element)
{
  GstTheoraDec *dec = GST_THEORA_DEC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      theora_info_init (&dec->info);
      theora_comment_init (&dec->comment);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      theora_clear (&dec->state);
      theora_comment_clear (&dec->comment);
      theora_info_clear (&dec->info);
      dec->packetno = 0;
      dec->granulepos = 0;
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  return parent_class->change_state (element);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gsttags"))
    return FALSE;

  if (!gst_element_register (plugin, "theoradec", GST_RANK_SECONDARY,
	  gst_theora_dec_get_type ()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gsttheora",
    "Theora plugin library",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)
