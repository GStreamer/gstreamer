/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
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

GType _gst_props_type;

#define GST_PROPS_ENTRY_IS_VARIABLE(a)	(((GstPropsEntry*)(a))->propstype > GST_PROPS_VAR_TYPE)

struct _GstPropsEntry {
  GQuark    	propid;
  GstPropsType 	propstype;		

  union {
    /* flat values */
    gboolean bool_data;
    guint32  fourcc_data;
    gint     int_data;
    gfloat   float_data;

    /* structured values */
    struct {
      GList *entries;
    } list_data;
    struct {
      gchar *string;
    } string_data;
    struct {
      gint min;
      gint max;
    } int_range_data;
    struct {
      gfloat min;
      gfloat max;
    } float_range_data;
  } data;
};

static GMemChunk *_gst_props_entries_chunk;
static GMutex *_gst_props_entries_chunk_lock;

static GMemChunk *_gst_props_chunk;
static GMutex *_gst_props_chunk_lock;

static gboolean 	gst_props_entry_check_compatibility 	(GstPropsEntry *entry1, GstPropsEntry *entry2);
static GList* 		gst_props_list_copy 			(GList *propslist);

	
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

  _gst_props_type = g_boxed_type_register_static ("GstProps",
		                       (GBoxedCopyFunc) gst_props_ref,
		                       (GBoxedFreeFunc) gst_props_unref);

}

static void
gst_props_debug_entry (GstPropsEntry *entry)
{
  const gchar *name = g_quark_to_string (entry->propid);

  switch (entry->propstype) {
    case GST_PROPS_INT_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s: int %d\n", name, entry->data.int_data);
      break;
    case GST_PROPS_FLOAT_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s: float %f\n", name, entry->data.float_data);
      break;
    case GST_PROPS_FOURCC_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s: fourcc %4.4s\n", name, (gchar*)&entry->data.fourcc_data);
      break;
    case GST_PROPS_BOOL_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s: bool %d\n", name, entry->data.bool_data);
      break;
    case GST_PROPS_STRING_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s: string %s\n", name, entry->data.string_data.string);
      break;
    case GST_PROPS_INT_RANGE_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s: int range %d-%d\n", name, entry->data.int_range_data.min,
		      entry->data.int_range_data.max);
      break;
    case GST_PROPS_FLOAT_RANGE_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%s: float range %f-%f\n", name, entry->data.float_range_data.min,
		      entry->data.float_range_data.max);
      break;
    case GST_PROPS_LIST_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "[list]\n");
      {
	GList *entries = entry->data.list_data.entries;

	while (entries) {
          gst_props_debug_entry ((GstPropsEntry *)entries->data);
	  entries = g_list_next (entries);
	}
      }
      break;
    default:
      g_warning ("unknown property type %d", entry->propstype);
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
  GQuark quark = (GQuark) GPOINTER_TO_INT (b);

  return (quark - entry2->propid);
}

/* This is implemented as a huge macro because we cannot pass
 * va_list variables by reference on some architectures.
 */
#define GST_PROPS_ENTRY_FILL(entry, var_args) 					\
G_STMT_START { 									\
  entry->propstype = va_arg (var_args, GstPropsType); 				\
										\
  switch (entry->propstype) {							\
    case GST_PROPS_INT_TYPE:							\
      entry->data.int_data = va_arg (var_args, gint);				\
      break;									\
    case GST_PROPS_INT_RANGE_TYPE:						\
      entry->data.int_range_data.min = va_arg (var_args, gint);			\
      entry->data.int_range_data.max = va_arg (var_args, gint);			\
      break;									\
    case GST_PROPS_FLOAT_TYPE:							\
      entry->data.float_data = va_arg (var_args, gdouble);			\
      break;									\
    case GST_PROPS_FLOAT_RANGE_TYPE:						\
      entry->data.float_range_data.min = va_arg (var_args, gdouble);		\
      entry->data.float_range_data.max = va_arg (var_args, gdouble);		\
      break;									\
    case GST_PROPS_FOURCC_TYPE:							\
      entry->data.fourcc_data = va_arg (var_args, gulong);			\
      break;									\
    case GST_PROPS_BOOL_TYPE:							\
      entry->data.bool_data = va_arg (var_args, gboolean);			\
      break;									\
    case GST_PROPS_STRING_TYPE:							\
      entry->data.string_data.string = g_strdup (va_arg (var_args, gchar*));	\
      break;									\
    default:									\
      break;									\
  }										\
} G_STMT_END


