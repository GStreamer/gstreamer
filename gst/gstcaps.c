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
#include <gst/gstcapsprivate.h>

static gboolean 	gst_caps_entry_check_compatibility 	(GstCapsEntry *entry1, GstCapsEntry *entry2);
	

void 
_gst_caps_initialize (void) 
{
}

static GstCapsEntry *
gst_caps_create_entry (GstCapsFactory factory, gint *skipped)
{
  GstCapsFactoryEntry tag;
  GstCapsEntry *entry;
  guint i=0;

  entry = g_new0 (GstCapsEntry, 1);

  tag = factory[i++];
  switch (GPOINTER_TO_INT (tag)) {
    case GST_CAPS_INT_ID:
      entry->capstype = GST_CAPS_INT_ID_NUM;
      entry->data.int_data = GPOINTER_TO_INT (factory[i++]);
      break;
    case GST_CAPS_INT_RANGE_ID:
      entry->capstype = GST_CAPS_INT_RANGE_ID_NUM;
      entry->data.int_range_data.min = GPOINTER_TO_INT (factory[i++]);
      entry->data.int_range_data.max = GPOINTER_TO_INT (factory[i++]);
      break;
    case GST_CAPS_FOURCC_ID:
      entry->capstype = GST_CAPS_FOURCC_ID_NUM;
      entry->data.fourcc_data = GPOINTER_TO_INT (factory[i++]);
      break;
    case GST_CAPS_LIST_ID:
      g_print("gstcaps: list not allowed in list\n");
      break;
    default:
      g_print("gstcaps: unknown caps id found\n");
      g_free (entry);
      entry = NULL;
      break;
  }

  *skipped = i;

  return entry;
}


static gint 
caps_compare_func (gconstpointer a,
		   gconstpointer b) 
{
  GstCapsEntry *entry1 = (GstCapsEntry *)a;
  GstCapsEntry *entry2 = (GstCapsEntry *)b;

  return (entry1->propid - entry2->propid);
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
  gint skipped;
  
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
  caps->properties = NULL;

  tag = factory[i++];
  
  while (tag) {
    GQuark quark;
    GstCapsEntry *entry;
    
    quark = g_quark_from_string ((gchar *)tag);

    tag = factory[i];
    switch (GPOINTER_TO_INT (tag)) {
      case GST_CAPS_LIST_ID: 
      {
        GstCapsEntry *list_entry;

        entry = g_new0 (GstCapsEntry, 1);
	entry->propid = quark;
	entry->capstype = GST_CAPS_LIST_ID_NUM;
	entry->data.list_data.entries = NULL;

	i++; // skip list tag
        tag = factory[i];
	while (tag) {
	  list_entry = gst_caps_create_entry (&factory[i], &skipped);
	  list_entry->propid = quark;
	  i += skipped;
          tag = factory[i];
	  entry->data.list_data.entries = g_list_prepend (entry->data.list_data.entries, list_entry);
	}
	entry->data.list_data.entries = g_list_reverse (entry->data.list_data.entries);
	i++; //skip NULL (list end)
	break;
      }
      default:
      {
	entry = gst_caps_create_entry (&factory[i], &skipped);
	entry->propid = quark;
	i += skipped;
	break;
      }
    }
    caps->properties = g_slist_insert_sorted (caps->properties, entry, caps_compare_func);
     
    tag = factory[i++];
  }

  return caps;
}

static void
gst_caps_dump_entry_func (GstCapsEntry *entry)
{
  switch (entry->capstype) {
    case GST_CAPS_INT_ID_NUM: 
      g_print("gstcaps:    int %d\n", entry->data.int_data);
      break;
    case GST_CAPS_INT_RANGE_ID_NUM: 
      g_print("gstcaps:    int range %d %d\n", 
		      entry->data.int_range_data.min,
		      entry->data.int_range_data.max);
      break;
    case GST_CAPS_FOURCC_ID_NUM: 
      g_print("gstcaps:    fourcc 0x%08x (%4.4s)\n", entry->data.fourcc_data, &entry->data.fourcc_data);
      break;
    case GST_CAPS_BOOL_ID_NUM: 
      g_print("gstcaps:    boolean %d\n", entry->data.bool_data);
      break;
    default:
      g_print("gstcaps:    **illegal entry**\n");
      break;
  }
}

static void
gst_caps_dump_list_func (gpointer entry,
		         gpointer list_entry)
{
  gst_caps_dump_entry_func ((GstCapsEntry *)entry);
}

static void
gst_caps_dump_func (gpointer data,
		    gpointer user_data)
{
  GstCapsEntry *entry;

  entry = (GstCapsEntry *)data;

  g_print("gstcaps:  property type \"%s\"\n", g_quark_to_string (entry->propid));

  switch (entry->capstype) {
    case GST_CAPS_LIST_ID_NUM: 
    {
      g_print("gstcaps:   list type (\n");
      g_list_foreach (entry->data.list_data.entries, gst_caps_dump_list_func, entry);
      g_print("gstcaps:   )\n");
      break;
    }
    default:
      gst_caps_dump_entry_func (entry);
      break;
  }
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

  g_slist_foreach (caps->properties, gst_caps_dump_func, caps);
  g_print("gstcaps: }\n");
}
	
