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

  guint packetno;
  guint64 granulepos;

  gboolean need_keyframe;
  gint width, height;
  gint offset_x, offset_y;
};

struct _GstTheoraDecClass
{
  GstElementClass parent_class;
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
static gboolean theora_dec_src_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static gboolean theora_dec_sink_convert (GstPad * pad,
    GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value);
static const GstFormat *theora_get_formats (GstPad * pad);
static const GstEventMask *theora_get_event_masks (GstPad * pad);
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
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->change_state = theora_dec_change_state;

  GST_DEBUG_CATEGORY_INIT (theoradec_debug, "theoradec", 0, "Theora decoder");
}

static void
gst_theora_dec_init (GstTheoraDec * dec)
{
  dec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&theora_dec_sink_factory), "sink");
  gst_pad_set_formats_function (dec->sinkpad, theora_get_formats);
  gst_pad_set_convert_function (dec->sinkpad, theora_dec_sink_convert);
  gst_pad_set_chain_function (dec->sinkpad, theora_dec_chain);
  gst_element_add_pad (GST_ELEMENT (dec), dec->sinkpad);

  dec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&theora_dec_src_factory), "src");
  gst_pad_use_explicit_caps (dec->srcpad);
  gst_pad_set_event_mask_function (dec->srcpad, theora_get_event_masks);
  gst_pad_set_event_function (dec->srcpad, theora_dec_src_event);
  gst_pad_set_query_type_function (dec->srcpad, theora_get_query_types);
  gst_pad_set_query_function (dec->srcpad, theora_dec_src_query);
  gst_pad_set_formats_function (dec->srcpad, theora_get_formats);
  gst_pad_set_convert_function (dec->srcpad, theora_dec_src_convert);

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

static const GstEventMask *
theora_get_event_masks (GstPad * pad)
{
  static const GstEventMask theora_src_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return theora_src_event_masks;
}

static const GstQueryType *
theora_get_query_types (GstPad * pad)
{
  static const GstQueryType theora_src_query_types[] = {
    GST_QUERY_TOTAL,
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

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));

  /* we need the info part before we can done something */
  if (dec->packetno < 1)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_value =
              src_value * 2 / (dec->info.height * dec->info.width * 3);
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
          *dest_value =
              scale * (((guint64) src_value * dec->info.fps_numerator) /
              (dec->info.fps_denominator * GST_SECOND));
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * (GST_SECOND * dec->info.fps_denominator /
              dec->info.fps_numerator);
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

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));

  /* we need the info part before we can done something */
  if (dec->packetno < 1)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_DEFAULT:
    {
      guint64 framecount;
      guint ilog;

      ilog = _theora_ilog (dec->info.keyframe_frequency_force - 1);

      /* granulepos is last ilog bits for counting pframes since last iframe and 
       * bits in front of that for the framenumber of the last iframe. */
      framecount = src_value >> ilog;
      framecount += src_value - (framecount << ilog);

      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = framecount * (GST_SECOND * dec->info.fps_denominator /
              dec->info.fps_numerator);
          break;
        default:
          res = FALSE;
      }
      break;
    }
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
theora_dec_src_query (GstPad * pad, GstQueryType query, GstFormat * format,
    gint64 * value)
{
  gint64 granulepos;
  GstTheoraDec *dec = GST_THEORA_DEC (gst_pad_get_parent (pad));
  GstFormat my_format = GST_FORMAT_DEFAULT;
  guint64 time;

  if (query == GST_QUERY_POSITION) {
    /* this is easy, we can convert a granule position to everything */
    granulepos = dec->granulepos;
  } else {
    /* for the total, we just forward the query to the peer */
    if (!gst_pad_query (GST_PAD_PEER (dec->sinkpad), query, &my_format,
            &granulepos))
      return FALSE;
  }

  /* and convert to the final format in two steps with time as the 
   * intermediate step */
  my_format = GST_FORMAT_TIME;
  if (!theora_dec_sink_convert (dec->sinkpad, GST_FORMAT_DEFAULT, granulepos,
          &my_format, &time))
    return FALSE;
  if (!gst_pad_convert (pad, my_format, time, format, value))
    return FALSE;

  GST_LOG_OBJECT (dec,
      "query %u: peer returned granulepos: %llu - we return %llu (format %u)",
      query, granulepos, *value, *format);
  return TRUE;
}

static gboolean
theora_dec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstTheoraDec *dec;
  GstFormat format;

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      guint64 value;

      /* we have to ask our peer to seek to time here as we know
       * nothing about how to generate a granulepos from the src
       * formats or anything.
       * 
       * First bring the requested format to time 
       */
      format = GST_FORMAT_TIME;
      res = gst_pad_convert (pad, GST_EVENT_SEEK_FORMAT (event),
          GST_EVENT_SEEK_OFFSET (event), &format, &value);
      if (!res)
        goto error;

      /* then seek with time on the peer */
      GstEvent *real_seek = gst_event_new_seek (
          (GST_EVENT_SEEK_TYPE (event) & ~GST_SEEK_FORMAT_MASK) |
          format, value);

      res = gst_pad_send_event (GST_PAD_PEER (dec->sinkpad), real_seek);
      if (!res)
        goto error;

      /* all worked, make sure we sync to keyframe */
      dec->need_keyframe = TRUE;

    error:
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
            "setting granuleposition to %" G_GUINT64_FORMAT " after discont",
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
        GstFormat time_format, default_format, bytes_format;

        time_format = GST_FORMAT_TIME;
        default_format = GST_FORMAT_DEFAULT;
        bytes_format = GST_FORMAT_BYTES;

        /* if one of them works, all of them work */
        if (theora_dec_sink_convert (dec->sinkpad, GST_FORMAT_DEFAULT,
                dec->granulepos, &time_format, &time)
            && theora_dec_src_convert (dec->srcpad, GST_FORMAT_TIME, time,
                &default_format, &value)
            && theora_dec_src_convert (dec->srcpad, GST_FORMAT_TIME, time,
                &bytes_format, &bytes)) {
          gst_pad_push (dec->srcpad,
              GST_DATA (gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
                      time, GST_FORMAT_DEFAULT, value, GST_FORMAT_BYTES, bytes,
                      0)));
          /* store new framenumber */
          dec->packetno = value + 3;
        } else {
          GST_ERROR_OBJECT (dec,
              "failed to parse data for DISCONT event, not sending any");
        }
        /* sync to keyframe */
        dec->need_keyframe = TRUE;
      }
      break;
    default:
      break;
  }
  gst_pad_event_default (dec->sinkpad, event);
}

