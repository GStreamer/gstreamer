/* 
 *  idctmmx32.cpp 
 *
 *	Copyright (C) Alberto Vigata - January 2000 - ultraflask@yahoo.com
 *
 *  This file is part of FlasKMPEG, a free MPEG to MPEG/AVI converter
 *	
 *  FlasKMPEG is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *   
 *  FlasKMPEG is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *   
 *  You should have received a copy of the GNU General Public License
 *  along with GNU Make; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 */  
    
/* MMX32 iDCT algorithm  (IEEE-1180 compliant) :: idct_mmx32() */
/* */
/* MPEG2AVI */
/* -------- */
/*  v0.16B33 initial release */
/* */
/* This was one of the harder pieces of work to code. */
/* Intel's app-note focuses on the numerical issues of the algorithm, but */
/* assumes the programmer is familiar with IDCT mathematics, leaving the */
/* form of the complete function up to the programmer's imagination. */
/* */
/*  ALGORITHM OVERVIEW */
/*  ------------------ */
/* I played around with the code for quite a few hours.  I came up */
/* with *A* working IDCT algorithm, however I'm not sure whether my routine */
/* is "the correct one."  But rest assured, my code passes all six IEEE  */
/* accuracy tests with plenty of margin. */
/* */
/*   My IDCT algorithm consists of 4 steps: */
/* */
/*   1) IDCT-row transformation (using the IDCT-row function) on all 8 rows */
/*      This yields an intermediate 8x8 matrix. */
/* */
/*   2) intermediate matrix transpose (mandatory) */
/* */
/*   3) IDCT-row transformation (2nd time) on all 8 rows of the intermediate */
/*      matrix.  The output is the final-result, in transposed form. */
/* */
/*   4) post-transformation matrix transpose  */
/*      (not necessary if the input-data is already transposed, this could */
/*       be done during the MPEG "zig-zag" scan, but since my algorithm */
/*       requires at least one transpose operation, why not re-use the */
/*       transpose-code.) */
/* */
/*   Although the (1st) and (3rd) steps use the SAME row-transform operation, */
/*   the (3rd) step uses different shift&round constants (explained later.) */
/* */
/*   Also note that the intermediate transpose (2) would not be neccessary, */
/*   if the subsequent operation were an iDCT-column transformation.  Since */
/*   we only have the iDCT-row transform, we transpose the intermediate */
/*   matrix and use the iDCT-row transform a 2nd time. */
/* */
/*   I had to change some constants/variables for my method to work : */
/* */
/*      As given by Intel, the #defines for SHIFT_INV_COL and RND_INV_COL are */
/*      wrong.  Not surprising since I'm not using a true column-transform  */
/*      operation, but the row-transform operation (as mentioned earlier.) */
/*      round_inv_col[], which is given as "4 short" values, should have the */
/*      same dimensions as round_inv_row[].  The corrected variables are  */
/*      shown. */
/* */
/*      Intel's code defines a different table for each each row operation. */
/*      The tables given are 0/4, 1/7, 2/6, and 5/3.  My code only uses row#0. */
/*      Using the other rows messes up the overall transform. */
/* */
/*   IMPLEMENTATION DETAILs */
/*   ---------------------- */
/*  */
/*   I divided the algorithm's work into two subroutines, */
/*    1) idct_mmx32_rows() - transforms 8 rows, then transpose */
/*    2) idct_mmx32_cols() - transforms 8 rows, then transpose */
/*       yields final result ("drop-in" direct replacement for INT32 IDCT) */
/* */
/*   The 2nd function is a clone of the 1st, with changes made only to the */
/*   shift&rounding instructions. */
/* */
/*      In the 1st function (rows), the shift & round instructions use  */
/*       SHIFT_INV_ROW & round_inv_row[] (renamed to r_inv_row[]) */
/* */
/*      In the 2nd function (cols)-> r_inv_col[], and */
/*       SHIFT_INV_COL & round_inv_col[] (renamed to r_inv_col[]) */
/* */
/*   Each function contains an integrated transpose-operator, which comes */
/*   AFTER the primary transformation operation.  In the future, I'll optimize */
/*   the code to do more of the transpose-work "in-place".  Right now, I've */
/*   left the code as two subroutines and a main calling function, so other */
/*   people can read the code more easily. */
/* */
/*   liaor@umcc.ais.org  http://members.tripod.com/~liaor */
/*   */
    
/*;============================================================================= */
/*; */
/*;  AP-922   http://developer.intel.com/vtune/cbts/strmsimd */
/*; These examples contain code fragments for first stage iDCT 8x8 */
/*; (for rows) and first stage DCT 8x8 (for columns) */
/*; */
/*;============================================================================= */
/*
mword typedef qword
qword ptr equ mword ptr */ 
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <mmx.h>
    
#define BITS_INV_ACC	4       /*; 4 or 5 for IEEE */
    /* 5 yields higher accuracy, but lessens dynamic range on the input matrix */
#define SHIFT_INV_ROW	(16 - BITS_INV_ACC)
#define SHIFT_INV_COL	(1 + BITS_INV_ACC +14 ) /* changed from Intel's val) */
/*#define SHIFT_INV_COL	(1 + BITS_INV_ACC ) */
    
#define RND_INV_ROW		(1 << (SHIFT_INV_ROW-1))
#define RND_INV_COL		(1 << (SHIFT_INV_COL-1)) 
#define RND_INV_CORR	(RND_INV_COL - 1)       /*; correction -1.0 and round */
/*#define RND_INV_ROW		(1024 * (6 - BITS_INV_ACC)) //; 1 << (SHIFT_INV_ROW-1) */
/*#define RND_INV_COL		(16 * (BITS_INV_ACC - 3)) //; 1 << (SHIFT_INV_COL-1) */
    
