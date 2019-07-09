/*
 * egl_vtable.h - EGL function definitions
 *
 * Copyright (C) 2014 Intel Corporation
 *   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301
 */

/* ------------------------------------------------------------------------- */
// Generate strings

#define GL_PROTO_GEN_STRING(x) \
  GL_PROTO_GEN_STRING_I(x)
#define GL_PROTO_GEN_STRING_I(x) \
  #x

/* ------------------------------------------------------------------------- */
// Concatenate arguments

#define GL_PROTO_GEN_CONCAT(a1, a2) \
  GL_PROTO_GEN_CONCAT2_I(a1, a2)
#define GL_PROTO_GEN_CONCAT2(a1, a2) \
  GL_PROTO_GEN_CONCAT2_I(a1, a2)
#define GL_PROTO_GEN_CONCAT2_I(a1, a2) \
  a1 ## a2

#define GL_PROTO_GEN_CONCAT3(a1, a2, a3) \
  GL_PROTO_GEN_CONCAT3_I(a1, a2, a3)
#define GL_PROTO_GEN_CONCAT3_I(a1, a2, a3) \
  a1 ## a2 ## a3

#define GL_PROTO_GEN_CONCAT4(a1, a2, a3, a4) \
  GL_PROTO_GEN_CONCAT4_I(a1, a2, a3, a4)
#define GL_PROTO_GEN_CONCAT4_I(a1, a2, a3, a4) \
  a1 ## a2 ## a3 ## a4

#define GL_PROTO_GEN_CONCAT5(a1, a2, a3, a4, a5) \
  GL_PROTO_GEN_CONCAT5_I(a1, a2, a3, a4, a5)
#define GL_PROTO_GEN_CONCAT5_I(a1, a2, a3, a4, a5) \
  a1 ## a2 ## a3 ## a4 ## a5

/* ------------------------------------------------------------------------- */
// Default macros

#ifndef EGL_PROTO_BEGIN
#define EGL_PROTO_BEGIN(NAME, TYPE, EXTENSION)
#endif
#ifndef EGL_PROTO_ARG_LIST
#define EGL_PROTO_ARG_LIST(...) GL_PROTO_ARG_LIST(__VA_ARGS__)
#endif
#ifndef EGL_PROTO_ARG
#define EGL_PROTO_ARG(NAME, TYPE) GL_PROTO_ARG(NAME, TYPE)
#endif
#ifndef EGL_PROTO_INVOKE
#define EGL_PROTO_INVOKE(NAME, TYPE, ARGS)
#endif
#ifndef EGL_PROTO_END
#define EGL_PROTO_END()
#endif
#ifndef EGL_DEFINE_EXTENSION
#define EGL_DEFINE_EXTENSION(EXTENSION)
#endif

#ifndef GL_PROTO_BEGIN
#define GL_PROTO_BEGIN(NAME, TYPE, EXTENSION)
#endif
#ifndef GL_PROTO_ARG_LIST
#define GL_PROTO_ARG_LIST(...)
#endif
#ifndef GL_PROTO_ARG
#define GL_PROTO_ARG(NAME, TYPE)
#endif
#ifndef GL_PROTO_INVOKE
#define GL_PROTO_INVOKE(NAME, TYPE, ARGS)
#endif
#ifndef GL_PROTO_END
#define GL_PROTO_END()
#endif
#ifndef GL_DEFINE_EXTENSION
#define GL_DEFINE_EXTENSION(EXTENSION)
#endif

/* NOTE: this is auto-generated code -- do not edit! */

EGL_PROTO_BEGIN(CreateImageKHR, EGLImageKHR, KHR_image_base)
EGL_PROTO_ARG_LIST(
EGL_PROTO_ARG(dpy, EGLDisplay),
EGL_PROTO_ARG(ctx, EGLContext),
EGL_PROTO_ARG(target, EGLenum),
EGL_PROTO_ARG(buffer, EGLClientBuffer),
EGL_PROTO_ARG(attrib_list, const EGLint *))
EGL_PROTO_INVOKE(CreateImageKHR, EGLImageKHR, (dpy, ctx, target, buffer, attrib_list))
EGL_PROTO_END()

