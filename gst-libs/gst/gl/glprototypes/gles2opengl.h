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

GST_GL_EXT_BEGIN (offscreen,
                  255, 255,
                  GST_GL_API_GLES2,
                  /* for some reason the ARB version of this
                     extension doesn't have an ARB suffix for the
                     functions */
                  "ARB:\0EXT\0OES\0",
                  "framebuffer_object\0")
GST_GL_EXT_FUNCTION (void, GenRenderbuffers,
                     (GLsizei               n,
                      GLuint               *renderbuffers))
GST_GL_EXT_FUNCTION (void, DeleteRenderbuffers,
                     (GLsizei               n,
                      const GLuint         *renderbuffers))
GST_GL_EXT_FUNCTION (void, BindRenderbuffer,
                     (GLenum                target,
                      GLuint                renderbuffer))
GST_GL_EXT_FUNCTION (void, RenderbufferStorage,
                     (GLenum                target,
                      GLenum                internalformat,
                      GLsizei               width,
                      GLsizei               height))
GST_GL_EXT_FUNCTION (void, GenFramebuffers,
                     (GLsizei               n,
                      GLuint               *framebuffers))
GST_GL_EXT_FUNCTION (void, BindFramebuffer,
                     (GLenum                target,
                      GLuint                framebuffer))
GST_GL_EXT_FUNCTION (void, FramebufferTexture2D,
                     (GLenum                target,
                      GLenum                attachment,
                      GLenum                textarget,
                      GLuint                texture,
                      GLint                 level))
GST_GL_EXT_FUNCTION (void, FramebufferRenderbuffer,
                     (GLenum                target,
                      GLenum                attachment,
                      GLenum                renderbuffertarget,
                      GLuint                renderbuffer))
GST_GL_EXT_FUNCTION (GLboolean, IsRenderbuffer,
                     (GLuint                renderbuffer))
GST_GL_EXT_FUNCTION (GLenum, CheckFramebufferStatus,
                     (GLenum                target))
GST_GL_EXT_FUNCTION (void, DeleteFramebuffers,
                     (GLsizei               n,
                      const                 GLuint *framebuffers))
GST_GL_EXT_FUNCTION (void, GenerateMipmap,
                     (GLenum                target))
GST_GL_EXT_FUNCTION (void, GetFramebufferAttachmentParameteriv,
                     (GLenum                target,
                      GLenum                attachment,
                      GLenum                pname,
                      GLint                *params))
GST_GL_EXT_FUNCTION (void, GetRenderbufferParameteriv,
                     (GLenum                target,
                      GLenum                pname,
                      GLint                *params))
GST_GL_EXT_FUNCTION (GLboolean, IsFramebuffer,
                     (GLuint                framebuffer))
GST_GL_EXT_END ()

GST_GL_EXT_BEGIN (blending, 1, 2,
                  GST_GL_API_GLES2,
                  "\0",
                  "\0")
GST_GL_EXT_FUNCTION (void, BlendEquation,
                     (GLenum                mode))
GST_GL_EXT_FUNCTION (void, BlendColor,
                     (GLclampf              red,
                      GLclampf              green,
                      GLclampf              blue,
                      GLclampf              alpha))
GST_GL_EXT_END ()

/* Optional, declared in 1.4 or GLES 1.2 */
GST_GL_EXT_BEGIN (blend_func_separate, 1, 4,
                  GST_GL_API_GLES2,
                  "EXT\0",
                  "blend_func_separate\0")
GST_GL_EXT_FUNCTION (void, BlendFuncSeparate,
                     (GLenum                srcRGB,
                      GLenum                dstRGB,
                      GLenum                srcAlpha,
                      GLenum                dstAlpha))
GST_GL_EXT_END ()

/* Optional, declared in 2.0 */
GST_GL_EXT_BEGIN (blend_equation_separate, 2, 0,
                  GST_GL_API_GLES2,
                  "EXT\0",
                  "blend_equation_separate\0")
