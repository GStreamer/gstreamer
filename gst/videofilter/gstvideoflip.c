/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
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

/*
 * This file was (probably) generated from gstvideoflip.c,
 * gstvideoflip.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoflip.h"

#include <gst/video/video.h>

/* GstVideoflip signals and args */
enum
{
  ARG_0,
  ARG_METHOD
      /* FILL ME */
};

GST_DEBUG_CATEGORY (videoflip_debug);
#define GST_CAT_DEFAULT videoflip_debug

static GstElementDetails videoflip_details =
GST_ELEMENT_DETAILS ("Video Flipper",
    "Filter/Effect/Video",
    "Flips and rotates video",
    "David Schleef <ds@schleef.org>");

static GstStaticPadTemplate gst_videoflip_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }"))
    );

static GstStaticPadTemplate gst_videoflip_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }"))
    );

static GstVideofilterClass *parent_class = NULL;

#define GST_TYPE_VIDEOFLIP_METHOD (gst_videoflip_method_get_type())

static GType
gst_videoflip_method_get_type (void)
{
  static GType videoflip_method_type = 0;
  static GEnumValue videoflip_methods[] = {
    {GST_VIDEOFLIP_METHOD_IDENTITY, "Identity (no rotation)", "none"},
    {GST_VIDEOFLIP_METHOD_90R, "Rotate clockwise 90 degrees", "clockwise"},
    {GST_VIDEOFLIP_METHOD_180, "Rotate 180 degrees", "rotate-180"},
    {GST_VIDEOFLIP_METHOD_90L, "Rotate counter-clockwise 90 degrees",
        "counterclockwise"},
    {GST_VIDEOFLIP_METHOD_HORIZ, "Flip horizontally", "horizontal-flip"},
    {GST_VIDEOFLIP_METHOD_VERT, "Flip vertically", "vertical-flip"},
    {GST_VIDEOFLIP_METHOD_TRANS,
        "Flip across upper left/lower right diagonal", "upper-left-diagonal"},
    {GST_VIDEOFLIP_METHOD_OTHER,
        "Flip across upper right/lower left diagonal", "upper-right-diagonal"},
    {0, NULL, NULL},
  };

  if (!videoflip_method_type) {
    videoflip_method_type = g_enum_register_static ("GstVideoflipMethod",
        videoflip_methods);
  }
  return videoflip_method_type;
}

static gboolean
gst_videoflip_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoflip *vf;
  GstStructure *in_s, *out_s;
  gboolean ret = FALSE;

  vf = GST_VIDEOFLIP (btrans);

  in_s = gst_caps_get_structure (incaps, 0);
  out_s = gst_caps_get_structure (outcaps, 0);

  if (gst_structure_get_int (in_s, "width", &vf->from_width) &&
      gst_structure_get_int (in_s, "height", &vf->from_height) &&
      gst_structure_get_int (out_s, "width", &vf->to_width) &&
      gst_structure_get_int (out_s, "height", &vf->to_height)) {
    /* Check that they are correct */
    switch (vf->method) {
      case GST_VIDEOFLIP_METHOD_90R:
      case GST_VIDEOFLIP_METHOD_90L:
      case GST_VIDEOFLIP_METHOD_TRANS:
      case GST_VIDEOFLIP_METHOD_OTHER:
        if ((vf->from_width != vf->to_height) ||
            (vf->from_height != vf->to_width)) {
          GST_DEBUG_OBJECT (vf, "we are inverting width and height but caps "
              "are not correct : %dx%d to %dx%d", vf->from_width,
              vf->from_height, vf->to_width, vf->to_height);
          goto beach;
        }
        break;
      case GST_VIDEOFLIP_METHOD_IDENTITY:

        break;
      case GST_VIDEOFLIP_METHOD_180:
      case GST_VIDEOFLIP_METHOD_HORIZ:
      case GST_VIDEOFLIP_METHOD_VERT:
        if ((vf->from_width != vf->to_width) ||
            (vf->from_height != vf->to_height)) {
          GST_DEBUG_OBJECT (vf, "we are keeping width and height but caps "
              "are not correct : %dx%d to %dx%d", vf->from_width,
              vf->from_height, vf->to_width, vf->to_height);
          goto beach;
        }
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  ret = TRUE;

beach:
  return ret;
}

static GstCaps *
gst_videoflip_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstVideoflip *videoflip;
  GstCaps *ret;
  gint width, height, i;

  videoflip = GST_VIDEOFLIP (trans);

  ret = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *structure = gst_caps_get_structure (ret, i);

    if (gst_structure_get_int (structure, "width", &width) &&
        gst_structure_get_int (structure, "height", &height)) {

      switch (videoflip->method) {
        case GST_VIDEOFLIP_METHOD_90R:
        case GST_VIDEOFLIP_METHOD_90L:
        case GST_VIDEOFLIP_METHOD_TRANS:
        case GST_VIDEOFLIP_METHOD_OTHER:
          gst_structure_set (structure, "width", G_TYPE_INT, height,
              "height", G_TYPE_INT, width, NULL);
          break;
        case GST_VIDEOFLIP_METHOD_IDENTITY:
        case GST_VIDEOFLIP_METHOD_180:
        case GST_VIDEOFLIP_METHOD_HORIZ:
        case GST_VIDEOFLIP_METHOD_VERT:
          gst_structure_set (structure, "width", G_TYPE_INT, width,
              "height", G_TYPE_INT, height, NULL);
          break;
        default:
          g_assert_not_reached ();
          break;
      }
    }
  }

  GST_DEBUG_OBJECT (videoflip, "transformed %" GST_PTR_FORMAT " to %"
      GST_PTR_FORMAT, caps, ret);

  return ret;
}

