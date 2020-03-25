/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
 *               2012 Collabora Ltd.
 *                 Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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
 * SECTION:gesclip
 * @title: GESClip
 * @short_description: Base class for elements that occupy a single
 * #GESLayer and maintain equal timings of their children
 *
 * #GESClip-s are the core objects of a #GESLayer. Each clip may exist in
 * a single layer but may control several #GESTrackElement-s that span
 * several #GESTrack-s. A clip will ensure that all its children share the
 * same #GESTimelineElement:start and #GESTimelineElement:duration in
 * their tracks, which will match the #GESTimelineElement:start and
 * #GESTimelineElement:duration of the clip itself. Therefore, changing
 * the timing of the clip will change the timing of the children, and a
 * change in the timing of a child will change the timing of the clip and
 * subsequently all its siblings. As such, a clip can be treated as a
 * singular object in its layer.
 *
 * For most uses of a #GESTimeline, it is often sufficient to only
 * interact with #GESClip-s directly, which will take care of creating and
 * organising the elements of the timeline's tracks.
 *
 * Most subclassed clips will be associated with some *core*
 * #GESTrackElement-s. When such a clip is added to a layer in a timeline,
 * it will create these children and they will be added to the timeline's
 * tracks. You can connect to the #GESContainer::child-added signal to be
 * notified of their creation. If a clip will produce several core
 * elements of the same #GESTrackElement:track-type but they are destined
 * for separate tracks, you should connect to the timeline's
 * #GESTimeline::select-tracks-for-object signal to coordinate which
 * tracks each element should land in.
 *
 * The #GESTimelineElement:in-point of the clip will control the
 * #GESTimelineElement:in-point of these core elements to be the same
 * value if their #GESTrackElement:has-internal-source is set to %TRUE.
 *
 * The #GESTimelineElement:max-duration of the clip is the minimum
 * #GESTimelineElement:max-duration of its children. If you set its value
 * to anything other than its current value, this will also set the
 * #GESTimelineElement:max-duration of all its core children to the same
 * value if their #GESTrackElement:has-internal-source is set to %TRUE.
 * As a special case, whilst a clip does not yet have any core children,
 * its #GESTimelineElement:max-duration may be set to indicate what its
 * value will be once they are created.
 *
 * ## Effects
 *
 * Some subclasses (#GESSourceClip and #GESBaseEffect) may also allow
 * their objects to have additional non-core #GESBaseEffect-s elements as
 * children. These are additional effects that are applied to the output
 * data of the core elements. They can be added to the clip using
 * ges_container_add(), which will take care of adding the effect to the
 * timeline's tracks. The new effect will be placed between the clip's
 * core track elements and its other effects. As such, the newly added
 * effect will be applied to any source data **before** the other existing
 * effects. You can change the ordering of effects using
 * ges_clip_set_top_effect_index().
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-clip.h"
#include "ges.h"
#include "ges-internal.h"

#include <string.h>

static GList *ges_clip_create_track_elements_func (GESClip * clip,
    GESTrackType type);
static gboolean _ripple (GESTimelineElement * element, GstClockTime start);
static gboolean _ripple_end (GESTimelineElement * element, GstClockTime end);
static gboolean _roll_start (GESTimelineElement * element, GstClockTime start);
static gboolean _roll_end (GESTimelineElement * element, GstClockTime end);
static gboolean _trim (GESTimelineElement * element, GstClockTime start);
static void _compute_height (GESContainer * container);

struct _GESClipPrivate
{
  /*< public > */
  GESLayer *layer;

  /*< private > */
  guint nb_effects;

  GList *copied_track_elements;
  GESLayer *copied_layer;
  gboolean prevent_priority_offset_update;
  gboolean prevent_resort;

  gboolean updating_max_duration;
  gboolean prevent_max_duration_update;
  gboolean setting_inpoint;

  /* The formats supported by this Clip */
  GESTrackType supportedformats;
};

typedef struct _CheckTrack
{
  GESTrack *track;
  GESTrackElement *source;
} CheckTrack;

enum
{
  PROP_0,
  PROP_LAYER,
  PROP_SUPPORTED_FORMATS,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->asset_type = GES_TYPE_CLIP_ASSET;
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESClip, ges_clip,
    GES_TYPE_CONTAINER, G_ADD_PRIVATE (GESClip)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

/****************************************************
 *              Listen to our children              *
 ****************************************************/

#define _IS_CORE_CHILD(child) \
  (ges_track_element_get_creators (GES_TRACK_ELEMENT (child)) != NULL)

#define _IS_CORE_INTERNAL_SOURCE_CHILD(child) \
  (_IS_CORE_CHILD (child) \
   && ges_track_element_has_internal_source (GES_TRACK_ELEMENT (child)))

/* @min_priority: The absolute minimum priority a child of @container should have
 * @max_priority: The absolute maximum priority a child of @container should have
 */
static void
_get_priority_range_full (GESContainer * container, guint32 * min_priority,
    guint32 * max_priority, guint32 priority_base)
{
  GESLayer *layer = GES_CLIP (container)->priv->layer;

  if (layer) {
    *min_priority = priority_base + layer->min_nle_priority;
    *max_priority = layer->max_nle_priority;
  } else {
    *min_priority = priority_base + MIN_NLE_PRIO;
    *max_priority = G_MAXUINT32;
  }
}

static void
_get_priority_range (GESContainer * container, guint32 * min_priority,
    guint32 * max_priority)
{
  _get_priority_range_full (container, min_priority, max_priority,
      _PRIORITY (container));
}

static void
_child_priority_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  /* we do not change the rest of the clip in response to a change in
   * the child priority */
  guint32 min_prio, max_prio;
  GESClipPrivate *priv = GES_CLIP (container)->priv;

  GST_DEBUG_OBJECT (container, "TimelineElement %" GES_FORMAT
      " priority changed to %u", GES_ARGS (child), _PRIORITY (child));

  if (!priv->prevent_priority_offset_update) {
    /* Update mapping */
    _get_priority_range (container, &min_prio, &max_prio);

    _ges_container_set_priority_offset (container, child,
        min_prio - _PRIORITY (child));
  }

  if (!priv->prevent_resort) {
    _ges_container_sort_children (container);
    _compute_height (container);
  }
}

static void
_child_inpoint_changed_cb (GESTimelineElement * child, GParamSpec * pspec,
    GESContainer * container)
{
  if (GES_CLIP (container)->priv->setting_inpoint)
    return;

  /* ignore non-core */
  /* if the track element has no internal content, then this means its
   * in-point has been set (back) to 0, we can ignore this update */
  if (!_IS_CORE_INTERNAL_SOURCE_CHILD (child))
    return;

  /* If the child->inpoint is the same as our own, set_inpoint will do
   * nothing. For example, when we set them in add_child (the notifies for
   * this are released after child_added is called because
   * ges_container_add freezes them) */
  _set_inpoint0 (GES_TIMELINE_ELEMENT (container), child->inpoint);
}

/* called when a child is added, removed or their max-duration changes */
static void
_update_max_duration (GESContainer * container)
{
  GList *tmp;
  GstClockTime min = GST_CLOCK_TIME_NONE;
  GESClipPrivate *priv = GES_CLIP (container)->priv;

  if (priv->prevent_max_duration_update)
    return;

  for (tmp = container->children; tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;
    if (_IS_CORE_CHILD (child)
        && GST_CLOCK_TIME_IS_VALID (child->maxduration))
      min = GST_CLOCK_TIME_IS_VALID (min) ? MIN (min, child->maxduration) :
          child->maxduration;
  }
  priv->updating_max_duration = TRUE;
  ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (container), min);
  priv->updating_max_duration = FALSE;
}

static void
_child_max_duration_changed_cb (GESTimelineElement * child,
    GParamSpec * pspec, GESContainer * container)
{
  /* ignore non-core */
  if (!_IS_CORE_CHILD (child))
    return;

  _update_max_duration (container);
}

static void
_child_has_internal_source_changed_cb (GESTimelineElement * child,
    GParamSpec * pspec, GESContainer * container)
{
  /* ignore non-core */
  /* if the track element is now registered to have no internal content,
   * we don't have to do anything */
  if (!_IS_CORE_INTERNAL_SOURCE_CHILD (child))
    return;

  /* otherwise, we need to make its in-point match ours */
  _set_inpoint0 (child, _INPOINT (container));
}


/*****************************************************
 *                                                   *
 * GESTimelineElement virtual methods implementation *
 *                                                   *
 *****************************************************/

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp, *children;
  GESContainer *container = GES_CONTAINER (element);

  GST_DEBUG_OBJECT (element, "Setting children start, (initiated_move: %"
      GST_PTR_FORMAT ")", container->initiated_move);

  /* get copy of children, since GESContainer may resort the clip */
  children = ges_container_get_children (container, FALSE);
  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    if (child != container->initiated_move)
      _set_start0 (GES_TIMELINE_ELEMENT (child), start);
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;
  g_list_free_full (children, gst_object_unref);

  return TRUE;
}

/* Whether @clip can have its in-point set to @inpoint because none of
 * its children have a max-duration below it */
