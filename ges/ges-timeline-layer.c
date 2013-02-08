/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
 *               2011 Mathieu Duponchelle <mathieu.duponchelle@epitech.eu>
 *               2013 Thibault Saunier <thibault.saunier@collabora.com>
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

/**
 * SECTION:ges-timeline-layer
 * @short_description: Non-overlapping sequence of GESClip
 *
 * Responsible for the ordering of the various contained Clip(s). A
 * timeline layer has a "priority" property, which is used to manage the
 * priorities of individual Clips. Two layers should not have the
 * same priority within a given timeline.
 */

#include "ges-internal.h"
#include "ges-timeline-layer.h"
#include "ges.h"
#include "ges-source-clip.h"

static void ges_meta_container_interface_init
    (GESMetaContainerInterface * iface);

G_DEFINE_TYPE_WITH_CODE (GESTimelineLayer, ges_timeline_layer,
    G_TYPE_INITIALLY_UNOWNED, G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE, NULL)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER,
        ges_meta_container_interface_init));

struct _GESTimelineLayerPrivate
{
  /*< private > */
  GList *objects_start;         /* The Clips sorted by start and
                                 * priority */

  guint32 priority;             /* The priority of the layer within the
                                 * containing timeline */
  gboolean auto_transition;
};

typedef struct
{
  GESClip *object;
  GESTimelineLayer *layer;
} NewAssetUData;

enum
{
  PROP_0,
  PROP_PRIORITY,
  PROP_AUTO_TRANSITION,
  PROP_LAST
};

enum
{
  OBJECT_ADDED,
  OBJECT_REMOVED,
  LAST_SIGNAL
};

static guint ges_timeline_layer_signals[LAST_SIGNAL] = { 0 };

/* GObject standard vmethods */
static void
ges_timeline_layer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);

  switch (property_id) {
    case PROP_PRIORITY:
      g_value_set_uint (value, layer->priv->priority);
      break;
    case PROP_AUTO_TRANSITION:
      g_value_set_boolean (value, layer->priv->auto_transition);
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
    case PROP_AUTO_TRANSITION:
      ges_timeline_layer_set_auto_transition (layer,
          g_value_get_boolean (value));
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
        (GESClip *) priv->objects_start->data);

  G_OBJECT_CLASS (ges_timeline_layer_parent_class)->dispose (object);
}

static void
ges_meta_container_interface_init (GESMetaContainerInterface * iface)
{

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
   * GESTimelineLayer:priority:
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
   * GESTimelineLayer:auto-transition:
   *
   * Sets whether transitions are added automagically when timeline objects overlap.
   */
  g_object_class_install_property (object_class, PROP_AUTO_TRANSITION,
      g_param_spec_boolean ("auto-transition", "Auto-Transition",
          "whether the transitions are added", FALSE, G_PARAM_READWRITE));

  /**
   * GESTimelineLayer::object-added:
   * @layer: the #GESTimelineLayer
   * @object: the #GESClip that was added.
   *
   * Will be emitted after the object was added to the layer.
   */
  ges_timeline_layer_signals[OBJECT_ADDED] =
      g_signal_new ("object-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass, object_added),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 1, GES_TYPE_CLIP);

  /**
   * GESTimelineLayer::object-removed:
   * @layer: the #GESTimelineLayer
   * @object: the #GESClip that was removed
   *
   * Will be emitted after the object was removed from the layer.
   */
  ges_timeline_layer_signals[OBJECT_REMOVED] =
      g_signal_new ("object-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass,
          object_removed), NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE,
      1, GES_TYPE_CLIP);
}

static void
ges_timeline_layer_init (GESTimelineLayer * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_LAYER, GESTimelineLayerPrivate);

  self->priv->priority = 0;
  self->priv->auto_transition = FALSE;
  self->min_gnl_priority = 0;
  self->max_gnl_priority = LAYER_HEIGHT;
}

