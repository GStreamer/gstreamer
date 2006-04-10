/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2006> Wim Taymans <wim@fluendo.com>
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
#include "config.h"
#endif
#include "qtdemux.h"

#include <stdlib.h>
#include <string.h>
#include <zlib.h>

GST_DEBUG_CATEGORY_EXTERN (qtdemux_debug);
#define GST_CAT_DEFAULT qtdemux_debug

/* temporary hack */
#define gst_util_dump_mem(a,b)  /* */

#define QTDEMUX_GUINT32_GET(a)  (GST_READ_UINT32_BE(a))
#define QTDEMUX_GUINT24_GET(a)  (GST_READ_UINT32_BE(a) >> 8)
#define QTDEMUX_GUINT16_GET(a)  (GST_READ_UINT16_BE(a))
#define QTDEMUX_GUINT8_GET(a) (*(guint8 *)(a))
#define QTDEMUX_FP32_GET(a)     ((GST_READ_UINT32_BE(a))/65536.0)
#define QTDEMUX_FP16_GET(a)     ((GST_READ_UINT16_BE(a))/256.0)
#define QTDEMUX_FOURCC_GET(a)   (GST_READ_UINT32_LE(a))

#define QTDEMUX_GUINT64_GET(a) ((((guint64)QTDEMUX_GUINT32_GET(a))<<32)|QTDEMUX_GUINT32_GET(((void *)a)+4))

typedef struct _QtNode QtNode;
typedef struct _QtNodeType QtNodeType;
typedef struct _QtDemuxSegment QtDemuxSegment;
typedef struct _QtDemuxSample QtDemuxSample;

//typedef struct _QtDemuxStream QtDemuxStream;

struct _QtNode
{
  guint32 type;
  gpointer data;
  gint len;
};

struct _QtNodeType
{
  guint32 fourcc;
  gchar *name;
  gint flags;
  void (*dump) (GstQTDemux * qtdemux, void *buffer, int depth);
};

struct _QtDemuxSample
{
  guint32 chunk;
  guint32 size;
  guint64 offset;
  guint64 timestamp;            /* In GstClockTime */
  guint32 duration;             /* in GstClockTime */
  gboolean keyframe;            /* TRUE when this packet is a keyframe */
};

struct _QtDemuxSegment
{
  /* global time and duration, all gst time */
  guint64 time;
  guint64 stop_time;
  guint64 duration;
  /* media time of trak, all gst time */
  guint64 media_start;
  guint64 media_stop;
  gdouble rate;
};

struct _QtDemuxStream
{
  GstPad *pad;

  /* stream type */
  guint32 subtype;
  GstCaps *caps;
  guint32 fourcc;

  /* duration/scale */
  guint32 duration;             /* in timescale */
  guint32 timescale;

  /* our samples */
  guint32 n_samples;
  QtDemuxSample *samples;
  gboolean all_keyframe;        /* TRUE when all samples are keyframes (no stss) */

  /* if we use chunks or samples */
  gboolean sampled;

  /* video info */
  gint width;
  gint height;
  /* Numerator/denominator framerate */
  gint fps_n;
  gint fps_d;
  guint16 bits_per_sample;
  guint16 color_table_id;

  /* audio info */
  gdouble rate;
  gint n_channels;
  guint samples_per_packet;
  guint samples_per_frame;
  guint bytes_per_packet;
  guint bytes_per_sample;
  guint bytes_per_frame;
  guint compression;

  /* when a discontinuity is pending */
  gboolean discont;

  /* current position */
  guint32 segment_index;
  guint32 sample_index;
  guint64 time_position;        /* in gst time */

  /* quicktime segments */
  guint32 n_segments;
  QtDemuxSegment *segments;
  gboolean segment_pending;
};

enum QtDemuxState
{
  QTDEMUX_STATE_INITIAL,        /* Initial state (haven't got the header yet) */
  QTDEMUX_STATE_HEADER,         /* Parsing the header */
  QTDEMUX_STATE_MOVIE,          /* Parsing/Playing the media data */
  QTDEMUX_STATE_BUFFER_MDAT     /* Buffering the mdat atom */
};

static GNode *qtdemux_tree_get_child_by_type (GNode * node, guint32 fourcc);
static GNode *qtdemux_tree_get_sibling_by_type (GNode * node, guint32 fourcc);

static GstElementDetails gst_qtdemux_details =
GST_ELEMENT_DETAILS ("QuickTime demuxer",
    "Codec/Demuxer",
    "Demultiplex a QuickTime file into audio and video streams",
    "David Schleef <ds@schleef.org>");

static GstStaticPadTemplate gst_qtdemux_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/quicktime; audio/x-m4a; application/x-3gp")
    );

static GstStaticPadTemplate gst_qtdemux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_qtdemux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstElementClass *parent_class = NULL;

/* we could generate these programmatically, but the generation code
 * is only a few lines shorter than the tables, and much uglier */
static const guint32 ff_qt_default_palette_256[256] = {
  0xFFFFFF, 0xFFFFCC, 0xFFFF99, 0xFFFF66, 0xFFFF33, 0xFFFF00,
  0xFFCCFF, 0xFFCCCC, 0xFFCC99, 0xFFCC66, 0xFFCC33, 0xFFCC00,
  0xFF99FF, 0xFF99CC, 0xFF9999, 0xFF9966, 0xFF9933, 0xFF9900,
  0xFF66FF, 0xFF66CC, 0xFF6699, 0xFF6666, 0xFF6633, 0xFF6600,
  0xFF33FF, 0xFF33CC, 0xFF3399, 0xFF3366, 0xFF3333, 0xFF3300,
  0xFF00FF, 0xFF00CC, 0xFF0099, 0xFF0066, 0xFF0033, 0xFF0000,
  0xCCFFFF, 0xCCFFCC, 0xCCFF99, 0xCCFF66, 0xCCFF33, 0xCCFF00,
  0xCCCCFF, 0xCCCCCC, 0xCCCC99, 0xCCCC66, 0xCCCC33, 0xCCCC00,
  0xCC99FF, 0xCC99CC, 0xCC9999, 0xCC9966, 0xCC9933, 0xCC9900,
  0xCC66FF, 0xCC66CC, 0xCC6699, 0xCC6666, 0xCC6633, 0xCC6600,
  0xCC33FF, 0xCC33CC, 0xCC3399, 0xCC3366, 0xCC3333, 0xCC3300,
  0xCC00FF, 0xCC00CC, 0xCC0099, 0xCC0066, 0xCC0033, 0xCC0000,
  0x99FFFF, 0x99FFCC, 0x99FF99, 0x99FF66, 0x99FF33, 0x99FF00,
  0x99CCFF, 0x99CCCC, 0x99CC99, 0x99CC66, 0x99CC33, 0x99CC00,
  0x9999FF, 0x9999CC, 0x999999, 0x999966, 0x999933, 0x999900,
  0x9966FF, 0x9966CC, 0x996699, 0x996666, 0x996633, 0x996600,
  0x9933FF, 0x9933CC, 0x993399, 0x993366, 0x993333, 0x993300,
  0x9900FF, 0x9900CC, 0x990099, 0x990066, 0x990033, 0x990000,
  0x66FFFF, 0x66FFCC, 0x66FF99, 0x66FF66, 0x66FF33, 0x66FF00,
  0x66CCFF, 0x66CCCC, 0x66CC99, 0x66CC66, 0x66CC33, 0x66CC00,
  0x6699FF, 0x6699CC, 0x669999, 0x669966, 0x669933, 0x669900,
  0x6666FF, 0x6666CC, 0x666699, 0x666666, 0x666633, 0x666600,
  0x6633FF, 0x6633CC, 0x663399, 0x663366, 0x663333, 0x663300,
  0x6600FF, 0x6600CC, 0x660099, 0x660066, 0x660033, 0x660000,
  0x33FFFF, 0x33FFCC, 0x33FF99, 0x33FF66, 0x33FF33, 0x33FF00,
  0x33CCFF, 0x33CCCC, 0x33CC99, 0x33CC66, 0x33CC33, 0x33CC00,
  0x3399FF, 0x3399CC, 0x339999, 0x339966, 0x339933, 0x339900,
  0x3366FF, 0x3366CC, 0x336699, 0x336666, 0x336633, 0x336600,
  0x3333FF, 0x3333CC, 0x333399, 0x333366, 0x333333, 0x333300,
  0x3300FF, 0x3300CC, 0x330099, 0x330066, 0x330033, 0x330000,
  0x00FFFF, 0x00FFCC, 0x00FF99, 0x00FF66, 0x00FF33, 0x00FF00,
  0x00CCFF, 0x00CCCC, 0x00CC99, 0x00CC66, 0x00CC33, 0x00CC00,
  0x0099FF, 0x0099CC, 0x009999, 0x009966, 0x009933, 0x009900,
  0x0066FF, 0x0066CC, 0x006699, 0x006666, 0x006633, 0x006600,
  0x0033FF, 0x0033CC, 0x003399, 0x003366, 0x003333, 0x003300,
  0x0000FF, 0x0000CC, 0x000099, 0x000066, 0x000033, 0xEE0000,
  0xDD0000, 0xBB0000, 0xAA0000, 0x880000, 0x770000, 0x550000,
  0x440000, 0x220000, 0x110000, 0x00EE00, 0x00DD00, 0x00BB00,
  0x00AA00, 0x008800, 0x007700, 0x005500, 0x004400, 0x002200,
  0x001100, 0x0000EE, 0x0000DD, 0x0000BB, 0x0000AA, 0x000088,
  0x000077, 0x000055, 0x000044, 0x000022, 0x000011, 0xEEEEEE,
  0xDDDDDD, 0xBBBBBB, 0xAAAAAA, 0x888888, 0x777777, 0x555555,
  0x444444, 0x222222, 0x111111, 0x000000
};

static const guint32 ff_qt_grayscale_palette_256[256] = {
  0xffffff, 0xfefefe, 0xfdfdfd, 0xfcfcfc, 0xfbfbfb, 0xfafafa, 0xf9f9f9,
  0xf8f8f8, 0xf7f7f7, 0xf6f6f6, 0xf5f5f5, 0xf4f4f4, 0xf3f3f3, 0xf2f2f2,
  0xf1f1f1, 0xf0f0f0, 0xefefef, 0xeeeeee, 0xededed, 0xececec, 0xebebeb,
  0xeaeaea, 0xe9e9e9, 0xe8e8e8, 0xe7e7e7, 0xe6e6e6, 0xe5e5e5, 0xe4e4e4,
  0xe3e3e3, 0xe2e2e2, 0xe1e1e1, 0xe0e0e0, 0xdfdfdf, 0xdedede, 0xdddddd,
  0xdcdcdc, 0xdbdbdb, 0xdadada, 0xd9d9d9, 0xd8d8d8, 0xd7d7d7, 0xd6d6d6,
  0xd5d5d5, 0xd4d4d4, 0xd3d3d3, 0xd2d2d2, 0xd1d1d1, 0xd0d0d0, 0xcfcfcf,
  0xcecece, 0xcdcdcd, 0xcccccc, 0xcbcbcb, 0xcacaca, 0xc9c9c9, 0xc8c8c8,
  0xc7c7c7, 0xc6c6c6, 0xc5c5c5, 0xc4c4c4, 0xc3c3c3, 0xc2c2c2, 0xc1c1c1,
  0xc0c0c0, 0xbfbfbf, 0xbebebe, 0xbdbdbd, 0xbcbcbc, 0xbbbbbb, 0xbababa,
  0xb9b9b9, 0xb8b8b8, 0xb7b7b7, 0xb6b6b6, 0xb5b5b5, 0xb4b4b4, 0xb3b3b3,
  0xb2b2b2, 0xb1b1b1, 0xb0b0b0, 0xafafaf, 0xaeaeae, 0xadadad, 0xacacac,
  0xababab, 0xaaaaaa, 0xa9a9a9, 0xa8a8a8, 0xa7a7a7, 0xa6a6a6, 0xa5a5a5,
  0xa4a4a4, 0xa3a3a3, 0xa2a2a2, 0xa1a1a1, 0xa0a0a0, 0x9f9f9f, 0x9e9e9e,
  0x9d9d9d, 0x9c9c9c, 0x9b9b9b, 0x9a9a9a, 0x999999, 0x989898, 0x979797,
  0x969696, 0x959595, 0x949494, 0x939393, 0x929292, 0x919191, 0x909090,
  0x8f8f8f, 0x8e8e8e, 0x8d8d8d, 0x8c8c8c, 0x8b8b8b, 0x8a8a8a, 0x898989,
  0x888888, 0x878787, 0x868686, 0x858585, 0x848484, 0x838383, 0x828282,
  0x818181, 0x808080, 0x7f7f7f, 0x7e7e7e, 0x7d7d7d, 0x7c7c7c, 0x7b7b7b,
  0x7a7a7a, 0x797979, 0x787878, 0x777777, 0x767676, 0x757575, 0x747474,
  0x737373, 0x727272, 0x717171, 0x707070, 0x6f6f6f, 0x6e6e6e, 0x6d6d6d,
  0x6c6c6c, 0x6b6b6b, 0x6a6a6a, 0x696969, 0x686868, 0x676767, 0x666666,
  0x656565, 0x646464, 0x636363, 0x626262, 0x616161, 0x606060, 0x5f5f5f,
  0x5e5e5e, 0x5d5d5d, 0x5c5c5c, 0x5b5b5b, 0x5a5a5a, 0x595959, 0x585858,
  0x575757, 0x565656, 0x555555, 0x545454, 0x535353, 0x525252, 0x515151,
  0x505050, 0x4f4f4f, 0x4e4e4e, 0x4d4d4d, 0x4c4c4c, 0x4b4b4b, 0x4a4a4a,
  0x494949, 0x484848, 0x474747, 0x464646, 0x454545, 0x444444, 0x434343,
  0x424242, 0x414141, 0x404040, 0x3f3f3f, 0x3e3e3e, 0x3d3d3d, 0x3c3c3c,
  0x3b3b3b, 0x3a3a3a, 0x393939, 0x383838, 0x373737, 0x363636, 0x353535,
  0x343434, 0x333333, 0x323232, 0x313131, 0x303030, 0x2f2f2f, 0x2e2e2e,
  0x2d2d2d, 0x2c2c2c, 0x2b2b2b, 0x2a2a2a, 0x292929, 0x282828, 0x272727,
  0x262626, 0x252525, 0x242424, 0x232323, 0x222222, 0x212121, 0x202020,
  0x1f1f1f, 0x1e1e1e, 0x1d1d1d, 0x1c1c1c, 0x1b1b1b, 0x1a1a1a, 0x191919,
  0x181818, 0x171717, 0x161616, 0x151515, 0x141414, 0x131313, 0x121212,
  0x111111, 0x101010, 0x0f0f0f, 0x0e0e0e, 0x0d0d0d, 0x0c0c0c, 0x0b0b0b,
  0x0a0a0a, 0x090909, 0x080808, 0x070707, 0x060606, 0x050505, 0x040404,
  0x030303, 0x020202, 0x010101, 0x000000
};

static void gst_qtdemux_class_init (GstQTDemuxClass * klass);
static void gst_qtdemux_base_init (GstQTDemuxClass * klass);
static void gst_qtdemux_init (GstQTDemux * quicktime_demux);
static void gst_qtdemux_dispose (GObject * object);
static GstStateChangeReturn gst_qtdemux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_qtdemux_loop (GstPad * pad);
static GstFlowReturn gst_qtdemux_chain (GstPad * sinkpad, GstBuffer * inbuf);
static gboolean qtdemux_sink_activate (GstPad * sinkpad);
static gboolean qtdemux_sink_activate_pull (GstPad * sinkpad, gboolean active);
static gboolean qtdemux_sink_activate_push (GstPad * sinkpad, gboolean active);
static gboolean gst_qtdemux_handle_sink_event (GstPad * pad, GstEvent * event);

static void qtdemux_parse_moov (GstQTDemux * qtdemux, void *buffer, int length);
static void qtdemux_parse (GstQTDemux * qtdemux, GNode * node, void *buffer,
    int length);
static QtNodeType *qtdemux_type_get (guint32 fourcc);
static void qtdemux_node_dump (GstQTDemux * qtdemux, GNode * node);
static void qtdemux_parse_tree (GstQTDemux * qtdemux);
static void qtdemux_parse_udta (GstQTDemux * qtdemux, GNode * udta);
static void qtdemux_tag_add_str (GstQTDemux * qtdemux, const char *tag,
    GNode * node);
static void qtdemux_tag_add_num (GstQTDemux * qtdemux, const char *tag1,
    const char *tag2, GNode * node);
static void qtdemux_tag_add_gnre (GstQTDemux * qtdemux, const char *tag,
    GNode * node);

static void gst_qtdemux_handle_esds (GstQTDemux * qtdemux,
    QtDemuxStream * stream, GNode * esds);
static GstCaps *qtdemux_video_caps (GstQTDemux * qtdemux, guint32 fourcc,
    const guint8 * stsd_data, const gchar ** codec_name);
static GstCaps *qtdemux_audio_caps (GstQTDemux * qtdemux,
    QtDemuxStream * stream, guint32 fourcc, const guint8 * data, int len,
    const gchar ** codec_name);

static GType
gst_qtdemux_get_type (void)
{
  static GType qtdemux_type = 0;

  if (!qtdemux_type) {
    static const GTypeInfo qtdemux_info = {
      sizeof (GstQTDemuxClass),
      (GBaseInitFunc) gst_qtdemux_base_init, NULL,
      (GClassInitFunc) gst_qtdemux_class_init,
      NULL, NULL, sizeof (GstQTDemux), 0,
      (GInstanceInitFunc) gst_qtdemux_init,
    };

    qtdemux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstQTDemux", &qtdemux_info,
        0);
  }
  return qtdemux_type;
}

static void
gst_qtdemux_base_init (GstQTDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qtdemux_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qtdemux_videosrc_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_qtdemux_audiosrc_template));
  gst_element_class_set_details (element_class, &gst_qtdemux_details);

}

static void
gst_qtdemux_class_init (GstQTDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_qtdemux_dispose;

  gstelement_class->change_state = gst_qtdemux_change_state;
}

