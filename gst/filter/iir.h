/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE Ogg Vorbis SOFTWARE CODEC SOURCE CODE.  *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS SOURCE IS GOVERNED BY *
 * THE GNU PUBLIC LICENSE 2, WHICH IS INCLUDED WITH THIS SOURCE.    *
 * PLEASE READ THESE TERMS DISTRIBUTING.                            *
 *                                                                  *
 * THE OggSQUISH SOURCE CODE IS (C) COPYRIGHT 1994-2000             *
 * by Monty <monty@xiph.org> and The XIPHOPHORUS Company            *
 * http://www.xiph.org/                                             *
 *                                                                  *
 ********************************************************************

  function: Direct Form I, II IIR filters, plus some specializations
  last mod: $Id$

 ********************************************************************/

#ifndef _V_IIR_H_
#define _V_IIR_H_

typedef struct
{
  int stages;
  double *coeff_A;
  double *coeff_B;
  double *z_A;
  double *z_B;
  int ring;
  double gain;
} IIR_state;

void IIR_init (IIR_state * s, int stages, double gain, double *A, double *B);
void IIR_clear (IIR_state * s);
double IIR_filter (IIR_state * s, double in);
double IIR_filter_ChebBand (IIR_state * s, double in);

#endif
