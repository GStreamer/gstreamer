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

#include "gst_private.h"

#include "gstcaps.h"
#include "gsttype.h"
#include "gstmemchunk.h"
#include "gstinfo.h"

#ifndef GST_DISABLE_TRACE
/* #define GST_WITH_ALLOC_TRACE */
#include "gsttrace.h"

static GstAllocTrace *_gst_caps_trace;
#endif

static GstMemChunk *_gst_caps_chunk;

GType _gst_caps_type;

extern GstProps *	__gst_props_from_string_func		(gchar *s, gchar **end, gboolean caps);
extern gboolean		__gst_props_parse_string		(gchar *r, gchar **end, gchar **next);

/* transform functions */
static void		gst_caps_transform_to_string		(const GValue *src_value, GValue *dest_value);

static void		gst_caps_destroy			(GstCaps *caps);


static void
gst_caps_transform_to_string (const GValue *src_value, GValue *dest_value)
{
  GstCaps *caps = g_value_peek_pointer (src_value);
  dest_value->data[0].v_pointer = gst_caps_to_string (caps);
}
/**
 * gst_caps_to_string:
 * caps: the caps to convert to a string
 *
 * Converts a #GstCaps into a readable format. This is mainly intended for
 * debugging purposes. You have to free the string using g_free.
 * A string converted with #gst_caps_to_string can always be converted back to
 * its caps representation using #gst_caps_from_string.
 *
 * Returns: A newly allocated string
 */
gchar *
gst_caps_to_string (GstCaps *caps)
{
  gchar *ret;
  GString *result;

  result = g_string_new ("");

  if(caps==NULL){
    g_string_append(result, "NULL");
  }

  while (caps) {
    gchar *props;
    GValue value = { 0, }; /* the important thing is that value.type = 0 */
    
    g_string_append_printf (result, "\"%s\"", gst_caps_get_mime (caps));

    if (caps->properties) {
      g_value_init (&value, GST_TYPE_PROPS);
      g_value_set_boxed  (&value, caps->properties);
      props = g_strdup_value_contents (&value);

      g_value_unset (&value);
      g_string_append (result, ", ");
      g_string_append (result, props);
      g_free (props);
    }

    caps = caps->next;
    if (caps)
      g_string_append (result, "; ");
  }
  ret = result->str;
  g_string_free (result, FALSE);
  return ret;
}

static GstCaps *
gst_caps_from_string_func (gchar *r)
{
  gchar *mime, *w;
  GstCaps *caps, *append;
  GstProps *props = NULL;

  mime = r;
  if (!__gst_props_parse_string (r, &w, &r)) goto error;
    
  if (*r == '\0') goto found;
  if (*r++ != ',') goto error;
  while (g_ascii_isspace (*r)) r++;
    
  props = __gst_props_from_string_func (r, &r, TRUE);
  if (!props) goto error;

found:
  *w = '\0';
  if (*mime == '\0') {
    gst_props_unref (props);
    goto error;
  }  
  caps = gst_caps_new ("parsed caps", mime, props);
  if (*r == '\0')
    return caps;
  
  while (g_ascii_isspace (*r)) r++;
  if (*r == ';') {
    r++;
    while (g_ascii_isspace (*r)) r++;
    append = gst_caps_from_string_func (r);
    if (!append) {
      gst_caps_unref (caps);
      goto error;
    }
    gst_caps_append (caps, append);
  }

  return caps;

error:
  return NULL;
}
/**
 * gst_caps_from_string:
 * str: the str to convert into caps
 *
 * Tries to convert a string into a #GstCaps. This is mainly intended for
 * debugging purposes. The returned caps are floating.
 *
 * Returns: A floating caps or NULL if the string couldn't be converted
 */
GstCaps *
gst_caps_from_string (gchar *str)
{
  gchar *s;
  GstCaps *caps;
  g_return_val_if_fail (str != NULL, NULL);  
 
  s = g_strdup (str);
  caps = gst_caps_from_string_func (s);
  g_free (s);

  return caps;
}
void
_gst_caps_initialize (void)
{
  _gst_caps_chunk = gst_mem_chunk_new ("GstCaps",
                  sizeof (GstCaps), sizeof (GstCaps) * 256,
                  G_ALLOC_AND_FREE);

  _gst_caps_type = g_boxed_type_register_static ("GstCaps",
                                       (GBoxedCopyFunc) gst_caps_ref,
                                       (GBoxedFreeFunc) gst_caps_unref);

  g_value_register_transform_func (_gst_caps_type, G_TYPE_STRING,
				   gst_caps_transform_to_string);

#ifndef GST_DISABLE_TRACE
  _gst_caps_trace = gst_alloc_trace_register (GST_CAPS_TRACE_NAME);
#endif
}

