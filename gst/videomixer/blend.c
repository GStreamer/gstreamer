/* 
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
 * Copyright (C) 2006 Mindfruit Bv.
 *   Author: Sjoerd Simons <sjoerd@luon.net>
 *   Author: Alex Ugarte <alexugarte@gmail.com>
 * Copyright (C) 2009 Alex Ugarte <augarte@vicomtech.org>
 * Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "blend.h"

#include <liboil/liboil.h>
#include <liboil/liboilcpu.h>
#include <liboil/liboilfunction.h>

#include <string.h>

#include <gst/video/video.h>

#define BLEND(D,S,alpha) (((D) * (256 - (alpha)) + (S) * (alpha)) >> 8)

#ifdef HAVE_GCC_ASM
#if defined(HAVE_CPU_I386) || defined(HAVE_CPU_X86_64)
#define BUILD_X86_ASM

#define GENERIC
#include "blend_mmx.h"
#undef GENERIC
#endif
#endif

/* Below are the implementations of everything */

inline static void
_blend_u8_c (guint8 * dest, const guint8 * src,
    gint src_stride, gint dest_stride, gint src_width, gint src_height,
    gint dest_width, gint b_alpha)
{
  gint i, j;
  gint src_add = src_stride - src_width;
  gint dest_add = dest_stride - dest_width;

  for (i = 0; i < src_height; i++) {
    for (j = 0; j < src_width; j++) {
      *dest = BLEND (*dest, *src, b_alpha);
      dest++;
      src++;
    }
    src += src_add;
    dest += dest_add;
  }
}

/* A32 is for AYUV, ARGB and BGRA */
#define BLEND_A32(name, LOOP) \
static void \
blend_##name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  guint s_alpha; \
  gint src_stride, dest_stride; \
  \
  src_stride = src_width * 4; \
  dest_stride = dest_width * 4; \
  \
  s_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (s_alpha == 0)) \
    return; \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    src += -xpos * 4; \
    src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    src += -ypos * src_stride; \
    src_height -= -ypos; \
    ypos = 0; \
  } \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    src_height = dest_height - ypos; \
  } \
  \
  dest = dest + 4 * xpos + (ypos * dest_stride); \
  \
  LOOP (dest, src, src_height, src_width, src_stride, dest_stride, s_alpha); \
}

#define BLEND_A32_LOOP_C(name, A, C1, C2, C3) \
static inline void \
_blend_loop_##name##_c (guint8 *dest, const guint8 *src, gint src_height, gint src_width, gint src_stride, gint dest_stride, guint s_alpha) { \
  gint i, j; \
  gint alpha; \
  gint src_add = src_stride - (4 * src_width); \
  gint dest_add = dest_stride - (4 * src_width); \
  \
  for (i = 0; i < src_height; i++) { \
    for (j = 0; j < src_width; j++) { \
      alpha = (src[A] * s_alpha) >> 8; \
      dest[A] = 0xff; \
      dest[C1] = BLEND(dest[C1], src[C1], alpha); \
      dest[C2] = BLEND(dest[C2], src[C2], alpha); \
      dest[C3] = BLEND(dest[C3], src[C3], alpha); \
      \
      src += 4; \
      dest += 4; \
    } \
    src += src_add; \
    dest += dest_add; \
  } \
}

BLEND_A32_LOOP_C (argb, 0, 1, 2, 3);
BLEND_A32_LOOP_C (bgra, 3, 2, 1, 0);
BLEND_A32 (argb_c, _blend_loop_argb_c);
BLEND_A32 (bgra_c, _blend_loop_bgra_c);

#define A32_CHECKER_C(name, RGB, A, C1, C2, C3) \
static void \
fill_checker_##name##_c (guint8 * dest, gint width, gint height) \
{ \
  gint i, j; \
  gint val; \
  static const gint tab[] = { 80, 160, 80, 160 }; \
  \
  if (!RGB) { \
    for (i = 0; i < height; i++) { \
      for (j = 0; j < width; j++) { \
        dest[A] = 0xff; \
        dest[C1] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)]; \
        dest[C2] = 128; \
        dest[C3] = 128; \
        dest += 4; \
      } \
    } \
  } else { \
    for (i = 0; i < height; i++) { \
      for (j = 0; j < width; j++) { \
        val = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)]; \
        dest[A] = 0xFF; \
        dest[C1] = val; \
        dest[C2] = val; \
        dest[C3] = val; \
        dest += 4; \
      } \
    } \
  } \
}

A32_CHECKER_C (argb, TRUE, 0, 1, 2, 3);
A32_CHECKER_C (bgra, TRUE, 3, 2, 1, 0);
A32_CHECKER_C (ayuv, FALSE, 0, 1, 2, 3);

#define YUV_TO_R(Y,U,V) (CLAMP (1.164 * (Y - 16) + 1.596 * (V - 128), 0, 255))
#define YUV_TO_G(Y,U,V) (CLAMP (1.164 * (Y - 16) - 0.813 * (V - 128) - 0.391 * (U - 128), 0, 255))
#define YUV_TO_B(Y,U,V) (CLAMP (1.164 * (Y - 16) + 2.018 * (U - 128), 0, 255))

