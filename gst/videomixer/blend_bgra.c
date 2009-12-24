/* 
 * Copyright (C) 2009 Alex Ugarte <augarte@vicomtech.org>
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

#define BLEND_NORMAL(B1,G1,R1,B2,G2,R2,B,G,R,alpha)     \
        B = ((B1*(255-alpha))+(B2*alpha))>>8;           \
        G = ((G1*(255-alpha))+(G2*alpha))>>8;           \
        R = ((R1*(255-alpha))+(R2*alpha))>>8;

#define BLEND_MODE BLEND_NORMAL

#define CREATE_FUNCTIONS(name, a, r, g, b) \
void \
gst_videomixer_blend_##name##_##name (guint8 * src, gint xpos, gint ypos, \
    gint src_width, gint src_height, gdouble src_alpha, \
    guint8 * dest, gint dest_width, gint dest_height) \
{ \
  gint alpha, s_alpha; \
  gint i, j; \
  gint src_stride, dest_stride; \
  gint src_add, dest_add; \
  gint B, G, R; \
  \
  src_stride = src_width * 4; \
  dest_stride = dest_width * 4; \
  \
  s_alpha = CLAMP ((gint) (src_alpha * 256), 0, 256); \
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
  src_add = src_stride - (4 * src_width); \
  dest_add = dest_stride - (4 * src_width); \
  \
  dest = dest + 4 * xpos + (ypos * dest_stride); \
  \
  for (i = 0; i < src_height; i++) { \
    for (j = 0; j < src_width; j++) { \
      alpha = (src[a] * s_alpha) >> 8; \
      BLEND_MODE (dest[b], dest[g], dest[r], src[b], src[g], src[r], \
          B, G, R, alpha); \
      dest[b] = B; \
      dest[g] = G; \
      dest[r] = R; \
      dest[a] = 0xff; \
      \
      src += 4; \
      dest += 4; \
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
  \
  for (i = 0; i < height; i++) { \
    for (j = 0; j < width; j++) { \
      dest[b] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       /* blue */ \
      dest[g] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       /* green */ \
      dest[r] = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       /* red */ \
      dest[a] = 0xFF;           /* alpha */ \
      dest += 4; \
    } \
  } \
} \
\
void \
gst_videomixer_fill_##name##_color (guint8 * dest, gint width, gint height, \
    gint colY, gint colU, gint colV) \
{ \
  gint red, green, blue; \
  gint i, j; \
  \
  red = CLAMP (1.164 * (colY - 16) + 1.596 * (colV - 128), 0, 255); \
  green = \
      CLAMP (1.164 * (colY - 16) - 0.813 * (colV - 128) - 0.391 * (colU - 128), \
      0, 255); \
  blue = CLAMP (1.164 * (colY - 16) + 2.018 * (colU - 128), 0, 255); \
  \
  for (i = 0; i < height; i++) { \
    for (j = 0; j < width; j++) { \
      dest[b] = blue; \
      dest[g] = green; \
      dest[r] = red; \
      dest[a] = 0xff; \
      dest += 4; \
    } \
  } \
}

CREATE_FUNCTIONS (argb, 0, 1, 2, 3);
CREATE_FUNCTIONS (bgra, 3, 2, 1, 0);

#undef BLEND_MODE
