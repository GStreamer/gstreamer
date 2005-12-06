/* Resampling library
 * Copyright (C) <2001> David Schleef <ds@schleef.org>
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


#ifndef __FUNCTABLE_H__
#define __FUNCTABLE_H__

typedef void FunctableFunc (double *fx, double *dfx, double x, void *closure);

typedef struct _Functable Functable;
struct _Functable {
  int length;

  double offset;
  double multiplier;

  double inv_multiplier;

  double *fx;
  double *dfx;
};

Functable *functable_new (void);
void functable_setup (Functable *t);
void functable_free (Functable *t);

void functable_set_length (Functable *t, int length);
void functable_set_offset (Functable *t, double offset);
void functable_set_multiplier (Functable *t, double multiplier);
void functable_calculate (Functable *t, FunctableFunc func, void *closure);
void functable_calculate_multiply (Functable *t, FunctableFunc func, void *closure);


double functable_evaluate (Functable *t,double x);

double functable_fir(Functable *t,double x0,int n,double *data,int len);
void functable_fir2(Functable *t,double *r0, double *r1, double x0,
        int n,double *data,int len);

void functable_func_sinc(double *fx, double *dfx, double x, void *closure);
void functable_func_boxcar(double *fx, double *dfx, double x, void *closure);
void functable_func_hanning(double *fx, double *dfx, double x, void *closure);

#endif /* __PRIVATE_H__ */

