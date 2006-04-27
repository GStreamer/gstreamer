/* AVI muxer plugin for GStreamer
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *           (C) 2006 Mark Nauwelaerts <manauw@skynet.be>
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

GST_DEBUG_CATEGORY_STATIC (avimux_debug);
#define GST_CAT_DEFAULT avimux_debug

enum
{
  ARG_0,
  ARG_BIGFILE
};

#define DEFAULT_BIGFILE TRUE

static const GstElementDetails gst_avi_mux_details =
GST_ELEMENT_DETAILS ("Avi muxer",
    "Codec/Muxer",
    "Muxes audio and video into an avi stream",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>");

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
        "framerate = (fraction) [ 0, MAX ]; "
        "image/jpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ]; "
        "video/x-divx, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ], "
        "divxversion = (int) [ 3, 5 ]; "
        "video/x-xvid, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ]; "
        "video/x-3ivx, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ]; "
        "video/x-msmpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ], "
        "msmpegversion = (int) [ 41, 43 ]; "
        "video/mpeg, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ], "
        "mpegversion = (int) 1, "
        "systemstream = (boolean) FALSE; "
        "video/x-h263, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ]; "
        "video/x-h264, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0, MAX ]; "
        "video/x-dv, "
        "width = (int) 720, "
        "height = (int) { 576, 480 }, "
        "framerate = (fraction) [ 0, MAX ], "
        "systemstream = (boolean) FALSE; "
        "video/x-huffyuv, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) [ 0, MAX ]")
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


static void gst_avi_mux_base_init (gpointer g_class);
static void gst_avi_mux_class_init (GstAviMuxClass * klass);
static void gst_avi_mux_init (GstAviMux * avimux);

static GstFlowReturn gst_avi_mux_collect_pads (GstCollectPads * pads,
    GstAviMux * avimux);
static gboolean gst_avi_mux_handle_event (GstPad * pad, GstEvent * event);
static GstPad *gst_avi_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name);
static void gst_avi_mux_release_pad (GstElement * element, GstPad * pad);
static void gst_avi_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_avi_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_avi_mux_change_state (GstElement * element,
    GstStateChange transition);

static GstElementClass *parent_class = NULL;

GType
gst_avi_mux_get_type (void)
{
  static GType avimux_type = 0;

  if (!avimux_type) {
    static const GTypeInfo avimux_info = {
      sizeof (GstAviMuxClass),
      gst_avi_mux_base_init,
      NULL,
      (GClassInitFunc) gst_avi_mux_class_init,
      NULL,
      NULL,
      sizeof (GstAviMux),
      0,
      (GInstanceInitFunc) gst_avi_mux_init,
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
gst_avi_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_sink_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_sink_factory));

  gst_element_class_set_details (element_class, &gst_avi_mux_details);

  GST_DEBUG_CATEGORY_INIT (avimux_debug, "avimux", 0, "Muxer for AVI streams");
}

static void
gst_avi_mux_finalize (GObject * object)
{
  GstAviMux *mux = GST_AVI_MUX (object);

  g_free (mux->idx);
  mux->idx = NULL;

  gst_object_unref (mux->collect);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_avi_mux_class_init (GstAviMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->get_property = gst_avi_mux_get_property;
  gobject_class->set_property = gst_avi_mux_set_property;
  gobject_class->finalize = gst_avi_mux_finalize;

  g_object_class_install_property (gobject_class, ARG_BIGFILE,
      g_param_spec_boolean ("bigfile", "Bigfile Support (>2GB)",
          "Support for openDML-2.0 (big) AVI files", DEFAULT_BIGFILE,
          G_PARAM_READWRITE));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_avi_mux_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_avi_mux_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_avi_mux_change_state);
}

static void
gst_avi_mux_init (GstAviMux * avimux)
{
  avimux->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_use_fixed_caps (avimux->srcpad);
  gst_element_add_pad (GST_ELEMENT (avimux), avimux->srcpad);

  avimux->audiocollectdata = NULL;
  avimux->audio_pad_connected = FALSE;
  avimux->videocollectdata = NULL;
  avimux->video_pad_connected = FALSE;

  avimux->num_frames = 0;

  /* audio/video/AVI header initialisation */
  memset (&(avimux->avi_hdr), 0, sizeof (gst_riff_avih));
  memset (&(avimux->vids_hdr), 0, sizeof (gst_riff_strh));
  memset (&(avimux->vids), 0, sizeof (gst_riff_strf_vids));
  memset (&(avimux->auds_hdr), 0, sizeof (gst_riff_strh));
  memset (&(avimux->auds), 0, sizeof (gst_riff_strf_auds));
  avimux->vids_hdr.type = GST_MAKE_FOURCC ('v', 'i', 'd', 's');
  avimux->vids_hdr.rate = 1;
  avimux->avi_hdr.max_bps = 10000000;
  avimux->auds_hdr.type = GST_MAKE_FOURCC ('a', 'u', 'd', 's');
  avimux->vids_hdr.quality = 0xFFFFFFFF;
  avimux->auds_hdr.quality = 0xFFFFFFFF;
  avimux->tags = NULL;
  avimux->tags_snap = NULL;

  avimux->idx = NULL;

  avimux->write_header = TRUE;

  avimux->enable_large_avi = DEFAULT_BIGFILE;

  avimux->collect = gst_collect_pads_new ();

  gst_collect_pads_set_function (avimux->collect,
      (GstCollectPadsFunction) (GST_DEBUG_FUNCPTR (gst_avi_mux_collect_pads)),
      avimux);
}