#define A32_COLOR(name, RGB, LOOP) \
static void \
fill_color_##name (guint8 * dest, gint width, gint height, gint Y, gint U, gint V) \
{ \
  gint c1, c2, c3; \
  \
  if (RGB) { \
    c1 = YUV_TO_R (Y, U, V); \
    c2 = YUV_TO_G (Y, U, V); \
    c3 = YUV_TO_B (Y, U, V); \
  } else { \
    c1 = Y; \
    c2 = U; \
    c3 = V; \
  } \
  LOOP (dest, height, width, c1, c2, c3); \
}

#define A32_COLOR_LOOP_C(name, A, C1, C2, C3) \
static inline void \
_fill_color_loop_##name##_c (guint8 *dest, gint height, gint width, gint c1, gint c2, gint c3) { \
  gint i, j; \
  \
  for (i = 0; i < height; i++) { \
    for (j = 0; j < width; j++) { \
      dest[A] = 0xff; \
      dest[C1] = c1; \
      dest[C2] = c2; \
      dest[C3] = c3; \
      dest += 4; \
    } \
  } \
}

A32_COLOR_LOOP_C (ac1c2c3, 0, 1, 2, 3);
A32_COLOR_LOOP_C (c3c2c1a, 3, 2, 1, 0);
A32_COLOR_LOOP_C (ac3c2c1, 0, 3, 2, 1);
A32_COLOR_LOOP_C (c1c2c3a, 1, 2, 3, 0);
A32_COLOR (argb_c, TRUE, _fill_color_loop_ac1c2c3_c);
A32_COLOR (bgra_c, TRUE, _fill_color_loop_c3c2c1a_c);
A32_COLOR (abgr_c, TRUE, _fill_color_loop_ac3c2c1_c);
A32_COLOR (rgba_c, TRUE, _fill_color_loop_c1c2c3a_c);
A32_COLOR (ayuv_c, FALSE, _fill_color_loop_ac1c2c3_c);

