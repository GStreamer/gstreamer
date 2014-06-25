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
 * SECTION:plugin-opengl
 *
 * Cross-platform OpenGL plugin.
 * <refsect2>
 * <title>Debugging</title>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-0.10 --gst-debug=gldisplay:3 videotestsrc ! glimagesink
 * ]| A debugging pipeline.
  |[
 * GST_GL_SHADER_DEBUG=1 gst-launch-0.10 videotestsrc ! glimagesink
 * ]| A debugging pipelines related to shaders.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglimagesink.h"

#include "gstglfiltercube.h"
#include "gstgleffects.h"
#include "gstglcolorscale.h"
#include "gstglvideomixer.h"
#include "gstglfiltershader.h"
#if HAVE_GRAPHENE
#include "gstgltransformation.h"
#endif

#if GST_GL_HAVE_OPENGL
#include "gstgltestsrc.h"
#include "gstglfilterlaplacian.h"
#include "gstglfilterglass.h"
#include "gstglfilterapp.h"
#include "gstglfilterblur.h"
#include "gstglfilterreflectedscreen.h"
#include "gstglfiltersobel.h"
#include "gstgldeinterlace.h"
#include "gstglmosaic.h"
#if HAVE_PNG
#include "gstgldifferencematte.h"
#include "gstglbumper.h"
#if HAVE_JPEG
#include "gstgloverlay.h"
#endif /* HAVE_JPEG */
#endif /* HAVE_PNG */
#endif /* GST_GL_HAVE_OPENGL */

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#endif

#if GST_GL_HAVE_WINDOW_X11
#include <X11/Xlib.h>
#endif

#define GST_CAT_DEFAULT gst_gl_gstgl_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Register filters that make up the gstgl plugin */
static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_gl_gstgl_debug, "gstopengl", 0, "gstopengl");

#ifdef USE_EGL_RPI
  GST_DEBUG ("Initialize BCM host");
  bcm_host_init ();
#endif

#if GST_GL_HAVE_WINDOW_X11
  if (g_getenv ("GST_GL_XINITTHREADS"))
    XInitThreads ();
#endif

  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_SECONDARY, GST_TYPE_GLIMAGE_SINK)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfiltercube",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_CUBE)) {
    return FALSE;
  }
#if HAVE_GRAPHENE
  if (!gst_element_register (plugin, "gltransformation",
          GST_RANK_NONE, GST_TYPE_GL_TRANSFORMATION)) {
    return FALSE;
  }
#endif
  if (!gst_element_register (plugin, "gleffects",
          GST_RANK_NONE, gst_gl_effects_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glcolorscale",
          GST_RANK_NONE, GST_TYPE_GL_COLORSCALE)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glvideomixer",
          GST_RANK_NONE, GST_TYPE_GL_VIDEO_MIXER)) {
    return FALSE;
  }
  if (!gst_element_register (plugin, "glshader",
          GST_RANK_NONE, gst_gl_filtershader_get_type ())) {
    return FALSE;
  }
#if GST_GL_HAVE_OPENGL
  if (!gst_element_register (plugin, "gltestsrc",
          GST_RANK_NONE, GST_TYPE_GL_TEST_SRC)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterblur",
          GST_RANK_NONE, gst_gl_filterblur_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfiltersobel",
          GST_RANK_NONE, gst_gl_filtersobel_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterlaplacian",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_LAPLACIAN)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterglass",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_GLASS)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterapp",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_APP)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterreflectedscreen",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_REFLECTED_SCREEN)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "gldeinterlace",
          GST_RANK_NONE, GST_TYPE_GL_DEINTERLACE)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glmosaic",
          GST_RANK_NONE, GST_TYPE_GL_MOSAIC)) {
    return FALSE;
  }
#if HAVE_PNG
  if (!gst_element_register (plugin, "gldifferencematte",
          GST_RANK_NONE, gst_gl_differencematte_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glbumper",
          GST_RANK_NONE, gst_gl_bumper_get_type ())) {
    return FALSE;
  }
#if HAVE_JPEG
  if (!gst_element_register (plugin, "gloverlay",
          GST_RANK_NONE, gst_gl_overlay_get_type ())) {
    return FALSE;
  }
#endif /* HAVE_JPEG */
#endif /* HAVE_PNG */
#endif /* GST_GL_HAVE_OPENGL */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    opengl,
    "OpenGL plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