static gboolean
gst_avi_mux_vidsink_set_caps (GstPad * pad, GstCaps * vscaps)
{
  GstAviMux *avimux;
  GstStructure *structure;
  const gchar *mimetype;
  const GValue *fps;
  gint width, height;

  avimux = GST_AVI_MUX (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (avimux, "%s:%s, caps=%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), vscaps);

  structure = gst_caps_get_structure (vscaps, 0);
  mimetype = gst_structure_get_name (structure);

  /* global */
  avimux->vids.size = sizeof (gst_riff_strf_vids);
  avimux->vids.planes = 1;
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height)) {
    goto refuse_caps;
  }

  avimux->vids.width = width;
  avimux->vids.height = height;

  fps = gst_structure_get_value (structure, "framerate");
  if (fps == NULL || !GST_VALUE_HOLDS_FRACTION (fps))
    goto refuse_caps;

  avimux->vids_hdr.rate = gst_value_get_fraction_numerator (fps);
  avimux->vids_hdr.scale = gst_value_get_fraction_denominator (fps);

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
    } else if (gst_structure_has_name (structure, "video/x-msmpeg")) {
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

    if (!avimux->vids.compression)
      goto refuse_caps;
  }

  avimux->vids_hdr.fcc_handler = avimux->vids.compression;
  avimux->vids.image_size = avimux->vids.height * avimux->vids.width;
  avimux->avi_hdr.width = avimux->vids.width;
  avimux->avi_hdr.height = avimux->vids.height;
  avimux->avi_hdr.us_frame = avimux->vids_hdr.scale;

  gst_object_unref (avimux);
  return TRUE;

refuse_caps:
  {
    GST_WARNING_OBJECT (avimux, "refused caps %" GST_PTR_FORMAT, vscaps);
    gst_object_unref (avimux);
    return FALSE;
  }
}

static gboolean
gst_avi_mux_audsink_set_caps (GstPad * pad, GstCaps * vscaps)
{
  GstAviMux *avimux;
  GstStructure *structure;
  const gchar *mimetype;
  gint channels, rate;

  avimux = GST_AVI_MUX (GST_PAD_PARENT (pad));

  GST_DEBUG_OBJECT (avimux, "%s:%s, caps=%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), vscaps);

  structure = gst_caps_get_structure (vscaps, 0);
  mimetype = gst_structure_get_name (structure);

  /* we want these for all */
  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate)) {
    goto refuse_caps;
  }

  avimux->auds.channels = channels;
  avimux->auds.rate = rate;

  if (!strcmp (mimetype, "audio/x-raw-int")) {
    gint width, depth;

    avimux->auds.format = GST_RIFF_WAVE_FORMAT_PCM;

    if (!gst_structure_get_int (structure, "width", &width) ||
        (width != 8 && !gst_structure_get_int (structure, "depth", &depth))) {
      goto refuse_caps;
    }

    avimux->auds.blockalign = width;
    avimux->auds.size = (width == 8) ? 8 : depth;

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

    if (!avimux->auds.format)
      goto refuse_caps;
  }

  avimux->auds_hdr.rate = avimux->auds.blockalign * avimux->auds.rate;
  avimux->auds_hdr.samplesize = avimux->auds.blockalign;
  avimux->auds_hdr.scale = 1;
  return TRUE;


refuse_caps:
  {
    GST_WARNING_OBJECT (avimux, "refused caps %" GST_PTR_FORMAT, vscaps);
    gst_object_unref (avimux);
    return FALSE;
  }
}

static void
gst_avi_mux_pad_link (GstPad * pad, GstPad * peer, gpointer data)
{
  GstAviMux *avimux = GST_AVI_MUX (data);

  if (avimux->audiocollectdata && pad == avimux->audiocollectdata->pad) {
    avimux->audio_pad_connected = TRUE;
  } else if (avimux->videocollectdata && pad == avimux->videocollectdata->pad) {
    avimux->video_pad_connected = TRUE;
  } else {
    g_assert_not_reached ();
  }

  GST_DEBUG_OBJECT (avimux, "pad '%s' connected", GST_PAD_NAME (pad));
}

