/* AVI muxer plugin for GStreamer
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
 * - the old avimuxer (by Wim Taymans)
 * - xawtv's aviwriter (by Gerd Knorr)
 * - mjpegtools' avilib (by Rainer Johanni)
 * - openDML large-AVI docs
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gst/gst-i18n-plugin.h"
#include <stdlib.h>
#include <string.h>

#include <gst/video/video.h>

#include "gstavimux.h"

#ifndef LE_FROM_GUINT16
#define LE_FROM_GUINT16 GUINT16_FROM_LE
#endif
#ifndef LE_FROM_GUINT32
#define LE_FROM_GUINT32 GUINT32_FROM_LE
#endif

/* AviMux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BIGFILE,
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-msvideo")
    );

static GstStaticPadTemplate video_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("video_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) { YUY2, I420 }, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ]; "
        "video/x-jpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ]; "
        "video/x-divx, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "divxversion = (int) [ 3, 5 ]; "
        "video/x-xvid, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ]; "
        "video/x-3ivx, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ]; "
        "video/x-msmpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "msmpegversion = (int) [ 41, 43 ]; "
        "video/mpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "mpegversion = (int) 1, "
        "systemstream = (boolean) FALSE; "
        "video/x-h263, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ]; "
        "video/x-dv, "
        "width = (int) 720, "
        "height = (int) { 576, 480 }, "
        "systemstream = (boolean) FALSE; "
        "video/x-huffyuv, "
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ]")
    );

static GstStaticPadTemplate audio_sink_factory =
    GST_STATIC_PAD_TEMPLATE ("audio_%d",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) LITTLE_ENDIAN, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "rate = (int) [ 1000, 96000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        "rate = (int) [ 1000, 96000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-vorbis, "
        "rate = (int) [ 1000, 96000 ], "
        "channels = (int) [ 1, 2 ]; "
        "audio/x-ac3, "
        "rate = (int) [ 1000, 96000 ], " "channels = (int) [ 1, 2 ]")
    );


static void gst_avimux_base_init (gpointer g_class);
static void gst_avimux_class_init (GstAviMuxClass * klass);
static void gst_avimux_init (GstAviMux * avimux);

static void gst_avimux_loop (GstElement * element);
static gboolean gst_avimux_handle_event (GstPad * pad, GstEvent * event);
static GstPad *gst_avimux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_avimux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_avimux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstElementStateReturn gst_avimux_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_avimux_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_avimux_get_type (void)
{
  static GType avimux_type = 0;

  if (!avimux_type) {
    static const GTypeInfo avimux_info = {
      sizeof (GstAviMuxClass),
      gst_avimux_base_init,
      NULL,
      (GClassInitFunc) gst_avimux_class_init,
      NULL,
      NULL,
      sizeof (GstAviMux),
      0,
      (GInstanceInitFunc) gst_avimux_init,
    };

    avimux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAviMux", &avimux_info, 0);
  }
  return avimux_type;
}

static void
gst_avimux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  static GstElementDetails gst_avimux_details =
      GST_ELEMENT_DETAILS ("Avi multiplexer",
      "Codec/Muxer",
      "Muxes audio and video into an avi stream",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));

  gst_element_class_set_details (element_class, &gst_avimux_details);
}

static void
gst_avimux_class_init (GstAviMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIGFILE,
      g_param_spec_boolean ("bigfile", "Bigfile Support",
          "Support for openDML-2.0 (big) AVI files", 0, G_PARAM_READWRITE));

  gstelement_class->request_new_pad = gst_avimux_request_new_pad;

  gstelement_class->change_state = gst_avimux_change_state;

  gstelement_class->get_property = gst_avimux_get_property;
  gstelement_class->set_property = gst_avimux_set_property;
}

static const GstEventMask *
gst_avimux_get_event_masks (GstPad * pad)
{
  static const GstEventMask gst_avimux_sink_event_masks[] = {
    {GST_EVENT_EOS, 0},
    {0,}
  };

  return gst_avimux_sink_event_masks;
}

static void
gst_avimux_init (GstAviMux * avimux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (avimux);

  avimux->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_element_add_pad (GST_ELEMENT (avimux), avimux->srcpad);

  GST_FLAG_SET (GST_ELEMENT (avimux), GST_ELEMENT_EVENT_AWARE);

  avimux->audiosinkpad = NULL;
  avimux->audio_pad_connected = FALSE;
  avimux->videosinkpad = NULL;
  avimux->video_pad_connected = FALSE;

  avimux->audio_buffer_queue = NULL;
  avimux->video_buffer_queue = NULL;

  avimux->num_frames = 0;

  /* audio/video/AVI header initialisation */
  memset (&(avimux->avi_hdr), 0, sizeof (gst_riff_avih));
  memset (&(avimux->vids_hdr), 0, sizeof (gst_riff_strh));
  memset (&(avimux->vids), 0, sizeof (gst_riff_strf_vids));
  memset (&(avimux->auds_hdr), 0, sizeof (gst_riff_strh));
  memset (&(avimux->auds), 0, sizeof (gst_riff_strf_auds));
  avimux->vids_hdr.type = GST_MAKE_FOURCC ('v', 'i', 'd', 's');
  avimux->vids_hdr.rate = 1000000;
  avimux->avi_hdr.max_bps = 10000000;
  avimux->auds_hdr.type = GST_MAKE_FOURCC ('a', 'u', 'd', 's');
  avimux->vids_hdr.quality = 0xFFFFFFFF;
  avimux->auds_hdr.quality = 0xFFFFFFFF;

  avimux->idx = NULL;

  avimux->write_header = TRUE;

  avimux->enable_large_avi = TRUE;

  gst_element_set_loop_function (GST_ELEMENT (avimux), gst_avimux_loop);
}

