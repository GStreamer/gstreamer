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


#ifndef __GST_THREAD_H__
#define __GST_THREAD_H__


#include <gst/gstbin.h>
#include <pthread.h>


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

extern GstElementDetails gst_thread_details;


typedef enum {
  GST_THREAD_CREATE		= (1 << 16),
  GST_THREAD_STATE_SPINNING	= (1 << 17),
  GST_THREAD_STATE_REAPING	= (1 << 18),
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

