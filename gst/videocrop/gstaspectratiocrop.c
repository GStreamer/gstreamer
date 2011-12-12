/* GStreamer video frame cropping to aspect-ratio
 * Copyright (C) 2009 Thijs Vermeir <thijsvermeir@gmail.com>
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

/**
 * SECTION:element-aspectratiocrop
 * @see_also: #GstVideoCrop
 *
 * This element crops video frames to a specified #GstAspectRatioCrop:aspect-ratio.
 *
 * If the aspect-ratio is already correct, the element will operate
 * in pass-through mode.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v videotestsrc ! video/x-raw-rgb,height=640,width=480 ! aspectratiocrop aspect-ratio=16/9 ! ximagesink
 * ]| This pipeline generates a videostream in 4/3 and crops it to 16/9.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>

#include "gstaspectratiocrop.h"

#include "gst/glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (aspect_ratio_crop_debug);
#define GST_CAT_DEFAULT aspect_ratio_crop_debug

enum
{
  ARG_0,
  ARG_ASPECT_RATIO_CROP,
};

/* we support the same caps as videocrop */
#define ASPECT_RATIO_CROP_CAPS                          \
  GST_VIDEO_CAPS_RGBx ";"                        \
  GST_VIDEO_CAPS_xRGB ";"                        \
  GST_VIDEO_CAPS_BGRx ";"                        \
  GST_VIDEO_CAPS_xBGR ";"                        \
  GST_VIDEO_CAPS_RGBA ";"                        \
  GST_VIDEO_CAPS_ARGB ";"                        \
  GST_VIDEO_CAPS_BGRA ";"                        \
  GST_VIDEO_CAPS_ABGR ";"                        \
  GST_VIDEO_CAPS_RGB ";"                         \
  GST_VIDEO_CAPS_BGR ";"                         \
  GST_VIDEO_CAPS_YUV ("AYUV") ";"                \
  GST_VIDEO_CAPS_YUV ("YUY2") ";"                \
  GST_VIDEO_CAPS_YUV ("YVYU") ";"                \
  GST_VIDEO_CAPS_YUV ("UYVY") ";"                \
  GST_VIDEO_CAPS_YUV ("Y800") ";"                \
  GST_VIDEO_CAPS_YUV ("I420") ";"                \
  GST_VIDEO_CAPS_YUV ("YV12") ";"                \
  GST_VIDEO_CAPS_RGB_16 ";"                      \
  GST_VIDEO_CAPS_RGB_15

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (ASPECT_RATIO_CROP_CAPS)
    );

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (ASPECT_RATIO_CROP_CAPS)
    );

GST_BOILERPLATE (GstAspectRatioCrop, gst_aspect_ratio_crop, GstBin,
    GST_TYPE_BIN);

static void gst_aspect_ratio_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_aspect_ratio_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_aspect_ratio_crop_set_cropping (GstAspectRatioCrop *
    aspect_ratio_crop, gint top, gint right, gint bottom, gint left);
static GstCaps *gst_aspect_ratio_crop_get_caps (GstPad * pad);
static gboolean gst_aspect_ratio_crop_set_caps (GstPad * pad, GstCaps * caps);
static void gst_aspect_ratio_crop_finalize (GObject * object);
static void gst_aspect_ratio_transform_structure (GstAspectRatioCrop *
    aspect_ratio_crop, GstStructure * structure, GstStructure ** new_structure,
    gboolean set_videocrop);

