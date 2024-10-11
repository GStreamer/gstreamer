/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) <2010> Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) <2011> Youness Alaoui <youness.alaoui@collabora.co.uk>
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

/*
 * This file was (probably) generated from gstvideoflip.c,
 * gstvideoflip.c,v 1.7 2003/11/08 02:48:59 dschleef Exp
 */
/**
 * SECTION:element-videoflip
 * @title: videoflip
 *
 * Flips and rotates video.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 videotestsrc ! videoflip method=clockwise ! videoconvert ! ximagesink
 * ]| This pipeline flips the test image 90 degrees clockwise.
 *
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoflip.h"

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>

/* GstVideoFlip properties */
enum
{
  PROP_0,
  PROP_METHOD,
  PROP_VIDEO_DIRECTION
      /* FILL ME */
};

#define PROP_METHOD_DEFAULT GST_VIDEO_FLIP_METHOD_IDENTITY

GST_DEBUG_CATEGORY_STATIC (video_flip_debug);
#define GST_CAT_DEFAULT video_flip_debug

static GstStaticPadTemplate gst_video_flip_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ AYUV, "
            "ARGB, BGRA, ABGR, RGBA, Y444, xRGB, RGBx, xBGR, BGRx, "
            "RGB, BGR, I420, YV12, IYUV, YUY2, UYVY, YVYU, NV12, NV21,"
            "GRAY8, GRAY16_BE, GRAY16_LE, I420_10LE, I420_10BE, I420_12LE, I420_12BE, "
            "I422_10LE, I422_10BE, I422_12LE, I422_12BE, Y444_10LE, Y444_10BE, Y444_12LE, Y444_12BE }"))
    );

static GstStaticPadTemplate gst_video_flip_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ AYUV, "
            "ARGB, BGRA, ABGR, RGBA, Y444, xRGB, RGBx, xBGR, BGRx, "
            "RGB, BGR, I420, YV12, IYUV, YUY2, UYVY, YVYU, NV12, NV21,"
            "GRAY8, GRAY16_BE, GRAY16_LE, I420_10LE, I420_10BE, I420_12LE, I420_12BE, "
            "I422_10LE, I422_10BE, I422_12LE, I422_12BE, Y444_10LE, Y444_10BE, Y444_12LE, Y444_12BE }"))
    );

#define GST_TYPE_VIDEO_FLIP_METHOD (gst_video_flip_method_get_type())

static const GEnumValue video_flip_methods[] = {
  {GST_VIDEO_FLIP_METHOD_IDENTITY, "Identity (no rotation)", "none"},
  {GST_VIDEO_FLIP_METHOD_90R, "Rotate clockwise 90 degrees", "clockwise"},
  {GST_VIDEO_FLIP_METHOD_180, "Rotate 180 degrees", "rotate-180"},
  {GST_VIDEO_FLIP_METHOD_90L, "Rotate counter-clockwise 90 degrees",
      "counterclockwise"},
  {GST_VIDEO_FLIP_METHOD_HORIZ, "Flip horizontally", "horizontal-flip"},
  {GST_VIDEO_FLIP_METHOD_VERT, "Flip vertically", "vertical-flip"},
  {GST_VIDEO_FLIP_METHOD_TRANS,
      "Flip across upper left/lower right diagonal", "upper-left-diagonal"},
  {GST_VIDEO_FLIP_METHOD_OTHER,
      "Flip across upper right/lower left diagonal", "upper-right-diagonal"},
  {GST_VIDEO_FLIP_METHOD_AUTO,
      "Select flip method based on image-orientation tag", "automatic"},
  {0, NULL, NULL},
};

static GType
gst_video_flip_method_get_type (void)
{
  static GType video_flip_method_type = 0;

  if (!video_flip_method_type) {
    video_flip_method_type = g_enum_register_static ("GstVideoFlipMethod",
        video_flip_methods);
  }
  return video_flip_method_type;
}

static void
gst_video_flip_video_direction_interface_init (GstVideoDirectionInterface *
    iface)
{
  /* We implement the video-direction property */
}

#define gst_video_flip_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVideoFlip, gst_video_flip, GST_TYPE_VIDEO_FILTER,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_DIRECTION,
        gst_video_flip_video_direction_interface_init));
GST_ELEMENT_REGISTER_DEFINE (videoflip, "videoflip", GST_RANK_NONE,
    GST_TYPE_VIDEO_FLIP);

