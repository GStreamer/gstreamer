/* 
 * Copyright (C) 2006 Mindfruit Bv.
 *   Author: Sjoerd Simons <sjoerd@luon.net>
 *   Author: Alex Ugarte <alexugarte@gmail.com>
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

#define BLEND_NORMAL(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)     \
        Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;           \
        U = ((U1*(255-alpha))+(U2*alpha))>>8;           \
        V = ((V1*(255-alpha))+(V2*alpha))>>8;

#define BLEND_ADD(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)                \
        Y = Y1+((Y2*alpha)>>8);                                 \
        U = U1+(((127*(255-alpha)+(U2*alpha)))>>8)-127;         \
        V = V1+(((127*(255-alpha)+(V2*alpha)))>>8)-127;         \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        }                                                       \
        U = MIN (U,255);                                        \
        V = MIN (V,255);

#define BLEND_SUBTRACT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)           \
        Y = Y1-((Y2*alpha)>>8);                                 \
        U = U1+(((127*(255-alpha)+(U2*alpha)))>>8)-127;         \
        V = V1+(((127*(255-alpha)+(V2*alpha)))>>8)-127;         \
        if (Y<0) {                                              \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }

#define BLEND_DARKEN(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)     \
        if (Y1 < Y2) {                                  \
          Y = Y1; U = U1; V = V1;                       \
        }                                               \
        else {                                          \
          Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;         \
          U = ((U1*(255-alpha))+(U2*alpha))>>8;         \
          V = ((V1*(255-alpha))+(V2*alpha))>>8;         \
        }

#define BLEND_LIGHTEN(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)    \
        if (Y1 > Y2) {                                  \
          Y = Y1; U = U1; V = V1;                       \
        }                                               \
        else {                                          \
          Y = ((Y1*(255-alpha))+(Y2*alpha))>>8;         \
          U = ((U1*(255-alpha))+(U2*alpha))>>8;         \
          V = ((V1*(255-alpha))+(V2*alpha))>>8;         \
        }

#define BLEND_MULTIPLY(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)                   \
        Y = (Y1*(256*(255-alpha) +(Y2*alpha)))>>16;                     \
        U = ((U1*(255-alpha)*256)+(alpha*(U1*Y2+128*(256-Y2))))>>16;    \
        V = ((V1*(255-alpha)*256)+(alpha*(V1*Y2+128*(256-Y2))))>>16;

#define BLEND_DIFFERENCE(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)         \
        Y = ABS((gint)Y1-(gint)Y2)+127;                         \
        U = ABS((gint)U1-(gint)U2)+127;                         \
        V = ABS((gint)V1-(gint)V2)+127;                         \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \
        U = CLAMP(U, 0, 255);                                   \
        V = CLAMP(V, 0, 255);

#define BLEND_EXCLUSION(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)          \
        Y = ((gint)(Y1^0xff)*Y2+(gint)(Y2^0xff)*Y1)>>8;         \
        U = ((gint)(U1^0xff)*Y2+(gint)(Y2^0xff)*U1)>>8;         \
        V = ((gint)(V1^0xff)*Y2+(gint)(Y2^0xff)*V1)>>8;         \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \
        U = CLAMP(U, 0, 255);                                   \
        V = CLAMP(V, 0, 255);

#define BLEND_SOFTLIGHT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)          \
        Y = (gint)Y1+(gint)Y2 - 127;                            \
        U = (gint)U1+(gint)U2 - 127;                            \
        V = (gint)V1+(gint)V2 - 127;                            \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \

#define BLEND_HARDLIGHT(Y1,U1,V1,Y2,U2,V2,alpha,Y,U,V)          \
        Y = (gint)Y1+(gint)Y2*2 - 255;                          \
        U = (gint)U1+(gint)U2 - 127;                            \
        V = (gint)V1+(gint)V2 - 127;                            \
        Y = ((Y*alpha)+(Y1*(255-alpha)))>>8;                    \
        U = ((U*alpha)+(U1*(255-alpha)))>>8;                    \
        V = ((V*alpha)+(V1*(255-alpha)))>>8;                    \
        if (Y>255) {                                            \
          gint mult = MAX (0, 288-Y);                           \
          U = ((U*mult) + (127*(32-mult)))>>5;                  \
          V = ((V*mult) + (127*(32-mult)))>>5;                  \
          Y = 255;                                              \
        } else if (Y<0) {                                       \
          gint mult = MIN (32, -Y);                             \
          U = ((U*(32-mult)) + (127*mult))>>5;                  \
          V = ((V*(32-mult)) + (127*mult))>>5;                  \
          Y = 0;                                                \
        }                                                       \

#define BLEND_MODE BLEND_NORMAL
#if 0
#define BLEND_MODE BLEND_NORMAL
#define BLEND_MODE BLEND_ADD
#define BLEND_MODE BLEND_SUBTRACT
#define BLEND_MODE BLEND_LIGHTEN
#define BLEND_MODE BLEND_DARKEN
#define BLEND_MODE BLEND_MULTIPLY
#define BLEND_MODE BLEND_DIFFERENCE
#define BLEND_MODE BLEND_EXCLUSION
#define BLEND_MODE BLEND_SOFTLIGHT
#define BLEND_MODE BLEND_HARDLIGHT
#endif

/* I420 */
/* Copied from jpegenc */
#define VIDEO_Y_ROWSTRIDE(width) (GST_ROUND_UP_4(width))
#define VIDEO_U_ROWSTRIDE(width) (GST_ROUND_UP_8(width)/2)
#define VIDEO_V_ROWSTRIDE(width) ((GST_ROUND_UP_8(VIDEO_Y_ROWSTRIDE(width)))/2)

