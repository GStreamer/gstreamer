/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *               <2001> Wim Taymans <wim.taymans@chello.be>
 *               <2011> Stefan Sauer <ensonic@user.sf.net>
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
#include <string.h>
#include "gstsmooth.h"
#include <gst/video/video.h>

/* Smooth args */

enum
{
  PROP_0,
  PROP_ACTIVE,
  PROP_TOLERANCE,
  PROP_FILTER_SIZE,
  PROP_LUMA_ONLY
};

static GstStaticPadTemplate gst_smooth_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")
    )
    );

static GstStaticPadTemplate gst_smooth_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420")
    )
    );


static gboolean gst_smooth_set_caps (GstBaseTransform * btrans,
    GstCaps * incaps, GstCaps * outcaps);

static GstFlowReturn gst_smooth_transform (GstBaseTransform * btrans,
    GstBuffer * inbuf, GstBuffer * outbuf);
static void smooth_filter (unsigned char *dest, unsigned char *src,
    int width, int height, int tolerance, int filtersize);

static void gst_smooth_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_smooth_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

GST_BOILERPLATE (GstSmooth, gst_smooth, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void
gst_smooth_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &gst_smooth_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_smooth_src_template);
  gst_element_class_set_details_simple (element_class, "Smooth effect",
      "Filter/Effect/Video",
      "Apply a smooth filter to an image",
      "Wim Taymans <wim.taymans@chello.be>");
}

static void
gst_smooth_class_init (GstSmoothClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *btrans_class = (GstBaseTransformClass *) klass;

  gobject_class->set_property = gst_smooth_set_property;
  gobject_class->get_property = gst_smooth_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ACTIVE,
      g_param_spec_boolean ("active", "active", "process video", TRUE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TOLERANCE,
      g_param_spec_int ("tolerance",
          "tolerance", "contrast tolerance for smoothing", G_MININT,
          G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER_SIZE,
      g_param_spec_int ("filter-size", "filter-size", "size of media filter",
          G_MININT, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_LUMA_ONLY,
      g_param_spec_boolean ("luma-only", "luma-only", "only filter luma part",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  btrans_class->transform = GST_DEBUG_FUNCPTR (gst_smooth_transform);
  btrans_class->set_caps = GST_DEBUG_FUNCPTR (gst_smooth_set_caps);
}

static gboolean
gst_smooth_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstSmooth *filter;
  GstStructure *structure;
  gboolean ret;

  filter = GST_SMOOTH (btrans);

  structure = gst_caps_get_structure (incaps, 0);
  ret = gst_structure_get_int (structure, "width", &filter->width);
  ret &= gst_structure_get_int (structure, "height", &filter->height);

  return ret;
}

static void
gst_smooth_init (GstSmooth * smooth, GstSmoothClass * klass)
{
  smooth->active = TRUE;
  smooth->tolerance = 8;
  smooth->filtersize = 3;
  smooth->luma_only = TRUE;
}

static void
smooth_filter (guchar * dest, guchar * src, gint width, gint height,
    gint tolerance, gint filtersize)
{
  gint refval, aktval, upperval, lowerval, numvalues, sum;
  gint x, y, fx, fy, fy1, fy2, fx1, fx2;
  guchar *srcp = src;

  fy1 = 0;
  fy2 = MIN (filtersize + 1, height) * width;

  for (y = 0; y < height; y++) {
    if (y > (filtersize + 1))
      fy1 += width;
    if (y < height - (filtersize + 1))
      fy2 += width;

    for (x = 0; x < width; x++) {
      refval = *src;
      upperval = refval + tolerance;
      lowerval = refval - tolerance;

      numvalues = 1;
      sum = refval;

      fx1 = MAX (x - filtersize, 0) + fy1;
      fx2 = MIN (x + filtersize + 1, width) + fy1;

      for (fy = fy1; fy < fy2; fy += width) {
        for (fx = fx1; fx < fx2; fx++) {
          aktval = srcp[fx];
          if ((lowerval - aktval) * (upperval - aktval) < 0) {
            numvalues++;
            sum += aktval;
          }
        }                       /*for fx */
        fx1 += width;
        fx2 += width;
      }                         /*for fy */

      src++;
      *dest++ = sum / numvalues;
    }
  }
}

static GstFlowReturn
gst_smooth_transform (GstBaseTransform * btrans, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstSmooth *smooth;
  guint8 *idata, *odata;
  guint size, lumsize, chromsize;

  smooth = GST_SMOOTH (btrans);
  idata = GST_BUFFER_DATA (inbuf);
  odata = GST_BUFFER_DATA (outbuf);
  size = GST_BUFFER_SIZE (inbuf);

  if (!smooth->active) {
    memcpy (odata, idata, size);
    return GST_FLOW_OK;
  }

  GST_DEBUG_OBJECT (smooth, "smooth: have buffer of %d", size);

  lumsize = smooth->width * smooth->height;
  chromsize = lumsize / 4;

  smooth_filter (odata, idata, smooth->width, smooth->height,
      smooth->tolerance, smooth->filtersize);
  if (!smooth->luma_only) {
    smooth_filter (odata + lumsize, idata + lumsize,
        smooth->width / 2, smooth->height / 2, smooth->tolerance,
        smooth->filtersize / 2);
    smooth_filter (odata + lumsize + chromsize,
        idata + lumsize + chromsize, smooth->width / 2, smooth->height / 2,
        smooth->tolerance, smooth->filtersize / 2);
  } else {
    memcpy (odata + lumsize, idata + lumsize, chromsize * 2);
  }

  return GST_FLOW_OK;
}

static void
gst_smooth_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSmooth *smooth;

  g_return_if_fail (GST_IS_SMOOTH (object));
  smooth = GST_SMOOTH (object);

  switch (prop_id) {
    case PROP_ACTIVE:
      smooth->active = g_value_get_boolean (value);
      break;
    case PROP_TOLERANCE:
      smooth->tolerance = g_value_get_int (value);
      break;
    case PROP_FILTER_SIZE:
      smooth->filtersize = g_value_get_int (value);
      break;
    case PROP_LUMA_ONLY:
      smooth->luma_only = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_smooth_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSmooth *smooth;

  g_return_if_fail (GST_IS_SMOOTH (object));
  smooth = GST_SMOOTH (object);

  switch (prop_id) {
    case PROP_ACTIVE:
      g_value_set_boolean (value, smooth->active);
      break;
    case PROP_TOLERANCE:
      g_value_set_int (value, smooth->tolerance);
      break;
    case PROP_FILTER_SIZE:
      g_value_set_int (value, smooth->filtersize);
      break;
    case PROP_LUMA_ONLY:
      g_value_set_boolean (value, smooth->luma_only);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "smooth",
      GST_RANK_NONE, GST_TYPE_SMOOTH);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "smooth",
    "Apply a smooth filter to an image",
    plugin_init, VERSION, "LGPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
