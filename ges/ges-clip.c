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
static void _compute_height (GESContainer * container);

struct _GESClipPrivate
{
  /*< public > */
  GESLayer *layer;

  /*< private > */
  guint nb_effects;

  GList *copied_track_elements;
  GESLayer *copied_layer;
  GESTimeline *copied_timeline;
  gboolean prevent_priority_offset_update;
  gboolean prevent_resort;

  gboolean updating_max_duration;
  gboolean prevent_max_duration_update;
  gboolean setting_inpoint;

  gboolean allow_any_track;

  /* The formats supported by this Clip */
  GESTrackType supportedformats;

  GstClockTime duration_limit;
  gboolean prevent_duration_limit_update;
};

enum
{
  PROP_0,
  PROP_LAYER,
  PROP_SUPPORTED_FORMATS,
  PROP_DURATION_LIMIT,
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

#define _IS_CORE_CHILD(child) GES_TRACK_ELEMENT_IS_CORE(child)

#define _IS_TOP_EFFECT(child) \
  (!_IS_CORE_CHILD (child) && GES_IS_BASE_EFFECT (child))

#define _IS_CORE_INTERNAL_SOURCE_CHILD(child) \
  (_IS_CORE_CHILD (child) \
  && ges_track_element_has_internal_source (GES_TRACK_ELEMENT (child)))

#define _MIN_CLOCK_TIME(a, b) \
  (GST_CLOCK_TIME_IS_VALID (a) ? \
  (GST_CLOCK_TIME_IS_VALID (b) ? MIN (a, b) : a) : b) \

#define _CLOCK_TIME_IS_LESS(first, second) \
  (GST_CLOCK_TIME_IS_VALID (first) && (!GST_CLOCK_TIME_IS_VALID (second) \
  || first < second))


typedef struct _DurationLimitData
{
  GESTrackElement *child;
  GESTrack *track;
  guint32 priority;
  GstClockTime max_duration;
  GstClockTime inpoint;
  gboolean active;
} DurationLimitData;

static DurationLimitData *
_duration_limit_data_new (GESTrackElement * child)
{
  GESTrack *track = ges_track_element_get_track (child);
  DurationLimitData *data = g_new0 (DurationLimitData, 1);

  data->child = gst_object_ref (child);
  data->track = track ? gst_object_ref (track) : NULL;
  data->inpoint = _INPOINT (child);
  data->max_duration = _MAXDURATION (child);
  data->priority = _PRIORITY (child);
  data->active = ges_track_element_is_active (child);

  return data;
}

static void
_duration_limit_data_free (gpointer data_p)
{
  DurationLimitData *data = data_p;
  gst_clear_object (&data->track);
  gst_clear_object (&data->child);
  g_free (data);
}

static GList *
_duration_limit_data_list (GESClip * clip)
{
  GList *tmp, *list = NULL;

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next)
    list = g_list_prepend (list, _duration_limit_data_new (tmp->data));

  return list;
}

static gint
_cmp_by_track_then_priority (gconstpointer a_p, gconstpointer b_p)
{
  const DurationLimitData *a = a_p, *b = b_p;
  if (a->track < b->track)
    return -1;
  else if (a->track > b->track)
    return 1;
  /* if higher priority (numerically lower) place later */
  if (a->priority < b->priority)
    return -1;
  else if (a->priority > b->priority)
    return 1;
  return 0;
}

#define _INTERNAL_LIMIT(data) \
  ((data->active && GST_CLOCK_TIME_IS_VALID (data->max_duration)) ? \
    data->max_duration - data->inpoint : GST_CLOCK_TIME_NONE)

static GstClockTime
_calculate_duration_limit (GESClip * self, GList * child_data)
{
  GstClockTime limit = GST_CLOCK_TIME_NONE;
  GList *tmp;

  child_data = g_list_sort (child_data, _cmp_by_track_then_priority);

  tmp = child_data;

  while (tmp) {
    /* we have the first element in the track, of the lowest priority, and
     * work our way up from here */
    DurationLimitData *data = tmp->data;
    GESTrack *track = data->track;
    if (track) {
      GstClockTime track_limit = _INTERNAL_LIMIT (data);

      for (tmp = tmp->next; tmp; tmp = tmp->next) {
        data = tmp->data;
        if (data->track != track)
          break;
        track_limit = _MIN_CLOCK_TIME (track_limit, _INTERNAL_LIMIT (data));
      }

      GST_LOG_OBJECT (self, "duration-limit for track %" GST_PTR_FORMAT
          " is %" GST_TIME_FORMAT, track, GST_TIME_ARGS (track_limit));
      limit = _MIN_CLOCK_TIME (limit, track_limit);
    } else {
      /* children not in a track do not affect the duration-limit */
      for (tmp = tmp->next; tmp; tmp = tmp->next) {
        data = tmp->data;
        if (data->track)
          break;
      }
    }
  }
  GST_LOG_OBJECT (self, "calculated duration-limit for the clip is %"
      GST_TIME_FORMAT, GST_TIME_ARGS (limit));

  g_list_free_full (child_data, _duration_limit_data_free);

  return limit;
}

static void
_update_duration_limit (GESClip * self)
{
  GstClockTime duration_limit;

  if (self->priv->prevent_duration_limit_update)
    return;

  duration_limit = _calculate_duration_limit (self,
      _duration_limit_data_list (self));

  if (duration_limit != self->priv->duration_limit) {
    GESTimelineElement *element = GES_TIMELINE_ELEMENT (self);

    self->priv->duration_limit = duration_limit;
    GST_INFO_OBJECT (self, "duration-limit for the clip is %"
        GST_TIME_FORMAT, GST_TIME_ARGS (duration_limit));

    if (_CLOCK_TIME_IS_LESS (duration_limit, element->duration)
        && !GES_TIMELINE_ELEMENT_BEING_EDITED (self)) {
      gboolean res;

      GST_INFO_OBJECT (self, "Automatically reducing duration to %"
          GST_TIME_FORMAT " to match the new duration-limit because "
          "the current duration %" GST_TIME_FORMAT " exceeds it",
          GST_TIME_ARGS (duration_limit), GST_TIME_ARGS (element->duration));

      /* trim end with no snapping */
      if (element->timeline)
        res = timeline_tree_trim (timeline_get_tree (element->timeline),
            element, 0, GST_CLOCK_DIFF (duration_limit, element->duration),
            GES_EDGE_END, 0);
      else
        res = ges_timeline_element_set_duration (element, duration_limit);

      if (!res)
        GST_ERROR_OBJECT (self, "Could not reduce the duration of the "
            "clip to below its duration-limit of %" GST_TIME_FORMAT,
            GST_TIME_ARGS (duration_limit));
    }
    /* notify after the auto-change in duration to allow the user to set
     * the duration in response to the change in their callbacks */
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION_LIMIT]);
  }
}

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
_child_priority_changed (GESContainer * container, GESTimelineElement * child)
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

