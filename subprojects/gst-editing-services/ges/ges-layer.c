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
 * SECTION:geslayer
 * @title: GESLayer
 * @short_description: Non-overlapping sequence of #GESClip
 *
 * #GESLayer-s are responsible for collecting and ordering #GESClip-s.
 *
 * A layer within a timeline will have an associated priority,
 * corresponding to their index within the timeline. A layer with the
 * index/priority 0 will have the highest priority and the layer with the
 * largest index will have the lowest priority (the order of priorities,
 * in this sense, is the _reverse_ of the numerical ordering of the
 * indices). ges_timeline_move_layer() should be used if you wish to
 * change how layers are prioritised in a timeline.
 *
 * Layers with higher priorities will have their content priorities
 * over content from lower priority layers, similar to how layers are
 * used in image editing. For example, if two separate layers both
 * display video content, then the layer with the higher priority will
 * have its images shown first. The other layer will only have its image
 * shown if the higher priority layer has no content at the given
 * playtime, or is transparent in some way. Audio content in separate
 * layers will simply play in addition.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-layer.h"
#include "ges.h"
#include "ges-source-clip.h"

static void ges_meta_container_interface_init
    (GESMetaContainerInterface * iface);

struct _GESLayerPrivate
{
  /*< private > */
  GList *clips_start;           /* The Clips sorted by start and
                                 * priority */

  guint32 priority;             /* The priority of the layer within the
                                 * containing timeline */
  gboolean auto_transition;

  GHashTable *tracks_activness;
};

typedef struct
{
  GESClip *clip;
  GESLayer *layer;
} NewAssetUData;

typedef struct
{
  GESTrack *track;
  GESLayer *layer;
  gboolean active;
  gboolean track_disposed;
} LayerActivnessData;

static void
_track_disposed_cb (LayerActivnessData * data, GObject * disposed_track)
{
  data->track_disposed = TRUE;
  g_hash_table_remove (data->layer->priv->tracks_activness, data->track);
}

static void
layer_activness_data_free (LayerActivnessData * data)
{
  if (!data->track_disposed)
    g_object_weak_unref ((GObject *) data->track,
        (GWeakNotify) _track_disposed_cb, data);
  g_free (data);
}

static LayerActivnessData *
layer_activness_data_new (GESTrack * track, GESLayer * layer, gboolean active)
{
  LayerActivnessData *data = g_new0 (LayerActivnessData, 1);

  data->layer = layer;
  data->track = track;
  data->active = active;
  g_object_weak_ref (G_OBJECT (track), (GWeakNotify) _track_disposed_cb, data);

  return data;
}

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
  ACTIVE_CHANGED,
  LAST_SIGNAL
};

static guint ges_layer_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE_WITH_CODE (GESLayer, ges_layer,
    G_TYPE_INITIALLY_UNOWNED, G_ADD_PRIVATE (GESLayer)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE, NULL)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER,
        ges_meta_container_interface_init));

