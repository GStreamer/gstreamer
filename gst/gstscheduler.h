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
#include <gst/gstbin.h>

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
  /* this scheduler supports select and lock calls */
  GST_SCHEDULER_FLAG_NEW_API,

  /* padding */
  GST_SCHEDULER_FLAG_LAST 		= GST_OBJECT_FLAG_LAST + 4
} GstSchedulerFlags;

#define GST_SCHEDULER_PARENT(sched)		((sched)->parent)
#define GST_SCHEDULER_STATE(sched)		((sched)->state)

/*typedef struct _GstScheduler GstScheduler; */
/*typedef struct _GstSchedulerClass GstSchedulerClass; */
typedef enum {
  GST_SCHEDULER_STATE_NONE,
  GST_SCHEDULER_STATE_RUNNING,
  GST_SCHEDULER_STATE_STOPPED,
  GST_SCHEDULER_STATE_ERROR
} GstSchedulerState;

struct _GstScheduler {
  GstObject 		 object;

  GstElement 		*parent;
  GstScheduler 		*parent_sched;

  GstSchedulerState 	 state;
  GstClock		*clock;
  GstClock		*current_clock;

  GList			*clock_providers;
  GList			*clock_receivers;

  GList			*schedulers;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstSchedulerClass {
  GstObjectClass parent_class;

  /* virtual methods */
  void 			(*setup)		(GstScheduler *sched);
  void 			(*reset)		(GstScheduler *sched);
  void 			(*add_element)		(GstScheduler *sched, GstElement *element);
  void 			(*remove_element)	(GstScheduler *sched, GstElement *element);
  void 			(*add_scheduler)	(GstScheduler *sched, GstScheduler *sched2);
  void 			(*remove_scheduler)	(GstScheduler *sched, GstScheduler *sched2);
  GstElementStateReturn (*state_transition)	(GstScheduler *sched, GstElement *element, gint transition);
  void			(*scheduling_change)	(GstScheduler *sched, GstElement *element);
  /* next two are optional, require NEW_API flag */
  /* FIXME 0.9: rename to (un)lock_object */
  void 			(*lock_element)		(GstScheduler *sched, GstObject *object);
  void 			(*unlock_element)	(GstScheduler *sched, GstObject *object);
  gboolean		(*yield)		(GstScheduler *sched, GstElement *element);
  gboolean		(*interrupt)		(GstScheduler *sched, GstElement *element);
  void 			(*error)		(GstScheduler *sched, GstElement *element);
  void 			(*pad_link)		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
  void 			(*pad_unlink)		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
  /* optional, requires NEW_API flag */
  GstData *   		(*pad_select)		(GstScheduler *sched, GstPad **selected, GstPad **pads);
  GstClockReturn	(*clock_wait)		(GstScheduler *sched, GstElement *element,
		  				 GstClockID id, GstClockTimeDiff *jitter);
  GstSchedulerState 	(*iterate)		(GstScheduler *sched);
  /* for debugging */
  void 			(*show)			(GstScheduler *sched);

  /* signals */
  void                  (*object_sync)          (GstScheduler *sched, GstClock *clock, GstObject *object,
			                         GstClockID id);

  gpointer _gst_reserved[GST_PADDING];
};

GType			gst_scheduler_get_type		(void);


void			gst_scheduler_setup		(GstScheduler *sched);
void			gst_scheduler_reset		(GstScheduler *sched);
void			gst_scheduler_add_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_add_scheduler	(GstScheduler *sched, GstScheduler *sched2);
void			gst_scheduler_remove_scheduler	(GstScheduler *sched, GstScheduler *sched2);
GstElementStateReturn	gst_scheduler_state_transition	(GstScheduler *sched, GstElement *element, gint transition);
void			gst_scheduler_scheduling_change	(GstScheduler *sched, GstElement *element);
#ifndef GST_DISABLE_DEPRECATED
void			gst_scheduler_lock_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_unlock_element	(GstScheduler *sched, GstElement *element);
#endif
gboolean		gst_scheduler_yield		(GstScheduler *sched, GstElement *element);
gboolean		gst_scheduler_interrupt		(GstScheduler *sched, GstElement *element);
void			gst_scheduler_error		(GstScheduler *sched, GstElement *element);
void			gst_scheduler_pad_link		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
void			gst_scheduler_pad_unlink	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
#ifndef GST_DISABLE_DEPRECATED
GstPad*                 gst_scheduler_pad_select 	(GstScheduler *sched, GList *padlist);
#endif
GstClockReturn		gst_scheduler_clock_wait	(GstScheduler *sched, GstElement *element,
							 GstClockID id, GstClockTimeDiff *jitter);
gboolean		gst_scheduler_iterate		(GstScheduler *sched);

void			gst_scheduler_use_clock		(GstScheduler *sched, GstClock *clock);
void			gst_scheduler_set_clock		(GstScheduler *sched, GstClock *clock);
GstClock*		gst_scheduler_get_clock		(GstScheduler *sched);
void			gst_scheduler_auto_clock	(GstScheduler *sched);

void			gst_scheduler_show		(GstScheduler *sched);

/*
 * creating schedulers
 *
 */
#define GST_TYPE_SCHEDULER_FACTORY 		(gst_scheduler_factory_get_type ())
#define GST_SCHEDULER_FACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SCHEDULER_FACTORY, GstSchedulerFactory))
#define GST_IS_SCHEDULER_FACTORY(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SCHEDULER_FACTORY))
#define GST_SCHEDULER_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SCHEDULER_FACTORY, GstSchedulerFactoryClass))
#define GST_IS_SCHEDULER_FACTORY_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SCHEDULER_FACTORY))
#define GST_SCHEDULER_FACTORY_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SCHEDULER_FACTORY, GstSchedulerFactoryClass))

/* change this to change the default scheduler */
#define GST_SCHEDULER_DEFAULT_NAME	"opt"

typedef struct _GstSchedulerFactory GstSchedulerFactory;
typedef struct _GstSchedulerFactoryClass GstSchedulerFactoryClass;

struct _GstSchedulerFactory {
  GstPluginFeature feature;

  gchar *longdesc;              /* long description of the scheduler (well, don't overdo it..) */
  GType type;                 	/* unique GType of the scheduler */

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstSchedulerFactoryClass {
  GstPluginFeatureClass parent;

  gpointer _gst_reserved[GST_PADDING];
};

GType			gst_scheduler_factory_get_type		(void);

GstSchedulerFactory*	gst_scheduler_factory_new		(const gchar *name, const gchar *longdesc, GType type);
void                    gst_scheduler_factory_destroy		(GstSchedulerFactory *factory);

GstSchedulerFactory*	gst_scheduler_factory_find		(const gchar *name);

GstScheduler*		gst_scheduler_factory_create		(GstSchedulerFactory *factory, GstElement *parent);
GstScheduler*		gst_scheduler_factory_make		(const gchar *name, GstElement *parent);

void			gst_scheduler_factory_set_default_name	(const gchar* name);
G_CONST_RETURN gchar*	gst_scheduler_factory_get_default_name	(void);


G_END_DECLS

#endif /* __GST_SCHEDULER_H__ */