/* returns TRUE if duration-limit needs to be updated */
static gboolean
_child_inpoint_changed (GESClip * self, GESTimelineElement * child)
{
  if (self->priv->setting_inpoint)
    return FALSE;

  /* if we have a non-core child, then we do not need the in-point of the
   * clip to change. Similarly, if the track element is core but has no
   * internal content, then this means its in-point has been set (back) to
   * 0, which means we do not need to update the in-point of the clip. */
  if (!_IS_CORE_INTERNAL_SOURCE_CHILD (child))
    return TRUE;

  /* if setting the in-point of the clip, this will handle the change in
   * the duration-limit */

  /* If the child->inpoint is the same as our own, set_inpoint will do
   * nothing. For example, when we set them in add_child (the notifies for
   * this are released after child_added is called because
   * ges_container_add freezes them) */
  _set_inpoint0 (GES_TIMELINE_ELEMENT (self), child->inpoint);
  return FALSE;
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
    if (_IS_CORE_CHILD (child))
      min = _MIN_CLOCK_TIME (min, child->maxduration);
  }
  priv->updating_max_duration = TRUE;
  ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (container), min);
  priv->updating_max_duration = FALSE;
}

static void
_child_max_duration_changed (GESContainer * container,
    GESTimelineElement * child)
{
  /* ignore non-core */
  if (!_IS_CORE_CHILD (child))
    return;

  _update_max_duration (container);
}

static void
_child_has_internal_source_changed (GESClip * self, GESTimelineElement * child)
{
  /* ignore non-core */
  /* if the track element is now registered to have no internal content,
   * we don't have to do anything */
  if (!_IS_CORE_INTERNAL_SOURCE_CHILD (child))
    return;

  /* otherwise, we need to make its in-point match ours */
  _set_inpoint0 (child, _INPOINT (self));
}

#define _IS_PROP(prop) (g_strcmp0 (name, prop) == 0)

static void
_child_property_changed_cb (GESTimelineElement * child, GParamSpec * pspec,
    GESClip * self)
{
  gboolean update = FALSE;
  const gchar *name = pspec->name;

  if (_IS_PROP ("track") || _IS_PROP ("active")) {
    update = TRUE;
  } else if (_IS_PROP ("priority")) {
    update = TRUE;
    _child_priority_changed (GES_CONTAINER (self), child);
  } else if (_IS_PROP ("in-point")) {
    update = _child_inpoint_changed (self, child);
  } else if (_IS_PROP ("max-duration")) {
    update = TRUE;
    _child_max_duration_changed (GES_CONTAINER (self), child);
  } else if (_IS_PROP ("has-internal-source")) {
    _child_has_internal_source_changed (self, child);
  }

  if (update)
    _update_duration_limit (self);
}

/****************************************************
 *              Restrict our children               *
 ****************************************************/

static gboolean
_track_contains_core (GESClip * clip, GESTrack * track, gboolean core)
{
  GList *tmp;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;
    if (_IS_CORE_CHILD (child) == core
        && ges_track_element_get_track (child) == track)
      return TRUE;
  }
  return FALSE;
}

gboolean
ges_clip_can_set_track_of_child (GESClip * clip, GESTrackElement * child,
    GESTrack * track)
{
  GESTrack *current_track = ges_track_element_get_track (child);

  if (clip->priv->allow_any_track)
    return TRUE;

  if (current_track == track)
    return TRUE;

  if (current_track) {
    /* can not remove a core element from a track if a non-core one sits
     * above it */
    if (_IS_CORE_CHILD (child)
        && _track_contains_core (clip, current_track, FALSE)) {
      GST_INFO_OBJECT (clip, "Cannot move the core child %" GES_FORMAT
          " to the track %" GST_PTR_FORMAT " because it has non-core "
          "siblings above it in its current track %" GST_PTR_FORMAT,
          GES_ARGS (child), track, current_track);
      return FALSE;
    }
    /* otherwise can remove */
  }
  if (track) {
    GESTimeline *clip_timeline = GES_TIMELINE_ELEMENT_TIMELINE (clip);
    const GESTimeline *track_timeline = ges_track_get_timeline (track);
    if (track_timeline == NULL) {
      GST_INFO_OBJECT (clip, "Cannot move the child %" GES_FORMAT
          " to the track %" GST_PTR_FORMAT " because it is not part "
          "of a timeline", GES_ARGS (child), track);
      return FALSE;
    }
    if (track_timeline != clip_timeline) {
      GST_INFO_OBJECT (clip, "Cannot move the child %" GES_FORMAT
          " to the track %" GST_PTR_FORMAT " because its timeline %"
          GST_PTR_FORMAT " does not match the clip's timeline %"
          GST_PTR_FORMAT, GES_ARGS (child), track, track_timeline,
          clip_timeline);
      return FALSE;
    }
    /* one core child per track, and other children (effects) can only be
     * placed in a track that already has a core child */
    if (_IS_CORE_CHILD (child)) {
      if (_track_contains_core (clip, track, TRUE)) {
        GST_INFO_OBJECT (clip, "Cannot move the core child %" GES_FORMAT
            " to the track %" GST_PTR_FORMAT " because it contains a "
            "core sibling", GES_ARGS (child), track);
        return FALSE;
      }
    } else {
      if (!_track_contains_core (clip, track, TRUE)) {
        GST_INFO_OBJECT (clip, "Cannot move the non-core child %"
            GES_FORMAT " to the track %" GST_PTR_FORMAT " because it "
            " does not contain a core sibling", GES_ARGS (child), track);
        return FALSE;
      }
    }
  }
  return TRUE;
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
  GESClip *self = GES_CLIP (element);
  GList *tmp;
  GESClipPrivate *priv = self->priv;
  gboolean prev_prevent = priv->prevent_duration_limit_update;

  priv->setting_inpoint = TRUE;
  priv->prevent_duration_limit_update = TRUE;
  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    if (_IS_CORE_INTERNAL_SOURCE_CHILD (child)) {
      if (!_set_inpoint0 (child, inpoint)) {
        GST_ERROR_OBJECT ("Could not set the in-point of child %"
            GES_FORMAT " to %" GST_TIME_FORMAT, GES_ARGS (child),
            GST_TIME_ARGS (inpoint));
        if (break_on_failure) {
          priv->setting_inpoint = FALSE;
          priv->prevent_duration_limit_update = prev_prevent;
          return FALSE;
        }
      }
    }
  }
  priv->setting_inpoint = FALSE;
  priv->prevent_duration_limit_update = prev_prevent;

  _update_duration_limit (self);

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

    if (child != container->initiated_move)
      _set_duration0 (GES_TIMELINE_ELEMENT (child), duration);
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;
  g_list_free_full (children, gst_object_unref);

  return TRUE;
}