/* GObject standard vmethods */
static void
ges_layer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESLayer *layer = GES_LAYER (object);

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
ges_layer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESLayer *layer = GES_LAYER (object);

  switch (property_id) {
    case PROP_PRIORITY:
      GST_FIXME ("Deprecated, use ges_timeline_move_layer instead");
      layer_set_priority (layer, g_value_get_uint (value), FALSE);
      break;
    case PROP_AUTO_TRANSITION:
      ges_layer_set_auto_transition (layer, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_layer_dispose (GObject * object)
{
  GESLayer *layer = GES_LAYER (object);
  GESLayerPrivate *priv = layer->priv;

  GST_DEBUG ("Disposing layer");

  while (priv->clips_start)
    ges_layer_remove_clip (layer, (GESClip *) priv->clips_start->data);

  g_clear_pointer (&layer->priv->tracks_activness, g_hash_table_unref);

  G_OBJECT_CLASS (ges_layer_parent_class)->dispose (object);
}

static gboolean
_register_metas (GESLayer * layer)
{
  ges_meta_container_register_meta_float (GES_META_CONTAINER (layer),
      GES_META_READ_WRITE, GES_META_VOLUME, 1.0);

  return TRUE;
}

static void
ges_meta_container_interface_init (GESMetaContainerInterface * iface)
{

}

static void
ges_layer_class_init (GESLayerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_layer_get_property;
  object_class->set_property = ges_layer_set_property;
  object_class->dispose = ges_layer_dispose;

  /**
   * GESLayer:priority:
   *
   * The priority of the layer in the #GESTimeline. 0 is the highest
   * priority. Conceptually, a timeline is a stack of layers,
   * and the priority of the layer represents its position in the stack. Two
   * layers should not have the same priority within a given GESTimeline.
   *
   * Note that the timeline needs to be committed (with #ges_timeline_commit)
   * for the change to be taken into account.
   *
   * Deprecated:1.16.0: use #ges_timeline_move_layer instead. This deprecation means
   * that you will not need to handle layer priorities at all yourself, GES
   * will make sure there is never 'gaps' between layer priorities.
   */
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_uint ("priority", "Priority",
          "The priority of the layer", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

  /**
   * GESLayer:auto-transition:
   *
   * Whether to automatically create a #GESTransitionClip whenever two
   * #GESSource-s that both belong to a #GESClip in the layer overlap.
   * See #GESTimeline for what counts as an overlap.
   *
   * When a layer is added to a #GESTimeline, if this property is left as
   * %FALSE, but the timeline's #GESTimeline:auto-transition is %TRUE, it
   * will be set to %TRUE as well.
   */
  g_object_class_install_property (object_class, PROP_AUTO_TRANSITION,
      g_param_spec_boolean ("auto-transition", "Auto-Transition",
          "whether the transitions are added", FALSE, G_PARAM_READWRITE));

  /**
   * GESLayer::clip-added:
   * @layer: The #GESLayer
   * @clip: The clip that was added
   *
   * Will be emitted after the clip is added to the layer.
   */
  ges_layer_signals[OBJECT_ADDED] =
      g_signal_new ("clip-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESLayerClass, object_added),
      NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_CLIP);

  /**
   * GESLayer::clip-removed:
   * @layer: The #GESLayer
   * @clip: The clip that was removed
   *
   * Will be emitted after the clip is removed from the layer.
   */
  ges_layer_signals[OBJECT_REMOVED] =
      g_signal_new ("clip-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESLayerClass,
          object_removed), NULL, NULL, NULL, G_TYPE_NONE, 1, GES_TYPE_CLIP);

  /**
   * GESLayer::active-changed:
   * @layer: The #GESLayer
   * @active: Whether @layer has been made active or de-active in the @tracks
   * @tracks: (element-type GESTrack) (transfer none): A list of #GESTrack
   * which have been activated or deactivated
   *
   * Will be emitted whenever the layer is activated or deactivated
   * for some #GESTrack. See ges_layer_set_active_for_tracks().
   *
   * Since: 1.18
   */
  ges_layer_signals[ACTIVE_CHANGED] =
      g_signal_new ("active-changed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      G_TYPE_BOOLEAN, G_TYPE_PTR_ARRAY);
}

static void
ges_layer_init (GESLayer * self)
{
  self->priv = ges_layer_get_instance_private (self);

  self->priv->priority = 0;
  self->priv->auto_transition = FALSE;
  self->min_nle_priority = MIN_NLE_PRIO;
  self->max_nle_priority = LAYER_HEIGHT + MIN_NLE_PRIO;

  self->priv->tracks_activness =
      g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) layer_activness_data_free);

  _register_metas (self);
}

static gint
ges_layer_resync_priorities_by_type (GESLayer * layer,
    gint starting_priority, GType type)
{
  GstClockTime next_reset = 0;
  gint priority = starting_priority, max_priority = priority;
  GList *tmp;
  GESTimelineElement *element;

  layer->priv->clips_start =
      g_list_sort (layer->priv->clips_start,
      (GCompareFunc) element_start_compare);
  for (tmp = layer->priv->clips_start; tmp; tmp = tmp->next) {

    element = GES_TIMELINE_ELEMENT (tmp->data);

    if (GES_IS_TRANSITION_CLIP (element)) {
      /* Blindly set transitions priorities to 0 */
      _set_priority0 (element, 0);
      continue;
    } else if (!g_type_is_a (G_OBJECT_TYPE (element), type))
      continue;

    if (element->start > next_reset) {
      priority = starting_priority;
      next_reset = 0;
    }

    if (element->start + element->duration > next_reset)
      next_reset = element->start + element->duration;

    _set_priority0 (element, priority);
    priority = priority + GES_CONTAINER_HEIGHT (element);

    if (priority > max_priority)
      max_priority = priority;
  }

  return max_priority;
}

/**
 * ges_layer_resync_priorities:
 * @layer: The #GESLayer
 *
 * Resyncs the priorities of the clips controlled by @layer.
 */
gboolean
ges_layer_resync_priorities (GESLayer * layer)
{
  gint min_source_prios;

  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);

  GST_INFO_OBJECT (layer, "Resync priorities (prio: %d)",
      layer->priv->priority);

  min_source_prios = ges_layer_resync_priorities_by_type (layer, 1,
      GES_TYPE_OPERATION_CLIP);

  ges_layer_resync_priorities_by_type (layer, min_source_prios,
      GES_TYPE_SOURCE_CLIP);

  return TRUE;
}

