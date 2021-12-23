/*
 * GStreamer
 * Copyright (C) 2008-2010 Sebastian Dröge <slomo@collabora.co.uk>
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

#include <string.h>

#include "gstdeinterlacemethod.h"

G_DEFINE_ABSTRACT_TYPE (GstDeinterlaceMethod, gst_deinterlace_method,
    GST_TYPE_OBJECT);

gboolean
gst_deinterlace_method_supported (GType type, GstVideoFormat format, gint width,
    gint height)
{
  GstDeinterlaceMethodClass *klass =
      GST_DEINTERLACE_METHOD_CLASS (g_type_class_ref (type));
  gboolean ret;

  if (format == GST_VIDEO_FORMAT_UNKNOWN)
    ret = TRUE;
  else
    ret = klass->supported (klass, format, width, height);
  g_type_class_unref (klass);

  return ret;
}

static gboolean
gst_deinterlace_method_supported_impl (GstDeinterlaceMethodClass * klass,
    GstVideoFormat format, gint width, gint height)
{
  switch (format) {
    case GST_VIDEO_FORMAT_YUY2:
      return (klass->deinterlace_frame_yuy2 != NULL);
    case GST_VIDEO_FORMAT_YVYU:
      return (klass->deinterlace_frame_yvyu != NULL);
    case GST_VIDEO_FORMAT_UYVY:
      return (klass->deinterlace_frame_uyvy != NULL);
    case GST_VIDEO_FORMAT_I420:
      return (klass->deinterlace_frame_i420 != NULL);
    case GST_VIDEO_FORMAT_YV12:
      return (klass->deinterlace_frame_yv12 != NULL);
    case GST_VIDEO_FORMAT_Y444:
      return (klass->deinterlace_frame_y444 != NULL);
    case GST_VIDEO_FORMAT_Y42B:
      return (klass->deinterlace_frame_y42b != NULL);
    case GST_VIDEO_FORMAT_Y41B:
      return (klass->deinterlace_frame_y41b != NULL);
    case GST_VIDEO_FORMAT_AYUV:
      return (klass->deinterlace_frame_ayuv != NULL);
    case GST_VIDEO_FORMAT_NV12:
      return (klass->deinterlace_frame_nv12 != NULL);
    case GST_VIDEO_FORMAT_NV21:
      return (klass->deinterlace_frame_nv21 != NULL);
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
      return (klass->deinterlace_frame_argb != NULL);
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
      return (klass->deinterlace_frame_abgr != NULL);
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      return (klass->deinterlace_frame_rgba != NULL);
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      return (klass->deinterlace_frame_bgra != NULL);
    case GST_VIDEO_FORMAT_RGB:
      return (klass->deinterlace_frame_rgb != NULL);
    case GST_VIDEO_FORMAT_BGR:
      return (klass->deinterlace_frame_bgr != NULL);
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case GST_VIDEO_FORMAT_Y444_16BE:
    case GST_VIDEO_FORMAT_Y444_12BE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_I422_12BE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I420_12BE:
    case GST_VIDEO_FORMAT_I420_10BE:
#else
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I420_10LE:
#endif
      return (klass->deinterlace_frame_planar_high != NULL);
    default:
      return FALSE;
  }
}

void
gst_deinterlace_method_setup (GstDeinterlaceMethod * self, GstVideoInfo * vinfo)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  klass->setup (self, vinfo);
}

static void
gst_deinterlace_method_setup_impl (GstDeinterlaceMethod * self,
    GstVideoInfo * vinfo)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  self->vinfo = vinfo;

  self->deinterlace_frame = NULL;

  if (GST_VIDEO_INFO_FORMAT (self->vinfo) == GST_VIDEO_FORMAT_UNKNOWN)
    return;

  switch (GST_VIDEO_INFO_FORMAT (self->vinfo)) {
    case GST_VIDEO_FORMAT_YUY2:
      self->deinterlace_frame = klass->deinterlace_frame_yuy2;
      break;
    case GST_VIDEO_FORMAT_YVYU:
      self->deinterlace_frame = klass->deinterlace_frame_yvyu;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      self->deinterlace_frame = klass->deinterlace_frame_uyvy;
      break;
    case GST_VIDEO_FORMAT_I420:
      self->deinterlace_frame = klass->deinterlace_frame_i420;
      break;
    case GST_VIDEO_FORMAT_YV12:
      self->deinterlace_frame = klass->deinterlace_frame_yv12;
      break;
    case GST_VIDEO_FORMAT_Y444:
      self->deinterlace_frame = klass->deinterlace_frame_y444;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      self->deinterlace_frame = klass->deinterlace_frame_y42b;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      self->deinterlace_frame = klass->deinterlace_frame_y41b;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      self->deinterlace_frame = klass->deinterlace_frame_ayuv;
      break;
    case GST_VIDEO_FORMAT_NV12:
      self->deinterlace_frame = klass->deinterlace_frame_nv12;
      break;
    case GST_VIDEO_FORMAT_NV21:
      self->deinterlace_frame = klass->deinterlace_frame_nv21;
      break;
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
      self->deinterlace_frame = klass->deinterlace_frame_argb;
      break;
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
      self->deinterlace_frame = klass->deinterlace_frame_abgr;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      self->deinterlace_frame = klass->deinterlace_frame_rgba;
      break;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      self->deinterlace_frame = klass->deinterlace_frame_bgra;
      break;
    case GST_VIDEO_FORMAT_RGB:
      self->deinterlace_frame = klass->deinterlace_frame_rgb;
      break;
    case GST_VIDEO_FORMAT_BGR:
      self->deinterlace_frame = klass->deinterlace_frame_bgr;
      break;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case GST_VIDEO_FORMAT_Y444_16BE:
    case GST_VIDEO_FORMAT_Y444_12BE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_I422_12BE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I420_12BE:
    case GST_VIDEO_FORMAT_I420_10BE:
#else
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I420_10LE:
#endif
      self->deinterlace_frame = klass->deinterlace_frame_planar_high;
      break;
    default:
      self->deinterlace_frame = NULL;
      break;
  }
}

static void
gst_deinterlace_method_class_init (GstDeinterlaceMethodClass * klass)
{
  klass->setup = gst_deinterlace_method_setup_impl;
  klass->supported = gst_deinterlace_method_supported_impl;
}

static void
gst_deinterlace_method_init (GstDeinterlaceMethod * self)
{
  self->vinfo = NULL;
}

void
gst_deinterlace_method_deinterlace_frame (GstDeinterlaceMethod * self,
    const GstDeinterlaceField * history, guint history_count,
    GstVideoFrame * outframe, int cur_field_idx)
{
  g_assert (self->deinterlace_frame != NULL);
  self->deinterlace_frame (self, history, history_count, outframe,
      cur_field_idx);
}

gint
gst_deinterlace_method_get_fields_required (GstDeinterlaceMethod * self)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  return klass->fields_required;
}

gint
gst_deinterlace_method_get_latency (GstDeinterlaceMethod * self)
{
  GstDeinterlaceMethodClass *klass = GST_DEINTERLACE_METHOD_GET_CLASS (self);

  return klass->latency;
}

G_DEFINE_ABSTRACT_TYPE (GstDeinterlaceSimpleMethod,
    gst_deinterlace_simple_method, GST_TYPE_DEINTERLACE_METHOD);

static gboolean
gst_deinterlace_simple_method_supported (GstDeinterlaceMethodClass * mklass,
    GstVideoFormat format, gint width, gint height)
{
  GstDeinterlaceSimpleMethodClass *klass =
      GST_DEINTERLACE_SIMPLE_METHOD_CLASS (mklass);

  if (!GST_DEINTERLACE_METHOD_CLASS
      (gst_deinterlace_simple_method_parent_class)->supported (mklass, format,
          width, height))
    return FALSE;

  switch (format) {
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
      return (klass->interpolate_scanline_argb != NULL
          && klass->copy_scanline_argb != NULL);
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      return (klass->interpolate_scanline_rgba != NULL
          && klass->copy_scanline_rgba != NULL);
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
      return (klass->interpolate_scanline_abgr != NULL
          && klass->copy_scanline_abgr != NULL);
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      return (klass->interpolate_scanline_bgra != NULL
          && klass->copy_scanline_bgra != NULL);
    case GST_VIDEO_FORMAT_RGB:
      return (klass->interpolate_scanline_rgb != NULL
          && klass->copy_scanline_rgb != NULL);
    case GST_VIDEO_FORMAT_BGR:
      return (klass->interpolate_scanline_bgr != NULL
          && klass->copy_scanline_bgr != NULL);
    case GST_VIDEO_FORMAT_YUY2:
      return (klass->interpolate_scanline_yuy2 != NULL
          && klass->copy_scanline_yuy2 != NULL);
    case GST_VIDEO_FORMAT_YVYU:
      return (klass->interpolate_scanline_yvyu != NULL
          && klass->copy_scanline_yvyu != NULL);
    case GST_VIDEO_FORMAT_UYVY:
      return (klass->interpolate_scanline_uyvy != NULL
          && klass->copy_scanline_uyvy != NULL);
    case GST_VIDEO_FORMAT_AYUV:
      return (klass->interpolate_scanline_ayuv != NULL
          && klass->copy_scanline_ayuv != NULL);
    case GST_VIDEO_FORMAT_NV12:
      return (klass->interpolate_scanline_nv12 != NULL
          && klass->copy_scanline_nv12 != NULL
          && klass->interpolate_scanline_planar_y != NULL
          && klass->copy_scanline_planar_y != NULL);
    case GST_VIDEO_FORMAT_NV21:
      return (klass->interpolate_scanline_nv21 != NULL
          && klass->copy_scanline_nv21 != NULL
          && klass->interpolate_scanline_planar_y != NULL
          && klass->copy_scanline_planar_y != NULL);
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      return (klass->interpolate_scanline_planar_y != NULL
          && klass->copy_scanline_planar_y != NULL &&
          klass->interpolate_scanline_planar_u != NULL
          && klass->copy_scanline_planar_u != NULL &&
          klass->interpolate_scanline_planar_v != NULL
          && klass->copy_scanline_planar_v != NULL);
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case GST_VIDEO_FORMAT_Y444_16BE:
    case GST_VIDEO_FORMAT_Y444_12BE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_I422_12BE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I420_12BE:
    case GST_VIDEO_FORMAT_I420_10BE:
#else
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I420_10LE:
#endif
      return (klass->interpolate_scanline_planar_y_16bits != NULL
          && klass->copy_scanline_planar_y_16bits != NULL &&
          klass->interpolate_scanline_planar_u_16bits != NULL
          && klass->copy_scanline_planar_u_16bits != NULL &&
          klass->interpolate_scanline_planar_v_16bits != NULL
          && klass->copy_scanline_planar_v_16bits != NULL);
    default:
      return FALSE;
  }
}

static void
    gst_deinterlace_simple_method_interpolate_scanline_packed
    (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines, guint stride)
{
  memcpy (out, scanlines->m1, stride);
}

static void
gst_deinterlace_simple_method_copy_scanline_packed (GstDeinterlaceSimpleMethod *
    self, guint8 * out, const GstDeinterlaceScanlineData * scanlines,
    guint stride)
{
  memcpy (out, scanlines->m0, stride);
}

typedef struct
{
  const GstDeinterlaceField *history;
  guint history_count;
  gint cur_field_idx;
} LinesGetter;

#define CLAMP_LOW(i) (((i)<0) ? (i+2) : (i))
#define CLAMP_HI(i) (((i)>=(frame_height)) ? (i-2) : (i))

static guint8 *
get_line (LinesGetter * lg, gint field_offset, guint plane, gint line,
    gint line_offset)
{
  const GstVideoFrame *frame;
  gint idx, frame_height;
  guint8 *data;

  idx = lg->cur_field_idx + field_offset;
  if (idx < 0 || idx >= lg->history_count)
    return NULL;

  frame = lg->history[idx].frame;
  g_assert (frame);

  /* Now frame already refers to the field we want, the correct one is taken
   * from the history */
  if (GST_VIDEO_INFO_INTERLACE_MODE (&frame->info) ==
      GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
    /* Alternate frame containing a single field, adjust the line index */
    line /= 2;
    switch (line_offset) {
      case -2:
      case 2:
        line_offset /= 2;
        break;
      case 1:
        /* the "next" line of a top field line is the same line of a bottom
         * field */
        if (!GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_TFF))
          line_offset = 0;
        break;
      case -1:
        /* the "previous" line of a bottom field line is the same line of a
         * top field */
        if (GST_VIDEO_FRAME_FLAG_IS_SET (frame, GST_VIDEO_FRAME_FLAG_TFF))
          line_offset = 0;
        break;
      case 0:
        break;
      default:
        g_assert_not_reached ();
        break;
    }
  }

  frame_height = GST_VIDEO_FRAME_COMP_HEIGHT (frame, plane);
  line += line_offset;

  data = GST_VIDEO_FRAME_PLANE_DATA ((frame), plane);
  data += CLAMP_HI (CLAMP_LOW (line)) * GST_VIDEO_FRAME_PLANE_STRIDE ((frame),
      plane);

  return data;
}