static gboolean
_set_max_duration (GESTimelineElement * element, GstClockTime maxduration)
{
  GList *tmp;
  GESClip *self = GES_CLIP (element);
  GESClipPrivate *priv = self->priv;
  GstClockTime new_min = GST_CLOCK_TIME_NONE;
  gboolean has_core = FALSE;
  gboolean res = FALSE;
  gboolean prev_prevent = priv->prevent_duration_limit_update;

  /* if we are setting based on a change in the minimum */
  if (priv->updating_max_duration)
    return TRUE;

  /* else, we set every core child to have the same max duration */

  priv->prevent_duration_limit_update = TRUE;
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
        new_min = _MIN_CLOCK_TIME (new_min, child->maxduration);
      }
    }
  }
  priv->prevent_max_duration_update = FALSE;
  priv->prevent_duration_limit_update = prev_prevent;

  if (!has_core) {
    /* allow max-duration to be set arbitrarily when we have no
     * core children, even though there is no actual minimum max-duration
     * when it has no core children */
    if (GST_CLOCK_TIME_IS_VALID (maxduration))
      GST_INFO_OBJECT (element,
          "Allowing max-duration of the clip to be set to %" GST_TIME_FORMAT
          " because it has no core children", GST_TIME_ARGS (maxduration));
    res = TRUE;
    goto done;
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
    goto done;
  }

  res = TRUE;

done:
  _update_duration_limit (self);

  return res;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GESClipPrivate *priv = GES_CLIP (element)->priv;
  GList *tmp;
  guint32 min_prio, max_prio;
  gboolean prev_prevent = priv->prevent_duration_limit_update;
  GESContainer *container = GES_CONTAINER (element);

  /* send the new 'priority' to determine what the new 'min_prio' should
   * be for the clip */
  _get_priority_range_full (container, &min_prio, &max_prio, priority);

  /* offsets will remain constant for the children */
  priv->prevent_resort = TRUE;
  priv->prevent_priority_offset_update = TRUE;
  priv->prevent_duration_limit_update = TRUE;
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
   * offsets. As such, the height and duration-limit remains the same as
   * well. */
  priv->prevent_resort = FALSE;
  priv->prevent_priority_offset_update = FALSE;
  priv->prevent_duration_limit_update = prev_prevent;

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
  GESClip *self = GES_CLIP (container);
  GESClipClass *klass = GES_CLIP_GET_CLASS (GES_CLIP (container));
  guint max_prio, min_prio;
  GESTrack *track;
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (container);
  GESClipPrivate *priv = self->priv;
  GESAsset *asset, *creator_asset;
  gboolean prev_prevent = priv->prevent_duration_limit_update;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (element), FALSE);

  if (element->timeline && element->timeline != timeline) {
    GST_WARNING_OBJECT (self, "Cannot add %" GES_FORMAT " as a child "
        "because its timeline is %" GST_PTR_FORMAT " rather than the "
        "clip's timeline %" GST_PTR_FORMAT, GES_ARGS (element),
        element->timeline, timeline);
    return FALSE;
  }

  asset = ges_extractable_get_asset (GES_EXTRACTABLE (self));
  creator_asset =
      ges_track_element_get_creator_asset (GES_TRACK_ELEMENT (element));
  if (creator_asset && asset != creator_asset) {
    GST_WARNING_OBJECT (self,
        "Cannot add the track element %" GES_FORMAT " as a child "
        "because it is a core element created by another clip with a "
        "different asset to the current clip's asset", GES_ARGS (element));
    return FALSE;
  }

  track = ges_track_element_get_track (GES_TRACK_ELEMENT (element));

  if (track && ges_track_get_timeline (track) != timeline) {
    /* really, an element in a track should have the same timeline as
     * the track, so we would have checked this with the
     * element->timeline check. But technically a user could get around
     * this, so we double check here. */
    GST_WARNING_OBJECT (self, "Cannot add %" GES_FORMAT " as a child "
        "because its track %" GST_PTR_FORMAT " is part of the timeline %"
        GST_PTR_FORMAT " rather than the clip's timeline %" GST_PTR_FORMAT,
        GES_ARGS (element), track, ges_track_get_timeline (track), timeline);
    return FALSE;
  }

  /* NOTE: notifies are currently frozen by ges_container_add */
  _get_priority_range (container, &min_prio, &max_prio);
  if (creator_asset) {
    /* NOTE: Core track elements that are base effects are added like any
     * other core elements. In particular, they are *not* added to the
     * list of added effects, so we do not increase nb_effects. */

    if (track && !priv->allow_any_track
        && _track_contains_core (self, track, TRUE)) {
      GST_WARNING_OBJECT (self, "Cannot add the core child %" GES_FORMAT
          " because it is in the same track %" GST_PTR_FORMAT " as an "
          "existing core child", GES_ARGS (element), track);
      return FALSE;
    }

    /* Set the core element to have the same in-point, which we don't
     * apply to effects */
    if (ges_track_element_has_internal_source (GES_TRACK_ELEMENT (element))) {
      /* adding can fail if the max-duration of the element is smaller
       * than the current in-point of the clip */
      if (!_set_inpoint0 (element, _INPOINT (self))) {
        GST_ERROR_OBJECT (element, "Could not set the in-point of the "
            "element %" GES_FORMAT " to %" GST_TIME_FORMAT ". Not adding "
            "as a child", GES_ARGS (element), GST_TIME_ARGS (_INPOINT (self)));
        return FALSE;
      }
    }

    /* Always add at the same priority, on top of existing effects */
    _set_priority0 (element, min_prio + priv->nb_effects);
  } else if (GES_CLIP_CLASS_CAN_ADD_EFFECTS (klass) && _IS_TOP_EFFECT (element)) {
    GList *tmp;
    /* Add the effect at the lowest priority among effects (just after
     * the core elements). Need to shift the core elements up by 1
     * to make room. */

    if (track && !priv->allow_any_track
        && !_track_contains_core (GES_CLIP (self), track, TRUE)) {
      GST_WARNING_OBJECT (self, "Cannot add the effect %" GES_FORMAT
          " because its track %" GST_PTR_FORMAT " does not contain one "
          "of the clip's core children", GES_ARGS (element), track);
      return FALSE;
    }

    GST_DEBUG_OBJECT (self, "Adding %ith effect: %" GES_FORMAT
        " Priority %i", priv->nb_effects + 1, GES_ARGS (element),
        min_prio + priv->nb_effects);

    /* changing priorities, and updating their offset */
    priv->prevent_resort = TRUE;
    priv->prevent_duration_limit_update = TRUE;
    tmp = g_list_nth (container->children, priv->nb_effects);
    for (; tmp; tmp = tmp->next)
      ges_timeline_element_set_priority (GES_TIMELINE_ELEMENT (tmp->data),
          GES_TIMELINE_ELEMENT_PRIORITY (tmp->data) + 1);

    _set_priority0 (element, min_prio + priv->nb_effects);
    priv->nb_effects++;
    priv->prevent_resort = FALSE;
    priv->prevent_duration_limit_update = prev_prevent;
    /* no need to call _ges_container_sort_children (container) since
     * there is no change to the ordering yet (this happens after the
     * child is actually added) */
    /* The height has already changed (increased by 1) */
    _compute_height (container);
    /* update duration limit in _child_added */
  } else {
    if (_IS_TOP_EFFECT (element))
      GST_WARNING_OBJECT (self, "Cannot add the effect %" GES_FORMAT
          " because it is not a core element created by the clip itself "
          "and the %s class does not allow for adding extra effects",
          GES_ARGS (element), G_OBJECT_CLASS_NAME (klass));
    else if (GES_CLIP_CLASS_CAN_ADD_EFFECTS (klass))
      GST_WARNING_OBJECT (self, "Cannot add the track element %"
          GES_FORMAT " because it is neither a core element created by "
          "the clip itself, nor a GESBaseEffect", GES_ARGS (element));
    else
      GST_WARNING_OBJECT (self, "Cannot add the track element %"
          GES_FORMAT " because it is not a core element created by the "
          "clip itself", GES_ARGS (element));
    return FALSE;
  }

  _set_start0 (element, GES_TIMELINE_ELEMENT_START (self));
  _set_duration0 (element, GES_TIMELINE_ELEMENT_DURATION (self));

  return TRUE;
}