/**
 * ges_timeline_layer_resync_priorities:
 * @layer: a #GESTimelineLayer
 *
 * Resyncs the priorities of the objects controlled by @layer.
 * This method
 */
static gboolean
ges_timeline_layer_resync_priorities (GESTimelineLayer * layer)
{
  GList *tmp;
  GESTimelineElement *obj;

  GST_DEBUG ("Resync priorities of %p", layer);

  /* TODO : Inhibit composition updates while doing this.
   * Ideally we want to do it from an even higher level, but here will
   * do in the meantime. */

  for (tmp = layer->priv->objects_start; tmp; tmp = tmp->next) {
    obj = GES_TIMELINE_ELEMENT (tmp->data);
    _set_priority0 (obj, _PRIORITY (obj));
  }

  return TRUE;
}

static void
new_asset_cb (GESAsset * source, GAsyncResult * res, NewAssetUData * udata)
{
  GError *error = NULL;

  GESAsset *asset = ges_asset_request_finish (res, &error);

  GST_DEBUG_OBJECT (udata->layer, "%" GST_PTR_FORMAT " Asset loaded, "
      "setting its asset", udata->object);

  if (error) {
    GESProject *project = udata->layer->timeline ?
        GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE
            (udata->layer->timeline))) : NULL;
    if (project) {
      gchar *possible_id;

      possible_id = ges_project_try_updating_id (project, source, error);
      if (possible_id) {
        ges_asset_request_async (ges_asset_get_extractable_type (source),
            possible_id, NULL, (GAsyncReadyCallback) new_asset_cb, udata);
        g_free (possible_id);
        return;
      }
    }

    GST_ERROR ("Asset could not be created for uri %s, error: %s",
        ges_asset_get_id (asset), error->message);

  } else {
    GESProject *project = udata->layer->timeline ?
        GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE
            (udata->layer->timeline))) : NULL;
    ges_extractable_set_asset (GES_EXTRACTABLE (udata->object), asset);

    ges_project_add_asset (project, asset);
    ges_timeline_layer_add_object (udata->layer, udata->object);
  }

  g_object_unref (asset);
  g_slice_free (NewAssetUData, udata);
}

/* Public methods */
/**
 * ges_timeline_layer_remove_object:
 * @layer: a #GESTimelineLayer
 * @object: the #GESClip to remove
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
ges_timeline_layer_remove_object (GESTimelineLayer * layer, GESClip * object)
{
  GESTimelineLayer *current_layer;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);
  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);

  GST_DEBUG ("layer:%p, object:%p", layer, object);

  current_layer = ges_clip_get_layer (object);
  if (G_UNLIKELY (current_layer != layer)) {
    GST_WARNING ("Clip doesn't belong to this layer");

    if (current_layer != NULL)
      g_object_unref (current_layer);

    return FALSE;
  }
  g_object_unref (current_layer);

  /* emit 'object-removed' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_REMOVED], 0, object);

  /* inform the object it's no longer in a layer */
  ges_clip_set_layer (object, NULL);

  /* Remove it from our list of controlled objects */
  layer->priv->objects_start =
      g_list_remove (layer->priv->objects_start, object);

  /* Remove our reference to the object */
  g_object_unref (object);

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
 * ges_timeline_layer_get_auto_transition:
 * @layer: a #GESTimelineLayer
 *
 * Gets whether transitions are automatically added when objects
 * overlap or not.
 *
 * Returns: %TRUE if transitions are automatically added, else %FALSE.
 */
gboolean
ges_timeline_layer_get_auto_transition (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), 0);

  return layer->priv->auto_transition;
}

/**
 * ges_timeline_layer_set_auto_transition:
 * @layer: a #GESTimelineLayer
 * @auto_transition: whether the auto_transition is active
 *
 * Sets the layer to the given @auto_transition. See the documentation of the
 * property auto_transition for more information.
 */
