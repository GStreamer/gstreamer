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

//#define DEBUG_ENABLED

#include <stdarg.h>
#include <gst/gst.h>

void 
_gst_caps_initialize (void) 
{
}
/**
 * gst_caps_register:
 * @factory: the factory to register
 *
 * Register the factory. 
 *
 * Returns: The registered capability
 */
GstCaps *
gst_caps_register (GstCapsFactory factory)
{
  GstCapsFactoryEntry tag;
  gint i = 0;
  guint16 typeid;
  GstCaps *caps;
  
  g_return_val_if_fail (factory != NULL, NULL);

  tag = factory[i++];

  g_return_val_if_fail (tag != NULL, NULL);
  
  typeid = gst_type_find_by_mime ((gchar *)tag);
  if (typeid == 0) {
     GstTypeFactory *factory = g_new0 (GstTypeFactory, 1);

     factory->mime = g_strdup ((gchar *)tag);
     factory->exts = NULL;
     factory->typefindfunc = NULL;

     typeid = gst_type_register (factory);
  }

  caps = g_new0 (GstCaps, 1);
  g_return_val_if_fail (caps != NULL, NULL);

  caps->id = typeid;
  caps->properties = gst_props_register (&factory[i]);

  return caps;
}


/**
 * gst_caps_dump:
 * @caps: the capability to dump
 *
 * Dumps the contents of the capabilty one the console
 */
void
gst_caps_dump (GstCaps *caps)
{
  g_return_if_fail (caps != NULL);

  g_print("gstcaps: {\ngstcaps:  mime type \"%d\"\n", caps->id);

  gst_props_dump (caps->properties);

  g_print("gstcaps: }\n");
}
	

/**
 * gst_caps_check_compatibility:
 * @fromcaps: a capabilty
 * @tocaps: a capabilty
 *
 * Checks whether two capabilities are compatible
 *
 * Returns: true if compatible, false otherwise
 */
gboolean
gst_caps_check_compatibility (GstCaps *fromcaps, GstCaps *tocaps)
{
  g_return_val_if_fail (fromcaps != NULL, FALSE);
  g_return_val_if_fail (tocaps != NULL, FALSE);
	
  if (fromcaps->id != tocaps->id)
    return FALSE;

  return gst_props_check_compatibility (fromcaps->properties, tocaps->properties);
}