/* Y444, Y42B, I420, YV12, Y41B */
#define PLANAR_YUV_BLEND(name,format_name,format_enum,x_round,y_round,MEMCPY,BLENDLOOP) \
inline static void \
_blend_##format_name##_##name (const guint8 * src, guint8 * dest, \
    gint src_stride, gint dest_stride, gint src_width, gint src_height, \
    gint dest_width, gdouble src_alpha) \
{ \
  gint i; \
  gint b_alpha; \
  \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (src_alpha == 0.0)) { \
    GST_INFO ("Fast copy (alpha == 0.0)"); \
    return; \
  } \
  \
  /* If it's completely opaque, we do a fast copy */ \
  if (G_UNLIKELY (src_alpha == 1.0)) { \
    GST_INFO ("Fast copy (alpha == 1.0)"); \
    for (i = 0; i < src_height; i++) { \
      MEMCPY (dest, src, src_width); \
      src += src_stride; \
      dest += dest_stride; \
    } \
    return; \
  } \
  \
  b_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  BLENDLOOP(dest, src, src_stride, dest_stride, src_width, src_height, dest_width, b_alpha); \
} \
\
static void \
blend_##format_name##_##name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  const guint8 *b_src; \
  guint8 *b_dest; \
  gint b_src_width = src_width; \
  gint b_src_height = src_height; \
  gint xoffset = 0; \
  gint yoffset = 0; \
  gint src_comp_rowstride, dest_comp_rowstride; \
  gint src_comp_height, dest_comp_height; \
  gint src_comp_width, dest_comp_width; \
  gint comp_ypos, comp_xpos; \
  gint comp_yoffset, comp_xoffset; \
  \
  xpos = x_round (xpos); \
  ypos = y_round (ypos); \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    xoffset = -xpos; \
    b_src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    yoffset += -ypos; \
    b_src_height -= -ypos; \
    ypos = 0; \
  } \
  /* If x or y offset are larger then the source it's outside of the picture */ \
  if (xoffset > src_width || yoffset > src_width) { \
    return; \
  } \
  \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    b_src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    b_src_height = dest_height - ypos; \
  } \
  if (b_src_width < 0 || b_src_height < 0) { \
    return; \
  } \
  \
  /* First mix Y, then U, then V */ \
  b_src = src + gst_video_format_get_component_offset (format_enum, 0, src_width, src_height); \
  b_dest = dest + gst_video_format_get_component_offset (format_enum, 0, dest_width, dest_height); \
  src_comp_rowstride = gst_video_format_get_row_stride (format_enum, 0, src_width); \
  dest_comp_rowstride = gst_video_format_get_row_stride (format_enum, 0, dest_width); \
  src_comp_height = gst_video_format_get_component_height (format_enum, 0, b_src_height); \
  dest_comp_height = gst_video_format_get_component_height (format_enum, 0, dest_height); \
  src_comp_width = gst_video_format_get_component_width (format_enum, 0, b_src_width); \
  dest_comp_width = gst_video_format_get_component_width (format_enum, 0, dest_width); \
  comp_xpos = (xpos == 0) ? 0 : gst_video_format_get_component_width (format_enum, 0, xpos); \
  comp_ypos = (ypos == 0) ? 0 : gst_video_format_get_component_height (format_enum, 0, ypos); \
  comp_xoffset = (xoffset == 0) ? 0 : gst_video_format_get_component_width (format_enum, 0, xoffset); \
  comp_yoffset = (yoffset == 0) ? 0 : gst_video_format_get_component_height (format_enum, 0, yoffset); \
  _blend_##format_name##_##name (b_src + comp_xoffset + comp_yoffset * src_comp_rowstride, \
      b_dest + comp_xpos + comp_ypos * dest_comp_rowstride, \
      src_comp_rowstride, \
      dest_comp_rowstride, src_comp_width, src_comp_height, \
      dest_comp_width, src_alpha); \
  \
  b_src = src + gst_video_format_get_component_offset (format_enum, 1, src_width, src_height); \
  b_dest = dest + gst_video_format_get_component_offset (format_enum, 1, dest_width, dest_height); \
  src_comp_rowstride = gst_video_format_get_row_stride (format_enum, 1, src_width); \
  dest_comp_rowstride = gst_video_format_get_row_stride (format_enum, 1, dest_width); \
  src_comp_height = gst_video_format_get_component_height (format_enum, 1, b_src_height); \
  dest_comp_height = gst_video_format_get_component_height (format_enum, 1, dest_height); \
  src_comp_width = gst_video_format_get_component_width (format_enum, 1, b_src_width); \
  dest_comp_width = gst_video_format_get_component_width (format_enum, 1, dest_width); \
  comp_xpos = (xpos == 0) ? 0 : gst_video_format_get_component_width (format_enum, 1, xpos); \
  comp_ypos = (ypos == 0) ? 0 : gst_video_format_get_component_height (format_enum, 1, ypos); \
  comp_xoffset = (xoffset == 0) ? 0 : gst_video_format_get_component_width (format_enum, 1, xoffset); \
  comp_yoffset = (yoffset == 0) ? 0 : gst_video_format_get_component_height (format_enum, 1, yoffset); \
  _blend_##format_name##_##name (b_src + comp_xoffset + comp_yoffset * src_comp_rowstride, \
      b_dest + comp_xpos + comp_ypos * dest_comp_rowstride, \
      src_comp_rowstride, \
      dest_comp_rowstride, src_comp_width, src_comp_height, \
      dest_comp_width, src_alpha); \
  \
  b_src = src + gst_video_format_get_component_offset (format_enum, 2, src_width, src_height); \
  b_dest = dest + gst_video_format_get_component_offset (format_enum, 2, dest_width, dest_height); \
  src_comp_rowstride = gst_video_format_get_row_stride (format_enum, 2, src_width); \
  dest_comp_rowstride = gst_video_format_get_row_stride (format_enum, 2, dest_width); \
  src_comp_height = gst_video_format_get_component_height (format_enum, 2, b_src_height); \
  dest_comp_height = gst_video_format_get_component_height (format_enum, 2, dest_height); \
  src_comp_width = gst_video_format_get_component_width (format_enum, 2, b_src_width); \
  dest_comp_width = gst_video_format_get_component_width (format_enum, 2, dest_width); \
  comp_xpos = (xpos == 0) ? 0 : gst_video_format_get_component_width (format_enum, 2, xpos); \
  comp_ypos = (ypos == 0) ? 0 : gst_video_format_get_component_height (format_enum, 2, ypos); \
  comp_xoffset = (xoffset == 0) ? 0 : gst_video_format_get_component_width (format_enum, 2, xoffset); \
  comp_yoffset = (yoffset == 0) ? 0 : gst_video_format_get_component_height (format_enum, 2, yoffset); \
  _blend_##format_name##_##name (b_src + comp_xoffset + comp_yoffset * src_comp_rowstride, \
      b_dest + comp_xpos + comp_ypos * dest_comp_rowstride, \
      src_comp_rowstride, \
      dest_comp_rowstride, src_comp_width, src_comp_height, \
      dest_comp_width, src_alpha); \
}

#define PLANAR_YUV_FILL_CHECKER(name, format_name, format_enum, MEMSET) \
static void \
fill_checker_##format_name##_##name (guint8 * dest, gint width, gint height) \
{ \
  gint i, j; \
  static const int tab[] = { 80, 160, 80, 160 }; \
  guint8 *p; \
  gint comp_width, comp_height; \
  gint rowstride; \
  \
  p = dest + gst_video_format_get_component_offset (format_enum, 0, width, height); \
  comp_width = gst_video_format_get_component_width (format_enum, 0, width); \
  comp_height = gst_video_format_get_component_height (format_enum, 0, height); \
  rowstride = gst_video_format_get_row_stride (format_enum, 0, width); \
  \
  for (i = 0; i < comp_height; i++) { \
    for (j = 0; j < comp_width; j++) { \
      *p++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)]; \
    } \
    p += rowstride - comp_width; \
  } \
  \
  p = dest + gst_video_format_get_component_offset (format_enum, 1, width, height); \
  comp_width = gst_video_format_get_component_width (format_enum, 1, width); \
  comp_height = gst_video_format_get_component_height (format_enum, 1, height); \
  rowstride = gst_video_format_get_row_stride (format_enum, 1, width); \
  \
  for (i = 0; i < comp_height; i++) { \
    MEMSET (p, 0x80, comp_width); \
    p += rowstride; \
  } \
  \
  p = dest + gst_video_format_get_component_offset (format_enum, 2, width, height); \
  comp_width = gst_video_format_get_component_width (format_enum, 2, width); \
  comp_height = gst_video_format_get_component_height (format_enum, 2, height); \
  rowstride = gst_video_format_get_row_stride (format_enum, 2, width); \
  \
  for (i = 0; i < comp_height; i++) { \
    MEMSET (p, 0x80, comp_width); \
    p += rowstride; \
  } \
}

