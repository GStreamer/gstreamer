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
#include <gst/gsttask.h>

G_BEGIN_DECLS

#define GST_TYPE_SCHEDULER 		(gst_scheduler_get_type ())
#define GST_SCHEDULER(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_SCHEDULER, GstScheduler))
#define GST_IS_SCHEDULER(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_SCHEDULER))
#define GST_SCHEDULER_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_SCHEDULER,GstSchedulerClass))
#define GST_IS_SCHEDULER_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_SCHEDULER))
#define GST_SCHEDULER_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_SCHEDULER, GstSchedulerClass))

typedef enum {
  /* padding */
  GST_SCHEDULER_FLAG_LAST 		= GST_OBJECT_FLAG_LAST + 4
} GstSchedulerFlags;

#define GST_SCHEDULER_PARENT(sched)		((sched)->parent)
#define GST_SCHEDULER_STATE(sched)		((sched)->state)

/*typedef struct _GstScheduler GstScheduler; */
/*typedef struct _GstSchedulerClass GstSchedulerClass; */

struct _GstScheduler {
  GstObject 		 object;

  GstElement 		*parent;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstSchedulerClass {
  GstObjectClass parent_class;

  /* virtual methods */
  void 			(*setup)		(GstScheduler *sched);
  void 			(*reset)		(GstScheduler *sched);

  GstTask*		(*create_task)		(GstScheduler *sched, GstTaskFunction func, gpointer data);

  gpointer _gst_reserved[GST_PADDING];
};

GType			gst_scheduler_get_type		(void);


void			gst_scheduler_setup		(GstScheduler *sched);
void			gst_scheduler_reset		(GstScheduler *sched);
GstTask*		gst_scheduler_create_task	(GstScheduler *sched, GstTaskFunction func, gpointer data);


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
#define GST_SCHEDULER_DEFAULT_NAME	"thread"

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

gboolean		gst_scheduler_register			(GstPlugin *plugin, const gchar *name, 
								 const gchar *longdesc, GType type);
GstSchedulerFactory*	gst_scheduler_factory_new		(const gchar *name, const gchar *longdesc, GType type);
void                    gst_scheduler_factory_destroy		(GstSchedulerFactory *factory);

GstSchedulerFactory*	gst_scheduler_factory_find		(const gchar *name);

GstScheduler*		gst_scheduler_factory_create		(GstSchedulerFactory *factory, GstElement *parent);
GstScheduler*		gst_scheduler_factory_make		(const gchar *name, GstElement *parent);

void			gst_scheduler_factory_set_default_name	(const gchar* name);
G_CONST_RETURN gchar*	gst_scheduler_factory_get_default_name	(void);


G_END_DECLS

#endif /* __GST_SCHEDULER_H__ */