static void
gst_deinterlace_simple_method_deinterlace_frame_packed (GstDeinterlaceMethod *
    method, const GstDeinterlaceField * history, guint history_count,
    GstVideoFrame * outframe, gint cur_field_idx)
{
  GstDeinterlaceSimpleMethod *self = GST_DEINTERLACE_SIMPLE_METHOD (method);
#ifndef G_DISABLE_ASSERT
  GstDeinterlaceMethodClass *dm_class = GST_DEINTERLACE_METHOD_GET_CLASS (self);
#endif
  GstDeinterlaceScanlineData scanlines;
  guint cur_field_flags;
  gint i;
  gint frame_height, frame_width;
  LinesGetter lg = { history, history_count, cur_field_idx };
  GstVideoFrame *framep, *frame0, *frame1, *frame2;

  g_assert (self->interpolate_scanline_packed != NULL);
  g_assert (self->copy_scanline_packed != NULL);

  frame_height = GST_VIDEO_FRAME_HEIGHT (outframe);
  frame_width = GST_VIDEO_FRAME_PLANE_STRIDE (outframe, 0);

  frame0 = history[cur_field_idx].frame;
  frame_width = MIN (frame_width, GST_VIDEO_FRAME_PLANE_STRIDE (frame0, 0));
  cur_field_flags = history[cur_field_idx].flags;

  framep = (cur_field_idx > 0 ? history[cur_field_idx - 1].frame : NULL);
  if (framep)
    frame_width = MIN (frame_width, GST_VIDEO_FRAME_PLANE_STRIDE (framep, 0));

  g_assert (dm_class->fields_required <= 5);

  frame1 =
      (cur_field_idx + 1 <
      history_count ? history[cur_field_idx + 1].frame : NULL);
  if (frame1)
    frame_width = MIN (frame_width, GST_VIDEO_FRAME_PLANE_STRIDE (frame1, 0));

  frame2 =
      (cur_field_idx + 2 <
      history_count ? history[cur_field_idx + 2].frame : NULL);
  if (frame2)
    frame_width = MIN (frame_width, GST_VIDEO_FRAME_PLANE_STRIDE (frame2, 0));

#define LINE(x,i) (((guint8*)GST_VIDEO_FRAME_PLANE_DATA((x),0)) + i * \
    GST_VIDEO_FRAME_PLANE_STRIDE((x),0))

  for (i = 0; i < frame_height; i++) {
    memset (&scanlines, 0, sizeof (scanlines));
    scanlines.bottom_field = (cur_field_flags == PICTURE_INTERLACED_BOTTOM);

    if (!((i & 1) ^ scanlines.bottom_field)) {
      /* copying */
      scanlines.tp = get_line (&lg, -1, 0, i, -1);
      scanlines.bp = get_line (&lg, -1, 0, i, 1);

      scanlines.tt0 = get_line (&lg, 0, 0, i, -2);
      scanlines.m0 = get_line (&lg, 0, 0, i, 0);
      scanlines.bb0 = get_line (&lg, 0, 0, i, 2);

      scanlines.t1 = get_line (&lg, 1, 0, i, -1);
      scanlines.b1 = get_line (&lg, 1, 0, i, 1);

      scanlines.tt2 = get_line (&lg, 2, 0, i, -2);
      scanlines.m2 = get_line (&lg, 2, 0, i, 0);
      scanlines.bb2 = get_line (&lg, 2, 0, i, 2);

      self->copy_scanline_packed (self, LINE (outframe, i), &scanlines,
          frame_width);
    } else {
      /* interpolating */
      scanlines.tp2 = get_line (&lg, -2, 0, i, -1);
      scanlines.bp2 = get_line (&lg, -2, 0, i, 1);

      scanlines.ttp = get_line (&lg, -1, 0, i, -2);
      scanlines.mp = get_line (&lg, -1, 0, i, 0);
      scanlines.bbp = get_line (&lg, -1, 0, i, 2);

      scanlines.t0 = get_line (&lg, 0, 0, i, -1);
      scanlines.b0 = get_line (&lg, 0, 0, i, 1);

      scanlines.tt1 = get_line (&lg, 1, 0, i, -2);
      scanlines.m1 = get_line (&lg, 1, 0, i, 0);
      scanlines.bb1 = get_line (&lg, 1, 0, i, 2);

      scanlines.t2 = get_line (&lg, 2, 0, i, -1);
      scanlines.b2 = get_line (&lg, 2, 0, i, 1);

      self->interpolate_scanline_packed (self, LINE (outframe, i), &scanlines,
          frame_width);
    }
#undef LINE
  }
}

