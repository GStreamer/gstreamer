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

/*#include <ml.h> */
#include "private.h"

void
conv_double_float_ref (double *dest, float *src, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    dest[i] = src[i];
  }
}

void
conv_float_double_ref (float *dest, double *src, int n)
{
  int i;

  for (i = 0; i < n; i++) {
    dest[i] = src[i];
  }
}

void
conv_double_float_dstr (double *dest, float *src, int n, int dstr)
{
  int i;
  void *d = dest;

  for (i = 0; i < n; i++) {
    (*(double *) d) = *src++;
    d += dstr;
  }
}

void
conv_float_double_sstr (float *dest, double *src, int n, int sstr)
{
  int i;
  void *s = src;

  for (i = 0; i < n; i++) {
    *dest++ = *(double *) s;
    s += sstr;
  }
}
