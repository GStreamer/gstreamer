/*
 * GStreamer faceoverlay plugin
 * Copyright (C) 2011 Laura Lucas Alday <lauralucas@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * SECTION:element-faceoverlay
 *
 * Overlays a SVG image over a detected face in a video stream.
 * x, y, w, and h properties are optional, and change the image position and
 * size relative to the detected face position and size.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch autovideosrc ! ffmpegcolorspace ! faceoverlay location=/path/to/gnome-video-effects/pixmaps/bow.svg x=-5 y=-15 w=0.3 h=0.1 ! ffmpegcolorspace ! autovideosink

 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstfaceoverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_face_overlay_debug);
#define GST_CAT_DEFAULT gst_face_overlay_debug

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define GST_STR_VIDEO_CAPS GST_VIDEO_CAPS_BGRA
#else
#define GST_STR_VIDEO_CAPS GST_VIDEO_CAPS_ARGB
#endif

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_X,
  PROP_Y,
  PROP_W,
  PROP_H
};

/* the capabilities of the inputs and outputs. */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_STR_VIDEO_CAPS)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_STR_VIDEO_CAPS)
    );

GST_BOILERPLATE (GstFaceOverlay, gst_face_overlay, GstBin, GST_TYPE_BIN);

static void gst_face_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_face_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_face_overlay_message_handler (GstBin * bin,
    GstMessage * message);
static GstStateChangeReturn gst_face_overlay_change_state (GstElement * element,
    GstStateChange transition);
static gboolean gst_face_overlay_create_children (GstFaceOverlay * filter);
static gboolean gst_face_overlay_reset (GstFaceOverlay * filter);
static gboolean gst_face_overlay_create_pad (GstFaceOverlay * filter,
    GstPad * filter_pad, const char *pad_name, GstElement * child_element);
static gboolean toggle_pads_link_state (GstPad * pad1, GstPad * pad2);


static gboolean
toggle_pads_link_state (GstPad * pad1, GstPad * pad2)
{
  gboolean ok = TRUE;

  if (gst_pad_is_linked (pad1)) {
    if (gst_pad_get_direction (pad1) == GST_PAD_SINK)
      gst_pad_unlink (pad2, pad1);
    else
      gst_pad_unlink (pad1, pad2);
  } else {
    if (gst_pad_get_direction (pad1) == GST_PAD_SINK)
      ok &= (gst_pad_link (pad2, pad1) == 0);
    else
      ok &= (gst_pad_link (pad1, pad2) == 0);
  }

  return ok;
}

/* Unlinks and removes the pad that was created in gst_face_overlay_init ()
 * and adds the internal element ghost pad instead  */
static gboolean
gst_face_overlay_create_pad (GstFaceOverlay * filter, GstPad * filter_pad,
    const char *pad_name, GstElement * child_element)
{
  GstPad *peer = NULL;
  GstPad *pad = NULL;
  gboolean ok = TRUE;

  /* get the outside world pad connected to faceoverlay src/sink pad */
  peer = gst_pad_get_peer (filter_pad);

  /* unlink and remove the faceoverlay src/sink pad */
  toggle_pads_link_state (peer, filter_pad);

  gst_element_remove_pad (GST_ELEMENT (filter), filter_pad);

  /* add a ghost pad pointing to the child element pad (facedetect sink or
   * svg_overlay src depending on filter_pad direction) and add it to
   * faceoverlay bin */
  pad = gst_element_get_static_pad (child_element, pad_name);
  filter_pad = gst_ghost_pad_new (pad_name, pad);
  gst_object_unref (GST_OBJECT (pad));

  gst_element_add_pad (GST_ELEMENT (filter), filter_pad);

  /* link the child element pad to the outside world thru the ghost pad */
  toggle_pads_link_state (peer, filter_pad);

  g_object_unref (peer);

  return ok;
}

static gboolean
gst_face_overlay_reset (GstFaceOverlay * filter)
{
  gst_element_set_state (filter->face_detect, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (filter), filter->face_detect);
  filter->face_detect = NULL;

  gst_element_set_state (filter->svg_overlay, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (filter), filter->svg_overlay);
  filter->svg_overlay = NULL;

  gst_element_set_state (filter->colorspace, GST_STATE_NULL);
  gst_bin_remove (GST_BIN (filter), filter->colorspace);
  filter->colorspace = NULL;

  return TRUE;
}

static gboolean
gst_face_overlay_create_children (GstFaceOverlay * filter)
{
  gboolean ret = TRUE;

  if ((filter->colorspace = gst_element_factory_make ("ffmpegcolorspace",
              NULL)) == NULL) {
    return FALSE;
  }

  if ((filter->face_detect = gst_element_factory_make ("facedetect",
              NULL)) == NULL) {
    return FALSE;
  }
  g_object_set (filter->face_detect, "display", 0, NULL);

  if ((filter->svg_overlay = gst_element_factory_make ("rsvgoverlay",
              NULL)) == NULL) {
    return FALSE;
  }

  gst_bin_add_many (GST_BIN (filter),
      filter->face_detect, filter->colorspace, filter->svg_overlay, NULL);

  ret &= gst_element_link_pads (filter->face_detect, "src",
      filter->colorspace, "sink");
  ret &= gst_element_link_pads (filter->colorspace, "src",
      filter->svg_overlay, "sink");

  ret &= gst_face_overlay_create_pad (filter, filter->sinkpad, "sink",
      filter->face_detect);
  ret &= gst_face_overlay_create_pad (filter, filter->srcpad, "src",
      filter->svg_overlay);

  return ret;

}

