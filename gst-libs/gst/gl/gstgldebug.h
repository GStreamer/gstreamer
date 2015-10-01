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

#ifndef __GST_GL_DEBUG_H__
#define __GST_GL_DEBUG_H__

#include <gst/gl/gl.h>

G_BEGIN_DECLS

#if !defined(GST_DISABLE_GST_DEBUG)
void gst_gl_insert_debug_marker (GstGLContext * context,
                                 const gchar * format, ...) G_GNUC_PRINTF (2, 3);
#else /* GST_DISABLE_GST_DEBUG */
#if G_HAVE_ISO_VARARGS
#define gst_gl_insert_debug_marker(...) G_STMT_START{ }G_STMT_END
#else /* G_HAVE_ISO_VARARGS */
#if G_HAVE_GNUC_VARARGS
#define gst_gl_insert_debug_marker(args...) G_STMT_START{ }G_STMT_END
#else /* G_HAVE_GNUC_VARARGS */
static inline void
gst_gl_insert_debug_marker (GstGLContext * context, const gchar * format, ...)
{
}
#endif /* G_HAVE_GNUC_VARARGS */
#endif /* G_HAVE_ISO_VARARGS */
#endif /* GST_DISABLE_GST_DEBUG */

G_END_DECLS

#endif /* __GST_GL_DEBUG_H__ */
