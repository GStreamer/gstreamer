/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstevent.h: Header for GstEvent subsystem
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


#include "gstevent.h"
/* debugging and error checking */
#include "gstlog.h"
#include "gstinfo.h"

static void		gst_event_free 		(GstData *data);
static void		gst_event_lock_free	(GstData *data);

/* static GMemChunk *_gst_event_chunk = NULL;*/

void
_gst_event_initialize (void)
{
/*  gint eventsize = sizeof(GstData);
  eventsize = eventsize > sizeof(GstEventEOS) ? eventsize : sizeof(GstEventEOS);
  eventsize = eventsize > sizeof(GstEventDiscontinuous) ? eventsize : sizeof(GstEventDiscontinuous);
  eventsize = eventsize > sizeof(GstEventNewMedia) ? eventsize : sizeof(GstEventNewMedia);
  eventsize = eventsize > sizeof(GstEventLength) ? eventsize : sizeof(GstEventLength);
  eventsize = eventsize > sizeof(GstEventLock) ? eventsize : sizeof(GstEventLock);
  eventsize = eventsize > sizeof(GstEventUnLock) ? eventsize : sizeof(GstEventUnLock);
  eventsize = eventsize > sizeof(GstEventSeek) ? eventsize : sizeof(GstEventSeek);
  eventsize = eventsize > sizeof(GstEventFlush) ? eventsize : sizeof(GstEventFlush);
  eventsize = eventsize > sizeof(GstEventEmpty) ? eventsize : sizeof(GstEventEmpty);

  if (_gst_event_chunk == NULL)
  {
    _gst_event_chunk = g_mem_chunk_new ("GstEvent", eventsize,
					eventsize * 32, G_ALLOC_AND_FREE);
    GST_INFO (GST_CAT_EVENT, "event system initialized.");
  }*/
}
/**
 * gst_event_new:
 * @type: The type of the new event
 *
 * Allocate a new event of the given type.
 *
 * Returns: A new event.
 */
GstData*
gst_event_new (GstDataType type)
{
  GstData *event = NULL;

  switch (type)
  {
    case GST_EVENT_EOS:
    case GST_EVENT_DISCONTINUOUS:
    case GST_EVENT_NEWMEDIA:
    case GST_EVENT_FLUSH:
    case GST_EVENT_EMPTY:
    case GST_EVENT_UNLOCK:
      /* event = g_mem_chunk_alloc (_gst_event_chunk);*/
      event = g_new (GstData, 1);
      g_return_val_if_fail (event != NULL, NULL);
      gst_event_init (event);
      event->type = type;
      break;
    default:
      g_warning ("you called gst_event_new with a custom type (%ld) which is not allowed", (glong) type);
      return NULL;
  }
  GST_DEBUG (GST_CAT_EVENT, "creating new event %p (type %d)", event, type);

  return event;
}
/**
 * gst_event_init:
 * @event: The event to be initialized
 * 
 * Initializes an event.
 * This function should be called by the init routine of a subtype
 * of "event".
 */
void
gst_event_init	(GstData *data)
{
  gst_data_init (data);
  data->free = gst_event_free;
}
static void
gst_event_free (GstData *data)
{
  GST_DEBUG (GST_CAT_EVENT, "freeing event %p", data);
  /* g_mem_chunk_free (_gst_event_chunk, data); */
  g_free (data);
}
/**
 * gst_event_seek_init:
 * @event: The event to be initialized
 * @type: The type of the event
 * @offset_type: The default offset_type
 * 
 * Initializes a seek event.
 * This function should be called by the init routine of a subtype
 * of "seek event".
 */
void
gst_event_seek_init (GstEventSeek *event, GstSeekType type, GstOffsetType offset_type)
{
  guint i;
  
  gst_data_init ((GstData *) event);
  GST_DATA_TYPE (event) = GST_EVENT_SEEK;
  ((GstData *) event)->free = gst_event_free;
  
  GST_EVENT_SEEK_TYPE (event) = type;
  event->original = offset_type;
  for (i = 0; i < GST_OFFSET_TYPES; i++)
  {
    event->offset[i] = GST_OFFSET_INVALID;
    event->accuracy[i] = GST_ACCURACY_NONE;
  }
  event->flush = TRUE;
}
/**
 * gst_event_new_seek:
 * @type: The type of the seek event
 * @offset_type: The type you seek
 * @offset: The offset of the seek
 * @flush: A boolean indicating a flush has to be performed as well
 *
 * Allocate a new seek event with the given parameters.
 *
 * Returns: A new seek event.
 */
