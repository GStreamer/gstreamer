/*
 * GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
 * Copyright (C) 2010 Sreerenj Balachandran <bsreerenj@gmail.com>
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
 * SECTION:element-opencvtextoverlay
 *
 * opencvtextoverlay renders the text on top of the video frames
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-0.10 videotestsrc ! ffmpegcolorspace ! opencvtextoverlay text="Opencv Text Overlay " ! ffmpegcolorspace ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstopencvutils.h"
#include "gsttextoverlay.h"

GST_DEBUG_CATEGORY_STATIC (gst_opencv_text_overlay_debug);
#define GST_CAT_DEFAULT gst_opencv_opencv_text_overlay_debug

#define DEFAULT_PROP_TEXT 	""
#define DEFAULT_PROP_WIDTH 	1
#define DEFAULT_PROP_HEIGHT 	1
#define DEFAULT_PROP_XPOS	50
#define DEFAULT_PROP_YPOS	50
#define DEFAULT_PROP_THICKNESS	2
#define DEFAULT_PROP_COLOR	0


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};
#define DEFAULT_WIDTH     1.0
#define DEFAULT_HEIGHT    1.0
enum
{
  PROP_0,
  PROP_XPOS,
  PROP_YPOS,
  PROP_THICKNESS,
  PROP_COLOR_R,
  PROP_COLOR_G,
  PROP_COLOR_B,
  PROP_TEXT,
  PROP_HEIGHT,
  PROP_WIDTH
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB)
    );

GST_BOILERPLATE (GstOpencvTextOverlay, gst_opencv_text_overlay, GstElement,
    GST_TYPE_ELEMENT);

static void gst_opencv_text_overlay_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_opencv_text_overlay_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_opencv_text_overlay_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_opencv_text_overlay_chain (GstPad * pad,
    GstBuffer * buf);



/* Clean up */
static void
gst_opencv_text_overlay_finalize (GObject * obj)
{
  GstOpencvTextOverlay *filter = GST_OPENCV_TEXT_OVERLAY (obj);

  if (filter->cvImage) {
    cvReleaseImage (&filter->cvImage);
  }

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}



/* GObject vmethod implementations */

static void
gst_opencv_text_overlay_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "opencvtextoverlay",
      "Filter/Effect/Video",
      "Write text on the top of video", "sreerenj<bsreerenj@gmail.com>");

  gst_element_class_add_static_pad_template (element_class, &src_factory);
  gst_element_class_add_static_pad_template (element_class, &sink_factory);
}