static void
gst_qtdemux_init (GstQTDemux * qtdemux)
{
  qtdemux->sinkpad =
      gst_pad_new_from_static_template (&gst_qtdemux_sink_template, "sink");
  gst_pad_set_activate_function (qtdemux->sinkpad, qtdemux_sink_activate);
  gst_pad_set_activatepull_function (qtdemux->sinkpad,
      qtdemux_sink_activate_pull);
  gst_pad_set_activatepush_function (qtdemux->sinkpad,
      qtdemux_sink_activate_push);
  gst_pad_set_chain_function (qtdemux->sinkpad, gst_qtdemux_chain);
  gst_pad_set_event_function (qtdemux->sinkpad, gst_qtdemux_handle_sink_event);
  gst_element_add_pad (GST_ELEMENT (qtdemux), qtdemux->sinkpad);

  qtdemux->state = QTDEMUX_STATE_INITIAL;
  /* FIXME, use segment last_stop for this */
  qtdemux->last_ts = GST_CLOCK_TIME_NONE;
  qtdemux->pullbased = FALSE;
  qtdemux->neededbytes = 16;
  qtdemux->todrop = 0;
  qtdemux->adapter = gst_adapter_new ();
  qtdemux->offset = 0;
  qtdemux->mdatoffset = GST_CLOCK_TIME_NONE;
  qtdemux->mdatbuffer = NULL;
  gst_segment_init (&qtdemux->segment, GST_FORMAT_TIME);
}

static void
gst_qtdemux_dispose (GObject * object)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (object);

  if (qtdemux->adapter) {
    g_object_unref (G_OBJECT (qtdemux->adapter));
    qtdemux->adapter = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

#if 0
static gboolean
gst_qtdemux_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  QtDemuxStream *stream = gst_pad_get_element_private (pad);

  if (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e') &&
      (src_format == GST_FORMAT_BYTES || *dest_format == GST_FORMAT_BYTES))
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        case GST_FORMAT_DEFAULT:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_TIME:
          *dest_value = src_value * 1;  /* FIXME */
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}
#endif

static const GstQueryType *
gst_qtdemux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return src_types;
}

static gboolean
gst_qtdemux_handle_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = FALSE;
  GstQTDemux *qtdemux = GST_QTDEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
      if (GST_CLOCK_TIME_IS_VALID (qtdemux->segment.last_stop)) {
        gst_query_set_position (query, GST_FORMAT_TIME,
            qtdemux->segment.last_stop);
        res = TRUE;
      }
      break;
    case GST_QUERY_DURATION:
      if (qtdemux->pullbased && qtdemux->duration != 0
          && qtdemux->timescale != 0) {
        gint64 duration;

        duration = gst_util_uint64_scale_int (qtdemux->duration,
            GST_SECOND, qtdemux->timescale);

        gst_query_set_duration (query, GST_FORMAT_TIME, duration);
        res = TRUE;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (qtdemux);

  return res;
}

/* push event on all source pads; takes ownership of the event */
static void
gst_qtdemux_push_event (GstQTDemux * qtdemux, GstEvent * event)
{
  guint n;

  GST_DEBUG_OBJECT (qtdemux, "pushing %s event on all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (n = 0; n < qtdemux->n_streams; n++) {
    gst_pad_push_event (qtdemux->streams[n]->pad, gst_event_ref (event));
  }
  gst_event_unref (event);
}

/* find the index of the sample that includes the data for @media_time
 *
 * Returns the index of the sample or n_samples when the sample was not
 * found.
 */
/* FIXME, binary search would be nice here */
static guint32
gst_qtdemux_find_index (GstQTDemux * qtdemux, QtDemuxStream * str,
    guint64 media_time)
{
  guint32 i;

  if (str->n_samples == 0)
    return 0;

  for (i = 0; i < str->n_samples; i++) {
    if (str->samples[i].timestamp > media_time) {
      /* first sample after media_time, we need the previous one */
      return (i == 0 ? 0 : i - 1);
    }
  }
  return str->n_samples - 1;
}

/* find the index of the keyframe needed to decode the sample at @index
 * of stream @str.
 *
 * Returns the index of the keyframe.
 */
static guint32
gst_qtdemux_find_keyframe (GstQTDemux * qtdemux, QtDemuxStream * str,
    guint32 index)
{
  if (index >= str->n_samples)
    return str->n_samples;

  /* all keyframes, return index */
  if (str->all_keyframe)
    return index;

  /* else go back until we have a keyframe */
  while (TRUE) {
    if (str->samples[index].keyframe)
      break;

    if (index == 0)
      break;

    index--;
  }
  return index;
}

/* find the segment for @time_position for @stream
 *
 * Returns -1 if the segment cannot be found.
 */
static guint32
gst_qtdemux_find_segment (GstQTDemux * qtdemux, QtDemuxStream * stream,
    guint64 time_position)
{
  gint i;
  guint32 seg_idx;

  /* find segment corresponding to time_position if we are looking
   * for a segment. */
  seg_idx = -1;
  for (i = 0; i < stream->n_segments; i++) {
    QtDemuxSegment *segment = &stream->segments[i];

    if (segment->time <= time_position && time_position < segment->stop_time) {
      seg_idx = i;
      break;
    }
  }
  return seg_idx;
}

/* move the stream @str to the sample position @index.
 *
 * Updates @str->sample_index and marks discontinuity if needed.
 */
static void
gst_qtdemux_move_stream (GstQTDemux * qtdemux, QtDemuxStream * str,
    guint32 index)
{
  /* no change needed */
  if (index == str->sample_index)
    return;

  GST_DEBUG_OBJECT (qtdemux, "moving to sample %u of %u", index,
      str->n_samples);

  /* position changed, we have a discont */
  str->sample_index = index;
  str->discont = TRUE;
}

/* perform the seek.
 *
 * We set all segment_indexes in the streams to unknown and
 * adjust the time_position to the desired position. this is enough
 * to trigger a segment switch in the streaming thread to start
 * streaming from the desired position.
 *
 * Keyframe seeking is a little more complicated when dealing with
 * segments. Ideally we want to move to the previous keyframe in 
 * the segment but there might not be a keyframe in the segment. In
 * fact, none of the segments could contain a keyframe. We take a
 * practical approach: seek to the previous keyframe in the segment,
 * if there is none, seek to the beginning of the segment.
 */
static gboolean
gst_qtdemux_perform_seek (GstQTDemux * qtdemux, GstSegment * segment)
{
  gint64 desired_offset;
  gint n;

  desired_offset = segment->last_stop;

  GST_DEBUG_OBJECT (qtdemux, "seeking to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (desired_offset));

  if (segment->flags & GST_SEEK_FLAG_KEY_UNIT) {
    guint64 min_offset;

    min_offset = desired_offset;

    /* for each stream, find the index of the sample in the segment
     * and move back to the previous keyframe. */
    for (n = 0; n < qtdemux->n_streams; n++) {
      QtDemuxStream *str;
      guint32 index;
      guint32 seg_idx;
      guint64 media_start;
      guint64 media_time;
      guint64 seg_time;
      QtDemuxSegment *seg;

      str = qtdemux->streams[n];

      seg_idx = gst_qtdemux_find_segment (qtdemux, str, desired_offset);
      GST_DEBUG_OBJECT (qtdemux, "align segment %d", seg_idx);

      /* segment not found, continue with normal flow */
      if (seg_idx == -1)
        continue;

      /* get segment and time in the segment */
      seg = &str->segments[seg_idx];
      seg_time = desired_offset - seg->time;

      /* get the media time in the segment */
      media_start = seg->media_start + seg_time;

      /* get the index of the sample with media time */
      index = gst_qtdemux_find_index (qtdemux, str, media_start);
      GST_DEBUG_OBJECT (qtdemux, "sample for %" GST_TIME_FORMAT " at %u",
          GST_TIME_ARGS (media_start), index);
      /* find previous keyframe */
      index = gst_qtdemux_find_keyframe (qtdemux, str, index);
      /* get timestamp of keyframe */
      media_time = str->samples[index].timestamp;

      GST_DEBUG_OBJECT (qtdemux, "keyframe at %u with time %" GST_TIME_FORMAT,
          index, GST_TIME_ARGS (media_time));

      /* keyframes in the segment get a chance to change the
       * desired_offset. keyframes out of the segment are
       * ignored. */
      if (media_time >= seg->media_start) {
        guint64 seg_time;

        /* this keyframe is inside the segment, convert back to
         * segment time */
        seg_time = (media_time - seg->media_start) + seg->time;
        if (seg_time < min_offset)
          min_offset = seg_time;
      }
    }
    desired_offset = min_offset;

    GST_DEBUG_OBJECT (qtdemux, "keyframe seek, align to %"
        GST_TIME_FORMAT, GST_TIME_ARGS (desired_offset));
  }

  /* and set all streams to the final position */
  for (n = 0; n < qtdemux->n_streams; n++) {
    QtDemuxStream *stream = qtdemux->streams[n];

    stream->time_position = desired_offset;
    stream->sample_index = 0;
    stream->segment_index = -1;
  }
  segment->last_stop = desired_offset;
  segment->time = desired_offset;

  /* we stop at the end */
  if (segment->stop == -1)
    segment->stop = segment->duration;

  return TRUE;
}

/* do a seek in pull based mode */
static gboolean
gst_qtdemux_do_seek (GstQTDemux * qtdemux, GstPad * pad, GstEvent * event)
{
  gdouble rate;
  GstFormat format;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  gint64 cur, stop;
  gboolean flush;
  gboolean res;
  gboolean update;
  GstSegment seeksegment;

  if (event) {
    GST_DEBUG_OBJECT (qtdemux, "doing seek with event");

    gst_event_parse_seek (event, &rate, &format, &flags,
        &cur_type, &cur, &stop_type, &stop);

    /* we have to have a format as the segment format. Try to convert
     * if not. */
    if (format != GST_FORMAT_TIME) {
      GstFormat fmt;

      fmt = GST_FORMAT_TIME;
      res = TRUE;
      if (cur_type != GST_SEEK_TYPE_NONE)
        res = gst_pad_query_convert (pad, format, cur, &fmt, &cur);
      if (res && stop_type != GST_SEEK_TYPE_NONE)
        res = gst_pad_query_convert (pad, format, stop, &fmt, &stop);
      if (!res)
        goto no_format;

      format = fmt;
    }
  } else {
    GST_DEBUG_OBJECT (qtdemux, "doing seek without event");
    flags = 0;
  }

  flush = flags & GST_SEEK_FLAG_FLUSH;

  GST_DEBUG_OBJECT (qtdemux, "seek format %d", format);

  /* stop streaming, either by flushing or by pausing the task */
  if (flush) {
    /* unlock upstream pull_range */
    gst_pad_push_event (qtdemux->sinkpad, gst_event_new_flush_start ());
    /* make sure out loop function exits */
    gst_qtdemux_push_event (qtdemux, gst_event_new_flush_start ());
  } else {
    /* non flushing seek, pause the task */
    qtdemux->segment_running = FALSE;
    gst_pad_pause_task (qtdemux->sinkpad);
  }

  /* wait for streaming to finish */
  GST_PAD_STREAM_LOCK (qtdemux->sinkpad);

  /* copy segment, we need this because we still need the old
   * segment when we close the current segment. */
  memcpy (&seeksegment, &qtdemux->segment, sizeof (GstSegment));

  if (event) {
    /* configure the segment with the seek variables */
    GST_DEBUG_OBJECT (qtdemux, "configuring seek");
    gst_segment_set_seek (&seeksegment, rate, format, flags,
        cur_type, cur, stop_type, stop, &update);
  }

  /* now do the seek, this actually never returns FALSE */
  res = gst_qtdemux_perform_seek (qtdemux, &seeksegment);

  /* prepare for streaming again */
  if (flush) {
    gst_pad_push_event (qtdemux->sinkpad, gst_event_new_flush_stop ());
    gst_qtdemux_push_event (qtdemux, gst_event_new_flush_stop ());
  } else if (qtdemux->segment_running) {
    /* we are running the current segment and doing a non-flushing seek,
     * close the segment first based on the last_stop. */
    GST_DEBUG_OBJECT (qtdemux, "closing running segment %" G_GINT64_FORMAT
        " to %" G_GINT64_FORMAT, qtdemux->segment.start,
        qtdemux->segment.last_stop);

    gst_qtdemux_push_event (qtdemux,
        gst_event_new_new_segment (TRUE,
            qtdemux->segment.rate, qtdemux->segment.format,
            qtdemux->segment.start, qtdemux->segment.last_stop,
            qtdemux->segment.time));
  }

  /* commit the new segment */
  memcpy (&qtdemux->segment, &seeksegment, sizeof (GstSegment));

  if (qtdemux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    gst_element_post_message (GST_ELEMENT_CAST (qtdemux),
        gst_message_new_segment_start (GST_OBJECT_CAST (qtdemux),
            qtdemux->segment.format, qtdemux->segment.last_stop));
  }

  /* restart streaming, NEWSEGMENT will be sent from the streaming
   * thread. */
  qtdemux->segment_running = TRUE;
  gst_pad_start_task (qtdemux->sinkpad, (GstTaskFunction) gst_qtdemux_loop,
      qtdemux->sinkpad);

  GST_PAD_STREAM_UNLOCK (qtdemux->sinkpad);

  return TRUE;

  /* ERRORS */
no_format:
  {
    GST_DEBUG_OBJECT (qtdemux, "unsupported format given, seek aborted.");
    return FALSE;
  }
}

static gboolean
gst_qtdemux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstQTDemux *qtdemux = GST_QTDEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_qtdemux_do_seek (qtdemux, pad, event);
      break;
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (qtdemux);

  gst_event_unref (event);

  return res;
}

GST_DEBUG_CATEGORY (qtdemux_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (qtdemux_debug, "qtdemux", 0, "qtdemux plugin");

  return gst_element_register (plugin, "qtdemux",
      GST_RANK_PRIMARY, GST_TYPE_QTDEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "qtdemux",
    "Quicktime stream demuxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

static gboolean
gst_qtdemux_handle_sink_event (GstPad * sinkpad, GstEvent * event)
{
  GstQTDemux *demux = GST_QTDEMUX (GST_PAD_PARENT (sinkpad));
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      /* We need to convert it to a GST_FORMAT_TIME new segment */
    default:
      gst_pad_event_default (demux->sinkpad, event);
      return TRUE;
  }

  gst_event_unref (event);
  return res;
}

static GstStateChangeReturn
gst_qtdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gint n;

      qtdemux->state = QTDEMUX_STATE_INITIAL;
      qtdemux->last_ts = GST_CLOCK_TIME_NONE;
      qtdemux->neededbytes = 16;
      qtdemux->todrop = 0;
      qtdemux->pullbased = FALSE;
      qtdemux->offset = 0;
      qtdemux->mdatoffset = GST_CLOCK_TIME_NONE;
      if (qtdemux->mdatbuffer)
        gst_buffer_unref (qtdemux->mdatbuffer);
      qtdemux->mdatbuffer = NULL;
      gst_adapter_clear (qtdemux->adapter);
      for (n = 0; n < qtdemux->n_streams; n++) {
        gst_element_remove_pad (element, qtdemux->streams[n]->pad);
        g_free (qtdemux->streams[n]->samples);
        if (qtdemux->streams[n]->caps)
          gst_caps_unref (qtdemux->streams[n]->caps);
        g_free (qtdemux->streams[n]);
        g_free (qtdemux->streams[n]->segments);
      }
      qtdemux->n_streams = 0;
      gst_segment_init (&qtdemux->segment, GST_FORMAT_TIME);
      break;
    }
    default:
      break;
  }

  return result;
}

