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
     GstTypeDefinition definition;
     GstTypeFactory *factory;

     definition.name = "capstype";
     definition.mime = g_strdup (mime);
     definition.exts = NULL;
     definition.typefindfunc = NULL;

     factory = gst_typefactory_new (&definition);

     typeid = gst_type_register (factory);
  }
  return typeid;
}

/**
 * gst_caps_new:
 * @name: the name of this capability
 * @mime: the mime type to attach to the capability
 * @props: the properties to add to this capability
 *
 * Create a new capability with the given mime typei and properties.
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new (const gchar *name, const gchar *mime, GstProps *props)
{
  GstCaps *caps;

  g_return_val_if_fail (mime != NULL, NULL);

  g_mutex_lock (_gst_caps_chunk_lock);
  caps = g_mem_chunk_alloc (_gst_caps_chunk);
  g_mutex_unlock (_gst_caps_chunk_lock);

  caps->name = g_strdup (name);
  caps->id = get_type_for_mime (mime);
  caps->properties = props;
  caps->next = NULL;
  caps->refcount = 1;
  caps->lock = g_mutex_new ();

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
  GstCaps *next;

  g_return_if_fail (caps != NULL);

  GST_CAPS_LOCK (caps);
  next = caps->next;
  g_free (caps->name);
  g_free (caps);
  GST_CAPS_UNLOCK (caps);

  if (next) 
    gst_caps_unref (next);
}

/**
 * gst_caps_unref:
 * @caps: the caps to unref
 *
 * Decrease the refcount of this caps structure, 
 * destroying it when the refcount is 0
 *
 * Returns: caps or NULL if the refcount reached 0
 */
GstCaps*
gst_caps_unref (GstCaps *caps)
{
  gboolean zero;
  GstCaps **next;

  g_return_val_if_fail (caps != NULL, NULL);
  g_return_val_if_fail (caps->refcount > 0, NULL);

  GST_CAPS_LOCK (caps);
  caps->refcount--;
  zero = (caps->refcount == 0);
  next = &caps->next;
  GST_CAPS_UNLOCK (caps);

  if (*next)
    *next = gst_caps_unref (*next);

  if (zero) {
    gst_caps_destroy (caps);
    caps = NULL;
  }
  return caps;
}

/**
 * gst_caps_ref:
 * @caps: the caps to ref
 *
 * Increase the refcount of this caps structure
 *
 * Returns: the caps with the refcount incremented
 */
GstCaps*
gst_caps_ref (GstCaps *caps)
{
  g_return_val_if_fail (caps != NULL, NULL);

  GST_CAPS_LOCK (caps);
  caps->refcount++;
  GST_CAPS_UNLOCK (caps);

  return caps;
}

/**
 * gst_caps_copy:
 * @caps: the caps to copy
 *
 * Copies the caps.
 *
 * Returns: a copy of the GstCaps structure.
 */
GstCaps*
gst_caps_copy (GstCaps *caps)
{
  GstCaps *new = caps;;

  g_return_val_if_fail (caps != NULL, NULL);

  GST_CAPS_LOCK (caps);
  new = gst_caps_new (
		  caps->name,
		  (gst_type_find_by_id (caps->id))->mime,
		  gst_props_copy (caps->properties));
  GST_CAPS_UNLOCK (caps);

  return new;
}

/**
 * gst_caps_copy_on_write:
 * @caps: the caps to copy
 *
 * Copies the caps if the refcount is greater than 1
 *
 * Returns: a pointer to a GstCaps strcuture that can
 * be safely written to
 */
GstCaps*
gst_caps_copy_on_write (GstCaps *caps)
{
  GstCaps *new = caps;
  gboolean needcopy;

  g_return_val_if_fail (caps != NULL, NULL);

  GST_CAPS_LOCK (caps);
  needcopy = (caps->refcount > 1);
  GST_CAPS_UNLOCK (caps);

  if (needcopy) {
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
 * @type_id: the type id to set
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
 * gst_caps_chain:
 * @caps: a capabilty
 * @...: more capabilities
 *
 * chains the given capabilities
 *
 * Returns: the new capability
 */
GstCaps*
gst_caps_chain (GstCaps *caps, ...)
{
  GstCaps *orig = caps;
  va_list var_args;

  va_start (var_args, caps);

  while (caps) {
    GstCaps *toadd;
    
    toadd = va_arg (var_args, GstCaps*);
    gst_caps_append (caps, toadd);
    
    caps = toadd;
  }
  va_end (var_args);
  
  return orig;
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
  
  g_return_val_if_fail (caps != capstoadd, caps);

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
  
  g_return_val_if_fail (caps != capstoadd, caps);

  if (capstoadd == NULL)
    return caps;

  while (capstoadd->next) {
    capstoadd = capstoadd->next;
  }
  capstoadd->next = caps;

  return orig;
}

/**
 * gst_caps_get_by_name:
 * @caps: a capabilty
 * @name: the name of the capability to get
 *
 * Get the capability with the given name from this
 * chain of capabilities.
 *
 * Returns: the first capability in the chain with the 
 * given name
 */
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
    GST_DEBUG (GST_CAT_CAPS,"mime types differ (%s to %s)\n",
	       gst_type_find_by_id (fromcaps->id)->mime, 
	       gst_type_find_by_id (tocaps->id)->mime);
    return FALSE;
  }

  if (tocaps->properties) {
    if (fromcaps->properties) {
      return gst_props_check_compatibility (fromcaps->properties, tocaps->properties);
    }
    else {
      GST_DEBUG (GST_CAT_CAPS,"no source caps\n");
      return FALSE;
    }
  }
  else {
    // assume it accepts everything
    GST_DEBUG (GST_CAT_CAPS,"no caps\n");
    return TRUE;
  }
}

/**
 * gst_caps_check_compatibility:
 * @fromcaps: a capabilty
 * @tocaps: a capabilty
 *
 * Checks whether two capabilities are compatible.
 *
 * Returns: TRUE if compatible, FALSE otherwise
 */
gboolean
gst_caps_check_compatibility (GstCaps *fromcaps, GstCaps *tocaps)
{
  if (fromcaps == NULL) {
    if (tocaps == NULL) {
      GST_DEBUG (GST_CAT_CAPS,"no caps\n");
      return TRUE;
    }
    else {
      GST_DEBUG (GST_CAT_CAPS,"no source but destination caps\n");
      return FALSE;
    }
  }
  else {
    if (tocaps == NULL) {
      GST_DEBUG (GST_CAT_CAPS,"source caps and no destination caps\n");
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

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
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
      caps->lock = g_mutex_new ();
      caps->next = NULL;
	
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

#endif /* GST_DISABLE_LOADSAVE_REGISTRY */
