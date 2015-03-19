/*
 *  gstvaapivideobuffer.c - Gstreamer/VA video buffer
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

/**
 * SECTION:gstvaapivideobuffer
 * @short_description: VA video buffer for GStreamer
 *
 * This functions creates and decorates a #GstBuffer that is going to
 * be used by VA base gstreamer elements.
 */

#include "gstcompat.h"
#include "gstvaapivideobuffer.h"

static GstBuffer *
new_vbuffer (GstVaapiVideoMeta * meta)
{
  GstBuffer *buffer;

  g_return_val_if_fail (meta != NULL, NULL);

  buffer = gst_buffer_new ();
  if (buffer)
    gst_buffer_set_vaapi_video_meta (buffer, meta);
  gst_vaapi_video_meta_unref (meta);
  return buffer;
}

GstBuffer *
gst_vaapi_video_buffer_new (GstVaapiVideoMeta * meta)
{
  g_return_val_if_fail (meta != NULL, NULL);

  return new_vbuffer (gst_vaapi_video_meta_ref (meta));
}

GstBuffer *
gst_vaapi_video_buffer_new_empty (void)
{
  return gst_buffer_new ();
}

GstBuffer *
gst_vaapi_video_buffer_new_from_pool (GstVaapiVideoPool * pool)
{
  return new_vbuffer (gst_vaapi_video_meta_new_from_pool (pool));
}

GstBuffer *
gst_vaapi_video_buffer_new_from_buffer (GstBuffer * buffer)
{
  GstVaapiVideoMeta *const meta = gst_buffer_get_vaapi_video_meta (buffer);

  return meta ? new_vbuffer (gst_vaapi_video_meta_ref (meta)) : NULL;
}

GstBuffer *
gst_vaapi_video_buffer_new_with_image (GstVaapiImage * image)
{
  return new_vbuffer (gst_vaapi_video_meta_new_with_image (image));
}

GstBuffer *
gst_vaapi_video_buffer_new_with_surface_proxy (GstVaapiSurfaceProxy * proxy)
{
  return new_vbuffer (gst_vaapi_video_meta_new_with_surface_proxy (proxy));
}
