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
#include "config.h"
#endif

#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "private.h"



double
functable_sinc (void *p, double x)
{
  if (x == 0)
    return 1;
  return sin (x) / x;
}

double
functable_dsinc (void *p, double x)
{
  if (x == 0)
    return 0;
  return cos (x) / x - sin (x) / (x * x);
}

double
functable_window_boxcar (void *p, double x)
{
  if (x < -1 || x > 1)
    return 0;
  return 1;
}

double
functable_window_dboxcar (void *p, double x)
{
  return 0;
}

double
functable_window_std (void *p, double x)
{
  if (x < -1 || x > 1)
    return 0;
  return (1 - x * x) * (1 - x * x);
}

double
functable_window_dstd (void *p, double x)
{
  if (x < -1 || x > 1)
    return 0;
  return -4 * x * (1 - x * x);
}



void
functable_init (functable_t * t)
{
  int i;
  double x;

  t->fx = malloc (sizeof (double) * (t->len + 1));
  t->fdx = malloc (sizeof (double) * (t->len + 1));

  t->invoffset = 1.0 / t->offset;

  for (i = 0; i < t->len + 1; i++) {
    x = t->start + t->offset * i;
    x *= t->scale;

    t->fx[i] = t->func_x (t->priv, x);
    t->fdx[i] = t->scale * t->func_dx (t->priv, x);
  }
  if (t->func2_x) {
    double f1x, f1dx;
    double f2x, f2dx;

    for (i = 0; i < t->len + 1; i++) {
      x = t->start + t->offset * i;
      x *= t->scale2;

      f2x = t->func2_x (t->priv, x);
      f2dx = t->scale2 * t->func2_dx (t->priv, x);

      f1x = t->fx[i];
      f1dx = t->fdx[i];

      t->fx[i] = f1x * f2x;
      t->fdx[i] = f1x * f2dx + f1dx * f2x;
    }
  }
}

double
functable_eval (functable_t * t, double x)
{
  int i;
  double f0, f1, w0, w1;
  double x2, x3;
  double w;

  if (x < t->start || x > (t->start + (t->len + 1) * t->offset)) {
    printf ("x out of range %g\n", x);
  }
  x -= t->start;
  x /= t->offset;
  i = floor (x);
  x -= i;

  x2 = x * x;
  x3 = x2 * x;

  f1 = 3 * x2 - 2 * x3;
  f0 = 1 - f1;
  w0 = (x - 2 * x2 + x3) * t->offset;
  w1 = (-x2 + x3) * t->offset;

  /*printf("i=%d x=%g f0=%g f1=%g w0=%g w1=%g\n",i,x,f0,f1,w0,w1); */

  w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->fdx[i] * w0 + t->fdx[i + 1] * w1;

  /*w = t->fx[i] * (1-x) + t->fx[i+1] * x; */

  return w;
}


double
functable_fir (functable_t * t, double x, int n, double *data, int len)
{
  int i, j;
  double f0, f1, w0, w1;
  double x2, x3;
  double w;
  double sum;

  x -= t->start;
  x /= t->offset;
  i = floor (x);
  x -= i;

  x2 = x * x;
  x3 = x2 * x;

  f1 = 3 * x2 - 2 * x3;
  f0 = 1 - f1;
  w0 = (x - 2 * x2 + x3) * t->offset;
  w1 = (-x2 + x3) * t->offset;

  sum = 0;
  for (j = 0; j < len; j++) {
    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->fdx[i] * w0 + t->fdx[i + 1] * w1;
    sum += data[j * 2] * w;
    i += n;
  }

  return sum;
}

void
functable_fir2 (functable_t * t, double *r0, double *r1, double x,
    int n, double *data, int len)
{
  int i, j;
  double f0, f1, w0, w1;
  double x2, x3;
  double w;
  double sum0, sum1;
  double floor_x;

  x -= t->start;
  x *= t->invoffset;
  floor_x = floor (x);
  i = floor_x;
  x -= floor_x;

  x2 = x * x;
  x3 = x2 * x;

  f1 = 3 * x2 - 2 * x3;
  f0 = 1 - f1;
  w0 = (x - 2 * x2 + x3) * t->offset;
  w1 = (-x2 + x3) * t->offset;

  sum0 = 0;
  sum1 = 0;
  for (j = 0; j < len; j++) {
    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->fdx[i] * w0 + t->fdx[i + 1] * w1;
    sum0 += data[j * 2] * w;
    sum1 += data[j * 2 + 1] * w;
    i += n;

#define unroll2
#define unroll3
#define unroll4
#ifdef unroll2
    j++;

    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->fdx[i] * w0 + t->fdx[i + 1] * w1;
    sum0 += data[j * 2] * w;
    sum1 += data[j * 2 + 1] * w;
    i += n;
#endif
#ifdef unroll3
    j++;

    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->fdx[i] * w0 + t->fdx[i + 1] * w1;
    sum0 += data[j * 2] * w;
    sum1 += data[j * 2 + 1] * w;
    i += n;
#endif
#ifdef unroll4
    j++;

    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->fdx[i] * w0 + t->fdx[i + 1] * w1;
    sum0 += data[j * 2] * w;
    sum1 += data[j * 2 + 1] * w;
    i += n;
#endif
  }

  *r0 = sum0;
  *r1 = sum1;
}



#ifdef unused
void
functable_fir2_altivec (functable_t * t, float *r0, float *r1,
    double x, int n, float *data, int len)
{
  int i, j;
  double f0, f1, w0, w1;
  double x2, x3;
  double w;
  double sum0, sum1;
  double floor_x;

  x -= t->start;
  x *= t->invoffset;
  floor_x = floor (x);
  i = floor_x;
  x -= floor_x;

  x2 = x * x;
  x3 = x2 * x;

  f1 = 3 * x2 - 2 * x3;
  f0 = 1 - f1;
  w0 = (x - 2 * x2 + x3) * t->offset;
  w1 = (-x2 + x3) * t->offset;

  sum0 = 0;
  sum1 = 0;
  for (j = 0; j < len; j++) {
    /* t->fx, t->fdx needs to be multiplexed by n */
    /* we need 5 consecutive floats, which fit into 2 vecs */
    /* load v0, t->fx[i] */
    /* load v1, t->fx[i+n] */
    /* v2 = v0 (not correct) */
    /* v3 = (v0>>32) || (v1<<3*32) (not correct) */
    /* */
    /* load v4, t->dfx[i] */
    /* load v5, t->dfx[i+n] */
    /* v6 = v4 (not correct) */
    /* v7 = (v4>>32) || (v5<<3*32) (not correct) */
    /*  */
    /* v8 = splat(f0) */
    /* v9 = splat(f1) */
    /* v10 = splat(w0) */
    /* v11 = splat(w1) */
    /* */
    /* v12 = v2 * v8 */
    /* v12 += v3 * v9 */
    /* v12 += v6 * v10 */
    /* v12 += v7 * v11 */

    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->fdx[i] * w0 + t->fdx[i + 1] * w1;

    /* v13 = data[j*2] */
    /* v14 = data[j*2+4] */
    /* v15 = deinterlace_high(v13,v14) */
    /* v16 = deinterlace_low(v13,v14) */
    /* (sum0) v17 += multsum(v13,v15) */
    /* (sum1) v18 += multsum(v14,v16) */

    sum0 += data[j * 2] * w;
    sum1 += data[j * 2 + 1] * w;
    i += n;

  }

  *r0 = sum0;
  *r1 = sum1;
}
#endif
