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

#include <sys/time.h>

/* #define GST_DEBUG_ENABLED */
#include "gst_private.h"

#include "gstclock.h"

#define CLASS(clock)  GST_CLOCK_CLASS (G_OBJECT_GET_CLASS (clock))

static GMemChunk *_gst_clock_entries_chunk;
static GMutex *_gst_clock_entries_chunk_lock;
static GList *_gst_clock_entries_pool;

static void		gst_clock_class_init		(GstClockClass *klass);
static void		gst_clock_init			(GstClock *clock);


static GstObjectClass *parent_class = NULL;
/* static guint gst_clock_signals[LAST_SIGNAL] = { 0 }; */

typedef struct _GstClockEntry GstClockEntry;

static void 		gst_clock_free_entry 		(GstClock *clock, GstClockEntry *entry);

typedef enum {
  GST_ENTRY_OK,
  GST_ENTRY_RESTART,
} GstEntryStatus;

struct _GstClockEntry {
  GstClockTime 		 time;
  GstEntryStatus 	 status;
  GstClockCallback 	 func;
  gpointer		 user_data;
  GMutex 		*lock;
  GCond 		*cond;
};

#define GST_CLOCK_ENTRY(entry)          ((GstClockEntry *)(entry))
#define GST_CLOCK_ENTRY_TIME(entry)     (((GstClockEntry *)(entry))->time)
#define GST_CLOCK_ENTRY_LOCK(entry)     (g_mutex_lock ((entry)->lock))
#define GST_CLOCK_ENTRY_UNLOCK(entry)   (g_mutex_unlock ((entry)->lock))
#define GST_CLOCK_ENTRY_SIGNAL(entry)   (g_cond_signal ((entry)->cond))
#define GST_CLOCK_ENTRY_WAIT(entry)     (g_cond_wait (entry->cond, entry->lock))
#define GST_CLOCK_ENTRY_TIMED_WAIT(entry, time)         (g_cond_timed_wait (entry->cond, entry->lock, (time)))

static GstClockEntry*
gst_clock_entry_new (GstClockTime time,
		     GstClockCallback func, gpointer user_data)
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
  entry->func = func;
  entry->user_data = user_data;

  return entry;
}

static gint
clock_compare_func (gconstpointer a,
                    gconstpointer b)
{
  GstClockEntry *entry1 = (GstClockEntry *)a;
  GstClockEntry *entry2 = (GstClockEntry *)b;

  return (entry1->time - entry2->time);
}

GType
gst_clock_get_type (void)
{
  static GType clock_type = 0;

  if (!clock_type) {
    static const GTypeInfo clock_info = {
      sizeof (GstClockClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_clock_class_init,
      NULL,
      NULL,
      sizeof (GstClock),
      4,
      (GInstanceInitFunc) gst_clock_init,
      NULL
    };
    clock_type = g_type_register_static (GST_TYPE_OBJECT, "GstClock", 
		    			 &clock_info,  G_TYPE_FLAG_ABSTRACT);
  }
  return clock_type;
}

static void
gst_clock_class_init (GstClockClass *klass)
{
  GObjectClass *gobject_class;
  GstObjectClass *gstobject_class;

  gobject_class = (GObjectClass*) klass;
  gstobject_class = (GstObjectClass*) klass;

  parent_class = g_type_class_ref (GST_TYPE_OBJECT);

  _gst_clock_entries_chunk = g_mem_chunk_new ("GstClockEntries",
                     sizeof (GstClockEntry), sizeof (GstClockEntry) * 32,
                     G_ALLOC_AND_FREE);
  _gst_clock_entries_chunk_lock = g_mutex_new ();
  _gst_clock_entries_pool = NULL;
}

static void
gst_clock_init (GstClock *clock)
{
  clock->speed = 1.0;
  clock->active = FALSE;
  clock->start_time = 0;
  clock->last_time = 0;
  clock->entries = NULL;
  clock->async_supported = FALSE;

  clock->active_mutex = g_mutex_new ();
  clock->active_cond = g_cond_new ();
}

gboolean
gst_clock_async_supported (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), FALSE);

  return clock->async_supported;
}


void
gst_clock_reset (GstClock *clock)
{
  GstClockTime time = 0LL;

  g_return_if_fail (GST_IS_CLOCK (clock));

  if (CLASS (clock)->get_internal_time) {
    time = CLASS (clock)->get_internal_time (clock);
  }

  GST_LOCK (clock);
  clock->active = FALSE;
  clock->start_time = time;
  clock->last_time = 0LL;
  GST_UNLOCK (clock);
}

void
gst_clock_activate (GstClock *clock, gboolean active)
{
  GstClockTime time = 0LL;

  g_return_if_fail (GST_IS_CLOCK (clock));

  clock->active = active;
	        
  if (CLASS (clock)->get_internal_time) {
    time = CLASS (clock)->get_internal_time (clock);
  }

  GST_LOCK (clock);
  if (active) {
    clock->start_time = time - clock->last_time;;
  }
  else {
    clock->last_time = time - clock->start_time;
  }
  GST_UNLOCK (clock);

  g_mutex_lock (clock->active_mutex);	
  g_cond_broadcast (clock->active_cond);	
  g_mutex_unlock (clock->active_mutex);	
}

