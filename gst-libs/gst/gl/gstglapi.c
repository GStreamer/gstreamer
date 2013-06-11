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

#include "gstglapi.h"

gchar *
gst_gl_api_string (GstGLAPI api)
{
  GString *str = NULL;
  gchar *ret;

  if (api == GST_GL_API_NONE) {
    str = g_string_new ("none");
    return str->str;
  } else if (api == GST_GL_API_ANY) {
    str = g_string_new ("any");
    return str->str;
  }

  if (api & GST_GL_API_OPENGL) {
    str = g_string_new ("opengl");
  }
  if (api & GST_GL_API_OPENGL3) {
    if (str) {
      g_string_append (str, " opengl3");
    } else {
      str = g_string_new ("opengl3");
    }
  }
  if (api & GST_GL_API_GLES) {
    if (str) {
      g_string_append (str, " gles1");
    } else {
      str = g_string_new ("gles1");
    }
  }
  if (api & GST_GL_API_GLES2) {
    if (str) {
      g_string_append (str, " gles2");
    } else {
      str = g_string_new ("gles2");
    }
  }
  if (api & GST_GL_API_GLES3) {
    if (str) {
      g_string_append (str, " gles3");
    } else {
      str = g_string_new ("gles3");
    }
  }

  ret = g_string_free (str, FALSE);

  return ret;
}
