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
#include "gstmemchunk.h"

#ifndef GST_DISABLE_TRACE
/* #define GST_WITH_ALLOC_TRACE */
#include "gsttrace.h"
static GstAllocTrace *_props_trace;
static GstAllocTrace *_entries_trace;
#endif

GType _gst_props_type;
GType _gst_props_entry_type;

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

static GstMemChunk *_gst_props_entries_chunk;
static GstMemChunk *_gst_props_chunk;

static gboolean 	gst_props_entry_check_compatibility 	(GstPropsEntry *entry1, GstPropsEntry *entry2);
static GList* 		gst_props_list_copy 			(GList *propslist);

static void
transform_func (const GValue *src_value,
		GValue *dest_value)
{
  GstProps *props = g_value_peek_pointer (src_value);
  GString *result = g_string_new ("");

  if (props) {
    GList *propslist = props->properties;

    while (propslist) {
      GstPropsEntry *entry = (GstPropsEntry *)propslist->data;
      const gchar *name = g_quark_to_string (entry->propid);

      switch (entry->propstype) {
        case GST_PROPS_INT_TYPE:
  	  g_string_append_printf (result, "%s=(int) %d", name, entry->data.int_data);
	  break;
        case GST_PROPS_FLOAT_TYPE:
  	  g_string_append_printf (result, "%s=(float) %f", name, entry->data.float_data);
	  break;
        case GST_PROPS_FOURCC_TYPE:
  	  g_string_append_printf (result, "%s=(fourcc) '%4.4s'", name, (gchar *)&entry->data.fourcc_data);
	  break;
        case GST_PROPS_BOOLEAN_TYPE:
  	  g_string_append_printf (result, "%s=(boolean) %s", name, 
			  	  (entry->data.bool_data ? "TRUE" : "FALSE"));
	  break;
        case GST_PROPS_STRING_TYPE:
	  g_string_append_printf (result, "%s=(string) '%s'", name, entry->data.string_data.string);
	  break;
        default:
	  break;
      }

      propslist = g_list_next (propslist);
      if (propslist) {
        g_string_append (result, "; ");
      }
    }
  }
  dest_value->data[0].v_pointer = result->str;
  g_string_free (result, FALSE);
}

void
_gst_props_initialize (void)
{
  _gst_props_entries_chunk = gst_mem_chunk_new ("GstPropsEntries",
		  sizeof (GstPropsEntry), sizeof (GstPropsEntry) * 1024,
		  G_ALLOC_AND_FREE);

  _gst_props_chunk = gst_mem_chunk_new ("GstProps",
		  sizeof (GstProps), sizeof (GstProps) * 256,
		  G_ALLOC_AND_FREE);

  _gst_props_type = g_boxed_type_register_static ("GstProps",
		                       (GBoxedCopyFunc) gst_props_ref,
		                       (GBoxedFreeFunc) gst_props_unref);

  g_value_register_transform_func (_gst_props_type,
                                   G_TYPE_STRING,
                                   transform_func);

  _gst_props_entry_type = g_boxed_type_register_static ("GstPropsEntry",
		  (GBoxedCopyFunc) gst_props_entry_copy,
		  (GBoxedFreeFunc) gst_props_entry_destroy);

#ifndef GST_DISABLE_TRACE
  _props_trace = gst_alloc_trace_register (GST_PROPS_TRACE_NAME);
  _entries_trace = gst_alloc_trace_register (GST_PROPS_ENTRY_TRACE_NAME);
#endif
}