gboolean
ges_clip_can_set_inpoint_of_child (GESClip * clip, GESTimelineElement * child,
    GstClockTime inpoint)
{
  GList *tmp;

  /* don't bother checking if we are setting the value */
  if (clip->priv->setting_inpoint)
    return TRUE;

  /* non-core children do not effect our in-point */
  if (!_IS_CORE_CHILD (child))
    return TRUE;

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    if (_IS_CORE_INTERNAL_SOURCE_CHILD (child)
        && GST_CLOCK_TIME_IS_VALID (child->maxduration)
        && child->maxduration < inpoint)
      return FALSE;
  }
  return TRUE;
}

/* returns TRUE if we did not break early */
static gboolean
_set_childrens_inpoint (GESTimelineElement * element, GstClockTime inpoint,
    gboolean break_on_failure)
{
  GList *tmp;
  GESClipPrivate *priv = GES_CLIP (element)->priv;

  priv->setting_inpoint = TRUE;
  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    if (_IS_CORE_INTERNAL_SOURCE_CHILD (child)) {
      if (!_set_inpoint0 (child, inpoint)) {
        GST_ERROR_OBJECT ("Could not set the in-point of child %"
            GES_FORMAT " to %" GST_TIME_FORMAT, GES_ARGS (child),
            GST_TIME_ARGS (inpoint));
        if (break_on_failure)
          return FALSE;
      }
    }
  }
  priv->setting_inpoint = FALSE;

  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  if (!_set_childrens_inpoint (element, inpoint, TRUE)) {
    _set_childrens_inpoint (element, element->inpoint, FALSE);
    return FALSE;
  }
  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GList *tmp, *children;

  GESContainer *container = GES_CONTAINER (element);

  /* get copy of children, since GESContainer may resort the clip */
  children = ges_container_get_children (container, FALSE);
  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    if (child != container->initiated_move) {
      ELEMENT_SET_FLAG (child, GES_TIMELINE_ELEMENT_SET_SIMPLE);
      _set_duration0 (GES_TIMELINE_ELEMENT (child), duration);
      ELEMENT_UNSET_FLAG (child, GES_TIMELINE_ELEMENT_SET_SIMPLE);
    }
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;
  g_list_free_full (children, gst_object_unref);

  return TRUE;
}

static gboolean
_set_max_duration (GESTimelineElement * element, GstClockTime maxduration)
{
  GList *tmp;
  GESClipPrivate *priv = GES_CLIP (element)->priv;
  GstClockTime new_min = GST_CLOCK_TIME_NONE;
  gboolean has_core = FALSE;

  /* if we are setting based on a change in the minimum */
  if (priv->updating_max_duration)
    return TRUE;

  /* else, we set every core child to have the same max duration */

  priv->prevent_max_duration_update = TRUE;
  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    if (_IS_CORE_CHILD (child)) {
      has_core = TRUE;
      if (ges_track_element_has_internal_source (GES_TRACK_ELEMENT (child))) {
        if (!ges_timeline_element_set_max_duration (child, maxduration))
          GST_ERROR_OBJECT ("Could not set the max-duration of child %"
              GES_FORMAT " to %" GST_TIME_FORMAT, GES_ARGS (child),
              GST_TIME_ARGS (maxduration));

        if (GST_CLOCK_TIME_IS_VALID (child->maxduration))
          new_min = GST_CLOCK_TIME_IS_VALID (new_min) ?
              MIN (new_min, child->maxduration) : child->maxduration;
      }
    }
  }
  priv->prevent_max_duration_update = FALSE;

  if (!has_core) {
    /* allow max-duration to be set arbitrarily when we have no
     * core children, even though there is no actual minimum max-duration
     * when it has no core children */
    if (GST_CLOCK_TIME_IS_VALID (maxduration))
      GST_INFO_OBJECT (element,
          "Allowing max-duration of the clip to be set to %" GST_TIME_FORMAT
          " because it has no core children", GST_TIME_ARGS (maxduration));
    return TRUE;
  }

  if (new_min != maxduration) {
    if (GST_CLOCK_TIME_IS_VALID (new_min))
      GST_WARNING_OBJECT (element, "Failed to set the max-duration of the "
          "clip to %" GST_TIME_FORMAT " because it was not possible to "
          "match this with the actual minimum of %" GST_TIME_FORMAT,
          GST_TIME_ARGS (maxduration), GST_TIME_ARGS (new_min));
    else
      GST_WARNING_OBJECT (element, "Failed to set the max-duration of the "
          "clip to %" GST_TIME_FORMAT " because it has no core children "
          "whose max-duration could be set to anything other than "
          "GST_CLOCK_TIME_NONE", GST_TIME_ARGS (maxduration));
    priv->updating_max_duration = TRUE;
    ges_timeline_element_set_max_duration (element, new_min);
    priv->updating_max_duration = FALSE;
    return FALSE;
  }

  return TRUE;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GESClipPrivate *priv = GES_CLIP (element)->priv;
  GList *tmp;
  guint32 min_prio, max_prio;

  GESContainer *container = GES_CONTAINER (element);

  /* send the new 'priority' to determine what the new 'min_prio' should
   * be for the clip */
  _get_priority_range_full (container, &min_prio, &max_prio, priority);

  /* offsets will remain constant for the children */
  priv->prevent_resort = TRUE;
  priv->prevent_priority_offset_update = TRUE;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    guint32 track_element_prio;
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;
    gint off = _ges_container_get_priority_offset (container, child);

    if (off >= LAYER_HEIGHT) {
      GST_ERROR ("%s child %s as a priority offset %d >= LAYER_HEIGHT %d"
          " ==> clamping it to 0", GES_TIMELINE_ELEMENT_NAME (element),
          GES_TIMELINE_ELEMENT_NAME (child), off, LAYER_HEIGHT);
      off = 0;
    }

    /* Want the priority offset 'off' of each child to stay the same with
     * the new priority. The offset is calculated from
     *   offset = min_priority - child_priority
     */
    track_element_prio = min_prio - off;

    if (track_element_prio > max_prio) {
      GST_WARNING ("%p priority of %i, is outside of the its containing "
          "layer space. (%d/%d) setting it to the maximum it can be",
          container, priority, min_prio, max_prio);

      track_element_prio = max_prio;
    }
    _set_priority0 (child, track_element_prio);
  }
  /* no need to re-sort the container since we maintained the relative
   * offsets. As such, the height remains the same as well. */
  priv->prevent_resort = FALSE;
  priv->prevent_priority_offset_update = FALSE;

  return TRUE;
}

static guint32
_get_layer_priority (GESTimelineElement * element)
{
  GESClip *clip = GES_CLIP (element);

  if (clip->priv->layer == NULL)
    return GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY;

  return ges_layer_get_priority (clip->priv->layer);
}

static gboolean
_get_natural_framerate (GESTimelineElement * self, gint * framerate_n,
    gint * framerate_d)
{
  GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (self));

  if (!asset) {
    GST_WARNING_OBJECT (self, "No asset set?");

    return FALSE;
  }

  return ges_clip_asset_get_natural_framerate (GES_CLIP_ASSET (asset),
      framerate_n, framerate_d);
}

/****************************************************
 *                                                  *
 *  GESContainer virtual methods implementation     *
 *                                                  *
 ****************************************************/

static void
_compute_height (GESContainer * container)
{
  GList *tmp;
  guint32 min_prio = G_MAXUINT32, max_prio = 0;

  if (container->children == NULL) {
    /* FIXME Why not 0! */
    _ges_container_set_height (container, 1);
    return;
  }

  /* Go over all childs and check if height has changed */
  for (tmp = container->children; tmp; tmp = tmp->next) {
    guint tck_priority = _PRIORITY (tmp->data);

    if (tck_priority < min_prio)
      min_prio = tck_priority;
    if (tck_priority > max_prio)
      max_prio = tck_priority;
  }

  _ges_container_set_height (container, max_prio - min_prio + 1);
}

