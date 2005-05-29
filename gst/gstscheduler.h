/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstscheduler.h: Header for default scheduler code
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


#ifndef __GST_SCHEDULER_H__
#define __GST_SCHEDULER_H__

#include <glib.h>
#include <gst/gstelement.h>
#include <gst/gstdata.h>

G_BEGIN_DECLS

#define GST_TYPE_SCHEDULER 		(gst_scheduler_get_type ())
#define GST_SCHEDULER(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SCHEDULER, GstScheduler))
#define GST_IS_SCHEDULER(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SCHEDULER))
#define GST_SCHEDULER_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SCHEDULER,GstSchedulerClass))
#define GST_IS_SCHEDULER_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SCHEDULER))
#define GST_SCHEDULER_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SCHEDULER, GstSchedulerClass))

typedef enum {
  /* this scheduler works with a fixed clock */
  GST_SCHEDULER_FLAG_FIXED_CLOCK	= GST_OBJECT_FLAG_LAST,

  /* padding */
  GST_SCHEDULER_FLAG_LAST 		= GST_OBJECT_FLAG_LAST + 4
} GstSchedulerFlags;

typedef (*GstMarshalFunc) (gpointer data);
struct _GstScheduler {
  GstObject 		object;

  GstClock		*clock;
  GstClock		*current_clock;

  GList			*clock_providers;
  GList			*clock_receivers;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstSchedulerClass {
  GstObjectClass parent_class;

  /* virtual methods */
  /* required */
  void			(*marshal)		(GstScheduler *sched, GstMarshalFunc func, gpointer data);
  void			(*add_action)		(GstScheduler *sched, GstAction *action);
  void			(*remove_action)	(GstScheduler *sched, GstAction *action);
  /* FIXME: can/want this optional, too? */
  void			(*pad_push)		(GstScheduler *sched, GstRealPad *pad, GstData *data);
  /* optional */
  void			(*update_values)	(GstScheduler *sched, GstAction *action);
  void			(*toggle_active)	(GstScheduler *sched, GstAction *action);
  void 			(*add_element)		(GstScheduler *sched, GstElement *element);
  void 			(*remove_element)	(GstScheduler *sched, GstElement *element);
  GstElementStateReturn (*state_transition)	(GstScheduler *sched, GstElement *element, gint transition);
  void 			(*error)		(GstScheduler *sched, GstElement *element);
  /* FIXME: make this GstRealPad */
  void 			(*pad_link)		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
  void 			(*pad_unlink)		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
  /* for debugging */
  void 			(*show)			(GstScheduler *sched);

  gpointer _gst_reserved[GST_PADDING];
};

GType			gst_scheduler_get_type		(void);

void			gst_scheduler_add_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_marshal		(GstScheduler *sched, GstMarshalFunc func, gpointer data);
/* FIXME: make private? */
GstElementStateReturn	gst_scheduler_state_transition	(GstScheduler *sched, GstElement *element, gint transition);
void			gst_scheduler_error		(GstScheduler *sched, GstElement *element);
void			gst_scheduler_pad_link		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
void			gst_scheduler_pad_unlink	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
void			gst_scheduler_pad_push		(GstScheduler *sched, GstRealPad *pad, GstData *data);

void			gst_scheduler_use_clock		(GstScheduler *sched, GstClock *clock);
void			gst_scheduler_set_clock		(GstScheduler *sched, GstClock *clock);
GstClock*		gst_scheduler_get_clock		(GstScheduler *sched);
void			gst_scheduler_auto_clock	(GstScheduler *sched);

void			gst_scheduler_show		(GstScheduler *sched);


G_END_DECLS

#endif /* __GST_SCHEDULER_H__ */