#define PLANAR_YUV_FILL_COLOR(name,format_name,format_enum,MEMSET) \
static void \
fill_color_##format_name##_##name (guint8 * dest, gint width, gint height, \
    gint colY, gint colU, gint colV) \
{ \
  guint8 *p; \
  gint comp_width, comp_height; \
  gint rowstride; \
  gint i; \
  \
  p = dest + gst_video_format_get_component_offset (format_enum, 0, width, height); \
  comp_width = gst_video_format_get_component_width (format_enum, 0, width); \
  comp_height = gst_video_format_get_component_height (format_enum, 0, height); \
  rowstride = gst_video_format_get_row_stride (format_enum, 0, width); \
  \
  for (i = 0; i < comp_height; i++) { \
    MEMSET (p, colY, comp_width); \
    p += rowstride; \
  } \
  \
  p = dest + gst_video_format_get_component_offset (format_enum, 1, width, height); \
  comp_width = gst_video_format_get_component_width (format_enum, 1, width); \
  comp_height = gst_video_format_get_component_height (format_enum, 1, height); \
  rowstride = gst_video_format_get_row_stride (format_enum, 1, width); \
  \
  for (i = 0; i < comp_height; i++) { \
    MEMSET (p, colU, comp_width); \
    p += rowstride; \
  } \
  \
  p = dest + gst_video_format_get_component_offset (format_enum, 2, width, height); \
  comp_width = gst_video_format_get_component_width (format_enum, 2, width); \
  comp_height = gst_video_format_get_component_height (format_enum, 2, height); \
  rowstride = gst_video_format_get_row_stride (format_enum, 2, width); \
  \
  for (i = 0; i < comp_height; i++) { \
    MEMSET (p, colV, comp_width); \
    p += rowstride; \
  } \
}

#define GST_ROUND_UP_1(x) (x)

PLANAR_YUV_BLEND (c, i420, GST_VIDEO_FORMAT_I420, GST_ROUND_UP_2,
    GST_ROUND_UP_2, memcpy, _blend_u8_c);
PLANAR_YUV_FILL_CHECKER (c, i420, GST_VIDEO_FORMAT_I420, memset);
PLANAR_YUV_FILL_COLOR (c, i420, GST_VIDEO_FORMAT_I420, memset);
PLANAR_YUV_FILL_COLOR (c, yv12, GST_VIDEO_FORMAT_YV12, memset);
PLANAR_YUV_BLEND (c, y444, GST_VIDEO_FORMAT_Y444, GST_ROUND_UP_1,
    GST_ROUND_UP_1, memcpy, _blend_u8_c);
PLANAR_YUV_FILL_CHECKER (c, y444, GST_VIDEO_FORMAT_Y444, memset);
PLANAR_YUV_FILL_COLOR (c, y444, GST_VIDEO_FORMAT_Y444, memset);
PLANAR_YUV_BLEND (c, y42b, GST_VIDEO_FORMAT_Y42B, GST_ROUND_UP_2,
    GST_ROUND_UP_1, memcpy, _blend_u8_c);
PLANAR_YUV_FILL_CHECKER (c, y42b, GST_VIDEO_FORMAT_Y42B, memset);
PLANAR_YUV_FILL_COLOR (c, y42b, GST_VIDEO_FORMAT_Y42B, memset);
PLANAR_YUV_BLEND (c, y41b, GST_VIDEO_FORMAT_Y41B, GST_ROUND_UP_4,
    GST_ROUND_UP_1, memcpy, _blend_u8_c);
PLANAR_YUV_FILL_CHECKER (c, y41b, GST_VIDEO_FORMAT_Y41B, memset);
PLANAR_YUV_FILL_COLOR (c, y41b, GST_VIDEO_FORMAT_Y41B, memset);

/* RGB, BGR, xRGB, xBGR, RGBx, BGRx */

#define RGB_BLEND(name, bpp, MEMCPY, BLENDLOOP) \
static void \
blend_##name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  gint b_alpha; \
  gint i; \
  gint src_stride, dest_stride; \
  \
  src_stride = GST_ROUND_UP_4 (src_width * bpp); \
  dest_stride = GST_ROUND_UP_4 (dest_width * bpp); \
  \
  b_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    src += -xpos * bpp; \
    src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    src += -ypos * src_stride; \
    src_height -= -ypos; \
    ypos = 0; \
  } \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    src_height = dest_height - ypos; \
  } \
  \
  dest = dest + bpp * xpos + (ypos * dest_stride); \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (src_alpha == 0.0)) { \
    GST_INFO ("Fast copy (alpha == 0.0)"); \
    return; \
  } \
  \
  /* If it's completely opaque, we do a fast copy */ \
  if (G_UNLIKELY (src_alpha == 1.0)) { \
    GST_INFO ("Fast copy (alpha == 1.0)"); \
    for (i = 0; i < src_height; i++) { \
      MEMCPY (dest, src, bpp * src_width); \
      src += src_stride; \
      dest += dest_stride; \
    } \
    return; \
  } \
  \
  BLENDLOOP(dest, src, src_stride, dest_stride, bpp * src_width, src_height, bpp * dest_width, b_alpha); \
}

