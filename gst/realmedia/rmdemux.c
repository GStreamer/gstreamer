/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David A. Schleef <ds@schleef.org>
 * Copyright (C) <2004> Stephane Loeuillet <gstreamer@leroutier.net>
 * Copyright (C) <2005> Owen Fraser-Green <owen@discobabe.net>
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
  guint32 subformat;
  guint32 format;

  int id;
  GstCaps *caps;
  GstPad *pad;
  int timescale;

  int sample_index;
  GstRMDemuxIndex *index;
  int index_length;
  double frame_rate;

  guint16 width;
  guint16 height;
  guint16 flavor;
  guint16 rate;                 // samplerate
  guint16 n_channels;           // channels
  guint16 sample_width;         // bits_per_sample
  guint16 leaf_size;            // subpacket_size
  guint32 packet_size;          // coded_frame_size
  guint16 version;
  guint32 extra_data_size;      // codec_data_length
  guint8 *extra_data;           // extras
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
  RMDEMUX_STATE_HEADER_UNKNOWN,
  RMDEMUX_STATE_HEADER_RMF,
  RMDEMUX_STATE_HEADER_PROP,
  RMDEMUX_STATE_HEADER_MDPR,
  RMDEMUX_STATE_HEADER_INDX,
  RMDEMUX_STATE_HEADER_DATA,
  RMDEMUX_STATE_HEADER_CONT,
  RMDEMUX_STATE_HEADER_SEEKING,
  RMDEMUX_STATE_SEEKING,
  RMDEMUX_STATE_DATA_PACKET,
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
static void gst_rmdemux_dispose (GObject * object);
static GstElementStateReturn gst_rmdemux_change_state (GstElement * element);
static GstFlowReturn gst_rmdemux_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_rmdemux_sink_event (GstPad * pad, GstEvent * event);

static void gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_prop (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_indx (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_data (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_cont (GstRMDemux * rmdemux, const void *data,
    int length);
static void gst_rmdemux_parse_packet (GstRMDemux * rmdemux, const void *data,
    guint16 version, guint16 length);

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

  gobject_class->dispose = gst_rmdemux_dispose;
}

static void
gst_rmdemux_dispose (GObject * object)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (object);

  if (rmdemux->adapter) {
    g_object_unref (rmdemux->adapter);
    rmdemux->adapter = NULL;
  }

  GST_CALL_PARENT (G_OBJECT_CLASS, dispose, (object));
}

static void
gst_rmdemux_init (GstRMDemux * rmdemux)
{
  rmdemux->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&gst_rmdemux_sink_template), "sink");
  gst_pad_set_event_function (rmdemux->sinkpad, gst_rmdemux_sink_event);
  gst_pad_set_chain_function (rmdemux->sinkpad, gst_rmdemux_chain);
  gst_element_add_pad (GST_ELEMENT (rmdemux), rmdemux->sinkpad);

  rmdemux->adapter = gst_adapter_new ();
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rmdemux",
      GST_RANK_PRIMARY, GST_TYPE_RMDEMUX);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "rmdemux",
    "Realmedia stream demuxer",
    plugin_init, VERSION, "LGPL", GST_PACKAGE, GST_ORIGIN)

     static gboolean gst_rmdemux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret = TRUE;

  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_LOG_OBJECT (rmdemux, "handling event");

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_DISCONTINUOUS:
      GST_DEBUG_OBJECT (rmdemux, "discontinuous event");
      gst_event_unref (event);
      break;
    default:
      ret = gst_pad_event_default (rmdemux->sinkpad, event);
      break;
  }

  return ret;
}

