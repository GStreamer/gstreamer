/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
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

#include <string.h>
#include <zlib.h>

GST_DEBUG_CATEGORY_EXTERN (qtdemux_debug);
#define GST_CAT_DEFAULT qtdemux_debug

#define QTDEMUX_GUINT32_GET(a) GUINT32_FROM_BE(*(guint32 *)(a))
#define QTDEMUX_GUINT16_GET(a) GUINT16_FROM_BE(*(guint16 *)(a))
#define QTDEMUX_GUINT8_GET(a) (*(guint8 *)(a))
#define QTDEMUX_FP32_GET(a) (GUINT32_FROM_BE(*(guint16 *)(a))/65536.0)
#define QTDEMUX_FP16_GET(a) (GUINT16_FROM_BE(*(guint16 *)(a))/256.0)
#define QTDEMUX_FOURCC_GET(a) GUINT32_FROM_LE(*(guint32 *)(a))

#define QTDEMUX_GUINT64_GET(a) ((((guint64)QTDEMUX_GUINT32_GET(a))<<32)|QTDEMUX_GUINT32_GET(((void *)a)+4))

typedef struct _QtNode QtNode;
typedef struct _QtNodeType QtNodeType;
typedef struct _QtDemuxSample QtDemuxSample;

//typedef struct _QtDemuxStream QtDemuxStream;

struct _QtNode
{
  guint32 type;
  gpointer data;
  int len;
};

struct _QtNodeType
{
  guint32 fourcc;
  char *name;
  int flags;
  void (*dump) (GstQTDemux * qtdemux, void *buffer, int depth);
};

struct _QtDemuxSample
{
  int sample_index;
  int chunk;
  int size;
  guint32 offset;
  guint64 timestamp;
  guint64 duration;
};

struct _QtDemuxStream
{
  guint32 subtype;
  GstCaps *caps;
  GstPad *pad;
  int n_samples;
  QtDemuxSample *samples;
  int timescale;

  int sample_index;

  int width;
  int height;
  float fps;

  double rate;
  int n_channels;
  guint bytes_per_frame;
  guint compression;
  guint samples_per_packet;
};

enum QtDemuxState
{
  QTDEMUX_STATE_NULL,
  QTDEMUX_STATE_HEADER,
  QTDEMUX_STATE_HEADER_SEEKING,
  QTDEMUX_STATE_SEEKING,
  QTDEMUX_STATE_MOVIE,
  QTDEMUX_STATE_SEEKING_EOS,
  QTDEMUX_STATE_EOS,
};

static GNode *qtdemux_tree_get_child_by_type (GNode * node, guint32 fourcc);
static GNode *qtdemux_tree_get_sibling_by_type (GNode * node, guint32 fourcc);

static GstElementDetails gst_qtdemux_details = {
  "QuickTime Demuxer",
  "Codec/Demuxer",
  "Demultiplex a QuickTime file into audio and video streams",
  "David Schleef <ds@schleef.org>"
};

enum
{
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_qtdemux_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/quicktime; audio/x-m4a")
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

static void gst_qtdemux_class_init (GstQTDemuxClass * klass);
static void gst_qtdemux_base_init (GstQTDemuxClass * klass);
static void gst_qtdemux_init (GstQTDemux * quicktime_demux);
static GstElementStateReturn gst_qtdemux_change_state (GstElement * element);
static void gst_qtdemux_loop_header (GstElement * element);
static gboolean gst_qtdemux_handle_sink_event (GstQTDemux * qtdemux);

static void qtdemux_parse_moov (GstQTDemux * qtdemux, void *buffer, int length);
static void qtdemux_parse (GstQTDemux * qtdemux, GNode * node, void *buffer,
    int length);
static QtNodeType *qtdemux_type_get (guint32 fourcc);
static void qtdemux_node_dump (GstQTDemux * qtdemux, GNode * node);
static void qtdemux_parse_tree (GstQTDemux * qtdemux);
static GstCaps *qtdemux_video_caps (GstQTDemux * qtdemux, guint32 fourcc,
    const guint8 * stsd_data);
static GstCaps *qtdemux_audio_caps (GstQTDemux * qtdemux, guint32 fourcc,
    const guint8 * data, int len);

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

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_qtdemux_change_state;
}

static void
gst_qtdemux_init (GstQTDemux * qtdemux)
{
  qtdemux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_qtdemux_sink_template), "sink");
  gst_element_set_loop_function (GST_ELEMENT (qtdemux),
      gst_qtdemux_loop_header);
  gst_element_add_pad (GST_ELEMENT (qtdemux), qtdemux->sinkpad);
}

static const GstFormat *
gst_qtdemux_get_src_formats (GstPad * pad)
{
  static const GstFormat src_a_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_BYTES,
    GST_FORMAT_DEFAULT,
    0
  };
  static const GstFormat src_v_formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_DEFAULT,
    0
  };
  QtDemuxStream *stream = gst_pad_get_element_private (pad);

  return (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e')) ?
      src_v_formats : src_a_formats;
}

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

static const GstQueryType *
gst_qtdemux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType src_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return src_types;
}

static const GstEventMask *
gst_qtdemux_get_event_mask (GstPad * pad)
{
  static const GstEventMask masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_KEY_UNIT},
    {0,}
  };

  return masks;
}

static gboolean
gst_qtdemux_handle_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = TRUE;

  //QtDemuxStream *stream = gst_pad_get_element_private(pad);

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = 0;           /* FIXME */
          break;
        case GST_FORMAT_BYTES:
          *value = 0;           /* FIXME */
          break;
        case GST_FORMAT_DEFAULT:
          *value = 0;           /* FIXME */
          break;
        default:
          res = FALSE;
          break;
      }
      break;
    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = 0;           /* FIXME */
          break;
        case GST_FORMAT_BYTES:
          *value = 0;           /* FIXME */
          break;
        case GST_FORMAT_DEFAULT:
          *value = 0;           /* FIXME */
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

  return res;
}

