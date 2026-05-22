/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#ifndef __GST_GL_ALPHA_COMBINE_H__
#define __GST_GL_ALPHA_COMBINE_H__

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#define GST_TYPE_GL_ALPHA_COMBINE (gst_gl_alpha_combine_get_type())
#define GST_GL_ALPHA_COMBINE(obj) \
    (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_GL_ALPHA_COMBINE, GstGLAlphaCombine))
#define GST_GL_ALPHA_COMBINE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_GL_ALPHA_COMBINE, GstGLAlphaCombineClass))
#define GST_IS_GL_ALPHA_COMBINE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_GL_ALPHA_COMBINE))
#define GST_IS_GL_ALPHA_COMBINE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_GL_ALPHA_COMBINE))

typedef struct _GstGLAlphaCombine GstGLAlphaCombine;
typedef struct _GstGLAlphaCombineClass GstGLAlphaCombineClass;

/**
 * GstGLAlphaCombineAlphaComponent:
 * @GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_RED: Red component (Since: 1.30)
 * @GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_GREEN: Green component (Since: 1.30)
 * @GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_BLUE: Blue component (Since: 1.30)
 * @GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_ALPHA: Alpha component (Since: 1.30)
 *
 * The RGBA component to sample from the alpha input and write to the output
 * alpha channel.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_RED,
  GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_GREEN,
  GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_BLUE,
  GST_GL_ALPHA_COMBINE_ALPHA_COMPONENT_ALPHA,
} GstGLAlphaCombineAlphaComponent;

struct _GstGLAlphaCombine
{
  GstGLMixer mixer;

  GstGLMixerPad *color_pad;
  GstGLMixerPad *alpha_pad;

  GstGLShader *shader;
  GstGLMemory *out_tex;
  GstGLTextureTarget color_texture_target;
  GstGLTextureTarget alpha_texture_target;
  GstGLTextureTarget shader_color_texture_target;
  GstGLTextureTarget shader_alpha_texture_target;
  guint vao;
  guint vertex_buffer;
  guint vbo_indices;

  GstGLAlphaCombineAlphaComponent alpha_component;
};

struct _GstGLAlphaCombineClass
{
  GstGLMixerClass mixer_class;
};

GType gst_gl_alpha_combine_get_type (void);

G_END_DECLS

#endif /* __GST_GL_ALPHA_COMBINE_H__ */
