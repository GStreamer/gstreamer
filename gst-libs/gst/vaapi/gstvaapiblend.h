/*
 *  gstvaapiblend.h - Video processing blend
 *
 *  Copyright (C) 2019 Intel Corporation
 *    Author: U. Artie Eoff <ullysses.a.eoff@intel.com>
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

#ifndef GST_VAAPI_BLEND_H
#define GST_VAAPI_BLEND_H

#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPI_BLEND \
  (gst_vaapi_blend_get_type ())
#define GST_VAAPI_BLEND(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_BLEND, GstVaapiBlend))
#define GST_IS_VAAPI_BLEND(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VAAPI_BLEND))

typedef struct _GstVaapiBlend GstVaapiBlend;
typedef struct _GstVaapiBlendSurface GstVaapiBlendSurface;

struct _GstVaapiBlendSurface
{
  GstVaapiSurface const *surface;
  const GstVaapiRectangle *crop;
  GstVaapiRectangle target;
  gdouble alpha;
};

typedef GstVaapiBlendSurface* (*GstVaapiBlendSurfaceNextFunc)(gpointer data);

GstVaapiBlend *
gst_vaapi_blend_new (GstVaapiDisplay * display);

void
gst_vaapi_blend_replace (GstVaapiBlend ** old_blend_ptr,
    GstVaapiBlend * new_blend);

gboolean
gst_vaapi_blend_process (GstVaapiBlend * blend, GstVaapiSurface * output,
    GstVaapiBlendSurfaceNextFunc next, gpointer user_data);

GType
gst_vaapi_blend_get_type (void) G_GNUC_CONST;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiBlend, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_FILTER_H */
