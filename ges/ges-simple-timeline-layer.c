/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

/**
 * SECTION:ges-simple-timeline-layer
 * @short_description: High-level #GESTimelineLayer
 *
 * #GESSimpleTimelineLayer allows using #GESTimelineObject(s) with a list-like
 * API.
 */

#include "ges-internal.h"
#include "ges-simple-timeline-layer.h"

G_DEFINE_TYPE (GESSimpleTimelineLayer, ges_simple_timeline_layer,
    GES_TYPE_TIMELINE_LAYER);

static void
ges_simple_timeline_layer_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_timeline_layer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_timeline_layer_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_simple_timeline_layer_parent_class)->dispose (object);
}

static void
ges_simple_timeline_layer_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_simple_timeline_layer_parent_class)->finalize (object);
}

static void
ges_simple_timeline_layer_class_init (GESSimpleTimelineLayerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_simple_timeline_layer_get_property;
  object_class->set_property = ges_simple_timeline_layer_set_property;
  object_class->dispose = ges_simple_timeline_layer_dispose;
  object_class->finalize = ges_simple_timeline_layer_finalize;
}

static void
ges_simple_timeline_layer_init (GESSimpleTimelineLayer * self)
{
}

/**
 * ges_simple_timeline_layer_add_object:
 * @layer: a #GESSimpleTimelineLayer
 * @object: the #GESTimelineObject to add
 * @position: the position at which to add the object
 *
 * Adds the object at the given position in the layer. The position is where
 * the object will be inserted. To put the object before all objects, use
 * position 0. To put after all objects, use position -1.
 *
 * The layer will steal a reference to the provided object.
 *
 * NOT IMPLEMENTED !
 *
 * Returns: TRUE if the object was successfuly added, else FALSE.
 */
gboolean
ges_simple_timeline_layer_add_object (GESSimpleTimelineLayer * layer,
    GESTimelineObject * object, gint position)
{
  /* NOT IMPLEMENTED */

  return FALSE;
}

/**
 * ges_simple_timeline_layer_move_object:
 * @layer: a #GESSimpleTimelineLayer
 * @object: the #GESTimelineObject to move
 * @newposition: the new position at which to move the object
 *
 * Moves the object to the given position in the layer. To put the object before
 * all other objects, use position 0. To put the objects after all objects, use
 * position -1.
 *
 * NOT IMPLEMENTED !
 *
 * Returns: TRUE if the object was successfuly moved, else FALSE.
 */

gboolean
ges_simple_timeline_layer_move_object (GESSimpleTimelineLayer * layer,
    GESTimelineObject * object, gint newposition)
{
  /* NOT IMPLEMENTED */

  return FALSE;
}

/**
 * ges_simple_timeline_layer_new:
 *
 * Creates a new #GESSimpleTimelineLayer.
 *
 * Returns: The new #GESSimpleTimelineLayer
 */
GESSimpleTimelineLayer *
ges_simple_timeline_layer_new (void)
{
  return g_object_new (GES_TYPE_SIMPLE_TIMELINE_LAYER, NULL);
}
