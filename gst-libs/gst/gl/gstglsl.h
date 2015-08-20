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

#ifndef __GST_GLSL_H__
#define __GST_GLSL_H__

#include <gst/gl/gstgl_fwd.h>

G_BEGIN_DECLS

GQuark gst_glsl_error_quark (void);
#define GST_GLSL_ERROR (gst_glsl_error_quark ())

typedef enum {
  GST_GLSL_ERROR_COMPILE,
  GST_GLSL_ERROR_LINK,
  GST_GLSL_ERROR_PROGRAM,
} GstGLSLError;

typedef enum
{
  GST_GLSL_VERSION_NONE = 0,

  GST_GLSL_VERSION_100 = 100, /* ES */
  GST_GLSL_VERSION_110 = 110, /* GL */
  GST_GLSL_VERSION_120 = 120, /* GL */
  GST_GLSL_VERSION_130 = 130, /* GL */
  GST_GLSL_VERSION_140 = 140, /* GL */
  GST_GLSL_VERSION_150 = 150, /* GL */
  GST_GLSL_VERSION_300 = 300, /* ES */
  GST_GLSL_VERSION_310 = 310, /* ES */
  GST_GLSL_VERSION_320 = 320, /* ES */
  GST_GLSL_VERSION_330 = 330, /* GL */
  GST_GLSL_VERSION_400 = 400, /* GL */
  GST_GLSL_VERSION_410 = 410, /* GL */
  GST_GLSL_VERSION_420 = 420, /* GL */
  GST_GLSL_VERSION_430 = 430, /* GL */
  GST_GLSL_VERSION_440 = 440, /* GL */
  GST_GLSL_VERSION_450 = 450, /* GL */

  GST_GLSL_VERSION_ANY = -1,
} GstGLSLVersion;

typedef enum
{
  /* XXX: maybe make GstGLAPI instead */
  GST_GLSL_PROFILE_NONE = 0,

  GST_GLSL_PROFILE_ES = (1 << 0),
  GST_GLSL_PROFILE_CORE = (1 << 1),
  GST_GLSL_PROFILE_COMPATIBILITY = (1 << 2),

  GST_GLSL_PROFILE_ANY = -1,
} GstGLSLProfile;

GstGLSLVersion gst_glsl_version_from_string         (const gchar * string);
const gchar *  gst_glsl_version_to_string           (GstGLSLVersion version);

GstGLSLProfile gst_glsl_profile_from_string         (const gchar * string);
const gchar *  gst_glsl_profile_to_string           (GstGLSLProfile profile);

gchar *        gst_glsl_version_profile_to_string   (GstGLSLVersion version,
                                                     GstGLSLProfile profile);
gboolean       gst_glsl_version_profile_from_string (const gchar * string,
                                                     GstGLSLVersion * version,
                                                     GstGLSLProfile * profile);

gboolean       gst_glsl_string_get_version_profile  (const gchar *s,
                                                     GstGLSLVersion * version,
                                                     GstGLSLProfile * profile);

GstGLSLVersion gst_gl_version_to_glsl_version       (GstGLAPI gl_api, gint maj, gint min);
gboolean       gst_gl_context_supports_glsl_profile_version (GstGLContext * context,
                                                             GstGLSLVersion version,
                                                             GstGLSLProfile profile);

G_END_DECLS

#endif /* __GST_GLSL_H__ */