static void
gst_avi_mux_pad_unlink (GstPad * pad, GstPad * peer, gpointer data)
{
  GstAviMux *avimux = GST_AVI_MUX (data);

  if (avimux->audiocollectdata && pad == avimux->audiocollectdata->pad) {
    avimux->audio_pad_connected = FALSE;
    avimux->audiocollectdata = NULL;
  } else if (avimux->videocollectdata && pad == avimux->videocollectdata->pad) {
    avimux->video_pad_connected = FALSE;
    avimux->videocollectdata = NULL;
  } else {
    g_assert_not_reached ();
  }

  gst_collect_pads_remove_pad (avimux->collect, pad);

  GST_DEBUG_OBJECT (avimux, "pad '%s' unlinked and removed from collect",
      GST_PAD_NAME (pad));
}

/* TODO GstCollectPads will block if it has to manage a non-linked pad;
 * best to upgrade it so it helps all muxers using it */
static GstPad *
gst_avi_mux_request_new_pad (GstElement * element,
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

  g_return_val_if_fail (GST_IS_AVI_MUX (element), NULL);

  avimux = GST_AVI_MUX (element);

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    if (avimux->audiocollectdata)
      return NULL;
    newpad = gst_pad_new_from_template (templ, "audio_00");
    gst_pad_set_setcaps_function (newpad,
        GST_DEBUG_FUNCPTR (gst_avi_mux_audsink_set_caps));
    avimux->audiocollectdata = gst_collect_pads_add_pad (avimux->collect,
        newpad, sizeof (GstCollectData));
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    if (avimux->videocollectdata)
      return NULL;
    newpad = gst_pad_new_from_template (templ, "video_00");
    gst_pad_set_setcaps_function (newpad,
        GST_DEBUG_FUNCPTR (gst_avi_mux_vidsink_set_caps));
    avimux->videocollectdata = gst_collect_pads_add_pad (avimux->collect,
        newpad, sizeof (GstCollectData));
  } else {
    g_warning ("avimux: this is not our template!\n");
    return NULL;
  }

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events */
  avimux->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (newpad);
  gst_pad_set_event_function (newpad,
      GST_DEBUG_FUNCPTR (gst_avi_mux_handle_event));

  g_signal_connect (newpad, "linked",
      G_CALLBACK (gst_avi_mux_pad_link), avimux);
  g_signal_connect (newpad, "unlinked",
      G_CALLBACK (gst_avi_mux_pad_unlink), avimux);

  gst_element_add_pad (element, newpad);

  return newpad;
}

static void
gst_avi_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstAviMux *avimux = GST_AVI_MUX (element);

  if (avimux->videocollectdata && pad == avimux->videocollectdata->pad) {
    avimux->videocollectdata = NULL;
  } else if (avimux->audiocollectdata && pad == avimux->audiocollectdata->pad) {
    avimux->audiocollectdata = NULL;
  } else {
    g_warning ("Unknown pad %s", GST_PAD_NAME (pad));
    return;
  }

  GST_DEBUG_OBJECT (avimux, "removed pad '%s'", GST_PAD_NAME (pad));
  gst_collect_pads_remove_pad (avimux->collect, pad);
  gst_element_remove_pad (element, pad);
}

/* maybe some of these functions should be moved to riff.h? */

/* DISCLAIMER: this function is ugly. So be it (i.e. it makes the rest easier) */
/* so is this struct */

typedef struct _GstMarkedBuffer
{
  guint *highmark;
  GstBuffer *buffer;
} GstMarkedBuffer;

static void
gst_avi_mux_write_tag (const GstTagList * list, const gchar * tag,
    gpointer data)
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
  GstBuffer *buf = ((GstMarkedBuffer *) data)->buffer;
  guint *highmark = ((GstMarkedBuffer *) data)->highmark;
  guint8 *buffdata = GST_BUFFER_DATA (buf) + *highmark;
  gchar *str;

  for (n = 0; rifftags[n].fcc != 0; n++) {
    if (!strcmp (rifftags[n].tag, tag) &&
        gst_tag_list_get_string (list, tag, &str)) {
      len = strlen (str);
      plen = len + 1;
      if (plen & 1)
        plen++;
      if (GST_BUFFER_SIZE (buf) >= *highmark + 8 + plen) {
        GST_WRITE_UINT32_LE (buffdata, rifftags[n].fcc);
        GST_WRITE_UINT32_LE (buffdata + 4, len + 1);
        memcpy (buffdata + 8, str, len);
        buffdata[8 + len] = 0;
        *highmark += 8 + plen;
        GST_DEBUG ("writing tag in buffer %p, highmark at %d", buf, *highmark);
      }
      break;
    }
  }
}