GST_GL_EXT_FUNCTION (void, BlendEquationSeparate,
                     (GLenum                modeRGB,
                      GLenum                modeAlpha))
GST_GL_EXT_END ()

/* GL and GLES 2.0 apis */
GST_GL_EXT_BEGIN (two_point_zero_api,
                  2, 0,
                  GST_GL_API_GLES2,
                  "\0",
                  "\0")
GST_GL_EXT_FUNCTION (void, StencilFuncSeparate,
                     (GLenum face, GLenum func, GLint ref, GLuint mask))
GST_GL_EXT_FUNCTION (void, StencilMaskSeparate,
                     (GLenum face, GLuint mask))
GST_GL_EXT_FUNCTION (void, StencilOpSeparate,
                     (GLenum face, GLenum fail, GLenum zfail, GLenum zpass))
GST_GL_EXT_END ()

/* This lists functions that are unique to GL 2.0 or GLES 2.0 and are
 * not in the old GLSL extensions */
GST_GL_EXT_BEGIN (shaders_glsl_2_only, 2, 0,
                  GST_GL_API_GLES2,
                  "\0",
                  "\0")
GST_GL_EXT_FUNCTION (GLuint, CreateProgram,
                     (void))
GST_GL_EXT_FUNCTION (GLuint, CreateShader,
                     (GLenum                shaderType))
GST_GL_EXT_FUNCTION (void, DeleteShader,
                     (GLuint                shader))
GST_GL_EXT_FUNCTION (void, AttachShader,
                     (GLuint                program,
                      GLuint                shader))
GST_GL_EXT_FUNCTION (void, UseProgram,
                     (GLuint                program))
GST_GL_EXT_FUNCTION (void, DeleteProgram,
                     (GLuint                program))
GST_GL_EXT_FUNCTION (void, GetShaderInfoLog,
                     (GLuint                shader,
                      GLsizei               maxLength,
                      GLsizei              *length,
                      char                 *infoLog))
GST_GL_EXT_FUNCTION (void, GetProgramInfoLog,
                     (GLuint                program,
                      GLsizei               bufSize,
                      GLsizei              *length,
                      char                 *infoLog))
GST_GL_EXT_FUNCTION (void, GetShaderiv,
                     (GLuint                shader,
                      GLenum                pname,
                      GLint                *params))
GST_GL_EXT_FUNCTION (void, GetProgramiv,
                     (GLuint                program,
                      GLenum                pname,
                      GLint                *params))
GST_GL_EXT_FUNCTION (void, DetachShader,
                     (GLuint program, GLuint shader))
GST_GL_EXT_FUNCTION (void, GetAttachedShaders,
                     (GLuint program,
                      GLsizei maxcount,
                      GLsizei* count,
                      GLuint* shaders))
GST_GL_EXT_FUNCTION (GLboolean, glIsShader,
                     (GLuint shader))
GST_GL_EXT_FUNCTION (GLboolean, glIsProgram,
                     (GLuint program))
GST_GL_EXT_END ()

/* These functions are provided by GL_ARB_shader_objects or are in GL
 * 2.0 core */
GST_GL_EXT_BEGIN (shader_objects_or_gl2, 2, 0,
                  GST_GL_API_GLES2,
                  "ARB\0",
                  "shader_objects\0")
GST_GL_EXT_FUNCTION (void, ShaderSource,
                     (GLuint                shader,
                      GLsizei               count,
                      const char          **string,
                      const GLint          *length))
GST_GL_EXT_FUNCTION (void, CompileShader,
                     (GLuint                shader))
GST_GL_EXT_FUNCTION (void, LinkProgram,
                     (GLuint                program))
GST_GL_EXT_FUNCTION (GLint, GetUniformLocation,
                     (GLuint                program,
                      const char           *name))
