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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-timeline-layer
 * @short_description: Non-overlaping sequence of #GESTimelineObject
 *
 * Responsible for the ordering of the various contained TimelineObject(s). A
 * timeline layer has a "priority" property, which is used to manage the
 * priorities of individual TimelineObjects. Two layers should not have the
 * same priority within a given timeline.
 */

#include "ges-internal.h"
#include "gesmarshal.h"
#include "ges-timeline-layer.h"
#include "ges.h"

#define LAYER_HEIGHT 10

G_DEFINE_TYPE (GESTimelineLayer, ges_timeline_layer, G_TYPE_INITIALLY_UNOWNED);

struct _GESTimelineLayerPrivate
{
  /*< private > */
  GSList *objects_start;        /* The TimelineObjects sorted by start and
                                 * priority */

  guint32 priority;             /* The priority of the layer within the 
                                 * containing timeline */
};

enum
{
  PROP_0,
  PROP_PRIORITY,
};

enum
{
  OBJECT_ADDED,
  OBJECT_REMOVED,
  LAST_SIGNAL
};

static guint ges_timeline_layer_signals[LAST_SIGNAL] = { 0 };

static gboolean ges_timeline_layer_resync_priorities (GESTimelineLayer * layer);

static void
ges_timeline_layer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);

  switch (property_id) {
    case PROP_PRIORITY:
      g_value_set_uint (value, layer->priv->priority);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_layer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);

  switch (property_id) {
    case PROP_PRIORITY:
      ges_timeline_layer_set_priority (layer, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_layer_dispose (GObject * object)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);
  GESTimelineLayerPrivate *priv = layer->priv;

  GST_DEBUG ("Disposing layer");

  while (priv->objects_start)
    ges_timeline_layer_remove_object (layer,
        (GESTimelineObject *) priv->objects_start->data);

  G_OBJECT_CLASS (ges_timeline_layer_parent_class)->dispose (object);
}

static void
ges_timeline_layer_class_init (GESTimelineLayerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineLayerPrivate));

  object_class->get_property = ges_timeline_layer_get_property;
  object_class->set_property = ges_timeline_layer_set_property;
  object_class->dispose = ges_timeline_layer_dispose;

  /**
   * GESTimelineLayer:priority
   *
   * The priority of the layer in the #GESTimeline. 0 is the highest
   * priority. Conceptually, a #GESTimeline is a stack of GESTimelineLayers,
   * and the priority of the layer represents its position in the stack. Two
   * layers should not have the same priority within a given GESTimeline.
   */
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_uint ("priority", "Priority",
          "The priority of the layer", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

  /**
   * GESTimelineLayer::object-added
   * @layer: the #GESTimelineLayer
   * @object: the #GESTimelineObject that was added.
   *
   * Will be emitted after the object was added to the layer.
   */
  ges_timeline_layer_signals[OBJECT_ADDED] =
      g_signal_new ("object-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass, object_added),
      NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_OBJECT);

  /**
   * GESTimelineLayer::object-removed
   * @layer: the #GESTimelineLayer
   * @object: the #GESTimelineObject that was removed
   *
   * Will be emitted after the object was removed from the layer.
   */
  ges_timeline_layer_signals[OBJECT_REMOVED] =
      g_signal_new ("object-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass,
          object_removed), NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_OBJECT);

}

static void
ges_timeline_layer_init (GESTimelineLayer * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_LAYER, GESTimelineLayerPrivate);

  self->priv->priority = 0;
  self->min_gnl_priority = 0;
  self->max_gnl_priority = 9;
}

/**
 * ges_timeline_layer_new:
 *
 * Creates a new #GESTimelineLayer.
 *
 * Returns: A new #GESTimelineLayer
 */
GESTimelineLayer *
ges_timeline_layer_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_LAYER, NULL);
}

void
ges_timeline_layer_set_timeline (GESTimelineLayer * layer,
    GESTimeline * timeline)
{
  GST_DEBUG ("layer:%p, timeline:%p", layer, timeline);

  layer->timeline = timeline;
}

static gint
objects_start_compare (GESTimelineObject * a, GESTimelineObject * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    return 0;
  }
  if (a->start < b->start)
    return -1;
  if (a->start > b->start)
    return 1;
  return 0;
}

/**
 * ges_timeline_layer_add_object:
 * @layer: a #GESTimelineLayer
 * @object: (transfer full): the #GESTimelineObject to add.
 *
 * Adds the given object to the layer. Sets the object's parent, and thus
 * takes ownership of the object.
 *
 * An object can only be added to one layer.
 *
 * Returns: TRUE if the object was properly added to the layer, or FALSE
 * if the @layer refuses to add the object.
 */