static void
    gst_deinterlace_simple_method_interpolate_scanline_planar_y
    (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines, guint size)
{
  memcpy (out, scanlines->m1, size);
}

static void
gst_deinterlace_simple_method_copy_scanline_planar_y (GstDeinterlaceSimpleMethod
    * self, guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint
    size)
{
  memcpy (out, scanlines->m0, size);
}

static void
    gst_deinterlace_simple_method_interpolate_scanline_planar_u
    (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines, guint size)
{
  memcpy (out, scanlines->m1, size);
}

static void
gst_deinterlace_simple_method_copy_scanline_planar_u (GstDeinterlaceSimpleMethod
    * self, guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint
    size)
{
  memcpy (out, scanlines->m0, size);
}

static void
    gst_deinterlace_simple_method_interpolate_scanline_planar_v
    (GstDeinterlaceSimpleMethod * self, guint8 * out,
    const GstDeinterlaceScanlineData * scanlines, guint size)
{
  memcpy (out, scanlines->m1, size);
}

static void
gst_deinterlace_simple_method_copy_scanline_planar_v (GstDeinterlaceSimpleMethod
    * self, guint8 * out, const GstDeinterlaceScanlineData * scanlines, guint
    size)
{
  memcpy (out, scanlines->m0, size);
}