static GstBuffer *
gst_avi_mux_riff_get_avi_header (GstAviMux * avimux)
{
  GstTagList *tags;
  const GstTagList *iface_tags;
  GstBuffer *buffer;
  guint8 *buffdata;
  guint size = 0;
  guint highmark = 0;

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

  GST_DEBUG ("creating avi header, header_size %u, data_size %u, idx_size %u",
      avimux->header_size, avimux->data_size, avimux->idx_size);

  /* tags */
  iface_tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (avimux));
  if ((iface_tags || avimux->tags) && !avimux->tags_snap) {
    if (iface_tags && avimux->tags) {
      tags = gst_tag_list_merge (iface_tags, avimux->tags,
          GST_TAG_MERGE_APPEND);
    } else if (iface_tags) {
      tags = gst_tag_list_copy (iface_tags);
    } else {
      tags = gst_tag_list_copy (avimux->tags);
    }
  } else {
    tags = avimux->tags_snap;
  }
  avimux->tags_snap = tags;
  if (avimux->tags_snap)
    size += 1024;

  /* allocate the buffer */
  buffer = gst_buffer_new_and_alloc (size);
  buffdata = GST_BUFFER_DATA (buffer);
  highmark = 0;
  GST_DEBUG ("creating buffer %p, size %d, highmark at 0",
      buffer, GST_BUFFER_SIZE (buffer));

  /* avi header metadata */
  memcpy (buffdata + 0, "RIFF", 4);
  GST_WRITE_UINT32_LE (buffdata + 4,
      avimux->header_size + avimux->idx_size + avimux->data_size +
      avimux->tag_size);
  memcpy (buffdata + 8, "AVI ", 4);
  memcpy (buffdata + 12, "LIST", 4);
  GST_WRITE_UINT32_LE (buffdata + 16, avimux->header_size - 4 * 5);
  memcpy (buffdata + 20, "hdrl", 4);
  memcpy (buffdata + 24, "avih", 4);
  GST_WRITE_UINT32_LE (buffdata + 28, sizeof (gst_riff_avih));
  buffdata += 32;
  highmark += 32;

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
  highmark += 56;

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
    highmark += 116;
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
    highmark += 92;
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
    highmark += 24;
  }

  /* tags */
  if (tags) {
    guint8 *ptr;
    guint startsize;
    GstMarkedBuffer data = { &highmark, buffer };

    memcpy (buffdata + 0, "LIST", 4);
    ptr = buffdata + 4;         /* fill in later */
    startsize = highmark + 4;
    memcpy (buffdata + 8, "INFO", 4);
    buffdata += 12;
    highmark += 12;

    /* 12 bytes is needed for data header */
    GST_BUFFER_SIZE (buffer) -= 12;
    gst_tag_list_foreach (tags, gst_avi_mux_write_tag, &data);
    /* do not free tags here, as it refers to the tag snapshot */
    GST_BUFFER_SIZE (buffer) += 12;
    buffdata = GST_BUFFER_DATA (buffer) + highmark;

    /* update list size */
    GST_WRITE_UINT32_LE (ptr, highmark - startsize - 4);
    avimux->tag_size = highmark - startsize + 4;
  }

  /* avi data header */
  memcpy (buffdata + 0, "LIST", 4);
  GST_WRITE_UINT32_LE (buffdata + 4, avimux->data_size);
  memcpy (buffdata + 8, "movi", 4);
  buffdata += 12;
  highmark += 12;

  {                             /* only the part that is filled in actually makes up the header
                                 *  unref the parent as we only need this part from now on */
    GstBuffer *subbuffer = gst_buffer_create_sub (buffer, 0, highmark);

    gst_buffer_unref (buffer);
    return subbuffer;
  }
}

static GstBuffer *
gst_avi_mux_riff_get_avix_header (guint32 datax_size)
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
gst_avi_mux_riff_get_video_header (guint32 video_frame_size)
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
gst_avi_mux_riff_get_audio_header (guint32 audio_sample_size)
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
gst_avi_mux_add_index (GstAviMux * avimux, guchar * code, guint32 flags,
    guint32 size)
{
  if (avimux->idx_index == avimux->idx_count) {
    avimux->idx_count += 256;
    avimux->idx =
        g_realloc (avimux->idx,
        avimux->idx_count * sizeof (gst_riff_index_entry));
  }
  memcpy (&(avimux->idx[avimux->idx_index].id), code, 4);
  avimux->idx[avimux->idx_index].flags = GUINT32_FROM_LE (flags);
  avimux->idx[avimux->idx_index].offset = GUINT32_FROM_LE (avimux->idx_offset);
  avimux->idx[avimux->idx_index].size = GUINT32_FROM_LE (size);
  avimux->idx_index++;
}

