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

#ifndef _GST_SURFACE_CONVERTER_H_
#define _GST_SURFACE_CONVERTER_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstSurfaceConverter is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>

G_BEGIN_DECLS

/* Forward declaration from gstsurfacebuffer.h */
typedef struct _GstSurfaceBuffer GstSurfaceBuffer;

#define GST_TYPE_SURFACE_CONVERTER             (gst_surface_converter_get_type ())
#define GST_SURFACE_CONVERTER(obj)             (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SURFACE_CONVERTER, GstSurfaceConverter))
#define GST_IS_SURFACE_CONVERTER(obj)          (GST_IMPLEMENTS_INTERFACE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SURFACE_CONVERTER))
#define GST_SURFACE_CONVERTER_GET_IFACE(inst)  (G_TYPE_INSTANCE_GET_INTERFACE ((inst), GST_TYPE_SURFACE_CONVERTER, GstSurfaceConverterInterface))

typedef struct _GstSurfaceConverter GstSurfaceConverter;
typedef struct _GstSurfaceConverterInterface GstSurfaceConverterInterface;

/**
 * GstSurfaceConverterInterface:
 * @parent: parent interface type.
 * @upload: vmethod to upload #GstSurfaceBuffer.
 *
 * #GstSurfaceConverterInterface interface.
 */
struct _GstSurfaceConverterInterface
{
  GTypeInterface parent;

  gboolean (*upload) (GstSurfaceConverter *converter,
                      GstSurfaceBuffer *buffer);
};

GType     gst_surface_converter_get_type (void);

gboolean  gst_surface_converter_upload (GstSurfaceConverter *converter,
                                        GstSurfaceBuffer *buffer);

G_END_DECLS

#endif
