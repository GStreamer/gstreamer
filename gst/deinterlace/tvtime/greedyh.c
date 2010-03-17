/*
 *
 * GStreamer
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
 * Copyright (C) 2008 Sebastian Dröge <slomo@collabora.co.uk>
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
 * Relicensed for GStreamer from GPL to LGPL with permit from Billy Biggs.
 * See: http://bugzilla.gnome.org/show_bug.cgi?id=163578
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "greedyhmacros.h"

#include <stdlib.h>
#include "_stdint.h"
#include <string.h>

#include "gst/gst.h"
#include "plugins.h"
#include "gstdeinterlace.h"

#define GST_TYPE_DEINTERLACE_METHOD_GREEDY_H	(gst_deinterlace_method_greedy_h_get_type ())
#define GST_IS_DEINTERLACE_METHOD_GREEDY_H(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_DEINTERLACE_METHOD_GREEDY_H))
#define GST_IS_DEINTERLACE_METHOD_GREEDY_H_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_DEINTERLACE_METHOD_GREEDY_H))
#define GST_DEINTERLACE_METHOD_GREEDY_H_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_DEINTERLACE_METHOD_GREEDY_H, GstDeinterlaceMethodGreedyHClass))
#define GST_DEINTERLACE_METHOD_GREEDY_H(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_DEINTERLACE_METHOD_GREEDY_H, GstDeinterlaceMethodGreedyH))
#define GST_DEINTERLACE_METHOD_GREEDY_H_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_DEINTERLACE_METHOD_GREEDY_H, GstDeinterlaceMethodGreedyHClass))
#define GST_DEINTERLACE_METHOD_GREEDY_H_CAST(obj)	((GstDeinterlaceMethodGreedyH*)(obj))

GType gst_deinterlace_method_greedy_h_get_type (void);

typedef struct
{
  GstDeinterlaceMethod parent;

  guint max_comb, motion_threshold, motion_sense;
} GstDeinterlaceMethodGreedyH;

typedef struct
{
  GstDeinterlaceMethodClass parent_class;
  void (*scanline) (GstDeinterlaceMethodGreedyH * self, uint8_t * L2,
      uint8_t * L1, uint8_t * L3, uint8_t * L2P, uint8_t * Dest, int size);
} GstDeinterlaceMethodGreedyHClass;

static void
greedyDScaler_C (GstDeinterlaceMethodGreedyH * self, uint8_t * L1, uint8_t * L2,
    uint8_t * L3, uint8_t * L2P, uint8_t * Dest, int size)
{
  int Pos;
  uint8_t l1_l, l1_1_l, l3_l, l3_1_l;
  uint8_t l1_c, l1_1_c, l3_c, l3_1_c;
  uint8_t avg_l, avg_c, avg_l_1, avg_c_1;
  uint8_t avg_l__1 = 0, avg_c__1 = 0;
  uint8_t avg_s_l, avg_s_c;
  uint8_t avg_sc_l, avg_sc_c;
  uint8_t best_l, best_c;
  uint16_t mov_l;
  uint8_t out_l, out_c;
  uint8_t l2_l, l2_c, lp2_l, lp2_c;
  uint8_t l2_l_diff, l2_c_diff, lp2_l_diff, lp2_c_diff;
  uint8_t min_l, min_c, max_l, max_c;
  guint max_comb = self->max_comb;
  guint motion_sense = self->motion_sense;
  guint motion_threshold = self->motion_threshold;

  for (Pos = 0; Pos < size; Pos += 2) {
    l1_l = L1[0];
    l1_c = L1[1];
    l3_l = L3[0];
    l3_c = L3[1];

    if (Pos == size - 1) {
      l1_1_l = l1_l;
      l1_1_c = l1_c;
      l3_1_l = l3_l;
      l3_1_c = l3_c;
    } else {
      l1_1_l = L1[2];
      l1_1_c = L1[3];
      l3_1_l = L3[2];
      l3_1_c = L3[3];
    }

    /* Average of L1 and L3 */
    avg_l = (l1_l + l3_l) / 2;
    avg_c = (l1_c + l3_c) / 2;

    if (Pos == 0) {
      avg_l__1 = avg_l;
      avg_c__1 = avg_c;
    }

    /* Average of next L1 and next L3 */
    avg_l_1 = (l1_1_l + l3_1_l) / 2;
    avg_c_1 = (l1_1_c + l3_1_c) / 2;

    /* Calculate average of one pixel forward and previous */
    avg_s_l = (avg_l__1 + avg_l_1) / 2;
    avg_s_c = (avg_c__1 + avg_c_1) / 2;

    /* Calculate average of center and surrounding pixels */
    avg_sc_l = (avg_l + avg_s_l) / 2;
    avg_sc_c = (avg_c + avg_s_c) / 2;

    /* move forward */
    avg_l__1 = avg_l;
    avg_c__1 = avg_c;

    /* Get best L2/L2P, i.e. least diff from above average */
    l2_l = L2[0];
    l2_c = L2[1];
    lp2_l = L2P[0];
    lp2_c = L2P[1];

    l2_l_diff = ABS (l2_l - avg_sc_l);
    l2_c_diff = ABS (l2_c - avg_sc_c);

    lp2_l_diff = ABS (lp2_l - avg_sc_l);
    lp2_c_diff = ABS (lp2_c - avg_sc_c);

    if (l2_l_diff > lp2_l_diff)
      best_l = lp2_l;
    else
      best_l = l2_l;

    if (l2_c_diff > lp2_c_diff)
      best_c = lp2_c;
    else
      best_c = l2_c;

    /* Clip this best L2/L2P by L1/L3 and allow to differ by GreedyMaxComb */
    max_l = MAX (l1_l, l3_l);
    min_l = MIN (l1_l, l3_l);

    if (max_l < 256 - max_comb)
      max_l += max_comb;
    else
      max_l = 255;

    if (min_l > max_comb)
      min_l -= max_comb;
    else
      min_l = 0;

    max_c = MAX (l1_c, l3_c);
    min_c = MIN (l1_c, l3_c);

    if (max_c < 256 - max_comb)
      max_c += max_comb;
    else
      max_c = 255;

    if (min_c > max_comb)
      min_c -= max_comb;
    else
      min_c = 0;

    out_l = CLAMP (best_l, min_l, max_l);
    out_c = CLAMP (best_c, min_c, max_c);

    /* Do motion compensation for luma, i.e. how much
     * the weave pixel differs */
    mov_l = ABS (l2_l - lp2_l);
    if (mov_l > motion_threshold)
      mov_l -= motion_threshold;
    else
      mov_l = 0;

    mov_l = mov_l * motion_sense;
    if (mov_l > 256)
      mov_l = 256;

    /* Weighted sum on clipped weave pixel and average */
    out_l = (out_l * (256 - mov_l) + avg_sc_l * mov_l) / 256;

    Dest[0] = out_l;
    Dest[1] = out_c;

    Dest += 2;
    L1 += 2;
    L2 += 2;
    L3 += 2;
    L2P += 2;
  }
}

