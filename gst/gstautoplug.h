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

#ifndef GST_DISABLE_AUTOPLUG

#include <gst/gstelement.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define GST_TYPE_AUTOPLUG \
  (gst_autoplug_get_type())
#define GST_AUTOPLUG(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUTOPLUG,GstAutoplug))
#define GST_AUTOPLUG_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUG,GstAutoplugClass))
#define GST_IS_AUTOPLUG(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUTOPLUG))
#define GST_IS_AUTOPLUG_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUG))

typedef struct _GstAutoplug GstAutoplug;
typedef struct _GstAutoplugClass GstAutoplugClass;

typedef enum {
  GST_AUTOPLUG_TO_CAPS 		= GST_OBJECT_FLAG_LAST,
  GST_AUTOPLUG_TO_RENDERER,

  GST_AUTOPLUG_FLAG_LAST	= GST_OBJECT_FLAG_LAST + 8,
} GstAutoplugFlags;
	

struct _GstAutoplug {
  GstObject object;
};

struct _GstAutoplugClass {
  GstObjectClass parent_class;

  /* signal callbacks */
  void (*new_object)  (GstAutoplug *autoplug, GstObject *object);

  /* perform the autoplugging */
  GstElement* (*autoplug_to_caps) (GstAutoplug *autoplug, GstCaps *srccaps, GstCaps *sinkcaps, va_list args);
  GstElement* (*autoplug_to_renderers) (GstAutoplug *autoplug, GstCaps *srccaps, GstElement *target, va_list args);
};


GType			gst_autoplug_get_type			(void);

void			gst_autoplug_signal_new_object		(GstAutoplug *autoplug, GstObject *object);

GstElement*		gst_autoplug_to_caps			(GstAutoplug *autoplug, GstCaps *srccaps, GstCaps *sinkcaps, ...);
GstElement*		gst_autoplug_to_renderers		(GstAutoplug *autoplug, GstCaps *srccaps, 
								 GstElement *target, ...);


/*
 * creating autopluggers
 *
 */
#define GST_TYPE_AUTOPLUG_FACTORY \
  (gst_autoplug_factory_get_type())
#define GST_AUTOPLUG_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AUTOPLUG_FACTORY,GstAutoplugFactory))
#define GST_AUTOPLUG_FACTORY_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AUTOPLUG_FACTORY,GstAutoplugFactoryClass))
#define GST_IS_AUTOPLUG_FACTORY(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AUTOPLUG_FACTORY))
#define GST_IS_AUTOPLUG_FACTORY_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AUTOPLUG_FACTORY))

typedef struct _GstAutoplugFactory GstAutoplugFactory;
typedef struct _GstAutoplugFactoryClass GstAutoplugFactoryClass;

struct _GstAutoplugFactory {
  GstPluginFeature feature;

  gchar *longdesc;              /* long description of the autoplugger (well, don't overdo it..) */
  GType type;                 /* unique GType of the autoplugger */
};

struct _GstAutoplugFactoryClass {
  GstPluginFeatureClass parent;
};

GType			gst_autoplug_factory_get_type		(void);

GstAutoplugFactory*	gst_autoplug_factory_new			(const gchar *name, const gchar *longdesc, GType type);
void                    gst_autoplug_factory_destroy		(GstAutoplugFactory *factory);

GstAutoplugFactory*	gst_autoplug_factory_find		(const gchar *name);
GList*			gst_autoplug_factory_get_list		(void);

GstAutoplug*		gst_autoplug_factory_create		(GstAutoplugFactory *factory);
GstAutoplug*		gst_autoplug_factory_make		(const gchar *name);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#else /* GST_DISABLE_AUTOPLUG */

#pragma GCC poison	gst_autoplug_get_type	
#pragma GCC poison	gst_autoplug_signal_new_object	
#pragma GCC poison	gst_autoplug_to_caps
#pragma GCC poison	gst_autoplug_to_renderers

#pragma GCC poison	gst_autoplug_factory_get_type	
#pragma GCC poison	gst_autoplug_factory_new
#pragma GCC poison      gst_autoplug_factory_destroy

#pragma GCC poison	gst_autoplug_factory_find
#pragma GCC poison	gst_autoplug_factory_get_list

#pragma GCC poison	gst_autoplug_factory_create
#pragma GCC poison	gst_autoplug_factory_make

#endif /* GST_DISABLE_AUTOPLUG */

#endif /* __GST_AUTOPLUG_H__ */