void
layer_set_priority (GESLayer * layer, guint priority, gboolean emit)
{
  GST_DEBUG ("layer:%p, priority:%d", layer, priority);

  if (priority != layer->priv->priority) {
    layer->priv->priority = priority;
    layer->min_nle_priority = (priority * LAYER_HEIGHT) + MIN_NLE_PRIO;
    layer->max_nle_priority = ((priority + 1) * LAYER_HEIGHT) + MIN_NLE_PRIO;

    ges_layer_resync_priorities (layer);
  }

  if (emit)
    g_object_notify (G_OBJECT (layer), "priority");
}

static void
new_asset_cb (GESAsset * source, GAsyncResult * res, NewAssetUData * udata)
{
  GError *error = NULL;

  GESAsset *asset = ges_asset_request_finish (res, &error);

  GST_DEBUG_OBJECT (udata->layer, "%" GST_PTR_FORMAT " Asset loaded, "
      "setting its asset", udata->clip);

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
    ges_extractable_set_asset (GES_EXTRACTABLE (udata->clip), asset);

    ges_project_add_asset (project, asset);

    /* clip was already ref-sinked when creating udata,
     * gst_layer_add_clip() creates a new ref as such and
     * below we unref the ref from udata */
    ges_layer_add_clip (udata->layer, udata->clip);
  }

  gst_object_unref (asset);
  gst_object_unref (udata->clip);
  g_free (udata);
}

/**
 * ges_layer_get_duration:
 * @layer: The layer to get the duration from
 *
 * Retrieves the duration of the layer, which is the difference
 * between the start of the layer (always time 0) and the end (which will
 * be the end time of the final clip).
 *
 * Returns: The duration of @layer.
 */
GstClockTime
ges_layer_get_duration (GESLayer * layer)
{
  GList *tmp;
  GstClockTime duration = 0;

  g_return_val_if_fail (GES_IS_LAYER (layer), 0);

  for (tmp = layer->priv->clips_start; tmp; tmp = tmp->next) {
    duration = MAX (duration, _END (tmp->data));
  }

  return duration;
}

static gboolean
ges_layer_remove_clip_internal (GESLayer * layer, GESClip * clip,
    gboolean emit_removed)
{
  GESLayer *current_layer;
  GList *tmp;
  GESTimeline *timeline = layer->timeline;

  GST_DEBUG ("layer:%p, clip:%p", layer, clip);

  current_layer = ges_clip_get_layer (clip);
  if (G_UNLIKELY (current_layer != layer)) {
    GST_WARNING ("Clip doesn't belong to this layer");

    if (current_layer != NULL)
      gst_object_unref (current_layer);

    return FALSE;
  }
  gst_object_unref (current_layer);

  /* Remove it from our list of controlled objects */
  layer->priv->clips_start = g_list_remove (layer->priv->clips_start, clip);

  if (emit_removed) {
    /* emit 'clip-removed' */
    g_signal_emit (layer, ges_layer_signals[OBJECT_REMOVED], 0, clip);
  }

  /* inform the clip it's no longer in a layer */
  ges_clip_set_layer (clip, NULL);
  /* so neither in a timeline */
  if (timeline)
    ges_timeline_remove_clip (timeline, clip);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next)
    ges_track_element_set_layer_active (tmp->data, TRUE);

  /* Remove our reference to the clip */
  gst_object_unref (clip);

  return TRUE;
}