static void
extract_initial_length_and_fourcc (guint8 * data, guint32 * plength,
    guint32 * pfourcc)
{
  guint32 length;
  guint32 fourcc;

  length = GST_READ_UINT32_BE (data);
  GST_DEBUG ("length %08x", length);
  fourcc = GST_READ_UINT32_LE (data + 4);
  GST_DEBUG ("atom type %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));

  if (length == 0) {
    length = G_MAXUINT32;
  }
  if (length == 1) {
    /* this means we have an extended size, which is the 64 bit value of
     * the next 8 bytes */
    guint32 length1, length2;

    length1 = GST_READ_UINT32_BE (data + 8);
    GST_DEBUG ("length1 %08x", length1);
    length2 = GST_READ_UINT32_BE (data + 12);
    GST_DEBUG ("length2 %08x", length2);

    /* FIXME: I guess someone didn't want to make 64 bit size work :) */
    length = length2;
  }

  if (plength)
    *plength = length;
  if (pfourcc)
    *pfourcc = fourcc;
}

static GstFlowReturn
gst_qtdemux_loop_state_header (GstQTDemux * qtdemux)
{
  guint32 length;
  guint32 fourcc;
  GstBuffer *buf = NULL;
  GstFlowReturn ret = GST_FLOW_OK;
  guint64 cur_offset = qtdemux->offset;

  ret = gst_pad_pull_range (qtdemux->sinkpad, cur_offset, 16, &buf);
  if (ret != GST_FLOW_OK)
    goto beach;
  extract_initial_length_and_fourcc (GST_BUFFER_DATA (buf), &length, &fourcc);
  gst_buffer_unref (buf);


  switch (fourcc) {
    case GST_MAKE_FOURCC ('m', 'd', 'a', 't'):
    case GST_MAKE_FOURCC ('f', 'r', 'e', 'e'):
    case GST_MAKE_FOURCC ('w', 'i', 'd', 'e'):
    case GST_MAKE_FOURCC ('P', 'I', 'C', 'T'):
    case GST_MAKE_FOURCC ('p', 'n', 'o', 't'):
      goto ed_edd_and_eddy;
    case GST_MAKE_FOURCC ('m', 'o', 'o', 'v'):{
      GstBuffer *moov;

      ret = gst_pad_pull_range (qtdemux->sinkpad, cur_offset, length, &moov);
      if (ret != GST_FLOW_OK)
        goto beach;
      if (length != GST_BUFFER_SIZE (moov)) {
        GST_WARNING_OBJECT (qtdemux,
            "We got less than expected (received %d, wanted %d)",
            GST_BUFFER_SIZE (moov), length);
        ret = GST_FLOW_ERROR;
        goto beach;
      }
      cur_offset += length;
      qtdemux->offset += length;

      qtdemux_parse_moov (qtdemux, GST_BUFFER_DATA (moov), length);
      if (1) {
        qtdemux_node_dump (qtdemux, qtdemux->moov_node);
      }
      qtdemux_parse_tree (qtdemux);
      g_node_destroy (qtdemux->moov_node);
      gst_buffer_unref (moov);
      qtdemux->moov_node = NULL;
      qtdemux->state = QTDEMUX_STATE_MOVIE;
      GST_DEBUG_OBJECT (qtdemux, "switching state to STATE_MOVIE (%d)",
          qtdemux->state);
    }
      break;
    ed_edd_and_eddy:
    default:{
      GST_LOG ("unknown %08x '%" GST_FOURCC_FORMAT "' at %d",
          fourcc, GST_FOURCC_ARGS (fourcc), cur_offset);
      cur_offset += length;
      qtdemux->offset += length;
      break;
    }
  }

beach:
  return ret;
}

/* activate the given segment number @seg_idx of @stream at time @offset.
 * @offset is an absolute global position over all the segments.
 *
 * This will push out a NEWSEGMENT event with the right values and
 * position the stream index to the first decodable sample before 
 * @offset.
 */
static gboolean
gst_qtdemux_activate_segment (GstQTDemux * qtdemux, QtDemuxStream * stream,
    guint32 seg_idx, guint64 offset)
{
  GstEvent *event;
  QtDemuxSegment *segment;
  guint32 index, kf_index;
  guint64 seg_time;
  guint64 start, stop;

  /* update the current segment */
  stream->segment_index = seg_idx;

  /* get the segment */
  segment = &stream->segments[seg_idx];

  if (offset < segment->time)
    return FALSE;

  /* get time in this segment */
  seg_time = offset - segment->time;

  if (seg_time >= segment->duration)
    return FALSE;

  /* calc media start/stop */
  start = segment->media_start + seg_time;
  stop = segment->media_stop;

  GST_DEBUG_OBJECT (qtdemux, "newsegment %d from %" GST_TIME_FORMAT
      " to %" GST_TIME_FORMAT ", time %" GST_TIME_FORMAT, seg_idx,
      GST_TIME_ARGS (start), GST_TIME_ARGS (stop), GST_TIME_ARGS (offset));

  event = gst_event_new_new_segment (FALSE, segment->rate, GST_FORMAT_TIME,
      start, stop, offset);

  gst_pad_push_event (stream->pad, event);

  /* and move to the keyframe before the indicated media time of the
   * segment */
  index = gst_qtdemux_find_index (qtdemux, stream, start);

  GST_DEBUG_OBJECT (qtdemux, "moving data pointer to %" GST_TIME_FORMAT
      ", index: %u", GST_TIME_ARGS (start), index);

  /* we're at the right spot */
  if (index == stream->sample_index)
    return TRUE;

  /* find keyframe of the target index */
  kf_index = gst_qtdemux_find_keyframe (qtdemux, stream, index);

  /* if we move forwards, we don't have to go back to the previous
   * keyframe since we already sent that. We can also just jump to
   * the keyframe right before the target index if there is one. */
  if (index > stream->sample_index) {
    /* moving forwards check if we move past a keyframe */
    if (kf_index > stream->sample_index) {
      GST_DEBUG_OBJECT (qtdemux, "moving forwards to keyframe at %u", kf_index);
      gst_qtdemux_move_stream (qtdemux, stream, kf_index);
    } else {
      GST_DEBUG_OBJECT (qtdemux, "moving forwards, keyframe at %u already sent",
          kf_index);
    }
  } else {
    GST_DEBUG_OBJECT (qtdemux, "moving backwards to keyframe at %u", kf_index);
    gst_qtdemux_move_stream (qtdemux, stream, kf_index);
  }

  return TRUE;
}

/* prepare to get the current sample of @stream, getting essential values.
 * 
 * This function will also prepare and send the segment when needed.
 *
 * Return FALSE if the stream is EOS.
 */
static gboolean
gst_qtdemux_prepare_current_sample (GstQTDemux * qtdemux,
    QtDemuxStream * stream, guint64 * offset, guint * size, guint64 * timestamp,
    guint32 * duration, gboolean * keyframe)
{
  QtDemuxSample *sample;
  guint64 time_position;
  guint32 seg_idx;

  g_return_val_if_fail (stream != NULL, FALSE);

  time_position = stream->time_position;
  if (time_position == -1)
    goto eos;

  seg_idx = stream->segment_index;
  if (seg_idx == -1) {
    /* find segment corresponding to time_position if we are looking
     * for a segment. */
    seg_idx = gst_qtdemux_find_segment (qtdemux, stream, time_position);

    /* nothing found, we're really eos */
    if (seg_idx == -1)
      goto eos;
  }

  /* different segment, activate it, sample_index will be set. */
  if (stream->segment_index != seg_idx)
    gst_qtdemux_activate_segment (qtdemux, stream, seg_idx, time_position);

  /* now get the info for the sample we're at */
  sample = &stream->samples[stream->sample_index];

  *offset = sample->offset;
  *size = sample->size;
  *timestamp = sample->timestamp;
  *duration = sample->duration;
  *keyframe = stream->all_keyframe || sample->keyframe;

  return TRUE;

  /* special cases */
eos:
  {
    stream->time_position = -1;
    return FALSE;
  }
}

/* move to the next sample in @stream.
 *
 * Moves to the next segment when needed.
 */
static void
gst_qtdemux_advance_sample (GstQTDemux * qtdemux, QtDemuxStream * stream)
{
  QtDemuxSample *sample;
  QtDemuxSegment *segment;

  /* move to next sample */
  stream->sample_index++;

  /* get current segment */
  segment = &stream->segments[stream->segment_index];

  /* reached the last sample, we need the next segment */
  if (stream->sample_index == stream->n_samples)
    goto next_segment;

  /* get next sample */
  sample = &stream->samples[stream->sample_index];

  /* see if we are past the segment */
  if (sample->timestamp >= segment->media_stop)
    goto next_segment;

  if (sample->timestamp >= segment->media_start) {
    /* inside the segment, update time_position, looks very familiar to
     * GStreamer segments, doesn't it? */
    stream->time_position =
        (sample->timestamp - segment->media_start) + segment->time;
  } else {
    /* not yet in segment, time does not yet increment. This means
     * that we are still prerolling keyframes to the decoder so it can
     * decode the first sample of the segment. */
    stream->time_position = segment->time;
  }
  return;

  /* move to the next segment */
next_segment:
  {
    GST_DEBUG_OBJECT (qtdemux, "segment %d ended ", stream->segment_index);

    if (stream->segment_index == stream->n_segments - 1) {
      /* are we at the end of the last segment, we're EOS */
      stream->time_position = -1;
    } else {
      /* else we're only at the end of the current segment */
      stream->time_position = segment->stop_time;
    }
    /* make sure we select a new segment */
    stream->segment_index = -1;
  }
}

static GstFlowReturn
gst_qtdemux_loop_state_movie (GstQTDemux * qtdemux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf = NULL;
  QtDemuxStream *stream;
  guint64 min_time;
  guint64 offset;
  guint64 timestamp;
  guint32 duration;
  gboolean keyframe;
  guint size;
  gint index;
  gint i;

  /* Figure out the next stream sample to output, min_time is expressed in
   * global time and runs over the edit list segments. */
  min_time = G_MAXUINT64;
  index = -1;
  for (i = 0; i < qtdemux->n_streams; i++) {
    guint64 position;

    stream = qtdemux->streams[i];
    position = stream->time_position;

    /* position of -1 is EOS */
    if (position != -1 && position < min_time) {
      min_time = position;
      index = i;
    }
  }
  /* all are EOS */
  if (index == -1)
    goto eos;

  /* check for segment end */
  if (qtdemux->segment.stop != -1 && qtdemux->segment.stop < min_time)
    goto eos;

  stream = qtdemux->streams[index];

  /* fetch info for the current sample of this stream */
  if (!gst_qtdemux_prepare_current_sample (qtdemux, stream, &offset, &size,
          &timestamp, &duration, &keyframe))
    goto eos;

  GST_LOG_OBJECT (qtdemux,
      "pushing from stream %d, offset=%" G_GUINT64_FORMAT
      ",size=%d timestamp=%" GST_TIME_FORMAT,
      index, offset, size, GST_TIME_ARGS (timestamp));

  /* hmm, empty sample, skip and move to next sample */
  if (G_UNLIKELY (size <= 0))
    goto next;

  GST_LOG_OBJECT (qtdemux, "reading %d bytes @ %" G_GUINT64_FORMAT, size,
      offset);

  ret = gst_pad_pull_range (qtdemux->sinkpad, offset, size, &buf);
  if (ret != GST_FLOW_OK)
    goto beach;

  /* we're going to modify the metadata */
  buf = gst_buffer_make_metadata_writable (buf);

  if (stream->discont) {
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DISCONT);
    stream->discont = FALSE;
  }

  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;

  qtdemux->last_ts = min_time;
  gst_segment_set_last_stop (&qtdemux->segment, GST_FORMAT_TIME, min_time);

  if (!keyframe)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT);

  GST_LOG_OBJECT (qtdemux,
      "Pushing buffer with time %" GST_TIME_FORMAT " on pad %p",
      GST_TIME_ARGS (timestamp), stream->pad);

  gst_buffer_set_caps (buf, stream->caps);

  ret = gst_pad_push (stream->pad, buf);

next:
  gst_qtdemux_advance_sample (qtdemux, stream);

beach:
  return ret;

  /* special cases */
eos:
  {
    GST_DEBUG_OBJECT (qtdemux, "No samples left for any streams - EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto beach;
  }
}

static void
gst_qtdemux_loop (GstPad * pad)
{
  GstQTDemux *qtdemux;
  guint64 cur_offset;
  GstFlowReturn ret;

  qtdemux = GST_QTDEMUX (gst_pad_get_parent (pad));

  cur_offset = qtdemux->offset;
  GST_LOG_OBJECT (qtdemux, "loop at position %" G_GUINT64_FORMAT ", state %d",
      cur_offset, qtdemux->state);

  switch (qtdemux->state) {
    case QTDEMUX_STATE_INITIAL:
    case QTDEMUX_STATE_HEADER:
      ret = gst_qtdemux_loop_state_header (qtdemux);
      break;
    case QTDEMUX_STATE_MOVIE:
      ret = gst_qtdemux_loop_state_movie (qtdemux);
      break;
    default:
      /* ouch */
      goto invalid_state;
  }

  /* if all is fine, continue */
  if (G_LIKELY (ret == GST_FLOW_OK))
    goto done;

  /* we don't care about unlinked pads */
  if (ret == GST_FLOW_NOT_LINKED)
    goto done;

  /* other errors make us stop */
  GST_LOG_OBJECT (qtdemux, "pausing task, reason %s", gst_flow_get_name (ret));

  qtdemux->segment_running = FALSE;
  gst_pad_pause_task (pad);

  /* fatal errors need special actions */
  if (GST_FLOW_IS_FATAL (ret)) {
    /* check EOS */
    if (ret == GST_FLOW_UNEXPECTED) {
      if (qtdemux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
        gint64 stop;

        if ((stop = qtdemux->segment.stop) == -1)
          stop = qtdemux->segment.duration;

        GST_LOG_OBJECT (qtdemux, "Sending segment done, at end of segment");
        gst_element_post_message (GST_ELEMENT_CAST (qtdemux),
            gst_message_new_segment_done (GST_OBJECT_CAST (qtdemux),
                GST_FORMAT_TIME, stop));
      } else {
        GST_LOG_OBJECT (qtdemux, "Sending EOS at end of segment");
        gst_qtdemux_push_event (qtdemux, gst_event_new_eos ());
      }
    } else {
      gst_qtdemux_push_event (qtdemux, gst_event_new_eos ());
      GST_ELEMENT_ERROR (qtdemux, STREAM, FAILED,
          (NULL), ("streaming stopped, reason %s", gst_flow_get_name (ret)));
    }
  }

done:
  gst_object_unref (qtdemux);
  return;

  /* ERRORS */
invalid_state:
  {
    GST_ELEMENT_ERROR (qtdemux, STREAM, FAILED,
        (NULL), ("streaming stopped, invalid state"));
    qtdemux->segment_running = FALSE;
    gst_pad_pause_task (pad);
    gst_qtdemux_push_event (qtdemux, gst_event_new_eos ());
    goto done;
  }
}

/*
  next_entry_size
  
  Returns the size of the first entry at the current offset.
  If -1, there are none (which means EOS or empty file).
*/

static guint64
next_entry_size (GstQTDemux * demux)
{
  QtDemuxStream *stream;
  int i;
  int smallidx = -1;
  guint64 smalloffs = -1;

  GST_LOG_OBJECT (demux, "Finding entry at offset %lld", demux->offset);

  for (i = 0; i < demux->n_streams; i++) {
    stream = demux->streams[i];

    GST_LOG_OBJECT (demux,
        "Checking Stream %d (sample_index:%d / offset:%lld / size:%d / chunk:%d)",
        i, stream->sample_index, stream->samples[stream->sample_index].offset,
        stream->samples[stream->sample_index].size,
        stream->samples[stream->sample_index].chunk);

    if (((smalloffs == -1)
            || (stream->samples[stream->sample_index].offset < smalloffs))
        && (stream->samples[stream->sample_index].size)) {
      smallidx = i;
      smalloffs = stream->samples[stream->sample_index].offset;
    }
  }

  GST_LOG_OBJECT (demux, "stream %d offset %lld demux->offset :%lld",
      smallidx, smalloffs, demux->offset);

  if (smallidx == -1)
    return -1;
  stream = demux->streams[smallidx];

  if (stream->samples[stream->sample_index].offset >= demux->offset) {
    demux->todrop =
        stream->samples[stream->sample_index].offset - demux->offset;
    return stream->samples[stream->sample_index].size + demux->todrop;
  }

  GST_DEBUG_OBJECT (demux, "There wasn't any entry at offset %lld",
      demux->offset);
  return -1;
}

static void
gst_qtdemux_post_buffering (GstQTDemux * demux, gint num, gint denom)
{
  gint perc = (gint) ((gdouble) num * 100.0 / (gdouble) denom);

  gst_element_post_message (GST_ELEMENT (demux),
      gst_message_new_custom (GST_MESSAGE_BUFFERING,
          GST_OBJECT (demux),
          gst_structure_new ("GstMessageBuffering",
              "buffer-percent", G_TYPE_INT, perc, NULL)));
}

/* FIXME, unverified after edit list updates */
static GstFlowReturn
gst_qtdemux_chain (GstPad * sinkpad, GstBuffer * inbuf)
{
  GstQTDemux *demux;
  GstFlowReturn ret = GST_FLOW_OK;

  demux = GST_QTDEMUX (gst_pad_get_parent (sinkpad));

  gst_adapter_push (demux->adapter, inbuf);

  GST_DEBUG_OBJECT (demux, "pushing in inbuf %p, neededbytes:%u, available:%u",
      inbuf, demux->neededbytes, gst_adapter_available (demux->adapter));

  while (((gst_adapter_available (demux->adapter)) >= demux->neededbytes) &&
      (ret == GST_FLOW_OK)) {

    GST_DEBUG_OBJECT (demux,
        "state:%d , demux->neededbytes:%d, demux->offset:%lld", demux->state,
        demux->neededbytes, demux->offset);

    switch (demux->state) {
      case QTDEMUX_STATE_INITIAL:{
        const guint8 *data;
        guint32 fourcc;
        guint32 size;

        data = gst_adapter_peek (demux->adapter, demux->neededbytes);

        /* get fourcc/length, set neededbytes */
        extract_initial_length_and_fourcc ((guint8 *) data, &size, &fourcc);
        GST_DEBUG_OBJECT (demux,
            "Peeking found [%" GST_FOURCC_FORMAT "] size:%ld",
            GST_FOURCC_ARGS (fourcc), size);
        if ((fourcc == GST_MAKE_FOURCC ('m', 'd', 'a', 't'))) {
          if (demux->n_streams > 0) {
            demux->state = QTDEMUX_STATE_MOVIE;
            demux->neededbytes = next_entry_size (demux);
          } else {
            demux->state = QTDEMUX_STATE_BUFFER_MDAT;
            demux->neededbytes = size;
            demux->mdatoffset = demux->offset;
          }
        } else {
          demux->neededbytes = size;
          demux->state = QTDEMUX_STATE_HEADER;
        }
        break;
      }
      case QTDEMUX_STATE_HEADER:{
        guint8 *data;
        guint32 fourcc;

        GST_DEBUG_OBJECT (demux, "In header");

        data = gst_adapter_take (demux->adapter, demux->neededbytes);

        /* parse the header */
        extract_initial_length_and_fourcc (data, NULL, &fourcc);
        if (fourcc == GST_MAKE_FOURCC ('m', 'o', 'o', 'v')) {
          GST_DEBUG_OBJECT (demux, "Parsing [moov]");

          qtdemux_parse_moov (demux, data, demux->neededbytes);
          qtdemux_node_dump (demux, demux->moov_node);
          qtdemux_parse_tree (demux);

          g_node_destroy (demux->moov_node);
          g_free (data);
          demux->moov_node = NULL;
        } else {
          GST_WARNING_OBJECT (demux,
              "Unknown fourcc while parsing header : %" GST_FOURCC_FORMAT,
              GST_FOURCC_ARGS (fourcc));
          /* Let's jump that one and go back to initial state */
        }

        GST_DEBUG_OBJECT (demux, "Finished parsing the header");
        if (demux->mdatbuffer && demux->n_streams) {
          /* the mdat was before the header */
          GST_DEBUG_OBJECT (demux, "We have n_streams:%d and mdatbuffer:%p",
              demux->n_streams, demux->mdatbuffer);
          gst_adapter_clear (demux->adapter);
          GST_DEBUG_OBJECT (demux, "mdatbuffer starts with %" GST_FOURCC_FORMAT,
              GST_FOURCC_ARGS (GST_READ_UINT32_BE (demux->mdatbuffer)));
          gst_adapter_push (demux->adapter, demux->mdatbuffer);
          demux->mdatbuffer = NULL;
          demux->offset = demux->mdatoffset;
          demux->neededbytes = next_entry_size (demux);
          demux->state = QTDEMUX_STATE_MOVIE;
        } else {
          GST_DEBUG_OBJECT (demux, "Carrying on normally");
          demux->offset += demux->neededbytes;
          demux->neededbytes = 16;
          demux->state = QTDEMUX_STATE_INITIAL;
        }

        break;
      }
      case QTDEMUX_STATE_BUFFER_MDAT:{
        GST_DEBUG_OBJECT (demux, "Got our buffer at offset %lld",
            demux->mdatoffset);
        if (demux->mdatbuffer)
          gst_buffer_unref (demux->mdatbuffer);
        demux->mdatbuffer = gst_buffer_new ();
        gst_buffer_set_data (demux->mdatbuffer,
            gst_adapter_take (demux->adapter, demux->neededbytes),
            demux->neededbytes);
        GST_DEBUG_OBJECT (demux, "mdatbuffer starts with %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (GST_READ_UINT32_BE (demux->mdatbuffer)));
        demux->offset += demux->neededbytes;
        demux->neededbytes = 16;
        demux->state = QTDEMUX_STATE_INITIAL;
        gst_qtdemux_post_buffering (demux, 1, 1);

        break;
      }
      case QTDEMUX_STATE_MOVIE:{
        guint8 *data;
        GstBuffer *outbuf;
        QtDemuxStream *stream = NULL;
        int i = -1;

        GST_DEBUG_OBJECT (demux, "BEGIN // in MOVIE for offset %lld",
            demux->offset);

        if (demux->todrop) {
          gst_adapter_flush (demux->adapter, demux->todrop);
          demux->neededbytes -= demux->todrop;
          demux->offset += demux->todrop;
        }

        /* Figure out which stream this is packet belongs to */
        for (i = 0; i < demux->n_streams; i++) {
          stream = demux->streams[i];
          GST_LOG_OBJECT (demux,
              "Checking stream %d (sample_index:%d / offset:%lld / size:%d / chunk:%d)",
              i, stream->sample_index,
              stream->samples[stream->sample_index].offset,
              stream->samples[stream->sample_index].size,
              stream->samples[stream->sample_index].chunk);

          if (stream->samples[stream->sample_index].offset == demux->offset)
            break;
        }

        if (stream == NULL)
          goto unknown_stream;

        /* first buffer? */
        /* FIXME : this should be handled in sink_event */
        if (demux->last_ts == GST_CLOCK_TIME_NONE) {
          gst_qtdemux_push_event (demux,
              gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
                  0, GST_CLOCK_TIME_NONE, 0));
        }

        /* get data */
        data = gst_adapter_take (demux->adapter, demux->neededbytes);

        /* Put data in a buffer, set timestamps, caps, ... */
        outbuf = gst_buffer_new ();
        gst_buffer_set_data (outbuf, data, demux->neededbytes);
        GST_DEBUG_OBJECT (demux, "stream : %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (stream->fourcc));

        GST_BUFFER_TIMESTAMP (outbuf) =
            stream->samples[stream->sample_index].timestamp;
        demux->last_ts = GST_BUFFER_TIMESTAMP (outbuf);
        GST_BUFFER_DURATION (outbuf) =
            stream->samples[stream->sample_index].duration;

        /* send buffer */
        GST_LOG_OBJECT (demux,
            "Pushing buffer with time %" GST_TIME_FORMAT " on pad %p",
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)), stream->pad);
        gst_buffer_set_caps (outbuf, stream->caps);
        ret = gst_pad_push (stream->pad, outbuf);

        stream->sample_index++;

        /* update current offset and figure out size of next buffer */
        GST_LOG_OBJECT (demux, "bumping offset:%lld up by %lld",
            demux->offset, demux->neededbytes);
        demux->offset += demux->neededbytes;
        GST_LOG_OBJECT (demux, "offset is now %lld", demux->offset);

        if ((demux->neededbytes = next_entry_size (demux)) == -1)
          goto eos;
        break;
      }
      default:
        goto invalid_state;
    }
  }

  /* when buffering movie data, at least show user something is happening */
  if (ret == GST_FLOW_OK && demux->state == QTDEMUX_STATE_BUFFER_MDAT &&
      gst_adapter_available (demux->adapter) <= demux->neededbytes) {
    gst_qtdemux_post_buffering (demux, gst_adapter_available (demux->adapter),
        demux->neededbytes);
  }

