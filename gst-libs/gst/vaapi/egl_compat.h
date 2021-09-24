/*
 * egl_compat.h - EGL compatiliby layer
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

#ifndef EGL_COMPAT_H
#define EGL_COMPAT_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "ogl_compat.h"

#ifndef GL_OES_EGL_image
#define GL_OES_EGL_image 1
typedef void *GLeglImageOES;
typedef void (*PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)(GLenum target,
    GLeglImageOES image);
typedef void (*PFNGLEGLIMAGETARGETRENDERBUFFERSTORAGEOESPROC)(GLenum target,
    GLeglImageOES image);
#endif /* GL_OES_EGL_image */

#endif /* EGL_COMPAT_H */