static GstCaps *
gst_video_flip_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps, GstCaps * filter)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (trans);
  GstCaps *ret;
  gint width, height, i;

  ret = gst_caps_copy (caps);

  GST_OBJECT_LOCK (videoflip);

  if (videoflip->change_configuring_method) {
    GEnumValue *configuring_method_enum, *method_enum;
    GEnumClass *enum_class =
        g_type_class_ref (GST_TYPE_VIDEO_ORIENTATION_METHOD);

    configuring_method_enum =
        g_enum_get_value (enum_class, videoflip->configuring_method);
    method_enum = g_enum_get_value (enum_class, videoflip->proposed_method);
    GST_LOG_OBJECT (videoflip,
        "Changing configuring method from %s to proposed %s",
        configuring_method_enum ? configuring_method_enum->value_nick : "(nil)",
        method_enum ? method_enum->value_nick : "(nil)");
    g_type_class_unref (enum_class);

    videoflip->configuring_method = videoflip->proposed_method;
  }
  videoflip->change_configuring_method = FALSE;

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *structure = gst_caps_get_structure (ret, i);
    gint par_n, par_d;

    if (gst_structure_get_int (structure, "width", &width) &&
        gst_structure_get_int (structure, "height", &height)) {

      switch (videoflip->configuring_method) {
        case GST_VIDEO_ORIENTATION_90R:
        case GST_VIDEO_ORIENTATION_90L:
        case GST_VIDEO_ORIENTATION_UL_LR:
        case GST_VIDEO_ORIENTATION_UR_LL:
          gst_structure_set (structure, "width", G_TYPE_INT, height,
              "height", G_TYPE_INT, width, NULL);
          if (gst_structure_get_fraction (structure, "pixel-aspect-ratio",
                  &par_n, &par_d)) {
            if (par_n != 1 || par_d != 1) {
              GValue val = { 0, };

              g_value_init (&val, GST_TYPE_FRACTION);
              gst_value_set_fraction (&val, par_d, par_n);
              gst_structure_set_value (structure, "pixel-aspect-ratio", &val);
              g_value_unset (&val);
            }
          }
          break;
        case GST_VIDEO_ORIENTATION_IDENTITY:
        case GST_VIDEO_ORIENTATION_180:
        case GST_VIDEO_ORIENTATION_HORIZ:
        case GST_VIDEO_ORIENTATION_VERT:
          gst_structure_set (structure, "width", G_TYPE_INT, width,
              "height", G_TYPE_INT, height, NULL);
          break;
        case GST_VIDEO_ORIENTATION_CUSTOM:
          GST_WARNING_OBJECT (videoflip, "unsupported custom orientation");
          break;
        default:
          g_assert_not_reached ();
          break;
      }
    }
  }
  GST_OBJECT_UNLOCK (videoflip);

  GST_DEBUG_OBJECT (videoflip, "transformed %" GST_PTR_FORMAT " to %"
      GST_PTR_FORMAT, caps, ret);

  if (filter) {
    GstCaps *intersection;

    GST_DEBUG_OBJECT (videoflip, "Using filter caps %" GST_PTR_FORMAT, filter);
    intersection =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (ret);
    ret = intersection;
    GST_DEBUG_OBJECT (videoflip, "Intersection %" GST_PTR_FORMAT, ret);
  }

  return ret;
}

static void
gst_video_flip_planar_yuv (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y;
  guint8 const *s;
  guint8 *d;
  gint src_y_stride, src_u_stride, src_v_stride;
  gint src_y_height, src_u_height, src_v_height;
  gint src_y_width, src_u_width, src_v_width;
  gint dest_y_stride, dest_u_stride, dest_v_stride;
  gint dest_y_height, dest_u_height, dest_v_height;
  gint dest_y_width, dest_u_width, dest_v_width;

  src_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0);
  src_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 1);
  src_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 2);

  dest_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0);
  dest_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 1);
  dest_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 2);

  src_y_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 0);
  src_u_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 1);
  src_v_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 2);

  dest_y_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 0);
  dest_u_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 1);
  dest_v_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 2);

  src_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 0);
  src_u_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 1);
  src_v_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 2);

  dest_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 0);
  dest_u_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 1);
  dest_v_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 2);

  switch (videoflip->active_method) {
    case GST_VIDEO_ORIENTATION_90R:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + y];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + y];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_90L:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[x * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[x * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[x * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_180:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[y * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[y * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[y * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + x];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + x];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + x];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] = s[x * src_y_stride + y];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] = s[x * src_u_stride + y];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] = s[x * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_IDENTITY:
      gst_video_frame_copy (dest, src);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_video_flip_planar_yuv_16bit (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y;
  guint16 const *s;
  guint16 *d;
  gint src_y_stride, src_u_stride, src_v_stride;
  gint src_y_height, src_u_height, src_v_height;
  gint src_y_width, src_u_width, src_v_width;
  gint dest_y_stride, dest_u_stride, dest_v_stride;
  gint dest_y_height, dest_u_height, dest_v_height;
  gint dest_y_width, dest_u_width, dest_v_width;

  /* Divide strides by 2 because we're operating on guint16's */
  src_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0) / 2;
  src_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 1) / 2;
  src_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 2) / 2;

  dest_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0) / 2;
  dest_u_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 1) / 2;
  dest_v_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 2) / 2;

  src_y_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 0);
  src_u_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 1);
  src_v_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 2);

  dest_y_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 0);
  dest_u_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 1);
  dest_v_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 2);

  src_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 0);
  src_u_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 1);
  src_v_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 2);

  dest_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 0);
  dest_u_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 1);
  dest_v_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 2);

  switch (videoflip->active_method) {
    case GST_VIDEO_ORIENTATION_90R:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + y];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + y];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_90L:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[x * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[x * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[x * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_180:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[y * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[y * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[y * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + x];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + x];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + x];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] = s[x * src_y_stride + y];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] = s[x * src_u_stride + y];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] = s[x * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 2);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 2);
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_IDENTITY:
      gst_video_frame_copy (dest, src);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static inline void