static gboolean
_add_child (GESContainer * container, GESTimelineElement * element)
{
  GESClipClass *klass = GES_CLIP_GET_CLASS (GES_CLIP (container));
  guint max_prio, min_prio;
  GList *creators;
  GESClipPrivate *priv = GES_CLIP (container)->priv;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (element), FALSE);

  if (element->timeline
      && element->timeline != GES_TIMELINE_ELEMENT_TIMELINE (container)) {
    GST_WARNING_OBJECT (container, "Cannot add %" GES_FORMAT " as a child "
        "because its timeline is %" GST_PTR_FORMAT " rather than the "
        "clip's timeline %" GST_PTR_FORMAT, GES_ARGS (element),
        element->timeline, GES_TIMELINE_ELEMENT_TIMELINE (container));
    return FALSE;
  }

  creators = ges_track_element_get_creators (GES_TRACK_ELEMENT (element));
  if (creators && !g_list_find (creators, container)) {
    GST_WARNING_OBJECT (container,
        "Cannot add the track element %" GES_FORMAT " because it was "
        "created for a different clip %" GES_FORMAT " as its core child",
        GES_ARGS (element), GES_ARGS (creators->data));
    return FALSE;
  }

  /* NOTE: notifies are currently frozen by ges_container_add */
  _get_priority_range (container, &min_prio, &max_prio);
  if (creators) {
    /* NOTE: Core track elements that are base effects are added like any
     * other core clip. In particular, they are *not* added to the list of
     * added effects, so we don not increase nb_effects. */

    /* Set the core element to have the same in-point, which we don't
     * apply to effects */
    if (ges_track_element_has_internal_source (GES_TRACK_ELEMENT (element))) {
      /* adding can fail if the max-duration of the element is smaller
       * than the current in-point of the clip */
      if (!_set_inpoint0 (element, _INPOINT (container))) {
        GST_ERROR_OBJECT (element, "Could not set the in-point of the "
            "element %" GES_FORMAT " to %" GST_TIME_FORMAT ". Not adding "
            "as a child", GES_ARGS (element),
            GST_TIME_ARGS (_INPOINT (container)));
        return FALSE;
      }
    }

    /* Always add at the same priority, on top of existing effects */
    _set_priority0 (element, min_prio + priv->nb_effects);
  } else if (GES_CLIP_CLASS_CAN_ADD_EFFECTS (klass) &&
      GES_IS_BASE_EFFECT (element)) {
    GList *tmp;
    /* Add the effect at the lowest priority among effects (just after
     * the core elements). Need to shift the core elements up by 1
     * to make room. */
    GST_DEBUG_OBJECT (container, "Adding %ith effect: %" GES_FORMAT
        " Priority %i", priv->nb_effects + 1, GES_ARGS (element),
        min_prio + priv->nb_effects);

    /* changing priorities, and updating their offset */
    priv->prevent_resort = TRUE;
    tmp = g_list_nth (GES_CONTAINER_CHILDREN (container), priv->nb_effects);
    for (; tmp; tmp = tmp->next)
      ges_timeline_element_set_priority (GES_TIMELINE_ELEMENT (tmp->data),
          GES_TIMELINE_ELEMENT_PRIORITY (tmp->data) + 1);

    _set_priority0 (element, min_prio + priv->nb_effects);
    priv->nb_effects++;
    priv->prevent_resort = FALSE;
    /* no need to call _ges_container_sort_children (container) since
     * there is no change to the ordering yet (this happens after the
     * child is actually added) */
    /* The height has already changed (increased by 1) */
    _compute_height (container);
  } else {
    if (GES_IS_BASE_EFFECT (element))
      GST_WARNING_OBJECT (container, "Cannot add the effect %" GES_FORMAT
          " because it is not a core element created by the clip itself "
          "and the %s class does not allow for adding extra effects",
          GES_ARGS (element), G_OBJECT_CLASS_NAME (klass));
    else if (GES_CLIP_CLASS_CAN_ADD_EFFECTS (klass))
      GST_WARNING_OBJECT (container, "Cannot add the track element %"
          GES_FORMAT " because it is neither a core element created by "
          "the clip itself, nor a GESBaseEffect", GES_ARGS (element));
    else
      GST_WARNING_OBJECT (container, "Cannot add the track element %"
          GES_FORMAT " because it is not a core element created by the "
          "clip itself", GES_ARGS (element));
    return FALSE;
  }

  _set_start0 (element, GES_TIMELINE_ELEMENT_START (container));
  _set_duration0 (element, GES_TIMELINE_ELEMENT_DURATION (container));

  return TRUE;
}

static gboolean
_remove_child (GESContainer * container, GESTimelineElement * element)
{
  GESClipPrivate *priv = GES_CLIP (container)->priv;

  /* NOTE: notifies are currently frozen by ges_container_add */
  if (!_IS_CORE_CHILD (element) && GES_IS_BASE_EFFECT (element)) {
    GList *tmp;
    GST_DEBUG_OBJECT (container, "Resyncing effects priority.");

    /* changing priorities, so preventing a re-sort */
    priv->prevent_resort = TRUE;
    for (tmp = GES_CONTAINER_CHILDREN (container); tmp; tmp = tmp->next) {
      guint32 sibling_prio = GES_TIMELINE_ELEMENT_PRIORITY (tmp->data);
      if (sibling_prio > element->priority)
        ges_timeline_element_set_priority (GES_TIMELINE_ELEMENT (tmp->data),
            sibling_prio - 1);
    }
    priv->nb_effects--;
    priv->prevent_resort = FALSE;
    /* no need to re-sort the children since the rest keep the same
     * relative priorities */
    /* height may have changed */
    _compute_height (container);
  }
  /* Creator is not reset so that the child can be readded to @container
   * but not to any other clip by the end user */
  return TRUE;
}

static void
_child_added (GESContainer * container, GESTimelineElement * element)
{
  g_signal_connect (G_OBJECT (element), "notify::priority",
      G_CALLBACK (_child_priority_changed_cb), container);
  g_signal_connect (G_OBJECT (element), "notify::in-point",
      G_CALLBACK (_child_inpoint_changed_cb), container);
  g_signal_connect (G_OBJECT (element), "notify::max-duration",
      G_CALLBACK (_child_max_duration_changed_cb), container);
  g_signal_connect (G_OBJECT (element), "notify::has-internal-source",
      G_CALLBACK (_child_has_internal_source_changed_cb), container);

  _child_priority_changed_cb (element, NULL, container);

  if (_IS_CORE_CHILD (element))
    _update_max_duration (container);
}

static void
_child_removed (GESContainer * container, GESTimelineElement * element)
{
  g_signal_handlers_disconnect_by_func (element, _child_priority_changed_cb,
      container);
  g_signal_handlers_disconnect_by_func (element, _child_inpoint_changed_cb,
      container);
  g_signal_handlers_disconnect_by_func (element,
      _child_max_duration_changed_cb, container);
  g_signal_handlers_disconnect_by_func (element,
      _child_has_internal_source_changed_cb, container);

  if (_IS_CORE_CHILD (element))
    _update_max_duration (container);
}

static void
add_clip_to_list (gpointer key, gpointer clip, GList ** list)
{
  *list = g_list_prepend (*list, gst_object_ref (clip));
}

static void
_transfer_child (GESClip * from_clip, GESClip * to_clip,
    GESTrackElement * child)
{
  /* We need to bump the refcount to avoid the object to be destroyed */
  gst_object_ref (child);
  ges_container_remove (GES_CONTAINER (from_clip),
      GES_TIMELINE_ELEMENT (child));

  if (_IS_CORE_CHILD (child)) {
    ges_track_element_clear_creators (child);
    ges_track_element_add_creator (child, to_clip);
  }

  ges_container_add (GES_CONTAINER (to_clip), GES_TIMELINE_ELEMENT (child));
  gst_object_unref (child);
}

/* make each clip's child share creators to allow their children
 * to be moved between the clips */
static void
_share_creators (GList * clips)
{
  GList *clip1, *clip2, *child;

  for (clip1 = clips; clip1; clip1 = clip1->next) {
    for (child = GES_CONTAINER_CHILDREN (clip1->data); child;
        child = child->next) {
      if (!_IS_CORE_CHILD (child->data))
        continue;
      for (clip2 = clips; clip2; clip2 = clip2->next)
        ges_track_element_add_creator (child->data, clip2->data);
    }
  }
}

static GList *
_ungroup (GESContainer * container, gboolean recursive)
{
  GESClip *tmpclip;
  GESTrackType track_type;
  GESTrackElement *track_element;

  gboolean first_obj = TRUE;
  GList *tmp, *children, *ret = NULL;
  GESClip *clip = GES_CLIP (container);
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (container);
  GESLayer *layer = clip->priv->layer;
  GHashTable *_tracktype_clip = g_hash_table_new (g_int_hash, g_int_equal);

  /* If there is no TrackElement, just return @container in a list */
  if (GES_CONTAINER_CHILDREN (container) == NULL) {
    GST_DEBUG ("No TrackElement, simply returning");
    return g_list_prepend (ret, container);
  }

  /* We need a copy of the current list of tracks */
  children = ges_container_get_children (container, FALSE);
  for (tmp = children; tmp; tmp = tmp->next) {
    track_element = GES_TRACK_ELEMENT (tmp->data);
    track_type = ges_track_element_get_track_type (track_element);

    tmpclip = g_hash_table_lookup (_tracktype_clip, &track_type);
    if (tmpclip == NULL) {
      if (G_UNLIKELY (first_obj == TRUE)) {
        tmpclip = clip;
        first_obj = FALSE;
      } else {
        tmpclip = GES_CLIP (ges_timeline_element_copy (element, FALSE));
        if (layer) {
          /* Add new container to the same layer as @container */
          ges_clip_set_moving_from_layer (tmpclip, TRUE);
          ges_layer_add_clip (layer, tmpclip);
          ges_clip_set_moving_from_layer (tmpclip, FALSE);
        }
      }

      g_hash_table_insert (_tracktype_clip, &track_type, tmpclip);
      ges_clip_set_supported_formats (tmpclip, track_type);
    }

    /* Move trackelement to the container it is supposed to land into */
    if (tmpclip != clip)
      _transfer_child (clip, tmpclip, track_element);
  }
  g_list_free_full (children, gst_object_unref);
  g_hash_table_foreach (_tracktype_clip, (GHFunc) add_clip_to_list, &ret);
  g_hash_table_unref (_tracktype_clip);

  _share_creators (ret);
  return ret;
}