static void
    gst_deinterlace_simple_method_deinterlace_frame_planar_plane
    (GstDeinterlaceSimpleMethod * self, GstVideoFrame * dest,
    LinesGetter * lg,
    guint cur_field_flags, gint plane,
    GstDeinterlaceSimpleMethodFunction copy_scanline,
    GstDeinterlaceSimpleMethodFunction interpolate_scanline)
{
  GstDeinterlaceScanlineData scanlines;
  gint i;
  gint frame_height, frame_width;

  frame_height = GST_VIDEO_FRAME_COMP_HEIGHT (dest, plane);
  frame_width = GST_VIDEO_FRAME_COMP_WIDTH (dest, plane) *
      GST_VIDEO_FRAME_COMP_PSTRIDE (dest, plane);

  g_assert (interpolate_scanline != NULL);
  g_assert (copy_scanline != NULL);

#define LINE(x,i) (((guint8*)GST_VIDEO_FRAME_PLANE_DATA((x),plane)) + i * \
    GST_VIDEO_FRAME_PLANE_STRIDE((x),plane))

  for (i = 0; i < frame_height; i++) {
    memset (&scanlines, 0, sizeof (scanlines));
    scanlines.bottom_field = (cur_field_flags == PICTURE_INTERLACED_BOTTOM);

    if (!((i & 1) ^ scanlines.bottom_field)) {
      /* copying */
      scanlines.tp = get_line (lg, -1, plane, i, -1);
      scanlines.bp = get_line (lg, -1, plane, i, 1);

      scanlines.tt0 = get_line (lg, 0, plane, i, -2);
      scanlines.m0 = get_line (lg, 0, plane, i, 0);
      scanlines.bb0 = get_line (lg, 0, plane, i, 2);

      scanlines.t1 = get_line (lg, 1, plane, i, -1);
      scanlines.b1 = get_line (lg, 1, plane, i, 1);

      scanlines.tt2 = get_line (lg, 2, plane, i, -2);
      scanlines.m2 = get_line (lg, 2, plane, i, 0);
      scanlines.bb2 = get_line (lg, 2, plane, i, 2);

      copy_scanline (self, LINE (dest, i), &scanlines, frame_width);
    } else {
      /* interpolating */
      scanlines.tp2 = get_line (lg, -2, plane, i, -1);
      scanlines.bp2 = get_line (lg, -2, plane, i, 1);

      scanlines.ttp = get_line (lg, -1, plane, i, -2);
      scanlines.mp = get_line (lg, -1, plane, i, 0);
      scanlines.bbp = get_line (lg, -1, plane, i, 2);

      scanlines.t0 = get_line (lg, 0, plane, i, -1);
      scanlines.b0 = get_line (lg, 0, plane, i, 1);

      scanlines.tt1 = get_line (lg, 1, plane, i, -2);
      scanlines.m1 = get_line (lg, 1, plane, i, 0);
      scanlines.bb1 = get_line (lg, 1, plane, i, 2);

      scanlines.t2 = get_line (lg, 2, plane, i, -1);
      scanlines.b2 = get_line (lg, 2, plane, i, 1);

      interpolate_scanline (self, LINE (dest, i), &scanlines, frame_width);
    }
#undef LINE
  }
}

