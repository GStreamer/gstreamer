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
 * @title: GstOpengl
 *
 * Cross-platform OpenGL plugin.
 *
 * ## Debugging
 *
 * ## Examples
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

#include "gstglimagesink.h"
#include "gstgluploadelement.h"
#include "gstgldownloadelement.h"
#include "gstglcolorconvertelement.h"
#include "gstglcolorbalance.h"
#include "gstglfilterbin.h"
#include "gstglsinkbin.h"
#include "gstglsrcbin.h"
#include "gstglmixerbin.h"

#include "gstglfiltercube.h"
#include "gstgleffects.h"
#include "gstglcolorscale.h"
#include "gstglvideomixer.h"
#include "gstglfiltershader.h"
#include "gstglfilterapp.h"
#include "gstglstereosplit.h"
#include "gstglstereomix.h"
#include "gstglviewconvert.h"
#include "gstgltestsrc.h"
#include "gstgldeinterlace.h"

#if HAVE_GRAPHENE
#include "gstgltransformation.h"
#include "gstglvideoflip.h"
#endif
#if HAVE_JPEG
#if HAVE_PNG
#include "gstgloverlay.h"
#endif /* HAVE_PNG */
#endif /* HAVE_JPEG */

#if GST_GL_HAVE_OPENGL
#include "gstglfilterglass.h"
/* #include "gstglfilterreflectedscreen.h" */
#include "gstglmosaic.h"
#if HAVE_PNG
#include "gstgldifferencematte.h"
/* #include "gstglbumper.h" */
#endif /* HAVE_PNG */
#endif /* GST_GL_HAVE_OPENGL */

#if GST_GL_HAVE_WINDOW_COCOA
/* avoid including Cocoa/CoreFoundation from a C file... */
extern GType gst_ca_opengl_layer_sink_bin_get_type (void);
#endif

#if GST_GL_HAVE_WINDOW_DISPMANX
extern void bcm_host_init (void);
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

#if GST_GL_HAVE_WINDOW_DISPMANX
  GST_DEBUG ("Initialize BCM host");
  bcm_host_init ();
#endif

#if GST_GL_HAVE_WINDOW_X11
  if (g_getenv ("GST_GL_XINITTHREADS"))
    XInitThreads ();
#endif

  if (!gst_element_register (plugin, "glimagesink",
          GST_RANK_SECONDARY, gst_gl_image_sink_bin_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glimagesinkelement",
          GST_RANK_NONE, gst_glimage_sink_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glupload",
          GST_RANK_NONE, GST_TYPE_GL_UPLOAD_ELEMENT)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "gldownload",
          GST_RANK_NONE, GST_TYPE_GL_DOWNLOAD_ELEMENT)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glcolorconvert",
          GST_RANK_NONE, GST_TYPE_GL_COLOR_CONVERT_ELEMENT)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glcolorbalance",
          GST_RANK_NONE, GST_TYPE_GL_COLOR_BALANCE)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterbin",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_BIN)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glsinkbin",
          GST_RANK_NONE, GST_TYPE_GL_SINK_BIN)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glsrcbin",
          GST_RANK_NONE, GST_TYPE_GL_SRC_BIN)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glmixerbin",
          GST_RANK_NONE, GST_TYPE_GL_MIXER_BIN)) {
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

  if (!gst_element_register (plugin, "glvideoflip",
          GST_RANK_NONE, GST_TYPE_GL_VIDEO_FLIP)) {
    return FALSE;
  }
#endif

  if (!gst_gl_effects_register_filters (plugin, GST_RANK_NONE)) {
    return FALSE;
  };

  if (!gst_element_register (plugin, "glcolorscale",
          GST_RANK_NONE, GST_TYPE_GL_COLORSCALE)) {
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

  if (!gst_element_register (plugin, "glshader",
          GST_RANK_NONE, gst_gl_filtershader_get_type ())) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glfilterapp",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_APP)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glviewconvert",
          GST_RANK_NONE, GST_TYPE_GL_VIEW_CONVERT_ELEMENT)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glstereosplit",
          GST_RANK_NONE, GST_TYPE_GL_STEREOSPLIT)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "glstereomix",
          GST_RANK_NONE, GST_TYPE_GL_STEREO_MIX)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "gltestsrc",
          GST_RANK_NONE, GST_TYPE_GL_TEST_SRC)) {
    return FALSE;
  }

  if (!gst_element_register (plugin, "gldeinterlace",
          GST_RANK_NONE, GST_TYPE_GL_DEINTERLACE)) {
    return FALSE;
  }
#if HAVE_JPEG
#if HAVE_PNG
  if (!gst_element_register (plugin, "gloverlay",
          GST_RANK_NONE, gst_gl_overlay_get_type ())) {
    return FALSE;
  }
#endif /* HAVE_PNG */
#endif /* HAVE_JPEG */
#if GST_GL_HAVE_OPENGL
  if (!gst_element_register (plugin, "glfilterglass",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_GLASS)) {
    return FALSE;
  }
#if 0
  if (!gst_element_register (plugin, "glfilterreflectedscreen",
          GST_RANK_NONE, GST_TYPE_GL_FILTER_REFLECTED_SCREEN)) {
    return FALSE;
  }
#endif
  if (!gst_element_register (plugin, "glmosaic",
          GST_RANK_NONE, GST_TYPE_GL_MOSAIC)) {
    return FALSE;
  }
#if HAVE_PNG
  if (!gst_element_register (plugin, "gldifferencematte",
          GST_RANK_NONE, gst_gl_differencematte_get_type ())) {
    return FALSE;
  }
#if 0
  if (!gst_element_register (plugin, "glbumper",
          GST_RANK_NONE, gst_gl_bumper_get_type ())) {
    return FALSE;
  }
#endif
#endif /* HAVE_PNG */
#endif /* GST_GL_HAVE_OPENGL */
#if GST_GL_HAVE_WINDOW_COCOA
  if (!gst_element_register (plugin, "caopengllayersink",
          GST_RANK_NONE, gst_ca_opengl_layer_sink_bin_get_type ())) {
    return FALSE;
  }
#endif

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    opengl,
    "OpenGL plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
