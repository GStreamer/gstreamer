/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstprops.c: Properties subsystem for generic usage
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

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gstlog.h"
#include "gstprops.h"
#include "gstpropsprivate.h"

static GMemChunk *_gst_props_entries_chunk;
static GMutex *_gst_props_entries_chunk_lock;

static GMemChunk *_gst_props_chunk;
static GMutex *_gst_props_chunk_lock;

static gboolean 	gst_props_entry_check_compatibility 	(GstPropsEntry *entry1, GstPropsEntry *entry2);
	
void 
_gst_props_initialize (void) 
{
  _gst_props_entries_chunk = g_mem_chunk_new ("GstPropsEntries", 
		  sizeof (GstPropsEntry), sizeof (GstPropsEntry) * 256, 
		  G_ALLOC_AND_FREE);
  _gst_props_entries_chunk_lock = g_mutex_new ();

  _gst_props_chunk = g_mem_chunk_new ("GstProps", 
		  sizeof (GstProps), sizeof (GstProps) * 256, 
		  G_ALLOC_AND_FREE);
  _gst_props_chunk_lock = g_mutex_new ();
}

static void
gst_props_debug_entry (GstPropsEntry *entry)
{
  switch (entry->propstype) {
    case GST_PROPS_INT_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "%d\n", entry->data.int_data);
      break;
    case GST_PROPS_FLOAT_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "%f\n", entry->data.float_data);
      break;
    case GST_PROPS_FOURCC_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "%4.4s\n", (gchar*)&entry->data.fourcc_data);
      break;
    case GST_PROPS_BOOL_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "%d\n", entry->data.bool_data);
      break;
    case GST_PROPS_STRING_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s\n", entry->data.string_data.string);
      break;
    case GST_PROPS_INT_RANGE_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "%d-%d\n", entry->data.int_range_data.min,
		      entry->data.int_range_data.max);
      break;
    case GST_PROPS_FLOAT_RANGE_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "%f-%f\n", entry->data.float_range_data.min,
		      entry->data.float_range_data.max);
      break;
    case GST_PROPS_LIST_ID:
      GST_DEBUG (GST_CAT_PROPERTIES, "[list]\n");
    default:
      break;
  }
}

static gint 
props_compare_func (gconstpointer a,
		    gconstpointer b) 
{
  GstPropsEntry *entry1 = (GstPropsEntry *)a;
  GstPropsEntry *entry2 = (GstPropsEntry *)b;

  return (entry1->propid - entry2->propid);
}

static gint 
props_find_func (gconstpointer a,
		 gconstpointer b) 
{
  GstPropsEntry *entry2 = (GstPropsEntry *)a;
  GQuark entry1 = (GQuark) GPOINTER_TO_INT (b);

  return (entry1 - entry2->propid);
}

/* This is implemented as a huge macro because we cannot pass
 * va_list variables by reference on some architectures.
 */
#define GST_PROPS_ENTRY_FILL(entry, var_args) 					\
G_STMT_START { 									\
  entry->propstype = va_arg (var_args, GstPropsId); 				\
										\
  switch (entry->propstype) {							\
    case GST_PROPS_INT_ID:							\
      entry->data.int_data = va_arg (var_args, gint);				\
      break;									\
    case GST_PROPS_INT_RANGE_ID:						\
      entry->data.int_range_data.min = va_arg (var_args, gint);			\
      entry->data.int_range_data.max = va_arg (var_args, gint);			\
      break;									\
    case GST_PROPS_FLOAT_ID:							\
      entry->data.float_data = va_arg (var_args, gdouble);			\
      break;									\
    case GST_PROPS_FLOAT_RANGE_ID:						\
      entry->data.float_range_data.min = va_arg (var_args, gdouble);		\
      entry->data.float_range_data.max = va_arg (var_args, gdouble);		\
      break;									\
    case GST_PROPS_FOURCC_ID:							\
      entry->data.fourcc_data = va_arg (var_args, gulong);			\
      break;									\
    case GST_PROPS_BOOL_ID:							\
      entry->data.bool_data = va_arg (var_args, gboolean);			\
      break;									\
    case GST_PROPS_STRING_ID:							\
      entry->data.string_data.string = g_strdup (va_arg (var_args, gchar*));	\
      break;									\
    default:									\
      break;									\
  }										\
} G_STMT_END

