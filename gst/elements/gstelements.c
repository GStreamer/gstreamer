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


#include <gst/gst.h>

#include "gstasyncdisksrc.h"
#include "gstaudiosink.h"
#include "gstaudiosrc.h"
#include "gstdisksrc.h"
#include "gstidentity.h"
#include "gstfakesink.h"
#include "gstfakesrc.h"
#include "gstfdsink.h"
#include "gstfdsrc.h"
#include "gstpipefilter.h"
#include "gstsinesrc.h"
#include "gsttee.h"

#if HAVE_LIBGHTTP
#include <gsthttpsrc.h>
#endif /* HAVE_LIBGHTTP */


struct _elements_entry {
  gchar *name;
  GtkType (*type) (void);
  GstElementDetails *details;
  gboolean (*factoryinit) (GstElementFactory *factory);
};

static struct _elements_entry _elements[] = {
  { "fakesrc", 	    gst_fakesrc_get_type, 	&gst_fakesrc_details,      NULL },
  { "fakesink",     gst_fakesink_get_type, 	&gst_fakesink_details,     NULL },
  { "asyncdisksrc", gst_asyncdisksrc_get_type, 	&gst_asyncdisksrc_details, NULL },
  { "audiosink",    gst_audiosink_get_type, 	&gst_audiosink_details,    gst_audiosink_factory_init },
  { "audiosrc",     gst_audiosrc_get_type, 	&gst_audiosrc_details, 	   NULL },
  { "disksrc", 	    gst_disksrc_get_type, 	&gst_disksrc_details,      NULL },
  { "identity",     gst_identity_get_type,  	&gst_identity_details,     NULL },
  { "fdsink",       gst_fdsink_get_type, 	&gst_fdsink_details,       NULL },
  { "fdsrc", 	    gst_fdsrc_get_type, 	&gst_fdsrc_details,        NULL },
  { "pipefilter",   gst_pipefilter_get_type, 	&gst_pipefilter_details,   NULL },
  { "sinesrc", 	    gst_sinesrc_get_type, 	&gst_sinesrc_details,      NULL },
  { "tee",     	    gst_tee_get_type, 		&gst_tee_details,     	   gst_tee_factory_init },

#if HAVE_LIBGHTTP
  { "httpsrc", 	    gst_httpsrc_get_type, 	&gst_httpsrc_details,      NULL },
#endif /* HAVE_LIBGHTTP */
  
  { NULL, 0 },
};

GstPlugin *plugin_init (GModule *module) 
{
  GstPlugin *plugin;
  GstElementFactory *factory;
  gint i = 0;

  plugin = gst_plugin_new("gstelements");
  g_return_val_if_fail(plugin != NULL,NULL);

  gst_plugin_set_longname (plugin, "Standard GST Elements");

  while (_elements[i].name) {
    factory = gst_elementfactory_new (_elements[i].name,
                                      (_elements[i].type) (),
                                      _elements[i].details);
    if (factory != NULL) {
      gst_plugin_add_factory (plugin, factory);
      if (_elements[i].factoryinit) {
        _elements[i].factoryinit (factory);
      }
//      g_print("added factory '%s'\n",_elements[i].name);
    }
    i++;
  }

//  INFO (GST_INFO_PLUGIN_LOAD,"gstelements: loaded %d standard elements", i);

  return plugin;
}