#define RGB_FILL_CHECKER_C(name, bpp, r, g, b) \
static void \
fill_checker_##name##_c (guint8 * dest, gint width, gint height) \
{ \
  gint i, j; \
  static const int tab[] = { 80, 160, 80, 160 }; \
  gint dest_add = GST_ROUND_UP_4 (width * bpp) - width * bpp; \
  \
  for (i = 0; i < height; i++) { \
    for (j = 0; j < width; j++) { \
      dest[r] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       /* red */ \
      dest[g] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       /* green */ \
      dest[b] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       /* blue */ \
      dest += bpp; \
    } \
    dest += dest_add; \
  } \
}

#define RGB_FILL_COLOR(name, bpp, MEMSET_RGB) \
static void \
fill_color_##name (guint8 * dest, gint width, gint height, \
    gint colY, gint colU, gint colV) \
{ \
  gint red, green, blue; \
  gint i; \
  gint dest_stride = GST_ROUND_UP_4 (width * bpp); \
  \
  red = YUV_TO_R (colY, colU, colV); \
  green = YUV_TO_G (colY, colU, colV); \
  blue = YUV_TO_B (colY, colU, colV); \
  \
  for (i = 0; i < height; i++) { \
    MEMSET_RGB (dest, red, green, blue, width); \
    dest += dest_stride; \
  } \
}

#define MEMSET_RGB_C(name, bpp, r, g, b) \
static inline void \
_memset_##name##_c (guint8* dest, gint red, gint green, gint blue, gint width) { \
  gint j; \
  \
  for (j = 0; j < width; j++) { \
    dest[r] = red; \
    dest[g] = green; \
    dest[b] = blue; \
    dest += bpp; \
  } \
}

RGB_BLEND (rgb_c, 3, memcpy, _blend_u8_c);
RGB_FILL_CHECKER_C (rgb, 3, 0, 1, 2);
MEMSET_RGB_C (rgb, 3, 0, 1, 2);
RGB_FILL_COLOR (rgb_c, 3, _memset_rgb_c);

MEMSET_RGB_C (bgr, 3, 2, 1, 0);
RGB_FILL_COLOR (bgr_c, 3, _memset_bgr_c);

RGB_BLEND (xrgb_c, 4, memcpy, _blend_u8_c);
RGB_FILL_CHECKER_C (xrgb, 4, 1, 2, 3);
MEMSET_RGB_C (xrgb, 4, 1, 2, 3);
RGB_FILL_COLOR (xrgb_c, 4, _memset_xrgb_c);

MEMSET_RGB_C (xbgr, 4, 3, 2, 1);
RGB_FILL_COLOR (xbgr_c, 4, _memset_xbgr_c);

MEMSET_RGB_C (rgbx, 4, 0, 1, 2);
RGB_FILL_COLOR (rgbx_c, 4, _memset_rgbx_c);

MEMSET_RGB_C (bgrx, 4, 2, 1, 0);
RGB_FILL_COLOR (bgrx_c, 4, _memset_bgrx_c);

/* YUY2, YVYU, UYVY */

#define PACKED_422_BLEND(name, MEMCPY, BLENDLOOP) \
static void \
blend_##name (const guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  gint b_alpha; \
  gint i; \
  gint src_stride, dest_stride; \
  \
  src_stride = GST_ROUND_UP_4 (src_width * 2); \
  dest_stride = GST_ROUND_UP_4 (dest_width * 2); \
  \
  b_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
  \
  xpos = GST_ROUND_UP_2 (xpos); \
  \
  /* adjust src pointers for negative sizes */ \
  if (xpos < 0) { \
    src += -xpos * 2; \
    src_width -= -xpos; \
    xpos = 0; \
  } \
  if (ypos < 0) { \
    src += -ypos * src_stride; \
    src_height -= -ypos; \
    ypos = 0; \
  } \
  \
  /* adjust width/height if the src is bigger than dest */ \
  if (xpos + src_width > dest_width) { \
    src_width = dest_width - xpos; \
  } \
  if (ypos + src_height > dest_height) { \
    src_height = dest_height - ypos; \
  } \
  \
  dest = dest + 2 * xpos + (ypos * dest_stride); \
  /* If it's completely transparent... we just return */ \
  if (G_UNLIKELY (src_alpha == 0.0)) { \
    GST_INFO ("Fast copy (alpha == 0.0)"); \
    return; \
  } \
  \
  /* If it's completely opaque, we do a fast copy */ \
  if (G_UNLIKELY (src_alpha == 1.0)) { \
    GST_INFO ("Fast copy (alpha == 1.0)"); \
    for (i = 0; i < src_height; i++) { \
      MEMCPY (dest, src, 2 * src_width); \
      src += src_stride; \
      dest += dest_stride; \
    } \
    return; \
  } \
  \
  BLENDLOOP(dest, src, src_stride, dest_stride, 2 * src_width, src_height, 2 * dest_width, b_alpha); \
}

