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

#include <glib.h>

#include <gst/gstbin.h>


G_BEGIN_DECLS

extern GPrivate *gst_thread_current;

typedef enum {
  GST_THREAD_STATE_SPINNING	= GST_BIN_FLAG_LAST,
  GST_THREAD_STATE_REAPING,
  /* when iterating with mutex locked (special cases)
     may only be set by thread itself */
  GST_THREAD_MUTEX_LOCKED,

  /* padding */
  GST_THREAD_FLAG_LAST 		= GST_BIN_FLAG_LAST + 4
} GstThreadState;

#define GST_TYPE_THREAD 		(gst_thread_get_type())
#define GST_THREAD(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_THREAD,GstThread))
#define GST_IS_THREAD(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_THREAD))
#define GST_THREAD_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_THREAD,GstThreadClass))
#define GST_IS_THREAD_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_THREAD))
#define GST_THREAD_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_THREAD, GstThreadClass))

typedef struct _GstThread 	GstThread;
typedef struct _GstThreadClass 	GstThreadClass;

struct _GstThread {
  GstBin 	 bin;

  GThread 	*thread_id;		/* id of the thread, if any */
  GThreadPriority priority;

  GMutex 	*lock;			/* thread lock/condititon pairs */
  GCond 	*cond;			/* used to control the thread */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstThreadClass {
  GstBinClass parent_class;

  /* signals */
  void	(*shutdown)	(GstThread *thread);

  gpointer _gst_reserved[GST_PADDING];
};

GType 	gst_thread_get_type	(void);

GstElement*	gst_thread_new		(const gchar *name);

void		gst_thread_set_priority (GstThread *thread, GThreadPriority priority);
GstThread *	gst_thread_get_current	(void);

G_END_DECLS


#endif /* __GST_THREAD_H__ */
