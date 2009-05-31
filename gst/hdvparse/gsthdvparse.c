/*
 * GStreamer
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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
 * SECTION:element-HDVParse
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc ! mpegtsdemux ! hdvparse ! fakesink silent=TRUE
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "gsthdvparse.h"

GST_DEBUG_CATEGORY_STATIC (gst_hdvparse_debug);
#define GST_CAT_DEFAULT gst_hdvparse_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("private/hdv-a1")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("private/hdv-a1,parsed=(boolean)True")
    );

/* debug category for fltering log messages
 *
 * exchange the string 'Template HDVParse' with your description
 */
#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_hdvparse_debug, "hdvparse", 0, "HDV private stream parser");

GST_BOILERPLATE_FULL (GstHDVParse, gst_hdvparse, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM, DEBUG_INIT);

static GstFlowReturn gst_hdvparse_transform_ip (GstBaseTransform * base,
    GstBuffer * outbuf);

/* GObject vmethod implementations */

static void
gst_hdvparse_base_init (gpointer klass)
{
  static GstElementDetails element_details = {
    "HDVParser",
    "Data/Parser",
    "HDV private stream Parser",
    "Edward Hervey <bilboed@bilboed.com>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the HDVParse's class */
static void
gst_hdvparse_class_init (GstHDVParseClass * klass)
{
  GST_BASE_TRANSFORM_CLASS (klass)->transform_ip =
      GST_DEBUG_FUNCPTR (gst_hdvparse_transform_ip);
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_hdvparse_init (GstHDVParse * filter, GstHDVParseClass * klass)
{
  GstBaseTransform *transform = GST_BASE_TRANSFORM (filter);

  gst_base_transform_set_in_place (transform, TRUE);
  gst_base_transform_set_passthrough (transform, TRUE);
}

static void
gst_hdvparse_parse (GstHDVParse * filter, GstBuffer * buf)
{
  GST_MEMDUMP_OBJECT (filter, "BUFFER", GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  return;
}

/* GstBaseTransform vmethod implementations */

static GstFlowReturn
gst_hdvparse_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstHDVParse *filter = GST_HDVPARSE (base);

  gst_hdvparse_parse (filter, outbuf);

  return GST_FLOW_OK;
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
HDVParse_init (GstPlugin * HDVParse)
{
  return gst_element_register (HDVParse, "hdvparse", GST_RANK_PRIMARY,
      GST_TYPE_HDVPARSE);
}

/* gstreamer looks for this structure to register HDVParses
 *
 * exchange the string 'Template HDVParse' with you HDVParse description
 */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "hdvparse",
    "HDV private stream parser",
    HDVParse_init, VERSION, "LGPL", "GStreamer", "http://gstreamer.net/")