static void
gst_deinterlace_simple_method_deinterlace_frame_planar (GstDeinterlaceMethod *
    method, const GstDeinterlaceField * history, guint history_count,
    GstVideoFrame * outframe, gint cur_field_idx)
{
  GstDeinterlaceSimpleMethod *self = GST_DEINTERLACE_SIMPLE_METHOD (method);
#ifndef G_DISABLE_ASSERT
  GstDeinterlaceMethodClass *dm_class = GST_DEINTERLACE_METHOD_GET_CLASS (self);
#endif
  guint cur_field_flags = history[cur_field_idx].flags;
  gint i;
  GstDeinterlaceSimpleMethodFunction copy_scanline;
  GstDeinterlaceSimpleMethodFunction interpolate_scanline;
  LinesGetter lg = { history, history_count, cur_field_idx };

  g_assert (self->interpolate_scanline_planar[0] != NULL);
  g_assert (self->interpolate_scanline_planar[1] != NULL);
  g_assert (self->interpolate_scanline_planar[2] != NULL);
  g_assert (self->copy_scanline_planar[0] != NULL);
  g_assert (self->copy_scanline_planar[1] != NULL);
  g_assert (self->copy_scanline_planar[2] != NULL);
  g_assert (dm_class->fields_required <= 5);

  for (i = 0; i < 3; i++) {
    copy_scanline = self->copy_scanline_planar[i];
    interpolate_scanline = self->interpolate_scanline_planar[i];

    gst_deinterlace_simple_method_deinterlace_frame_planar_plane (self,
        outframe, &lg, cur_field_flags, i, copy_scanline, interpolate_scanline);
  }
}