static GstPadLinkReturn
gst_avimux_vidsinkconnect (GstPad * pad, const GstCaps * vscaps)
{
  GstAviMux *avimux;
  GstStructure *structure;
  const gchar *mimetype;
  gdouble fps = 0.;
  gboolean ret;

  avimux = GST_AVIMUX (gst_pad_get_parent (pad));

  GST_DEBUG ("avimux: video sinkconnect triggered on %s",
      gst_pad_get_name (pad));

  structure = gst_caps_get_structure (vscaps, 0);
  mimetype = gst_structure_get_name (structure);

  /* global */
  avimux->vids.size = sizeof (gst_riff_strf_vids);
  avimux->vids.planes = 1;
  ret = gst_structure_get_int (structure, "width", &avimux->vids.width);
  ret &= gst_structure_get_int (structure, "height", &avimux->vids.height);
  ret &= gst_structure_get_double (structure, "framerate", &fps);
  if (!ret)
    return GST_PAD_LINK_REFUSED;

  if (fps != 0.)
    avimux->vids_hdr.scale = avimux->vids_hdr.rate / fps;

  if (!strcmp (mimetype, "video/x-raw-yuv")) {
    guint32 format;

    gst_structure_get_fourcc (structure, "format", &format);
    avimux->vids.compression = format;
    switch (format) {
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        avimux->vids.bit_cnt = 16;
        break;
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        avimux->vids.bit_cnt = 12;
        break;
    }
  } else {
    avimux->vids.bit_cnt = 24;
    avimux->vids.compression = 0;

    /* find format */
    if (!strcmp (mimetype, "video/x-huffyuv")) {
      avimux->vids.compression = GST_MAKE_FOURCC ('H', 'F', 'Y', 'U');
    } else if (!strcmp (mimetype, "video/x-jpeg")) {
      avimux->vids.compression = GST_MAKE_FOURCC ('M', 'J', 'P', 'G');
    } else if (!strcmp (mimetype, "video/x-divx")) {
      gint divxversion;

      gst_structure_get_int (structure, "divxversion", &divxversion);
      switch (divxversion) {
        case 3:
          avimux->vids.compression = GST_MAKE_FOURCC ('D', 'I', 'V', '3');
          break;
        case 4:
          avimux->vids.compression = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
          break;
        case 5:
          avimux->vids.compression = GST_MAKE_FOURCC ('D', 'X', '5', '0');
          break;
      }
    } else if (!strcmp (mimetype, "video/x-xvid")) {
      avimux->vids.compression = GST_MAKE_FOURCC ('X', 'V', 'I', 'D');
    } else if (!strcmp (mimetype, "video/x-3ivx")) {
      avimux->vids.compression = GST_MAKE_FOURCC ('3', 'I', 'V', '2');
    } else if (!strcmp (mimetype, "video/x-msmpeg")) {
      gint msmpegversion;

      gst_structure_get_int (structure, "msmpegversion", &msmpegversion);
      switch (msmpegversion) {
        case 41:
          avimux->vids.compression = GST_MAKE_FOURCC ('M', 'P', 'G', '4');
          break;
        case 42:
          avimux->vids.compression = GST_MAKE_FOURCC ('M', 'P', '4', '2');
          break;
        case 43:
          avimux->vids.compression = GST_MAKE_FOURCC ('M', 'P', '4', '3');
          break;
      }
    } else if (!strcmp (mimetype, "video/x-dv")) {
      avimux->vids.compression = GST_MAKE_FOURCC ('D', 'V', 'S', 'D');
    } else if (!strcmp (mimetype, "video/x-h263")) {
      avimux->vids.compression = GST_MAKE_FOURCC ('H', '2', '6', '3');
    } else if (!strcmp (mimetype, "video/mpeg")) {
      avimux->vids.compression = GST_MAKE_FOURCC ('M', 'P', 'E', 'G');
    }

    if (!avimux->vids.compression) {
      return GST_PAD_LINK_DELAYED;
    }
  }

  avimux->vids_hdr.fcc_handler = avimux->vids.compression;
  avimux->vids.image_size = avimux->vids.height * avimux->vids.width;
  avimux->avi_hdr.width = avimux->vids.width;
  avimux->avi_hdr.height = avimux->vids.height;
  avimux->avi_hdr.us_frame = avimux->vids_hdr.scale;
  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_avimux_audsinkconnect (GstPad * pad, const GstCaps * vscaps)
{
  GstAviMux *avimux;
  GstStructure *structure;
  const gchar *mimetype;
  int i;

  avimux = GST_AVIMUX (gst_pad_get_parent (pad));

  GST_DEBUG ("avimux: audio sinkconnect triggered on %s",
      gst_pad_get_name (pad));

  structure = gst_caps_get_structure (vscaps, 0);
  mimetype = gst_structure_get_name (structure);

  /* we want these for all */
  gst_structure_get_int (structure, "channels", &i);
  avimux->auds.channels = i;
  gst_structure_get_int (structure, "rate", &i);
  avimux->auds.rate = i;

  if (!strcmp (mimetype, "audio/x-raw-int")) {
    avimux->auds.format = GST_RIFF_WAVE_FORMAT_PCM;

    gst_structure_get_int (structure, "width", &i);
    avimux->auds.blockalign = i;
    gst_structure_get_int (structure, "depth", &i);
    avimux->auds.size = i;

    /* set some more info straight */
    avimux->auds.blockalign /= 8;
    avimux->auds.blockalign *= avimux->auds.channels;
    avimux->auds.av_bps = avimux->auds.blockalign * avimux->auds.rate;
  } else if (!strcmp (mimetype, "audio/mpeg") ||
      !strcmp (mimetype, "audio/x-vorbis") ||
      !strcmp (mimetype, "audio/x-ac3")) {
    avimux->auds.format = 0;

    if (!strcmp (mimetype, "audio/mpeg")) {
      gint layer = 3;

      gst_structure_get_int (structure, "layer", &layer);
      switch (layer) {
        case 3:
          avimux->auds.format = GST_RIFF_WAVE_FORMAT_MPEGL3;
          break;
        case 1:
        case 2:
          avimux->auds.format = GST_RIFF_WAVE_FORMAT_MPEGL12;
          break;
      }
    } else if (!strcmp (mimetype, "audio/x-vorbis")) {
      avimux->auds.format = GST_RIFF_WAVE_FORMAT_VORBIS3;
    } else if (!strcmp (mimetype, "audio/x-ac3")) {
      avimux->auds.format = GST_RIFF_WAVE_FORMAT_A52;
    }

    avimux->auds.blockalign = 1;
    avimux->auds.av_bps = 0;
    avimux->auds.size = 16;

    if (!avimux->auds.format) {
      return GST_PAD_LINK_REFUSED;
    }
  }

  avimux->auds_hdr.rate = avimux->auds.blockalign * avimux->auds.rate;
  avimux->auds_hdr.samplesize = avimux->auds.blockalign;
  avimux->auds_hdr.scale = avimux->auds.blockalign;
  return GST_PAD_LINK_OK;
}

static void
gst_avimux_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  GstAviMux *avimux = GST_AVIMUX (data);
  const gchar *padname = gst_pad_get_name (pad);

  if (pad == avimux->audiosinkpad) {
    avimux->audio_pad_connected = TRUE;
  } else if (pad == avimux->videosinkpad) {
    avimux->video_pad_connected = TRUE;
  } else {
    g_warning ("Unknown padname '%s'", padname);
    return;
  }

  GST_DEBUG ("pad '%s' connected", padname);
}

