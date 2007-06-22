/* ASF muxer plugin for GStreamer
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

/* based on:
 * - ffmpeg ASF muxer
 * - Owen's GStreamer ASF demuxer ("just reverse it"(tm)?)
 * - Some own random bits and bytes and stuffies and more
 * - Grolsch (oh, and Heineken) beer
 * - Borrelnootjes (and chips, and stuff)
 * - Why are you reading this?
 *
 * "The best code is written when you're drunk.
 *  You'll just never understand it, too."
 *   -- truth hurts.
 */

/* stream does NOT work on Windows Media Player (does work on
 * other (Linux-based) players, because we do not specify bitrate
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <string.h>

#include <gst/video/video.h>

/* for audio codec IDs */
#include <gst/riff/riff-ids.h>

#include "asfheaders.h"
#include "gstasfmux.h"

/* elementfactory information */
static GstElementDetails gst_asfmux_details = {
  "Asf multiplexer",
  "Codec/Muxer",
  "Muxes audio and video streams into an asf stream",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
};

/* AsfMux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate gst_asfmux_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-ms-asf")
    );

static GstStaticPadTemplate gst_asfmux_videosink_template =
    GST_STATIC_PAD_TEMPLATE ("video_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) { YUY2, I420 }, "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "image/jpeg, "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "video/x-divx, "
        "divxversion = (int) [ 3, 5 ], "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "video/x-xvid, "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "video/x-3ivx, "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "video/x-msmpeg, "
        "msmpegversion = (int) [ 41, 43 ], "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "video/mpeg, "
        "mpegversion = (int) 1,"
        "systemstream = (boolean) false,"
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "video/x-h263, "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], "
        "framerate = (double) [ 1, MAX ]; "
        "video/x-dv, "
        "systemstream = (boolean) false,"
        "width = (int) 720,"
        "height = (int) { 576, 480 };"
        "video/x-huffyuv, "
        "width = (int) [ 1, MAX], "
        "height = (int) [ 1, MAX], " "framerate = (double) [ 1, MAX ]")
    );

static GstStaticPadTemplate gst_asfmux_audiosink_template =
    GST_STATIC_PAD_TEMPLATE ("audio_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) { true, false }, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "rate = (int) [ 1000, 96000 ], "
        "channels = (int) [ 1, 2]; "
        "audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) { 1, 3 }, "
        "rate = (int) [ 1000, 96000 ], "
        "channels = (int) [ 1, 2]; "
        "audio/x-vorbis, "
        "rate = (int) [ 1000, 96000 ], "
        "channels = (int) [ 1, 2]; "
        "audio/x-ac3, "
        "rate = (int) [ 1000, 96000 ], " "channels = (int) [ 1, 2]")
    );

#define GST_ASF_PACKET_SIZE 3200
#define GST_ASF_PACKET_HEADER_SIZE 12
#define GST_ASF_FRAME_HEADER_SIZE 17

static void gst_asfmux_base_init (gpointer g_class);
static void gst_asfmux_class_init (GstAsfMuxClass * klass);
static void gst_asfmux_init (GstAsfMux * asfmux);

static void gst_asfmux_loop (GstElement * element);
static gboolean gst_asfmux_handle_event (GstPad * pad, GstEvent * event);
static GstPad *gst_asfmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static GstStateChangeReturn gst_asfmux_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

/*static guint gst_asfmux_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_asfmux_get_type (void)
{
  static GType asfmux_type = 0;

  if (!asfmux_type) {
    static const GTypeInfo asfmux_info = {
      sizeof (GstAsfMuxClass),
      gst_asfmux_base_init,
      NULL,
      (GClassInitFunc) gst_asfmux_class_init,
      NULL,
      NULL,
      sizeof (GstAsfMux),
      0,
      (GInstanceInitFunc) gst_asfmux_init,
    };

    asfmux_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstAsfMux", &asfmux_info, 0);
  }
  return asfmux_type;
}

static void
gst_asfmux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asfmux_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asfmux_videosink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_asfmux_audiosink_template));
  gst_element_class_set_details (element_class, &gst_asfmux_details);
}

static void
gst_asfmux_class_init (GstAsfMuxClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->request_new_pad = gst_asfmux_request_new_pad;
  gstelement_class->change_state = gst_asfmux_change_state;
}

static const GstEventMask *
gst_asfmux_get_event_masks (GstPad * pad)
{
  static const GstEventMask gst_asfmux_sink_event_masks[] = {
    {GST_EVENT_EOS, 0},
    {0,}
  };

  return gst_asfmux_sink_event_masks;
}

static void
gst_asfmux_init (GstAsfMux * asfmux)
{
  gint n;

  asfmux->srcpad =
      gst_pad_new_from_static_template (&gst_asfmux_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (asfmux), asfmux->srcpad);

  GST_OBJECT_FLAG_SET (GST_ELEMENT (asfmux), GST_ELEMENT_EVENT_AWARE);

  asfmux->num_outputs = asfmux->num_video = asfmux->num_audio = 0;
  memset (&asfmux->output, 0, sizeof (asfmux->output));
  for (n = 0; n < MAX_ASF_OUTPUTS; n++) {
    asfmux->output[n].index = n;
    asfmux->output[n].connected = FALSE;
  }
  asfmux->write_header = TRUE;
  asfmux->packet = NULL;
  asfmux->num_packets = 0;
  asfmux->sequence = 0;

  gst_element_set_loop_function (GST_ELEMENT (asfmux), gst_asfmux_loop);
}

static GstPadLinkReturn
gst_asfmux_vidsink_link (GstPad * pad, const GstCaps * caps)
{
  GstAsfMux *asfmux;
  GstAsfMuxStream *stream = NULL;
  GstStructure *structure;
  gint n;
  const gchar *mimetype;
  gint w, h;
  gboolean ret;

  asfmux = GST_ASFMUX (gst_pad_get_parent (pad));

  for (n = 0; n < asfmux->num_outputs; n++) {
    if (asfmux->output[n].pad == pad) {
      stream = &asfmux->output[n];
      break;
    }
  }
  g_assert (n < asfmux->num_outputs);
  g_assert (stream != NULL);
  g_assert (stream->type == ASF_STREAM_VIDEO);

  GST_DEBUG ("asfmux: video sinkconnect triggered on %s",
      gst_pad_get_name (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* global */
  ret = gst_structure_get_int (structure, "width", &w);
  ret &= gst_structure_get_int (structure, "height", &h);

  if (!ret)
    return GST_PAD_LINK_REFUSED;

  stream->header.video.stream.width = w;
  stream->header.video.stream.height = h;
  stream->header.video.stream.unknown = 2;
  stream->header.video.stream.size = 40;
  stream->bitrate = 0;          /* TODO */

  mimetype = gst_structure_get_name (structure);
  if (!strcmp (mimetype, "video/x-raw-yuv")) {
    guint32 format;

    ret = gst_structure_get_fourcc (structure, "format", &format);
    if (!ret)
      return GST_PAD_LINK_REFUSED;

    stream->header.video.format.tag = format;
    switch (format) {
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        stream->header.video.format.depth = 16;
        stream->header.video.format.planes = 1;
        break;
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        stream->header.video.format.depth = 12;
        stream->header.video.format.planes = 3;
        break;
    }

    goto done;
  } else {
    stream->header.video.format.depth = 24;
    stream->header.video.format.planes = 1;
    stream->header.video.format.tag = 0;

    /* find format */
    if (!strcmp (mimetype, "video/x-huffyuv")) {
      stream->header.video.format.tag = GST_MAKE_FOURCC ('H', 'F', 'Y', 'U');
    } else if (!strcmp (mimetype, "image/jpeg")) {
      stream->header.video.format.tag = GST_MAKE_FOURCC ('M', 'J', 'P', 'G');
    } else if (!strcmp (mimetype, "video/x-divx")) {
      gint divxversion;

      gst_structure_get_int (structure, "divxversion", &divxversion);
      switch (divxversion) {
        case 3:
          stream->header.video.format.tag =
              GST_MAKE_FOURCC ('D', 'I', 'V', '3');
          break;
        case 4:
          stream->header.video.format.tag =
              GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
          break;
        case 5:
          stream->header.video.format.tag =
              GST_MAKE_FOURCC ('D', 'X', '5', '0');
          break;
      }
    } else if (!strcmp (mimetype, "video/x-xvid")) {
      stream->header.video.format.tag = GST_MAKE_FOURCC ('X', 'V', 'I', 'D');
    } else if (!strcmp (mimetype, "video/x-3ivx")) {
      stream->header.video.format.tag = GST_MAKE_FOURCC ('3', 'I', 'V', '2');
    } else if (!strcmp (mimetype, "video/x-msmpeg")) {
      gint msmpegversion;

      gst_structure_get_int (structure, "msmpegversion", &msmpegversion);
      switch (msmpegversion) {
        case 41:
          stream->header.video.format.tag =
              GST_MAKE_FOURCC ('M', 'P', 'G', '4');
          break;
        case 42:
          stream->header.video.format.tag =
              GST_MAKE_FOURCC ('M', 'P', '4', '2');
          break;
        case 43:
          stream->header.video.format.tag =
              GST_MAKE_FOURCC ('M', 'P', '4', '3');
          break;
      }
    } else if (!strcmp (mimetype, "video/x-dv")) {
      stream->header.video.format.tag = GST_MAKE_FOURCC ('D', 'V', 'S', 'D');
    } else if (!strcmp (mimetype, "video/x-h263")) {
      stream->header.video.format.tag = GST_MAKE_FOURCC ('H', '2', '6', '3');
    } else if (!strcmp (mimetype, "video/mpeg")) {
      stream->header.video.format.tag = GST_MAKE_FOURCC ('M', 'P', 'E', 'G');
    }

    if (!stream->header.video.format.tag) {
      return GST_PAD_LINK_REFUSED;
    }

    goto done;
  }