#define VIDEO_Y_OFFSET(w,h) (0)
#define VIDEO_U_OFFSET(w,h) (VIDEO_Y_OFFSET(w,h)+(VIDEO_Y_ROWSTRIDE(w)*GST_ROUND_UP_2(h)))
#define VIDEO_V_OFFSET(w,h) (VIDEO_U_OFFSET(w,h)+(VIDEO_U_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

#define VIDEO_SIZE(w,h)     (VIDEO_V_OFFSET(w,h)+(VIDEO_V_ROWSTRIDE(w)*GST_ROUND_UP_2(h)/2))

inline static void
gst_i420_do_blend (guint8 * src, guint8 * dest,
    gint src_stride, gint dest_stride, gint src_width, gint src_height,
    gint dest_width, gdouble src_alpha)
{
  int i, j;
  gint b_alpha;

  /* If it's completely transparent... we just return */
  if (G_UNLIKELY (src_alpha == 0.0)) {
    GST_INFO ("Fast copy (alpha == 0.0)");
    return;
  }

  /* If it's completely opaque, we do a fast copy */
  if (G_UNLIKELY (src_alpha == 1.0)) {
    GST_INFO ("Fast copy (alpha == 1.0)");
    for (i = 0; i < src_height; i++) {
      memcpy (dest, src, src_width);
      src += src_stride;
      dest += dest_stride;
    }
    return;
  }

  b_alpha = (gint) (src_alpha * 255);

  for (i = 0; i < src_height; i++) {
    for (j = 0; j < src_width; j++) {
      *dest = (b_alpha * (*src) + (255 - b_alpha) * (*dest)) >> 16;
      dest++;
      src++;
    }
    src += src_stride - src_width;
    dest += dest_stride - dest_width;
  }
}

/* note that this function does packing conversion and blending at the
 * same time */
