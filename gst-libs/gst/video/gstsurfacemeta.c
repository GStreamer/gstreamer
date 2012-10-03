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

#include "gstsurfacemeta.h"

/**
 * SECTION:gstsurfacemeta
 * @short_description: Accelerated surface metadata
 *
 * This meta data is used to abstract hardware accelerated buffers and enable
 * generic convertion to standard type such as GL textures. The media type for
 * those buffers is defined by #GST_VIDEO_CAPS_SURFACE. An implementation
 * specific type must be set using the "type" key (e.g. type="vaapi").
 * Available convertion type are speficied using seperate boolean
 * arguement (e.g. opengl=true). Having this information in the capabilities
 * allow easy negotiating of such feature with other elements (e.g. a
 * ClutterGstVideoSink can claim accpeting caps "video/x-surface,opengl=true").
 * <note>
 *   The GstSurfaceMeta interface is unstable API and may change in future.
 *   One can define GST_USE_UNSTABLE_API to acknowledge and avoid this warning.
 * </note>
 */

GType
gst_surface_meta_api_get_type (void)
{
  static volatile GType type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&type)) {
    GType _type = gst_meta_api_type_register ("GstSurfaceMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

const GstMetaInfo *
gst_surface_meta_get_info (void)
{
  static const GstMetaInfo *meta_info = NULL;

  if (g_once_init_enter (&meta_info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_SURFACE_META_API_TYPE, "GstSurfaceMeta",
        sizeof (GstSurfaceMeta),
        (GstMetaInitFunction) NULL,
        (GstMetaFreeFunction) NULL, (GstMetaTransformFunction) NULL);
    g_once_init_leave (&meta_info, meta);
  }
  return meta_info;
}

/**
 * gst_surface_meta_create_converter:
 * @meta: a #GstSurfaceMeta
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
gst_surface_meta_create_converter (GstSurfaceMeta * meta,
    const gchar * type, GValue * dest)
{
  g_return_val_if_fail (meta != NULL, FALSE);

  return meta->create_converter (meta, type, dest);
}
