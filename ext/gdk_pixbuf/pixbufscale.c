/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2004> Jan Schmidt <thaytan@mad.scientist.com>
 * Copyright (C) <2004> Tim-Philipp Mueller <t.i.m@orange.net>
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
#include "config.h"
#endif
#include <pixbufscale.h>
#include <gst/video/video.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#define ROUND_UP_2(x)  (((x)+1)&~1)
#define ROUND_UP_4(x)  (((x)+3)&~3)
#define ROUND_UP_8(x)  (((x)+7)&~7)

/* These match the ones gstvideoconvert uses (Tim) */
#define GST_RGB24_ROWSTRIDE(width)    (ROUND_UP_4 ((width)*3))
#define GST_RGB24_SIZE(width,height)  ((height)*GST_RGB24_ROWSTRIDE(width))


GST_DEBUG_CATEGORY_STATIC (pixbufscale_debug);
#define GST_CAT_DEFAULT pixbufscale_debug

/* GstPixbufScale signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_METHOD
      /* FILL ME */
};

static GstStaticPadTemplate gst_pixbufscale_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

static GstStaticPadTemplate gst_pixbufscale_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("RGB"))
    );

#define GST_TYPE_PIXBUFSCALE_METHOD (gst_pixbufscale_method_get_type())
static GType
gst_pixbufscale_method_get_type (void)
{
  static GType pixbufscale_method_type = 0;
  static const GEnumValue pixbufscale_methods[] = {
    {GST_PIXBUFSCALE_NEAREST, "0", "Nearest Neighbour"},
    {GST_PIXBUFSCALE_TILES, "1", "Tiles"},
    {GST_PIXBUFSCALE_BILINEAR, "2", "Bilinear"},
    {GST_PIXBUFSCALE_HYPER, "3", "Hyper"},
    {0, NULL, NULL},
  };

  if (!pixbufscale_method_type) {
    pixbufscale_method_type =
        g_enum_register_static ("GstPixbufScaleMethod", pixbufscale_methods);
  }
  return pixbufscale_method_type;
}

static void gst_pixbufscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pixbufscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static GstCaps *gst_pixbufscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter);
static gboolean gst_pixbufscale_set_info (GstVideoFilter * filter,
    GstCaps * in, GstVideoInfo * in_info, GstCaps * out,
    GstVideoInfo * out_info);
static GstCaps *gst_pixbufscale_fixate_caps (GstBaseTransform * base,
    GstPadDirection direction, GstCaps * caps, GstCaps * othercaps);
static GstFlowReturn gst_pixbufscale_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in, GstVideoFrame * out);
static gboolean gst_pixbufscale_handle_src_event (GstBaseTransform * trans,
    GstEvent * event);

#define gst_pixbufscale_parent_class parent_class
G_DEFINE_TYPE (GstPixbufScale, gst_pixbufscale, GST_TYPE_VIDEO_FILTER);

static void
gst_pixbufscale_class_init (GstPixbufScaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstBaseTransformClass *trans_class;
  GstVideoFilterClass *filter_class;

  gobject_class = (GObjectClass *) klass;
  element_class = (GstElementClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;
  filter_class = (GstVideoFilterClass *) klass;

  gst_element_class_set_static_metadata (element_class,
      "GdkPixbuf image scaler", "Filter/Effect/Video", "Resizes video",
      "Jan Schmidt <thaytan@mad.scientist.com>, "
      "Wim Taymans <wim.taymans@chello.be>, "
      "Renato Filho <renato.filho@indt.org.br>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pixbufscale_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_pixbufscale_sink_template));

  gobject_class->set_property = gst_pixbufscale_set_property;
  gobject_class->get_property = gst_pixbufscale_get_property;

  g_object_class_install_property (gobject_class,
      ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_PIXBUFSCALE_METHOD, GST_PIXBUFSCALE_BILINEAR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_pixbufscale_transform_caps);
  trans_class->fixate_caps = GST_DEBUG_FUNCPTR (gst_pixbufscale_fixate_caps);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_pixbufscale_handle_src_event);
  trans_class->passthrough_on_same_caps = TRUE;

  filter_class->set_info = GST_DEBUG_FUNCPTR (gst_pixbufscale_set_info);
  filter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_pixbufscale_transform_frame);

  parent_class = g_type_class_peek_parent (klass);
}

static void
gst_pixbufscale_init (GstPixbufScale * pixbufscale)
{
  GST_DEBUG_OBJECT (pixbufscale, "_init");

  pixbufscale->method = GST_PIXBUFSCALE_TILES;
  pixbufscale->gdk_method = GDK_INTERP_TILES;
}