void
ges_timeline_layer_set_auto_transition (GESTimelineLayer * layer,
    gboolean auto_transition)
{

  g_return_if_fail (GES_IS_TIMELINE_LAYER (layer));

  layer->priv->auto_transition = auto_transition;
  g_object_notify (G_OBJECT (layer), "auto-transition");
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
 * Returns: (transfer full) (element-type GESClip): a #GList of
 * timeline objects. The user is responsible for
 * unreffing the contained objects and freeing the list.
 */

GList *
ges_timeline_layer_get_objects (GESTimelineLayer * layer)
{
  GESTimelineLayerClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), NULL);

  klass = GES_TIMELINE_LAYER_GET_CLASS (layer);

  if (klass->get_objects) {
    return klass->get_objects (layer);
  }

  return g_list_sort (g_list_copy_deep (layer->priv->objects_start,
          (GCopyFunc) gst_object_ref, NULL),
      (GCompareFunc) element_start_compare);
}

/**
 * ges_timeline_layer_is_empty:
 * @layer: The #GESTimelineLayer to check
 *
 * Convenience method to check if @layer is empty (doesn't contain any object),
 * or not.
 *
 * Returns: %TRUE if @layer is empty, %FALSE if it already contains at least
 * one #GESClip
 */
gboolean
ges_timeline_layer_is_empty (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);

  return (layer->priv->objects_start == NULL);
}

/**
 * ges_timeline_layer_add_object:
 * @layer: a #GESTimelineLayer
 * @object: (transfer full): the #GESClip to add.
 *
 * Adds the given object to the layer. Sets the object's parent, and thus
 * takes ownership of the object.
 *
 * An object can only be added to one layer.
 *
 * Calling this method will construct and properly set all the media related
 * elements on @object. If you need to know when those objects (actually #GESTrackElement)
 * are constructed, you should connect to the object::track-element-added signal which
 * is emited right after those elements are ready to be used.
 *
 * Returns: TRUE if the object was properly added to the layer, or FALSE
 * if the @layer refuses to add the object.
 */
gboolean
ges_timeline_layer_add_object (GESTimelineLayer * layer, GESClip * object)
{
  GESAsset *asset;
  GESTimelineLayerPrivate *priv;
  GESTimelineLayer *current_layer;
  guint32 maxprio, minprio, prio;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);
  g_return_val_if_fail (GES_IS_CLIP (object), FALSE);

  GST_DEBUG_OBJECT (layer, "adding object:%p", object);

  priv = layer->priv;
  current_layer = ges_clip_get_layer (object);
  if (G_UNLIKELY (current_layer)) {
    GST_WARNING ("Clip %p already belongs to another layer", object);
    g_object_unref (current_layer);

    return FALSE;
  }

  asset = ges_extractable_get_asset (GES_EXTRACTABLE (object));
  if (asset == NULL) {
    gchar *id;
    NewAssetUData *mudata = g_slice_new (NewAssetUData);

    mudata->object = object;
    mudata->layer = layer;

    GST_DEBUG_OBJECT (layer, "%" GST_PTR_FORMAT " as no reference to any "
        "assets creating a asset... trying sync", object);

    id = ges_extractable_get_id (GES_EXTRACTABLE (object));
    asset = ges_asset_request (G_OBJECT_TYPE (object), id, NULL);
    if (asset == NULL) {
      GESProject *project = layer->timeline ?
          GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE
              (layer->timeline))) : NULL;

      ges_asset_request_async (G_OBJECT_TYPE (object),
          id, NULL, (GAsyncReadyCallback) new_asset_cb, mudata);

      if (project)
        ges_project_add_loading_asset (project, G_OBJECT_TYPE (object), id);
      g_free (id);

      GST_LOG_OBJECT (layer, "Object added async");
      return TRUE;
    }
    g_free (id);

    ges_extractable_set_asset (GES_EXTRACTABLE (object), asset);

    g_slice_free (NewAssetUData, mudata);
  }


  g_object_ref_sink (object);

  /* Take a reference to the object and store it stored by start/priority */
  priv->objects_start = g_list_insert_sorted (priv->objects_start, object,
      (GCompareFunc) element_start_compare);

  /* Inform the object it's now in this layer */
  ges_clip_set_layer (object, layer);

  GST_DEBUG ("current object priority : %d, layer min/max : %d/%d",
      _PRIORITY (object), layer->min_gnl_priority, layer->max_gnl_priority);

  /* Set the priority. */
  maxprio = layer->max_gnl_priority;
  minprio = layer->min_gnl_priority;
  prio = _PRIORITY (object);

  if (minprio + prio > (maxprio)) {
    GST_WARNING_OBJECT (layer,
        "%p is out of the layer space, setting its priority to "
        "%d, setting it to the maximum priority of the layer: %d", object, prio,
        maxprio - minprio);
    _set_priority0 (GES_TIMELINE_ELEMENT (object), LAYER_HEIGHT - 1);
  }

  /* If the object has an acceptable priority, we just let it with its current
   * priority */
  ges_timeline_layer_resync_priorities (layer);

  /* emit 'object-added' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_ADDED], 0, object);

  return TRUE;
}

/**
 * ges_timeline_layer_add_asset:
 * @layer: a #GESTimelineLayer
 * @asset: The asset to add to
 * @start: The start value to set on the new #GESClip
 * @inpoint: The inpoint value to set on the new #GESClip
 * @duration: The duration value to set on the new #GESClip
 * @rate: The rate value to set on the new #GESClip
 * @track_types: The #GESTrackType to set on the the new #GESClip
 *
 * Creates Clip from asset, adds it to layer and
 * returns a reference to it.
 *
 * Returns: (transfer none): Created #GESClip
 */
