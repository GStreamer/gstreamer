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

#include "vasurfacebuffer.h"
#include "vasurfaceimage.h"

void
gst_va_buffer_surface_unref (gpointer data)
{
  GstVaBufferSurface *buf = data;

  g_return_if_fail (GST_IS_VA_DISPLAY (buf->display));
  g_return_if_fail (buf != NULL);

  if (g_atomic_ref_count_dec (&buf->ref_count)) {
    GST_LOG_OBJECT (buf->display, "Destroying surface %#x", buf->surface);
    va_destroy_surfaces (buf->display, &buf->surface, 1);
    gst_clear_object (&buf->display);
    g_free (buf);
  }
}

/* The refcount starts at zero and the first attach sets it to one.
 * A saturated refcount keeps the surface alive on purpose instead
 * of freeing it too early. */
void
gst_va_buffer_surface_ref (GstVaBufferSurface * buf)
{
  if (g_atomic_ref_count_compare (&buf->ref_count, 0))
    g_atomic_ref_count_init (&buf->ref_count);
  else
    g_atomic_ref_count_inc (&buf->ref_count);
}


GstVaBufferSurface *
gst_va_buffer_surface_new (GstVaDisplay * display, VASurfaceID surface)
{
  g_return_val_if_fail (GST_IS_VA_DISPLAY (display), NULL);
  g_return_val_if_fail (surface != VA_INVALID_SURFACE, NULL);

  GstVaBufferSurface *buf = g_new (GstVaBufferSurface, 1);

  g_atomic_int_set (&buf->ref_count, 0);
  g_atomic_int_set (&buf->ref_mems_count, 0);
  buf->surface = surface;
  buf->display = display ? gst_object_ref (display) : NULL;
  buf->n_mems = 0;

  return buf;
}
