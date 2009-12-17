/* 
 * Copyright (C) 2004 Wim Taymans <wim@fluendo.com>
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

#include <gst/gst.h>

#ifdef HAVE_GCC_ASM
#if defined(HAVE_CPU_I386) || defined(HAVE_CPU_X86_64)
#define BUILD_X86_ASM
#endif
#endif

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

/* note that this function does packing conversion and blending at the
 * same time */
void
gst_videomixer_blend_ayuv_ayuv (guint8 * src, gint xpos, gint ypos,
    gint src_width, gint src_height, gdouble src_alpha,
    guint8 * dest, gint dest_width, gint dest_height)
{
  gint alpha, b_alpha;
  gint i, j;
  gint src_stride, dest_stride;
  gint src_add, dest_add;
  gint Y, U, V;

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

  for (i = 0; i < src_height; i++) {
    for (j = 0; j < src_width; j++) {
      alpha = (src[0] * b_alpha) >> 8;
      BLEND_MODE (dest[1], dest[2], dest[3], src[1], src[2], src[3],
          alpha, Y, U, V);
      dest[0] = 0xff;
      dest[1] = Y;
      dest[2] = U;
      dest[3] = V;

      src += 4;
      dest += 4;
    }
    src += src_add;
    dest += dest_add;
  }
}

#undef BLEND_MODE

#ifdef BUILD_X86_ASM
void
gst_videomixer_blend_ayuv_ayuv_mmx (guint8 * src, gint xpos, gint ypos,
    gint src_width, gint src_height, gdouble src_alpha,
    guint8 * dest, gint dest_width, gint dest_height)
{
  gint b_alpha;
  gint i;
  gint src_stride, dest_stride;
  gint src_add, dest_add;

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

  for (i = 0; i < src_height; i++) {
    gulong old_ebx;

      /* *INDENT-OFF* */
      __asm__ __volatile__ (
          "movl       %%ebx ,      %6   \n\t"
          "pxor       %%mm7 ,   %%mm7   \n\t"   /* mm7 = 0 */
          "pcmpeqd    %%mm6 ,   %%mm6   \n\t"   /* mm6 = 0xffff... */
          "punpcklbw  %%mm7 ,   %%mm6   \n\t"   /* mm6 = 0x00ff00ff00ff... */
          "pcmpeqd    %%mm5 ,   %%mm5   \n\t"   /* mm5 = 0xffff... */
          "psrlq        $56 ,   %%mm5   \n\t"   /* mm5 = 0x0...0ff */
          "xor        %%ebx ,   %%ebx   \n\t"   /* ebx = 0 */
          "1:                           \n\t"
          "movzbl      (%2) ,   %%eax   \n\t"   /* eax == source alpha */
          "imul          %4 ,   %%eax   \n\t"   /* eax = source alpha * alpha */
          "sar           $8 ,   %%eax   \n\t"   /* eax = (source alpha * alpha) / 256 */
          "movd       %%eax ,   %%mm0   \n\t"   /* mm0 = apply alpha */
          "movd        (%2) ,   %%mm2   \n\t"   /* mm2 = src */
          "movd        (%3) ,   %%mm1   \n\t"   /* mm1 = dest */
          "punpcklwd  %%mm0 ,   %%mm0   \n\t"
          "punpckldq  %%mm0 ,   %%mm0   \n\t"   /* mm0 == 0a 0a 0a 0a */
          "punpcklbw  %%mm7 ,   %%mm1   \n\t"   /* mm1 == dv du dy da */
          "punpcklbw  %%mm7 ,   %%mm2   \n\t"   /* mm2 == sv su sy sa */
          "pmullw     %%mm0 ,   %%mm2   \n\t"   /* mm2 == a * s */
          "pandn      %%mm6 ,   %%mm0   \n\t"   /* mm0 == 255 - a */
          "pmullw     %%mm0 ,   %%mm1   \n\t"   /* mm1 == (255 - a) * d */
          "paddusw    %%mm2 ,   %%mm1   \n\t"   /* mm1 == s + d */
          "psrlw         $8 ,   %%mm1   \n\t"
          "packuswb   %%mm7 ,   %%mm1   \n\t"
          "por        %%mm5 ,   %%mm1   \n\t"   /* mm1 = 0x.....ff */
          "movd       %%mm1 ,    (%3)   \n\t"   /* dest = mm1 */
          "add           $4 ,     %1    \n\t"
          "add           $4 ,     %0    \n\t"
          "add           $1 ,   %%ebx   \n\t"
          "cmp        %%ebx ,      %5   \n\t"
          "jne                     1b   \n\t"
          "movl          %6 ,   %%ebx   \n\t"
          :"=r" (src), "=r" (dest)
          :"0" (src), "1" (dest), "r" (b_alpha), "r" (src_width), "m" (old_ebx)
          :"%eax", "memory"
#ifdef __MMX__
          , "mm0", "mm1", "mm2", "mm5", "mm6", "mm7"
#endif
      );
      /* *INDENT-ON* */
    src += src_add;
    dest += dest_add;
  }
  __asm__ __volatile__ ("emms");
}
#endif

/* fill a buffer with a checkerboard pattern */
void
gst_videomixer_fill_ayuv_checker (guint8 * dest, gint width, gint height)
{
  gint i, j;
  static const int tab[] = { 80, 160, 80, 160 };

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = 0xff;
      *dest++ = tab[((i & 0x8) >> 3) + ((j & 0x8) >> 3)];
      *dest++ = 128;
      *dest++ = 128;
    }
  }
}

void
gst_videomixer_fill_ayuv_color (guint8 * dest, gint width, gint height,
    gint colY, gint colU, gint colV)
{
  gint i, j;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest++ = 0xff;
      *dest++ = colY;
      *dest++ = colU;
      *dest++ = colV;
    }
  }
}

#ifdef BUILD_X86_ASM
void
gst_videomixer_fill_ayuv_color_mmx (guint8 * dest, gint width, gint height,
    gint colY, gint colU, gint colV)
{
  guint64 val;
  guint nvals = width * height;

  val = (((guint64) 0xff)) | (((guint64) colY) << 8) |
      (((guint64) colU) << 16) | (((guint64) colV) << 24);
  val = (val << 32) | val;

  /* *INDENT-OFF* */
  __asm__ __volatile__ (
    "cmp      $2 ,    %2  \n\t"
    "jb       2f          \n\t"
    "movq     %4 , %%mm0  \n\t"
    "1:                   \n\t"
    "movq  %%mm0 ,  (%1)  \n\t"
    "sub      $2 ,    %0  \n\t"
    "add      $8 ,    %1  \n\t"
    "cmp      $2 ,    %2  \n\t"
    "jae      1b          \n\t"
    "emms                 \n\t"
    "2:                   \n\t"
    : "=r" (nvals), "=r" (dest)
    : "0" (nvals), "1" (dest), "m" (val)
    : "memory"
#ifdef __MMX__
      , "mm0"
#endif
  );

  /* *INDENT-ON* */
  if (nvals)
    GST_WRITE_UINT32_LE (&dest[-4], (guint32) (val & 0xffffffff));
}
#endif