GESClip *
ges_timeline_layer_add_asset (GESTimelineLayer * layer,
    GESAsset * asset, GstClockTime start, GstClockTime inpoint,
    GstClockTime duration, gdouble rate, GESTrackType track_types)
{
  GESClip *clip;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), NULL);
  g_return_val_if_fail (GES_IS_ASSET (asset), NULL);
  g_return_val_if_fail (g_type_is_a (ges_asset_get_extractable_type
          (asset), GES_TYPE_CLIP), NULL);

  GST_DEBUG_OBJECT (layer, "Adding asset %s with: start: %" GST_TIME_FORMAT
      " inpoint: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT " rate %f"
      " track types: %d (%s)", ges_asset_get_id (asset), GST_TIME_ARGS (start),
      GST_TIME_ARGS (inpoint), GST_TIME_ARGS (duration), rate, track_types,
      ges_track_type_name (track_types));

  clip = GES_CLIP (ges_asset_extract (asset, NULL));
  _set_start0 (GES_TIMELINE_ELEMENT (clip), start);
  _set_inpoint0 (GES_TIMELINE_ELEMENT (clip), inpoint);
  if (track_types != GES_TRACK_TYPE_UNKNOWN)
    ges_clip_set_supported_formats (clip, track_types);

  if (GST_CLOCK_TIME_IS_VALID (duration)) {
    _set_duration0 (GES_TIMELINE_ELEMENT (clip), duration);
  }

  if (!ges_timeline_layer_add_object (layer, clip)) {
    gst_object_unref (clip);

    return NULL;
  }

  return clip;
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

/**
 * ges_timeline_layer_get_timeline:
 * @layer: The #GESTimelineLayer to get the parent #GESTimeline from
 *
 * Get the #GESTimeline in which #GESTimelineLayer currently is.
 *
 * Returns: (transfer none):  the #GESTimeline in which #GESTimelineLayer
 * currently is or %NULL if not in any timeline yet.
 */
GESTimeline *
ges_timeline_layer_get_timeline (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), NULL);

  return layer->timeline;
}

void
ges_timeline_layer_set_timeline (GESTimelineLayer * layer,
    GESTimeline * timeline)
{
  GST_DEBUG ("layer:%p, timeline:%p", layer, timeline);

  layer->timeline = timeline;
}