static void
gst_aspect_ratio_crop_set_cropping (GstAspectRatioCrop * aspect_ratio_crop,
    gint top, gint right, gint bottom, gint left)
{
  GValue value = { 0 };
  if (G_UNLIKELY (!aspect_ratio_crop->videocrop)) {
    GST_WARNING_OBJECT (aspect_ratio_crop,
        "Can't set the settings if there is no cropping element");
    return;
  }

  g_value_init (&value, G_TYPE_INT);
  g_value_set_int (&value, top);
  GST_DEBUG_OBJECT (aspect_ratio_crop, "set top cropping to: %d", top);
  g_object_set_property (G_OBJECT (aspect_ratio_crop->videocrop), "top",
      &value);
  g_value_set_int (&value, right);
  GST_DEBUG_OBJECT (aspect_ratio_crop, "set right cropping to: %d", right);
  g_object_set_property (G_OBJECT (aspect_ratio_crop->videocrop), "right",
      &value);
  g_value_set_int (&value, bottom);
  GST_DEBUG_OBJECT (aspect_ratio_crop, "set bottom cropping to: %d", bottom);
  g_object_set_property (G_OBJECT (aspect_ratio_crop->videocrop), "bottom",
      &value);
  g_value_set_int (&value, left);
  GST_DEBUG_OBJECT (aspect_ratio_crop, "set left cropping to: %d", left);
  g_object_set_property (G_OBJECT (aspect_ratio_crop->videocrop), "left",
      &value);

  g_value_unset (&value);
}

static gboolean
gst_aspect_ratio_crop_set_caps (GstPad * pad, GstCaps * caps)
{
  GstAspectRatioCrop *aspect_ratio_crop;
  GstPad *peer_pad;
  GstStructure *structure;
  gboolean ret;

  aspect_ratio_crop = GST_ASPECT_RATIO_CROP (gst_pad_get_parent (pad));

  g_mutex_lock (aspect_ratio_crop->crop_lock);

  structure = gst_caps_get_structure (caps, 0);
  gst_aspect_ratio_transform_structure (aspect_ratio_crop, structure, NULL,
      TRUE);
  peer_pad =
      gst_element_get_static_pad (GST_ELEMENT (aspect_ratio_crop->videocrop),
      "sink");
  ret = gst_pad_set_caps (peer_pad, caps);
  gst_object_unref (peer_pad);
  gst_object_unref (aspect_ratio_crop);
  g_mutex_unlock (aspect_ratio_crop->crop_lock);
  return ret;
}

static void
gst_aspect_ratio_crop_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "aspectratiocrop",
      "Filter/Effect/Video",
      "Crops video into a user-defined aspect-ratio",
      "Thijs Vermeir <thijsvermeir@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);
}