/*  return GST_PAD_LINK_REFUSED; */

done:
  stream->bitrate = 1024 * 1024;
  stream->header.video.format.size = stream->header.video.stream.size;
  stream->header.video.format.width = stream->header.video.stream.width;
  stream->header.video.format.height = stream->header.video.stream.height;
  stream->header.video.format.image_size = stream->header.video.stream.width *
      stream->header.video.stream.height;
  stream->header.video.format.xpels_meter = 0;
  stream->header.video.format.ypels_meter = 0;
  stream->header.video.format.num_colors = 0;
  stream->header.video.format.imp_colors = 0;

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_asfmux_audsink_link (GstPad * pad, const GstCaps * caps)
{
  GstAsfMux *asfmux;
  GstAsfMuxStream *stream = NULL;
  gint n;
  const gchar *mimetype;
  gint rate, channels;
  gboolean ret;
  GstStructure *structure;

  asfmux = GST_ASFMUX (gst_pad_get_parent (pad));

  for (n = 0; n < asfmux->num_outputs; n++) {
    if (asfmux->output[n].pad == pad) {
      stream = &asfmux->output[n];
      break;
    }
  }
  g_assert (n < asfmux->num_outputs);
  g_assert (stream != NULL);
  g_assert (stream->type == ASF_STREAM_AUDIO);

  GST_DEBUG ("asfmux: audio sink_link triggered on %s", gst_pad_get_name (pad));

  structure = gst_caps_get_structure (caps, 0);

  /* we want these for all */
  ret = gst_structure_get_int (structure, "channels", &channels);
  ret &= gst_structure_get_int (structure, "rate", &rate);

  if (!ret)
    return GST_PAD_LINK_REFUSED;

  stream->header.audio.sample_rate = rate;
  stream->header.audio.channels = channels;

  mimetype = gst_structure_get_name (structure);
  if (!strcmp (mimetype, "audio/x-raw-int")) {
    gint block, size;

    stream->header.audio.codec_tag = GST_RIFF_WAVE_FORMAT_PCM;

    gst_structure_get_int (structure, "width", &block);
    gst_structure_get_int (structure, "depth", &size);

    stream->header.audio.block_align = block;
    stream->header.audio.word_size = size;
    stream->header.audio.size = 0;

    /* set some more info straight */
    stream->header.audio.block_align /= 8;
    stream->header.audio.block_align *= stream->header.audio.channels;
    stream->header.audio.byte_rate = stream->header.audio.block_align *
        stream->header.audio.sample_rate;
    goto done;
  } else {
    stream->header.audio.codec_tag = 0;

    if (!strcmp (mimetype, "audio/mpeg")) {
      gint layer = 3;

      gst_structure_get_int (structure, "layer", &layer);
      switch (layer) {
        case 3:
          stream->header.audio.codec_tag = GST_RIFF_WAVE_FORMAT_MPEGL3;
          break;
        case 1:
        case 2:
          stream->header.audio.codec_tag = GST_RIFF_WAVE_FORMAT_MPEGL12;
          break;
      }
    } else if (!strcmp (mimetype, "audio/x-vorbis")) {
      stream->header.audio.codec_tag = GST_RIFF_WAVE_FORMAT_VORBIS3;
    } else if (!strcmp (mimetype, "audio/x-ac3")) {
      stream->header.audio.codec_tag = GST_RIFF_WAVE_FORMAT_A52;
    }

    stream->header.audio.block_align = 1;
    stream->header.audio.byte_rate = 8 * 1024;
    stream->header.audio.word_size = 16;
    stream->header.audio.size = 0;

    if (!stream->header.audio.codec_tag) {
      return GST_PAD_LINK_REFUSED;
    }

    goto done;
  }
/*  return GST_PAD_LINK_REFUSED; */

done:
  stream->bitrate = stream->header.audio.byte_rate * 8;
  return GST_PAD_LINK_OK;
}