static gboolean
_remove_child (GESContainer * container, GESTimelineElement * element)
{
  GESClipPrivate *priv = GES_CLIP (container)->priv;

  /* NOTE: notifies are currently frozen by ges_container_add */
  if (_IS_TOP_EFFECT (element)) {
    GList *tmp;
    gboolean prev_prevent = priv->prevent_duration_limit_update;
    GST_DEBUG_OBJECT (container, "Resyncing effects priority.");

    /* changing priorities, so preventing a re-sort */
    priv->prevent_resort = TRUE;
    priv->prevent_duration_limit_update = TRUE;
    for (tmp = GES_CONTAINER_CHILDREN (container); tmp; tmp = tmp->next) {
      guint32 sibling_prio = GES_TIMELINE_ELEMENT_PRIORITY (tmp->data);
      if (sibling_prio > element->priority)
        ges_timeline_element_set_priority (GES_TIMELINE_ELEMENT (tmp->data),
            sibling_prio - 1);
    }
    priv->nb_effects--;
    priv->prevent_resort = FALSE;
    priv->prevent_duration_limit_update = prev_prevent;
    /* no need to re-sort the children since the rest keep the same
     * relative priorities */
    /* height may have changed */
    _compute_height (container);
  }
  /* duration-limit updated in _child_removed */
  return TRUE;
}

static void
_child_added (GESContainer * container, GESTimelineElement * element)
{
  GESClip *self = GES_CLIP (container);

  g_signal_connect (element, "notify", G_CALLBACK (_child_property_changed_cb),
      self);

  _child_priority_changed (container, element);

  if (_IS_CORE_CHILD (element))
    _update_max_duration (container);

  _update_duration_limit (self);
}

static void
_child_removed (GESContainer * container, GESTimelineElement * element)
{
  GESClip *self = GES_CLIP (container);

  g_signal_handlers_disconnect_by_func (element, _child_property_changed_cb,
      self);

  if (_IS_CORE_CHILD (element))
    _update_max_duration (container);

  _update_duration_limit (self);
}

static void
add_clip_to_list (gpointer key, gpointer clip, GList ** list)
{
  *list = g_list_prepend (*list, gst_object_ref (clip));
}

/* NOTE: Since this does not change the track of @child, this should
 * only be called if it is guaranteed that neither @from_clip nor @to_clip
 * will not break the track rules:
 * + no more than one core child per track
 * + every non-core child must be in the same track as a core child
 * NOTE: Since this does not change the creator asset of the child, this
 * should only be called for transferring children between clips with the
 * same asset.
 * NOTE: This also prevents the update of the duration-limit, so you
 * should ensure that you call _update_duration_limit on both clips when
 * transferring has completed.
 */
static void
_transfer_child (GESClip * from_clip, GESClip * to_clip,
    GESTrackElement * child)
{
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (to_clip);
  gboolean prev_prevent_from = from_clip->priv->prevent_duration_limit_update;
  gboolean prev_prevent_to = to_clip->priv->prevent_duration_limit_update;

  /* We need to bump the refcount to avoid the object to be destroyed */
  gst_object_ref (child);

  /* don't want to change tracks */
  ges_timeline_set_moving_track_elements (timeline, TRUE);

  from_clip->priv->prevent_duration_limit_update = TRUE;
  to_clip->priv->prevent_duration_limit_update = TRUE;

  ges_container_remove (GES_CONTAINER (from_clip),
      GES_TIMELINE_ELEMENT (child));

  to_clip->priv->allow_any_track = TRUE;
  if (!ges_container_add (GES_CONTAINER (to_clip),
          GES_TIMELINE_ELEMENT (child)))
    GST_ERROR ("%" GES_FORMAT " could not add child %p while"
        " transfering, this should never happen", GES_ARGS (to_clip), child);
  to_clip->priv->allow_any_track = FALSE;
  ges_timeline_set_moving_track_elements (timeline, FALSE);

  from_clip->priv->prevent_duration_limit_update = prev_prevent_from;
  to_clip->priv->prevent_duration_limit_update = prev_prevent_to;

  gst_object_unref (child);
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

  children = ges_container_get_children (container, FALSE);
  /* _add_child will add core elements at the lowest priority and new
   * non-core effects at the lowest effect priority, so we need to add
   * the highest priority children first to preserve the effect order.
   * @children is already ordered by highest priority first */
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
          /* adding to the same layer should not fail when moving */
          ges_layer_add_clip (layer, tmpclip);
          ges_clip_set_moving_from_layer (tmpclip, FALSE);
        }
      }

      g_hash_table_insert (_tracktype_clip, &track_type, tmpclip);
      ges_clip_set_supported_formats (tmpclip, track_type);
    }

    /* Move trackelement to the container it is supposed to land into */
    /* Note: it is safe to transfer the element whilst not changing tracks
     * because all track elements in the same track will stay in the
     * same clip */
    if (tmpclip != clip)
      _transfer_child (clip, tmpclip, track_element);
  }
  g_list_free_full (children, gst_object_unref);
  g_hash_table_foreach (_tracktype_clip, (GHFunc) add_clip_to_list, &ret);
  g_hash_table_unref (_tracktype_clip);

  /* Need to update the duration limit.
   * Since we have divided the clip by its tracks, the duration-limit,
   * which is a minimum value calculated per track, can only increase in
   * value, which means the duration of the clip should not change, which
   * means updating should always be possible */
  for (tmp = ret; tmp; tmp = tmp->next)
    _update_duration_limit (tmp->data);

  return ret;
}