static void
gst_props_debug_entry (GstPropsEntry *entry)
{
  const gchar *name = g_quark_to_string (entry->propid);

  switch (entry->propstype) {
    case GST_PROPS_INT_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: %s: int %d", entry, name, entry->data.int_data);
      break;
    case GST_PROPS_FLOAT_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: %s: float %f", entry, name, entry->data.float_data);
      break;
    case GST_PROPS_FOURCC_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: %s: fourcc %c%c%c%c", entry, name,
	(entry->data.fourcc_data>>0)&0xff,
	(entry->data.fourcc_data>>8)&0xff,
	(entry->data.fourcc_data>>16)&0xff,
	(entry->data.fourcc_data>>24)&0xff);
      break;
    case GST_PROPS_BOOLEAN_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: %s: bool %d", entry, name, entry->data.bool_data);
      break;
    case GST_PROPS_STRING_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: %s: string \"%s\"", entry, name, entry->data.string_data.string);
      break;
    case GST_PROPS_INT_RANGE_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: %s: int range %d-%d", entry, name, entry->data.int_range_data.min,
		      entry->data.int_range_data.max);
      break;
    case GST_PROPS_FLOAT_RANGE_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: %s: float range %f-%f", entry, name, entry->data.float_range_data.min,
		      entry->data.float_range_data.max);
      break;
    case GST_PROPS_LIST_TYPE:
      GST_DEBUG (GST_CAT_PROPERTIES, "%p: [list]", entry);
      {
	GList *entries = entry->data.list_data.entries;

	while (entries) {
          gst_props_debug_entry ((GstPropsEntry *)entries->data);
	  entries = g_list_next (entries);
	}
      }
      break;
    default:
      g_warning ("unknown property type %d at %p", entry->propstype, entry);
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
#define GST_PROPS_ENTRY_FILL(entry, var_args)	 				\
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
    case GST_PROPS_BOOLEAN_TYPE:						\
      entry->data.bool_data = va_arg (var_args, gboolean);			\
      break;									\
    case GST_PROPS_STRING_TYPE:							\
      entry->data.string_data.string = g_strdup (va_arg (var_args, gchar*));	\
      break;									\
    case GST_PROPS_GLIST_TYPE:							\
      entry->propstype = GST_PROPS_LIST_TYPE;					\
      entry->data.list_data.entries = g_list_copy (va_arg (var_args, GList*));	\
      break;									\
    default:									\
      break;									\
  }										\
} G_STMT_END


#define GST_PROPS_ENTRY_READ(entry, var_args, safe, result)			\
G_STMT_START { 									\
										\
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
      case GST_PROPS_BOOLEAN_TYPE:						\
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

  entry = gst_mem_chunk_alloc (_gst_props_entries_chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_entries_trace, entry);
#endif

  GST_DEBUG (GST_CAT_PROPERTIES, "new entry %p", entry);

  return entry;
}

static void
gst_props_entry_clean (GstPropsEntry *entry)
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
}

/**
 * gst_props_entry_destroy:
 * @entry: the entry to destroy
 *
 * Free the given propsentry
 */
void
gst_props_entry_destroy (GstPropsEntry *entry)
{
  GST_DEBUG (GST_CAT_PROPERTIES, "destroy entry %p", entry);

  gst_props_entry_clean (entry);

  gst_mem_chunk_free (_gst_props_entries_chunk, entry);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_entries_trace, entry);
#endif
}

GType
gst_props_get_type (void)
{
  return _gst_props_type;
}

/**
 * gst_props_empty_new:
 *
 * Create a new empty property.
 *
 * Returns: the new property
 */
GstProps*
gst_props_empty_new (void)
{
  GstProps *props;

  props = gst_mem_chunk_alloc (_gst_props_chunk);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_new (_props_trace, props);
#endif

  GST_DEBUG (GST_CAT_PROPERTIES, "new %p", props);

  props->properties = NULL;
  props->refcount = 1;
  GST_PROPS_FLAG_SET (props, GST_PROPS_FLOATING);
  GST_PROPS_FLAG_SET (props, GST_PROPS_FIXED);

  return props;
}

/**
 * gst_props_replace:
 * @oldprops: the props to take replace
 * @newprops: the props to take replace 
 *
 * Replace the pointer to the props, doing proper
 * refcounting.
 */
void
gst_props_replace (GstProps **oldprops, GstProps *newprops)
{
  if (*oldprops != newprops) {
    if (newprops)  gst_props_ref   (newprops);
    if (*oldprops) gst_props_unref (*oldprops);

    *oldprops = newprops;
  }
}

/**
 * gst_props_replace_sink:
 * @oldprops: the props to take replace
 * @newprops: the props to take replace 
 *
 * Replace the pointer to the props and take ownership.
 */
