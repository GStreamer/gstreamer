/* GStreamer
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
#include <math.h>
#include <sys/soundcard.h>

//#define DEBUG_ENABLED
#include "gst_arts.h"
#include "gst_artsio_impl.h"

/* elementfactory information */
static GstElementDetails gst_arts_details = {
  "aRts plugin",
  "Filter/Audio",
  "aRts plugin",
  VERSION,
  "Erik Walthinsen <omega@temple-baptist.com,
Stefan Westerfeld <stefan@space.twc.de>",
  "(C) 2000",
};


GST_PADTEMPLATE_FACTORY ( sink_temp,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "arts_sample",
    "audio/raw",
    "format",   GST_PROPS_INT ("int"),
    "law",      GST_PROPS_INT (0),
    "depth",    GST_PROPS_INT (16),
    "width",    GST_PROPS_INT (16),
    "signed",   GST_PROPS_BOOLEAN (TRUE),
    "channels", GST_PROPS_INT (2),
    "endianness", GST_PROPS_INT (G_LITTLE_ENDIAN)
  )
)

GST_PADTEMPLATE_FACTORY ( src_temp,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "arts_sample",
    "audio/raw",
    "format",   GST_PROPS_INT ("int"),
    "law",      GST_PROPS_INT (0),
    "depth",    GST_PROPS_INT (16),
    "width",    GST_PROPS_INT (16),
    "signed",   GST_PROPS_BOOLEAN (TRUE),
    "channels", GST_PROPS_INT (2),
    "endianness", GST_PROPS_INT (G_LITTLE_ENDIAN)
  )
)

static GstPadTemplate*
mad_src_template_factory (void)
{  
  static GstPadTemplate *templ = NULL;
 
  if (!templ) {
    templ = gst_padtemplate_new (
        "src",
        GST_PAD_SRC,
        GST_PAD_ALWAYS,
        gst_caps_new (
          "mad_src",
          "audio/raw",
          gst_props_new (
            "format",   GST_PROPS_STRING ("int"),
             "law",         GST_PROPS_INT (0),
             "endianness",  GST_PROPS_INT (G_BYTE_ORDER),
             "signed",      GST_PROPS_BOOLEAN (TRUE),
             "width",       GST_PROPS_INT (16),
             "depth",       GST_PROPS_INT (16),
             "rate",        GST_PROPS_INT (44100),
             "channels",    GST_PROPS_INT (2),
             NULL)),
        NULL);
  } 
  return templ;
}       

enum {
  ARG_0,
  ARG_LAST,
};

static void			gst_arts_class_init		(GstARTSClass *klass);
static void			gst_arts_init			(GstARTS *arts);

static void			gst_arts_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void			gst_arts_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementStateReturn	gst_arts_change_state		(GstElement *element);
static void			gst_arts_loop			(GstElement *element);


static GstElementClass *parent_class = NULL;
//static guint gst_arts_signals[LAST_SIGNAL] = { 0 };

GType
gst_arts_get_type (void)
{
  static GType gst_arts_type = 0;

  if (!gst_arts_type) {
    static const GTypeInfo gst_arts_info = {
      sizeof(GstARTSClass),      NULL,
      NULL,
      (GClassInitFunc)gst_arts_class_init,
      NULL,
      NULL,
      sizeof(GstARTS),
      0,
      (GInstanceInitFunc)gst_arts_init,
    };
    gst_arts_type = g_type_register_static(GST_TYPE_ELEMENT, "GstArts", &gst_arts_info, 0);
  }
  return gst_arts_type;
} 

static void
gst_arts_class_init (GstARTSClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  gobject_class->set_property = gst_arts_set_property;
  gobject_class->get_property = gst_arts_get_property;

  gstelement_class->change_state = gst_arts_change_state;
}

static void
gst_arts_init (GstARTS *arts)
{
  arts->sinkpad = gst_pad_new_from_template(GST_PADTEMPLATE_GET(sink_temp),"sink");
  gst_element_add_pad(GST_ELEMENT(arts),arts->sinkpad);

//  arts->srcpad = gst_pad_new_from_template(GST_PADTEMPLATE_GET(src_temp),"src");
  arts->srcpad = gst_pad_new_from_template(mad_src_template_factory (), "src");
  gst_element_add_pad(GST_ELEMENT(arts),arts->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (arts), gst_arts_loop);

  arts->wrapper = gst_arts_wrapper_new(arts->sinkpad,arts->srcpad);
}

static void
gst_arts_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstARTS *arts = (GstARTS*)object;
  GstARTSClass *oclass = (GstARTSClass*)(G_OBJECT_CLASS (object));

}

static void
gst_arts_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstARTS *arts = (GstARTS*)object;
  GstARTSClass *oclass = (GstARTSClass*)(G_OBJECT_CLASS (object));

}

static GstElementStateReturn
gst_arts_change_state (GstElement *element)
{
  GstARTS *arts = (GstARTS*)element;

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    default:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_arts_loop	(GstElement *element)
{
  GstARTS *arts = (GstARTS*)element;

  g_return_if_fail (arts != NULL);

  gst_arts_wrapper_do(arts->wrapper);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *gstarts;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstarts = gst_elementfactory_new("gstarts",GST_TYPE_ARTS,&gst_arts_details);
  g_return_val_if_fail(gstarts != NULL, FALSE);

  gst_elementfactory_add_padtemplate(gstarts, GST_PADTEMPLATE_GET(sink_temp));
//  gst_elementfactory_add_padtemplate(gstarts, GST_PADTEMPLATE_GET(src_temp));
  gst_elementfactory_add_padtemplate(gstarts, mad_src_template_factory ());

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (gstarts));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gst_arts",
  plugin_init
};