/* Public methods */
/**
 * ges_layer_remove_clip:
 * @layer: The #GESLayer
 * @clip: The clip to remove
 *
 * Removes the given clip from the layer.
 *
 * Returns: %TRUE if @clip was removed from @layer, or %FALSE if the
 * operation failed.
 */
gboolean
ges_layer_remove_clip (GESLayer * layer, GESClip * clip)
{
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);
  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);

  return ges_layer_remove_clip_internal (layer, clip, TRUE);
}

/**
 * ges_layer_set_priority:
 * @layer: The #GESLayer
 * @priority: The priority to set
 *
 * Sets the layer to the given priority. See #GESLayer:priority.
 *
 * Deprecated:1.16.0: use #ges_timeline_move_layer instead. This deprecation means
 * that you will not need to handle layer priorities at all yourself, GES
 * will make sure there is never 'gaps' between layer priorities.
 */
void
ges_layer_set_priority (GESLayer * layer, guint priority)
{
  g_return_if_fail (GES_IS_LAYER (layer));

  GST_FIXME ("Deprecated, use ges_timeline_move_layer instead");

  layer_set_priority (layer, priority, TRUE);
}

/**
 * ges_layer_get_auto_transition:
 * @layer: The #GESLayer
 *
 * Gets the #GESLayer:auto-transition of the layer.
 *
 * Returns: %TRUE if transitions are automatically added to @layer.
 */
gboolean
ges_layer_get_auto_transition (GESLayer * layer)
{
  g_return_val_if_fail (GES_IS_LAYER (layer), 0);

  return layer->priv->auto_transition;
}

/**
 * ges_layer_set_auto_transition:
 * @layer: The #GESLayer
 * @auto_transition: Whether transitions should be automatically added to
 * the layer
 *
 * Sets #GESLayer:auto-transition for the layer. Use
 * ges_timeline_set_auto_transition() if you want all layers within a
 * #GESTimeline to have #GESLayer:auto-transition set to %TRUE. Use this
 * method if you want different values for different layers (and make sure
 * to keep #GESTimeline:auto-transition as %FALSE for the corresponding
 * timeline).
 */
void
ges_layer_set_auto_transition (GESLayer * layer, gboolean auto_transition)
{

  g_return_if_fail (GES_IS_LAYER (layer));

  if (layer->priv->auto_transition == auto_transition)
    return;

  layer->priv->auto_transition = auto_transition;
  g_object_notify (G_OBJECT (layer), "auto-transition");
}

/**
 * ges_layer_get_priority:
 * @layer: The #GESLayer
 *
 * Get the priority of the layer. When inside a timeline, this is its
 * index in the timeline. See ges_timeline_move_layer().
 *
 * Returns: The priority of @layer within its timeline.
 */
guint
ges_layer_get_priority (GESLayer * layer)
{
  g_return_val_if_fail (GES_IS_LAYER (layer), 0);

  return layer->priv->priority;
}

/**
 * ges_layer_get_clips:
 * @layer: The #GESLayer
 *
 * Get the #GESClip-s contained in this layer.
 *
 * Returns: (transfer full) (element-type GESClip): A list of clips in
 * @layer.
 */

GList *
ges_layer_get_clips (GESLayer * layer)
{
  GESLayerClass *klass;

  g_return_val_if_fail (GES_IS_LAYER (layer), NULL);

  klass = GES_LAYER_GET_CLASS (layer);

  if (klass->get_objects) {
    return klass->get_objects (layer);
  }

  return g_list_sort (g_list_copy_deep (layer->priv->clips_start,
          (GCopyFunc) gst_object_ref, NULL),
      (GCompareFunc) element_start_compare);
}