static gboolean
gst_qtdemux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;

  //QtDemuxStream *stream = gst_pad_get_element_private(pad);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      GST_DEBUG ("seek format %d", GST_EVENT_SEEK_FORMAT (event));

      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_BYTES:
        case GST_FORMAT_DEFAULT:
        case GST_FORMAT_TIME:
        {
          gint64 desired_offset = GST_EVENT_SEEK_OFFSET (event);

          GST_DEBUG ("seeking to %" G_GINT64_FORMAT, desired_offset);

          res = FALSE;
        }
        default:
          res = FALSE;
          break;
      }
    default:
      res = FALSE;
      break;
  }

  gst_event_unref (event);

  return res;
}



GST_DEBUG_CATEGORY (qtdemux_debug);

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (qtdemux_debug, "qtdemux", 0, "qtdemux plugin");

  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  if (!gst_library_load ("gstgetbits"))
    return FALSE;

  return gst_element_register (plugin, "qtdemux",
      GST_RANK_PRIMARY, GST_TYPE_QTDEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "qtdemux",
    "Quicktime stream demuxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN);

static gboolean
gst_qtdemux_handle_sink_event (GstQTDemux * qtdemux)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;

  gst_bytestream_get_status (qtdemux->bs, &remaining, &event);

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;
  GST_DEBUG ("qtdemux: event %p %d", event, type);

  switch (type) {
    case GST_EVENT_EOS:
      gst_bytestream_flush (qtdemux->bs, remaining);
      gst_pad_event_default (qtdemux->sinkpad, event);
      return FALSE;
    case GST_EVENT_FLUSH:
      //g_warning("flush event");
      break;
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG ("discontinuous event");
      //gst_bytestream_flush_fast(qtdemux->bs, remaining);
      break;
    default:
      g_warning ("unhandled event %d", type);
      break;
  }

  gst_event_unref (event);
  return TRUE;
}

static GstElementStateReturn
gst_qtdemux_change_state (GstElement * element)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      qtdemux->bs = gst_bytestream_new (qtdemux->sinkpad);
      qtdemux->state = QTDEMUX_STATE_HEADER;
      /* FIXME */
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (qtdemux->bs);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static void
gst_qtdemux_loop_header (GstElement * element)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (element);
  guint8 *data;
  guint32 length;
  guint32 fourcc;
  GstBuffer *buf;
  int offset;
  int cur_offset;
  int size;
  int ret;

  /* FIXME _tell gets the offset wrong */
  //cur_offset = gst_bytestream_tell(qtdemux->bs);

  cur_offset = qtdemux->offset;
  GST_DEBUG ("loop at position %d", cur_offset);

  switch (qtdemux->state) {
    case QTDEMUX_STATE_HEADER:
    {
      do {
        ret = gst_bytestream_peek_bytes (qtdemux->bs, &data, 16);
        if (ret < 16) {
          if (!gst_qtdemux_handle_sink_event (qtdemux)) {
            return;
          }
        } else {
          break;
        }
      } while (1);

      length = GUINT32_FROM_BE (*(guint32 *) data);
      GST_DEBUG ("length %08x", length);
      fourcc = GUINT32_FROM_LE (*(guint32 *) (data + 4));
      GST_DEBUG ("fourcc " GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));

      if (length == 0) {
        length = gst_bytestream_length (qtdemux->bs) - cur_offset;
      }
      if (length == 1) {
        guint32 length1, length2;

        length1 = GUINT32_FROM_BE (*(guint32 *) (data + 8));
        GST_DEBUG ("length1 %08x", length1);
        length2 = GUINT32_FROM_BE (*(guint32 *) (data + 12));
        GST_DEBUG ("length2 %08x", length2);

        length = length2;
      }

      switch (fourcc) {
        case GST_MAKE_FOURCC ('m', 'd', 'a', 't'):
        case GST_MAKE_FOURCC ('f', 'r', 'e', 'e'):
        case GST_MAKE_FOURCC ('w', 'i', 'd', 'e'):
        case GST_MAKE_FOURCC ('P', 'I', 'C', 'T'):
        case GST_MAKE_FOURCC ('p', 'n', 'o', 't'):
          break;
        case GST_MAKE_FOURCC ('m', 'o', 'o', 'v'):
        {
          GstBuffer *moov;

          do {
            ret = gst_bytestream_read (qtdemux->bs, &moov, length);
            if (ret < length) {
              GST_DEBUG ("read failed (%d < %d)", ret, length);
              if (!gst_qtdemux_handle_sink_event (qtdemux)) {
                return;
              }
            } else {
              break;
            }
          } while (1);

          qtdemux_parse_moov (qtdemux, GST_BUFFER_DATA (moov), length);
          if (1)
            qtdemux_node_dump (qtdemux, qtdemux->moov_node);
          qtdemux_parse_tree (qtdemux);
          qtdemux->state = QTDEMUX_STATE_MOVIE;
          break;
        }
        default:
        {
          GST_LOG ("unknown %08x '" GST_FOURCC_FORMAT "' at %d",
              fourcc, GST_FOURCC_ARGS (fourcc), cur_offset);
          break;
        }
      }
      ret = gst_bytestream_seek (qtdemux->bs, cur_offset + length,
          GST_SEEK_METHOD_SET);
      qtdemux->offset = cur_offset + length;
      GST_DEBUG ("seek returned %d", ret);
      break;
    }
    case QTDEMUX_STATE_SEEKING_EOS:
    {
      guint8 *data;

      do {
        ret = gst_bytestream_peek_bytes (qtdemux->bs, &data, 1);
        if (ret < 1) {
          if (!gst_qtdemux_handle_sink_event (qtdemux)) {
            return;
          }
        } else {
          break;
        }
      } while (TRUE);
      gst_element_set_eos (element);

      qtdemux->state = QTDEMUX_STATE_EOS;
      return;
    }
    case QTDEMUX_STATE_EOS:
      g_warning ("spinning in EOS\n");
      return;
    case QTDEMUX_STATE_MOVIE:
    {
      QtDemuxStream *stream;
      guint64 min_time;
      int index = -1;
      int i;

      min_time = G_MAXUINT64;
      for (i = 0; i < qtdemux->n_streams; i++) {
        stream = qtdemux->streams[i];

        if (stream->sample_index < stream->n_samples &&
            stream->samples[stream->sample_index].timestamp < min_time) {
          min_time = stream->samples[stream->sample_index].timestamp;
          index = i;
        }
      }

      if (index == -1) {
        for (i = 0; i < qtdemux->n_streams; i++) {
          gst_pad_push (qtdemux->streams[i]->pad,
              GST_DATA (gst_event_new (GST_EVENT_EOS)));
        }
        ret = gst_bytestream_seek (qtdemux->bs, 0, GST_SEEK_METHOD_END);
        GST_DEBUG ("seek returned %d", ret);

        qtdemux->state = QTDEMUX_STATE_SEEKING_EOS;
        return;
      }

      stream = qtdemux->streams[index];

      offset = stream->samples[stream->sample_index].offset;
      size = stream->samples[stream->sample_index].size;

      GST_INFO
          ("pushing from stream %d, sample_index=%d offset=%d size=%d timestamp=%lld",
          index, stream->sample_index, offset, size,
          stream->samples[stream->sample_index].timestamp);

      cur_offset = gst_bytestream_tell (qtdemux->bs);
      if (offset != cur_offset) {
        GST_DEBUG ("seeking to offset %d", offset);
        GST_LOG ("seeking to offset %d", offset);
        ret = gst_bytestream_seek (qtdemux->bs, offset, GST_SEEK_METHOD_SET);
        GST_DEBUG ("seek returned %d", ret);
        return;
      }

      GST_DEBUG ("reading %d bytes", size);
      buf = NULL;
      do {
        ret = gst_bytestream_read (qtdemux->bs, &buf, size);
        if (ret < size) {
          GST_DEBUG ("read failed (%d < %d)", ret, size);
          if (!gst_qtdemux_handle_sink_event (qtdemux)) {
            return;
          }
        } else {
          break;
        }
      } while (TRUE);

      if (buf) {
        /* hum... */
        if (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e')) {
          float fps =
              1. * GST_SECOND / stream->samples[stream->sample_index].duration;
          if (fps != stream->fps) {
            gst_caps_set_simple (stream->caps, "framerate", G_TYPE_DOUBLE, fps,
                NULL);
            stream->fps = fps;
            gst_pad_set_explicit_caps (stream->pad, stream->caps);
          }
        }

        GST_BUFFER_TIMESTAMP (buf) =
            stream->samples[stream->sample_index].timestamp;
        GST_BUFFER_DURATION (buf) =
            stream->samples[stream->sample_index].duration;
        gst_pad_push (stream->pad, GST_DATA (buf));

        GST_INFO ("pushing buffer on %" GST_PTR_FORMAT, stream->pad);
      }
      stream->sample_index++;
      break;
    }
    default:
      /* unreached */
      g_assert (0);
  }

}