done:
  gst_object_unref (demux);

  return ret;

  /* ERRORS */
unknown_stream:
  {
    GST_ELEMENT_ERROR (demux, STREAM, FAILED, (NULL), ("unknown stream found"));
    ret = GST_FLOW_ERROR;
    goto done;
  }
eos:
  {
    GST_DEBUG_OBJECT (demux, "no next entry, EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto done;
  }
invalid_state:
  {
    GST_ELEMENT_ERROR (demux, STREAM, FAILED,
        (NULL), ("qtdemuxer invalid state %d", demux->state));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static gboolean
qtdemux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);
  else
    return gst_pad_activate_push (sinkpad, TRUE);
}

static gboolean
qtdemux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  GstQTDemux *demux = GST_QTDEMUX (GST_PAD_PARENT (sinkpad));

  if (active) {
    /* if we have a scheduler we can start the task */
    demux->pullbased = TRUE;
    demux->segment_running = TRUE;
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_qtdemux_loop, sinkpad);
  } else {
    demux->segment_running = FALSE;
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

static gboolean
qtdemux_sink_activate_push (GstPad * sinkpad, gboolean active)
{
  GstQTDemux *demux = GST_QTDEMUX (GST_PAD_PARENT (sinkpad));

  demux->pullbased = FALSE;

  return TRUE;
}

static void
gst_qtdemux_add_stream (GstQTDemux * qtdemux,
    QtDemuxStream * stream, GstTagList * list)
{
  if (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e')) {
    gchar *name = g_strdup_printf ("video_%02d", qtdemux->n_video_streams);

    stream->pad =
        gst_pad_new_from_static_template (&gst_qtdemux_videosrc_template, name);
    g_free (name);

    /* guess fps, FIXME */
    if ((stream->n_samples == 1) && (stream->samples[0].duration == 0)) {
      stream->fps_n = 0;
      stream->fps_d = 1;
    } else {
      stream->fps_n = stream->timescale;
      if (stream->samples[0].duration == 0)
        stream->fps_d = 1;
      else
        stream->fps_d = stream->samples[0].duration;
    }

    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "width", G_TYPE_INT, stream->width,
          "height", G_TYPE_INT, stream->height,
          "framerate", GST_TYPE_FRACTION, stream->fps_n, stream->fps_d, NULL);
      if ((stream->bits_per_sample & 0x1F) == 8) {
        const guint32 *palette_data = NULL;

        if ((stream->bits_per_sample & 0x20) != 0)
          palette_data = ff_qt_grayscale_palette_256;
        if ((stream->color_table_id & 0x08) != 0)
          palette_data = ff_qt_default_palette_256;

        if (palette_data) {
          GstBuffer *palette = gst_buffer_new ();

          GST_BUFFER_DATA (palette) = (guint8 *) palette_data;
          GST_BUFFER_SIZE (palette) = sizeof (guint32) * 256;
          gst_caps_set_simple (stream->caps, "palette_data",
              GST_TYPE_BUFFER, palette, NULL);
          gst_buffer_unref (palette);
        }
      }
    }
    qtdemux->n_video_streams++;
  } else {
    gchar *name = g_strdup_printf ("audio_%02d", qtdemux->n_audio_streams);

    stream->pad =
        gst_pad_new_from_static_template (&gst_qtdemux_audiosrc_template, name);
    g_free (name);
    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, stream->n_channels, NULL);
    }
    qtdemux->n_audio_streams++;
  }

  gst_pad_use_fixed_caps (stream->pad);

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  qtdemux->streams[qtdemux->n_streams] = stream;
  qtdemux->n_streams++;
  GST_DEBUG_OBJECT (qtdemux, "n_streams is now %d", qtdemux->n_streams);

  gst_pad_set_event_function (stream->pad, gst_qtdemux_handle_src_event);
  gst_pad_set_query_type_function (stream->pad,
      gst_qtdemux_get_src_query_types);
  gst_pad_set_query_function (stream->pad, gst_qtdemux_handle_src_query);

  GST_DEBUG_OBJECT (qtdemux, "setting caps %" GST_PTR_FORMAT, stream->caps);
  gst_pad_set_caps (stream->pad, stream->caps);

  GST_DEBUG_OBJECT (qtdemux, "adding pad %s %p to qtdemux %p",
      GST_OBJECT_NAME (stream->pad), stream->pad, qtdemux);
  gst_element_add_pad (GST_ELEMENT (qtdemux), stream->pad);
  if (list) {
    gst_element_found_tags_for_pad (GST_ELEMENT (qtdemux), stream->pad, list);
  }
}


#define QT_CONTAINER 1

#define FOURCC_moov     GST_MAKE_FOURCC('m','o','o','v')
#define FOURCC_mvhd     GST_MAKE_FOURCC('m','v','h','d')
#define FOURCC_clip     GST_MAKE_FOURCC('c','l','i','p')
#define FOURCC_trak     GST_MAKE_FOURCC('t','r','a','k')
#define FOURCC_udta     GST_MAKE_FOURCC('u','d','t','a')
#define FOURCC_ctab     GST_MAKE_FOURCC('c','t','a','b')
#define FOURCC_tkhd     GST_MAKE_FOURCC('t','k','h','d')
#define FOURCC_crgn     GST_MAKE_FOURCC('c','r','g','n')
#define FOURCC_matt     GST_MAKE_FOURCC('m','a','t','t')
#define FOURCC_kmat     GST_MAKE_FOURCC('k','m','a','t')
#define FOURCC_edts     GST_MAKE_FOURCC('e','d','t','s')
#define FOURCC_elst     GST_MAKE_FOURCC('e','l','s','t')
#define FOURCC_load     GST_MAKE_FOURCC('l','o','a','d')
#define FOURCC_tref     GST_MAKE_FOURCC('t','r','e','f')
#define FOURCC_imap     GST_MAKE_FOURCC('i','m','a','p')
#define FOURCC___in     GST_MAKE_FOURCC(' ',' ','i','n')
#define FOURCC___ty     GST_MAKE_FOURCC(' ',' ','t','y')
#define FOURCC_mdia     GST_MAKE_FOURCC('m','d','i','a')
#define FOURCC_mdhd     GST_MAKE_FOURCC('m','d','h','d')
#define FOURCC_hdlr     GST_MAKE_FOURCC('h','d','l','r')
#define FOURCC_minf     GST_MAKE_FOURCC('m','i','n','f')
#define FOURCC_vmhd     GST_MAKE_FOURCC('v','m','h','d')
#define FOURCC_smhd     GST_MAKE_FOURCC('s','m','h','d')
#define FOURCC_gmhd     GST_MAKE_FOURCC('g','m','h','d')
#define FOURCC_gmin     GST_MAKE_FOURCC('g','m','i','n')
#define FOURCC_dinf     GST_MAKE_FOURCC('d','i','n','f')
#define FOURCC_dref     GST_MAKE_FOURCC('d','r','e','f')
#define FOURCC_stbl     GST_MAKE_FOURCC('s','t','b','l')
#define FOURCC_stsd     GST_MAKE_FOURCC('s','t','s','d')
#define FOURCC_stts     GST_MAKE_FOURCC('s','t','t','s')
#define FOURCC_stss     GST_MAKE_FOURCC('s','t','s','s')
#define FOURCC_stsc     GST_MAKE_FOURCC('s','t','s','c')
#define FOURCC_stsz     GST_MAKE_FOURCC('s','t','s','z')
#define FOURCC_stco     GST_MAKE_FOURCC('s','t','c','o')
#define FOURCC_vide     GST_MAKE_FOURCC('v','i','d','e')
#define FOURCC_soun     GST_MAKE_FOURCC('s','o','u','n')
#define FOURCC_co64     GST_MAKE_FOURCC('c','o','6','4')
#define FOURCC_cmov     GST_MAKE_FOURCC('c','m','o','v')
#define FOURCC_dcom     GST_MAKE_FOURCC('d','c','o','m')
#define FOURCC_cmvd     GST_MAKE_FOURCC('c','m','v','d')
#define FOURCC_hint     GST_MAKE_FOURCC('h','i','n','t')
#define FOURCC_mp4a     GST_MAKE_FOURCC('m','p','4','a')
#define FOURCC_mp4v     GST_MAKE_FOURCC('m','p','4','v')
#define FOURCC_wave     GST_MAKE_FOURCC('w','a','v','e')
#define FOURCC_appl     GST_MAKE_FOURCC('a','p','p','l')
#define FOURCC_esds     GST_MAKE_FOURCC('e','s','d','s')
#define FOURCC_hnti     GST_MAKE_FOURCC('h','n','t','i')
#define FOURCC_rtp_     GST_MAKE_FOURCC('r','t','p',' ')
#define FOURCC_sdp_     GST_MAKE_FOURCC('s','d','p',' ')
#define FOURCC_meta     GST_MAKE_FOURCC('m','e','t','a')
#define FOURCC_ilst     GST_MAKE_FOURCC('i','l','s','t')
#define FOURCC__nam     GST_MAKE_FOURCC(0xa9,'n','a','m')
#define FOURCC__ART     GST_MAKE_FOURCC(0xa9,'A','R','T')
#define FOURCC__wrt     GST_MAKE_FOURCC(0xa9,'w','r','t')
#define FOURCC__grp     GST_MAKE_FOURCC(0xa9,'g','r','p')
#define FOURCC__alb     GST_MAKE_FOURCC(0xa9,'a','l','b')
#define FOURCC_gnre     GST_MAKE_FOURCC('g','n','r','e')
#define FOURCC_disc     GST_MAKE_FOURCC('d','i','s','c')
#define FOURCC_disk     GST_MAKE_FOURCC('d','i','s','k')
#define FOURCC_trkn     GST_MAKE_FOURCC('t','r','k','n')
#define FOURCC_cpil     GST_MAKE_FOURCC('c','p','i','l')
#define FOURCC_tmpo     GST_MAKE_FOURCC('t','m','p','o')
#define FOURCC__too     GST_MAKE_FOURCC(0xa9,'t','o','o')
#define FOURCC_____     GST_MAKE_FOURCC('-','-','-','-')
#define FOURCC_free     GST_MAKE_FOURCC('f','r','e','e')
#define FOURCC_data     GST_MAKE_FOURCC('d','a','t','a')
#define FOURCC_SVQ3     GST_MAKE_FOURCC('S','V','Q','3')
#define FOURCC_rmra     GST_MAKE_FOURCC('r','m','r','a')
#define FOURCC_rmda     GST_MAKE_FOURCC('r','m','d','a')
#define FOURCC_rdrf     GST_MAKE_FOURCC('r','d','r','f')
#define FOURCC__gen     GST_MAKE_FOURCC(0xa9, 'g', 'e', 'n')

