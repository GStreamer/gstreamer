/* 
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


#include <gst/gst.h>
#include <string.h>
#include "videomixer.h"

#define BLEND_NORMAL(R1,G1,B1,R2,G2,B2,alpha,R,G,B)     \
        R = ((R1*(255-alpha))+(R2*alpha))>>8;           \
        G = ((G1*(255-alpha))+(G2*alpha))>>8;           \
        B = ((B1*(255-alpha))+(B2*alpha))>>8;

#define BLEND_MODE BLEND_NORMAL

#define CREATE_FUNCTIONS(name, bpp, r, g, b) \
void \
gst_videomixer_blend_##name##_##name (guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  gint b_alpha; \
  gint i, j; \
  gint src_stride, dest_stride; \
  gint src_add, dest_add; \
  gint R, G, B; \
  \
  src_stride = GST_ROUND_UP_4 (src_width * bpp); \
  dest_stride = GST_ROUND_UP_4 (dest_width * bpp); \
  \
  b_alpha = CLAMP ((gint) (src_alpha * 255), 0, 255); \
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
  src_add = src_stride - (bpp * src_width); \
  dest_add = dest_stride - (bpp * src_width); \
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
      memcpy (dest, src, bpp * src_width); \
      src += src_stride; \
      dest += dest_stride; \
    } \
    return; \
  } \
  \
  for (i = 0; i < src_height; i++) { \
    for (j = 0; j < src_width; j++) { \
      BLEND_MODE (dest[r], dest[g], dest[b], src[r], src[g], src[b], \
          b_alpha, R, G, B); \
      dest[r] = R; \
      dest[g] = G; \
      dest[b] = B; \
      \
      src += bpp; \
      dest += bpp; \
    } \
    src += src_add; \
    dest += dest_add; \
  } \
} \
\
/* fill a buffer with a checkerboard pattern */ \
void \
gst_videomixer_fill_##name##_checker (guint8 * dest, gint width, gint height) \
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
} \
\
void \
gst_videomixer_fill_##name##_color (guint8 * dest, gint width, gint height, \
    gint colY, gint colU, gint colV) \
{ \
  gint red, green, blue; \
  gint i, j; \
  gint dest_add = GST_ROUND_UP_4 (width * bpp) - width * bpp; \
  \
  red = CLAMP (1.164 * (colY - 16) + 1.596 * (colV - 128), 0, 255); \
  green = \
      CLAMP (1.164 * (colY - 16) - 0.813 * (colV - 128) - 0.391 * (colU - 128), \
      0, 255); \
  blue = CLAMP (1.164 * (colY - 16) + 2.018 * (colU - 128), 0, 255); \
  \
  for (i = 0; i < height; i++) { \
    for (j = 0; j < width; j++) { \
      dest[r] = red; \
      dest[g] = green; \
      dest[b] = blue; \
      dest += bpp; \
    } \
    dest += dest_add; \
  } \
}

CREATE_FUNCTIONS (rgb, 3, 0, 1, 2);
CREATE_FUNCTIONS (bgr, 3, 2, 1, 0);
CREATE_FUNCTIONS (xrgb, 4, 1, 2, 3);
CREATE_FUNCTIONS (xbgr, 4, 3, 2, 1);
CREATE_FUNCTIONS (rgbx, 4, 0, 1, 2);
CREATE_FUNCTIONS (bgrx, 4, 2, 1, 0);
