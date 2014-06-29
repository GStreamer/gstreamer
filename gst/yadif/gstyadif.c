/* GStreamer
 * Copyright (C) 2013 David Schleef <ds@schleef.org>
 * Copyright (C) 2013 Rdio, Inc. <ingestions@rd.io>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstyadif
 *
 * The yadif element deinterlaces video, using the YADIF deinterlacing
 * filter copied from Libav.  This element only handles the simple case
 * of interlaced-mode=interleaved video instead of the more complex
 * inverse telecine and deinterlace cases that are handled by the
 * deinterlace element.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc pattern=ball ! interlace ! yadif ! xvimagesink
 * ]|
 * This pipeline creates an interlaced test pattern, and then deinterlaces
 * it using the yadif filter.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>
#include <gst/video/video.h>
#include "gstyadif.h"

GST_DEBUG_CATEGORY_STATIC (gst_yadif_debug_category);
#define GST_CAT_DEFAULT gst_yadif_debug_category

/* prototypes */


static void gst_yadif_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_yadif_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_yadif_dispose (GObject * object);
static void gst_yadif_finalize (GObject * object);

static GstCaps *gst_yadif_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_yadif_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps);
static gboolean gst_yadif_get_unit_size (GstBaseTransform * trans,
    GstCaps * caps, gsize * size);
static gboolean gst_yadif_start (GstBaseTransform * trans);
static gboolean gst_yadif_stop (GstBaseTransform * trans);
static GstFlowReturn gst_yadif_transform (GstBaseTransform * trans,
    GstBuffer * inbuf, GstBuffer * outbuf);

enum
{
  PROP_0,
  PROP_MODE
};

#define DEFAULT_MODE GST_DEINTERLACE_MODE_AUTO

/* pad templates */

static GstStaticPadTemplate gst_yadif_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{Y42B,I420,Y444}")
        ",interlace-mode=(string){interleaved,mixed,progressive}")
    );

static GstStaticPadTemplate gst_yadif_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{Y42B,I420,Y444}")
        ",interlace-mode=(string)progressive")
    );

#define GST_TYPE_DEINTERLACE_MODES (gst_deinterlace_modes_get_type ())
static GType
gst_deinterlace_modes_get_type (void)
{
  static GType deinterlace_modes_type = 0;

  static const GEnumValue modes_types[] = {
    {GST_DEINTERLACE_MODE_AUTO, "Auto detection", "auto"},
    {GST_DEINTERLACE_MODE_INTERLACED, "Force deinterlacing", "interlaced"},
    {GST_DEINTERLACE_MODE_DISABLED, "Run in passthrough mode", "disabled"},
    {0, NULL, NULL},
  };

  if (!deinterlace_modes_type) {
    deinterlace_modes_type =
        g_enum_register_static ("GstYadifModes", modes_types);
  }
  return deinterlace_modes_type;
}


/* class initialization */

G_DEFINE_TYPE_WITH_CODE (GstYadif, gst_yadif, GST_TYPE_BASE_TRANSFORM,
    GST_DEBUG_CATEGORY_INIT (gst_yadif_debug_category, "yadif", 0,
        "debug category for yadif element"));

