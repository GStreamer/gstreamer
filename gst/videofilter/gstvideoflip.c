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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * This file was (probably) generated from gstvideoflip.c,
 * gstvideoflip.c,v 1.7 2003/11/08 02:48:59 dschleef Exp 
 */
/**
 * SECTION:element-videoflip
 *
 * Flips and rotates video.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! videoflip method=clockwise ! ffmpegcolorspace ! ximagesink
 * ]| This pipeline flips the test image 90 degrees clockwise.
 * </refsect2>
 *
 * Last reviewed on 2010-04-18 (0.10.22)
 */


#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideoflip.h"

#include <string.h>
#include <gst/gst.h>
#include <gst/controller/gstcontroller.h>
#include <gst/video/video.h>

/* GstVideoFlip properties */
enum
{
  PROP_0,
  PROP_METHOD
      /* FILL ME */
};

#define PROP_METHOD_DEFAULT GST_VIDEO_FLIP_METHOD_IDENTITY

GST_DEBUG_CATEGORY_STATIC (video_flip_debug);
#define GST_CAT_DEFAULT video_flip_debug

static GstStaticPadTemplate gst_video_flip_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ABGR ";" GST_VIDEO_CAPS_RGBA ";"
        GST_VIDEO_CAPS_YUV ("Y444") ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("IYUV") ";"
        GST_VIDEO_CAPS_YUV ("YUY2") ";" GST_VIDEO_CAPS_YUV ("UYVY") ";"
        GST_VIDEO_CAPS_YUV ("YVYU")

    )
    );

static GstStaticPadTemplate gst_video_flip_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ABGR ";" GST_VIDEO_CAPS_RGBA ";"
        GST_VIDEO_CAPS_YUV ("Y444") ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";" GST_VIDEO_CAPS_YUV ("IYUV") ";"
        GST_VIDEO_CAPS_YUV ("YUY2") ";" GST_VIDEO_CAPS_YUV ("UYVY") ";"
        GST_VIDEO_CAPS_YUV ("YVYU")
    )
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

GST_BOILERPLATE (GstVideoFlip, gst_video_flip, GstVideoFilter,
    GST_TYPE_VIDEO_FILTER);

