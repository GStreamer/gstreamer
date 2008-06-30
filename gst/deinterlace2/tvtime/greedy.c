/*
 *
 * GStreamer
 * Copyright (c) 2000 Tom Barry  All rights reserved.
 * mmx.h port copyright (c) 2002 Billy Biggs <vektor@dumbterm.net>.
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

/*
 * Relicensed for GStreamer from GPL to LGPL with permit from Tom Barry
 * and Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "_stdint.h"

#include "gstdeinterlace2.h"
#include <string.h>

// This is a simple lightweight DeInterlace method that uses little CPU time
// but gives very good results for low or intermedite motion.
// It defers frames by one field, but that does not seem to produce noticeable
// lip sync problems.
//
// The method used is to take either the older or newer weave pixel depending
// upon which give the smaller comb factor, and then clip to avoid large damage
// when wrong.
//
// I'd intended this to be part of a larger more elaborate method added to 
// Blended Clip but this give too good results for the CPU to ignore here.

static const int GreedyMaxComb = 15;

static inline void
deinterlace_greedy_packed422_scanline_c (uint8_t * m0, uint8_t * t1,
    uint8_t * b1, uint8_t * m2, uint8_t * output, int width)
{
  int avg, l2_diff, lp2_diff, max, min, best;

  // L2 == m0
  // L1 == t1
  // L3 == b1
  // LP2 == m2

  while (width--) {
    avg = (*t1 + *b1) / 2;

    l2_diff = ABS (*m0 - avg);
    lp2_diff = ABS (*m2 - avg);

    if (l2_diff > lp2_diff)
      best = *m2;
    else
      best = *m0;

    max = MAX (*t1, *b1);
    min = MIN (*t1, *b1);

    if (max < 256 - GreedyMaxComb)
      max += GreedyMaxComb;
    else
      max = 255;

    if (min > GreedyMaxComb)
      min -= GreedyMaxComb;
    else
      min = 0;

    *output = CLAMP (best, min, max);

    // Advance to the next set of pixels.
    output += 1;
    m0 += 1;
    t1 += 1;
    b1 += 1;
    m2 += 1;
  }
}

#ifdef HAVE_CPU_I386
#include "mmx.h"
static void
deinterlace_greedy_packed422_scanline_mmx (uint8_t * m0, uint8_t * t1,
    uint8_t * b1, uint8_t * m2, uint8_t * output, int width)
{
  mmx_t MaxComb;

  mmx_t ShiftMask;

  // How badly do we let it weave? 0-255
  MaxComb.ub[0] = GreedyMaxComb;
  MaxComb.ub[1] = GreedyMaxComb;
  MaxComb.ub[2] = GreedyMaxComb;
  MaxComb.ub[3] = GreedyMaxComb;
  MaxComb.ub[4] = GreedyMaxComb;
  MaxComb.ub[5] = GreedyMaxComb;
  MaxComb.ub[6] = GreedyMaxComb;
  MaxComb.ub[7] = GreedyMaxComb;

  ShiftMask.ub[0] = 0x7f;
  ShiftMask.ub[1] = 0x7f;
  ShiftMask.ub[2] = 0x7f;
  ShiftMask.ub[3] = 0x7f;
  ShiftMask.ub[4] = 0x7f;
  ShiftMask.ub[5] = 0x7f;
  ShiftMask.ub[6] = 0x7f;
  ShiftMask.ub[7] = 0x7f;

  // L2 == m0
  // L1 == t1
  // L3 == b1
  // LP2 == m2  

  movq_m2r (MaxComb, mm6);

  for (; width > 7; width -= 8) {
    movq_m2r (*t1, mm1);        // L1
    movq_m2r (*m0, mm2);        // L2
    movq_m2r (*b1, mm3);        // L3
    movq_m2r (*m2, mm0);        // LP2

    // average L1 and L3 leave result in mm4
    movq_r2r (mm1, mm4);        // L1
    movq_r2r (mm3, mm5);        // L3
    psrlw_i2r (1, mm4);         // L1/2
    pand_m2r (ShiftMask, mm4);
    psrlw_i2r (1, mm5);         // L3/2
    pand_m2r (ShiftMask, mm5);
    paddusb_r2r (mm5, mm4);     // (L1 + L3) / 2

    // get abs value of possible L2 comb
    movq_r2r (mm2, mm7);        // L2
    psubusb_r2r (mm4, mm7);     // L2 - avg
    movq_r2r (mm4, mm5);        // avg
    psubusb_r2r (mm2, mm5);     // avg - L2
    por_r2r (mm7, mm5);         // abs(avg-L2)

    // get abs value of possible LP2 comb
    movq_r2r (mm0, mm7);        // LP2
    psubusb_r2r (mm4, mm7);     // LP2 - avg
    psubusb_r2r (mm0, mm4);     // avg - LP2
    por_r2r (mm7, mm4);         // abs(avg-LP2)

    // use L2 or LP2 depending upon which makes smaller comb
    psubusb_r2r (mm5, mm4);     // see if it goes to zero
    psubusb_r2r (mm5, mm5);     // 0
    pcmpeqb_r2r (mm5, mm4);     // if (mm4=0) then FF else 0
    pcmpeqb_r2r (mm4, mm5);     // opposite of mm4

    // if Comb(LP2) <= Comb(L2) then mm4=ff, mm5=0 else mm4=0, mm5 = 55
    pand_r2r (mm2, mm5);        // use L2 if mm5 == ff, else 0
    pand_r2r (mm0, mm4);        // use LP2 if mm4 = ff, else 0
    por_r2r (mm5, mm4);         // may the best win

    // Now lets clip our chosen value to be not outside of the range
    // of the high/low range L1-L3 by more than abs(L1-L3)
    // This allows some comb but limits the damages and also allows more
    // detail than a boring oversmoothed clip.

    movq_r2r (mm1, mm2);        // copy L1
    psubusb_r2r (mm3, mm2);     // - L3, with saturation
    paddusb_r2r (mm3, mm2);     // now = Max(L1,L3)

    pcmpeqb_r2r (mm7, mm7);     // all ffffffff
    psubusb_r2r (mm1, mm7);     // - L1 
    paddusb_r2r (mm7, mm3);     // add, may sat at fff..
    psubusb_r2r (mm7, mm3);     // now = Min(L1,L3)

    // allow the value to be above the high or below the low by amt of MaxComb
    paddusb_r2r (mm6, mm2);     // increase max by diff
    psubusb_r2r (mm6, mm3);     // lower min by diff

    psubusb_r2r (mm3, mm4);     // best - Min
    paddusb_r2r (mm3, mm4);     // now = Max(best,Min(L1,L3)

    pcmpeqb_r2r (mm7, mm7);     // all ffffffff
    psubusb_r2r (mm4, mm7);     // - Max(best,Min(best,L3) 
    paddusb_r2r (mm7, mm2);     // add may sat at FFF..
    psubusb_r2r (mm7, mm2);     // now = Min( Max(best, Min(L1,L3), L2 )=L2 clipped

    movq_r2m (mm2, *output);    // move in our clipped best

    // Advance to the next set of pixels.
    output += 8;
    m0 += 8;
    t1 += 8;
    b1 += 8;
    m2 += 8;
  }
  emms ();
  if (width > 0)
    deinterlace_greedy_packed422_scanline_c (m0, t1, b1, m2, output, width);
}

#include "sse.h"

static void
deinterlace_greedy_packed422_scanline_mmxext (uint8_t * m0, uint8_t * t1,
    uint8_t * b1, uint8_t * m2, uint8_t * output, int width)
{
  mmx_t MaxComb;

  // How badly do we let it weave? 0-255
  MaxComb.ub[0] = GreedyMaxComb;
  MaxComb.ub[1] = GreedyMaxComb;
  MaxComb.ub[2] = GreedyMaxComb;
  MaxComb.ub[3] = GreedyMaxComb;
  MaxComb.ub[4] = GreedyMaxComb;
  MaxComb.ub[5] = GreedyMaxComb;
  MaxComb.ub[6] = GreedyMaxComb;
  MaxComb.ub[7] = GreedyMaxComb;

  // L2 == m0
  // L1 == t1
  // L3 == b1
  // LP2 == m2

  movq_m2r (MaxComb, mm6);

  for (; width > 7; width -= 8) {
    movq_m2r (*t1, mm1);        // L1
    movq_m2r (*m0, mm2);        // L2
    movq_m2r (*b1, mm3);        // L3
    movq_m2r (*m2, mm0);        // LP2

    // average L1 and L3 leave result in mm4
    movq_r2r (mm1, mm4);        // L1
    pavgb_r2r (mm3, mm4);       // (L1 + L3)/2

    // get abs value of possible L2 comb
    movq_r2r (mm2, mm7);        // L2
    psubusb_r2r (mm4, mm7);     // L2 - avg
    movq_r2r (mm4, mm5);        // avg
    psubusb_r2r (mm2, mm5);     // avg - L2
    por_r2r (mm7, mm5);         // abs(avg-L2)

    // get abs value of possible LP2 comb
    movq_r2r (mm0, mm7);        // LP2
    psubusb_r2r (mm4, mm7);     // LP2 - avg
    psubusb_r2r (mm0, mm4);     // avg - LP2
    por_r2r (mm7, mm4);         // abs(avg-LP2)

    // use L2 or LP2 depending upon which makes smaller comb
    psubusb_r2r (mm5, mm4);     // see if it goes to zero
    pxor_r2r (mm5, mm5);        // 0
    pcmpeqb_r2r (mm5, mm4);     // if (mm4=0) then FF else 0
    pcmpeqb_r2r (mm4, mm5);     // opposite of mm4

    // if Comb(LP2) <= Comb(L2) then mm4=ff, mm5=0 else mm4=0, mm5 = 55
    pand_r2r (mm2, mm5);        // use L2 if mm5 == ff, else 0
    pand_r2r (mm0, mm4);        // use LP2 if mm4 = ff, else 0
    por_r2r (mm5, mm4);         // may the best win

    // Now lets clip our chosen value to be not outside of the range
    // of the high/low range L1-L3 by more than abs(L1-L3)
    // This allows some comb but limits the damages and also allows more
    // detail than a boring oversmoothed clip.

    movq_r2r (mm1, mm2);        // copy L1
    pmaxub_r2r (mm3, mm2);      // now = Max(L1,L3)

    pminub_r2r (mm1, mm3);      // now = Min(L1,L3)

    // allow the value to be above the high or below the low by amt of MaxComb
    paddusb_r2r (mm6, mm2);     // increase max by diff
    psubusb_r2r (mm6, mm3);     // lower min by diff


    pmaxub_r2r (mm3, mm4);      // now = Max(best,Min(L1,L3)
    pminub_r2r (mm4, mm2);      // now = Min( Max(best, Min(L1,L3)), L2 )=L2 clipped

    movq_r2m (mm2, *output);    // move in our clipped best

    // Advance to the next set of pixels.
    output += 8;
    m0 += 8;
    t1 += 8;
    b1 += 8;
    m2 += 8;
  }
  emms ();

  if (width > 0)
    deinterlace_greedy_packed422_scanline_c (m0, t1, b1, m2, output, width);
}

#endif

static void
deinterlace_frame_di_greedy (GstDeinterlace2 * object)
{
  void (*func) (uint8_t * L2, uint8_t * L1, uint8_t * L3, uint8_t * L2P,
      uint8_t * Dest, int size);

  int InfoIsOdd = 0;
  int Line;
  unsigned int Pitch = object->field_stride;
  unsigned char *L1;            // ptr to Line1, of 3
  unsigned char *L2;            // ptr to Line2, the weave line
  unsigned char *L3;            // ptr to Line3

  unsigned char *L2P;           // ptr to prev Line2
  unsigned char *Dest = GST_BUFFER_DATA (object->out_buf);

#ifdef HAVE_CPU_I386
  if (object->cpu_feature_flags & OIL_IMPL_FLAG_MMXEXT) {
    func = deinterlace_greedy_packed422_scanline_mmxext;
  } else if (object->cpu_feature_flags & OIL_IMPL_FLAG_MMX) {
    func = deinterlace_greedy_packed422_scanline_mmx;
  } else {
    func = deinterlace_greedy_packed422_scanline_c;
  }
#else
  func = deinterlace_greedy_packed422_scanline_c;
#endif

  // copy first even line no matter what, and the first odd line if we're
  // processing an EVEN field. (note diff from other deint rtns.)

  if (object->field_history[object->history_count - 1].flags ==
      PICTURE_INTERLACED_BOTTOM) {
    InfoIsOdd = 1;

    L1 = GST_BUFFER_DATA (object->field_history[object->history_count - 2].buf);
    L2 = GST_BUFFER_DATA (object->field_history[object->history_count - 1].buf);
    L3 = L1 + Pitch;
    L2P =
        GST_BUFFER_DATA (object->field_history[object->history_count - 3].buf);

    // copy first even line
    memcpy (Dest, L1, object->line_length);
    Dest += object->output_stride;
  } else {
    InfoIsOdd = 0;
    L1 = GST_BUFFER_DATA (object->field_history[object->history_count - 2].buf);
    L2 = GST_BUFFER_DATA (object->field_history[object->history_count -
            1].buf) + Pitch;
    L3 = L1 + Pitch;
    L2P =
        GST_BUFFER_DATA (object->field_history[object->history_count - 3].buf) +
        Pitch;

    // copy first even line
    memcpy (Dest, GST_BUFFER_DATA (object->field_history[0].buf),
        object->line_length);
    Dest += object->output_stride;
    // then first odd line
    memcpy (Dest, L1, object->line_length);
    Dest += object->output_stride;
  }

  for (Line = 0; Line < (object->field_height - 1); ++Line) {
    func (L2, L1, L3, L2P, Dest, object->line_length);
    Dest += object->output_stride;
    memcpy (Dest, L3, object->line_length);
    Dest += object->output_stride;

    L1 += Pitch;
    L2 += Pitch;
    L3 += Pitch;
    L2P += Pitch;
  }

  if (InfoIsOdd) {
    memcpy (Dest, L2, object->line_length);
  }
}

static deinterlace_method_t greedyl_method = {
  0,                            //DEINTERLACE_PLUGIN_API_VERSION,
  "Motion Adaptive: Simple Detection",
  "AdaptiveSimple",
  4,
  0,
  0,
  0,
  0,
  1,
  deinterlace_frame_di_greedy,
  {"Uses heuristics to detect motion in the input",
        "frames and reconstruct image detail where",
        "possible.  Use this for high quality output",
        "even on monitors set to an arbitrary refresh",
        "rate.",
        "",
        "Simple detection uses linear interpolation",
        "where motion is detected, using a two-field",
        "buffer.  This is the Greedy: Low Motion",
      "deinterlacer from DScaler."}
};

deinterlace_method_t *
dscaler_greedyl_get_method (void)
{
  return &greedyl_method;
}