void
gst_qtdemux_add_stream (GstQTDemux * qtdemux, QtDemuxStream * stream)
{
  if (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e')) {
    gchar *name = g_strdup_printf ("video_%02d", qtdemux->n_video_streams);

    stream->pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_qtdemux_videosrc_template), name);
    g_free (name);
    stream->fps = 1. * GST_SECOND / stream->samples[0].duration;
    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "width", G_TYPE_INT, stream->width,
          "height", G_TYPE_INT, stream->height,
          "framerate", G_TYPE_DOUBLE, stream->fps, NULL);
    }
    qtdemux->n_video_streams++;
  } else {
    gchar *name = g_strdup_printf ("audio_%02d", qtdemux->n_audio_streams);

    stream->pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_qtdemux_audiosrc_template), name);
    g_free (name);
    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, stream->n_channels, NULL);
    }
    qtdemux->n_audio_streams++;
  }

  gst_pad_use_explicit_caps (stream->pad);

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  qtdemux->streams[qtdemux->n_streams] = stream;
  qtdemux->n_streams++;
  GST_DEBUG ("n_streams is now %d", qtdemux->n_streams);

  gst_pad_set_event_mask_function (stream->pad, gst_qtdemux_get_event_mask);
  gst_pad_set_event_function (stream->pad, gst_qtdemux_handle_src_event);
  gst_pad_set_query_type_function (stream->pad,
      gst_qtdemux_get_src_query_types);
  gst_pad_set_query_function (stream->pad, gst_qtdemux_handle_src_query);
  gst_pad_set_formats_function (stream->pad, gst_qtdemux_get_src_formats);
  gst_pad_set_convert_function (stream->pad, gst_qtdemux_src_convert);

  gst_pad_set_explicit_caps (stream->pad, stream->caps);

  GST_DEBUG ("adding pad %p to qtdemux %p", stream->pad, qtdemux);
  gst_element_add_pad (GST_ELEMENT (qtdemux), stream->pad);
}


#define QT_CONTAINER 1

