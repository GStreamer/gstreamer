/*
 *  gstvaapitexture.h - VA texture abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
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

#ifndef GST_VAAPI_TEXTURE_H
#define GST_VAAPI_TEXTURE_H

#include <GL/gl.h>
#include <gst/vaapi/gstvaapitypes.h>
#include <gst/vaapi/gstvaapiobject.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

typedef struct _GstVaapiTexture                 GstVaapiTexture;

GstVaapiTexture *
gst_vaapi_texture_new(
    GstVaapiDisplay *display,
    GLenum           target,
    GLenum           format,
    guint            width,
    guint            height
);

GstVaapiTexture *
gst_vaapi_texture_new_with_texture(
    GstVaapiDisplay *display,
    GLuint           texture_id,
    GLenum           target,
    GLenum           format
);

GstVaapiTexture *
gst_vaapi_texture_ref(GstVaapiTexture *texture);

void
gst_vaapi_texture_unref(GstVaapiTexture *texture);

void
gst_vaapi_texture_replace(GstVaapiTexture **old_texture_ptr,
    GstVaapiTexture *new_texture);

GLuint
gst_vaapi_texture_get_id(GstVaapiTexture *texture);

GLenum
gst_vaapi_texture_get_target(GstVaapiTexture *texture);

GLenum
gst_vaapi_texture_get_format(GstVaapiTexture *texture);

guint
gst_vaapi_texture_get_width(GstVaapiTexture *texture);

guint
gst_vaapi_texture_get_height(GstVaapiTexture *texture);

void
gst_vaapi_texture_get_size(
    GstVaapiTexture *texture,
    guint           *pwidth,
    guint           *pheight
);

gboolean
gst_vaapi_texture_put_surface(
    GstVaapiTexture *texture,
    GstVaapiSurface *surface,
    guint            flags
);

G_END_DECLS

#endif /* GST_VAAPI_TEXTURE_H */
