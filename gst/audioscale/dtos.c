/* Resampling library
 * Copyright (C) <2001> David A. Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or any later version.
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
#include <config.h>
#endif

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

/*#include <ml.h> */
#include <resample.h>



#define short_to_double_table
/*#define short_to_double_altivec */
#define short_to_double_unroll

#ifdef short_to_double_table
static float ints_high[256];
static float ints_low[256];

void
conv_double_short_table (double *dest, short *src, int n)
{
  static int init = 0;
  int i;
  unsigned int idx;

  if (!init) {
    for (i = 0; i < 256; i++) {
      ints_high[i] = 256.0 * ((i < 128) ? i : i - 256);
      ints_low[i] = i;
    }
    init = 1;
  }

  if (n & 1) {
    idx = (unsigned short) *src++;
    *dest++ = ints_high[(idx >> 8)] + ints_low[(idx & 0xff)];
    n -= 1;
  }
  for (i = 0; i < n; i += 2) {
    idx = (unsigned short) *src++;
    *dest++ = ints_high[(idx >> 8)] + ints_low[(idx & 0xff)];
    idx = (unsigned short) *src++;
    *dest++ = ints_high[(idx >> 8)] + ints_low[(idx & 0xff)];
  }
}

#endif

#ifdef short_to_double_unroll
void
conv_double_short_unroll (double *dest, short *src, int n)
{
  if (n & 1) {
    *dest++ = *src++;
    n--;
  }
  if (n & 2) {
    *dest++ = *src++;
    *dest++ = *src++;
    n -= 2;
  }
  while (n > 0) {
    *dest++ = *src++;
    *dest++ = *src++;
    *dest++ = *src++;
    *dest++ = *src++;
    n -= 4;
  }
}
#endif

void
conv_double_short_ref (double *dest, short *src, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    dest[i] = src[i];
  }
}

#ifdef HAVE_CPU_PPC
#if 0
static union
{
  int i[4];
  float f[4];
}
av_tmp __attribute__ ((__aligned__ (16)));

void
conv_double_short_altivec (double *dest, short *src, int n)
{
  int i;

  for (i = 0; i < n; i += 4) {
    av_tmp.i[0] = src[0];
    av_tmp.i[1] = src[1];
    av_tmp.i[2] = src[2];
    av_tmp.i[3] = src[3];

  asm ("	lvx 0,0,%0\n" "	vcfsx 1,0,0\n" "	stvx 1,0,%0\n": :"r" (&av_tmp)
        );

    dest[0] = av_tmp.f[0];
    dest[1] = av_tmp.f[1];
    dest[2] = av_tmp.f[2];
    dest[3] = av_tmp.f[3];
    src += 4;
    dest += 4;
  }
}
#endif
#endif



/* double to short */

void
conv_short_double_ref (short *dest, double *src, int n)
{
  int i;
  double x;

  for (i = 0; i < n; i++) {
    x = *src++;
    if (x < -32768.0)
      x = -32768.0;
    if (x > 32767.0)
      x = 32767.0;
    *dest++ = rint (x);
  }
}

/* #ifdef HAVE_CPU_PPC */
#if 0
void
conv_short_double_ppcasm (short *dest, double *src, int n)
{
  int tmp[2];
  double min = -32768.0;
  double max = 32767.0;
  double ftmp0, ftmp1;

  asm __volatile__ ("\taddic. %3,%3,-8\n"
      "\taddic. %6,%6,-2\n"
      "loop:\n"
      "\tlfdu %0,8(%3)\n"
      "\tfsub %1,%0,%4\n"
      "\tfsel %0,%1,%0,%4\n"
      "\tfsub %1,%0,%5\n"
      "\tfsel %0,%1,%5,%0\n"
      "\tfctiw %1,%0\n"
      "\taddic. 5,5,-1\n"
      "\tstfd %1,0(%2)\n"
      "\tlhz 9,6(%2)\n"
      "\tsthu 9,2(%6)\n" "\tbne loop\n":"=&f" (ftmp0), "=&f" (ftmp1)
      :"b" (tmp), "r" (src), "f" (min), "f" (max), "r" (dest)
      :"r9", "r5");

}
#endif


void
conv_double_short_dstr (double *dest, short *src, int n, int dstr)
{
  int i;
  void *d = dest;

  for (i = 0; i < n; i++) {
    (*(double *) d) = *src++;
    d += dstr;
  }
}

void
conv_short_double_sstr (short *dest, double *src, int n, int sstr)
{
  int i;
  double x;
  void *s = src;

  for (i = 0; i < n; i++) {
    x = *(double *) s;
    if (x < -32768.0)
      x = -32768.0;
    if (x > 32767.0)
      x = 32767.0;
    *dest++ = rint (x);
    s += sstr;
  }
}
