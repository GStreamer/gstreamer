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

#include "functable.h"
#include "debug.h"



void
functable_func_sinc (double *fx, double *dfx, double x, void *closure)
{
  if (x == 0) {
    *fx = 1;
    *dfx = 0;
    return;
  }

  *fx = sin (x) / x;
  *dfx = (cos (x) - sin (x) / x) / x;
}

void
functable_func_boxcar (double *fx, double *dfx, double x, void *closure)
{
  double width = *(double *) closure;

  if (x < width && x > -width) {
    *fx = 1;
  } else {
    *fx = 0;
  }
  *dfx = 0;
}

void
functable_func_hanning (double *fx, double *dfx, double x, void *closure)
{
  double width = *(double *) closure;

  if (x < width && x > -width) {
    x /= width;
    *fx = (1 - x * x) * (1 - x * x);
    *dfx = -2 * 2 * x / width * (1 - x * x);
  } else {
    *fx = 0;
    *dfx = 0;
  }
}


Functable *
functable_new (void)
{
  Functable *ft;

  ft = malloc (sizeof (Functable));
  memset (ft, 0, sizeof (Functable));

  return ft;
}

void
functable_free (Functable * ft)
{
  free (ft);
}

void
functable_set_length (Functable * t, int length)
{
  t->length = length;
}

void
functable_set_offset (Functable * t, double offset)
{
  t->offset = offset;
}

void
functable_set_multiplier (Functable * t, double multiplier)
{
  t->multiplier = multiplier;
}

void
functable_calculate (Functable * t, FunctableFunc func, void *closure)
{
  int i;
  double x;

  if (t->fx)
    free (t->fx);
  if (t->dfx)
    free (t->dfx);

  t->fx = malloc (sizeof (double) * (t->length + 1));
  t->dfx = malloc (sizeof (double) * (t->length + 1));

  t->inv_multiplier = 1.0 / t->multiplier;

  for (i = 0; i < t->length + 1; i++) {
    x = t->offset + t->multiplier * i;

    func (&t->fx[i], &t->dfx[i], x, closure);
  }
}

void
functable_calculate_multiply (Functable * t, FunctableFunc func, void *closure)
{
  int i;
  double x;

  for (i = 0; i < t->length + 1; i++) {
    double afx, adfx, bfx, bdfx;

    afx = t->fx[i];
    adfx = t->dfx[i];
    x = t->offset + t->multiplier * i;
    func (&bfx, &bdfx, x, closure);
    t->fx[i] = afx * bfx;
    t->dfx[i] = afx * bdfx + adfx * bfx;
  }

}

double
functable_evaluate (Functable * t, double x)
{
  int i;
  double f0, f1, w0, w1;
  double x2, x3;
  double w;

  if (x < t->offset || x > (t->offset + t->length * t->multiplier)) {
    RESAMPLE_DEBUG ("x out of range %g", x);
  }

  x -= t->offset;
  x *= t->inv_multiplier;
  i = floor (x);
  x -= i;

  x2 = x * x;
  x3 = x2 * x;

  f1 = 3 * x2 - 2 * x3;
  f0 = 1 - f1;
  w0 = (x - 2 * x2 + x3) * t->multiplier;
  w1 = (-x2 + x3) * t->multiplier;

  w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->dfx[i] * w0 + t->dfx[i + 1] * w1;

  /*w = t->fx[i] * (1-x) + t->fx[i+1] * x; */

  return w;
}


double
functable_fir (Functable * t, double x, int n, double *data, int len)
{
  int i, j;
  double f0, f1, w0, w1;
  double x2, x3;
  double w;
  double sum;

  x -= t->offset;
  x /= t->multiplier;
  i = floor (x);
  x -= i;

  x2 = x * x;
  x3 = x2 * x;

  f1 = 3 * x2 - 2 * x3;
  f0 = 1 - f1;
  w0 = (x - 2 * x2 + x3) * t->multiplier;
  w1 = (-x2 + x3) * t->multiplier;

  sum = 0;
  for (j = 0; j < len; j++) {
    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->dfx[i] * w0 + t->dfx[i + 1] * w1;
    sum += data[j * 2] * w;
    i += n;
  }

  return sum;
}

void
functable_fir2 (Functable * t, double *r0, double *r1, double x,
    int n, double *data, int len)
{
  int i, j;
  double f0, f1, w0, w1;
  double x2, x3;
  double w;
  double sum0, sum1;
  double floor_x;

  x -= t->offset;
  x *= t->inv_multiplier;
  floor_x = floor (x);
  i = floor_x;
  x -= floor_x;

  x2 = x * x;
  x3 = x2 * x;

  f1 = 3 * x2 - 2 * x3;
  f0 = 1 - f1;
  w0 = (x - 2 * x2 + x3) * t->multiplier;
  w1 = (-x2 + x3) * t->multiplier;

  sum0 = 0;
  sum1 = 0;
  for (j = 0; j < len; j++) {
    w = t->fx[i] * f0 + t->fx[i + 1] * f1 + t->dfx[i] * w0 + t->dfx[i + 1] * w1;
    sum0 += data[j * 2] * w;
    sum1 += data[j * 2 + 1] * w;
    i += n;
  }

  *r0 = sum0;
  *r1 = sum1;
}