static GstFlowReturn
gst_avi_mux_write_index (GstAviMux * avimux)
{
  GstFlowReturn res;
  GstBuffer *buffer;
  guint8 *buffdata;

  buffer = gst_buffer_new_and_alloc (8);
  buffdata = GST_BUFFER_DATA (buffer);
  memcpy (buffdata + 0, "idx1", 4);
  GST_WRITE_UINT32_LE (buffdata + 4,
      avimux->idx_index * sizeof (gst_riff_index_entry));

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (avimux->srcpad));
  res = gst_pad_push (avimux->srcpad, buffer);
  if (res != GST_FLOW_OK)
    return res;

  buffer = gst_buffer_new ();
  GST_BUFFER_SIZE (buffer) = avimux->idx_index * sizeof (gst_riff_index_entry);
  GST_BUFFER_DATA (buffer) = (guint8 *) avimux->idx;
  GST_BUFFER_MALLOCDATA (buffer) = GST_BUFFER_DATA (buffer);
  avimux->idx = NULL;           /* will be free()'ed by gst_buffer_unref() */
  avimux->total_data += GST_BUFFER_SIZE (buffer) + 8;

  gst_buffer_set_caps (buffer, GST_PAD_CAPS (avimux->srcpad));
  res = gst_pad_push (avimux->srcpad, buffer);
  if (res != GST_FLOW_OK)
    return res;

  avimux->idx_size += avimux->idx_index * sizeof (gst_riff_index_entry) + 8;

  /* update header */
  avimux->avi_hdr.flags |= GST_RIFF_AVIH_HASINDEX;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_avi_mux_bigfile (GstAviMux * avimux, gboolean last)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstBuffer *header;
  GstEvent *event;

  if (avimux->is_bigfile) {
    /* search back */
    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
        avimux->avix_start, GST_CLOCK_TIME_NONE, avimux->avix_start);
    /* if the event succeeds */
    gst_pad_push_event (avimux->srcpad, event);

    /* rewrite AVIX header */
    header = gst_avi_mux_riff_get_avix_header (avimux->datax_size);
    gst_buffer_set_caps (header, GST_PAD_CAPS (avimux->srcpad));
    if ((res = gst_pad_push (avimux->srcpad, header)) != GST_FLOW_OK)
      return res;

    /* go back to current location */
    event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
        avimux->total_data, GST_CLOCK_TIME_NONE, avimux->total_data);
    gst_pad_push_event (avimux->srcpad, event);
  }
  avimux->avix_start = avimux->total_data;

  if (last)
    return res;

  avimux->is_bigfile = TRUE;
  avimux->numx_frames = 0;
  avimux->datax_size = 0;

  header = gst_avi_mux_riff_get_avix_header (0);
  avimux->total_data += GST_BUFFER_SIZE (header);
  gst_buffer_set_caps (header, GST_PAD_CAPS (avimux->srcpad));
  return gst_pad_push (avimux->srcpad, header);
}

/* enough header blabla now, let's go on to actually writing the headers */

static GstFlowReturn
gst_avi_mux_start_file (GstAviMux * avimux)
{
  GstFlowReturn res;
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

  avimux->tag_size = 0;

  /* header */
  avimux->avi_hdr.streams =
      (avimux->video_pad_connected ? 1 : 0) +
      (avimux->audio_pad_connected ? 1 : 0);
  avimux->is_bigfile = FALSE;

  header = gst_avi_mux_riff_get_avi_header (avimux);
  avimux->total_data += GST_BUFFER_SIZE (header);

  gst_buffer_set_caps (header, GST_PAD_CAPS (avimux->srcpad));
  res = gst_pad_push (avimux->srcpad, header);

  avimux->idx_offset = avimux->total_data;

  avimux->write_header = FALSE;
  avimux->restart = FALSE;

  return res;
}

