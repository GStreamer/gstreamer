/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstclock.c: Clock subsystem for maintaining time sync
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

#include "gstossclock.h"

static GMemChunk *_gst_clock_entries_chunk;
static GMutex *_gst_clock_entries_chunk_lock;
static GList *_gst_clock_entries_pool;

typedef struct _GstClockEntry GstClockEntry;

typedef enum {
  GST_ENTRY_OK,
  GST_ENTRY_RESTART,
} GstEntryStatus;

struct _GstClockEntry {
  GstClockTime time;
  GstEntryStatus status; 
  GMutex *lock;
  GCond *cond;
};

static void 		gst_oss_clock_class_init 	(GstOssClockClass *klass);
static void 		gst_oss_clock_init 		(GstOssClock *clock);

static void 		gst_oss_clock_reset 		(GstClock *clock);
static void		gst_oss_clock_activate 		(GstClock *clock, gboolean activate);
static void 		gst_oss_clock_set_time 		(GstClock *clock, GstClockTime time);
static GstClockReturn	gst_oss_clock_wait 		(GstClock *clock, GstClockTime time);

static GstSystemClockClass *parent_class = NULL;
/* static guint gst_oss_clock_signals[LAST_SIGNAL] = { 0 }; */
  
GType
gst_oss_clock_get_type (void)
{ 
  static GType clock_type = 0;
	    
  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstOssClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_oss_clock_class_init,
      NULL,
      NULL,
      sizeof (GstOssClock),
      4,
      (GInstanceInitFunc) gst_oss_clock_init,
      NULL
    };
    clock_type = g_type_register_static (GST_TYPE_SYSTEM_CLOCK, "GstOssClock",
                                         &clock_info, 0);
  }
  return clock_type;
}


static void
gst_oss_clock_class_init (GstOssClockClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;
  GstClockClass *gstclock_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;
  gstclock_class = (GstClockClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_SYSTEM_CLOCK);

  gstclock_class->reset = 	gst_oss_clock_reset;
  gstclock_class->activate = 	gst_oss_clock_activate;
  gstclock_class->set_time =	gst_oss_clock_set_time;
  gstclock_class->wait = 	gst_oss_clock_wait;
}

static void
gst_oss_clock_init (GstOssClock *clock)
{
  gst_object_set_name (GST_OBJECT (clock), "GstOssClock");
  clock->is_updated = FALSE;
}

#define GST_CLOCK_ENTRY(entry)  	((GstClockEntry *)(entry))
#define GST_CLOCK_ENTRY_TIME(entry)  	(((GstClockEntry *)(entry))->time)
#define GST_CLOCK_ENTRY_LOCK(entry)  	(g_mutex_lock ((entry)->lock))
#define GST_CLOCK_ENTRY_UNLOCK(entry)  	(g_mutex_unlock ((entry)->lock))
#define GST_CLOCK_ENTRY_SIGNAL(entry)  	(g_cond_signal ((entry)->cond))
#define GST_CLOCK_ENTRY_WAIT(entry)  	(g_cond_wait (entry->cond, entry->lock))
#define GST_CLOCK_ENTRY_TIMED_WAIT(entry, time)  	(g_cond_timed_wait (entry->cond, entry->lock, (time)))

static GstClockEntry*
gst_clock_entry_new (GstClockTime time)
{
  GstClockEntry *entry;

  g_mutex_lock (_gst_clock_entries_chunk_lock);
  if (_gst_clock_entries_pool) {
    entry = GST_CLOCK_ENTRY (_gst_clock_entries_pool->data);

    _gst_clock_entries_pool = g_list_remove (_gst_clock_entries_pool, entry);
    g_mutex_unlock (_gst_clock_entries_chunk_lock);
  }
  else {
    entry = g_mem_chunk_alloc (_gst_clock_entries_chunk);
    g_mutex_unlock (_gst_clock_entries_chunk_lock);

    entry->lock = g_mutex_new ();
    entry->cond = g_cond_new ();
  }

  entry->time = time;

  return entry;
}

static void
gst_clock_entry_free (GstClockEntry *entry)
{
  g_mutex_lock (_gst_clock_entries_chunk_lock);
  _gst_clock_entries_pool = g_list_prepend (_gst_clock_entries_pool, entry);
  g_mutex_unlock (_gst_clock_entries_chunk_lock);
}

GstOssClock*
gst_oss_clock_new (gchar *name, GstElement *owner)
{
  GstOssClock *oss_clock = GST_OSS_CLOCK (g_object_new (GST_TYPE_OSS_CLOCK, NULL));

  oss_clock->entries = NULL;
  oss_clock->current_time = 0;
  oss_clock->next_time = 0;

  _gst_clock_entries_chunk = g_mem_chunk_new ("GstClockEntries",
                     sizeof (GstClockEntry), sizeof (GstClockEntry) * 32,
                     G_ALLOC_AND_FREE);
  _gst_clock_entries_chunk_lock = g_mutex_new ();
  _gst_clock_entries_pool = NULL;

  return oss_clock;
}