EGL_PROTO_BEGIN(DestroyImageKHR, EGLImageKHR, KHR_image_base)
EGL_PROTO_ARG_LIST(
EGL_PROTO_ARG(dpy, EGLDisplay),
EGL_PROTO_ARG(image, EGLImageKHR))
EGL_PROTO_INVOKE(DestroyImageKHR, EGLImageKHR, (dpy, image))
EGL_PROTO_END()

EGL_PROTO_BEGIN(CreateDRMImageMESA, EGLImageKHR, MESA_drm_image)
EGL_PROTO_ARG_LIST(
EGL_PROTO_ARG(dpy, EGLDisplay),
EGL_PROTO_ARG(attrib_list, const EGLint *))
EGL_PROTO_INVOKE(CreateDRMImageMESA, EGLImageKHR, (dpy, attrib_list))
EGL_PROTO_END()

EGL_PROTO_BEGIN(ExportDRMImageMESA, EGLImageKHR, MESA_drm_image)
EGL_PROTO_ARG_LIST(
EGL_PROTO_ARG(dpy, EGLDisplay),
EGL_PROTO_ARG(image, EGLImageKHR),
EGL_PROTO_ARG(name, EGLint *),
EGL_PROTO_ARG(handle, EGLint *),
EGL_PROTO_ARG(stride, EGLint *))
EGL_PROTO_INVOKE(ExportDRMImageMESA, EGLImageKHR, (dpy, image, name, handle, stride))
EGL_PROTO_END()

EGL_PROTO_BEGIN(ExportDMABUFImageMESA, EGLBoolean, MESA_image_dma_buf_export)
EGL_PROTO_ARG_LIST(
EGL_PROTO_ARG(dpy, EGLDisplay),
EGL_PROTO_ARG(image, EGLImageKHR),
EGL_PROTO_ARG(fds, int *),
EGL_PROTO_ARG(strides, EGLint *),
EGL_PROTO_ARG(offsets, EGLint *))
EGL_PROTO_INVOKE(ExportDMABUFImageMESA, EGLBoolean, (dpy, image, fds, strides, offsets))
EGL_PROTO_END()

EGL_PROTO_BEGIN(ExportDMABUFImageQueryMESA, EGLBoolean, MESA_image_dma_buf_export)
EGL_PROTO_ARG_LIST(
EGL_PROTO_ARG(dpy, EGLDisplay),
EGL_PROTO_ARG(image, EGLImageKHR),
EGL_PROTO_ARG(fourcc, int *),
EGL_PROTO_ARG(num_planes, int *),
EGL_PROTO_ARG(modifiers, EGLuint64KHR *))
EGL_PROTO_INVOKE(ExportDMABUFImageQueryMESA, EGLBoolean, (dpy, image, fourcc, num_planes, modifiers))
EGL_PROTO_END()

EGL_DEFINE_EXTENSION(EXT_image_dma_buf_import)
EGL_DEFINE_EXTENSION(KHR_create_context)
EGL_DEFINE_EXTENSION(KHR_gl_texture_2D_image)
EGL_DEFINE_EXTENSION(KHR_image_base)
EGL_DEFINE_EXTENSION(KHR_surfaceless_context)
EGL_DEFINE_EXTENSION(MESA_configless_context)
EGL_DEFINE_EXTENSION(MESA_drm_image)
EGL_DEFINE_EXTENSION(MESA_image_dma_buf_export)