static gboolean
_group_test_share_track (GESClip * clip1, GESClip * clip2)
{
  GList *tmp1, *tmp2;
  GESTrackElement *child1, *child2;
  for (tmp1 = GES_CONTAINER_CHILDREN (clip1); tmp1; tmp1 = tmp1->next) {
    child1 = tmp1->data;
    for (tmp2 = GES_CONTAINER_CHILDREN (clip2); tmp2; tmp2 = tmp2->next) {
      child2 = tmp2->data;
      if (ges_track_element_get_track (child1)
          == ges_track_element_get_track (child2)) {
        GST_INFO_OBJECT (clip1, "Cannot group with clip %" GES_FORMAT
            " because its child %" GES_FORMAT " shares the same "
            "track with our child %" GES_FORMAT, GES_ARGS (clip2),
            GES_ARGS (child2), GES_ARGS (child1));
        return TRUE;
      }
    }
  }
  return FALSE;
}

#define _GROUP_TEST_EQUAL(val, expect, format) \
if (val != expect) { \
  GST_INFO_OBJECT (clip, "Cannot group with other clip %" GES_FORMAT \
      " because the clip's " #expect " is %" format " rather than the " \
      #expect " of the other clip %" format, GES_ARGS (first_clip), val, \
      expect); \
  return NULL; \
}

static GESContainer *
_group (GList * containers)
{
  GESClip *first_clip = NULL;
  GESTimeline *timeline;
  GESTrackType supported_formats;
  GESLayer *layer;
  GList *tmp, *tmp2, *tmpclip;
  GstClockTime start, inpoint, duration;
  GESTimelineElement *element;

  GESAsset *asset;
  GESContainer *ret = NULL;

  if (!containers)
    return NULL;

  for (tmp = containers; tmp; tmp = tmp->next) {
    if (GES_IS_CLIP (tmp->data) == FALSE) {
      GST_DEBUG ("Can only work with clips");
      return NULL;
    }
    if (!first_clip) {
      first_clip = tmp->data;
      element = GES_TIMELINE_ELEMENT (first_clip);
      start = element->start;
      inpoint = element->inpoint;
      duration = element->duration;
      timeline = element->timeline;
      layer = first_clip->priv->layer;
      asset = ges_extractable_get_asset (GES_EXTRACTABLE (first_clip));
    }
  }

  for (tmp = containers; tmp; tmp = tmp->next) {
    GESClip *clip;
    GESAsset *cmp_asset;

    element = GES_TIMELINE_ELEMENT (tmp->data);
    clip = GES_CLIP (element);

    _GROUP_TEST_EQUAL (element->start, start, G_GUINT64_FORMAT);
    _GROUP_TEST_EQUAL (element->duration, duration, G_GUINT64_FORMAT);
    _GROUP_TEST_EQUAL (element->inpoint, inpoint, G_GUINT64_FORMAT);
    _GROUP_TEST_EQUAL (element->timeline, timeline, GST_PTR_FORMAT);
    _GROUP_TEST_EQUAL (clip->priv->layer, layer, GST_PTR_FORMAT);
    cmp_asset = ges_extractable_get_asset (GES_EXTRACTABLE (clip));
    if (cmp_asset != asset) {
      GST_INFO_OBJECT (clip, "Cannot group with other clip %"
          GES_FORMAT " because the clip's asset is %s rather than "
          " the asset of the other clip %s", GES_ARGS (first_clip),
          cmp_asset ? ges_asset_get_id (cmp_asset) : NULL,
          asset ? ges_asset_get_id (asset) : NULL);
      return NULL;
    }
    /* make sure we don't share the same track */
    for (tmp2 = tmp->next; tmp2; tmp2 = tmp2->next) {
      if (_group_test_share_track (clip, tmp2->data))
        return NULL;
    }
  }

  /* And now pass all TrackElements to the first clip,
   * and remove others from the layer (updating the supported formats) */
  ret = containers->data;
  supported_formats = GES_CLIP (ret)->priv->supportedformats;
  for (tmpclip = containers->next; tmpclip; tmpclip = tmpclip->next) {
    GESClip *cclip = tmpclip->data;
    GList *children = ges_container_get_children (GES_CONTAINER (cclip), FALSE);

    /* _add_child will add core elements at the lowest priority and new
     * non-core effects at the lowest effect priority, so we need to add
     * the highest priority children first to preserve the effect order.
     * @children is already ordered by highest priority first.
     * Priorities between children in different tracks (as tmpclips are)
     * is not important */
    for (tmp = children; tmp; tmp = tmp->next) {
      GESTrackElement *celement = GES_TRACK_ELEMENT (tmp->data);
      /* Note: it is safe to transfer the element whilst not changing
       * tracks because the elements from different clips will have
       * children in separate tracks. So it should not be possible for
       * two core children to appear in the same track */
      _transfer_child (cclip, GES_CLIP (ret), celement);
      supported_formats |= ges_track_element_get_track_type (celement);
    }
    g_list_free_full (children, gst_object_unref);
    /* duration-limit should be GST_CLOCK_TIME_NONE now that we have no
     * children */
    _update_duration_limit (cclip);

    ges_layer_remove_clip (layer, cclip);
  }

  /* Need to update the duration limit.
   * Each received clip C_i that has been grouped may have had a different
   * duration-limit L_i. In each case the duration must be less than
   * this limit, and since each clip shares the same duration, we have
   * for each clip C_i:
   *   duration <= L_i
   * Thus:
   *   duration <= min_i (L_i)
   *
   * Now, upon grouping each clip C_i into C, we have not changed the
   * children properties that affect the duration-limit. And since the
   * duration-limit is calculated as the minimum amongst the tracks of C,
   * this means that the duration-limit for C should be
   *   L = min_i (L_i) >= duration
   * Therefore, we can safely set the duration-limit of C to L without
   * changing the duration of C. */
  _update_duration_limit (GES_CLIP (ret));

  ges_clip_set_supported_formats (GES_CLIP (ret), supported_formats);

  return ret;
}