static void
gst_asfmux_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  GstAsfMux *asfmux;
  GstAsfMuxStream *stream = NULL;
  gint n;

  asfmux = GST_ASFMUX (gst_pad_get_parent (pad));

  for (n = 0; n < asfmux->num_outputs; n++) {
    if (asfmux->output[n].pad == pad) {
      stream = &asfmux->output[n];
      break;
    }
  }
  g_assert (n < asfmux->num_outputs);
  g_assert (stream != NULL);
  g_assert (stream->connected == FALSE);

  stream->connected = TRUE;
}

static void
gst_asfmux_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  GstAsfMux *asfmux;
  GstAsfMuxStream *stream = NULL;
  gint n;

  asfmux = GST_ASFMUX (gst_pad_get_parent (pad));

  for (n = 0; n < asfmux->num_outputs; n++) {
    if (asfmux->output[n].pad == pad) {
      stream = &asfmux->output[n];
      break;
    }
  }
  g_assert (n < asfmux->num_outputs);
  g_assert (stream != NULL);
  g_assert (stream->connected == TRUE);

  stream->connected = FALSE;
}

static GstPad *
gst_asfmux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstAsfMux *asfmux;
  GstPad *newpad;
  gchar *padname;
  GstPadLinkFunction linkfunc;
  GstAsfMuxStream *stream;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);
  g_return_val_if_fail (templ->direction == GST_PAD_SINK, NULL);
  g_return_val_if_fail (GST_IS_ASFMUX (element), NULL);

  asfmux = GST_ASFMUX (element);

  stream = &asfmux->output[asfmux->num_outputs++];
  stream->queue = NULL;
  stream->time = 0;
  stream->connected = FALSE;
  stream->eos = FALSE;
  stream->seqnum = 0;

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    padname = g_strdup_printf ("audio_%02d", asfmux->num_audio++);
    stream->type = ASF_STREAM_AUDIO;
    linkfunc = gst_asfmux_audsink_link;
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    padname = g_strdup_printf ("video_%02d", asfmux->num_video++);
    stream->type = ASF_STREAM_VIDEO;
    linkfunc = gst_asfmux_vidsink_link;
  } else {
    g_warning ("asfmux: this is not our template!\n");
    return NULL;
  }

  newpad = gst_pad_new_from_template (templ, padname);
  stream->pad = newpad;
  g_free (padname);

  g_signal_connect (newpad, "linked",
      G_CALLBACK (gst_asfmux_pad_link), (gpointer) asfmux);
  g_signal_connect (newpad, "unlinked",
      G_CALLBACK (gst_asfmux_pad_unlink), (gpointer) asfmux);
  gst_pad_set_link_function (newpad, linkfunc);
  gst_element_add_pad (element, newpad);
  gst_pad_set_event_mask_function (newpad, gst_asfmux_get_event_masks);

  return newpad;
}