static GstElementStateReturn
gst_rmdemux_change_state (GstElement * element)
{
  GstRMDemux *rmdemux = GST_RMDEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      rmdemux->state = RMDEMUX_STATE_HEADER;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_adapter_clear (rmdemux->adapter);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static GstFlowReturn
gst_rmdemux_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  const guint8 *data;
  guint16 version;

  GstRMDemux *rmdemux = GST_RMDEMUX (GST_PAD_PARENT (pad));

  GST_STREAM_LOCK (pad);

  gst_adapter_push (rmdemux->adapter, buffer);

  while (TRUE) {
    switch (rmdemux->state) {
      case RMDEMUX_STATE_HEADER:
      {
        if (gst_adapter_available (rmdemux->adapter) < 10)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, 10);

        rmdemux->object_id = RMDEMUX_FOURCC_GET (data + 0);
        rmdemux->size = RMDEMUX_GUINT32_GET (data + 4) - 10;
        rmdemux->object_version = RMDEMUX_GUINT16_GET (data + 8);

        GST_LOG_OBJECT (rmdemux, "header found with object_id="
            GST_FOURCC_FORMAT
            " size=%08x object_version=%d",
            GST_FOURCC_ARGS (rmdemux->object_id), rmdemux->size,
            rmdemux->object_version);

        gst_adapter_flush (rmdemux->adapter, 10);

        switch (rmdemux->object_id) {
          case GST_MAKE_FOURCC ('.', 'R', 'M', 'F'):
            rmdemux->state = RMDEMUX_STATE_HEADER_RMF;
            break;
          case GST_MAKE_FOURCC ('P', 'R', 'O', 'P'):
            rmdemux->state = RMDEMUX_STATE_HEADER_PROP;
            break;
          case GST_MAKE_FOURCC ('M', 'D', 'P', 'R'):
            rmdemux->state = RMDEMUX_STATE_HEADER_MDPR;
            break;
          case GST_MAKE_FOURCC ('I', 'N', 'D', 'X'):
            rmdemux->state = RMDEMUX_STATE_HEADER_INDX;
            break;
          case GST_MAKE_FOURCC ('D', 'A', 'T', 'A'):
            rmdemux->state = RMDEMUX_STATE_HEADER_DATA;
            break;
          case GST_MAKE_FOURCC ('C', 'O', 'N', 'T'):
            rmdemux->state = RMDEMUX_STATE_HEADER_CONT;
            break;
          default:
            rmdemux->state = RMDEMUX_STATE_HEADER_UNKNOWN;
            break;
        }
        break;
      }
      case RMDEMUX_STATE_HEADER_UNKNOWN:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        GST_WARNING_OBJECT (rmdemux, "Unknown object_id " GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (rmdemux->object_id));

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_RMF:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        if ((rmdemux->object_version == 0) || (rmdemux->object_version == 1)) {
          data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

          gst_rmdemux_parse__rmf (rmdemux, data, rmdemux->size);
        }

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_PROP:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_prop (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_MDPR:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_mdpr (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_CONT:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_cont (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_HEADER_DATA:
      {
        /* The actual header is only 8 bytes */
        rmdemux->size = 8;
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_data (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_DATA_PACKET;
        break;
      }
      case RMDEMUX_STATE_HEADER_INDX:
      {
        if (gst_adapter_available (rmdemux->adapter) < rmdemux->size)
          goto unlock;
        data = gst_adapter_peek (rmdemux->adapter, rmdemux->size);

        gst_rmdemux_parse_indx (rmdemux, data, rmdemux->size);

        gst_adapter_flush (rmdemux->adapter, rmdemux->size);
        rmdemux->state = RMDEMUX_STATE_HEADER;
        break;
      }
      case RMDEMUX_STATE_DATA_PACKET:
      {
        if (gst_adapter_available (rmdemux->adapter) < 2)
          goto unlock;

        data = gst_adapter_peek (rmdemux->adapter, 2);
        version = RMDEMUX_GUINT16_GET (data);
        GST_DEBUG_OBJECT (rmdemux, "Data packet with version=%d", version);

        if (version == 0 || version == 1) {
          guint16 length;

          if (gst_adapter_available (rmdemux->adapter) < 4)
            goto unlock;
          data = gst_adapter_peek (rmdemux->adapter, 4);

          length = RMDEMUX_GUINT16_GET (data + 2);
          if (length == 0) {
            gst_adapter_flush (rmdemux->adapter, 2);
          } else {
            if (gst_adapter_available (rmdemux->adapter) < length)
              goto unlock;
            data = gst_adapter_peek (rmdemux->adapter, length);

            gst_rmdemux_parse_packet (rmdemux, data + 4, version, length);
            rmdemux->chunk_index++;

            gst_adapter_flush (rmdemux->adapter, length);
          }

          if (rmdemux->chunk_index == rmdemux->n_chunks || length == 0)
            rmdemux->state = RMDEMUX_STATE_HEADER;
        } else {
          /* Stream done */
          gst_adapter_flush (rmdemux->adapter, 2);
          rmdemux->state = RMDEMUX_STATE_HEADER;
        }
      }
    }
  }

unlock:
  GST_STREAM_UNLOCK (pad);
  return ret;
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
        GST_WARNING_OBJECT (rmdemux, "Unknown video FOURCC code");
    }

    if (version) {
      stream->caps =
          gst_caps_new_simple ("video/x-pn-realvideo", "rmversion", G_TYPE_INT,
          (int) version,
          "format", G_TYPE_INT,
          (int) stream->format,
          "subformat", G_TYPE_INT, (int) stream->subformat, NULL);
    }

    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "width", G_TYPE_INT, stream->width,
          "height", G_TYPE_INT, stream->height,
          "framerate", G_TYPE_DOUBLE, stream->frame_rate, NULL);
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

        /* RealAudio 10 (AAC) */
      case GST_RM_AUD_RAAC:
        version = 10;
        break;

        /* MPEG-4 based */
      case GST_RM_AUD_RACP:
        stream->caps =
            gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT,
            (int) 4, NULL);
        break;

        /* Sony ATRAC3 */
      case GST_RM_AUD_ATRC:
        stream->caps = gst_caps_new_simple ("audio/x-vnd.sony.atrac3", NULL);
        break;

        /* RealAudio G2 audio */
      case GST_RM_AUD_COOK:
        version = 8;
        break;

        /* RALF is lossless */
      case GST_RM_AUD_RALF:
        stream->caps = gst_caps_new_simple ("audio/x-ralf-mpeg4-generic", NULL);
        break;

        /* Sipro/ACELP.NET Voice Codec (MIME unknown) */
      case GST_RM_AUD_SIPR:
        stream->caps = gst_caps_new_simple ("audio/x-sipro", NULL);
        break;

      default:
        GST_WARNING_OBJECT (rmdemux,
            "Unknown audio FOURCC code " GST_FOURCC_FORMAT, stream->fourcc);
        break;
    }

    if (version) {
      stream->caps =
          gst_caps_new_simple ("audio/x-pn-realaudio", "raversion", G_TYPE_INT,
          (int) version, NULL);
    }

    if (stream->caps) {
      gst_caps_set_simple (stream->caps,
          "flavor", G_TYPE_INT, (int) stream->flavor,
          "rate", G_TYPE_INT, (int) stream->rate,
          "channels", G_TYPE_INT, (int) stream->n_channels,
          "width", G_TYPE_INT, (int) stream->sample_width,
          "leaf_size", G_TYPE_INT, (int) stream->leaf_size,
          "packet_size", G_TYPE_INT, (int) stream->packet_size,
          "height", G_TYPE_INT, (int) stream->height, NULL);
    }
    rmdemux->n_audio_streams++;


  } else {
    GST_WARNING_OBJECT (rmdemux, "not adding stream of type %d",
        stream->subtype);
    return;
  }

  GST_PAD_ELEMENT_PRIVATE (stream->pad) = stream;
  rmdemux->streams[rmdemux->n_streams] = stream;
  rmdemux->n_streams++;
  GST_LOG_OBJECT (rmdemux, "n_streams is now %d", rmdemux->n_streams);

  if (stream->pad) {
    GST_DEBUG_OBJECT (rmdemux, "setting caps: %p", stream->caps);

    gst_pad_set_caps (stream->pad, stream->caps);
    gst_caps_unref (stream->caps);

    GST_DEBUG_OBJECT (rmdemux, "adding pad %p to rmdemux %p", stream->pad,
        rmdemux);
    gst_element_add_pad (GST_ELEMENT (rmdemux), stream->pad);

    /* If there's some extra data then send it as the first packet */
    if (stream->extra_data_size > 0) {
      GstBuffer *buffer;

      if (gst_pad_alloc_buffer (stream->pad, GST_BUFFER_OFFSET_NONE,
              stream->extra_data_size, stream->caps, &buffer) != GST_FLOW_OK) {
        GST_WARNING_OBJECT (rmdemux, "failed to alloc src buffer for stream %d",
            stream->id);
        return;
      }

      memcpy (GST_BUFFER_DATA (buffer), stream->extra_data,
          stream->extra_data_size);

      if (GST_PAD_IS_USABLE (stream->pad)) {
        GST_DEBUG_OBJECT (rmdemux, "Pushing extra_data of size %d to pad",
            stream->extra_data_size);
        gst_pad_push (stream->pad, buffer);
      }
    }
  }
}

