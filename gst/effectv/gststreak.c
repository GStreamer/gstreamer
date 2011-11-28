/* GStreamer
 * Copyright (C) <2009> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * EffecTV - Realtime Digital Video Effector
 * Copyright (C) 2001-2006 FUKUCHI Kentaro
 *
 * StreakTV - afterimage effector.
 * Copyright (C) 2001-2002 FUKUCHI Kentaro
 *
 * This combines the StreakTV and BaltanTV effects, which are
 * very similar. BaltanTV is used if the feedback property is set
 * to TRUE, otherwise StreakTV is used.
 *
 * EffecTV is free software. This library is free software;
 * you can redistribute it and/or
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

/**
 * SECTION:element-streaktv
 *
 * StreakTV makes after images of moving objects.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! streaktv ! ffmpegcolorspace ! autovideosink
 * ]| This pipeline shows the effect of streaktv on a test stream.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

#include "gststreak.h"
#include "gsteffectv.h"

#include <gst/video/video.h>

#define DEFAULT_FEEDBACK FALSE

enum
{
  PROP_0,
  PROP_FEEDBACK
};

GST_BOILERPLATE (GstStreakTV, gst_streaktv, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static GstStaticPadTemplate gst_streaktv_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR "; " GST_VIDEO_CAPS_xRGB)
    );

static GstStaticPadTemplate gst_streaktv_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx "; " GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR "; " GST_VIDEO_CAPS_xRGB)
    );



static GstFlowReturn
gst_streaktv_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstStreakTV *filter = GST_STREAKTV (trans);
  guint32 *src, *dest;
  GstFlowReturn ret = GST_FLOW_OK;
  gint i, cf;
  gint video_area = filter->width * filter->height;
  guint32 **planetable = filter->planetable;
  gint plane = filter->plane;
  guint stride_mask, stride_shift, stride;

  GST_OBJECT_LOCK (filter);
  if (filter->feedback) {
    stride_mask = 0xfcfcfcfc;
    stride = 8;
    stride_shift = 2;
  } else {
    stride_mask = 0xf8f8f8f8;
    stride = 4;
    stride_shift = 3;
  }

  src = (guint32 *) GST_BUFFER_DATA (in);
  dest = (guint32 *) GST_BUFFER_DATA (out);

  for (i = 0; i < video_area; i++) {
    planetable[plane][i] = (src[i] & stride_mask) >> stride_shift;
  }

  cf = plane & (stride - 1);
  if (filter->feedback) {
    for (i = 0; i < video_area; i++) {
      dest[i] = planetable[cf][i]
          + planetable[cf + stride][i]
          + planetable[cf + stride * 2][i]
          + planetable[cf + stride * 3][i];
      planetable[plane][i] = (dest[i] & stride_mask) >> stride_shift;
    }
  } else {
    for (i = 0; i < video_area; i++) {
      dest[i] = planetable[cf][i]
          + planetable[cf + stride][i]
          + planetable[cf + stride * 2][i]
          + planetable[cf + stride * 3][i]
          + planetable[cf + stride * 4][i]
          + planetable[cf + stride * 5][i]
          + planetable[cf + stride * 6][i]
          + planetable[cf + stride * 7][i];
    }
  }

  plane++;
  filter->plane = plane & (PLANES - 1);
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static gboolean
gst_streaktv_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstStreakTV *filter = GST_STREAKTV (btrans);
  GstStructure *structure;
  gboolean ret = FALSE;

  structure = gst_caps_get_structure (incaps, 0);

  GST_OBJECT_LOCK (filter);
  if (gst_structure_get_int (structure, "width", &filter->width) &&
      gst_structure_get_int (structure, "height", &filter->height)) {
    gint i;

    if (filter->planebuffer)
      g_free (filter->planebuffer);

    filter->planebuffer =
        g_new0 (guint32, filter->width * filter->height * 4 * PLANES);
    for (i = 0; i < PLANES; i++)
      filter->planetable[i] =
          &filter->planebuffer[filter->width * filter->height * i];

    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (filter);

  return ret;
}

static gboolean
gst_streaktv_start (GstBaseTransform * trans)
{
  GstStreakTV *filter = GST_STREAKTV (trans);

  filter->plane = 0;

  return TRUE;
}

static void
gst_streaktv_finalize (GObject * object)
{
  GstStreakTV *filter = GST_STREAKTV (object);

  if (filter->planebuffer) {
    g_free (filter->planebuffer);
    filter->planebuffer = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_streaktv_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstStreakTV *filter = GST_STREAKTV (object);

  switch (prop_id) {
    case PROP_FEEDBACK:
      if (G_UNLIKELY (GST_STATE (filter) >= GST_STATE_PAUSED)) {
        g_warning ("Changing the \"feedback\" property only allowed "
            "in state < PLAYING");
        return;
      }

      filter->feedback = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_streaktv_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstStreakTV *filter = GST_STREAKTV (object);

  switch (prop_id) {
    case PROP_FEEDBACK:
      g_value_set_boolean (value, filter->feedback);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_streaktv_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "StreakTV effect",
      "Filter/Effect/Video",
      "StreakTV makes after images of moving objects",
      "FUKUCHI, Kentarou <fukuchi@users.sourceforge.net>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_streaktv_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_streaktv_src_template);
}

static void
gst_streaktv_class_init (GstStreakTVClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_streaktv_set_property;
  gobject_class->get_property = gst_streaktv_get_property;

  gobject_class->finalize = gst_streaktv_finalize;

  g_object_class_install_property (gobject_class, PROP_FEEDBACK,
      g_param_spec_boolean ("feedback", "Feedback",
          "Feedback", DEFAULT_FEEDBACK,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_streaktv_set_caps);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_streaktv_transform);
  trans_class->start = GST_DEBUG_FUNCPTR (gst_streaktv_start);
}

static void
gst_streaktv_init (GstStreakTV * filter, GstStreakTVClass * klass)
{
  filter->feedback = DEFAULT_FEEDBACK;
}