static void
gst_pixbufscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPixbufScale *src;

  g_return_if_fail (GST_IS_PIXBUFSCALE (object));
  src = GST_PIXBUFSCALE (object);

  switch (prop_id) {
    case ARG_METHOD:
      src->method = g_value_get_enum (value);
      switch (src->method) {
        case GST_PIXBUFSCALE_NEAREST:
          src->gdk_method = GDK_INTERP_NEAREST;
          break;
        case GST_PIXBUFSCALE_TILES:
          src->gdk_method = GDK_INTERP_TILES;
          break;
        case GST_PIXBUFSCALE_BILINEAR:
          src->gdk_method = GDK_INTERP_BILINEAR;
          break;
        case GST_PIXBUFSCALE_HYPER:
          src->gdk_method = GDK_INTERP_HYPER;
          break;
      }
      break;
    default:
      break;
  }
}

static void
gst_pixbufscale_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstPixbufScale *src;

  g_return_if_fail (GST_IS_PIXBUFSCALE (object));
  src = GST_PIXBUFSCALE (object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, src->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static GstCaps *
gst_pixbufscale_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstCaps *ret;
  int i;

  ret = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *structure = gst_caps_get_structure (ret, i);

    gst_structure_set (structure,
        "width", GST_TYPE_INT_RANGE, 16, 4096,
        "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
    gst_structure_remove_field (structure, "pixel-aspect-ratio");
  }

  if (filter) {
    GstCaps *intersection;

    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
  }

  GST_DEBUG_OBJECT (trans, "returning caps: %" GST_PTR_FORMAT, ret);
  return ret;
}

static gboolean
gst_pixbufscale_set_info (GstVideoFilter * filter, GstCaps * in,
    GstVideoInfo * in_info, GstCaps * out, GstVideoInfo * out_info)
{
  if (in_info->width == out_info->width && in_info->height == out_info->height) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), TRUE);
  } else {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (filter), FALSE);
  }

  GST_DEBUG_OBJECT (filter, "from=%dx%d, size %" G_GSIZE_FORMAT
      " -> to=%dx%d, size %" G_GSIZE_FORMAT,
      in_info->width, in_info->height, in_info->size,
      out_info->width, out_info->height, out_info->size);

  return TRUE;
}

static GstCaps *
gst_pixbufscale_fixate_caps (GstBaseTransform * base, GstPadDirection direction,
    GstCaps * caps, GstCaps * othercaps)
{
  GstStructure *ins, *outs;
  const GValue *from_par, *to_par;

  othercaps = gst_caps_truncate (othercaps);
  othercaps = gst_caps_make_writable (othercaps);

  GST_DEBUG_OBJECT (base, "trying to fixate othercaps %" GST_PTR_FORMAT
      " based on caps %" GST_PTR_FORMAT, othercaps, caps);

  ins = gst_caps_get_structure (caps, 0);
  outs = gst_caps_get_structure (othercaps, 0);

  from_par = gst_structure_get_value (ins, "pixel-aspect-ratio");
  to_par = gst_structure_get_value (outs, "pixel-aspect-ratio");

  if (from_par && to_par) {
    GValue to_ratio = { 0, };   /* w/h of output video */
    int from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d;
    int count = 0, w = 0, h = 0, num, den;

    /* if both width and height are already fixed, we can't do anything
     *      * about it anymore */
    if (gst_structure_get_int (outs, "width", &w))
      ++count;
    if (gst_structure_get_int (outs, "height", &h))
      ++count;
    if (count == 2) {
      GST_DEBUG_OBJECT (base, "dimensions already set to %dx%d, not fixating",
          w, h);
      goto done;
    }

    gst_structure_get_int (ins, "width", &from_w);
    gst_structure_get_int (ins, "height", &from_h);
    from_par_n = gst_value_get_fraction_numerator (from_par);
    from_par_d = gst_value_get_fraction_denominator (from_par);
    to_par_n = gst_value_get_fraction_numerator (to_par);
    to_par_d = gst_value_get_fraction_denominator (to_par);

    g_value_init (&to_ratio, GST_TYPE_FRACTION);
    gst_value_set_fraction (&to_ratio, from_w * from_par_n * to_par_d,
        from_h * from_par_d * to_par_n);
    num = gst_value_get_fraction_numerator (&to_ratio);
    den = gst_value_get_fraction_denominator (&to_ratio);
    GST_DEBUG_OBJECT (base,
        "scaling input with %dx%d and PAR %d/%d to output PAR %d/%d",
        from_w, from_h, from_par_n, from_par_d, to_par_n, to_par_d);
    GST_DEBUG_OBJECT (base,
        "resulting output should respect ratio of %d/%d", num, den);

    /* now find a width x height that respects this display ratio.
     *      * prefer those that have one of w/h the same as the incoming video
     *           * using wd / hd = num / den */

    /* start with same height, because of interlaced video */
    /* check hd / den is an integer scale factor, and scale wd with the PAR */
    if (from_h % den == 0) {
      GST_DEBUG_OBJECT (base, "keeping video height");
      h = from_h;
      w = h * num / den;
    } else if (from_w % num == 0) {
      GST_DEBUG_OBJECT (base, "keeping video width");
      w = from_w;
      h = w * den / num;
    } else {
      GST_DEBUG_OBJECT (base, "approximating but keeping video height");
      h = from_h;
      w = h * num / den;
    }
    GST_DEBUG_OBJECT (base, "scaling to %dx%d", w, h);
    /* now fixate */
    gst_structure_fixate_field_nearest_int (outs, "width", w);
    gst_structure_fixate_field_nearest_int (outs, "height", h);
  } else {
    gint width, height;

    if (gst_structure_get_int (ins, "width", &width)) {
      if (gst_structure_has_field (outs, "width")) {
        gst_structure_fixate_field_nearest_int (outs, "width", width);
      }
    }
    if (gst_structure_get_int (ins, "height", &height)) {
      if (gst_structure_has_field (outs, "height")) {
        gst_structure_fixate_field_nearest_int (outs, "height", height);
      }
    }
  }

done:
  GST_DEBUG_OBJECT (base, "fixated othercaps to %" GST_PTR_FORMAT, othercaps);

  return othercaps;
}