/*.data */
/*Align 16 */
const static long r_inv_row[2] = { RND_INV_ROW, RND_INV_ROW };
const static long r_inv_col[2] = { RND_INV_COL, RND_INV_COL };
const static long r_inv_corr[2] = { RND_INV_CORR, RND_INV_CORR };


/*const static short r_inv_col[4] =  */
/*	{RND_INV_COL, RND_INV_COL, RND_INV_COL, RND_INV_COL}; */
/*const static short r_inv_corr[4] = */
/*	{RND_INV_CORR, RND_INV_CORR, RND_INV_CORR, RND_INV_CORR}; */
    
/* constants for the forward DCT

/*#define BITS_FRW_ACC	3 //; 2 or 3 for accuracy */
/*#define SHIFT_FRW_COL	BITS_FRW_ACC */
/*#define SHIFT_FRW_ROW	(BITS_FRW_ACC + 17) */
/*#define RND_FRW_ROW		(262144 * (BITS_FRW_ACC - 1)) //; 1 << (SHIFT_FRW_ROW-1) */
const static __int64 one_corr = 0x0001000100010001;
const static long r_frw_row[2] = { RND_FRW_ROW, RND_FRW_ROW };


/*const static short tg_1_16[4] = {13036, 13036, 13036, 13036 }; //tg * (2<<16) + 0.5 */
/*const static short tg_2_16[4] = {27146, 27146, 27146, 27146 }; //tg * (2<<16) + 0.5 */
/*const static short tg_3_16[4] = {-21746, -21746, -21746, -21746 }; //tg * (2<<16) + 0.5 */
/*const static short cos_4_16[4] = {-19195, -19195, -19195, -19195 }; //cos * (2<<16) + 0.5 */
/*const static short ocos_4_16[4] = {23170, 23170, 23170, 23170 }; //cos * (2<<15) + 0.5 */
    
/*concatenated table, for forward DCT transformation */
const static short tg_all_16[] = { 13036, 13036, 13036, 13036, /* tg * (2<<16) + 0.5 */
  27146, 27146, 27146, 27146,   /*tg * (2<<16) + 0.5 */
  -21746, -21746, -21746, -21746,       /* tg * (2<<16) + 0.5 */
  -19195, -19195, -19195, -19195,       /*cos * (2<<16) + 0.5 */
  23170, 23170, 23170, 23170
};                              /*cos * (2<<15) + 0.5 */


#define tg_1_16 (tg_all_16 + 0)
#define tg_2_16 (tg_all_16 + 8)
#define tg_3_16 (tg_all_16 + 16)
#define cos_4_16 (tg_all_16 + 24)
#define ocos_4_16 (tg_all_16 + 32)
    */
/*
;=============================================================================
;
; The first stage iDCT 8x8 - inverse DCTs of rows
;
;-----------------------------------------------------------------------------
; The 8-point inverse DCT direct algorithm
;-----------------------------------------------------------------------------
;
; static const short w[32] = {
; FIX(cos_4_16), FIX(cos_2_16), FIX(cos_4_16), FIX(cos_6_16),
; FIX(cos_4_16), FIX(cos_6_16), -FIX(cos_4_16), -FIX(cos_2_16),
; FIX(cos_4_16), -FIX(cos_6_16), -FIX(cos_4_16), FIX(cos_2_16),
; FIX(cos_4_16), -FIX(cos_2_16), FIX(cos_4_16), -FIX(cos_6_16),
; FIX(cos_1_16), FIX(cos_3_16), FIX(cos_5_16), FIX(cos_7_16),
; FIX(cos_3_16), -FIX(cos_7_16), -FIX(cos_1_16), -FIX(cos_5_16),
; FIX(cos_5_16), -FIX(cos_1_16), FIX(cos_7_16), FIX(cos_3_16),
; FIX(cos_7_16), -FIX(cos_5_16), FIX(cos_3_16), -FIX(cos_1_16) };
;
; #define DCT_8_INV_ROW(x, y)

;{
; int a0, a1, a2, a3, b0, b1, b2, b3;
;
; a0 =x[0]*w[0]+x[2]*w[1]+x[4]*w[2]+x[6]*w[3];
; a1 =x[0]*w[4]+x[2]*w[5]+x[4]*w[6]+x[6]*w[7];
; a2 = x[0] * w[ 8] + x[2] * w[ 9] + x[4] * w[10] + x[6] * w[11];
; a3 = x[0] * w[12] + x[2] * w[13] + x[4] * w[14] + x[6] * w[15];
; b0 = x[1] * w[16] + x[3] * w[17] + x[5] * w[18] + x[7] * w[19];
; b1 = x[1] * w[20] + x[3] * w[21] + x[5] * w[22] + x[7] * w[23];
; b2 = x[1] * w[24] + x[3] * w[25] + x[5] * w[26] + x[7] * w[27];
; b3 = x[1] * w[28] + x[3] * w[29] + x[5] * w[30] + x[7] * w[31];
;
; y[0] = SHIFT_ROUND ( a0 + b0 );
; y[1] = SHIFT_ROUND ( a1 + b1 );
; y[2] = SHIFT_ROUND ( a2 + b2 );
; y[3] = SHIFT_ROUND ( a3 + b3 );
; y[4] = SHIFT_ROUND ( a3 - b3 );
; y[5] = SHIFT_ROUND ( a2 - b2 );
; y[6] = SHIFT_ROUND ( a1 - b1 );
; y[7] = SHIFT_ROUND ( a0 - b0 );
;}
;
;-----------------------------------------------------------------------------
;
; In this implementation the outputs of the iDCT-1D are multiplied
; for rows 0,4 - by cos_4_16,
; for rows 1,7 - by cos_1_16,
; for rows 2,6 - by cos_2_16,
; for rows 3,5 - by cos_3_16
; and are shifted to the left for better accuracy
;
; For the constants used,
; FIX(float_const) = (short) (float_const * (1<<15) + 0.5)
;
;=============================================================================
;=============================================================================
IF _MMX ; MMX code
;=============================================================================

/*; Table for rows 0,4 - constants are multiplied by cos_4_16 */
const short tab_i_04[] = { 16384, 16384, 16384, -16384,        /* ; movq-> w06 w04 w02 w00 */
  21407, 8867, 8867, -21407,    /* w07 w05 w03 w01 */
  16384, -16384, 16384, 16384,  /*; w14 w12 w10 w08 */
  -8867, 21407, -21407, -8867,  /*; w15 w13 w11 w09 */
  22725, 12873, 19266, -22725,  /*; w22 w20 w18 w16 */
  19266, 4520, -4520, -12873,   /*; w23 w21 w19 w17 */
  12873, 4520, 4520, 19266,     /*; w30 w28 w26 w24 */
  -22725, 19266, -12873, -22725
};                              /*w31 w29 w27 w25 */