static GstCaps *
gst_video_flip_transform_caps (GstBaseTransform * trans,
    GstPadDirection direction, GstCaps * caps)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (trans);
  GstCaps *ret;
  gint width, height, i;

  ret = gst_caps_copy (caps);

  for (i = 0; i < gst_caps_get_size (ret); i++) {
    GstStructure *structure = gst_caps_get_structure (ret, i);
    gint par_n, par_d;

    if (gst_structure_get_int (structure, "width", &width) &&
        gst_structure_get_int (structure, "height", &height)) {

      switch (videoflip->method) {
        case GST_VIDEO_FLIP_METHOD_90R:
        case GST_VIDEO_FLIP_METHOD_90L:
        case GST_VIDEO_FLIP_METHOD_TRANS:
        case GST_VIDEO_FLIP_METHOD_OTHER:
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
        case GST_VIDEO_FLIP_METHOD_IDENTITY:
        case GST_VIDEO_FLIP_METHOD_180:
        case GST_VIDEO_FLIP_METHOD_HORIZ:
        case GST_VIDEO_FLIP_METHOD_VERT:
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

static gboolean
gst_video_flip_get_unit_size (GstBaseTransform * btrans, GstCaps * caps,
    guint * size)
{
  GstVideoFormat format;
  gint width, height;

  if (!gst_video_format_parse_caps (caps, &format, &width, &height))
    return FALSE;

  *size = gst_video_format_get_size (format, width, height);

  GST_DEBUG_OBJECT (btrans, "our frame size is %d bytes (%dx%d)", *size,
      width, height);

  return TRUE;
}

static void
gst_video_flip_planar_yuv (GstVideoFlip * videoflip, guint8 * dest,
    const guint8 * src)
{
  gint x, y;
  guint8 const *s;
  guint8 *d;
  GstVideoFormat format = videoflip->format;
  gint sw = videoflip->from_width;
  gint sh = videoflip->from_height;
  gint dw = videoflip->to_width;
  gint dh = videoflip->to_height;
  gint src_y_stride, src_u_stride, src_v_stride;
  gint src_y_offset, src_u_offset, src_v_offset;
  gint src_y_height, src_u_height, src_v_height;
  gint src_y_width, src_u_width, src_v_width;
  gint dest_y_stride, dest_u_stride, dest_v_stride;
  gint dest_y_offset, dest_u_offset, dest_v_offset;
  gint dest_y_height, dest_u_height, dest_v_height;
  gint dest_y_width, dest_u_width, dest_v_width;

  src_y_stride = gst_video_format_get_row_stride (format, 0, sw);
  src_u_stride = gst_video_format_get_row_stride (format, 1, sw);
  src_v_stride = gst_video_format_get_row_stride (format, 2, sw);

  dest_y_stride = gst_video_format_get_row_stride (format, 0, dw);
  dest_u_stride = gst_video_format_get_row_stride (format, 1, dw);
  dest_v_stride = gst_video_format_get_row_stride (format, 2, dw);

  src_y_offset = gst_video_format_get_component_offset (format, 0, sw, sh);
  src_u_offset = gst_video_format_get_component_offset (format, 1, sw, sh);
  src_v_offset = gst_video_format_get_component_offset (format, 2, sw, sh);

  dest_y_offset = gst_video_format_get_component_offset (format, 0, dw, dh);
  dest_u_offset = gst_video_format_get_component_offset (format, 1, dw, dh);
  dest_v_offset = gst_video_format_get_component_offset (format, 2, dw, dh);

  src_y_width = gst_video_format_get_component_width (format, 0, sw);
  src_u_width = gst_video_format_get_component_width (format, 1, sw);
  src_v_width = gst_video_format_get_component_width (format, 2, sw);

  dest_y_width = gst_video_format_get_component_width (format, 0, dw);
  dest_u_width = gst_video_format_get_component_width (format, 1, dw);
  dest_v_width = gst_video_format_get_component_width (format, 2, dw);

  src_y_height = gst_video_format_get_component_height (format, 0, sh);
  src_u_height = gst_video_format_get_component_height (format, 1, sh);
  src_v_height = gst_video_format_get_component_height (format, 2, sh);

  dest_y_height = gst_video_format_get_component_height (format, 0, dh);
  dest_u_height = gst_video_format_get_component_height (format, 1, dh);
  dest_v_height = gst_video_format_get_component_height (format, 2, dh);

  switch (videoflip->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
      /* Flip Y */
      s = src + src_y_offset;
      d = dest + dest_y_offset;
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + y];
        }
      }
      /* Flip U */
      s = src + src_u_offset;
      d = dest + dest_u_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + y];
        }
      }
      /* Flip V */
      s = src + src_v_offset;
      d = dest + dest_v_offset;
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_90L:
      /* Flip Y */
      s = src + src_y_offset;
      d = dest + dest_y_offset;
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[x * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = src + src_u_offset;
      d = dest + dest_u_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[x * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = src + src_v_offset;
      d = dest + dest_v_offset;
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[x * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_180:
      /* Flip Y */
      s = src + src_y_offset;
      d = dest + dest_y_offset;
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = src + src_u_offset;
      d = dest + dest_u_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = src + src_v_offset;
      d = dest + dest_v_offset;
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_HORIZ:
      /* Flip Y */
      s = src + src_y_offset;
      d = dest + dest_y_offset;
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[y * src_y_stride + (src_y_width - 1 - x)];
        }
      }
      /* Flip U */
      s = src + src_u_offset;
      d = dest + dest_u_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[y * src_u_stride + (src_u_width - 1 - x)];
        }
      }
      /* Flip V */
      s = src + src_v_offset;
      d = dest + dest_v_offset;
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[y * src_v_stride + (src_v_width - 1 - x)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_VERT:
      /* Flip Y */
      s = src + src_y_offset;
      d = dest + dest_y_offset;
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - y) * src_y_stride + x];
        }
      }
      /* Flip U */
      s = src + src_u_offset;
      d = dest + dest_u_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - y) * src_u_stride + x];
        }
      }
      /* Flip V */
      s = src + src_v_offset;
      d = dest + dest_v_offset;
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - y) * src_v_stride + x];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_TRANS:
      /* Flip Y */
      s = src + src_y_offset;
      d = dest + dest_y_offset;
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] = s[x * src_y_stride + y];
        }
      }
      /* Flip U */
      s = src + src_u_offset;
      d = dest + dest_u_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] = s[x * src_u_stride + y];
        }
      }
      /* Flip V */
      s = src + src_v_offset;
      d = dest + dest_v_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_v_stride + x] = s[x * src_v_stride + y];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_OTHER:
      /* Flip Y */
      s = src + src_y_offset;
      d = dest + dest_y_offset;
      for (y = 0; y < dest_y_height; y++) {
        for (x = 0; x < dest_y_width; x++) {
          d[y * dest_y_stride + x] =
              s[(src_y_height - 1 - x) * src_y_stride + (src_y_width - 1 - y)];
        }
      }
      /* Flip U */
      s = src + src_u_offset;
      d = dest + dest_u_offset;
      for (y = 0; y < dest_u_height; y++) {
        for (x = 0; x < dest_u_width; x++) {
          d[y * dest_u_stride + x] =
              s[(src_u_height - 1 - x) * src_u_stride + (src_u_width - 1 - y)];
        }
      }
      /* Flip V */
      s = src + src_v_offset;
      d = dest + dest_v_offset;
      for (y = 0; y < dest_v_height; y++) {
        for (x = 0; x < dest_v_width; x++) {
          d[y * dest_v_stride + x] =
              s[(src_v_height - 1 - x) * src_v_stride + (src_v_width - 1 - y)];
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_IDENTITY:
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_video_flip_packed_simple (GstVideoFlip * videoflip, guint8 * dest,
    const guint8 * src)
{
  gint x, y, z;
  guint8 const *s = src;
  guint8 *d = dest;
  GstVideoFormat format = videoflip->format;
  gint sw = videoflip->from_width;
  gint sh = videoflip->from_height;
  gint dw = videoflip->to_width;
  gint dh = videoflip->to_height;
  gint src_stride, dest_stride;
  gint bpp;

  src_stride = gst_video_format_get_row_stride (format, 0, sw);
  dest_stride = gst_video_format_get_row_stride (format, 0, dw);
  /* This is only true for non-subsampled formats! */
  bpp = gst_video_format_get_pixel_stride (format, 0);

  switch (videoflip->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - x) * src_stride + y * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_90L:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[x * src_stride + (sw - 1 - y) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_180:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - y) * src_stride + (sw - 1 - x) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_HORIZ:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[y * src_stride + (sw - 1 - x) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_VERT:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - y) * src_stride + x * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_TRANS:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] = s[x * src_stride + y * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_OTHER:
      for (y = 0; y < dh; y++) {
        for (x = 0; x < dw; x++) {
          for (z = 0; z < bpp; z++) {
            d[y * dest_stride + x * bpp + z] =
                s[(sh - 1 - x) * src_stride + (sw - 1 - y) * bpp + z];
          }
        }
      }
      break;
    case GST_VIDEO_FLIP_METHOD_IDENTITY:
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}


static void
gst_video_flip_y422 (GstVideoFlip * videoflip, guint8 * dest,
    const guint8 * src)
{
  gint x, y;
  guint8 const *s = src;
  guint8 *d = dest;
  GstVideoFormat format = videoflip->format;
  gint sw = videoflip->from_width;
  gint sh = videoflip->from_height;
  gint dw = videoflip->to_width;
  gint dh = videoflip->to_height;
  gint src_stride, dest_stride;
  gint bpp;
  gint y_offset;
  gint u_offset;
  gint v_offset;
  gint y_stride;

  src_stride = gst_video_format_get_row_stride (format, 0, sw);
  dest_stride = gst_video_format_get_row_stride (format, 0, dw);

  y_offset = gst_video_format_get_component_offset (format, 0, sw, sh);
  u_offset = gst_video_format_get_component_offset (format, 1, sw, sh);
  v_offset = gst_video_format_get_component_offset (format, 2, sw, sh);
  y_stride = gst_video_format_get_pixel_stride (format, 0);
  bpp = y_stride;

  switch (videoflip->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
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
    case GST_VIDEO_FLIP_METHOD_90L:
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
    case GST_VIDEO_FLIP_METHOD_180:
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
    case GST_VIDEO_FLIP_METHOD_HORIZ:
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
    case GST_VIDEO_FLIP_METHOD_VERT:
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
    case GST_VIDEO_FLIP_METHOD_TRANS:
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
    case GST_VIDEO_FLIP_METHOD_OTHER:
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
    case GST_VIDEO_FLIP_METHOD_IDENTITY:
      g_assert_not_reached ();
      break;
    default:
      g_assert_not_reached ();
      break;
  }
}


static gboolean
gst_video_flip_set_caps (GstBaseTransform * btrans, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstVideoFlip *vf = GST_VIDEO_FLIP (btrans);
  GstVideoFormat in_format, out_format;
  gboolean ret = FALSE;

  vf->process = NULL;

  if (!gst_video_format_parse_caps (incaps, &in_format, &vf->from_width,
          &vf->from_height)
      || !gst_video_format_parse_caps (outcaps, &out_format, &vf->to_width,
          &vf->to_height))
    goto invalid_caps;

  if (in_format != out_format)
    goto invalid_caps;
  vf->format = in_format;

  /* Check that they are correct */
  switch (vf->method) {
    case GST_VIDEO_FLIP_METHOD_90R:
    case GST_VIDEO_FLIP_METHOD_90L:
    case GST_VIDEO_FLIP_METHOD_TRANS:
    case GST_VIDEO_FLIP_METHOD_OTHER:
      if ((vf->from_width != vf->to_height) ||
          (vf->from_height != vf->to_width)) {
        GST_ERROR_OBJECT (vf, "we are inverting width and height but caps "
            "are not correct : %dx%d to %dx%d", vf->from_width,
            vf->from_height, vf->to_width, vf->to_height);
        goto beach;
      }
      break;
    case GST_VIDEO_FLIP_METHOD_IDENTITY:

      break;
    case GST_VIDEO_FLIP_METHOD_180:
    case GST_VIDEO_FLIP_METHOD_HORIZ:
    case GST_VIDEO_FLIP_METHOD_VERT:
      if ((vf->from_width != vf->to_width) ||
          (vf->from_height != vf->to_height)) {
        GST_ERROR_OBJECT (vf, "we are keeping width and height but caps "
            "are not correct : %dx%d to %dx%d", vf->from_width,
            vf->from_height, vf->to_width, vf->to_height);
        goto beach;
      }
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  ret = TRUE;

  switch (vf->format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
      vf->process = gst_video_flip_planar_yuv;
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
      vf->process = gst_video_flip_packed_simple;
      break;
    default:
      break;
  }

beach:
  return ret && (vf->process != NULL);

invalid_caps:
  GST_ERROR_OBJECT (vf, "Invalid caps: %" GST_PTR_FORMAT " -> %" GST_PTR_FORMAT,
      incaps, outcaps);
  return FALSE;
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
    gst_object_sync_values (G_OBJECT (videoflip), stream_time);
}

static GstFlowReturn
gst_video_flip_transform (GstBaseTransform * trans, GstBuffer * in,
    GstBuffer * out)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (trans);
  guint8 *dest;
  const guint8 *src;

  if (G_UNLIKELY (videoflip->process == NULL))
    goto not_negotiated;

  src = GST_BUFFER_DATA (in);
  dest = GST_BUFFER_DATA (out);

  GST_LOG_OBJECT (videoflip, "videoflip: flipping %dx%d to %dx%d (%s)",
      videoflip->from_width, videoflip->from_height, videoflip->to_width,
      videoflip->to_height, video_flip_methods[videoflip->method].value_nick);

  GST_OBJECT_LOCK (videoflip);
  videoflip->process (videoflip, dest, src);
  GST_OBJECT_UNLOCK (videoflip);

  return GST_FLOW_OK;

not_negotiated:
  GST_ERROR_OBJECT (videoflip, "Not negotiated yet");
  return GST_FLOW_NOT_NEGOTIATED;
}

static gboolean
gst_video_flip_src_event (GstBaseTransform * trans, GstEvent * event)
{
  GstVideoFlip *vf = GST_VIDEO_FLIP (trans);
  gdouble new_x, new_y, x, y;
  GstStructure *structure;
  gboolean ret;

  GST_DEBUG_OBJECT (vf, "handling %s event", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NAVIGATION:
      event =
          GST_EVENT (gst_mini_object_make_writable (GST_MINI_OBJECT (event)));

      structure = (GstStructure *) gst_event_get_structure (event);
      if (gst_structure_get_double (structure, "pointer_x", &x) &&
          gst_structure_get_double (structure, "pointer_y", &y)) {
        GST_DEBUG_OBJECT (vf, "converting %fx%f", x, y);
        switch (vf->method) {
          case GST_VIDEO_FLIP_METHOD_90R:
            new_x = y;
            new_y = vf->to_width - x;
            break;
          case GST_VIDEO_FLIP_METHOD_90L:
            new_x = vf->to_height - y;
            new_y = x;
            break;
          case GST_VIDEO_FLIP_METHOD_OTHER:
            new_x = vf->to_height - y;
            new_y = vf->to_width - x;
            break;
          case GST_VIDEO_FLIP_METHOD_TRANS:
            new_x = y;
            new_y = x;
            break;
          case GST_VIDEO_FLIP_METHOD_180:
            new_x = vf->to_width - x;
            new_y = vf->to_height - y;
            break;
          case GST_VIDEO_FLIP_METHOD_HORIZ:
            new_x = vf->to_width - x;
            new_y = y;
            break;
          case GST_VIDEO_FLIP_METHOD_VERT:
            new_x = x;
            new_y = vf->to_height - y;
            break;
          default:
            new_x = x;
            new_y = y;
            break;
        }
        GST_DEBUG_OBJECT (vf, "to %fx%f", new_x, new_y);
        gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE, new_x,
            "pointer_y", G_TYPE_DOUBLE, new_y, NULL);
      }
      break;
    default:
      break;
  }

  ret = GST_BASE_TRANSFORM_CLASS (parent_class)->src_event (trans, event);

  return ret;
}

static void
gst_video_flip_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVideoFlip *videoflip = GST_VIDEO_FLIP (object);

  switch (prop_id) {
    case PROP_METHOD:
    {
      GstVideoFlipMethod method;

      method = g_value_get_enum (value);
      GST_OBJECT_LOCK (videoflip);
      if (method != videoflip->method) {
        GstBaseTransform *btrans = GST_BASE_TRANSFORM (videoflip);

        GST_DEBUG_OBJECT (videoflip, "Changing method from %s to %s",
            video_flip_methods[videoflip->method].value_nick,
            video_flip_methods[method].value_nick);

        videoflip->method = method;
        GST_OBJECT_UNLOCK (videoflip);

        gst_base_transform_set_passthrough (btrans,
            method == GST_VIDEO_FLIP_METHOD_IDENTITY);
        gst_base_transform_reconfigure (btrans);
      } else {
        GST_OBJECT_UNLOCK (videoflip);
      }
    }
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
      g_value_set_enum (value, videoflip->method);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_video_flip_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video flipper",
      "Filter/Effect/Video",
      "Flips and rotates video", "David Schleef <ds@schleef.org>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_video_flip_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_video_flip_src_template);
}

static void
gst_video_flip_class_init (GstVideoFlipClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) klass;

  GST_DEBUG_CATEGORY_INIT (video_flip_debug, "videoflip", 0, "videoflip");

  gobject_class->set_property = gst_video_flip_set_property;
  gobject_class->get_property = gst_video_flip_get_property;

  g_object_class_install_property (gobject_class, PROP_METHOD,
      g_param_spec_enum ("method", "method", "method",
          GST_TYPE_VIDEO_FLIP_METHOD, PROP_METHOD_DEFAULT,
          GST_PARAM_CONTROLLABLE | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  trans_class->transform_caps =
      GST_DEBUG_FUNCPTR (gst_video_flip_transform_caps);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_video_flip_set_caps);
  trans_class->get_unit_size = GST_DEBUG_FUNCPTR (gst_video_flip_get_unit_size);
  trans_class->transform = GST_DEBUG_FUNCPTR (gst_video_flip_transform);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_video_flip_before_transform);
  trans_class->src_event = GST_DEBUG_FUNCPTR (gst_video_flip_src_event);
}

static void
gst_video_flip_init (GstVideoFlip * videoflip, GstVideoFlipClass * klass)
{
  videoflip->method = PROP_METHOD_DEFAULT;
  gst_base_transform_set_passthrough (GST_BASE_TRANSFORM (videoflip), TRUE);
}