static GstFlowReturn
gst_avi_mux_stop_file (GstAviMux * avimux)
{
  GstFlowReturn res = GST_FLOW_OK;
  GstEvent *event;
  GstBuffer *header;

  /* if bigfile, rewrite header, else write indexes */
  /* don't bail out at once if error, still try to re-write header */
  if (avimux->video_pad_connected) {
    if (avimux->is_bigfile) {
      res = gst_avi_mux_bigfile (avimux, TRUE);
      avimux->idx_size = 0;
    } else {
      res = gst_avi_mux_write_index (avimux);
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
        GST_ELEMENT_WARNING (avimux, STREAM, MUX,
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
  header = gst_avi_mux_riff_get_avi_header (avimux);
  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
      0, GST_CLOCK_TIME_NONE, 0);
  gst_pad_push_event (avimux->srcpad, event);

  gst_buffer_set_caps (header, GST_PAD_CAPS (avimux->srcpad));
  /* the first error survives */
  if (res == GST_FLOW_OK)
    res = gst_pad_push (avimux->srcpad, header);
  else
    gst_pad_push (avimux->srcpad, header);

  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
      avimux->total_data, GST_CLOCK_TIME_NONE, avimux->total_data);
  gst_pad_push_event (avimux->srcpad, event);

  avimux->write_header = TRUE;

  return res;
}

static GstFlowReturn
gst_avi_mux_restart_file (GstAviMux * avimux)
{
  GstFlowReturn res;

  if ((res = gst_avi_mux_stop_file (avimux)) != GST_FLOW_OK)
    return res;

  gst_pad_push_event (avimux->srcpad, gst_event_new_eos ());

  return gst_avi_mux_start_file (avimux);
}

/* handle events (search) */
static gboolean
gst_avi_mux_handle_event (GstPad * pad, GstEvent * event)
{
  GstAviMux *avimux;
  GstTagList *list;
  gboolean ret;

  avimux = GST_AVI_MUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
#if 0
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
#endif
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &list);
      if (avimux->tags) {
        gst_tag_list_insert (avimux->tags, list, GST_TAG_MERGE_PREPEND);
      } else {
        avimux->tags = gst_tag_list_copy (list);
      }
      break;
    default:
      break;
  }


  /* now GstCollectPads can take care of the rest, e.g. EOS */
  ret = avimux->collect_event (pad, event);

  gst_object_unref (avimux);

  return ret;
}


#if 0
/* fill the internal queue for each available pad */
static void
gst_avi_mux_fill_queue (GstAviMux * avimux)
{
  GstBuffer *buffer;

  while (!avimux->audio_buffer_queue &&
      avimux->audiosinkpad &&
      avimux->audio_pad_connected &&
      GST_PAD_IS_USABLE (avimux->audiosinkpad) && !avimux->audio_pad_eos) {
    buffer = GST_BUFFER (gst_pad_pull (avimux->audiosinkpad));
    if (GST_IS_EVENT (buffer)) {
      gst_avi_mux_handle_event (avimux->audiosinkpad, GST_EVENT (buffer));
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
      gst_avi_mux_handle_event (avimux->videosinkpad, GST_EVENT (buffer));
    } else {
      avimux->video_buffer_queue = buffer;
      break;
    }
  }
}
#endif

/* send extra 'padding' data */
static GstFlowReturn
gst_avi_mux_send_pad_data (GstAviMux * avimux, gulong num_bytes)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new_and_alloc (num_bytes);
  memset (GST_BUFFER_DATA (buffer), 0, num_bytes);
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (avimux->srcpad));
  return gst_pad_push (avimux->srcpad, buffer);
}

/* strip buffer of time/caps meaning, is now only raw data;
 * bit of a work-around for the following ...*/
/* TODO on basesink:
 * - perhaps use a default format like basesrc (to be chosen by derived element)
 *   and only act on segment, etc that are of such type
 * - in any case, basesink could be more careful in deciding when
 *   to drop buffers, currently it decides on GST_FORMAT_TIME base
 *   even when its clip_segment is GST_FORMAT_BYTE based,
 *   and gst_segment_clip feels this reason enough to drop it
 *   (reasonable doubt is not enough to let pass :-) )
 */
static GstBuffer *
gst_avi_mux_strip_buffer (GstAviMux * avimux, GstBuffer * buffer)
{
  buffer = gst_buffer_make_metadata_writable (buffer);
  GST_BUFFER_TIMESTAMP (buffer) = GST_CLOCK_TIME_NONE;
  gst_buffer_set_caps (buffer, GST_PAD_CAPS (avimux->srcpad));
  return buffer;
}

