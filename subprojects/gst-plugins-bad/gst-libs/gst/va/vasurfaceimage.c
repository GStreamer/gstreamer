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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "vasurfaceimage.h"
#include "gstvavideoformat.h"
#include <va/va.h>

/* XXX: find a better log category */
#define GST_CAT_DEFAULT gst_va_display_debug
GST_DEBUG_CATEGORY_EXTERN (gst_va_display_debug);

gboolean
va_destroy_surfaces (GstVaDisplay * display, VASurfaceID * surfaces,
    gint num_surfaces)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  g_return_val_if_fail (num_surfaces > 0, FALSE);

  status = vaDestroySurfaces (dpy, surfaces, num_surfaces);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaDestroySurfaces: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
va_create_surfaces (GstVaDisplay * display, guint rt_format, guint fourcc,
    guint width, guint height, gint usage_hint, guint64 * modifiers,
    guint num_modifiers, VASurfaceAttribExternalBuffers * ext_buf,
    VASurfaceID * surfaces, guint num_surfaces)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  /* *INDENT-OFF* */
  VASurfaceAttrib attrs[6] = {
    {
      .type = VASurfaceAttribUsageHint,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypeInteger,
      .value.value.i = usage_hint,
    },
    {
      .type = VASurfaceAttribMemoryType,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypeInteger,
      .value.value.i = (ext_buf && ext_buf->num_buffers > 0)
                               ? VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME
                               : VA_SURFACE_ATTRIB_MEM_TYPE_VA,
    },
  };
  VADRMFormatModifierList modifier_list = {
    .num_modifiers = num_modifiers,
    .modifiers = modifiers,
  };
  /* *INDENT-ON* */
  VAStatus status;
  guint num_attrs = 2;

  g_return_val_if_fail (num_surfaces > 0, FALSE);

  if (fourcc > 0) {
    /* *INDENT-OFF* */
    attrs[num_attrs++] = (VASurfaceAttrib) {
      .type = VASurfaceAttribPixelFormat,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypeInteger,
      .value.value.i = fourcc,
    };
    /* *INDENT-ON* */
  }

  if (ext_buf) {
    /* *INDENT-OFF* */
    attrs[num_attrs++] = (VASurfaceAttrib) {
      .type = VASurfaceAttribExternalBufferDescriptor,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypePointer,
      .value.value.p = ext_buf,
    };
    /* *INDENT-ON* */
  }

  if (num_modifiers > 0 && modifiers) {
    /* *INDENT-OFF* */
    attrs[num_attrs++] = (VASurfaceAttrib) {
      .type = VASurfaceAttribDRMFormatModifiers,
      .flags = VA_SURFACE_ATTRIB_SETTABLE,
      .value.type = VAGenericValueTypePointer,
      .value.value.p = &modifier_list,
    };
    /* *INDENT-ON* */
  }

  status = vaCreateSurfaces (dpy, rt_format, width, height, surfaces,
      num_surfaces, attrs, num_attrs);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaCreateSurfaces: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
va_export_surface_to_dmabuf (GstVaDisplay * display, VASurfaceID surface,
    guint32 flags, VADRMPRIMESurfaceDescriptor * desc)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  status = vaExportSurfaceHandle (dpy, surface,
      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, flags, desc);
  if (status != VA_STATUS_SUCCESS) {
    GST_INFO ("vaExportSurfaceHandle: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
va_destroy_image (GstVaDisplay * display, VAImageID image_id)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  status = vaDestroyImage (dpy, image_id);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaDestroyImage: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

gboolean
va_get_derive_image (GstVaDisplay * display, VASurfaceID surface,
    VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  status = vaDeriveImage (dpy, surface, image);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaDeriveImage: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
va_create_image (GstVaDisplay * display, GstVideoFormat format, gint width,
    gint height, VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  const VAImageFormat *va_format;
  VAStatus status;

  va_format = gst_va_image_format_from_video_format (format);
  if (!va_format)
    return FALSE;

  status =
      vaCreateImage (dpy, (VAImageFormat *) va_format, width, height, image);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaCreateImage: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

gboolean
va_get_image (GstVaDisplay * display, VASurfaceID surface, VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  status = vaGetImage (dpy, surface, 0, 0, image->width, image->height,
      image->image_id);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaGetImage: %s", vaErrorStr (status));
    return FALSE;
  }

  return TRUE;
}

gboolean
va_sync_surface (GstVaDisplay * display, VASurfaceID surface)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  status = vaSyncSurface (dpy, surface);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaSyncSurface: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

gboolean
va_map_buffer (GstVaDisplay * display, VABufferID buffer, gpointer * data)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  status = vaMapBuffer (dpy, buffer, data);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaMapBuffer: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

gboolean
va_unmap_buffer (GstVaDisplay * display, VABufferID buffer)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  status = vaUnmapBuffer (dpy, buffer);
  if (status != VA_STATUS_SUCCESS) {
    GST_WARNING ("vaUnmapBuffer: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

gboolean
va_put_image (GstVaDisplay * display, VASurfaceID surface, VAImage * image)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;

  if (!va_sync_surface (display, surface))
    return FALSE;

  status = vaPutImage (dpy, surface, image->image_id, 0, 0, image->width,
      image->height, 0, 0, image->width, image->height);
  if (status != VA_STATUS_SUCCESS) {
    GST_ERROR ("vaPutImage: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}

gboolean
va_ensure_image (GstVaDisplay * display, VASurfaceID surface,
    GstVideoInfo * info, VAImage * image, gboolean derived)
{
  gboolean ret = TRUE;

  if (image->image_id != VA_INVALID_ID)
    return TRUE;

  if (!va_sync_surface (display, surface))
    return FALSE;

  if (derived) {
    ret = va_get_derive_image (display, surface, image);
  } else {
    ret = va_create_image (display, GST_VIDEO_INFO_FORMAT (info),
        GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info), image);
  }

  return ret;
}

gboolean
va_check_surface (GstVaDisplay * display, VASurfaceID surface)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  VAStatus status;
  VASurfaceStatus state;

  status = vaQuerySurfaceStatus (dpy, surface, &state);

  if (status != VA_STATUS_SUCCESS)
    GST_ERROR ("vaQuerySurfaceStatus: %s", vaErrorStr (status));

  GST_LOG ("surface %#x status %d", surface, state);

  return (status == VA_STATUS_SUCCESS);
}

gboolean
va_copy_surface (GstVaDisplay * display, VASurfaceID dst, VASurfaceID src)
{
  VADisplay dpy = gst_va_display_get_va_dpy (display);
  /* *INDENT-OFF* */
  VACopyObject obj_src = {
    .obj_type = VACopyObjectSurface,
    .object = {
      .surface_id = src,
    },
  };
  VACopyObject obj_dst = {
    .obj_type = VACopyObjectSurface,
    .object = {
      .surface_id = dst,
    },
  };
  VACopyOption option = {
    .bits = {
      .va_copy_sync = VA_EXEC_SYNC,
      .va_copy_mode = VA_EXEC_MODE_DEFAULT,
    },
  };
  /* *INDENT-ON* */
  VAStatus status;

  status = vaCopy (dpy, &obj_dst, &obj_src, option);
  if (status != VA_STATUS_SUCCESS) {
    GST_INFO ("vaCopy: %s", vaErrorStr (status));
    return FALSE;
  }
  return TRUE;
}