/**
 * gst_props_new:
 * @firstname: the first property name
 * @...: the property values 
 *
 * Create a new property from the given key/value pairs
 *
 * Returns: the new property
 */
GstProps*
gst_props_new (const gchar *firstname, ...)
{
  GstProps *props;
  va_list var_args;
  
  va_start (var_args, firstname);

  props = gst_props_newv (firstname, var_args);
  
  va_end (var_args);
  
  return props;
} 

/**
 * gst_props_add_to_list:
 * @entries: the existing list of entries
 * @entry: the new entry to add to the list
 *
 * Add a property to a list of properties.
 *
 * Returns: a pointer to a list with the new entry added.
 */
static GList *
gst_props_add_to_list (GList * entries, GstPropsEntry *entry)
{
  return g_list_prepend (entries, entry);
}

/**
 * gst_props_merge_int_entries:
 * @newentry: the new entry
 * @oldentry: an old entry
 *
 * Tries to merge oldentry into newentry, if there is a simpler single entry which represents
 *
 * Assumes that the entries are either ints or int ranges.
 *
 * Returns: TRUE if the entries were merged, FALSE otherwise.
 */
static gboolean
gst_props_merge_int_entries(GstPropsEntry * newentry, GstPropsEntry * oldentry)
{
  gint new_min, new_max, old_min, old_max;
  gboolean can_merge = FALSE;

  if (newentry->propstype == GST_PROPS_INT_ID) {
    new_min = newentry->data.int_data;
    new_max = newentry->data.int_data;
  } else {
    new_min = newentry->data.int_range_data.min;
    new_max = newentry->data.int_range_data.max;
  }

  if (oldentry->propstype == GST_PROPS_INT_ID) {
    old_min = oldentry->data.int_data;
    old_max = oldentry->data.int_data;
  } else {
    old_min = oldentry->data.int_range_data.min;
    old_max = oldentry->data.int_range_data.max;
  }

  /* Put range which starts lower into (new_min, new_max) */
  if (old_min < new_min) {
    gint tmp;
    tmp = old_min;
    old_min = new_min;
    new_min = tmp;
    tmp = old_max;
    old_max = new_max;
    new_max = tmp;
  }

  /* new_min is min of either entry - second half of the following conditional */
  /* is to avoid overflow problems. */
  if (new_max >= old_min - 1 && old_min - 1 < old_min) {
    /* ranges overlap, or are adjacent.  Pick biggest maximum. */
    can_merge = TRUE;
    if (old_max > new_max) new_max = old_max;
  }

  if (can_merge) {
    if (new_min == new_max) {
      newentry->propstype = GST_PROPS_INT_ID;
      newentry->data.int_data = new_min;
    } else {
      newentry->propstype = GST_PROPS_INT_RANGE_ID;
      newentry->data.int_range_data.min = new_min;
      newentry->data.int_range_data.max = new_max;
    }
  }
  return can_merge;
}

/**
 * gst_props_add_to_int_list:
 * @entries: the existing list of entries
 * @entry: the new entry to add to the list
 *
 * Add an integer property to a list of properties, removing duplicates
 * and merging ranges.
 *
 * Assumes that the existing list is in simplest form, contains
 * only ints and int ranges, and that the new entry is an int or 
 * an int range.
 *
 * Returns: a pointer to a list with the new entry added.
 */
static GList *
gst_props_add_to_int_list (GList * entries, GstPropsEntry * newentry)
{
  GList * i;

  i = entries;
  while (i) {
    GstPropsEntry * oldentry = (GstPropsEntry *)(i->data);
    gboolean merged = gst_props_merge_int_entries(newentry, oldentry);

    if (merged) {
      /* replace the existing one with the merged one */
      g_mutex_lock (_gst_props_entries_chunk_lock);
      g_mem_chunk_free (_gst_props_entries_chunk, oldentry);
      g_mutex_unlock (_gst_props_entries_chunk_lock);
      entries = g_list_remove_link (entries, i);
      g_list_free_1 (i);

      /* start again: it's possible that this change made an earlier entry */
      /* mergeable, and the pointer is now invalid anyway. */
      i = entries;
    }

    i = g_list_next (i);
  }

  return gst_props_add_to_list (entries, newentry);
}

