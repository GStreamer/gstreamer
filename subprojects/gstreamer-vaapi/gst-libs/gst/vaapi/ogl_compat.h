/*
 * ogl_compat.h - OpenGL compatiliby layer
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

#ifndef OGL_COMPAT_H
#define OGL_COMPAT_H

typedef void                    GLvoid;
typedef char                    GLchar;
typedef unsigned char           GLubyte;
typedef unsigned char           GLboolean;
typedef int                     GLint;
typedef unsigned int            GLuint;
typedef int                     GLsizei;
typedef float                   GLfloat;
typedef double                  GLdouble;
typedef GLuint                  GLenum;
typedef GLuint                  GLbitfield;
typedef GLfloat                 GLclampf;

#define GL_VENDOR               0x1F00
#define GL_RENDERER             0x1F01
#define GL_VERSION              0x1F02
#define GL_EXTENSIONS           0x1F03
#define GL_NEAREST              0x2600
#define GL_LINEAR               0x2601

#define GL_DEPTH_BUFFER_BIT     0x00000100
#define GL_COLOR_BUFFER_BIT     0x00004000
#define GL_FALSE                0
#define GL_TRUE                 1
#define GL_NONE                 0

#define GL_BLEND                0x0BE2
#define GL_DEPTH_TEST           0x0B71

#define GL_TEXTURE0             0x84C0
#define GL_TEXTURE1             0x84C1
#define GL_TEXTURE2             0x84C2
#define GL_TEXTURE3             0x84C3
#define GL_TEXTURE_2D           0x0DE1
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#define GL_TEXTURE_MAG_FILTER   0x2800
#define GL_TEXTURE_MIN_FILTER   0x2801
#define GL_TEXTURE_WRAP_S       0x2802
#define GL_TEXTURE_WRAP_T       0x2803

#define GL_UNPACK_ALIGNMENT     0x0cf5

#define GL_TRIANGLE_FAN         0x0006

#define GL_BYTE                 0x1400
#define GL_UNSIGNED_BYTE        0x1401
#define GL_SHORT                0x1402
#define GL_UNSIGNED_SHORT       0x1403
#define GL_INT                  0x1404
#define GL_UNSIGNED_INT         0x1405
#define GL_FLOAT                0x1406

#define GL_ALPHA                0x1906
#define GL_RGB                  0x1907
#define GL_RGBA                 0x1908
#define GL_LUMINANCE            0x1909
#define GL_LUMINANCE_ALPHA      0x190A

#define GL_REPEAT               0x2901
#define GL_CLAMP_TO_EDGE        0x812F

#define GL_VERTEX_ARRAY         0x8074
#define GL_TEXTURE_COORD_ARRAY  0x8078

#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84

#define GL_BGRA_EXT             0x80e1
#ifndef GL_R8
#define GL_R8                   GL_R8_EXT
#endif
#ifndef GL_RG8
#define GL_RG8                  GL_RG8_EXT
#endif

#endif /* OGL_COMPAT_H */