/**
 * ges_layer_is_empty:
 * @layer: The #GESLayer to check
 *
 * Convenience method to check if the layer is empty (doesn't contain
 * any #GESClip), or not.
 *
 * Returns: %TRUE if @layer is empty, %FALSE if it contains at least
 * one clip.
 */
gboolean
ges_layer_is_empty (GESLayer * layer)
{
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);

  return (layer->priv->clips_start == NULL);
}

/**
 * ges_layer_add_clip_full:
 * @layer: The #GESLayer
 * @clip: (transfer floating): The clip to add
 * @error: (nullable): Return location for an error
 *
 * Adds the given clip to the layer. If the method succeeds, the layer
 * will take ownership of the clip.
 *
 * This method will fail and return %FALSE if @clip already resides in
 * some layer. It can also fail if the additional clip breaks some
 * compositional rules (see #GESTimelineElement).
 *
 * Returns: %TRUE if @clip was properly added to @layer, or %FALSE
 * if @layer refused to add @clip.
 * Since: 1.18
 */
gboolean
ges_layer_add_clip_full (GESLayer * layer, GESClip * clip, GError ** error)
{
  GList *tmp, *prev_children, *new_children;
  GESAsset *asset;
  GESLayerPrivate *priv;
  GESLayer *current_layer;
  GESTimeline *timeline;
  GESContainer *container;
  GError *timeline_error = NULL;

  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);
  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (clip);
  container = GES_CONTAINER (clip);

  GST_DEBUG_OBJECT (layer, "adding clip:%p", clip);
  gst_object_ref_sink (clip);

  priv = layer->priv;
  current_layer = ges_clip_get_layer (clip);
  if (G_UNLIKELY (current_layer)) {
    GST_WARNING_OBJECT (layer, "Clip %" GES_FORMAT " already belongs to "
        "another layer", GES_ARGS (clip));
    gst_object_unref (clip);
    gst_object_unref (current_layer);
    return FALSE;
  }

  if (timeline && timeline != layer->timeline) {
    /* if a clip is not in any layer, its timeline should not be set */
    GST_ERROR_OBJECT (layer, "Clip %" GES_FORMAT " timeline %"
        GST_PTR_FORMAT " does not match that of the layer %"
        GST_PTR_FORMAT, GES_ARGS (clip), timeline, layer->timeline);
    gst_object_unref (clip);
    return FALSE;
  }

  timeline = layer->timeline;

  asset = ges_extractable_get_asset (GES_EXTRACTABLE (clip));
  if (asset == NULL) {
    gchar *id;
    NewAssetUData *mudata = g_new (NewAssetUData, 1);

    mudata->clip = clip;
    mudata->layer = layer;

    GST_DEBUG_OBJECT (layer, "%" GST_PTR_FORMAT " as no reference to any "
        "assets creating a asset... trying sync", clip);

    id = ges_extractable_get_id (GES_EXTRACTABLE (clip));
    asset = ges_asset_request (G_OBJECT_TYPE (clip), id, NULL);
    if (asset == NULL) {
      GESProject *project = layer->timeline ?
          GES_PROJECT (ges_extractable_get_asset (GES_EXTRACTABLE
              (layer->timeline))) : NULL;

      ges_asset_request_async (G_OBJECT_TYPE (clip),
          id, NULL, (GAsyncReadyCallback) new_asset_cb, mudata);

      if (project)
        ges_project_add_loading_asset (project, G_OBJECT_TYPE (clip), id);
      g_free (id);

      GST_LOG_OBJECT (layer, "Object added async");
      return TRUE;
    }
    g_free (id);

    ges_extractable_set_asset (GES_EXTRACTABLE (clip), asset);

    g_free (mudata);
    gst_clear_object (&asset);
  }

  /* Take a reference to the clip and store it stored by start/priority */
  priv->clips_start = g_list_insert_sorted (priv->clips_start, clip,
      (GCompareFunc) element_start_compare);

  /* Inform the clip it's now in this layer */
  ges_clip_set_layer (clip, layer);

  GST_DEBUG ("current clip priority : %d, Height: %d", _PRIORITY (clip),
      LAYER_HEIGHT);

  /* Set the priority. */
  if (_PRIORITY (clip) > LAYER_HEIGHT) {
    GST_WARNING_OBJECT (layer,
        "%p is out of the layer space, setting its priority to "
        "%d, setting it to the maximum priority of the layer: %d", clip,
        _PRIORITY (clip), LAYER_HEIGHT - 1);
    _set_priority0 (GES_TIMELINE_ELEMENT (clip), LAYER_HEIGHT - 1);
  }

  ges_layer_resync_priorities (layer);

  /* FIXME: ideally we would only emit if we are going to return TRUE.
   * However, for backward-compatibility, we ensure the "clip-added"
   * signal is released before the clip's "child-added" signal, which is
   * invoked by ges_timeline_add_clip */
  g_signal_emit (layer, ges_layer_signals[OBJECT_ADDED], 0, clip);

  prev_children = ges_container_get_children (container, FALSE);

  if (timeline && !ges_timeline_add_clip (timeline, clip, &timeline_error)) {
    GST_INFO_OBJECT (layer, "Could not add the clip %" GES_FORMAT
        " to the timeline %" GST_PTR_FORMAT, GES_ARGS (clip), timeline);

    if (timeline_error) {
      if (error) {
        *error = timeline_error;
      } else {
        GST_WARNING_OBJECT (timeline, "Adding the clip %" GES_FORMAT
            " to the timeline failed: %s", GES_ARGS (clip),
            timeline_error->message);
        g_error_free (timeline_error);
      }
    }

    /* remove any track elements that were newly created */
    new_children = ges_container_get_children (container, FALSE);
    for (tmp = new_children; tmp; tmp = tmp->next) {
      if (!g_list_find (prev_children, tmp->data))
        ges_container_remove (container, tmp->data);
    }
    g_list_free_full (prev_children, gst_object_unref);
    g_list_free_full (new_children, gst_object_unref);

    /* FIXME: change emit signal to FALSE once we are able to delay the
     * "clip-added" signal until after ges_timeline_add_clip */
    ges_layer_remove_clip_internal (layer, clip, TRUE);
    return FALSE;
  }

  g_list_free_full (prev_children, gst_object_unref);

  for (tmp = container->children; tmp; tmp = tmp->next) {
    GESTrack *track = ges_track_element_get_track (tmp->data);

    if (track)
      ges_track_element_set_layer_active (tmp->data,
          ges_layer_get_active_for_track (layer, track));
  }

  return TRUE;
}

