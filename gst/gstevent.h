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


#ifndef __GST_EVENT_H__
#define __GST_EVENT_H__

#include <gst/gstobject.h>
#include <gst/gstdata.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

typedef enum {
  GST_EVENT_UNKNOWN,
  GST_EVENT_EOS,
  GST_EVENT_FLUSH,
  GST_EVENT_EMPTY,
  GST_EVENT_SEEK,
  GST_EVENT_DISCONTINUOUS
} GstEventType;

extern GType _gst_event_type;

#define GST_TYPE_EVENT		(_gst_event_type)
#define GST_EVENT(event)	((GstEvent*)(event))
#define GST_IS_EVENT(event)	(GST_DATA_TYPE(event) == GST_TYPE_EVENT)

#define GST_EVENT_TYPE(event)	(GST_EVENT(event)->type)

typedef struct _GstEvent GstEvent;

struct _GstEvent {
  GstData data;

  GstEventType type;
};

void 		_gst_event_initialize 	(void);
	
GstEvent*	gst_event_new	        (GstEventType type);
void		gst_event_free 		(GstEvent* event);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_EVENT_H__ */
