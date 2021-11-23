/* GStreamer
 * Copyright (C) 2021 Igalia, S.L.
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

#include "gstvasurfacecopy.h"

#include "gstvaallocator.h"
#include "gstvadisplay_priv.h"
#include "gstvafilter.h"
#include "vasurfaceimage.h"

#define GST_CAT_DEFAULT gst_va_memory_debug
GST_DEBUG_CATEGORY_EXTERN (gst_va_memory_debug);

struct _GstVaSurfaceCopy
{
  GstVaDisplay *display;

  GstVideoInfo info;
  gboolean has_copy;

  GRecMutex lock;
  GstVaFilter *filter;
};

static gboolean
_has_copy (GstVaDisplay * display)
{
#if VA_CHECK_VERSION (1, 12, 0)
  VADisplay dpy;
  VADisplayAttribute attr = {
    .type = VADisplayAttribCopy,
    .flags = VA_DISPLAY_ATTRIB_GETTABLE,
  };
  VAStatus status;

  dpy = gst_va_display_get_va_dpy (display);

  gst_va_display_lock (display);
  status = vaGetDisplayAttributes (dpy, &attr, 1);
  gst_va_display_unlock (display);
  if (status != VA_STATUS_SUCCESS) {
    GST_INFO ("vaGetDisplayAttribures: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
#else
  return FALSE;
#endif
}

GstVaSurfaceCopy *
gst_va_surface_copy_new (GstVaDisplay * display, GstVideoInfo * vinfo)
{
  GstVaSurfaceCopy *self;

  g_return_val_if_fail (GST_IS_VA_DISPLAY (display), NULL);
  g_return_val_if_fail (vinfo != NULL, NULL);

  self = g_slice_new (GstVaSurfaceCopy);
  self->display = gst_object_ref (display);
  self->has_copy = _has_copy (display);
  self->info = *vinfo;
  self->filter = NULL;
  g_rec_mutex_init (&self->lock);

  if (gst_va_display_has_vpp (display)) {
    self->filter = gst_va_filter_new (display);
    if (!(gst_va_filter_open (self->filter)
            && gst_va_filter_set_video_info (self->filter, vinfo, vinfo)))
      gst_clear_object (&self->filter);
  }

  return self;
}

void
gst_va_surface_copy_free (GstVaSurfaceCopy * self)
{
  g_return_if_fail (self && GST_IS_VA_DISPLAY (self->display));

  gst_clear_object (&self->display);
  if (self->filter) {
    gst_va_filter_close (self->filter);
    gst_clear_object (&self->filter);
  }

  g_rec_mutex_clear (&self->lock);

  g_slice_free (GstVaSurfaceCopy, self);
}

static gboolean
_vpp_copy_surface (GstVaSurfaceCopy * self, VASurfaceID dst, VASurfaceID src)
{
  gboolean ret;

  GstVaSample gst_src = {
    .surface = src,
  };
  GstVaSample gst_dst = {
    .surface = dst,
  };

  g_rec_mutex_lock (&self->lock);
  ret = gst_va_filter_process (self->filter, &gst_src, &gst_dst);
  g_rec_mutex_unlock (&self->lock);

  return ret;
}

gboolean
gst_va_surface_copy (GstVaSurfaceCopy * self, VASurfaceID dst, VASurfaceID src)
{
  VAImage image = {.image_id = VA_INVALID_ID, };
  gboolean ret;

  g_return_val_if_fail (self && GST_IS_VA_DISPLAY (self->display), FALSE);

  if (self->has_copy && va_copy_surface (self->display, dst, src)) {
    GST_LOG ("GPU copy of %#x to %#x", src, dst);
    return TRUE;
  }

  if (self->filter && _vpp_copy_surface (self, dst, src)) {
    GST_LOG ("VPP copy of %#x to %#x", src, dst);
    return TRUE;
  }

  if (!va_ensure_image (self->display, src, &self->info, &image, FALSE))
    return FALSE;

  if ((ret = va_put_image (self->display, dst, &image)))
    GST_LOG ("shallow copy of %#x to %#x", src, dst);

  va_unmap_buffer (self->display, image.buf);
  va_destroy_image (self->display, image.image_id);

  return ret;
}
