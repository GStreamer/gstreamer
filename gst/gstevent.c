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
  };

  // round up to the nearest 32 bytes for cache-line and other efficiencies
  eventsize = (((eventsize-1) / 32) + 1) * 32;

  _gst_event_chunk = g_mem_chunk_new ("GstEvent", eventsize,
  				      eventsize * 32, G_ALLOC_AND_FREE);

  _gst_event_chunk_lock = g_mutex_new ();

  // register the type
  _gst_event_type = g_type_register_static (G_TYPE_INT, "GstEvent", &event_info, 0);
}

GstEvent*
gst_event_new (GstEventType type)
{
  GstEvent *event;

  g_mutex_lock (_gst_event_chunk_lock);
  event = g_mem_chunk_alloc (_gst_event_chunk);
  g_mutex_unlock (_gst_event_chunk_lock);
  GST_INFO (GST_CAT_EVENT, "creating new event %p", event);

  GST_DATA_TYPE (event) = _gst_event_type;
  GST_EVENT_TYPE (event) = type;

  return event;
}

void
gst_event_free (GstEvent* event)
{
  g_mutex_lock (_gst_event_chunk_lock);
  g_mem_chunk_free (_gst_event_chunk, event);
  g_mutex_unlock (_gst_event_chunk_lock);
}
