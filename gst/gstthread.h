/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstthread.h: Header for GstThread object
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


#ifndef __GST_THREAD_H__
#define __GST_THREAD_H__

#include <pthread.h>

#include <gst/gstbin.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern GstElementDetails gst_thread_details;


typedef enum {
  GST_THREAD_CREATE		= GST_BIN_FLAG_LAST,
  GST_THREAD_STATE_SPINNING,
  GST_THREAD_STATE_REAPING,

  /* padding */
  GST_THREAD_FLAG_LAST 		= GST_BIN_FLAG_LAST + 4,
} GstThreadState;


#define GST_TYPE_THREAD \
  (gst_thread_get_type())
#define GST_THREAD(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_THREAD,GstThread))
#define GST_THREAD_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_THREAD,GstThreadClass))
#define GST_IS_THREAD(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_THREAD))
#define GST_IS_THREAD_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_THREAD))

typedef struct _GstThread 	GstThread;
typedef struct _GstThreadClass 	GstThreadClass;

struct _GstThread {
  GstBin bin;

  pthread_t thread_id;		/* id of the thread, if any */
  GMutex *lock;			/* thread lock/condititon pair... */
  GCond *cond;			/* used to control the thread */

  gint transition;		/* the current state transition */
};

struct _GstThreadClass {
  GstBinClass parent_class;
};

GtkType 	gst_thread_get_type	(void);

GstElement*	gst_thread_new		(guchar *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_THREAD_H__ */     