#define ROUND_UP_2(x) (((x) + 1) & ~1)
#define ROUND_UP_4(x) (((x) + 3) & ~3)
#define ROUND_UP_8(x) (((x) + 7) & ~7)

static void
theora_dec_chain (GstPad * pad, GstData * data)
{
  GstBuffer *buf;
  GstTheoraDec *dec;
  ogg_packet packet;
  guint64 offset_end;
  GstClockTime outtime;

  dec = GST_THEORA_DEC (gst_pad_get_parent (pad));
  if (GST_IS_EVENT (data)) {
    theora_dec_event (dec, GST_EVENT (data));
    return;
  }

  buf = GST_BUFFER (data);

  if (dec->packetno >= 3) {
    offset_end = GST_BUFFER_OFFSET_END (buf);
    if (offset_end != -1) {
      dec->granulepos = offset_end;
      /* granulepos to time */
      outtime = GST_SECOND * theora_granule_time (&dec->state, dec->granulepos);
    } else {
      GstFormat time_format = GST_FORMAT_TIME;

      /* framenumber to time */
      theora_dec_src_convert (dec->srcpad, GST_FORMAT_DEFAULT,
          dec->packetno - 3, &time_format, &outtime);
    }
  } else {
    /* we don't know yet */
    outtime = -1;
  }

  /* make ogg_packet out of the buffer */
  packet.packet = GST_BUFFER_DATA (buf);
  packet.bytes = GST_BUFFER_SIZE (buf);
  packet.granulepos = dec->granulepos;
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

      /* add black borders to make width/height/offsets even. we need this because
       * we cannot express an offset to the peer plugin. */
      dec->width =
          ROUND_UP_2 (dec->info.frame_width + (dec->info.offset_x & 1));
      dec->height =
          ROUND_UP_2 (dec->info.frame_height + (dec->info.offset_y & 1));
      dec->offset_x = dec->info.offset_x & ~1;
      dec->offset_y = dec->info.offset_y & ~1;

      GST_DEBUG_OBJECT (dec, "after fixup frame dimension %dx%d, offset %d:%d",
          dec->width, dec->height, dec->offset_x, dec->offset_y);

      /* done */
      theora_decode_init (&dec->state, &dec->info);
      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
          "framerate", G_TYPE_DOUBLE,
          ((gdouble) dec->info.fps_numerator) / dec->info.fps_denominator,
          "pixel-aspect-ratio", GST_TYPE_FRACTION, par_num, par_den,
          "width", G_TYPE_INT, dec->width, "height", G_TYPE_INT,
          dec->height, NULL);
      gst_pad_set_explicit_caps (dec->srcpad, caps);
      gst_caps_free (caps);
    }
  } else {
    /* normal data packet */
    yuv_buffer yuv;
    GstBuffer *out;
    guint i;
    gboolean keyframe;
    gint out_size;
    gint stride_y, stride_uv;
    gint width, height;
    gint cwidth, cheight;

    /* the second most significant bit of the first data byte is cleared 
     * for keyframes */
    keyframe = (packet.packet[0] & 0x40) == 0;
    if (keyframe) {
      dec->need_keyframe = FALSE;
    } else if (dec->need_keyframe) {
      /* drop frames if we're looking for a keyframe */
      gst_data_unref (data);
      return;
    }
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
    out = gst_pad_alloc_buffer (dec->srcpad, GST_BUFFER_OFFSET_NONE, out_size);

    /* copy the visible region to the destination. This is actually pretty
     * complicated and gstreamer doesn't support all the needed caps to do this
     * correctly. For example, when we have an odd offset, we should only combine
     * 1 row/column of luma samples with on chroma sample in colorspace conversion. 
     * We compensate for this by adding a block border around the image when the
     * offset of size is odd (see above).
     */
    {
      guint8 *dest_y, *src_y;
      guint8 *dest_u, *src_u;
      guint8 *dest_v, *src_v;

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

    GST_BUFFER_OFFSET (out) = dec->packetno - 4;
    GST_BUFFER_OFFSET_END (out) = dec->packetno - 3;
    GST_BUFFER_DURATION (out) =
        GST_SECOND * ((gdouble) dec->info.fps_denominator) /
        dec->info.fps_numerator;
    GST_BUFFER_TIMESTAMP (out) = outtime;

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
      dec->need_keyframe = TRUE;
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
      break;
  }

  return parent_class->change_state (element);
}
