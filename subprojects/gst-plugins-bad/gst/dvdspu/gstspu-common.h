/* GStreamer DVD Sub-Picture Unit
 * Copyright (C) 2007 Fluendo S.A. <info@fluendo.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */
#ifndef __GSTSPU_COMMON_H__
#define __GSTSPU_COMMON_H__

#include <glib.h>
#include <gst/video/video.h>

G_BEGIN_DECLS

/* FIXME: Move this back to gstdvdspu.h when the renderers no longer use it: */
typedef struct _GstDVDSpu      GstDVDSpu;

typedef struct SpuState SpuState;
typedef struct SpuColour SpuColour;
typedef struct SpuRect SpuRect;

/* Describe the limits of a rectangle */
struct SpuRect {
  gint16 left;
  gint16 top;
  gint16 right;
  gint16 bottom;
};

/* Store a pre-multiplied colour value. The YUV fields hold the YUV values
 * multiplied by the 8-bit alpha, to save computing it while rendering */
struct SpuColour {
  guint16 Y;
  guint16 U;
  guint16 V;
  guint8 A;
};

void gstspu_clear_comp_buffers (SpuState * state);
void gstspu_blend_comp_buffers (SpuState * state, guint8 * planes[3]);


G_END_DECLS

#endif /* __GSTSPU_COMMON_H__ */