#define PACKED_422_FILL_CHECKER_C(name, Y1, U, Y2, V) \
static void \
fill_checker_##name##_c (guint8 * dest, gint width, gint height) \
{ \
  gint i, j; \
  static const int tab[] = { 80, 160, 80, 160 }; \
  gint dest_add; \
  \
  width = GST_ROUND_UP_2 (width); \
  dest_add = GST_ROUND_UP_4 (width * 2) - width * 2; \
  width /= 2; \
  \
  for (i = 0; i < height; i++) { \
    for (j = 0; j < width; j++) { \
      dest[Y1] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)]; \
      dest[Y2] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)]; \
      dest[U] = 128; \
      dest[V] = 128; \
      dest += 4; \
    } \
    dest += dest_add; \
  } \
}

#define PACKED_422_FILL_COLOR(name, Y1, U, Y2, V) \
static void \
fill_color_##name (guint8 * dest, gint width, gint height, \
    gint colY, gint colU, gint colV) \
{ \
  gint i, j; \
  gint dest_add; \
  \
  width = GST_ROUND_UP_2 (width); \
  dest_add = GST_ROUND_UP_4 (width * 2) - width * 2; \
  width /= 2; \
  \
  for (i = 0; i < height; i++) { \
    for (j = 0; j < width; j++) { \
      dest[Y1] = colY; \
      dest[Y2] = colY; \
      dest[U] = colU; \
      dest[V] = colV; \
      dest += 4; \
    } \
    dest += dest_add; \
  } \
}

PACKED_422_BLEND (yuy2_c, memcpy, _blend_u8_c);
PACKED_422_FILL_CHECKER_C (yuy2, 0, 1, 2, 3);
PACKED_422_FILL_CHECKER_C (uyvy, 1, 0, 3, 2);
PACKED_422_FILL_COLOR (yuy2_c, 0, 1, 2, 3);
PACKED_422_FILL_COLOR (yvyu_c, 0, 3, 2, 1);
PACKED_422_FILL_COLOR (uyvy_c, 1, 0, 3, 2);

/* MMX Implementations */
#ifdef BUILD_X86_ASM

#define MEMSET_xRGB_MMX(name, r, g, b) \
static inline void \
_memset_##name##_mmx (guint8* dest, gint red, gint green, gint blue, gint width) { \
  guint32 val = (red << r) | (green << g) | (blue << b); \
  \
  _memset_u32_mmx ((guint32 *) dest, val, width); \
}

#define A32
#define NAME_BLEND _blend_loop_argb_mmx
#define A_OFF 0
#include "blend_mmx.h"
#undef NAME_BLEND
#undef A_OFF

#define NAME_BLEND _blend_loop_bgra_mmx
#define A_OFF 24
#include "blend_mmx.h"
#undef NAME_BLEND
#undef A_OFF
#undef A32

BLEND_A32 (argb_mmx, _blend_loop_argb_mmx);
BLEND_A32 (bgra_mmx, _blend_loop_bgra_mmx);

#define A32_COLOR_LOOP_MMX(name, A, C1, C2, C3) \
static inline void \
_fill_color_loop_##name##_mmx (guint8 *dest, gint height, gint width, gint c1, gint c2, gint c3) { \
  guint32 val = (0xff << A) | (c1 << C1) | (c2 << C2) | (c3 << C3); \
  \
  _memset_u32_mmx ((guint32 *) dest, val, height*width); \
}

A32_COLOR_LOOP_MMX (argb, 0, 8, 16, 24);
A32_COLOR_LOOP_MMX (abgr, 0, 24, 16, 8);
A32_COLOR_LOOP_MMX (rgba, 24, 0, 8, 16);
A32_COLOR_LOOP_MMX (bgra, 24, 16, 8, 0);

A32_COLOR (argb_mmx, TRUE, _fill_color_loop_argb_mmx);
A32_COLOR (bgra_mmx, TRUE, _fill_color_loop_bgra_mmx);
A32_COLOR (abgr_mmx, TRUE, _fill_color_loop_abgr_mmx);
A32_COLOR (rgba_mmx, TRUE, _fill_color_loop_rgba_mmx);
A32_COLOR (ayuv_mmx, FALSE, _fill_color_loop_argb_mmx);

PLANAR_YUV_BLEND (mmx, i420, GST_VIDEO_FORMAT_I420, GST_ROUND_UP_2,
    GST_ROUND_UP_2, _memcpy_u8_mmx, _blend_u8_mmx);
PLANAR_YUV_FILL_CHECKER (mmx, i420, GST_VIDEO_FORMAT_I420, _memset_u8_mmx);
PLANAR_YUV_FILL_COLOR (mmx, i420, GST_VIDEO_FORMAT_I420, _memset_u8_mmx);
PLANAR_YUV_FILL_COLOR (mmx, yv12, GST_VIDEO_FORMAT_YV12, _memset_u8_mmx);
PLANAR_YUV_BLEND (mmx, y444, GST_VIDEO_FORMAT_Y444, GST_ROUND_UP_1,
    GST_ROUND_UP_1, _memcpy_u8_mmx, _blend_u8_mmx);
PLANAR_YUV_FILL_CHECKER (mmx, y444, GST_VIDEO_FORMAT_Y444, _memset_u8_mmx);
PLANAR_YUV_FILL_COLOR (mmx, y444, GST_VIDEO_FORMAT_Y444, _memset_u8_mmx);
PLANAR_YUV_BLEND (mmx, y42b, GST_VIDEO_FORMAT_Y42B, GST_ROUND_UP_2,
    GST_ROUND_UP_1, _memcpy_u8_mmx, _blend_u8_mmx);