GL_PROTO_BEGIN(GetError, GLenum, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(GetError, GLenum, ())
GL_PROTO_END()

GL_PROTO_BEGIN(GetString, const GLubyte *, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(name, GLenum))
GL_PROTO_INVOKE(GetString, const GLubyte *, (name))
GL_PROTO_END()

GL_PROTO_BEGIN(GetIntegerv, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(params, GLint *))
GL_PROTO_INVOKE(GetIntegerv, void, (pname, params))
GL_PROTO_END()

GL_PROTO_BEGIN(Enable, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(cap, GLenum))
GL_PROTO_INVOKE(Enable, void, (cap))
GL_PROTO_END()

GL_PROTO_BEGIN(Disable, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(cap, GLenum))
GL_PROTO_INVOKE(Disable, void, (cap))
GL_PROTO_END()

GL_PROTO_BEGIN(IsEnabled, GLboolean, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(cap, GLenum))
GL_PROTO_INVOKE(IsEnabled, GLboolean, (cap))
GL_PROTO_END()

GL_PROTO_BEGIN(Finish, void, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(Finish, void, ())
GL_PROTO_END()

GL_PROTO_BEGIN(Flush, void, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(Flush, void, ())
GL_PROTO_END()

GL_PROTO_BEGIN(Begin, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(mode, GLenum))
GL_PROTO_INVOKE(Begin, void, (mode))
GL_PROTO_END()

GL_PROTO_BEGIN(End, void, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(End, void, ())
GL_PROTO_END()

GL_PROTO_BEGIN(Color4f, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(red, GLfloat),
GL_PROTO_ARG(green, GLfloat),
GL_PROTO_ARG(blue, GLfloat),
GL_PROTO_ARG(alpha, GLfloat))
GL_PROTO_INVOKE(Color4f, void, (red, green, blue, alpha))
GL_PROTO_END()

GL_PROTO_BEGIN(Clear, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(mask, GLbitfield))
GL_PROTO_INVOKE(Clear, void, (mask))
GL_PROTO_END()

GL_PROTO_BEGIN(ClearColor, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(red, GLclampf),
GL_PROTO_ARG(green, GLclampf),
GL_PROTO_ARG(blue, GLclampf),
GL_PROTO_ARG(alpha, GLclampf))
GL_PROTO_INVOKE(ClearColor, void, (red, green, blue, alpha))
GL_PROTO_END()

GL_PROTO_BEGIN(PushMatrix, void, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(PushMatrix, void, ())
GL_PROTO_END()

GL_PROTO_BEGIN(PopMatrix, void, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(PopMatrix, void, ())
GL_PROTO_END()

GL_PROTO_BEGIN(LoadIdentity, void, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(LoadIdentity, void, ())
GL_PROTO_END()

GL_PROTO_BEGIN(MatrixMode, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(mode, GLenum))
GL_PROTO_INVOKE(MatrixMode, void, (mode))
GL_PROTO_END()

GL_PROTO_BEGIN(PushAttrib, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(mask, GLbitfield))
GL_PROTO_INVOKE(PushAttrib, void, (mask))
GL_PROTO_END()

GL_PROTO_BEGIN(PopAttrib, void, CORE_1_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(PopAttrib, void, ())
GL_PROTO_END()

GL_PROTO_BEGIN(Viewport, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(x, GLint),
GL_PROTO_ARG(y, GLint),
GL_PROTO_ARG(width, GLsizei),
GL_PROTO_ARG(height, GLsizei))
GL_PROTO_INVOKE(Viewport, void, (x, y, width, height))
GL_PROTO_END()

GL_PROTO_BEGIN(Frustum, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(left, GLdouble),
GL_PROTO_ARG(right, GLdouble),
GL_PROTO_ARG(bottom, GLdouble),
GL_PROTO_ARG(top, GLdouble),
GL_PROTO_ARG(zNear, GLdouble),
GL_PROTO_ARG(zFar, GLdouble))
GL_PROTO_INVOKE(Frustum, void, (left, right, bottom, top, zNear, zFar))
GL_PROTO_END()

GL_PROTO_BEGIN(Scalef, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(x, GLfloat),
GL_PROTO_ARG(y, GLfloat),
GL_PROTO_ARG(z, GLfloat))
GL_PROTO_INVOKE(Scalef, void, (x, y, z))
GL_PROTO_END()

GL_PROTO_BEGIN(Translatef, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(x, GLfloat),
GL_PROTO_ARG(y, GLfloat),
GL_PROTO_ARG(z, GLfloat))
GL_PROTO_INVOKE(Translatef, void, (x, y, z))
GL_PROTO_END()

GL_PROTO_BEGIN(EnableClientState, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(array, GLenum))
GL_PROTO_INVOKE(EnableClientState, void, (array))
GL_PROTO_END()

GL_PROTO_BEGIN(DisableClientState, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(array, GLenum))
GL_PROTO_INVOKE(DisableClientState, void, (array))
GL_PROTO_END()

GL_PROTO_BEGIN(TexCoordPointer, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(size, GLint),
GL_PROTO_ARG(type, GLenum),
GL_PROTO_ARG(stride, GLsizei),
GL_PROTO_ARG(pointer, const GLvoid *))
GL_PROTO_INVOKE(TexCoordPointer, void, (size, type, stride, pointer))
GL_PROTO_END()

GL_PROTO_BEGIN(VertexPointer, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(size, GLint),
GL_PROTO_ARG(type, GLenum),
GL_PROTO_ARG(stride, GLsizei),
GL_PROTO_ARG(pointer, const GLvoid *))
GL_PROTO_INVOKE(VertexPointer, void, (size, type, stride, pointer))
GL_PROTO_END()

GL_PROTO_BEGIN(EnableVertexAttribArray, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(index, GLuint))
GL_PROTO_INVOKE(EnableVertexAttribArray, void, (index))
GL_PROTO_END()

GL_PROTO_BEGIN(DisableVertexAttribArray, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(index, GLuint))
GL_PROTO_INVOKE(DisableVertexAttribArray, void, (index))
GL_PROTO_END()

GL_PROTO_BEGIN(GetVertexAttribPointerv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(index, GLuint),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(pointer, GLvoid **))
GL_PROTO_INVOKE(GetVertexAttribPointerv, void, (index, pname, pointer))
GL_PROTO_END()

GL_PROTO_BEGIN(VertexAttribPointer, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(index, GLuint),
GL_PROTO_ARG(size, GLint),
GL_PROTO_ARG(type, GLenum),
GL_PROTO_ARG(normalized, GLboolean),
GL_PROTO_ARG(stride, GLsizei),
GL_PROTO_ARG(pointer, const GLvoid *))
GL_PROTO_INVOKE(VertexAttribPointer, void, (index, size, type, normalized, stride, pointer))
GL_PROTO_END()

GL_PROTO_BEGIN(DrawArrays, void, CORE_1_1)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(mode, GLenum),
GL_PROTO_ARG(first, GLint),
GL_PROTO_ARG(count, GLsizei))
GL_PROTO_INVOKE(DrawArrays, void, (mode, first, count))
GL_PROTO_END()

GL_PROTO_BEGIN(GenTextures, void, CORE_1_1)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(n, GLsizei),
GL_PROTO_ARG(textures, GLuint *))
GL_PROTO_INVOKE(GenTextures, void, (n, textures))
GL_PROTO_END()

GL_PROTO_BEGIN(DeleteTextures, void, CORE_1_1)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(n, GLsizei),
GL_PROTO_ARG(textures, const GLuint *))
GL_PROTO_INVOKE(DeleteTextures, void, (n, textures))
GL_PROTO_END()

GL_PROTO_BEGIN(BindTexture, void, CORE_1_1)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(texture, GLuint))
GL_PROTO_INVOKE(BindTexture, void, (target, texture))
GL_PROTO_END()

GL_PROTO_BEGIN(ActiveTexture, void, CORE_1_3)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(texture, GLenum))
GL_PROTO_INVOKE(ActiveTexture, void, (texture))
GL_PROTO_END()

GL_PROTO_BEGIN(GetTexLevelParameteriv, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(level, GLint),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(params, GLint *))
GL_PROTO_INVOKE(GetTexLevelParameteriv, void, (target, level, pname, params))
GL_PROTO_END()

GL_PROTO_BEGIN(TexParameterf, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(param, GLfloat))
GL_PROTO_INVOKE(TexParameterf, void, (target, pname, param))
GL_PROTO_END()

GL_PROTO_BEGIN(TexParameterfv, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(params, const GLfloat *))
GL_PROTO_INVOKE(TexParameterfv, void, (target, pname, params))
GL_PROTO_END()

GL_PROTO_BEGIN(TexParameteri, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(param, GLint))
GL_PROTO_INVOKE(TexParameteri, void, (target, pname, param))
GL_PROTO_END()

GL_PROTO_BEGIN(TexParameteriv, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(params, const GLint *))
GL_PROTO_INVOKE(TexParameteriv, void, (target, pname, params))
GL_PROTO_END()

GL_PROTO_BEGIN(TexImage2D, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(level, GLint),
GL_PROTO_ARG(internalformat, GLint),
GL_PROTO_ARG(width, GLsizei),
GL_PROTO_ARG(height, GLsizei),
GL_PROTO_ARG(border, GLint),
GL_PROTO_ARG(format, GLenum),
GL_PROTO_ARG(type, GLenum),
GL_PROTO_ARG(pixels, const GLvoid *))
GL_PROTO_INVOKE(TexImage2D, void, (target, level, internalformat, width, height, border, format, type, pixels))
GL_PROTO_END()

GL_PROTO_BEGIN(TexSubImage2D, void, CORE_1_1)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(level, GLint),
GL_PROTO_ARG(xoffset, GLint),
GL_PROTO_ARG(yoffset, GLint),
GL_PROTO_ARG(width, GLsizei),
GL_PROTO_ARG(height, GLsizei),
GL_PROTO_ARG(format, GLenum),
GL_PROTO_ARG(type, GLenum),
GL_PROTO_ARG(UNUSED, GLuint),
GL_PROTO_ARG(pixels, const GLvoid *))
GL_PROTO_INVOKE(TexSubImage2D, void, (target, level, xoffset, yoffset, width, height, format, type, UNUSED, pixels))
GL_PROTO_END()

GL_PROTO_BEGIN(PixelStoref, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(param, GLfloat))
GL_PROTO_INVOKE(PixelStoref, void, (pname, param))
GL_PROTO_END()

GL_PROTO_BEGIN(PixelStorei, void, CORE_1_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(param, GLint))
GL_PROTO_INVOKE(PixelStorei, void, (pname, param))
GL_PROTO_END()

GL_PROTO_BEGIN(CreateShader, GLuint, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(type, GLenum))
GL_PROTO_INVOKE(CreateShader, GLuint, (type))
GL_PROTO_END()

GL_PROTO_BEGIN(DeleteShader, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint))
GL_PROTO_INVOKE(DeleteShader, void, (program))
GL_PROTO_END()

GL_PROTO_BEGIN(ShaderSource, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(shader, GLuint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(string, const GLchar * const *),
GL_PROTO_ARG(length, const GLint *))
GL_PROTO_INVOKE(ShaderSource, void, (shader, count, string, length))
GL_PROTO_END()

GL_PROTO_BEGIN(CompileShader, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(shader, GLuint))
GL_PROTO_INVOKE(CompileShader, void, (shader))
GL_PROTO_END()

GL_PROTO_BEGIN(GetShaderiv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(shader, GLuint),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(params, GLint *))
GL_PROTO_INVOKE(GetShaderiv, void, (shader, pname, params))
GL_PROTO_END()

GL_PROTO_BEGIN(GetShaderInfoLog, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(shader, GLuint),
GL_PROTO_ARG(bufSize, GLsizei),
GL_PROTO_ARG(length, GLsizei *),
GL_PROTO_ARG(infoLog, GLchar *))
GL_PROTO_INVOKE(GetShaderInfoLog, void, (shader, bufSize, length, infoLog))
GL_PROTO_END()

GL_PROTO_BEGIN(CreateProgram, GLuint, CORE_2_0)
GL_PROTO_ARG_LIST()
GL_PROTO_INVOKE(CreateProgram, GLuint, ())
GL_PROTO_END()

GL_PROTO_BEGIN(DeleteProgram, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint))
GL_PROTO_INVOKE(DeleteProgram, void, (program))
GL_PROTO_END()

GL_PROTO_BEGIN(AttachShader, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint),
GL_PROTO_ARG(shader, GLuint))
GL_PROTO_INVOKE(AttachShader, void, (program, shader))
GL_PROTO_END()

