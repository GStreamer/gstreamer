/* GStreamer
 * Copyright (C) 2011 Collabora Ltd.
 * Copyright (C) 2011 Intel
 *
 * Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstsurfacebuffer.h"

/**
 * SECTION:gstsurfacebuffer
 * @short_description: Accelerated surface base class
 *
 * This base class is used to abstract hardware accelerated buffers and enable
 * generic convertion to standard type such as GL textures. The media type for
 * those buffers is defined by #GST_VIDEO_CAPS_SURFACE. An implementation
 * specific type must be set using the "type" key (e.g. type="vaapi").
 * Available convertion type are speficied using seperate boolean
 * arguement (e.g. opengl=true). Having this information in the capabilities
 * allow easy negotiating of such feature with other elements (e.g. a
 * ClutterGstVideoSink can claim accpeting caps "video/x-surface,opengl=true").
 * <note>
 *   The GstVideoContext interface is unstable API and may change in future.
 *   One can define GST_USE_UNSTABLE_API to acknowledge and avoid this warning.
 * </note>
 */

G_DEFINE_TYPE (GstSurfaceBuffer, gst_surface_buffer, GST_TYPE_BUFFER);

static GstSurfaceConverter *
gst_surface_buffer_default_create_converter (GstSurfaceBuffer * surface,
    const gchar * type, GValue * dest)
{
  return NULL;
}

static void
gst_surface_buffer_class_init (GstSurfaceBufferClass * klass)
{
  klass->create_converter = gst_surface_buffer_default_create_converter;
}

static void
gst_surface_buffer_init (GstSurfaceBuffer * surface)
{
  /* Nothing to do */
}

/**
 * gst_surface_buffer_create_converter:
 * @buffer: a #GstSurfaceBuffer
 * @type: the type to convert to
 * @dest: a #GValue containing the destination to upload
 *
 * This method is used to create a type specific converter. The converter will
 * serve as context to accelerate the data convertion. This converter object
 * shall be discarded when the pipeline state changes to NULL and renewed when
 * caps are changed.
 *
 * Returns: newly allocated #GstSurfaceConverter
 */
GstSurfaceConverter *
gst_surface_buffer_create_converter (GstSurfaceBuffer * buffer,
    const gchar * type, GValue * dest)
{
  g_return_val_if_fail (GST_IS_SURFACE_BUFFER (buffer), FALSE);
  return GST_SURFACE_BUFFER_GET_CLASS (buffer)->create_converter (buffer,
      type, dest);
}