/**
 * gst_props_newv:
 * @firstname: the first property name
 * @var_args: the property values
 *
 * Create a new property from the list of entries.
 *
 * Returns: the new property created from the list of entries
 */
GstProps*
gst_props_newv (const gchar *firstname, va_list var_args)
{
  GstProps *props;
  gboolean inlist = FALSE;
  const gchar *prop_name;
  GstPropsEntry *list_entry = NULL;

  typedef enum {
      GST_PROPS_LIST_T_UNSET,
      GST_PROPS_LIST_T_INTS,
      GST_PROPS_LIST_T_FLOATS,
      GST_PROPS_LIST_T_MISC,
  } list_types;

  /* type of the list */
  list_types list_type = GST_PROPS_LIST_T_UNSET;
  /* type of current item */
  list_types entry_type = GST_PROPS_LIST_T_UNSET;

  if (firstname == NULL)
    return NULL;

  g_mutex_lock (_gst_props_chunk_lock);
  props = g_mem_chunk_alloc (_gst_props_chunk);
  g_mutex_unlock (_gst_props_chunk_lock);

  props->properties = NULL;
  props->refcount = 1;

  prop_name = firstname;

  /* properties */
  while (prop_name) {
    GstPropsEntry *entry;
    
    g_mutex_lock (_gst_props_entries_chunk_lock);
    entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
    g_mutex_unlock (_gst_props_entries_chunk_lock);

    entry->propid = g_quark_from_string (prop_name);
    GST_PROPS_ENTRY_FILL (entry, var_args);

    switch (entry->propstype) {
      case GST_PROPS_INT_ID:
      case GST_PROPS_INT_RANGE_ID:
	entry_type = GST_PROPS_LIST_T_INTS;
	break;
      case GST_PROPS_FLOAT_ID:
      case GST_PROPS_FLOAT_RANGE_ID:
	entry_type = GST_PROPS_LIST_T_FLOATS;
	break;
      case GST_PROPS_FOURCC_ID:
      case GST_PROPS_BOOL_ID:
      case GST_PROPS_STRING_ID:
	entry_type = GST_PROPS_LIST_T_MISC;
	break;
      case GST_PROPS_LIST_ID:
	g_return_val_if_fail (inlist == FALSE, NULL);
	inlist = TRUE;
	list_entry = entry;
	list_type = GST_PROPS_LIST_T_UNSET;
	list_entry->data.list_data.entries = NULL;
	break;
      case GST_PROPS_END_ID:
	g_return_val_if_fail (inlist == TRUE, NULL);

	/* if list was of size 1, replace the list by a the item it contains */
	if (g_list_length(list_entry->data.list_data.entries) == 1) {
	  GstPropsEntry * subentry = (GstPropsEntry *)(list_entry->data.list_data.entries->data);
	  list_entry->propstype = subentry->propstype;
	  list_entry->data = subentry->data;
	  g_mutex_lock (_gst_props_entries_chunk_lock);
	  g_mem_chunk_free (_gst_props_entries_chunk, subentry);
	  g_mutex_unlock (_gst_props_entries_chunk_lock);
	}

        g_mutex_lock (_gst_props_entries_chunk_lock);
        g_mem_chunk_free (_gst_props_entries_chunk, entry);
        g_mutex_unlock (_gst_props_entries_chunk_lock);
	inlist = FALSE;
	list_entry = NULL;
        prop_name = va_arg (var_args, gchar*);
	continue;
      default:
	g_warning ("unknown property type found %d for '%s'\n", entry->propstype, prop_name);
        g_mutex_lock (_gst_props_entries_chunk_lock);
        g_mem_chunk_free (_gst_props_entries_chunk, entry);
        g_mutex_unlock (_gst_props_entries_chunk_lock);
	break;
    }

    if (inlist && (list_entry != entry)) {
      if (list_type == GST_PROPS_LIST_T_UNSET) list_type = entry_type;
      if (list_type != entry_type) {
	g_warning ("property list contained incompatible entry types\n");
      } else {
	switch (list_type) {
	  case GST_PROPS_LIST_T_INTS:
	    list_entry->data.list_data.entries =
		    gst_props_add_to_int_list (list_entry->data.list_data.entries, entry);
	    break;
	  default:
	    list_entry->data.list_data.entries =
		    gst_props_add_to_list (list_entry->data.list_data.entries, entry);
	    break;
	}
      }
    }
    else {
      props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
    }
    if (!inlist)
      prop_name = va_arg (var_args, gchar*);
  }

  return props;
}