/* can we seek? If not, we assume we're streamable */
static gboolean
gst_asfmux_can_seek (GstAsfMux * asfmux)
{
#if 0
  const GstEventMask *masks =
      gst_pad_get_event_masks (GST_PAD_PEER (asfmux->srcpad));

  /* this is for stream or file-storage */
  while (masks != NULL && masks->type != 0) {
    if (masks->type == GST_EVENT_SEEK) {
      return TRUE;
    } else {
      masks++;
    }
  }

  return FALSE;
#endif
  return TRUE;
}

static gboolean
gst_asfmux_is_stream (GstAsfMux * asfmux)
{
  /* this is for RTP */
  return FALSE;                 /*!gst_asfmux_can_seek (asfmux) */
}

/* handle events (search) */
static gboolean
gst_asfmux_handle_event (GstPad * pad, GstEvent * event)
{
  GstAsfMux *asfmux;
  GstEventType type;
  gint n;

  asfmux = GST_ASFMUX (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_EOS:
      /* is this allright? */
      for (n = 0; n < asfmux->num_outputs; n++) {
        if (asfmux->output[n].pad == pad) {
          asfmux->output[n].eos = TRUE;
          break;
        }
      }
      if (n == asfmux->num_outputs) {
        g_warning ("Unknown pad for EOS!");
      }
      break;
    default:
      break;
  }

  return TRUE;
}

/* fill the internal queue for each available pad */
static void
gst_asfmux_fill_queue (GstAsfMux * asfmux)
{
  GstBuffer *buffer;
  gint n;

  for (n = 0; n < asfmux->num_outputs; n++) {
    GstAsfMuxStream *stream = &asfmux->output[n];

    while (stream->queue == NULL &&
        stream->pad != NULL &&
        stream->connected == TRUE &&
        GST_PAD_IS_USABLE (stream->pad) && stream->eos == FALSE) {
      buffer = GST_BUFFER (gst_pad_pull (stream->pad));
      if (GST_IS_EVENT (buffer)) {
        gst_asfmux_handle_event (stream->pad, GST_EVENT (buffer));
      } else {
        stream->queue = buffer;
      }
    }
  }
}

static guint
gst_asfmux_packet_remaining (GstAsfMux * asfmux)
{
  guint position;

  if (asfmux->packet != NULL) {
    position = GST_BUFFER_SIZE (asfmux->packet);
  } else {
    position = 0;
  }

  return GST_ASF_PACKET_SIZE - GST_ASF_PACKET_HEADER_SIZE - 2 - position;
}

static void
gst_asfmux_put_buffer (GstBuffer * packet, guint8 * data, guint length)
{
  if ((GST_BUFFER_MAXSIZE (packet) - GST_BUFFER_SIZE (packet)) >= length) {
    guint8 *pos = GST_BUFFER_DATA (packet) + GST_BUFFER_SIZE (packet);

    memcpy (pos, data, length);
    GST_BUFFER_SIZE (packet) += length;
  } else {
    g_warning ("Buffer too small");
  }
}

static void
gst_asfmux_put_byte (GstBuffer * packet, guint8 data)
{
  if ((GST_BUFFER_MAXSIZE (packet) - GST_BUFFER_SIZE (packet)) >= sizeof (data)) {
    guint8 *pos = GST_BUFFER_DATA (packet) + GST_BUFFER_SIZE (packet);

    *(guint8 *) pos = data;
    GST_BUFFER_SIZE (packet) += 1;
  } else {
    g_warning ("Buffer too small");
  }
}

static void
gst_asfmux_put_le16 (GstBuffer * packet, guint16 data)
{
  if ((GST_BUFFER_MAXSIZE (packet) - GST_BUFFER_SIZE (packet)) >= sizeof (data)) {
    guint8 *pos = GST_BUFFER_DATA (packet) + GST_BUFFER_SIZE (packet);

    *(guint16 *) pos = GUINT16_TO_LE (data);
    GST_BUFFER_SIZE (packet) += 2;
  } else {
    g_warning ("Buffer too small");
  }
}

static void
gst_asfmux_put_le32 (GstBuffer * packet, guint32 data)
{
  if ((GST_BUFFER_MAXSIZE (packet) - GST_BUFFER_SIZE (packet)) >= sizeof (data)) {
    guint8 *pos = GST_BUFFER_DATA (packet) + GST_BUFFER_SIZE (packet);

    *(guint32 *) pos = GUINT32_TO_LE (data);
    GST_BUFFER_SIZE (packet) += 4;
  } else {
    g_warning ("Buffer too small");
  }
}

static void
gst_asfmux_put_le64 (GstBuffer * packet, guint64 data)
{
  if ((GST_BUFFER_MAXSIZE (packet) - GST_BUFFER_SIZE (packet)) >= sizeof (data)) {
    guint8 *pos = GST_BUFFER_DATA (packet) + GST_BUFFER_SIZE (packet);

    *(guint64 *) pos = GUINT64_TO_LE (data);
    GST_BUFFER_SIZE (packet) += 8;
  } else {
    g_warning ("Buffer too small");
  }
}

static void
gst_asfmux_put_time (GstBuffer * packet, guint64 time)
{
  gst_asfmux_put_le64 (packet, time + G_GINT64_CONSTANT (116444736000000000));
}