void
gst_props_replace_sink (GstProps **oldprops, GstProps *newprops)
{
  gst_props_replace (oldprops, newprops);
  gst_props_sink (newprops);
}

/**
 * gst_props_add_entry:
 * @props: the property to add the entry to
 * @entry: the entry to add
 *
 * Addes the given propsentry to the props
 */
void
gst_props_add_entry (GstProps *props, GstPropsEntry *entry)
{
  g_return_if_fail (props);
  g_return_if_fail (entry);

  if (GST_PROPS_IS_FIXED (props) && GST_PROPS_ENTRY_IS_VARIABLE (entry)) {
    GST_PROPS_FLAG_UNSET (props, GST_PROPS_FIXED);
  }
  props->properties = g_list_insert_sorted (props->properties, entry, props_compare_func);
}

/**
 * gst_props_remove_entry:
 * @props: the property to remove the entry from
 * @entry: the entry to remove
 *
 * Removes the given propsentry from the props.
 */
void
gst_props_remove_entry (GstProps *props, GstPropsEntry *entry)
{
  g_return_if_fail (props != NULL);
  g_return_if_fail (entry != NULL);

  props->properties = g_list_remove (props->properties, entry);
}

/**
 * gst_props_remove_entry_by_name:
 * @props: the property to remove the entry from
 * @name: the name of the entry to remove
 *
 * Removes the propsentry with the given name from the props.
 */
void
gst_props_remove_entry_by_name (GstProps *props, const gchar *name)
{
  GList *lentry;
  GQuark quark;

  g_return_if_fail (props != NULL);
  g_return_if_fail (name != NULL);

  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);
  if (lentry) {
    gst_props_remove_entry (props, (GstPropsEntry *)lentry->data);
  }
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

/**
 * gst_props_debug:
 * @props: the props to debug
 *
 * Dump the contents of the given properties into the DEBUG log.
 */