GType
gst_caps_get_type (void)
{
  return _gst_caps_type;
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

     factory = gst_type_factory_new (&definition);
     typeid = gst_type_register (factory);

     g_free (definition.mime);
  }
  return typeid;
}

/**
 * gst_caps_new:
 * @name: the name of this capability
 * @mime: the mime type to attach to the capability
 * @props: the properties to add to this capability
 *
 * Create a new capability with the given mime type and properties.
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new (const gchar *name, const gchar *mime, GstProps *props)
{
  g_return_val_if_fail (mime != NULL, NULL);

  return gst_caps_new_id (name, get_type_for_mime (mime), props);
}

/**
 * gst_caps_new_id:
 * @name: the name of this capability
 * @id: the id of the mime type 
 * @props: the properties to add to this capability
 *
 * Create a new capability with the given mime typeid and properties.
 *
 * Returns: a new capability
 */
GstCaps*
gst_caps_new_id (const gchar *name, const guint16 id, GstProps *props)
{
  GstCaps *caps;

  caps = gst_mem_chunk_alloc (_gst_caps_chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_gst_caps_trace, caps);
#endif

  GST_CAT_LOG (GST_CAT_CAPS, "new %p, props %p", caps, props);

  gst_props_ref (props);
  gst_props_sink (props);

  caps->name = g_strdup (name);
  caps->id = id;
  caps->properties = props;
  caps->next = NULL;
  caps->refcount = 1;
  GST_CAPS_FLAG_SET (caps, GST_CAPS_FLOATING);

  return caps;
}

/**
 * gst_caps_get_any:
 *
 * Return a copy of the caps that represents any capability.
 *
 * Returns: the ANY capability
 */
GstCaps*
gst_caps_get_any (void)
{
#if 0
  static GstCaps *caps;

  if (!caps){
    caps = GST_CAPS_NEW ("gst_caps_any", "*", NULL);
    gst_caps_ref(caps);
    gst_caps_sink(caps);
  }

  return gst_caps_ref(caps);
#else
  return NULL;
#endif
}

/**
 * gst_caps_replace:
 * @oldcaps: the caps to take replace
 * @newcaps: the caps to take replace 
 *
 * Replace the pointer to the caps, doing proper
 * refcounting.
 */
void
gst_caps_replace (GstCaps **oldcaps, GstCaps *newcaps)
{
  if (*oldcaps != newcaps) {
    if (newcaps)  gst_caps_ref   (newcaps);
    if (*oldcaps) gst_caps_unref (*oldcaps);

    *oldcaps = newcaps;
  }
}

/**
 * gst_caps_replace_sink:
 * @oldcaps: the caps to take replace
 * @newcaps: the caps to take replace 
 *
 * Replace the pointer to the caps and take ownership.
 */
void
gst_caps_replace_sink (GstCaps **oldcaps, GstCaps *newcaps)
{
  gst_caps_replace (oldcaps, newcaps);
  gst_caps_sink (newcaps);
}

/**
 * gst_caps_destroy:
 * @caps: the caps to destroy
 *
 * Frees the memory used by this caps structure and all
 * the chained caps and properties.
 */
static void
gst_caps_destroy (GstCaps *caps)
{
  GstCaps *next;

  if (caps == NULL)
    return;

  next = caps->next;

  GST_CAT_LOG (GST_CAT_CAPS, "destroy %p", caps);

  gst_props_unref (caps->properties);
  g_free (caps->name);

#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_gst_caps_trace, caps);
#endif
  gst_mem_chunk_free (_gst_caps_chunk, caps);

  if (next) 
    gst_caps_unref (next);
}

/**
 * gst_caps_debug:
 * @caps: the caps to print out
 * @label: a label to put on the printout, or NULL
 *
 * Print out the contents of the caps structure. Useful for debugging.
 */