/**
 * ges_layer_add_clip:
 * @layer: The #GESLayer
 * @clip: (transfer floating): The clip to add
 *
 * See ges_layer_add_clip_full(), which also gives an error.
 *
 * Returns: %TRUE if @clip was properly added to @layer, or %FALSE
 * if @layer refused to add @clip.
 */
gboolean
ges_layer_add_clip (GESLayer * layer, GESClip * clip)
{
  return ges_layer_add_clip_full (layer, clip, NULL);
}

/**
 * ges_layer_add_asset_full:
 * @layer: The #GESLayer
 * @asset: The asset to extract the new clip from
 * @start: The #GESTimelineElement:start value to set on the new clip
 * If `start == #GST_CLOCK_TIME_NONE`, it will be added to the end
 * of @layer, i.e. it will be set to @layer's duration
 * @inpoint: The #GESTimelineElement:in-point value to set on the new
 * clip
 * @duration: The #GESTimelineElement:duration value to set on the new
 * clip
 * @track_types: The #GESClip:supported-formats to set on the the new
 * clip, or #GES_TRACK_TYPE_UNKNOWN to use the default
 * @error: (nullable): Return location for an error
 *
 * Extracts a new clip from an asset and adds it to the layer with
 * the given properties.
 *
 * Returns: (transfer none): The newly created clip.
 * Since: 1.18
 */
