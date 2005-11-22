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
  ARG_BIGFILE
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
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ]; "
        "image/jpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ]; "
        "video/x-divx, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ], "
        "divxversion = (int) [ 3, 5 ]; "
        "video/x-xvid, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ]; "
        "video/x-3ivx, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ]; "
        "video/x-msmpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ], "
        "msmpegversion = (int) [ 41, 43 ]; "
        "video/mpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ], "
        "mpegversion = (int) 1, "
        "systemstream = (boolean) FALSE; "
        "video/x-h263, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ]; "
        "video/x-h264, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (double) [ 0, MAX ]; "
        "video/x-dv, "
        "width = (int) 720, "
        "height = (int) { 576, 480 }, "
        "framerate = (double) [ 0, MAX ], "
        "systemstream = (boolean) FALSE; "
        "video/x-huffyuv, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (double) [ 0, MAX ]")
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
static void gst_avimux_release_pad (GstElement * element, GstPad * pad);
static void gst_avimux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_avimux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_avimux_change_state (GstElement * element,
    GstStateChange transition);

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
    static const GInterfaceInfo tag_setter_info = {
      NULL,
      NULL,
      NULL
    };

    avimux_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstAviMux", &avimux_info, 0);
    g_type_add_interface_static (avimux_type, GST_TYPE_TAG_SETTER,
        &tag_setter_info);
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
  gstelement_class->release_pad = gst_avimux_release_pad;

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

  GST_OBJECT_FLAG_SET (GST_ELEMENT (avimux), GST_ELEMENT_EVENT_AWARE);

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
  avimux->tags = NULL;

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
    } else if (!strcmp (mimetype, "image/jpeg")) {
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
  avimux->auds_hdr.scale = 1;
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
  } else if (pad == avimux->videosinkpad) {
    avimux->video_pad_connected = FALSE;
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

static void
gst_avimux_release_pad (GstElement * element, GstPad * pad)
{
  GstAviMux *avimux = GST_AVIMUX (element);

  if (pad == avimux->videosinkpad) {
    avimux->videosinkpad = NULL;
  } else if (pad == avimux->audiosinkpad) {
    avimux->audiosinkpad = NULL;
  } else {
    g_warning ("Unknown pad %s", gst_pad_get_name (pad));
    return;
  }

  GST_DEBUG ("Removed pad '%s'", gst_pad_get_name (pad));
  gst_element_remove_pad (element, pad);
}

/* maybe some of these functions should be moved to riff.h? */

/* DISCLAIMER: this function is ugly. So be it (i.e. it makes the rest easier) */

static void
gst_avimux_write_tag (const GstTagList * list, const gchar * tag, gpointer data)
{
  const struct
  {
    guint32 fcc;
    gchar *tag;
  } rifftags[] = {
    {
    GST_RIFF_INFO_ICMT, GST_TAG_COMMENT}, {
    GST_RIFF_INFO_INAM, GST_TAG_TITLE}, {
    GST_RIFF_INFO_ISFT, GST_TAG_ENCODER}, {
    GST_RIFF_INFO_IGNR, GST_TAG_GENRE}, {
    GST_RIFF_INFO_ICOP, GST_TAG_COPYRIGHT}, {
    GST_RIFF_INFO_IART, GST_TAG_ARTIST}, {
    GST_RIFF_INFO_IARL, GST_TAG_LOCATION}, {
    0, NULL}
  };
  gint n, len, plen;
  GstBuffer *buf = data;
  guint8 *buffdata = GST_BUFFER_DATA (buf) + GST_BUFFER_SIZE (buf);
  gchar *str;

  for (n = 0; rifftags[n].fcc != 0; n++) {
    if (!strcmp (rifftags[n].tag, tag) &&
        gst_tag_list_get_string (list, tag, &str)) {
      len = strlen (str);
      plen = len + 1;
      if (plen & 1)
        plen++;
      if (GST_BUFFER_MAXSIZE (buf) >= GST_BUFFER_SIZE (buf) + 8 + plen) {
        GST_WRITE_UINT32_LE (buffdata, rifftags[n].fcc);
        GST_WRITE_UINT32_LE (buffdata + 4, len + 1);
        memcpy (buffdata + 8, str, len);
        buffdata[8 + len] = 0;
        GST_BUFFER_SIZE (buf) += 8 + plen;
      }
      break;
    }
  }
}

static GstBuffer *
gst_avimux_riff_get_avi_header (GstAviMux * avimux)
{
  GstTagList *tags;
  const GstTagList *iface_tags;
  GstBuffer *buffer;
  guint8 *buffdata;
  guint size = 0;

  /* first, let's see what actually needs to be in the buffer */
  size += 32 + sizeof (gst_riff_avih);  /* avi header */
  if (avimux->video_pad_connected) {    /* we have video */
    size += 28 + sizeof (gst_riff_strh) + sizeof (gst_riff_strf_vids);  /* vid hdr */
    size += 24;                 /* odml header */
  }
  if (avimux->audio_pad_connected) {    /* we have audio */
    size += 28 + sizeof (gst_riff_strh) + sizeof (gst_riff_strf_auds);  /* aud hdr */
  }
  /* this is the "riff size" */
  avimux->header_size = size;
  size += 12;                   /* avi data header */

  /* tags */
  iface_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (avimux));
  if (iface_tags || avimux->tags) {
    size += 1024;
    if (iface_tags && avimux->tags) {
      tags = gst_tag_list_merge (iface_tags, avimux->tags,
          GST_TAG_MERGE_APPEND);
    } else if (iface_tags) {
      tags = gst_tag_list_copy (iface_tags);
    } else {
      tags = gst_tag_list_copy (avimux->tags);
    }
  } else {
    tags = NULL;
  }

  /* allocate the buffer */
  buffer = gst_buffer_new_and_alloc (size);
  buffdata = GST_BUFFER_DATA (buffer);
  GST_BUFFER_SIZE (buffer) = 0;

  /* avi header metadata */
  memcpy (buffdata + 0, "RIFF", 4);
  GST_WRITE_UINT32_LE (buffdata + 4,
      avimux->header_size + avimux->idx_size + avimux->data_size);
  memcpy (buffdata + 8, "AVI ", 4);
  memcpy (buffdata + 12, "LIST", 4);
  GST_WRITE_UINT32_LE (buffdata + 16, avimux->header_size - 4 * 5);
  memcpy (buffdata + 20, "hdrl", 4);
  memcpy (buffdata + 24, "avih", 4);
  GST_WRITE_UINT32_LE (buffdata + 28, sizeof (gst_riff_avih));
  buffdata += 32;
  GST_BUFFER_SIZE (buffer) += 32;

  /* the AVI header itself */
  GST_WRITE_UINT32_LE (buffdata + 0, avimux->avi_hdr.us_frame);
  GST_WRITE_UINT32_LE (buffdata + 4, avimux->avi_hdr.max_bps);
  GST_WRITE_UINT32_LE (buffdata + 8, avimux->avi_hdr.pad_gran);
  GST_WRITE_UINT32_LE (buffdata + 12, avimux->avi_hdr.flags);
  GST_WRITE_UINT32_LE (buffdata + 16, avimux->avi_hdr.tot_frames);
  GST_WRITE_UINT32_LE (buffdata + 20, avimux->avi_hdr.init_frames);
  GST_WRITE_UINT32_LE (buffdata + 24, avimux->avi_hdr.streams);
  GST_WRITE_UINT32_LE (buffdata + 28, avimux->avi_hdr.bufsize);
  GST_WRITE_UINT32_LE (buffdata + 32, avimux->avi_hdr.width);
  GST_WRITE_UINT32_LE (buffdata + 36, avimux->avi_hdr.height);
  GST_WRITE_UINT32_LE (buffdata + 40, avimux->avi_hdr.scale);
  GST_WRITE_UINT32_LE (buffdata + 44, avimux->avi_hdr.rate);
  GST_WRITE_UINT32_LE (buffdata + 48, avimux->avi_hdr.start);
  GST_WRITE_UINT32_LE (buffdata + 52, avimux->avi_hdr.length);
  buffdata += 56;
  GST_BUFFER_SIZE (buffer) += 56;

  if (avimux->video_pad_connected) {
    /* video header metadata */
    memcpy (buffdata + 0, "LIST", 4);
    GST_WRITE_UINT32_LE (buffdata + 4,
        sizeof (gst_riff_strh) + sizeof (gst_riff_strf_vids) + 4 * 5);
    memcpy (buffdata + 8, "strl", 4);
    /* generic header */
    memcpy (buffdata + 12, "strh", 4);
    GST_WRITE_UINT32_LE (buffdata + 16, sizeof (gst_riff_strh));
    /* the actual header */
    GST_WRITE_UINT32_LE (buffdata + 20, avimux->vids_hdr.type);
    GST_WRITE_UINT32_LE (buffdata + 24, avimux->vids_hdr.fcc_handler);
    GST_WRITE_UINT32_LE (buffdata + 28, avimux->vids_hdr.flags);
    GST_WRITE_UINT32_LE (buffdata + 32, avimux->vids_hdr.priority);
    GST_WRITE_UINT32_LE (buffdata + 36, avimux->vids_hdr.init_frames);
    GST_WRITE_UINT32_LE (buffdata + 40, avimux->vids_hdr.scale);
    GST_WRITE_UINT32_LE (buffdata + 44, avimux->vids_hdr.rate);
    GST_WRITE_UINT32_LE (buffdata + 48, avimux->vids_hdr.start);
    GST_WRITE_UINT32_LE (buffdata + 52, avimux->vids_hdr.length);
    GST_WRITE_UINT32_LE (buffdata + 56, avimux->vids_hdr.bufsize);
    GST_WRITE_UINT32_LE (buffdata + 60, avimux->vids_hdr.quality);
    GST_WRITE_UINT32_LE (buffdata + 64, avimux->vids_hdr.samplesize);
    /* the video header */
    memcpy (buffdata + 68, "strf", 4);
    GST_WRITE_UINT32_LE (buffdata + 72, sizeof (gst_riff_strf_vids));
    /* the actual header */
    GST_WRITE_UINT32_LE (buffdata + 76, avimux->vids.size);
    GST_WRITE_UINT32_LE (buffdata + 80, avimux->vids.width);
    GST_WRITE_UINT32_LE (buffdata + 84, avimux->vids.height);
    GST_WRITE_UINT16_LE (buffdata + 88, avimux->vids.planes);
    GST_WRITE_UINT16_LE (buffdata + 90, avimux->vids.bit_cnt);
    GST_WRITE_UINT32_LE (buffdata + 92, avimux->vids.compression);
    GST_WRITE_UINT32_LE (buffdata + 96, avimux->vids.image_size);
    GST_WRITE_UINT32_LE (buffdata + 100, avimux->vids.xpels_meter);
    GST_WRITE_UINT32_LE (buffdata + 104, avimux->vids.ypels_meter);
    GST_WRITE_UINT32_LE (buffdata + 108, avimux->vids.num_colors);
    GST_WRITE_UINT32_LE (buffdata + 112, avimux->vids.imp_colors);
    buffdata += 116;
    GST_BUFFER_SIZE (buffer) += 116;
  }

  if (avimux->audio_pad_connected) {
    /* audio header */
    memcpy (buffdata + 0, "LIST", 4);
    GST_WRITE_UINT32_LE (buffdata + 4,
        sizeof (gst_riff_strh) + sizeof (gst_riff_strf_auds) + 4 * 5);
    memcpy (buffdata + 8, "strl", 4);
    /* generic header */
    memcpy (buffdata + 12, "strh", 4);
    GST_WRITE_UINT32_LE (buffdata + 16, sizeof (gst_riff_strh));
    /* the actual header */
    GST_WRITE_UINT32_LE (buffdata + 20, avimux->auds_hdr.type);
    GST_WRITE_UINT32_LE (buffdata + 24, avimux->auds_hdr.fcc_handler);
    GST_WRITE_UINT32_LE (buffdata + 28, avimux->auds_hdr.flags);
    GST_WRITE_UINT32_LE (buffdata + 32, avimux->auds_hdr.priority);
    GST_WRITE_UINT32_LE (buffdata + 36, avimux->auds_hdr.init_frames);
    GST_WRITE_UINT32_LE (buffdata + 40, avimux->auds_hdr.scale);
    GST_WRITE_UINT32_LE (buffdata + 44, avimux->auds_hdr.rate);
    GST_WRITE_UINT32_LE (buffdata + 48, avimux->auds_hdr.start);
    GST_WRITE_UINT32_LE (buffdata + 52, avimux->auds_hdr.length);
    GST_WRITE_UINT32_LE (buffdata + 56, avimux->auds_hdr.bufsize);
    GST_WRITE_UINT32_LE (buffdata + 60, avimux->auds_hdr.quality);
    GST_WRITE_UINT32_LE (buffdata + 64, avimux->auds_hdr.samplesize);
    /* the audio header */
    memcpy (buffdata + 68, "strf", 4);
    GST_WRITE_UINT32_LE (buffdata + 72, sizeof (gst_riff_strf_auds));
    /* the actual header */
    GST_WRITE_UINT16_LE (buffdata + 76, avimux->auds.format);
    GST_WRITE_UINT16_LE (buffdata + 78, avimux->auds.channels);
    GST_WRITE_UINT32_LE (buffdata + 80, avimux->auds.rate);
    GST_WRITE_UINT32_LE (buffdata + 84, avimux->auds.av_bps);
    GST_WRITE_UINT16_LE (buffdata + 88, avimux->auds.blockalign);
    GST_WRITE_UINT16_LE (buffdata + 90, avimux->auds.size);
    buffdata += 92;
    GST_BUFFER_SIZE (buffer) += 92;
  }

  if (avimux->video_pad_connected) {
    /* odml header */
    memcpy (buffdata + 0, "LIST", 4);
    GST_WRITE_UINT32_LE (buffdata + 4, sizeof (guint32) + 4 * 3);
    memcpy (buffdata + 8, "odml", 4);
    memcpy (buffdata + 12, "dmlh", 4);
    GST_WRITE_UINT32_LE (buffdata + 16, sizeof (guint32));
    GST_WRITE_UINT32_LE (buffdata + 20, avimux->total_frames);
    buffdata += 24;
    GST_BUFFER_SIZE (buffer) += 24;
  }

  /* tags */
  if (tags) {
    guint8 *ptr;
    guint startsize;

    memcpy (buffdata + 0, "LIST", 4);
    ptr = buffdata + 4;         /* fill in later */
    startsize = GST_BUFFER_SIZE (buffer) + 4;
    memcpy (buffdata + 8, "INFO", 4);
    buffdata += 12;
    GST_BUFFER_SIZE (buffer) += 12;

    /* 12 bytes is needed for data header */
    GST_BUFFER_MAXSIZE (buffer) -= 12;
    gst_tag_list_foreach (tags, gst_avimux_write_tag, buffer);
    gst_tag_list_free (tags);
    GST_BUFFER_MAXSIZE (buffer) += 12;
    buffdata = GST_BUFFER_DATA (buffer) + GST_BUFFER_SIZE (buffer);

    /* update list size */
    GST_WRITE_UINT32_LE (ptr, GST_BUFFER_SIZE (buffer) - startsize - 4);
  }

  /* avi data header */
  memcpy (buffdata + 0, "LIST", 4);
  GST_WRITE_UINT32_LE (buffdata + 4, avimux->data_size);
  memcpy (buffdata + 8, "movi", 4);
  buffdata += 12;
  GST_BUFFER_SIZE (buffer) += 12;

  return buffer;
}