/* Useful macros */
#define GST_VIDEO_I420_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define GST_VIDEO_I420_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define GST_VIDEO_I420_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(GST_VIDEO_I420_Y_ROWSTRIDE(width)))/2)

#define GST_VIDEO_I420_Y_OFFSET(w,h) (0)
#define GST_VIDEO_I420_U_OFFSET(w,h) (GST_VIDEO_I420_Y_OFFSET(w,h)+(GST_VIDEO_I420_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define GST_VIDEO_I420_V_OFFSET(w,h) (GST_VIDEO_I420_U_OFFSET(w,h)+(GST_VIDEO_I420_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define GST_VIDEO_I420_SIZE(w,h)     (GST_VIDEO_I420_V_OFFSET(w,h)+(GST_VIDEO_I420_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

static gboolean
gst_videoflip_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstVideoflip *videoflip;
  GstStructure *structure;
  gboolean ret = FALSE;
  gint width, height;

  videoflip = GST_VIDEOFLIP (btrans);

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_get_int (structure, "width", &width) &&
      gst_structure_get_int (structure, "height", &height)) {
    *size = GST_VIDEO_I420_SIZE (width, height);
    ret = TRUE;
    GST_DEBUG_OBJECT (videoflip, "our frame size is %d bytes (%dx%d)", *size,
        width, height);
  }

  return ret;
}

static GstFlowReturn
gst_videoflip_flip (GstVideoflip * videoflip, unsigned char *dest,
    unsigned char *src, int sw, int sh, int dw, int dh)
{
  GstFlowReturn ret = GST_FLOW_OK;
  int x, y;

  switch (videoflip->method) {
    case GST_VIDEOFLIP_METHOD_90R:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - x) * sw + y];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_90L:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[x * sw + (sw - 1 - y)];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_180:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - y) * sw + (sw - 1 - x)];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_HORIZ:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[y * sw + (sw - 1 - x)];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_VERT:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - y) * sw + x];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_TRANS:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[x * sw + y];
        }
      }
      break;
    case GST_VIDEOFLIP_METHOD_OTHER:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          dest[y * dw + x] = src[(sh - 1 - x) * sw + (sw - 1 - y)];
        }
      }
      break;
    default:
      ret = GST_FLOW_ERROR;
      break;
  }

  return ret;
}

static GstFlowReturn
gst_videoflip_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoflip *videoflip;
  gpointer dest, src;
  int sw, sh, dw, dh;
  GstFlowReturn ret = GST_FLOW_OK;

  videoflip = GST_VIDEOFLIP (trans);

  gst_buffer_stamp (out, in);

  src = GST_BUFFER_DATA (in);
  dest = GST_BUFFER_DATA (out);
  sw = videoflip->from_width;
  sh = videoflip->from_height;
  dw = videoflip->to_width;
  dh = videoflip->to_height;

  GST_LOG_OBJECT (videoflip, "videoflip: scaling planar 4:1:1 %dx%d to %dx%d",
      sw, sh, dw, dh);

  ret = gst_videoflip_flip (videoflip, dest, src, sw, sh, dw, dh);
  if (ret != GST_FLOW_OK)
    goto beach;

  src += sw * sh;
  dest += dw * dh;

  dh = dh >> 1;
  dw = dw >> 1;
  sh = sh >> 1;
  sw = sw >> 1;

  ret = gst_videoflip_flip (videoflip, dest, src, sw, sh, dw, dh);
  if (ret != GST_FLOW_OK)
    goto beach;

  src += sw * sh;
  dest += dw * dh;

  ret = gst_videoflip_flip (videoflip, dest, src, sw, sh, dw, dh);