GST_GL_EXT_FUNCTION (void, Uniform1f,
                     (GLint                 location,
                      GLfloat               v0))
GST_GL_EXT_FUNCTION (void, Uniform2f,
                     (GLint                 location,
                      GLfloat               v0,
                      GLfloat               v1))
GST_GL_EXT_FUNCTION (void, Uniform3f,
                     (GLint                 location,
                      GLfloat               v0,
                      GLfloat               v1,
                      GLfloat               v2))
GST_GL_EXT_FUNCTION (void, Uniform4f,
                     (GLint                 location,
                      GLfloat               v0,
                      GLfloat               v1,
                      GLfloat               v2,
                      GLfloat               v3))
GST_GL_EXT_FUNCTION (void, Uniform1fv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLfloat *       value))
GST_GL_EXT_FUNCTION (void, Uniform2fv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLfloat *       value))
GST_GL_EXT_FUNCTION (void, Uniform3fv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLfloat *       value))
GST_GL_EXT_FUNCTION (void, Uniform4fv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLfloat *       value))
GST_GL_EXT_FUNCTION (void, Uniform1i,
                     (GLint                 location,
                      GLint                 v0))
GST_GL_EXT_FUNCTION (void, Uniform2i,
                     (GLint                 location,
                      GLint                 v0,
                      GLint                 v1))
GST_GL_EXT_FUNCTION (void, Uniform3i,
                     (GLint                 location,
                      GLint                 v0,
                      GLint                 v1,
                      GLint                 v2))
GST_GL_EXT_FUNCTION (void, Uniform4i,
                     (GLint                 location,
                      GLint                 v0,
                      GLint                 v1,
                      GLint                 v2,
                      GLint                 v3))
GST_GL_EXT_FUNCTION (void, Uniform1iv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLint *         value))
GST_GL_EXT_FUNCTION (void, Uniform2iv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLint *         value))
GST_GL_EXT_FUNCTION (void, Uniform3iv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLint *         value))
GST_GL_EXT_FUNCTION (void, Uniform4iv,
                     (GLint                 location,
                      GLsizei               count,
                      const GLint *         value))
GST_GL_EXT_FUNCTION (void, UniformMatrix2fv,
                     (GLint                 location,
                      GLsizei               count,
                      GLboolean             transpose,
                      const GLfloat        *value))
GST_GL_EXT_FUNCTION (void, UniformMatrix3fv,
                     (GLint                 location,
                      GLsizei               count,
                      GLboolean             transpose,
                      const GLfloat        *value))
GST_GL_EXT_FUNCTION (void, UniformMatrix4fv,
                     (GLint                 location,
                      GLsizei               count,
                      GLboolean             transpose,
                      const GLfloat        *value))

GST_GL_EXT_FUNCTION (void, GetUniformfv,
                     (GLuint                program,
                      GLint                 location,
                      GLfloat              *params))
GST_GL_EXT_FUNCTION (void, GetUniformiv,
                     (GLuint                program,
                      GLint                 location,
                      GLint                *params))
GST_GL_EXT_FUNCTION (void, GetActiveUniform,
                     (GLuint program,
                      GLuint index,
                      GLsizei bufsize,
                      GLsizei* length,
                      GLint* size,
                      GLenum* type,
                      GLchar* name))
GST_GL_EXT_FUNCTION (void, GetShaderSource,
                     (GLuint shader,
                      GLsizei bufsize,
                      GLsizei* length,
                      GLchar* source))
GST_GL_EXT_FUNCTION (void, glValidateProgram, (GLuint program))
GST_GL_EXT_END ()

/* These functions are provided by GL_ARB_vertex_shader or are in GL
 * 2.0 core */
GST_GL_EXT_BEGIN (vertex_shaders, 2, 0,
                  GST_GL_API_GLES2,
                  "ARB\0",
                  "vertex_shader\0")