/*; Table for rows 1,7 - constants are multiplied by cos_1_16 */
const short tab_i_17[] = { 22725, 22725, 22725, -22725,        /* ; movq-> w06 w04 w02 w00 */
  29692, 12299, 12299, -29692,  /*      ; w07 w05 w03 w01 */
  22725, -22725, 22725, 22725,  /*; w14 w12 w10 w08 */
  -12299, 29692, -29692, -12299,        /*; w15 w13 w11 w09 */
  31521, 17855, 26722, -31521,  /*; w22 w20 w18 w16 */
  26722, 6270, -6270, -17855,   /*; w23 w21 w19 w17 */
  17855, 6270, 6270, 26722,     /*; w30 w28 w26 w24 */
  -31521, 26722, -17855, -31521
};                              /* w31 w29 w27 w25 */


/*; Table for rows 2,6 - constants are multiplied by cos_2_16 */
const short tab_i_26[] = { 21407, 21407, 21407, -21407,        /* ; movq-> w06 w04 w02 w00 */
  27969, 11585, 11585, -27969,  /* ; w07 w05 w03 w01 */
  21407, -21407, 21407, 21407,  /* ; w14 w12 w10 w08 */
  -11585, 27969, -27969, -11585,        /*  ;w15 w13 w11 w09 */
  29692, 16819, 25172, -29692,  /* ;w22 w20 w18 w16 */
  25172, 5906, -5906, -16819,   /* ;w23 w21 w19 w17 */
  16819, 5906, 5906, 25172,     /* ;w30 w28 w26 w24 */
  -29692, 25172, -16819, -29692
};                              /*  ;w31 w29 w27 w25 */


/*; Table for rows 3,5 - constants are multiplied by cos_3_16 */
const short tab_i_35[] = { 19266, 19266, 19266, -19266,        /*; movq-> w06 w04 w02 w00 */
  25172, 10426, 10426, -25172,  /*; w07 w05 w03 w01 */
  19266, -19266, 19266, 19266,  /*; w14 w12 w10 w08 */
  -10426, 25172, -25172, -10426,        /*; w15 w13 w11 w09 */
  26722, 15137, 22654, -26722,  /*; w22 w20 w18 w16 */
  22654, 5315, -5315, -15137,   /*; w23 w21 w19 w17 */
  15137, 5315, 5315, 22654,     /*; w30 w28 w26 w24 */
  -26722, 22654, -15137, -26722
};                              /*; w31 w29 w27 w25 */