#define FOURCC_moov	GST_MAKE_FOURCC('m','o','o','v')
#define FOURCC_mvhd	GST_MAKE_FOURCC('m','v','h','d')
#define FOURCC_clip	GST_MAKE_FOURCC('c','l','i','p')
#define FOURCC_trak	GST_MAKE_FOURCC('t','r','a','k')
#define FOURCC_udta	GST_MAKE_FOURCC('u','d','t','a')
#define FOURCC_ctab	GST_MAKE_FOURCC('c','t','a','b')
#define FOURCC_tkhd	GST_MAKE_FOURCC('t','k','h','d')
#define FOURCC_crgn	GST_MAKE_FOURCC('c','r','g','n')
#define FOURCC_matt	GST_MAKE_FOURCC('m','a','t','t')
#define FOURCC_kmat	GST_MAKE_FOURCC('k','m','a','t')
#define FOURCC_edts	GST_MAKE_FOURCC('e','d','t','s')
#define FOURCC_elst	GST_MAKE_FOURCC('e','l','s','t')
#define FOURCC_load	GST_MAKE_FOURCC('l','o','a','d')
#define FOURCC_tref	GST_MAKE_FOURCC('t','r','e','f')
#define FOURCC_imap	GST_MAKE_FOURCC('i','m','a','p')
#define FOURCC___in	GST_MAKE_FOURCC(' ',' ','i','n')
#define FOURCC___ty	GST_MAKE_FOURCC(' ',' ','t','y')
#define FOURCC_mdia	GST_MAKE_FOURCC('m','d','i','a')
#define FOURCC_mdhd	GST_MAKE_FOURCC('m','d','h','d')
#define FOURCC_hdlr	GST_MAKE_FOURCC('h','d','l','r')
#define FOURCC_minf	GST_MAKE_FOURCC('m','i','n','f')
#define FOURCC_vmhd	GST_MAKE_FOURCC('v','m','h','d')
#define FOURCC_smhd	GST_MAKE_FOURCC('s','m','h','d')
#define FOURCC_gmhd	GST_MAKE_FOURCC('g','m','h','d')
#define FOURCC_gmin	GST_MAKE_FOURCC('g','m','i','n')
#define FOURCC_dinf	GST_MAKE_FOURCC('d','i','n','f')
#define FOURCC_dref	GST_MAKE_FOURCC('d','r','e','f')
#define FOURCC_stbl	GST_MAKE_FOURCC('s','t','b','l')
#define FOURCC_stsd	GST_MAKE_FOURCC('s','t','s','d')
#define FOURCC_stts	GST_MAKE_FOURCC('s','t','t','s')
#define FOURCC_stss	GST_MAKE_FOURCC('s','t','s','s')
#define FOURCC_stsc	GST_MAKE_FOURCC('s','t','s','c')
#define FOURCC_stsz	GST_MAKE_FOURCC('s','t','s','z')
#define FOURCC_stco	GST_MAKE_FOURCC('s','t','c','o')
#define FOURCC_vide	GST_MAKE_FOURCC('v','i','d','e')
#define FOURCC_soun	GST_MAKE_FOURCC('s','o','u','n')
#define FOURCC_co64	GST_MAKE_FOURCC('c','o','6','4')
#define FOURCC_cmov	GST_MAKE_FOURCC('c','m','o','v')
#define FOURCC_dcom	GST_MAKE_FOURCC('d','c','o','m')
#define FOURCC_cmvd	GST_MAKE_FOURCC('c','m','v','d')
#define FOURCC_hint	GST_MAKE_FOURCC('h','i','n','t')
#define FOURCC_mp4a	GST_MAKE_FOURCC('m','p','4','a')
#define FOURCC_wave	GST_MAKE_FOURCC('w','a','v','e')
#define FOURCC_esds	GST_MAKE_FOURCC('e','s','d','s')


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
  {FOURCC_udta, "user data", 0,},       /* special container */
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
  {FOURCC_dcom, "compressed data", 0,
      qtdemux_dump_dcom},
  {FOURCC_cmvd, "compressed movie data", 0,
      qtdemux_dump_cmvd},
  {FOURCC_hint, "hint", 0, qtdemux_dump_unknown},
  {FOURCC_mp4a, "mp4a", 0, qtdemux_dump_unknown},
  {FOURCC_wave, "wave", QT_CONTAINER},
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
    g_warning ("inflate() returned %d\n", ret);
  }

  g_free (z);
  return buffer;
}

static void
qtdemux_parse_moov (GstQTDemux * qtdemux, void *buffer, int length)
{
  GNode *cmov;

  qtdemux->moov_node = g_node_new (buffer);

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

  GST_LOG ("qtdemux_parse %p %d", buffer, length);

  node_length = QTDEMUX_GUINT32_GET (buffer);
  fourcc = QTDEMUX_FOURCC_GET (buffer + 4);

  type = qtdemux_type_get (fourcc);

  if (fourcc == 0 || node_length == 8)
    return;

  GST_LOG ("parsing '" GST_FOURCC_FORMAT "', length=%d",
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
        GST_LOG ("bad length");
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

      buf = buffer + 16;
      end = buffer + length;
      while (buf < end) {
        GNode *child;

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
    } else if (fourcc == FOURCC_mp4a) {
      void *buf;
      guint32 len;
      guint32 version;

      version = QTDEMUX_GUINT32_GET (buffer + 16);
      if (version == 0x00010000) {
        buf = buffer + 0x34;
        end = buffer + length;
        while (buf < end) {
          GNode *child;

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

  GST_LOG ("unknown QuickTime node type " GST_FOURCC_FORMAT,
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

  node_length = GUINT32_FROM_BE (*(guint32 *) buffer);
  fourcc = GUINT32_FROM_LE (*(guint32 *) (buffer + 4));

  type = qtdemux_type_get (fourcc);

  depth = (g_node_depth (node) - 1) * 2;
  GST_LOG ("%*s'" GST_FOURCC_FORMAT "', [%d], %s",
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
  GST_LOG ("%*s  time scale:    1/%u sec", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 20));
  GST_LOG ("%*s  duration:      %u", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 24));
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
  GST_LOG ("%*s  type:          " GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + 12)));
  GST_LOG ("%*s  subtype:       " GST_FOURCC_FORMAT, depth, "",
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + 16)));
  GST_LOG ("%*s  manufacturer:  " GST_FOURCC_FORMAT, depth, "",
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
    GST_LOG ("%*s    type:          " GST_FOURCC_FORMAT, depth, "",
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
    GST_LOG ("%*s    type:          " GST_FOURCC_FORMAT, depth, "",
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (buffer + offset + 4)));
    GST_LOG ("%*s    data reference:%d", depth, "",
        QTDEMUX_GUINT16_GET (buffer + offset + 14));

    GST_LOG ("%*s    version/rev.:  %08x", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset + 16));
    GST_LOG ("%*s    vendor:        " GST_FOURCC_FORMAT, depth, "",
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
  int i;
  int n;
  int offset;
  int sample_size;

  GST_LOG ("%*s  version/flags: %08x", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 8));
  GST_LOG ("%*s  sample size:   %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  sample_size = QTDEMUX_GUINT32_GET (buffer + 12);
  if (sample_size == 0) {
    GST_LOG ("%*s  n entries:     %d", depth, "",
        QTDEMUX_GUINT32_GET (buffer + 16));
    n = QTDEMUX_GUINT32_GET (buffer + 16);
    offset = 20;
    for (i = 0; i < n; i++) {
      GST_LOG ("%*s    sample size:   %u", depth, "",
          QTDEMUX_GUINT32_GET (buffer + offset));

      offset += 4;
    }
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
  GST_LOG ("%*s  n entries:     %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  n = QTDEMUX_GUINT32_GET (buffer + 12);
  offset = 16;
  for (i = 0; i < n; i++) {
    GST_LOG ("%*s    chunk offset:  %08x", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));

    offset += 4;
  }
}

static void
qtdemux_dump_co64 (GstQTDemux * qtdemux, void *buffer, int depth)
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
    GST_LOG ("%*s    chunk offset:  %" G_GUINT64_FORMAT, depth, "",
        QTDEMUX_GUINT64_GET (buffer + offset));

    offset += 8;
  }
}