static void qtdemux_dump_mvhd (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_tkhd (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_elst (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_mdhd (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_hdlr (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_vmhd (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_dref (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_stsd (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_stts (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_stss (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_stsc (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_stsz (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_stco (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_co64 (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_dcom (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_cmvd (GstQTDemux * qtdemux, void *buffer, int depth);
static void qtdemux_dump_unknown (GstQTDemux * qtdemux, void *buffer,
    int depth);

QtNodeType qt_node_types[] = {
  {FOURCC_moov, "movie", QT_CONTAINER,},
  {FOURCC_mvhd, "movie header", 0,
      qtdemux_dump_mvhd},
  {FOURCC_clip, "clipping", QT_CONTAINER,},
  {FOURCC_trak, "track", QT_CONTAINER,},
  {FOURCC_udta, "user data", QT_CONTAINER,},    /* special container */
  {FOURCC_ctab, "color table", 0,},
  {FOURCC_tkhd, "track header", 0,
      qtdemux_dump_tkhd},
  {FOURCC_crgn, "clipping region", 0,},
  {FOURCC_matt, "track matte", QT_CONTAINER,},
  {FOURCC_kmat, "compressed matte", 0,},
  {FOURCC_edts, "edit", QT_CONTAINER,},
  {FOURCC_elst, "edit list", 0,
      qtdemux_dump_elst},
  {FOURCC_load, "track load settings", 0,},
  {FOURCC_tref, "track reference", QT_CONTAINER,},
  {FOURCC_imap, "track input map", QT_CONTAINER,},
  {FOURCC___in, "track input", 0,},     /* special container */
  {FOURCC___ty, "input type", 0,},
  {FOURCC_mdia, "media", QT_CONTAINER},
  {FOURCC_mdhd, "media header", 0,
      qtdemux_dump_mdhd},
  {FOURCC_hdlr, "handler reference", 0,
      qtdemux_dump_hdlr},
  {FOURCC_minf, "media information", QT_CONTAINER},
  {FOURCC_vmhd, "video media information", 0,
      qtdemux_dump_vmhd},
  {FOURCC_smhd, "sound media information", 0},
  {FOURCC_gmhd, "base media information header", 0},
  {FOURCC_gmin, "base media info", 0},
  {FOURCC_dinf, "data information", QT_CONTAINER},
  {FOURCC_dref, "data reference", 0,
      qtdemux_dump_dref},
  {FOURCC_stbl, "sample table", QT_CONTAINER},
  {FOURCC_stsd, "sample description", 0,
      qtdemux_dump_stsd},
  {FOURCC_stts, "time-to-sample", 0,
      qtdemux_dump_stts},
  {FOURCC_stss, "sync sample", 0,
      qtdemux_dump_stss},
  {FOURCC_stsc, "sample-to-chunk", 0,
      qtdemux_dump_stsc},
  {FOURCC_stsz, "sample size", 0,
      qtdemux_dump_stsz},
  {FOURCC_stco, "chunk offset", 0,
      qtdemux_dump_stco},
  {FOURCC_co64, "64-bit chunk offset", 0,
      qtdemux_dump_co64},
  {FOURCC_vide, "video media", 0},
  {FOURCC_cmov, "compressed movie", QT_CONTAINER},
  {FOURCC_dcom, "compressed data", 0, qtdemux_dump_dcom},
  {FOURCC_cmvd, "compressed movie data", 0, qtdemux_dump_cmvd},
  {FOURCC_hint, "hint", 0,},
  {FOURCC_mp4a, "mp4a", 0,},
  {FOURCC_mp4v, "mp4v", 0,},
  {FOURCC_wave, "wave", QT_CONTAINER},
  {FOURCC_appl, "appl", QT_CONTAINER},
  {FOURCC_esds, "esds", 0},
  {FOURCC_hnti, "hnti", QT_CONTAINER},
  {FOURCC_rtp_, "rtp ", 0, qtdemux_dump_unknown},
  {FOURCC_sdp_, "sdp ", 0, qtdemux_dump_unknown},
  {FOURCC_meta, "meta", 0, qtdemux_dump_unknown},
  {FOURCC_ilst, "ilst", QT_CONTAINER,},
  {FOURCC__nam, "Name", QT_CONTAINER,},
  {FOURCC__ART, "Artist", QT_CONTAINER,},
  {FOURCC__wrt, "Writer", QT_CONTAINER,},
  {FOURCC__grp, "Group", QT_CONTAINER,},
  {FOURCC__alb, "Album", QT_CONTAINER,},
  {FOURCC_gnre, "Genre", QT_CONTAINER,},
  {FOURCC_trkn, "Track Number", QT_CONTAINER,},
  {FOURCC_disc, "Disc Number", QT_CONTAINER,},
  {FOURCC_disk, "Disc Number", QT_CONTAINER,},
  {FOURCC_cpil, "cpil", QT_CONTAINER,},
  {FOURCC_tmpo, "Tempo", QT_CONTAINER,},
  {FOURCC__too, "too", QT_CONTAINER,},
  {FOURCC_____, "----", QT_CONTAINER,},
  {FOURCC_data, "data", 0, qtdemux_dump_unknown},
  {FOURCC_free, "free", 0,},
  {FOURCC_SVQ3, "SVQ3", 0,},
  {FOURCC_rmra, "rmra", QT_CONTAINER,},
  {FOURCC_rmda, "rmda", QT_CONTAINER,},
  {FOURCC_rdrf, "rdrf", 0,},
  {FOURCC__gen, "Custom Genre", QT_CONTAINER,},
  {0, "unknown", 0},
};
static int n_qt_node_types = sizeof (qt_node_types) / sizeof (qt_node_types[0]);


static void *
qtdemux_zalloc (void *opaque, unsigned int items, unsigned int size)
{
  return g_malloc (items * size);
}

static void
qtdemux_zfree (void *opaque, void *addr)
{
  g_free (addr);
}

static void *
qtdemux_inflate (void *z_buffer, int z_length, int length)
{
  void *buffer;
  z_stream *z;
  int ret;

  z = g_new0 (z_stream, 1);
  z->zalloc = qtdemux_zalloc;
  z->zfree = qtdemux_zfree;
  z->opaque = NULL;

  z->next_in = z_buffer;
  z->avail_in = z_length;

  buffer = g_malloc (length);
  ret = inflateInit (z);
  while (z->avail_in > 0) {
    if (z->avail_out == 0) {
      length += 1024;
      buffer = realloc (buffer, length);
      z->next_out = buffer + z->total_out;
      z->avail_out = 1024;
    }
    ret = inflate (z, Z_SYNC_FLUSH);
    if (ret != Z_OK)
      break;
  }
  if (ret != Z_STREAM_END) {
    g_warning ("inflate() returned %d", ret);
  }

  g_free (z);
  return buffer;
}

static void
qtdemux_parse_moov (GstQTDemux * qtdemux, void *buffer, int length)
{
  GNode *cmov;

  qtdemux->moov_node = g_node_new (buffer);

  GST_DEBUG_OBJECT (qtdemux, "parsing 'moov' atom");
  qtdemux_parse (qtdemux, qtdemux->moov_node, buffer, length);

  cmov = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_cmov);
  if (cmov) {
    GNode *dcom;
    GNode *cmvd;

    dcom = qtdemux_tree_get_child_by_type (cmov, FOURCC_dcom);
    cmvd = qtdemux_tree_get_child_by_type (cmov, FOURCC_cmvd);

    if (QTDEMUX_FOURCC_GET (dcom->data + 8) == GST_MAKE_FOURCC ('z', 'l', 'i',
            'b')) {
      int uncompressed_length;
      int compressed_length;
      void *buf;

      uncompressed_length = QTDEMUX_GUINT32_GET (cmvd->data + 8);
      compressed_length = QTDEMUX_GUINT32_GET (cmvd->data + 4) - 12;
      GST_LOG ("length = %d", uncompressed_length);

      buf = qtdemux_inflate (cmvd->data + 12, compressed_length,
          uncompressed_length);

      qtdemux->moov_node_compressed = qtdemux->moov_node;
      qtdemux->moov_node = g_node_new (buf);

      qtdemux_parse (qtdemux, qtdemux->moov_node, buf, uncompressed_length);
    } else {
      GST_LOG ("unknown header compression type");
    }
  }
}

static void
qtdemux_parse (GstQTDemux * qtdemux, GNode * node, void *buffer, int length)
{
  guint32 fourcc;
  guint32 node_length;
  QtNodeType *type;
  void *end;

  GST_LOG ("qtdemux_parse buffer %p length %d", buffer, length);

  node_length = QTDEMUX_GUINT32_GET (buffer);
  fourcc = QTDEMUX_FOURCC_GET (buffer + 4);

  type = qtdemux_type_get (fourcc);

  if (fourcc == 0 || node_length == 8)
    return;

  GST_LOG ("parsing '%" GST_FOURCC_FORMAT "', length=%d",
      GST_FOURCC_ARGS (fourcc), node_length);

  if (type->flags & QT_CONTAINER) {
    void *buf;
    guint32 len;

    buf = buffer + 8;
    end = buffer + length;
    while (buf < end) {
      GNode *child;

      if (buf + 8 >= end) {
        /* FIXME: get annoyed */
        GST_LOG ("buffer overrun");
      }
      len = QTDEMUX_GUINT32_GET (buf);
      if (len < 8) {
        GST_WARNING ("atom length too short (%d < 8)", len);
        break;
      }
      if (len > (end - buf)) {
        GST_WARNING ("atom length too long (%d > %d)", len, end - buf);
        break;
      }

      child = g_node_new (buf);
      g_node_append (node, child);
      qtdemux_parse (qtdemux, child, buf, len);

      buf += len;
    }
  } else {
    if (fourcc == FOURCC_stsd) {
      void *buf;
      guint32 len;

      GST_DEBUG_OBJECT (qtdemux,
          "parsing stsd (sample table, sample description) atom");
      buf = buffer + 16;
      end = buffer + length;
      while (buf < end) {
        GNode *child;

        if (buf + 8 >= end) {
          /* FIXME: get annoyed */
          GST_LOG ("buffer overrun");
        }
        len = QTDEMUX_GUINT32_GET (buf);
        if (len < 8) {
          GST_WARNING ("length too short (%d < 8)");
          break;
        }
        if (len > (end - buf)) {
          GST_WARNING ("length too long (%d > %d)", len, end - buf);
          break;
        }

        child = g_node_new (buf);
        g_node_append (node, child);
        qtdemux_parse (qtdemux, child, buf, len);

        buf += len;
      }
    } else if (fourcc == FOURCC_mp4a) {
      void *buf;
      guint32 len;
      guint32 version;

      version = QTDEMUX_GUINT32_GET (buffer + 16);
      if (version == 0x00010000 || 1) {
        buf = buffer + 0x24;
        end = buffer + length;

        while (buf < end) {
          GNode *child;

          if (buf + 8 >= end) {
            /* FIXME: get annoyed */
            GST_LOG ("buffer overrun");
          }
          len = QTDEMUX_GUINT32_GET (buf);
          if (len < 8) {
            GST_WARNING ("length too short (%d < 8)");
            break;
          }
          if (len > (end - buf)) {
            GST_WARNING ("length too long (%d > %d)", len, end - buf);
            break;
          }

          child = g_node_new (buf);
          g_node_append (node, child);
          qtdemux_parse (qtdemux, child, buf, len);

          buf += len;
        }
      }
    } else if (fourcc == FOURCC_mp4v) {
      void *buf;
      guint32 len;
      guint32 version;
      int tlen;

      GST_DEBUG_OBJECT (qtdemux, "parsing in mp4v");
      version = QTDEMUX_GUINT32_GET (buffer + 16);
      GST_DEBUG_OBJECT (qtdemux, "version %08x", version);
      if (1 || version == 0x00000000) {

        buf = buffer + 0x32;
        end = buffer + length;

        /* FIXME Quicktime uses PASCAL string while
         * the iso format uses C strings. Check the file
         * type before attempting to parse the string here. */
        tlen = QTDEMUX_GUINT8_GET (buf);
        GST_DEBUG_OBJECT (qtdemux, "tlen = %d", tlen);
        buf++;
        GST_DEBUG_OBJECT (qtdemux, "string = %.*s", tlen, (char *) buf);
        /* the string has a reserved space of 32 bytes so skip
         * the remaining 31 */
        buf += 31;
        buf += 4;               /* and 4 bytes reserved */

        gst_util_dump_mem (buf, end - buf);
        while (buf < end) {
          GNode *child;

          if (buf + 8 >= end) {
            /* FIXME: get annoyed */
            GST_LOG ("buffer overrun");
          }
          len = QTDEMUX_GUINT32_GET (buf);
          if (len == 0)
            break;
          if (len < 8) {
            GST_WARNING ("length too short (%d < 8)");
            break;
          }
          if (len > (end - buf)) {
            GST_WARNING ("length too long (%d > %d)", len, end - buf);
            break;
          }

          child = g_node_new (buf);
          g_node_append (node, child);
          qtdemux_parse (qtdemux, child, buf, len);

          buf += len;
        }
      }
    } else if (fourcc == FOURCC_meta) {
      void *buf;
      guint32 len;

      buf = buffer + 12;
      end = buffer + length;
      while (buf < end) {
        GNode *child;

        if (buf + 8 >= end) {
          /* FIXME: get annoyed */
          GST_LOG ("buffer overrun");
        }
        len = QTDEMUX_GUINT32_GET (buf);
        if (len < 8) {
          GST_WARNING ("length too short (%d < 8)");
          break;
        }
        if (len > (end - buf)) {
          GST_WARNING ("length too long (%d > %d)", len, end - buf);
          break;
        }

        child = g_node_new (buf);
        g_node_append (node, child);
        qtdemux_parse (qtdemux, child, buf, len);

        buf += len;
      }
    } else if (fourcc == FOURCC_SVQ3) {
      void *buf;
      guint32 len;
      guint32 version;
      int tlen;

      GST_LOG ("parsing in SVQ3");
      buf = buffer + 12;
      end = buffer + length;
      version = QTDEMUX_GUINT32_GET (buffer + 16);
      GST_DEBUG_OBJECT (qtdemux, "version %08x", version);
      if (1 || version == 0x00000000) {

        buf = buffer + 0x32;
        end = buffer + length;

        tlen = QTDEMUX_GUINT8_GET (buf);
        GST_DEBUG_OBJECT (qtdemux, "tlen = %d", tlen);
        buf++;
        GST_DEBUG_OBJECT (qtdemux, "string = %.*s", tlen, (char *) buf);
        buf += tlen;
        buf += 23;

        gst_util_dump_mem (buf, end - buf);
        while (buf < end) {
          GNode *child;

          if (buf + 8 >= end) {
            /* FIXME: get annoyed */
            GST_LOG ("buffer overrun");
          }
          len = QTDEMUX_GUINT32_GET (buf);
          if (len == 0)
            break;
          if (len < 8) {
            GST_WARNING ("length too short (%d < 8)");
            break;
          }
          if (len > (end - buf)) {
            GST_WARNING ("length too long (%d > %d)", len, end - buf);
            break;
          }

          child = g_node_new (buf);
          g_node_append (node, child);
          qtdemux_parse (qtdemux, child, buf, len);

          buf += len;
        }
      }
    }
#if 0
    if (fourcc == FOURCC_cmvd) {
      int uncompressed_length;
      void *buf;

      uncompressed_length = QTDEMUX_GUINT32_GET (buffer + 8);
      GST_LOG ("length = %d", uncompressed_length);

      buf =
          qtdemux_inflate (buffer + 12, node_length - 12, uncompressed_length);

      end = buf + uncompressed_length;
      while (buf < end) {
        GNode *child;
        guint32 len;

        if (buf + 8 >= end) {
          /* FIXME: get annoyed */
          GST_LOG ("buffer overrun");
        }
        len = QTDEMUX_GUINT32_GET (buf);

        child = g_node_new (buf);
        g_node_append (node, child);
        qtdemux_parse (qtdemux, child, buf, len);

        buf += len;
      }
    }
#endif
  }
}

static QtNodeType *
qtdemux_type_get (guint32 fourcc)
{
  int i;

  for (i = 0; i < n_qt_node_types; i++) {
    if (qt_node_types[i].fourcc == fourcc)
      return qt_node_types + i;
  }

  GST_WARNING ("unknown QuickTime node type %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (fourcc));
  return qt_node_types + n_qt_node_types - 1;
}

static gboolean
qtdemux_node_dump_foreach (GNode * node, gpointer data)
{
  void *buffer = node->data;
  guint32 node_length;
  guint32 fourcc;
  QtNodeType *type;
  int depth;

  node_length = GST_READ_UINT32_BE (buffer);
  fourcc = GST_READ_UINT32_LE (buffer + 4);

  type = qtdemux_type_get (fourcc);

  depth = (g_node_depth (node) - 1) * 2;
  GST_LOG ("%*s'%" GST_FOURCC_FORMAT "', [%d], %s",
      depth, "", GST_FOURCC_ARGS (fourcc), node_length, type->name);

  if (type->dump)
    type->dump (data, buffer, depth);

  return FALSE;
}

static void
qtdemux_node_dump (GstQTDemux * qtdemux, GNode * node)
{
  g_node_traverse (qtdemux->moov_node, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
      qtdemux_node_dump_foreach, qtdemux);
}

static void
qtdemux_dump_mvhd (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  creation time: %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  GST_LOG ("%*s  modify time:   %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 16));
  qtdemux->duration = QTDEMUX_GUINT32_GET (buffer + 24);
  qtdemux->timescale = QTDEMUX_GUINT32_GET (buffer + 20);
  GST_LOG ("%*s  time scale:    1/%u sec", depth, "", qtdemux->timescale);
  GST_LOG ("%*s  duration:      %u", depth, "", qtdemux->duration);
  GST_LOG ("%*s  pref. rate:    %g", depth, "", QTDEMUX_FP32_GET (buffer + 28));
  GST_LOG ("%*s  pref. volume:  %g", depth, "", QTDEMUX_FP16_GET (buffer + 32));
  GST_LOG ("%*s  preview time:  %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 80));
  GST_LOG ("%*s  preview dur.:  %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 84));
  GST_LOG ("%*s  poster time:   %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 88));
  GST_LOG ("%*s  select time:   %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 92));
  GST_LOG ("%*s  select dur.:   %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 96));
  GST_LOG ("%*s  current time:  %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 100));
  GST_LOG ("%*s  next track ID: %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 104));
}

static void
qtdemux_dump_tkhd (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  creation time: %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  GST_LOG ("%*s  modify time:   %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 16));
  GST_LOG ("%*s  track ID:      %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 20));
  GST_LOG ("%*s  duration:      %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 28));
  GST_LOG ("%*s  layer:         %u", depth, "",
      QTDEMUX_GUINT16_GET (buffer + 36));
  GST_LOG ("%*s  alt group:     %u", depth, "",
      QTDEMUX_GUINT16_GET (buffer + 38));
  GST_LOG ("%*s  volume:        %g", depth, "", QTDEMUX_FP16_GET (buffer + 44));
  GST_LOG ("%*s  track width:   %g", depth, "", QTDEMUX_FP32_GET (buffer + 84));
  GST_LOG ("%*s  track height:  %g", depth, "", QTDEMUX_FP32_GET (buffer + 88));

}

static void
qtdemux_dump_elst (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int i;
  int n;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  n entries:     %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    track dur:     %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + 16 + i * 12));
    GST_LOG ("%*s    media time:    %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + 20 + i * 12));
    GST_LOG ("%*s    media rate:    %g", depth, "",
        QTDEMUX_FP32_GET (buffer + 24 + i * 12));
  }
}

static void
qtdemux_dump_mdhd (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  creation time: %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  GST_LOG ("%*s  modify time:   %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 16));
  GST_LOG ("%*s  time scale:    1/%u sec", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 20));
  GST_LOG ("%*s  duration:      %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 24));
  GST_LOG ("%*s  language:      %u", depth, "",
      QTDEMUX_GUINT16_GET (buffer + 28));
  GST_LOG ("%*s  quality:       %u", depth, "",
      QTDEMUX_GUINT16_GET (buffer + 30));

}

static void
qtdemux_dump_hdlr (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  type:          %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + 12)));
  GST_LOG ("%*s  subtype:       %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + 16)));
  GST_LOG ("%*s  manufacturer:  %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + 20)));
  GST_LOG ("%*s  flags:         %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 24));
  GST_LOG ("%*s  flags mask:    %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 28));
  GST_LOG ("%*s  name:          %*s", depth, "",
      QTDEMUX_GUINT8_GET (buffer + 32), (char *) (buffer + 33));

}

static void
qtdemux_dump_vmhd (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  mode/color:    %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 16));
}

static void
qtdemux_dump_dref (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int n;
  int i;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  n entries:     %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    size:          %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));
    GST_LOG ("%*s    type:          %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + offset + 4)));
    offset += QTDEMUX_GUINT32_GET (buffer + offset);
  }
}

static void
qtdemux_dump_stsd (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    size:          %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));
    GST_LOG ("%*s    type:          %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + offset + 4)));
    GST_LOG ("%*s    data reference:%d", depth, "",
        QTDEMUX_GUINT16_GET (buffer + offset + 14));

    GST_LOG ("%*s    version/rev.:  %08x", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 16));
    GST_LOG ("%*s    vendor:        %" GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + offset + 20)));
    GST_LOG ("%*s    temporal qual: %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 24));
    GST_LOG ("%*s    spatial qual:  %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 28));
    GST_LOG ("%*s    width:         %u", depth, "",
        QTDEMUX_GUINT16_GET (buffer + offset + 32));
    GST_LOG ("%*s    height:        %u", depth, "",
        QTDEMUX_GUINT16_GET (buffer + offset + 34));
    GST_LOG ("%*s    horiz. resol:  %g", depth, "",
        QTDEMUX_FP32_GET (buffer + offset + 36));
    GST_LOG ("%*s    vert. resol.:  %g", depth, "",
        QTDEMUX_FP32_GET (buffer + offset + 40));
    GST_LOG ("%*s    data size:     %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 44));
    GST_LOG ("%*s    frame count:   %u", depth, "",
        QTDEMUX_GUINT16_GET (buffer + offset + 48));
    GST_LOG ("%*s    compressor:    %d %d %d", depth, "",
        QTDEMUX_GUINT8_GET (buffer + offset + 49),
        QTDEMUX_GUINT8_GET (buffer + offset + 50),
        QTDEMUX_GUINT8_GET (buffer + offset + 51));
    //(char *) (buffer + offset + 51));
    GST_LOG ("%*s    depth:         %u", depth, "",
        QTDEMUX_GUINT16_GET (buffer + offset + 82));
    GST_LOG ("%*s    color table ID:%u", depth, "",
        QTDEMUX_GUINT16_GET (buffer + offset + 84));

    offset += QTDEMUX_GUINT32_GET (buffer + offset);
  }
}

static void
qtdemux_dump_stts (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    count:         %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));
    GST_LOG ("%*s    duration:      %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 4));

    offset += 8;
  }
}

static void
qtdemux_dump_stss (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    sample:        %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));

    offset += 4;
  }
}

static void
qtdemux_dump_stsc (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    first chunk:   %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));
    GST_LOG ("%*s    sample per ch: %u", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 4));
    GST_LOG ("%*s    sample desc id:%08x", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 8));

    offset += 12;
  }
}

static void
qtdemux_dump_stsz (GstQTDemux * qtdemux, void *buffer, int depth)
{
  //int i;
  int n;
  int offset;
  int sample_size;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  sample_size = QTDEMUX_GUINT32_GET (buffer + 12);
  GST_LOG ("%*s  sample size:   %d", depth, "", sample_size);
  if (sample_size == 0) {
    GST_LOG ("%*s  n entries:     %d", depth, "",
        QTDEMUX_GUINT32_GET (buffer + 16));
    n = QTDEMUX_GUINT32_GET (buffer + 16);
    offset = 20;
#if 0
    for (i = 0; i < n; i++) {
      GST_LOG ("%*s    sample size:   %u", depth, "",
          QTDEMUX_GUINT32_GET (buffer + offset));

      offset += 4;
    }
#endif
  }
}

static void
qtdemux_dump_stco (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  GST_LOG ("%*s  n entries:     %d", depth, "", n);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    chunk offset:  %d", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));

    offset += 4;
  }
}

static void
qtdemux_dump_co64 (GstQTDemux * qtdemux, void *buffer, int depth)
{
  //int i;
  int n;
  int offset;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  n entries:     %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  offset = 16;
#if 0
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    chunk offset:  %" G_GUINT64_FORMAT, depth, "",
        QTDEMUX_GUINT64_GET (buffer + offset));

    offset += 8;
  }
#endif
}

static void
qtdemux_dump_dcom (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  compression type: %" GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + 8)));
}

static void
qtdemux_dump_cmvd (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  length: %d", depth, "", QTDEMUX_GUINT32_GET (buffer + 8));
}

static void
qtdemux_dump_unknown (GstQTDemux * qtdemux, void *buffer, int depth)
{
  int len;

  GST_LOG ("%*s  length: %d", depth, "", QTDEMUX_GUINT32_GET (buffer + 0));

  len = QTDEMUX_GUINT32_GET (buffer + 0);
  gst_util_dump_mem (buffer, len);

}


static GNode *
qtdemux_tree_get_child_by_type (GNode * node, guint32 fourcc)
{
  GNode *child;
  void *buffer;
  guint32 child_fourcc;

  for (child = g_node_first_child (node); child;
      child = g_node_next_sibling (child)) {
    buffer = child->data;

    child_fourcc = GST_READ_UINT32_LE (buffer);
    GST_LOG ("First chunk of buffer %p is [%" GST_FOURCC_FORMAT "]",
        buffer, GST_FOURCC_ARGS (child_fourcc));

    child_fourcc = GST_READ_UINT32_LE (buffer + 4);
    GST_LOG ("buffer %p has fourcc [%" GST_FOURCC_FORMAT "]",
        buffer, GST_FOURCC_ARGS (child_fourcc));

    if (child_fourcc == fourcc) {
      return child;
    }
  }
  return NULL;
}

static GNode *
qtdemux_tree_get_sibling_by_type (GNode * node, guint32 fourcc)
{
  GNode *child;
  void *buffer;
  guint32 child_fourcc;

  for (child = g_node_next_sibling (node); child;
      child = g_node_next_sibling (child)) {
    buffer = child->data;

    child_fourcc = GST_READ_UINT32_LE (buffer + 4);

    if (child_fourcc == fourcc) {
      return child;
    }
  }
  return NULL;
}

static void qtdemux_parse_trak (GstQTDemux * qtdemux, GNode * trak);

static void
qtdemux_parse_tree (GstQTDemux * qtdemux)
{
  GNode *mvhd;
  GNode *trak;
  GNode *udta;

  mvhd = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_mvhd);
  if (mvhd == NULL) {
    GNode *rmra, *rmda, *rdrf;

    rmra = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_rmra);
    if (rmra) {
      rmda = qtdemux_tree_get_child_by_type (rmra, FOURCC_rmda);
      if (rmra) {
        rdrf = qtdemux_tree_get_child_by_type (rmda, FOURCC_rdrf);
        if (rdrf) {
          GstStructure *s;
          GstMessage *msg;

          GST_LOG ("New location: %s", (char *) rdrf->data + 20);
          s = gst_structure_new ("redirect", "new-location", G_TYPE_STRING,
              (char *) rdrf->data + 20, NULL);
          msg = gst_message_new_element (GST_OBJECT (qtdemux), s);
          gst_element_post_message (GST_ELEMENT (qtdemux), msg);
          return;
        }
      }
    }

    GST_LOG ("No mvhd node found.");
    return;
  }

  qtdemux->timescale = QTDEMUX_GUINT32_GET (mvhd->data + 20);
  qtdemux->duration = QTDEMUX_GUINT32_GET (mvhd->data + 24);

  GST_INFO_OBJECT (qtdemux, "timescale: %d", qtdemux->timescale);
  GST_INFO_OBJECT (qtdemux, "duration: %d", qtdemux->duration);

  if (qtdemux->timescale != 0 && qtdemux->duration != 0) {
    gint64 duration;

    duration = gst_util_uint64_scale_int (qtdemux->duration,
        GST_SECOND, qtdemux->timescale);

    gst_segment_set_duration (&qtdemux->segment, GST_FORMAT_TIME, duration);
  }

  trak = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_trak);
  qtdemux_parse_trak (qtdemux, trak);

/*  trak = qtdemux_tree_get_sibling_by_type(trak, FOURCC_trak);
  if(trak)qtdemux_parse_trak(qtdemux, trak);*/

  /* kid pads */
  while ((trak = qtdemux_tree_get_sibling_by_type (trak, FOURCC_trak)) != NULL)
    qtdemux_parse_trak (qtdemux, trak);
  gst_element_no_more_pads (GST_ELEMENT (qtdemux));

  /* tags */
  udta = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_udta);
  if (udta) {
    qtdemux_parse_udta (qtdemux, udta);

    if (qtdemux->tag_list) {
      GST_DEBUG_OBJECT (qtdemux,
          "calling gst_element_found_tags with %" GST_PTR_FORMAT,
          qtdemux->tag_list);
      gst_element_found_tags (GST_ELEMENT (qtdemux), qtdemux->tag_list);
      qtdemux->tag_list = NULL;
    }
  } else {
    GST_LOG ("No udta node found.");
  }
}

static void
qtdemux_parse_trak (GstQTDemux * qtdemux, GNode * trak)
{
  int offset;
  GNode *tkhd;
  GNode *mdia;
  GNode *mdhd;
  GNode *hdlr;
  GNode *minf;
  GNode *stbl;
  GNode *stsd;
  GNode *stsc;
  GNode *stss;
  GNode *stsz;
  GNode *stco;
  GNode *co64;
  GNode *stts;
  GNode *mp4a;
  GNode *mp4v;
  GNode *wave;
  GNode *esds;
  GNode *edts;
  int n_samples;
  QtDemuxSample *samples;
  int n_samples_per_chunk;
  int index;
  int i, j, k;
  QtDemuxStream *stream;
  int n_sample_times;
  guint64 timestamp, time;
  int sample_size;
  int sample_index;
  GstTagList *list = NULL;
  const gchar *codec = NULL;

  tkhd = qtdemux_tree_get_child_by_type (trak, FOURCC_tkhd);
  g_return_if_fail (tkhd);

  GST_LOG ("track[tkhd] version/flags: 0x%08x",
      QTDEMUX_GUINT32_GET (tkhd->data + 8));

  mdia = qtdemux_tree_get_child_by_type (trak, FOURCC_mdia);
  g_assert (mdia);

  mdhd = qtdemux_tree_get_child_by_type (mdia, FOURCC_mdhd);
  g_assert (mdhd);

  /* new streams always need a discont */
  stream = g_new0 (QtDemuxStream, 1);
  stream->discont = TRUE;
  stream->segment_index = -1;
  stream->time_position = 0;
  stream->sample_index = 0;

  stream->timescale = QTDEMUX_GUINT32_GET (mdhd->data + 20);
  stream->duration = QTDEMUX_GUINT32_GET (mdhd->data + 24);

  GST_LOG ("track timescale: %d", stream->timescale);
  GST_LOG ("track duration: %d", stream->duration);

  {
    guint64 tdur1, tdur2;

    /* don't overflow */
    tdur1 = stream->timescale * (guint64) qtdemux->duration;
    tdur2 = qtdemux->timescale * (guint64) stream->duration;

    /* HACK:
     * some of those trailers, nowadays, have prologue images that are
     * themselves vide tracks as well. I haven't really found a way to
     * identify those yet, except for just looking at their duration. */
    if (tdur1 != 0 && (tdur2 * 10 / tdur1) < 2) {
      GST_WARNING ("Track shorter than 20%% (%d/%d vs. %d/%d) of the stream "
          "found, assuming preview image or something; skipping track",
          stream->duration, stream->timescale, qtdemux->duration,
          qtdemux->timescale);
      g_free (stream);
      return;
    }
  }

  hdlr = qtdemux_tree_get_child_by_type (mdia, FOURCC_hdlr);
  g_assert (hdlr);

  GST_LOG ("track type: %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (hdlr->data + 12)));
  GST_LOG ("track subtype: %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (hdlr->data + 16)));

  stream->subtype = QTDEMUX_FOURCC_GET (hdlr->data + 16);

  minf = qtdemux_tree_get_child_by_type (mdia, FOURCC_minf);
  g_assert (minf);

  stbl = qtdemux_tree_get_child_by_type (minf, FOURCC_stbl);
  g_assert (stbl);

  stsd = qtdemux_tree_get_child_by_type (stbl, FOURCC_stsd);
  g_assert (stsd);

  if (stream->subtype == FOURCC_vide) {
    guint32 fourcc;

    offset = 16;
    GST_LOG ("st type:          %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + offset + 4)));

    stream->width = QTDEMUX_GUINT16_GET (stsd->data + offset + 32);
    stream->height = QTDEMUX_GUINT16_GET (stsd->data + offset + 34);
    stream->fps_n = 0;          /* this is filled in later */
    stream->fps_d = 0;          /* this is filled in later */
    stream->bits_per_sample = QTDEMUX_GUINT16_GET (stsd->data + offset + 82);
    stream->color_table_id = QTDEMUX_GUINT16_GET (stsd->data + offset + 84);

    GST_LOG ("frame count:   %u",
        QTDEMUX_GUINT16_GET (stsd->data + offset + 48));

    stream->fourcc = fourcc = QTDEMUX_FOURCC_GET (stsd->data + offset + 4);
    stream->caps = qtdemux_video_caps (qtdemux, fourcc, stsd->data, &codec);
    if (codec) {
      list = gst_tag_list_new ();
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_VIDEO_CODEC, codec, NULL);
    }

    esds = NULL;
    mp4v = qtdemux_tree_get_child_by_type (stsd, FOURCC_mp4v);
    if (mp4v)
      esds = qtdemux_tree_get_child_by_type (mp4v, FOURCC_esds);

    if (esds) {
      gst_qtdemux_handle_esds (qtdemux, stream, esds);
    } else {
      if (QTDEMUX_FOURCC_GET ((char *) stsd->data + 16 + 4) ==
          GST_MAKE_FOURCC ('a', 'v', 'c', '1')) {
        gint len = QTDEMUX_GUINT32_GET (stsd->data) - 0x66;
        guint8 *stsddata = stsd->data + 0x66;

        /* find avcC */
        while (len >= 0x8 &&
            QTDEMUX_FOURCC_GET (stsddata + 0x4) !=
            GST_MAKE_FOURCC ('a', 'v', 'c', 'C') &&
            QTDEMUX_GUINT32_GET (stsddata) < len) {
          len -= QTDEMUX_GUINT32_GET (stsddata);
          stsddata += QTDEMUX_GUINT32_GET (stsddata);
        }

        /* parse, if found */
        if (len > 0x8 &&
            QTDEMUX_FOURCC_GET (stsddata + 0x4) ==
            GST_MAKE_FOURCC ('a', 'v', 'c', 'C')) {
          GstBuffer *buf;
          gint size;

          if (QTDEMUX_GUINT32_GET (stsddata) < len)
            size = QTDEMUX_GUINT32_GET (stsddata) - 0x8;
          else
            size = len - 0x8;

          buf = gst_buffer_new_and_alloc (size);
          memcpy (GST_BUFFER_DATA (buf), stsddata + 0x8, size);
          gst_caps_set_simple (stream->caps,
              "codec_data", GST_TYPE_BUFFER, buf, NULL);
          gst_buffer_unref (buf);
        }
      } else if (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4) ==
          GST_MAKE_FOURCC ('S', 'V', 'Q', '3')) {
        GstBuffer *buf;
        gint len = QTDEMUX_GUINT32_GET (stsd->data);

        buf = gst_buffer_new_and_alloc (len);
        memcpy (GST_BUFFER_DATA (buf), stsd->data, len);
        gst_caps_set_simple (stream->caps,
            "codec_data", GST_TYPE_BUFFER, buf, NULL);
        gst_buffer_unref (buf);
      } else if (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4) ==
          GST_MAKE_FOURCC ('V', 'P', '3', '1')) {
        GstBuffer *buf;
        gint len = QTDEMUX_GUINT32_GET (stsd->data);

        buf = gst_buffer_new_and_alloc (len);
        memcpy (GST_BUFFER_DATA (buf), stsd->data, len);
        gst_caps_set_simple (stream->caps,
            "codec_data", GST_TYPE_BUFFER, buf, NULL);
        gst_buffer_unref (buf);
      } else if (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4) ==
          GST_MAKE_FOURCC ('r', 'l', 'e', ' ')) {
        gst_caps_set_simple (stream->caps,
            "depth", G_TYPE_INT, QTDEMUX_GUINT16_GET (stsd->data + offset + 82),
            NULL);
      }
    }

    GST_INFO_OBJECT (qtdemux,
        "type %" GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + offset + 4)),
        stream->caps);
  } else if (stream->subtype == FOURCC_soun) {
    int version, samplesize;
    guint32 fourcc;
    int len;
    guint16 compression_id;

    len = QTDEMUX_GUINT32_GET (stsd->data + 16);
    GST_LOG ("st type:          %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4)));

    stream->fourcc = fourcc = QTDEMUX_FOURCC_GET (stsd->data + 16 + 4);
    offset = 32;

    version = QTDEMUX_GUINT32_GET (stsd->data + offset);
    stream->n_channels = QTDEMUX_GUINT16_GET (stsd->data + offset + 8);
    samplesize = QTDEMUX_GUINT16_GET (stsd->data + offset + 10);
    compression_id = QTDEMUX_GUINT16_GET (stsd->data + offset + 12);
    stream->rate = QTDEMUX_FP32_GET (stsd->data + offset + 16);

    GST_LOG ("version/rev:      %08x", version);
    GST_LOG ("vendor:           %08x",
        QTDEMUX_GUINT32_GET (stsd->data + offset + 4));
    GST_LOG ("n_channels:       %d", stream->n_channels);
    GST_LOG ("sample_size:      %d", samplesize);
    GST_LOG ("compression_id:   %d", compression_id);
    GST_LOG ("packet size:      %d",
        QTDEMUX_GUINT16_GET (stsd->data + offset + 14));
    GST_LOG ("sample rate:      %g", stream->rate);

    if (compression_id == 0xfffe)
      stream->sampled = TRUE;

    offset = 52;
    if (version == 0x00010000) {
      stream->samples_per_packet = QTDEMUX_GUINT32_GET (stsd->data + offset);
      stream->samples_per_frame = stream->n_channels;
      stream->bytes_per_packet = QTDEMUX_GUINT32_GET (stsd->data + offset + 4);
      stream->bytes_per_frame = QTDEMUX_GUINT32_GET (stsd->data + offset + 8);
      stream->bytes_per_sample = QTDEMUX_GUINT32_GET (stsd->data + offset + 12);

      GST_LOG ("samples/packet:   %d", stream->samples_per_packet);
      GST_LOG ("bytes/packet:     %d", stream->bytes_per_packet);
      GST_LOG ("bytes/frame:      %d", stream->bytes_per_frame);
      GST_LOG ("bytes/sample:     %d", stream->bytes_per_sample);

      offset = 68;
    } else if (version == 0x00000000) {
      stream->bytes_per_sample = samplesize / 8;
      stream->samples_per_frame = stream->n_channels;
      stream->bytes_per_frame = stream->n_channels * stream->bytes_per_sample;
      stream->samples_per_packet = stream->samples_per_frame;
      stream->bytes_per_packet = stream->bytes_per_sample;

      /* Yes, these have to be hard-coded */
      if (fourcc == GST_MAKE_FOURCC ('M', 'A', 'C', '6')) {
        stream->samples_per_packet = 6;
        stream->bytes_per_packet = 1;
        stream->bytes_per_frame = 1 * stream->n_channels;
        stream->bytes_per_sample = 1;
        stream->samples_per_frame = 6 * stream->n_channels;
      }
      if (fourcc == GST_MAKE_FOURCC ('M', 'A', 'C', '3')) {
        stream->samples_per_packet = 3;
        stream->bytes_per_packet = 1;
        stream->bytes_per_frame = 1 * stream->n_channels;
        stream->bytes_per_sample = 1;
        stream->samples_per_frame = 3 * stream->n_channels;
      }
      if (fourcc == GST_MAKE_FOURCC ('i', 'm', 'a', '4')) {
        stream->samples_per_packet = 64;
        stream->bytes_per_packet = 34;
        stream->bytes_per_frame = 34 * stream->n_channels;
        stream->bytes_per_sample = 2;
        stream->samples_per_frame = 64 * stream->n_channels;
      }
      if (fourcc == GST_MAKE_FOURCC ('u', 'l', 'a', 'w') ||
          fourcc == GST_MAKE_FOURCC ('a', 'l', 'a', 'w')) {
        stream->samples_per_packet = 1;
        stream->bytes_per_packet = 1;
        stream->bytes_per_frame = 1 * stream->n_channels;
        stream->bytes_per_sample = 2;
        stream->samples_per_frame = 6 * stream->n_channels;
      }
    } else {
      GST_WARNING ("unknown version %08x", version);
    }

    stream->caps = qtdemux_audio_caps (qtdemux, stream, fourcc, NULL, 0,
        &codec);

    if (codec) {
      list = gst_tag_list_new ();
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_AUDIO_CODEC, codec, NULL);
    }

    mp4a = qtdemux_tree_get_child_by_type (stsd, FOURCC_mp4a);
    wave = NULL;
    if (mp4a)
      wave = qtdemux_tree_get_child_by_type (mp4a, FOURCC_wave);

    esds = NULL;
    if (wave)
      esds = qtdemux_tree_get_child_by_type (wave, FOURCC_esds);
    else if (mp4a)
      esds = qtdemux_tree_get_child_by_type (mp4a, FOURCC_esds);

    if (esds) {
      gst_qtdemux_handle_esds (qtdemux, stream, esds);
#if 0
      GstBuffer *buffer;
      int len = QTDEMUX_GUINT32_GET (esds->data);

      buffer = gst_buffer_new_and_alloc (len - 8);
      memcpy (GST_BUFFER_DATA (buffer), esds->data + 8, len - 8);

      gst_caps_set_simple (stream->caps, "codec_data",
          GST_TYPE_BUFFER, buffer, NULL);
#endif
    } else {
      if (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4) ==
          GST_MAKE_FOURCC ('Q', 'D', 'M', '2')) {
        gint len = QTDEMUX_GUINT32_GET (stsd->data);

        if (len > 0x4C) {
          GstBuffer *buf = gst_buffer_new_and_alloc (len - 0x4C);

          memcpy (GST_BUFFER_DATA (buf),
              (guint8 *) stsd->data + 0x4C, len - 0x4C);
          gst_caps_set_simple (stream->caps,
              "codec_data", GST_TYPE_BUFFER, buf, NULL);
          gst_buffer_unref (buf);
        }
        gst_caps_set_simple (stream->caps,
            "samplesize", G_TYPE_INT, samplesize, NULL);
      } else if (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4) ==
          GST_MAKE_FOURCC ('a', 'l', 'a', 'c')) {
        gint len = QTDEMUX_GUINT32_GET (stsd->data);

        if (len > 0x34) {
          GstBuffer *buf = gst_buffer_new_and_alloc (len - 0x34);

          memcpy (GST_BUFFER_DATA (buf),
              (guint8 *) stsd->data + 0x34, len - 0x34);
          gst_caps_set_simple (stream->caps,
              "codec_data", GST_TYPE_BUFFER, buf, NULL);
          gst_buffer_unref (buf);
        }
        gst_caps_set_simple (stream->caps,
            "samplesize", G_TYPE_INT, samplesize, NULL);
      }
    }
    GST_INFO_OBJECT (qtdemux,
        "type %" GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4)),
        stream->caps);
  } else {
    GST_INFO_OBJECT (qtdemux, "unknown subtype %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (stream->subtype));
    g_free (stream);
    return;
  }

  /* promote to sampled format */
  if (stream->fourcc == GST_MAKE_FOURCC ('s', 'a', 'm', 'r')) {
    /* force mono 8000 Hz for AMR */
    stream->sampled = TRUE;
    stream->n_channels = 1;
    stream->rate = 8000;
  }
  if (stream->fourcc == GST_MAKE_FOURCC ('m', 'p', '4', 'a'))
    stream->sampled = TRUE;

  /* sample to chunk */
  stsc = qtdemux_tree_get_child_by_type (stbl, FOURCC_stsc);
  g_assert (stsc);
  /* sample size */
  stsz = qtdemux_tree_get_child_by_type (stbl, FOURCC_stsz);
  g_assert (stsz);
  /* chunk offsets */
  stco = qtdemux_tree_get_child_by_type (stbl, FOURCC_stco);
  co64 = qtdemux_tree_get_child_by_type (stbl, FOURCC_co64);
  g_assert (stco || co64);
  /* sample time */
  stts = qtdemux_tree_get_child_by_type (stbl, FOURCC_stts);
  g_assert (stts);
  /* sample sync, can be NULL */
  stss = qtdemux_tree_get_child_by_type (stbl, FOURCC_stss);

  sample_size = QTDEMUX_GUINT32_GET (stsz->data + 12);
  if (sample_size == 0 || stream->sampled) {
    n_samples = QTDEMUX_GUINT32_GET (stsz->data + 16);
    GST_DEBUG_OBJECT (qtdemux, "stsz sample_size 0, allocating n_samples %d",
        n_samples);
    stream->n_samples = n_samples;
    samples = g_new0 (QtDemuxSample, n_samples);
    stream->samples = samples;

    for (i = 0; i < n_samples; i++) {
      if (sample_size == 0)
        samples[i].size = QTDEMUX_GUINT32_GET (stsz->data + i * 4 + 20);
      else
        samples[i].size = sample_size;

      GST_LOG_OBJECT (qtdemux, "sample %d has size %d", i, samples[i].size);
      /* init other fields to defaults for this sample */
      samples[i].keyframe = FALSE;
    }
    n_samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 12);
    index = 0;
    offset = 16;
    for (i = 0; i < n_samples_per_chunk; i++) {
      guint32 first_chunk, last_chunk;
      guint32 samples_per_chunk;

      first_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 0) - 1;
      if (i == n_samples_per_chunk - 1) {
        last_chunk = G_MAXUINT32;
      } else {
        last_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 12) - 1;
      }
      samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 4);

      for (j = first_chunk; j < last_chunk; j++) {
        guint64 chunk_offset;

        if (stco) {
          chunk_offset = QTDEMUX_GUINT32_GET (stco->data + 16 + j * 4);
        } else {
          chunk_offset = QTDEMUX_GUINT64_GET (co64->data + 16 + j * 8);
        }
        for (k = 0; k < samples_per_chunk; k++) {
          GST_LOG_OBJECT (qtdemux, "Creating entry %d with offset %lld",
              index, chunk_offset);
          samples[index].chunk = j;
          samples[index].offset = chunk_offset;
          chunk_offset += samples[index].size;
          index++;
          if (index >= n_samples)
            goto done;
        }
      }
    }
  done:

    n_sample_times = QTDEMUX_GUINT32_GET (stts->data + 12);
    timestamp = 0;
    time = 0;
    index = 0;
    for (i = 0; i < n_sample_times; i++) {
      guint32 n;
      guint32 duration;

      n = QTDEMUX_GUINT32_GET (stts->data + 16 + 8 * i);
      duration = QTDEMUX_GUINT32_GET (stts->data + 16 + 8 * i + 4);
      for (j = 0; j < n; j++) {
        GST_INFO_OBJECT (qtdemux, "sample %d: timestamp %" GST_TIME_FORMAT,
            index, GST_TIME_ARGS (timestamp));

        samples[index].timestamp = timestamp;
        /* add non-scaled values to avoid rounding errors */
        time += duration;
        timestamp = gst_util_uint64_scale_int (time,
            GST_SECOND, stream->timescale);
        samples[index].duration = timestamp - samples[index].timestamp;

        index++;
      }
    }
    if (stss) {
      /* mark keyframes */
      guint32 n_sample_syncs;

      n_sample_syncs = QTDEMUX_GUINT32_GET (stss->data + 12);
      if (n_sample_syncs == 0) {
        stream->all_keyframe = TRUE;
      } else {
        offset = 16;
        for (i = 0; i < n_sample_syncs; i++) {
          /* not that the first sample is index 1, not 0 */
          index = QTDEMUX_GUINT32_GET (stss->data + offset);
          samples[index - 1].keyframe = TRUE;
          offset += 4;
        }
      }
    } else {
      /* no stss, all samples are keyframes */
      stream->all_keyframe = TRUE;
    }
  } else {
    GST_DEBUG_OBJECT (qtdemux,
        "stsz sample_size %d != 0, treating chunks as samples", sample_size);

    /* treat chunks as samples */
    if (stco) {
      n_samples = QTDEMUX_GUINT32_GET (stco->data + 12);
    } else {
      n_samples = QTDEMUX_GUINT32_GET (co64->data + 12);
    }
    stream->n_samples = n_samples;
    GST_DEBUG_OBJECT (qtdemux, "allocating n_samples %d", n_samples);
    samples = g_new0 (QtDemuxSample, n_samples);
    stream->samples = samples;

    n_samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 12);
    GST_DEBUG_OBJECT (qtdemux, "n_samples_per_chunk %d", n_samples_per_chunk);
    offset = 16;
    sample_index = 0;
    timestamp = 0;
    for (i = 0; i < n_samples_per_chunk; i++) {
      guint32 first_chunk, last_chunk;
      guint32 samples_per_chunk;

      first_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 0) - 1;
      /* the last chunk of each entry is calculated by taking the first chunk
       * of the next entry; except if there is no next, where we fake it with
       * INT_MAX */
      if (i == n_samples_per_chunk - 1) {
        last_chunk = G_MAXUINT32;
      } else {
        last_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 12) - 1;
      }
      samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 4);

      GST_LOG_OBJECT (qtdemux,
          "entry %d has first_chunk %d, last_chunk %d, samples_per_chunk %d", i,
          first_chunk, last_chunk, samples_per_chunk);

      for (j = first_chunk; j < last_chunk; j++) {
        guint64 chunk_offset;

        if (j >= n_samples)
          goto done2;
        if (stco) {
          chunk_offset = QTDEMUX_GUINT32_GET (stco->data + 16 + j * 4);
        } else {
          chunk_offset = QTDEMUX_GUINT64_GET (co64->data + 16 + j * 8);
        }
        GST_LOG_OBJECT (qtdemux,
            "Creating entry %d with offset %" G_GUINT64_FORMAT, j,
            chunk_offset);

        samples[j].chunk = j;
        samples[j].offset = chunk_offset;

        samples[j].size = (samples_per_chunk * stream->n_channels) /
            stream->samples_per_frame * stream->bytes_per_frame;

        GST_INFO_OBJECT (qtdemux, "sample %d: timestamp %" GST_TIME_FORMAT
            ", size %u", j, GST_TIME_ARGS (timestamp), samples[j].size);

        samples[j].timestamp = timestamp;
        sample_index += samples_per_chunk;

        timestamp = gst_util_uint64_scale_int (sample_index,
            GST_SECOND, stream->timescale);
        samples[j].duration = timestamp - samples[j].timestamp;

        samples[j].keyframe = TRUE;
      }
    }
  }