static GESContainer *
_group (GList * containers)
{
  CheckTrack *tracks = NULL;
  GESTimeline *timeline = NULL;
  GESTrackType supported_formats;
  GESLayer *layer = NULL;
  GList *tmp, *tmpclip, *tmpelement, *list;
  GstClockTime start, inpoint, duration;

  GESAsset *asset = NULL;
  GESContainer *ret = NULL;
  guint nb_tracks = 0, i = 0;

  start = inpoint = duration = GST_CLOCK_TIME_NONE;

  if (!containers)
    return NULL;

  /* First check if all the containers are clips, if they
   * all have the same start/inpoint/duration and are in the same
   * layer.
   *
   * We also need to make sure that all source have been created by the
   * same asset, keep the information */
  for (tmp = containers; tmp; tmp = tmp->next) {
    GESClip *clip;
    GESTimeline *tmptimeline;
    GESContainer *tmpcontainer;
    GESTimelineElement *element;

    tmpcontainer = GES_CONTAINER (tmp->data);
    element = GES_TIMELINE_ELEMENT (tmp->data);
    if (GES_IS_CLIP (element) == FALSE) {
      GST_DEBUG ("Can only work with clips");
      goto done;
    }
    clip = GES_CLIP (tmp->data);
    tmptimeline = GES_TIMELINE_ELEMENT_TIMELINE (element);
    if (!timeline) {
      GList *tmptrack;

      start = _START (tmpcontainer);
      inpoint = _INPOINT (tmpcontainer);
      duration = _DURATION (tmpcontainer);
      timeline = tmptimeline;
      layer = clip->priv->layer;
      nb_tracks = g_list_length (GES_TIMELINE_GET_TRACKS (timeline));
      tracks = g_new0 (CheckTrack, nb_tracks);

      for (tmptrack = GES_TIMELINE_GET_TRACKS (timeline); tmptrack;
          tmptrack = tmptrack->next) {
        tracks[i].track = tmptrack->data;
        i++;
      }
    } else {
      /* FIXME: we should allow the inpoint to be different if not a
       * core track element of the clip.
       * E.g. a GESEffect on a GESUriClip */
      if (start != _START (tmpcontainer) ||
          inpoint != _INPOINT (tmpcontainer) ||
          duration != _DURATION (tmpcontainer) || clip->priv->layer != layer) {
        GST_INFO ("All children must have the same start, inpoint, duration "
            "and be in the same layer");

        goto done;
      } else {
        GList *tmp2;

        for (tmp2 = GES_CONTAINER_CHILDREN (tmp->data); tmp2; tmp2 = tmp2->next) {
          GESTrackElement *track_element = GES_TRACK_ELEMENT (tmp2->data);

          if (GES_IS_SOURCE (track_element)) {
            guint i;

            for (i = 0; i < nb_tracks; i++) {
              if (tracks[i].track ==
                  ges_track_element_get_track (track_element)) {
                if (tracks[i].source) {
                  GST_INFO ("Can not link clips with various source for a "
                      "same track");

                  goto done;
                }
                tracks[i].source = track_element;
                break;
              }
            }
          }
        }
      }
    }
  }


  /* Then check that all sources have been created by the same asset,
   * otherwise we can not group */
  for (i = 0; i < nb_tracks; i++) {
    if (tracks[i].source == NULL) {
      GST_FIXME ("Check what to do here as we might end up having a mess");

      continue;
    }

    /* FIXME Check what to do if we have source that have no assets */
    if (!asset) {
      asset =
          ges_extractable_get_asset (GES_EXTRACTABLE
          (ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (tracks
                      [i].source))));
      continue;
    }
    if (asset !=
        ges_extractable_get_asset (GES_EXTRACTABLE
            (ges_timeline_element_get_parent (GES_TIMELINE_ELEMENT (tracks
                        [i].source))))) {
      GST_INFO ("Can not link clips with source coming from different assets");

      goto done;
    }
  }

  /* keep containers alive for _share_creators */
  list = g_list_copy_deep (containers, (GCopyFunc) gst_object_ref, NULL);

  /* And now pass all TrackElements to the first clip,
   * and remove others from the layer (updating the supported formats) */
  ret = containers->data;
  supported_formats = GES_CLIP (ret)->priv->supportedformats;
  for (tmpclip = containers->next; tmpclip; tmpclip = tmpclip->next) {
    GESClip *cclip = tmpclip->data;
    GList *children = ges_container_get_children (GES_CONTAINER (cclip), FALSE);

    for (tmpelement = children; tmpelement; tmpelement = tmpelement->next) {
      GESTrackElement *celement = GES_TRACK_ELEMENT (tmpelement->data);
      _transfer_child (cclip, GES_CLIP (ret), celement);
      supported_formats |= ges_track_element_get_track_type (celement);
    }
    g_list_free_full (children, gst_object_unref);

    ges_layer_remove_clip (layer, cclip);
  }

  ges_clip_set_supported_formats (GES_CLIP (ret), supported_formats);

  _share_creators (list);
  g_list_free_full (list, gst_object_unref);

done:
  if (tracks)
    g_free (tracks);

  return ret;
}

static void
_deep_copy (GESTimelineElement * element, GESTimelineElement * copy)
{
  GList *tmp;
  GESClip *self = GES_CLIP (element), *ccopy = GES_CLIP (copy);
  GESTrackElement *el, *el_copy;

  if (!self->priv->layer)
    return;

  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    el = GES_TRACK_ELEMENT (tmp->data);
    /* copies the children properties */
    el_copy = GES_TRACK_ELEMENT (ges_timeline_element_copy (tmp->data, TRUE));

    /* any element created by self, will have its copy considered created
     * by self's copy */
    if (_IS_CORE_CHILD (el))
      ges_track_element_add_creator (el_copy, ccopy);

    ges_track_element_copy_bindings (el, el_copy, GST_CLOCK_TIME_NONE);
    ccopy->priv->copied_track_elements =
        g_list_append (ccopy->priv->copied_track_elements, el_copy);
  }

  ccopy->priv->copied_layer = g_object_ref (self->priv->layer);
}

static GESTimelineElement *
_paste (GESTimelineElement * element, GESTimelineElement * ref,
    GstClockTime paste_position)
{
  GList *tmp;
  GESClip *self = GES_CLIP (element);
  GESClip *nclip = GES_CLIP (ges_timeline_element_copy (element, FALSE));

  if (self->priv->copied_layer)
    nclip->priv->copied_layer = g_object_ref (self->priv->copied_layer);
  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (nclip), paste_position);

  for (tmp = self->priv->copied_track_elements; tmp; tmp = tmp->next) {
    GESTrackElement *new_trackelement, *trackelement =
        GES_TRACK_ELEMENT (tmp->data);

    /* NOTE: we do not deep copy the track element, we instead call
     * ges_track_element_copy_properties explicitly, which is the
     * deep_copy for the GESTrackElementClass. */
    new_trackelement =
        GES_TRACK_ELEMENT (ges_timeline_element_copy (GES_TIMELINE_ELEMENT
            (trackelement), FALSE));
    if (new_trackelement == NULL) {
      GST_WARNING_OBJECT (trackelement, "Could not create a copy");
      continue;
    }

    if (_IS_CORE_CHILD (trackelement))
      ges_track_element_add_creator (new_trackelement, nclip);

    gst_object_ref_sink (new_trackelement);
    if (!ges_container_add (GES_CONTAINER (nclip),
            GES_TIMELINE_ELEMENT (new_trackelement))) {
      GST_ERROR_OBJECT (self, "Failed add the copied child track element %"
          GES_FORMAT " to the copy %" GES_FORMAT,
          GES_ARGS (new_trackelement), GES_ARGS (nclip));
      gst_object_unref (new_trackelement);
      continue;
    }

    ges_track_element_copy_properties (GES_TIMELINE_ELEMENT (trackelement),
        GES_TIMELINE_ELEMENT (new_trackelement));

    ges_track_element_copy_bindings (trackelement, new_trackelement,
        GST_CLOCK_TIME_NONE);
    gst_object_unref (new_trackelement);
  }

  /* FIXME: should we bypass the select-tracks-for-object signal when
   * copying and pasting? */
  if (self->priv->copied_layer) {
    if (!ges_layer_add_clip (self->priv->copied_layer, nclip)) {
      GST_INFO ("%" GES_FORMAT " could not be pasted to %" GST_TIME_FORMAT,
          GES_ARGS (element), GST_TIME_ARGS (paste_position));

      return NULL;
    }

  }

  return GES_TIMELINE_ELEMENT (nclip);
}