/**
 * gst_props_set:
 * @props: the props to modify
 * @name: the name of the entry to modify
 * @...: More property entries.
 *
 * Modifies the value of the given entry in the props struct.
 *
 * Returns: the new modified property structure.
 */
GstProps*
gst_props_set (GstProps *props, const gchar *name, ...)
{
  GQuark quark;
  GList *lentry;
  va_list var_args;
  
  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *entry;

    entry = (GstPropsEntry *)lentry->data;

    va_start (var_args, name);

    GST_PROPS_ENTRY_FILL (entry, var_args);

    va_end (var_args);
  }
  else {
    g_print("gstprops: no property '%s' to change\n", name);
  }

  return props;
}

/**
 * gst_props_unref:
 * @props: the props to unref
 *
 * Decrease the refcount of the property structure, destroying
 * the property if the refcount is 0.
 */
void
gst_props_unref (GstProps *props)
{
  g_return_if_fail (props != NULL);
  
  props->refcount--;

  if (props->refcount == 0)
    gst_props_destroy (props);
}

/**
 * gst_props_ref:
 * @props: the props to ref
 *
 * Increase the refcount of the property structure.
 */
void
gst_props_ref (GstProps *props)
{
  g_return_if_fail (props != NULL);
  
  props->refcount++;
}

/**
 * gst_props_destroy:
 * @props: the props to destroy
 *
 * Destroy the property, freeing all the memory that
 * was allocated.
 */
void
gst_props_destroy (GstProps *props)
{
  GList *entries;

  g_return_if_fail (props != NULL);
  
  entries = props->properties;

  while (entries) {
    GstPropsEntry *entry = (GstPropsEntry *)entries->data;

    switch (entry->propstype) {
      case GST_PROPS_STRING_ID:		
        g_free (entry->data.string_data.string);
        break;				
      /* FIXME also free the lists */
      default:			
        break;		
    }
    g_mutex_lock (_gst_props_entries_chunk_lock);
    g_mem_chunk_free (_gst_props_entries_chunk, entry);
    g_mutex_unlock (_gst_props_entries_chunk_lock);

    entries = g_list_next (entries);
  }
  g_list_free (props->properties);

  g_mutex_lock (_gst_props_chunk_lock);
  g_mem_chunk_free (_gst_props_chunk, props);
  g_mutex_unlock (_gst_props_chunk_lock);
}

/**
 * gst_props_copy:
 * @props: the props to copy
 *
 * Copy the property structure.
 *
 * Returns: the new property that is a copy of the original
 * one.
 */
GstProps*
gst_props_copy (GstProps *props)
{
  GstProps *new;
  GList *properties;

  g_return_val_if_fail (props != NULL, NULL);

  g_mutex_lock (_gst_props_chunk_lock);
  new = g_mem_chunk_alloc (_gst_props_chunk);
  g_mutex_unlock (_gst_props_chunk_lock);

  new->properties = NULL;

  properties = props->properties;

  while (properties) {
    GstPropsEntry *entry = (GstPropsEntry *)properties->data;
    GstPropsEntry *newentry;

    g_mutex_lock (_gst_props_entries_chunk_lock);
    newentry = g_mem_chunk_alloc (_gst_props_entries_chunk);
    g_mutex_unlock (_gst_props_entries_chunk_lock);

    /* FIXME copy lists too */
    memcpy (newentry, entry, sizeof (GstPropsEntry));

    new->properties = g_list_prepend (new->properties, newentry);
    
    properties = g_list_next (properties);
  }
  new->properties = g_list_reverse (new->properties);

  return new;
}

/**
 * gst_props_copy_on_write:
 * @props: the props to copy on write
 *
 * Copy the property structure if the refcount is >1.
 *
 * Returns: A new props that can be safely written to.
 */
GstProps*
gst_props_copy_on_write (GstProps *props)
{
  GstProps *new = props;;

  g_return_val_if_fail (props != NULL, NULL);

  if (props->refcount > 1) {
    new = gst_props_copy (props);
    gst_props_unref (props);
  }

  return props;
}

