/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include "paint.h"
#include "gstmask.h"

enum
{
  BOX_VERTICAL 		= 1,
  BOX_HORIZONTAL 	= 2,
  TRIGANLE_LINEAR 	= 3,
};

static gint boxes_1b[][7] = 
{
#define WIPE_B1_1	0
  { BOX_VERTICAL,   0, 0, 0,  1, 1, 1 },
#define WIPE_B1_2	1
  { BOX_HORIZONTAL, 0, 0, 0,  1, 1, 1 }
};

static gint boxes_2b[][7*2] = 
{
#define WIPE_B2_21	0
  { BOX_VERTICAL,   0, 0, 1,  1, 2, 0,
    BOX_VERTICAL,   1, 0, 0,  2, 2, 1 },
#define WIPE_B2_22	1
  { BOX_HORIZONTAL, 0, 0, 1,  2, 1, 0,
    BOX_HORIZONTAL, 0, 1, 0,  2, 2, 1 },
};

static gint triangles_2t[][2*9] = 
{
  /* 3 -> 6 */
#define WIPE_T2_3	0
  { 0, 0, 0,  0, 1, 1,  1, 1, 1,   
    1, 0, 1,  0, 0, 0,  1, 1, 1 },
#define WIPE_T2_4	WIPE_T2_3+1
  { 0, 0, 1,  1, 0, 0,  0, 1, 1, 
    1, 0, 0,  0, 1, 1,  1, 1, 1 },
#define WIPE_T2_5	WIPE_T2_4+1
  { 0, 0, 1,  0, 1, 1,  1, 1, 0,
    1, 0, 1,  0, 0, 1,  1, 1, 0 },
#define WIPE_T2_6	WIPE_T2_5+1
  { 0, 0, 1,  1, 0, 1,  0, 1, 0,
    1, 0, 1,  0, 1, 0,  1, 1, 1 },
#define WIPE_T2_41	WIPE_T2_6+1
  { 0, 0, 0,  1, 0, 1,  0, 1, 1,
    1, 0, 1,  0, 1, 1,  1, 1, 2 },
#define WIPE_T2_42	WIPE_T2_41+1
  { 0, 0, 1,  1, 0, 0,  1, 1, 1,
    0, 0, 1,  0, 1, 2,  1, 1, 1 },
#define WIPE_T2_45	WIPE_T2_42+1
  { 0, 0, 1,  1, 0, 0,  0, 1, 0,
    1, 0, 0,  0, 1, 0,  1, 1, 1 },
#define WIPE_T2_46	WIPE_T2_45+1
  { 0, 0, 0,  1, 0, 1,  1, 1, 0,
    0, 0, 0,  0, 1, 1,  1, 1, 0 },
#define WIPE_T2_241	WIPE_T2_46+1
  { 0, 0, 0,  1, 0, 0,  1, 1, 1,
    0, 0, 0,  1, 1, 1,  0, 1, 2 },
#define WIPE_T2_242	WIPE_T2_241+1
  { 0, 1, 0,  1, 1, 0,  1, 0, 1,
    0, 1, 0,  1, 0, 1,  0, 0, 2 },
#define WIPE_T2_243	WIPE_T2_242+1
  { 1, 1, 0,  0, 1, 0,  0, 0, 1,
    1, 1, 0,  0, 0, 1,  1, 0, 2 },
#define WIPE_T2_244	WIPE_T2_243+1
  { 1, 0, 0,  0, 0, 0,  0, 1, 1,
    1, 0, 0,  0, 1, 1,  1, 1, 2 },
#define WIPE_T2_245	WIPE_T2_244+1
  { 0, 0, 0,  2, 0, 0,  2, 2, 1,   
    2, 2, 0,  0, 2, 0,  0, 0, 1 },
#define WIPE_T2_246	WIPE_T2_245+1
  { 0, 2, 0,  0, 0, 0,  2, 0, 1,   
    2, 0, 0,  2, 2, 0,  0, 2, 1 },
};

static gint triangles_3t[][3*9] = 
{
  /* 23 -> 26 */
#define WIPE_T3_23	0
  { 0, 0, 1,  1, 0, 0,  0, 2, 1,   
    1, 0, 0,  0, 2, 1,  2, 2, 1,
    1, 0, 0,  2, 0, 1,  2, 2, 1 },
#define WIPE_T3_24	1
  { 0, 0, 1,  2, 0, 1,  2, 1, 0,
    0, 0, 1,  2, 1, 0,  0, 2, 1,
    2, 1, 0,  0, 2, 1,  2, 2, 1 },
#define WIPE_T3_25	2
  { 0, 0, 1,  0, 2, 1,  1, 2, 0,
    0, 0, 1,  2, 0, 1,  1, 2, 0,
    2, 0, 1,  1, 2, 0,  2, 2, 1 },
#define WIPE_T3_26	3
  { 0, 0, 1,  2, 0, 1,  0, 1, 0,
    2, 0, 1,  0, 1, 0,  2, 2, 1,
    0, 1, 0,  0, 2, 1,  2, 2, 1 },
#define WIPE_T3_221	4
  { 1, 0, 0,  2, 0, 0,  2, 2, 1,   
    1, 0, 0,  2, 2, 1,  0, 2, 3,
    1, 0, 0,  0, 2, 3,  0, 0, 4 },
#define WIPE_T3_222	5
  { 2, 1, 0,  2, 2, 0,  0, 2, 1,   
    2, 1, 0,  0, 2, 1,  0, 0, 3,
    2, 1, 0,  0, 0, 3,  2, 0, 4 },
#define WIPE_T3_223	6
  { 1, 2, 0,  0, 2, 0,  0, 0, 1,   
    1, 2, 0,  0, 0, 1,  2, 0, 3,
    1, 2, 0,  2, 0, 3,  2, 2, 4 },
#define WIPE_T3_224	7
  { 0, 1, 0,  0, 0, 0,  2, 0, 1,   
    0, 1, 0,  2, 0, 1,  2, 2, 3,
    0, 1, 0,  2, 2, 3,  0, 2, 4 },
};

