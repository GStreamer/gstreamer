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

static GMemChunk *_gst_caps_chunk;
static GMutex *_gst_caps_chunk_lock;

void
_gst_caps_initialize (void)
{
  _gst_caps_chunk = g_mem_chunk_new ("GstCaps",
                  sizeof (GstCaps), sizeof (GstCaps) * 256,
                  G_ALLOC_AND_FREE);
  _gst_caps_chunk_lock = g_mutex_new ();
}

static guint16
get_type_for_mime (const gchar *mime)
{
  guint16 typeid;

  typeid = gst_type_find_by_mime (mime);
  if (typeid == 0) {
     GstTypeFactory factory; // = g_new0 (GstTypeFactory, 1);

     factory.mime = g_strdup (mime);
     factory.exts = NULL;
     factory.typefindfunc = NULL;

     typeid = gst_type_register (&factory);
  }
  return typeid;
}

/**
 * gst_caps_new:
 * @name: the name of this capability
 * @mime: the mime type to attach to the capability
 *
 * Create a new capability with the given mime type.
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new (const gchar *name, const gchar *mime)
{
  GstCaps *caps;

  g_return_val_if_fail (mime != NULL, NULL);

  g_mutex_lock (_gst_caps_chunk_lock);
  caps = g_mem_chunk_alloc (_gst_caps_chunk);
  g_mutex_unlock (_gst_caps_chunk_lock);

  caps->name = g_strdup (name);
  caps->id = get_type_for_mime (mime);
  caps->properties = NULL;
  caps->next = NULL;
  caps->refcount = 1;

  return caps;
}

/**
 * gst_caps_new_with_props:
 * @name: the name of this capability
 * @mime: the mime type to attach to the capability
 * @props: the properties for this capability
 *
 * Create a new capability with the given mime type and the given properties.
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new_with_props (const gchar *name, const gchar *mime, GstProps *props)
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
 * Returns: the registered capability
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
 * Returns: the registered capability
 */
GstCaps*
gst_caps_register_count (GstCapsFactory *factory, guint *counter)
{
  GstCapsFactoryEntry tag;
  gint i = 0;
  gchar *name;
  GstCaps *caps;

  g_return_val_if_fail (factory != NULL, NULL);

  tag = (*factory)[i++];
  g_return_val_if_fail (tag != NULL, NULL);

  name = tag;

  tag = (*factory)[i++];
  g_return_val_if_fail (tag != NULL, NULL);

  caps = gst_caps_new_with_props (name, (gchar *)tag,
                   gst_props_register_count (&(*factory)[i], counter));

  *counter += 2;

  return caps;
}

/**
 * gst_caps_destroy:
 * @caps: the caps to destroy
 *
 * Frees the memory used by this caps structure and all
 * the chained caps and properties.
 */
void
gst_caps_destroy (GstCaps *caps)
{
  g_return_if_fail (caps != NULL);

  if (caps->next) 
    gst_caps_unref (caps->next);

  g_free (caps->name);
  g_free (caps);
}

/**
 * gst_caps_unref:
 * @caps: the caps to unref
 *
 * Decrease the refcount of this caps structure, 
 * destroying it when the refcount is 0
 */
void
gst_caps_unref (GstCaps *caps)
{
  g_return_if_fail (caps != NULL);

  caps->refcount--;

  if (caps->next)
    gst_caps_unref (caps->next);

  if (caps->refcount == 0)
    gst_caps_destroy (caps);
}

/**
 * gst_caps_ref:
 * @caps: the caps to ref
 *
 * Increase the refcount of this caps structure
 */
void
gst_caps_ref (GstCaps *caps)
{
  g_return_if_fail (caps != NULL);

  caps->refcount++;
}

/**
 * gst_caps_copy_on_write:
 * @caps: the caps to copy
 *
 * Copies the caps if the refcount is greater than 1
 */
GstCaps*
gst_caps_copy (GstCaps *caps)
{
  GstCaps *new = caps;;

  g_return_val_if_fail (caps != NULL, NULL);

  new = gst_caps_new_with_props (
		  caps->name,
		  (gst_type_find_by_id (caps->id))->mime,
		  gst_props_copy (caps->properties));

  return new;
}