#ifdef BUILD_X86_ASM

#define IS_MMXEXT
#define SIMD_TYPE MMXEXT
#define FUNCT_NAME greedyDScaler_MMXEXT
#include "greedyh.asm"
#undef SIMD_TYPE
#undef IS_MMXEXT
#undef FUNCT_NAME

#define IS_3DNOW
#define SIMD_TYPE 3DNOW
#define FUNCT_NAME greedyDScaler_3DNOW
#include "greedyh.asm"
#undef SIMD_TYPE
#undef IS_3DNOW
#undef FUNCT_NAME

#define IS_MMX
#define SIMD_TYPE MMX
#define FUNCT_NAME greedyDScaler_MMX
#include "greedyh.asm"
#undef SIMD_TYPE
#undef IS_MMX
#undef FUNCT_NAME

#endif

static void
deinterlace_frame_di_greedyh (GstDeinterlaceMethod * d_method,
    GstDeinterlace * object, GstBuffer * outbuf)
{
  GstDeinterlaceMethodGreedyH *self =
      GST_DEINTERLACE_METHOD_GREEDY_H (d_method);
  GstDeinterlaceMethodGreedyHClass *klass =
      GST_DEINTERLACE_METHOD_GREEDY_H_GET_CLASS (self);
  int InfoIsOdd = 0;
  int Line;
  unsigned int Pitch = object->field_stride;

  unsigned char *L1;            // ptr to Line1, of 3
  unsigned char *L2;            // ptr to Line2, the weave line
  unsigned char *L3;            // ptr to Line3

  unsigned char *L2P;           // ptr to prev Line2
  unsigned char *Dest = GST_BUFFER_DATA (outbuf);

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
    oil_memcpy (Dest, L1, object->row_stride);
    Dest += object->row_stride;
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
    oil_memcpy (Dest,
        GST_BUFFER_DATA (object->field_history[object->history_count - 2].buf),
        object->row_stride);
    Dest += object->row_stride;
    // then first odd line
    oil_memcpy (Dest, L1, object->row_stride);
    Dest += object->row_stride;
  }

  for (Line = 0; Line < (object->field_height - 1); ++Line) {
    klass->scanline (self, L1, L2, L3, L2P, Dest, object->row_stride);
    Dest += object->row_stride;
    oil_memcpy (Dest, L3, object->row_stride);
    Dest += object->row_stride;

    L1 += Pitch;
    L2 += Pitch;
    L3 += Pitch;
    L2P += Pitch;
  }

  if (InfoIsOdd) {
    oil_memcpy (Dest, L2, object->row_stride);
  }
}