static gint triangles_4t[][4*9] = 
{
#define WIPE_T4_61	0
  { 0, 0, 1,  1, 0, 0,  1, 2, 1,   
    0, 0, 1,  0, 2, 2,  1, 2, 1,
    1, 0, 0,  2, 0, 1,  1, 2, 1,
    2, 0, 1,  1, 2, 1,  2, 2, 2 },
#define WIPE_T4_62	1
  { 0, 0, 2,  2, 0, 1,  0, 1, 1,   
    2, 0, 1,  0, 1, 1,  2, 1, 0,
    0, 1, 1,  2, 1, 0,  2, 2, 1,
    0, 1, 1,  0, 2, 2,  2, 2, 1 },
#define WIPE_T4_63	2
  { 0, 0, 2,  1, 0, 1,  0, 2, 1,   
    1, 0, 1,  0, 2, 1,  1, 2, 0,
    1, 0, 1,  1, 2, 0,  2, 2, 1,
    1, 0, 1,  2, 0, 2,  2, 2, 1 },
#define WIPE_T4_64	3
  { 0, 0, 1,  2, 0, 2,  2, 1, 1,   
    0, 0, 1,  0, 1, 0,  2, 1, 1,
    0, 1, 0,  2, 1, 1,  0, 2, 1,
    2, 1, 1,  0, 2, 1,  2, 2, 2 },
#define WIPE_T4_65	4
  { 0, 0, 0,  1, 0, 1,  1, 2, 0,   
    0, 0, 0,  0, 2, 1,  1, 2, 0,
    1, 0, 1,  2, 0, 0,  1, 2, 0,
    2, 0, 0,  1, 2, 0,  2, 2, 1 },
#define WIPE_T4_66	5
  { 0, 0, 1,  2, 0, 0,  0, 1, 0,   
    2, 0, 0,  0, 1, 0,  2, 1, 1,
    0, 1, 0,  2, 1, 1,  2, 2, 0,
    0, 1, 0,  0, 2, 1,  2, 2, 0 },
#define WIPE_T4_67	6
  { 0, 0, 1,  1, 0, 0,  0, 2, 0,   
    1, 0, 0,  0, 2, 0,  1, 2, 1,
    1, 0, 0,  1, 2, 1,  2, 2, 0,
    1, 0, 0,  2, 0, 1,  2, 2, 0 },
#define WIPE_T4_68	7
  { 0, 0, 0,  2, 0, 1,  2, 1, 0,   
    0, 0, 0,  0, 1, 1,  2, 1, 0,
    0, 1, 1,  2, 1, 0,  0, 2, 0,
    2, 1, 0,  0, 2, 0,  2, 2, 1 },
#define WIPE_T4_101	8
  { 0, 0, 1,  2, 0, 1,  1, 1, 0,   
    0, 0, 1,  1, 1, 0,  0, 2, 1,
    1, 1, 0,  0, 2, 1,  2, 2, 1,
    2, 0, 1,  1, 1, 0,  2, 2, 1 },
#define WIPE_T4_231	9
  { 1, 0, 0,  1, 2, 0,  2, 2, 1,   
    1, 0, 0,  2, 2, 1,  2, 0, 2,
    1, 0, 0,  1, 2, 0,  0, 2, 1,
    1, 0, 0,  0, 2, 1,  0, 0, 2 },
#define WIPE_T4_232	10
  { 2, 1, 0,  0, 1, 0,  0, 0, 1,   
    2, 1, 0,  0, 0, 1,  2, 0, 2,
    2, 1, 0,  0, 1, 0,  0, 2, 1,
    2, 1, 0,  0, 2, 1,  2, 2, 2 },
#define WIPE_T4_233	11
  { 1, 2, 0,  1, 0, 0,  2, 0, 1,   
    1, 2, 0,  2, 0, 1,  2, 2, 2,
    1, 2, 0,  1, 0, 0,  0, 0, 1,
    1, 2, 0,  0, 0, 1,  0, 2, 2 },
#define WIPE_T4_234	12
  { 0, 1, 0,  2, 1, 0,  2, 0, 1,   
    0, 1, 0,  2, 0, 1,  0, 0, 2,
    0, 1, 0,  2, 1, 0,  2, 2, 1,
    0, 1, 0,  2, 2, 1,  0, 2, 2 },
#define WIPE_T4_225	13
  { 1, 0, 0,  2, 0, 0,  2, 2, 1,   
    1, 0, 0,  2, 2, 1,  1, 2, 2,
    1, 2, 0,  0, 2, 0,  0, 0, 1,
    1, 2, 0,  0, 0, 1,  1, 0, 2 },
#define WIPE_T4_226	14
  { 0, 1, 0,  0, 0, 0,  2, 0, 1,   
    0, 1, 0,  2, 0, 1,  2, 1, 2,
    2, 1, 0,  2, 2, 0,  0, 2, 1,
    2, 1, 0,  0, 2, 1,  0, 1, 2 },
};