/**
 * gst_caps_copy_on_write:
 * @caps: the caps to copy
 *
 * Copies the caps if the refcount is greater than 1
 */
GstCaps*
gst_caps_copy_on_write (GstCaps *caps)
{
  GstCaps *new = caps;;

  g_return_val_if_fail (caps != NULL, NULL);

  if (caps->refcount > 1) {
    new = gst_caps_copy (caps);
    gst_caps_unref (caps);
  }

  return new;
}

/**
 * gst_caps_get_name:
 * @caps: the caps to get the name from
 *
 * Get the name of a GstCaps structure.
 *
 * Returns: the name of the caps
 */
const gchar*
gst_caps_get_name (GstCaps *caps)
{
  g_return_val_if_fail (caps != NULL, NULL);

  return (const gchar *)caps->name;
}

/**
 * gst_caps_set_name:
 * @caps: the caps to set the name to
 * @name: the name to set
 *
 * Set the name of a caps.
 */
void
gst_caps_set_name (GstCaps *caps, const gchar *name)
{
  g_return_if_fail (caps != NULL);

  if (caps->name)
    g_free (caps->name);

  caps->name = g_strdup (name);
}

/**
 * gst_caps_get_mime:
 * @caps: the caps to get the mime type from
 *
 * Get the mime type of the caps as a string.
 *
 * Returns: the mime type of the caps
 */
const gchar*
gst_caps_get_mime (GstCaps *caps)
{
  GstType *type;

  g_return_val_if_fail (caps != NULL, NULL);

  type = gst_type_find_by_id (caps->id);

  if (type)
    return type->mime;
  else
    return "unknown/unknown";
}

/**
 * gst_caps_set_mime:
 * @caps: the caps to set the mime type to
 * @mime: the mime type to attach to the caps
 *
 * Set the mime type of the caps as a string.
 */
void
gst_caps_set_mime (GstCaps *caps, const gchar *mime)
{
  g_return_if_fail (caps != NULL);
  g_return_if_fail (mime != NULL);

  caps->id = get_type_for_mime (mime);
}

/**
 * gst_caps_get_type_id:
 * @caps: the caps to get the type id from
 *
 * Get the type id of the caps.
 *
 * Returns: the type id of the caps
 */
guint16
gst_caps_get_type_id (GstCaps *caps)
{
  g_return_val_if_fail (caps != NULL, 0);

  return caps->id;
}

/**
 * gst_caps_set_type_id:
 * @caps: the caps to set the type id to
 * @typeid: the type id to set
 *
 * Set the type id of the caps.
 */
void
gst_caps_set_type_id (GstCaps *caps, guint16 type_id)
{
  g_return_if_fail (caps != NULL);

  caps->id = type_id;
}

/**
 * gst_caps_set_props:
 * @caps: the caps to attach the properties to
 * @props: the properties to attach
 *
 * Set the properties to the given caps.
 *
 * Returns: the new caps structure
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
 * Get the properties of the given caps.
 *
 * Returns: the properties of the caps
 */
GstProps*
gst_caps_get_props (GstCaps *caps)
{
  g_return_val_if_fail (caps != NULL, NULL);

  return caps->properties;
}

/**
 * gst_caps_append:
 * @caps: a capabilty
 * @capstoadd: the capability to append
 *
 * Appends a capability to the existing capability.
 *
 * Returns: the new capability
 */
GstCaps*
gst_caps_append (GstCaps *caps, GstCaps *capstoadd)
{
  GstCaps *orig = caps;

  if (caps == NULL)
    return capstoadd;
  
  while (caps->next) {
    caps = caps->next;
  }
  caps->next = capstoadd;

  return orig;
}

/**
 * gst_caps_prepend:
 * @caps: a capabilty
 * @capstoadd: a capabilty to prepend
 *
 * prepend the capability to the list of capabilities
 *
 * Returns: the new capability
 */
GstCaps*
gst_caps_prepend (GstCaps *caps, GstCaps *capstoadd)
{
  GstCaps *orig = capstoadd;
  
  if (capstoadd == NULL)
    return caps;

  while (capstoadd->next) {
    capstoadd = capstoadd->next;
  }
  capstoadd->next = caps;

  return orig;
}