static void
gst_deinterlace_simple_method_deinterlace_frame_nv12 (GstDeinterlaceMethod *
    method, const GstDeinterlaceField * history, guint history_count,
    GstVideoFrame * outframe, gint cur_field_idx)
{
  GstDeinterlaceSimpleMethod *self = GST_DEINTERLACE_SIMPLE_METHOD (method);
#ifndef G_DISABLE_ASSERT
  GstDeinterlaceMethodClass *dm_class = GST_DEINTERLACE_METHOD_GET_CLASS (self);
#endif
  guint cur_field_flags = history[cur_field_idx].flags;
  LinesGetter lg = { history, history_count, cur_field_idx, };

  /* Y plane is at position 0 */
  g_assert (self->interpolate_scanline_packed != NULL);
  g_assert (self->copy_scanline_packed != NULL);
  g_assert (self->interpolate_scanline_planar[0] != NULL);
  g_assert (self->copy_scanline_planar[0] != NULL);
  g_assert (dm_class->fields_required <= 5);

  /* Y plane first, then UV/VU plane */
  gst_deinterlace_simple_method_deinterlace_frame_planar_plane (self,
      outframe, &lg, cur_field_flags, 0,
      self->copy_scanline_planar[0], self->interpolate_scanline_planar[0]);
  gst_deinterlace_simple_method_deinterlace_frame_planar_plane (self,
      outframe, &lg, cur_field_flags, 1,
      self->copy_scanline_packed, self->interpolate_scanline_packed);
}