/* do audio buffer */
static GstFlowReturn
gst_avi_mux_do_audio_buffer (GstAviMux * avimux)
{
  GstFlowReturn res;
  GstBuffer *data, *header;
  gulong total_size, pad_bytes = 0;

  data = gst_collect_pads_pop (avimux->collect, avimux->audiocollectdata);
  data = gst_avi_mux_strip_buffer (avimux, data);

  /* write a audio header + index entry */
  if (GST_BUFFER_SIZE (data) & 1) {
    pad_bytes = 2 - (GST_BUFFER_SIZE (data) & 1);
  }
  header = gst_avi_mux_riff_get_audio_header (GST_BUFFER_SIZE (data));
  total_size = GST_BUFFER_SIZE (header) + GST_BUFFER_SIZE (data) + pad_bytes;

  if (avimux->is_bigfile) {
    avimux->datax_size += total_size;
  } else {
    avimux->data_size += total_size;
    avimux->audio_size += GST_BUFFER_SIZE (data);
    avimux->audio_time += GST_BUFFER_DURATION (data);
    gst_avi_mux_add_index (avimux, (guchar *) "01wb", 0x0,
        GST_BUFFER_SIZE (data));
  }

  gst_buffer_set_caps (header, GST_PAD_CAPS (avimux->srcpad));
  if ((res = gst_pad_push (avimux->srcpad, header)) != GST_FLOW_OK)
    return res;
  if ((res = gst_pad_push (avimux->srcpad, data)) != GST_FLOW_OK)
    return res;

  if (pad_bytes) {
    if ((res = gst_avi_mux_send_pad_data (avimux, pad_bytes)) != GST_FLOW_OK)
      return res;
  }

  /* if any push above fails, we're in trouble with file consistency anyway */
  avimux->total_data += total_size;
  avimux->idx_offset += total_size;

  return res;
}


/* do video buffer */
static GstFlowReturn
gst_avi_mux_do_video_buffer (GstAviMux * avimux)
{
  GstFlowReturn res;
  GstBuffer *data, *header;
  gulong total_size, pad_bytes = 0;

  data = gst_collect_pads_pop (avimux->collect, avimux->videocollectdata);
  data = gst_avi_mux_strip_buffer (avimux, data);

  if (avimux->restart) {
    if ((res = gst_avi_mux_restart_file (avimux)) != GST_FLOW_OK)
      return res;
  }

  /* write a video header + index entry */
  if ((avimux->is_bigfile ? avimux->datax_size : avimux->data_size) +
      GST_BUFFER_SIZE (data) > 1024 * 1024 * 2000) {
    if (avimux->enable_large_avi) {
      if ((res = gst_avi_mux_bigfile (avimux, FALSE)) != GST_FLOW_OK)
        return res;
    } else {
      if ((res = gst_avi_mux_restart_file (avimux)) != GST_FLOW_OK)
        return res;
    }
  }

  if (GST_BUFFER_SIZE (data) & 1) {
    pad_bytes = 2 - (GST_BUFFER_SIZE (data) & 1);
  }
  header = gst_avi_mux_riff_get_video_header (GST_BUFFER_SIZE (data));
  total_size = GST_BUFFER_SIZE (header) + GST_BUFFER_SIZE (data) + pad_bytes;
  avimux->total_frames++;

  if (avimux->is_bigfile) {
    avimux->datax_size += total_size;
    avimux->numx_frames++;
  } else {
    guint flags = 0x2;

    if (!GST_BUFFER_FLAG_IS_SET (data, GST_BUFFER_FLAG_DELTA_UNIT))
      flags |= 0x10;
    avimux->data_size += total_size;
    avimux->num_frames++;
    gst_avi_mux_add_index (avimux, (guchar *) "00db", flags,
        GST_BUFFER_SIZE (data));
  }

  gst_buffer_set_caps (header, GST_PAD_CAPS (avimux->srcpad));
  if ((res = gst_pad_push (avimux->srcpad, header)) != GST_FLOW_OK)
    return res;
  if ((res = gst_pad_push (avimux->srcpad, data)) != GST_FLOW_OK)
    return res;

  if (pad_bytes) {
    if ((res = gst_avi_mux_send_pad_data (avimux, pad_bytes)) != GST_FLOW_OK)
      return res;
  }

  /* if any push above fails, we're in trouble with file consistency anyway */
  avimux->total_data += total_size;
  avimux->idx_offset += total_size;

  return res;
}