void
gst_props_debug (GstProps *props)
{
  GList *propslist = props->properties;

  GST_DEBUG (GST_CAT_PROPERTIES, "props %p, refcount %d, flags %d", props, props->refcount, props->flags);

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
gst_props_merge_int_entries(GstPropsEntry *newentry, GstPropsEntry *oldentry)
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
static GList*
gst_props_add_to_int_list (GList *entries, GstPropsEntry *newentry)
{
  GList *i;

  i = entries;
  while (i) {
    GstPropsEntry *oldentry = (GstPropsEntry *)(i->data);
    gboolean merged = gst_props_merge_int_entries(newentry, oldentry);

    if (merged) {
      /* replace the existing one with the merged one */
      gst_props_entry_destroy (oldentry);

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

GType
gst_props_entry_get_type (void)
{
  return _gst_props_entry_type;
}

static GstPropsEntry*
gst_props_entry_newv (const gchar *name, va_list var_args)
{
  GstPropsEntry *entry;

  entry = gst_props_alloc_entry ();
  entry->propid = g_quark_from_string (name);
  GST_PROPS_ENTRY_FILL (entry, var_args);

  return entry;
}

/**
 * gst_props_entry_new:
 * @name: the name of the props entry
 * @...: the value of the entry
 *
 * Create a new property entry with the given key/value.
 *
 * Returns: the new entry.
 */
GstPropsEntry*
gst_props_entry_new (const gchar *name, ...)
{
  va_list var_args;
  GstPropsEntry *entry;

  va_start (var_args, name);
  entry = gst_props_entry_newv (name, var_args);
  va_end (var_args);

  return entry;
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

  props = gst_props_empty_new ();

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
      case GST_PROPS_BOOLEAN_TYPE:
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
	  GstPropsEntry *subentry = (GstPropsEntry *)(list_entry->data.list_data.entries->data);
	  list_entry->propstype = subentry->propstype;
	  list_entry->data = subentry->data;
          gst_props_entry_destroy (subentry);
	}
	else {
	  list_entry->data.list_data.entries =
		    g_list_reverse (list_entry->data.list_data.entries);
	}

        gst_props_entry_destroy (entry);
	inlist = FALSE;
	list_entry = NULL;
        prop_name = va_arg (var_args, gchar*);
	continue;
      default:
	g_warning ("unknown property type found %d for '%s'\n", entry->propstype, prop_name);
        gst_props_entry_destroy (entry);
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
 * terminating NULL is necessary as only one property can be changed.
 *
 * Returns: the new modified property structure.
 */
GstProps*
gst_props_set (GstProps *props, const gchar *name, ...)
{
  GQuark quark;
  GList *lentry;
  va_list var_args;

  g_return_val_if_fail (props != NULL, NULL);

  quark = g_quark_from_string (name);

  lentry = g_list_find_custom (props->properties, GINT_TO_POINTER (quark), props_find_func);

  if (lentry) {
    GstPropsEntry *entry;

    entry = (GstPropsEntry *)lentry->data;

    va_start (var_args, name);
    gst_props_entry_clean (entry);
    GST_PROPS_ENTRY_FILL (entry, var_args);
    va_end (var_args);
  }
  else {
    g_warning ("gstprops: no property '%s' to change\n", name);
  }

  return props;
}


/**
 * gst_props_unref:
 * @props: the props to unref
 *
 * Decrease the refcount of the property structure, destroying
 * the property if the refcount is 0.
 *
 * Returns: handle to unrefed props or NULL when it was
 * destroyed.
 */
GstProps*
gst_props_unref (GstProps *props)
{
  if (props == NULL)
    return NULL;

  g_return_val_if_fail (props->refcount > 0, NULL);

  GST_DEBUG (GST_CAT_PROPERTIES, "unref %p (%d->%d)", props, props->refcount, props->refcount-1);
  props->refcount--;

  if (props->refcount == 0) {
    gst_props_destroy (props);
    return NULL;
  }

  return props;
}

/**
 * gst_props_ref:
 * @props: the props to ref
 *
 * Increase the refcount of the property structure.
 *
 * Returns: handle to refed props.
 */
GstProps*
gst_props_ref (GstProps *props)
{
  if (props == NULL)
    return NULL;

  g_return_val_if_fail (props->refcount > 0, NULL);

  GST_DEBUG (GST_CAT_PROPERTIES, "ref %p (%d->%d)", props, props->refcount, props->refcount+1);

  props->refcount++;

  return props;
}

/**
 * gst_props_sink:
 * @props: the props to sink
 *
 * If the props if floating, decrease its refcount. Usually used 
 * with gst_props_ref() to take ownership of the props.
 */
void
gst_props_sink (GstProps *props)
{
  if (props == NULL)
    return;

  GST_DEBUG (GST_CAT_PROPERTIES, "sink %p", props);

  if (GST_PROPS_IS_FLOATING (props)) {
    GST_PROPS_FLAG_UNSET (props, GST_PROPS_FLOATING);
    gst_props_unref (props);
  }
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

  gst_mem_chunk_free (_gst_props_chunk, props);
#ifndef GST_DISABLE_TRACE
  gst_alloc_trace_free (_props_trace, props);
#endif
}

/**
 * gst_props_entry_copy:
 * @entry: the entry to copy
 *
 * Copy the propsentry.
 *
 * Returns: a new #GstPropsEntry that is a copy of the original
 * given entry.
 */
GstPropsEntry*
gst_props_entry_copy (const GstPropsEntry *entry)
{
  GstPropsEntry *newentry;

  newentry = gst_props_alloc_entry ();
  memcpy (newentry, entry, sizeof (GstPropsEntry));

  switch (entry->propstype) {
    case GST_PROPS_LIST_TYPE:
      newentry->data.list_data.entries = gst_props_list_copy (entry->data.list_data.entries);
      break;
    case GST_PROPS_STRING_TYPE:
      newentry->data.string_data.string = g_strdup (entry->data.string_data.string);
      break;
    default:
      /* FIXME more? */
      break;
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

  new = gst_props_empty_new ();
  new->properties = gst_props_list_copy (props->properties);
  GST_PROPS_FLAGS (new) = GST_PROPS_FLAGS (props) | GST_PROPS_FLOATING;

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

/**
 * gst_props_get_entry:
 * @props: the props to query
 * @name: the name of the entry to get
 *
 * Get the props entry with the geven name
 *
 * Returns: The props entry with the geven name or NULL when
 * the entry was not found.
 */
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

/**
 * gst_props_has_property:
 * @props: the props to check
 * @name: the name of the key to find
 *
 * Checks if a given props has a property with the given name.
 *
 * Returns: TRUE if the property was found, FALSE otherwise.
 */
gboolean
gst_props_has_property (GstProps *props, const gchar *name)
{
  return (gst_props_get_entry (props, name) != NULL);
}

/**
 * gst_props_has_property_typed:
 * @props: the props to check
 * @name: the name of the key to find
 * @type: the type of the required property
 *
 * Checks if a given props has a property with the given name and the given type.
 *
 * Returns: TRUE if the property was found, FALSE otherwise.
 */
gboolean
gst_props_has_property_typed (GstProps *props, const gchar *name, GstPropsType type)
{
  const GstPropsEntry *entry;

  entry = gst_props_get_entry (props, name);
  if (!entry) 
    return FALSE;

  return (entry->propstype == type);
}

/**
 * gst_props_has_fixed_property:
 * @props: the props to check
 * @name: the name of the key to find
 *
 * Checks if a given props has a property with the given name that
 * is also fixed, ie. is not a list or a range.
 *
 * Returns: TRUE if the property was found, FALSE otherwise.
 */
gboolean
gst_props_has_fixed_property (GstProps *props, const gchar *name)
{
  const GstPropsEntry *entry;

  entry = gst_props_get_entry (props, name);
  if (!entry)
    return FALSE;

  return !GST_PROPS_ENTRY_IS_VARIABLE (entry);
}

/**
 * gst_props_entry_get_props_type:
 * @entry: the props entry to query
 *
 * Get the type of the given props entry.
 *
 * Returns: The type of the props entry.
 */
GstPropsType
gst_props_entry_get_props_type (const GstPropsEntry *entry)
{
  g_return_val_if_fail (entry != NULL, GST_PROPS_INVALID_TYPE);

  return entry->propstype;
}

/**
 * gst_props_entry_get_name:
 * @entry: the props entry to query
 *
 * Get the name of the given props entry. 
 *
 * Returns: The name of the props entry.
 */
const gchar*
gst_props_entry_get_name (const GstPropsEntry *entry)
{
  g_return_val_if_fail (entry != NULL, NULL);

  return g_quark_to_string (entry->propid);
}

/**
 * gst_props_entry_is_fixed:
 * @entry: the props entry to query
 *
 * Checks if the props entry is fixe, ie. is not a list
 * or a range.
 *
 * Returns: TRUE is the props entry is fixed.
 */
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

/**
 * gst_props_entry_get:
 * @entry: the props entry to query
 * @...: a pointer to a type that can hold the value.
 *
 * Gets the contents of the entry.
 *
 * Returns: TRUE is the props entry could be fetched.
 */
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

static gboolean
gst_props_getv (GstProps *props, gboolean safe, gchar *first_name, va_list var_args)
{
  while (first_name) {
    const GstPropsEntry *entry = gst_props_get_entry (props, first_name);
    gboolean result;

    if (!entry) return FALSE;
    GST_PROPS_ENTRY_READ (entry, var_args, safe, &result);
    if (!result) return FALSE;

    first_name = va_arg (var_args, gchar *);
  }
  return TRUE;
}

/**
 * gst_props_get:
 * @props: the props to query
 * @first_name: the first key
 * @...: a pointer to a datastructure that can hold the value.
 *
 * Gets the contents of the props into given key/value pairs.
 * Make sure you pass a NULL terminated list.
 *
 * Returns: TRUE if all of the props entries could be fetched.
 */
gboolean
gst_props_get (GstProps *props, gchar *first_name, ...)
{
  va_list var_args;
  gboolean ret;

  va_start (var_args, first_name);
  ret = gst_props_getv (props, FALSE, first_name, var_args);
  va_end (var_args);

  return ret;
}

/**
 * gst_props_get_safe:
 * @props: the props to query
 * @first_name: the first key
 * @...: a pointer to a datastructure that can hold the value.
 *
 * Gets the contents of the props into given key/value pairs.
 *
 * Returns: TRUE if all of the props entries could be fetched.
 */
gboolean
gst_props_get_safe (GstProps *props, gchar *first_name, ...)
{
  va_list var_args;
  gboolean ret;

  va_start (var_args, first_name);
  ret = gst_props_getv (props, TRUE, first_name, var_args);
  va_end (var_args);

  return ret;
}

/**
 * gst_props_entry_get_int:
 * @entry: the props entry to query
 * @val: a pointer to a gint to hold the value.
 *
 * Get the contents of the entry into the given gint.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the
 * entry is not of given type or did not exist.
 */
gboolean
gst_props_entry_get_int (const GstPropsEntry *entry, gint *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_INT_TYPE, val);
}

/**
 * gst_props_entry_get_float:
 * @entry: the props entry to query
 * @val: a pointer to a gfloat to hold the value.
 *
 * Get the contents of the entry into the given gfloat.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the
 * entry is not of given type or did not exist.
 */
gboolean
gst_props_entry_get_float (const GstPropsEntry *entry, gfloat *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_FLOAT_TYPE, val);
}

/**
 * gst_props_entry_get_fourcc_int:
 * @entry: the props entry to query
 * @val: a pointer to a guint32 to hold the value.
 *
 * Get the contents of the entry into the given guint32.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the
 * entry is not of given type or did not exist.
 */
gboolean
gst_props_entry_get_fourcc_int (const GstPropsEntry *entry, guint32 *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_FOURCC_TYPE, val);
}

/**
 * gst_props_entry_get_boolean:
 * @entry: the props entry to query
 * @val: a pointer to a gboolean to hold the value.
 *
 * Get the contents of the entry into the given gboolean.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the
 * entry is not of given type or did not exist.
 */
gboolean
gst_props_entry_get_boolean (const GstPropsEntry *entry, gboolean *val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_BOOLEAN_TYPE, val);
}

/**
 * gst_props_entry_get_string:
 * @entry: the props entry to query
 * @val: a pointer to a gchar* to hold the value.
 *
 * Get the contents of the entry into the given gchar*.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the
 * entry is not of given type or did not exist.
 */
gboolean
gst_props_entry_get_string (const GstPropsEntry *entry, const gchar **val)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_STRING_TYPE, val);
}