*/
/* CONCATENATED TABLE, rows 0,1,2,3,4,5,6,7 (in order ) */
/* */
/* In our implementation, however, we only use row0 ! */
/* */
static const short tab_i_01234567[] = { 
      /*row0, this row is required */
      16384, 16384, 16384, -16384,      /* ; movq-> w06 w04 w02 w00 */
  21407, 8867, 8867, -21407,    /* w07 w05 w03 w01 */
  16384, -16384, 16384, 16384,  /*; w14 w12 w10 w08 */
  -8867, 21407, -21407, -8867,  /*; w15 w13 w11 w09 */
  22725, 12873, 19266, -22725,  /*; w22 w20 w18 w16 */
  19266, 4520, -4520, -12873,   /*; w23 w21 w19 w17 */
  12873, 4520, 4520, 19266,     /*; w30 w28 w26 w24 */
  -22725, 19266, -12873, -22725,        /*w31 w29 w27 w25 */
  
      /* the rest of these rows (1-7), aren't used ! */
      
      /*row1 */
      22725, 22725, 22725, -22725,      /* ; movq-> w06 w04 w02 w00 */
  29692, 12299, 12299, -29692,  /*      ; w07 w05 w03 w01 */
  22725, -22725, 22725, 22725,  /*; w14 w12 w10 w08 */
  -12299, 29692, -29692, -12299,        /*; w15 w13 w11 w09 */
  31521, 17855, 26722, -31521,  /*; w22 w20 w18 w16 */
  26722, 6270, -6270, -17855,   /*; w23 w21 w19 w17 */
  17855, 6270, 6270, 26722,     /*; w30 w28 w26 w24 */
  -31521, 26722, -17855, -31521,        /* w31 w29 w27 w25 */
  
      /*row2 */
      21407, 21407, 21407, -21407,      /* ; movq-> w06 w04 w02 w00 */
  27969, 11585, 11585, -27969,  /* ; w07 w05 w03 w01 */
  21407, -21407, 21407, 21407,  /* ; w14 w12 w10 w08 */
  -11585, 27969, -27969, -11585,        /*  ;w15 w13 w11 w09 */
  29692, 16819, 25172, -29692,  /* ;w22 w20 w18 w16 */
  25172, 5906, -5906, -16819,   /* ;w23 w21 w19 w17 */
  16819, 5906, 5906, 25172,     /* ;w30 w28 w26 w24 */
  -29692, 25172, -16819, -29692,        /*  ;w31 w29 w27 w25 */
  
      /*row3 */
      19266, 19266, 19266, -19266,      /*; movq-> w06 w04 w02 w00 */
  25172, 10426, 10426, -25172,  /*; w07 w05 w03 w01 */
  19266, -19266, 19266, 19266,  /*; w14 w12 w10 w08 */
  -10426, 25172, -25172, -10426,        /*; w15 w13 w11 w09 */
  26722, 15137, 22654, -26722,  /*; w22 w20 w18 w16 */
  22654, 5315, -5315, -15137,   /*; w23 w21 w19 w17 */
  15137, 5315, 5315, 22654,     /*; w30 w28 w26 w24 */
  -26722, 22654, -15137, -26722,        /*; w31 w29 w27 w25 */
  
      /*row4 */
      16384, 16384, 16384, -16384,      /* ; movq-> w06 w04 w02 w00 */
  21407, 8867, 8867, -21407,    /* w07 w05 w03 w01 */
  16384, -16384, 16384, 16384,  /*; w14 w12 w10 w08 */
  -8867, 21407, -21407, -8867,  /*; w15 w13 w11 w09 */
  22725, 12873, 19266, -22725,  /*; w22 w20 w18 w16 */
  19266, 4520, -4520, -12873,   /*; w23 w21 w19 w17 */
  12873, 4520, 4520, 19266,     /*; w30 w28 w26 w24 */
  -22725, 19266, -12873, -22725,        /*w31 w29 w27 w25 */
  
      /*row5 */
      19266, 19266, 19266, -19266,      /*; movq-> w06 w04 w02 w00 */
  25172, 10426, 10426, -25172,  /*; w07 w05 w03 w01 */
  19266, -19266, 19266, 19266,  /*; w14 w12 w10 w08 */
  -10426, 25172, -25172, -10426,        /*; w15 w13 w11 w09 */
  26722, 15137, 22654, -26722,  /*; w22 w20 w18 w16 */
  22654, 5315, -5315, -15137,   /*; w23 w21 w19 w17 */
  15137, 5315, 5315, 22654,     /*; w30 w28 w26 w24 */
  -26722, 22654, -15137, -26722,        /*; w31 w29 w27 w25 */
  
      /*row6 */
      21407, 21407, 21407, -21407,      /* ; movq-> w06 w04 w02 w00 */
  27969, 11585, 11585, -27969,  /* ; w07 w05 w03 w01 */
  21407, -21407, 21407, 21407,  /* ; w14 w12 w10 w08 */
  -11585, 27969, -27969, -11585,        /*  ;w15 w13 w11 w09 */
  29692, 16819, 25172, -29692,  /* ;w22 w20 w18 w16 */
  25172, 5906, -5906, -16819,   /* ;w23 w21 w19 w17 */
  16819, 5906, 5906, 25172,     /* ;w30 w28 w26 w24 */
  -29692, 25172, -16819, -29692,        /*  ;w31 w29 w27 w25 */
  
      /*row7 */
      22725, 22725, 22725, -22725,      /* ; movq-> w06 w04 w02 w00 */
  29692, 12299, 12299, -29692,  /*      ; w07 w05 w03 w01 */
  22725, -22725, 22725, 22725,  /*; w14 w12 w10 w08 */
  -12299, 29692, -29692, -12299,        /*; w15 w13 w11 w09 */
  31521, 17855, 26722, -31521,  /*; w22 w20 w18 w16 */
  26722, 6270, -6270, -17855,   /*; w23 w21 w19 w17 */
  17855, 6270, 6270, 26722,     /*; w30 w28 w26 w24 */
  -31521, 26722, -17855, -31521
};                              /* w31 w29 w27 w25 */


#define INP eax                 /* pointer to (short *blk) */
#define OUT ecx                 /* pointer to output (temporary store space qwTemp[]) */
#define TABLE ebx               /* pointer to tab_i_01234567[] */
#define round_inv_row edx
#define round_inv_col edx
    
#define ROW_STRIDE 8            /* for 8x8 matrix transposer */
    
/* private variables and functions */
    
/*temporary storage space, 8x8 of shorts */
__inline static void idct_mmx32_rows (short *blk);     /* transform rows */
__inline static void idct_mmx32_cols (short *blk);      /* transform "columns" */

        /* the "column" transform actually transforms rows, it is */
        /* identical to the row-transform except for the ROUNDING */
        /* and SHIFTING coefficients. */