void
gst_caps_debug (GstCaps *caps, const gchar *label)
{
  GST_CAT_DEBUG (GST_CAT_CAPS, "starting caps debug: %s", label);
  if (caps && caps->refcount == 0) {
    g_warning ("Warning: refcount of caps %s is 0", label);
    return;
  }

  while (caps) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "caps: %p %s %s (%sfixed) (refcount %d) %s", 
	       caps, caps->name, gst_caps_get_mime (caps), 
               GST_CAPS_IS_FIXED (caps) ? "" : "NOT ", caps->refcount,
               GST_CAPS_IS_FLOATING (caps) ? "FLOATING" : "");

    if (caps->properties) {
      gst_props_debug (caps->properties);
    }
    else {
      GST_CAT_DEBUG (GST_CAT_CAPS, "no properties");
    }

    caps = caps->next;
  }
  GST_CAT_DEBUG (GST_CAT_CAPS, "finished caps debug");
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

  if (caps == NULL)
    return NULL;

  g_return_val_if_fail (caps->refcount > 0, NULL);

  GST_CAT_LOG (GST_CAT_CAPS, "unref %p (%d->%d) %d", 
	     caps, caps->refcount, caps->refcount-1, GST_CAPS_FLAGS (caps));

  caps->refcount--;
  zero = (caps->refcount == 0);

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
  if (caps == NULL)
    return NULL;

  g_return_val_if_fail (caps->refcount > 0, NULL);

  GST_CAT_LOG (GST_CAT_CAPS, "ref %p (%d->%d) %d", 
	     caps, caps->refcount, caps->refcount+1, GST_CAPS_FLAGS (caps));

  caps->refcount++;

  return caps;
}

/**
 * gst_caps_sink:
 * @caps: the caps to take ownership of
 *
 * Take ownership of a GstCaps
 */
void
gst_caps_sink (GstCaps *caps)
{
  if (caps == NULL)
    return;

  if (GST_CAPS_IS_FLOATING (caps)) {
    GST_CAT_LOG (GST_CAT_CAPS, "sink %p", caps);

    GST_CAPS_FLAG_UNSET (caps, GST_CAPS_FLOATING);
    gst_caps_unref (caps);
  }
}

/**
 * gst_caps_copy_1:
 * @caps: the caps to copy
 *
 * Copies the caps, not copying any chained caps.
 *
 * Returns: a floating copy of the GstCaps structure.
 */
GstCaps*
gst_caps_copy_1 (GstCaps *caps)
{
  GstCaps *newcaps;
  
  if (!caps)
    return NULL;

  newcaps = gst_caps_new_id (
		  caps->name,
		  caps->id,
		  gst_props_copy (caps->properties));

  return newcaps;
}

/**
 * gst_caps_copy:
 * @caps: the caps to copy
 *
 * Copies the caps.
 *
 * Returns: a floating copy of the GstCaps structure.
 */
GstCaps*
gst_caps_copy (GstCaps *caps)
{
  GstCaps *new = NULL, *walk = NULL;

  while (caps) {
    GstCaps *newcaps;

    newcaps = gst_caps_copy_1 (caps);

    if (new == NULL) {
      new = walk = newcaps;
    }
    else {
      walk = walk->next = newcaps;
    }
    caps = caps->next;
  }

  return new;
}

/**
 * gst_caps_copy_on_write:
 * @caps: the caps to copy
 *
 * Copies the caps if the refcount is greater than 1
 *
 * Returns: a pointer to a GstCaps strcuture that can
 * be safely written to.
 */
