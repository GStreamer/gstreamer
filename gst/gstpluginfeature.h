/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstpluginfeature.h: Header for base GstPluginFeature
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


#ifndef __GST_PLUGIN_FEATURE_H__
#define __GST_PLUGIN_FEATURE_H__

#include <glib-object.h>
#include <gst/gsttypes.h>

G_BEGIN_DECLS

#define GST_TYPE_PLUGIN_FEATURE 		(gst_plugin_feature_get_type())
#define GST_PLUGIN_FEATURE(obj) 		(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_PLUGIN_FEATURE, GstPluginFeature))
#define GST_IS_PLUGIN_FEATURE(obj) 		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_PLUGIN_FEATURE))
#define GST_PLUGIN_FEATURE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_PLUGIN_FEATURE, GstPluginFeatureClass))
#define GST_IS_PLUGIN_FEATURE_CLASS(klass) 	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_PLUGIN_FEATURE))
#define GST_PLUGIN_FEATURE_GET_CLASS(obj) 	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_PLUGIN_FEATURE, GstPluginFeatureClass))

#define GST_PLUGIN_FEATURE_NAME(feature)  (GST_PLUGIN_FEATURE (feature)->name)

typedef struct _GstPluginFeature GstPluginFeature;
typedef struct _GstPluginFeatureClass GstPluginFeatureClass;

struct _GstPluginFeature {
  GObject 	 object;

  /*< private >*/
  gchar 	*name;
  guint   	 rank;

  gpointer 	 manager;

  GST_OBJECT_PADDING
};

struct _GstPluginFeatureClass {
  GObjectClass	parent_class;

  void          (*unload_thyself)      (GstPluginFeature *feature);

  GST_CLASS_PADDING
};

typedef struct {
  const gchar 	*name;
  GType		 type;
} GstTypeNameData;

/* filter */
typedef gboolean        (*GstPluginFeatureFilter)       (GstPluginFeature *feature,
                                                         gpointer user_data);

/* normal GObject stuff */
GType		gst_plugin_feature_get_type		(void);

gboolean	gst_plugin_feature_ensure_loaded 	(GstPluginFeature *feature);
void		gst_plugin_feature_unload_thyself 	(GstPluginFeature *feature);

gboolean	gst_plugin_feature_type_name_filter	(GstPluginFeature *feature,
							 GstTypeNameData *data);

void		gst_plugin_feature_set_rank		(GstPluginFeature *feature, guint rank);
void		gst_plugin_feature_set_name		(GstPluginFeature *feature, const gchar *name);
guint		gst_plugin_feature_get_rank		(GstPluginFeature *feature);
G_CONST_RETURN gchar *gst_plugin_feature_get_name	(GstPluginFeature *feature);

G_END_DECLS


#endif /* __GST_PLUGIN_FEATURE_H__ */