static void
gst_avimux_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  GstAviMux *avimux = GST_AVIMUX (data);
  const gchar *padname = gst_pad_get_name (pad);

  if (pad == avimux->audiosinkpad) {
    avimux->audio_pad_connected = FALSE;
    avimux->audiosinkpad = NULL;
  } else if (pad == avimux->videosinkpad) {
    avimux->video_pad_connected = FALSE;
    avimux->videosinkpad = NULL;
  } else {
    g_warning ("Unknown padname '%s'", padname);
    return;
  }

  GST_DEBUG ("pad '%s' unlinked", padname);
}

static GstPad *
gst_avimux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name)
{
  GstAviMux *avimux;
  GstPad *newpad;
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);

  g_return_val_if_fail (templ != NULL, NULL);

  if (templ->direction != GST_PAD_SINK) {
    g_warning ("avimux: request pad that is not a SINK pad\n");
    return NULL;
  }

  g_return_val_if_fail (GST_IS_AVIMUX (element), NULL);

  avimux = GST_AVIMUX (element);

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    g_return_val_if_fail (avimux->audiosinkpad == NULL, NULL);
    newpad = gst_pad_new_from_template (templ, "audio_00");
    gst_pad_set_link_function (newpad, gst_avimux_audsinkconnect);
    avimux->audiosinkpad = newpad;
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    g_return_val_if_fail (avimux->videosinkpad == NULL, NULL);
    newpad = gst_pad_new_from_template (templ, "video_00");
    gst_pad_set_link_function (newpad, gst_avimux_vidsinkconnect);
    avimux->videosinkpad = newpad;
  } else {
    g_warning ("avimux: this is not our template!\n");
    return NULL;
  }

  g_signal_connect (newpad, "linked",
      G_CALLBACK (gst_avimux_pad_link), (gpointer) avimux);
  g_signal_connect (newpad, "unlinked",
      G_CALLBACK (gst_avimux_pad_unlink), (gpointer) avimux);
  gst_element_add_pad (element, newpad);
  gst_pad_set_event_mask_function (newpad, gst_avimux_get_event_masks);

  return newpad;
}