GL_PROTO_BEGIN(LinkProgram, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint))
GL_PROTO_INVOKE(LinkProgram, void, (program))
GL_PROTO_END()

GL_PROTO_BEGIN(UseProgram, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint))
GL_PROTO_INVOKE(UseProgram, void, (program))
GL_PROTO_END()

GL_PROTO_BEGIN(GetProgramiv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint),
GL_PROTO_ARG(pname, GLenum),
GL_PROTO_ARG(params, GLint *))
GL_PROTO_INVOKE(GetProgramiv, void, (program, pname, params))
GL_PROTO_END()

GL_PROTO_BEGIN(GetProgramInfoLog, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint),
GL_PROTO_ARG(bufSize, GLsizei),
GL_PROTO_ARG(length, GLsizei *),
GL_PROTO_ARG(infoLog, GLchar *))
GL_PROTO_INVOKE(GetProgramInfoLog, void, (program, bufSize, length, infoLog))
GL_PROTO_END()

GL_PROTO_BEGIN(BindAttribLocation, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint),
GL_PROTO_ARG(index, GLuint),
GL_PROTO_ARG(name, const GLchar *))
GL_PROTO_INVOKE(BindAttribLocation, void, (program, index, name))
GL_PROTO_END()

GL_PROTO_BEGIN(GetUniformLocation, GLint, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(program, GLuint),
GL_PROTO_ARG(name, const GLchar *))
GL_PROTO_INVOKE(GetUniformLocation, GLint, (program, name))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform1f, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLfloat))
GL_PROTO_INVOKE(Uniform1f, void, (location, v0))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform1fv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLfloat *))
GL_PROTO_INVOKE(Uniform1fv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform1i, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLint))
GL_PROTO_INVOKE(Uniform1i, void, (location, v0))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform1iv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLint *))
GL_PROTO_INVOKE(Uniform1iv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform2f, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLfloat),
GL_PROTO_ARG(v1, GLfloat))
GL_PROTO_INVOKE(Uniform2f, void, (location, v0, v1))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform2fv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLfloat *))
GL_PROTO_INVOKE(Uniform2fv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform2i, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLint),
GL_PROTO_ARG(v1, GLint))
GL_PROTO_INVOKE(Uniform2i, void, (location, v0, v1))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform2iv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLint *))
GL_PROTO_INVOKE(Uniform2iv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform3f, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLfloat),
GL_PROTO_ARG(v1, GLfloat),
GL_PROTO_ARG(v2, GLfloat))
GL_PROTO_INVOKE(Uniform3f, void, (location, v0, v1, v2))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform3fv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLfloat *))
GL_PROTO_INVOKE(Uniform3fv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform3i, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLint),
GL_PROTO_ARG(v1, GLint),
GL_PROTO_ARG(v2, GLint))
GL_PROTO_INVOKE(Uniform3i, void, (location, v0, v1, v2))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform3iv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLint *))
GL_PROTO_INVOKE(Uniform3iv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform4f, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLfloat),
GL_PROTO_ARG(v1, GLfloat),
GL_PROTO_ARG(v2, GLfloat),
GL_PROTO_ARG(v3, GLfloat))
GL_PROTO_INVOKE(Uniform4f, void, (location, v0, v1, v2, v3))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform4fv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLfloat *))
GL_PROTO_INVOKE(Uniform4fv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform4i, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(v0, GLint),
GL_PROTO_ARG(v1, GLint),
GL_PROTO_ARG(v2, GLint),
GL_PROTO_ARG(v3, GLint))
GL_PROTO_INVOKE(Uniform4i, void, (location, v0, v1, v2, v3))
GL_PROTO_END()