G_GNUC_UNUSED static void
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

static char *
re_get_pascal_string (const guint8 * ptr)
{
  int length;

  length = ptr[0];
  return g_strndup ((char *) ptr + 1, length);
}

static int
re_skip_pascal_string (const guint8 * ptr)
{
  int length;

  length = ptr[0];

  return length + 1;
}

static void
gst_rmdemux_parse__rmf (GstRMDemux * rmdemux, const void *data, int length)
{
  GST_LOG_OBJECT (rmdemux, "file_version: %d", RMDEMUX_GUINT32_GET (data));
  GST_LOG_OBJECT (rmdemux, "num_headers: %d", RMDEMUX_GUINT32_GET (data + 4));
}

static void
gst_rmdemux_parse_prop (GstRMDemux * rmdemux, const void *data, int length)
{
  GST_LOG_OBJECT (rmdemux, "max bitrate: %d", RMDEMUX_GUINT32_GET (data));
  GST_LOG_OBJECT (rmdemux, "avg bitrate: %d", RMDEMUX_GUINT32_GET (data + 4));
  GST_LOG_OBJECT (rmdemux, "max packet size: %d",
      RMDEMUX_GUINT32_GET (data + 8));
  GST_LOG_OBJECT (rmdemux, "avg packet size: %d",
      RMDEMUX_GUINT32_GET (data + 12));
  GST_LOG_OBJECT (rmdemux, "number of packets: %d",
      RMDEMUX_GUINT32_GET (data + 16));

  GST_LOG_OBJECT (rmdemux, "duration: %d", RMDEMUX_GUINT32_GET (data + 20));
  rmdemux->duration = RMDEMUX_GUINT32_GET (data + 20);

  GST_LOG_OBJECT (rmdemux, "preroll: %d", RMDEMUX_GUINT32_GET (data + 24));
  GST_LOG_OBJECT (rmdemux, "offset of INDX section: 0x%08x",
      RMDEMUX_GUINT32_GET (data + 28));
  GST_LOG_OBJECT (rmdemux, "offset of DATA section: 0x%08x",
      RMDEMUX_GUINT32_GET (data + 32));
  GST_LOG_OBJECT (rmdemux, "n streams: %d", RMDEMUX_GUINT16_GET (data + 36));
  GST_LOG_OBJECT (rmdemux, "flags: 0x%04x", RMDEMUX_GUINT16_GET (data + 38));
}