static void
gst_yadif_class_init (GstYadifClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstBaseTransformClass *base_transform_class =
      GST_BASE_TRANSFORM_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_yadif_sink_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_yadif_src_template));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "YADIF deinterlacer", "Video/Filter",
      "Deinterlace video using YADIF filter", "David Schleef <ds@schleef.org>");

  gobject_class->set_property = gst_yadif_set_property;
  gobject_class->get_property = gst_yadif_get_property;
  gobject_class->dispose = gst_yadif_dispose;
  gobject_class->finalize = gst_yadif_finalize;
  base_transform_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_yadif_transform_caps);
  base_transform_class->set_caps = GST_DEBUG_FUNCPTR (gst_yadif_set_caps);
  base_transform_class->get_unit_size =
      GST_DEBUG_FUNCPTR (gst_yadif_get_unit_size);
  base_transform_class->start = GST_DEBUG_FUNCPTR (gst_yadif_start);
  base_transform_class->stop = GST_DEBUG_FUNCPTR (gst_yadif_stop);
  base_transform_class->transform = GST_DEBUG_FUNCPTR (gst_yadif_transform);

  g_object_class_install_property (gobject_class, PROP_MODE,
      g_param_spec_enum ("mode", "Deinterlace Mode",
          "Deinterlace mode",
          GST_TYPE_DEINTERLACE_MODES,
          DEFAULT_MODE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

}

static void
gst_yadif_init (GstYadif * yadif)
{
}

void
gst_yadif_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstYadif *yadif = GST_YADIF (object);

  switch (property_id) {
    case PROP_MODE:
      yadif->mode = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_yadif_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstYadif *yadif = GST_YADIF (object);

  switch (property_id) {
    case PROP_MODE:
      g_value_set_enum (value, yadif->mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_yadif_dispose (GObject * object)
{
  /* GstYadif *yadif = GST_YADIF (object); */

  /* clean up as possible.  may be called multiple times */

  G_OBJECT_CLASS (gst_yadif_parent_class)->dispose (object);
}

void
gst_yadif_finalize (GObject * object)
{
  /* GstYadif *yadif = GST_YADIF (object); */

  /* clean up object here */

  G_OBJECT_CLASS (gst_yadif_parent_class)->finalize (object);
}


static GstCaps *
gst_yadif_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *othercaps;

  othercaps = gst_caps_copy (caps);

  if (direction == GST_PAD_SRC) {
    GValue value = G_VALUE_INIT;
    GValue v = G_VALUE_INIT;

    g_value_init (&value, GST_TYPE_LIST);
    g_value_init (&v, G_TYPE_STRING);

    g_value_set_string (&v, "interleaved");
    gst_value_list_append_value (&value, &v);
    g_value_set_string (&v, "mixed");
    gst_value_list_append_value (&value, &v);
    g_value_set_string (&v, "progressive");
    gst_value_list_append_value (&value, &v);

    gst_caps_set_value (othercaps, "interlace-mode", &value);
    g_value_reset (&value);
    g_value_reset (&v);
  } else {
    gst_caps_set_simple (othercaps, "interlace-mode", G_TYPE_STRING,
        "progressive", NULL);
  }

  return othercaps;
}

static gboolean
gst_yadif_set_caps (GstBaseTransform * trans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstYadif *yadif = GST_YADIF (trans);

  gst_video_info_from_caps (&yadif->video_info, incaps);

  return TRUE;
}

static gboolean
gst_yadif_get_unit_size (GstBaseTransform * trans, GstCaps * caps, gsize * size)
{
  GstVideoInfo info;

  if (gst_video_info_from_caps (&info, caps)) {
    *size = GST_VIDEO_INFO_SIZE (&info);

    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_yadif_start (GstBaseTransform * trans)
{

  return TRUE;
}

static gboolean
gst_yadif_stop (GstBaseTransform * trans)
{

  return TRUE;
}

void yadif_filter (GstYadif * yadif, int parity, int tff);

static GstFlowReturn
gst_yadif_transform (GstBaseTransform * trans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstYadif *yadif = GST_YADIF (trans);
  int parity;
  int tff;

  parity = 0;
  tff = 0;

  if (!gst_video_frame_map (&yadif->dest_frame, &yadif->video_info, outbuf,
          GST_MAP_WRITE))
    goto dest_map_failed;

  if (!gst_video_frame_map (&yadif->cur_frame, &yadif->video_info, inbuf,
          GST_MAP_READ))
    goto src_map_failed;

  yadif->next_frame = yadif->cur_frame;
  yadif->prev_frame = yadif->cur_frame;

  yadif_filter (yadif, parity, tff);

  gst_video_frame_unmap (&yadif->dest_frame);
  gst_video_frame_unmap (&yadif->cur_frame);
  return GST_FLOW_OK;

dest_map_failed:
  {
    GST_ERROR_OBJECT (yadif, "failed to map dest");
    return GST_FLOW_ERROR;
  }
src_map_failed:
  {
    GST_ERROR_OBJECT (yadif, "failed to map src");
    gst_video_frame_unmap (&yadif->dest_frame);
    return GST_FLOW_ERROR;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{

  return gst_element_register (plugin, "yadif", GST_RANK_NONE, GST_TYPE_YADIF);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    yadif,
    "YADIF deinterlacing filter",
    plugin_init, VERSION, "GPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN)