static void
gst_deinterlace_simple_method_setup (GstDeinterlaceMethod * method,
    GstVideoInfo * vinfo)
{
  GstDeinterlaceSimpleMethod *self = GST_DEINTERLACE_SIMPLE_METHOD (method);
  GstDeinterlaceSimpleMethodClass *klass =
      GST_DEINTERLACE_SIMPLE_METHOD_GET_CLASS (self);

  GST_DEINTERLACE_METHOD_CLASS
      (gst_deinterlace_simple_method_parent_class)->setup (method, vinfo);

  self->interpolate_scanline_packed = NULL;
  self->copy_scanline_packed = NULL;

  self->interpolate_scanline_planar[0] = NULL;
  self->interpolate_scanline_planar[1] = NULL;
  self->interpolate_scanline_planar[2] = NULL;
  self->copy_scanline_planar[0] = NULL;
  self->copy_scanline_planar[1] = NULL;
  self->copy_scanline_planar[2] = NULL;

  if (GST_VIDEO_INFO_FORMAT (vinfo) == GST_VIDEO_FORMAT_UNKNOWN)
    return;

  switch (GST_VIDEO_INFO_FORMAT (vinfo)) {
    case GST_VIDEO_FORMAT_YUY2:
      self->interpolate_scanline_packed = klass->interpolate_scanline_yuy2;
      self->copy_scanline_packed = klass->copy_scanline_yuy2;
      break;
    case GST_VIDEO_FORMAT_YVYU:
      self->interpolate_scanline_packed = klass->interpolate_scanline_yvyu;
      self->copy_scanline_packed = klass->copy_scanline_yvyu;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      self->interpolate_scanline_packed = klass->interpolate_scanline_uyvy;
      self->copy_scanline_packed = klass->copy_scanline_uyvy;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      self->interpolate_scanline_packed = klass->interpolate_scanline_ayuv;
      self->copy_scanline_packed = klass->copy_scanline_ayuv;
      break;
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_xRGB:
      self->interpolate_scanline_packed = klass->interpolate_scanline_argb;
      self->copy_scanline_packed = klass->copy_scanline_argb;
      break;
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_xBGR:
      self->interpolate_scanline_packed = klass->interpolate_scanline_abgr;
      self->copy_scanline_packed = klass->copy_scanline_abgr;
      break;
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_RGBx:
      self->interpolate_scanline_packed = klass->interpolate_scanline_rgba;
      self->copy_scanline_packed = klass->copy_scanline_rgba;
      break;
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_BGRx:
      self->interpolate_scanline_packed = klass->interpolate_scanline_bgra;
      self->copy_scanline_packed = klass->copy_scanline_bgra;
      break;
    case GST_VIDEO_FORMAT_RGB:
      self->interpolate_scanline_packed = klass->interpolate_scanline_rgb;
      self->copy_scanline_packed = klass->copy_scanline_rgb;
      break;
    case GST_VIDEO_FORMAT_BGR:
      self->interpolate_scanline_packed = klass->interpolate_scanline_bgr;
      self->copy_scanline_packed = klass->copy_scanline_bgr;
      break;
    case GST_VIDEO_FORMAT_NV12:
      self->interpolate_scanline_packed = klass->interpolate_scanline_nv12;
      self->copy_scanline_packed = klass->copy_scanline_nv12;
      self->interpolate_scanline_planar[0] =
          klass->interpolate_scanline_planar_y;
      self->copy_scanline_planar[0] = klass->copy_scanline_planar_y;
      break;
    case GST_VIDEO_FORMAT_NV21:
      self->interpolate_scanline_packed = klass->interpolate_scanline_nv21;
      self->copy_scanline_packed = klass->copy_scanline_nv21;
      self->interpolate_scanline_planar[0] =
          klass->interpolate_scanline_planar_y;
      self->copy_scanline_planar[0] = klass->copy_scanline_planar_y;
      break;
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:
      self->interpolate_scanline_planar[0] =
          klass->interpolate_scanline_planar_y;
      self->copy_scanline_planar[0] = klass->copy_scanline_planar_y;
      self->interpolate_scanline_planar[1] =
          klass->interpolate_scanline_planar_u;
      self->copy_scanline_planar[1] = klass->copy_scanline_planar_u;
      self->interpolate_scanline_planar[2] =
          klass->interpolate_scanline_planar_v;
      self->copy_scanline_planar[2] = klass->copy_scanline_planar_v;
      break;
#if G_BYTE_ORDER == G_BIG_ENDIAN
    case GST_VIDEO_FORMAT_Y444_16BE:
    case GST_VIDEO_FORMAT_Y444_12BE:
    case GST_VIDEO_FORMAT_Y444_10BE:
    case GST_VIDEO_FORMAT_I422_12BE:
    case GST_VIDEO_FORMAT_I422_10BE:
    case GST_VIDEO_FORMAT_I420_12BE:
    case GST_VIDEO_FORMAT_I420_10BE:
#else
    case GST_VIDEO_FORMAT_Y444_16LE:
    case GST_VIDEO_FORMAT_Y444_12LE:
    case GST_VIDEO_FORMAT_Y444_10LE:
    case GST_VIDEO_FORMAT_I422_12LE:
    case GST_VIDEO_FORMAT_I422_10LE:
    case GST_VIDEO_FORMAT_I420_12LE:
    case GST_VIDEO_FORMAT_I420_10LE:
#endif
      self->interpolate_scanline_planar[0] =
          klass->interpolate_scanline_planar_y_16bits;
      self->copy_scanline_planar[0] = klass->copy_scanline_planar_y_16bits;
      self->interpolate_scanline_planar[1] =
          klass->interpolate_scanline_planar_u_16bits;
      self->copy_scanline_planar[1] = klass->copy_scanline_planar_u_16bits;
      self->interpolate_scanline_planar[2] =
          klass->interpolate_scanline_planar_v_16bits;
      self->copy_scanline_planar[2] = klass->copy_scanline_planar_v_16bits;
      break;
    default:
      break;
  }
}