static gboolean
_lookup_child (GESTimelineElement * self, const gchar * prop_name,
    GObject ** child, GParamSpec ** pspec)
{
  GList *tmp;

  if (GES_TIMELINE_ELEMENT_CLASS (ges_clip_parent_class)->lookup_child (self,
          prop_name, child, pspec))
    return TRUE;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    if (ges_timeline_element_lookup_child (tmp->data, prop_name, child, pspec))
      return TRUE;
  }

  return FALSE;
}

/****************************************************
 *                                                  *
 *    GObject virtual methods implementation        *
 *                                                  *
 ****************************************************/
static void
ges_clip_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESClip *clip = GES_CLIP (object);

  switch (property_id) {
    case PROP_LAYER:
      g_value_set_object (value, clip->priv->layer);
      break;
    case PROP_SUPPORTED_FORMATS:
      g_value_set_flags (value, clip->priv->supportedformats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_clip_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESClip *clip = GES_CLIP (object);

  switch (property_id) {
    case PROP_SUPPORTED_FORMATS:
      ges_clip_set_supported_formats (clip, g_value_get_flags (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_clip_dispose (GObject * object)
{
  GESClip *self = GES_CLIP (object);

  g_list_free_full (self->priv->copied_track_elements, g_object_unref);
  self->priv->copied_track_elements = NULL;
  g_clear_object (&self->priv->copied_layer);

  G_OBJECT_CLASS (ges_clip_parent_class)->dispose (object);
}


static void
ges_clip_class_init (GESClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESContainerClass *container_class = GES_CONTAINER_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  object_class->get_property = ges_clip_get_property;
  object_class->set_property = ges_clip_set_property;
  object_class->dispose = ges_clip_dispose;
  klass->create_track_elements = ges_clip_create_track_elements_func;
  klass->create_track_element = NULL;

  /**
   * GESClip:supported-formats:
   *
   * The #GESTrackType-s that the clip supports, which it can create
   * #GESTrackElement-s for. Note that this can be a combination of
   * #GESTrackType flags to indicate support for several
   * #GESTrackElement:track-type elements.
   */
  properties[PROP_SUPPORTED_FORMATS] = g_param_spec_flags ("supported-formats",
      "Supported formats", "Formats supported by the clip",
      GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_AUDIO | GES_TRACK_TYPE_VIDEO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_SUPPORTED_FORMATS,
      properties[PROP_SUPPORTED_FORMATS]);

  /**
   * GESClip:layer:
   *
   * The layer this clip lies in.
   *
   * If you want to connect to this property's #GObject::notify signal,
   * you should connect to it with g_signal_connect_after() since the
   * signal emission may be stopped internally.
   */
  properties[PROP_LAYER] = g_param_spec_object ("layer", "Layer",
      "The GESLayer where this clip is being used.",
      GES_TYPE_LAYER, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_LAYER,
      properties[PROP_LAYER]);

  element_class->ripple = _ripple;
  element_class->ripple_end = _ripple_end;
  element_class->roll_start = _roll_start;
  element_class->roll_end = _roll_end;
  element_class->trim = _trim;
  element_class->set_start = _set_start;
  element_class->set_duration = _set_duration;
  element_class->set_inpoint = _set_inpoint;
  element_class->set_priority = _set_priority;
  element_class->set_max_duration = _set_max_duration;
  element_class->paste = _paste;
  element_class->deep_copy = _deep_copy;
  element_class->lookup_child = _lookup_child;
  element_class->get_layer_priority = _get_layer_priority;
  element_class->get_natural_framerate = _get_natural_framerate;

  container_class->add_child = _add_child;
  container_class->remove_child = _remove_child;
  container_class->child_removed = _child_removed;
  container_class->child_added = _child_added;
  container_class->ungroup = _ungroup;
  container_class->group = _group;
  container_class->grouping_priority = G_MAXUINT;
}

static void
ges_clip_init (GESClip * self)
{
  GESClipPrivate *priv;
  priv = self->priv = ges_clip_get_instance_private (self);
  priv->layer = NULL;
  priv->nb_effects = 0;
  priv->prevent_priority_offset_update = FALSE;
  priv->prevent_resort = FALSE;
  priv->updating_max_duration = FALSE;
  priv->prevent_max_duration_update = FALSE;
  priv->setting_inpoint = FALSE;
}

/**
 * ges_clip_create_track_element:
 * @clip: A #GESClip
 * @type: The track to create an element for
 *
 * Creates the core #GESTrackElement of the clip, of the given track type.
 *
 * Note, unlike ges_clip_create_track_elements(), this does not add the
 * created track element to the clip or set their timings.
 *
 * Returns: (transfer floating) (nullable): The element created
 * by @clip, or %NULL if @clip can not provide a track element for the
 * given @type or an error occurred.
 */
GESTrackElement *
ges_clip_create_track_element (GESClip * clip, GESTrackType type)
{
  GESClipClass *class;
  GESTrackElement *res;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);

  GST_DEBUG_OBJECT (clip, "Creating track element for %s",
      ges_track_type_name (type));
  if (!(type & clip->priv->supportedformats)) {
    GST_DEBUG_OBJECT (clip, "We don't support this track type %i", type);
    return NULL;
  }

  class = GES_CLIP_GET_CLASS (clip);

  if (G_UNLIKELY (class->create_track_element == NULL)) {
    GST_ERROR ("No 'create_track_element' implementation available fo type %s",
        G_OBJECT_TYPE_NAME (clip));
    return NULL;
  }

  res = class->create_track_element (clip, type);
  return res;

}

/**
 * ges_clip_create_track_elements:
 * @clip: A #GESClip
 * @type: The track-type to create elements for
 *
 * Creates the core #GESTrackElement-s of the clip, of the given track
 * type, and adds them to the clip.
 *
 * Returns: (transfer container) (element-type GESTrackElement): A list of
 * the #GESTrackElement-s created by @clip for the given @type, or %NULL
 * if no track elements are created or an error occurred.
 */

GList *
ges_clip_create_track_elements (GESClip * clip, GESTrackType type)
{
  /* add_list holds a ref to its elements to keep them alive
   * result does not */
  GList *result = NULL, *add_list = NULL, *tmp, *children;
  GESClipClass *klass;
  gboolean readding_effects_only = TRUE;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);

  klass = GES_CLIP_GET_CLASS (clip);

  if (!(klass->create_track_elements)) {
    GST_WARNING ("no GESClip::create_track_elements implentation");
    return NULL;
  }

  GST_DEBUG_OBJECT (clip, "Creating TrackElements for type: %s",
      ges_track_type_name (type));
  children = ges_container_get_children (GES_CONTAINER (clip), TRUE);
  for (tmp = children; tmp; tmp = tmp->next) {
    GESTrackElement *child = GES_TRACK_ELEMENT (tmp->data);

    if (!ges_track_element_get_track (child)
        && ges_track_element_get_track_type (child) & type) {
      GST_DEBUG_OBJECT (clip, "Removing for reusage: %" GST_PTR_FORMAT, child);
      add_list = g_list_append (add_list, gst_object_ref (child));
      ges_container_remove (GES_CONTAINER (clip), tmp->data);
      if (_IS_CORE_CHILD (child))
        readding_effects_only = FALSE;
    }
  }
  g_list_free_full (children, gst_object_unref);

  /* FIXME: we need something smarter to determine whether we should
   * create the track elements.
   * Currently, if the clip contains at least one element with a matching
   * track-type, not in a track and not a GESBaseEffect, we will not
   * recreate the track elements! But this is not a reliable indicator.
   *
   * For example, consider a uri clip that creates two audio track
   * elements: El_A and El_B. First, we add the clip to a timeline that
   * only has a single track: Track_A, and we connect to the timeline's
   * ::select-tracks-for-object signal to only allow El_A to end up in
   * Track_A. As such, whilst both El_A and El_B are initially created,
   * El_B will eventually be removed from the clip since it has no track
   * (see clip_track_element_added_cb in ges-timeline.c). As such, we now
   * have a clip that only contains El_A.
   * Next, we remove Track_A from the timeline. El_A will remain a child
   * of the clip, but now has its track unset.
   * Next, we add Track_B to the timeline, and we connect to the
   * timeline's ::select-tracks-for-object signal to only allow El_B to
   * end up in Track_B.
   *
   * However, since the clip contains an audio track element, that is not
   * an effect and has no track set: El_A. Therefore, the
   * create_track_elements method below will not be called, so we will not
   * have an El_B created for Track_B!
   *
   * Moreover, even if we did recreate the track elements, we would be
   * creating El_A again! We could destroy and recreate El_A instead, or
   * we would need a way to determine exactly which elements need to be
   * recreated.
   */
  if (readding_effects_only) {
    GList *track_elements = klass->create_track_elements (clip, type);
    for (tmp = track_elements; tmp; tmp = tmp->next) {
      gst_object_ref_sink (tmp->data);
      ges_track_element_add_creator (tmp->data, clip);
    }
    add_list = g_list_concat (track_elements, add_list);
  }

  for (tmp = add_list; tmp; tmp = tmp->next) {
    GESTimelineElement *el = GES_TIMELINE_ELEMENT (tmp->data);
    if (ges_container_add (GES_CONTAINER (clip), el))
      result = g_list_append (result, el);
    else
      GST_ERROR_OBJECT (clip, "Failed add the track element %"
          GES_FORMAT " to the clip", GES_ARGS (el));
  }
  g_list_free_full (add_list, gst_object_unref);

  return result;
}

/*
 * default implementation of GESClipClass::create_track_elements
 */
GList *
ges_clip_create_track_elements_func (GESClip * clip, GESTrackType type)
{
  GESTrackElement *result;

  GST_DEBUG_OBJECT (clip, "Creating trackelement for track: %s",
      ges_track_type_name (type));
  result = ges_clip_create_track_element (clip, type);
  if (!result) {
    GST_DEBUG ("Did not create track element");
    return NULL;
  }

  return g_list_append (NULL, result);
}

void
ges_clip_set_layer (GESClip * clip, GESLayer * layer)
{
  if (layer == clip->priv->layer)
    return;

  clip->priv->layer = layer;

  GST_DEBUG ("clip:%p, layer:%p", clip, layer);

  /* We do not want to notify the setting of layer = NULL when
   * it is actually the result of a move between layer (as we know
   * that it will be added to another layer right after, and this
   * is what imports here.) */
  if (!ELEMENT_FLAG_IS_SET (clip, GES_CLIP_IS_MOVING))
    g_object_notify_by_pspec (G_OBJECT (clip), properties[PROP_LAYER]);
}

/**
 * ges_clip_set_moving_from_layer:
 * @clip: A #GESClip
 * @is_moving: %TRUE if you want to start moving @clip to another layer
 * %FALSE when you finished moving it
 *
 * Sets the clip in a moving to layer state. You might rather use the
 * ges_clip_move_to_layer function to move #GESClip-s
 * from a layer to another.
 **/
void
ges_clip_set_moving_from_layer (GESClip * clip, gboolean is_moving)
{
  g_return_if_fail (GES_IS_CLIP (clip));

  if (is_moving)
    ELEMENT_SET_FLAG (clip, GES_CLIP_IS_MOVING);
  else
    ELEMENT_UNSET_FLAG (clip, GES_CLIP_IS_MOVING);
}

/**
 * ges_clip_is_moving_from_layer:
 * @clip: A #GESClip
 *
 * Tells you if the clip is currently moving from a layer to another.
 * You might rather use the ges_clip_move_to_layer function to
 * move #GESClip-s from a layer to another.
 *
 * Returns: %TRUE if @clip is currently moving from its current layer
 * %FALSE otherwize.
 **/
gboolean
ges_clip_is_moving_from_layer (GESClip * clip)
{
  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);

  return ELEMENT_FLAG_IS_SET (clip, GES_CLIP_IS_MOVING);
}

/**
 * ges_clip_move_to_layer:
 * @clip: A #GESClip
 * @layer: The new layer
 *
 * Moves a clip to a new layer. If the clip already exists in a layer, it
 * is first removed from its current layer before being added to the new
 * layer.
 *
 * Returns: %TRUE if @clip was successfully moved to @layer.
 */
gboolean
ges_clip_move_to_layer (GESClip * clip, GESLayer * layer)
{
  gboolean ret;
  GESLayer *current_layer;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);

  ELEMENT_SET_FLAG (clip, GES_CLIP_IS_MOVING);
  if (layer->timeline
      && !timeline_tree_can_move_element (timeline_get_tree (layer->timeline),
          GES_TIMELINE_ELEMENT (clip),
          ges_layer_get_priority (layer),
          GES_TIMELINE_ELEMENT_START (clip),
          GES_TIMELINE_ELEMENT_DURATION (clip), NULL)) {
    GST_INFO_OBJECT (layer, "Clip %" GES_FORMAT " can't move to layer %d",
        GES_ARGS (clip), ges_layer_get_priority (layer));
    ELEMENT_UNSET_FLAG (clip, GES_CLIP_IS_MOVING);
    return FALSE;
  }

  current_layer = clip->priv->layer;

  if (current_layer == NULL) {
    GST_DEBUG ("Not moving %p, only adding it to %p", clip, layer);

    return ges_layer_add_clip (layer, clip);
  }

  GST_DEBUG_OBJECT (clip, "moving to layer %p, priority: %d", layer,
      ges_layer_get_priority (layer));

  gst_object_ref (clip);
  ret = ges_layer_remove_clip (current_layer, clip);

  if (!ret) {
    ELEMENT_UNSET_FLAG (clip, GES_CLIP_IS_MOVING);
    gst_object_unref (clip);
    return FALSE;
  }

  ret = ges_layer_add_clip (layer, clip);
  ELEMENT_UNSET_FLAG (clip, GES_CLIP_IS_MOVING);

  gst_object_unref (clip);
  g_object_notify_by_pspec (G_OBJECT (clip), properties[PROP_LAYER]);


  return ret && (clip->priv->layer == layer);
}

/**
 * ges_clip_find_track_element:
 * @clip: A #GESClip
 * @track: (allow-none): The track to search in, or %NULL to search in
 * all tracks
 * @type: The type of track element to search for, or `G_TYPE_NONE` to
 * match any type
 *
 * Finds an element controlled by the clip. If @track is given,
 * then only the track elements in @track are searched for. If @type is
 * given, then this function searches for a track element of the given
 * @type.
 *
 * Note, if multiple track elements in the clip match the given criteria,
 * this will return the element amongst them with the highest
 * #GESTimelineElement:priority (numerically, the smallest). See
 * ges_clip_find_track_elements() if you wish to find all such elements.
 *
 * Returns: (transfer full) (nullable): The element controlled by
 * @clip, in @track, and of the given @type, or %NULL if no such element
 * could be found.
 */

GESTrackElement *
ges_clip_find_track_element (GESClip * clip, GESTrack * track, GType type)
{
  GList *tmp;
  GESTrackElement *otmp;

  GESTrackElement *ret = NULL;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (!(track == NULL && type == G_TYPE_NONE), NULL);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = g_list_next (tmp)) {
    otmp = (GESTrackElement *) tmp->data;

    if ((type != G_TYPE_NONE) && !G_TYPE_CHECK_INSTANCE_TYPE (tmp->data, type))
      continue;

    if ((track == NULL) || (ges_track_element_get_track (otmp) == track)) {
      ret = GES_TRACK_ELEMENT (tmp->data);
      gst_object_ref (ret);
      break;
    }
  }

  return ret;
}

