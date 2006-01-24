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
  guint64 offset;
  guint64 timestamp;            /* In GstClockTime */
  guint32 duration;             /* in stream->timescale units */
};

struct _QtDemuxStream
{
  guint32 subtype;
  GstCaps *caps;
  guint32 fourcc;
  GstPad *pad;
  int n_samples;
  QtDemuxSample *samples;
  int timescale;

  int sample_index;

  int width;
  int height;
  /* Numerator/denominator framerate */
  gint fps_n;
  gint fps_d;

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
};

static GNode *qtdemux_tree_get_child_by_type (GNode * node, guint32 fourcc);
static GNode *qtdemux_tree_get_sibling_by_type (GNode * node, guint32 fourcc);

static GstElementDetails gst_qtdemux_details = {
  "QuickTime Demuxer",
  "Codec/Demuxer",
  "Demultiplex a QuickTime file into audio and video streams",
  "David Schleef <ds@schleef.org>"
};

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

static void gst_qtdemux_class_init (GstQTDemuxClass * klass);
static void gst_qtdemux_base_init (GstQTDemuxClass * klass);
static void gst_qtdemux_init (GstQTDemux * quicktime_demux);
static GstStateChangeReturn gst_qtdemux_change_state (GstElement * element,
    GstStateChange transition);
static void gst_qtdemux_loop_header (GstPad * pad);
static gboolean qtdemux_sink_activate (GstPad * sinkpad);
static gboolean qtdemux_sink_activate_pull (GstPad * sinkpad, gboolean active);

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

  gstelement_class->change_state = gst_qtdemux_change_state;
}

static void
gst_qtdemux_init (GstQTDemux * qtdemux)
{
  qtdemux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_qtdemux_sink_template), "sink");
  gst_pad_set_activate_function (qtdemux->sinkpad, qtdemux_sink_activate);
  gst_pad_set_activatepull_function (qtdemux->sinkpad,
      qtdemux_sink_activate_pull);
  gst_element_add_pad (GST_ELEMENT (qtdemux), qtdemux->sinkpad);

  qtdemux->state = QTDEMUX_STATE_HEADER;
  qtdemux->last_ts = GST_CLOCK_TIME_NONE;
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
      if (GST_CLOCK_TIME_IS_VALID (qtdemux->last_ts)) {
        gst_query_set_position (query, GST_FORMAT_TIME, qtdemux->last_ts);
        res = TRUE;
      }
      break;
    case GST_QUERY_DURATION:
      if (qtdemux->duration != 0 && qtdemux->timescale != 0) {
        gst_query_set_duration (query, GST_FORMAT_TIME,
            (guint64) qtdemux->duration * GST_SECOND / qtdemux->timescale);
        res = TRUE;
      }
      break;
    default:
      res = FALSE;
      break;
  }

  return res;
}

/* sends event to all source pads; takes ownership of the event */
static void
gst_qtdemux_send_event (GstQTDemux * qtdemux, GstEvent * event)
{
  guint n;

  GST_DEBUG_OBJECT (qtdemux, "pushing %s event on all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (n = 0; n < qtdemux->n_streams; n++) {
    gst_event_ref (event);
    gst_pad_push_event (qtdemux->streams[n]->pad, event);
  }
  gst_event_unref (event);
}