static void
qtdemux_dump_dcom (GstQTDemux * qtdemux, void *buffer, int depth)
{
  GST_LOG ("%*s  compression type: " GST_FOURCC_FORMAT, depth, "",
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
  GST_LOG ("%*s  length: %d", depth, "", QTDEMUX_GUINT32_GET (buffer + 8));

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

    child_fourcc = GUINT32_FROM_LE (*(guint32 *) (buffer + 4));

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

    child_fourcc = GUINT32_FROM_LE (*(guint32 *) (buffer + 4));

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

  mvhd = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_mvhd);
  if (mvhd == NULL) {
    GST_LOG ("No mvhd node found.");
    return;
  }

  qtdemux->timescale = QTDEMUX_GUINT32_GET (mvhd->data + 20);
  qtdemux->duration = QTDEMUX_GUINT32_GET (mvhd->data + 24);

  GST_INFO ("timescale: %d", qtdemux->timescale);
  GST_INFO ("duration: %d", qtdemux->duration);

  trak = qtdemux_tree_get_child_by_type (qtdemux->moov_node, FOURCC_trak);
  qtdemux_parse_trak (qtdemux, trak);

/*  trak = qtdemux_tree_get_sibling_by_type(trak, FOURCC_trak);
  if(trak)qtdemux_parse_trak(qtdemux, trak);*/

  while ((trak = qtdemux_tree_get_sibling_by_type (trak, FOURCC_trak)) != NULL)
    qtdemux_parse_trak (qtdemux, trak);
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
  GNode *stsz;
  GNode *stco;
  GNode *co64;
  GNode *stts;
  GNode *mp4a;
  GNode *wave;
  GNode *esds;
  int n_samples;
  QtDemuxSample *samples;
  int n_samples_per_chunk;
  int index;
  int i, j, k;
  QtDemuxStream *stream;
  int n_sample_times;
  guint64 timestamp;
  int sample_size;
  int sample_index;

  stream = g_new0 (QtDemuxStream, 1);

  tkhd = qtdemux_tree_get_child_by_type (trak, FOURCC_tkhd);
  g_assert (tkhd);

  /* track duration? */

  mdia = qtdemux_tree_get_child_by_type (trak, FOURCC_mdia);
  g_assert (mdia);

  mdhd = qtdemux_tree_get_child_by_type (mdia, FOURCC_mdhd);
  g_assert (mdhd);

  stream->timescale = QTDEMUX_GUINT32_GET (mdhd->data + 20);
  GST_INFO ("track timescale: %d", stream->timescale);

  hdlr = qtdemux_tree_get_child_by_type (mdia, FOURCC_hdlr);
  g_assert (hdlr);

  GST_LOG ("track type: " GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (hdlr->data + 12)));
  GST_LOG ("track subtype: " GST_FOURCC_FORMAT,
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
    GST_LOG ("st type:          " GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + offset + 4)));

    stream->width = QTDEMUX_GUINT16_GET (stsd->data + offset + 32);
    stream->height = QTDEMUX_GUINT16_GET (stsd->data + offset + 34);
    stream->fps = 0.;           /* this is filled in later */

    GST_LOG ("frame count:   %u",
        QTDEMUX_GUINT16_GET (stsd->data + offset + 48));

    fourcc = QTDEMUX_FOURCC_GET (stsd->data + offset + 4);
    stream->caps = qtdemux_video_caps (qtdemux, fourcc, stsd->data);
    GST_INFO ("type " GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + offset + 4)),
        stream->caps);
  } else if (stream->subtype == FOURCC_soun) {
    int version, samplesize;
    guint32 fourcc;
    int len;

    len = QTDEMUX_GUINT32_GET (stsd->data + 16);
    GST_LOG ("st type:          " GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4)));

    fourcc = QTDEMUX_FOURCC_GET (stsd->data + 16 + 4);

    offset = 32;
    GST_LOG ("version/rev:      %08x",
        QTDEMUX_GUINT32_GET (stsd->data + offset));
    version = QTDEMUX_GUINT32_GET (stsd->data + offset);
    GST_LOG ("vendor:           %08x",
        QTDEMUX_GUINT32_GET (stsd->data + offset + 4));
    GST_LOG ("n_channels:       %d",
        QTDEMUX_GUINT16_GET (stsd->data + offset + 8));
    stream->n_channels = QTDEMUX_GUINT16_GET (stsd->data + offset + 8);
    GST_LOG ("sample_size:      %d",
        QTDEMUX_GUINT16_GET (stsd->data + offset + 10));
    samplesize = QTDEMUX_GUINT16_GET (stsd->data + offset + 10);
    GST_LOG ("compression_id:   %d",
        QTDEMUX_GUINT16_GET (stsd->data + offset + 12));
    GST_LOG ("packet size:      %d",
        QTDEMUX_GUINT16_GET (stsd->data + offset + 14));
    GST_LOG ("sample rate:      %g",
        QTDEMUX_FP32_GET (stsd->data + offset + 16));
    stream->rate = QTDEMUX_FP32_GET (stsd->data + offset + 16);

    offset = 52;
    if (version == 0x00010000) {
      GST_LOG ("samples/packet:   %d",
          QTDEMUX_GUINT32_GET (stsd->data + offset));
      stream->samples_per_packet = QTDEMUX_GUINT32_GET (stsd->data + offset);
      GST_LOG ("bytes/packet:     %d",
          QTDEMUX_GUINT32_GET (stsd->data + offset + 4));
      GST_LOG ("bytes/frame:      %d",
          QTDEMUX_GUINT32_GET (stsd->data + offset + 8));
      stream->bytes_per_frame = QTDEMUX_GUINT32_GET (stsd->data + offset + 8);
      GST_LOG ("bytes/sample:     %d",
          QTDEMUX_GUINT32_GET (stsd->data + offset + 12));
      stream->compression = 1;
      offset = 68;
    } else if (version == 0x00000000) {
      stream->bytes_per_frame = stream->n_channels * samplesize / 8;
      stream->samples_per_packet = 1;
      stream->compression = 1;

      /* Yes, these have to be hard-coded */
      if (fourcc == GST_MAKE_FOURCC ('M', 'A', 'C', '6'))
        stream->compression = 6;
      if (fourcc == GST_MAKE_FOURCC ('M', 'A', 'C', '3'))
        stream->compression = 3;
      if (fourcc == GST_MAKE_FOURCC ('i', 'm', 'a', '4'))
        stream->compression = 4;
    } else {
      GST_ERROR ("unknown version %08x", version);
    }

    stream->caps = qtdemux_audio_caps (qtdemux, fourcc, NULL, 0);

    mp4a = qtdemux_tree_get_child_by_type (stsd, FOURCC_mp4a);
    wave = NULL;
    if (mp4a)
      wave = qtdemux_tree_get_child_by_type (mp4a, FOURCC_wave);
    esds = NULL;
    if (wave)
      esds = qtdemux_tree_get_child_by_type (wave, FOURCC_esds);
    if (esds) {
      GstBuffer *buffer;
      int len = QTDEMUX_GUINT32_GET (esds->data);

      buffer = gst_buffer_new_and_alloc (len - 8);
      memcpy (GST_BUFFER_DATA (buffer), esds->data + 8, len - 8);

      gst_caps_set_simple (stream->caps, "esds", GST_TYPE_BUFFER, buffer, NULL);
    }
    GST_INFO ("type " GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4)),
        stream->caps);
  } else {
    GST_INFO ("unknown subtype");
    return;
  }

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

  sample_size = QTDEMUX_GUINT32_GET (stsz->data + 12);
  if (sample_size == 0) {
    n_samples = QTDEMUX_GUINT32_GET (stsz->data + 16);
    stream->n_samples = n_samples;
    samples = g_malloc (sizeof (QtDemuxSample) * n_samples);
    stream->samples = samples;

    for (i = 0; i < n_samples; i++) {
      samples[i].size = QTDEMUX_GUINT32_GET (stsz->data + i * 4 + 20);
    }
    n_samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 12);
    index = 0;
    offset = 16;
    for (i = 0; i < n_samples_per_chunk; i++) {
      int first_chunk, last_chunk;
      int samples_per_chunk;

      first_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 0) - 1;
      if (i == n_samples_per_chunk - 1) {
        last_chunk = INT_MAX;
      } else {
        last_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 12) - 1;
      }
      samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 4);

      for (j = first_chunk; j < last_chunk; j++) {
        int chunk_offset;

        if (stco) {
          chunk_offset = QTDEMUX_GUINT32_GET (stco->data + 16 + j * 4);
        } else {
          chunk_offset = QTDEMUX_GUINT64_GET (co64->data + 16 + j * 8);
        }
        for (k = 0; k < samples_per_chunk; k++) {
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
    index = 0;
    for (i = 0; i < n_sample_times; i++) {
      int n;
      int duration;
      guint64 time;

      n = QTDEMUX_GUINT32_GET (stts->data + 16 + 8 * i);
      duration = QTDEMUX_GUINT32_GET (stts->data + 16 + 8 * i + 4);
      time = (GST_SECOND * duration) / stream->timescale;
      for (j = 0; j < n; j++) {
        //GST_INFO("moo %lld", timestamp);
        samples[index].timestamp = timestamp;
        samples[index].duration = time;
        timestamp += time;
        index++;
      }
    }
  } else {
    int sample_width;
    guint64 timestamp = 0;

    GST_LOG ("treating chunks as samples");

    /* treat chunks as samples */
    if (stco) {
      n_samples = QTDEMUX_GUINT32_GET (stco->data + 12);
    } else {
      n_samples = QTDEMUX_GUINT32_GET (co64->data + 12);
    }
    stream->n_samples = n_samples;
    samples = g_malloc (sizeof (QtDemuxSample) * n_samples);
    stream->samples = samples;

    sample_width = QTDEMUX_GUINT16_GET (stsd->data + offset + 10) / 8;

    n_samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 12);
    offset = 16;
    sample_index = 0;
    for (i = 0; i < n_samples_per_chunk; i++) {
      int first_chunk, last_chunk;
      int samples_per_chunk;

      first_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 0) - 1;
      if (i == n_samples - 1) {
        last_chunk = INT_MAX;
      } else {
        last_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 12) - 1;
      }
      samples_per_chunk = QTDEMUX_GUINT32_GET (stsc->data + 16 + i * 12 + 4);

      for (j = first_chunk; j < last_chunk; j++) {
        int chunk_offset;

        if (j >= n_samples)
          goto done2;
        if (stco) {
          chunk_offset = QTDEMUX_GUINT32_GET (stco->data + 16 + j * 4);
        } else {
          chunk_offset = QTDEMUX_GUINT64_GET (co64->data + 16 + j * 8);
        }
        samples[j].chunk = j;
        samples[j].offset = chunk_offset;
        samples[j].size =
            samples_per_chunk * stream->bytes_per_frame /
            stream->samples_per_packet / stream->compression;
        samples[j].duration =
            samples_per_chunk * GST_SECOND / (stream->rate / 2);
        samples[j].timestamp = timestamp;
        timestamp += (samples_per_chunk * GST_SECOND) / stream->rate;
#if 0
        GST_INFO ("moo samples_per_chunk=%d rate=%d dur=%lld %lld",
            (int) samples_per_chunk,
            (int) stream->rate,
            (long long) ((samples_per_chunk * GST_SECOND) / stream->rate),
            (long long) timestamp);
#endif
        samples[j].sample_index = sample_index;
        sample_index += samples_per_chunk;
      }
    }
