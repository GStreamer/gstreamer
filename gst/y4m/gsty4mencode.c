/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#include <string.h>
#include <gst/gst.h>
#include "gstlavencode.h"

static GstElementDetails lavencode_details = {
  "LavEncode",
  "Filter/LAV/Encoder",
  "Encodes a YUV frame into the lav format (mjpeg_tools)",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
};


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

GST_PADTEMPLATE_FACTORY (lavencode_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "test_src",
    "application/x-lav",
    NULL
  )
)

GST_PADTEMPLATE_FACTORY (lavencode_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "test_src",
    "video/raw",
      "format", GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
      "width", GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height", GST_PROPS_INT_RANGE (0, G_MAXINT)
  )
)

static void			gst_lavencode_class_init	(GstLavEncodeClass *klass);
static void			gst_lavencode_init		(GstLavEncode *filter);

static void			gst_lavencode_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_lavencode_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void			gst_lavencode_chain		(GstPad *pad, GstBuffer *buf);
static GstElementStateReturn 	gst_lavencode_change_state 	(GstElement *element);


static GstElementClass *parent_class = NULL;
//static guint gst_filter_signals[LAST_SIGNAL] = { 0 };

GType
gst_lavencode_get_type(void) {
  static GType lavencode_type = 0;

  if (!lavencode_type) {
    static const GTypeInfo lavencode_info = {
      sizeof(GstLavEncodeClass),      NULL,
      NULL,
      (GClassInitFunc)gst_lavencode_class_init,
      NULL,
      NULL,
      sizeof(GstLavEncode),
      0,
      (GInstanceInitFunc)gst_lavencode_init,
    };
    lavencode_type = g_type_register_static(GST_TYPE_ELEMENT, "GstLavEncode", &lavencode_info, 0);
  }
  return lavencode_type;
}

static void
gst_lavencode_class_init (GstLavEncodeClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_lavencode_change_state;

  gobject_class->set_property = gst_lavencode_set_property;
  gobject_class->get_property = gst_lavencode_get_property;
}

static void
gst_lavencode_newcaps (GstPad *pad, GstCaps *caps)
{
  GstLavEncode *filter;

  filter = GST_LAVENCODE (gst_pad_get_parent (pad));

  filter->width = gst_caps_get_int (caps, "width");
  filter->height = gst_caps_get_int (caps, "height");
}

static void
gst_lavencode_init (GstLavEncode *filter)
{
  filter->sinkpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (lavencode_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_lavencode_chain);
  gst_pad_set_newcaps_function (filter->sinkpad, gst_lavencode_newcaps);

  filter->srcpad = gst_pad_new_from_template(
		  GST_PADTEMPLATE_GET (lavencode_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->init = TRUE;
}

static void
gst_lavencode_chain (GstPad *pad,GstBuffer *buf)
{
  GstLavEncode *filter;
  GstBuffer* outbuf;
  gchar *header;
  gint len;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  filter = GST_LAVENCODE (gst_pad_get_parent (pad));
  g_return_if_fail(filter != NULL);
  g_return_if_fail(GST_IS_LAVENCODE(filter));

  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (buf) + 256);

  if (filter->init) {
    header = "YUV4MPEG %d %d %d\nFRAME\n";
    filter->init = FALSE;
  }
  else {
    header = "FRAME\n";
  }

  snprintf (GST_BUFFER_DATA (outbuf), 255, header, filter->width, filter->height, 3);
  len = strlen (GST_BUFFER_DATA (outbuf));

  memcpy (GST_BUFFER_DATA (outbuf) + len, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (buf) + len;

  gst_buffer_unref(buf);

  gst_pad_push(filter->srcpad,outbuf);
}

static void
gst_lavencode_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstLavEncode *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_LAVENCODE(object));
  filter = GST_LAVENCODE(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_lavencode_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstLavEncode *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_LAVENCODE(object));
  filter = GST_LAVENCODE(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_lavencode_change_state (GstElement *element)
{
  GstLavEncode *filter;

  g_return_val_if_fail (GST_IS_LAVENCODE (element), GST_STATE_FAILURE);

  filter = GST_LAVENCODE(element);

  if (GST_STATE_TRANSITION (element) == GST_STATE_NULL_TO_READY) {
    filter->init = TRUE;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new("lavencode",GST_TYPE_LAVENCODE,
                                   &lavencode_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (lavencode_src_factory));
  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (lavencode_sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "lavencode",
  plugin_init
};
