/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstfilter.c: element for filter plug-ins
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

#include "gstfilter.h"


struct _elements_entry {
  gchar *name;
  GType (*type) (void);
  GstElementDetails *details;
  gboolean (*factoryinit) (GstElementFactory *factory);
};

static struct _elements_entry _elements[] = {
  { "iir",  gst_iir_get_type,  &gst_iir_details,  NULL },
  { NULL, 0 },
};

GstPadTemplate* 
gst_filter_src_factory (void)
{
  static GstPadTemplate *templ = NULL;
  if (!templ) {
    templ = GST_PAD_TEMPLATE_NEW ( 
  		"src",
  		GST_PAD_SRC,
  		GST_PAD_ALWAYS,
  		GST_CAPS_NEW (
  		  "filter_src",
  		  "audio/raw",
  		    "format",		GST_PROPS_STRING ("float"),
  		    "rate",            	GST_PROPS_INT_RANGE (1, G_MAXINT),
		    "layout",     	GST_PROPS_STRING ("gfloat"),
		    "intercept",  	GST_PROPS_FLOAT(0.0),
		    "slope",      	GST_PROPS_FLOAT(1.0),
		    "channels",   	GST_PROPS_INT (1)
		)
  	     );
  }
  return templ;
}

GstPadTemplate* 
gst_filter_sink_factory (void)
{
  static GstPadTemplate *templ = NULL;
  if (!templ) {
    templ = GST_PAD_TEMPLATE_NEW ( 
  		"sink",
  		GST_PAD_SINK,
  		GST_PAD_ALWAYS,
  		GST_CAPS_NEW (
  		  "filter_src",
  		  "audio/raw",
  		    "format",		GST_PROPS_STRING ("float"),
  		    "rate",            	GST_PROPS_INT_RANGE (1, G_MAXINT),
		    "layout",     	GST_PROPS_STRING ("gfloat"),
		    "intercept",  	GST_PROPS_FLOAT(0.0),
		    "slope",      	GST_PROPS_FLOAT(1.0),
		    "channels",   	GST_PROPS_INT (1)
		)
  	     );
  }
  return templ;
}

static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;
  gint i = 0;

  while (_elements[i].name) {
    factory = gst_element_factory_new (_elements[i].name,
                                      (_elements[i].type) (),
                                       _elements[i].details);

    if (!factory) {
      g_warning ("gst_filter_new failed for `%s'",
                 _elements[i].name);
      continue;
    }
    gst_element_factory_add_pad_template (factory, gst_filter_src_factory ());
    gst_element_factory_add_pad_template (factory, gst_filter_sink_factory ());

    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    if (_elements[i].factoryinit) {
      _elements[i].factoryinit (factory);
    }
    i++;
  }

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "filter",
  plugin_init
};