static GstStateChangeReturn
gst_face_overlay_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstFaceOverlay *filter = GST_FACEOVERLAY (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_face_overlay_create_children (filter))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_face_overlay_reset (filter);
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_face_overlay_message_handler (GstBin * bin, GstMessage * message)
{
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_ELEMENT &&
      strcmp (gst_structure_get_name (message->structure), "facedetect") == 0) {
    GstFaceOverlay *filter = GST_FACEOVERLAY (bin);

    /* optionally draw the image once every two messages for better performance
     * filter->process_message = !filter->process_message;
     *  if(!filter->process_message)
     *    return;
     */

    guint x, y, width, height;
    int delta_x, delta_y, svg_x, svg_y, svg_width, svg_height;
    const GstStructure *face;
    int face_count;

    face_count =
        gst_value_list_get_size (gst_structure_get_value (message->structure,
            "faces"));

    /* The last face in the list seems to be the right one, objects mistakenly
     * detected as faces for a couple of frames seem to be in the list
     * beginning. TODO: needs confirmation. */
    face =
        gst_value_get_structure (gst_value_list_get_value
        (gst_structure_get_value (message->structure, "faces"),
            face_count - 1));
    gst_structure_get_uint (face, "x", &x);
    gst_structure_get_uint (face, "y", &y);
    gst_structure_get_uint (face, "width", &width);
    gst_structure_get_uint (face, "height", &height);

    /* Apply x and y offsets relative to face position and size.
     * Set image width and height as a fraction of face width and height.
     * Cast to int since face position and size will never be bigger than
     * G_MAX_INT and we may have negative values as svg_x or svg_y */

    delta_x = (int) (filter->x * (int) width);
    svg_x = (int) x + delta_x;

    delta_y = (int) (filter->y * (int) height);
    svg_y = (int) y + delta_y;

    svg_width = (int) width *filter->w;
    svg_height = (int) height *filter->h;

    g_object_set (filter->svg_overlay,
        "location", filter->location,
        "x", svg_x, "y", svg_y, "width", svg_width, "height", svg_height, NULL);

  }

  GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

/* GObject vmethod implementations */
/* the _base_init() function is meant to initialize class and child class
 * properties during each new child class creation */
static void
gst_face_overlay_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "faceoverlay",
      "Filter/Editor/Video",
      "Overlays SVG graphics over a detected face in a video stream",
      "Laura Lucas Alday <lauralucas@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* initialize the faceoverlay's class */
/* the _class_init() function is used to initialise the class only once
 * (specifying what signals, arguments and virtual functions the class has and
 * setting up global state) */
static void
gst_face_overlay_class_init (GstFaceOverlayClass * klass)
{
  GObjectClass *gobject_class;
  GstBinClass *gstbin_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gstbin_class = GST_BIN_CLASS (klass);
  gstelement_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_face_overlay_set_property;
  gobject_class->get_property = gst_face_overlay_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "Location",
          "Location of SVG file to use for face overlay",
          "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_X,
      g_param_spec_float ("x", "face x offset",
          "Specify image x relative to detected face x.", -G_MAXFLOAT,
          G_MAXFLOAT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_Y,
      g_param_spec_float ("y", "face y offset",
          "Specify image y relative to detected face y.", -G_MAXFLOAT,
          G_MAXFLOAT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_W,
      g_param_spec_float ("w", "face width percent",
          "Specify image width relative to face width.", 0, G_MAXFLOAT, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_H,
      g_param_spec_float ("h", "face height percent",
          "Specify image height relative to face height.", 0, G_MAXFLOAT, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstbin_class->handle_message =
      GST_DEBUG_FUNCPTR (gst_face_overlay_message_handler);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_face_overlay_change_state);

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 * the _init() function is used to initialise a specific instance of this type.
 */
static void
gst_face_overlay_init (GstFaceOverlay * filter, GstFaceOverlayClass * gclass)
{
  filter->x = 0;
  filter->y = 0;
  filter->w = 1;
  filter->h = 1;
  filter->colorspace = NULL;
  filter->svg_overlay = NULL;
  filter->face_detect = NULL;
  filter->location = NULL;
  filter->process_message = TRUE;

  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

}

static void
gst_face_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFaceOverlay *filter = GST_FACEOVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      filter->location = g_value_dup_string (value);
      break;
    case PROP_X:
      filter->x = g_value_get_float (value);
      break;
    case PROP_Y:
      filter->y = g_value_get_float (value);
      break;
    case PROP_W:
      filter->w = g_value_get_float (value);
      break;
    case PROP_H:
      filter->h = g_value_get_float (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_face_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFaceOverlay *filter = GST_FACEOVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, filter->location);
      break;
    case PROP_X:
      g_value_set_float (value, filter->x);
      break;
    case PROP_Y:
      g_value_set_float (value, filter->y);
      break;
    case PROP_W:
      g_value_set_float (value, filter->w);
      break;
    case PROP_H:
      g_value_set_float (value, filter->h);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
faceoverlay_init (GstPlugin * faceoverlay)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_face_overlay_debug, "faceoverlay",
      0, "SVG Face Overlay");

  return gst_element_register (faceoverlay, "faceoverlay", GST_RANK_NONE,
      GST_TYPE_FACEOVERLAY);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "faceoverlay"
#endif

/* gstreamer looks for this structure to register plugins */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "faceoverlay",
    "SVG Face Overlay",
    faceoverlay_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