static gboolean
gst_qtdemux_handle_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstQTDemux *qtdemux = GST_QTDEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:{
      GstFormat format;
      GstSeekFlags flags;
      gint64 desired_offset;

      gst_event_parse_seek (event, NULL, &format, &flags, NULL,
          &desired_offset, NULL, NULL);
      GST_DEBUG ("seek format %d", format);

      switch (format) {
        case GST_FORMAT_TIME:{
          gint i = 0, n;
          QtDemuxStream *stream = gst_pad_get_element_private (pad);

          GST_DEBUG ("seeking to %" G_GINT64_FORMAT, desired_offset);

          if (!stream->n_samples) {
            res = FALSE;
            break;
          }

          gst_pad_push_event (qtdemux->sinkpad, gst_event_new_flush_start ());
          GST_PAD_STREAM_LOCK (pad);

          gst_qtdemux_send_event (qtdemux, gst_event_new_flush_start ());

          /* resync to new time */
          for (n = 0; n < qtdemux->n_streams; n++) {
            QtDemuxStream *str = qtdemux->streams[n];

            for (i = 0; i < str->n_samples; i++) {
              /* Seek to the sample just before the desired offset and
               * let downstream throw away bits outside of the segment */
              if (str->samples[i].timestamp > desired_offset) {
                if (i > 0)
                  --i;
                break;
              }
            }
            str->sample_index = i;
          }
          gst_pad_push_event (qtdemux->sinkpad, gst_event_new_flush_stop ());
          gst_qtdemux_send_event (qtdemux, gst_event_new_flush_stop ());

          gst_qtdemux_send_event (qtdemux,
              gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
                  desired_offset, GST_CLOCK_TIME_NONE, desired_offset));

          /* and restart */
          gst_pad_start_task (qtdemux->sinkpad,
              (GstTaskFunction) gst_qtdemux_loop_header, qtdemux->sinkpad);

          GST_PAD_STREAM_UNLOCK (pad);
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

  return gst_element_register (plugin, "qtdemux",
      GST_RANK_PRIMARY, GST_TYPE_QTDEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "qtdemux",
    "Quicktime stream demuxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN);

#if 0
static gboolean
gst_qtdemux_handle_sink_event (GstQTDemux * qtdemux)
{
  gboolean res = TRUE;
  guint32 remaining;
  GstEvent *event;
  GstEventType type;

  gst_bytestream_get_status (qtdemux->bs, &remaining, &event);

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;
  GST_DEBUG ("qtdemux: event %p %d", event, type);

  switch (type) {
    case GST_EVENT_INTERRUPT:
      gst_event_unref (event);
      return FALSE;
    case GST_EVENT_EOS:
      gst_bytestream_flush (qtdemux->bs, remaining);
      gst_pad_event_default (qtdemux->sinkpad, event);
      return FALSE;
    case GST_EVENT_FLUSH:
      break;
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG ("discontinuous event");
      //gst_bytestream_flush_fast(qtdemux->bs, remaining);
      break;
    default:
      gst_pad_event_default (qtdemux->sinkpad, event);
      return TRUE;
  }

  gst_event_unref (event);
  return res;
}
#endif

static GstStateChangeReturn
gst_qtdemux_change_state (GstElement * element, GstStateChange transition)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (element);
  GstStateChangeReturn result = GST_STATE_CHANGE_FAILURE;

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:{
      gint n;

      qtdemux->state = QTDEMUX_STATE_HEADER;
      qtdemux->last_ts = GST_CLOCK_TIME_NONE;
      for (n = 0; n < qtdemux->n_streams; n++) {
        gst_element_remove_pad (element, qtdemux->streams[n]->pad);
        g_free (qtdemux->streams[n]->samples);
        if (qtdemux->streams[n]->caps)
          gst_caps_unref (qtdemux->streams[n]->caps);
        g_free (qtdemux->streams[n]);
      }
      qtdemux->n_streams = 0;
      break;
    }
    default:
      break;
  }

  return result;
}

static void
gst_qtdemux_loop_header (GstPad * pad)
{
  GstQTDemux *qtdemux = GST_QTDEMUX (GST_OBJECT_PARENT (pad));
  guint8 *data;
  guint32 length;
  guint32 fourcc;
  GstBuffer *buf = NULL;
  guint64 offset;
  guint64 cur_offset;
  int size;
  GstFlowReturn ret;

  cur_offset = qtdemux->offset;
  GST_DEBUG_OBJECT (qtdemux, "loop at position %" G_GUINT64_FORMAT ", state %d",
      cur_offset, qtdemux->state);

  switch (qtdemux->state) {
    case QTDEMUX_STATE_HEADER:{
      if (gst_pad_pull_range (qtdemux->sinkpad,
              cur_offset, 16, &buf) != GST_FLOW_OK)
        goto error;
      data = GST_BUFFER_DATA (buf);
      length = GST_READ_UINT32_BE (data);
      GST_DEBUG ("length %08x", length);
      fourcc = GST_READ_UINT32_LE (data + 4);
      GST_DEBUG ("atom type %" GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));

      gst_buffer_unref (buf);

      if (length == 0) {
        length = G_MAXUINT32;   //gst_bytestream_length (qtdemux->bs) - cur_offset;
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

      switch (fourcc) {
        case GST_MAKE_FOURCC ('m', 'd', 'a', 't'):
        case GST_MAKE_FOURCC ('f', 'r', 'e', 'e'):
        case GST_MAKE_FOURCC ('w', 'i', 'd', 'e'):
        case GST_MAKE_FOURCC ('P', 'I', 'C', 'T'):
        case GST_MAKE_FOURCC ('p', 'n', 'o', 't'):
          goto ed_edd_and_eddy;
        case GST_MAKE_FOURCC ('m', 'o', 'o', 'v'):{
          GstBuffer *moov;

          if (gst_pad_pull_range (qtdemux->sinkpad, cur_offset, length,
                  &moov) != GST_FLOW_OK)
            goto error;
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
          break;
        }
        ed_edd_and_eddy:
        default:{
          GST_LOG ("unknown %08x '%" GST_FOURCC_FORMAT "' at %d",
              fourcc, GST_FOURCC_ARGS (fourcc), cur_offset);
          cur_offset += length;
          qtdemux->offset += length;
          break;
        }
      }
#if 0
      ret = gst_bytestream_seek (qtdemux->bs, cur_offset + length,
          GST_SEEK_METHOD_SET);
      GST_DEBUG ("seek returned %d", ret);
      if (ret == FALSE) {
        length = cur_offset + length;
        cur_offset = qtdemux->offset;
        length -= cur_offset;
        if (gst_bytestream_flush (qtdemux->bs, length) == FALSE) {
          if (!gst_qtdemux_handle_sink_event (qtdemux)) {
            return;
          }
        }
      }
#endif
      //qtdemux->offset = cur_offset + length;
      break;
    }
    case QTDEMUX_STATE_MOVIE:{
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
        GST_DEBUG_OBJECT (qtdemux, "No samples left for any streams - EOS");
        gst_pad_event_default (qtdemux->sinkpad, gst_event_new_eos ());
        break;
      }

      stream = qtdemux->streams[index];

      offset = stream->samples[stream->sample_index].offset;
      size = stream->samples[stream->sample_index].size;

      GST_INFO
          ("pushing from stream %d, sample_index=%d offset=%d size=%d timestamp=%lld",
          index, stream->sample_index, offset, size,
          stream->samples[stream->sample_index].timestamp);

      buf = NULL;
      if (size > 0) {
        GST_LOG_OBJECT (qtdemux, "reading %d bytes @ %lld", size, offset);

        if (gst_pad_pull_range (qtdemux->sinkpad, offset,
                size, &buf) != GST_FLOW_OK)
          goto error;
      }

      if (buf) {
        /* hum... FIXME changing framerate breaks horribly, better set
         * an average framerate, or get rid of the framerate property. */
        if (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e')) {
          //float fps =
          //   1. * GST_SECOND / stream->samples[stream->sample_index].duration;
          /*
             if (fps != stream->fps) {
             gst_caps_set_simple (stream->caps, "framerate", G_TYPE_DOUBLE, fps,
             NULL);
             stream->fps = fps;
             gst_pad_set_explicit_caps (stream->pad, stream->caps);
             }
           */
        }

        /* first buffer? */
        if (qtdemux->last_ts == GST_CLOCK_TIME_NONE) {
          gst_qtdemux_send_event (qtdemux,
              gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
                  0, GST_CLOCK_TIME_NONE, 0));
        }

        /* timestamps of AMR aren't known... */
        if (stream->fourcc != GST_MAKE_FOURCC ('s', 'a', 'm', 'r')) {
          GST_BUFFER_TIMESTAMP (buf) =
              stream->samples[stream->sample_index].timestamp;
          qtdemux->last_ts = GST_BUFFER_TIMESTAMP (buf);
          GST_BUFFER_DURATION (buf) =
              GST_SECOND * stream->samples[stream->sample_index].duration
              / stream->timescale;
        }

        GST_DEBUG ("Pushing buffer with time %" GST_TIME_FORMAT " on pad %p",
            GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), stream->pad);
        gst_buffer_set_caps (buf, stream->caps);
        ret = gst_pad_push (stream->pad, buf);
        if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)
          goto error;
      }
      stream->sample_index++;
      break;
    }
    default:
      /* oh crap */
      g_error ("State=%d", qtdemux->state);
  }

  return;

