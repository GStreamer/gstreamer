/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2004> Stephane Loeuillet <gstreamer@leroutier.net>
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
#include "rmdemux.h"

#include <string.h>
#include <zlib.h>

#define RMDEMUX_GUINT32_GET(a)	GST_READ_UINT32_BE(a)
#define RMDEMUX_GUINT16_GET(a)	GST_READ_UINT16_BE(a)
#define RMDEMUX_FOURCC_GET(a)	GST_READ_UINT32_LE(a)

typedef struct _GstRMDemuxIndex GstRMDemuxIndex;

struct _GstRMDemuxStream
{
  guint32 subtype;
  guint32 fourcc;
  int id;
  GstCaps *caps;
  GstPad *pad;
  int n_samples;
  int timescale;

  int sample_index;
  GstRMDemuxIndex *index;
  int index_length;

  int width;
  int height;
  double rate;
  int n_channels;
};

struct _GstRMDemuxIndex
{
  int unknown;
  guint32 offset;
  int timestamp;
  int frame;
};

enum GstRMDemuxState
{
  RMDEMUX_STATE_NULL,
  RMDEMUX_STATE_HEADER,
  RMDEMUX_STATE_HEADER_SEEKING,
  RMDEMUX_STATE_SEEKING,
  RMDEMUX_STATE_PLAYING,
  RMDEMUX_STATE_SEEKING_EOS,
  RMDEMUX_STATE_EOS
};

enum GstRMDemuxStreamType
{
  GST_RMDEMUX_STREAM_UNKNOWN,
  GST_RMDEMUX_STREAM_VIDEO,
  GST_RMDEMUX_STREAM_AUDIO,
  GST_RMDEMUX_STREAM_FILEINFO
};

static GstElementDetails gst_rmdemux_details = {
  "RealMedia Demuxer",
  "Codec/Demuxer",
  "Demultiplex a RealMedia file into audio and video streams",
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

static GstStaticPadTemplate gst_rmdemux_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/vnd.rn-realmedia")
    );

static GstStaticPadTemplate gst_rmdemux_videosrc_template =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_rmdemux_audiosrc_template =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS_ANY);

static GstElementClass *parent_class = NULL;

static void gst_rmdemux_class_init (GstRMDemuxClass * klass);
static void gst_rmdemux_base_init (GstRMDemuxClass * klass);
static void gst_rmdemux_init (GstRMDemux * rmdemux);
static GstElementStateReturn gst_rmdemux_change_state (GstElement * element);
static void gst_rmdemux_loop (GstElement * element);
static gboolean gst_rmdemux_handle_sink_event (GstRMDemux * rmdemux);

//static GstCaps *gst_rmdemux_video_caps(GstRMDemux *rmdemux, guint32 fourcc);
//static GstCaps *gst_rmdemux_audio_caps(GstRMDemux *rmdemux, guint32 fourcc);