/**
 * gst_props_entry_get_int_range:
 * @entry: the props entry to query
 * @min: a pointer to a gint to hold the minimun value.
 * @max: a pointer to a gint to hold the maximum value.
 *
 * Get the contents of the entry into the given gints.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the
 * entry is not of given type or did not exist.
 */
gboolean
gst_props_entry_get_int_range (const GstPropsEntry *entry, gint *min, gint *max)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_INT_RANGE_TYPE, min, max);
}

/**
 * gst_props_entry_get_float_range:
 * @entry: the props entry to query
 * @min: a pointer to a gfloat to hold the minimun value.
 * @max: a pointer to a gfloat to hold the maximum value.
 *
 * Get the contents of the entry into the given gfloats.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the
 * entry is not of given type or did not exist.
 */
gboolean
gst_props_entry_get_float_range (const GstPropsEntry *entry, gfloat *min, gfloat *max)
{
  return gst_props_entry_get_safe (entry, GST_PROPS_FLOAT_RANGE_TYPE, min, max);
}

/**
 * gst_props_entry_get_list:
 * @entry: the props entry to query
 * @val: a pointer to a GList to hold the value.
 *
 * Get the contents of the entry into the given GList.
 *
 * Returns: TRUE is the value could be fetched. FALSE if the 
 * entry is not of given type or did not exist.
 */
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
  GST_DEBUG (GST_CAT_PROPERTIES,"compare: %s %s", g_quark_to_string (entry1->propid), g_quark_to_string (entry2->propid));

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
          GST_DEBUG(GST_CAT_PROPERTIES,"\"%c%c%c%c\" <--> \"%c%c%c%c\" ?",
	    (entry2->data.fourcc_data>>0)&0xff,
	    (entry2->data.fourcc_data>>8)&0xff,
	    (entry2->data.fourcc_data>>16)&0xff,
	    (entry2->data.fourcc_data>>24)&0xff,
	    (entry1->data.fourcc_data>>0)&0xff,
	    (entry1->data.fourcc_data>>8)&0xff,
	    (entry1->data.fourcc_data>>16)&0xff,
	    (entry1->data.fourcc_data>>24)&0xff);
	  return (entry2->data.fourcc_data == entry1->data.fourcc_data);
        default:
	  break;
      }
      break;
    case GST_PROPS_INT_TYPE:
      switch (entry2->propstype) {
	/* b   <--->   a - d */
        case GST_PROPS_INT_RANGE_TYPE:
          GST_DEBUG(GST_CAT_PROPERTIES,"%d <= %d <= %d ?",entry2->data.int_range_data.min,
                    entry1->data.int_data,entry2->data.int_range_data.max);
	  return (entry2->data.int_range_data.min <= entry1->data.int_data &&
	          entry2->data.int_range_data.max >= entry1->data.int_data);
	/* b   <--->   a */
        case GST_PROPS_INT_TYPE:
          GST_DEBUG(GST_CAT_PROPERTIES,"%d == %d ?",entry1->data.int_data,entry2->data.int_data);
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
    case GST_PROPS_BOOLEAN_TYPE:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_BOOLEAN_TYPE:
          return (entry2->data.bool_data == entry1->data.bool_data);
        default:
	  break;
      }
    case GST_PROPS_STRING_TYPE:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_STRING_TYPE:
          GST_DEBUG(GST_CAT_PROPERTIES,"\"%s\" <--> \"%s\" ?",
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
	    intersection = g_list_concat (intersection, 
			       intersectentry->data.list_data.entries);
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
            GstPropsEntry *this = (GstPropsEntry *)entries->data;
            if (this->propstype != GST_PROPS_INT_TYPE) {
              /* no hope, this list doesn't even contain ints! */
              gst_props_entry_destroy (result);
              result = NULL;
              break;
            }
            if (this->data.int_data >= entry1->data.int_range_data.min &&
                this->data.int_data <= entry1->data.int_range_data.max)
	    {
	      /* prepend and reverse at the end */
              result->data.list_data.entries = g_list_prepend (result->data.list_data.entries,
                                                               gst_props_entry_copy (this));
            }
            entries = g_list_next (entries);
          }
	  if (result) {
	    result->data.list_data.entries = g_list_reverse (result->data.list_data.entries);
	  }
          break;
        }
        case GST_PROPS_INT_TYPE:
        {
	  if (entry1->data.int_range_data.min <= entry2->data.int_data && 
	      entry1->data.int_range_data.max >= entry2->data.int_data) 
	  {
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
	      entry1->data.float_range_data.max >= entry2->data.float_data) 
	  {
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
    case GST_PROPS_BOOLEAN_TYPE:
      switch (entry2->propstype) {
	/* t   <--->   t */
        case GST_PROPS_BOOLEAN_TYPE:
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

/* when running over the entries in sorted order we can
 * optimize addition with _prepend and a reverse at the end */
#define gst_props_entry_add_sorted_prepend(props, entry) 		\
G_STMT_START {						 		\
  /* avoid double evaluation of input */				\
  GstPropsEntry *toadd = (entry);					\
  if (GST_PROPS_ENTRY_IS_VARIABLE (toadd))		 		\
    GST_PROPS_FLAG_UNSET ((props), GST_PROPS_FIXED);			\
  props->properties = g_list_prepend ((props)->properties, toadd);	\
} G_STMT_END

/**
 * gst_props_intersect:
 * @props1: a property
 * @props2: another property
 *
 * Calculates the intersection bewteen two GstProps.
 *
 * Returns: a GstProps with the intersection or NULL if the 
 * intersection is empty. The new GstProps is floating and must
 * be unreffed afetr use.
 */
GstProps*
gst_props_intersect (GstProps *props1, GstProps *props2)
{
  GList *props1list;
  GList *props2list;
  GstProps *intersection;
  GList *leftovers;
  GstPropsEntry *iprops = NULL;

  g_return_val_if_fail (props1 != NULL, NULL);
  g_return_val_if_fail (props2 != NULL, NULL);

  intersection = gst_props_empty_new ();

  props1list = props1->properties;
  props2list = props2->properties;

  while (props1list && props2list) {
    GstPropsEntry *entry1;
    GstPropsEntry *entry2;

    entry1 = (GstPropsEntry *)props1list->data;
    entry2 = (GstPropsEntry *)props2list->data;

    while (entry1->propid < entry2->propid) {
      gst_props_entry_add_sorted_prepend (intersection, gst_props_entry_copy (entry1));

      props1list = g_list_next (props1list);
      if (!props1list)
	goto end;

      entry1 = (GstPropsEntry *)props1list->data;
    }
    while (entry1->propid > entry2->propid) {
      gst_props_entry_add_sorted_prepend (intersection, gst_props_entry_copy (entry2));

      props2list = g_list_next (props2list);
      if (!props2list)
	goto end;

      entry2 = (GstPropsEntry *)props2list->data;
    }
    /* at this point we are talking about the same property */
    iprops = gst_props_entry_intersect (entry1, entry2);
    if (!iprops) {
      /* common properties did not intersect, intersection is empty */
      gst_props_unref (intersection);
      return NULL;
    }

    gst_props_entry_add_sorted_prepend (intersection, iprops);

    props1list = g_list_next (props1list);
    props2list = g_list_next (props2list);
  }

end:
  /* at this point one of the lists could contain leftover properties, while
   * the other one is NULL */
  leftovers = props1list;
  if (!leftovers)
    leftovers = props2list;

  while (leftovers) {
    gst_props_entry_add_sorted_prepend (intersection,
		         gst_props_entry_copy ((GstPropsEntry *) leftovers->data));
    leftovers = g_list_next (leftovers);
  }

  intersection->properties = g_list_reverse (intersection->properties);

  return intersection;
}

/**
 * gst_props_normalize:
 * @props: a property
 *
 * Unrolls all lists in the given GstProps. This is usefull if you
 * want to loop over the props.
 *
 * Returns: A GList with the unrolled props entries. g_list_free 
 * after usage.
 */
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

	/* FIXME fixed flags is probably messed up here */
	newprops = gst_props_copy (props);
        lentry = g_list_find_custom (newprops->properties, GINT_TO_POINTER (list_entry->propid), props_find_func);
	if (lentry) {
          GList *new_list;

          new_entry = (GstPropsEntry *) lentry->data;
	  memcpy (new_entry, list_entry, sizeof (GstPropsEntry));

	  new_list = gst_props_normalize (newprops);
          result = g_list_concat (new_list, result);
	}
	else {
          /* FIXME append or prepend */
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
    /* no result, create list with input props */
    result = g_list_prepend (result, props);
  }
  else {
    result = g_list_reverse (result);
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
      str = g_strdup_printf ("%c%c%c%c",
	(entry->data.fourcc_data>>0)&0xff,
	(entry->data.fourcc_data>>8)&0xff,
	(entry->data.fourcc_data>>16)&0xff,
	(entry->data.fourcc_data>>24)&0xff);
      xmlAddChild (parent, xmlNewComment (str));
      g_free(str);
      subtree = xmlNewChild (parent, NULL, "fourcc", NULL);
      xmlNewProp (subtree, "name", g_quark_to_string (entry->propid));
      str = g_strdup_printf ("%08x", entry->data.fourcc_data);
      xmlNewProp (subtree, "hexvalue", str);
      g_free(str);
      break;
    case GST_PROPS_BOOLEAN_TYPE:
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
    entry->propstype = GST_PROPS_BOOLEAN_TYPE;
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
    gst_props_entry_destroy (entry);
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

  props = gst_props_empty_new ();

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

