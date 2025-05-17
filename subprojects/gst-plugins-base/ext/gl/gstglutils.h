/*
 * GStreamer
 * Copyright (C) 2016 Matthew Waters <matthew@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __EXT_GL_GST_GL_UTILS_H__
#define __EXT_GL_GST_GL_UTILS_H__

#include <gst/gl/gl.h>

G_BEGIN_DECLS

gboolean gst_gl_context_gen_shader (GstGLContext * context,
    const gchar * shader_vertex_source,
    const gchar * shader_fragment_source, GstGLShader ** shader);

/* Populated in the plugin init function */
extern GQuark _gst_gl_tags_quark;

/* This is public API in 1.28 */
#define gst_meta_api_type_tags_contain_only(api,valid_tags) \
  gst_gl_gst_meta_api_type_tags_contain_only(api,valid_tags)

gboolean gst_gl_gst_meta_api_type_tags_contain_only (GType api,
    const gchar ** valid_tags);

G_END_DECLS

#endif /* __EXT_GL_GST_GL_UTILS_H__ */
