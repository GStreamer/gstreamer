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
/*
 * Cogl
 *
 * An object oriented GL/GLES Abstraction/Utility Layer
 *
 * Copyright (C) 2009, 2011 Intel Corporation.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

/* These are the core GL functions which are only available in big
   GL */
GST_GL_EXT_BEGIN (only_in_big_gl,
                  0, 0,
                  0, /* not in GLES */
                  "\0",
                  "\0")
GST_GL_EXT_FUNCTION (void, GetTexLevelParameteriv,
                     (GLenum target, GLint level,
                      GLenum pname, GLint *params))
GST_GL_EXT_FUNCTION (void, GetTexImage,
                     (GLenum target, GLint level,
                      GLenum format, GLenum type,
                      GLvoid *pixels))
GST_GL_EXT_FUNCTION (void, ClipPlane,
                     (GLenum plane, const double *equation))
GST_GL_EXT_FUNCTION (void, DepthRange,
                     (double near_val, double far_val))
GST_GL_EXT_FUNCTION (void, DrawBuffer,
                     (GLenum mode))
GST_GL_EXT_END ()

GST_GL_EXT_BEGIN (framebuffer_discard, 255, 255,
                  0, /* not in either GLES */
                  "EXT\0",
                  "framebuffer_discard\0")
GST_GL_EXT_FUNCTION (void, DiscardFramebuffer,
                     (GLenum           target,
                      GLsizei          numAttachments,
                      const GLenum    *attachments))
GST_GL_EXT_END ()

/* These only list functions that come from the old GLSL extensions.
 * Functions that are common to the extensions and GLSL 2.0 should
 * instead be listed in cogl-glsl-functions.h */
GST_GL_EXT_BEGIN (shader_objects, 255, 255,
                  0, /* not in either GLES */
                  "ARB\0",
                  "shader_objects\0")
GST_GL_EXT_FUNCTION (GLuint, CreateProgramObject,
                     (void))
GST_GL_EXT_FUNCTION (GLuint, CreateShaderObject,
                     (GLenum shaderType))
GST_GL_EXT_FUNCTION (void, DeleteObject,
                     (GLuint obj))
GST_GL_EXT_FUNCTION (void, AttachObject,
                     (GLuint container, GLuint obj))
GST_GL_EXT_FUNCTION (void, UseProgramObject,
                     (GLuint programObj))
GST_GL_EXT_FUNCTION (void, GetInfoLog,
                     (GLuint                obj,
                      GLsizei               maxLength,
                      GLsizei              *length,
                      char                 *infoLog))
GST_GL_EXT_FUNCTION (void, GetObjectParameteriv,
                     (GLuint                obj,
                      GLenum                pname,
                      GLint                *params))
GST_GL_EXT_FUNCTION (void, DetachObject,
                     (GLuint container, GLuint obj))
GST_GL_EXT_FUNCTION (void, GetAttachedObjects,
                     (GLuint program,
                      GLsizei maxcount,
                      GLsizei* count,
                      GLuint* shaders))
GST_GL_EXT_END ()


/* ARB_fragment_program */
GST_GL_EXT_BEGIN (arbfp, 255, 255,
                  0, /* not in either GLES */
                  "ARB\0",
                  "fragment_program\0")
GST_GL_EXT_FUNCTION (void, GenPrograms,
                     (GLsizei               n,
                      GLuint               *programs))
GST_GL_EXT_FUNCTION (void, DeletePrograms,
                     (GLsizei               n,
                      GLuint               *programs))
GST_GL_EXT_FUNCTION (void, BindProgram,
                     (GLenum                target,
                      GLuint                program))
GST_GL_EXT_FUNCTION (void, ProgramString,
                     (GLenum                target,
                      GLenum                format,
                      GLsizei               len,
                      const void           *program))
GST_GL_EXT_FUNCTION (void, ProgramLocalParameter4fv,
                     (GLenum                target,
                      GLuint                index,
                      GLfloat              *params))
GST_GL_EXT_END ()

/* Eventually we want to remove this category */
GST_GL_EXT_BEGIN (fixed_function_gl_only,
                  0, 0,
                  0,
                  "\0",
                  "\0")
GST_GL_EXT_FUNCTION (void, PushAttrib,
                     (GLbitfield            mask))
GST_GL_EXT_FUNCTION (void, PopAttrib,
                     (void))
GST_GL_EXT_FUNCTION (void, TexImage1D,
                     (GLenum                target,
                      GLint                 level,
                      GLint                 internalFormat,
                      GLsizei               width,
                      GLint                 border,
                      GLenum                format,
                      GLenum                type,
                      const GLvoid         *data))
GST_GL_EXT_FUNCTION (void, Rotatef,
                     (GLfloat angle, GLfloat x, GLfloat y, GLfloat z))
GST_GL_EXT_FUNCTION (void, Translatef,
                     (GLfloat x, GLfloat y, GLfloat z))
GST_GL_EXT_FUNCTION (void, Scalef,
                     (GLfloat x, GLfloat y, GLfloat z))
GST_GL_EXT_FUNCTION (void, Lightfv,
                     (GLenum light, GLenum pname, const GLfloat *params))
GST_GL_EXT_FUNCTION (void, ColorMaterial,
                     (GLenum face, GLenum pname))
GST_GL_EXT_FUNCTION (void, ShadeModel,
                     (GLenum value))
GST_GL_EXT_END ()

GST_GL_EXT_BEGIN (gl3,
                  3, 1,
                  GST_GL_API_GLES3, /* not in GLES */
                  "\0",
                  "\0")
GST_GL_EXT_FUNCTION (const GLubyte*, GetStringi,
                     (GLenum name, GLint index))
GST_GL_EXT_END ()
