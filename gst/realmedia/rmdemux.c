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
  guint32 subid;
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

GST_DEBUG_CATEGORY_STATIC (rmdemux_debug);
#define GST_CAT_DEFAULT rmdemux_debug

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

  GST_DEBUG_CATEGORY_INIT (rmdemux_debug, "rmdemux",
      0, "Demuxer for Realmedia streams");
}

static void
gst_rmdemux_init (GstRMDemux * rmdemux)
{
  GST_FLAG_SET (rmdemux, GST_ELEMENT_EVENT_AWARE);

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
      gst_pad_event_default (rmdemux->sinkpad, event);
      return FALSE;
    case GST_EVENT_INTERRUPT:
      gst_event_unref (event);
      return FALSE;
    case GST_EVENT_FLUSH:
      break;
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG ("discontinuous event");
      //gst_bytestream_flush_fast(rmdemux->bs, remaining);
      break;
    default:
      GST_WARNING ("unhandled event %d", type);
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

      GST_LOG ("fourcc " GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));
      GST_LOG ("length %08x", length);

      rlen = MIN (length, 4096) - 8;

      switch (fourcc) {
        case GST_MAKE_FOURCC ('.', 'R', 'M', 'F'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          gst_rmdemux_dump__rmf (rmdemux, data + 8, rlen);
          gst_rmdemux_parse__rmf (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('P', 'R', 'O', 'P'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          gst_rmdemux_dump_prop (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_prop (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('M', 'D', 'P', 'R'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          gst_rmdemux_dump_mdpr (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_mdpr (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('I', 'N', 'D', 'X'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          gst_rmdemux_dump_indx (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_indx (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('D', 'A', 'T', 'A'):
          rmdemux->data_offset = rmdemux->offset + 10;
          gst_rmdemux_dump_data (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_data (rmdemux, data + 8, rlen);
          break;
        case GST_MAKE_FOURCC ('C', 'O', 'N', 'T'):
          gst_bytestream_read (rmdemux->bs, &buf, length);
          data = GST_BUFFER_DATA (buf);
          gst_rmdemux_dump_cont (rmdemux, data + 8, rlen);
          gst_rmdemux_parse_cont (rmdemux, data + 8, rlen);
          break;
        default:
          GST_WARNING ("unknown fourcc " GST_FOURCC_FORMAT,
              GST_FOURCC_ARGS (fourcc));
          break;
      }

      rmdemux->offset += length ? length : 8;
      if (rmdemux->offset < rmdemux->length) {
        ret = gst_bytestream_seek (rmdemux->bs, rmdemux->offset,
            GST_SEEK_METHOD_SET);
      } else {
        rmdemux->offset = rmdemux->data_offset + 8;
        rmdemux->state = RMDEMUX_STATE_PLAYING;
        ret = gst_bytestream_seek (rmdemux->bs, rmdemux->offset,
            GST_SEEK_METHOD_SET);

        GST_DEBUG ("no more pads to come");
        gst_element_no_more_pads (element);
      }
      break;
    }
    case RMDEMUX_STATE_SEEKING_EOS:
    {
      guint8 *data;

      for (;;) {
        ret = gst_bytestream_peek_bytes (rmdemux->bs, &data, 1);
        if (ret < 1) {
          if (!gst_rmdemux_handle_sink_event (rmdemux))
            break;
        } else {
          /* didn't expect this */
          GST_WARNING ("expected EOS event");
          break;
        }
      }

      rmdemux->state = RMDEMUX_STATE_EOS;
      return;
    }
    case RMDEMUX_STATE_EOS:
      g_warning ("spinning in EOS");
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
      GST_DEBUG ("length %d stream id %d timestamp %d unknown %d",
          length, id, timestamp, unknown1);

      gst_bytestream_flush (rmdemux->bs, 12);

      gst_bytestream_read (rmdemux->bs, &buffer, length - 12);
      stream = gst_rmdemux_get_stream_by_id (rmdemux, id);

      if (stream && stream->pad && GST_PAD_IS_USABLE (stream->pad)) {
        gst_pad_push (stream->pad, GST_DATA (buffer));
      } else {
        gst_buffer_unref (buffer);
      }

      rmdemux->chunk_index++;
      GST_DEBUG ("chunk_index %d n_chunks %d", rmdemux->chunk_index,
          rmdemux->n_chunks);
      if (rmdemux->chunk_index < rmdemux->n_chunks) {
        rmdemux->offset += length;
        ret = gst_bytestream_seek (rmdemux->bs, rmdemux->offset,
            GST_SEEK_METHOD_SET);
      } else {
        ret = gst_bytestream_seek (rmdemux->bs, 0, GST_SEEK_METHOD_END);
        GST_DEBUG ("seek to end returned %d", ret);
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
  int version = 0;

  if (stream->subtype == GST_RMDEMUX_STREAM_VIDEO) {
    stream->pad =
        gst_pad_new_from_template (gst_static_pad_template_get
        (&gst_rmdemux_videosrc_template), g_strdup_printf ("video_%02d",
            rmdemux->n_video_streams));
    switch (stream->fourcc) {
      case GST_RM_VDO_RV10:
        version = 1;
        break;
      case GST_RM_VDO_RV20:
        version = 2;
        break;
      case GST_RM_VDO_RV30:
        version = 3;
        break;
      case GST_RM_VDO_RV40:
        version = 4;
        break;
      default:
        GST_WARNING ("Unknown video FOURCC code");
    }
    if (version) {
      stream->caps =
          gst_caps_new_simple ("video/x-pn-realvideo", "rmversion", G_TYPE_INT,
          (int) version, "rmsubid", GST_TYPE_FOURCC, stream->subid, NULL);
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
        /* Older RealAudio Codecs */
      case GST_RM_AUD_14_4:
        version = 1;
        break;

      case GST_RM_AUD_28_8:
        version = 2;
        break;

        /* DolbyNet (Dolby AC3, low bitrate) */
      case GST_RM_AUD_DNET:
        stream->caps =
            gst_caps_new_simple ("audio/x-ac3", "rate", G_TYPE_INT,
            (int) stream->rate, NULL);
        break;

        /* MPEG-4 based */
      case GST_RM_AUD_RAAC:
      case GST_RM_AUD_RACP:
        stream->caps =
            gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT,
            (int) 4, NULL);
        break;

        /* RealAudio audio/RALF is lossless */
      case GST_RM_AUD_COOK:
      case GST_RM_AUD_RALF:

        /* Sipro/ACELP-NET Voice Codec */
      case GST_RM_AUD_SIPR:

        /* Sony ATRAC3 */
      case GST_RM_AUD_ATRC:
        GST_WARNING ("Nothing known to decode this audio FOURCC code");
        break;

      default:
        GST_WARNING ("Unknown audio FOURCC code " GST_FOURCC_FORMAT,
            stream->fourcc);
        break;
    }

    if (version) {
      stream->caps =
          gst_caps_new_simple ("audio/x-pn-realaudio", "raversion", G_TYPE_INT,
          (int) version, NULL);
    }

    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, stream->n_channels, NULL);
    }
    rmdemux->n_audio_streams++;
  } else {
    GST_WARNING ("not adding stream of type %d", stream->subtype);
    return;
  }

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  rmdemux->streams[rmdemux->n_streams] = stream;
  rmdemux->n_streams++;
  GST_LOG ("n_streams is now %d", rmdemux->n_streams);

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
#if 0
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
#endif
}

static int
re_dump_pascal_string (guint8 * ptr)
{
  int length;

  length = ptr[0];
  GST_DEBUG ("string: %.*s", length, (char *) ptr + 1);

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
  GST_LOG ("version: %d", RMDEMUX_GUINT16_GET (data + 0));
  GST_LOG ("unknown: %d", RMDEMUX_GUINT32_GET (data + 2));
  GST_LOG ("unknown: %d", RMDEMUX_GUINT32_GET (data + 6));
}

static void
gst_rmdemux_parse_prop (GstRMDemux * rmdemux, void *data, int length)
{

  rmdemux->duration = RMDEMUX_GUINT32_GET (data + 22);
}

static void
gst_rmdemux_dump_prop (GstRMDemux * rmdemux, void *data, int length)
{
  GST_LOG ("version: %d", RMDEMUX_GUINT16_GET (data + 0));
  GST_LOG ("max bitrate: %d", RMDEMUX_GUINT32_GET (data + 2));
  GST_LOG ("avg bitrate: %d", RMDEMUX_GUINT32_GET (data + 6));
  GST_LOG ("max packet size: %d", RMDEMUX_GUINT32_GET (data + 10));
  GST_LOG ("avg packet size: %d", RMDEMUX_GUINT32_GET (data + 14));
  GST_LOG ("number of packets: %d", RMDEMUX_GUINT32_GET (data + 18));
  GST_LOG ("duration: %d", RMDEMUX_GUINT32_GET (data + 22));
  GST_LOG ("preroll: %d", RMDEMUX_GUINT32_GET (data + 26));
  GST_LOG ("offset of INDX section: 0x%08x", RMDEMUX_GUINT32_GET (data + 30));
  GST_LOG ("offset of DATA section: 0x%08x", RMDEMUX_GUINT32_GET (data + 34));
  GST_LOG ("n streams: %d", RMDEMUX_GUINT16_GET (data + 38));
  GST_LOG ("flags: 0x%04x", RMDEMUX_GUINT16_GET (data + 40));
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

  /* stream1_type_string for audio and video stream is a "put_whatever_you_want" field :
     observed values :
     - "[The ]Video/Audio Stream" (File produced by an official Real encoder)
     - "RealVideoPremierePlugIn-VIDEO/AUDIO" (File produced by Abobe Premiere)

     so, we should not rely on it to know which stream type it is
   */

  if (strcmp (stream2_type_string, "video/x-pn-realvideo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_VIDEO;
  } else if (strcmp (stream2_type_string, "audio/x-pn-realaudio") == 0) {
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream1_type_string, "") == 0 &&
      strcmp (stream2_type_string, "logical-fileinfo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_FILEINFO;
  } else {
    stream_type = GST_RMDEMUX_STREAM_UNKNOWN;
    GST_WARNING ("unknown stream type \"%s\",\"%s\"", stream1_type_string,
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
      stream->subid = RMDEMUX_GUINT32_GET (data + offset + 30);
      break;
    case GST_RMDEMUX_STREAM_AUDIO:{
      int audio_fourcc_offset;

      /* .ra4/.ra5 */
      stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);

      stream->rate = RMDEMUX_GUINT32_GET (data + offset + 48);

      switch (stream->fourcc) {
        case GST_RM_AUD_xRA4:
          audio_fourcc_offset = 62;
          break;
        case GST_RM_AUD_xRA5:
          audio_fourcc_offset = 66;
          break;
        default:
          audio_fourcc_offset = 0;
          GST_WARNING ("Unknown audio stream format");
      }

      /*  14_4, 28_8, cook, dnet, sipr, raac, racp, ralf, atrc */
      stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + audio_fourcc_offset);
      break;
    }
    case GST_RMDEMUX_STREAM_FILEINFO:
    {
      int element_nb;

      /* Length of this section */
      GST_DEBUG ("length2: 0x%08x", RMDEMUX_GUINT32_GET (data + offset));
      offset += 4;

      /* Unknown : 00 00 00 00 */
      re_hexdump_bytes (data + offset, 4, offset);
      offset += 4;

      /* Number of variables that would follow (loop iterations) */
      element_nb = RMDEMUX_GUINT32_GET (data + offset);
      offset += 4;

      while (element_nb) {
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

  GST_LOG ("version: %d", RMDEMUX_GUINT16_GET (data + 0));
  GST_LOG ("stream id: %d", RMDEMUX_GUINT16_GET (data + 2));
  GST_LOG ("max bitrate: %d", RMDEMUX_GUINT32_GET (data + 4));
  GST_LOG ("avg bitrate: %d", RMDEMUX_GUINT32_GET (data + 8));
  GST_LOG ("max packet size: %d", RMDEMUX_GUINT32_GET (data + 12));
  GST_LOG ("avg packet size: %d", RMDEMUX_GUINT32_GET (data + 16));
  GST_LOG ("start time: %d", RMDEMUX_GUINT32_GET (data + 20));
  GST_LOG ("preroll: %d", RMDEMUX_GUINT32_GET (data + 24));
  GST_LOG ("duration: %d", RMDEMUX_GUINT32_GET (data + 28));

  offset = 32;
  stream_type = re_get_pascal_string (data + offset);
  offset += re_dump_pascal_string (data + offset);
  offset += re_dump_pascal_string (data + offset);
  GST_LOG ("length: 0x%08x", RMDEMUX_GUINT32_GET (data + offset));
  offset += 4;

  if (strstr (stream_type, "Video Stream")) {
    GST_LOG ("unknown: 0x%08x", RMDEMUX_GUINT32_GET (data + offset + 0));
    GST_LOG ("unknown: 0x%08x", RMDEMUX_GUINT32_GET (data + offset + 4));
    fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);
    GST_LOG ("fourcc: " GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));
    GST_LOG ("width: %d", RMDEMUX_GUINT16_GET (data + offset + 12));
    GST_LOG ("height: %d", RMDEMUX_GUINT16_GET (data + offset + 14));
    GST_LOG ("rate: %d", RMDEMUX_GUINT16_GET (data + offset + 16));
    GST_LOG ("subid: 0x%08x", RMDEMUX_GUINT32_GET (data + offset + 30));
    offset += 18;
  } else if (strstr (stream_type, "Audio Stream")) {
    GST_LOG ("unknown: 0x%08x", RMDEMUX_GUINT32_GET (data + offset + 0));
    GST_LOG ("unknown: 0x%08x", RMDEMUX_GUINT32_GET (data + offset + 4));
    fourcc = RMDEMUX_FOURCC_GET (data + offset + 8);
    GST_LOG ("fourcc: " GST_FOURCC_FORMAT, GST_FOURCC_ARGS (fourcc));
    re_hexdump_bytes (data + offset + 12, 14, offset + 12);
    GST_LOG ("packet size 1: %d", RMDEMUX_GUINT16_GET (data + offset + 26));
    re_hexdump_bytes (data + offset + 28, 14, offset + 28);
    GST_LOG ("packet size 2: %d", RMDEMUX_GUINT16_GET (data + offset + 42));
    GST_LOG ("unknown: 0x%08x", RMDEMUX_GUINT32_GET (data + offset + 44));
    GST_LOG ("rate1: %d", RMDEMUX_GUINT32_GET (data + offset + 48));
    GST_LOG ("rate2: %d", RMDEMUX_GUINT32_GET (data + offset + 52));
    offset += 56;
  } else if (strcmp (stream_type, "") == 0) {

    int element_nb;

    /* Length of this section */
    GST_LOG ("length2: 0x%08x", RMDEMUX_GUINT32_GET (data + offset));
    offset += 4;

    /* Unknown : 00 00 00 00 */
    re_hexdump_bytes (data + offset, 4, offset);
    offset += 4;

    /* Number of variables that would follow (loop iterations) */
    element_nb = RMDEMUX_GUINT32_GET (data + offset);
    offset += 4;

    while (element_nb) {
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
  GST_LOG ("n_entries: %d, stream_id: %d, offset to next INDX: 0x%08x",
      n, RMDEMUX_GUINT16_GET (data + 6), RMDEMUX_GUINT32_GET (data + 8));
  offset = 12;
  for (i = 0; i < n; i++) {
    GST_DEBUG
        ("unknown: 0x%04x, offset: 0x%08x, timestamp: %d, frame index: %d",
        RMDEMUX_GUINT16_GET (data + offset + 0),
        RMDEMUX_GUINT32_GET (data + offset + 2),
        RMDEMUX_GUINT32_GET (data + offset + 6),
        RMDEMUX_GUINT32_GET (data + offset + 10));
    offset += 14;
  }
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

  GST_LOG ("version: %d, n_chunks: %d, unknown: 0x%08x",
      RMDEMUX_GUINT16_GET (data + 0), RMDEMUX_GUINT32_GET (data + offset + 2),
      RMDEMUX_GUINT32_GET (data + offset + 6));

  re_hexdump_bytes (data + offset, 10, offset);
  offset += 10;
  while (offset < length) {
    n = RMDEMUX_GUINT32_GET (data + offset);
    GST_LOG ("length: %d, unknown: 0x%08x, unknown: %08x",
        n, RMDEMUX_GUINT32_GET (data + offset + 4),
        RMDEMUX_GUINT32_GET (data + offset + 8));
    offset += 12;
    re_hexdump_bytes (data + offset, n - 12, offset);
    offset += n - 12;
  }
  g_print ("\n");
}

static void
gst_rmdemux_parse_cont (GstRMDemux * rmdemux, void *data, int length)
{
  int offset = 0;

  GST_DEBUG ("File Content : (CONT)");
  offset += re_dump_pascal_string (data + offset + 3);
}

static void
gst_rmdemux_dump_cont (GstRMDemux * rmdemux, void *data, int length)
{

}