static void gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_parse_prop (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_parse_indx (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_parse_data (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_parse_cont (GstRMDemux * rmdemux, void *data,
    int length);

static void gst_rmdemux_dump__rmf (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_dump_prop (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_dump_mdpr (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_dump_indx (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_dump_data (GstRMDemux * rmdemux, void *data,
    int length);
static void gst_rmdemux_dump_cont (GstRMDemux * rmdemux, void *data,
    int length);

static GstRMDemuxStream *gst_rmdemux_get_stream_by_id (GstRMDemux * rmdemux,
    int id);

static GType
gst_rmdemux_get_type (void)
{
  static GType rmdemux_type = 0;

  if (!rmdemux_type) {
    static const GTypeInfo rmdemux_info = {
      sizeof (GstRMDemuxClass),
      (GBaseInitFunc) gst_rmdemux_base_init, NULL,
      (GClassInitFunc) gst_rmdemux_class_init,
      NULL, NULL, sizeof (GstRMDemux), 0,
      (GInstanceInitFunc) gst_rmdemux_init,
    };

    rmdemux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRMDemux", &rmdemux_info,
        0);
  }
  return rmdemux_type;
}

static void
gst_rmdemux_base_init (GstRMDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_videosrc_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_rmdemux_audiosrc_template));
  gst_element_class_set_details (element_class, &gst_rmdemux_details);
}

static void
gst_rmdemux_class_init (GstRMDemuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_rmdemux_change_state;
}

static void
gst_rmdemux_init (GstRMDemux * rmdemux)
{
  rmdemux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rmdemux_sink_template), "sink");
  gst_element_set_loop_function (GST_ELEMENT (rmdemux), gst_rmdemux_loop);
  gst_element_add_pad (GST_ELEMENT (rmdemux), rmdemux->sinkpad);
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  return gst_element_register (plugin, "rmdemux",
      GST_RANK_PRIMARY, GST_TYPE_RMDEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rmdemux",
    "Realmedia stream demuxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

     static gboolean gst_rmdemux_handle_sink_event (GstRMDemux * rmdemux)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;

  gst_bytestream_get_status (rmdemux->bs, &remaining, &event);

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;
  GST_DEBUG ("rmdemux: event %p %d", event, type);

  switch (type) {
    case GST_EVENT_EOS:
      gst_bytestream_flush (rmdemux->bs, remaining);
      //gst_pad_event_default(rmdemux->sinkpad, event);
      return FALSE;
    case GST_EVENT_FLUSH:
      g_warning ("flush event");
      break;
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG ("discontinuous event\n");
      //gst_bytestream_flush_fast(rmdemux->bs, remaining);
      break;
    default:
      g_warning ("unhandled event %d", type);
      break;
  }

  gst_event_unref (event);
  return TRUE;
}

static GstElementStateReturn
gst_rmdemux_change_state (GstElement * element)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      rmdemux->bs = gst_bytestream_new (rmdemux->sinkpad);
      rmdemux->state = RMDEMUX_STATE_HEADER;
      /* FIXME */
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (rmdemux->bs);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static void
gst_rmdemux_loop (GstElement * element)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (element);
  guint8 *data;
  guint32 length;
  guint32 fourcc;
  GstBuffer *buf;

  //int offset;
  int cur_offset;

  //int size;
  int ret;
  int rlen;
  int debug = 1;

  /* FIXME _tell gets the offset wrong */
  //cur_offset = gst_bytestream_tell(rmdemux->bs);

  cur_offset = rmdemux->offset;
  GST_DEBUG ("loop at position %d, state %d", cur_offset, rmdemux->state);

  if (rmdemux->length == 0) {
    rmdemux->length = gst_bytestream_length (rmdemux->bs);
  }

  switch (rmdemux->state) {
    case RMDEMUX_STATE_HEADER:
    {
      do {
        ret = gst_bytestream_peek_bytes (rmdemux->bs, &data, 16);
        if (ret < 16) {
          if (!gst_rmdemux_handle_sink_event (rmdemux)) {
            return;
          }
        } else {
          break;
        }
      } while (1);

      fourcc = RMDEMUX_FOURCC_GET (data + 0);
      length = RMDEMUX_GUINT32_GET (data + 4);

      g_print ("fourcc " GST_FOURCC_FORMAT "\n", GST_FOURCC_ARGS (fourcc));
      g_print ("length %08x\n", length);

      rlen = MIN (length, 4096) - 8;

      switch (fourcc) {
        case GST_MAKE_FOURCC ('.', 'R', 'M', 'F'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          if (debug)
            gst_rmdemux_dump__rmf (rmdemux, data + 8, rlen);
          gst_rmdemux_parse__rmf (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('P', 'R', 'O', 'P'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          if (debug)
            gst_rmdemux_dump_prop (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_prop (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('M', 'D', 'P', 'R'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          if (debug)
            gst_rmdemux_dump_mdpr (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_mdpr (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('I', 'N', 'D', 'X'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          if (debug)
            gst_rmdemux_dump_indx (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_indx (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('D', 'A', 'T', 'A'):
          rmdemux->data_offset = rmdemux->offset + 10;
          if (debug)
            gst_rmdemux_dump_data (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_data (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('C', 'O', 'N', 'T'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          if (debug)
            gst_rmdemux_dump_cont (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_cont (rmdemux, data + 8, rlen);
          break;
        default:
          g_print ("unknown fourcc " GST_FOURCC_FORMAT "\n",
              GST_FOURCC_ARGS (fourcc));
          break;
      }

      rmdemux->offset += length;
      if (rmdemux->offset < rmdemux->length) {
        ret = gst_bytestream_seek (rmdemux->bs, rmdemux->offset,
            GST_SEEK_METHOD_SET);
      } else {
        rmdemux->offset = rmdemux->data_offset + 8;
        rmdemux->state = RMDEMUX_STATE_PLAYING;
        ret = gst_bytestream_seek (rmdemux->bs, rmdemux->offset,
            GST_SEEK_METHOD_SET);
      }

      break;
    }
    case RMDEMUX_STATE_SEEKING_EOS:
    {
      guint8 *data;
      int i;

      for (i = 0; i < rmdemux->n_streams; i++) {
        GstPad *pad = rmdemux->streams[i]->pad;

        if (pad) {
          gst_pad_push (pad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
        }
      }

      ret = gst_bytestream_peek_bytes (rmdemux->bs, &data, 1);
      if (ret < 1) {
        gst_rmdemux_handle_sink_event (rmdemux);
      } else {
        /* didn't expect this */
        g_warning ("expected EOS event");
      }
      gst_element_set_eos (element);

      rmdemux->state = RMDEMUX_STATE_EOS;
      return;
    }
    case RMDEMUX_STATE_EOS:
      g_warning ("spinning in EOS\n");
      return;
    case RMDEMUX_STATE_PLAYING:
    {
      int id, timestamp, unknown1;
      GstRMDemuxStream *stream;
      GstBuffer *buffer;

      do {
        ret = gst_bytestream_peek_bytes (rmdemux->bs, &data, 10);
        if (ret < 10) {
          if (!gst_rmdemux_handle_sink_event (rmdemux)) {
            return;
          }
        } else {
          break;
        }
      } while (1);

      length = RMDEMUX_GUINT32_GET (data + 0);
      id = RMDEMUX_GUINT16_GET (data + 4);
      timestamp = RMDEMUX_GUINT32_GET (data + 6);
      unknown1 = RMDEMUX_GUINT16_GET (data + 10);
      g_print ("length %d stream id %d timestamp %d unknown %d\n",
          length, id, timestamp, unknown1);

      gst_bytestream_flush (rmdemux->bs, 12);

      gst_bytestream_read (rmdemux->bs, &buffer, length - 12);
      stream = gst_rmdemux_get_stream_by_id (rmdemux, id);

      if (stream->pad) {
        gst_pad_push (stream->pad, GST_DATA (buffer));
      }

      rmdemux->chunk_index++;
      g_print ("chunk_index %d n_chunks %d\n", rmdemux->chunk_index,
          rmdemux->n_chunks);
      if (rmdemux->chunk_index < rmdemux->n_chunks) {
        rmdemux->offset += length;
        ret = gst_bytestream_seek (rmdemux->bs, rmdemux->offset,
            GST_SEEK_METHOD_SET);
      } else {
        ret = gst_bytestream_seek (rmdemux->bs, 0, GST_SEEK_METHOD_END);
        g_print ("seek to end returned %d\n", ret);
        rmdemux->state = RMDEMUX_STATE_SEEKING_EOS;
      }

      break;
    }
    default:
      /* unreached */
      g_assert (0);
  }

}

static GstRMDemuxStream *
gst_rmdemux_get_stream_by_id (GstRMDemux * rmdemux, int id)
{
  int i;
  GstRMDemuxStream *stream;

  for (i = 0; i < rmdemux->n_streams; i++) {
    stream = rmdemux->streams[i];
    if (stream->id == id) {
      return stream;
    }
  }

  return NULL;
}

void
gst_rmdemux_add_stream (GstRMDemux * rmdemux, GstRMDemuxStream * stream)
{
  if (stream->subtype == GST_RMDEMUX_STREAM_VIDEO) {
    stream->pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_rmdemux_videosrc_template), g_strdup_printf ("video_%02d",
            rmdemux->n_video_streams));
    switch (stream->fourcc) {
      case GST_MAKE_FOURCC ('R', 'V', '1', '0'):
      case GST_MAKE_FOURCC ('R', 'V', '2', '0'):
      case GST_MAKE_FOURCC ('R', 'V', '3', '0'):
      case GST_MAKE_FOURCC ('R', 'V', '4', '0'):
        stream->caps = gst_caps_new_simple ("video/x-pn-realvideo", NULL);
/*           "systemstream", G_TYPE_BOOLEAN, FALSE,
            "rmversion", G_TYPE_INT, version, NULL); */
        break;
      default:
        g_print ("Unknown video FOURCC code\n");
    }
    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "width", G_TYPE_INT, stream->width,
          "height", G_TYPE_INT, stream->height, NULL);
    }
    rmdemux->n_video_streams++;
  } else if (stream->subtype == GST_RMDEMUX_STREAM_AUDIO) {
    stream->pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_rmdemux_audiosrc_template), g_strdup_printf ("audio_%02d",
            rmdemux->n_audio_streams));
    switch (stream->fourcc) {
      case GST_MAKE_FOURCC ('.', 'r', 'a', '4'):
      case GST_MAKE_FOURCC ('.', 'r', 'a', '5'):
        stream->caps = gst_caps_new_simple ("audio/x-pn-realaudio", NULL);
/*             "raversion", G_TYPE_INT, version, NULL); */
        break;
      default:
        g_print ("Unknown audio FOURCC code\n");
    }
    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, stream->n_channels, NULL);
    }
    rmdemux->n_audio_streams++;
  } else {
    g_print ("not adding stream of type %d\n", stream->subtype);
    return;
  }

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  rmdemux->streams[rmdemux->n_streams] = stream;
  rmdemux->n_streams++;
  g_print ("n_streams is now %d\n", rmdemux->n_streams);

  if (stream->pad) {
    gst_pad_use_explicit_caps (stream->pad);

    GST_DEBUG ("setting caps: " GST_PTR_FORMAT, stream->caps);

    gst_pad_set_explicit_caps (stream->pad, stream->caps);

    GST_DEBUG ("adding pad %p to rmdemux %p", stream->pad, rmdemux);
    gst_element_add_pad (GST_ELEMENT (rmdemux), stream->pad);
  }
}


#if 0

static GstCaps *
gst_rmdemux_video_caps (GstRMDemux * rmdemux, guint32 fourcc)
{
  return NULL;
}

static GstCaps *
gst_rmdemux_audio_caps (GstRMDemux * rmdemux, guint32 fourcc)
{
  return NULL;
}

#endif



static void
re_hexdump_bytes (guint8 * ptr, int len, int offset)
{
  guint8 *end = ptr + len;
  int i;

  while (1) {
    if (ptr >= end)
      return;
    g_print ("%08x: ", offset);
    for (i = 0; i < 16; i++) {
      if (ptr + i >= end) {
        g_print ("   ");
      } else {
        g_print ("%02x ", ptr[i]);
      }
    }
    for (i = 0; i < 16; i++) {
      if (ptr + i >= end) {
        g_print (" ");
      } else {
        g_print ("%c", g_ascii_isprint (ptr[i]) ? ptr[i] : '.');
      }
    }
    g_print ("\n");
    ptr += 16;
    offset += 16;
  }

}

static int
re_dump_pascal_string (guint8 * ptr)
{
  int length;

  length = ptr[0];
  g_print ("string: %.*s\n", length, (char *) ptr + 1);

  return length + 1;
}

static char *
re_get_pascal_string (guint8 * ptr)
{
  int length;

  length = ptr[0];
  return g_strndup (ptr + 1, length);
}

static int
re_skip_pascal_string (guint8 * ptr)
{
  int length;

  length = ptr[0];

  return length + 1;
}


static void
gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, void *data, int length)
{

}

static void
gst_rmdemux_dump__rmf (GstRMDemux * rmdemux, void *data, int length)
{
  g_print ("version: %d\n", RMDEMUX_GUINT16_GET (data + 0));
  g_print ("unknown: %d\n", RMDEMUX_GUINT32_GET (data + 2));
  g_print ("unknown: %d\n", RMDEMUX_GUINT32_GET (data + 6));
  g_print ("\n");
}

static void
gst_rmdemux_parse_prop (GstRMDemux * rmdemux, void *data, int length)
{

  rmdemux->duration = RMDEMUX_GUINT32_GET (data + 22);
}

static void
gst_rmdemux_dump_prop (GstRMDemux * rmdemux, void *data, int length)
{
  g_print ("version: %d\n", RMDEMUX_GUINT16_GET (data + 0));
  g_print ("max bitrate: %d\n", RMDEMUX_GUINT32_GET (data + 2));
  g_print ("avg bitrate: %d\n", RMDEMUX_GUINT32_GET (data + 6));
  g_print ("max packet size: %d\n", RMDEMUX_GUINT32_GET (data + 10));
  g_print ("avg packet size: %d\n", RMDEMUX_GUINT32_GET (data + 14));
  g_print ("number of packets: %d\n", RMDEMUX_GUINT32_GET (data + 18));
  g_print ("duration: %d\n", RMDEMUX_GUINT32_GET (data + 22));
  g_print ("preroll: %d\n", RMDEMUX_GUINT32_GET (data + 26));
  g_print ("offset of INDX section: 0x%08x\n", RMDEMUX_GUINT32_GET (data + 30));
  g_print ("offset of DATA section: 0x%08x\n", RMDEMUX_GUINT32_GET (data + 34));
  g_print ("n streams: %d\n", RMDEMUX_GUINT16_GET (data + 38));
  g_print ("flags: 0x%04x\n", RMDEMUX_GUINT16_GET (data + 40));
  g_print ("\n");
}

static void
gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux, void *data, int length)
{
  GstRMDemuxStream *stream;
  char *stream1_type_string;
  char *stream2_type_string;
  int stream_type;
  int offset;

  stream = g_new0 (GstRMDemuxStream, 1);

  stream->id = RMDEMUX_GUINT16_GET (data + 2);

  offset = 32;
  stream_type = GST_RMDEMUX_STREAM_UNKNOWN;
  stream1_type_string = re_get_pascal_string (data + offset);
  offset += re_skip_pascal_string (data + offset);
  stream2_type_string = re_get_pascal_string (data + offset);
  offset += re_skip_pascal_string (data + offset);
  /* It could either be "Video Stream" or "The Video Stream",
     same thing for Audio */
  if (strstr (stream1_type_string, "Video Stream")) {
    stream_type = GST_RMDEMUX_STREAM_VIDEO;
  } else if (strstr (stream1_type_string, "Audio Stream")) {
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream1_type_string, "") == 0 &&
      strcmp (stream2_type_string, "logical-fileinfo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_FILEINFO;
  } else {
    stream_type = GST_RMDEMUX_STREAM_UNKNOWN;
    g_print ("unknown stream type \"%s\",\"%s\"\n", stream1_type_string,
        stream2_type_string);
  }
  g_free (stream1_type_string);
  g_free (stream2_type_string);

  offset += 4;

  stream->subtype = stream_type;
  switch (stream_type) {
    case GST_RMDEMUX_STREAM_VIDEO:
      /* RV10/RV20/RV30/RV40 => video/x-pn-realvideo, version=1,2,3,4 */
      stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);

      stream->width = RMDEMUX_GUINT16_GET (data + offset + 12);
      stream->height = RMDEMUX_GUINT16_GET (data + offset + 14);
      stream->rate = RMDEMUX_GUINT16_GET (data + offset + 16);
      break;
    case GST_RMDEMUX_STREAM_AUDIO:
      /* .ra4/.ra5 => audio/x-pn-realaudio, version=4,5 */
      stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);

      stream->rate = RMDEMUX_GUINT32_GET (data + offset + 48);
      /* cook (cook), sipro (sipr), dnet (dnet) */
      break;
    case GST_RMDEMUX_STREAM_FILEINFO:
    {
      int element_nb;

      /* Length of this section */
      g_print ("length2: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset));
      offset += 4;

      /* Unknown : 00 00 00 00 */
      re_hexdump_bytes (data + offset, 4, offset);
      offset += 4;

      /* Number of variables that would follow (loop iterations) */
      element_nb = RMDEMUX_GUINT32_GET (data + offset);
      offset += 4;

      while (element_nb) {
        printf ("\n");

        /* Category Id : 00 00 00 XX 00 00 */
        re_hexdump_bytes (data + offset, 6, offset);
        offset += 6;

        /* Variable Name */
        offset += re_dump_pascal_string (data + offset);

        /* Variable Value Type */
        /*   00 00 00 00 00 => integer/boolean, preceded by length */
        /*   00 00 00 02 00 => pascal string, preceded by length, no trailing \0 */
        re_hexdump_bytes (data + offset, 5, offset);
        offset += 5;

        /* Variable Value */
        offset += re_dump_pascal_string (data + offset);

        element_nb--;
      }
    }
      break;
    case GST_RMDEMUX_STREAM_UNKNOWN:
    default:
      break;
  }

  gst_rmdemux_add_stream (rmdemux, stream);
}

static void
gst_rmdemux_dump_mdpr (GstRMDemux * rmdemux, void *data, int length)
{
  int offset = 0;
  char *stream_type;
  guint32 fourcc;

  g_print ("version: %d\n", RMDEMUX_GUINT16_GET (data + 0));
  g_print ("stream id: %d\n", RMDEMUX_GUINT16_GET (data + 2));
  g_print ("max bitrate: %d\n", RMDEMUX_GUINT32_GET (data + 4));
  g_print ("avg bitrate: %d\n", RMDEMUX_GUINT32_GET (data + 8));
  g_print ("max packet size: %d\n", RMDEMUX_GUINT32_GET (data + 12));
  g_print ("avg packet size: %d\n", RMDEMUX_GUINT32_GET (data + 16));
  g_print ("start time: %d\n", RMDEMUX_GUINT32_GET (data + 20));
  g_print ("preroll: %d\n", RMDEMUX_GUINT32_GET (data + 24));
  g_print ("duration: %d\n", RMDEMUX_GUINT32_GET (data + 28));

  offset = 32;
  stream_type = re_get_pascal_string (data + offset);
  offset += re_dump_pascal_string (data + offset);
  offset += re_dump_pascal_string (data + offset);
  g_print ("length: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset));
  offset += 4;

  if (strstr (stream_type, "Video Stream")) {
    g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 0));
    g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 4));
    fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);
    g_print ("fourcc: " GST_FOURCC_FORMAT "\n", GST_FOURCC_ARGS (fourcc));
    g_print ("width: %d\n", RMDEMUX_GUINT16_GET (data + offset + 12));
    g_print ("height: %d\n", RMDEMUX_GUINT16_GET (data + offset + 14));
    g_print ("rate: %d\n", RMDEMUX_GUINT16_GET (data + offset + 16));
    offset += 18;
  } else if (strstr (stream_type, "Audio Stream")) {
    g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 0));
    g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 4));
    fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);
    g_print ("fourcc: " GST_FOURCC_FORMAT "\n", GST_FOURCC_ARGS (fourcc));
    re_hexdump_bytes (data + offset + 12, 14, offset + 12);
    g_print ("packet size 1: %d\n", RMDEMUX_GUINT16_GET (data + offset + 26));
    re_hexdump_bytes (data + offset + 28, 14, offset + 28);
    g_print ("packet size 2: %d\n", RMDEMUX_GUINT16_GET (data + offset + 42));
    g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 44));
    g_print ("rate1: %d\n", RMDEMUX_GUINT32_GET (data + offset + 48));
    g_print ("rate2: %d\n", RMDEMUX_GUINT32_GET (data + offset + 52));
    offset += 56;
  } else if (strcmp (stream_type, "") == 0) {

    int element_nb;

    /* Length of this section */
    g_print ("length2: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset));
    offset += 4;

    /* Unknown : 00 00 00 00 */
    re_hexdump_bytes (data + offset, 4, offset);
    offset += 4;

    /* Number of variables that would follow (loop iterations) */
    element_nb = RMDEMUX_GUINT32_GET (data + offset);
    offset += 4;

    while (element_nb) {
      printf ("\n");

      /* Category Id : 00 00 00 XX 00 00 */
      re_hexdump_bytes (data + offset, 6, offset);
      offset += 6;

      /* Variable Name */
      offset += re_dump_pascal_string (data + offset);

      /* Variable Value Type */
      /*   00 00 00 00 00 => integer/boolean, preceded by length */
      /*   00 00 00 02 00 => pascal string, preceded by length, no trailing \0 */
      re_hexdump_bytes (data + offset, 5, offset);
      offset += 5;

      /* Variable Value */
      offset += re_dump_pascal_string (data + offset);

      element_nb--;
    }
  }
  re_hexdump_bytes (data + offset, length - offset, offset);
  g_print ("\n");
}