GST_GL_EXT_FUNCTION (void, VertexAttribPointer,
                     (GLuint		 index,
                      GLint		 size,
                      GLenum		 type,
                      GLboolean		 normalized,
                      GLsizei		 stride,
                      const GLvoid        *pointer))
GST_GL_EXT_FUNCTION (void, EnableVertexAttribArray,
                     (GLuint		 index))
GST_GL_EXT_FUNCTION (void, DisableVertexAttribArray,
                     (GLuint		 index))
GST_GL_EXT_FUNCTION (void, VertexAttrib1f, (GLuint indx, GLfloat x))
GST_GL_EXT_FUNCTION (void, VertexAttrib1fv,
                     (GLuint indx, const GLfloat* values))
GST_GL_EXT_FUNCTION (void, VertexAttrib2f, (GLuint indx, GLfloat x, GLfloat y))
GST_GL_EXT_FUNCTION (void, VertexAttrib2fv,
                     (GLuint indx, const GLfloat* values))
GST_GL_EXT_FUNCTION (void, VertexAttrib3f,
                     (GLuint indx, GLfloat x, GLfloat y, GLfloat z))
GST_GL_EXT_FUNCTION (void, VertexAttrib3fv,
                     (GLuint indx, const GLfloat* values))
GST_GL_EXT_FUNCTION (void, VertexAttrib4f,
                     (GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w))
GST_GL_EXT_FUNCTION (void, VertexAttrib4fv,
                     (GLuint indx, const GLfloat* values))
GST_GL_EXT_FUNCTION (void, GetVertexAttribfv,
                     (GLuint index, GLenum pname, GLfloat* params))
GST_GL_EXT_FUNCTION (void, GetVertexAttribiv,
                     (GLuint index, GLenum pname, GLint* params))
GST_GL_EXT_FUNCTION (void, GetVertexAttribPointerv,
                     (GLuint index, GLenum pname, GLvoid** pointer))
GST_GL_EXT_FUNCTION (GLint, glGetAttribLocation,
                     (GLuint program, const char *name))
GST_GL_EXT_FUNCTION (void, BindAttribLocation,
                     (GLuint program,
                      GLuint index,
                      const GLchar* name))
GST_GL_EXT_FUNCTION (void, GetActiveAttrib,
                     (GLuint program,
                      GLuint index,
                      GLsizei bufsize,
                      GLsizei* length,
                      GLint* size,
                      GLenum* type,
                      GLchar* name))
GST_GL_EXT_END ()


GST_GL_EXT_BEGIN (texture_3d, 1, 2,
                  0, /* not in either GLES */
                  "OES\0",
                  "texture_3D\0")
GST_GL_EXT_FUNCTION (void, TexImage3D,
                     (GLenum target, GLint level,
                      GLint internalFormat,
                      GLsizei width, GLsizei height,
                      GLsizei depth, GLint border,
                      GLenum format, GLenum type,
                      const GLvoid *pixels))
GST_GL_EXT_FUNCTION (void, TexSubImage3D,
                     (GLenum target, GLint level,
                      GLint xoffset, GLint yoffset,
                      GLint zoffset, GLsizei width,
                      GLsizei height, GLsizei depth,
                      GLenum format,
                      GLenum type, const GLvoid *pixels))
GST_GL_EXT_END ()

GST_GL_EXT_BEGIN (offscreen_blit, 255, 255,
                  0, /* not in either GLES */
                  "EXT\0ANGLE\0",
                  "framebuffer_blit\0")
GST_GL_EXT_FUNCTION (void, BlitFramebuffer,
                     (GLint                 srcX0,
                      GLint                 srcY0,
                      GLint                 srcX1,
                      GLint                 srcY1,
                      GLint                 dstX0,
                      GLint                 dstY0,
                      GLint                 dstX1,
                      GLint                 dstY1,
                      GLbitfield            mask,
                      GLenum                filter))
GST_GL_EXT_END ()
