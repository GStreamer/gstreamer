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


#include "gst/gstinfo.h"
#include "gst/gstevent.h"
#include <string.h>		/* memcpy */

/* #define MEMPROF */

GType _gst_event_type;

static GMemChunk *_gst_event_chunk;
static GMutex *_gst_event_chunk_lock;

void
_gst_event_initialize (void)
{
  gint eventsize = sizeof(GstEvent);
  static const GTypeInfo event_info = {
    0,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0, 
    0,
    NULL,
    NULL,
  };

  /* round up to the nearest 32 bytes for cache-line and other efficiencies */
  eventsize = (((eventsize-1) / 32) + 1) * 32;

  _gst_event_chunk = g_mem_chunk_new ("GstEvent", eventsize,
  				      eventsize * 32, G_ALLOC_AND_FREE);
  _gst_event_chunk_lock = g_mutex_new ();

  /* register the type */
  _gst_event_type = g_type_register_static (G_TYPE_INT, "GstEvent", &event_info, 0);
}

/**
 * gst_event_new:
 * @type: The type of the new event
 *
 * Allocate a new event of the given type.
 *
 * Returns: A new event.
 */
GstEvent*
gst_event_new (GstEventType type)
{
  GstEvent *event;

#ifndef MEMPROF
  g_mutex_lock (_gst_event_chunk_lock);
  event = g_mem_chunk_alloc (_gst_event_chunk);
  g_mutex_unlock (_gst_event_chunk_lock);
#else
  event = g_new0(GstEvent, 1);
#endif
  GST_INFO (GST_CAT_EVENT, "creating new event %p", event);

  GST_DATA_TYPE (event) = _gst_event_type;
  GST_EVENT_TYPE (event) = type;
  GST_EVENT_TIMESTAMP (event) = 0LL;
  GST_EVENT_SRC (event) = NULL;

  return event;
}

/**
 * gst_event_copy:
 * @event: The event to copy
 *
 * Copy the event
 *
 * Returns: A copy of the event.
 */
GstEvent*
gst_event_copy (GstEvent *event)
{
  GstEvent *copy;

#ifndef MEMPROF
  g_mutex_lock (_gst_event_chunk_lock);
  copy = g_mem_chunk_alloc (_gst_event_chunk);
  g_mutex_unlock (_gst_event_chunk_lock);
#else
  copy = g_new0(GstEvent, 1);
#endif

  memcpy (copy, event, sizeof (GstEvent));
  
  /* FIXME copy/ref additional fields */

  return copy;
}

/**
 * gst_event_free:
 * @event: The event to free
 *
 * Free the given element.
 */
void
gst_event_free (GstEvent* event)
{
  GST_INFO (GST_CAT_EVENT, "freeing event %p", event);

  g_mutex_lock (_gst_event_chunk_lock);
  if (GST_EVENT_SRC (event)) {
    gst_object_unref (GST_EVENT_SRC (event));
  }
  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }
#ifndef MEMPROF
  g_mem_chunk_free (_gst_event_chunk, event);
#else
  g_free (event);
#endif
  g_mutex_unlock (_gst_event_chunk_lock);
}

/**
 * gst_event_new_seek:
 * @type: The type of the seek event
 * @offset: The offset of the seek
 *
 * Allocate a new seek event with the given parameters.
 *
 * Returns: A new seek event.
 */
GstEvent*       
gst_event_new_seek (GstSeekType type, gint64 offset)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_SEEK);
  GST_EVENT_SEEK_TYPE (event) = type;
  GST_EVENT_SEEK_OFFSET (event) = offset;

  return event;
}

/**
 * gst_event_new_discontinuous:
 * @new_media: A flag indicating a new media type starts
 * @format1: The format of the discont value
 * @...: more discont values and formats
 *
 * Allocate a new discontinuous event with the geven format/value pairs.
 *
 * Returns: A new discontinuous event.
 */
GstEvent*
gst_event_new_discontinuous (gboolean new_media, GstSeekType format1, ...)
{
  va_list var_args;
  GstEvent *event;
  gint count = 0;

  event = gst_event_new (GST_EVENT_DISCONTINUOUS);
  GST_EVENT_DISCONT_NEW_MEDIA (event) = new_media;

  va_start (var_args, format1);
	        
  while (format1) {

    GST_EVENT_DISCONT_OFFSET (event, count).format = format1 & GST_SEEK_FORMAT_MASK;
    GST_EVENT_DISCONT_OFFSET (event, count).value = va_arg (var_args, gint64);

    format1 = va_arg (var_args, GstSeekType);

    count++;
  }
  va_end (var_args);

  GST_EVENT_DISCONT_OFFSET_LEN (event) = count;
		    
  return event;
}

/**
 * gst_event_discont_get_value:
 * @event: The event to query
 * @format: The format of the discont value
 * @value: A pointer to the value
 *
 * Get the value for the given format in the dicont event.
 *
 * Returns: TRUE if the discont event caries the specified format/value pair.
 */
gboolean
gst_event_discont_get_value (GstEvent *event, GstFormat format, gint64 *value)
{
  gint i, n;

  g_return_val_if_fail (event, FALSE);
  g_return_val_if_fail (value, FALSE);

  n = GST_EVENT_DISCONT_OFFSET_LEN (event);

  for (i = 0; i < n; i++) {
    if (GST_EVENT_DISCONT_OFFSET(event,i).format == format) {
      *value = GST_EVENT_DISCONT_OFFSET(event,i).value;
      return TRUE;
    }
  }
  
  return FALSE;
}


GstEvent*
gst_event_new_size (GstFormat format, gint64 value)
{
  GstEvent *event;

  event = gst_event_new (GST_EVENT_SIZE);
  GST_EVENT_SIZE_FORMAT (event) = format;
  GST_EVENT_SIZE_VALUE (event) = value;
  
  return event;
}



