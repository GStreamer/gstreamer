/* GStreamer
 * Copyright (C) 2020 Igalia, S.L.
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvadisplay_wrapped.h"

struct _GstVaDisplayWrapped
{
  GstVaDisplay parent;
};

#define gst_va_display_wrapped_parent_class parent_class
G_DEFINE_TYPE (GstVaDisplayWrapped, gst_va_display_wrapped,
    GST_TYPE_VA_DISPLAY);

static void
gst_va_display_wrapped_class_init (GstVaDisplayWrappedClass * klass)
{
}

static void
gst_va_display_wrapped_init (GstVaDisplayWrapped * self)
{
}

/**
 * gst_va_display_wrapped_new:
 * @handle: a #VADisplay to wrap
 *
 * Creates a #GstVaDisplay wrapping an already created and initialized
 * VADisplay.
 *
 * Returns: a new #GstVaDisplay if @handle is valid, Otherwise %NULL.
 **/
GstVaDisplay *
gst_va_display_wrapped_new (guintptr handle)
{
  GstVaDisplay *dpy;

  g_return_val_if_fail (handle, NULL);

  dpy = g_object_new (GST_TYPE_VA_DISPLAY_WRAPPED, "va-display", handle, NULL);
  if (!gst_va_display_initialize (dpy)) {
    gst_object_unref (dpy);
    return NULL;
  }

  return gst_object_ref_sink (dpy);
}
