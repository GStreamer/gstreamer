/*
 * Copyright (C) 2014, Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@collabora.com>
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

#ifndef __GST_AMC_2D_TEXTURE_RENDER_H__
#define __GST_AMC_2D_TEXTURE_RENDER_H__

#include <gst/gst.h>
#include <gst/gl/gl.h>

#include "gstamcsurfacetexture.h"

G_BEGIN_DECLS

typedef struct _GstAmc2DTextureRenderer GstAmc2DTextureRenderer;

struct _GstAmc2DTextureRenderer {

  /* private */
  gboolean             initialized;

  GstGLContext         *context;

  GstVideoInfo         info;

  GLuint               fbo;
  GLuint               depth_buffer;

  GstGLShader          *shader;

  GLint                shader_attr_position_loc;
  GLint                shader_attr_texture_loc;

  GError               *gl_context_error;
  gboolean             gl_context_result;

  GstAmcSurfaceTexture *surface_texture;

  guint                tex_id;
  guint                oes_tex_id;

  gfloat               transformation_matrix[16];

  /* out fields */
  gboolean             result;
};

GstAmc2DTextureRenderer * gst_amc_2d_texture_renderer_new     (GstGLContext * context,
                                                              GstAmcSurfaceTexture *surface_texture,
                                                              guint width,
                                                              guint height);

void gst_amc_2d_texture_renderer_free                         (GstAmc2DTextureRenderer * render);

gboolean gst_amc_2d_texture_renderer_render                   (GstAmc2DTextureRenderer * render,
                                                              guint tex_id,
                                                              GError ** error);

G_END_DECLS
#endif