/**
 * ges_clip_get_layer:
 * @clip: A #GESClip
 *
 * Gets the #GESClip:layer of the clip.
 *
 * Returns: (transfer full) (nullable): The layer @clip is in, or %NULL if
 * @clip is not in any layer.
 */
GESLayer *
ges_clip_get_layer (GESClip * clip)
{
  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);

  if (clip->priv->layer != NULL)
    gst_object_ref (G_OBJECT (clip->priv->layer));

  return clip->priv->layer;
}

/**
 * ges_clip_get_top_effects:
 * @clip: A #GESClip
 *
 * Gets the #GESBaseEffect-s that have been added to the clip. The
 * returned list is ordered by their internal index in the clip. See
 * ges_clip_get_top_effect_index().
 *
 * Returns: (transfer full) (element-type GESTrackElement): A list of all
 * #GESBaseEffect-s that have been added to @clip.
 */
GList *
ges_clip_get_top_effects (GESClip * clip)
{
  GList *tmp, *ret;
  GESTimelineElement *child;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);

  GST_DEBUG_OBJECT (clip, "Getting the %i top effects", clip->priv->nb_effects);
  ret = NULL;

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    child = tmp->data;
    if (GES_IS_BASE_EFFECT (child) && !_IS_CORE_CHILD (child))
      ret = g_list_append (ret, gst_object_ref (child));
  }

  /* list is already sorted by index because the list of children is
   * sorted by priority */
  return ret;
}

static gboolean
_is_added_effect (GESClip * clip, GESBaseEffect * effect)
{
  if (GES_TIMELINE_ELEMENT_PARENT (effect) != GES_TIMELINE_ELEMENT (clip)) {
    GST_WARNING_OBJECT (clip, "The effect %" GES_FORMAT
        " doe not belong to this clip", GES_ARGS (effect));
    return FALSE;
  }
  if (_IS_CORE_CHILD (effect)) {
    GST_WARNING_OBJECT (clip, "The effect %" GES_FORMAT " is not a top "
        "effect of this clip (it is a core element of the clip)",
        GES_ARGS (effect));
    return FALSE;
  }
  return TRUE;
}

/**
 * ges_clip_get_top_effect_index:
 * @clip: A #GESClip
 * @effect: The effect we want to get the index of
 *
 * Gets the internal index of an effect in the clip. The index of effects
 * in a clip will run from 0 to n-1, where n is the total number of
 * effects. If two effects share the same #GESTrackElement:track, the
 * effect with the numerically lower index will be applied to the source
 * data **after** the other effect, i.e. output data will always flow from
 * a higher index effect to a lower index effect.
 *
 * Returns: The index of @effect in @clip, or -1 if something went wrong.
 */