static GstFlowReturn
gst_pixbufscale_transform_frame (GstVideoFilter * filter,
    GstVideoFrame * in, GstVideoFrame * out)
{
  GstPixbufScale *scale;
  GdkPixbuf *src_pixbuf, *dest_pixbuf;

  scale = GST_PIXBUFSCALE (filter);

  src_pixbuf =
      gdk_pixbuf_new_from_data (GST_VIDEO_FRAME_COMP_DATA (in, 0),
      GDK_COLORSPACE_RGB, FALSE, 8, GST_VIDEO_FRAME_WIDTH (in),
      GST_VIDEO_FRAME_HEIGHT (in),
      GST_VIDEO_FRAME_COMP_STRIDE (in, 0), NULL, NULL);

  dest_pixbuf =
      gdk_pixbuf_new_from_data (GST_VIDEO_FRAME_COMP_DATA (out, 0),
      GDK_COLORSPACE_RGB, FALSE, 8, GST_VIDEO_FRAME_WIDTH (out),
      GST_VIDEO_FRAME_HEIGHT (out),
      GST_VIDEO_FRAME_COMP_STRIDE (out, 0), NULL, NULL);

  gdk_pixbuf_scale (src_pixbuf, dest_pixbuf, 0, 0,
      GST_VIDEO_FRAME_WIDTH (out),
      GST_VIDEO_FRAME_HEIGHT (out), 0, 0,
      (double) GST_VIDEO_FRAME_WIDTH (out) / GST_VIDEO_FRAME_WIDTH (in),
      (double) GST_VIDEO_FRAME_HEIGHT (out) / GST_VIDEO_FRAME_HEIGHT (in),
      scale->gdk_method);

  g_object_unref (src_pixbuf);
  g_object_unref (dest_pixbuf);

  return GST_FLOW_OK;
}

static gboolean
gst_pixbufscale_handle_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstPixbufScale *pixbufscale;
  gboolean ret;
  double a;
  GstStructure *structure;

  pixbufscale = GST_PIXBUFSCALE (trans);

  GST_DEBUG_OBJECT (pixbufscale, "handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &a)) {
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
            a * pixbufscale->from_width / pixbufscale->to_width, NULL);
      }
      if (gst_structure_get_double (structure, "pointer_y", &a)) {
        gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
            a * pixbufscale->from_height / pixbufscale->to_height, NULL);
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

gboolean
pixbufscale_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "gdkpixbufscale", GST_RANK_NONE,
          GST_TYPE_PIXBUFSCALE))
    return FALSE;

  GST_DEBUG_CATEGORY_INIT (pixbufscale_debug, "gdkpixbufscale", 0,
      "pixbufscale element");

  return TRUE;
}
