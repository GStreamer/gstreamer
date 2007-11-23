/*
 * GStreamer
 * Copyright 2007 Edgard Lima <edgard.lima@indt.org.br>
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
 * SECTION:element-plugin
 *
 * <refsect2>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v -m filesrc location=./test.jpeg ! metadatamux ! filesink location=meta.jpeg
 * </programlisting>
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstmetadatamux.h"

GST_DEBUG_CATEGORY_STATIC (gst_metadata_mux_debug);
#define GST_CAT_DEFAULT gst_metadata_mux_debug

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
};

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg, tags-extracted = (bool) true;"
        "image/png,  tags-extracted = (bool) true")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("image/jpeg; " "image/png")
    );

GST_BOILERPLATE (GstMetadataMux, gst_metadata_mux, GstElement,
    GST_TYPE_ELEMENT);

static void gst_metadata_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_metadata_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_metadata_mux_set_caps (GstPad * pad, GstCaps * caps);
static GstFlowReturn gst_metadata_mux_chain (GstPad * pad, GstBuffer * buf);

static void
gst_metadata_mux_base_init (gpointer gclass)
{
  static GstElementDetails element_details = {
    "PluginTemplate",
    "Generic/PluginTemplate",
    "Generic Template Element",
    "Thomas Vander Stichele <thomas@apestaart.org>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &element_details);
}

/* initialize the plugin's class */
static void
gst_metadata_mux_class_init (GstMetadataMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_metadata_mux_set_property;
  gobject_class->get_property = gst_metadata_mux_get_property;


}

/* initialize the new element
 * instantiate pads and add them to element
 * set functions
 * initialize structure
 */
static void
gst_metadata_mux_init (GstMetadataMux * filter, GstMetadataMuxClass * gclass)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (filter);

  filter->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_setcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_mux_set_caps));
  gst_pad_set_getcaps_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  filter->srcpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "src"), "src");
  gst_pad_set_getcaps_function (filter->srcpad,
      GST_DEBUG_FUNCPTR (gst_pad_proxy_getcaps));

  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);
  gst_pad_set_chain_function (filter->sinkpad,
      GST_DEBUG_FUNCPTR (gst_metadata_mux_chain));

}

static void
gst_metadata_mux_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMetadataMux *filter = GST_METADATA_MUX (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_metadata_mux_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMetadataMux *filter = GST_METADATA_MUX (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean
gst_metadata_mux_set_caps (GstPad * pad, GstCaps * caps)
{
  GstMetadataMux *filter;
  GstPad *otherpad;

  filter = GST_METADATA_MUX (gst_pad_get_parent (pad));
  otherpad = (pad == filter->srcpad) ? filter->sinkpad : filter->srcpad;

  return gst_pad_set_caps (pad, caps);
}

/* chain function
 * this function does the actual processing
 */

static GstFlowReturn
gst_metadata_mux_chain (GstPad * pad, GstBuffer * buf)
{
  GstMetadataMux *filter;

  filter = GST_METADATA_MUX (GST_OBJECT_PARENT (pad));

  /* just push out the incoming buffer without touching it */
  return gst_pad_push (filter->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and pad templates
 * register the features
 *
 * exchange the string 'plugin' with your elemnt name
 */
gboolean
gst_metadata_mux_plugin_init (GstPlugin * plugin)
{
  /* exchange the strings 'plugin' and 'Template plugin' with your
   * plugin name and description */
  GST_DEBUG_CATEGORY_INIT (gst_metadata_mux_debug, "metadatamux",
      0, "Metadata muxer");

  return gst_element_register (plugin, "metadatamux",
      GST_RANK_NONE, GST_TYPE_METADATA_MUX);
}
