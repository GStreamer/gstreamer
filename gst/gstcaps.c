/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstcaps.c: Element capabilities subsystem
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

//#define GST_DEBUG_ENABLED
#include "gst_private.h"

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
 * @name: the name of this capability
 * @mime: the mime type to attach to the capability
 *
 * create a new capability with the given mime type
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new (gchar *name, gchar *mime)
{
  GstCaps *caps;

  g_return_val_if_fail (mime != NULL, NULL);
  
  caps = g_new0 (GstCaps, 1);
  caps->name = g_strdup (name);
  caps->id = get_type_for_mime (mime);
  caps->properties = NULL;
  
  return caps;
}

/**
 * gst_caps_new_with_props:
 * @name: the name of this capability
 * @mime: the mime type to attach to the capability
 * @props: the properties for this capability
 *
 * create a new capability with the given mime type
 * and the given properties
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new_with_props (gchar *name, gchar *mime, GstProps *props)
{
  GstCaps *caps;
  
  caps = gst_caps_new (name, mime);
  caps->properties = props;

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
  guint dummy;

  return gst_caps_register_count (factory, &dummy);
}

/**
 * gst_caps_register_count:
 * @factory: the factory to register
 * @counter: count how many entries were consumed
 *
 * Register the factory. 
 *
 * Returns: The registered capability
 */
GstCaps*
gst_caps_register_count (GstCapsFactory *factory, guint *counter)
{
  GstCapsFactoryEntry tag;
  gint i = 0;
  guint16 typeid;
  gchar *name;
  GstCaps *caps;

  g_return_val_if_fail (factory != NULL, NULL);

  tag = (*factory)[i++];
  g_return_val_if_fail (tag != NULL, NULL);

  name = tag;

  tag = (*factory)[i++];
  g_return_val_if_fail (tag != NULL, NULL);
  
  typeid = get_type_for_mime ((gchar *)tag);

  caps = g_new0 (GstCaps, 1);
  g_return_val_if_fail (caps != NULL, NULL);

  caps->name = g_strdup (name);
  caps->id = typeid;
  caps->properties = gst_props_register_count (&(*factory)[i], counter);

  *counter += 2;

  return caps;
}

/**
 * gst_caps_set_props:
 * @caps: the caps to attach the properties to
 * @props: the properties to attach
 *
 * set the properties to the given caps
 *
 * Returns: The new caps structure
 */
GstCaps*
gst_caps_set_props (GstCaps *caps, GstProps *props)
{
  g_return_val_if_fail (caps != NULL, caps);
  g_return_val_if_fail (props != NULL, caps);
  g_return_val_if_fail (caps->properties == NULL, caps);

  caps->properties = props;
  
  return caps;
}

/**
 * gst_caps_get_props:
 * @caps: the caps to get the properties from
 *
 * get the properties of the given caps
 *
 * Returns: The properties of the caps
 */
GstProps*
gst_caps_get_props (GstCaps *caps)
{
  g_return_val_if_fail (caps != NULL, NULL);

  return caps->properties;
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
    DEBUG ("gstcaps: mime types wrong\n");
    return FALSE;
  }

  if (tocaps->properties) {
    if (fromcaps->properties) {
      return gst_props_check_compatibility (fromcaps->properties, tocaps->properties);
    }
    else {
      DEBUG ("gstcaps: no source caps\n");
      return FALSE;
    }
  }
  else {
    // assume it accepts everything
    DEBUG ("gstcaps: no caps\n");
    return TRUE;
  }
}

/**
 * gst_caps_list_check_compatibility:
 * @fromcaps: a capabilty
 * @tocaps: a capabilty
 *
 * Checks whether two capability lists are compatible
 *
 * Returns: true if compatible, false otherwise
 */
gboolean
gst_caps_list_check_compatibility (GList *fromcaps, GList *tocaps)
{
  while (fromcaps) {
    GstCaps *fromcap = (GstCaps *)fromcaps->data;
    GList *destcaps = tocaps;

    while (destcaps) {
      GstCaps *destcap = (GstCaps *)destcaps->data;

      if (gst_caps_check_compatibility (fromcap, destcap))
	return TRUE;

      destcaps = g_list_next (destcaps);
    }
    fromcaps = g_list_next (fromcaps);
  }
  return FALSE;
}

/**
 * gst_caps_save_thyself:
 * @caps: a capabilty to save
 * @parent: the parent XML node pointer
 *
 * save the capability into an XML representation
 *
 * Returns: a new XML node pointer
 */
xmlNodePtr      
gst_caps_save_thyself (GstCaps *caps, xmlNodePtr parent)
{
  xmlNodePtr subtree;

  g_return_val_if_fail (caps != NULL, NULL);

  xmlNewChild (parent, NULL, "name", caps->name);
  xmlNewChild (parent, NULL, "type", gst_type_find_by_id (caps->id)->mime);
  if (caps->properties) {
    subtree = xmlNewChild (parent, NULL, "properties", NULL);

    gst_props_save_thyself (caps->properties, subtree);
  }

  return parent;
}

/**
 * gst_caps_load_thyself:
 * @parent: the parent XML node pointer
 *
 * load a new caps from the XML representation
 *
 * Returns: a new capability
 */
GstCaps*        
gst_caps_load_thyself (xmlNodePtr parent)
{
  GstCaps *caps = g_new0 (GstCaps, 1);
  xmlNodePtr field = parent->childs;

  while (field) {
    if (!strcmp (field->name, "name")) {
      caps->name = g_strdup (xmlNodeGetContent (field));
    }
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