static void
gst_asfmux_put_guid (GstBuffer * packet, ASFGuidHash * hash, guint8 id)
{
  gint n = 0;
  ASFGuid *guid;

  /* find GUID */
  while (hash[n].obj_id != id && hash[n].obj_id != ASF_OBJ_UNDEFINED) {
    n++;
  }
  guid = &hash[n].guid;

  gst_asfmux_put_le32 (packet, guid->v1);
  gst_asfmux_put_le32 (packet, guid->v2);
  gst_asfmux_put_le32 (packet, guid->v3);
  gst_asfmux_put_le32 (packet, guid->v4);
}

static void
gst_asfmux_put_string (GstBuffer * packet, const gchar * str)
{
  gunichar2 *utf16_str = g_utf8_to_utf16 (str, strlen (str), NULL, NULL, NULL);
  gint i, len = strlen (str);

  /* this is not an off-by-one-bug, we need the terminating /0 too */
  for (i = 0; i <= len; i++) {
    gst_asfmux_put_le16 (packet, utf16_str[i]);
  }

  g_free (utf16_str);
}

static void
gst_asfmux_put_flush (GstAsfMux * asfmux)
{
  gst_pad_push (asfmux->srcpad, GST_DATA (gst_event_new_flush ()));
}

/* write an asf chunk (only used in streaming case) */
static void
gst_asfmux_put_chunk (GstBuffer * packet,
    GstAsfMux * asfmux, guint16 type, guint length, gint flags)
{
  gst_asfmux_put_le16 (packet, type);
  gst_asfmux_put_le16 (packet, length + 8);
  gst_asfmux_put_le32 (packet, asfmux->sequence++);
  gst_asfmux_put_le16 (packet, flags);
  gst_asfmux_put_le16 (packet, length + 8);
}

static void
gst_asfmux_put_wav_header (GstBuffer * packet, asf_stream_audio * hdr)
{
  gst_asfmux_put_le16 (packet, hdr->codec_tag);
  gst_asfmux_put_le16 (packet, hdr->channels);
  gst_asfmux_put_le32 (packet, hdr->sample_rate);
  gst_asfmux_put_le32 (packet, hdr->byte_rate);
  gst_asfmux_put_le16 (packet, hdr->block_align);
  gst_asfmux_put_le16 (packet, hdr->word_size);
  gst_asfmux_put_le16 (packet, hdr->size);
}

static void
gst_asfmux_put_vid_header (GstBuffer * packet, asf_stream_video * hdr)
{
  gst_asfmux_put_le32 (packet, hdr->width);
  gst_asfmux_put_le32 (packet, hdr->height);
  gst_asfmux_put_byte (packet, hdr->unknown);
  gst_asfmux_put_le16 (packet, hdr->size);
}

static void
gst_asfmux_put_bmp_header (GstBuffer * packet, asf_stream_video_format * hdr)
{
  gst_asfmux_put_le32 (packet, hdr->size);
  gst_asfmux_put_le32 (packet, hdr->width);
  gst_asfmux_put_le32 (packet, hdr->height);
  gst_asfmux_put_le16 (packet, hdr->planes);
  gst_asfmux_put_le16 (packet, hdr->depth);
  gst_asfmux_put_le32 (packet, hdr->tag);
  gst_asfmux_put_le32 (packet, hdr->image_size);
  gst_asfmux_put_le32 (packet, hdr->xpels_meter);
  gst_asfmux_put_le32 (packet, hdr->ypels_meter);
  gst_asfmux_put_le32 (packet, hdr->num_colors);
  gst_asfmux_put_le32 (packet, hdr->imp_colors);
}

/* init header */
static guint
gst_asfmux_put_header (GstBuffer * packet, ASFGuidHash * hash, guint8 id)
{
  guint pos = GST_BUFFER_SIZE (packet);

  gst_asfmux_put_guid (packet, hash, id);
  gst_asfmux_put_le64 (packet, 24);
  return pos;
}

/* update header size */
static void
gst_asfmux_end_header (GstBuffer * packet, guint pos)
{
  guint cur = GST_BUFFER_SIZE (packet);

  GST_BUFFER_SIZE (packet) = pos + sizeof (ASFGuid);
  gst_asfmux_put_le64 (packet, cur - pos);
  GST_BUFFER_SIZE (packet) = cur;
}