GstCaps*
gst_caps_copy_on_write (GstCaps *caps)
{
  GstCaps *new = caps;
  gboolean needcopy;

  g_return_val_if_fail (caps != NULL, NULL);

  needcopy = (caps->refcount > 1);

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

  gst_props_replace_sink (&caps->properties, props);

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
 * gst_caps_has_property:
 * @caps: the caps to query
 * @name: the name of the property to search for
 *
 * Figure out whether this caps contains the requested property.
 *
 * Returns: true if the caps contains the property.
 */

gboolean
gst_caps_has_property (GstCaps *caps, const gchar *name)
{
  GstProps *props = gst_caps_get_props (caps);

  return (props != NULL &&
	  gst_props_has_property (props, name));
}

/**
 * gst_caps_has_property_typed:
 * @caps: the caps to query
 * @name: the name of the property to search for
 * @type: the type of the property to search for
 *
 * Figure out whether this caps contains the requested property,
 * and whether this property is of the requested type.
 *
 * Returns: true if the caps contains the typed property.
 */

gboolean
gst_caps_has_property_typed (GstCaps *caps, const gchar *name, GstPropsType type)
{
  GstProps *props = gst_caps_get_props (caps);

  return (props != NULL &&
	  gst_props_has_property_typed (props, name, type));
}

/**
 * gst_caps_has_fixed_property
 * @caps: the caps to query
 * @name: the name of the property to search for
 *
 * Figure out whether this caps contains the requested property,
 * and whether this property is fixed.
 *
 * Returns: true if the caps contains the fixed property.
 */

gboolean
gst_caps_has_fixed_property (GstCaps *caps, const gchar *name)
{
  GstProps *props = gst_caps_get_props (caps);

  return (props != NULL &&
	  gst_props_has_fixed_property (props, name));
}

/**
 * gst_caps_next:
 * @caps: the caps to query
 *
 * Get the next caps of this chained caps.
 *
 * Returns: the next caps or NULL if the chain ended.
 */
GstCaps*
gst_caps_next (GstCaps *caps)
{
  if (caps == NULL)
    return NULL;

  return caps->next;
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
  
  if (caps == NULL || caps == capstoadd)
    return capstoadd;
  
  while (caps->next) {
    caps = caps->next;
  }
  gst_caps_replace_sink (&caps->next, capstoadd);

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

  g_return_val_if_fail (caps != capstoadd, caps);

  while (capstoadd->next) {
    capstoadd = capstoadd->next;
  }
  gst_caps_replace_sink (&capstoadd->next, caps);

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
    GST_CAT_DEBUG (GST_CAT_CAPS,"mime types differ (%s to %s)",
	       gst_type_find_by_id (fromcaps->id)->mime, 
	       gst_type_find_by_id (tocaps->id)->mime);
    return FALSE;
  }

  if (tocaps->properties) {
    if (fromcaps->properties) {
      return gst_props_check_compatibility (fromcaps->properties, tocaps->properties);
    }
    else {
      GST_CAT_DEBUG (GST_CAT_CAPS,"no source caps");
      return FALSE;
    }
  }
  else {
    /* assume it accepts everything */
    GST_CAT_DEBUG (GST_CAT_CAPS,"no caps");
    return TRUE;
  }
}

/**
 * gst_caps_is_always_compatible:
 * @fromcaps: a #GstCaps capability to check compatibility of.
 * @tocaps: the #GstCaps capability to check compatibility with.
 *
 * Checks if a link is always possible from fromcaps to tocaps, for all
 * possible capabilities.
 *
 * Returns: TRUE if compatible under all circumstances, FALSE otherwise.
 */
gboolean
gst_caps_is_always_compatible (GstCaps *fromcaps, GstCaps *tocaps)
{
  if (fromcaps == NULL) {
    if (tocaps == NULL) {
      /* if both are NULL, they can always link.  Think filesrc ! filesink */
      GST_CAT_DEBUG (GST_CAT_CAPS, "both caps NULL, compatible");
      return TRUE;
    }
    else {
      /* if source caps are NULL, it could be sending anything, so the
       * destination can't know if it can accept this.  Think filesrc ! mad */
      GST_CAT_DEBUG (GST_CAT_CAPS, "source caps NULL, not guaranteed compatible");
      return FALSE;
    }
  }
  else {
    if (tocaps == NULL) {
      /* if the dest caps are NULL, the element can accept anything, always,
       * so they're compatible by definition.  Think mad ! filesink */
      GST_CAT_DEBUG (GST_CAT_CAPS,"destination caps NULL");
      return TRUE;
    }
  }

  while (fromcaps) {
    GstCaps *destcaps = tocaps;
    /* assume caps is incompatible */
    gboolean compat = FALSE;

    while (destcaps && !compat) {
      if (gst_caps_check_compatibility_func (fromcaps, destcaps)) {
	compat = TRUE;
      }
      destcaps =  destcaps->next;
    }
    if (!compat)
      return FALSE;

    fromcaps =  fromcaps->next;
  }
  return TRUE;
}

