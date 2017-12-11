/*
 * GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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

/**
 * SECTION:plugin-openglmixers
 * @title: OpenGL Mixers
 *
 * Cross-platform OpenGL mixer plugin.
 *
 * ## Debugging
 *
 * ## Examples
 * FIXME: update with a mixer example
 * |[
 * gst-launch-1.0 --gst-debug=gldisplay:3 videotestsrc ! glimagesink
 * ]| A debugging pipeline.
  |[
 * GST_DEBUG=gl*:6 gst-launch-1.0 videotestsrc ! glimagesink
 * ]| A debugging pipelines related to shaders.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglmixerbin.h"
#include "gstglvideomixer.h"
#include "gstglstereomix.h"

#if GST_GL_HAVE_OPENGL
#include "gstglmosaic.h"
#endif /* GST_GL_HAVE_OPENGL */

#if GST_GL_HAVE_WINDOW_DISPMANX
extern void bcm_host_init (void);
#endif

#if GST_GL_HAVE_WINDOW_X11
#include <X11/Xlib.h>
#endif

GST_DEBUG_CATEGORY_STATIC (glmixers_debug);
#define GST_CAT_DEFAULT glmixers_debug

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (glmixers_debug, "openglmixers", 0, "OpenGL Mixers");

#if GST_GL_HAVE_WINDOW_DISPMANX
  GST_DEBUG ("Initialize BCM host");
  bcm_host_init ();
#endif

#if GST_GL_HAVE_WINDOW_X11
  if (g_getenv ("GST_GL_XINITTHREADS"))
    XInitThreads ();
#endif

  if (!gst_element_register (plugin, "glmixerbin",
          GST_RANK_NONE, GST_TYPE_GL_MIXER_BIN)) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "glvideomixer",
          GST_RANK_NONE, gst_gl_video_mixer_bin_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glvideomixerelement",
          GST_RANK_NONE, gst_gl_video_mixer_get_type ())) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "glstereomix",
          GST_RANK_NONE, GST_TYPE_GL_STEREO_MIX)) {
    return FALSE;
  }
#if GST_GL_HAVE_OPENGL
  if (!gst_element_register (plugin, "glmosaic",
          GST_RANK_NONE, GST_TYPE_GL_MOSAIC)) {
    return FALSE;
  }
#endif /* GST_GL_HAVE_OPENGL */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    openglmixers,
    "OpenGL mixers",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