#define GST_PROPS_ENTRY_READ(entry, var_args, safe, result)			\
G_STMT_START { 									\
  *result = TRUE;								\
										\
  if (safe) {									\
    GstPropsType propstype = va_arg (var_args, GstPropsType); 			\
    if (propstype != entry->propstype) {					\
      *result = FALSE;								\
    }										\
  }										\
  if (*result) {								\
    switch (entry->propstype) {							\
      case GST_PROPS_INT_TYPE:							\
        *(va_arg (var_args, gint*)) = entry->data.int_data;			\
        break;									\
      case GST_PROPS_INT_RANGE_TYPE:						\
        *(va_arg (var_args, gint*)) = entry->data.int_range_data.min;		\
        *(va_arg (var_args, gint*)) = entry->data.int_range_data.max;		\
        break;									\
      case GST_PROPS_FLOAT_TYPE:						\
        *(va_arg (var_args, gfloat*)) = entry->data.float_data;			\
        break;									\
      case GST_PROPS_FLOAT_RANGE_TYPE:						\
        *(va_arg (var_args, gfloat*)) = entry->data.float_range_data.min;	\
        *(va_arg (var_args, gfloat*)) = entry->data.float_range_data.max;	\
        break;									\
      case GST_PROPS_FOURCC_TYPE:						\
        *(va_arg (var_args, guint32*)) = entry->data.fourcc_data;		\
        break;									\
      case GST_PROPS_BOOL_TYPE:							\
        *(va_arg (var_args, gboolean*)) = entry->data.bool_data;		\
        break;									\
      case GST_PROPS_STRING_TYPE:						\
        *(va_arg (var_args, gchar**)) = entry->data.string_data.string;		\
        break;									\
      case GST_PROPS_LIST_TYPE:							\
        *(va_arg (var_args, GList**)) = entry->data.list_data.entries;		\
        break;									\
      default:									\
        *result = FALSE;							\
        break;									\
    }										\
  }										\
} G_STMT_END

static GstPropsEntry*
gst_props_alloc_entry (void)
{
  GstPropsEntry *entry;

  g_mutex_lock (_gst_props_entries_chunk_lock);
  entry = g_mem_chunk_alloc (_gst_props_entries_chunk);
  g_mutex_unlock (_gst_props_entries_chunk_lock);

  return entry;
}

static void
gst_props_entry_destroy (GstPropsEntry *entry)
{
  switch (entry->propstype) {
    case GST_PROPS_STRING_TYPE:		
      g_free (entry->data.string_data.string);
      break;				
    case GST_PROPS_LIST_TYPE:		
    {
      GList *entries = entry->data.list_data.entries;

      while (entries) {
	gst_props_entry_destroy ((GstPropsEntry *)entries->data);
	entries = g_list_next (entries);
      }
      g_list_free (entry->data.list_data.entries);
      break;
    }
    default:			
      break;		
  }
  g_mutex_lock (_gst_props_entries_chunk_lock);
  g_mem_chunk_free (_gst_props_entries_chunk, entry);
  g_mutex_unlock (_gst_props_entries_chunk_lock);
}

static GstProps*
gst_props_alloc (void)
{
  GstProps *props;

  g_mutex_lock (_gst_props_chunk_lock);
  props = g_mem_chunk_alloc (_gst_props_chunk);
  g_mutex_unlock (_gst_props_chunk_lock);

  props->properties = NULL;
  props->refcount = 1;
  props->fixed = TRUE;

  return props;
}

static void
gst_props_add_entry (GstProps *props, GstPropsEntry *entry)
{
  g_return_if_fail (props);
  g_return_if_fail (entry);

  if (props->fixed && GST_PROPS_ENTRY_IS_VARIABLE (entry)) {
    props->fixed = FALSE;
  }
  props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
}

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