PLANAR_YUV_FILL_CHECKER (mmx, y42b, GST_VIDEO_FORMAT_Y42B, _memset_u8_mmx);
PLANAR_YUV_FILL_COLOR (mmx, y42b, GST_VIDEO_FORMAT_Y42B, _memset_u8_mmx);
PLANAR_YUV_BLEND (mmx, y41b, GST_VIDEO_FORMAT_Y41B, GST_ROUND_UP_4,
    GST_ROUND_UP_1, _memcpy_u8_mmx, _blend_u8_mmx);
PLANAR_YUV_FILL_CHECKER (mmx, y41b, GST_VIDEO_FORMAT_Y41B, _memset_u8_mmx);
PLANAR_YUV_FILL_COLOR (mmx, y41b, GST_VIDEO_FORMAT_Y41B, _memset_u8_mmx);

RGB_BLEND (rgb_mmx, 3, _memcpy_u8_mmx, _blend_u8_mmx);

RGB_BLEND (xrgb_mmx, 4, _memcpy_u8_mmx, _blend_u8_mmx);
MEMSET_xRGB_MMX (xrgb, 16, 8, 0);
RGB_FILL_COLOR (xrgb_mmx, 4, _memset_xrgb_mmx);

MEMSET_xRGB_MMX (xbgr, 0, 8, 16);
RGB_FILL_COLOR (xbgr_mmx, 4, _memset_xbgr_mmx);

MEMSET_xRGB_MMX (rgbx, 24, 16, 8);
RGB_FILL_COLOR (rgbx_mmx, 4, _memset_rgbx_mmx);

MEMSET_xRGB_MMX (bgrx, 8, 16, 24);
RGB_FILL_COLOR (bgrx_mmx, 4, _memset_bgrx_mmx);

PACKED_422_BLEND (yuy2_mmx, _memcpy_u8_mmx, _blend_u8_mmx);
#endif

/* Init function */
BlendFunction gst_video_mixer_blend_argb;
BlendFunction gst_video_mixer_blend_bgra;
/* AYUV/ABGR is equal to ARGB, RGBA is equal to BGRA */
BlendFunction gst_video_mixer_blend_y444;
BlendFunction gst_video_mixer_blend_y42b;
BlendFunction gst_video_mixer_blend_i420;
/* I420 is equal to YV12 */
BlendFunction gst_video_mixer_blend_y41b;
BlendFunction gst_video_mixer_blend_rgb;
/* BGR is equal to RGB */
BlendFunction gst_video_mixer_blend_rgbx;
/* BGRx, xRGB, xBGR are equal to RGBx */
BlendFunction gst_video_mixer_blend_yuy2;
/* YVYU and UYVY are equal to YUY2 */

FillCheckerFunction gst_video_mixer_fill_checker_argb;
FillCheckerFunction gst_video_mixer_fill_checker_bgra;
/* ABGR is equal to ARGB, RGBA is equal to BGRA */
FillCheckerFunction gst_video_mixer_fill_checker_ayuv;
FillCheckerFunction gst_video_mixer_fill_checker_y444;
FillCheckerFunction gst_video_mixer_fill_checker_y42b;
FillCheckerFunction gst_video_mixer_fill_checker_i420;
/* I420 is equal to YV12 */
FillCheckerFunction gst_video_mixer_fill_checker_y41b;
FillCheckerFunction gst_video_mixer_fill_checker_rgb;
/* BGR is equal to RGB */
FillCheckerFunction gst_video_mixer_fill_checker_xrgb;
/* BGRx, xRGB, xBGR are equal to RGBx */
FillCheckerFunction gst_video_mixer_fill_checker_yuy2;
/* YVYU is equal to YUY2 */
FillCheckerFunction gst_video_mixer_fill_checker_uyvy;

FillColorFunction gst_video_mixer_fill_color_argb;
FillColorFunction gst_video_mixer_fill_color_bgra;
FillColorFunction gst_video_mixer_fill_color_abgr;
FillColorFunction gst_video_mixer_fill_color_rgba;
FillColorFunction gst_video_mixer_fill_color_ayuv;
FillColorFunction gst_video_mixer_fill_color_y444;
FillColorFunction gst_video_mixer_fill_color_y42b;
FillColorFunction gst_video_mixer_fill_color_i420;
FillColorFunction gst_video_mixer_fill_color_yv12;
FillColorFunction gst_video_mixer_fill_color_y41b;
FillColorFunction gst_video_mixer_fill_color_rgb;
FillColorFunction gst_video_mixer_fill_color_bgr;
FillColorFunction gst_video_mixer_fill_color_xrgb;
FillColorFunction gst_video_mixer_fill_color_xbgr;
FillColorFunction gst_video_mixer_fill_color_rgbx;
FillColorFunction gst_video_mixer_fill_color_bgrx;
FillColorFunction gst_video_mixer_fill_color_yuy2;
FillColorFunction gst_video_mixer_fill_color_yvyu;
FillColorFunction gst_video_mixer_fill_color_uyvy;