G_DEFINE_TYPE (GstDeinterlaceMethodGreedyH, gst_deinterlace_method_greedy_h,
    GST_TYPE_DEINTERLACE_METHOD);

enum
{
  ARG_0,
  ARG_MAX_COMB,
  ARG_MOTION_THRESHOLD,
  ARG_MOTION_SENSE
};

static void
gst_deinterlace_method_greedy_h_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDeinterlaceMethodGreedyH *self = GST_DEINTERLACE_METHOD_GREEDY_H (object);

  switch (prop_id) {
    case ARG_MAX_COMB:
      self->max_comb = g_value_get_uint (value);
      break;
    case ARG_MOTION_THRESHOLD:
      self->motion_threshold = g_value_get_uint (value);
      break;
    case ARG_MOTION_SENSE:
      self->motion_sense = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_deinterlace_method_greedy_h_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstDeinterlaceMethodGreedyH *self = GST_DEINTERLACE_METHOD_GREEDY_H (object);

  switch (prop_id) {
    case ARG_MAX_COMB:
      g_value_set_uint (value, self->max_comb);
      break;
    case ARG_MOTION_THRESHOLD:
      g_value_set_uint (value, self->motion_threshold);
      break;
    case ARG_MOTION_SENSE:
      g_value_set_uint (value, self->motion_sense);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
gst_deinterlace_method_greedy_h_class_init (GstDeinterlaceMethodGreedyHClass *
    klass)
{
  GstDeinterlaceMethodClass *dim_class = (GstDeinterlaceMethodClass *) klass;
  GObjectClass *gobject_class = (GObjectClass *) klass;
#ifdef BUILD_X86_ASM
  guint cpu_flags = oil_cpu_get_flags ();
#endif

  gobject_class->set_property = gst_deinterlace_method_greedy_h_set_property;
  gobject_class->get_property = gst_deinterlace_method_greedy_h_get_property;

  g_object_class_install_property (gobject_class, ARG_MAX_COMB,
      g_param_spec_uint ("max-comb",
          "Max comb",
          "Max Comb", 0, 255, 5, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, ARG_MOTION_THRESHOLD,
      g_param_spec_uint ("motion-threshold",
          "Motion Threshold",
          "Motion Threshold",
          0, 255, 25, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  g_object_class_install_property (gobject_class, ARG_MOTION_SENSE,
      g_param_spec_uint ("motion-sense",
          "Motion Sense",
          "Motion Sense",
          0, 255, 30, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );

  dim_class->fields_required = 4;
  dim_class->deinterlace_frame = deinterlace_frame_di_greedyh;
  dim_class->name = "Motion Adaptive: Advanced Detection";
  dim_class->nick = "greedyh";
  dim_class->latency = 1;

#ifdef BUILD_X86_ASM
  if (cpu_flags & OIL_IMPL_FLAG_MMXEXT) {
    klass->scanline = greedyDScaler_MMXEXT;
  } else if (cpu_flags & OIL_IMPL_FLAG_3DNOW) {
    klass->scanline = greedyDScaler_3DNOW;
  } else if (cpu_flags & OIL_IMPL_FLAG_MMX) {
    klass->scanline = greedyDScaler_MMX;
  } else {
    klass->scanline = greedyDScaler_C;
  }
#else
  klass->scanline = greedyDScaler_C;
#endif
}

static void
gst_deinterlace_method_greedy_h_init (GstDeinterlaceMethodGreedyH * self)
{
  self->max_comb = 5;
  self->motion_threshold = 25;
  self->motion_sense = 30;
}
