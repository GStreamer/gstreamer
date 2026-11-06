/* GStreamer
 * Copyright (C) 2026 Igalia, S.L.
 *     Author: Víctor Jáquez <vjaquez@igalia.com>
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

#pragma once

#include <gst/gst.h>
#include <gst/video/video.h>
#include <va/va.h>

#include <gst/va/gstvadisplay.h>

G_BEGIN_DECLS

typedef struct _GstVaBufferSurface GstVaBufferSurface;
struct _GstVaBufferSurface
{
  GstVaDisplay *display;
  VASurfaceID surface;
  guint n_mems;
  GstMemory *mems[GST_VIDEO_MAX_PLANES];
  gatomicrefcount ref_count;
  gint ref_mems_count;
};

G_GNUC_INTERNAL
void                  gst_va_buffer_surface_unref         (gpointer data);

G_GNUC_INTERNAL
GstVaBufferSurface *  gst_va_buffer_surface_new           (GstVaDisplay * display,
                                                           VASurfaceID surface);

static inline
void gst_va_buffer_surface_ref (GstVaBufferSurface * buf) {
  g_atomic_ref_count_inc (&buf->ref_count);
}

G_END_DECLS