void
gst_video_mixer_init_blend (void)
{
  guint cpu_flags;

  oil_init ();
  cpu_flags = oil_cpu_get_flags ();

  gst_video_mixer_blend_argb = blend_argb_c;
  gst_video_mixer_blend_bgra = blend_bgra_c;
  gst_video_mixer_blend_i420 = blend_i420_c;
  gst_video_mixer_blend_y444 = blend_y444_c;
  gst_video_mixer_blend_y42b = blend_y42b_c;
  gst_video_mixer_blend_y41b = blend_y41b_c;
  gst_video_mixer_blend_rgb = blend_rgb_c;
  gst_video_mixer_blend_xrgb = blend_xrgb_c;
  gst_video_mixer_blend_yuy2 = blend_yuy2_c;

  gst_video_mixer_fill_checker_argb = fill_checker_argb_c;
  gst_video_mixer_fill_checker_bgra = fill_checker_bgra_c;
  gst_video_mixer_fill_checker_ayuv = fill_checker_ayuv_c;
  gst_video_mixer_fill_checker_i420 = fill_checker_i420_c;
  gst_video_mixer_fill_checker_y444 = fill_checker_y444_c;
  gst_video_mixer_fill_checker_y42b = fill_checker_y42b_c;
  gst_video_mixer_fill_checker_y41b = fill_checker_y41b_c;
  gst_video_mixer_fill_checker_rgb = fill_checker_rgb_c;
  gst_video_mixer_fill_checker_xrgb = fill_checker_xrgb_c;
  gst_video_mixer_fill_checker_yuy2 = fill_checker_yuy2_c;
  gst_video_mixer_fill_checker_uyvy = fill_checker_uyvy_c;

  gst_video_mixer_fill_color_argb = fill_color_argb_c;
  gst_video_mixer_fill_color_bgra = fill_color_bgra_c;
  gst_video_mixer_fill_color_abgr = fill_color_abgr_c;
  gst_video_mixer_fill_color_rgba = fill_color_rgba_c;
  gst_video_mixer_fill_color_ayuv = fill_color_ayuv_c;
  gst_video_mixer_fill_color_i420 = fill_color_i420_c;
  gst_video_mixer_fill_color_yv12 = fill_color_yv12_c;
  gst_video_mixer_fill_color_y444 = fill_color_y444_c;
  gst_video_mixer_fill_color_y42b = fill_color_y42b_c;
  gst_video_mixer_fill_color_y41b = fill_color_y41b_c;
  gst_video_mixer_fill_color_rgb = fill_color_rgb_c;
  gst_video_mixer_fill_color_bgr = fill_color_bgr_c;
  gst_video_mixer_fill_color_xrgb = fill_color_xrgb_c;
  gst_video_mixer_fill_color_xbgr = fill_color_xbgr_c;
  gst_video_mixer_fill_color_rgbx = fill_color_rgbx_c;
  gst_video_mixer_fill_color_bgrx = fill_color_bgrx_c;
  gst_video_mixer_fill_color_yuy2 = fill_color_yuy2_c;
  gst_video_mixer_fill_color_yvyu = fill_color_yvyu_c;
  gst_video_mixer_fill_color_uyvy = fill_color_uyvy_c;

#ifdef BUILD_X86_ASM
  if (cpu_flags & OIL_IMPL_FLAG_MMX) {
    gst_video_mixer_blend_argb = blend_argb_mmx;
    gst_video_mixer_blend_bgra = blend_bgra_mmx;
    gst_video_mixer_blend_i420 = blend_i420_mmx;
    gst_video_mixer_blend_y444 = blend_y444_mmx;
    gst_video_mixer_blend_y42b = blend_y42b_mmx;
    gst_video_mixer_blend_y41b = blend_y41b_mmx;
    gst_video_mixer_blend_rgb = blend_rgb_mmx;
    gst_video_mixer_blend_xrgb = blend_xrgb_mmx;
    gst_video_mixer_blend_yuy2 = blend_yuy2_mmx;

    gst_video_mixer_fill_checker_i420 = fill_checker_i420_mmx;
    gst_video_mixer_fill_checker_y444 = fill_checker_y444_mmx;
    gst_video_mixer_fill_checker_y42b = fill_checker_y42b_mmx;
    gst_video_mixer_fill_checker_y41b = fill_checker_y41b_mmx;

    gst_video_mixer_fill_color_argb = fill_color_argb_mmx;
    gst_video_mixer_fill_color_bgra = fill_color_bgra_mmx;
    gst_video_mixer_fill_color_abgr = fill_color_abgr_mmx;
    gst_video_mixer_fill_color_rgba = fill_color_rgba_mmx;
    gst_video_mixer_fill_color_ayuv = fill_color_ayuv_mmx;
    gst_video_mixer_fill_color_i420 = fill_color_i420_mmx;
    gst_video_mixer_fill_color_yv12 = fill_color_yv12_mmx;
    gst_video_mixer_fill_color_y444 = fill_color_y444_mmx;
    gst_video_mixer_fill_color_y42b = fill_color_y42b_mmx;
    gst_video_mixer_fill_color_y41b = fill_color_y41b_mmx;
    gst_video_mixer_fill_color_xrgb = fill_color_xrgb_mmx;
    gst_video_mixer_fill_color_xbgr = fill_color_xbgr_mmx;
    gst_video_mixer_fill_color_rgbx = fill_color_rgbx_mmx;
    gst_video_mixer_fill_color_bgrx = fill_color_bgrx_mmx;
  }
#endif
}