static GstCaps*
gst_caps_intersect_func (GstCaps *caps1, GstCaps *caps2)
{
  GstCaps *result = NULL;
  GstProps *props;

  if (caps1->id != caps2->id) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "mime types differ (%s to %s)",
	       gst_type_find_by_id (caps1->id)->mime, 
	       gst_type_find_by_id (caps2->id)->mime);
    return NULL;
  }

  if (caps1->properties == NULL) {
    return gst_caps_ref (caps2);
  }
  if (caps2->properties == NULL) {
    return gst_caps_ref (caps1);
  }
  
  props = gst_props_intersect (caps1->properties, caps2->properties);
  if (props) {
    result = gst_caps_new_id ("intersect", caps1->id, props);
    gst_caps_ref (result);
    gst_caps_sink (result);
  }

  return result;
}

/**
 * gst_caps_intersect:
 * @caps1: a capability
 * @caps2: a capability
 *
 * Make the intersection between two caps.
 *
 * Returns: The intersection of the two caps or NULL if the intersection
 * is empty. unref the caps after use.
 */
GstCaps*
gst_caps_intersect (GstCaps *caps1, GstCaps *caps2)
{
  GstCaps *result = NULL, *walk = NULL;

  /* printing the name is not useful here since caps can be chained */
  GST_CAT_DEBUG (GST_CAT_CAPS, "intersecting caps %p and %p", caps1, caps2);
		  
  if (caps1 == NULL) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "first caps is NULL, return other caps");
    return gst_caps_ref (caps2);
  }
  if (caps2 == NULL) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "second caps is NULL, return other caps");
    return gst_caps_ref (caps1);
  }

  /* same caps */
  if (caps1 == caps2) {
    return gst_caps_ref (caps1);
  }

  while (caps1) {
    GstCaps *othercaps = caps2;

    while (othercaps) {
      GstCaps *intersection;
      
      intersection = gst_caps_intersect_func (caps1, othercaps);

      if (intersection) {
        if (!result) {
  	  walk = result = intersection;
        }
        else {
	  walk = walk->next = intersection;
        }
      }
      othercaps = othercaps->next;
    }
    caps1 = caps1->next;
  }

  return result;
}

GstCaps*
gst_caps_union (GstCaps *caps1, GstCaps *caps2)
{
  GstCaps *result = NULL;

  /* printing the name is not useful here since caps can be chained */
  GST_CAT_DEBUG (GST_CAT_CAPS, "making union of caps %p and %p", caps1, caps2);
		  
  if (caps1 == NULL) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "first caps is NULL, return other caps");
    return gst_caps_ref (caps2);
  }
  if (caps2 == NULL) {
    GST_CAT_DEBUG (GST_CAT_CAPS, "second caps is NULL, return other caps");
    return gst_caps_ref (caps1);
  }

  return result;
}

/**
 * gst_caps_normalize:
 * @caps: a capabilty
 *
 * Make the normalisation of the caps. This will return a new caps
 * that is equivalent to the input caps with the exception that all
 * lists are unrolled. This function is useful when you want to iterate
 * the caps. unref the caps after use.
 *
 * Returns: The normalisation of the caps. Unref after usage.
 */
GstCaps*
gst_caps_normalize (GstCaps *caps)
{
  GstCaps *result = NULL, *walk;

  if (caps == NULL)
    return caps;

  GST_CAT_DEBUG (GST_CAT_CAPS, "normalizing caps %p ", caps);

  walk = caps;

  while (caps) {
    GList *proplist;

    proplist = gst_props_normalize (caps->properties);
    while (proplist) {
      GstProps *props = (GstProps *) proplist->data;
      GstCaps *newcaps = gst_caps_new_id (caps->name, caps->id, props);

      gst_caps_ref (newcaps);
      gst_caps_sink (newcaps);

      if (result == NULL)
	walk = result = newcaps;
      else {
 	walk = walk->next = newcaps;
      }
      proplist = g_list_next (proplist);  
    }
    caps = caps->next;
  }
  return result;
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

      caps = gst_mem_chunk_alloc0 (_gst_caps_chunk);
#ifndef GST_DISABLE_TRACE
      gst_alloc_trace_new (_gst_caps_trace, caps);
#endif

      caps->refcount = 1;
      GST_CAPS_FLAG_SET (caps, GST_CAPS_FLOATING);
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
          GstProps *props = gst_props_load_thyself (subfield);

	  gst_props_ref (props);
	  gst_props_sink (props);
          caps->properties = props;
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
