/* GStreamer
 * Copyright (C) 2003 Benjamin Otte <in7y118@public.uni-hamburg.de>
 *
 * gsttypefind.h: typefinding subsystem
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


#ifndef __GST_TYPE_FIND_H__
#define __GST_TYPE_FIND_H__

#include <gst/gstbuffer.h>
#include <gst/gstcaps.h>
#include <gst/gstplugin.h>
#include <gst/gstpluginfeature.h>
#include <gst/gsttypes.h>

G_BEGIN_DECLS
#define GST_TYPE_TYPE_FIND_FACTORY                 (gst_type_find_factory_get_type())
#define GST_TYPE_FIND_FACTORY(obj)                 (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_TYPE_FIND_FACTORY, GstTypeFindFactory))
#define GST_IS_TYPE_FIND_FACTORY(obj)              (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_TYPE_FIND_FACTORY))
#define GST_TYPE_FIND_FACTORY_CLASS(klass)         (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_TYPE_FIND_FACTORY, GstTypeFindFactoryClass))
#define GST_IS_TYPE_FIND_FACTORY_CLASS(klass)      (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_TYPE_FIND_FACTORY))
#define GST_TYPE_FIND_FACTORY_GET_CLASS(obj)       (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_TYPE_FIND_FACTORY, GstTypeFindFactoryClass))
typedef struct _GstTypeFind GstTypeFind;
typedef struct _GstTypeFindFactory GstTypeFindFactory;
typedef struct _GstTypeFindFactoryClass GstTypeFindFactoryClass;

typedef void (*GstTypeFindFunction) (GstTypeFind * find, gpointer data);

typedef enum
{
  GST_TYPE_FIND_MINIMUM = 1,
  GST_TYPE_FIND_POSSIBLE = 50,
  GST_TYPE_FIND_LIKELY = 80,
  GST_TYPE_FIND_NEARLY_CERTAIN = 99,
  GST_TYPE_FIND_MAXIMUM = 100,
}
GstTypeFindProbability;

struct _GstTypeFind
{
  /* private to the caller of the typefind function */
  guint8 *(*peek) (gpointer data, gint64 offset, guint size);
  void (*suggest) (gpointer data, guint probability, const GstCaps * caps);

  gpointer data;

  /* optional */
    guint64 (*get_length) (gpointer data);

  /* <private> */
  gpointer _gst_reserved[GST_PADDING];
};

struct _GstTypeFindFactory
{
  GstPluginFeature feature;
  /* <private> */

  GstTypeFindFunction function;
  gchar **extensions;
  GstCaps *caps;		/* FIXME: not yet saved in registry */

  gpointer user_data;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstTypeFindFactoryClass
{
  GstPluginFeatureClass parent;
  /* <private> */

  gpointer _gst_reserved[GST_PADDING];
};

/* typefind function interface */
guint8 *gst_type_find_peek (GstTypeFind * find, gint64 offset, guint size);
void gst_type_find_suggest (GstTypeFind * find,
    guint probability, const GstCaps * caps);
guint64 gst_type_find_get_length (GstTypeFind * find);

/* registration interface */
gboolean gst_type_find_register (GstPlugin * plugin,
    const gchar * name,
    guint rank,
    GstTypeFindFunction func,
    gchar ** extensions, const GstCaps * possible_caps, gpointer data);

/* typefinding interface */

GType gst_type_find_factory_get_type (void);

GList *gst_type_find_factory_get_list (void);

gchar **gst_type_find_factory_get_extensions (const GstTypeFindFactory *
    factory);
const GstCaps *gst_type_find_factory_get_caps (const GstTypeFindFactory *
    factory);
void gst_type_find_factory_call_function (const GstTypeFindFactory * factory,
    GstTypeFind * find);


G_END_DECLS
#endif /* __GST_TYPE_FIND_H__ */
