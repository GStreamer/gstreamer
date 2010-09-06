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
#include "tvtime.h"


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

typedef void (*GreedyLScanlineFunction) (GstDeinterlaceMethodGreedyL * self,
    const guint8 * L2, const guint8 * L1, const guint8 * L3, const guint8 * L2P,
    guint8 * Dest, gint width);

typedef struct
{
  GstDeinterlaceMethodClass parent_class;
  GreedyLScanlineFunction scanline;
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
deinterlace_greedy_scanline_orc (GstDeinterlaceMethodGreedyL * self,
    const guint8 * m0, const guint8 * t1,
    const guint8 * b1, const guint8 * m2, guint8 * output, gint width)
{
  deinterlace_line_greedy (output, m0, t1, b1, m2, self->max_comb, width);
}

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
    memcpy (Dest, L1, RowStride);
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
    memcpy (Dest, L1, RowStride);
    Dest += RowStride;
    // then first odd line
    memcpy (Dest, L1, RowStride);
    Dest += RowStride;
  }

  for (Line = 0; Line < (FieldHeight - 1); ++Line) {
    klass->scanline (self, L2, L1, L3, L2P, Dest, RowStride);
    Dest += RowStride;
    memcpy (Dest, L3, RowStride);
    Dest += RowStride;

    L1 += Pitch;
    L2 += Pitch;
    L3 += Pitch;
    L2P += Pitch;
  }

  if (InfoIsOdd) {
    memcpy (Dest, L2, RowStride);
  }
}

static void
deinterlace_frame_di_greedy_planar_plane (GstDeinterlaceMethodGreedyL * self,
    const guint8 * L1, const guint8 * L2, const guint8 * L3, const guint8 * L2P,
    guint8 * Dest, gint RowStride, gint FieldHeight, gint Pitch, gint InfoIsOdd,
    GreedyLScanlineFunction scanline)
{
  gint Line;

  // copy first even line no matter what, and the first odd line if we're
  // processing an EVEN field. (note diff from other deint rtns.)

  if (InfoIsOdd) {
    // copy first even line
    memcpy (Dest, L1, RowStride);
    Dest += RowStride;
  } else {
    // copy first even line
    memcpy (Dest, L1, RowStride);
    Dest += RowStride;
    // then first odd line
    memcpy (Dest, L1, RowStride);
    Dest += RowStride;
  }

  for (Line = 0; Line < (FieldHeight - 1); ++Line) {
    scanline (self, L2, L1, L3, L2P, Dest, RowStride);
    Dest += RowStride;
    memcpy (Dest, L3, RowStride);
    Dest += RowStride;

    L1 += Pitch;
    L2 += Pitch;
    L3 += Pitch;
    L2P += Pitch;
  }

  if (InfoIsOdd) {
    memcpy (Dest, L2, RowStride);
  }
}

static void
deinterlace_frame_di_greedy_planar (GstDeinterlaceMethod * method,
    const GstDeinterlaceField * history, guint history_count,
    GstBuffer * outbuf)
{
  GstDeinterlaceMethodGreedyL *self = GST_DEINTERLACE_METHOD_GREEDY_L (method);
  GstDeinterlaceMethodGreedyLClass *klass =
      GST_DEINTERLACE_METHOD_GREEDY_L_GET_CLASS (self);
  gint InfoIsOdd;
  gint RowStride;
  gint FieldHeight;
  gint Pitch;
  const guint8 *L1;             // ptr to Line1, of 3
  const guint8 *L2;             // ptr to Line2, the weave line
  const guint8 *L3;             // ptr to Line3
  const guint8 *L2P;            // ptr to prev Line2
  guint8 *Dest;
  gint i;
  gint Offset;
  GreedyLScanlineFunction scanline = klass->scanline;

  for (i = 0; i < 3; i++) {
    Offset = method->offset[i];

    InfoIsOdd = (history[history_count - 1].flags == PICTURE_INTERLACED_BOTTOM);
    RowStride = method->row_stride[i];
    FieldHeight = method->height[i] / 2;
    Pitch = method->row_stride[i] * 2;

    Dest = GST_BUFFER_DATA (outbuf) + Offset;

    L1 = GST_BUFFER_DATA (history[history_count - 2].buf) + Offset;
    if (history[history_count - 2].flags & PICTURE_INTERLACED_BOTTOM)
      L1 += RowStride;

    L2 = GST_BUFFER_DATA (history[history_count - 1].buf) + Offset;
    if (history[history_count - 1].flags & PICTURE_INTERLACED_BOTTOM)
      L2 += RowStride;

    L3 = L1 + Pitch;
    L2P = GST_BUFFER_DATA (history[history_count - 3].buf) + Offset;
    if (history[history_count - 3].flags & PICTURE_INTERLACED_BOTTOM)
      L2P += RowStride;

    deinterlace_frame_di_greedy_planar_plane (self, L1, L2, L3, L2P, Dest,
        RowStride, FieldHeight, Pitch, InfoIsOdd, scanline);
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
  dim_class->deinterlace_frame_uyvy = deinterlace_frame_di_greedy_packed;
  dim_class->deinterlace_frame_y444 = deinterlace_frame_di_greedy_planar;
  dim_class->deinterlace_frame_y42b = deinterlace_frame_di_greedy_planar;
  dim_class->deinterlace_frame_i420 = deinterlace_frame_di_greedy_planar;
  dim_class->deinterlace_frame_yv12 = deinterlace_frame_di_greedy_planar;
  dim_class->deinterlace_frame_y41b = deinterlace_frame_di_greedy_planar;
  dim_class->deinterlace_frame_ayuv = deinterlace_frame_di_greedy_planar;
  dim_class->deinterlace_frame_argb = deinterlace_frame_di_greedy_packed;
  dim_class->deinterlace_frame_rgba = deinterlace_frame_di_greedy_packed;
  dim_class->deinterlace_frame_abgr = deinterlace_frame_di_greedy_packed;
  dim_class->deinterlace_frame_bgra = deinterlace_frame_di_greedy_packed;
  dim_class->deinterlace_frame_rgb = deinterlace_frame_di_greedy_packed;
  dim_class->deinterlace_frame_bgr = deinterlace_frame_di_greedy_packed;

  klass->scanline = deinterlace_greedy_scanline_orc;
}

static void
gst_deinterlace_method_greedy_l_init (GstDeinterlaceMethodGreedyL * self)
{
  self->max_comb = 15;
}