#if 0
/* take the oldest buffer in our internal queue and push-it */
static gboolean
gst_avi_mux_do_one_buffer (GstAviMux * avimux)
{
  if (avimux->video_buffer_queue && avimux->audio_buffer_queue) {
    if (GST_BUFFER_TIMESTAMP (avimux->video_buffer_queue) <=
        GST_BUFFER_TIMESTAMP (avimux->audio_buffer_queue))
      gst_avi_mux_do_video_buffer (avimux);
    else
      gst_avi_mux_do_audio_buffer (avimux);
  } else if (avimux->video_buffer_queue || avimux->audio_buffer_queue) {
    if (avimux->video_buffer_queue)
      gst_avi_mux_do_video_buffer (avimux);
    else
      gst_avi_mux_do_audio_buffer (avimux);
  } else {
    /* simply finish off the file and send EOS */
    gst_avi_mux_stop_file (avimux);
    gst_pad_push (avimux->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
    gst_element_set_eos (GST_ELEMENT (avimux));
    return FALSE;
  }

  return TRUE;
}


static void
gst_avi_mux_loop (GstElement * element)
{
  GstAviMux *avimux;

  avimux = GST_AVI_MUX (element);

  /* first fill queue (some elements only set caps when
   * flowing data), then write header */
  gst_avi_mux_fill_queue (avimux);

  if (avimux->write_header)
    gst_avi_mux_start_file (avimux);

  gst_avi_mux_do_one_buffer (avimux);
}
#endif

/* pick the oldest buffer from the pads and push it */
static GstFlowReturn
gst_avi_mux_do_one_buffer (GstAviMux * avimux)
{
  GstBuffer *video_buf = NULL;
  GstBuffer *audio_buf = NULL;
  GstFlowReturn res = GST_FLOW_OK;
  GstClockTime video_time = GST_CLOCK_TIME_NONE;
  GstClockTime audio_time = GST_CLOCK_TIME_NONE;

  if (avimux->videocollectdata && avimux->video_pad_connected) {
    video_buf =
        gst_collect_pads_peek (avimux->collect, avimux->videocollectdata);
  }

  if (avimux->audiocollectdata && avimux->audio_pad_connected) {
    audio_buf =
        gst_collect_pads_peek (avimux->collect, avimux->audiocollectdata);
  }

  /* segment info is used to translate the incoming timestamps 
   * to outgoing muxed (running) timeline */
  if (video_buf) {
    video_time =
        gst_segment_to_running_time (&avimux->videocollectdata->segment,
        GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (video_buf));
    GST_DEBUG ("peeked video buffer %p (time %" GST_TIME_FORMAT ")"
        ", running %" GST_TIME_FORMAT, video_buf,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (video_buf)),
        GST_TIME_ARGS (video_time));
  }
  if (audio_buf) {
    audio_time =
        gst_segment_to_running_time (&avimux->audiocollectdata->segment,
        GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (audio_buf));
    GST_DEBUG ("peeked audio buffer %p (time %" GST_TIME_FORMAT ")"
        ", running %" GST_TIME_FORMAT, audio_buf,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (audio_buf)),
        GST_TIME_ARGS (audio_time));

  }

  /* now use re-calculated time to choose */
  if (video_buf && audio_buf) {
    /* either video and audio can be translated, or translate neither */
    if (!GST_CLOCK_TIME_IS_VALID (video_time)
        || !GST_CLOCK_TIME_IS_VALID (audio_time)) {
      video_time = GST_BUFFER_TIMESTAMP (video_buf);
      audio_time = GST_BUFFER_TIMESTAMP (audio_buf);
    }
    if (video_time <= audio_time)
      res = gst_avi_mux_do_video_buffer (avimux);
    else
      res = gst_avi_mux_do_audio_buffer (avimux);
  } else if (video_buf) {
    res = gst_avi_mux_do_video_buffer (avimux);
  } else if (audio_buf) {
    res = gst_avi_mux_do_audio_buffer (avimux);
  } else {
    /* simply finish off the file and send EOS */
    gst_avi_mux_stop_file (avimux);
    gst_pad_push_event (avimux->srcpad, gst_event_new_eos ());
    return GST_FLOW_UNEXPECTED;
  }

  /* unref the peek obtained above */
  if (video_buf)
    gst_buffer_unref (video_buf);
  if (audio_buf)
    gst_buffer_unref (audio_buf);

  return res;
}

static GstFlowReturn
gst_avi_mux_collect_pads (GstCollectPads * pads, GstAviMux * avimux)
{
  GstFlowReturn res;

  if (G_UNLIKELY (avimux->write_header)) {
    if ((res = gst_avi_mux_start_file (avimux)) != GST_FLOW_OK)
      return res;
  }

  return gst_avi_mux_do_one_buffer (avimux);
}


static void
gst_avi_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstAviMux *avimux;

  avimux = GST_AVI_MUX (object);

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
gst_avi_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstAviMux *avimux;

  avimux = GST_AVI_MUX (object);

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
gst_avi_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstAviMux *avimux;

  avimux = GST_AVI_MUX (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (avimux->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      /* avimux->video_pad_eos = FALSE; */
      /* avimux->audio_pad_eos = FALSE; */
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (avimux->collect);
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state) {
    GstStateChangeReturn ret;

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (ret != GST_STATE_CHANGE_SUCCESS)
      return ret;
  }

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (avimux->tags) {
        gst_tag_list_free (avimux->tags);
        avimux->tags = NULL;
      }
      if (avimux->tags_snap) {
        gst_tag_list_free (avimux->tags_snap);
        avimux->tags_snap = NULL;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_STATE_CHANGE_SUCCESS;
}