static gint triangles_5t[][5*9] = 
{
#define WIPE_T5_201	0
  { 1, 1, 0,  1, 0, 0,  2, 0, 1,   
    1, 1, 0,  2, 0, 1,  2, 2, 3,
    1, 1, 0,  2, 2, 3,  0, 2, 5,
    1, 1, 0,  0, 2, 5,  0, 0, 7,
    1, 1, 0,  0, 0, 7,  1, 0, 8 },
#define WIPE_T5_202	1
  { 1, 1, 0,  2, 1, 0,  2, 2, 1,   
    1, 1, 0,  2, 2, 1,  0, 2, 3,
    1, 1, 0,  0, 2, 3,  0, 0, 5,
    1, 1, 0,  0, 0, 5,  2, 0, 7,
    1, 1, 0,  2, 0, 7,  2, 1, 8 },
#define WIPE_T5_203	2
  { 1, 1, 0,  1, 2, 0,  0, 2, 1,   
    1, 1, 0,  0, 2, 1,  0, 0, 3,
    1, 1, 0,  0, 0, 3,  2, 0, 5,
    1, 1, 0,  2, 0, 5,  2, 2, 7,
    1, 1, 0,  2, 2, 7,  1, 2, 8 },
#define WIPE_T5_204	3
  { 1, 1, 0,  0, 1, 0,  0, 0, 1,   
    1, 1, 0,  0, 0, 1,  2, 0, 3,
    1, 1, 0,  2, 0, 3,  2, 2, 5,
    1, 1, 0,  2, 2, 5,  0, 2, 7,
    1, 1, 0,  0, 2, 7,  0, 1, 8 },
};