rotate_yuv422_plane (GstVideoFrame * dest, const GstVideoFrame * src,
    gint plane_index, GstVideoOrientationMethod method,
    gboolean is_chroma, gboolean is_le)
{
  gint src_stride, src_height, src_width;
  gint dest_stride, dest_height, dest_width;
  gint x, y;
  guint scale;
  guint16 const *s, *addr;
  guint16 *d;
  guint16 val, val2;

  s = GST_VIDEO_FRAME_PLANE_DATA (src, plane_index);
  d = GST_VIDEO_FRAME_PLANE_DATA (dest, plane_index);

  /* Divide strides by 2 because we're operating on guint16's */
  src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, plane_index) / 2;
  src_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, plane_index);
  src_width = GST_VIDEO_FRAME_COMP_WIDTH (src, plane_index);

  dest_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, plane_index) / 2;
  dest_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, plane_index);
  dest_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, plane_index);

  scale = is_chroma ? 2 : 1;

  switch (method) {
    case GST_VIDEO_ORIENTATION_90R:
      if (is_le) {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s + (src_height - 1 - x * scale) * src_stride + y / scale;
            val = GST_READ_UINT16_LE (addr);

            if (is_chroma && x * 2 + 1 < src_height) {
              addr = s + (src_height - 1 - (x * 2 + 1)) * src_stride + y / 2;
              val2 = GST_READ_UINT16_LE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_LE (d + y * dest_stride + x, val);
          }
        }
      } else {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s + (src_height - 1 - x * scale) * src_stride + y / scale;
            val = GST_READ_UINT16_BE (addr);

            if (is_chroma && x * 2 + 1 < src_height) {
              addr = s + (src_height - 1 - (x * 2 + 1)) * src_stride + y / 2;
              val2 = GST_READ_UINT16_BE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_BE (d + y * dest_stride + x, val);
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_90L:
      if (is_le) {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s + x * scale * src_stride + (src_width - 1 - y / scale);
            val = GST_READ_UINT16_LE (addr);

            if (is_chroma && x * 2 + 1 < src_width) {
              addr = s + (x * 2 + 1) * src_stride + (src_width - 1 - y / 2);
              val2 = GST_READ_UINT16_LE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_LE (d + y * dest_stride + x, val);
          }
        }
      } else {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s + x * scale * src_stride + (src_width - 1 - y / scale);
            val = GST_READ_UINT16_BE (addr);

            if (is_chroma && x * 2 + 1 < src_width) {
              addr = s + (x * 2 + 1) * src_stride + (src_width - 1 - y / 2);
              val2 = GST_READ_UINT16_BE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_BE (d + y * dest_stride + x, val);
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_180:
      for (y = 0; y < dest_height; y++) {
        for (x = 0; x < dest_width; x++) {
          d[y * dest_stride + x] =
              s[(src_height - 1 - y) * src_stride + (src_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      for (y = 0; y < dest_height; y++) {
        for (x = 0; x < dest_width; x++) {
          d[y * dest_stride + x] = s[y * src_stride + (src_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      for (y = 0; y < dest_height; y++) {
        for (x = 0; x < dest_width; x++) {
          d[y * dest_stride + x] = s[(src_height - 1 - y) * src_stride + x];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      if (is_le) {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s + x * scale * src_stride + y / scale;
            val = GST_READ_UINT16_LE (addr);

            if (is_chroma && x * 2 + 1 < src_width) {
              addr = s + (x * 2 + 1) * src_stride + y / 2;
              val2 = GST_READ_UINT16_LE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_LE (d + y * dest_stride + x, val);
          }
        }
      } else {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s + x * scale * src_stride + y / scale;
            val = GST_READ_UINT16_BE (addr);

            if (is_chroma && x * 2 + 1 < src_width) {
              addr = s + (x * 2 + 1) * src_stride + y / 2;
              val2 = GST_READ_UINT16_BE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_BE (d + y * dest_stride + x, val);
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      if (is_le) {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s
                + (src_height - 1 - x * scale) * src_stride
                + (src_width - 1 - y / scale);
            val = GST_READ_UINT16_LE (addr);

            if (is_chroma && x * 2 + 1 < src_width) {
              addr = s
                  + (src_height - 1 - (x * 2 + 1)) * src_stride
                  + (src_width - 1 - y / 2);
              val2 = GST_READ_UINT16_LE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_LE (d + y * dest_stride + x, val);
          }
        }
      } else {
        for (y = 0; y < dest_height; y++) {
          for (x = 0; x < dest_width; x++) {
            addr = s
                + (src_height - 1 - x * scale) * src_stride
                + (src_width - 1 - y / scale);
            val = GST_READ_UINT16_BE (addr);

            if (is_chroma && x * 2 + 1 < src_width) {
              addr = s
                  + (src_height - 1 - (x * 2 + 1)) * src_stride
                  + (src_width - 1 - y / 2);
              val2 = GST_READ_UINT16_BE (addr);
              val = (val + val2) / 2;
            }

            GST_WRITE_UINT16_BE (d + y * dest_stride + x, val);
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_IDENTITY:
      gst_video_frame_copy (dest, src);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_video_flip_planar_yuv_422_16bit (GstVideoFlip * videoflip,
    GstVideoFrame * dest, const GstVideoFrame * src)
{
  gboolean format_is_le;

  /* We need to consider the endianness during transforms
   * which need average chrominance values between two pixels */
  format_is_le = GST_VIDEO_FORMAT_INFO_IS_LE (dest->info.finfo);

  /* Attempt to get the compiler to inline specialized variants of this function 
   * to avoid too much branching due to endianness checks */
  if (format_is_le) {
    rotate_yuv422_plane (dest, src, 0, videoflip->active_method, FALSE, TRUE);
    rotate_yuv422_plane (dest, src, 1, videoflip->active_method, TRUE, TRUE);
    rotate_yuv422_plane (dest, src, 2, videoflip->active_method, TRUE, TRUE);
  } else {
    rotate_yuv422_plane (dest, src, 0, videoflip->active_method, FALSE, FALSE);
    rotate_yuv422_plane (dest, src, 1, videoflip->active_method, TRUE, FALSE);
    rotate_yuv422_plane (dest, src, 2, videoflip->active_method, TRUE, FALSE);
  }
}

static void
gst_video_flip_semi_planar_yuv (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y;
  guint8 const *s;
  guint8 *d;
  gint s_off, d_off;
  gint src_y_stride, src_uv_stride;
  gint src_y_height, src_uv_height;
  gint src_y_width, src_uv_width;
  gint dest_y_stride, dest_uv_stride;
  gint dest_y_height, dest_uv_height;
  gint dest_y_width, dest_uv_width;


  src_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0);
  src_uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 1);

  dest_y_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0);
  dest_uv_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 1);

  src_y_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 0);
  src_uv_width = GST_VIDEO_FRAME_COMP_WIDTH (src, 1);

  dest_y_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 0);
  dest_uv_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, 1);

  src_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 0);
  src_uv_height = GST_VIDEO_FRAME_COMP_HEIGHT (src, 1);

  dest_y_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 0);
  dest_uv_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, 1);

  switch (videoflip->active_method) {
    case GST_VIDEO_ORIENTATION_90R:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + y];
        }
      }
      /* Flip UV */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_uv_height; y++) {
        for (x = 0; x < dest_uv_width; x++) {
          d_off = y * dest_uv_stride + x * 2;
          s_off = (src_uv_height - 1 - x) * src_uv_stride + y * 2;
          d[d_off] = s[s_off];
          d[d_off + 1] = s[s_off + 1];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_90L:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[x * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip UV */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_uv_height; y++) {
        for (x = 0; x < dest_uv_width; x++) {
          d_off = y * dest_uv_stride + x * 2;
          s_off = x * src_uv_stride + (src_uv_width - 1 - y) * 2;
          d[d_off] = s[s_off];
          d[d_off + 1] = s[s_off + 1];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_180:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip UV */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_uv_height; y++) {
        for (x = 0; x < dest_uv_width; x++) {
          d_off = y * dest_uv_stride + x * 2;
          s_off = (src_uv_height - 1 - y) * src_uv_stride + (src_uv_width - 1 -
              x) * 2;
          d[d_off] = s[s_off];
          d[d_off + 1] = s[s_off + 1];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[y * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip UV */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_uv_height; y++) {
        for (x = 0; x < dest_uv_width; x++) {
          d_off = y * dest_uv_stride + x * 2;
          s_off = y * src_uv_stride + (src_uv_width - 1 - x) * 2;
          d[d_off] = s[s_off];
          d[d_off + 1] = s[s_off + 1];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + x];
        }
      }
      /* Flip UV */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_uv_height; y++) {
        for (x = 0; x < dest_uv_width; x++) {
          d_off = y * dest_uv_stride + x * 2;
          s_off = (src_uv_height - 1 - y) * src_uv_stride + x * 2;
          d[d_off] = s[s_off];
          d[d_off + 1] = s[s_off + 1];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] = s[x * src_y_stride + y];
        }
      }
      /* Flip UV */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_uv_height; y++) {
        for (x = 0; x < dest_uv_width; x++) {
          d_off = y * dest_uv_stride + x * 2;
          s_off = x * src_uv_stride + y * 2;
          d[d_off] = s[s_off];
          d[d_off + 1] = s[s_off + 1];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      /* Flip Y */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip UV */
      s = GST_VIDEO_FRAME_PLANE_DATA (src, 1);
      d = GST_VIDEO_FRAME_PLANE_DATA (dest, 1);
      for (y = 0; y < dest_uv_height; y++) {
        for (x = 0; x < dest_uv_width; x++) {
          d_off = y * dest_uv_stride + x * 2;
          s_off = (src_uv_height - 1 - x) * src_uv_stride + (src_uv_width - 1 -
              y) * 2;
          d[d_off] = s[s_off];
          d[d_off + 1] = s[s_off + 1];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_IDENTITY:
      gst_video_frame_copy (dest, src);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_video_flip_packed_simple (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y, z;
  guint8 const *s;
  guint8 *d;
  gint sw = GST_VIDEO_FRAME_WIDTH (src);
  gint sh = GST_VIDEO_FRAME_HEIGHT (src);
  gint dw = GST_VIDEO_FRAME_WIDTH (dest);
  gint dh = GST_VIDEO_FRAME_HEIGHT (dest);
  gint src_stride, dest_stride;
  gint bpp;

  s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);

  src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0);
  dest_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0);
  /* This is only true for non-subsampled formats! */
  bpp = GST_VIDEO_FRAME_COMP_PSTRIDE (src, 0);

  switch (videoflip->active_method) {
    case GST_VIDEO_ORIENTATION_90R:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - x) * src_stride + y * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_90L:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[x * src_stride + (sw - 1 - y) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_180:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - y) * src_stride + (sw - 1 - x) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[y * src_stride + (sw - 1 - x) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - y) * src_stride + x * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] = s[x * src_stride + y * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - x) * src_stride + (sw - 1 - y) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_IDENTITY:
      gst_video_frame_copy (dest, src);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_video_flip_y422 (GstVideoFlip * videoflip, GstVideoFrame * dest,
    const GstVideoFrame * src)
{
  gint x, y;
  guint8 const *s;
  guint8 *d;
  gint sw = GST_VIDEO_FRAME_WIDTH (src);
  gint sh = GST_VIDEO_FRAME_HEIGHT (src);
  gint dw = GST_VIDEO_FRAME_WIDTH (dest);
  gint dh = GST_VIDEO_FRAME_HEIGHT (dest);
  gint src_stride, dest_stride;
  gint bpp;
  gint y_offset;
  gint u_offset;
  gint v_offset;
  gint y_stride;

  s = GST_VIDEO_FRAME_PLANE_DATA (src, 0);
  d = GST_VIDEO_FRAME_PLANE_DATA (dest, 0);

  src_stride = GST_VIDEO_FRAME_PLANE_STRIDE (src, 0);
  dest_stride = GST_VIDEO_FRAME_PLANE_STRIDE (dest, 0);

  y_offset = GST_VIDEO_FRAME_COMP_OFFSET (src, 0);
  u_offset = GST_VIDEO_FRAME_COMP_OFFSET (src, 1);
  v_offset = GST_VIDEO_FRAME_COMP_OFFSET (src, 2);
  y_stride = GST_VIDEO_FRAME_COMP_PSTRIDE (src, 0);
  bpp = y_stride;

  switch (videoflip->active_method) {
    case GST_VIDEO_ORIENTATION_90R:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = (y & ~1);

          u = s[(sh - 1 - x) * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + u_offset]
                + u) >> 1;
          v = s[(sh - 1 - x) * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + v_offset]
                + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - x) * src_stride + y * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - (x + 1)) * src_stride + y * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_90L:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = ((sw - 1 - y) & ~1);

          u = s[x * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(x + 1) * src_stride + even_y * bpp + u_offset] + u) >> 1;
          v = s[x * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(x + 1) * src_stride + even_y * bpp + v_offset] + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[x * src_stride + (sw - 1 - y) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(x + 1) * src_stride + (sw - 1 - y) * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_180:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_x = ((sw - 1 - x) & ~1);

          u = (s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset]) / 2;
          v = (s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset]) / 2;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - y) * src_stride + (sw - 1 - x) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - y) * src_stride + (sw - 1 - (x + 1)) * bpp +
                y_offset];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_HORIZ:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_x = ((sw - 1 - x) & ~1);

          u = (s[y * src_stride + even_x * bpp + u_offset] +
              s[y * src_stride + even_x * bpp + u_offset]) / 2;
          v = (s[y * src_stride + even_x * bpp + v_offset] +
              s[y * src_stride + even_x * bpp + v_offset]) / 2;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[y * src_stride + (sw - 1 - x) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[y * src_stride + (sw - 1 - (x + 1)) * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_VERT:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_x = (x & ~1);

          u = (s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + u_offset]) / 2;
          v = (s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset] +
              s[(sh - 1 - y) * src_stride + even_x * bpp + v_offset]) / 2;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - y) * src_stride + x * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - y) * src_stride + (x + 1) * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UL_LR:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = (y & ~1);

          u = s[x * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(x + 1) * src_stride + even_y * bpp + u_offset] + u) >> 1;
          v = s[x * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(x + 1) * src_stride + even_y * bpp + v_offset] + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[x * src_stride + y * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(x + 1) * src_stride + y * bpp + y_offset];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_UR_LL:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x += 2) {
          guint8 u;
          guint8 v;
          /* u/v must be calculated using the offset of the even column */
          gint even_y = ((sw - 1 - y) & ~1);

          u = s[(sh - 1 - x) * src_stride + even_y * bpp + u_offset];
          if (x + 1 < dw)
            u = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + u_offset]
                + u) >> 1;
          v = s[(sh - 1 - x) * src_stride + even_y * bpp + v_offset];
          if (x + 1 < dw)
            v = (s[(sh - 1 - (x + 1)) * src_stride + even_y * bpp + v_offset]
                + v) >> 1;

          d[y * dest_stride + x * bpp + u_offset] = u;
          d[y * dest_stride + x * bpp + v_offset] = v;
          d[y * dest_stride + x * bpp + y_offset] =
              s[(sh - 1 - x) * src_stride + (sw - 1 - y) * bpp + y_offset];
          if (x + 1 < dw)
            d[y * dest_stride + (x + 1) * bpp + y_offset] =
                s[(sh - 1 - (x + 1)) * src_stride + (sw - 1 - y) * bpp +
                y_offset];
        }
      }
      break;
    case GST_VIDEO_ORIENTATION_IDENTITY:
      gst_video_frame_copy (dest, src);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_video_flip_configure_process (GstVideoFlip * vf)
{
  switch (vf->v_format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
      vf->process = gst_video_flip_planar_yuv;
      break;
    case GST_VIDEO_FORMAT_I420_10LE:
    case GST_VIDEO_FORMAT_I420_10BE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I420_12BE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_12BE:
      vf->process = gst_video_flip_planar_yuv_16bit;
      break;
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_I422_12BE:
      vf->process = gst_video_flip_planar_yuv_422_16bit;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_YVYU:
      vf->process = gst_video_flip_y422;
      break;
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_GRAY8:
    case GST_VIDEO_FORMAT_GRAY16_BE:
    case GST_VIDEO_FORMAT_GRAY16_LE:
      vf->process = gst_video_flip_packed_simple;
      break;
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      vf->process = gst_video_flip_semi_planar_yuv;
      break;
    default:
      break;
  }
}