void
gst_props_debug (GstProps *props)
{
  GList *propslist = props->properties;

  while (propslist) { 
    GstPropsEntry *entry = (GstPropsEntry *)propslist->data;

    gst_props_debug_entry (entry);
    
    propslist = g_list_next (propslist);
  }
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

  if (newentry->propstype == GST_PROPS_INT_TYPE) {
    new_min = newentry->data.int_data;
    new_max = newentry->data.int_data;
  } else {
    new_min = newentry->data.int_range_data.min;
    new_max = newentry->data.int_range_data.max;
  }

  if (oldentry->propstype == GST_PROPS_INT_TYPE) {
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
      newentry->propstype = GST_PROPS_INT_TYPE;
      newentry->data.int_data = new_min;
    } else {
      newentry->propstype = GST_PROPS_INT_RANGE_TYPE;
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

  return g_list_prepend (entries, newentry);
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

  props = gst_props_alloc ();

  prop_name = firstname;

  /* properties */
  while (prop_name) {
    GstPropsEntry *entry;
   
    entry = gst_props_alloc_entry ();
    entry->propid = g_quark_from_string (prop_name);
    GST_PROPS_ENTRY_FILL (entry, var_args);

    switch (entry->propstype) {
      case GST_PROPS_INT_TYPE:
      case GST_PROPS_INT_RANGE_TYPE:
	entry_type = GST_PROPS_LIST_T_INTS;
	break;
      case GST_PROPS_FLOAT_TYPE:
      case GST_PROPS_FLOAT_RANGE_TYPE:
	entry_type = GST_PROPS_LIST_T_FLOATS;
	break;
      case GST_PROPS_FOURCC_TYPE:
      case GST_PROPS_BOOL_TYPE:
      case GST_PROPS_STRING_TYPE:
	entry_type = GST_PROPS_LIST_T_MISC;
	break;
      case GST_PROPS_LIST_TYPE:
	g_return_val_if_fail (inlist == FALSE, NULL);
	inlist = TRUE;
	list_entry = entry;
	list_type = GST_PROPS_LIST_T_UNSET;
	list_entry->data.list_data.entries = NULL;
	break;
      case GST_PROPS_END_TYPE:
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
	else {
	  list_entry->data.list_data.entries =
		    g_list_reverse (list_entry->data.list_data.entries);
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
		    g_list_prepend (list_entry->data.list_data.entries, entry);
	    break;
	}
      }
    }
    else {
      gst_props_add_entry (props, entry);
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
 * @...: The prop entry.
 *
 * Modifies the value of the given entry in the props struct.
 * For the optional args, use GST_PROPS_FOO, where FOO is INT,
 * STRING, etc. This macro expands to a variable number of arguments,
 * hence the lack of precision in the function prototype. No
 * terminating NULL is necessary.
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
  if (props == NULL)
    return;
  
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

  if (props == NULL)
    return;
  
  entries = props->properties;

  while (entries) {
    gst_props_entry_destroy ((GstPropsEntry *)entries->data);
    entries = g_list_next (entries);
  }
  g_list_free (props->properties);

  g_mutex_lock (_gst_props_chunk_lock);
  g_mem_chunk_free (_gst_props_chunk, props);
  g_mutex_unlock (_gst_props_chunk_lock);
}

/* 
 * copy entries 
 */
static GstPropsEntry*
gst_props_entry_copy (GstPropsEntry *entry)
{
  GstPropsEntry *newentry;

  newentry = gst_props_alloc_entry ();
  memcpy (newentry, entry, sizeof (GstPropsEntry));
  if (entry->propstype == GST_PROPS_LIST_TYPE) {
    newentry->data.list_data.entries = gst_props_list_copy (entry->data.list_data.entries);
  }
  else if (entry->propstype == GST_PROPS_STRING_TYPE) {
    newentry->data.string_data.string = g_strdup (entry->data.string_data.string);
  }

  return newentry;
}

static GList*
gst_props_list_copy (GList *propslist)
{
  GList *new = NULL;

  while (propslist) {
    GstPropsEntry *entry = (GstPropsEntry *)propslist->data;

    new = g_list_prepend (new, gst_props_entry_copy (entry));
    
    propslist = g_list_next (propslist);
  }
  new = g_list_reverse (new);

  return new;
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

  if (props == NULL)
    return NULL;

  new = gst_props_alloc ();
  new->properties = gst_props_list_copy (props->properties);
  new->fixed = props->fixed;

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

  return new;
}

const GstPropsEntry*
gst_props_get_entry (GstProps *props, const gchar *name)
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
    return thisentry;
  }
  return NULL;
}

gboolean
gst_props_has_property (GstProps *props, const gchar *name)
{
  return (gst_props_get_entry (props, name) != NULL);
}

gboolean
gst_props_has_property_typed (GstProps *props, const gchar *name, GstPropsType type)
{
  const GstPropsEntry *entry;

  entry = gst_props_get_entry (props, name);
  if (!entry) 
    return FALSE;

  return (entry->propstype == type);
}

gboolean
gst_props_has_fixed_property (GstProps *props, const gchar *name)
{
  const GstPropsEntry *entry;

  entry = gst_props_get_entry (props, name);
  if (!entry) 
    return FALSE;

  return !GST_PROPS_ENTRY_IS_VARIABLE (entry);
}

GstPropsType
gst_props_entry_get_type (const GstPropsEntry *entry)
{
  g_return_val_if_fail (entry != NULL, GST_PROPS_INVALID_TYPE);

  return entry->propstype;
}

const gchar*
gst_props_entry_get_name (const GstPropsEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return g_quark_to_string (entry->propid);
}

gboolean
gst_props_entry_is_fixed (const GstPropsEntry *entry)
{
  g_return_val_if_fail (entry != NULL, FALSE);

  return !GST_PROPS_ENTRY_IS_VARIABLE (entry);
}

static gboolean
gst_props_entry_getv (const GstPropsEntry *entry, gboolean safe, va_list var_args)
{
  gboolean result;

  GST_PROPS_ENTRY_READ (entry, var_args, safe, &result);

  return result;
}