beach:
  return ret;
}

static gboolean
gst_videoflip_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstVideoflip *vf;
  gboolean ret;
  gdouble x, y;
  GstStructure *structure;

  vf = GST_VIDEOFLIP (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (vf, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &x) &&
          gst_structure_get_double (structure, "pointer_y", &y)) {
        switch (vf->method) {
          case GST_VIDEOFLIP_METHOD_90R:
          case GST_VIDEOFLIP_METHOD_OTHER:
            x = y;
            y = vf->to_width - x;
            break;
          case GST_VIDEOFLIP_METHOD_90L:
          case GST_VIDEOFLIP_METHOD_TRANS:
            x = vf->to_height - y;
            y = x;
            break;
          case GST_VIDEOFLIP_METHOD_180:
            x = vf->to_width - x;
            y = vf->to_height - y;
            break;
          case GST_VIDEOFLIP_METHOD_HORIZ:
            x = vf->to_width - x;
            y = y;
            break;
          case GST_VIDEOFLIP_METHOD_VERT:
            x = x;
            y = vf->to_height - y;
            break;
          default:
            x = x;
            y = y;
            break;
        }
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, x,
            "pointer_y", G_TYPE_DOUBLE, y, NULL);
      }
      break;
    default:
      break;
  }

  ret = gst_pad_event_default (pad, event);

  gst_object_unref (vf);

  return ret;
}

static void
gst_videoflip_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoflip *videoflip;
  GstVideofilter *videofilter;

  g_return_if_fail (GST_IS_VIDEOFLIP (object));
  videoflip = GST_VIDEOFLIP (object);
  videofilter = GST_VIDEOFILTER (object);

  switch (prop_id) {
    case ARG_METHOD:
    {
      GstVideoflipMethod method;

      method = g_value_get_enum (value);
      if (method != videoflip->method) {
        GstBaseTransform *btrans = GST_BASE_TRANSFORM (videoflip);

        g_mutex_lock (btrans->transform_lock);
        gst_pad_set_caps (btrans->sinkpad, NULL);
        gst_pad_set_caps (btrans->srcpad, NULL);
        g_mutex_unlock (btrans->transform_lock);
        videoflip->method = method;
      }
    }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videoflip_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoflip *videoflip;

  g_return_if_fail (GST_IS_VIDEOFLIP (object));
  videoflip = GST_VIDEOFLIP (object);

  switch (prop_id) {
    case ARG_METHOD:
      g_value_set_enum (value, videoflip->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_videoflip_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &videoflip_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videoflip_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_videoflip_src_template));
}

static void
gst_videoflip_class_init (gpointer klass, gpointer class_data)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  trans_class = (GstBaseTransformClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_videoflip_set_property;
  gobject_class->get_property = gst_videoflip_get_property;

  g_object_class_install_property (gobject_class, ARG_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_VIDEOFLIP_METHOD, GST_VIDEOFLIP_METHOD_90R,
          G_PARAM_READWRITE));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_videoflip_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_videoflip_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_videoflip_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_videoflip_transform);
}

static void
gst_videoflip_init (GTypeInstance * instance, gpointer g_class)
{
  GstVideoflip *videoflip = GST_VIDEOFLIP (instance);
  GstBaseTransform *btrans = GST_BASE_TRANSFORM (instance);

  GST_DEBUG_OBJECT (videoflip, "gst_videoflip_init");

  videoflip->method = GST_VIDEOFLIP_METHOD_90R;

  gst_pad_set_event_function (btrans->srcpad,
      GST_DEBUG_FUNCPTR (gst_videoflip_handle_src_event));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (videoflip_debug, "videoflip", 0, "videoflip");

  return gst_element_register (plugin, "videoflip", GST_RANK_NONE,
      GST_TYPE_VIDEOFLIP);
}

GType
gst_videoflip_get_type (void)
{
  static GType videoflip_type = 0;

  if (!videoflip_type) {
    static const GTypeInfo videoflip_info = {
      sizeof (GstVideoflipClass),
      gst_videoflip_base_init,
      NULL,
      gst_videoflip_class_init,
      NULL,
      NULL,
      sizeof (GstVideoflip),
      0,
      gst_videoflip_init,
    };

    videoflip_type = g_type_register_static (GST_TYPE_VIDEOFILTER,
        "GstVideoflip", &videoflip_info, 0);
  }
  return videoflip_type;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "videoflip",
    "Flips and rotates video",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
