/*
 * Copyright (C) 2012, Collabora Ltd.
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
 *
 * Copyright (C) 2015, Collabora Ltd.
 *   Author: Justin Kim <justin.kim@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_ANDROID_GRAPHICS_SURFACETEXTURE_H__
#define __GST_ANDROID_GRAPHICS_SURFACETEXTURE_H__

#include <gst/gst.h>
#include <jni.h>


G_BEGIN_DECLS

typedef struct _GstAGSurfaceTexture GstAGSurfaceTexture;

/* android.graphics.SurfaceTexture */
struct _GstAGSurfaceTexture {
  /* < private > */
  jobject object; /* global reference */
};


gboolean gst_android_graphics_surfacetexture_init (void);
void gst_android_graphics_surfacetexture_deinit (void);

/* android.graphics.SurfaceTexture */
GstAGSurfaceTexture *gst_ag_surfacetexture_new (gint texture_id);
void gst_ag_surfacetexture_release (GstAGSurfaceTexture *self);
void gst_ag_surfacetexture_free (GstAGSurfaceTexture *self);

G_END_DECLS

#endif /* __GST_ANDROID_GRAPHICS_SURFACETEXTURE_H__ */