static void
gst_asfmux_file_start (GstAsfMux * asfmux, guint64 file_size, guint64 data_size)
{
  GstBuffer *header = gst_buffer_new_and_alloc (4096);
  guint bitrate;
  guint header_offset, header_pos, header_size;
  gint n;
  guint64 duration;

  bitrate = 0;
  for (n = 0; n < asfmux->num_outputs; n++) {
    GstAsfMuxStream *stream = &asfmux->output[n];

    bitrate += stream->bitrate;
  }

  GST_BUFFER_SIZE (header) = 0;
  if (asfmux->packet != NULL) {
    duration = GST_BUFFER_DURATION (asfmux->packet) +
        GST_BUFFER_TIMESTAMP (asfmux->packet);
  } else {
    duration = 0;
  }

  if (gst_asfmux_is_stream (asfmux)) {
    /* start of stream (length will be patched later) */
    gst_asfmux_put_chunk (header, asfmux, 0x4824, 0, 0xc00);
  }

  gst_asfmux_put_guid (header, asf_object_guids, ASF_OBJ_HEADER);
  /* header length, will be patched after */
  gst_asfmux_put_le64 (header, ~0);
  /* number of chunks in header */
  gst_asfmux_put_le32 (header, 3 + /*has_title + */ asfmux->num_outputs);
  gst_asfmux_put_byte (header, 1);      /* ??? */
  gst_asfmux_put_byte (header, 2);      /* ??? */

  /* file header */
  header_offset = GST_BUFFER_SIZE (header);
  header_pos = gst_asfmux_put_header (header, asf_object_guids, ASF_OBJ_FILE);
  gst_asfmux_put_guid (header, asf_object_guids, ASF_OBJ_UNDEFINED);
  gst_asfmux_put_le64 (header, file_size);
  gst_asfmux_put_time (header, 0);
  gst_asfmux_put_le64 (header, asfmux->num_packets);    /* number of packets */
  gst_asfmux_put_le64 (header, duration / (GST_SECOND / 10000000));     /* end time stamp (in 100ns units) */
  gst_asfmux_put_le64 (header, duration / (GST_SECOND / 10000000));     /* duration (in 100ns units) */
  gst_asfmux_put_le64 (header, 0);      /* start time stamp */
  gst_asfmux_put_le32 (header, gst_asfmux_can_seek (asfmux) ? 0x02 : 0x01);     /* seekable or streamable */
  gst_asfmux_put_le32 (header, GST_ASF_PACKET_SIZE);    /* packet size */
  gst_asfmux_put_le32 (header, GST_ASF_PACKET_SIZE);    /* packet size */
  gst_asfmux_put_le32 (header, bitrate);        /* Nominal data rate in bps */
  gst_asfmux_end_header (header, header_pos);

  /* unknown headers */
  header_pos = gst_asfmux_put_header (header, asf_object_guids, ASF_OBJ_HEAD1);
  gst_asfmux_put_guid (header, asf_object_guids, ASF_OBJ_HEAD2);
  gst_asfmux_put_le32 (header, 6);
  gst_asfmux_put_le16 (header, 0);
  gst_asfmux_end_header (header, header_pos);

  /* title and other infos */
#if 0
  if (has_title) {
    header_pos =
        gst_asfmux_put_header (header, asf_object_guids, ASF_OBJ_COMMENT);
    gst_asfmux_put_le16 (header, 2 * (strlen (title) + 1));
    gst_asfmux_put_le16 (header, 2 * (strlen (author) + 1));
    gst_asfmux_put_le16 (header, 2 * (strlen (copyright) + 1));
    gst_asfmux_put_le16 (header, 2 * (strlen (comment) + 1));
    gst_asfmux_put_le16 (header, 0);    /* rating */
    gst_asfmux_put_string (header, title);
    gst_asfmux_put_string (header, author);
    gst_asfmux_put_string (header, copyright);
    gst_asfmux_put_string (header, comment);
    gst_asfmux_end_header (header, header_pos);
  }
#endif

  /* stream headers */
  for (n = 0; n < asfmux->num_outputs; n++) {
    GstAsfMuxStream *stream = &asfmux->output[n];
    guint obj_size = 0;

    stream->seqnum = 0;
    header_pos =
        gst_asfmux_put_header (header, asf_object_guids, ASF_OBJ_STREAM);

    switch (stream->type) {
      case ASF_STREAM_AUDIO:
        obj_size = 18;
        gst_asfmux_put_guid (header, asf_stream_guids, ASF_STREAM_AUDIO);
        gst_asfmux_put_guid (header, asf_correction_guids, ASF_CORRECTION_OFF);
        break;
      case ASF_STREAM_VIDEO:
        obj_size = 11 + 40;
        gst_asfmux_put_guid (header, asf_stream_guids, ASF_STREAM_VIDEO);
        gst_asfmux_put_guid (header, asf_correction_guids, ASF_CORRECTION_OFF);
        break;
      default:
        g_assert (0);
    }

    gst_asfmux_put_le64 (header, 0);    /* offset */
    gst_asfmux_put_le32 (header, obj_size);     /* wav header len */
    gst_asfmux_put_le32 (header, 0);    /* additional data len */
    gst_asfmux_put_le16 (header, n + 1);        /* stream number */
    gst_asfmux_put_le32 (header, 0);    /* ??? */

    switch (stream->type) {
      case ASF_STREAM_AUDIO:
        gst_asfmux_put_wav_header (header, &stream->header.audio);
        break;
      case ASF_STREAM_VIDEO:
        gst_asfmux_put_vid_header (header, &stream->header.video.stream);
        gst_asfmux_put_bmp_header (header, &stream->header.video.format);
        break;
    }

    gst_asfmux_end_header (header, header_pos);
  }

  /* media comments */
  header_pos =
      gst_asfmux_put_header (header, asf_object_guids, ASF_OBJ_CODEC_COMMENT);
  gst_asfmux_put_guid (header, asf_object_guids, ASF_OBJ_CODEC_COMMENT1);
  gst_asfmux_put_le32 (header, asfmux->num_outputs);
  for (n = 0; n < asfmux->num_outputs; n++) {
    GstAsfMuxStream *stream = &asfmux->output[n];
    const char codec[] = "Unknown codec";

    gst_asfmux_put_le16 (header, stream->index + 1);
    /* Isn't this wrong? This is UTF16! */
    gst_asfmux_put_le16 (header, strlen (codec) + 1);
    gst_asfmux_put_string (header, codec);
    gst_asfmux_put_le16 (header, 0);    /* no parameters */

    /* id */
    switch (stream->type) {
      case ASF_STREAM_AUDIO:
        gst_asfmux_put_le16 (header, 2);
        gst_asfmux_put_le16 (header, stream->header.audio.codec_tag);
        break;
      case ASF_STREAM_VIDEO:
        gst_asfmux_put_le16 (header, 4);
        gst_asfmux_put_le32 (header, stream->header.video.format.tag);
        break;
      default:
        g_assert (0);
    }
  }
  gst_asfmux_end_header (header, header_pos);

  /* patch the header size fields */
  header_pos = GST_BUFFER_SIZE (header);
  header_size = header_pos - header_offset;
  if (gst_asfmux_is_stream (asfmux)) {
    header_size += 8 + 30 + 50;

    GST_BUFFER_SIZE (header) = header_offset - 10 - 30;
    gst_asfmux_put_le16 (header, header_size);
    GST_BUFFER_SIZE (header) = header_offset - 2 - 30;
    gst_asfmux_put_le16 (header, header_size);

    header_size -= 8 + 30 + 50;
  }
  header_size += 24 + 6;
  GST_BUFFER_SIZE (header) = header_offset - 14;
  gst_asfmux_put_le64 (header, header_size);
  GST_BUFFER_SIZE (header) = header_pos;

  /* movie chunk, followed by packets of packet_size */
  asfmux->data_offset = GST_BUFFER_SIZE (header);
  gst_asfmux_put_guid (header, asf_object_guids, ASF_OBJ_DATA);
  gst_asfmux_put_le64 (header, data_size);
  gst_asfmux_put_guid (header, asf_object_guids, ASF_OBJ_UNDEFINED);
  gst_asfmux_put_le64 (header, asfmux->num_packets);    /* nb packets */
  gst_asfmux_put_byte (header, 1);      /* ??? */
  gst_asfmux_put_byte (header, 1);      /* ??? */

  gst_pad_push (asfmux->srcpad, GST_DATA (header));
  asfmux->write_header = FALSE;
}