static void 
idct_mmx32_rows (short *blk)
{                               /* transform all 8 rows of 8x8 iDCT block */
  int x;
  short qwTemp[64];
  short *out = &qwTemp[0];
  short *inptr = blk;

  
      /* this subroutine performs two operations */
      /* 1) iDCT row transform */
      /*            for( i = 0; i < 8; ++ i) */
      /*                    DCT_8_INV_ROW_1( blk[i*8], qwTemp[i] ); */
      /* */
      /* 2) transpose the matrix (which was stored in qwTemp[]) */
      /*        qwTemp[] -> [8x8 matrix transpose] -> blk[] */
      for (x = 0; x < 8; x++) {        /* transform one row per iteration */
    movq_m2r (*(inptr), mm0);   /* 0 ; x3 x2 x1 x0 */
    movq_m2r (*(inptr + 4), mm1);      /* 1 ; x7 x6 x5 x4 */
    movq_r2r (mm0, mm2);        /* 2 ; x3 x2 x1 x0 */
    movq_m2r (*(tab_i_01234567), mm3); /* 3 ; w06 w04 w02 w00 */
    punpcklwd_r2r (mm1, mm0);   /* x5 x1 x4 x0 */
    
        /* ---------- */
        movq_r2r (mm0, mm5);    /* 5 ; x5 x1 x4 x0 */
    punpckldq_r2r (mm0, mm0);   /* x4 x0 x4 x0 */
    movq_m2r (*(tab_i_01234567 + 4), mm4);     /* 4 ; w07 w05 w03 w01 */
    punpckhwd_r2r (mm1, mm2);   /* 1 ; x7 x3 x6 x2 */
    pmaddwd_r2r (mm0, mm3);    /* x4*w06+x0*w04 x4*w02+x0*w00 */
    movq_r2r (mm2, mm6);        /* 6 ; x7 x3 x6 x2 */
    movq_m2r (*(tab_i_01234567 + 16), mm1);    /* 1 ; w22 w20 w18 w16 */
    punpckldq_r2r (mm2, mm2);   /* x6 x2 x6 x2 */
    pmaddwd_r2r (mm2, mm4);    /* x6*w07+x2*w05 x6*w03+x2*w01 */
    punpckhdq_r2r (mm5, mm5);   /* x5 x1 x5 x1 */
    pmaddwd_m2r (*(tab_i_01234567 + 8), mm0);  /* x4*w14+x0*w12 x4*w10+x0*w08 */
    punpckhdq_r2r (mm6, mm6);   /* x7 x3 x7 x3 */
    movq_m2r (*(tab_i_01234567 + 20), mm7);    /* 7 ; w23 w21 w19 w17 */
    pmaddwd_r2r (mm5, mm1);     /* x5*w22+x1*w20 x5*w18+x1*w16 */
    paddd_m2r (*(r_inv_row), mm3);     /* +rounder */
    pmaddwd_r2r (mm6, mm7);     /* x7*w23+x3*w21 x7*w19+x3*w17 */
    pmaddwd_m2r (*(tab_i_01234567 + 12), mm2); /* x6*w15+x2*w13 x6*w11+x2*w09 */
    paddd_r2r (mm4, mm3);       /* 4 ; a1=sum(even1) a0=sum(even0) */
    pmaddwd_m2r (*(tab_i_01234567 + 24), mm5); /* x5*w30+x1*w28 x5*w26+x1*w24 */
    movq_r2r (mm3, mm4);        /* 4 ; a1 a0 */
    pmaddwd_m2r (*(tab_i_01234567 + 28), mm6); /* x7*w31+x3*w29 x7*w27+x3*w25 */
    paddd_r2r (mm7, mm1);       /* 7 ; b1=sum(odd1) b0=sum(odd0) */
    paddd_m2r (*(r_inv_row), mm0);     /* +rounder */
    psubd_r2r (mm1, mm3);       /* a1-b1 a0-b0 */
    psrad_i2r (SHIFT_INV_ROW, mm3);    /* y6=a1-b1 y7=a0-b0 */
    paddd_r2r (mm4, mm1);       /* 4 ; a1+b1 a0+b0 */
    paddd_r2r (mm2, mm0);      /* 2 ; a3=sum(even3) a2=sum(even2) */
    psrad_i2r (SHIFT_INV_ROW, mm1);     /* y1=a1+b1 y0=a0+b0 */
    paddd_r2r (mm6, mm5);      /* 6 ; b3=sum(odd3) b2=sum(odd2) */
    movq_r2r (mm0, mm4);        /* 4 ; a3 a2 */
    paddd_r2r (mm5, mm0);      /* a3+b3 a2+b2 */
    psubd_r2r (mm5, mm4);       /* 5 ; a3-b3 a2-b2 */
    psrad_i2r (SHIFT_INV_ROW, mm4);    /* y4=a3-b3 y5=a2-b2 */
    psrad_i2r (SHIFT_INV_ROW, mm0);     /* y3=a3+b3 y2=a2+b2 */
    packssdw_r2r (mm3, mm4);   /* 3 ; y6 y7 y4 y5 */
    packssdw_r2r (mm0, mm1);   /* 0 ; y3 y2 y1 y0 */
    movq_r2r (mm4, mm7);        /* 7 ; y6 y7 y4 y5 */
    psrld_i2r (16, mm4);       /* 0 y6 0 y4 */
    movq_r2m (mm1, *(out));    /* 1 ; save y3 y2 y1 y0 */
    pslld_i2r (16, mm7);        /* y7 0 y5 0 */
    por_r2r (mm4, mm7);        /* 4 ; y7 y6 y5 y4 */
    
        /* begin processing row 1 */
        movq_r2m (mm7, *(out + 4));     /* 7 ; save y7 y6 y5 y4 */
    inptr += 8;
    out += 8;
  }
  
      /* done with the iDCT row-transformation */
      
      /* now we have to transpose the output 8x8 matrix */
      /* 8x8 (OUT) -> 8x8't' (IN) */
      /* the transposition is implemented as 4 sub-operations. */
      /* 1) transpose upper-left quad */
      /* 2) transpose lower-right quad */
      /* 3) transpose lower-left quad */
      /* 4) transpose upper-right quad */
      
      /* mm0 = 1st row [ A B C D ] row1 */
      /* mm1 = 2nd row [ E F G H ] 2 */
      /* mm2 = 3rd row [ I J K L ] 3 */
      /* mm3 = 4th row [ M N O P ] 4 */
      
      /* 1) transpose upper-left quad */
      out = &qwTemp[0];
  movq_m2r (*(out + ROW_STRIDE * 0), mm0);
  movq_m2r (*(out + ROW_STRIDE * 1), mm1);
  movq_r2r (mm0, mm4);         /* mm4 = copy of row1[A B C D] */
  movq_m2r (*(out + ROW_STRIDE * 2), mm2);
  punpcklwd_r2r (mm1, mm0);    /* mm0 = [ 0 4 1 5] */
  movq_m2r (*(out + ROW_STRIDE * 3), mm3);
  punpckhwd_r2r (mm1, mm4);    /* mm4 = [ 2 6 3 7] */
  movq_r2r (mm2, mm6);
  punpcklwd_r2r (mm3, mm2);    /* mm2 = [ 8 12 9 13] */
  punpckhwd_r2r (mm3, mm6);    /* mm6 = 10 14 11 15] */
  movq_r2r (mm0, mm1);          /* mm1 = [ 0 4 1 5] */
  inptr = blk;
  punpckldq_r2r (mm2, mm0);   /* final result mm0 = row1 [0 4 8 12] */
  movq_r2r (mm4, mm3);         /* mm3 = [ 2 6 3 7] */
  punpckhdq_r2r (mm2, mm1);     /* mm1 = final result mm1 = row2 [1 5 9 13] */
  movq_r2m (mm0, *(inptr + ROW_STRIDE * 0));   /* store row 1 */
  punpckldq_r2r (mm6, mm4);     /* final result mm4 = row3 [2 6 10 14] */
  
/* begin reading next quadrant (lower-right) */
      movq_m2r (*(out + ROW_STRIDE * 4 + 4), mm0);
  punpckhdq_r2r (mm6, mm3);    /* final result mm3 = row4 [3 7 11 15] */
  movq_r2m (mm4, *(inptr + ROW_STRIDE * 2));   /* store row 3 */
  movq_r2r (mm0, mm4);          /* mm4 = copy of row1[A B C D] */
  movq_r2m (mm1, *(inptr + ROW_STRIDE * 1));   /* store row 2 */
  movq_m2r (*(out + ROW_STRIDE * 5 + 4), mm1);
  movq_r2m (mm3, *(inptr + ROW_STRIDE * 3));  /* store row 4 */
  punpcklwd_r2r (mm1, mm0);     /* mm0 = [ 0 4 1 5] */
  
      /* 2) transpose lower-right quadrant */
      
/*	movq mm0, qword ptr [OUT + ROW_STRIDE*4 + 8] */
      
/*	movq mm1, qword ptr [OUT + ROW_STRIDE*5 + 8] */
/*	 movq mm4, mm0;	// mm4 = copy of row1[A B C D] */
      movq_m2r (*(out + ROW_STRIDE * 6 + 4), mm2);
  
/*	 punpcklwd mm0, mm1; // mm0 = [ 0 4 1 5] */
      punpckhwd_r2r (mm1, mm4); /* mm4 = [ 2 6 3 7] */
  movq_m2r (*(out + ROW_STRIDE * 7 + 4), mm3);
  movq_r2r (mm2, mm6);
  punpcklwd_r2r (mm3, mm2);   /* mm2 = [ 8 12 9 13] */
  movq_r2r (mm0, mm1);          /* mm1 = [ 0 4 1 5] */
  punpckhwd_r2r (mm3, mm6);    /* mm6 = 10 14 11 15] */
  movq_r2r (mm4, mm3);          /* mm3 = [ 2 6 3 7] */
  punpckldq_r2r (mm2, mm0);    /* final result mm0 = row1 [0 4 8 12] */
  punpckhdq_r2r (mm2, mm1);    /* mm1 = final result mm1 = row2 [1 5 9 13] */
  ;                             /* slot */
  movq_r2m (mm0, *(inptr + ROW_STRIDE * 4 + 4));       /* store row 1 */
  punpckldq_r2r (mm6, mm4);     /* final result mm4 = row3 [2 6 10 14] */
  movq_m2r (*(out + ROW_STRIDE * 4), mm0);
  punpckhdq_r2r (mm6, mm3);    /* final result mm3 = row4 [3 7 11 15] */
  movq_r2m (mm4, *(inptr + ROW_STRIDE * 6 + 4));       /* store row 3 */
  movq_r2r (mm0, mm4);          /* mm4 = copy of row1[A B C D] */
  movq_r2m (mm1, *(inptr + ROW_STRIDE * 5 + 4));       /* store row 2 */
  ;                             /* slot */
  movq_m2r (*(out + ROW_STRIDE * 5), mm1);
  ;                            /* slot */
  movq_r2m (mm3, *(inptr + ROW_STRIDE * 7 + 4));       /* store row 4 */
  punpcklwd_r2r (mm1, mm0);     /* mm0 = [ 0 4 1 5] */
  
      /* 3) transpose lower-left */
/*	movq mm0, qword ptr [OUT + ROW_STRIDE * 4 ] */
      
/*	movq mm1, qword ptr [OUT + ROW_STRIDE * 5 ] */
/*	 movq mm4, mm0;	// mm4 = copy of row1[A B C D] */
      movq_m2r (*(out + ROW_STRIDE * 6), mm2);
  
/*	 punpcklwd mm0, mm1; // mm0 = [ 0 4 1 5] */
      punpckhwd_r2r (mm1, mm4); /* mm4 = [ 2 6 3 7] */
  movq_m2r (*(out + ROW_STRIDE * 7), mm3);
  movq_r2r (mm2, mm6);
  punpcklwd_r2r (mm3, mm2);   /* mm2 = [ 8 12 9 13] */
  movq_r2r (mm0, mm1);          /* mm1 = [ 0 4 1 5] */
  punpckhwd_r2r (mm3, mm6);    /* mm6 = 10 14 11 15] */
  movq_r2r (mm4, mm3);          /* mm3 = [ 2 6 3 7] */
  punpckldq_r2r (mm2, mm0);    /* final result mm0 = row1 [0 4 8 12] */
  punpckhdq_r2r (mm2, mm1);    /* mm1 = final result mm1 = row2 [1 5 9 13] */
  ;                             /*slot */
  movq_r2m (mm0, *(inptr + ROW_STRIDE * 0 + 4));       /* store row 1 */
  punpckldq_r2r (mm6, mm4);     /* final result mm4 = row3 [2 6 10 14] */
  
/* begin reading next quadrant (upper-right) */
      movq_m2r (*(out + ROW_STRIDE * 0 + 4), mm0);
  punpckhdq_r2r (mm6, mm3);    /* final result mm3 = row4 [3 7 11 15] */
  movq_r2m (mm4, *(inptr + ROW_STRIDE * 2 + 4));       /* store row 3 */
  movq_r2r (mm0, mm4);          /* mm4 = copy of row1[A B C D] */
  movq_r2m (mm1, *(inptr + ROW_STRIDE * 1 + 4));       /* store row 2 */
  movq_m2r (*(out + ROW_STRIDE * 1 + 4), mm1);
  movq_r2m (mm3, *(inptr + ROW_STRIDE * 3 + 4));      /* store row 4 */
  punpcklwd_r2r (mm1, mm0);     /* mm0 = [ 0 4 1 5] */
  
      /* 2) transpose lower-right quadrant */
      
/*	movq mm0, qword ptr [OUT + ROW_STRIDE*4 + 8] */
      
/*	movq mm1, qword ptr [OUT + ROW_STRIDE*5 + 8] */
/*	 movq mm4, mm0;	// mm4 = copy of row1[A B C D] */
      movq_m2r (*(out + ROW_STRIDE * 2 + 4), mm2);
  
/*	 punpcklwd mm0, mm1; // mm0 = [ 0 4 1 5] */
      punpckhwd_r2r (mm1, mm4); /* mm4 = [ 2 6 3 7] */
  movq_m2r (*(out + ROW_STRIDE * 3 + 4), mm3);
  movq_r2r (mm2, mm6);
  punpcklwd_r2r (mm3, mm2);   /* mm2 = [ 8 12 9 13] */
  movq_r2r (mm0, mm1);          /* mm1 = [ 0 4 1 5] */
  punpckhwd_r2r (mm3, mm6);    /* mm6 = 10 14 11 15] */
  movq_r2r (mm4, mm3);          /* mm3 = [ 2 6 3 7] */
  punpckldq_r2r (mm2, mm0);    /* final result mm0 = row1 [0 4 8 12] */
  punpckhdq_r2r (mm2, mm1);    /* mm1 = final result mm1 = row2 [1 5 9 13] */
  ;                             /* slot */
  movq_r2m (mm0, *(inptr + ROW_STRIDE * 4));   /* store row 1 */
  punpckldq_r2r (mm6, mm4);     /* final result mm4 = row3 [2 6 10 14] */
  movq_r2m (mm1, *(inptr + ROW_STRIDE * 5));   /* store row 2 */
  punpckhdq_r2r (mm6, mm3);     /* final result mm3 = row4 [3 7 11 15] */
  movq_r2m (mm4, *(inptr + ROW_STRIDE * 6));   /* store row 3 */
  ;                             /* slot */
  movq_r2m (mm3, *(inptr + ROW_STRIDE * 7));   /* store row 4 */
  ;                             /* slot */
}
static void 
idct_mmx32_cols (short *blk)
{                               /* transform all 8 cols of 8x8 iDCT block */
  int x;
  short *inptr = blk;

  
      /* Despite the function's name, the matrix is transformed */
      /* row by row.  This function is identical to idct_mmx32_rows(), */
      /* except for the SHIFT amount and ROUND_INV amount. */
      
      /* this subroutine performs two operations */
      /* 1) iDCT row transform */
      /*              for( i = 0; i < 8; ++ i) */
      /*                      DCT_8_INV_ROW_1( blk[i*8], qwTemp[i] ); */
      /* */
      /* 2) transpose the matrix (which was stored in qwTemp[]) */
      /*        qwTemp[] -> [8x8 matrix transpose] -> blk[] */
      for (x = 0; x < 8; x++) {       /* transform one row per iteration */
    movq_m2r (*(inptr), mm0);  /* 0 ; x3 x2 x1 x0 */
    movq_m2r (*(inptr + 4), mm1);      /* 1 ; x7 x6 x5 x4 */
    movq_r2r (mm0, mm2);        /* 2 ; x3 x2 x1 x0 */
    movq_m2r (*(tab_i_01234567), mm3); /* 3 ; w06 w04 w02 w00 */
    punpcklwd_r2r (mm1, mm0);   /* x5 x1 x4 x0 */
    
/* ---------- */
        movq_r2r (mm0, mm5);    /* 5 ; x5 x1 x4 x0 */
    punpckldq_r2r (mm0, mm0);   /* x4 x0 x4 x0 */
    movq_m2r (*(tab_i_01234567 + 4), mm4);     /* 4 ; w07 w05 w03 w01 */
    punpckhwd_r2r (mm1, mm2);   /* 1 ; x7 x3 x6 x2 */
    pmaddwd_r2r (mm0, mm3);    /* x4*w06+x0*w04 x4*w02+x0*w00 */
    movq_r2r (mm2, mm6);        /* 6 ; x7 x3 x6 x2 */
    movq_m2r (*(tab_i_01234567 + 16), mm1);    /* 1 ; w22 w20 w18 w16 */
    punpckldq_r2r (mm2, mm2);   /* x6 x2 x6 x2 */
    pmaddwd_r2r (mm2, mm4);    /* x6*w07+x2*w05 x6*w03+x2*w01 */
    punpckhdq_r2r (mm5, mm5);   /* x5 x1 x5 x1 */
    pmaddwd_m2r (*(tab_i_01234567 + 8), mm0);  /* x4*w14+x0*w12 x4*w10+x0*w08 */
    punpckhdq_r2r (mm6, mm6);   /* x7 x3 x7 x3 */
    movq_m2r (*(tab_i_01234567 + 20), mm7);    /* 7 ; w23 w21 w19 w17 */
    pmaddwd_r2r (mm5, mm1);     /* x5*w22+x1*w20 x5*w18+x1*w16 */
    paddd_m2r (*(r_inv_col), mm3);     /* +rounder */
    pmaddwd_r2r (mm6, mm7);     /* x7*w23+x3*w21 x7*w19+x3*w17 */
    pmaddwd_m2r (*(tab_i_01234567 + 12), mm2); /* x6*w15+x2*w13 x6*w11+x2*w09 */
    paddd_r2r (mm4, mm3);       /* 4 ; a1=sum(even1) a0=sum(even0) */
    pmaddwd_m2r (*(tab_i_01234567 + 24), mm5); /* x5*w30+x1*w28 x5*w26+x1*w24 */
    movq_r2r (mm3, mm4);        /* 4 ; a1 a0 */
    pmaddwd_m2r (*(tab_i_01234567 + 28), mm6); /* x7*w31+x3*w29 x7*w27+x3*w25 */
    paddd_r2r (mm7, mm1);       /* 7 ; b1=sum(odd1) b0=sum(odd0) */
    paddd_m2r (*(r_inv_col), mm0);     /* +rounder */
    psubd_r2r (mm1, mm3);       /* a1-b1 a0-b0 */
    psrad_i2r (SHIFT_INV_COL, mm3);    /* y6=a1-b1 y7=a0-b0 */
    paddd_r2r (mm4, mm1);       /* 4 ; a1+b1 a0+b0 */
    paddd_r2r (mm2, mm0);      /* 2 ; a3=sum(even3) a2=sum(even2) */
    psrad_i2r (SHIFT_INV_COL, mm1);     /* y1=a1+b1 y0=a0+b0 */
    paddd_r2r (mm6, mm5);      /* 6 ; b3=sum(odd3) b2=sum(odd2) */
    movq_r2r (mm0, mm4);        /* 4 ; a3 a2 */
    paddd_r2r (mm5, mm0);      /* a3+b3 a2+b2 */
    psubd_r2r (mm5, mm4);       /* 5 ; a3-b3 a2-b2 */
    psrad_i2r (SHIFT_INV_COL, mm4);   /* y4=a3-b3 y5=a2-b2 */
    psrad_i2r (SHIFT_INV_COL, mm0);     /* y3=a3+b3 y2=a2+b2 */
    packssdw_r2r (mm3, mm4);   /* 3 ; y6 y7 y4 y5 */
    packssdw_r2r (mm0, mm1);   /* 0 ; y3 y2 y1 y0 */
    movq_r2r (mm4, mm7);        /* 7 ; y6 y7 y4 y5 */
    psrld_i2r (16, mm4);       /* 0 y6 0 y4 */
    movq_r2m (mm1, *(inptr));  /* 1 ; save y3 y2 y1 y0 */
    pslld_i2r (16, mm7);        /* y7 0 y5 0 */
    por_r2r (mm4, mm7);        /* 4 ; y7 y6 y5 y4 */
    
        /* begin processing row 1 */
        movq_r2m (mm7, *(inptr + 4));   /* 7 ; save y7 y6 y5 y4 */
    inptr += 8;
  }
  
      /* done with the iDCT column-transformation */
}


/*	 */
/* public interface to MMX32 IDCT 8x8 operation */
/* */
void 
gst_idct_mmx32_idct (short *blk) 
{
  
      /* 1) iDCT row transformation */
      idct_mmx32_rows (blk);    /* 1) transform iDCT row, and transpose */
  
      /* 2) iDCT column transformation */
      idct_mmx32_cols (blk);    /* 2) transform iDCT row, and transpose */
  emms ();                     /* restore processor state */
  /* all done */
}


