/*
 * GStreamer
 * Copyright (C) 2009 Julien Isorce <julien.isorce@gmail.com>
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

/* Compatibility for OpenGL ES 2.0 */

#ifndef __GST_GL_ES2__
#define __GST_GL_ES2__

#include <glib.h>

G_BEGIN_DECLS

/* SUPPORTED */
/* FIXME: On iOS this exists but maps to an actual BGRA extension */
#ifdef __APPLE__
#ifdef GL_BGRA
#undef GL_BGRA
#endif
#endif

//FIXME:
#define GL_RGB16 GL_RGB565
#define GL_RGB8 GL_RGB
#define GL_RGBA8 GL_RGBA
#define GL_BGRA GL_RGBA
#define GL_BGR GL_RGB
#define GL_UNSIGNED_INT_8_8_8_8 GL_UNSIGNED_BYTE
#define GL_UNSIGNED_INT_8_8_8_8_REV GL_UNSIGNED_BYTE
//END FIXME

/* UNSUPPORTED */

#define GL_YCBCR_MESA 0
#define GL_UNSIGNED_SHORT_8_8_MESA 0
#define GL_UNSIGNED_SHORT_8_8_MESA 0
#define GL_UNSIGNED_SHORT_8_8_REV_MESA 0

#define GL_COLOR_ATTACHMENT1 0
#define GL_COLOR_ATTACHMENT2 0
#define GL_TEXTURE_ENV 0
#define GL_TEXTURE_ENV_MODE 0
#define GL_DEPTH24_STENCIL8 0

G_END_DECLS

#endif /* __GST_GL_ES2__ */