GESClip *
ges_layer_add_asset_full (GESLayer * layer,
    GESAsset * asset, GstClockTime start, GstClockTime inpoint,
    GstClockTime duration, GESTrackType track_types, GError ** error)
{
  GESClip *clip;

  g_return_val_if_fail (GES_IS_LAYER (layer), NULL);
  g_return_val_if_fail (GES_IS_ASSET (asset), NULL);
  g_return_val_if_fail (!error || !*error, NULL);
  g_return_val_if_fail (g_type_is_a (ges_asset_get_extractable_type
          (asset), GES_TYPE_CLIP), NULL);

  GST_DEBUG_OBJECT (layer, "Adding asset %s with: start: %" GST_TIME_FORMAT
      " inpoint: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
      " track types: %d (%s)", ges_asset_get_id (asset), GST_TIME_ARGS (start),
      GST_TIME_ARGS (inpoint), GST_TIME_ARGS (duration), track_types,
      ges_track_type_name (track_types));

  clip = GES_CLIP (ges_asset_extract (asset, NULL));

  if (!GST_CLOCK_TIME_IS_VALID (start)) {
    start = ges_layer_get_duration (layer);

    GST_DEBUG_OBJECT (layer,
        "No start specified, setting it to %" GST_TIME_FORMAT,
        GST_TIME_ARGS (start));
  }

  _set_start0 (GES_TIMELINE_ELEMENT (clip), start);
  _set_inpoint0 (GES_TIMELINE_ELEMENT (clip), inpoint);
  if (track_types != GES_TRACK_TYPE_UNKNOWN)
    ges_clip_set_supported_formats (clip, track_types);

  if (GST_CLOCK_TIME_IS_VALID (duration)) {
    _set_duration0 (GES_TIMELINE_ELEMENT (clip), duration);
  }

  if (!ges_layer_add_clip_full (layer, clip, error)) {
    return NULL;
  }

  return clip;
}

/**
 * ges_layer_add_asset:
 * @layer: The #GESLayer
 * @asset: The asset to extract the new clip from
 * @start: The #GESTimelineElement:start value to set on the new clip
 * If `start == #GST_CLOCK_TIME_NONE`, it will be added to the end
 * of @layer, i.e. it will be set to @layer's duration
 * @inpoint: The #GESTimelineElement:in-point value to set on the new
 * clip
 * @duration: The #GESTimelineElement:duration value to set on the new
 * clip
 * @track_types: The #GESClip:supported-formats to set on the the new
 * clip, or #GES_TRACK_TYPE_UNKNOWN to use the default
 *
 * See ges_layer_add_asset_full(), which also gives an error.
 *
 * Returns: (transfer none) (nullable): The newly created clip.
 */
GESClip *
ges_layer_add_asset (GESLayer * layer,
    GESAsset * asset, GstClockTime start, GstClockTime inpoint,
    GstClockTime duration, GESTrackType track_types)
{
  return ges_layer_add_asset_full (layer, asset, start, inpoint, duration,
      track_types, NULL);
}

/**
 * ges_layer_new:
 *
 * Creates a new layer.
 *
 * Returns: (transfer floating): A new layer.
 */
GESLayer *
ges_layer_new (void)
{
  return g_object_new (GES_TYPE_LAYER, NULL);
}

/**
 * ges_layer_get_timeline:
 * @layer: The #GESLayer
 *
 * Gets the timeline that the layer is a part of.
 *
 * Returns: (transfer none) (nullable): The timeline that @layer
 * is currently part of, or %NULL if it is not associated with any
 * timeline.
 */
GESTimeline *
ges_layer_get_timeline (GESLayer * layer)
{
  g_return_val_if_fail (GES_IS_LAYER (layer), NULL);

  return layer->timeline;
}

void
ges_layer_set_timeline (GESLayer * layer, GESTimeline * timeline)
{
  GList *tmp;

  g_return_if_fail (GES_IS_LAYER (layer));

  GST_DEBUG ("layer:%p, timeline:%p", layer, timeline);

  for (tmp = layer->priv->clips_start; tmp; tmp = tmp->next) {
    ges_timeline_element_set_timeline (tmp->data, timeline);
  }

  layer->timeline = timeline;
}

/**
 * ges_layer_get_clips_in_interval:
 * @layer: The #GESLayer
 * @start: Start of the interval
 * @end: End of the interval
 *
 * Gets the clips within the layer that appear between @start and @end.
 *
 * Returns: (transfer full) (element-type GESClip): A list of #GESClip-s
 * that intersect the interval `[start, end)` in @layer.
 */
