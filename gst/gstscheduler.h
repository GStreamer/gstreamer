/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstschedulerr.h: Header for default schedulerr code
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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


#define GST_TYPE_SCHEDULER \
  (gst_scheduler_get_type())
#define GST_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_SCHEDULER,GstScheduler))
#define GST_SCHEDULER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_SCHEDULER,GstSchedulerClass))
#define GST_IS_SCHEDULER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_SCHEDULER))
#define GST_IS_SCHEDULER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_SCHEDULER))


#define GST_SCHEDULER_PARENT(sched)		((sched)->parent)
#define GST_SCHEDULER_STATE(sched)		((sched)->state)

/*typedef struct _GstScheduler GstScheduler; */
/*typedef struct _GstSchedulerClass GstSchedulerClass; */
typedef enum {
  GST_SCHEDULER_STATE_NONE,
  GST_SCHEDULER_STATE_RUNNING,
  GST_SCHEDULER_STATE_STOPPED,
  GST_SCHEDULER_STATE_ERROR,
} GstSchedulerState;

struct _GstScheduler {
  GstObject object;

  GstElement *parent;

  GstSchedulerState state;
};

struct _GstSchedulerClass {
  GstObjectClass parent_class;

  /* virtual methods */
  void 			(*setup)		(GstScheduler *sched);
  void 			(*reset)		(GstScheduler *sched);
  void 			(*add_element)		(GstScheduler *sched, GstElement *element);
  void 			(*remove_element)	(GstScheduler *sched, GstElement *element);
  GstElementStateReturn (*state_transition)	(GstScheduler *sched, GstElement *element, gint transition);
  void 			(*lock_element)		(GstScheduler *sched, GstElement *element);
  void 			(*unlock_element)	(GstScheduler *sched, GstElement *element);
  void 			(*yield)		(GstScheduler *sched, GstElement *element);
  gboolean		(*interrupt)		(GstScheduler *sched, GstElement *element);
  void 			(*error)		(GstScheduler *sched, GstElement *element);
  void 			(*pad_connect)		(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
  void 			(*pad_disconnect)	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
  void 			(*pad_select)		(GstScheduler *sched, GList *padlist);
  GstSchedulerState 	(*iterate)		(GstScheduler *sched);
  /* for debugging */
  void 			(*show)			(GstScheduler *sched);

  /* signals go here */
};

GType			gst_scheduler_get_type		(void);

#define         	gst_scheduler_destroy(sched)	gst_object_destroy(GST_OBJECT(sched))

void			gst_scheduler_setup		(GstScheduler *sched);
void			gst_scheduler_reset		(GstScheduler *sched);
void			gst_scheduler_add_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_remove_element	(GstScheduler *sched, GstElement *element);
GstElementStateReturn	gst_scheduler_state_transition	(GstScheduler *sched, GstElement *element, gint transition);
void			gst_scheduler_lock_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_unlock_element	(GstScheduler *sched, GstElement *element);
void			gst_scheduler_yield		(GstScheduler *sched, GstElement *element);
gboolean		gst_scheduler_interrupt		(GstScheduler *sched, GstElement *element);
void			gst_scheduler_error		(GstScheduler *sched, GstElement *element);
void			gst_scheduler_pad_connect	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
void			gst_scheduler_pad_disconnect	(GstScheduler *sched, GstPad *srcpad, GstPad *sinkpad);
GstPad*                 gst_scheduler_pad_select 	(GstScheduler *sched, GList *padlist);
gboolean		gst_scheduler_iterate		(GstScheduler *sched);

void			gst_scheduler_show		(GstScheduler *sched);

/*
 * creating schedulers
 *
 */
#define GST_TYPE_SCHEDULERFACTORY \
  (gst_schedulerfactory_get_type ())
#define GST_SCHEDULERFACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SCHEDULERFACTORY, GstSchedulerFactory))
#define GST_SCHEDULERFACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SCHEDULERFACTORY, GstSchedulerFactoryClass))
#define GST_IS_SCHEDULERFACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SCHEDULERFACTORY))
#define GST_IS_SCHEDULERFACTORY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SCHEDULERFACTORY))

typedef struct _GstSchedulerFactory GstSchedulerFactory;
typedef struct _GstSchedulerFactoryClass GstSchedulerFactoryClass;

struct _GstSchedulerFactory {
  GstPluginFeature feature;

  gchar *longdesc;              /* long description of the scheduler (well, don't overdo it..) */
  GType type;                 	/* unique GType of the scheduler */
};

struct _GstSchedulerFactoryClass {
  GstPluginFeatureClass parent;
};

GType			gst_schedulerfactory_get_type		(void);

GstSchedulerFactory*	gst_schedulerfactory_new		(const gchar *name, const gchar *longdesc, GType type);
void                    gst_schedulerfactory_destroy		(GstSchedulerFactory *factory);

GstSchedulerFactory*	gst_schedulerfactory_find		(const gchar *name);
GList*			gst_schedulerfactory_get_list		(void);

GstScheduler*		gst_schedulerfactory_create		(GstSchedulerFactory *factory, GstElement *parent);
GstScheduler*		gst_schedulerfactory_make		(const gchar *name, GstElement *parent);

void			gst_schedulerfactory_set_default_name	(const gchar* name);
const gchar*		gst_schedulerfactory_get_default_name	(void);


#ifdef __cplusplus   
}
#endif /* __cplusplus */

#endif /* __GST_SCHEDULER_H__ */