static GstBuffer *
gst_avimux_riff_get_avix_header (guint32 datax_size)
{
  GstBuffer *buffer;
  guint8 *buffdata;

  buffer = gst_buffer_new_and_alloc (24);
  buffdata = GST_BUFFER_DATA (buffer);

  memcpy (buffdata + 0, "LIST", 4);
  GST_WRITE_UINT32_LE (buffdata + 4, datax_size + 4 * 4);
  memcpy (buffdata + 8, "AVIX", 4);
  memcpy (buffdata + 12, "LIST", 4);
  GST_WRITE_UINT32_LE (buffdata + 16, datax_size);
  memcpy (buffdata + 20, "movi", 4);

  return buffer;
}

static GstBuffer *
gst_avimux_riff_get_video_header (guint32 video_frame_size)
{
  GstBuffer *buffer;
  guint8 *buffdata;

  buffer = gst_buffer_new_and_alloc (8);
  buffdata = GST_BUFFER_DATA (buffer);
  memcpy (buffdata + 0, "00db", 4);
  GST_WRITE_UINT32_LE (buffdata + 4, video_frame_size);

  return buffer;
}

static GstBuffer *
gst_avimux_riff_get_audio_header (guint32 audio_sample_size)
{
  GstBuffer *buffer;
  guint8 *buffdata;

  buffer = gst_buffer_new_and_alloc (8);
  buffdata = GST_BUFFER_DATA (buffer);
  memcpy (buffdata + 0, "01wb", 4);
  GST_WRITE_UINT32_LE (buffdata + 4, audio_sample_size);

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
  guint8 *buffdata;

  buffer = gst_buffer_new_and_alloc (8);
  buffdata = GST_BUFFER_DATA (buffer);
  memcpy (buffdata + 0, "idx1", 4);
  GST_WRITE_UINT32_LE (buffdata + 4,
      avimux->idx_index * sizeof (gst_riff_index_entry));
  gst_pad_push (avimux->srcpad, GST_DATA (buffer));

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = avimux->idx_index * sizeof (gst_riff_index_entry);
  GST_BUFFER_DATA (buffer) = (guint8 *) avimux->idx;
  avimux->idx = NULL;           /* will be free()'ed by gst_buffer_unref() */
  avimux->total_data += GST_BUFFER_SIZE (buffer) + 8;
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
  gst_pad_push (avimux->srcpad, GST_DATA (header));

  avimux->idx_offset = avimux->total_data;

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

  /* set rate and everything having to do with that */
  avimux->avi_hdr.max_bps = 0;
  if (avimux->audio_pad_connected) {
    /* calculate bps if needed */
    if (!avimux->auds.av_bps) {
      if (avimux->audio_time) {
        avimux->auds.av_bps =
            (GST_SECOND * avimux->audio_size) / avimux->audio_time;
      } else {
        GST_ELEMENT_ERROR (avimux, STREAM, MUX,
            (_("No or invalid input audio, AVI stream will be corrupt.")),
            (NULL));
        avimux->auds.av_bps = 0;
      }
      avimux->auds_hdr.rate = avimux->auds.av_bps * avimux->auds_hdr.scale;
    }
    avimux->avi_hdr.max_bps += avimux->auds.av_bps;
  }
  if (avimux->video_pad_connected) {
    avimux->avi_hdr.max_bps += ((avimux->vids.bit_cnt + 7) / 8) *
        (1000000. / avimux->avi_hdr.us_frame) * avimux->vids.image_size;
  }

  /* statistics/total_frames/... */
  avimux->avi_hdr.tot_frames = avimux->num_frames;
  if (avimux->video_pad_connected) {
    avimux->vids_hdr.length = avimux->num_frames;
  }
  if (avimux->audio_pad_connected) {
    avimux->auds_hdr.length =
        (avimux->audio_time * avimux->auds_hdr.rate) / GST_SECOND;
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
    case GST_EVENT_TAG:
      if (avimux->tags) {
        gst_tag_list_insert (avimux->tags, gst_event_tag_get_list (event),
            GST_TAG_MERGE_PREPEND);
      } else {
        avimux->tags = gst_tag_list_copy (gst_event_tag_get_list (event));
      }
      break;
    default:
      break;
  }

  gst_event_unref (event);

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

static GstStateChangeReturn
gst_avimux_change_state (GstElement * element, GstStateChange transition)
{
  GstAviMux *avimux;

  g_return_val_if_fail (GST_IS_AVIMUX (element), GST_STATE_CHANGE_FAILURE);

  avimux = GST_AVIMUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      avimux->video_pad_eos = avimux->audio_pad_eos = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (avimux->tags) {
        gst_tag_list_free (avimux->tags);
        avimux->tags = NULL;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return GST_STATE_CHANGE_SUCCESS;
}
