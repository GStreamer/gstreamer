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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <gst/gstobject.h>

#define GST_TYPE_PLUGIN_FEATURE \
  (gst_plugin_feature_get_type())
#define GST_PLUGIN_FEATURE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_PLUGIN_FEATURE,GstPluginFeature))
#define GST_PLUGIN_FEATURE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_PLUGIN_FEATURE,GstPluginFeatureClass))
#define GST_IS_PLUGIN_FEATURE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_PLUGIN_FEATURE))
#define GST_IS_PLUGIN_FEATURE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_PLUGIN_FEATURE))

typedef struct _GstPluginFeature GstPluginFeature;
typedef struct _GstPluginFeatureClass GstPluginFeatureClass;

struct _GstPluginFeature {
  GstObject object;

  gpointer manager;
};

struct _GstPluginFeatureClass {
  GstObjectClass	parent_class;

  void          (*unload_thyself)      (GstPluginFeature *feature);
};


/* normal GObject stuff */
GType		gst_plugin_feature_get_type		(void);

void		gst_plugin_feature_ensure_loaded 	(GstPluginFeature *feature);
void		gst_plugin_feature_unload_thyself 	(GstPluginFeature *feature);

#ifdef __cplusplus
}
#endif /* __cplusplus */


#endif /* __GST_PLUGIN_FEATURE_H__ */

