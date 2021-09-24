/*
 *  gstvaapitexturemap.h - VA texture Hash map
 *
 *  Copyright (C) 2016 Intel Corporation
 *  Copyright (C) 2016 Igalia S.L.
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

#ifndef GST_VAAPI_TEXTURE_MAP_H
#define GST_VAAPI_TEXTURE_MAP_H

#include <gst/vaapi/gstvaapitexture.h>

G_BEGIN_DECLS

typedef struct _GstVaapiTextureMap GstVaapiTextureMap;
typedef struct _GstVaapiTextureMapClass GstVaapiTextureMapClass;

#define GST_TYPE_VAAPI_TEXTURE_MAP \
  (gst_vaapi_texture_map_get_type ())
#define GST_VAAPI_TEXTURE_MAP(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VAAPI_TEXTURE_MAP, GstVaapiTextureMap))

GstVaapiTextureMap *
gst_vaapi_texture_map_new (void);

gboolean
gst_vaapi_texture_map_add (GstVaapiTextureMap * map,
                           GstVaapiTexture * texture,
                           guint id);

GstVaapiTexture *
gst_vaapi_texture_map_lookup (GstVaapiTextureMap * map,
                              guint id);

void
gst_vaapi_texture_map_reset (GstVaapiTextureMap * map);

GType
gst_vaapi_texture_map_get_type (void) G_GNUC_CONST;

G_DEFINE_AUTOPTR_CLEANUP_FUNC(GstVaapiTextureMap, gst_object_unref)

G_END_DECLS

#endif /* GST_VAAPI_TEXTURE_MAP_H */
