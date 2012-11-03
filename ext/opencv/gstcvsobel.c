/*
 * GStreamer
 * Copyright (C) 2010 Thiago Santos <thiago.sousa.santos@collabora.co.uk>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstopencvutils.h"
#include "gstcvsobel.h"

GST_DEBUG_CATEGORY_STATIC (gst_cv_sobel_debug);
#define GST_CAT_DEFAULT gst_cv_sobel_debug

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("GRAY8"))
    );


#if G_BYTE_ORDER == G_BIG_ENDIAN
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("GRAY16_BE"))
    );
#else
static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("GRAY16_LE"))
    );
#endif

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};
enum
{
  PROP_0,
  PROP_X_ORDER,
  PROP_Y_ORDER,
  PROP_APERTURE_SIZE
};

#define DEFAULT_X_ORDER 1
#define DEFAULT_Y_ORDER 0
#define DEFAULT_APERTURE_SIZE 3

G_DEFINE_TYPE (GstCvSobel, gst_cv_sobel, GST_TYPE_OPENCV_VIDEO_FILTER);

static void gst_cv_sobel_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_cv_sobel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_cv_sobel_transform_caps (GstBaseTransform * trans,
    GstPadDirection dir, GstCaps * caps, GstCaps * filter);

static GstFlowReturn gst_cv_sobel_transform (GstOpencvVideoFilter * filter,
    GstBuffer * buf, IplImage * img, GstBuffer * outbuf, IplImage * outimg);

/* initialize the cvsobel's class */
static void
gst_cv_sobel_class_init (GstCvSobelClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *gstbasetransform_class;
  GstOpencvVideoFilterClass *gstopencvbasefilter_class;

  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  gobject_class = (GObjectClass *) klass;
  gstbasetransform_class = (GstBaseTransformClass *) klass;
  gstopencvbasefilter_class = (GstOpencvVideoFilterClass *) klass;

  gobject_class->set_property = gst_cv_sobel_set_property;
  gobject_class->get_property = gst_cv_sobel_get_property;

  gstbasetransform_class->transform_caps = gst_cv_sobel_transform_caps;

  gstopencvbasefilter_class->cv_trans_func = gst_cv_sobel_transform;

  g_object_class_install_property (gobject_class, PROP_X_ORDER,
      g_param_spec_int ("x-order", "x order",
          "Order of the derivative x", -1, G_MAXINT,
          DEFAULT_X_ORDER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_Y_ORDER,
      g_param_spec_int ("y-order", "y order",
          "Order of the derivative y", -1, G_MAXINT,
          DEFAULT_Y_ORDER, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_APERTURE_SIZE,
      g_param_spec_int ("aperture-size", "aperture size",
          "Size of the extended Sobel Kernel (1, 3, 5 or 7)", 1, 7,
          DEFAULT_APERTURE_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));

  gst_element_class_set_static_metadata (element_class,
      "cvsobel",
      "Transform/Effect/Video",
      "Applies cvSobel OpenCV function to the image",
      "Thiago Santos<thiago.sousa.santos@collabora.co.uk>");
}

static void
gst_cv_sobel_init (GstCvSobel * filter)
{
  filter->x_order = DEFAULT_X_ORDER;
  filter->y_order = DEFAULT_Y_ORDER;
  filter->aperture_size = DEFAULT_APERTURE_SIZE;

  gst_base_transform_set_in_place (GST_BASE_TRANSFORM (filter), FALSE);
}

static GstCaps *
gst_cv_sobel_transform_caps (GstBaseTransform * trans, GstPadDirection dir,
    GstCaps * caps, GstCaps * filter)
{
  GstCaps *to, *ret;
  GstCaps *templ;
  GstStructure *structure;
  GstPad *other;
  gint i;

  to = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    const GValue *v;
    GValue list = { 0, };
    GValue val = { 0, };

    structure = gst_structure_copy (gst_caps_get_structure (caps, i));

    g_value_init (&list, GST_TYPE_LIST);

    g_value_init (&val, G_TYPE_STRING);
    g_value_set_string (&val, "GRAY8");
    gst_value_list_append_value (&list, &val);
    g_value_unset (&val);

    g_value_init (&val, G_TYPE_STRING);
#if G_BYTE_ORDER == G_BIG_ENDIAN
    g_value_set_string (&val, "GRAY16_BE");
#else
    g_value_set_string (&val, "GRAY16_LE");
#endif
    gst_value_list_append_value (&list, &val);
    g_value_unset (&val);

    v = gst_structure_get_value (structure, "format");

    gst_value_list_merge (&val, v, &list);
    gst_structure_set_value (structure, "format", &val);
    g_value_unset (&val);
    g_value_unset (&list);

    gst_structure_remove_field (structure, "colorimetry");
    gst_structure_remove_field (structure, "chroma-site");

    gst_caps_append_structure (to, structure);

  }
  /* filter against set allowed caps on the pad */
  other = (dir == GST_PAD_SINK) ? trans->srcpad : trans->sinkpad;
  templ = gst_pad_get_pad_template_caps (other);
  ret = gst_caps_intersect (to, templ);
  gst_caps_unref (to);
  gst_caps_unref (templ);

  if (ret && filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  return ret;
}

static void
gst_cv_sobel_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstCvSobel *filter = GST_CV_SOBEL (object);

  switch (prop_id) {
    case PROP_X_ORDER:
      filter->x_order = g_value_get_int (value);
      break;
    case PROP_Y_ORDER:
      filter->y_order = g_value_get_int (value);
      break;
    case PROP_APERTURE_SIZE:{
      gint as = g_value_get_int (value);

      if (as % 2 != 1) {
        GST_WARNING_OBJECT (filter, "Invalid value %d for aperture size", as);
      } else
        filter->aperture_size = g_value_get_int (value);
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cv_sobel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstCvSobel *filter = GST_CV_SOBEL (object);

  switch (prop_id) {
    case PROP_X_ORDER:
      g_value_set_int (value, filter->x_order);
      break;
    case PROP_Y_ORDER:
      g_value_set_int (value, filter->y_order);
      break;
    case PROP_APERTURE_SIZE:
      g_value_set_int (value, filter->aperture_size);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstFlowReturn
gst_cv_sobel_transform (GstOpencvVideoFilter * base, GstBuffer * buf,
    IplImage * img, GstBuffer * outbuf, IplImage * outimg)
{
  GstCvSobel *filter = GST_CV_SOBEL (base);

  cvSobel (img, outimg, filter->x_order, filter->y_order,
      filter->aperture_size);

  return GST_FLOW_OK;
}

gboolean
gst_cv_sobel_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_cv_sobel_debug, "cvsobel", 0, "cvsobel");

  return gst_element_register (plugin, "cvsobel", GST_RANK_NONE,
      GST_TYPE_CV_SOBEL);
}