static gboolean
gst_video_flip_set_info (GstVideoFilter * vfilter, GstCaps * incaps,
    GstVideoInfo * in_info, GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstVideoFlip *vf = GST_VIDEO_FLIP (vfilter);
  gboolean ret = FALSE, need_reconfigure = FALSE;

  vf->process = NULL;

  if (GST_VIDEO_INFO_FORMAT (in_info) != GST_VIDEO_INFO_FORMAT (out_info))
    goto invalid_caps;

  /* Check that they are correct */
  GST_OBJECT_LOCK (vf);
  switch (vf->configuring_method) {
    case GST_VIDEO_ORIENTATION_90R:
    case GST_VIDEO_ORIENTATION_90L:
    case GST_VIDEO_ORIENTATION_UL_LR:
    case GST_VIDEO_ORIENTATION_UR_LL:
      if ((in_info->width != out_info->height) ||
          (in_info->height != out_info->width)) {
        GST_ERROR_OBJECT (vf, "we are inverting width and height but caps "
            "are not correct : %dx%d to %dx%d", in_info->width,
            in_info->height, out_info->width, out_info->height);
        goto beach;
      }
      break;
    case GST_VIDEO_ORIENTATION_IDENTITY:
    case GST_VIDEO_ORIENTATION_180:
    case GST_VIDEO_ORIENTATION_HORIZ:
    case GST_VIDEO_ORIENTATION_VERT:
      if ((in_info->width != out_info->width) ||
          (in_info->height != out_info->height)) {
        GST_ERROR_OBJECT (vf, "we are keeping width and height but caps "
            "are not correct : %dx%d to %dx%d", in_info->width,
            in_info->height, out_info->width, out_info->height);
        goto beach;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  ret = TRUE;

  {
    GEnumValue *active_method_enum, *method_enum;
    GEnumClass *enum_class =
        g_type_class_ref (GST_TYPE_VIDEO_ORIENTATION_METHOD);

    active_method_enum = g_enum_get_value (enum_class, vf->active_method);
    method_enum = g_enum_get_value (enum_class, vf->configuring_method);
    GST_LOG_OBJECT (vf, "Changing active method from %s to configuring %s",
        active_method_enum ? active_method_enum->value_nick : "(nil)",
        method_enum ? method_enum->value_nick : "(nil)");
    g_type_class_unref (enum_class);
  }
  vf->active_method = vf->configuring_method;
  vf->change_configuring_method = TRUE;
  if (vf->active_method != vf->proposed_method)
    need_reconfigure = TRUE;

  vf->v_format = GST_VIDEO_INFO_FORMAT (in_info);
  gst_video_flip_configure_process (vf);

beach:
  GST_OBJECT_UNLOCK (vf);
  if (need_reconfigure) {
    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (vf));
  }

  return ret && (vf->process != NULL);

invalid_caps:
  GST_ERROR_OBJECT (vf, "Invalid caps: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
      incaps, outcaps);
  return FALSE;
}

static void
gst_video_flip_set_method (GstVideoFlip * videoflip,
    GstVideoOrientationMethod method, gboolean from_tag)
{
  GST_OBJECT_LOCK (videoflip);

  if (method == GST_VIDEO_ORIENTATION_CUSTOM) {
    GST_WARNING_OBJECT (videoflip, "unsupported custom orientation");
    GST_OBJECT_UNLOCK (videoflip);
    return;
  }

  /* Store updated method */
  if (from_tag)
    videoflip->tag_method = method;
  else
    videoflip->method = method;

  /* Get the new method */
  if (videoflip->method == GST_VIDEO_ORIENTATION_AUTO)
    method = videoflip->tag_method;
  else
    method = videoflip->method;

  if (method != videoflip->proposed_method) {
    GEnumValue *active_method_enum, *method_enum;
    GstBaseTransform *btrans = GST_BASE_TRANSFORM (videoflip);
    GEnumClass *enum_class =
        g_type_class_ref (GST_TYPE_VIDEO_ORIENTATION_METHOD);

    active_method_enum =
        g_enum_get_value (enum_class, videoflip->active_method);
    method_enum = g_enum_get_value (enum_class, method);
    GST_LOG_OBJECT (videoflip, "Changing method from %s to %s",
        active_method_enum ? active_method_enum->value_nick : "(nil)",
        method_enum ? method_enum->value_nick : "(nil)");
    g_type_class_unref (enum_class);

    videoflip->proposed_method = method;
    videoflip->change_configuring_method = TRUE;

    GST_OBJECT_UNLOCK (videoflip);

    gst_base_transform_set_passthrough (btrans,
        method == GST_VIDEO_ORIENTATION_IDENTITY);
    gst_base_transform_reconfigure_src (btrans);
  } else {
    GST_OBJECT_UNLOCK (videoflip);
  }
}

static void
gst_video_flip_before_transform (GstBaseTransform * trans, GstBuffer * in)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (trans);
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (in);
  stream_time =
      gst_segment_to_stream_time (&trans->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (videoflip, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (GST_OBJECT (videoflip), stream_time);
}

static GstFlowReturn
gst_video_flip_transform_frame (GstVideoFilter * vfilter,
    GstVideoFrame * in_frame, GstVideoFrame * out_frame)
{
  GEnumClass *enum_class;
  GstVideoOrientationMethod active, proposed;
  GEnumValue *active_method_enum;
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (vfilter);

  GST_OBJECT_LOCK (videoflip);
  if (G_UNLIKELY (videoflip->process == NULL))
    goto not_negotiated;

  if (videoflip->configuring_method != videoflip->active_method) {
    videoflip->active_method = videoflip->configuring_method;
    gst_video_flip_configure_process (videoflip);
  }

  enum_class = g_type_class_ref (GST_TYPE_VIDEO_ORIENTATION_METHOD);
  active_method_enum = g_enum_get_value (enum_class, videoflip->active_method);
  GST_LOG_OBJECT (videoflip,
      "videoflip: flipping (%s), input %ux%u output %ux%u",
      active_method_enum ? active_method_enum->value_nick : "(nil)",
      GST_VIDEO_FRAME_WIDTH (in_frame), GST_VIDEO_FRAME_HEIGHT (in_frame),
      GST_VIDEO_FRAME_WIDTH (out_frame), GST_VIDEO_FRAME_HEIGHT (out_frame));
  g_type_class_unref (enum_class);

  videoflip->process (videoflip, out_frame, in_frame);

  proposed = videoflip->proposed_method;
  active = videoflip->active_method;
  videoflip->change_configuring_method = TRUE;
  GST_OBJECT_UNLOCK (videoflip);

  if (proposed != active) {
    gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (videoflip),
        proposed == GST_VIDEO_ORIENTATION_IDENTITY);
    gst_base_transform_reconfigure_src (GST_BASE_TRANSFORM (videoflip));
  }

  return GST_FLOW_OK;

not_negotiated:
  {
    GST_OBJECT_UNLOCK (videoflip);
    GST_ERROR_OBJECT (videoflip, "Not negotiated yet");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
gst_video_flip_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoFlip *vf = GST_VIDEO_FLIP (trans);
  gdouble new_x, new_y, x, y;
  gboolean ret;
  GstVideoInfo *out_info = &GST_VIDEO_FILTER (trans)->out_info;

  GST_DEBUG_OBJECT (vf, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event = gst_event_make_writable (event);

      if (gst_navigation_event_get_coordinates (event, &x, &y)) {
        GST_DEBUG_OBJECT (vf, "converting %fx%f", x, y);
        GST_OBJECT_LOCK (vf);
        switch (vf->active_method) {
          case GST_VIDEO_ORIENTATION_90R:
            new_x = y;
            new_y = out_info->width - x;
            break;
          case GST_VIDEO_ORIENTATION_90L:
            new_x = out_info->height - y;
            new_y = x;
            break;
          case GST_VIDEO_ORIENTATION_UR_LL:
            new_x = out_info->height - y;
            new_y = out_info->width - x;
            break;
          case GST_VIDEO_ORIENTATION_UL_LR:
            new_x = y;
            new_y = x;
            break;
          case GST_VIDEO_ORIENTATION_180:
            new_x = out_info->width - x;
            new_y = out_info->height - y;
            break;
          case GST_VIDEO_ORIENTATION_HORIZ:
            new_x = out_info->width - x;
            new_y = y;
            break;
          case GST_VIDEO_ORIENTATION_VERT:
            new_x = x;
            new_y = out_info->height - y;
            break;
          default:
            new_x = x;
            new_y = y;
            break;
        }
        GST_OBJECT_UNLOCK (vf);
        GST_DEBUG_OBJECT (vf, "to %fx%f", new_x, new_y);
        gst_navigation_event_set_coordinates (event, new_x, new_y);
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

static gboolean
gst_video_flip_sink_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoFlip *vf = GST_VIDEO_FLIP (trans);
  GstTagList *taglist;
  GstVideoOrientationMethod method;
  gboolean ret;

  GST_DEBUG_OBJECT (vf, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &taglist);

      if (gst_video_orientation_from_tag (taglist, &method)) {
        if (gst_tag_list_get_scope (taglist) == GST_TAG_SCOPE_STREAM) {
          vf->got_orientation_stream_tag = TRUE;
        } else if (gst_tag_list_get_scope (taglist) == GST_TAG_SCOPE_GLOBAL) {
          vf->global_tag_method = method;
        }

        if (gst_tag_list_get_scope (taglist) == GST_TAG_SCOPE_GLOBAL
            && vf->got_orientation_stream_tag) {
          GST_DEBUG_OBJECT (vf,
              "ignoring global tags as we received stream specific ones: %"
              GST_PTR_FORMAT, taglist);
        } else {
          gst_video_flip_set_method (vf, method, TRUE);
        }

        if (vf->method == GST_VIDEO_ORIENTATION_AUTO) {
          /* Update the orientation tag as we rotate the video accordingly.
           * The event (and so the tag list) can be shared so always copy both. */
          taglist = gst_tag_list_copy (taglist);

          gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
              "image-orientation", "rotate-0", NULL);

          gst_event_unref (event);
          event = gst_event_new_tag (taglist);
        }
      } else {
        // no orientation in tag
        if (gst_tag_list_get_scope (taglist) == GST_TAG_SCOPE_STREAM) {
          GST_DEBUG_OBJECT (vf,
              "stream tag does not contain orientation, restore the global one: %d",
              vf->global_tag_method);
          vf->got_orientation_stream_tag = FALSE;
          gst_video_flip_set_method (vf, vf->global_tag_method, TRUE);
        } else if (gst_tag_list_get_scope (taglist) == GST_TAG_SCOPE_GLOBAL) {
          vf->global_tag_method = GST_VIDEO_ORIENTATION_IDENTITY;

          if (!vf->got_orientation_stream_tag) {
            GST_DEBUG_OBJECT (vf,
                "global taglist withtout orientation, set to identity");
            gst_video_flip_set_method (vf, GST_VIDEO_ORIENTATION_IDENTITY,
                TRUE);
          } else {
            // keep using the orientation from the stream tag
          }
        }
      }

      break;
    case GST_EVENT_STREAM_START:
    {
      const gchar *stream_id;

      gst_event_parse_stream_start (event, &stream_id);
      if (g_strcmp0 (stream_id, vf->stream_id) != 0) {
        GST_DEBUG_OBJECT (vf, "new stream, reset orientation from tags");
        vf->got_orientation_stream_tag = FALSE;
        vf->global_tag_method = GST_VIDEO_ORIENTATION_IDENTITY;
        gst_video_flip_set_method (vf, GST_VIDEO_ORIENTATION_IDENTITY, TRUE);

        g_clear_pointer (&vf->stream_id, g_free);
        vf->stream_id = g_strdup (stream_id);
      }
    }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->sink_event (trans, event);

  return ret;
}

static void
gst_video_flip_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (object);

  switch (prop_id) {
    case PROP_METHOD:
    case PROP_VIDEO_DIRECTION:
      gst_video_flip_set_method (videoflip, g_value_get_enum (value), FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_flip_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (object);

  switch (prop_id) {
    case PROP_METHOD:
    case PROP_VIDEO_DIRECTION:
      g_value_set_enum (value, videoflip->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_flip_finalize (GObject * object)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (object);

  g_clear_pointer (&videoflip->stream_id, g_free);

  G_OBJECT_CLASS (gst_video_flip_parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_video_flip_change_state (GstElement * element, GstStateChange transition)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (element);
  GstStateChangeReturn result;

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      g_clear_pointer (&videoflip->stream_id, g_free);
      break;
    default:
      break;
  }

  return result;
}

static void
gst_video_flip_constructed (GObject * object)
{
  GstVideoFlip *self = GST_VIDEO_FLIP (object);

  if (self->method == (GstVideoOrientationMethod) PROP_METHOD_DEFAULT) {
    gst_video_flip_set_method (self,
        (GstVideoOrientationMethod) PROP_METHOD_DEFAULT, FALSE);
  }
}

static void
gst_video_flip_class_init (GstVideoFlipClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;
  GstVideoFilterClass *vfilter_class = (GstVideoFilterClass *) klass;
  GParamSpec *pspec;

  GST_DEBUG_CATEGORY_INIT (video_flip_debug, "videoflip", 0, "videoflip");

  gobject_class->set_property = gst_video_flip_set_property;
  gobject_class->get_property = gst_video_flip_get_property;
  gobject_class->constructed = gst_video_flip_constructed;
  gobject_class->finalize = gst_video_flip_finalize;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method",
          "method (deprecated, use video-direction instead)",
          GST_TYPE_VIDEO_FLIP_METHOD, PROP_METHOD_DEFAULT,
          GST_PARAM_CONTROLLABLE | GST_PARAM_MUTABLE_PLAYING |
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_override_property (gobject_class, PROP_VIDEO_DIRECTION,
      "video-direction");
  /* override the overriden property's flags to include the mutable in playing
   * flag */
  pspec = g_object_class_find_property (gobject_class, "video-direction");
  pspec->flags |= GST_PARAM_MUTABLE_PLAYING;

  gstelement_class->change_state = gst_video_flip_change_state;

  gst_element_class_set_static_metadata (gstelement_class, "Video flipper",
      "Filter/Effect/Video",
      "Flips and rotates video", "David Schleef <ds@schleef.org>");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_video_flip_sink_template);
  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_video_flip_src_template);

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_flip_transform_caps);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_video_flip_before_transform);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_video_flip_src_event);
  trans_class->sink_event = GST_DEBUG_FUNCPTR (gst_video_flip_sink_event);

  vfilter_class->set_info = GST_DEBUG_FUNCPTR (gst_video_flip_set_info);
  vfilter_class->transform_frame =
      GST_DEBUG_FUNCPTR (gst_video_flip_transform_frame);

  gst_type_mark_as_plugin_api (GST_TYPE_VIDEO_FLIP_METHOD, 0);
}

static void
gst_video_flip_init (GstVideoFlip * videoflip)
{
  /* We initialize to the default and call set_method() from constructed
   * if the value hasn't changed, this ensures set_method() does get called
   * even if the non-construct method / direction properties aren't set
   */
  videoflip->method = (GstVideoOrientationMethod) PROP_METHOD_DEFAULT;

  /* AUTO is not valid for active method, this is just to ensure we setup the
   * method in gst_video_flip_set_method() */
  videoflip->active_method = GST_VIDEO_ORIENTATION_AUTO;
  videoflip->proposed_method = GST_VIDEO_ORIENTATION_IDENTITY;
  videoflip->configuring_method = GST_VIDEO_ORIENTATION_IDENTITY;
}