gint
ges_clip_get_top_effect_index (GESClip * clip, GESBaseEffect * effect)
{
  guint max_prio, min_prio;

  g_return_val_if_fail (GES_IS_CLIP (clip), -1);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), -1);
  if (!_is_added_effect (clip, effect))
    return -1;

  _get_priority_range (GES_CONTAINER (clip), &min_prio, &max_prio);

  return GES_TIMELINE_ELEMENT_PRIORITY (effect) - min_prio;
}

/* TODO 2.0 remove as it is Deprecated */
gint
ges_clip_get_top_effect_position (GESClip * clip, GESBaseEffect * effect)
{
  return ges_clip_get_top_effect_index (clip, effect);
}

/* TODO 2.0 remove as it is Deprecated */
gboolean
ges_clip_set_top_effect_priority (GESClip * clip,
    GESBaseEffect * effect, guint newpriority)
{
  return ges_clip_set_top_effect_index (clip, effect, newpriority);
}

/**
 * ges_clip_set_top_effect_index:
 * @clip: A #GESClip
 * @effect: An effect within @clip to move
 * @newindex: The index for @effect in @clip
 *
 * Set the index of an effect within the clip. See
 * ges_clip_get_top_effect_index(). The new index must be an existing
 * index of the clip. The effect is moved to the new index, and the other
 * effects may be shifted in index accordingly to otherwise maintain the
 * ordering.
 *
 * Returns: %TRUE if @effect was successfully moved to @newindex.
 */
gboolean
ges_clip_set_top_effect_index (GESClip * clip, GESBaseEffect * effect,
    guint newindex)
{
  gint inc;
  GList *tmp;
  guint current_prio, min_prio, max_prio;
  GESTrackElement *track_element;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);

  if (!_is_added_effect (clip, effect))
    return FALSE;

  track_element = GES_TRACK_ELEMENT (effect);
  current_prio = _PRIORITY (track_element);

  _get_priority_range (GES_CONTAINER (clip), &min_prio, &max_prio);

  newindex = newindex + min_prio;
  /*  We don't change the priority */
  if (current_prio == newindex)
    return TRUE;

  if (newindex > (clip->priv->nb_effects - 1 + min_prio)) {
    GST_DEBUG ("You are trying to make %p not a top effect", effect);
    return FALSE;
  }

  if (current_prio > clip->priv->nb_effects + min_prio) {
    GST_ERROR ("%p is not a top effect", effect);
    return FALSE;
  }

  GST_DEBUG_OBJECT (clip, "Setting top effect %" GST_PTR_FORMAT "priority: %i",
      effect, newindex);

  if (current_prio < newindex)
    inc = -1;
  else
    inc = +1;

  /* prevent a re-sort of the list whilst we are traversing it! */
  clip->priv->prevent_resort = TRUE;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *tmpo = GES_TRACK_ELEMENT (tmp->data);
    guint tck_priority = _PRIORITY (tmpo);

    if (tmpo == track_element)
      continue;

    /* only need to change the priority for those between the new and old
     * index */
    if ((inc == +1 && tck_priority >= newindex && tck_priority < current_prio)
        || (inc == -1 && tck_priority <= newindex
            && tck_priority > current_prio)) {
      _set_priority0 (GES_TIMELINE_ELEMENT (tmpo), tck_priority + inc);
    }
  }
  _set_priority0 (GES_TIMELINE_ELEMENT (track_element), newindex);

  clip->priv->prevent_resort = FALSE;
  _ges_container_sort_children (GES_CONTAINER (clip));
  /* height should have stayed the same */

  return TRUE;
}

/**
 * ges_clip_split:
 * @clip: The #GESClip to split
 * @position: The timeline position at which to perform the split
 *
 * Splits a clip at the given timeline position into two clips. The clip
 * must already have a #GESClip:layer.
 *
 * The original clip's #GESTimelineElement:duration is reduced such that
 * its end point matches the split position. Then a new clip is created in
 * the same layer, whose #GESTimelineElement:start matches the split
 * position and #GESTimelineElement:duration will be set such that its end
 * point matches the old end point of the original clip. Thus, the two
 * clips together will occupy the same positions in the timeline as the
 * original clip did.
 *
 * The children of the new clip will be new copies of the original clip's
 * children, so it will share the same sources and use the same
 * operations.
 *
 * The new clip will also have its #GESTimelineElement:in-point set so
 * that any internal data will appear in the timeline at the same time.
 * Thus, when the timeline is played, the playback of data should
 * appear the same. This may be complicated by any additional
 * #GESEffect-s that have been placed on the original clip that depend on
 * the playback time or change the data consumption rate of sources. This
 * method will attempt to translate these effects such that the playback
 * appears the same. In such complex situations, you may get a better
 * result if you place the clip in a separate sub #GESProject, which only
 * contains this clip (and its effects), and in the original layer
 * create two neighbouring #GESUriClip-s that reference this sub-project,
 * but at a different #GESTimelineElement:in-point.
 *
 * Returns: (transfer none) (nullable): The newly created clip resulting
 * from the splitting @clip, or %NULL if @clip can't be split.
 */
GESClip *
ges_clip_split (GESClip * clip, guint64 position)
{
  GList *tmp;
  GESClip *new_object;
  GstClockTime start, inpoint, duration, old_duration, new_duration;
  gdouble media_duration_factor;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (clip->priv->layer, NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (position), NULL);

  duration = _DURATION (clip);
  start = _START (clip);
  inpoint = _INPOINT (clip);

  if (position >= start + duration || position <= start) {
    GST_WARNING_OBJECT (clip, "Can not split %" GST_TIME_FORMAT
        " out of boundaries", GST_TIME_ARGS (position));
    return NULL;
  }

  old_duration = position - start;
  if (!timeline_tree_can_move_element (timeline_get_tree
          (GES_TIMELINE_ELEMENT_TIMELINE (clip)), GES_TIMELINE_ELEMENT (clip),
          GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip),
          start, old_duration, NULL)) {
    GST_WARNING_OBJECT (clip,
        "Can not split %" GES_FORMAT " at %" GST_TIME_FORMAT
        " as timeline would be in an illegal" " state.", GES_ARGS (clip),
        GST_TIME_ARGS (position));
    return NULL;
  }

  new_duration = duration + start - position;
  if (!timeline_tree_can_move_element (timeline_get_tree
          (GES_TIMELINE_ELEMENT_TIMELINE (clip)), GES_TIMELINE_ELEMENT (clip),
          GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip), position, new_duration,
          NULL)) {
    GST_WARNING_OBJECT (clip,
        "Can not split %" GES_FORMAT " at %" GST_TIME_FORMAT
        " as timeline would end up in an illegal" " state.", GES_ARGS (clip),
        GST_TIME_ARGS (position));
    return NULL;
  }

  GST_DEBUG_OBJECT (clip, "Spliting at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));

  /* Create the new Clip */
  new_object = GES_CLIP (ges_timeline_element_copy (GES_TIMELINE_ELEMENT (clip),
          FALSE));

  GST_DEBUG_OBJECT (new_object, "New 'splitted' clip");
  /* Set new timing properties on the Clip */
  media_duration_factor =
      ges_timeline_element_get_media_duration_factor (GES_TIMELINE_ELEMENT
      (clip));
  _set_start0 (GES_TIMELINE_ELEMENT (new_object), position);
  _set_inpoint0 (GES_TIMELINE_ELEMENT (new_object),
      inpoint + old_duration * media_duration_factor);
  _set_duration0 (GES_TIMELINE_ELEMENT (new_object), new_duration);

  _DURATION (clip) = old_duration;
  g_object_notify (G_OBJECT (clip), "duration");

  /* We do not want the timeline to create again TrackElement-s */
  ges_clip_set_moving_from_layer (new_object, TRUE);
  ges_layer_add_clip (clip->priv->layer, new_object);
  ges_clip_set_moving_from_layer (new_object, FALSE);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *new_trackelement, *trackelement =
        GES_TRACK_ELEMENT (tmp->data);

    new_trackelement =
        GES_TRACK_ELEMENT (ges_timeline_element_copy (GES_TIMELINE_ELEMENT
            (trackelement), FALSE));
    if (new_trackelement == NULL) {
      GST_WARNING_OBJECT (trackelement, "Could not create a copy");
      continue;
    }

    if (_IS_CORE_CHILD (trackelement))
      ges_track_element_add_creator (new_trackelement, new_object);

    /* FIXME: in-point for non-core track elements should be shifted by
     * the split (adding them to the new clip will not set their in-point)
     * Handle this once generic time effects are supported in splitting */
    ges_container_add (GES_CONTAINER (new_object),
        GES_TIMELINE_ELEMENT (new_trackelement));
    ges_track_element_copy_properties (GES_TIMELINE_ELEMENT (trackelement),
        GES_TIMELINE_ELEMENT (new_trackelement));

    /* FIXME: is position - start + inpoint always the correct splitting
     * point for control bindings? What coordinate system are control
     * bindings given in? */
    /* NOTE: control bindings that are not registered in GES are not
     * handled */
    ges_track_element_copy_bindings (trackelement, new_trackelement,
        position - start + inpoint);
  }

  /* FIXME: The below leads to a *second* notify signal for duration */
  ELEMENT_SET_FLAG (clip, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  _DURATION (clip) = duration;
  _set_duration0 (GES_TIMELINE_ELEMENT (clip), old_duration);
  ELEMENT_UNSET_FLAG (clip, GES_TIMELINE_ELEMENT_SET_SIMPLE);

  return new_object;
}