/**
 * gst_props_get_int:
 * @props: the props to get the int value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as an integer.
 *
 * Returns: the integer value of the named entry, 0 if not found.
 */
gint
gst_props_get_int (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  g_return_val_if_fail (props != NULL, 0);
  g_return_val_if_fail (name != NULL, 0);

  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.int_data;
  }
  
  return 0;
}

/**
 * gst_props_get_float:
 * @props: the props to get the float value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as a float.
 *
 * Returns: the float value of the named entry, 0.0 if not found.
 */
gfloat
gst_props_get_float (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.float_data;
  }
  
  return 0.0F;
}

/**
 * gst_props_get_fourcc_int:
 * @props: the props to get the fourcc value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as a gulong fourcc.
 *
 * Returns: the fourcc value of the named entry, 0 if not found.
 */
gulong
gst_props_get_fourcc_int (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  g_return_val_if_fail (props != NULL, 0);
  g_return_val_if_fail (name != NULL, 0);

  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.fourcc_data;
  }
  
  return 0;
}

/**
 * gst_props_get_boolean:
 * @props: the props to get the fourcc value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as a boolean value.
 *
 * Returns: the boolean value of the named entry, 0 if not found.
 */
gboolean
gst_props_get_boolean (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  g_return_val_if_fail (props != NULL, FALSE);
  g_return_val_if_fail (name != NULL, FALSE);

  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.bool_data;
  }
  
  return 0;
}

/**
 * gst_props_get_string:
 * @props: the props to get the fourcc value from
 * @name: the name of the props entry to get.
 *
 * Get the named entry as a string value.
 *
 * Returns: the string value of the named entry, NULL if not found.
 */
const gchar*
gst_props_get_string (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;
  
  g_return_val_if_fail (props != NULL, NULL);
  g_return_val_if_fail (name != NULL, NULL);

  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *thisentry;

    thisentry = (GstPropsEntry *)lentry->data;

    return thisentry->data.string_data.string;
  }
  
  return NULL;
}

/**
 * gst_props_merge:
 * @props: the property to merge into
 * @tomerge: the property to merge 
 *
 * Merge the properties of tomerge into props.
 *
 * Returns: the new merged property 
 */
GstProps*
gst_props_merge (GstProps *props, GstProps *tomerge)
{
  GList *merge_props;

  g_return_val_if_fail (props != NULL, NULL);
  g_return_val_if_fail (tomerge != NULL, NULL);

  merge_props = tomerge->properties;

  /* FIXME do proper merging here... */
  while (merge_props) {
    GstPropsEntry *entry = (GstPropsEntry *)merge_props->data;

    props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
	  
    merge_props = g_list_next (merge_props);
  }

  return props;
}


/* entry2 is always a list, entry1 never is */
static gboolean
gst_props_entry_check_list_compatibility (GstPropsEntry *entry1, GstPropsEntry *entry2)
{
  GList *entrylist = entry2->data.list_data.entries;
  gboolean found = FALSE;

  while (entrylist && !found) {
    GstPropsEntry *entry = (GstPropsEntry *) entrylist->data;

    found |= gst_props_entry_check_compatibility (entry1, entry);

    entrylist = g_list_next (entrylist);
  }

  return found;
}