static gint triangles_6t[][6*9] = 
{
#define WIPE_T6_205	0
  { 1, 1, 0,  1, 0, 0,  2, 0, 1,   
    1, 1, 0,  2, 0, 1,  2, 2, 3,
    1, 1, 0,  2, 2, 3,  1, 2, 4,
    1, 1, 0,  1, 2, 0,  0, 2, 1,
    1, 1, 0,  0, 2, 1,  0, 0, 3,
    1, 1, 0,  0, 0, 3,  1, 0, 4 },
#define WIPE_T6_206	1
  { 1, 1, 0,  2, 1, 0,  2, 2, 1,   
    1, 1, 0,  2, 2, 1,  0, 2, 3,
    1, 1, 0,  0, 2, 3,  0, 1, 4,
    1, 1, 0,  0, 1, 0,  0, 0, 1,
    1, 1, 0,  0, 0, 1,  2, 0, 3,
    1, 1, 0,  2, 0, 3,  2, 1, 4 },
#define WIPE_T6_211	2
  { 1, 1, 0,  1, 0, 0,  2, 0, 1,   
    1, 1, 0,  2, 0, 1,  2, 2, 3,
    1, 1, 0,  2, 2, 3,  1, 2, 4,
    1, 1, 0,  1, 0, 0,  0, 0, 1,
    1, 1, 0,  0, 0, 1,  0, 2, 3,
    1, 1, 0,  0, 2, 3,  1, 2, 4 },
#define WIPE_T6_212	3
  { 1, 1, 0,  2, 1, 0,  2, 2, 1,   
    1, 1, 0,  2, 2, 1,  0, 2, 3,
    1, 1, 0,  0, 2, 3,  0, 1, 4,
    1, 1, 0,  2, 1, 0,  2, 0, 1,
    1, 1, 0,  2, 0, 1,  0, 0, 3,
    1, 1, 0,  0, 0, 3,  0, 1, 4 },
#define WIPE_T6_227	4
  { 1, 0, 0,  2, 0, 0,  2, 1, 1,   
    1, 0, 0,  2, 1, 1,  0, 1, 3,
    1, 0, 0,  0, 1, 3,  0, 0, 4,
    1, 2, 0,  2, 2, 0,  2, 1, 1,
    1, 2, 0,  2, 1, 1,  0, 1, 3,
    1, 2, 0,  0, 1, 3,  0, 2, 4 },
#define WIPE_T6_228	5
  { 0, 1, 0,  0, 0, 0,  1, 0, 1,   
    0, 1, 0,  1, 0, 1,  1, 2, 3,
    0, 1, 0,  1, 2, 3,  0, 2, 4,
    2, 1, 0,  2, 0, 0,  1, 0, 1,
    2, 1, 0,  1, 0, 1,  1, 2, 3,
    2, 1, 0,  1, 2, 3,  2, 2, 4 },
};
static gint triangles_8t[][8*9] = 
{
  /* 7 */
#define WIPE_T8_7	0
  { 0, 0, 0,  1, 0, 1,  1, 1, 1,   
    1, 0, 1,  2, 0, 0,  1, 1, 1,
    2, 0, 0,  1, 1, 1,  2, 1, 1,
    1, 1, 1,  2, 1, 1,  2, 2, 0,
    1, 1, 1,  1, 2, 1,  2, 2, 0,
    1, 1, 1,  0, 2, 0,  1, 2, 1,
    0, 1, 1,  1, 1, 1,  0, 2, 0,
    0, 0, 0,  0, 1, 1,  1, 1, 1 },
#define WIPE_T8_43	1
  { 0, 0, 1,  1, 0, 0,  1, 1, 1,   
    1, 0, 0,  2, 0, 1,  1, 1, 1,
    2, 0, 1,  1, 1, 1,  2, 1, 2,
    1, 1, 1,  2, 1, 2,  2, 2, 1,
    1, 1, 1,  1, 2, 0,  2, 2, 1,
    1, 1, 1,  0, 2, 1,  1, 2, 0,
    0, 1, 2,  1, 1, 1,  0, 2, 1,
    0, 0, 1,  0, 1, 2,  1, 1, 1 },
#define WIPE_T8_44	2
  { 0, 0, 1,  1, 0, 2,  1, 1, 1,   
    1, 0, 2,  2, 0, 1,  1, 1, 1,
    2, 0, 1,  1, 1, 1,  2, 1, 0,
    1, 1, 1,  2, 1, 0,  2, 2, 1,
    1, 1, 1,  1, 2, 2,  2, 2, 1,
    1, 1, 1,  0, 2, 1,  1, 2, 2,
    0, 1, 0,  1, 1, 1,  0, 2, 1,
    0, 0, 1,  0, 1, 0,  1, 1, 1 },
#define WIPE_T8_47	3
  { 0, 0, 0,  1, 0, 1,  1, 1, 0,   
    1, 0, 1,  2, 0, 0,  1, 1, 0,
    2, 0, 0,  1, 1, 0,  2, 1, 1,
    1, 1, 0,  2, 1, 1,  2, 2, 0,
    1, 1, 0,  1, 2, 1,  2, 2, 0,
    1, 1, 0,  0, 2, 0,  1, 2, 1,
    0, 1, 1,  1, 1, 0,  0, 2, 0,
    0, 0, 0,  0, 1, 1,  1, 1, 0 },
#define WIPE_T8_48	4
  { 0, 0, 1,  1, 0, 0,  0, 1, 0,   
    1, 0, 0,  0, 1, 0,  1, 1, 1,
    1, 0, 0,  2, 0, 1,  2, 1, 0,
    1, 0, 0,  1, 1, 1,  2, 1, 0,
    0, 1, 0,  1, 1, 1,  1, 2, 0,
    0, 1, 0,  0, 2, 1,  1, 2, 0,
    1, 1, 1,  2, 1, 0,  1, 2, 0,
    2, 1, 0,  1, 2, 0,  2, 2, 1 },
#define WIPE_T8_207	5
  { 1, 1, 0,  1, 0, 0,  2, 0, 1,   
    1, 1, 0,  2, 0, 1,  2, 1, 2,
    1, 1, 0,  2, 1, 0,  2, 2, 1,
    1, 1, 0,  2, 2, 1,  1, 2, 2,
    1, 1, 0,  1, 2, 0,  0, 2, 1,
    1, 1, 0,  0, 2, 1,  0, 1, 2,
    1, 1, 0,  0, 1, 0,  0, 0, 1,
    1, 1, 0,  0, 0, 1,  1, 0, 2 },
#define WIPE_T8_213	6
  { 1, 1, 0,  1, 0, 0,  2, 0, 1,   
    1, 1, 0,  2, 0, 1,  2, 1, 2,
    1, 1, 0,  1, 2, 0,  2, 2, 1,
    1, 1, 0,  2, 2, 1,  2, 1, 2,
    1, 1, 0,  1, 2, 0,  0, 2, 1,
    1, 1, 0,  0, 2, 1,  0, 1, 2,
    1, 1, 0,  1, 0, 0,  0, 0, 1,
    1, 1, 0,  0, 0, 1,  0, 1, 2 },
#define WIPE_T8_214	7
  { 1, 1, 0,  2, 1, 0,  2, 0, 1,   
    1, 1, 0,  2, 0, 1,  1, 0, 2,
    1, 1, 0,  2, 1, 0,  2, 2, 1,
    1, 1, 0,  2, 2, 1,  1, 2, 2,
    1, 1, 0,  0, 1, 0,  0, 2, 1,
    1, 1, 0,  0, 2, 1,  1, 2, 2,
    1, 1, 0,  0, 1, 0,  0, 0, 1,
    1, 1, 0,  0, 0, 1,  1, 0, 2 },
#define WIPE_T8_235	8
  { 1, 0, 0,  1, 1, 0,  2, 1, 1,   
    1, 0, 0,  2, 1, 1,  2, 0, 2,
    1, 0, 0,  1, 1, 0,  0, 1, 1,
    1, 0, 0,  0, 1, 1,  0, 0, 2,
    1, 2, 0,  1, 1, 0,  2, 1, 1,
    1, 2, 0,  2, 1, 1,  2, 2, 2,
    1, 2, 0,  1, 1, 0,  0, 1, 1,
    1, 2, 0,  0, 1, 1,  0, 2, 2 },
#define WIPE_T8_236	9
  { 0, 1, 0,  1, 1, 0,  1, 0, 1,   
    0, 1, 0,  1, 0, 1,  0, 0, 2,
    0, 1, 0,  1, 1, 0,  1, 2, 1,
    0, 1, 0,  1, 2, 1,  0, 2, 2,
    2, 1, 0,  1, 1, 0,  1, 0, 1,
    2, 1, 0,  1, 0, 1,  2, 0, 2,
    2, 1, 0,  1, 1, 0,  1, 2, 1,
    2, 1, 0,  1, 2, 1,  2, 2, 2 },
};

static gint triangles_16t[][16*9] = 
{
  /* 8 */
#define WIPE_T16_8	0
  { 0, 0, 1,  2, 0, 1,  1, 1, 0,   
    2, 0, 1,  1, 1, 0,  2, 2, 1,
    1, 1, 0,  0, 2, 1,  2, 2, 1,
    0, 0, 1,  1, 1, 0,  0, 2, 1,
    2, 0, 1,  4, 0, 1,  3, 1, 0,   
    4, 0, 1,  3, 1, 0,  4, 2, 1,
    3, 1, 0,  2, 2, 1,  4, 2, 1,
    2, 0, 1,  3, 1, 0,  2, 2, 1,
    0, 2, 1,  2, 2, 1,  1, 3, 0,   
    2, 2, 1,  1, 3, 0,  2, 4, 1,
    1, 3, 0,  0, 4, 1,  2, 4, 1,
    0, 2, 1,  1, 3, 0,  0, 4, 1,
    2, 2, 1,  4, 2, 1,  3, 3, 0,   
    4, 2, 1,  3, 3, 0,  4, 4, 1,
    3, 3, 0,  2, 4, 1,  4, 4, 1,
    2, 2, 1,  3, 3, 0,  2, 4, 1 }
};

