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

#include <gst/gsttypes.h>
#include <gst/gstdata.h>
#include <gst/gstcaps.h>
#include <gst/gstformat.h>

G_BEGIN_DECLS

typedef enum {
  GST_EVENT_UNKNOWN,
  GST_EVENT_EOS,
  GST_EVENT_FLUSH,
  GST_EVENT_EMPTY,
  GST_EVENT_DISCONTINUOUS,
  GST_EVENT_NEW_MEDIA,
  GST_EVENT_QOS,
  GST_EVENT_SEEK,
  GST_EVENT_FILLER,
} GstEventType;

extern GType _gst_event_type;

#define GST_TYPE_EVENT		(_gst_event_type)
#define GST_EVENT(event)	((GstEvent*)(event))
#define GST_IS_EVENT(event)	(GST_DATA_TYPE(event) == GST_TYPE_EVENT)

#define GST_EVENT_TYPE(event)		(GST_EVENT(event)->type)
#define GST_EVENT_TIMESTAMP(event)	(GST_EVENT(event)->timestamp)
#define GST_EVENT_SRC(event)		(GST_EVENT(event)->src)

#define GST_SEEK_FORMAT_SHIFT	0
#define GST_SEEK_METHOD_SHIFT	16
#define GST_SEEK_FLAGS_SHIFT	20
#define GST_SEEK_FORMAT_MASK	0x0000ffff
#define GST_SEEK_METHOD_MASK	0x000f0000
#define GST_SEEK_FLAGS_MASK	0xfff00000

/* seek events */
typedef enum {
  GST_SEEK_METHOD_CUR		= (1 << GST_SEEK_METHOD_SHIFT),
  GST_SEEK_METHOD_SET		= (2 << GST_SEEK_METHOD_SHIFT),
  GST_SEEK_METHOD_END		= (3 << GST_SEEK_METHOD_SHIFT),

  GST_SEEK_FLAG_FLUSH		= (1 << (GST_SEEK_FLAGS_SHIFT + 0)),
  GST_SEEK_FLAG_ACCURATE	= (1 << (GST_SEEK_FLAGS_SHIFT + 1)),
  GST_SEEK_FLAG_KEY_UNIT	= (1 << (GST_SEEK_FLAGS_SHIFT + 2)),
} GstSeekType;

typedef enum {
  GST_SEEK_CERTAIN,
  GST_SEEK_FUZZY,
} GstSeekAccuracy;

typedef struct
{
  GstFormat 	format;
  gint64	value;
} GstFormatValue;

#define GST_EVENT_SEEK_TYPE(event)		(GST_EVENT(event)->event_data.seek.type)
#define GST_EVENT_SEEK_FORMAT(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_FORMAT_MASK)
#define GST_EVENT_SEEK_METHOD(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_METHOD_MASK)
#define GST_EVENT_SEEK_FLAGS(event)		(GST_EVENT_SEEK_TYPE(event) & GST_SEEK_FLAGS_MASK)
#define GST_EVENT_SEEK_OFFSET(event)		(GST_EVENT(event)->event_data.seek.offset)
#define GST_EVENT_SEEK_ACCURACY(event)		(GST_EVENT(event)->event_data.seek.accuracy)

#define GST_EVENT_DISCONT_NEW_MEDIA(event)	(GST_EVENT(event)->event_data.discont.new_media)
#define GST_EVENT_DISCONT_OFFSET(event,i)	(GST_EVENT(event)->event_data.discont.offsets[i])
#define GST_EVENT_DISCONT_OFFSET_LEN(event)	(GST_EVENT(event)->event_data.discont.noffsets)

struct _GstEvent {
  GstData data;

  GstEventType  type;
  guint64	timestamp;
  GstObject	*src;

  union {
    struct {
      GstSeekType 	type;
      gint64      	offset;
      GstSeekAccuracy 	accuracy;
    } seek;
    struct {
      GstFormatValue 	offsets[8];
      gint      	noffsets;
      gboolean		new_media;
    } discont;
  } event_data;
};

void 		_gst_event_initialize 		(void);
	
GstEvent*	gst_event_new	        	(GstEventType type);
GstEvent*	gst_event_copy	        	(GstEvent *event);
void		gst_event_free 			(GstEvent *event);

/* seek event */
GstEvent*	gst_event_new_seek		(GstSeekType type, gint64 offset);

/* discontinous event */
GstEvent*	gst_event_new_discontinuous	(gboolean new_media,
						 GstFormat format1, ...);
gboolean	gst_event_discont_get_value	(GstEvent *event, GstFormat format, gint64 *value);

#define		gst_event_new_filler()		gst_event_new(GST_EVENT_FILLER)

/* flush events */
#define		gst_event_new_flush()		gst_event_new(GST_EVENT_FLUSH)

G_END_DECLS

#endif /* __GST_EVENT_H__ */