/*
done2:
    n_sample_times = QTDEMUX_GUINT32_GET(stts->data + 12);
    GST_LOG("n_sample_times = %d",n_sample_times);
    timestamp = 0;
    index = 0;
    sample_index = 0;
    for(i=0;i<n_sample_times;i++){
      int duration;
      guint64 time;
  
      sample_index += QTDEMUX_GUINT32_GET(stts->data + 16 + 8*i);
      duration = QTDEMUX_GUINT32_GET(stts->data + 16 + 8*i + 4);
      for(;index < n_samples && samples[index].sample_index < sample_index;index++){
	int size;

        samples[index].timestamp = timestamp;
	size = samples[index+1].sample_index - samples[index].sample_index;
	time = GST_SECOND / stream->rate; //(GST_SECOND * duration * samples[index].size)/stream->timescale ;
        timestamp += time;
        samples[index].duration = time;
      }
    }
*/
  }
done2:
#if 0
  for (i = 0; i < n_samples; i++) {
    GST_LOG ("%d: %d %d %d %d %" G_GUINT64_FORMAT, i,
        samples[i].sample_index, samples[i].chunk,
        samples[i].offset, samples[i].size, samples[i].timestamp);
    if (i > 10)
      break;
  }
#endif

  gst_qtdemux_add_stream (qtdemux, stream);
}