typedef struct _GstWipeConfig GstWipeConfig;

struct _GstWipeConfig {
  gint 	*objects;
  gint   nobjects;
  gint   xscale;
  gint   yscale;
  gint   cscale;
};

static GstWipeConfig wipe_config[] = 
{
#define WIPE_CONFIG_1	0
  { boxes_1b[WIPE_B1_1],       1,  0, 0, 0 }, /* 1 */
#define WIPE_CONFIG_2	WIPE_CONFIG_1+1
  { boxes_1b[WIPE_B1_2],       1,  0, 0, 0 }, /* 2 */
#define WIPE_CONFIG_3	WIPE_CONFIG_2+1
  { triangles_2t[WIPE_T2_3],   2,  0, 0, 0 }, /* 3 */
#define WIPE_CONFIG_4	WIPE_CONFIG_3+1
  { triangles_2t[WIPE_T2_4],   2,  0, 0, 0 }, /* 4 */
#define WIPE_CONFIG_5	WIPE_CONFIG_4+1
  { triangles_2t[WIPE_T2_5],   2,  0, 0, 0 }, /* 5 */
#define WIPE_CONFIG_6	WIPE_CONFIG_5+1
  { triangles_2t[WIPE_T2_6],   2,  0, 0, 0 }, /* 6 */
#define WIPE_CONFIG_7	WIPE_CONFIG_6+1
  { triangles_8t[WIPE_T8_7],   8,  1, 1, 0 }, /* 7 */
#define WIPE_CONFIG_8	WIPE_CONFIG_7+1
  { triangles_16t[WIPE_T16_8], 16, 2, 2, 0 }, /* 8 */

#define WIPE_CONFIG_21	WIPE_CONFIG_8+1
  { boxes_2b[WIPE_B2_21],      2, 1, 1, 0 }, /* 21 */
#define WIPE_CONFIG_22	WIPE_CONFIG_21+1
  { boxes_2b[WIPE_B2_22],      2, 1, 1, 0 }, /* 22 */

#define WIPE_CONFIG_23	WIPE_CONFIG_22+1
  { triangles_3t[WIPE_T3_23],   3,  1, 1, 0 }, /* 23 */
#define WIPE_CONFIG_24	WIPE_CONFIG_23+1
  { triangles_3t[WIPE_T3_24],   3,  1, 1, 0 }, /* 24 */
#define WIPE_CONFIG_25	WIPE_CONFIG_24+1
  { triangles_3t[WIPE_T3_23],   3,  1, 1, 0 }, /* 25 */
#define WIPE_CONFIG_26	WIPE_CONFIG_25+1
  { triangles_3t[WIPE_T3_26],   3,  1, 1, 0 }, /* 26 */
#define WIPE_CONFIG_41	WIPE_CONFIG_26+1
  { triangles_2t[WIPE_T2_41],   2,  0, 0, 1 }, /* 41 */
#define WIPE_CONFIG_42	WIPE_CONFIG_41+1
  { triangles_2t[WIPE_T2_42],   2,  0, 0, 1 }, /* 42 */
#define WIPE_CONFIG_43	WIPE_CONFIG_42+1
  { triangles_8t[WIPE_T8_43],   8,  1, 1, 1 }, /* 43 */
#define WIPE_CONFIG_44	WIPE_CONFIG_43+1
  { triangles_8t[WIPE_T8_44],   8,  1, 1, 1 }, /* 44 */
#define WIPE_CONFIG_45	WIPE_CONFIG_44+1
  { triangles_2t[WIPE_T2_45],   2,  0, 0, 0 }, /* 45 */
#define WIPE_CONFIG_46	WIPE_CONFIG_45+1
  { triangles_2t[WIPE_T2_46],   2,  0, 0, 0 }, /* 46 */
#define WIPE_CONFIG_47	WIPE_CONFIG_46+1
  { triangles_8t[WIPE_T8_47],   8,  1, 1, 0 }, /* 47 */
#define WIPE_CONFIG_48	WIPE_CONFIG_47+1
  { triangles_8t[WIPE_T8_48],   8,  1, 1, 0 }, /* 48 */
#define WIPE_CONFIG_61	WIPE_CONFIG_48+1
  { triangles_4t[WIPE_T4_61],   4,  1, 1, 1 }, /* 61 */
#define WIPE_CONFIG_62	WIPE_CONFIG_61+1
  { triangles_4t[WIPE_T4_62],   4,  1, 1, 1 }, /* 62 */
#define WIPE_CONFIG_63	WIPE_CONFIG_62+1
  { triangles_4t[WIPE_T4_63],   4,  1, 1, 1 }, /* 63 */
#define WIPE_CONFIG_64	WIPE_CONFIG_63+1
  { triangles_4t[WIPE_T4_64],   4,  1, 1, 1 }, /* 64 */
#define WIPE_CONFIG_65	WIPE_CONFIG_64+1
  { triangles_4t[WIPE_T4_65],   4,  1, 1, 0 }, /* 65 */
#define WIPE_CONFIG_66	WIPE_CONFIG_65+1
  { triangles_4t[WIPE_T4_66],   4,  1, 1, 0 }, /* 66 */
#define WIPE_CONFIG_67	WIPE_CONFIG_66+1
  { triangles_4t[WIPE_T4_67],   4,  1, 1, 0 }, /* 67 */
#define WIPE_CONFIG_68	WIPE_CONFIG_67+1
  { triangles_4t[WIPE_T4_68],   4,  1, 1, 0 }, /* 68 */
#define WIPE_CONFIG_101	WIPE_CONFIG_68+1
  { triangles_4t[WIPE_T4_101],  4,  1, 1, 0 }, /* 101 */
#define WIPE_CONFIG_201	WIPE_CONFIG_101+1
  { triangles_5t[WIPE_T5_201],  5,  1, 1, 3 }, /* 201 */
#define WIPE_CONFIG_202	WIPE_CONFIG_201+1
  { triangles_5t[WIPE_T5_202],  5,  1, 1, 3 }, /* 202 */
#define WIPE_CONFIG_203	WIPE_CONFIG_202+1
  { triangles_5t[WIPE_T5_203],  5,  1, 1, 3 }, /* 203 */
#define WIPE_CONFIG_204	WIPE_CONFIG_203+1
  { triangles_5t[WIPE_T5_204],  5,  1, 1, 3 }, /* 204 */
#define WIPE_CONFIG_205	WIPE_CONFIG_204+1
  { triangles_6t[WIPE_T6_205],  6,  1, 1, 2 }, /* 205 */
#define WIPE_CONFIG_206	WIPE_CONFIG_205+1
  { triangles_6t[WIPE_T6_206],  6,  1, 1, 2 }, /* 206 */
#define WIPE_CONFIG_207	WIPE_CONFIG_206+1
  { triangles_8t[WIPE_T8_207],  8,  1, 1, 1 }, /* 207 */
#define WIPE_CONFIG_211	WIPE_CONFIG_207+1
  { triangles_6t[WIPE_T6_211],  6,  1, 1, 2 }, /* 211 */
#define WIPE_CONFIG_212	WIPE_CONFIG_211+1
  { triangles_6t[WIPE_T6_212],  6,  1, 1, 2 }, /* 212 */
#define WIPE_CONFIG_213	WIPE_CONFIG_212+1
  { triangles_8t[WIPE_T8_213],  8,  1, 1, 1 }, /* 213 */
#define WIPE_CONFIG_214	WIPE_CONFIG_213+1
  { triangles_8t[WIPE_T8_214],  8,  1, 1, 1 }, /* 214 */
#define WIPE_CONFIG_221	WIPE_CONFIG_214+1
  { triangles_3t[WIPE_T3_221],  3,  1, 1, 2 }, /* 221 */
#define WIPE_CONFIG_222	WIPE_CONFIG_221+1
  { triangles_3t[WIPE_T3_222],  3,  1, 1, 2 }, /* 222 */
#define WIPE_CONFIG_223	WIPE_CONFIG_222+1
  { triangles_3t[WIPE_T3_223],  3,  1, 1, 2 }, /* 223 */
#define WIPE_CONFIG_224	WIPE_CONFIG_223+1
  { triangles_3t[WIPE_T3_224],  3,  1, 1, 2 }, /* 224 */
#define WIPE_CONFIG_225	WIPE_CONFIG_224+1
  { triangles_4t[WIPE_T4_225],  4,  1, 1, 1 }, /* 225 */
#define WIPE_CONFIG_226	WIPE_CONFIG_225+1
  { triangles_4t[WIPE_T4_226],  4,  1, 1, 1 }, /* 226 */
#define WIPE_CONFIG_227	WIPE_CONFIG_226+1
  { triangles_6t[WIPE_T6_227],  6,  1, 1, 2 }, /* 227 */
#define WIPE_CONFIG_228	WIPE_CONFIG_227+1
  { triangles_6t[WIPE_T6_228],  6,  1, 1, 2 }, /* 228 */
#define WIPE_CONFIG_231	WIPE_CONFIG_228+1
  { triangles_4t[WIPE_T4_231],  4,  1, 1, 1 }, /* 231 */
#define WIPE_CONFIG_232	WIPE_CONFIG_231+1
  { triangles_4t[WIPE_T4_232],  4,  1, 1, 1 }, /* 232 */
#define WIPE_CONFIG_233	WIPE_CONFIG_232+1
  { triangles_4t[WIPE_T4_233],  4,  1, 1, 1 }, /* 233 */
#define WIPE_CONFIG_234	WIPE_CONFIG_233+1
  { triangles_4t[WIPE_T4_234],  4,  1, 1, 1 }, /* 234 */
#define WIPE_CONFIG_235	WIPE_CONFIG_234+1
  { triangles_8t[WIPE_T8_235],  8,  1, 1, 1 }, /* 235 */
#define WIPE_CONFIG_236	WIPE_CONFIG_235+1
  { triangles_8t[WIPE_T8_236],  8,  1, 1, 1 }, /* 236 */
#define WIPE_CONFIG_241	WIPE_CONFIG_236+1
  { triangles_2t[WIPE_T2_241],  2,  0, 0, 1 }, /* 241 */
#define WIPE_CONFIG_242	WIPE_CONFIG_241+1
  { triangles_2t[WIPE_T2_242],  2,  0, 0, 1 }, /* 242 */
#define WIPE_CONFIG_243	WIPE_CONFIG_242+1
  { triangles_2t[WIPE_T2_243],  2,  0, 0, 1 }, /* 243 */
#define WIPE_CONFIG_244	WIPE_CONFIG_243+1
  { triangles_2t[WIPE_T2_244],  2,  0, 0, 1 }, /* 244 */
#define WIPE_CONFIG_245	WIPE_CONFIG_244+1
  { triangles_2t[WIPE_T2_245],  2,  1, 1, 0 }, /* 245 */
#define WIPE_CONFIG_246	WIPE_CONFIG_245+1
  { triangles_2t[WIPE_T2_246],  2,  1, 1, 0 }, /* 246 */
};