GstCaps*
gst_caps_get_by_name (GstCaps *caps, const gchar *name)
{
  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);
   
  while (caps) {
    if (!strcmp (caps->name, name)) 
      return caps;
    caps = caps->next;
  }

  return NULL;
}
                                                                                                                   
static gboolean
gst_caps_check_compatibility_func (GstCaps *fromcaps, GstCaps *tocaps)
{
  if (fromcaps->id != tocaps->id) {
    GST_DEBUG (0,"gstcaps: mime types differ (%d to %d)\n",
	       fromcaps->id, tocaps->id);
    return FALSE;
  }

  if (tocaps->properties) {
    if (fromcaps->properties) {
      return gst_props_check_compatibility (fromcaps->properties, tocaps->properties);
    }
    else {
      GST_DEBUG (0,"gstcaps: no source caps\n");
      return FALSE;
    }
  }
  else {
    // assume it accepts everything
    GST_DEBUG (0,"gstcaps: no caps\n");
    return TRUE;
  }
}

/**
 * gst_caps_list_check_compatibility:
 * @fromcaps: a capabilty
 * @tocaps: a capabilty
 *
 * Checks whether two capability lists are compatible.
 *
 * Returns: TRUE if compatible, FALSE otherwise
 */
gboolean
gst_caps_check_compatibility (GstCaps *fromcaps, GstCaps *tocaps)
{
  if (fromcaps == NULL) {
    if (tocaps == NULL) {
      GST_DEBUG (0,"gstcaps: no caps\n");
      return TRUE;
    }
    else {
      GST_DEBUG (0,"gstcaps: no src but destination caps\n");
      return FALSE;
    }
  }
  else {
    if (tocaps == NULL) {
      GST_DEBUG (0,"gstcaps: src caps and no dest caps\n");
      return TRUE;
    }
  }

  while (fromcaps) {
    GstCaps *destcaps = tocaps;

    while (destcaps) {
      if (gst_caps_check_compatibility_func (fromcaps, destcaps))
	return TRUE;

      destcaps =  destcaps->next;
    }
    fromcaps =  fromcaps->next;
  }
  return FALSE;
}

/**
 * gst_caps_save_thyself:
 * @caps: a capabilty to save
 * @parent: the parent XML node pointer
 *
 * Save the capability into an XML representation.
 *
 * Returns: a new XML node pointer
 */
xmlNodePtr
gst_caps_save_thyself (GstCaps *caps, xmlNodePtr parent)
{
  xmlNodePtr subtree;
  xmlNodePtr subsubtree;

  while (caps) {
    subtree = xmlNewChild (parent, NULL, "capscomp", NULL);

    xmlNewChild (subtree, NULL, "name", caps->name);
    xmlNewChild (subtree, NULL, "type", gst_type_find_by_id (caps->id)->mime);
    if (caps->properties) {
      subsubtree = xmlNewChild (subtree, NULL, "properties", NULL);

      gst_props_save_thyself (caps->properties, subsubtree);
    }

    caps = caps->next;
  }

  return parent;
}

/**
 * gst_caps_load_thyself:
 * @parent: the parent XML node pointer
 *
 * Load a new caps from the XML representation.
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_load_thyself (xmlNodePtr parent)
{
  GstCaps *result = NULL;
  xmlNodePtr field = parent->xmlChildrenNode;

  while (field) {
    if (!strcmp (field->name, "capscomp")) {
      xmlNodePtr subfield = field->xmlChildrenNode;
      GstCaps *caps;
      gchar *content;

      g_mutex_lock (_gst_caps_chunk_lock);
      caps = g_mem_chunk_alloc0 (_gst_caps_chunk);
      g_mutex_unlock (_gst_caps_chunk_lock);

      caps->refcount = 1;
	
      while (subfield) {
        if (!strcmp (subfield->name, "name")) {
          caps->name = xmlNodeGetContent (subfield);
        }
        if (!strcmp (subfield->name, "type")) {
          content = xmlNodeGetContent (subfield);
          caps->id = get_type_for_mime (content);
          g_free (content);
        }
        else if (!strcmp (subfield->name, "properties")) {
          caps->properties = gst_props_load_thyself (subfield);
        }
	
        subfield = subfield->next;
      }
      result = gst_caps_append (result, caps);
    }
    field = field->next;
  }

  return result;
}