/* maybe some of these functions should be moved to riff.h? */

/* DISCLAIMER: this function is ugly. So be it (i.e. it makes the rest easier) */

static GstBuffer *
gst_avimux_riff_get_avi_header (GstAviMux * avimux)
{
  GstBuffer *buffer;
  guint8 *buffdata;
  guint16 temp16;
  guint32 temp32;

  buffer = gst_buffer_new ();

  /* first, let's see what actually needs to be in the buffer */
  GST_BUFFER_SIZE (buffer) = 0;
  GST_BUFFER_SIZE (buffer) += 32 + sizeof (gst_riff_avih);      /* avi header */
  if (avimux->video_pad_connected) {    /* we have video */
    GST_BUFFER_SIZE (buffer) += 28 + sizeof (gst_riff_strh) + sizeof (gst_riff_strf_vids);      /* vid hdr */
    GST_BUFFER_SIZE (buffer) += 24;     /* odml header */
  }
  if (avimux->audio_pad_connected) {    /* we have audio */
    GST_BUFFER_SIZE (buffer) += 28 + sizeof (gst_riff_strh) + sizeof (gst_riff_strf_auds);      /* aud hdr */
  }
  /* this is the "riff size" */
  avimux->header_size = GST_BUFFER_SIZE (buffer);
  GST_BUFFER_SIZE (buffer) += 12;       /* avi data header */

  /* allocate the buffer */
  buffdata = GST_BUFFER_DATA (buffer) = g_malloc (GST_BUFFER_SIZE (buffer));

  /* avi header metadata */
  memcpy (buffdata, "RIFF", 4);
  buffdata += 4;
  temp32 =
      LE_FROM_GUINT32 (avimux->header_size + avimux->idx_size +
      avimux->data_size);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  memcpy (buffdata, "AVI ", 4);
  buffdata += 4;
  memcpy (buffdata, "LIST", 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->header_size - 4 * 5);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  memcpy (buffdata, "hdrl", 4);
  buffdata += 4;
  memcpy (buffdata, "avih", 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (sizeof (gst_riff_avih));
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  /* the AVI header itself */
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.us_frame);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.max_bps);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.pad_gran);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.flags);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.tot_frames);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.init_frames);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.streams);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.bufsize);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.width);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.height);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.scale);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.rate);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.start);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->avi_hdr.length);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;

  if (avimux->video_pad_connected) {
    /* video header metadata */
    memcpy (buffdata, "LIST", 4);
    buffdata += 4;
    temp32 =
        LE_FROM_GUINT32 (sizeof (gst_riff_strh) + sizeof (gst_riff_strf_vids) +
        4 * 5);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    memcpy (buffdata, "strl", 4);
    buffdata += 4;
    /* generic header */
    memcpy (buffdata, "strh", 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (sizeof (gst_riff_strh));
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    /* the actual header */
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.type);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.fcc_handler);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.flags);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.priority);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.init_frames);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.scale);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.rate);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.start);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.length);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.bufsize);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.quality);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids_hdr.samplesize);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    /* the video header */
    memcpy (buffdata, "strf", 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (sizeof (gst_riff_strf_vids));
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    /* the actual header */
    temp32 = LE_FROM_GUINT32 (avimux->vids.size);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids.width);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids.height);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp16 = LE_FROM_GUINT16 (avimux->vids.planes);
    memcpy (buffdata, &temp16, 2);
    buffdata += 2;
    temp16 = LE_FROM_GUINT16 (avimux->vids.bit_cnt);
    memcpy (buffdata, &temp16, 2);
    buffdata += 2;
    temp32 = LE_FROM_GUINT32 (avimux->vids.compression);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids.image_size);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids.xpels_meter);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids.ypels_meter);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids.num_colors);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->vids.imp_colors);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
  }

  if (avimux->audio_pad_connected) {
    /* audio header */
    memcpy (buffdata, "LIST", 4);
    buffdata += 4;
    temp32 =
        LE_FROM_GUINT32 (sizeof (gst_riff_strh) + sizeof (gst_riff_strf_auds) +
        4 * 5);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    memcpy (buffdata, "strl", 4);
    buffdata += 4;
    /* generic header */
    memcpy (buffdata, "strh", 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (sizeof (gst_riff_strh));
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    /* the actual header */
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.type);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.fcc_handler);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.flags);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.priority);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.init_frames);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.scale);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.rate);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.start);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.length);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.bufsize);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.quality);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds_hdr.samplesize);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    /* the audio header */
    memcpy (buffdata, "strf", 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (sizeof (gst_riff_strf_auds));
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    /* the actual header */
    temp16 = LE_FROM_GUINT16 (avimux->auds.format);
    memcpy (buffdata, &temp16, 2);
    buffdata += 2;
    temp16 = LE_FROM_GUINT16 (avimux->auds.channels);
    memcpy (buffdata, &temp16, 2);
    buffdata += 2;
    temp32 = LE_FROM_GUINT32 (avimux->auds.rate);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->auds.av_bps);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp16 = LE_FROM_GUINT16 (avimux->auds.blockalign);
    memcpy (buffdata, &temp16, 2);
    buffdata += 2;
    temp16 = LE_FROM_GUINT16 (avimux->auds.size);
    memcpy (buffdata, &temp16, 2);
    buffdata += 2;
  }

  if (avimux->video_pad_connected) {
    /* odml header */
    memcpy (buffdata, "LIST", 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (sizeof (guint32) + 4 * 3);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    memcpy (buffdata, "odml", 4);
    buffdata += 4;
    memcpy (buffdata, "dmlh", 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (sizeof (guint32));
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
    temp32 = LE_FROM_GUINT32 (avimux->total_frames);
    memcpy (buffdata, &temp32, 4);
    buffdata += 4;
  }

  /* avi data header */
  memcpy (buffdata, "LIST", 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (avimux->data_size);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  memcpy (buffdata, "movi", 4);

  return buffer;
}

static GstBuffer *
gst_avimux_riff_get_avix_header (guint32 datax_size)
{
  GstBuffer *buffer;
  guint8 *buffdata;
  guint32 temp32;

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = 24;
  buffdata = GST_BUFFER_DATA (buffer) = g_malloc (GST_BUFFER_SIZE (buffer));

  memcpy (buffdata, "LIST", 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (datax_size + 4 * 4);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  memcpy (buffdata, "AVIX", 4);
  buffdata += 4;
  memcpy (buffdata, "LIST", 4);
  buffdata += 4;
  temp32 = LE_FROM_GUINT32 (datax_size);
  memcpy (buffdata, &temp32, 4);
  buffdata += 4;
  memcpy (buffdata, "movi", 4);

  return buffer;
}

static GstBuffer *
gst_avimux_riff_get_video_header (guint32 video_frame_size)
{
  GstBuffer *buffer;
  guint32 temp32;

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = g_malloc (8);
  GST_BUFFER_SIZE (buffer) = 8;
  memcpy (GST_BUFFER_DATA (buffer), "00db", 4);
  temp32 = LE_FROM_GUINT32 (video_frame_size);
  memcpy (GST_BUFFER_DATA (buffer) + 4, &temp32, 4);

  return buffer;
}

static GstBuffer *
gst_avimux_riff_get_audio_header (guint32 audio_sample_size)
{
  GstBuffer *buffer;
  guint32 temp32;

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = g_malloc (8);
  GST_BUFFER_SIZE (buffer) = 8;
  memcpy (GST_BUFFER_DATA (buffer), "01wb", 4);
  temp32 = LE_FROM_GUINT32 (audio_sample_size);
  memcpy (GST_BUFFER_DATA (buffer) + 4, &temp32, 4);

  return buffer;
}

/* some other usable functions (thankyou xawtv ;-) ) */

static void
gst_avimux_add_index (GstAviMux * avimux, guchar * code, guint32 flags,
    guint32 size)
{
  if (avimux->idx_index == avimux->idx_count) {
    avimux->idx_count += 256;
    avimux->idx =
        realloc (avimux->idx,
        avimux->idx_count * sizeof (gst_riff_index_entry));
  }
  memcpy (&(avimux->idx[avimux->idx_index].id), code, 4);
  avimux->idx[avimux->idx_index].flags = LE_FROM_GUINT32 (flags);
  avimux->idx[avimux->idx_index].offset = LE_FROM_GUINT32 (avimux->idx_offset);
  avimux->idx[avimux->idx_index].size = LE_FROM_GUINT32 (size);
  avimux->idx_index++;
}

static void
gst_avimux_write_index (GstAviMux * avimux)
{
  GstBuffer *buffer;
  guint32 temp32;

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = 8;
  GST_BUFFER_DATA (buffer) = g_malloc (8);
  memcpy (GST_BUFFER_DATA (buffer), "idx1", 4);
  temp32 = LE_FROM_GUINT32 (avimux->idx_index * sizeof (gst_riff_index_entry));
  memcpy (GST_BUFFER_DATA (buffer) + 4, &temp32, 4);
  gst_pad_push (avimux->srcpad, GST_DATA (buffer));

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = avimux->idx_index * sizeof (gst_riff_index_entry);
  GST_BUFFER_DATA (buffer) = (unsigned char *) avimux->idx;
  avimux->idx = NULL;           /* will be free()'ed by gst_buffer_unref() */
  avimux->total_data += GST_BUFFER_SIZE (buffer);
  gst_pad_push (avimux->srcpad, GST_DATA (buffer));

  avimux->idx_size += avimux->idx_index * sizeof (gst_riff_index_entry) + 8;

  /* update header */
  avimux->avi_hdr.flags |= GST_RIFF_AVIH_HASINDEX;
}

static void
gst_avimux_bigfile (GstAviMux * avimux, gboolean last)
{
  GstBuffer *header;
  GstEvent *event;

  if (avimux->is_bigfile) {
    /* sarch back */
    event = gst_event_new_seek (GST_FORMAT_BYTES |
        GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, avimux->avix_start);
    /* if the event succeeds */
    gst_pad_push (avimux->srcpad, GST_DATA (event));

    /* rewrite AVIX header */
    header = gst_avimux_riff_get_avix_header (avimux->datax_size);
    gst_pad_push (avimux->srcpad, GST_DATA (header));

    /* go back to current location */
    event = gst_event_new_seek (GST_FORMAT_BYTES |
        GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, avimux->total_data);
    gst_pad_push (avimux->srcpad, GST_DATA (event));
  }
  avimux->avix_start = avimux->total_data;

  if (last)
    return;

  avimux->is_bigfile = TRUE;
  avimux->numx_frames = 0;
  avimux->datax_size = 0;

  header = gst_avimux_riff_get_avix_header (0);
  avimux->total_data += GST_BUFFER_SIZE (header);
  gst_pad_push (avimux->srcpad, GST_DATA (header));
}

/* enough header blabla now, let's go on to actually writing the headers */

static void
gst_avimux_start_file (GstAviMux * avimux)
{
  GstBuffer *header;

  avimux->total_data = 0;
  avimux->total_frames = 0;
  avimux->data_size = 4;        /* ? */
  avimux->datax_size = 0;
  avimux->num_frames = 0;
  avimux->numx_frames = 0;
  avimux->audio_size = 0;
  avimux->audio_time = 0;
  avimux->avix_start = 0;

  avimux->idx_index = 0;
  avimux->idx_offset = 0;       /* see 10 lines below */
  avimux->idx_size = 0;
  avimux->idx_count = 0;
  avimux->idx = NULL;

  /* header */
  avimux->avi_hdr.streams =
      (avimux->video_pad_connected ? 1 : 0) +
      (avimux->audio_pad_connected ? 1 : 0);
  avimux->is_bigfile = FALSE;

  header = gst_avimux_riff_get_avi_header (avimux);
  avimux->total_data += GST_BUFFER_SIZE (header);
  avimux->idx_offset = avimux->total_data;
  gst_pad_push (avimux->srcpad, GST_DATA (header));

  avimux->write_header = FALSE;
  avimux->restart = FALSE;
}

static void
gst_avimux_stop_file (GstAviMux * avimux)
{
  GstEvent *event;
  GstBuffer *header;

  /* if bigfile, rewrite header, else write indexes */
  if (avimux->video_pad_connected) {
    if (avimux->is_bigfile) {
      gst_avimux_bigfile (avimux, TRUE);
      avimux->idx_size = 0;
    } else {
      gst_avimux_write_index (avimux);
    }
  }

  /* statistics/total_frames/... */
  avimux->avi_hdr.tot_frames = avimux->num_frames;
  if (avimux->video_pad_connected) {
    avimux->vids_hdr.length = avimux->num_frames;
  }
  if (avimux->audio_pad_connected) {
    avimux->auds_hdr.length =
        (avimux->audio_time * avimux->auds.rate) / GST_SECOND;
  }

  /* set rate and everything having to do with that */
  avimux->avi_hdr.max_bps = 0;
  if (avimux->audio_pad_connected) {
    /* calculate bps if needed */
    if (!avimux->auds.av_bps) {
      if (avimux->audio_time) {
        avimux->auds_hdr.rate =
            (GST_SECOND * avimux->audio_size) / avimux->audio_time;
      } else {
        GST_ELEMENT_ERROR (avimux, STREAM, MUX,
            (_("No or invalid input audio, AVI stream will be corrupt.")),
            (NULL));
        avimux->auds_hdr.rate = 0;
      }
      avimux->auds.av_bps = avimux->auds_hdr.rate * avimux->auds_hdr.scale;
    }
    avimux->avi_hdr.max_bps += avimux->auds.av_bps;
  }
  if (avimux->video_pad_connected) {
    avimux->avi_hdr.max_bps += ((avimux->vids.bit_cnt + 7) / 8) *
        (1000000. / avimux->avi_hdr.us_frame) * avimux->vids.image_size;
  }

  /* seek and rewrite the header */
  header = gst_avimux_riff_get_avi_header (avimux);
  event = gst_event_new_seek (GST_FORMAT_BYTES | GST_SEEK_METHOD_SET, 0);
  gst_pad_push (avimux->srcpad, GST_DATA (event));
  gst_pad_push (avimux->srcpad, GST_DATA (header));
  event = gst_event_new_seek (GST_FORMAT_BYTES |
      GST_SEEK_METHOD_SET, avimux->total_data);
  gst_pad_push (avimux->srcpad, GST_DATA (event));

  avimux->write_header = TRUE;
}

static void
gst_avimux_restart_file (GstAviMux * avimux)
{
  GstEvent *event;

  gst_avimux_stop_file (avimux);

  event = gst_event_new (GST_EVENT_EOS);
  gst_pad_push (avimux->srcpad, GST_DATA (event));

  gst_avimux_start_file (avimux);
}

/* handle events (search) */
static gboolean
gst_avimux_handle_event (GstPad * pad, GstEvent * event)
{
  GstAviMux *avimux;
  GstEventType type;

  avimux = GST_AVIMUX (gst_pad_get_parent (pad));

  type = event ? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;

  switch (type) {
    case GST_EVENT_EOS:
      /* is this allright? */
      if (pad == avimux->videosinkpad) {
        avimux->video_pad_eos = TRUE;
      } else if (pad == avimux->audiosinkpad) {
        avimux->audio_pad_eos = TRUE;
      } else {
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
gst_avimux_fill_queue (GstAviMux * avimux)
{
  GstBuffer *buffer;

  while (!avimux->audio_buffer_queue &&
      avimux->audiosinkpad &&
      avimux->audio_pad_connected &&
      GST_PAD_IS_USABLE (avimux->audiosinkpad) && !avimux->audio_pad_eos) {
    buffer = GST_BUFFER (gst_pad_pull (avimux->audiosinkpad));
    if (GST_IS_EVENT (buffer)) {
      gst_avimux_handle_event (avimux->audiosinkpad, GST_EVENT (buffer));
    } else {
      avimux->audio_buffer_queue = buffer;
      break;
    }
  }

  while (!avimux->video_buffer_queue &&
      avimux->videosinkpad &&
      avimux->video_pad_connected &&
      GST_PAD_IS_USABLE (avimux->videosinkpad) && !avimux->video_pad_eos) {
    buffer = GST_BUFFER (gst_pad_pull (avimux->videosinkpad));
    if (GST_IS_EVENT (buffer)) {
      gst_avimux_handle_event (avimux->videosinkpad, GST_EVENT (buffer));
    } else {
      avimux->video_buffer_queue = buffer;
      break;
    }
  }
}


/* send extra 'padding' data */
static void
gst_avimux_send_pad_data (GstAviMux * avimux, gulong num_bytes)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = num_bytes;
  GST_BUFFER_DATA (buffer) = g_malloc (num_bytes);
  memset (GST_BUFFER_DATA (buffer), 0, num_bytes);

  gst_pad_push (avimux->srcpad, GST_DATA (buffer));
}

/* do audio buffer */
static void
gst_avimux_do_audio_buffer (GstAviMux * avimux)
{
  GstBuffer *data = avimux->audio_buffer_queue, *header;
  gulong total_size, pad_bytes = 0;

  /* write a audio header + index entry */
  if (GST_BUFFER_SIZE (data) & 1) {
    pad_bytes = 2 - (GST_BUFFER_SIZE (data) & 1);
  }
  header = gst_avimux_riff_get_audio_header (GST_BUFFER_SIZE (data));
  total_size = GST_BUFFER_SIZE (header) + GST_BUFFER_SIZE (data) + pad_bytes;

  if (avimux->is_bigfile) {
    avimux->datax_size += total_size;
  } else {
    avimux->data_size += total_size;
    avimux->audio_size += GST_BUFFER_SIZE (data);
    avimux->audio_time += GST_BUFFER_DURATION (data);
    gst_avimux_add_index (avimux, "01wb", 0x0, GST_BUFFER_SIZE (data));
  }

  gst_pad_push (avimux->srcpad, GST_DATA (header));
  gst_pad_push (avimux->srcpad, GST_DATA (data));
  if (pad_bytes) {
    gst_avimux_send_pad_data (avimux, pad_bytes);
  }
  avimux->total_data += total_size;
  avimux->idx_offset += total_size;

  avimux->audio_buffer_queue = NULL;
}


/* do video buffer */
static void
gst_avimux_do_video_buffer (GstAviMux * avimux)
{
  GstBuffer *data = avimux->video_buffer_queue, *header;
  gulong total_size, pad_bytes = 0;

  if (avimux->restart)
    gst_avimux_restart_file (avimux);

  /* write a video header + index entry */
  if ((avimux->is_bigfile ? avimux->datax_size : avimux->data_size) +
      GST_BUFFER_SIZE (data) > 1024 * 1024 * 2000) {
    if (avimux->enable_large_avi)
      gst_avimux_bigfile (avimux, FALSE);
    else
      gst_avimux_restart_file (avimux);
  }

  if (GST_BUFFER_SIZE (data) & 1) {
    pad_bytes = 2 - (GST_BUFFER_SIZE (data) & 1);
  }
  header = gst_avimux_riff_get_video_header (GST_BUFFER_SIZE (data));
  total_size = GST_BUFFER_SIZE (header) + GST_BUFFER_SIZE (data) + pad_bytes;
  avimux->total_frames++;

  if (avimux->is_bigfile) {
    avimux->datax_size += total_size;
    avimux->numx_frames++;
  } else {
    guint flags = 0x2;

    if (GST_BUFFER_FLAG_IS_SET (data, GST_BUFFER_KEY_UNIT))
      flags |= 0x10;
    avimux->data_size += total_size;
    avimux->num_frames++;
    gst_avimux_add_index (avimux, "00db", flags, GST_BUFFER_SIZE (data));
  }

  gst_pad_push (avimux->srcpad, GST_DATA (header));
  gst_pad_push (avimux->srcpad, GST_DATA (data));
  if (pad_bytes) {
    gst_avimux_send_pad_data (avimux, pad_bytes);
  }
  avimux->total_data += total_size;
  avimux->idx_offset += total_size;

  avimux->video_buffer_queue = NULL;
}


/* take the oldest buffer in our internal queue and push-it */
static gboolean
gst_avimux_do_one_buffer (GstAviMux * avimux)
{
  if (avimux->video_buffer_queue && avimux->audio_buffer_queue) {
    if (GST_BUFFER_TIMESTAMP (avimux->video_buffer_queue) <=
        GST_BUFFER_TIMESTAMP (avimux->audio_buffer_queue))
      gst_avimux_do_video_buffer (avimux);
    else
      gst_avimux_do_audio_buffer (avimux);
  } else if (avimux->video_buffer_queue || avimux->audio_buffer_queue) {
    if (avimux->video_buffer_queue)
      gst_avimux_do_video_buffer (avimux);
    else
      gst_avimux_do_audio_buffer (avimux);
  } else {
    /* simply finish off the file and send EOS */
    gst_avimux_stop_file (avimux);
    gst_pad_push (avimux->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (avimux));
    return FALSE;
  }

  return TRUE;
}


static void
gst_avimux_loop (GstElement * element)
{
  GstAviMux *avimux;

  avimux = GST_AVIMUX (element);

  /* first fill queue (some elements only set caps when
   * flowing data), then write header */
  gst_avimux_fill_queue (avimux);

  if (avimux->write_header)
    gst_avimux_start_file (avimux);

  gst_avimux_do_one_buffer (avimux);
}

static void
gst_avimux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAviMux *avimux;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AVIMUX (object));
  avimux = GST_AVIMUX (object);

  switch (prop_id) {
    case ARG_BIGFILE:
      g_value_set_boolean (value, avimux->enable_large_avi);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avimux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAviMux *avimux;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_AVIMUX (object));
  avimux = GST_AVIMUX (object);

  switch (prop_id) {
    case ARG_BIGFILE:
      avimux->enable_large_avi = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_avimux_change_state (GstElement * element)
{
  GstAviMux *avimux;
  gint transition = GST_STATE_TRANSITION (element);

  g_return_val_if_fail (GST_IS_AVIMUX (element), GST_STATE_FAILURE);

  avimux = GST_AVIMUX (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_PLAYING:
      avimux->video_pad_eos = avimux->audio_pad_eos = FALSE;
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
