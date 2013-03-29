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

/**
 * SECTION:ges-simple-layer
 * @short_description: High-level GESLayer
 *
 * #GESSimpleLayer allows using #GESClip(s) with a list-like
 * API. Clients can add any type of GESClip to a
 * GESSimpleLayer, and the layer will automatically compute the
 * appropriate start times.
 *
 * Users should be aware that GESBaseTransitionClip objects are considered to
 * have a negative duration for the purposes of positioning GESSourceClip
 * objects (i.e., adding a GESBaseTransitionClip creates an overlap between
 * the two adjacent sources.
 */

#include <ges/ges.h>
#include "ges-internal.h"

static void ges_simple_layer_object_removed (GESLayer * layer, GESClip * clip);

static void ges_simple_layer_object_added (GESLayer * layer, GESClip * clip);

static void
clip_height_changed_cb (GESClip * clip G_GNUC_UNUSED,
    GParamSpec * arg G_GNUC_UNUSED, GESSimpleLayer * layer);

static GList *get_objects (GESLayer * layer);

G_DEFINE_TYPE (GESSimpleLayer, ges_simple_layer, GES_TYPE_LAYER);

struct _GESSimpleLayerPrivate
{
  /* Sorted list of objects */
  GList *objects;

  gboolean adding_object;
  gboolean valid;
};

enum
{
  OBJECT_MOVED,
  LAST_SIGNAL,
};

enum
{
  PROP_0,
  PROP_VALID,
  LAST_PROP,
};

static guint gstl_signals[LAST_SIGNAL] = { 0 };

static void
ges_simple_layer_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESSimpleLayer *self;
  self = GES_SIMPLE_LAYER (object);

  switch (property_id) {
    case PROP_VALID:
      g_value_set_boolean (value, self->priv->valid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_layer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_simple_layer_class_init (GESSimpleLayerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESLayerClass *layer_class = GES_LAYER_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESSimpleLayerPrivate));

  object_class->get_property = ges_simple_layer_get_property;
  object_class->set_property = ges_simple_layer_set_property;

  /* Be informed when objects are being added/removed from elsewhere */
  layer_class->object_removed = ges_simple_layer_object_removed;
  layer_class->object_added = ges_simple_layer_object_added;
  layer_class->get_objects = get_objects;

  /**
   * GESSimpleLayer:valid:
   *
   * FALSE when the arrangement of objects in the layer would cause errors or
   * unexpected output during playback. Do not set the containing pipeline
   * state to PLAYING when this property is FALSE.
   */
  g_object_class_install_property (object_class, PROP_VALID,
      g_param_spec_boolean ("valid", "Valid",
          "Layer is in a valid configuration", FALSE, G_PARAM_READABLE));

  /**
   * GESSimpleLayer::object-moved:
   * @layer: the #GESSimpleLayer
   * @object: the #GESClip that was added
   * @old: the previous position of the object
   * @new: the new position of the object
   *
   * Will be emitted when an object is moved with
   * #ges_simple_layer_move_object.
   */
  gstl_signals[OBJECT_MOVED] =
      g_signal_new ("object-moved", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESSimpleLayerClass,
          object_moved),
      NULL, NULL, g_cclosure_marshal_generic, G_TYPE_NONE, 3,
      GES_TYPE_CLIP, G_TYPE_INT, G_TYPE_INT);
}

static void
ges_simple_layer_init (GESSimpleLayer * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_SIMPLE_LAYER, GESSimpleLayerPrivate);

  self->priv->objects = NULL;
}