void
ges_clip_empty_from_track (GESClip * clip, GESTrack * track)
{
  GList *tmp;
  gboolean prev_prevent = clip->priv->prevent_duration_limit_update;

  if (track == NULL)
    return;
  /* allow us to remove in any order */
  clip->priv->allow_any_track = TRUE;
  clip->priv->prevent_duration_limit_update = TRUE;

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;
    if (ges_track_element_get_track (child) == track) {
      if (!ges_track_remove_element (track, child))
        GST_ERROR_OBJECT (clip, "Failed to remove child %" GES_FORMAT
            " from the track %" GST_PTR_FORMAT, GES_ARGS (child), track);
    }
  }
  clip->priv->allow_any_track = FALSE;
  clip->priv->prevent_duration_limit_update = prev_prevent;
  _update_duration_limit (clip);
}

static GESTrackElement *
_copy_track_element_to (GESTrackElement * orig, GESClip * to_clip,
    GstClockTime position)
{
  GESTrackElement *copy;
  GESTimelineElement *el_copy, *el_orig;

  /* NOTE: we do not deep copy the track element, we instead call
   * ges_track_element_copy_properties explicitly, which is the
   * deep_copy for the GESTrackElementClass. */
  el_orig = GES_TIMELINE_ELEMENT (orig);
  el_copy = ges_timeline_element_copy (el_orig, FALSE);

  if (el_copy == NULL)
    return NULL;

  copy = GES_TRACK_ELEMENT (el_copy);
  ges_track_element_copy_properties (el_orig, el_copy);
  /* NOTE: control bindings that are not registered in GES are not
   * handled */
  ges_track_element_copy_bindings (orig, copy, position);

  ges_track_element_set_creator_asset (copy,
      ges_track_element_get_creator_asset (orig));

  return copy;
}

static GESTrackElement *
ges_clip_copy_track_element_into (GESClip * clip, GESTrackElement * orig,
    GstClockTime position)
{
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (clip);
  GESTrackElement *copy;

  copy = _copy_track_element_to (orig, clip, position);
  if (copy == NULL) {
    GST_ERROR_OBJECT (clip, "Failed to create a copy of the "
        "element %" GES_FORMAT " for the clip", GES_ARGS (orig));
    return NULL;
  }

  gst_object_ref (copy);
  ges_timeline_set_moving_track_elements (timeline, TRUE);
  if (!ges_container_add (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (copy))) {
    GST_ERROR_OBJECT (clip, "Failed to add the copied child track "
        "element %" GES_FORMAT " to the clip", GES_ARGS (copy));
    ges_timeline_set_moving_track_elements (timeline, FALSE);
    gst_object_unref (copy);
    return NULL;
  }
  ges_timeline_set_moving_track_elements (timeline, FALSE);
  /* now owned by the clip */
  gst_object_unref (copy);

  return copy;
}

static void
_deep_copy (GESTimelineElement * element, GESTimelineElement * copy)
{
  GList *tmp;
  GESClip *self = GES_CLIP (element), *ccopy = GES_CLIP (copy);
  GESTrackElement *el, *el_copy;

  /* NOTE: this should only be called on a newly created @copy, so
   * its copied_track_elements, and copied_layer, should be free to set
   * without disposing of the previous values */
  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    el = GES_TRACK_ELEMENT (tmp->data);

    el_copy = _copy_track_element_to (el, ccopy, GST_CLOCK_TIME_NONE);
    if (!el_copy) {
      GST_ERROR_OBJECT (element, "Failed to copy the track element %"
          GES_FORMAT " for pasting", GES_ARGS (el));
      continue;
    }
    /* owned by copied_track_elements */
    gst_object_ref_sink (el_copy);

    /* _add_child will add core elements at the lowest priority and new
     * non-core effects at the lowest effect priority, so we need to add
     * the highest priority children first to preserve the effect order.
     * The clip's children are already ordered by highest priority first.
     * So we order copied_track_elements in the same way */
    ccopy->priv->copied_track_elements =
        g_list_append (ccopy->priv->copied_track_elements, el_copy);
  }

  ccopy->priv->copied_layer = g_object_ref (self->priv->layer);
  ccopy->priv->copied_timeline = self->priv->layer->timeline;
}

static GESTimelineElement *
_paste (GESTimelineElement * element, GESTimelineElement * ref,
    GstClockTime paste_position)
{
  GList *tmp;
  GESClip *self = GES_CLIP (element);
  GESLayer *layer = self->priv->copied_layer;
  GESClip *nclip = GES_CLIP (ges_timeline_element_copy (element, FALSE));

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (nclip), paste_position);

  /* paste in order of priority (highest first) */
  for (tmp = self->priv->copied_track_elements; tmp; tmp = tmp->next)
    ges_clip_copy_track_element_into (nclip, tmp->data, GST_CLOCK_TIME_NONE);

  if (layer) {
    if (layer->timeline != self->priv->copied_timeline) {
      GST_WARNING_OBJECT (self, "Cannot be pasted into the layer %"
          GST_PTR_FORMAT " because its timeline has changed", layer);
      gst_object_ref_sink (nclip);
      gst_object_unref (nclip);
      return NULL;
    }

    /* adding the clip to the layer will add it to the tracks, but not
     * necessarily the same ones depending on select-tracks-for-object */
    if (!ges_layer_add_clip (layer, nclip)) {
      GST_INFO ("%" GES_FORMAT " could not be pasted to %" GST_TIME_FORMAT,
          GES_ARGS (element), GST_TIME_ARGS (paste_position));

      return NULL;
    }
  }

  /* NOTE: self should not be used and be freed after this call, so we can
   * leave the freeing of copied_layer and copied_track_elements to the
   * dispose method */

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
    case PROP_DURATION_LIMIT:
      g_value_set_uint64 (value, clip->priv->duration_limit);
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

  g_list_free_full (self->priv->copied_track_elements, gst_object_unref);
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
      GES_TYPE_LAYER, G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (object_class, PROP_LAYER,
      properties[PROP_LAYER]);

  /**
   * GESClip:duration-limit:
   *
   * The maximum #GESTimelineElement:duration that can be *currently* set
   * for the clip, taking into account the #GESTimelineElement:in-point,
   * #GESTimelineElement:max-duration, GESTrackElement:active, and
   * #GESTrackElement:track properties of its children. If there is no
   * limit, this will be set to #GST_CLOCK_TIME_NONE.
   *
   * Note that whilst a clip has no children in any tracks, the limit will
   * be unknown, and similarly set to #GST_CLOCK_TIME_NONE.
   *
   * If the duration-limit would ever go below the current
   * #GESTimelineElement:duration of the clip due to a change in the above
   * variables, its #GESTimelineElement:duration will be set to the new
   * limit.
   */
  properties[PROP_DURATION_LIMIT] =
      g_param_spec_uint64 ("duration-limit", "Duration Limit",
      "A limit on the duration of the clip", 0, G_MAXUINT64,
      GST_CLOCK_TIME_NONE, G_PARAM_READABLE | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (object_class, PROP_DURATION_LIMIT,
      properties[PROP_DURATION_LIMIT]);

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
  self->priv = ges_clip_get_instance_private (self);
  self->priv->duration_limit = GST_CLOCK_TIME_NONE;
}

