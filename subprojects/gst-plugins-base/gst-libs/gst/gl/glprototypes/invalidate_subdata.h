/*
 * GStreamer
 * Copyright (C) 2024 Andreas Wittmann <andreas.wittmann@zeiss.com>
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

GST_GL_EXT_BEGIN (invalidate_subdata,
                  GST_GL_API_OPENGL3 |
                  GST_GL_API_GLES2,
                  4, 3,
                  3, 0,
                  "ARB:\0",
                  "invalidate_subdata\0")
GST_GL_EXT_FUNCTION (void, InvalidateFramebuffer,
                     (GLenum        target,
                      GLsizei       numAttachments,
                      const GLenum* attachments))
GST_GL_EXT_FUNCTION (void, InvalidateSubFramebuffer,
                     (GLenum        target,
                      GLsizei       numAttachments,
                      const GLenum* attachments,
                      GLint         x,
                      GLint         y,
                      GLsizei       width,
                      GLsizei       height))
GST_GL_EXT_END ()

GST_GL_EXT_BEGIN (invalidate_subdata_no_gles,
                  GST_GL_API_OPENGL3,
                  4, 3,
                  255, 255,
                  "ARB:\0",
                  "invalidate_subdata\0")
GST_GL_EXT_FUNCTION (void, InvalidateTexSubImage,
                     (GLuint        texture,
                      GLint         level,
                      GLint         xoffset,
                      GLint         yoffset,
                      GLint         zoffset,
                      GLsizei       width,
                      GLsizei       height,
                      GLsizei       depth))
GST_GL_EXT_FUNCTION (void, InvalidateTexImage,
                     (GLuint        texture,
                      GLint         level))
GST_GL_EXT_FUNCTION (void, InvalidateBufferSubData,
                     (GLuint        buffer,
                      GLintptr      offset,
                      GLsizeiptr    length))
GST_GL_EXT_FUNCTION (void, InvalidateBufferData,
                     (GLuint        buffer))
GST_GL_EXT_END ()