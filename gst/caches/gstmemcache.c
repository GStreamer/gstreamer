/* GStreamer
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

#include <gst/gst_private.h>
#include <gst/gstversion.h>
#include <gst/gstplugin.h>
#include <gst/gstcache.h>

#define GST_TYPE_MEM_CACHE		\
  (gst_cache_get_type ())
#define GST_MEM_CACHE(obj)		\
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_MEM_CACHE, GstMemCache))
#define GST_MEM_CACHE_CLASS(klass)	\
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_MEM_CACHE, GstMemCacheClass))
#define GST_IS_MEM_CACHE(obj)		\
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_MEM_CACHE))
#define GST_IS_MEM_CACHE_CLASS(obj)	\
  (GST_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_MEM_CACHE))

typedef struct _GstMemCache GstMemCache;
typedef struct _GstMemCacheClass GstMemCacheClass;

struct _GstMemCache {
  GstCache		 parent;
};

struct _GstMemCacheClass {
  GstCacheClass parent_class;
};

/* Cache signals and args */
enum {
  LAST_SIGNAL
};

enum {
  ARG_0,
  /* FILL ME */
};

static void		gst_mem_cache_class_init	(GstMemCacheClass *klass);
static void		gst_mem_cache_init		(GstMemCache *cache);

#define CLASS(mem_cache)  GST_MEM_CACHE_CLASS (G_OBJECT_GET_CLASS (mem_cache))

static GstCache *parent_class = NULL;
/*static guint gst_mem_cache_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mem_cache_get_type(void) {
  static GType mem_cache_type = 0;

  if (!mem_cache_type) {
    static const GTypeInfo mem_cache_info = {
      sizeof(GstMemCacheClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_mem_cache_class_init,
      NULL,
      NULL,
      sizeof(GstMemCache),
      1,
      (GInstanceInitFunc)gst_mem_cache_init,
      NULL
    };
    mem_cache_type = g_type_register_static(GST_TYPE_CACHE, "GstMemCache", &mem_cache_info, 0);
  }
  return mem_cache_type;
}

static void
gst_mem_cache_class_init (GstMemCacheClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_CACHE);
}

static void
gst_mem_cache_init (GstMemCache *cache)
{
  GST_DEBUG(0, "created new mem cache");
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstCacheFactory *factory;

  gst_plugin_set_longname (plugin, "A memory cache");

  factory = gst_cache_factory_new ("memcache",
	                           "A cache that stores entries in memory",
                                   gst_mem_cache_get_type());

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  else {
    g_warning ("could not register memcache");
  }
  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "gstcaches",
  plugin_init
};