void
gst_videomixer_blend_i420_i420 (guint8 * src, gint xpos, gint ypos,
    gint src_width, gint src_height, gdouble src_alpha,
    guint8 * dest, gint dest_width, gint dest_height)
{
  guint8 *b_src;
  guint8 *b_dest;
  gint b_src_width = src_width;
  gint b_src_height = src_height;
  gint xoffset = 0;
  gint yoffset = 0;

  xpos = GST_ROUND_UP_2 (xpos);
  ypos = GST_ROUND_UP_2 (ypos);

  /* adjust src pointers for negative sizes */
  if (xpos < 0) {
    xoffset = -xpos;
    b_src_width -= -xpos;
    xpos = 0;
  }
  if (ypos < 0) {
    yoffset += -ypos;
    b_src_height -= -ypos;
    ypos = 0;
  }
  /* If x or y offset are larger then the source it's outside of the picture */
  if (xoffset > src_width || yoffset > src_width) {
    return;
  }

  /* adjust width/height if the src is bigger than dest */
  if (xpos + src_width > dest_width) {
    b_src_width = dest_width - xpos;
  }
  if (ypos + src_height > dest_height) {
    b_src_height = dest_height - ypos;
  }
  if (b_src_width < 0 || b_src_height < 0) {
    return;
  }

  /* First mix Y, then U, then V */
  b_src = src + VIDEO_Y_OFFSET (src_width, src_height);
  b_dest = dest + VIDEO_Y_OFFSET (dest_width, dest_height);
  gst_i420_do_blend (b_src + xoffset + yoffset * VIDEO_Y_ROWSTRIDE (src_width),
      b_dest + xpos + ypos * VIDEO_Y_ROWSTRIDE (dest_width),
      VIDEO_Y_ROWSTRIDE (src_width),
      VIDEO_Y_ROWSTRIDE (dest_width), b_src_width, b_src_height,
      dest_width, src_alpha);

  b_src = src + VIDEO_U_OFFSET (src_width, src_height);
  b_dest = dest + VIDEO_U_OFFSET (dest_width, dest_height);

  gst_i420_do_blend (b_src + xoffset / 2 +
      yoffset / 2 * VIDEO_U_ROWSTRIDE (src_width),
      b_dest + xpos / 2 + ypos / 2 * VIDEO_U_ROWSTRIDE (dest_width),
      VIDEO_U_ROWSTRIDE (src_width), VIDEO_U_ROWSTRIDE (dest_width),
      b_src_width / 2, GST_ROUND_UP_2 (b_src_height) / 2, dest_width / 2,
      src_alpha);

  b_src = src + VIDEO_V_OFFSET (src_width, src_height);
  b_dest = dest + VIDEO_V_OFFSET (dest_width, dest_height);

  gst_i420_do_blend (b_src + xoffset / 2 +
      yoffset / 2 * VIDEO_V_ROWSTRIDE (src_width),
      b_dest + xpos / 2 + ypos / 2 * VIDEO_V_ROWSTRIDE (dest_width),
      VIDEO_V_ROWSTRIDE (src_width), VIDEO_V_ROWSTRIDE (dest_width),
      b_src_width / 2, GST_ROUND_UP_2 (b_src_height) / 2, dest_width / 2,
      src_alpha);
}

#undef BLEND_MODE

/* fill a buffer with a checkerboard pattern */
void
gst_videomixer_fill_i420_checker (guint8 * dest, gint width, gint height)
{
  int size;
  gint i, j;
  static const int tab[] = { 80, 160, 80, 160 };
  guint8 *p = dest;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *p++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];
    }
    p += VIDEO_Y_ROWSTRIDE (width) - width;
  }

  size = (VIDEO_U_ROWSTRIDE (width) * height) / 2;
  memset (dest + VIDEO_U_OFFSET (width, height), 0x80, size);

  size = (VIDEO_V_ROWSTRIDE (width) * height) / 2;
  memset (dest + VIDEO_V_OFFSET (width, height), 0x80, size);
}

void
gst_videomixer_fill_i420_color (guint8 * dest, gint width, gint height,
    gint colY, gint colU, gint colV)
{
  int size;

  size = VIDEO_Y_ROWSTRIDE (width) * height;
  memset (dest, colY, size);

  size = (VIDEO_U_ROWSTRIDE (width) * height) / 2;
  memset (dest + VIDEO_U_OFFSET (width, height), colU, size);

  size = (VIDEO_V_ROWSTRIDE (width) * height) / 2;
  memset (dest + VIDEO_V_OFFSET (width, height), colV, size);

}