static void
gst_aspect_ratio_crop_class_init (GstAspectRatioCropClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_aspect_ratio_crop_set_property;
  gobject_class->get_property = gst_aspect_ratio_crop_get_property;
  gobject_class->finalize = gst_aspect_ratio_crop_finalize;

  g_object_class_install_property (gobject_class, ARG_ASPECT_RATIO_CROP,
      gst_param_spec_fraction ("aspect-ratio", "aspect-ratio",
          "Target aspect-ratio of video", 0, 1, G_MAXINT, 1, 0, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_aspect_ratio_crop_finalize (GObject * object)
{
  GstAspectRatioCrop *aspect_ratio_crop;

  aspect_ratio_crop = GST_ASPECT_RATIO_CROP (object);

  if (aspect_ratio_crop->crop_lock)
    g_mutex_free (aspect_ratio_crop->crop_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_aspect_ratio_crop_init (GstAspectRatioCrop * aspect_ratio_crop,
    GstAspectRatioCropClass * klass)
{
  GstPad *link_pad;
  GstPad *src_pad;

  GST_DEBUG_CATEGORY_INIT (aspect_ratio_crop_debug, "aspectratiocrop", 0,
      "aspectratiocrop");

  aspect_ratio_crop->ar_num = 0;
  aspect_ratio_crop->ar_denom = 1;

  aspect_ratio_crop->crop_lock = g_mutex_new ();

  /* add the transform element */
  aspect_ratio_crop->videocrop = gst_element_factory_make ("videocrop", NULL);
  gst_bin_add (GST_BIN (aspect_ratio_crop), aspect_ratio_crop->videocrop);

  /* create ghost pad src */
  link_pad =
      gst_element_get_static_pad (GST_ELEMENT (aspect_ratio_crop->videocrop),
      "src");
  src_pad = gst_ghost_pad_new ("src", link_pad);
  gst_pad_set_getcaps_function (src_pad,
      GST_DEBUG_FUNCPTR (gst_aspect_ratio_crop_get_caps));
  gst_element_add_pad (GST_ELEMENT (aspect_ratio_crop), src_pad);
  gst_object_unref (link_pad);
  /* create ghost pad sink */
  link_pad =
      gst_element_get_static_pad (GST_ELEMENT (aspect_ratio_crop->videocrop),
      "sink");
  aspect_ratio_crop->sink = gst_ghost_pad_new ("sink", link_pad);
  gst_element_add_pad (GST_ELEMENT (aspect_ratio_crop),
      aspect_ratio_crop->sink);
  gst_object_unref (link_pad);
  gst_pad_set_setcaps_function (aspect_ratio_crop->sink,
      GST_DEBUG_FUNCPTR (gst_aspect_ratio_crop_set_caps));
}

static void
gst_aspect_ratio_transform_structure (GstAspectRatioCrop * aspect_ratio_crop,
    GstStructure * structure, GstStructure ** new_structure,
    gboolean set_videocrop)
{
  gdouble incoming_ar;
  gdouble requested_ar;
  gint width, height;
  gint cropvalue;
  gint par_d, par_n;

  /* Check if we need to change the aspect ratio */
  if (aspect_ratio_crop->ar_num < 1) {
    GST_DEBUG_OBJECT (aspect_ratio_crop, "No cropping requested");
    goto beach;
  }

  /* get the information from the caps */
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height))
    goto beach;

  if (!gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          &par_n, &par_d)) {
    par_d = par_n = 1;
  }

  incoming_ar = ((gdouble) (width * par_n)) / (height * par_d);
  GST_LOG_OBJECT (aspect_ratio_crop,
      "incoming caps width(%d), height(%d), par (%d/%d) : ar = %f", width,
      height, par_n, par_d, incoming_ar);

  requested_ar =
      (gdouble) aspect_ratio_crop->ar_num / aspect_ratio_crop->ar_denom;

  /* check if the original aspect-ratio is the aspect-ratio that we want */
  if (requested_ar == incoming_ar) {
    GST_DEBUG_OBJECT (aspect_ratio_crop,
        "Input video already has the correct aspect ratio (%.3f == %.3f)",
        incoming_ar, requested_ar);
    goto beach;
  } else if (requested_ar > incoming_ar) {
    /* fix aspect ratio with cropping on top and bottom */
    cropvalue =
        ((((double) aspect_ratio_crop->ar_denom /
                (double) (aspect_ratio_crop->ar_num)) * ((double) par_n /
                (double) par_d) * width) - height) / 2;
    if (cropvalue < 0) {
      cropvalue *= -1;
    }
    if (cropvalue >= (height / 2))
      goto crop_failed;
    if (set_videocrop) {
      gst_aspect_ratio_crop_set_cropping (aspect_ratio_crop, cropvalue, 0,
          cropvalue, 0);
    }
    if (new_structure) {
      *new_structure = gst_structure_copy (structure);
      gst_structure_set (*new_structure,
          "height", G_TYPE_INT, (int) (height - (cropvalue * 2)), NULL);
    }
  } else {
    /* fix aspect ratio with cropping on left and right */
    cropvalue =
        ((((double) aspect_ratio_crop->ar_num /
                (double) (aspect_ratio_crop->ar_denom)) * ((double) par_d /
                (double) par_n) * height) - width) / 2;
    if (cropvalue < 0) {
      cropvalue *= -1;
    }
    if (cropvalue >= (width / 2))
      goto crop_failed;
    if (set_videocrop) {
      gst_aspect_ratio_crop_set_cropping (aspect_ratio_crop, 0, cropvalue,
          0, cropvalue);
    }
    if (new_structure) {
      *new_structure = gst_structure_copy (structure);
      gst_structure_set (*new_structure,
          "width", G_TYPE_INT, (int) (width - (cropvalue * 2)), NULL);
    }
  }

  return;

crop_failed:
  GST_WARNING_OBJECT (aspect_ratio_crop,
      "can't crop to aspect ratio requested");
  goto beach;
beach:
  if (set_videocrop) {
    gst_aspect_ratio_crop_set_cropping (aspect_ratio_crop, 0, 0, 0, 0);
  }

  if (new_structure) {
    *new_structure = gst_structure_copy (structure);
  }
}

static GstCaps *
gst_aspect_ratio_crop_transform_caps (GstAspectRatioCrop * aspect_ratio_crop,
    GstCaps * caps)
{
  GstCaps *transform;
  gint size, i;

  transform = gst_caps_new_empty ();

  size = gst_caps_get_size (caps);

  for (i = 0; i < size; i++) {
    GstStructure *s;
    GstStructure *trans_s;

    s = gst_caps_get_structure (caps, i);

    gst_aspect_ratio_transform_structure (aspect_ratio_crop, s, &trans_s,
        FALSE);
    gst_caps_append_structure (transform, trans_s);
  }

  return transform;
}

static GstCaps *
gst_aspect_ratio_crop_get_caps (GstPad * pad)
{
  GstPad *peer;
  GstAspectRatioCrop *aspect_ratio_crop;
  GstCaps *return_caps;

  aspect_ratio_crop = GST_ASPECT_RATIO_CROP (gst_pad_get_parent (pad));

  g_mutex_lock (aspect_ratio_crop->crop_lock);

  peer = gst_pad_get_peer (aspect_ratio_crop->sink);
  if (peer == NULL) {
    return_caps = gst_static_pad_template_get_caps (&src_template);
    gst_caps_ref (return_caps);
  } else {
    GstCaps *peer_caps;

    peer_caps = gst_pad_get_caps (peer);
    return_caps =
        gst_aspect_ratio_crop_transform_caps (aspect_ratio_crop, peer_caps);
    gst_caps_unref (peer_caps);
    gst_object_unref (peer);
  }

  g_mutex_unlock (aspect_ratio_crop->crop_lock);
  gst_object_unref (aspect_ratio_crop);

  return return_caps;
}

static void
gst_aspect_ratio_crop_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAspectRatioCrop *aspect_ratio_crop;
  gboolean recheck = FALSE;

  aspect_ratio_crop = GST_ASPECT_RATIO_CROP (object);

  GST_OBJECT_LOCK (aspect_ratio_crop);
  switch (prop_id) {
    case ARG_ASPECT_RATIO_CROP:
      if (GST_VALUE_HOLDS_FRACTION (value)) {
        aspect_ratio_crop->ar_num = gst_value_get_fraction_numerator (value);
        aspect_ratio_crop->ar_denom =
            gst_value_get_fraction_denominator (value);
        recheck = (GST_PAD_CAPS (aspect_ratio_crop->sink) != NULL);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (aspect_ratio_crop);

  if (recheck) {
    gst_aspect_ratio_crop_set_caps (aspect_ratio_crop->sink,
        GST_PAD_CAPS (aspect_ratio_crop->sink));
  }
}

static void
gst_aspect_ratio_crop_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAspectRatioCrop *aspect_ratio_crop;

  aspect_ratio_crop = GST_ASPECT_RATIO_CROP (object);

  GST_OBJECT_LOCK (aspect_ratio_crop);
  switch (prop_id) {
    case ARG_ASPECT_RATIO_CROP:
      gst_value_set_fraction (value, aspect_ratio_crop->ar_num,
          aspect_ratio_crop->ar_denom);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (aspect_ratio_crop);
}