static GstCaps *
qtdemux_video_caps (GstQTDemux * qtdemux, guint32 fourcc,
    const guint8 * stsd_data)
{
  switch (fourcc) {
    case GST_MAKE_FOURCC ('j', 'p', 'e', 'g'):
      /* JPEG */
      return gst_caps_from_string ("image/jpeg");
    case GST_MAKE_FOURCC ('m', 'j', 'p', 'a'):
      /* Motion-JPEG (format A) */
      return gst_caps_from_string ("image/jpeg");
    case GST_MAKE_FOURCC ('m', 'j', 'p', 'b'):
      /* Motion-JPEG (format B) */
      return gst_caps_from_string ("image/jpeg");
    case GST_MAKE_FOURCC ('S', 'V', 'Q', '3'):
      if (stsd_data != NULL) {
        gst_getbits_t gb;
        gint halfpel_flag;
        gint thirdpel_flag;
        gint unknown_svq3_flag;
        gint low_delay;
        gint size;

        size = QTDEMUX_GUINT32_GET (stsd_data + 16);

        gst_getbits_init (&gb, NULL, NULL);
        gst_getbits_newbuf (&gb, (unsigned char *) stsd_data + 98 + 16 + 4,
            (size - 102 + 16));

        /* Infos ripped from ffmpeg see libavcodec/svq3.c */

        /* 'frame size code' and optional 'width, height' */
        if (gst_getbitsn (&gb, 3) == 7) {
          gst_getbitsn (&gb, 12);
          gst_getbitsn (&gb, 12);
        }

        halfpel_flag = gst_get1bit (&gb);
        thirdpel_flag = gst_get1bit (&gb);

        /* unknown fields */
        gst_get1bit (&gb);
        gst_get1bit (&gb);
        gst_get1bit (&gb);
        gst_get1bit (&gb);

        low_delay = gst_get1bit (&gb);

        /* unknown field */
        gst_get1bit (&gb);

        while (gst_get1bit (&gb)) {
          gst_getbitsn (&gb, 8);
        }

        unknown_svq3_flag = gst_get1bit (&gb);

        return gst_caps_new_simple ("video/x-svq",
            "svqversion", G_TYPE_INT, 3,
            "halfpel_flag", G_TYPE_INT, halfpel_flag,
            "thirdpel_flag", G_TYPE_INT, thirdpel_flag,
            "low_delay", G_TYPE_INT, low_delay,
            "unknown_svq3_flag", G_TYPE_INT, unknown_svq3_flag, NULL);
      }
      return gst_caps_from_string ("video/x-svq, " "svqversion = (int) 3");
    case GST_MAKE_FOURCC ('s', 'v', 'q', 'i'):
    case GST_MAKE_FOURCC ('S', 'V', 'Q', '1'):
      return gst_caps_from_string ("video/x-svq, " "svqversion = (int) 1");
    case GST_MAKE_FOURCC ('r', 'a', 'w', ' '):
      /* uncompressed RGB */
      return gst_caps_from_string ("video/x-raw-rgb, "
          "endianness = (int) BIG_ENDIAN");
      /*"bpp", GST_PROPS_INT(x),
         "depth", GST_PROPS_INT(x),
         "red_mask", GST_PROPS_INT(x),
         "green_mask", GST_PROPS_INT(x),
         "blue_mask", GST_PROPS_INT(x), FIXME! */
    case GST_MAKE_FOURCC ('Y', 'u', 'v', '2'):
      /* uncompressed YUV2 */
      return gst_caps_from_string ("video/x-raw-yuv, "
          "format = (fourcc) YUY2");
    case GST_MAKE_FOURCC ('m', 'p', 'e', 'g'):
      /* MPEG */
      return gst_caps_from_string ("video/mpeg, "
          "systemstream = (boolean) false, " "mpegversion = (int) 1");
    case GST_MAKE_FOURCC ('g', 'i', 'f', ' '):
      return gst_caps_from_string ("image/gif");
    case GST_MAKE_FOURCC ('h', '2', '6', '3'):
      /* H.263 */
      /* ffmpeg uses the height/width props, don't know why */
      return gst_caps_from_string ("video/x-h263");
    case GST_MAKE_FOURCC ('m', 'p', '4', 'v'):
      /* MPEG-4 */
      return gst_caps_from_string ("video/mpeg, "
          "mpegversion = (int) 4, " "systemstream = (boolean) false");
    case GST_MAKE_FOURCC ('3', 'I', 'V', '1'):
      return gst_caps_from_string ("video/x-3ivx");
    case GST_MAKE_FOURCC ('c', 'v', 'i', 'd'):
      /* Cinepak */
      return gst_caps_from_string ("video/x-cinepak");
    case GST_MAKE_FOURCC ('r', 'p', 'z', 'a'):
      /* Apple Video */
      return gst_caps_from_string ("video/x-apple-video");
    case GST_MAKE_FOURCC ('r', 'l', 'e', ' '):
      /* Run-length encoding */
    case GST_MAKE_FOURCC ('s', 'm', 'c', ' '):
    case GST_MAKE_FOURCC ('k', 'p', 'c', 'd'):
    default:
#if 0
      g_critical ("Don't know how to convert fourcc '" GST_FOURCC_FORMAT
          "' to caps\n", GST_FOURCC_ARGS (fourcc));
      return NULL;
#endif
      {
        char *s;

        s = g_strdup_printf ("video/x-gst-fourcc-" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (fourcc));
        return gst_caps_new_simple (s, NULL);
      }
  }
}