gboolean
gst_props_entry_get (const GstPropsEntry *entry, ...)
{
  gboolean result;
  va_list var_args;

  g_return_val_if_fail (entry != NULL, FALSE);

  va_start (var_args, entry);
  result = gst_props_entry_getv (entry, FALSE, var_args);
  va_end (var_args);

  return result;
}

static gboolean
gst_props_entry_get_safe (const GstPropsEntry *entry, ...)
{
  gboolean result;
  va_list var_args;

  g_return_val_if_fail (entry != NULL, FALSE);

  va_start (var_args, entry);
  result = gst_props_entry_getv (entry, TRUE, var_args);
  va_end (var_args);

  return result;
}

gboolean
gst_props_get (GstProps *props, gchar *first_name, ...)
{
  va_list var_args;

  va_start (var_args, first_name);

  while (first_name) {
    const GstPropsEntry *entry = gst_props_get_entry (props, first_name);
    gboolean result;

    if (!entry) return FALSE;
    GST_PROPS_ENTRY_READ (entry, var_args, FALSE, &result);
    if (!result) return FALSE;

    first_name = va_arg (var_args, gchar *);
  }
  va_end (var_args);
  
  return TRUE;
}

gboolean
gst_props_entry_get_int (const GstPropsEntry *entry, gint *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_INT_TYPE, val);
}

gboolean
gst_props_entry_get_float (const GstPropsEntry *entry, gfloat *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_FLOAT_TYPE, val);
}

gboolean
gst_props_entry_get_fourcc_int (const GstPropsEntry *entry, guint32 *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_FOURCC_TYPE, val);
}

gboolean
gst_props_entry_get_boolean (const GstPropsEntry *entry, gboolean *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_BOOL_TYPE, val);
}

gboolean
gst_props_entry_get_string (const GstPropsEntry *entry, const gchar **val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_STRING_TYPE, val);
}

gboolean
gst_props_entry_get_int_range (const GstPropsEntry *entry, gint *min, gint *max)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_INT_RANGE_TYPE, min, max);
}

gboolean
gst_props_entry_get_float_range (const GstPropsEntry *entry, gfloat *min, gfloat *max)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_FLOAT_RANGE_TYPE, min, max);
}

gboolean
gst_props_entry_get_list (const GstPropsEntry *entry, const GList **val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_LIST_TYPE, val);
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

    gst_props_add_entry (props, entry);
	  
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
  GST_DEBUG (GST_CAT_PROPERTIES,"compare: %s %s", g_quark_to_string (entry1->propid),
	                     g_quark_to_string (entry2->propid));

  if (entry2->propstype == GST_PROPS_LIST_TYPE && entry1->propstype != GST_PROPS_LIST_TYPE) {
    return gst_props_entry_check_list_compatibility (entry1, entry2);
  }

  switch (entry1->propstype) {
    case GST_PROPS_LIST_TYPE:
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
    case GST_PROPS_INT_RANGE_TYPE:
      switch (entry2->propstype) {
	/* a - b   <--->   a - c */
        case GST_PROPS_INT_RANGE_TYPE:
	  return (entry2->data.int_range_data.min <= entry1->data.int_range_data.min &&
	          entry2->data.int_range_data.max >= entry1->data.int_range_data.max);
        default:
	  break;
      }
      break;
    case GST_PROPS_FLOAT_RANGE_TYPE:
      switch (entry2->propstype) {
	/* a - b   <--->   a - c */
        case GST_PROPS_FLOAT_RANGE_TYPE:
	  return (entry2->data.float_range_data.min <= entry1->data.float_range_data.min &&
	          entry2->data.float_range_data.max >= entry1->data.float_range_data.max);
        default:
	  break;
      }
      break;
    case GST_PROPS_FOURCC_TYPE:
      switch (entry2->propstype) {
	/* b   <--->   a */
        case GST_PROPS_FOURCC_TYPE:
          GST_DEBUG(GST_CAT_PROPERTIES,"\"%4.4s\" <--> \"%4.4s\" ?\n",
			  (char*) &entry2->data.fourcc_data, (char*) &entry1->data.fourcc_data);
	  return (entry2->data.fourcc_data == entry1->data.fourcc_data);
        default:
	  break;
      }
      break;
    case GST_PROPS_INT_TYPE:
      switch (entry2->propstype) {
	/* b   <--->   a - d */
        case GST_PROPS_INT_RANGE_TYPE:
          GST_DEBUG(GST_CAT_PROPERTIES,"%d <= %d <= %d ?\n",entry2->data.int_range_data.min,
                    entry1->data.int_data,entry2->data.int_range_data.max);
	  return (entry2->data.int_range_data.min <= entry1->data.int_data &&
	          entry2->data.int_range_data.max >= entry1->data.int_data);
	/* b   <--->   a */
        case GST_PROPS_INT_TYPE:
          GST_DEBUG(GST_CAT_PROPERTIES,"%d == %d ?\n",entry1->data.int_data,entry2->data.int_data);
	  return (entry2->data.int_data == entry1->data.int_data);
        default:
	  break;
      }
      break;
    case GST_PROPS_FLOAT_TYPE:
      switch (entry2->propstype) {
	/* b   <--->   a - d */
        case GST_PROPS_FLOAT_RANGE_TYPE:
	  return (entry2->data.float_range_data.min <= entry1->data.float_data &&
	          entry2->data.float_range_data.max >= entry1->data.float_data);
	/* b   <--->   a */
        case GST_PROPS_FLOAT_TYPE:
	  return (entry2->data.float_data == entry1->data.float_data);
        default:
	  break;
      }
      break;
    case GST_PROPS_BOOL_TYPE:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_BOOL_TYPE:
          return (entry2->data.bool_data == entry1->data.bool_data);
        default:
	  break;
      }
    case GST_PROPS_STRING_TYPE:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_STRING_TYPE:
          GST_DEBUG(GST_CAT_PROPERTIES,"\"%s\" <--> \"%s\" ?\n",
			  entry2->data.string_data.string, entry1->data.string_data.string);
          return (!strcmp (entry2->data.string_data.string, entry1->data.string_data.string));
        default:
	  break;
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
      more++;
      sourcelist = g_list_next (sourcelist);
      if (sourcelist) entry1 = (GstPropsEntry *)sourcelist->data;
      else goto end;
    }
    while (entry1->propid > entry2->propid) {
      missing++;
      sinklist = g_list_next (sinklist);
      if (sinklist) entry2 = (GstPropsEntry *)sinklist->data;
      else goto end;
    }

    if (!gst_props_entry_check_compatibility (entry1, entry2)) {
	compatible = FALSE; 
	GST_DEBUG (GST_CAT_PROPERTIES, "%s are not compatible: ",
		   g_quark_to_string (entry1->propid));
    }

    sourcelist = g_list_next (sourcelist);
    sinklist = g_list_next (sinklist);
  }
  if (sinklist && compatible) {
    GstPropsEntry *entry2;
    entry2 = (GstPropsEntry *)sinklist->data;
    missing++;
  }