gboolean
gst_clock_is_active (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), FALSE);

  return clock->active;
}

GstClockTime
gst_clock_get_time (GstClock *clock)
{
  GstClockTime ret = 0LL;

  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);

  if (!clock->active) {
    /* clock is not activen return previous time */
    ret = clock->last_time;
  }
  else {
    if (CLASS (clock)->get_internal_time) {
      ret = CLASS (clock)->get_internal_time (clock) - clock->start_time;
    }
    /* make sure the time is increasing, else return last_time */
    if (ret < clock->last_time) {
      ret = clock->last_time;
    }
    else {
      clock->last_time = ret;
    }
  }

  return ret;
}

static GstClockID
gst_clock_wait_async_func (GstClock *clock, GstClockTime time,
		           GstClockCallback func, gpointer user_data)
{
  GstClockEntry *entry = NULL;
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  if (!clock->active) {
    g_mutex_lock (clock->active_mutex);	
    g_cond_wait (clock->active_cond, clock->active_mutex);	
    g_mutex_unlock (clock->active_mutex);	
  }

  entry = gst_clock_entry_new (time, func, user_data);

  GST_LOCK (clock);
  clock->entries = g_list_insert_sorted (clock->entries, entry, clock_compare_func);
  GST_UNLOCK (clock);

  return entry;
}

GstClockReturn
gst_clock_wait (GstClock *clock, GstClockTime time)
{
  GstClockID id;
  GstClockReturn res;
  
  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_STOPPED);

  id = gst_clock_wait_async_func (clock, time, NULL, NULL);
  res = gst_clock_wait_id (clock, id);

  return res;
}

GstClockID
gst_clock_wait_async (GstClock *clock, GstClockTime time,
		      GstClockCallback func, gpointer user_data)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), NULL);

  if (clock->async_supported) {
    return gst_clock_wait_async_func (clock, time, func, user_data);
  }
  return NULL;
}

static void
gst_clock_unlock_func (GstClock *clock, GstClockTime time, GstClockID id, gpointer user_data)
{
  GstClockEntry *entry = (GstClockEntry *) id;

  GST_CLOCK_ENTRY_LOCK (entry);
  GST_CLOCK_ENTRY_SIGNAL (entry);
  GST_CLOCK_ENTRY_UNLOCK (entry);
}

GstClockReturn
gst_clock_wait_id (GstClock *clock, GstClockID id)
{
  GstClockReturn res = GST_CLOCK_TIMEOUT;
  GstClockEntry *entry = (GstClockEntry *) id;
  GstClockTime current_real, current, target;
  GTimeVal timeval;
  
  g_return_val_if_fail (GST_IS_CLOCK (clock), GST_CLOCK_ERROR);
  g_return_val_if_fail (entry, GST_CLOCK_ERROR);

  current = gst_clock_get_time (clock);

  g_get_current_time (&timeval);
  current_real = GST_TIMEVAL_TO_TIME (timeval);

  GST_CLOCK_ENTRY_LOCK (entry);
  entry->func = gst_clock_unlock_func;
  target = GST_CLOCK_ENTRY_TIME (entry) - current + current_real;

  //g_print ("%lld %lld %lld\n", target, current, current_real);
  
  if (target > current_real) {
    timeval.tv_usec = target % 1000000;
    timeval.tv_sec = target / 1000000;

    GST_CLOCK_ENTRY_TIMED_WAIT (entry, &timeval);
  }
  GST_CLOCK_ENTRY_UNLOCK (entry);

  gst_clock_free_entry (clock, entry);

  return res;
}

GstClockID
gst_clock_get_next_id (GstClock *clock)
{
  GstClockEntry *entry = NULL;

  GST_LOCK (clock);
  if (clock->entries)
    entry = GST_CLOCK_ENTRY (clock->entries->data);
  GST_UNLOCK (clock);

  return (GstClockID *) entry;
}

GstClockTime
gst_clock_id_get_time (GstClockID id)
{
  return GST_CLOCK_ENTRY_TIME (id);
}

static void
gst_clock_free_entry (GstClock *clock, GstClockEntry *entry)
{
  GST_LOCK (clock);
  clock->entries = g_list_remove (clock->entries, entry);
  GST_UNLOCK (clock);

  g_mutex_lock (_gst_clock_entries_chunk_lock);
  _gst_clock_entries_pool = g_list_prepend (_gst_clock_entries_pool, entry);
  g_mutex_unlock (_gst_clock_entries_chunk_lock);
}

void
gst_clock_unlock_id (GstClock *clock, GstClockID id)
{
  GstClockEntry *entry = (GstClockEntry *) id;

  if (entry->func)
    entry->func (clock, gst_clock_get_time (clock), id, entry->user_data);

  gst_clock_free_entry (clock, entry);
}

void
gst_clock_set_resolution (GstClock *clock, guint64 resolution)
{
  g_return_if_fail (GST_IS_CLOCK (clock));

  if (CLASS (clock)->set_resolution)
    CLASS (clock)->set_resolution (clock, resolution);
}

guint64
gst_clock_get_resolution (GstClock *clock)
{
  g_return_val_if_fail (GST_IS_CLOCK (clock), 0LL);

  if (CLASS (clock)->get_resolution)
    return CLASS (clock)->get_resolution (clock);

  return 1LL;
}

