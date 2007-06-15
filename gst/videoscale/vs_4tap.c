/*
 * Image Scaling Functions (4 tap)
 * Copyright (c) 2005 David A. Schleef <ds@schleef.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "vs_image.h"
#include "vs_scanline.h"

#include <liboil/liboil.h>
#include <math.h>

#define SHIFT 10

#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define CLAMP(x,a,b) MAX(MIN((x),(b)),(a))

#ifndef M_PI
#define M_PI  3.14159265358979323846
#endif

#ifdef WIN32
#define rint(x) (floor((x)+0.5))
#endif

int16_t vs_4tap_taps[256][4];

double
vs_4tap_func (double x)
{
#if 0
  if (x < -1)
    return 0;
  if (x > 1)
    return 0;
  if (x < 0)
    return 1 + x;
  return 1 - x;
#endif
#if 0
  if (x == 0)
    return 1;
  return sin (M_PI * x) / (M_PI * x) * (1 - 0.25 * x * x);
#endif
#if 1
  if (x == 0)
    return 1;
  return sin (M_PI * x) / (M_PI * x);
#endif
}

void
vs_4tap_init (void)
{
  int i;
  double a, b, c, d;
  double sum;

  for (i = 0; i < 256; i++) {
    a = vs_4tap_func (-1 - i / 256.0);
    b = vs_4tap_func (0 - i / 256.0);
    c = vs_4tap_func (1 - i / 256.0);
    d = vs_4tap_func (2 - i / 256.0);
    sum = a + b + c + d;

    vs_4tap_taps[i][0] = rint ((1 << SHIFT) * (a / sum));
    vs_4tap_taps[i][1] = rint ((1 << SHIFT) * (b / sum));
    vs_4tap_taps[i][2] = rint ((1 << SHIFT) * (c / sum));
    vs_4tap_taps[i][3] = rint ((1 << SHIFT) * (d / sum));
  }
}


void
vs_scanline_resample_4tap_Y (uint8_t * dest, uint8_t * src,
    int n, int *xacc, int increment)
{
  int i;
  int j;
  int acc;
  int x;
  int y;

  acc = *xacc;
  for (i = 0; i < n; i++) {
    j = acc >> 16;
    x = (acc & 0xff00) >> 8;
    y = (vs_4tap_taps[x][0] * src[MAX (j - 1, 0)] +
        vs_4tap_taps[x][1] * src[j] +
        vs_4tap_taps[x][2] * src[j + 1] +
        vs_4tap_taps[x][3] * src[j + 2] + (1 << (SHIFT - 1))) >> SHIFT;
    if (y < 0)
      y = 0;
    if (y > 255)
      y = 255;
    dest[i] = y;
    acc += increment;
  }
  *xacc = acc;
}

void
vs_scanline_merge_4tap_Y (uint8_t * dest, uint8_t * src1, uint8_t * src2,
    uint8_t * src3, uint8_t * src4, int n, int acc)
{
  int i;
  int y;

  acc = (acc >> 8) & 0xff;
  for (i = 0; i < n; i++) {
    y = (vs_4tap_taps[acc][0] * src1[i] +
        vs_4tap_taps[acc][1] * src2[i] +
        vs_4tap_taps[acc][2] * src3[i] +
        vs_4tap_taps[acc][3] * src4[i] + (1 << (SHIFT - 1))) >> SHIFT;
    if (y < 0)
      y = 0;
    if (y > 255)
      y = 255;
    dest[i] = y;
  }

}


void
vs_image_scale_4tap_Y (const VSImage * dest, const VSImage * src,
    uint8_t * tmpbuf)
{
  int yacc;
  int y_increment;
  int x_increment;
  int i;
  int j;
  int xacc;
  int k;

  y_increment = ((src->height - 1) << 16) / (dest->height - 1);
  x_increment = ((src->width - 1) << 16) / (dest->width - 1);

  k = 0;
  for (i = 0; i < 4; i++) {
    xacc = 0;
    vs_scanline_resample_4tap_Y (tmpbuf + i * dest->width,
        src->pixels + i * src->stride, dest->width, &xacc, x_increment);
  }

  yacc = 0;
  for (i = 0; i < dest->height; i++) {
    uint8_t *t0, *t1, *t2, *t3;

    j = yacc >> 16;

    while (j > k) {
      k++;
      if (k + 3 < src->height) {
        xacc = 0;
        vs_scanline_resample_4tap_Y (tmpbuf + ((k + 3) & 3) * dest->width,
            src->pixels + (k + 3) * src->stride,
            dest->width, &xacc, x_increment);
      }
    }

    t0 = tmpbuf + (CLAMP (j - 1, 0, src->height - 1) & 3) * dest->width;
    t1 = tmpbuf + (CLAMP (j, 0, src->height - 1) & 3) * dest->width;
    t2 = tmpbuf + (CLAMP (j + 1, 0, src->height - 1) & 3) * dest->width;
    t3 = tmpbuf + (CLAMP (j + 2, 0, src->height - 1) & 3) * dest->width;
    vs_scanline_merge_4tap_Y (dest->pixels + i * dest->stride,
        t0, t1, t2, t3, dest->width, yacc & 0xffff);

    yacc += y_increment;
  }
}