static void
gst_asfmux_file_stop (GstAsfMux * asfmux)
{
  if (gst_asfmux_is_stream (asfmux)) {
    /* send EOS chunk */
    GstBuffer *footer = gst_buffer_new_and_alloc (16);

    GST_BUFFER_SIZE (footer) = 0;
    gst_asfmux_put_chunk (footer, asfmux, 0x4524, 0, 0);        /* end of stream */
    gst_pad_push (asfmux->srcpad, GST_DATA (footer));
  } else if (gst_asfmux_can_seek (asfmux)) {
    /* rewrite an updated header */
    gint64 filesize;
    GstFormat fmt = GST_FORMAT_BYTES;
    GstEvent *event;

    gst_pad_query (asfmux->srcpad, GST_QUERY_POSITION, &fmt, &filesize);
    event = gst_event_new_seek (GST_SEEK_METHOD_SET | GST_FORMAT_BYTES, 0);
    gst_pad_push (asfmux->srcpad, GST_DATA (event));
    gst_asfmux_file_start (asfmux, filesize, filesize - asfmux->data_offset);
    event =
        gst_event_new_seek (GST_SEEK_METHOD_SET | GST_FORMAT_BYTES, filesize);
    gst_pad_push (asfmux->srcpad, GST_DATA (event));
  }

  gst_asfmux_put_flush (asfmux);
}

static GstBuffer *
gst_asfmux_packet_header (GstAsfMux * asfmux)
{
  GstBuffer *packet = asfmux->packet, *header;
  guint flags, padsize = gst_asfmux_packet_remaining (asfmux);

  header = gst_buffer_new_and_alloc (GST_ASF_PACKET_HEADER_SIZE + 2 + 12);
  GST_BUFFER_SIZE (header) = 0;

  if (gst_asfmux_is_stream (asfmux)) {
    gst_asfmux_put_chunk (header, asfmux, 0x4424, GST_ASF_PACKET_SIZE, 0);
  }

  gst_asfmux_put_byte (header, 0x82);
  gst_asfmux_put_le16 (header, 0);

  flags = 0x01;                 /* nb segments present */
  if (padsize > 0) {
    if (padsize < 256) {
      flags |= 0x08;
    } else {
      flags |= 0x10;
    }
  }
  gst_asfmux_put_byte (header, flags);  /* flags */
  gst_asfmux_put_byte (header, 0x5d);
  if (flags & 0x10) {
    gst_asfmux_put_le16 (header, padsize - 2);
  } else if (flags & 0x08) {
    gst_asfmux_put_byte (header, padsize - 1);
  }
  gst_asfmux_put_le32 (header,
      GST_BUFFER_TIMESTAMP (packet) / (GST_SECOND / 1000));
  gst_asfmux_put_le16 (header,
      GST_BUFFER_DURATION (packet) / (GST_SECOND / 1000));
  gst_asfmux_put_byte (header, asfmux->packet_frames | 0x80);

  return header;
}

static void
gst_asfmux_frame_header (GstAsfMux * asfmux,
    GstAsfMuxStream * stream,
    guint position, guint length, guint total, guint64 time, gboolean key)
{
  /* fill in some values for the packet */
  if (!GST_CLOCK_TIME_IS_VALID (GST_BUFFER_TIMESTAMP (asfmux->packet))) {
    GST_BUFFER_TIMESTAMP (asfmux->packet) = time;
  }
  GST_BUFFER_DURATION (asfmux->packet) =
      time - GST_BUFFER_TIMESTAMP (asfmux->packet);

  gst_asfmux_put_byte (asfmux->packet, (stream->index + 1) | 0x80);     //(key ? 0x80 : 0));
  gst_asfmux_put_byte (asfmux->packet, stream->seqnum);
  gst_asfmux_put_le32 (asfmux->packet, position);
  gst_asfmux_put_byte (asfmux->packet, 0x08);
  gst_asfmux_put_le32 (asfmux->packet, total);
  gst_asfmux_put_le32 (asfmux->packet, time / (GST_SECOND / 1000));     /* time in ms */
  gst_asfmux_put_le16 (asfmux->packet, length);
}

