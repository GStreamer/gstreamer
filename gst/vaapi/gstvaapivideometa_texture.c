/*
 *  gstvaapivideometa_texture.c - GStreamer/VA video meta (GLTextureUpload)
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

#include "gst/vaapi/sysdeps.h"
#include "gstvaapivideometa.h"
#include "gstvaapivideometa_texture.h"
#include "gstvaapipluginutil.h"

#if GST_CHECK_VERSION(1,1,0) && USE_GLX
static void
gst_vaapi_texure_upload_free(gpointer data)
{
    GstVaapiTexture * const texture = data;

    if (texture)
        gst_vaapi_texture_unref(texture);
}

static gboolean
gst_vaapi_texture_upload(GstVideoGLTextureUploadMeta *meta, guint texture_id[4])
{
    GstVaapiVideoMeta * const vmeta =
        gst_buffer_get_vaapi_video_meta(meta->buffer);
    GstVaapiTexture *texture = meta->user_data;
    GstVaapiSurface * const surface = gst_vaapi_video_meta_get_surface(vmeta);
    GstVaapiDisplay * const dpy =
        gst_vaapi_object_get_display(GST_VAAPI_OBJECT(surface));

    if (gst_vaapi_display_get_display_type(dpy) != GST_VAAPI_DISPLAY_TYPE_GLX)
        return FALSE;

    if (texture) {
        GstVaapiDisplay * const tex_dpy =
            gst_vaapi_object_get_display(GST_VAAPI_OBJECT(texture));
        if (tex_dpy != dpy)
            gst_vaapi_texture_replace(&texture, NULL);
    }

    if (!texture) {
        /* FIXME: should we assume target? */
        texture = gst_vaapi_texture_new_with_texture(dpy, texture_id[0],
            GL_TEXTURE_2D, GL_RGBA);
        meta->user_data = texture;
    }

    if (!gst_vaapi_apply_composition(surface, meta->buffer))
        GST_WARNING("could not update subtitles");

    return gst_vaapi_texture_put_surface(texture, surface,
        gst_vaapi_video_meta_get_render_flags(vmeta));
}
#endif

#if GST_CHECK_VERSION(1,1,0)
gboolean
gst_buffer_add_texture_upload_meta(GstBuffer *buffer)
{
    GstVideoGLTextureUploadMeta *meta = NULL;
    GstVideoGLTextureType tex_type[] = { GST_VIDEO_GL_TEXTURE_TYPE_RGBA };

    if (!buffer)
        return FALSE;

#if USE_GLX
    meta = gst_buffer_add_video_gl_texture_upload_meta(buffer,
        GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL,
        1, tex_type, gst_vaapi_texture_upload,
        NULL, NULL, gst_vaapi_texure_upload_free);
#endif
    return meta != NULL;
}
#endif
