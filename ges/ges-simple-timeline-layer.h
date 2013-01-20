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

#ifndef _GES_SIMPLE_TIMELINE_LAYER
#define _GES_SIMPLE_TIMELINE_LAYER

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-timeline-layer.h>

G_BEGIN_DECLS

#define GES_TYPE_SIMPLE_TIMELINE_LAYER ges_simple_timeline_layer_get_type()

#define GES_SIMPLE_TIMELINE_LAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_SIMPLE_TIMELINE_LAYER, GESSimpleTimelineLayer))

#define GES_SIMPLE_TIMELINE_LAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_SIMPLE_TIMELINE_LAYER, GESSimpleTimelineLayerClass))

#define GES_IS_SIMPLE_TIMELINE_LAYER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_SIMPLE_TIMELINE_LAYER))

#define GES_IS_SIMPLE_TIMELINE_LAYER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_SIMPLE_TIMELINE_LAYER))

#define GES_SIMPLE_TIMELINE_LAYER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_SIMPLE_TIMELINE_LAYER, GESSimpleTimelineLayerClass))

typedef struct _GESSimpleTimelineLayerPrivate GESSimpleTimelineLayerPrivate;

/**
 * GESSimpleTimelineLayer:
 */

struct _GESSimpleTimelineLayer {
  /*< private >*/
  GESTimelineLayer parent;

  GESSimpleTimelineLayerPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESSimpleTimelineLayerClass {
  /*< private >*/
  GESTimelineLayerClass parent_class;

  /*< signals >*/
  void (*object_moved) (GESTimelineLayer * layer, GESClip * object,
      gint old_position, gint new_position);

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_simple_timeline_layer_get_type (void);

GESSimpleTimelineLayer* ges_simple_timeline_layer_new (void);

gboolean
ges_simple_timeline_layer_add_object (GESSimpleTimelineLayer *layer,
				      GESClip *object, gint position);

gboolean
ges_simple_timeline_layer_move_object (GESSimpleTimelineLayer *layer,
				       GESClip *object, gint newposition);

gboolean
ges_simple_timeline_layer_is_valid (GESSimpleTimelineLayer *layer);

GESClip *
ges_simple_timeline_layer_nth (GESSimpleTimelineLayer *layer,
			       gint position);

gint
ges_simple_timeline_layer_index (GESSimpleTimelineLayer *layer,
				 GESClip *object);

G_END_DECLS

#endif /* _GES_SIMPLE_TIMELINE_LAYER */