static void
gst_asfmux_frame_buffer (GstAsfMux * asfmux, guint8 * data, guint length)
{
  gst_asfmux_put_buffer (asfmux->packet, data, length);
  asfmux->packet_frames++;
}

static void
gst_asfmux_packet_flush (GstAsfMux * asfmux)
{
  GstBuffer *header, *packet = asfmux->packet;
  guint header_size;

  /* packet header */
  header = gst_asfmux_packet_header (asfmux);
  header_size = GST_BUFFER_SIZE (header);
  if (!gst_asfmux_can_seek (asfmux)) {
    header_size -= 12;          /* hack... bah */
  }

  /* Clear out the padding bytes */
  memset (GST_BUFFER_DATA (packet) + GST_BUFFER_SIZE (packet), 0,
      GST_BUFFER_MAXSIZE (packet) - GST_BUFFER_SIZE (packet));
  GST_BUFFER_SIZE (packet) = GST_ASF_PACKET_SIZE - header_size;

  /* send packet over */
  gst_pad_push (asfmux->srcpad, GST_DATA (header));
  gst_pad_push (asfmux->srcpad, GST_DATA (packet));
  gst_asfmux_put_flush (asfmux);
  asfmux->num_packets++;
  asfmux->packet_frames = 0;

  /* reset packet */
  asfmux->packet = NULL;
}

static void
gst_asfmux_write_buffer (GstAsfMux * asfmux,
    GstAsfMuxStream * stream, GstBuffer * buffer)
{
  guint position = 0, to_write, size = GST_BUFFER_SIZE (buffer), remaining;

  while (position < size) {
    remaining = gst_asfmux_packet_remaining (asfmux);
    if (remaining <= GST_ASF_FRAME_HEADER_SIZE) {
      gst_asfmux_packet_flush (asfmux);
      continue;
    } else if (remaining >= (GST_ASF_FRAME_HEADER_SIZE + size - position)) {
      to_write = size - position;
    } else {
      to_write = remaining - GST_ASF_FRAME_HEADER_SIZE;
    }

    if (asfmux->packet == NULL) {
      asfmux->packet_frames = 0;
      asfmux->packet = gst_buffer_new_and_alloc (GST_ASF_PACKET_SIZE);
      GST_BUFFER_SIZE (asfmux->packet) = 0;
    }

    /* write frame header plus data in this packet */
    gst_asfmux_frame_header (asfmux, stream, position, to_write, size,
        GST_BUFFER_TIMESTAMP (buffer),
        GST_BUFFER_FLAG_IS_SET (buffer, GST_BUFFER_KEY_UNIT));
    gst_asfmux_frame_buffer (asfmux, GST_BUFFER_DATA (buffer) + position,
        to_write);

    position += to_write;
  }

  stream->seqnum++;
}

/* take the oldest buffer in our internal queue and push-it */
static gboolean
gst_asfmux_do_one_buffer (GstAsfMux * asfmux)
{
  gint n, chosen = -1;

  /* find the earliest buffer */
  for (n = 0; n < asfmux->num_outputs; n++) {
    if (asfmux->output[n].queue != NULL) {
      if (chosen == -1 ||
          GST_BUFFER_TIMESTAMP (asfmux->output[n].queue) <
          GST_BUFFER_TIMESTAMP (asfmux->output[chosen].queue)) {
        chosen = n;
      }
    }
  }

  if (chosen == -1) {
    /* simply finish off the file and send EOS */
    gst_asfmux_file_stop (asfmux);
    gst_pad_push (asfmux->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (asfmux));
    return FALSE;
  }

  /* do this buffer */
  gst_asfmux_write_buffer (asfmux, &asfmux->output[chosen],
      asfmux->output[chosen].queue);

  /* update stream info after buffer push */
  gst_buffer_unref (asfmux->output[chosen].queue);
  asfmux->output[chosen].time =
      GST_BUFFER_TIMESTAMP (asfmux->output[chosen].queue);
  asfmux->output[chosen].queue = NULL;

  return TRUE;
}

static void
gst_asfmux_loop (GstElement * element)
{
  GstAsfMux *asfmux;

  asfmux = GST_ASFMUX (element);

  /* first fill queue (some elements only set caps when
   * flowing data), then write header */
  gst_asfmux_fill_queue (asfmux);

  if (asfmux->write_header == TRUE) {
    /* indeed, these are fake values. We need this so that
     * players will read the file. Without these fake values,
     * the players will mark the file as invalid and stop */
    gst_asfmux_file_start (asfmux, 0xFFFFFFFF, 0xFFFFFFFF);
  }

  gst_asfmux_do_one_buffer (asfmux);
}

static GstStateChangeReturn
gst_asfmux_change_state (GstElement * element, GstStateChange transition)
{
  GstAsfMux *asfmux;

  g_return_val_if_fail (GST_IS_ASFMUX (element), GST_STATE_CHANGE_FAILURE);

  asfmux = GST_ASFMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      for (n = 0; n < asfmux->num_outputs; n++) {
        asfmux->output[n].eos = FALSE;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