GL_PROTO_BEGIN(Uniform4iv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(value, const GLint *))
GL_PROTO_INVOKE(Uniform4iv, void, (location, count, value))
GL_PROTO_END()

GL_PROTO_BEGIN(UniformMatrix2fv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(transpose, GLboolean),
GL_PROTO_ARG(value, const GLfloat *))
GL_PROTO_INVOKE(UniformMatrix2fv, void, (location, count, transpose, value))
GL_PROTO_END()

GL_PROTO_BEGIN(UniformMatrix3fv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(transpose, GLboolean),
GL_PROTO_ARG(value, const GLfloat *))
GL_PROTO_INVOKE(UniformMatrix3fv, void, (location, count, transpose, value))
GL_PROTO_END()

GL_PROTO_BEGIN(UniformMatrix4fv, void, CORE_2_0)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(location, GLint),
GL_PROTO_ARG(count, GLsizei),
GL_PROTO_ARG(transpose, GLboolean),
GL_PROTO_ARG(value, const GLfloat *))
GL_PROTO_INVOKE(UniformMatrix4fv, void, (location, count, transpose, value))
GL_PROTO_END()

GL_PROTO_BEGIN(EGLImageTargetTexture2DOES, void, OES_EGL_image)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(image, void *))
GL_PROTO_INVOKE(EGLImageTargetTexture2DOES, void, (target, image))
GL_PROTO_END()

