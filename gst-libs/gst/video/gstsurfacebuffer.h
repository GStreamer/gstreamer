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

#ifndef _GST_SURFACE_BUFFER_H_
#define _GST_SURFACE_BUFFER_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstSurfaceBuffer is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/video/gstsurfaceconverter.h>

G_BEGIN_DECLS

#define GST_TYPE_SURFACE_BUFFER                       (gst_surface_buffer_get_type())
#define GST_SURFACE_BUFFER(obj)                       (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SURFACE_BUFFER,GstSurfaceBuffer))
#define GST_SURFACE_BUFFER_CLASS(klass)               (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SURFACE_BUFFER,GstSurfaceBufferClass))
#define GST_SURFACE_BUFFER_GET_CLASS(obj)             (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_SURFACE_BUFFER,GstSurfaceBufferClass))
#define GST_IS_SURFACE_BUFFER(obj)                    (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SURFACE_BUFFER))
#define GST_IS_SURFACE_BUFFER_CLASS(obj)              (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SURFACE_BUFFER))

/**
 * GST_VIDEO_CAPS_SURFACE:
 *
 * Base caps for GstSurfaceBuffer. Implementation specific type must be marked
 * using the type attribute (e.g. type=vaapi). Available convertion shall be
 * specified using boolean attributes (e.g. opengl=true).
 */
#define GST_VIDEO_CAPS_SURFACE "video/x-surface"

typedef struct _GstSurfaceBufferClass GstSurfaceBufferClass;

/**
 * GstSurfaceBuffer:
 * @parent: parent object
 */
struct _GstSurfaceBuffer
{
  GstBuffer parent;

  /*< private >*/
  void *padding[GST_PADDING];
};

/**
 * GstSurfaceBufferClass:
 * @parent_class: parent class type.
 * @create_converter: vmethod to create a converter.
 *
 * #GstVideoContextInterface interface.
 */
struct _GstSurfaceBufferClass
{
  GstBufferClass parent_class;

  GstSurfaceConverter * (*create_converter) (GstSurfaceBuffer *buffer,
                                             const gchar *type,
                                             GValue *dest);

  /*< private >*/
  void *padding[GST_PADDING];
};

GType                       gst_surface_buffer_get_type         (void);

GstSurfaceConverter  *gst_surface_buffer_create_converter (GstSurfaceBuffer *buffer,
                                                           const gchar *type,
                                                           GValue *dest);

G_END_DECLS

#endif
