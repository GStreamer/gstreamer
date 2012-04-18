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

#ifndef _GST_SURFACE_META_H_
#define _GST_SURFACE_META_H_

#ifndef GST_USE_UNSTABLE_API
#warning "GstSurfaceMeta is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/video/gstsurfaceconverter.h>

G_BEGIN_DECLS

typedef struct _GstSurfaceMeta GstSurfaceMeta;

/**
 * GstSurfaceMeta:
 * @create_converter: vmethod to create a converter.
 *
 */
struct _GstSurfaceMeta {
  GstMeta       meta;

  GstSurfaceConverter * (*create_converter) (GstSurfaceMeta *meta,
                                             const gchar *type,
                                             GValue *dest);
};

GType gst_surface_meta_api_get_type (void);
#define GST_SURFACE_META_API_TYPE (gst_surface_meta_api_get_type())

const GstMetaInfo *gst_surface_meta_get_info (void);
#define GST_SURFACE_META_INFO (gst_surface_meta_get_info())

#define gst_buffer_get_surface_meta(b) \
  ((GstSurfaceMeta*)gst_buffer_get_meta((b),GST_SURFACE_META_API_TYPE))
#define gst_buffer_add_surface_meta(b) \
  ((GstSurfaceMeta*)gst_buffer_add_meta((b),GST_SURFACE_META_INFO,NULL))

GstSurfaceConverter  *gst_surface_meta_create_converter (GstSurfaceMeta *meta,
                                                           const gchar *type,
                                                           GValue *dest);

G_END_DECLS

#endif