/**
 * ges_clip_create_track_element:
 * @clip: A #GESClip
 * @type: The track to create an element for
 *
 * Creates the core #GESTrackElement of the clip, of the given track type.
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
 * type.
 *
 * Returns: (transfer container) (element-type GESTrackElement): A list of
 * the #GESTrackElement-s created by @clip for the given @type, or %NULL
 * if no track elements are created or an error occurred.
 */

GList *
ges_clip_create_track_elements (GESClip * clip, GESTrackType type)
{
  GList *tmp, *ret;
  GESClipClass *klass;
  gboolean already_created = FALSE;
  GESAsset *asset;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);

  if ((clip->priv->supportedformats & type) == 0)
    return NULL;

  klass = GES_CLIP_GET_CLASS (clip);

  if (!(klass->create_track_elements)) {
    GST_WARNING ("no GESClip::create_track_elements implentation");
    return NULL;
  }

  GST_DEBUG_OBJECT (clip, "Creating TrackElements for type: %s",
      ges_track_type_name (type));
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = GES_TRACK_ELEMENT (tmp->data);

    if (_IS_CORE_CHILD (child)
        && ges_track_element_get_track_type (child) & type) {
      /* assume the core track elements have all been created if we find
       * at least one core child with the same type */
      already_created = TRUE;
      break;
    }
  }
  if (already_created)
    return NULL;

  ret = klass->create_track_elements (clip, type);
  asset = ges_extractable_get_asset (GES_EXTRACTABLE (clip));
  for (tmp = ret; tmp; tmp = tmp->next)
    ges_track_element_set_creator_asset (tmp->data, asset);
  return ret;
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
  gboolean ret = FALSE;
  GESLayer *current_layer;
  GESTimelineElement *element;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);

  element = GES_TIMELINE_ELEMENT (clip);
  current_layer = clip->priv->layer;

  if (current_layer == layer) {
    GST_INFO_OBJECT (clip, "Already in the layer %" GST_PTR_FORMAT, layer);
    return TRUE;
  }


  if (current_layer == NULL) {
    GST_DEBUG ("Not moving %p, only adding it to %p", clip, layer);

    return ges_layer_add_clip (layer, clip);
  }

  if (element->timeline != layer->timeline) {
    /* make sure we can perform the can_move_element_check in the timeline
     * of the layer */
    GST_WARNING_OBJECT (layer, "Cannot move clip %" GES_FORMAT " into "
        "the layer because its timeline %" GST_PTR_FORMAT " does not "
        "match the timeline of the layer %" GST_PTR_FORMAT,
        GES_ARGS (clip), element->timeline, layer->timeline);
    return FALSE;
  }

  if (layer->timeline && !GES_TIMELINE_ELEMENT_BEING_EDITED (clip)) {
    /* move to new layer, also checks moving of toplevel */
    return timeline_tree_move (timeline_get_tree (layer->timeline),
        element, (gint64) ges_layer_get_priority (current_layer) -
        (gint64) ges_layer_get_priority (layer), 0, GES_EDGE_NONE, 0);
  }

  gst_object_ref (clip);
  ELEMENT_SET_FLAG (clip, GES_CLIP_IS_MOVING);

  GST_DEBUG_OBJECT (clip, "moving to layer %p, priority: %d", layer,
      ges_layer_get_priority (layer));

  ret = ges_layer_remove_clip (current_layer, clip);

  if (!ret) {
    goto done;
  }

  ret = ges_layer_add_clip (layer, clip);

  if (ret) {
    g_object_notify_by_pspec (G_OBJECT (clip), properties[PROP_LAYER]);
  } else {
    /* try and move back into the original layer */
    ges_layer_add_clip (current_layer, clip);
  }

done:
  ELEMENT_UNSET_FLAG (clip, GES_CLIP_IS_MOVING);
  gst_object_unref (clip);

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
 * ges_clip_get_duration_limit:
 * @clip: A #GESClip
 *
 * Gets the #GESClip:duration-limit of the clip.
 *
 * Returns: The duration-limit of @clip.
 */