static GstCaps *
qtdemux_audio_caps (GstQTDemux * qtdemux, guint32 fourcc, const guint8 * data,
    int len)
{
  switch (fourcc) {
#if 0
    case GST_MAKE_FOURCC ('N', 'O', 'N', 'E'):
      return NULL;              /*gst_caps_from_string ("audio/raw"); */
#endif
    case GST_MAKE_FOURCC ('r', 'a', 'w', ' '):
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 8, " "depth = (int) 8, " "signed = (boolean) true");
    case GST_MAKE_FOURCC ('t', 'w', 'o', 's'):
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 16, "
          "depth = (int) 16, "
          "endianness = (int) BIG_ENDIAN, " "signed = (boolean) true");
    case GST_MAKE_FOURCC ('s', 'o', 'w', 't'):
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 16, "
          "depth = (int) 16, "
          "endianness = (int) G_LITTLE_ENDIAN, " "signed = (boolean) true");
    case GST_MAKE_FOURCC ('f', 'l', '6', '4'):
      return gst_caps_from_string ("audio/x-raw-float, "
          "width = (int) 64, " "endianness = (int) G_BIG_ENDIAN");
    case GST_MAKE_FOURCC ('f', 'l', '3', '2'):
      return gst_caps_from_string ("audio/x-raw-float, "
          "width = (int) 32, " "endianness = (int) G_BIG_ENDIAN");
    case GST_MAKE_FOURCC ('i', 'n', '2', '4'):
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 24, "
          "depth = (int) 32, "
          "endianness = (int) G_BIG_ENDIAN, " "signed = (boolean) true");
    case GST_MAKE_FOURCC ('i', 'n', '3', '2'):
      /* FIXME */
      return gst_caps_from_string ("audio/x-raw-int, "
          "width = (int) 32, "
          "depth = (int) 32, "
          "endianness = (int) G_BIG_ENDIAN, " "signed = (boolean) true");
    case GST_MAKE_FOURCC ('u', 'l', 'a', 'w'):
      /* FIXME */
      return gst_caps_from_string ("audio/x-mulaw");
    case GST_MAKE_FOURCC ('a', 'l', 'a', 'w'):
      /* FIXME */
      return gst_caps_from_string ("audio/x-alaw");
    case 0x6d730002:
      /* Microsoft ADPCM-ACM code 2 */
      return gst_caps_from_string ("audio/x-adpcm, "
          "layout = (string) microsoft");
    case 0x6d730011:
      /* FIXME DVI/Intel IMA ADPCM/ACM code 17 */
      return gst_caps_from_string ("audio/x-adpcm, "
          "layout = (string) quicktime");
    case 0x6d730055:
      /* MPEG layer 3, CBR only (pre QT4.1) */
    case 0x5500736d:
    case GST_MAKE_FOURCC ('.', 'm', 'p', '3'):
      /* MPEG layer 3, CBR & VBR (QT4.1 and later) */
      return gst_caps_from_string ("audio/mpeg, "
          "layer = (int) 3, " "mpegversion = (int) 1");
    case GST_MAKE_FOURCC ('M', 'A', 'C', '3'):
      /* MACE 3:1 */
      return gst_caps_from_string ("audio/x-mace, " "maceversion = (int) 3");
    case GST_MAKE_FOURCC ('M', 'A', 'C', '6'):
      /* MACE 6:1 */
      return gst_caps_from_string ("audio/x-mace, " "maceversion = (int) 6");
    case GST_MAKE_FOURCC ('O', 'g', 'g', 'V'):
      /* Ogg Vorbis */
      return gst_caps_from_string ("application/ogg");
    case GST_MAKE_FOURCC ('d', 'v', 'c', 'a'):
      /* DV audio */
      return gst_caps_from_string ("audio/x-dv");
    case GST_MAKE_FOURCC ('m', 'p', '4', 'a'):
      /* MPEG-4 AAC */
      return gst_caps_new_simple ("audio/mpeg",
          "mpegversion", G_TYPE_INT, 4, NULL);
    case GST_MAKE_FOURCC ('Q', 'D', 'M', '2'):
      /* FIXME: QDesign music version 2 (no constant) */
      return gst_caps_new_simple ("audio/x-qdm2",
          "framesize", G_TYPE_INT, QTDEMUX_GUINT32_GET (data + 52),
          "bitrate", G_TYPE_INT, QTDEMUX_GUINT32_GET (data + 40),
          "blocksize", G_TYPE_INT, QTDEMUX_GUINT32_GET (data + 44), NULL);
    case GST_MAKE_FOURCC ('a', 'g', 's', 'm'):
      /* GSM */
      return gst_caps_new_simple ("audio/x-gsm", NULL);
    case GST_MAKE_FOURCC ('q', 't', 'v', 'r'):
      /* ? */
    case GST_MAKE_FOURCC ('Q', 'D', 'M', 'C'):
      /* QDesign music */
    case GST_MAKE_FOURCC ('i', 'm', 'a', '4'):
      /* IMA 4:1 */
    case GST_MAKE_FOURCC ('Q', 'c', 'l', 'p'):
      /* QUALCOMM PureVoice */
    default:
#if 0
      g_critical ("Don't know how to convert fourcc '" GST_FOURCC_FORMAT
          "' to caps\n", GST_FOURCC_ARGS (fourcc));
      return NULL;
#endif
      {
        char *s;

        s = g_strdup_printf ("audio/x-gst-fourcc-" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (fourcc));
        return gst_caps_new_simple (s, NULL);
      }
  }
}
