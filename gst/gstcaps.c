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

#include "gstcaps.h"
#include "gsttype.h"

#include "gstpropsprivate.h"
void 
_gst_caps_initialize (void) 
{
}

static guint16
get_type_for_mime (gchar *mime)
{
  guint16 typeid;

  typeid = gst_type_find_by_mime (mime);
  if (typeid == 0) {
     GstTypeFactory *factory = g_new0 (GstTypeFactory, 1);

     factory->mime = g_strdup (mime);
     factory->exts = NULL;
     factory->typefindfunc = NULL;

     typeid = gst_type_register (factory);
  }
  return typeid;
}

/**
 * gst_caps_new:
 * @mime: the mime type to attach to the capability
 *
 * create a new capability with the given mime type
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new (gchar *mime)
{
  GstCaps *caps;

  g_return_val_if_fail (mime != NULL, NULL);
  
  caps = g_new0 (GstCaps, 1);
  caps->id = get_type_for_mime (mime);
  caps->properties = NULL;
  
  return caps;
}

/**
 * gst_caps_register:
 * @factory: the factory to register
 *
 * Register the factory. 
 *
 * Returns: The registered capability
 */
GstCaps*
gst_caps_register (GstCapsFactory *factory)
{
  GstCapsFactoryEntry tag;
  gint i = 0;
  guint16 typeid;
  GstCaps *caps;
  
  g_return_val_if_fail (factory != NULL, NULL);

  tag = (*factory)[i++];

  g_return_val_if_fail (tag != NULL, NULL);
  
  typeid = get_type_for_mime ((gchar *)tag);

  caps = g_new0 (GstCaps, 1);
  g_return_val_if_fail (caps != NULL, NULL);

  caps->id = typeid;
  caps->properties = gst_props_register (&(*factory)[i]);

  return caps;
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
	
  if (fromcaps->id != tocaps->id) {
    //g_print ("gstcaps: mime types wrong\n");
    return FALSE;
  }

  if (tocaps->properties) {
    GstPropsEntry *entry = (GstPropsEntry *)tocaps->properties;

    if (fromcaps->properties) {
      return gst_props_check_compatibility (fromcaps->properties, tocaps->properties);
    }
    else {
      //g_print ("gstcaps: no source caps\n");
      return FALSE;
    }
  }
  else {
    // assume it accepts everything
    //g_print ("gstcaps: no caps\n");
    return TRUE;
  }
}


xmlNodePtr      
gst_caps_save_thyself (GstCaps *caps, xmlNodePtr parent)
{
  xmlNodePtr subtree;

  g_return_val_if_fail (caps != NULL, NULL);

  xmlNewChild (parent, NULL, "type", gst_type_find_by_id (caps->id)->mime);
  if (caps->properties) {
    subtree = xmlNewChild (parent, NULL, "properties", NULL);

    gst_props_save_thyself (caps->properties, subtree);
  }

  return parent;
}

GstCaps*        
gst_caps_load_thyself (xmlNodePtr parent)
{
  GstCaps *caps = g_new0 (GstCaps, 1);
  xmlNodePtr field = parent->childs;

  while (field) {
    if (!strcmp (field->name, "type")) {
      caps->id = get_type_for_mime (xmlNodeGetContent (field));
    }
    else if (!strcmp (field->name, "properties")) {
      caps->properties = gst_props_load_thyself (field);
    }
    field = field->next;
  }

  return caps;
}