GList *
ges_layer_get_clips_in_interval (GESLayer * layer, GstClockTime start,
    GstClockTime end)
{
  GList *tmp;
  GList *intersecting_clips = NULL;
  GstClockTime clip_start, clip_end;
  gboolean clip_intersects;

  g_return_val_if_fail (GES_IS_LAYER (layer), NULL);

  layer->priv->clips_start =
      g_list_sort (layer->priv->clips_start,
      (GCompareFunc) element_start_compare);
  for (tmp = layer->priv->clips_start; tmp; tmp = tmp->next) {
    clip_intersects = FALSE;
    clip_start = ges_timeline_element_get_start (tmp->data);
    clip_end = clip_start + ges_timeline_element_get_duration (tmp->data);
    if (start <= clip_start && clip_start < end)
      clip_intersects = TRUE;
    else if (start < clip_end && clip_end <= end)
      clip_intersects = TRUE;
    else if (clip_start < start && clip_end > end)
      clip_intersects = TRUE;

    if (clip_intersects)
      intersecting_clips =
          g_list_insert_sorted (intersecting_clips,
          gst_object_ref (tmp->data), (GCompareFunc) element_start_compare);
  }
  return intersecting_clips;
}

/**
 * ges_layer_get_active_for_track:
 * @layer: The #GESLayer
 * @track: The #GESTrack to check if @layer is currently active for
 *
 * Gets whether the layer is active for the given track. See
 * ges_layer_set_active_for_tracks().
 *
 * Returns: %TRUE if @layer is active for @track, or %FALSE otherwise.
 *
 * Since: 1.18
 */
gboolean
ges_layer_get_active_for_track (GESLayer * layer, GESTrack * track)
{
  LayerActivnessData *d;

  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);
  g_return_val_if_fail (layer->timeline == ges_track_get_timeline (track),
      FALSE);

  d = g_hash_table_lookup (layer->priv->tracks_activness, track);

  return d ? d->active : TRUE;
}

/**
 * ges_layer_set_active_for_tracks:
 * @layer: The #GESLayer
 * @active: Whether elements in @tracks should be active or not
 * @tracks: (transfer none) (element-type GESTrack) (allow-none): The list of
 * tracks @layer should be (de-)active in, or %NULL to include all the tracks
 * in the @layer's timeline
 *
 * Activate or deactivate track elements in @tracks (or in all tracks if @tracks
 * is %NULL).
 *
 * When a layer is deactivated for a track, all the #GESTrackElement-s in
 * the track that belong to a #GESClip in the layer will no longer be
 * active in the track, regardless of their individual
 * #GESTrackElement:active value.
 *
 * Note that by default a layer will be active for all of its
 * timeline's tracks.
 *
 * Returns: %TRUE if the operation worked %FALSE otherwise.
 *
 * Since: 1.18
 */
gboolean
ges_layer_set_active_for_tracks (GESLayer * layer, gboolean active,
    GList * tracks)
{
  GList *tmp, *owned_tracks = NULL;
  GPtrArray *changed_tracks = NULL;

  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);

  if (!tracks && layer->timeline)
    owned_tracks = tracks = ges_timeline_get_tracks (layer->timeline);

  for (tmp = tracks; tmp; tmp = tmp->next) {
    GESTrack *track = tmp->data;

    /* Handle setting timeline later */
    g_return_val_if_fail (layer->timeline == ges_track_get_timeline (track),
        FALSE);

    if (ges_layer_get_active_for_track (layer, track) != active) {
      if (changed_tracks == NULL)
        changed_tracks = g_ptr_array_new ();
      g_ptr_array_add (changed_tracks, track);
    }
    g_hash_table_insert (layer->priv->tracks_activness, track,
        layer_activness_data_new (track, layer, active));
  }

  if (changed_tracks) {
    g_signal_emit (layer, ges_layer_signals[ACTIVE_CHANGED], 0, active,
        changed_tracks);
    g_ptr_array_unref (changed_tracks);
  }
  g_list_free_full (owned_tracks, gst_object_unref);

  return TRUE;
}
