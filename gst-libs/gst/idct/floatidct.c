/* Reference_IDCT.c, Inverse Discrete Fourier Transform, double precision          */

/* Copyright (C) 1996, MPEG Software Simulation Group. All Rights Reserved. */

/*
 * Disclaimer of Warranty
 *
 * These software programs are available to the user without any license fee or
 * royalty on an "as is" basis.  The MPEG Software Simulation Group disclaims
 * any and all warranties, whether express, implied, or statuary, including any
 * implied warranties or merchantability or of fitness for a particular
 * purpose.  In no event shall the copyright-holder be liable for any
 * incidental, punitive, or consequential damages of any kind whatsoever
 * arising from the use of these programs.
 *
 * This disclaimer of warranty extends to the user of these programs and user's
 * customers, employees, agents, transferees, successors, and assigns.
 *
 * The MPEG Software Simulation Group does not represent or warrant that the
 * programs furnished hereunder are free of infringement of any third-party
 * patents.
 *
 * Commercial implementations of MPEG-1 and MPEG-2 video, including shareware,
 * are subject to royalty fees to patent holders.  Many of these patents are
 * general enough such that they are unavoidable regardless of implementation
 * design.
 *
 */

/*  Perform IEEE 1180 reference (64-bit floating point, separable 8x1
 *  direct matrix multiply) Inverse Discrete Cosine Transform
*/


/* Here we use math.h to generate constants.  Compiler results may
   vary a little */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>

#ifndef PI
# ifdef M_PI
#  define PI M_PI
# else
#  define PI 3.14159265358979323846
# endif
#endif

/* private data */

/* cosine transform matrix for 8x1 IDCT */
static double gst_idct_float_c[8][8];

/* initialize DCT coefficient matrix */

void
gst_idct_init_float_idct ()
{
  int freq, time;
  double scale;

  for (freq = 0; freq < 8; freq++) {
    scale = (freq == 0) ? sqrt (0.125) : 0.5;
    for (time = 0; time < 8; time++)
      gst_idct_float_c[freq][time] =
          scale * cos ((PI / 8.0) * freq * (time + 0.5));
  }
}

/* perform IDCT matrix multiply for 8x8 coefficient block */

void
gst_idct_float_idct (block)
     short *block;
{
  int i, j, k, v;
  double partial_product;
  double tmp[64];

  for (i = 0; i < 8; i++)
    for (j = 0; j < 8; j++) {
      partial_product = 0.0;

      for (k = 0; k < 8; k++)
        partial_product += gst_idct_float_c[k][j] * block[8 * i + k];

      tmp[8 * i + j] = partial_product;
    }

  /* Transpose operation is integrated into address mapping by switching 
     loop order of i and j */

  for (j = 0; j < 8; j++)
    for (i = 0; i < 8; i++) {
      partial_product = 0.0;

      for (k = 0; k < 8; k++)
        partial_product += gst_idct_float_c[k][i] * tmp[8 * k + j];

      v = (int) floor (partial_product + 0.5);
      block[8 * i + j] = (v < -256) ? -256 : ((v > 255) ? 255 : v);
    }
}
