/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstelements.c:
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


#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

#include "gstaggregator.h"
#include "gstfakesink.h"
#include "gstfakesrc.h"
#include "gstfdsink.h"
#include "gstfdsrc.h"
#include "gstfilesink.h"
#include "gstfilesrc.h"
#include "gstidentity.h"
#include "gstmd5sink.h"
#include "gstmultidisksrc.h"
#include "gstpipefilter.h"
#include "gstshaper.h"
#include "gststatistics.h"
#include "gsttee.h"
#include "gsttypefindelement.h"


struct _elements_entry {
  gchar *name;
  GType (*type) (void);
  GstElementDetails *details;
  gboolean (*factoryinit) (GstElementFactory *factory);
};


extern GType gst_filesrc_get_type(void);
extern GstElementDetails gst_filesrc_details;

static struct _elements_entry _elements[] = {
  { "aggregator",   gst_aggregator_get_type, 	&gst_aggregator_details,	gst_aggregator_factory_init },
  { "fakesrc", 	    gst_fakesrc_get_type, 	&gst_fakesrc_details,		gst_fakesrc_factory_init },
  { "fakesink",     gst_fakesink_get_type, 	&gst_fakesink_details,		gst_fakesink_factory_init },
  { "fdsink",       gst_fdsink_get_type, 	&gst_fdsink_details,		NULL },
  { "fdsrc", 	    gst_fdsrc_get_type, 	&gst_fdsrc_details,		NULL },
  { "filesrc", 	    gst_filesrc_get_type, 	&gst_filesrc_details,		NULL },
  { "filesink",	    gst_filesink_get_type,      &gst_filesink_details, 		NULL },
  { "identity",     gst_identity_get_type,  	&gst_identity_details,		NULL },
  { "md5sink",      gst_md5sink_get_type, 	&gst_md5sink_details,		gst_md5sink_factory_init },
  { "multidisksrc", gst_multidisksrc_get_type,	&gst_multidisksrc_details,	NULL },
  { "pipefilter",   gst_pipefilter_get_type, 	&gst_pipefilter_details,	NULL },
  { "shaper",       gst_shaper_get_type, 	&gst_shaper_details,		gst_shaper_factory_init },
  { "statistics",   gst_statistics_get_type, 	&gst_statistics_details,	NULL },
  { "tee",     	    gst_tee_get_type, 		&gst_tee_details,		gst_tee_factory_init },
  { "typefind",     gst_type_find_element_get_type, &gst_type_find_element_details,   	NULL },
  { NULL, 0 },
};

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  gint i = 0;

  gst_plugin_set_longname (plugin, "Standard GST Elements");

  GST_DEBUG_CATEGORY_INIT (gst_aggregator_debug, "aggregator",	0,	"aggregator element");
  GST_DEBUG_CATEGORY_INIT (gst_fakesink_debug,	"fakesink",	0,	"fakesink element");
  GST_DEBUG_CATEGORY_INIT (gst_fakesrc_debug,	"fakesrc",	0,	"fakesrc element");
  GST_DEBUG_CATEGORY_INIT (gst_fdsink_debug,	"fdsink",	0,	"fdsink element");
  GST_DEBUG_CATEGORY_INIT (gst_fdsrc_debug,	"fdsrc",	0,	"fdsrc element");
  GST_DEBUG_CATEGORY_INIT (gst_filesink_debug,	"filesink",	0,	"filesink element");
  GST_DEBUG_CATEGORY_INIT (gst_filesrc_debug,	"filesrc",	0,	"filesrc element");
  GST_DEBUG_CATEGORY_INIT (gst_identity_debug,	"identity",	0,	"identity element");
  GST_DEBUG_CATEGORY_INIT (gst_md5sink_debug,	"md5sink",	0,	"md5sink element");
  GST_DEBUG_CATEGORY_INIT (gst_multidisksrc_debug, "multidisksrc", 0,	"multidisksrc element");
  GST_DEBUG_CATEGORY_INIT (gst_pipefilter_debug, "pipefilter",	0,	"pipefilter element");
  GST_DEBUG_CATEGORY_INIT (gst_shaper_debug,	"shaper",	0,	"shaper element");
  GST_DEBUG_CATEGORY_INIT (gst_statistics_debug, "statistics",	0,	"statistics element");
  GST_DEBUG_CATEGORY_INIT (gst_tee_debug,	"tee",		0,	"tee element");
  GST_DEBUG_CATEGORY_INIT (gst_type_find_element_debug,	"typefind", GST_DEBUG_BG_YELLOW | GST_DEBUG_FG_GREEN, "typefind element");

  while (_elements[i].name) {  
    factory = gst_element_factory_new (_elements[i].name,
                                      (_elements[i].type) (),
                                      _elements[i].details);

    if (!factory)
      {
	g_warning ("gst_element_factory_new failed for `%s'",
		   _elements[i].name);
	continue;
      }

    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
    if (_elements[i].factoryinit) {
      _elements[i].factoryinit (factory);
    }
/*      g_print("added factory '%s'\n",_elements[i].name); */
    i++;
  }

/*  INFO (GST_INFO_PLUGIN_LOAD,"gstelements: loaded %d standard elements", i);*/
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstelements",
  plugin_init
};