/* entry2 is always a list, entry1 never is */
static gboolean
gst_caps_entry_check_list_compatibility (GstCapsEntry *entry1, GstCapsEntry *entry2)
{
  GList *entrylist = entry2->data.list_data.entries;
  gboolean found = FALSE;

  while (entrylist && !found) {
    GstCapsEntry *entry = (GstCapsEntry *) entrylist->data;

    found |= gst_caps_entry_check_compatibility (entry1, entry);

    entrylist = g_list_next (entrylist);
  }

  return found;
}

static gboolean
gst_caps_entry_check_compatibility (GstCapsEntry *entry1, GstCapsEntry *entry2)
{
  DEBUG ("compare: %s %s\n", g_quark_to_string (entry1->propid),
	                     g_quark_to_string (entry2->propid));
  switch (entry1->capstype) {
    case GST_CAPS_LIST_ID_NUM:
    {
      GList *entrylist = entry1->data.list_data.entries;
      gboolean valid = TRUE;    // innocent until proven guilty

      while (entrylist && valid) {
	GstCapsEntry *entry = (GstCapsEntry *) entrylist->data;

	valid &= gst_caps_entry_check_compatibility (entry, entry2);
	
	entrylist = g_list_next (entrylist);
      }
      
      return valid;
    }
    case GST_CAPS_INT_RANGE_ID_NUM:
      switch (entry2->capstype) {
	// a - b   <--->   a - c
        case GST_CAPS_INT_RANGE_ID_NUM:
	  return (entry2->data.int_range_data.min <= entry1->data.int_range_data.min &&
	          entry2->data.int_range_data.max >= entry1->data.int_range_data.max);
        case GST_CAPS_LIST_ID_NUM:
	  return gst_caps_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_CAPS_FOURCC_ID_NUM:
      switch (entry2->capstype) {
	// b   <--->   a
        case GST_CAPS_FOURCC_ID_NUM:
	  return (entry2->data.fourcc_data == entry1->data.fourcc_data);
	// b   <--->   a,b,c
        case GST_CAPS_LIST_ID_NUM:
	  return gst_caps_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_CAPS_INT_ID_NUM:
      switch (entry2->capstype) {
	// b   <--->   a - d
        case GST_CAPS_INT_RANGE_ID_NUM:
	  return (entry2->data.int_range_data.min <= entry1->data.int_data &&
	          entry2->data.int_range_data.max >= entry1->data.int_data);
	// b   <--->   a
        case GST_CAPS_INT_ID_NUM:
	  return (entry2->data.int_data == entry1->data.int_data);
	// b   <--->   a,b,c
        case GST_CAPS_LIST_ID_NUM:
	  return gst_caps_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
      break;
    case GST_CAPS_BOOL_ID_NUM:
      switch (entry2->capstype) {
	// t   <--->   t
        case GST_CAPS_BOOL_ID_NUM:
          return (entry2->data.bool_data == entry1->data.bool_data);
        case GST_CAPS_LIST_ID_NUM:
	  return gst_caps_entry_check_list_compatibility (entry1, entry2);
        default:
          return FALSE;
      }
    default:
      break;
  }

  return FALSE;
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
  GSList *sourcelist;
  GSList *sinklist;
  gint missing = 0;
  gint more = 0;
  gboolean compatible = TRUE;

  g_return_val_if_fail (fromcaps != NULL, FALSE);
  g_return_val_if_fail (tocaps != NULL, FALSE);
	
  if (fromcaps->id != tocaps->id)
    return FALSE;

  sourcelist = fromcaps->properties;
  sinklist   = tocaps->properties;

  while (sourcelist && sinklist && compatible) {
    GstCapsEntry *entry1;
    GstCapsEntry *entry2;

    entry1 = (GstCapsEntry *)sourcelist->data;
    entry2 = (GstCapsEntry *)sinklist->data;

    while (entry1->propid < entry2->propid) {
      DEBUG ("source is more specific in \"%s\"\n", g_quark_to_string (entry1->propid));
      more++;
      sourcelist = g_slist_next (sourcelist);
      if (sourcelist) entry1 = (GstCapsEntry *)sourcelist->data;
      else goto end;
    }
    while (entry1->propid > entry2->propid) {
      DEBUG ("source has missing property \"%s\"\n", g_quark_to_string (entry2->propid));
      missing++;
      sinklist = g_slist_next (sinklist);
      if (sinklist) entry2 = (GstCapsEntry *)sinklist->data;
      else goto end;
    }

    compatible &= gst_caps_entry_check_compatibility (entry1, entry2);

    sourcelist = g_slist_next (sourcelist);
    sinklist = g_slist_next (sinklist);
  }
end:

  if (missing)
    return FALSE;

  return compatible;
}

/**
 * gst_caps_register_va:
 * @factory: the factories to register
 *
 * Register the given factories. 
 *
 * Returns: A list of the registered factories
 */
GList *
gst_caps_register_va (GstCapsFactory factory, ...)
{
  va_list var_args;
  GstCapsFactoryEntry *current_factory;

  va_start (var_args, factory);

  current_factory = (GstCapsFactoryEntry *) factory;
  
  while (current_factory) {
    gst_caps_register (current_factory);
    
    current_factory = va_arg (var_args, GstCapsFactoryEntry *);
  }
  
  va_end(var_args);

  return NULL;
}
