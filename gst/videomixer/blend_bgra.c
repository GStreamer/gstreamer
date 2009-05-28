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

void
gst_videomixer_blend_bgra_bgra (guint8 * src, gint xpos, gint ypos,
    gint src_width, gint src_height, gdouble src_alpha,
    guint8 * dest, gint dest_width, gint dest_height)
{
  gint alpha, b_alpha;
  gint i, j;
  gint src_stride, dest_stride;
  gint src_add, dest_add;
  gint B, G, R;

  src_stride = src_width * 4;
  dest_stride = dest_width * 4;

  b_alpha = (gint) (src_alpha * 255);

  /* adjust src pointers for negative sizes */
  if (xpos < 0) {
    src += -xpos * 4;
    src_width -= -xpos;
    xpos = 0;
  }
  if (ypos < 0) {
    src += -ypos * src_stride;
    src_height -= -ypos;
    ypos = 0;
  }
  /* adjust width/height if the src is bigger than dest */
  if (xpos + src_width > dest_width) {
    src_width = dest_width - xpos;
  }
  if (ypos + src_height > dest_height) {
    src_height = dest_height - ypos;
  }

  src_add = src_stride - (4 * src_width);
  dest_add = dest_stride - (4 * src_width);

  dest = dest + 4 * xpos + (ypos * dest_stride);

  /* we convert a square of 2x2 samples to generate 4 Luma and 2 chroma samples */
  for (i = 0; i < src_height; i++) {
    for (j = 0; j < src_width; j++) {
      alpha = (src[3] * b_alpha) >> 8;
      BLEND_MODE (dest[0], dest[1], dest[2], src[0], src[1], src[2],
          B, G, R, alpha);
      dest[0] = B;
      dest[1] = G;
      dest[2] = R;
      dest[3] = 0xff;

      src += 4;
      dest += 4;
    }
    src += src_add;
    dest += dest_add;
  }
}

#undef BLEND_MODE

/* fill a buffer with a checkerboard pattern */
void
gst_videomixer_fill_bgra_checker (guint8 * dest, gint width, gint height)
{
  gint i, j;
  static int tab[] = { 80, 160, 80, 160 };

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       //blue
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       //green
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];       //red    
      *dest++ = 0xFF;           //alpha
    }
  }
}

void
gst_videomixer_fill_bgra_color (guint8 * dest, gint width, gint height,
    gint colY, gint colU, gint colV)
{
  gint red, green, blue;
  gint i, j;

  red = CLAMP (1.164 * (colY - 16) + 1.596 * (colV - 128), 0, 255);
  green =
      CLAMP (1.164 * (colY - 16) - 0.813 * (colV - 128) - 0.391 * (colU - 128),
      0, 255);
  blue = CLAMP (1.164 * (colY - 16) + 2.018 * (colU - 128), 0, 255);

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = blue;
      *dest++ = green;
      *dest++ = red;
      *dest++ = 0xff;
    }
  }
}

size_t
gst_videomixer_calculate_frame_size_bgra (gint width, gint height)
{
  return GST_ROUND_UP_4 (width) * height * 4;
}