/* initialize the opencvtextoverlay's class */
static void
gst_opencv_text_overlay_class_init (GstOpencvTextOverlayClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_peek_parent (klass);
  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (gst_opencv_text_overlay_finalize);

  gobject_class->set_property = gst_opencv_text_overlay_set_property;
  gobject_class->get_property = gst_opencv_text_overlay_get_property;


  g_object_class_install_property (gobject_class, PROP_TEXT,
      g_param_spec_string ("text", "text",
          "Text to be display.", DEFAULT_PROP_TEXT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_XPOS,
      g_param_spec_int ("xpos", "horizontal position",
          "Sets the Horizontal position", 0, G_MAXINT,
          DEFAULT_PROP_XPOS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_YPOS,
      g_param_spec_int ("ypos", "vertical position",
          "Sets the Vertical position", 0, G_MAXINT,
          DEFAULT_PROP_YPOS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_THICKNESS,
      g_param_spec_int ("thickness", "font thickness",
          "Sets the Thickness of Font", 0, G_MAXINT,
          DEFAULT_PROP_THICKNESS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COLOR_R,
      g_param_spec_int ("colorR", "color -Red ",
          "Sets the color -R", 0, 255,
          DEFAULT_PROP_COLOR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COLOR_G,
      g_param_spec_int ("colorG", "color -Green",
          "Sets the color -G", 0, 255,
          DEFAULT_PROP_COLOR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_COLOR_B,
      g_param_spec_int ("colorB", "color -Blue",
          "Sets the color -B", 0, 255,
          DEFAULT_PROP_COLOR, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_double ("height", "Height",
          "Sets the height of fonts", 1.0, 5.0,
          DEFAULT_HEIGHT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_double ("width", "Width",
          "Sets the width of fonts", 1.0, 5.0,
          DEFAULT_WIDTH, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_opencv_text_overlay_init (GstOpencvTextOverlay * filter,
    GstOpencvTextOverlayClass * gclass)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_opencv_text_overlay_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_opencv_text_overlay_chain));

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  filter->textbuf = g_strdup (DEFAULT_PROP_TEXT);
  filter->width = DEFAULT_PROP_WIDTH;
  filter->height = DEFAULT_PROP_HEIGHT;
  filter->xpos = DEFAULT_PROP_XPOS;
  filter->ypos = DEFAULT_PROP_YPOS;
  filter->thickness = DEFAULT_PROP_THICKNESS;
  filter->colorR = DEFAULT_PROP_COLOR;
  filter->colorG = DEFAULT_PROP_COLOR;
  filter->colorB = DEFAULT_PROP_COLOR;

}

static void
gst_opencv_text_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOpencvTextOverlay *filter = GST_OPENCV_TEXT_OVERLAY (object);

  switch (prop_id) {
    case PROP_TEXT:
      g_free (filter->textbuf);
      filter->textbuf = g_value_dup_string (value);
      break;
    case PROP_XPOS:
      filter->xpos = g_value_get_int (value);
      break;
    case PROP_YPOS:
      filter->ypos = g_value_get_int (value);
      break;
    case PROP_THICKNESS:
      filter->thickness = g_value_get_int (value);
      break;

    case PROP_COLOR_R:
      filter->colorR = g_value_get_int (value);
      break;
    case PROP_COLOR_G:
      filter->colorG = g_value_get_int (value);
      break;
    case PROP_COLOR_B:
      filter->colorB = g_value_get_int (value);
      break;

    case PROP_HEIGHT:
      filter->height = g_value_get_double (value);
      break;
    case PROP_WIDTH:
      filter->width = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_opencv_text_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOpencvTextOverlay *filter = GST_OPENCV_TEXT_OVERLAY (object);

  switch (prop_id) {
    case PROP_TEXT:
      g_value_set_string (value, filter->textbuf);
      break;
    case PROP_XPOS:
      g_value_set_int (value, filter->xpos);
      break;
    case PROP_YPOS:
      g_value_set_int (value, filter->ypos);
      break;
    case PROP_THICKNESS:
      g_value_set_int (value, filter->thickness);
      break;
    case PROP_COLOR_R:
      g_value_set_int (value, filter->colorR);
      break;
    case PROP_COLOR_G:
      g_value_set_int (value, filter->colorG);
      break;
    case PROP_COLOR_B:
      g_value_set_int (value, filter->colorB);
      break;
    case PROP_HEIGHT:
      g_value_set_double (value, filter->height);
      break;
    case PROP_WIDTH:
      g_value_set_double (value, filter->width);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_opencv_text_overlay_set_caps (GstPad * pad, GstCaps * caps)
{
  GstOpencvTextOverlay *filter;
  GstPad *otherpad;

  gint width, height;
  GstStructure *structure;

  filter = GST_OPENCV_TEXT_OVERLAY (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  filter->cvImage = cvCreateImage (cvSize (width, height), IPL_DEPTH_8U, 3);
  filter->cvStorage = cvCreateMemStorage (0);

  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;
  gst_object_unref (filter);

  return gst_pad_set_caps (otherpad, caps);
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_opencv_text_overlay_chain (GstPad * pad, GstBuffer * buf)
{
  GstOpencvTextOverlay *filter;

  filter = GST_OPENCV_TEXT_OVERLAY (GST_OBJECT_PARENT (pad));

  filter->cvImage->imageData = (char *) GST_BUFFER_DATA (buf);

  cvInitFont (&(filter->font), CV_FONT_VECTOR0, filter->width, filter->height,
      0, filter->thickness, 0);

  buf = gst_buffer_make_writable (buf);
  cvPutText (filter->cvImage, filter->textbuf, cvPoint (filter->xpos,
          filter->ypos), &(filter->font), cvScalar (filter->colorR,
          filter->colorG, filter->colorB, 0));

  return gst_pad_push (filter->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
gboolean
gst_opencv_text_overlay_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template opencvtextoverlay' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_opencv_text_overlay_debug, "opencvtextoverlay",
      0, "Template opencvtextoverlay");

  return gst_element_register (plugin, "opencvtextoverlay", GST_RANK_NONE,
      GST_TYPE_OPENCV_TEXT_OVERLAY);
}