static gboolean
gst_props_entry_check_compatibility (GstPropsEntry *entry1, GstPropsEntry *entry2)
{
  GST_DEBUG (GST_CAT_PROPERTIES,"compare: %s %s\n", g_quark_to_string (entry1->propid),
	                     g_quark_to_string (entry2->propid));
  gst_props_debug_entry (entry1);
  gst_props_debug_entry (entry2);
  switch (entry1->propstype) {
    case GST_PROPS_LIST_ID:
    {
      GList *entrylist = entry1->data.list_data.entries;
      gboolean valid = TRUE;    /* innocent until proven guilty */

      while (entrylist && valid) {
	GstPropsEntry *entry = (GstPropsEntry *) entrylist->data;

	valid &= gst_props_entry_check_compatibility (entry, entry2);
	
	entrylist = g_list_next (entrylist);
      }
      
      return valid;
    }
    case GST_PROPS_INT_RANGE_ID:
      switch (entry2->propstype) {
	/* a - b   <--->   a - c */
        case GST_PROPS_INT_RANGE_ID:
	  return (entry2->data.int_range_data.min <= entry1->data.int_range_data.min &&
	          entry2->data.int_range_data.max >= entry1->data.int_range_data.max);
        case GST_PROPS_LIST_ID:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_FLOAT_RANGE_ID:
      switch (entry2->propstype) {
	/* a - b   <--->   a - c */
        case GST_PROPS_FLOAT_RANGE_ID:
	  return (entry2->data.float_range_data.min <= entry1->data.float_range_data.min &&
	          entry2->data.float_range_data.max >= entry1->data.float_range_data.max);
        case GST_PROPS_LIST_ID:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_FOURCC_ID:
      switch (entry2->propstype) {
	/* b   <--->   a */
        case GST_PROPS_FOURCC_ID:
	  return (entry2->data.fourcc_data == entry1->data.fourcc_data);
	/* b   <--->   a,b,c */
        case GST_PROPS_LIST_ID:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_INT_ID:
      switch (entry2->propstype) {
	/* b   <--->   a - d */
        case GST_PROPS_INT_RANGE_ID:
          GST_DEBUG(GST_CAT_PROPERTIES,"%d <= %d <= %d ?\n",entry2->data.int_range_data.min,
                    entry1->data.int_data,entry2->data.int_range_data.max);
	  return (entry2->data.int_range_data.min <= entry1->data.int_data &&
	          entry2->data.int_range_data.max >= entry1->data.int_data);
	/* b   <--->   a */
        case GST_PROPS_INT_ID:
          GST_DEBUG(GST_CAT_PROPERTIES,"%d == %d ?\n",entry1->data.int_data,entry2->data.int_data);
	  return (entry2->data.int_data == entry1->data.int_data);
	/* b   <--->   a,b,c */
        case GST_PROPS_LIST_ID:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_FLOAT_ID:
      switch (entry2->propstype) {
	/* b   <--->   a - d */
        case GST_PROPS_FLOAT_RANGE_ID:
	  return (entry2->data.float_range_data.min <= entry1->data.float_data &&
	          entry2->data.float_range_data.max >= entry1->data.float_data);
	/* b   <--->   a */
        case GST_PROPS_FLOAT_ID:
	  return (entry2->data.float_data == entry1->data.float_data);
	/* b   <--->   a,b,c */
        case GST_PROPS_LIST_ID:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_PROPS_BOOL_ID:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_BOOL_ID:
          return (entry2->data.bool_data == entry1->data.bool_data);
        case GST_PROPS_LIST_ID:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
    case GST_PROPS_STRING_ID:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_STRING_ID:
          return (!strcmp (entry2->data.string_data.string, entry1->data.string_data.string));
        case GST_PROPS_LIST_ID:
	  return gst_props_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
    default:
      break;
  }

  return FALSE;
}

/**
 * gst_props_check_compatibility:
 * @fromprops: a property
 * @toprops: a property
 *
 * Checks whether two capabilities are compatible.
 *
 * Returns: TRUE if compatible, FALSE otherwise
 */
gboolean
gst_props_check_compatibility (GstProps *fromprops, GstProps *toprops)
{
  GList *sourcelist;
  GList *sinklist;
  gint missing = 0;
  gint more = 0;
  gboolean compatible = TRUE;

  g_return_val_if_fail (fromprops != NULL, FALSE);
  g_return_val_if_fail (toprops != NULL, FALSE);
	
  sourcelist = fromprops->properties;
  sinklist   = toprops->properties;

  while (sourcelist && sinklist && compatible) {
    GstPropsEntry *entry1;
    GstPropsEntry *entry2;

    entry1 = (GstPropsEntry *)sourcelist->data;
    entry2 = (GstPropsEntry *)sinklist->data;

    while (entry1->propid < entry2->propid) {
      GST_DEBUG (GST_CAT_PROPERTIES,"source is more specific in \"%s\"\n", g_quark_to_string (entry1->propid));
      more++;
      sourcelist = g_list_next (sourcelist);
      if (sourcelist) entry1 = (GstPropsEntry *)sourcelist->data;
      else goto end;
    }
    while (entry1->propid > entry2->propid) {
      GST_DEBUG (GST_CAT_PROPERTIES,"source has missing property \"%s\"\n", g_quark_to_string (entry2->propid));
      missing++;
      sinklist = g_list_next (sinklist);
      if (sinklist) entry2 = (GstPropsEntry *)sinklist->data;
      else goto end;
    }

    if (!gst_props_entry_check_compatibility (entry1, entry2)) {
	compatible = FALSE;
	GST_DEBUG (GST_CAT_PROPERTIES, "%s are not compatible: \n",
		   g_quark_to_string (entry1->propid));
	gst_props_debug_entry (entry1);
	gst_props_debug_entry (entry2);
    }

    sourcelist = g_list_next (sourcelist);
    sinklist = g_list_next (sinklist);
  }
  if (sinklist && compatible) {
    GstPropsEntry *entry2;
    entry2 = (GstPropsEntry *)sinklist->data;
    missing++;
    GST_DEBUG (GST_CAT_PROPERTIES,"source has missing property \"%s\"\n", g_quark_to_string (entry2->propid));
  }
end:

  if (missing)
    return FALSE;

  return compatible;
}

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
static xmlNodePtr
gst_props_save_thyself_func (GstPropsEntry *entry, xmlNodePtr parent)
{
  xmlNodePtr subtree;
  gchar *str;

  switch (entry->propstype) {
    case GST_PROPS_INT_ID: 
      subtree = xmlNewChild (parent, NULL, "int", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%d", entry->data.int_data);
      xmlNewProp (subtree, "value", str);
      g_free(str);
      break;
    case GST_PROPS_INT_RANGE_ID: 
      subtree = xmlNewChild (parent, NULL, "range", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%d", entry->data.int_range_data.min);
      xmlNewProp (subtree, "min", str);
      g_free(str);
      str = g_strdup_printf ("%d", entry->data.int_range_data.max);
      xmlNewProp (subtree, "max", str);
      g_free(str);
      break;
    case GST_PROPS_FLOAT_ID: 
      subtree = xmlNewChild (parent, NULL, "float", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%f", entry->data.float_data);
      xmlNewProp (subtree, "value", str);
      g_free(str);
      break;
    case GST_PROPS_FLOAT_RANGE_ID: 
      subtree = xmlNewChild (parent, NULL, "floatrange", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%f", entry->data.float_range_data.min);
      xmlNewProp (subtree, "min", str);
      g_free(str);
      str = g_strdup_printf ("%f", entry->data.float_range_data.max);
      xmlNewProp (subtree, "max", str);
      g_free(str);
      break;
    case GST_PROPS_FOURCC_ID: 
      str = g_strdup_printf ("%4.4s", (gchar *)&entry->data.fourcc_data);
      xmlAddChild (parent, xmlNewComment (str));
      g_free(str);
      subtree = xmlNewChild (parent, NULL, "fourcc", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%08x", entry->data.fourcc_data);
      xmlNewProp (subtree, "hexvalue", str);
      g_free(str);
      break;
    case GST_PROPS_BOOL_ID: 
      subtree = xmlNewChild (parent, NULL, "boolean", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      xmlNewProp (subtree, "value", (entry->data.bool_data ?  "true" : "false"));
      break;
    case GST_PROPS_STRING_ID: 
      subtree = xmlNewChild (parent, NULL, "string", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      xmlNewProp (subtree, "value", entry->data.string_data.string);
      break;
    default:
      break;
  }

  return parent;
}

/**
 * gst_props_save_thyself:
 * @props: a property to save
 * @parent: the parent XML tree
 *
 * Saves the property into an XML representation.
 *
 * Returns: the new XML tree
 */
xmlNodePtr
gst_props_save_thyself (GstProps *props, xmlNodePtr parent)
{
  GList *proplist;
  xmlNodePtr subtree;

  g_return_val_if_fail (props != NULL, NULL);

  proplist = props->properties;

  while (proplist) {
    GstPropsEntry *entry = (GstPropsEntry *) proplist->data;

    switch (entry->propstype) {
      case GST_PROPS_LIST_ID: 
        subtree = xmlNewChild (parent, NULL, "list", NULL);
        xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
        g_list_foreach (entry->data.list_data.entries, (GFunc) gst_props_save_thyself_func, subtree);
      default:
    	gst_props_save_thyself_func (entry, parent);
    }

    proplist = g_list_next (proplist);
  }
  
  return parent;
}

static GstPropsEntry*
gst_props_load_thyself_func (xmlNodePtr field)
{
  GstPropsEntry *entry;
  gchar *prop;

  g_mutex_lock (_gst_props_entries_chunk_lock);
  entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
  g_mutex_unlock (_gst_props_entries_chunk_lock);

  if (!strcmp(field->name, "int")) {
    entry->propstype = GST_PROPS_INT_ID;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp(field, "value");
    sscanf (prop, "%d", &entry->data.int_data);
    g_free (prop);
  }
  else if (!strcmp(field->name, "range")) {
    entry->propstype = GST_PROPS_INT_RANGE_ID;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "min");
    sscanf (prop, "%d", &entry->data.int_range_data.min);
    g_free (prop);
    prop = xmlGetProp (field, "max");
    sscanf (prop, "%d", &entry->data.int_range_data.max);
    g_free (prop);
  }
  else if (!strcmp(field->name, "float")) {
    entry->propstype = GST_PROPS_FLOAT_ID;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp(field, "value");
    sscanf (prop, "%f", &entry->data.float_data);
    g_free (prop);
  }
  else if (!strcmp(field->name, "floatrange")) {
    entry->propstype = GST_PROPS_FLOAT_RANGE_ID;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "min");
    sscanf (prop, "%f", &entry->data.float_range_data.min);
    g_free (prop);
    prop = xmlGetProp (field, "max");
    sscanf (prop, "%f", &entry->data.float_range_data.max);
    g_free (prop);
  }
  else if (!strcmp(field->name, "boolean")) {
    entry->propstype = GST_PROPS_BOOL_ID;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "value");
    if (!strcmp (prop, "false")) entry->data.bool_data = 0;
    else entry->data.bool_data = 1;
    g_free (prop);
  }
  else if (!strcmp(field->name, "fourcc")) {
    entry->propstype = GST_PROPS_FOURCC_ID;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "hexvalue");
    sscanf (prop, "%08x", &entry->data.fourcc_data);
    g_free (prop);
  }
  else if (!strcmp(field->name, "string")) {
    entry->propstype = GST_PROPS_STRING_ID;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    entry->data.string_data.string = xmlGetProp (field, "value");
  }
  else {
    g_mutex_lock (_gst_props_entries_chunk_lock);
    g_mem_chunk_free (_gst_props_entries_chunk, entry);
    g_mutex_unlock (_gst_props_entries_chunk_lock);
    entry = NULL;
  }

  return entry;
}

/**
 * gst_props_load_thyself:
 * @parent: the XML tree to load from
 *
 * Creates a new property out of an XML tree.
 *
 * Returns: the new property
 */
GstProps*
gst_props_load_thyself (xmlNodePtr parent)
{
  GstProps *props;
  xmlNodePtr field = parent->xmlChildrenNode;
  gchar *prop;

  g_mutex_lock (_gst_props_chunk_lock);
  props = g_mem_chunk_alloc (_gst_props_chunk);
  g_mutex_unlock (_gst_props_chunk_lock);

  props->properties = NULL;
  props->refcount = 1;

  while (field) {
    if (!strcmp (field->name, "list")) {
      GstPropsEntry *entry;
      xmlNodePtr subfield = field->xmlChildrenNode;

      g_mutex_lock (_gst_props_entries_chunk_lock);
      entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
      g_mutex_unlock (_gst_props_entries_chunk_lock);

      entry->propstype = GST_PROPS_LIST_ID;
      entry->data.list_data.entries = NULL;
      prop = xmlGetProp (field, "name");
      entry->propid = g_quark_from_string (prop);
      g_free (prop);

      while (subfield) {
        GstPropsEntry *subentry = gst_props_load_thyself_func (subfield);

	if (subentry)
	  entry->data.list_data.entries = g_list_prepend (entry->data.list_data.entries, subentry);

        subfield = subfield->next;
      }
      entry->data.list_data.entries = g_list_reverse (entry->data.list_data.entries);
      props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
    }
    else {
      GstPropsEntry *entry;

      entry = gst_props_load_thyself_func (field);

      if (entry) 
	props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
    }
    field = field->next;
  }

  return props;
}
#endif /* GST_DISABLE_LOADSAVE_REGISTRY */

