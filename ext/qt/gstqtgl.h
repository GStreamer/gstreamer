/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

/* qt uses the same trick as us to typedef GLsync on gles2 but to a different
 * type which confuses the preprocessor.  As it's never actually used by qt
 * public headers, define it to something else to avoid redefinition
 * warnings/errors */

#include <gst/gl/gstglconfig.h>
#include <gst/gl/gstglfuncs.h>
#include <QtCore/qglobal.h>
#if (QT_VERSION >= QT_VERSION_CHECK(5, 9, 0))
#include <QtGui/qtgui-config.h>
#endif

/* The glext.h guard was renamed in 2018, but some software which
 * includes their own copy of the GL headers (such as qt) might have
 * older version which use the old guard. This would result in the
 * header being included again (and symbols redefined).
 *
 * To avoid this, we define the "old" guard if the "new" guard is
 * defined.*/
#if GST_GL_HAVE_OPENGL
#ifdef __gl_glext_h_
#ifndef __glext_h_
#define __glext_h_ 1
#endif
#endif
#endif

#if defined(QT_OPENGL_ES_2)
#define GLsync gst_qt_GLsync
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#undef GLsync
#endif /* defined(QT_OPENGL_ES_2) */
