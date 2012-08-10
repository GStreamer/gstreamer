/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-glcolorscale
 *
 * video frame scaling and colorspace conversion.
 *
 * <refsect2>
 * <title>Scaling and Color space conversion</title>
 * <para>
 * Equivalent to glupload ! gldownload.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv" ! glcolorscale ! ximagesink
 * ]| A pipeline to test colorspace conversion.
 * FBO is required.
  |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, width=640, height=480, format=(fourcc)AYUV" ! glcolorscale ! \
 *   "video/x-raw-yuv, width=320, height=240, format=(fourcc)YV12" ! autovideosink
 * ]| A pipeline to test hardware scaling and colorspace conversion.
 * FBO and GLSL are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gst-i18n-plugin.h>
#include <gst/pbutils/pbutils.h>

#include "gstglcolorscale.h"


#define GST_CAT_DEFAULT gst_gl_colorscale_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Source pad definition */
static GstStaticPadTemplate gst_gl_colorscale_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_DOWNLOAD_VIDEO_CAPS)
    );

/* Source pad definition */
static GstStaticPadTemplate gst_gl_colorscale_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_UPLOAD_VIDEO_CAPS)
    );

/* Properties */
enum
{
  PROP_0
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_colorscale_debug, "glcolorscale", 0, "glcolorscale element");

#define gst_gl_colorscale_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLColorscale, gst_gl_colorscale,
    GST_TYPE_BIN, DEBUG_INIT);

static void gst_gl_colorscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_colorscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_colorscale_post_missing_element_message (GstGLColorscale *
    colorscale, const gchar * name);
static gboolean gst_gl_colorscale_add_elements (GstGLColorscale * colorscale);
static void gst_gl_colorscale_reset (GstGLColorscale * colorscale);
static GstStateChangeReturn gst_gl_colorscale_change_state (GstElement *
    element, GstStateChange transition);

static void
gst_gl_colorscale_class_init (GstGLColorscaleClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_colorscale_set_property;
  gobject_class->get_property = gst_gl_colorscale_get_property;

  gst_element_class_set_details_simple (element_class, "OpenGL color scale",
      "Filter/Effect", "Colorspace converter and video scaler",
      "Julien Isorce <julien.isorce@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_colorscale_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gl_colorscale_sink_pad_template));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_gl_colorscale_change_state);
}

static void
gst_gl_colorscale_init (GstGLColorscale * colorscale)
{
  GstPadTemplate *templ;

  templ = gst_static_pad_template_get (&gst_gl_colorscale_sink_pad_template);
  colorscale->sinkpad =
      gst_ghost_pad_new_no_target_from_template ("sink", templ);
  gst_element_add_pad (GST_ELEMENT_CAST (colorscale), colorscale->sinkpad);
  gst_object_unref (templ);

  templ = gst_static_pad_template_get (&gst_gl_colorscale_src_pad_template);
  colorscale->srcpad = gst_ghost_pad_new_no_target_from_template ("src", templ);
  gst_element_add_pad (GST_ELEMENT_CAST (colorscale), colorscale->srcpad);
  gst_object_unref (templ);

  gst_gl_colorscale_reset (colorscale);
}

static void
gst_gl_colorscale_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_colorscale_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_colorscale_reset (GstGLColorscale * colorscale)
{
  if (colorscale->upload) {
    gst_element_set_state (colorscale->upload, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (colorscale), colorscale->upload);
    colorscale->upload = NULL;
  }
  if (colorscale->download) {
    gst_element_set_state (colorscale->download, GST_STATE_NULL);
    gst_bin_remove (GST_BIN (colorscale), colorscale->download);
    colorscale->download = NULL;
  }
  gst_ghost_pad_set_target (GST_GHOST_PAD (colorscale->srcpad), NULL);
  gst_ghost_pad_set_target (GST_GHOST_PAD (colorscale->sinkpad), NULL);
}

static void
gst_gl_colorscale_post_missing_element_message (GstGLColorscale * colorscale,
    const gchar * name)
{
  GstMessage *msg;

  msg = gst_missing_element_message_new (GST_ELEMENT_CAST (colorscale), name);
  gst_element_post_message (GST_ELEMENT_CAST (colorscale), msg);
}

static GstStateChangeReturn
gst_gl_colorscale_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstGLColorscale *colorscale = GST_GL_COLORSCALE (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gl_colorscale_add_elements (colorscale))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_gl_colorscale_reset (colorscale);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_gl_colorscale_add_elements (GstGLColorscale * colorscale)
{
  GstElement *upload, *download;
  GstPad *u_sink, *d_src;

  upload = gst_element_factory_make ("glupload", "glupload");
  download = gst_element_factory_make ("gldownload", "gldownload");

  if (upload == NULL) {
    gst_gl_colorscale_post_missing_element_message (colorscale, "glupload");
    GST_ELEMENT_WARNING (colorscale, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "glupload"),
        (_("Missing element '%s' - check your GStreamer installation."),
            "glupload"));
  }

  if (download == NULL) {
    gst_gl_colorscale_post_missing_element_message (colorscale, "gldownload");
    GST_ELEMENT_WARNING (colorscale, CORE, MISSING_PLUGIN,
        (_("Missing element '%s' - check your GStreamer installation."),
            "gldownload"),
        (_("Missing element '%s' - check your GStreamer installation."),
            "gldownload"));
  }

  gst_bin_add (GST_BIN_CAST (colorscale), upload);
  gst_bin_add (GST_BIN_CAST (colorscale), download);

  gst_element_link (upload, download);

  u_sink = gst_element_get_static_pad (upload, "sink");
  d_src = gst_element_get_static_pad (download, "src");

  if (!gst_ghost_pad_set_target (GST_GHOST_PAD (colorscale->srcpad), d_src)
      || !gst_ghost_pad_set_target (GST_GHOST_PAD (colorscale->sinkpad),
          u_sink))
    goto target_failed;

  colorscale->upload = upload;
  colorscale->download = download;

  gst_object_unref (u_sink);
  gst_object_unref (d_src);

  return TRUE;

/* ERRORS */
target_failed:
  {
    GST_ELEMENT_ERROR (colorscale, LIBRARY, INIT, (NULL),
        ("Failed to set target pad"));
    gst_object_unref (u_sink);
    gst_object_unref (d_src);
    return FALSE;
  }
}