static void
gstl_recalculate (GESSimpleLayer * self)
{
  GList *tmp;
  gint64 pos = 0;
  gint priority = 0;
  gint transition_priority = 0;
  gint height;
  GESClip *prev_object = NULL;
  GESClip *prev_transition = NULL;
  gboolean valid = TRUE;
  GESSimpleLayerPrivate *priv = self->priv;

  priority = GES_LAYER (self)->min_gnl_priority;

  GST_DEBUG ("recalculating values");

  if (priv->objects && GES_IS_BASE_TRANSITION_CLIP (priv->objects->data)) {
    valid = FALSE;
  }

  for (tmp = priv->objects; tmp; tmp = tmp->next) {
    GESClip *clip;
    guint64 dur;
    GList *l_next;

    clip = (GESClip *) tmp->data;
    dur = _DURATION (clip);
    height = GES_CONTAINER_HEIGHT (clip);

    if (GES_IS_SOURCE_CLIP (clip)) {

      GST_LOG ("%p clip: height: %d: priority %d", clip, height, priority);

      if (G_UNLIKELY (_START (clip) != pos)) {
        _set_start0 (GES_TIMELINE_ELEMENT (clip), pos);
      }

      if (G_UNLIKELY (_PRIORITY (clip) != priority)) {
        _set_priority0 (GES_TIMELINE_ELEMENT (clip), priority);
      }

      transition_priority = MAX (0, priority - 1);
      priority += height;
      pos += dur;

      g_assert (priority != -1);

    } else if (GES_IS_BASE_TRANSITION_CLIP (clip)) {

      pos -= dur;
      if (pos < 0)
        pos = 0;

      GST_LOG ("%p clip: height: %d: trans_priority %d Position: %"
          G_GINT64_FORMAT ", duration %" G_GINT64_FORMAT, clip, height,
          transition_priority, pos, dur);

      g_assert (transition_priority != -1);

      if (G_UNLIKELY (_START (clip) != pos))
        _set_start0 (GES_TIMELINE_ELEMENT (clip), pos);

      if (G_UNLIKELY (_PRIORITY (clip) != transition_priority)) {
        _set_priority0 (GES_TIMELINE_ELEMENT (clip), transition_priority);
      }

      /* sanity checks */
      l_next = g_list_next (tmp);

      if (GES_IS_BASE_TRANSITION_CLIP (prev_object)) {
        GST_ERROR ("two transitions in sequence!");
        valid = FALSE;
      }

      if (prev_object && (_DURATION (prev_object) < dur)) {
        GST_ERROR ("transition duration exceeds that of previous neighbor!");
        valid = FALSE;
      }

      if (l_next && (_DURATION (l_next->data) < dur)) {
        GST_ERROR ("transition duration exceeds that of next neighbor!");
        valid = FALSE;
      }

      if (prev_transition) {
        guint64 start, end;
        end = (_DURATION (prev_transition) + _START (prev_transition));

        start = pos;

        if (end > start) {
          GST_ERROR ("%" G_GUINT64_FORMAT ", %" G_GUINT64_FORMAT ": "
              "overlapping transitions!", start, end);
          valid = FALSE;
        }
      }
      prev_transition = clip;
    }

    prev_object = clip;

  }

  if (prev_object && GES_IS_BASE_TRANSITION_CLIP (prev_object)) {
    valid = FALSE;
  }

  GST_DEBUG ("Finished recalculating: final start pos is: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (pos));

  GES_LAYER (self)->max_gnl_priority = priority;

  if (valid != self->priv->valid) {
    self->priv->valid = valid;
    g_object_notify (G_OBJECT (self), "valid");
  }
}

/**
 * ges_simple_layer_add_object:
 * @layer: a #GESSimpleLayer
 * @object: the #GESClip to add
 * @position: the position at which to add the object
 *
 * Adds the object at the given position in the layer. The position is where
 * the object will be inserted. To put the object before all objects, use
 * position 0. To put after all objects, use position -1.
 *
 * When adding transitions, it is important that the adjacent objects
 * (objects at position, and position + 1) be (1) A derivative of
 * GESSourceClip or other non-transition, and (2) have a duration at least
 * as long as the duration of the transition.
 *
 * The layer will steal a reference to the provided object.
 *
 * Returns: TRUE if the object was successfuly added, else FALSE.
 */
gboolean
ges_simple_layer_add_object (GESSimpleLayer * layer,
    GESClip * clip, gint position)
{
  gboolean res;
  GList *nth;
  GESSimpleLayerPrivate *priv = layer->priv;

  GST_DEBUG ("layer:%p, clip:%p, position:%d", layer, clip, position);

  nth = g_list_nth (priv->objects, position);

  if (GES_IS_BASE_TRANSITION_CLIP (clip)) {
    GList *lprev = g_list_previous (nth);

    GESClip *prev = GES_CLIP (lprev ? lprev->data : NULL);
    GESClip *next = GES_CLIP (nth ? nth->data : NULL);

    if ((prev && GES_IS_BASE_TRANSITION_CLIP (prev)) ||
        (next && GES_IS_BASE_TRANSITION_CLIP (next))) {
      GST_ERROR ("Not adding transition: Only insert transitions between two"
          " sources, or at the begining or end the layer\n");
      return FALSE;
    }
  }


  priv->adding_object = TRUE;

  /* provisionally insert the clip */
  priv->objects = g_list_insert (priv->objects, clip, position);

  res = ges_layer_add_clip ((GESLayer *) layer, clip);

  /* Add to layer */
  if (G_UNLIKELY (!res)) {
    priv->adding_object = FALSE;
    /* we failed to add the clip, so remove it from our list */
    priv->objects = g_list_remove (priv->objects, clip);
    return FALSE;
  }

  priv->adding_object = FALSE;

  GST_DEBUG ("Adding clip %p to the list", clip);


  g_signal_connect (G_OBJECT (clip), "notify::height", G_CALLBACK
      (clip_height_changed_cb), layer);

  /* recalculate positions */
  gstl_recalculate (layer);

  return TRUE;
}

/**
 * ges_simple_layer_nth:
 * @layer: a #GESSimpleLayer
 * @position: The position in position to get, starting from 0.
 *
 * Gets the clip at the given position.
 *
 * Returns: (transfer none): The #GESClip at the given position or NULL if
 * the position is off the end of the layer.
 */

GESClip *
ges_simple_layer_nth (GESSimpleLayer * layer, gint position)
{
  GList *list;
  GESSimpleLayerPrivate *priv = layer->priv;

  list = g_list_nth (priv->objects, position);

  if (list)
    return GES_CLIP (list->data);

  return NULL;
}

/**
 * ges_simple_layer_index:
 * @layer: a #GESSimpleLayer
 * @clip: a #GESClip in the layer
 *
 * Gets the position of the given clip within the given layer.
 *
 * Returns: The position of the clip starting from 0, or -1 if the
 * clip was not found.
 */
gint
ges_simple_layer_index (GESSimpleLayer * layer, GESClip * clip)
{
  GESSimpleLayerPrivate *priv = layer->priv;
  return g_list_index (priv->objects, clip);
}

/**
 * ges_simple_layer_move_object:
 * @layer: a #GESSimpleLayer
 * @clip: the #GESClip to move
 * @newposition: the new position at which to move the clip
 *
 * Moves the clip to the given position in the layer. To put the clip before
 * all other objects, use position 0. To put the objects after all objects, use
 * position -1.
 *
 * Returns: TRUE if the clip was successfuly moved, else FALSE.
 */
gboolean
ges_simple_layer_move_object (GESSimpleLayer * layer,
    GESClip * clip, gint newposition)
{
  gint idx;
  GESSimpleLayerPrivate *priv = layer->priv;
  GESLayer *clip_layer;

  GST_DEBUG ("layer:%p, clip:%p, newposition:%d", layer, clip, newposition);

  clip_layer = ges_clip_get_layer (clip);
  if (G_UNLIKELY (clip_layer != (GESLayer *) layer)) {
    GST_WARNING ("Clip doesn't belong to this layer");
    if (clip_layer != NULL)
      gst_object_unref (clip_layer);
    return FALSE;
  }
  if (clip_layer != NULL)
    gst_object_unref (clip_layer);

  /* Find it's current position */
  idx = g_list_index (priv->objects, clip);
  if (G_UNLIKELY (idx == -1)) {
    GST_WARNING ("Clip not controlled by this layer");
    return FALSE;
  }

  GST_DEBUG ("Object was previously at position %d", idx);

  /* If we don't have to change its position, don't */
  if (idx == newposition)
    return TRUE;

  /* pop it off the list */
  priv->objects = g_list_remove (priv->objects, clip);

  /* re-add it at the proper position */
  priv->objects = g_list_insert (priv->objects, clip, newposition);

  /* recalculate positions */
  gstl_recalculate (layer);

  g_signal_emit (layer, gstl_signals[OBJECT_MOVED], 0, clip, idx, newposition);

  return TRUE;
}

/**
 * ges_simple_layer_new:
 *
 * Creates a new #GESSimpleLayer.
 *
 * Returns: The new #GESSimpleLayer
 */
GESSimpleLayer *
ges_simple_layer_new (void)
{
  return g_object_new (GES_TYPE_SIMPLE_LAYER, NULL);
}


/**
 * ges_simple_layer_is_valid:
 * @layer: a #GESSimpleLayer
 *
 * Checks whether the arrangement of objects in the layer would cause errors
 * or unexpected output during playback. Do not set the containing pipeline
 * state to PLAYING when this property is FALSE.
 *
 * Returns: #TRUE if current arrangement of the layer is valid else #FALSE.
 */
gboolean
ges_simple_layer_is_valid (GESSimpleLayer * layer)
{
  return layer->priv->valid;
}

static void
ges_simple_layer_object_removed (GESLayer * layer, GESClip * clip)
{
  GESSimpleLayer *sl = (GESSimpleLayer *) layer;

  /* remove clip from our list */
  sl->priv->objects = g_list_remove (sl->priv->objects, clip);
  gstl_recalculate (sl);
}

static void
ges_simple_layer_object_added (GESLayer * layer, GESClip * clip)
{
  GESSimpleLayer *sl = (GESSimpleLayer *) layer;

  if (sl->priv->adding_object == FALSE) {
    /* remove clip from our list */
    sl->priv->objects = g_list_append (sl->priv->objects, clip);
    gstl_recalculate (sl);
  }
  g_signal_connect_swapped (clip, "notify::duration",
      G_CALLBACK (gstl_recalculate), layer);
}

static void
clip_height_changed_cb (GESClip * clip,
    GParamSpec * arg G_GNUC_UNUSED, GESSimpleLayer * layer)
{
  GST_LOG ("layer %p: notify height changed %p", layer, clip);
  gstl_recalculate (layer);
}

static GList *
get_objects (GESLayer * l)
{
  GList *ret;
  GList *tmp;
  GESSimpleLayer *layer = (GESSimpleLayer *) l;

  ret = g_list_copy (layer->priv->objects);

  for (tmp = ret; tmp; tmp = tmp->next) {
    gst_object_ref (tmp->data);
  }

  return ret;
}
