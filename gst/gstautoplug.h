/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstautoplug.h: Header for autoplugging functionality
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


#ifndef __GST_AUTOPLUG_H__
#define __GST_AUTOPLUG_H__

#include <gst/gstelement.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_AUTOPLUG \
  (gst_autoplug_get_type())
#define GST_AUTOPLUG(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_AUTOPLUG,GstAutoplug))
#define GST_AUTOPLUG_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUG,GstAutoplugClass))
#define GST_IS_AUTOPLUG(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_AUTOPLUG))
#define GST_IS_AUTOPLUG_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUG))

typedef struct _GstAutoplug GstAutoplug;
typedef struct _GstAutoplugClass GstAutoplugClass;

struct _GstAutoplug {
  GtkObject object;
};

struct _GstAutoplugClass {
  GtkObjectClass parent_class;

  /* signal callbacks */
  void (*new_object)  (GstAutoplug *autoplug, GstObject *object);

  /* perform the autoplugging */
  GstElement* (*autoplug_caps_list) (GstAutoplug *autoplug, GList *srcpad, GList *sinkpad, va_list args);
};

typedef struct _GstAutoplugFactory GstAutoplugFactory;

struct _GstAutoplugFactory {
  gchar *name;                  /* name of autoplugger */
  gchar *longdesc;              /* long description of the autoplugger (well, don't overdo it..) */
  GtkType type;                 /* unique GtkType of the autoplugger */
};

GtkType			gst_autoplug_get_type			(void);

void			gst_autoplug_signal_new_object		(GstAutoplug *autoplug, GstObject *object);

GstElement*		gst_autoplug_caps_list			(GstAutoplug *autoplug, GList *srcpad, GList *sinkpad, ...);


/*
 * creating autopluggers
 *
 */
GstAutoplugFactory*	gst_autoplugfactory_new			(const gchar *name, const gchar *longdesc, GtkType type);
void                    gst_autoplugfactory_destroy		(GstAutoplugFactory *factory);

GstAutoplugFactory*	gst_autoplugfactory_find		(const gchar *name);
GList*			gst_autoplugfactory_get_list		(void);

GstAutoplug*		gst_autoplugfactory_create		(GstAutoplugFactory *factory);
GstAutoplug*		gst_autoplugfactory_make		(const gchar *name);

xmlNodePtr		gst_autoplugfactory_save_thyself	(GstAutoplugFactory *factory, xmlNodePtr parent);
GstAutoplugFactory*	gst_autoplugfactory_load_thyself	(xmlNodePtr parent);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_AUTOPLUG_H__ */

