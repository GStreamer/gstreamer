/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GES_TIMELINE_LAYER
#define _GES_TIMELINE_LAYER

#include <glib-object.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

#define GES_TYPE_TIMELINE_LAYER ges_timeline_layer_get_type()

#define GES_TIMELINE_LAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TIMELINE_LAYER, GESTimelineLayer))

#define GES_TIMELINE_LAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TIMELINE_LAYER, GESTimelineLayerClass))

#define GES_IS_TIMELINE_LAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TIMELINE_LAYER))

#define GES_IS_TIMELINE_LAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TIMELINE_LAYER))

#define GES_TIMELINE_LAYER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TIMELINE_LAYER, GESTimelineLayerClass))

typedef struct _GESTimelineLayerPrivate GESTimelineLayerPrivate;

/**
 * GESTimelineLayer:
 * @timeline: the #GESTimeline where this layer is being used.
 */
struct _GESTimelineLayer {
  GInitiallyUnowned parent;

  /*< public >*/

  GESTimeline *timeline;

  /*< protected >*/
  guint32 min_gnl_priority, max_gnl_priority;

  GESTimelineLayerPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTimelineLayerClass:
 * @get_objects: method to get the objects contained in the layer
 *
 * Subclasses can override the @get_objects if they can provide a more
 * efficient way of providing the list of contained #GESClip(s).
 */
struct _GESTimelineLayerClass {
  /*< private >*/
  GInitiallyUnownedClass parent_class;

  /*< public >*/
  /* virtual methods for subclasses */
  GList *(*get_objects) (GESTimelineLayer * layer);

  /*< private >*/
  /* Signals */
  void	(*object_added)		(GESTimelineLayer * layer, GESClip * object);
  void	(*object_removed)	(GESTimelineLayer * layer, GESClip * object);

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_timeline_layer_get_type (void);

GESTimelineLayer* ges_timeline_layer_new (void);

void     ges_timeline_layer_set_timeline  (GESTimelineLayer * layer,
					   GESTimeline * timeline);

GESTimeline *
ges_timeline_layer_get_timeline           (GESTimelineLayer * layer);

gboolean ges_timeline_layer_add_object    (GESTimelineLayer * layer,

					   GESClip * object);
GESClip * ges_timeline_layer_add_asset   (GESTimelineLayer *layer,
                                                       GESAsset *asset,
                                                       GstClockTime start,
                                                       GstClockTime inpoint,
                                                       GstClockTime duration,
                                                       gdouble rate,
                                                       GESTrackType track_types);

gboolean ges_timeline_layer_remove_object (GESTimelineLayer * layer,
					   GESClip * object);

void     ges_timeline_layer_set_priority  (GESTimelineLayer * layer,
					   guint priority);

gboolean ges_timeline_layer_is_empty      (GESTimelineLayer * layer);

guint   ges_timeline_layer_get_priority  (GESTimelineLayer * layer);

gboolean ges_timeline_layer_get_auto_transition (GESTimelineLayer * layer);

void ges_timeline_layer_set_auto_transition (GESTimelineLayer * layer,
					     gboolean auto_transition);

GList*   ges_timeline_layer_get_objects   (GESTimelineLayer * layer);

G_END_DECLS

#endif /* _GES_TIMELINE_LAYER */