end:

  if (missing)
    return FALSE;

  return compatible;
}

static GstPropsEntry*
gst_props_entry_intersect (GstPropsEntry *entry1, GstPropsEntry *entry2)
{
  GstPropsEntry *result = NULL;

  /* try to move the ranges and lists first */
  switch (entry2->propstype) {
    case GST_PROPS_INT_RANGE_TYPE:
    case GST_PROPS_FLOAT_RANGE_TYPE:
    case GST_PROPS_LIST_TYPE:
    {
      GstPropsEntry *temp;

      temp = entry1;
      entry1 = entry2;
      entry2 = temp;
    }
    default:
      break;
  }

  switch (entry1->propstype) {
    case GST_PROPS_LIST_TYPE:
    {
      GList *entrylist = entry1->data.list_data.entries;
      GList *intersection = NULL;

      while (entrylist) {
	GstPropsEntry *entry = (GstPropsEntry *) entrylist->data;
	GstPropsEntry *intersectentry;

	intersectentry = gst_props_entry_intersect (entry2, entry);

	if (intersectentry) {
	  if (intersectentry->propstype == GST_PROPS_LIST_TYPE) {
	    intersection = g_list_concat (intersection, intersectentry->data.list_data.entries);
	    /* set the list to NULL because the entries are concatenated to the above
	     * list and we don't want to free them */
	    intersectentry->data.list_data.entries = NULL;
	    gst_props_entry_destroy (intersectentry);
	  }
	  else {
	    intersection = g_list_prepend (intersection, intersectentry);
	  }
	}
	entrylist = g_list_next (entrylist);
      }
      if (intersection) {
	/* check if the list only contains 1 element, if so, we can just copy it */
	if (g_list_next (intersection) == NULL) {
	  result = (GstPropsEntry *) (intersection->data); 
	  g_list_free (intersection);
	}
	/* else we need to create a new entry to hold the list */
	else {
	  result = gst_props_alloc_entry ();
	  result->propid = entry1->propid;
	  result->propstype = GST_PROPS_LIST_TYPE;
	  result->data.list_data.entries = g_list_reverse (intersection);
	}
      }
      return result;
    }
    case GST_PROPS_INT_RANGE_TYPE:
      switch (entry2->propstype) {
	/* a - b   <--->   a - c */
        case GST_PROPS_INT_RANGE_TYPE:
        {
	  gint lower = MAX (entry1->data.int_range_data.min, entry2->data.int_range_data.min);
	  gint upper = MIN (entry1->data.int_range_data.max, entry2->data.int_range_data.max);

	  if (lower <= upper) {
            result = gst_props_alloc_entry ();
	    result->propid = entry1->propid;

	    if (lower == upper) {
	      result->propstype = GST_PROPS_INT_TYPE;
	      result->data.int_data = lower;
	    }
	    else {
	      result->propstype = GST_PROPS_INT_RANGE_TYPE;
	      result->data.int_range_data.min = lower;
	      result->data.int_range_data.max = upper;
	    }
	  }
	  break;
	}
        case GST_PROPS_LIST_TYPE:
        {
          GList *entries = entry2->data.list_data.entries;
          result = gst_props_alloc_entry ();
          result->propid = entry1->propid;
          result->propstype = GST_PROPS_LIST_TYPE;
          result->data.list_data.entries = NULL;
          while (entries) {
            GstPropsEntry * this = (GstPropsEntry *)entries->data;
            if (this->propstype != GST_PROPS_INT_TYPE) {
              /* no hope, this list doesn't even contain ints! */
              gst_props_entry_destroy (result);
              result = NULL;
              break;
            }
            if (this->data.int_data >= entry1->data.int_range_data.min &&
                this->data.int_data <= entry1->data.int_range_data.max) {
              result->data.list_data.entries = g_list_append (result->data.list_data.entries,
                                                              gst_props_entry_copy (this));
            }
            entries = g_list_next (entries);
          }
          break;
        }
        case GST_PROPS_INT_TYPE:
        {
	  if (entry1->data.int_range_data.min <= entry2->data.int_data && 
	      entry1->data.int_range_data.max >= entry2->data.int_data) {
            result = gst_props_entry_copy (entry2);
	  }
          break;
        }
        default:
	  break;
      }
      break;
    case GST_PROPS_FLOAT_RANGE_TYPE:
      switch (entry2->propstype) {
	/* a - b   <--->   a - c */
        case GST_PROPS_FLOAT_RANGE_TYPE:
        {
	  gfloat lower = MAX (entry1->data.float_range_data.min, entry2->data.float_range_data.min);
	  gfloat upper = MIN (entry1->data.float_range_data.max, entry2->data.float_range_data.max);

	  if (lower <= upper) {
            result = gst_props_alloc_entry ();
	    result->propid = entry1->propid;

	    if (lower == upper) {
	      result->propstype = GST_PROPS_FLOAT_TYPE;
	      result->data.float_data = lower;
	    }
	    else {
	      result->propstype = GST_PROPS_FLOAT_RANGE_TYPE;
	      result->data.float_range_data.min = lower;
	      result->data.float_range_data.max = upper;
	    }
	  }
	  break;
	}
        case GST_PROPS_FLOAT_TYPE:
	  if (entry1->data.float_range_data.min <= entry2->data.float_data && 
	      entry1->data.float_range_data.max >= entry2->data.float_data) {
            result = gst_props_entry_copy (entry2);
	  }
        default:
	  break;
      }
      break;
    case GST_PROPS_FOURCC_TYPE:
      switch (entry2->propstype) {
	/* b   <--->   a */
        case GST_PROPS_FOURCC_TYPE:
          if (entry1->data.fourcc_data == entry2->data.fourcc_data)
	    result = gst_props_entry_copy (entry1);
        default:
	  break;
      }
      break;
    case GST_PROPS_INT_TYPE:
      switch (entry2->propstype) {
	/* b   <--->   a */
        case GST_PROPS_INT_TYPE:
          if (entry1->data.int_data == entry2->data.int_data)
	    result = gst_props_entry_copy (entry1);
        default:
	  break;
      }
      break;
    case GST_PROPS_FLOAT_TYPE:
      switch (entry2->propstype) {
	/* b   <--->   a */
        case GST_PROPS_FLOAT_TYPE:
          if (entry1->data.float_data == entry2->data.float_data)
	    result = gst_props_entry_copy (entry1);
        default:
	  break;
      }
      break;
    case GST_PROPS_BOOL_TYPE:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_BOOL_TYPE:
          if (entry1->data.bool_data == entry2->data.bool_data)
	    result = gst_props_entry_copy (entry1);
        default:
	  break;
      }
    case GST_PROPS_STRING_TYPE:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_STRING_TYPE:
          if (!strcmp (entry1->data.string_data.string, entry2->data.string_data.string))
	    result = gst_props_entry_copy (entry1);
        default:
	  break;
      }
    default:
      break;
  }

  return result;
}