void
gst_oss_clock_set_update (GstOssClock *clock, gboolean update)
{
  GST_LOCK (clock);

  if (!update) { 

    GST_UNLOCK (clock);
    GST_CLOCK_CLASS (parent_class)->set_time (GST_CLOCK (clock), clock->current_time);
    GST_LOCK (clock);
    clock->is_updated = FALSE;

    /* FIXME, convert the entries to ones that wait for the system clock */
    if (clock->entries) {
      GList *entries = g_list_copy (clock->entries);
      while (entries) {
        GstClockEntry *entry = (GstClockEntry *)entries->data;
	
        GST_CLOCK_ENTRY_LOCK (entry);
        GST_CLOCK_ENTRY_SIGNAL (entry);
	entry->status = GST_ENTRY_RESTART;
        GST_CLOCK_ENTRY_UNLOCK (entry);

        clock->entries = g_list_remove (clock->entries, entry);
        entries = g_list_next (entries);
      }
    }
  }
  else {
    clock->is_updated = TRUE;
  }
  
  GST_UNLOCK (clock);
}

void
gst_oss_clock_set_base (GstOssClock *clock, guint64 base)
{
  GstOssClock *oss_clock = GST_OSS_CLOCK (clock);

  oss_clock->base_time = base;

  GST_CLOCK_CLASS (parent_class)->set_time (clock, base);
}

static void
gst_oss_clock_reset (GstClock *clock)
{
  GstOssClock *oss_clock = GST_OSS_CLOCK (clock);

  oss_clock->next_time = 0;
  oss_clock->current_time = 0;
  oss_clock->base_time = 0;

  GST_CLOCK_CLASS (parent_class)->reset (clock);
}

static void
gst_oss_clock_activate (GstClock *clock, gboolean activate)
{
  GstOssClock *oss_clock = GST_OSS_CLOCK (clock);

  if (!activate) {
    oss_clock->base_time = oss_clock->current_time;
  }

  GST_CLOCK_CLASS (parent_class)->activate (clock, activate);
}

static void
gst_oss_clock_set_time (GstClock *clock, GstClockTime time)
{
  GList *entries;
  GstOssClock *oss_clock = GST_OSS_CLOCK (clock);

  GST_LOCK (clock);

  time += oss_clock->base_time;

  //g_print ("set time %llu\n", time);

  oss_clock->current_time = time;

  if (oss_clock->next_time > time) {
    GST_UNLOCK (clock);
    return;
  }

  entries = g_list_copy (oss_clock->entries);

  while (entries) {
    GstClockEntry *entry = (GstClockEntry *)entries->data;

    if (GST_CLOCK_ENTRY_TIME (entry) <= oss_clock->current_time) {

      GST_CLOCK_ENTRY_LOCK (entry);
      GST_CLOCK_ENTRY_SIGNAL (entry);
      entry->status = GST_ENTRY_OK;
      GST_CLOCK_ENTRY_UNLOCK (entry);

      oss_clock->entries = g_list_remove (oss_clock->entries, entry);
    }
    else {
      break;
    }
    entries = g_list_next (entries);
  }

  if (oss_clock->entries) {
    oss_clock->next_time = GST_CLOCK_ENTRY_TIME (oss_clock->entries->data);
  }
  else {
    oss_clock->next_time = 0;
  }

  GST_UNLOCK (oss_clock);
}

static gint
clock_compare_func (gconstpointer a,
	            gconstpointer b)
{
  GstClockEntry *entry1 = (GstClockEntry *)a;
  GstClockEntry *entry2 = (GstClockEntry *)b;

  return (entry1->time - entry2->time);
}

static GstClockReturn
gst_oss_clock_wait (GstClock *clock, GstClockTime time)
{
  GstClockReturn ret;
  GstOssClock *oss_clock = GST_OSS_CLOCK (clock);

  GST_LOCK (clock);
restart:
  if (!oss_clock->is_updated) {
    GST_UNLOCK (clock);
    ret = GST_CLOCK_CLASS (parent_class)->wait (clock, time);
  }
  else if (time > oss_clock->current_time) {
    GstClockEntry *entry = gst_clock_entry_new (time);

    oss_clock->entries = g_list_insert_sorted (oss_clock->entries, entry, clock_compare_func);

    oss_clock->next_time = GST_CLOCK_ENTRY_TIME (oss_clock->entries->data);

    GST_CLOCK_ENTRY_LOCK (entry);
    GST_UNLOCK (clock);
    GST_CLOCK_ENTRY_WAIT (entry);
    if (entry->status == GST_ENTRY_RESTART) 
      goto restart;
    GST_CLOCK_ENTRY_UNLOCK (entry);

    gst_clock_entry_free (entry);

    ret = GST_CLOCK_TIMEOUT;
  }
  else {
    GST_UNLOCK (clock);

    ret = GST_CLOCK_EARLY;
  }

  return ret;
}