GL_PROTO_BEGIN(EGLImageTargetRenderbufferStorageOES, void, OES_EGL_image)
GL_PROTO_ARG_LIST(
GL_PROTO_ARG(target, GLenum),
GL_PROTO_ARG(image, void *))
GL_PROTO_INVOKE(EGLImageTargetRenderbufferStorageOES, void, (target, image))
GL_PROTO_END()

GL_DEFINE_EXTENSION(CORE_1_0)
GL_DEFINE_EXTENSION(CORE_1_1)
GL_DEFINE_EXTENSION(CORE_1_3)
GL_DEFINE_EXTENSION(CORE_2_0)
GL_DEFINE_EXTENSION(OES_EGL_image)

#undef EGL_PROTO_BEGIN
#undef EGL_PROTO_BEGIN_I
#undef EGL_PROTO_ARG_LIST
#undef EGL_PROTO_ARG
#undef EGL_PROTO_INVOKE
#undef EGL_PROTO_INVOKE_I
#undef EGL_PROTO_END
#undef EGL_DEFINE_EXTENSION
#undef EGL_DEFINE_EXTENSION_I

#undef GL_PROTO_BEGIN
#undef GL_PROTO_BEGIN_I
#undef GL_PROTO_ARG_LIST
#undef GL_PROTO_ARG
#undef GL_PROTO_INVOKE
#undef GL_PROTO_INVOKE_I
#undef GL_PROTO_END
#undef GL_DEFINE_EXTENSION
#undef GL_DEFINE_EXTENSION_I

#undef GL_PROTO_GEN_CONCAT5
#undef GL_PROTO_GEN_CONCAT5_I
#undef GL_PROTO_GEN_CONCAT4
#undef GL_PROTO_GEN_CONCAT4_I
#undef GL_PROTO_GEN_CONCAT3
#undef GL_PROTO_GEN_CONCAT3_I
#undef GL_PROTO_GEN_CONCAT2
#undef GL_PROTO_GEN_CONCAT2_I
#undef GL_PROTO_GEN_CONCAT
#undef GL_PROTO_GEN_STRING
#undef GL_PROTO_GEN_STRING_I