static void
gst_deinterlace_simple_method_class_init (GstDeinterlaceSimpleMethodClass
    * klass)
{
  GstDeinterlaceMethodClass *dm_class = (GstDeinterlaceMethodClass *) klass;

  dm_class->deinterlace_frame_ayuv =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_yuy2 =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_yvyu =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_uyvy =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_argb =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_abgr =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_rgba =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_bgra =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_rgb =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_bgr =
      gst_deinterlace_simple_method_deinterlace_frame_packed;
  dm_class->deinterlace_frame_i420 =
      gst_deinterlace_simple_method_deinterlace_frame_planar;
  dm_class->deinterlace_frame_yv12 =
      gst_deinterlace_simple_method_deinterlace_frame_planar;
  dm_class->deinterlace_frame_y444 =
      gst_deinterlace_simple_method_deinterlace_frame_planar;
  dm_class->deinterlace_frame_y42b =
      gst_deinterlace_simple_method_deinterlace_frame_planar;
  dm_class->deinterlace_frame_y41b =
      gst_deinterlace_simple_method_deinterlace_frame_planar;
  dm_class->deinterlace_frame_nv12 =
      gst_deinterlace_simple_method_deinterlace_frame_nv12;
  dm_class->deinterlace_frame_nv21 =
      gst_deinterlace_simple_method_deinterlace_frame_nv12;
  /* same as 8bits planar */
  dm_class->deinterlace_frame_planar_high =
      gst_deinterlace_simple_method_deinterlace_frame_planar;
  dm_class->fields_required = 2;
  dm_class->setup = gst_deinterlace_simple_method_setup;
  dm_class->supported = gst_deinterlace_simple_method_supported;

  klass->interpolate_scanline_yuy2 =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_yuy2 =
      gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_yvyu =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_yvyu =
      gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_ayuv =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_ayuv =
      gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_uyvy =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_uyvy =
      gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_nv12 =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_nv12 =
      gst_deinterlace_simple_method_copy_scanline_packed;

  klass->interpolate_scanline_argb =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_argb =
      gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_abgr =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_abgr =
      gst_deinterlace_simple_method_copy_scanline_packed;

  klass->interpolate_scanline_rgba =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_rgba =
      gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_bgra =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_bgra =
      gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_rgb =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_rgb = gst_deinterlace_simple_method_copy_scanline_packed;
  klass->interpolate_scanline_bgr =
      gst_deinterlace_simple_method_interpolate_scanline_packed;
  klass->copy_scanline_bgr = gst_deinterlace_simple_method_copy_scanline_packed;

  klass->interpolate_scanline_planar_y =
      gst_deinterlace_simple_method_interpolate_scanline_planar_y;
  klass->copy_scanline_planar_y =
      gst_deinterlace_simple_method_copy_scanline_planar_y;
  klass->interpolate_scanline_planar_u =
      gst_deinterlace_simple_method_interpolate_scanline_planar_u;
  klass->copy_scanline_planar_u =
      gst_deinterlace_simple_method_copy_scanline_planar_u;
  klass->interpolate_scanline_planar_v =
      gst_deinterlace_simple_method_interpolate_scanline_planar_v;
  klass->copy_scanline_planar_v =
      gst_deinterlace_simple_method_copy_scanline_planar_v;

  /* planar high bitdepth formats use the same methods as 8bits planar,
   * (i.e,memcpy) but interpolate_scanline_planar_{y,u,v}_16bits methods will
   * be configured by each subclass */
  klass->copy_scanline_planar_y_16bits =
      gst_deinterlace_simple_method_copy_scanline_planar_y;
  klass->copy_scanline_planar_u_16bits =
      gst_deinterlace_simple_method_copy_scanline_planar_u;
  klass->copy_scanline_planar_v_16bits =
      gst_deinterlace_simple_method_copy_scanline_planar_v;
}

static void
gst_deinterlace_simple_method_init (GstDeinterlaceSimpleMethod * self)
{
}