/**
 * ges_clip_set_supported_formats:
 * @clip: A #GESClip
 * @supportedformats: The #GESTrackType-s supported by @clip
 *
 * Sets the #GESClip:supported-formats of the clip. This should normally
 * only be called by subclasses, which should be responsible for updating
 * its value, rather than the user.
 */
void
ges_clip_set_supported_formats (GESClip * clip, GESTrackType supportedformats)
{
  g_return_if_fail (GES_IS_CLIP (clip));

  clip->priv->supportedformats = supportedformats;
}

/**
 * ges_clip_get_supported_formats:
 * @clip: A #GESClip
 *
 * Gets the #GESClip:supported-formats of the clip.
 *
 * Returns: The #GESTrackType-s supported by @clip.
 */
GESTrackType
ges_clip_get_supported_formats (GESClip * clip)
{
  g_return_val_if_fail (GES_IS_CLIP (clip), GES_TRACK_TYPE_UNKNOWN);

  return clip->priv->supportedformats;
}

gboolean
_ripple (GESTimelineElement * element, GstClockTime start)
{
  return ges_container_edit (GES_CONTAINER (element), NULL,
      ges_timeline_element_get_layer_priority (element),
      GES_EDIT_MODE_RIPPLE, GES_EDGE_NONE, start);
}

static gboolean
_ripple_end (GESTimelineElement * element, GstClockTime end)
{
  return ges_container_edit (GES_CONTAINER (element), NULL,
      ges_timeline_element_get_layer_priority (element),
      GES_EDIT_MODE_RIPPLE, GES_EDGE_END, end);
}

gboolean
_roll_start (GESTimelineElement * element, GstClockTime start)
{
  return ges_container_edit (GES_CONTAINER (element), NULL,
      ges_timeline_element_get_layer_priority (element),
      GES_EDIT_MODE_ROLL, GES_EDGE_START, start);
}

gboolean
_roll_end (GESTimelineElement * element, GstClockTime end)
{
  return ges_container_edit (GES_CONTAINER (element), NULL,
      ges_timeline_element_get_layer_priority (element),
      GES_EDIT_MODE_ROLL, GES_EDGE_END, end);
}

gboolean
_trim (GESTimelineElement * element, GstClockTime start)
{
  return ges_container_edit (GES_CONTAINER (element), NULL, -1,
      GES_EDIT_MODE_TRIM, GES_EDGE_START, start);
}

/**
 * ges_clip_add_asset:
 * @clip: A #GESClip
 * @asset: An asset with #GES_TYPE_TRACK_ELEMENT as its
 * #GESAsset:extractable-type
 *
 * Extracts a #GESTrackElement from an asset and adds it to the clip.
 * This can be used to add effects that derive from the asset to the
 * clip, but this method is not intended to be used to create the core
 * elements of the clip.
 *
 * Returns: (transfer none)(allow-none): The newly created element, or
 * %NULL if an error occurred.
 */
/* FIXME: this is not used elsewhere in the GES library */
GESTrackElement *
ges_clip_add_asset (GESClip * clip, GESAsset * asset)
{
  GESTrackElement *element;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (GES_IS_ASSET (asset), NULL);
  g_return_val_if_fail (g_type_is_a (ges_asset_get_extractable_type
          (asset), GES_TYPE_TRACK_ELEMENT), NULL);

  element = GES_TRACK_ELEMENT (ges_asset_extract (asset, NULL));

  if (!ges_container_add (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (element)))
    return NULL;

  return element;

}

/**
 * ges_clip_find_track_elements:
 * @clip: A #GESClip
 * @track: (allow-none): The track to search in, or %NULL to search in
 * all tracks
 * @track_type: The track-type of the track element to search for, or
 * #GES_TRACK_TYPE_UNKNOWN to match any track type
 * @type: The type of track element to search for, or %G_TYPE_NONE to
 * match any type
 *
 * Finds the #GESTrackElement-s controlled by the clip that match the
 * given criteria. If @track is given as %NULL and @track_type is given as
 * #GES_TRACK_TYPE_UNKNOWN, then the search will match all elements in any
 * track, including those with no track, and of any
 * #GESTrackElement:track-type. Otherwise, if @track is not %NULL, but
 * @track_type is #GES_TRACK_TYPE_UNKNOWN, then only the track elements in
 * @track are searched for. Otherwise, if @track_type is not
 * #GES_TRACK_TYPE_UNKNOWN, but @track is %NULL, then only the track
 * elements whose #GESTrackElement:track-type matches @track_type are
 * searched for. Otherwise, when both are given, the track elements that
 * match **either** criteria are searched for. Therefore, if you wish to
 * only find elements in a specific track, you should give the track as
 * @track, but you should not give the track's #GESTrack:track-type as
 * @track_type because this would also select elements from other tracks
 * of the same type.
 *
 * You may also give @type to _further_ restrict the search to track
 * elements of the given @type.
 *
 * Returns: (transfer full) (element-type GESTrackElement): A list of all
 * the #GESTrackElement-s controlled by @clip, in @track or of the given
 * @track_type, and of the given @type.
 */

GList *
ges_clip_find_track_elements (GESClip * clip, GESTrack * track,
    GESTrackType track_type, GType type)
{
  GList *tmp;
  GESTrackElement *otmp;

  GList *ret = NULL;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (!(track == NULL && type == G_TYPE_NONE &&
          track_type == GES_TRACK_TYPE_UNKNOWN), NULL);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = g_list_next (tmp)) {
    otmp = (GESTrackElement *) tmp->data;

    if ((type != G_TYPE_NONE) && !G_TYPE_CHECK_INSTANCE_TYPE (tmp->data, type))
      continue;

    /* TODO 2.0: an AND condition, using a condition like the above type
     * check would have made more sense here. Especially when both
     * track != NULL and track_type != GES_TRACK_TYPE_UNKNOWN are given */
    if ((track == NULL && track_type == GES_TRACK_TYPE_UNKNOWN) ||
        (track != NULL && ges_track_element_get_track (otmp) == track) ||
        (track_type != GES_TRACK_TYPE_UNKNOWN
            && ges_track_element_get_track_type (otmp) == track_type))
      ret = g_list_append (ret, gst_object_ref (otmp));
  }

  return ret;
}

/**
 * ges_clip_get_timeline_time_from_source_frame:
 * @clip: A #GESClip
 * @frame_number: The frame number to get the corresponding timestamp in the
 *                timeline coordinates
 * @err: A #GError set on errors
 *
 * This method allows you to convert a frame number into a #GstClockTime, this
 * can be used to either seek to a particular frame in the timeline or to later
 * on edit @self with that timestamp.
 *
 * This method should be use specifically in the case where you want to trim the
 * clip to a particular frame.
 *
 * The returned timestamp is in the global #GESTimeline time coordinates of @self, not
 * in the internal time coordinates. In practice, this means that you can not use
 * that time to set the clip #GESTimelineElement:in-point but it can be used in
 * the timeline editing API, for example as the @position argument of the
 * #ges_timeline_element_edit method.
 *
 * Note that you can get the frame timestamp of a particular clip asset with
 * #ges_clip_asset_get_frame_time.
 *
 * Returns: The timestamp corresponding to @frame_number in the element source
 * in the timeline coordinates.
 */
GstClockTime
ges_clip_get_timeline_time_from_source_frame (GESClip * clip,
    GESFrameNumber frame_number, GError ** err)
{
  GstClockTime frame_ts;
  GESClipAsset *asset;
  GstClockTimeDiff inpoint_diff;

  g_return_val_if_fail (GES_IS_CLIP (clip), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (!err || !*err, GST_CLOCK_TIME_NONE);

  if (!GES_FRAME_NUMBER_IS_VALID (frame_number))
    return GST_CLOCK_TIME_NONE;

  asset = GES_CLIP_ASSET (ges_extractable_get_asset (GES_EXTRACTABLE (clip)));
  frame_ts = ges_clip_asset_get_frame_time (asset, frame_number);
  if (!GST_CLOCK_TIME_IS_VALID (frame_ts))
    return GST_CLOCK_TIME_NONE;

  inpoint_diff = GST_CLOCK_DIFF (frame_ts, GES_TIMELINE_ELEMENT_INPOINT (clip));
  if (GST_CLOCK_DIFF (inpoint_diff, _START (clip)) < 0) {
    g_set_error (err, GES_ERROR, GES_ERROR_INVALID_FRAME_NUMBER,
        "Requested frame %" G_GINT64_FORMAT
        " would be outside the timeline.", frame_number);
    return GST_CLOCK_TIME_NONE;
  }

  return GST_CLOCK_DIFF (inpoint_diff, _START (clip));
}
