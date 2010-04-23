/*
 *
 * GStreamer
 * Copyright (c) 2000 Tom Barry  All rights reserved.
 * mmx.h port copyright (c) 2002 Billy Biggs <vektor@dumbterm.net>.
 *
 * Copyright (C) 2008,2010 Sebastian Dröge <slomo@collabora.co.uk>
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

#include "gstdeinterlacemethod.h"
#include <string.h>

#define GST_TYPE_DEINTERLACE_METHOD_GREEDY_L	(gst_deinterlace_method_greedy_l_get_type ())
#define GST_IS_DEINTERLACE_METHOD_GREEDY_L(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_GREEDY_L))
#define GST_IS_DEINTERLACE_METHOD_GREEDY_L_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_GREEDY_L))
#define GST_DEINTERLACE_METHOD_GREEDY_L_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_GREEDY_L, GstDeinterlaceMethodGreedyLClass))
#define GST_DEINTERLACE_METHOD_GREEDY_L(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_GREEDY_L, GstDeinterlaceMethodGreedyL))
#define GST_DEINTERLACE_METHOD_GREEDY_L_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_GREEDY_L, GstDeinterlaceMethodGreedyLClass))
#define GST_DEINTERLACE_METHOD_GREEDY_L_CAST(obj)	((GstDeinterlaceMethodGreedyL*)(obj))

GType gst_deinterlace_method_greedy_l_get_type (void);

typedef struct
{
  GstDeinterlaceMethod parent;

  guint max_comb;
} GstDeinterlaceMethodGreedyL;

typedef struct
{
  GstDeinterlaceMethodClass parent_class;
  void (*scanline) (GstDeinterlaceMethodGreedyL * self, const guint8 * L2,
      const guint8 * L1, const guint8 * L3, const guint8 * L2P, guint8 * Dest,
      gint width);
} GstDeinterlaceMethodGreedyLClass;

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

static inline void
deinterlace_greedy_scanline_c (GstDeinterlaceMethodGreedyL * self,
    const guint8 * m0, const guint8 * t1,
    const guint8 * b1, const guint8 * m2, guint8 * output, gint width)
{
  gint avg, l2_diff, lp2_diff, max, min, best;
  guint max_comb = self->max_comb;

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

    if (max < 256 - max_comb)
      max += max_comb;
    else
      max = 255;

    if (min > max_comb)
      min -= max_comb;
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

#ifdef BUILD_X86_ASM
#include "mmx.h"
static void
deinterlace_greedy_scanline_mmx (GstDeinterlaceMethodGreedyL * self,
    const guint8 * m0, const guint8 * t1,
    const guint8 * b1, const guint8 * m2, guint8 * output, gint width)
{
  mmx_t MaxComb;
  mmx_t ShiftMask;

  // How badly do we let it weave? 0-255
  MaxComb.ub[0] = self->max_comb;
  MaxComb.ub[1] = self->max_comb;
  MaxComb.ub[2] = self->max_comb;
  MaxComb.ub[3] = self->max_comb;
  MaxComb.ub[4] = self->max_comb;
  MaxComb.ub[5] = self->max_comb;
  MaxComb.ub[6] = self->max_comb;
  MaxComb.ub[7] = self->max_comb;

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
    deinterlace_greedy_scanline_c (self, m0, t1, b1, m2, output, width);
}

#include "sse.h"

static void
deinterlace_greedy_scanline_mmxext (GstDeinterlaceMethodGreedyL *
    self, const guint8 * m0, const guint8 * t1, const guint8 * b1,
    const guint8 * m2, guint8 * output, gint width)
{
  mmx_t MaxComb;

  // How badly do we let it weave? 0-255
  MaxComb.ub[0] = self->max_comb;
  MaxComb.ub[1] = self->max_comb;
  MaxComb.ub[2] = self->max_comb;
  MaxComb.ub[3] = self->max_comb;
  MaxComb.ub[4] = self->max_comb;
  MaxComb.ub[5] = self->max_comb;
  MaxComb.ub[6] = self->max_comb;
  MaxComb.ub[7] = self->max_comb;

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
    deinterlace_greedy_scanline_c (self, m0, t1, b1, m2, output, width);
}

#endif

static void
deinterlace_frame_di_greedy_packed (GstDeinterlaceMethod * method,
    const GstDeinterlaceField * history, guint history_count,
    GstBuffer * outbuf)
{
  GstDeinterlaceMethodGreedyL *self = GST_DEINTERLACE_METHOD_GREEDY_L (method);
  GstDeinterlaceMethodGreedyLClass *klass =
      GST_DEINTERLACE_METHOD_GREEDY_L_GET_CLASS (self);
  gint InfoIsOdd = 0;
  gint Line;
  gint RowStride = method->row_stride[0];
  gint FieldHeight = method->frame_height / 2;
  gint Pitch = method->row_stride[0] * 2;
  const guint8 *L1;             // ptr to Line1, of 3
  const guint8 *L2;             // ptr to Line2, the weave line
  const guint8 *L3;             // ptr to Line3
  const guint8 *L2P;            // ptr to prev Line2
  guint8 *Dest = GST_BUFFER_DATA (outbuf);

  // copy first even line no matter what, and the first odd line if we're
  // processing an EVEN field. (note diff from other deint rtns.)

  if (history[history_count - 1].flags == PICTURE_INTERLACED_BOTTOM) {
    InfoIsOdd = 1;

    L1 = GST_BUFFER_DATA (history[history_count - 2].buf);
    if (history[history_count - 2].flags & PICTURE_INTERLACED_BOTTOM)
      L1 += RowStride;

    L2 = GST_BUFFER_DATA (history[history_count - 1].buf);
    if (history[history_count - 1].flags & PICTURE_INTERLACED_BOTTOM)
      L2 += RowStride;

    L3 = L1 + Pitch;
    L2P = GST_BUFFER_DATA (history[history_count - 3].buf);
    if (history[history_count - 3].flags & PICTURE_INTERLACED_BOTTOM)
      L2P += RowStride;

    // copy first even line
    oil_memcpy (Dest, L1, RowStride);
    Dest += RowStride;
  } else {
    InfoIsOdd = 0;
    L1 = GST_BUFFER_DATA (history[history_count - 2].buf);
    if (history[history_count - 2].flags & PICTURE_INTERLACED_BOTTOM)
      L1 += RowStride;

    L2 = GST_BUFFER_DATA (history[history_count - 1].buf) + Pitch;
    if (history[history_count - 1].flags & PICTURE_INTERLACED_BOTTOM)
      L2 += RowStride;

    L3 = L1 + Pitch;
    L2P = GST_BUFFER_DATA (history[history_count - 3].buf) + Pitch;
    if (history[history_count - 3].flags & PICTURE_INTERLACED_BOTTOM)
      L2P += RowStride;

    // copy first even line
    oil_memcpy (Dest, GST_BUFFER_DATA (history[0].buf), RowStride);
    Dest += RowStride;
    // then first odd line
    oil_memcpy (Dest, L1, RowStride);
    Dest += RowStride;
  }

  for (Line = 0; Line < (FieldHeight - 1); ++Line) {
    klass->scanline (self, L2, L1, L3, L2P, Dest, RowStride);
    Dest += RowStride;
    oil_memcpy (Dest, L3, RowStride);
    Dest += RowStride;

    L1 += Pitch;
    L2 += Pitch;
    L3 += Pitch;
    L2P += Pitch;
  }

  if (InfoIsOdd) {
    oil_memcpy (Dest, L2, RowStride);
  }
}

G_DEFINE_TYPE (GstDeinterlaceMethodGreedyL, gst_deinterlace_method_greedy_l,
    GST_TYPE_DEINTERLACE_METHOD);

enum
{
  PROP_0,
  PROP_MAX_COMB
};

static void
gst_deinterlace_method_greedy_l_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlaceMethodGreedyL *self = GST_DEINTERLACE_METHOD_GREEDY_L (object);

  switch (prop_id) {
    case PROP_MAX_COMB:
      self->max_comb = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_deinterlace_method_greedy_l_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeinterlaceMethodGreedyL *self = GST_DEINTERLACE_METHOD_GREEDY_L (object);

  switch (prop_id) {
    case PROP_MAX_COMB:
      g_value_set_uint (value, self->max_comb);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_deinterlace_method_greedy_l_class_init (GstDeinterlaceMethodGreedyLClass *
    klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;
#ifdef BUILD_X86_ASM
  guint cpu_flags = oil_cpu_get_flags ();
#endif

  gobject_class->set_property = gst_deinterlace_method_greedy_l_set_property;
  gobject_class->get_property = gst_deinterlace_method_greedy_l_get_property;

  g_object_class_install_property (gobject_class, PROP_MAX_COMB,
      g_param_spec_uint ("max-comb",
          "Max comb",
          "Max Comb", 0, 255, 15, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  dim_class->fields_required = 4;
  dim_class->name = "Motion Adaptive: Simple Detection";
  dim_class->nick = "greedyl";
  dim_class->latency = 1;

  dim_class->deinterlace_frame_yuy2 = deinterlace_frame_di_greedy_packed;
  dim_class->deinterlace_frame_yvyu = deinterlace_frame_di_greedy_packed;

#ifdef BUILD_X86_ASM
  if (cpu_flags & OIL_IMPL_FLAG_MMXEXT) {
    klass->scanline = deinterlace_greedy_scanline_mmxext;
  } else if (cpu_flags & OIL_IMPL_FLAG_MMX) {
    klass->scanline = deinterlace_greedy_scanline_mmx;
  } else {
    klass->scanline = deinterlace_greedy_scanline_c;
  }
#else
  klass->scanline = deinterlace_greedy_scanline_c;
#endif
}

static void
gst_deinterlace_method_greedy_l_init (GstDeinterlaceMethodGreedyL * self)
{
  self->max_comb = 15;
}
