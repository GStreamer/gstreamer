/*
 *  gstvaapivideometa_texture.h - GStreamer/VA video meta (GLTextureUpload)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *  Copyright (C) 2013 Igalia
 *    Author: Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
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

#ifndef GST_VAAPI_VIDEO_META_TEXTURE_H
#define GST_VAAPI_VIDEO_META_TEXTURE_H

#include <gst/vaapi/gstvaapitexture.h>

G_BEGIN_DECLS

typedef struct _GstVaapiVideoMetaTexture GstVaapiVideoMetaTexture;

G_GNUC_INTERNAL
GstMeta *
gst_buffer_add_texture_upload_meta (GstBuffer * buffer);

G_GNUC_INTERNAL
gboolean
gst_buffer_ensure_texture_upload_meta (GstBuffer * buffer);

G_END_DECLS

#endif /* GST_VAAPI_VIDEO_META_TEXTURE_H */