/**
 * gst_props_intersect:
 * @props1: a property
 * @props2: another property
 *
 * Calculates the intersection bewteen two GstProps.
 *
 * Returns: a GstProps with the intersection or NULL if the 
 * intersection is empty.
 */
GstProps*
gst_props_intersect (GstProps *props1, GstProps *props2)
{
  GList *props1list;
  GList *props2list;
  GstProps *intersection;
  GList *leftovers;
  GstPropsEntry *iprops = NULL;

  intersection = gst_props_alloc ();
  intersection->fixed = TRUE;

  g_return_val_if_fail (props1 != NULL, NULL);
  g_return_val_if_fail (props2 != NULL, NULL);
	
  props1list = props1->properties;
  props2list = props2->properties;

  while (props1list && props2list) {
    GstPropsEntry *entry1;
    GstPropsEntry *entry2;

    entry1 = (GstPropsEntry *)props1list->data;
    entry2 = (GstPropsEntry *)props2list->data;

    while (entry1->propid < entry2->propid) {
      GstPropsEntry *toadd;

      GST_DEBUG (GST_CAT_PROPERTIES,"source is more specific in \"%s\"", g_quark_to_string (entry1->propid));

      toadd = gst_props_entry_copy (entry1);
      if (GST_PROPS_ENTRY_IS_VARIABLE (toadd))
	intersection->fixed = FALSE;

      intersection->properties = g_list_prepend (intersection->properties, toadd);

      props1list = g_list_next (props1list);
      if (props1list) 
	entry1 = (GstPropsEntry *)props1list->data;
      else 
	goto end;
    }
    while (entry1->propid > entry2->propid) {
      GstPropsEntry *toadd;

      toadd = gst_props_entry_copy (entry2);
      if (GST_PROPS_ENTRY_IS_VARIABLE (toadd))
	intersection->fixed = FALSE;

      intersection->properties = g_list_prepend (intersection->properties, toadd);

      props2list = g_list_next (props2list);
      if (props2list)
	entry2 = (GstPropsEntry *)props2list->data;
      else 
	goto end;
    }
    /* at this point we are talking about the same property */
    iprops = gst_props_entry_intersect (entry1, entry2);

    if (iprops) {
      if (GST_PROPS_ENTRY_IS_VARIABLE (iprops))
	intersection->fixed = FALSE;
      intersection->properties = g_list_prepend (intersection->properties, iprops);
    }
    else {
      gst_props_unref (intersection);
      return NULL;
    }

    props1list = g_list_next (props1list);
    props2list = g_list_next (props2list);
  }

end:
  /* at this point one of the lists could contain leftover properties */
  if (props1list)
    leftovers = props1list;
  else if (props2list)
    leftovers = props2list;
  else 
    leftovers = NULL;

  while (leftovers) {
    GstPropsEntry *entry;

    entry = (GstPropsEntry *) leftovers->data;
    if (GST_PROPS_ENTRY_IS_VARIABLE (entry))
      intersection->fixed = FALSE;
    intersection->properties = g_list_prepend (intersection->properties, gst_props_entry_copy (entry));

    leftovers = g_list_next (leftovers);
  }

  intersection->properties = g_list_reverse (intersection->properties);

  return intersection;
}