done2:

  /* parse and prepare segment info from the edit list */
  GST_DEBUG_OBJECT (qtdemux, "looking for edit list container");
  stream->n_segments = 0;
  stream->segments = NULL;
  if ((edts = qtdemux_tree_get_child_by_type (trak, FOURCC_edts))) {
    GNode *elst;
    gint n_segments;
    gint i, count;
    guint64 time, stime;
    guint8 *buffer;

    GST_DEBUG_OBJECT (qtdemux, "looking for edit list");
    if (!(elst = qtdemux_tree_get_child_by_type (edts, FOURCC_elst)))
      goto done3;

    buffer = elst->data;

    n_segments = QTDEMUX_GUINT32_GET (buffer + 12);

    /* we might allocate a bit too much, at least allocate 1 segment */
    stream->segments = g_new (QtDemuxSegment, MAX (n_segments, 1));

    /* segments always start from 0 */
    time = 0;
    stime = 0;
    count = 0;
    for (i = 0; i < n_segments; i++) {
      guint64 duration;
      guint64 media_time;
      QtDemuxSegment *segment;

      media_time = QTDEMUX_GUINT32_GET (buffer + 20 + i * 12);

      /* -1 media time is an empty segment, just ignore it */
      if (media_time == G_MAXUINT32)
        continue;

      duration = QTDEMUX_GUINT32_GET (buffer + 16 + i * 12);

      segment = &stream->segments[count++];

      /* time and duration expressed in global timescale */
      segment->time = stime;
      /* add non scaled values so we don't cause roundoff errors */
      time += duration;
      stime = gst_util_uint64_scale_int (time, GST_SECOND, qtdemux->timescale);
      segment->stop_time = stime;
      segment->duration = stime - segment->time;
      /* media_time expressed in stream timescale */
      segment->media_start =
          gst_util_uint64_scale_int (media_time, GST_SECOND, stream->timescale);
      segment->media_stop = segment->media_start + segment->duration;
      segment->rate = QTDEMUX_FP32_GET (buffer + 24 + i * 12);

      GST_DEBUG_OBJECT (qtdemux, "created segment %d time %" GST_TIME_FORMAT
          ", duration %" GST_TIME_FORMAT ", media_time %" GST_TIME_FORMAT
          ", rate %g", i, GST_TIME_ARGS (segment->time),
          GST_TIME_ARGS (segment->duration),
          GST_TIME_ARGS (segment->media_start), segment->rate);
    }
    GST_DEBUG_OBJECT (qtdemux, "found %d non-empty segments", count);
    stream->n_segments = count;
  }
