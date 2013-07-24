/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

/* This lists functions that are unique to GL 2.1 or GLES 3.0 and are
 * not in the old GLSL extensions */
GST_GL_EXT_BEGIN (shaders_2_1, 2, 1,
                  GST_GL_API_GLES3,
                  "\0",
                  "\0")
GST_GL_EXT_FUNCTION (void, UniformMatrix2x3fv,
                     (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
GST_GL_EXT_FUNCTION (void, UniformMatrix3x2fv,
                     (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
GST_GL_EXT_FUNCTION (void, UniformMatrix2x4fv,
                     (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
GST_GL_EXT_FUNCTION (void, UniformMatrix4x2fv,
                     (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
GST_GL_EXT_FUNCTION (void, UniformMatrix3x4fv,
                     (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
GST_GL_EXT_FUNCTION (void, UniformMatrix4x3fv,
                     (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value))
GST_GL_EXT_END ()

GST_GL_EXT_BEGIN (read_buffer, 1, 0,
                  GST_GL_API_GLES3,
                  "NV\0",
                  "read_buffer\0")
GST_GL_EXT_FUNCTION (void, ReadBuffer,
                     (GLenum mode))
GST_GL_EXT_END ()

GST_GL_EXT_BEGIN (draw_buffers, 2, 1,
                  GST_GL_API_GLES3,
                  "ARB\0ATI\0NV\0",
                  "draw_buffers\0")
GST_GL_EXT_FUNCTION (void, DrawBuffers,
                     (GLsizei n, const GLenum *bufs))
GST_GL_EXT_END ()