GList*
gst_props_normalize (GstProps *props)
{
  GList *entries;
  GList *result = NULL;

  if (!props) 
    return NULL;

  entries = props->properties;

  while (entries) {
    GstPropsEntry *entry = (GstPropsEntry *) entries->data;

    if (entry->propstype == GST_PROPS_LIST_TYPE) {
      GList *list_entries = entry->data.list_data.entries;

      while (list_entries) {
        GstPropsEntry *list_entry = (GstPropsEntry *) list_entries->data;
        GstPropsEntry *new_entry;
	GstProps *newprops;
	GList *lentry;

	newprops = gst_props_alloc ();
	newprops->properties = gst_props_list_copy (props->properties);
        lentry = g_list_find_custom (newprops->properties, GINT_TO_POINTER (list_entry->propid), props_find_func);
	if (lentry) {
          GList *new_list = NULL;

          new_entry = (GstPropsEntry *) lentry->data;
	  memcpy (new_entry, list_entry, sizeof (GstPropsEntry));

	  new_list = gst_props_normalize (newprops);
          result = g_list_concat (new_list, result);
	}
	else {
          result = g_list_append (result, newprops);
	}
	
        list_entries = g_list_next (list_entries);	
      }
      /* we break out of the loop because the other lists are
       * unrolled in the recursive call */
      break;
    }
    entries = g_list_next (entries);
  }
  if (!result) {
    result = g_list_prepend (result, props);
  }
  else {
    result = g_list_reverse (result);
    gst_props_unref (props);
  }
  return result;
}