static void
gst_rmdemux_parse_indx (GstRMDemux * rmdemux, void *data, int length)
{
  int offset;
  int n;
  int i;
  int id;
  GstRMDemuxIndex *index;
  GstRMDemuxStream *stream;

  n = RMDEMUX_GUINT16_GET (data + 4);
  id = RMDEMUX_GUINT16_GET (data + 6);

  stream = gst_rmdemux_get_stream_by_id (rmdemux, id);
  g_return_if_fail (stream != NULL);

  index = g_malloc (sizeof (GstRMDemuxIndex) * n);
  stream->index = index;
  stream->index_length = n;

  offset = 12;
  for (i = 0; i < n; i++) {
    index[i].unknown = RMDEMUX_GUINT16_GET (data + offset + 0);
    index[i].offset = RMDEMUX_GUINT32_GET (data + offset + 2);
    index[i].timestamp = RMDEMUX_GUINT32_GET (data + offset + 6);
    index[i].frame = RMDEMUX_GUINT32_GET (data + offset + 10);

    offset += 14;
  }

}

static void
gst_rmdemux_dump_indx (GstRMDemux * rmdemux, void *data, int length)
{
  int offset = 0;
  int n;
  int i;

  re_hexdump_bytes (data + 0, 4, 0);
  n = RMDEMUX_GUINT16_GET (data + 4);
  g_print ("n_entries: %d\n", n);
  g_print ("stream id: %d\n", RMDEMUX_GUINT16_GET (data + 6));
  g_print ("offset of next INDX: 0x%08x\n", RMDEMUX_GUINT32_GET (data + 8));
  offset = 12;
  for (i = 0; i < n; i++) {
    g_print ("unknown: 0x%04x\n", RMDEMUX_GUINT16_GET (data + offset + 0));
    g_print ("offset: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 2));
    g_print ("timestamp: %d\n", RMDEMUX_GUINT32_GET (data + offset + 6));
    g_print ("frame index: %d\n", RMDEMUX_GUINT32_GET (data + offset + 10));

    offset += 14;
  }
  g_print ("\n");
}

static void
gst_rmdemux_parse_data (GstRMDemux * rmdemux, void *data, int length)
{
  rmdemux->n_chunks = RMDEMUX_GUINT32_GET (data + 2);
}

static void
gst_rmdemux_dump_data (GstRMDemux * rmdemux, void *data, int length)
{
  int offset = 0;
  int n;

  g_print ("version: %d\n", RMDEMUX_GUINT16_GET (data + 0));
  g_print ("n_chunks: %d\n", RMDEMUX_GUINT32_GET (data + offset + 2));
  g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 6));

  re_hexdump_bytes (data + offset, 10, offset);
  offset += 10;
  while (offset < length) {
    n = RMDEMUX_GUINT32_GET (data + offset);
    g_print ("length: %d\n", n);
    g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 4));
    g_print ("unknown: 0x%08x\n", RMDEMUX_GUINT32_GET (data + offset + 8));
    offset += 12;
    re_hexdump_bytes (data + offset, n - 12, offset);
    offset += n - 12;
  }
  g_print ("\n");
}

static void
gst_rmdemux_parse_cont (GstRMDemux * rmdemux, void *data, int length)
{
//  int offset = 0;

//  offset += re_dump_pascal_string ( data + offset + 3 );
}

static void
gst_rmdemux_dump_cont (GstRMDemux * rmdemux, void *data, int length)
{

}