gboolean
ges_timeline_layer_add_object (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GESTimelineLayer *tl_obj_layer;
  guint32 maxprio, minprio, prio;

  GST_DEBUG ("layer:%p, object:%p", layer, object);

  tl_obj_layer = ges_timeline_object_get_layer (object);

  if (G_UNLIKELY (tl_obj_layer)) {
    GST_WARNING ("TimelineObject %p already belongs to another layer", object);
    g_object_unref (tl_obj_layer);
    return FALSE;
  }

  g_object_ref_sink (object);

  /* Take a reference to the object and store it stored by start/priority */
  layer->priv->objects_start =
      g_slist_insert_sorted (layer->priv->objects_start, object,
      (GCompareFunc) objects_start_compare);

  /* Inform the object it's now in this layer */
  ges_timeline_object_set_layer (object, layer);

  GST_DEBUG ("current object priority : %d, layer min/max : %d/%d",
      GES_TIMELINE_OBJECT_PRIORITY (object),
      layer->min_gnl_priority, layer->max_gnl_priority);

  /* Set the priority. */
  maxprio = layer->max_gnl_priority;
  minprio = layer->min_gnl_priority;
  prio = GES_TIMELINE_OBJECT_PRIORITY (object);
  if (minprio + prio > (maxprio)) {
    GST_WARNING ("%p is out of the layer %p space, setting its priority to "
        "setting its priority %d to the maximum priority of the layer %d",
        object, layer, prio, maxprio - minprio);
    ges_timeline_object_set_priority (object, LAYER_HEIGHT - 1);
  }
  /* If the object has an acceptable priority, we just let it with its current
   * priority */

  ges_timeline_layer_resync_priorities (layer);

  /* emit 'object-added' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_ADDED], 0, object);

  return TRUE;
}

/**
 * ges_timeline_layer_remove_object:
 * @layer: a #GESTimelineLayer
 * @object: the #GESTimelineObject to remove
 *
 * Removes the given @object from the @layer and unparents it.
 * Unparenting it means the reference owned by @layer on the @object will be
 * removed. If you wish to use the @object after this function, make sure you
 * call g_object_ref() before removing it from the @layer.
 *
 * Returns: TRUE if the object could be removed, FALSE if the layer does
 * not want to remove the object.
 */
gboolean
ges_timeline_layer_remove_object (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GESTimelineLayer *tl_obj_layer;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);

  GST_DEBUG ("layer:%p, object:%p", layer, object);

  tl_obj_layer = ges_timeline_object_get_layer (object);
  if (G_UNLIKELY (tl_obj_layer != layer)) {
    GST_WARNING ("TimelineObject doesn't belong to this layer");
    if (tl_obj_layer != NULL)
      g_object_unref (tl_obj_layer);
    return FALSE;
  }
  g_object_unref (tl_obj_layer);

  /* emit 'object-removed' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_REMOVED], 0, object);

  /* inform the object it's no longer in a layer */
  ges_timeline_object_set_layer (object, NULL);

  /* Remove it from our list of controlled objects */
  layer->priv->objects_start =
      g_slist_remove (layer->priv->objects_start, object);

  /* Remove our reference to the object */
  g_object_unref (object);

  return TRUE;
}

/**
 * ges_timeline_layer_resync_priorities:
 * @layer: a #GESTimelineLayer
 *
 * Resyncs the priorities of the objects controlled by @layer.
 * This method
 */
gboolean
ges_timeline_layer_resync_priorities (GESTimelineLayer * layer)
{
  GSList *tmp;
  GESTimelineObject *obj;

  /* TODO : Inhibit composition updates while doing this.
   * Ideally we want to do it from an even higher level, but here will
   * do in the meantime. */

  for (tmp = layer->priv->objects_start; tmp; tmp = tmp->next) {
    obj = GES_TIMELINE_OBJECT (tmp->data);
    ges_timeline_object_set_priority (obj, GES_TIMELINE_OBJECT_PRIORITY (obj));
  }

  return TRUE;
}

/**
 * ges_timeline_layer_set_priority:
 * @layer: a #GESTimelineLayer
 * @priority: the priority to set
 *
 * Sets the layer to the given @priority. See the documentation of the
 * priority property for more information.
 */
void
ges_timeline_layer_set_priority (GESTimelineLayer * layer, guint priority)
{
  g_return_if_fail (GES_IS_TIMELINE_LAYER (layer));

  GST_DEBUG ("layer:%p, priority:%d", layer, priority);

  if (priority != layer->priv->priority) {
    layer->priv->priority = priority;
    layer->min_gnl_priority = (priority * LAYER_HEIGHT);
    layer->max_gnl_priority = ((priority + 1) * LAYER_HEIGHT) - 1;

    ges_timeline_layer_resync_priorities (layer);
  }
}

/**
 * ges_timeline_layer_get_priority:
 * @layer: a #GESTimelineLayer
 *
 * Get the priority of @layer within the timeline.
 *
 * Returns: The priority of the @layer within the timeline.
 */
guint
ges_timeline_layer_get_priority (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), 0);

  return layer->priv->priority;
}

/**
 * ges_timeline_layer_get_objects:
 * @layer: a #GESTimelineLayer
 *
 * Get the timeline objects this layer contains.
 *
 * Returns: (transfer full) (element-type GESTimelineObject): a #GList of
 * timeline objects. The user is responsible for
 * unreffing the contained objects and freeing the list.
 */

GList *
ges_timeline_layer_get_objects (GESTimelineLayer * layer)
{
  GList *ret = NULL;
  GSList *tmp;
  GESTimelineLayerClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), NULL);

  klass = GES_TIMELINE_LAYER_GET_CLASS (layer);

  if (klass->get_objects) {
    return klass->get_objects (layer);
  }

  for (tmp = layer->priv->objects_start; tmp; tmp = tmp->next) {
    ret = g_list_prepend (ret, tmp->data);
    g_object_ref (tmp->data);
  }

  ret = g_list_reverse (ret);
  return ret;
}