GstEventSeek*       
gst_event_new_seek (GstSeekType type, GstOffsetType offset_type, gint64 offset, gboolean flush)
{
  GstEventSeek *event;

  g_return_val_if_fail (offset_type < GST_OFFSET_TYPES, NULL);
  /* event = g_mem_chunk_alloc (_gst_event_chunk); */
  event = g_new (GstEventSeek, 1);
  g_return_val_if_fail (event != NULL, NULL);
  gst_event_seek_init (event, type, offset_type);

  event->accuracy[offset_type] = GST_ACCURACY_SURE;
  event->offset[offset_type] = offset;

  event->flush = flush;

  return event;
}
/**
 * gst_event_copy_seek:
 * @to: where the data should be copied
 * @from: where the data should be taken
 * 
 * Copies all relevant data from one event to the other.
 */
void
gst_event_copy_seek (GstEventSeek *to, const GstEventSeek *from)
{
  guint i;
  
  gst_data_copy (GST_DATA (to), (const GstData *) from);

  to->type = from->type;
  to->original = from->original;
  for (i = 0; i < GST_OFFSET_TYPES; i++)
  {
    to->accuracy[i] = from->accuracy[i];
    to->offset[i] = from->offset[i];
  }
}
/**
 * gst_event_lock_init:
 * @event: The event to be initialized
 * 
 * Initializes a lock event.
 * This function should be called by the init routine of a subtype
 * of "lock event".
 */
void
gst_event_lock_init (GstEventLock *event)
{
  gst_data_init ((GstData *) event);
  GST_DATA_TYPE (event) = GST_EVENT_LOCK;
  ((GstData *) event)->free = gst_event_lock_free;

  event->on_delete = NULL;
  event->func_data = NULL;
}
static void
gst_event_lock_free (GstData *data)
{
  GstEventLock *event = GST_EVENT_LOCK (data);
  
  if (event->on_delete)
  {
    event->on_delete (event->func_data);
  }
  GST_DEBUG (GST_CAT_EVENT, "freeing lock event %p", data);
  /* g_mem_chunk_free (_gst_event_chunk, data); */
  g_free (data);
}
/**
 * gst_event_new_lock:
 * @func: The function to be called upon event destruction
 * @data: The data given to the function
 *
 * Allocate a new lock event with the given parameters.
 *
 * Returns: A new lock event.
 */
GstEventLock *
gst_event_new_lock (GstLockFunction func, gpointer data)
{
  GstEventLock *event;

  /* event = g_mem_chunk_alloc (_gst_event_chunk);*/
  event = g_new (GstEventLock, 1);
  g_return_val_if_fail (event != NULL, NULL);
  gst_event_lock_init (event);

  event->on_delete = func;
  event->func_data = data;
  
  return event;
}
/**
 * gst_event_copy_lock:
 * @to: where the data should be copied
 * @from: where the data should be taken
 * 
 * Copies all relevant data from one event to the other.
 */
void
gst_event_copy_lock (GstEventLock *to, const GstEventLock *from)
{
  gst_data_copy (GST_DATA (to), (const GstData *) from);

  to->on_delete = to->on_delete;
  to->func_data = from->func_data;
}
/**
 * gst_event_length_init:
 * @event: The event to be initialized
 * 
 * Initializes a length event.
 * This function should be called by the init routine of a subtype
 * of "length event".
 */
void
gst_event_length_init (GstEventLength *event)
{
  guint i;
  
  gst_data_init ((GstData *) event);
  GST_DATA_TYPE (event) = GST_EVENT_LENGTH;
  ((GstData *) event)->free = gst_event_free;

  event->original = GST_OFFSET_TYPES;
  for (i = 0; i < GST_OFFSET_TYPES; i++)
  {
    event->accuracy[i] = GST_ACCURACY_NONE;
    event->length[i] = GST_OFFSET_INVALID;
  }
}
/**
 * gst_event_new_length:
 * @original: The original information
 * @accuracy: how accurate this information is
 * @length: The length
 *
 * Allocate a new length event with the given parameters. The accuracy parameter
 * is used to indicate that the length might not be right. For example VBR mp3's can't
 * deliver a completely accurate length when the song starts.
 *
 * Returns: A new length event.
 */
GstEventLength *
gst_event_new_length (GstOffsetType original, GstEventAccuracy accuracy, guint64 length)
{
  GstEventLength *event;

  /* event = g_mem_chunk_alloc (_gst_event_chunk);*/
  event = g_new (GstEventLength, 1);
  g_return_val_if_fail (event != NULL, NULL);
  gst_event_length_init (event);

  event->original = original;
  event->accuracy[original] = accuracy;
  event->length[original] = length;
  
  return event;
}
/**
 * gst_event_copy_length:
 * @to: where the data should be copied
 * @from: where the data should be taken
 * 
 * Copies all relevant data from one event to the other.
 */
void
gst_event_copy_length (GstEventLength *to, const GstEventLength *from)
{
  guint i;
  
  gst_data_copy (GST_DATA (to), (const GstData *) from);

  to->original = from->original;
  for (i = 0; i < GST_OFFSET_TYPES; i++)
  {
    to->accuracy[i] = from->accuracy[i];
    to->length[i] = from->length[i];
  }
}