GstClockTime
ges_clip_get_duration_limit (GESClip * clip)
{
  g_return_val_if_fail (GES_IS_CLIP (clip), GST_CLOCK_TIME_NONE);

  return clip->priv->duration_limit;
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
    if (_IS_TOP_EFFECT (child))
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
  GESTimelineElement *element;
  GESTimeline *timeline;
  GHashTable *track_for_copy;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (clip->priv->layer, NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (position), NULL);

  element = GES_TIMELINE_ELEMENT (clip);
  timeline = element->timeline;

  duration = element->duration;
  start = element->start;
  inpoint = element->inpoint;

  if (position >= start + duration || position <= start) {
    GST_WARNING_OBJECT (clip, "Can not split %" GST_TIME_FORMAT
        " out of boundaries", GST_TIME_ARGS (position));
    return NULL;
  }

  old_duration = position - start;
  if (timeline && !timeline_tree_can_move_element (timeline_get_tree
          (timeline), element,
          ges_timeline_element_get_layer_priority (element),
          start, old_duration)) {
    GST_WARNING_OBJECT (clip,
        "Can not split %" GES_FORMAT " at %" GST_TIME_FORMAT
        " as timeline would be in an illegal" " state.", GES_ARGS (clip),
        GST_TIME_ARGS (position));
    return NULL;
  }

  new_duration = duration + start - position;
  if (timeline && !timeline_tree_can_move_element (timeline_get_tree
          (timeline), element,
          ges_timeline_element_get_layer_priority (element),
          position, new_duration)) {
    GST_WARNING_OBJECT (clip,
        "Can not split %" GES_FORMAT " at %" GST_TIME_FORMAT
        " as timeline would end up in an illegal" " state.", GES_ARGS (clip),
        GST_TIME_ARGS (position));
    return NULL;
  }

  GST_DEBUG_OBJECT (clip, "Spliting at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));

  /* Create the new Clip */
  new_object = GES_CLIP (ges_timeline_element_copy (element, FALSE));

  GST_DEBUG_OBJECT (new_object, "New 'splitted' clip");
  /* Set new timing properties on the Clip */
  media_duration_factor =
      ges_timeline_element_get_media_duration_factor (element);
  _set_start0 (GES_TIMELINE_ELEMENT (new_object), position);
  _set_inpoint0 (GES_TIMELINE_ELEMENT (new_object),
      inpoint + old_duration * media_duration_factor);
  _set_duration0 (GES_TIMELINE_ELEMENT (new_object), new_duration);

  /* We do not want the timeline to create again TrackElement-s */
  ges_clip_set_moving_from_layer (new_object, TRUE);
  /* adding to the same layer should not fail when moving */
  ges_layer_add_clip (clip->priv->layer, new_object);
  ges_clip_set_moving_from_layer (new_object, FALSE);

  /* split binding before duration changes */
  track_for_copy = g_hash_table_new_full (NULL, NULL,
      gst_object_unref, gst_object_unref);
  /* _add_child will add core elements at the lowest priority and new
   * non-core effects at the lowest effect priority, so we need to add the
   * highest priority children first to preserve the effect order. The
   * clip's children are already ordered by highest priority first. */
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *copy, *orig = tmp->data;
    GESTrack *track = ges_track_element_get_track (orig);
    /* FIXME: is position - start + inpoint always the correct splitting
     * point for control bindings? What coordinate system are control
     * bindings given in? */
    copy = ges_clip_copy_track_element_into (new_object, orig,
        position - start + inpoint);
    if (copy && track)
      g_hash_table_insert (track_for_copy, gst_object_ref (copy),
          gst_object_ref (track));
  }

  GES_TIMELINE_ELEMENT_SET_BEING_EDITED (clip);
  _set_duration0 (GES_TIMELINE_ELEMENT (clip), old_duration);
  GES_TIMELINE_ELEMENT_UNSET_BEING_EDITED (clip);

  /* add to the track after the duration change so we don't overlap! */
  for (tmp = GES_CONTAINER_CHILDREN (new_object); tmp; tmp = tmp->next) {
    GESTrackElement *copy = tmp->data;
    GESTrack *track = g_hash_table_lookup (track_for_copy, copy);
    if (track) {
      new_object->priv->allow_any_track = TRUE;
      ges_track_add_element (track, copy);
      new_object->priv->allow_any_track = FALSE;
    }
  }

  g_hash_table_unref (track_for_copy);

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

/**
 * ges_clip_add_child_to_track:
 * @clip: A #GESClip
 * @child: A child of @clip
 * @track: The track to add @child to
 * @err: Return location for an error
 *
 * Adds the track element child of the clip to a specific track.
 *
 * If the given child is already in another track, this will create a copy
 * of the child, add it to the clip, and add this copy to the track.
 *
 * You should only call this whilst a clip is part of a #GESTimeline, and
 * for tracks that are in the same timeline.
 *
 * This method is an alternative to using the
 * #GESTimeline::select-tracks-for-object signal, but can be used to
 * complement it when, say, you wish to copy a clip's children from one
 * track into a new one.
 *
 * When the child is a core child, it must be added to a track that does
 * not already contain another core child of the same clip. If it is not a
 * core child (an additional effect), then it must be added to a track
 * that already contains one of the core children of the same clip.
 *
 * This method can also fail if the adding the track element to the track
 * would break a configuration rule of the corresponding #GESTimeline,
 * such as causing three sources to overlap at a single time, or causing
 * a source to completely overlap another in the same track.
 *
 * Note that @err will not always be set upon failure.
 *
 * Returns: (transfer none): The element that was added to @track, either
 * @child or a copy of child, or %NULL if the element could not be added.
 */
GESTrackElement *
ges_clip_add_child_to_track (GESClip * clip, GESTrackElement * child,
    GESTrack * track, GError ** err)
{
  GESTimeline *timeline;
  GESTrackElement *el;
  GESTrack *current_track;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (child), NULL);
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);
  g_return_val_if_fail (!err || !*err, NULL);

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (clip);

  if (!g_list_find (GES_CONTAINER_CHILDREN (clip), child)) {
    GST_WARNING_OBJECT (clip, "The track element %" GES_FORMAT " is not "
        "a child of the clip", GES_ARGS (child));
    return NULL;
  }

  if (!timeline) {
    GST_WARNING_OBJECT (clip, "Cannot add children to tracks unless "
        "the clip is part of a timeline");
    return NULL;
  }

  if (timeline != ges_track_get_timeline (track)) {
    GST_WARNING_OBJECT (clip, "Cannot add the children to the track %"
        GST_PTR_FORMAT " because its timeline is %" GST_PTR_FORMAT
        " rather than that of the clip %" GST_PTR_FORMAT,
        track, ges_track_get_timeline (track), timeline);
    return NULL;
  }

  current_track = ges_track_element_get_track (child);

  if (current_track == track) {
    GST_WARNING_OBJECT (clip, "Child %s" GES_FORMAT " is already in the "
        "track %" GST_PTR_FORMAT, GES_ARGS (child), track);
    return NULL;
  }

  /* copy if the element is already in a track */
  if (current_track) {
    el = ges_clip_copy_track_element_into (clip, child, GST_CLOCK_TIME_NONE);
    if (!el) {
      GST_ERROR_OBJECT (clip, "Could not add a copy of the track element %"
          GES_FORMAT " to the clip so cannot add it to the track %"
          GST_PTR_FORMAT, GES_ARGS (child), track);
      return NULL;
    }
    if (_IS_TOP_EFFECT (child)) {
      /* add at next lowest priority */
      ges_clip_set_top_effect_index (clip, GES_BASE_EFFECT (el),
          ges_clip_get_top_effect_index (clip, GES_BASE_EFFECT (child)) + 1);
    }
  } else {
    el = child;
  }

  /* FIXME: set error if can not be added to track:
   * Either breaks the track rules for the clip, or the timeline
   * configuration rules */
  if (!ges_track_add_element (track, el)) {
    GST_WARNING_OBJECT (clip, "Could not add the track element %"
        GES_FORMAT " to the track %" GST_PTR_FORMAT, GES_ARGS (el), track);
    if (el != child)
      ges_container_remove (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (el));
    return NULL;
  }

  if (GES_IS_SOURCE (el))
    timeline_tree_create_transitions (timeline_get_tree (timeline),
        ges_timeline_find_auto_transition);

  return el;
}