done3:

  /* no segments, create one to play the complete trak */
  if (stream->n_segments == 0) {
    if (stream->segments == NULL)
      stream->segments = g_new (QtDemuxSegment, 1);

    stream->segments[0].time = 0;
    stream->segments[0].stop_time = qtdemux->segment.duration;
    stream->segments[0].duration = qtdemux->segment.duration;
    stream->segments[0].media_start = 0;
    stream->segments[0].media_stop = qtdemux->segment.duration;
    stream->segments[0].rate = 1.0;

    GST_DEBUG_OBJECT (qtdemux, "created dummy segment");
    stream->n_segments = 1;
  }
  GST_DEBUG_OBJECT (qtdemux, "using %d segments", stream->n_segments);

  gst_qtdemux_add_stream (qtdemux, stream, list);
}

static void
qtdemux_parse_udta (GstQTDemux * qtdemux, GNode * udta)
{
  GNode *meta;
  GNode *ilst;
  GNode *node;

  meta = qtdemux_tree_get_child_by_type (udta, FOURCC_meta);
  if (meta == NULL) {
    GST_LOG ("no meta");
    return;
  }

  ilst = qtdemux_tree_get_child_by_type (meta, FOURCC_ilst);
  if (ilst == NULL) {
    GST_LOG ("no ilst");
    return;
  }

  GST_DEBUG_OBJECT (qtdemux, "new tag list");
  qtdemux->tag_list = gst_tag_list_new ();

  node = qtdemux_tree_get_child_by_type (ilst, FOURCC__nam);
  if (node) {
    qtdemux_tag_add_str (qtdemux, GST_TAG_TITLE, node);
  }

  node = qtdemux_tree_get_child_by_type (ilst, FOURCC__ART);
  if (node) {
    qtdemux_tag_add_str (qtdemux, GST_TAG_ARTIST, node);
  } else {
    node = qtdemux_tree_get_child_by_type (ilst, FOURCC__wrt);
    if (node) {
      qtdemux_tag_add_str (qtdemux, GST_TAG_ARTIST, node);
    } else {
      node = qtdemux_tree_get_child_by_type (ilst, FOURCC__grp);
      if (node) {
        qtdemux_tag_add_str (qtdemux, GST_TAG_ARTIST, node);
      }
    }
  }

  node = qtdemux_tree_get_child_by_type (ilst, FOURCC__alb);
  if (node) {
    qtdemux_tag_add_str (qtdemux, GST_TAG_ALBUM, node);
  }

  node = qtdemux_tree_get_child_by_type (ilst, FOURCC_trkn);
  if (node) {
    qtdemux_tag_add_num (qtdemux, GST_TAG_TRACK_NUMBER,
        GST_TAG_TRACK_COUNT, node);
  }

  node = qtdemux_tree_get_child_by_type (ilst, FOURCC_disc);
  if (node) {
    qtdemux_tag_add_num (qtdemux, GST_TAG_ALBUM_VOLUME_NUMBER,
        GST_TAG_ALBUM_VOLUME_COUNT, node);
  } else {
    node = qtdemux_tree_get_child_by_type (ilst, FOURCC_disk);
    if (node) {
      qtdemux_tag_add_num (qtdemux, GST_TAG_ALBUM_VOLUME_NUMBER,
          GST_TAG_ALBUM_VOLUME_COUNT, node);
    }
  }

  node = qtdemux_tree_get_child_by_type (ilst, FOURCC_gnre);
  if (node) {
    qtdemux_tag_add_gnre (qtdemux, GST_TAG_GENRE, node);
  } else {
    node = qtdemux_tree_get_child_by_type (ilst, FOURCC__gen);
    if (node) {
      qtdemux_tag_add_str (qtdemux, GST_TAG_GENRE, node);
    }
  }
}

static void
qtdemux_tag_add_str (GstQTDemux * qtdemux, const char *tag, GNode * node)
{
  GNode *data;
  char *s;
  int len;
  int type;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QTDEMUX_GUINT32_GET (data->data);
    type = QTDEMUX_GUINT32_GET (data->data + 8);
    if (type == 0x00000001) {
      s = g_strndup ((char *) data->data + 16, len - 16);
      GST_DEBUG_OBJECT (qtdemux, "adding tag %s", s);
      gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE, tag, s, NULL);
      g_free (s);
    }
  }
}

static void
qtdemux_tag_add_num (GstQTDemux * qtdemux, const char *tag1,
    const char *tag2, GNode * node)
{
  GNode *data;
  int len;
  int type;
  int n1, n2;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QTDEMUX_GUINT32_GET (data->data);
    type = QTDEMUX_GUINT32_GET (data->data + 8);
    if (type == 0x00000000 && len >= 22) {
      n1 = GST_READ_UINT16_BE (data->data + 18);
      n2 = GST_READ_UINT16_BE (data->data + 20);
      GST_DEBUG_OBJECT (qtdemux, "adding tag %d/%d", n1, n2);
      gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE,
          tag1, n1, tag2, n2, NULL);
    }
  }
}

static void
qtdemux_tag_add_gnre (GstQTDemux * qtdemux, const char *tag, GNode * node)
{
  const gchar *genres[] = {
    "N/A", "Blues", "Classic Rock", "Country", "Dance", "Disco",
    "Funk", "Grunge", "Hip-Hop", "Jazz", "Metal", "New Age", "Oldies",
    "Other", "Pop", "R&B", "Rap", "Reggae", "Rock", "Techno",
    "Industrial", "Alternative", "Ska", "Death Metal", "Pranks",
    "Soundtrack", "Euro-Techno", "Ambient", "Trip-Hop", "Vocal",
    "Jazz+Funk", "Fusion", "Trance", "Classical", "Instrumental",
    "Acid", "House", "Game", "Sound Clip", "Gospel", "Noise",
    "AlternRock", "Bass", "Soul", "Punk", "Space", "Meditative",
    "Instrumental Pop", "Instrumental Rock", "Ethnic", "Gothic",
    "Darkwave", "Techno-Industrial", "Electronic", "Pop-Folk",
    "Eurodance", "Dream", "Southern Rock", "Comedy", "Cult", "Gangsta",
    "Top 40", "Christian Rap", "Pop/Funk", "Jungle", "Native American",
    "Cabaret", "New Wave", "Psychadelic", "Rave", "Showtunes",
    "Trailer", "Lo-Fi", "Tribal", "Acid Punk", "Acid Jazz", "Polka",
    "Retro", "Musical", "Rock & Roll", "Hard Rock", "Folk",
    "Folk/Rock", "National Folk", "Swing", "Fast-Fusion", "Bebob",
    "Latin", "Revival", "Celtic", "Bluegrass", "Avantgarde",
    "Gothic Rock", "Progressive Rock", "Psychedelic Rock",
    "Symphonic Rock", "Slow Rock", "Big Band", "Chorus",
    "Easy Listening", "Acoustic", "Humour", "Speech", "Chanson",
    "Opera", "Chamber Music", "Sonata", "Symphony", "Booty Bass",
    "Primus", "Porn Groove", "Satire", "Slow Jam", "Club", "Tango",
    "Samba", "Folklore", "Ballad", "Power Ballad", "Rhythmic Soul",
    "Freestyle", "Duet", "Punk Rock", "Drum Solo", "A capella",
    "Euro-House", "Dance Hall", "Goa", "Drum & Bass", "Club House",
    "Hardcore", "Terror", "Indie", "BritPop", "NegerPunk",
    "Polsk Punk", "Beat", "Christian Gangsta", "Heavy Metal",
    "Black Metal", "Crossover", "Contemporary C", "Christian Rock",
    "Merengue", "Salsa", "Thrash Metal", "Anime", "JPop", "SynthPop"
  };
  GNode *data;
  int len;
  int type;
  int n;

  data = qtdemux_tree_get_child_by_type (node, FOURCC_data);
  if (data) {
    len = QTDEMUX_GUINT32_GET (data->data);
    type = QTDEMUX_GUINT32_GET (data->data + 8);
    if (type == 0x00000000 && len >= 18) {
      n = GST_READ_UINT16_BE (data->data + 16);
      if (n > 0 && n < sizeof (genres) / sizeof (char *)) {
        GST_DEBUG_OBJECT (qtdemux, "adding %d [%s]", n, genres[n]);
        gst_tag_list_add (qtdemux->tag_list, GST_TAG_MERGE_REPLACE,
            tag, genres[n], NULL);
      }
    }
  }
}

