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

#include "gstdata.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_IS_EVENT(event)		(((GstData*)(event))->type > GST_EVENT_FIRST)

/* seek events */
typedef enum {
  GST_SEEK_ANY,
  GST_SEEK_SET,
  GST_SEEK_CUR,
  GST_SEEK_END,
} GstSeekType;

typedef enum {
  GST_ACCURACY_NONE,
  GST_ACCURACY_WILD_GUESS,
  GST_ACCURACY_GUESS,
  GST_ACCURACY_SURE,
} GstEventAccuracy;

#define GST_EVENT_EOS(event)		((GstEventEOS *) (event))
#define GST_EVENT_DISCONTINUOUS(event)	((GstEventDiscontinuous *) (event))
#define GST_EVENT_NEWMEDIA(event)	((GstEventNewMedia *) (event))
#define GST_EVENT_LENGTH(event)		((GstEventLength *) (event))
#define GST_EVENT_SEEK(event)		((GstEventSeek *) (event))
#define GST_EVENT_FLUSH(event)		((GstEventFlush *) (event))
#define GST_EVENT_EMPTY(event)		((GstEventEmpty *) (event))
#define GST_EVENT_LOCK(event)		((GstEventLock *) (event))
#define GST_EVENT_UNLOCK(event)		((GstEventUnLock *) (event))

#define GST_EVENT_SEEK_TYPE(event)	(GST_EVENT_SEEK(event)->type)

typedef struct _GstEventEOS GstEventEOS;
typedef struct _GstEventDiscontinuous GstEventDiscontinuous;
typedef struct _GstEventNewMedia GstEventNewMedia;
typedef struct _GstEventLength GstEventLength;

typedef struct _GstEventLock GstEventLock;
typedef struct _GstEventUnLock GstEventUnLock;
typedef struct _GstEventSeek GstEventSeek;
typedef struct _GstEventFlush GstEventFlush;
typedef struct _GstEventEmpty GstEventEmpty;
  
typedef void (*GstLockFunction) (gpointer data);

struct _GstEventEOS {
  GstData	 data;
};

struct _GstEventDiscontinuous {
  GstData	 data;
};

struct _GstEventNewMedia {
  GstData	 data;
};

struct _GstEventSeek {
  GstData	 	data;

  GstSeekType	 	type;
  GstOffsetType  	original;
  GstEventAccuracy	accuracy[GST_OFFSET_TYPES];
  gint64   	 	offset[GST_OFFSET_TYPES];
  
  gboolean		flush;
};

struct _GstEventFlush {
  GstData	 	data;
};

struct _GstEventEmpty {
  GstData	 	data;
};

struct _GstEventLock {
  GstData		data;
  
  GstLockFunction	on_delete;
  gpointer		func_data;
};

struct _GstEventUnLock {
  GstData	 	data;
};

struct _GstEventLength {
  GstData	 	data;

  GstOffsetType 	original;
  GstEventAccuracy	accuracy[GST_OFFSET_TYPES];
  guint64   	 	length[GST_OFFSET_TYPES];
};

/* initialize the event system */
void 		_gst_event_initialize 	(void);

/* inheritance */
void		gst_event_init		(GstData *data);

void		gst_event_seek_init	(GstEventSeek *event, GstSeekType type, GstOffsetType offset_type);
void		gst_event_lock_init	(GstEventLock *event);
void		gst_event_length_init	(GstEventLength *event);

/* private */
GstData*	gst_event_new	        (GstDataType type);
/* creation */
#define		gst_event_new_eos()	((GstEventEOS *) gst_event_new(GST_EVENT_EOS))
#define		gst_event_new_discontinuous() ((GstEventDiscontinuous *) gst_event_new(GST_EVENT_DISCONTINUOUS))
#define		gst_event_new_newmedia() ((GstEventNewMedia *) gst_event_new(GST_EVENT_NEWMEDIA))
GstEventLength *gst_event_new_length	(GstOffsetType original, GstEventAccuracy accuracy, guint64 length);
GstEventSeek *	gst_event_new_seek	(GstSeekType type, GstOffsetType offset_type, gint64 offset, gboolean flush);
#define		gst_event_new_flush()	((GstEventFlush *) gst_event_new(GST_EVENT_FLUSH))
#define		gst_event_new_empty()	((GstEventEmpty *) gst_event_new(GST_EVENT_EMPTY))
GstEventLock *	gst_event_new_lock	(GstLockFunction func, gpointer data);
#define		gst_event_new_unlock()	((GstEventUnLock *) gst_event_new(GST_EVENT_UNLOCK))

/* copying */
#define		gst_event_copy_eos(from, to)		gst_data_copy(GST_DATA(to), (const GstData *) from);
#define		gst_event_copy_discontinuous(from, to)	gst_data_copy(GST_DATA(to), (const GstData *) from);
#define		gst_event_copy_newmedia(from, to)	gst_data_copy(const GstData *) from, GST_DATA(to));
void		gst_event_copy_length			(GstEventLength *to, const GstEventLength *from);
void		gst_event_copy_seek			(GstEventSeek *to, const GstEventSeek *from);
#define		gst_event_copy_flush(from, to)		gst_data_copy(GST_DATA(to), (const GstData *) from);
#define		gst_event_copy_empty(from, to)		gst_data_copy(GST_DATA(to), (const GstData *) from);
void		gst_event_copy_lock			(GstEventLock *to, const GstEventLock *from);
#define		gst_event_copy_unlock(from, to)		gst_data_copy(GST_DATA(to), (const GstData *) from);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __GST_EVENT_H__ */