static void
gst_wipe_boxes_draw (GstMask *mask)
{
  GstWipeConfig *config = mask->user_data;
  gint *impacts = config->objects;
  gint width = (mask->width >> config->xscale);
  gint height = (mask->height >> config->yscale);
  gint depth = (1 << mask->bpp) >> config->cscale;

  gint i;

  for (i = 0; i < config->nobjects; i++) {
    switch (impacts[0]) {
      case BOX_VERTICAL:
        gst_smpte_paint_vbox (mask->data, mask->width, 
		          impacts[1] * width, impacts[2] * height, impacts[3] * depth,
		          impacts[4] * width, impacts[5] * height, impacts[6] * depth);
	impacts += 7;
        break;
      case BOX_HORIZONTAL:
        gst_smpte_paint_hbox (mask->data, mask->width, 
		          impacts[1] * width, impacts[2] * height, impacts[3] * depth,
		          impacts[4] * width, impacts[5] * height, impacts[6] * depth);
	impacts += 7;
      default:
        break;
    }
  }
}

static void
gst_wipe_triangles_clock_draw (GstMask *mask)
{
  GstWipeConfig *config = mask->user_data;
  gint *impacts = config->objects;
  gint width = (mask->width >> config->xscale);
  gint height = (mask->height >> config->yscale);
  gint depth = (1 << mask->bpp) >> config->cscale;
  gint i;

  g_print ("width %d %d\n", mask->width, width);


  for (i = 0; i < config->nobjects; i++) {
    gst_smpte_paint_triangle_clock (mask->data, mask->width,
	                   impacts[0] * width,
			   impacts[1] * height,
			   impacts[2] * depth,
	                   impacts[3] * width,
			   impacts[4] * height,
			   impacts[5] * depth,
	                   impacts[6] * width,
			   impacts[7] * height,
			   impacts[8] * depth);
    impacts += 9;
  }
}