error:
  GST_LOG_OBJECT (qtdemux, "pausing task and sending EOS");
  gst_pad_pause_task (qtdemux->sinkpad);
  gst_pad_event_default (qtdemux->sinkpad, gst_event_new_eos ());
  return;
}

static gboolean
qtdemux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
}

static gboolean
qtdemux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    gst_pad_start_task (sinkpad,
        (GstTaskFunction) gst_qtdemux_loop_header, sinkpad);
  } else {
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

void
gst_qtdemux_add_stream (GstQTDemux * qtdemux,
    QtDemuxStream * stream, GstTagList * list)
{
  if (stream->subtype == GST_MAKE_FOURCC ('v', 'i', 'd', 'e')) {
    GstPadTemplate *templ;
    gchar *name = g_strdup_printf ("video_%02d", qtdemux->n_video_streams);

    templ = gst_static_pad_template_get (&gst_qtdemux_videosrc_template);
    stream->pad = gst_pad_new_from_template (templ, name);
    gst_object_unref (templ);
    g_free (name);
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

  gst_pad_use_fixed_caps (stream->pad);

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  qtdemux->streams[qtdemux->n_streams] = stream;
  qtdemux->n_streams++;
  GST_DEBUG ("n_streams is now %d", qtdemux->n_streams);

  gst_pad_set_event_function (stream->pad, gst_qtdemux_handle_src_event);
  gst_pad_set_query_type_function (stream->pad,
      gst_qtdemux_get_src_query_types);
  gst_pad_set_query_function (stream->pad, gst_qtdemux_handle_src_query);

  GST_DEBUG ("setting caps %" GST_PTR_FORMAT, stream->caps);
  gst_pad_set_caps (stream->pad, stream->caps);

  GST_DEBUG ("adding pad %s %p to qtdemux %p",
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
        GST_ERROR ("atom length too short (%d < 8)", len);
        break;
      }
      if (len > (end - buf)) {
        GST_ERROR ("atom length too long (%d > %d)", len, end - buf);
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
          GST_ERROR ("length too short (%d < 8)");
          break;
        }
        if (len > (end - buf)) {
          GST_ERROR ("length too long (%d > %d)", len, end - buf);
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
            GST_ERROR ("length too short (%d < 8)");
            break;
          }
          if (len > (end - buf)) {
            GST_ERROR ("length too long (%d > %d)", len, end - buf);
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

      GST_DEBUG ("parsing in mp4v\n");
      version = QTDEMUX_GUINT32_GET (buffer + 16);
      GST_DEBUG ("version %08x\n", version);
      if (1 || version == 0x00000000) {

        buf = buffer + 0x32;
        end = buffer + length;

        /* FIXME Quicktime uses PASCAL string while
         * the iso format uses C strings. Check the file
         * type before attempting to parse the string here. */
        tlen = QTDEMUX_GUINT8_GET (buf);
        GST_DEBUG ("tlen = %d\n", tlen);
        buf++;
        GST_DEBUG ("string = %.*s\n", tlen, (char *) buf);
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
            GST_ERROR ("length too short (%d < 8)");
            break;
          }
          if (len > (end - buf)) {
            GST_ERROR ("length too long (%d > %d)", len, end - buf);
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
          GST_ERROR ("length too short (%d < 8)");
          break;
        }
        if (len > (end - buf)) {
          GST_ERROR ("length too long (%d > %d)", len, end - buf);
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

      GST_LOG ("parsing in SVQ3\n");
      buf = buffer + 12;
      end = buffer + length;
      version = QTDEMUX_GUINT32_GET (buffer + 16);
      GST_DEBUG ("version %08x\n", version);
      if (1 || version == 0x00000000) {

        buf = buffer + 0x32;
        end = buffer + length;

        tlen = QTDEMUX_GUINT8_GET (buf);
        GST_DEBUG ("tlen = %d\n", tlen);
        buf++;
        GST_DEBUG ("string = %.*s\n", tlen, (char *) buf);
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
            GST_ERROR ("length too short (%d < 8)");
            break;
          }
          if (len > (end - buf)) {
            GST_ERROR ("length too long (%d > %d)", len, end - buf);
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
  GST_LOG ("%*s  sample size:   %d", depth, "",
      QTDEMUX_GUINT32_GET (buffer + 12));
  sample_size = QTDEMUX_GUINT32_GET (buffer + 12);
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
    GST_LOG ("%*s    chunk offset:  %08x", depth, "",
        QTDEMUX_GUINT32_GET (buffer + offset));

    offset += 4;
  }
#endif
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

  GST_INFO ("timescale: %d", qtdemux->timescale);
  GST_INFO ("duration: %d", qtdemux->duration);

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
      GST_DEBUG ("calling gst_element_found_tags with %" GST_PTR_FORMAT,
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
  GNode *stsz;
  GNode *stco;
  GNode *co64;
  GNode *stts;
  GNode *mp4a;
  GNode *mp4v;
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
  GstTagList *list = NULL;
  const gchar *codec = NULL;

  stream = g_new0 (QtDemuxStream, 1);

  tkhd = qtdemux_tree_get_child_by_type (trak, FOURCC_tkhd);
  g_return_if_fail (tkhd);

  GST_LOG ("track[tkhd] version/flags: 0x%08x",
      QTDEMUX_GUINT32_GET (tkhd->data + 8));

  /* track duration? */

  mdia = qtdemux_tree_get_child_by_type (trak, FOURCC_mdia);
  g_assert (mdia);

  mdhd = qtdemux_tree_get_child_by_type (mdia, FOURCC_mdhd);
  g_assert (mdhd);

  stream->timescale = QTDEMUX_GUINT32_GET (mdhd->data + 20);
  GST_LOG ("track timescale: %d", stream->timescale);
  GST_LOG ("track duration: %d", QTDEMUX_GUINT32_GET (mdhd->data + 24));

  /* HACK:
   * some of those trailers, nowadays, have prologue images that are
   * themselves vide tracks as well. I haven't really found a way to
   * identify those yet, except for just looking at their duration. */
  if (stream->timescale * qtdemux->duration != 0 &&
      (guint64) QTDEMUX_GUINT32_GET (mdhd->data + 24) *
      qtdemux->timescale * 10 / (stream->timescale * qtdemux->duration) < 2) {
    GST_WARNING ("Track shorter than 20%% (%d/%d vs. %d/%d) of the stream "
        "found, assuming preview image or something; skipping track",
        QTDEMUX_GUINT32_GET (mdhd->data + 24), stream->timescale,
        qtdemux->duration, qtdemux->timescale);
    return;
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
      if (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4) ==
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
          memcpy (GST_BUFFER_DATA (buf), (guint8 *) stsd->data + 0x8, size);
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

    GST_INFO ("type %" GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + offset + 4)),
        stream->caps);
  } else if (stream->subtype == FOURCC_soun) {
    int version, samplesize;
    guint32 fourcc;
    int len;

    len = QTDEMUX_GUINT32_GET (stsd->data + 16);
    GST_LOG ("st type:          %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4)));

    stream->fourcc = fourcc = QTDEMUX_FOURCC_GET (stsd->data + 16 + 4);
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
      if (fourcc == GST_MAKE_FOURCC ('s', 'a', 'm', 'r')) {
        stream->n_channels = 1;
        stream->rate = 8000;
        stream->bytes_per_frame <<= 3;
      }
      if (fourcc == GST_MAKE_FOURCC ('u', 'l', 'a', 'w'))
        stream->compression = 2;
      if (fourcc == GST_MAKE_FOURCC ('a', 'g', 's', 'm')) {
        stream->bytes_per_frame *= 33;
        stream->compression = 320;
      }
    } else {
      GST_ERROR ("unknown version %08x", version);
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
    GST_INFO ("type %" GST_FOURCC_FORMAT " caps %" GST_PTR_FORMAT,
        GST_FOURCC_ARGS (QTDEMUX_FOURCC_GET (stsd->data + 16 + 4)),
        stream->caps);
  } else {
    GST_INFO ("unknown subtype %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (stream->subtype));
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
        guint64 chunk_offset;

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
        samples[index].duration = duration;
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
        guint64 chunk_offset;

        if (j >= n_samples)
          goto done2;
        if (stco) {
          chunk_offset = QTDEMUX_GUINT32_GET (stco->data + 16 + j * 4);
        } else {
          chunk_offset = QTDEMUX_GUINT64_GET (co64->data + 16 + j * 8);
        }
        samples[j].chunk = j;
        samples[j].offset = chunk_offset;
        if (stream->samples_per_packet * stream->compression != 0)
          samples[j].size =
              samples_per_chunk * stream->bytes_per_frame /
              stream->samples_per_packet / stream->compression;
        else if (stream->bytes_per_frame)
          samples[j].size = stream->bytes_per_frame;
        else
          samples[j].size = sample_size;
        samples[j].duration =
            samples_per_chunk * stream->timescale / (stream->rate / 2);
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
#if 0
  done2:
    n_sample_times = QTDEMUX_GUINT32_GET (stts->data + 12);
    GST_LOG ("n_sample_times = %d", n_sample_times);
    timestamp = 0;
    index = 0;
    sample_index = 0;
    for (i = 0; i < n_sample_times; i++) {
      int duration;
      guint64 time;

      sample_index += QTDEMUX_GUINT32_GET (stts->data + 16 + 8 * i);
      duration = QTDEMUX_GUINT32_GET (stts->data + 16 + 8 * i + 4);
      for (; index < n_samples && samples[index].sample_index < sample_index;
          index++) {
        int size;

        samples[index].timestamp = timestamp;
        size = samples[index + 1].sample_index - samples[index].sample_index;
        time = GST_SECOND / stream->rate;       //(GST_SECOND * duration * samples[index].size)/stream->timescale ;
        timestamp += time;
        samples[index].duration = time;
      }
    }
#endif
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

  GST_DEBUG ("new tag list\n");
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
      GST_DEBUG ("adding tag %s\n", s);
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
      GST_DEBUG ("adding tag %d/%d\n", n1, n2);
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
        GST_DEBUG ("adding %d [%s]\n", n, genres[n]);
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
  GST_DEBUG ("version/flags = %08x\n", QTDEMUX_GUINT32_GET (ptr));
  ptr += 4;
  while (ptr < end) {
    tag = QTDEMUX_GUINT8_GET (ptr);
    GST_DEBUG ("tag = %02x\n", tag);
    ptr++;
    len = get_size (ptr, &ptr);
    GST_DEBUG ("len = %d\n", len);

    switch (tag) {
      case 0x03:
        GST_DEBUG ("ID %04x\n", QTDEMUX_GUINT16_GET (ptr));
        GST_DEBUG ("priority %04x\n", QTDEMUX_GUINT8_GET (ptr + 2));
        ptr += 3;
        break;
      case 0x04:
        GST_DEBUG ("object_type_id %02x\n", QTDEMUX_GUINT8_GET (ptr));
        GST_DEBUG ("stream_type %02x\n", QTDEMUX_GUINT8_GET (ptr + 1));
        GST_DEBUG ("buffer_size_db %02x\n", QTDEMUX_GUINT24_GET (ptr + 2));
        GST_DEBUG ("max bitrate %d\n", QTDEMUX_GUINT32_GET (ptr + 5));
        GST_DEBUG ("avg bitrate %d\n", QTDEMUX_GUINT32_GET (ptr + 9));
        ptr += 13;
        break;
      case 0x05:
        GST_DEBUG ("data:\n");
        gst_util_dump_mem (ptr, len);
        data_ptr = ptr;
        data_len = len;
        ptr += len;
        break;
      case 0x06:
        GST_DEBUG ("data %02x\n", QTDEMUX_GUINT8_GET (ptr));
        ptr += 1;
        break;
      default:
        GST_ERROR ("parse error\n");
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
          "' to caps\n", GST_FOURCC_ARGS (fourcc));
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
      return gst_caps_new_simple ("audio/x-amr-nb", NULL);
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
          "' to caps\n", GST_FOURCC_ARGS (fourcc));
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