static void
gst_rmdemux_parse_mdpr (GstRMDemux * rmdemux, const void *data, int length)
{
  GstRMDemuxStream *stream;
  char *stream1_type_string;
  char *stream2_type_string;
  int stream_type;
  int offset;

  //re_hexdump_bytes ((guint8 *) data, length, 0);

  stream = g_new0 (GstRMDemuxStream, 1);

  stream->id = RMDEMUX_GUINT16_GET (data);
  GST_LOG_OBJECT (rmdemux, "stream_number=%d", stream->id);

  offset = 30;
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

  GST_LOG_OBJECT (rmdemux, "stream type: %s", stream1_type_string);
  GST_LOG_OBJECT (rmdemux, "MIME type=%s", stream2_type_string);

  if (strcmp (stream2_type_string, "video/x-pn-realvideo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_VIDEO;
  } else if (strcmp (stream2_type_string, "audio/x-pn-realaudio") == 0) {
    stream_type = GST_RMDEMUX_STREAM_AUDIO;
  } else if (strcmp (stream1_type_string, "") == 0 &&
      strcmp (stream2_type_string, "logical-fileinfo") == 0) {
    stream_type = GST_RMDEMUX_STREAM_FILEINFO;
  } else {
    stream_type = GST_RMDEMUX_STREAM_UNKNOWN;
    GST_WARNING_OBJECT (rmdemux, "unknown stream type \"%s\",\"%s\"",
        stream1_type_string, stream2_type_string);
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
      stream->subformat = RMDEMUX_GUINT32_GET (data + offset + 26);
      stream->format = RMDEMUX_GUINT32_GET (data + offset + 30);
      stream->extra_data_size = length - (offset + 34);
      stream->extra_data = (guint8 *) data + offset + 34;
      stream->frame_rate = (double) RMDEMUX_GUINT16_GET (data + offset + 22) +
          ((double) RMDEMUX_GUINT16_GET (data + offset + 24) / 65536.0);

      GST_DEBUG_OBJECT (rmdemux,
          "Video stream with fourcc=" GST_FOURCC_FORMAT
          " width=%d height=%d rate=%d frame_rate=%f subformat=%x format=%x extra_data_size=%d",
          GST_FOURCC_ARGS (stream->fourcc), stream->width, stream->height,
          stream->rate, stream->frame_rate, stream->subformat, stream->format,
          stream->extra_data_size);
      break;
    case GST_RMDEMUX_STREAM_AUDIO:{
      stream->version = RMDEMUX_GUINT16_GET (data + offset + 4);
      stream->flavor = RMDEMUX_GUINT16_GET (data + offset + 22);
      stream->packet_size = RMDEMUX_GUINT32_GET (data + offset + 24);
      stream->leaf_size = RMDEMUX_GUINT16_GET (data + offset + 44);
      stream->height = RMDEMUX_GUINT16_GET (data + offset + 40);

      switch (stream->version) {
        case 4:
          stream->rate = RMDEMUX_GUINT16_GET (data + offset + 48);
          stream->sample_width = RMDEMUX_GUINT16_GET (data + offset + 52);
          stream->n_channels = RMDEMUX_GUINT16_GET (data + offset + 54);
          stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 62);
          stream->extra_data_size = 16;
          stream->extra_data = (guint8 *) data + offset + 71;
          break;
        case 5:
          stream->rate = RMDEMUX_GUINT16_GET (data + offset + 54);
          stream->sample_width = RMDEMUX_GUINT16_GET (data + offset + 58);
          stream->n_channels = RMDEMUX_GUINT16_GET (data + offset + 60);
          stream->fourcc = RMDEMUX_FOURCC_GET (data + offset + 66);
          stream->extra_data_size = RMDEMUX_GUINT32_GET (data + offset + 74);
          stream->extra_data = (guint8 *) data + offset + 78;
          break;
      }

      /*  14_4, 28_8, cook, dnet, sipr, raac, racp, ralf, atrc */
      GST_DEBUG_OBJECT (rmdemux,
          "Audio stream with rate=%d sample_width=%d n_channels=%d",
          stream->rate, stream->sample_width, stream->n_channels);

      break;
    }
    case GST_RMDEMUX_STREAM_FILEINFO:
    {
      int element_nb;

      /* Length of this section */
      GST_DEBUG_OBJECT (rmdemux, "length2: 0x%08x",
          RMDEMUX_GUINT32_GET (data + offset));
      offset += 4;

      /* Unknown : 00 00 00 00 */
      offset += 4;

      /* Number of variables that would follow (loop iterations) */
      element_nb = RMDEMUX_GUINT32_GET (data + offset);
      offset += 4;

      while (element_nb) {
        /* Category Id : 00 00 00 XX 00 00 */
        offset += 6;

        /* Variable Name */
        offset += re_skip_pascal_string (data + offset);

        /* Variable Value Type */
        /*   00 00 00 00 00 => integer/boolean, preceded by length */
        /*   00 00 00 02 00 => pascal string, preceded by length, no trailing \0 */
        offset += 5;

        /* Variable Value */
        offset += re_skip_pascal_string (data + offset);

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
gst_rmdemux_parse_indx (GstRMDemux * rmdemux, const void *data, int length)
{
  int offset;
  int n;
  int i;
  int id;
  GstRMDemuxIndex *index;
  GstRMDemuxStream *stream;

  n = RMDEMUX_GUINT32_GET (data);
  id = RMDEMUX_GUINT16_GET (data + 4);

  stream = gst_rmdemux_get_stream_by_id (rmdemux, id);
  if (stream == NULL)
    return;

  index = g_malloc (sizeof (GstRMDemuxIndex) * n);
  stream->index = index;
  stream->index_length = n;

  offset = 8;
  for (i = 0; i < n; i++) {
    index[i].unknown = RMDEMUX_GUINT16_GET (data + offset + 0);
    index[i].offset = RMDEMUX_GUINT32_GET (data + offset + 2);
    index[i].timestamp = RMDEMUX_GUINT32_GET (data + offset + 6);
    index[i].frame = RMDEMUX_GUINT32_GET (data + offset + 10);

    offset += 14;
  }

}

static void
gst_rmdemux_parse_data (GstRMDemux * rmdemux, const void *data, int length)
{
  rmdemux->n_chunks = RMDEMUX_GUINT32_GET (data);
  rmdemux->chunk_index = 0;
  GST_DEBUG_OBJECT (rmdemux, "Data chunk found with %d packets",
      rmdemux->n_chunks);
}

static void
gst_rmdemux_parse_cont (GstRMDemux * rmdemux, const void *data, int length)
{
  gchar *title = (gchar *) re_get_pascal_string (data);

  GST_DEBUG_OBJECT (rmdemux, "File Content : (CONT) %s", title);
  g_free (title);
}

static void
gst_rmdemux_parse_packet (GstRMDemux * rmdemux, const void *data,
    guint16 version, guint16 length)
{
  guint16 id;
  guint32 timestamp;
  GstRMDemuxStream *stream;
  GstBuffer *buffer;
  guint16 packet_size;

  id = RMDEMUX_GUINT16_GET (data);
  timestamp = RMDEMUX_GUINT32_GET (data + 2);

  GST_DEBUG_OBJECT (rmdemux, "Parsing a packet for stream %d timestamp %d", id,
      timestamp);

  if (version == 0) {
    data += 8;
    packet_size = length - 12;
  } else {
    data += 9;
    packet_size = length - 13;
  }

  stream = gst_rmdemux_get_stream_by_id (rmdemux, id);

  if (gst_pad_alloc_buffer (stream->pad, GST_BUFFER_OFFSET_NONE,
          packet_size, stream->caps, &buffer) != GST_FLOW_OK) {
    GST_WARNING_OBJECT (rmdemux, "failed to alloc src buffer for stream %d",
        id);
    return;
  }

  memcpy (GST_BUFFER_DATA (buffer), (guint8 *) data, packet_size);
  GST_BUFFER_TIMESTAMP (buffer) = GST_SECOND * timestamp / 1000;

  if (stream && stream->pad && GST_PAD_IS_USABLE (stream->pad)) {
    GST_DEBUG_OBJECT (rmdemux, "Pushing buffer of size %d to pad", packet_size);
    gst_pad_push (stream->pad, buffer);
  }
}