/* taken from ffmpeg */
static unsigned int
get_size (guint8 * ptr, guint8 ** end)
{
  int count = 4;
  int len = 0;

  while (count--) {
    int c = *ptr;

    ptr++;
    len = (len << 7) | (c & 0x7f);
    if (!(c & 0x80))
      break;
  }
  if (end)
    *end = ptr;
  return len;
}

static void
gst_qtdemux_handle_esds (GstQTDemux * qtdemux, QtDemuxStream * stream,
    GNode * esds)
{
  int len = QTDEMUX_GUINT32_GET (esds->data);
  guint8 *ptr = esds->data;
  guint8 *end = ptr + len;
  int tag;
  guint8 *data_ptr = NULL;
  int data_len = 0;

  gst_util_dump_mem (ptr, len);
  ptr += 8;
  GST_DEBUG_OBJECT (qtdemux, "version/flags = %08x", QTDEMUX_GUINT32_GET (ptr));
  ptr += 4;
  while (ptr < end) {
    tag = QTDEMUX_GUINT8_GET (ptr);
    GST_DEBUG_OBJECT (qtdemux, "tag = %02x", tag);
    ptr++;
    len = get_size (ptr, &ptr);
    GST_DEBUG_OBJECT (qtdemux, "len = %d", len);

    switch (tag) {
      case 0x03:
        GST_DEBUG_OBJECT (qtdemux, "ID %04x", QTDEMUX_GUINT16_GET (ptr));
        GST_DEBUG_OBJECT (qtdemux, "priority %04x",
            QTDEMUX_GUINT8_GET (ptr + 2));
        ptr += 3;
        break;
      case 0x04:
        GST_DEBUG_OBJECT (qtdemux, "object_type_id %02x",
            QTDEMUX_GUINT8_GET (ptr));
        GST_DEBUG_OBJECT (qtdemux, "stream_type %02x",
            QTDEMUX_GUINT8_GET (ptr + 1));
        GST_DEBUG_OBJECT (qtdemux, "buffer_size_db %02x",
            QTDEMUX_GUINT24_GET (ptr + 2));
        GST_DEBUG_OBJECT (qtdemux, "max bitrate %d",
            QTDEMUX_GUINT32_GET (ptr + 5));
        GST_DEBUG_OBJECT (qtdemux, "avg bitrate %d",
            QTDEMUX_GUINT32_GET (ptr + 9));
        ptr += 13;
        break;
      case 0x05:
        GST_DEBUG_OBJECT (qtdemux, "data:");
        gst_util_dump_mem (ptr, len);
        data_ptr = ptr;
        data_len = len;
        ptr += len;
        break;
      case 0x06:
        GST_DEBUG_OBJECT (qtdemux, "data %02x", QTDEMUX_GUINT8_GET (ptr));
        ptr += 1;
        break;
      default:
        GST_ERROR ("parse error");
    }
  }

  if (data_ptr) {
    GstBuffer *buffer;

    buffer = gst_buffer_new_and_alloc (data_len);
    memcpy (GST_BUFFER_DATA (buffer), data_ptr, data_len);
    gst_util_dump_mem (GST_BUFFER_DATA (buffer), data_len);

    gst_caps_set_simple (stream->caps, "codec_data", GST_TYPE_BUFFER,
        buffer, NULL);
    gst_buffer_unref (buffer);
  }
}

#define _codec(name) \
  do { \
    if (codec_name) { \
      *codec_name = name; \
    } \
  } while (0)

static GstCaps *
qtdemux_video_caps (GstQTDemux * qtdemux, guint32 fourcc,
    const guint8 * stsd_data, const gchar ** codec_name)
{
  switch (fourcc) {
    case GST_MAKE_FOURCC ('p', 'n', 'g', ' '):
      _codec ("PNG still images");
      return gst_caps_from_string ("image/png");
    case GST_MAKE_FOURCC ('j', 'p', 'e', 'g'):
      _codec ("JPEG still images");
      return gst_caps_from_string ("image/jpeg");
    case GST_MAKE_FOURCC ('m', 'j', 'p', 'a'):
    case GST_MAKE_FOURCC ('A', 'V', 'D', 'J'):
      _codec ("Motion-JPEG");
      return gst_caps_from_string ("image/jpeg");
    case GST_MAKE_FOURCC ('m', 'j', 'p', 'b'):
      _codec ("Motion-JPEG format B");
      return gst_caps_from_string ("video/x-mjpeg-b");
    case GST_MAKE_FOURCC ('S', 'V', 'Q', '3'):
      _codec ("Sorensen video v.3");
      return gst_caps_from_string ("video/x-svq, " "svqversion = (int) 3");
    case GST_MAKE_FOURCC ('s', 'v', 'q', 'i'):
    case GST_MAKE_FOURCC ('S', 'V', 'Q', '1'):
      _codec ("Sorensen video v.1");
      return gst_caps_from_string ("video/x-svq, " "svqversion = (int) 1");
    case GST_MAKE_FOURCC ('r', 'a', 'w', ' '):
      _codec ("Raw RGB video");
      return gst_caps_from_string ("video/x-raw-rgb, "
          "endianness = (int) BIG_ENDIAN");
      /*"bpp", GST_PROPS_INT(x),
         "depth", GST_PROPS_INT(x),
         "red_mask", GST_PROPS_INT(x),
         "green_mask", GST_PROPS_INT(x),
         "blue_mask", GST_PROPS_INT(x), FIXME! */
    case GST_MAKE_FOURCC ('Y', 'u', 'v', '2'):
      _codec ("Raw packed YUV 4:2:2");
      return gst_caps_from_string ("video/x-raw-yuv, "
          "format = (fourcc) YUY2");
    case GST_MAKE_FOURCC ('m', 'p', 'e', 'g'):
      _codec ("MPEG-1 video");
      return gst_caps_from_string ("video/mpeg, "
          "systemstream = (boolean) false, " "mpegversion = (int) 1");
    case GST_MAKE_FOURCC ('g', 'i', 'f', ' '):
      _codec ("GIF still images");
      return gst_caps_from_string ("image/gif");
    case GST_MAKE_FOURCC ('h', '2', '6', '3'):
    case GST_MAKE_FOURCC ('s', '2', '6', '3'):
      _codec ("H.263");
      /* ffmpeg uses the height/width props, don't know why */
      return gst_caps_from_string ("video/x-h263");
    case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
      _codec ("MPEG-4 video");
      return gst_caps_from_string ("video/mpeg, "
          "mpegversion = (int) 4, " "systemstream = (boolean) false");
    case GST_MAKE_FOURCC ('3', 'i', 'v', 'd'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', 'D'):
      _codec ("Microsoft MPEG-4 4.3");  /* FIXME? */
      return gst_caps_from_string ("video/x-msmpeg, msmpegversion = (int) 43");
    case GST_MAKE_FOURCC ('3', 'I', 'V', '1'):
    case GST_MAKE_FOURCC ('3', 'I', 'V', '2'):
      _codec ("3ivX video");
      return gst_caps_from_string ("video/x-3ivx");
    case GST_MAKE_FOURCC ('D', 'I', 'V', '3'):
      _codec ("DivX 3");
      return gst_caps_from_string ("video/x-divx," "divxversion= (int) 3");
    case GST_MAKE_FOURCC ('D', 'I', 'V', 'X'):
      _codec ("DivX 4");
      return gst_caps_from_string ("video/x-divx," "divxversion= (int) 4");

    case GST_MAKE_FOURCC ('D', 'X', '5', '0'):
      _codec ("DivX 5");
      return gst_caps_from_string ("video/x-divx," "divxversion= (int) 5");
    case GST_MAKE_FOURCC ('c', 'v', 'i', 'd'):
      _codec ("Cinepak");
      return gst_caps_from_string ("video/x-cinepak");
    case GST_MAKE_FOURCC ('r', 'p', 'z', 'a'):
      _codec ("Apple video");
      return gst_caps_from_string ("video/x-apple-video");
    case GST_MAKE_FOURCC ('a', 'v', 'c', '1'):
      _codec ("H.264 / AVC");
      return gst_caps_from_string ("video/x-h264");
    case GST_MAKE_FOURCC ('r', 'l', 'e', ' '):
      _codec ("Run-length encoding");
      return gst_caps_from_string ("video/x-rle, layout=(string)quicktime");
    case GST_MAKE_FOURCC ('i', 'v', '3', '2'):
      _codec ("Indeo Video 3");
      return gst_caps_from_string ("video/x-indeo, indeoversion=(int)3");
    case GST_MAKE_FOURCC ('d', 'v', 'c', 'p'):
    case GST_MAKE_FOURCC ('d', 'v', 'c', ' '):
    case GST_MAKE_FOURCC ('d', 'v', 's', 'd'):
    case GST_MAKE_FOURCC ('d', 'v', '2', '5'):
      _codec ("DV Video");
      return gst_caps_from_string ("video/x-dv, systemstream=(boolean)false");
    case GST_MAKE_FOURCC ('s', 'm', 'c', ' '):
      _codec ("Apple Graphics (SMC)");
      return gst_caps_from_string ("video/x-smc");
    case GST_MAKE_FOURCC ('V', 'P', '3', '1'):
      _codec ("VP3");
      return gst_caps_from_string ("video/x-vp3");
    case GST_MAKE_FOURCC ('k', 'p', 'c', 'd'):
    default:
#if 0
      g_critical ("Don't know how to convert fourcc '%" GST_FOURCC_FORMAT
          "' to caps", GST_FOURCC_ARGS (fourcc));
      return NULL;
#endif
      {
        char *s;

        s = g_strdup_printf ("video/x-gst-fourcc-%" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (fourcc));
        return gst_caps_new_simple (s, NULL);
      }
  }
}

static GstCaps *
qtdemux_audio_caps (GstQTDemux * qtdemux, QtDemuxStream * stream,
    guint32 fourcc, const guint8 * data, int len, const gchar ** codec_name)
{
  switch (fourcc) {
#if 0
    case GST_MAKE_FOURCC ('N', 'O', 'N', 'E'):
      return NULL;              /*gst_caps_from_string ("audio/raw"); */
#endif
    case GST_MAKE_FOURCC ('r', 'a', 'w', ' '):
      _codec ("Raw 8-bit PCM audio");
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 8, " "depth = (int) 8, " "signed = (boolean) false");
    case GST_MAKE_FOURCC ('t', 'w', 'o', 's'):
      if (stream->bytes_per_frame == 1) {
        _codec ("Raw 8-bit PCM audio");
        return gst_caps_from_string ("audio/x-raw-int, "
            "width = (int) 8, " "depth = (int) 8, " "signed = (boolean) true");
      } else {
        _codec ("Raw 16-bit PCM audio");
        /* FIXME */
        return gst_caps_from_string ("audio/x-raw-int, "
            "width = (int) 16, "
            "depth = (int) 16, "
            "endianness = (int) BIG_ENDIAN, " "signed = (boolean) true");
      }
    case GST_MAKE_FOURCC ('s', 'o', 'w', 't'):
      if (stream->bytes_per_frame == 1) {
        _codec ("Raw 8-bit PCM audio");
        return gst_caps_from_string ("audio/x-raw-int, "
            "width = (int) 8, " "depth = (int) 8, " "signed = (boolean) true");
      } else {
        _codec ("Raw 16-bit PCM audio");
        /* FIXME */
        return gst_caps_from_string ("audio/x-raw-int, "
            "width = (int) 16, "
            "depth = (int) 16, "
            "endianness = (int) LITTLE_ENDIAN, " "signed = (boolean) true");
      }
    case GST_MAKE_FOURCC ('f', 'l', '6', '4'):
      _codec ("Raw 64-bit floating-point audio");
      return gst_caps_from_string ("audio/x-raw-float, "
          "width = (int) 64, " "endianness = (int) BIG_ENDIAN");
    case GST_MAKE_FOURCC ('f', 'l', '3', '2'):
      _codec ("Raw 32-bit floating-point audio");
      return gst_caps_from_string ("audio/x-raw-float, "
          "width = (int) 32, " "endianness = (int) BIG_ENDIAN");
    case GST_MAKE_FOURCC ('i', 'n', '2', '4'):
      _codec ("Raw 24-bit PCM audio");
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 24, "
          "depth = (int) 32, "
          "endianness = (int) BIG_ENDIAN, " "signed = (boolean) true");
    case GST_MAKE_FOURCC ('i', 'n', '3', '2'):
      _codec ("Raw 32-bit PCM audio");
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 32, "
          "depth = (int) 32, "
          "endianness = (int) BIG_ENDIAN, " "signed = (boolean) true");
    case GST_MAKE_FOURCC ('u', 'l', 'a', 'w'):
      _codec ("Mu-law audio");
      /* FIXME */
      return gst_caps_from_string ("audio/x-mulaw");
    case GST_MAKE_FOURCC ('a', 'l', 'a', 'w'):
      _codec ("A-law audio");
      /* FIXME */
      return gst_caps_from_string ("audio/x-alaw");
    case 0x6d730002:
      _codec ("Microsoft ADPCM");
      /* Microsoft ADPCM-ACM code 2 */
      return gst_caps_from_string ("audio/x-adpcm, "
          "layout = (string) microsoft");
    case 0x6d730011:
    case 0x6d730017:
      _codec ("DVI/Intel IMA ADPCM");
      /* FIXME DVI/Intel IMA ADPCM/ACM code 17 */
      return gst_caps_from_string ("audio/x-adpcm, "
          "layout = (string) quicktime");
    case 0x6d730055:
      /* MPEG layer 3, CBR only (pre QT4.1) */
    case 0x5500736d:
    case GST_MAKE_FOURCC ('.', 'm', 'p', '3'):
      _codec ("MPEG-1 layer 3");
      /* MPEG layer 3, CBR & VBR (QT4.1 and later) */
      return gst_caps_from_string ("audio/mpeg, "
          "layer = (int) 3, " "mpegversion = (int) 1");
    case GST_MAKE_FOURCC ('M', 'A', 'C', '3'):
      _codec ("MACE-3");
      return gst_caps_from_string ("audio/x-mace, " "maceversion = (int) 3");
    case GST_MAKE_FOURCC ('M', 'A', 'C', '6'):
      _codec ("MACE-6");
      return gst_caps_from_string ("audio/x-mace, " "maceversion = (int) 6");
    case GST_MAKE_FOURCC ('O', 'g', 'g', 'V'):
      /* ogg/vorbis */
      return gst_caps_from_string ("application/ogg");
    case GST_MAKE_FOURCC ('d', 'v', 'c', 'a'):
      _codec ("DV audio");
      return gst_caps_from_string ("audio/x-dv");
    case GST_MAKE_FOURCC ('m', 'p', '4', 'a'):
      _codec ("MPEG-4 AAC audio");
      return gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, "framed", G_TYPE_BOOLEAN, TRUE, NULL);
    case GST_MAKE_FOURCC ('Q', 'D', 'M', '2'):
      _codec ("QDesign Music v.2");
      /* FIXME: QDesign music version 2 (no constant) */
      if (data) {
        return gst_caps_new_simple ("audio/x-qdm2",
            "framesize", G_TYPE_INT, QTDEMUX_GUINT32_GET (data + 52),
            "bitrate", G_TYPE_INT, QTDEMUX_GUINT32_GET (data + 40),
            "blocksize", G_TYPE_INT, QTDEMUX_GUINT32_GET (data + 44), NULL);
      } else {
        return gst_caps_new_simple ("audio/x-qdm2", NULL);
      }
    case GST_MAKE_FOURCC ('a', 'g', 's', 'm'):
      _codec ("GSM audio");
      return gst_caps_new_simple ("audio/x-gsm", NULL);
    case GST_MAKE_FOURCC ('s', 'a', 'm', 'r'):
      _codec ("AMR audio");
      return gst_caps_new_simple ("audio/AMR", NULL);
    case GST_MAKE_FOURCC ('i', 'm', 'a', '4'):
      _codec ("Quicktime IMA ADPCM");
      return gst_caps_new_simple ("audio/x-adpcm",
          "layout", G_TYPE_STRING, "quicktime", NULL);
    case GST_MAKE_FOURCC ('a', 'l', 'a', 'c'):
      _codec ("Apple lossless audio");
      return gst_caps_new_simple ("audio/x-alac", NULL);
    case GST_MAKE_FOURCC ('q', 't', 'v', 'r'):
      /* ? */
    case GST_MAKE_FOURCC ('Q', 'D', 'M', 'C'):
      /* QDesign music */
    case GST_MAKE_FOURCC ('Q', 'c', 'l', 'p'):
      /* QUALCOMM PureVoice */
    default:
#if 0
      g_critical ("Don't know how to convert fourcc '%" GST_FOURCC_FORMAT
          "' to caps", GST_FOURCC_ARGS (fourcc));
      return NULL;
#endif
      {
        char *s;

        s = g_strdup_printf ("audio/x-gst-fourcc-%" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (fourcc));
        return gst_caps_new_simple (s, NULL);
      }
  }
}