static void
gst_wipe_triangles_draw (GstMask *mask)
{
  GstWipeConfig *config = mask->user_data;
  gint *impacts = config->objects;
  gint width = (mask->width >> config->xscale);
  gint height = (mask->height >> config->yscale);
  gint depth = (1 << mask->bpp) >> config->cscale;

  gint i;

  for (i = 0; i < config->nobjects; i++) {
    gst_smpte_paint_triangle_linear (mask->data, mask->width,
	                   impacts[0] * width, impacts[1] * height, impacts[2] * depth,
	                   impacts[3] * width, impacts[4] * height, impacts[5] * depth,
	                   impacts[6] * width, impacts[7] * height, impacts[8] * depth);
    impacts += 9;
  }
}

static GstMaskDefinition definitions[] = { 
 { 1,  "bar_wipe_lr", 
       "A bar moves from left to right", 
	gst_wipe_boxes_draw, _gst_mask_default_destroy, 
	&wipe_config[WIPE_CONFIG_1] },
 { 2,  "bar_wipe_tb", 
       "A bar moves from top to bottom", 
	gst_wipe_boxes_draw, _gst_mask_default_destroy, 
	&wipe_config[WIPE_CONFIG_2] },
 { 3,  "box_wipe_tl", 
       "A box expands from the upper-left corner to the lower-right corner", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy, 
	&wipe_config[WIPE_CONFIG_3] },
 { 4,  "box_wipe_tr", 
       "A box expands from the upper-right corner to the lower-left corner", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy, 
	&wipe_config[WIPE_CONFIG_4] },
 { 5,  "box_wipe_br", 
       "A box expands from the lower-right corner to the upper-left corner", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy, 
	&wipe_config[WIPE_CONFIG_5] },
 { 6,  "box_wipe_bl", 
       "A box expands from the lower-left corner to the upper-right corner", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_6] },
 { 7 , "four_box_wipe_ci", 
       "A box shape expands from each of the four corners toward the center", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_7] },
 { 8 , "four_box_wipe_co", 
       "A box shape expands from the center of each quadrant toward the corners of each quadrant", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_8] },
 { 21, "barndoor_v",
       "A central, vertical line splits and expands toward the left and right edges", 
	gst_wipe_boxes_draw, _gst_mask_default_destroy, 
	&wipe_config[WIPE_CONFIG_21] },
 { 22, "barndoor_h",
       "A central, horizontal line splits and expands toward the top and bottom edges", 
	gst_wipe_boxes_draw, _gst_mask_default_destroy, 
	&wipe_config[WIPE_CONFIG_22] },
 { 23, "box_wipe_tc",
       "A box expands from the top edge's midpoint to the bottom corners", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_23] },
 { 24, "box_wipe_rc",
       "A box expands from the right edge's midpoint to the left corners", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_24] },
 { 25, "box_wipe_bc",
       "A box expands from the bottom edge's midpoint to the top corners", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_25] },
 { 26, "box_wipe_lc",
       "A box expands from the left edge's midpoint to the right corners", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_26] },
 { 41, "diagonal_tl",
       "A diagonal line moves from the upper-left corner to the lower-right corner", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_41] },
 { 42, "diagonal_tr",
       "A diagonal line moves from the upper right corner to the lower-left corner", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_42] },
 { 43, "bowtie_v",
       "Two wedge shapes slide in from the top and bottom edges toward the center", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_43] },
 { 44, "bowtie_h",
       "Two wedge shapes slide in from the left and right edges toward the center", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_44] },
 { 45, "barndoor_dbl",
       "A diagonal line from the lower-left to upper-right corners splits and expands toward the opposite corners", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_45] },
 { 46, "barndoor_dtl",
       "A diagonal line from upper-left to lower-right corners splits and expands toward the opposite corners", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_46] },
 { 47, "misc_diagonal_dbd",
       "Four wedge shapes split from the center and retract toward the four edges", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_47] },
 { 48, "misc_diagonal_dd",
       "A diamond connecting the four edge midpoints simultaneously contracts toward the center and expands toward the edges", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_48] },
 { 61, "vee_d",
       "A wedge shape moves from top to bottom", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_61] },
 { 62, "vee_l",
       "A wedge shape moves from right to left", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_62] },
 { 63, "vee_u",
       "A wedge shape moves from bottom to top", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_63] },
 { 64, "vee_r",
       "A wedge shape moves from left to right", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_64] },
 { 65, "barnvee_d",
       "A 'V' shape extending from the bottom edge's midpoint to the opposite corners contracts toward the center and expands toward the edges", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_65] },
 { 66, "barnvee_l",
       "A 'V' shape extending from the left edge's midpoint to the opposite corners contracts toward the center and expands toward the edges", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_66] },
 { 67, "barnvee_u",
       "A 'V' shape extending from the top edge's midpoint to the opposite corners contracts toward the center and expands toward the edges", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_67] },
 { 68, "barnvee_r",
       "A 'V' shape extending from the right edge's midpoint to the opposite corners contracts toward the center and expands toward the edges", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_68] },
 { 101, "iris_rect",
	"A rectangle expands from the center.", 
	gst_wipe_triangles_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_101] },
 { 201, "clock_cw12",
	"A radial hand sweeps clockwise from the twelve o'clock position", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_201] },
 { 202, "clock_cw3",
	"A radial hand sweeps clockwise from the three o'clock position", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_202] },
 { 203, "clock_cw6",
	"A radial hand sweeps clockwise from the six o'clock position", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_203] },
 { 204, "clock_cw9",
	"A radial hand sweeps clockwise from the nine o'clock position", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_204] },
 { 205, "pinwheel_tbv",
	"Two radial hands sweep clockwise from the twelve and six o'clock positions", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_205] },
 { 206, "pinwheel_tbh",
	"Two radial hands sweep clockwise from the nine and three o'clock positions", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_206] },
 { 207, "pinwheel_fb",
	"Four radial hands sweep clockwise", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_207] },
 { 211, "fan_ct",
	"A fan unfolds from the top edge, the fan axis at the center", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_211] },
 { 212, "fan_cr",
	"A fan unfolds from the right edge, the fan axis at the center", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_212] },
 { 213, "doublefan_fov",
	"Two fans, their axes at the center, unfold from the top and bottom", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_213] },
 { 214, "doublefan_foh",
	"Two fans, their axes at the center, unfold from the left and right", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_214] },
 { 221, "singlesweep_cwt",
	"A radial hand sweeps clockwise from the top edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_221] },
 { 222, "singlesweep_cwr",
	"A radial hand sweeps clockwise from the right edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_222] },
 { 223, "singlesweep_cwb",
	"A radial hand sweeps clockwise from the bottom edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_223] },
 { 224, "singlesweep_cwl",
	"A radial hand sweeps clockwise from the left edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_224] },
 { 225, "doublesweep_pv",
	"Two radial hands sweep clockwise and counter-clockwise from the top and bottom edges' midpoints", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_225] },
 { 226, "doublesweep_pd",
	"Two radial hands sweep clockwise and counter-clockwise from the left and right edges' midpoints", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_226] },
 { 227, "doublesweep_ov",
	"Two radial hands attached at the top and bottom edges' midpoints sweep from right to left", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_227] },
 { 228, "doublesweep_oh",
	"Two radial hands attached at the left and right edges' midpoints sweep from top to bottom", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_228] },
 { 231, "fan_t",
	"A fan unfolds from the bottom, the fan axis at the top edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_231] },
 { 232, "fan_r",
	"A fan unfolds from the left, the fan axis at the right edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_232] },
 { 233, "fan_b",
	"A fan unfolds from the top, the fan axis at the bottom edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_233] },
 { 234, "fan_l",
	"A fan unfolds from the right, the fan axis at the left edge's midpoint", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_234] },
 { 235, "doublefan_fiv",
	"Two fans, their axes at the top and bottom, unfold from the center", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_235] },
 { 236, "doublefan_fih",
	"Two fans, their axes at the left and right, unfold from the center", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_236] },
 { 241, "singlesweep_cwtl",
	"A radial hand sweeps clockwise from the upper-left corner", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_241] },
 { 242, "singlesweep_cwbl",
	"A radial hand sweeps counter-clockwise from the lower-left corner.", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_242] },
 { 243, "singlesweep_cwbr",
	"A radial hand sweeps clockwise from the lower-right corner", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_243] },
 { 244, "singlesweep_cwtr",
	"A radial hand sweeps counter-clockwise from the upper-right corner", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_244] },
 { 245, "doublesweep_pdtl",
	"Two radial hands attached at the upper-left and lower-right corners sweep down and up", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_245] },
 { 246, "doublesweep_pdbl",
	"Two radial hands attached at the lower-left and upper-right corners sweep down and up", 
	gst_wipe_triangles_clock_draw, _gst_mask_default_destroy,
	&wipe_config[WIPE_CONFIG_246] },
 { 0, NULL, NULL, NULL }
};

void
_gst_barboxwipes_register (void)
{
  gint i = 0;

  while (definitions[i].short_name) {
    _gst_mask_register (&definitions[i]);
    i++;
  }
}