#ifndef GST_DISABLE_LOADSAVE_REGISTRY
static xmlNodePtr
gst_props_save_thyself_func (GstPropsEntry *entry, xmlNodePtr parent)
{
  xmlNodePtr subtree;
  gchar *str;

  switch (entry->propstype) {
    case GST_PROPS_INT_TYPE: 
      subtree = xmlNewChild (parent, NULL, "int", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%d", entry->data.int_data);
      xmlNewProp (subtree, "value", str);
      g_free(str);
      break;
    case GST_PROPS_INT_RANGE_TYPE: 
      subtree = xmlNewChild (parent, NULL, "range", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%d", entry->data.int_range_data.min);
      xmlNewProp (subtree, "min", str);
      g_free(str);
      str = g_strdup_printf ("%d", entry->data.int_range_data.max);
      xmlNewProp (subtree, "max", str);
      g_free(str);
      break;
    case GST_PROPS_FLOAT_TYPE: 
      subtree = xmlNewChild (parent, NULL, "float", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%f", entry->data.float_data);
      xmlNewProp (subtree, "value", str);
      g_free(str);
      break;
    case GST_PROPS_FLOAT_RANGE_TYPE: 
      subtree = xmlNewChild (parent, NULL, "floatrange", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%f", entry->data.float_range_data.min);
      xmlNewProp (subtree, "min", str);
      g_free(str);
      str = g_strdup_printf ("%f", entry->data.float_range_data.max);
      xmlNewProp (subtree, "max", str);
      g_free(str);
      break;
    case GST_PROPS_FOURCC_TYPE: 
      str = g_strdup_printf ("%4.4s", (gchar *)&entry->data.fourcc_data);
      xmlAddChild (parent, xmlNewComment (str));
      g_free(str);
      subtree = xmlNewChild (parent, NULL, "fourcc", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%08x", entry->data.fourcc_data);
      xmlNewProp (subtree, "hexvalue", str);
      g_free(str);
      break;
    case GST_PROPS_BOOL_TYPE: 
      subtree = xmlNewChild (parent, NULL, "boolean", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      xmlNewProp (subtree, "value", (entry->data.bool_data ?  "true" : "false"));
      break;
    case GST_PROPS_STRING_TYPE: 
      subtree = xmlNewChild (parent, NULL, "string", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      xmlNewProp (subtree, "value", entry->data.string_data.string);
      break;
    default:
      g_warning ("trying to save unknown property type %d", entry->propstype);
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
      case GST_PROPS_LIST_TYPE: 
        subtree = xmlNewChild (parent, NULL, "list", NULL);
        xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
        g_list_foreach (entry->data.list_data.entries, (GFunc) gst_props_save_thyself_func, subtree);
	break;
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

  entry = gst_props_alloc_entry ();

  if (!strcmp(field->name, "int")) {
    entry->propstype = GST_PROPS_INT_TYPE;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp(field, "value");
    sscanf (prop, "%d", &entry->data.int_data);
    g_free (prop);
  }
  else if (!strcmp(field->name, "range")) {
    entry->propstype = GST_PROPS_INT_RANGE_TYPE;
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
    entry->propstype = GST_PROPS_FLOAT_TYPE;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp(field, "value");
    sscanf (prop, "%f", &entry->data.float_data);
    g_free (prop);
  }
  else if (!strcmp(field->name, "floatrange")) {
    entry->propstype = GST_PROPS_FLOAT_RANGE_TYPE;
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
    entry->propstype = GST_PROPS_BOOL_TYPE;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "value");
    if (!strcmp (prop, "false")) entry->data.bool_data = 0;
    else entry->data.bool_data = 1;
    g_free (prop);
  }
  else if (!strcmp(field->name, "fourcc")) {
    entry->propstype = GST_PROPS_FOURCC_TYPE;
    prop = xmlGetProp(field, "name");
    entry->propid = g_quark_from_string (prop);
    g_free (prop);
    prop = xmlGetProp (field, "hexvalue");
    sscanf (prop, "%08x", &entry->data.fourcc_data);
    g_free (prop);
  }
  else if (!strcmp(field->name, "string")) {
    entry->propstype = GST_PROPS_STRING_TYPE;
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

  props = gst_props_alloc ();

  while (field) {
    if (!strcmp (field->name, "list")) {
      GstPropsEntry *entry;
      xmlNodePtr subfield = field->xmlChildrenNode;

      entry = gst_props_alloc_entry ();
      prop = xmlGetProp (field, "name");
      entry->propid = g_quark_from_string (prop);
      g_free (prop);
      entry->propstype = GST_PROPS_LIST_TYPE;
      entry->data.list_data.entries = NULL;

      while (subfield) {
        GstPropsEntry *subentry = gst_props_load_thyself_func (subfield);

	if (subentry)
	  entry->data.list_data.entries = g_list_prepend (entry->data.list_data.entries, subentry);

        subfield = subfield->next;
      }
      entry->data.list_data.entries = g_list_reverse (entry->data.list_data.entries);
      gst_props_add_entry (props, entry);
    }
    else {
      GstPropsEntry *entry;

      entry = gst_props_load_thyself_func (field);

      if (entry) 
        gst_props_add_entry (props, entry);
    }
    field = field->next;
  }

  return props;
}
#endif /* GST_DISABLE_LOADSAVE_REGISTRY */

