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
 * ## Core Children
 *
 * In more detail, clips will usually have some *core* #GESTrackElement
 * children, which are created by the clip when it is added to a layer in
 * a timeline. The type and form of these core children will depend on the
 * clip's subclass. You can use ges_track_element_is_core() to determine
 * whether a track element is considered such a core track element. Note,
 * if a core track element is part of a clip, it will always be treated as
 * a core *child* of the clip. You can connect to the
 * #GESContainer::child-added signal to be notified of their creation.
 *
 * When a child is added to a clip, the timeline will select its tracks
 * using #GESTimeline::select-tracks-for-object. Note that it may be the
 * case that the child will still have no set #GESTrackElement:track
 * after this process. For example, if the timeline does not have a track
 * of the corresponding #GESTrack:track-type. A clip can safely contain
 * such children, which may have their track set later, although they will
 * play no functioning role in the timeline in the meantime.
 *
 * If a clip may create track elements with various
 * #GESTrackElement:track-type(s), such as a #GESUriClip, but you only
 * want it to create a subset of these types, you should set the
 * #GESClip:supported-formats of the clip to the subset of types. This
 * should be done *before* adding the clip to a layer.
 *
 * If a clip will produce several core elements of the same
 * #GESTrackElement:track-type, you should connect to the timeline's
 * #GESTimeline::select-tracks-for-object signal to coordinate which
 * tracks each element should land in. Note, no two core children within a
 * clip can share the same #GESTrack, so you should not select the same
 * track for two separate core children. Provided you stick to this rule,
 * it is still safe to select several tracks for the same core child, the
 * core child will be copied into the additional tracks. You can manually
 * add the child to more tracks later using ges_clip_add_child_to_track().
 * If you do not wish to use a core child, you can always select no track.
 *
 * The #GESTimelineElement:in-point of the clip will control the
 * #GESTimelineElement:in-point of its core children to be the same
 * value if their #GESTrackElement:has-internal-source is set to %TRUE.
 *
 * The #GESTimelineElement:max-duration of the clip is the minimum
 * #GESTimelineElement:max-duration of its core children. If you set its
 * value to anything other than its current value, this will also set the
 * #GESTimelineElement:max-duration of all its core children to the same
 * value if their #GESTrackElement:has-internal-source is set to %TRUE.
 * As a special case, whilst a clip does not yet have any core children,
 * its #GESTimelineElement:max-duration may be set to indicate what its
 * value will be once they are created.
 *
 * ## Effects
 *
 * Some subclasses (#GESSourceClip and #GESBaseEffectClip) may also allow
 * their objects to have additional non-core #GESBaseEffect-s elements as
 * children. These are additional effects that are applied to the output
 * data of the core elements. They can be added to the clip using
 * ges_clip_add_top_effect(), which will take care of adding the effect to
 * the timeline's tracks. The new effect will be placed between the clip's
 * core track elements and its other effects. As such, the newly added
 * effect will be applied to any source data **before** the other existing
 * effects. You can change the ordering of effects using
 * ges_clip_set_top_effect_index().
 *
 * Tracks are selected for top effects in the same way as core children.
 * If you add a top effect to a clip before it is part of a timeline, and
 * later add the clip to a timeline, the track selection for the top
 * effects will occur just after the track selection for the core
 * children. If you add a top effect to a clip that is already part of a
 * timeline, the track selection will occur immediately. Since a top
 * effect must be applied on top of a core child, if you use
 * #GESTimeline::select-tracks-for-object, you should ensure that the
 * added effects are destined for a #GESTrack that already contains a core
 * child.
 *
 * In addition, if the core child in the track is not
 * #GESTrackElement:active, then neither can any of its effects be
 * #GESTrackElement:active. Therefore, if a core child is made in-active,
 * all of the additional effects in the same track will also become
 * in-active. Similarly, if an effect is set to be active, then the core
 * child will also become active, but other effects will be left alone.
 * Finally, if an active effect is added to the track of an in-active core
 * child, it will become in-active as well. Note, in contrast, setting a
 * core child to be active, or an effect to be in-active will *not* change
 * the other children in the same track.
 *
 * ### Time Effects
 *
 * Some effects also change the timing of their data (see #GESBaseEffect
 * for what counts as a time effect). Note that a #GESBaseEffectClip will
 * refuse time effects, but a #GESSource will allow them.
 *
 * When added to a clip, time effects may adjust the timing of other
 * children in the same track. Similarly, when changing the order of
 * effects, making them (in)-active, setting their time property values
 * or removing time effects. These can cause the #GESClip:duration-limit
 * to change in value. However, if such an operation would ever cause the
 * #GESTimelineElement:duration to shrink such that a clip's #GESSource is
 * totally overlapped in the timeline, the operation would be prevented.
 * Note that the same can happen when adding non-time effects with a
 * finite #GESTimelineElement:max-duration.
 *
 * Therefore, when working with time effects, you should -- more so than
 * usual -- not assume that setting the properties of the clip's children
 * will succeed. In particular, you should use
 * ges_timeline_element_set_child_property_full() when setting the time
 * properties.
 *
 * If you wish to preserve the *internal* duration of a source in a clip
 * during these time effect operations, you can do something like the
 * following.
 *
 * ```c
 * void
 * do_time_effect_change (GESClip * clip)
 * {
 *   GList *tmp, *children;
 *   GESTrackElement *source;
 *   GstClockTime source_outpoint;
 *   GstClockTime new_end;
 *   GError *error = NULL;
 *
 *   // choose some active source in a track to preserve the internal
 *   // duration of
 *   source = ges_clip_get_track_element (clip, NULL, GES_TYPE_SOURCE);
 *
 *   // note its current internal end time
 *   source_outpoint = ges_clip_get_internal_time_from_timeline_time (
 *         clip, source, GES_TIMELINE_ELEMENT_END (clip), NULL);
 *
 *   // handle invalid out-point
 *
 *   // stop the children's control sources from clamping when their
 *   // out-point changes with a change in the time effects
 *   children = ges_container_get_children (GES_CONTAINER (clip), FALSE);
 *
 *   for (tmp = children; tmp; tmp = tmp->next)
 *     ges_track_element_set_auto_clamp_control_sources (tmp->data, FALSE);
 *
 *   // add time effect, or set their children properties, or move them around
 *   ...
 *   // user can make sure that if a time effect changes one source, we should
 *   // also change the time effect for another source. E.g. if
 *   // "GstVideorate::rate" is set to 2.0, we also set "GstPitch::rate" to
 *   // 2.0
 *
 *   // Note the duration of the clip may have already changed if the
 *   // duration-limit of the clip dropped below its current value
 *
 *   new_end = ges_clip_get_timeline_time_from_internal_time (
 *         clip, source, source_outpoint, &error);
 *   // handle error
 *
 *   if (!ges_timeline_elemnet_edit_full (GES_TIMELINE_ELEMENT (clip),
 *         -1, GES_EDIT_MODE_TRIM, GES_EDGE_END, new_end, &error))
 *     // handle error
 *
 *   for (tmp = children; tmp; tmp = tmp->next)
 *     ges_track_element_set_auto_clamp_control_sources (tmp->data, TRUE);
 *
 *   g_list_free_full (children, gst_object_unref);
 *   gst_object_unref (source);
 * }
 * ```
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
static GstClockTime _convert_core_time (GESClip * clip, GstClockTime time,
    gboolean to_timeline, gboolean * no_core, GError ** error);

struct _GESClipPrivate
{
  /*< public > */
  GESLayer *layer;

  /*< private > */
  guint nb_effects;

  GList *copied_track_elements;
  GESLayer *copied_layer;
  GESTimeline *copied_timeline;
  gboolean prevent_resort;

  gboolean updating_max_duration;
  gboolean setting_max_duration;
  gboolean setting_inpoint;
  gboolean setting_priority;
  gboolean setting_active;

  gboolean allow_any_track;

  /* The formats supported by this Clip */
  GESTrackType supportedformats;

  GstClockTime duration_limit;
  gboolean prevent_duration_limit_update;
  gboolean prevent_children_outpoint_update;

  gboolean allow_any_remove;

  gint nb_scale_effects;
  gboolean use_effect_priority;
  guint32 effect_priority;
  GError *add_error;
  GError *remove_error;
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
 *                                                  *
 *              Listen to our children              *
 *                 and restrict them                *
 *                                                  *
 ****************************************************/

#define _IS_CORE_CHILD(child) \
    ges_track_element_is_core (GES_TRACK_ELEMENT (child))

#define _IS_TOP_EFFECT(child) \
  (!_IS_CORE_CHILD (child) && GES_IS_BASE_EFFECT (child))

#define _IS_CORE_INTERNAL_SOURCE_CHILD(child) \
  (_IS_CORE_CHILD (child) \
  && ges_track_element_has_internal_source (GES_TRACK_ELEMENT (child)))

#define _MIN_CLOCK_TIME(a, b) \
  (GST_CLOCK_TIME_IS_VALID (a) ? \
  (GST_CLOCK_TIME_IS_VALID (b) ? MIN (a, b) : a) : b) \

/****************************************************
 *                 duration-limit                   *
 ****************************************************/

typedef struct _DurationLimitData
{
  GESTrackElement *child;
  GESTrack *track;
  guint32 priority;
  GstClockTime max_duration;
  GstClockTime inpoint;
  gboolean active;
  GHashTable *time_property_values;
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

  if (GES_IS_TIME_EFFECT (child))
    data->time_property_values =
        ges_base_effect_get_time_property_values (GES_BASE_EFFECT (child));

  return data;
}

static void
_duration_limit_data_free (gpointer data_p)
{
  DurationLimitData *data = data_p;
  gst_clear_object (&data->track);
  gst_clear_object (&data->child);
  if (data->time_property_values)
    g_hash_table_unref (data->time_property_values);
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

/* transfer-full of data */
static GList *
_duration_limit_data_list_with_data (GESClip * clip, DurationLimitData * data)
{
  GList *tmp, *list = g_list_append (NULL, data);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;
    if (data->child == child)
      continue;
    list = g_list_prepend (list, _duration_limit_data_new (child));
  }

  return list;
}

static gint
_cmp_duration_limit_data_by_track_then_priority (gconstpointer a_p,
    gconstpointer b_p)
{
  const DurationLimitData *a = a_p, *b = b_p;
  if (a->track < b->track)
    return -1;
  else if (a->track > b->track)
    return 1;
  /* if higher priority (numerically lower) place later */
  if (a->priority < b->priority)
    return 1;
  else if (a->priority > b->priority)
    return -1;
  return 0;
}

#define _INTERNAL_LIMIT(data) \
  ((data->active && GST_CLOCK_TIME_IS_VALID (data->max_duration)) ? \
    data->max_duration - data->inpoint : GST_CLOCK_TIME_NONE)

static GstClockTime
_calculate_track_duration_limit (GESClip * self, GList * start, GList * end)
{
  GList *tmp;
  DurationLimitData *data = start->data;
  GstClockTime track_limit;

  /* convert source-duration to timeline-duration
   * E.g. consider the following stack
   *
   *       *=============================*
   *       |           source            |
   *       |        in-point = 5         |
   *       |      max-duration = 20      |
   *       *=============================*
   *       5         10        15        20   (internal coordinates)
   *
   *  duration-limit = 15 because max-duration - in-point = 15
   *
   *       0         5         10        15
   *       *=============================*
   *       |         time-effect         |    | sink_to_source
   *       |         rate = 0.5          |    v    / 0.5
   *       *=============================*
   *       0         10        20        30
   *
   *  duration-limit = 30 because rate effect can make it last longer
   *
   *       13        23        33    (internal coordinates)
   *       *===================*
   *       |effect-with-source |
   *       |   in-point = 13   |
   *       | max-duration = 33 |
   *       *===================*
   *       13        23        33    (internal coordinates)
   *
   *  duration-limit = 20 because effect-with-source cannot cover 30
   *
   *       0         10        20
   *       *===================*
   *       |    time-effect    |    | sink_to_source
   *       |    rate = 2.0     |    v     / 2.0
   *       *===================*
   *       0         5         10
   *
   *  duration-limit = 10 because rate effect uses up twice as much
   *
   * -----------------------------------------------timeline
   */

  while (!_IS_CORE_CHILD (data->child)) {
    GST_WARNING_OBJECT (self, "Child %" GES_FORMAT " has a lower "
        "priority than the core child in the same track. Ignoring.",
        GES_ARGS (data->child));

    start = start->next;
    if (start == end) {
      GST_ERROR_OBJECT (self, "Track %" GST_PTR_FORMAT " is missing a "
          "core child", data->track);
      return GST_CLOCK_TIME_NONE;
    }
    data = start->data;
  }

  track_limit = _INTERNAL_LIMIT (data);

  for (tmp = start->next; tmp != end; tmp = tmp->next) {
    data = tmp->data;

    if (GES_IS_TIME_EFFECT (data->child)) {
      GESBaseEffect *effect = GES_BASE_EFFECT (data->child);
      if (data->inpoint)
        GST_ERROR_OBJECT (self, "Did not expect an in-point to be set "
            "for the time effect %" GES_FORMAT, GES_ARGS (effect));
      if (GST_CLOCK_TIME_IS_VALID (data->max_duration))
        GST_ERROR_OBJECT (self, "Did not expect a max-duration to be set "
            "for the time effect %" GES_FORMAT, GES_ARGS (effect));

      if (data->active) {
        /* for the time effect, the minimum time it will receive is 0
         * (it should map 0 -> 0), and the maximum time will be track_limit */
        track_limit = ges_base_effect_translate_sink_to_source_time (effect,
            track_limit, data->time_property_values);
      }
    } else {
      GstClockTime el_limit = _INTERNAL_LIMIT (data);
      track_limit = _MIN_CLOCK_TIME (track_limit, el_limit);
    }
  }

  GST_LOG_OBJECT (self, "Track duration-limit for track %" GST_PTR_FORMAT
      " is %" GST_TIME_FORMAT, data->track, GST_TIME_ARGS (track_limit));

  return track_limit;
}

/* transfer-full of child_data */
static GstClockTime
_calculate_duration_limit (GESClip * self, GList * child_data)
{
  GstClockTime limit = GST_CLOCK_TIME_NONE;
  GList *start, *end;

  child_data = g_list_sort (child_data,
      _cmp_duration_limit_data_by_track_then_priority);

  start = child_data;

  while (start) {
    /* we have the first element in the track, of the lowest priority, and
     * work our way up from here */
    GESTrack *track = ((DurationLimitData *) (start->data))->track;

    end = start;
    do {
      end = end->next;
    } while (end && ((DurationLimitData *) (end->data))->track == track);

    if (track) {
      GstClockTime track_limit =
          _calculate_track_duration_limit (self, start, end);
      limit = _MIN_CLOCK_TIME (limit, track_limit);
    }
    start = end;
  }
  GST_LOG_OBJECT (self, "calculated duration-limit for the clip is %"
      GST_TIME_FORMAT, GST_TIME_ARGS (limit));

  g_list_free_full (child_data, _duration_limit_data_free);

  return limit;
}

static void
_update_children_outpoints (GESClip * self)
{
  GList *tmp;

  if (self->priv->prevent_children_outpoint_update)
    return;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    ges_track_element_update_outpoint (tmp->data);
  }
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

    if (GES_CLOCK_TIME_IS_LESS (duration_limit, element->duration)
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
            GES_EDGE_END, 0, NULL);
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

/* transfer full of child_data */
static gboolean
_can_update_duration_limit (GESClip * self, GList * child_data, GError ** error)
{
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (self);
  GstClockTime duration = _calculate_duration_limit (self, child_data);
  GESTimelineElement *element = GES_TIMELINE_ELEMENT (self);

  if (GES_CLOCK_TIME_IS_LESS (duration, element->duration)) {
    /* NOTE: timeline would normally not be NULL at this point */
    if (timeline
        && !timeline_tree_can_move_element (timeline_get_tree (timeline),
            element, ges_timeline_element_get_layer_priority (element),
            element->start, duration, error)) {
      return FALSE;
    }
  }
  return TRUE;
}

/****************************************************
 *                    priority                      *
 ****************************************************/

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

gboolean
ges_clip_can_set_priority_of_child (GESClip * clip, GESTrackElement * child,
    guint32 priority, GError ** error)
{
  GList *child_data;
  DurationLimitData *data;

  if (clip->priv->setting_priority)
    return TRUE;

  data = _duration_limit_data_new (child);
  data->priority = priority;

  child_data = _duration_limit_data_list_with_data (clip, data);

  if (!_can_update_duration_limit (clip, child_data, error)) {
    GST_INFO_OBJECT (clip, "Cannot move the child %" GES_FORMAT " from "
        "priority %" G_GUINT32_FORMAT " to %" G_GUINT32_FORMAT " because "
        "the duration-limit cannot be adjusted", GES_ARGS (child),
        _PRIORITY (child), priority);
    return FALSE;
  }

  return TRUE;
}

static void
_child_priority_changed (GESContainer * container, GESTimelineElement * child)
{
  /* we do not change the rest of the clip in response to a change in
   * the child priority */
  GST_DEBUG_OBJECT (container, "TimelineElement %" GES_FORMAT
      " priority changed to %u", GES_ARGS (child), _PRIORITY (child));

  if (!(GES_CLIP (container))->priv->prevent_resort) {
    _ges_container_sort_children (container);
    _compute_height (container);
  }
}

/****************************************************
 *                    in-point                      *
 ****************************************************/

GstClockTime
ges_clip_duration_limit_with_new_children_inpoints (GESClip * clip,
    GHashTable * child_inpoints)
{
  GHashTableIter iter;
  gpointer key, value;
  GList *child_data = NULL;

  g_hash_table_iter_init (&iter, child_inpoints);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GESTrackElement *child = key;
    GstClockTime *inpoint_p = value;
    DurationLimitData *data = _duration_limit_data_new (child);
    data->inpoint = *inpoint_p;
    child_data = g_list_prepend (child_data, data);
  }

  return _calculate_duration_limit (clip, child_data);
}

static gboolean
_can_set_inpoint_of_core_children (GESClip * clip, GstClockTime inpoint,
    GError ** error)
{
  GList *tmp;
  GList *child_data = NULL;

  if (GES_TIMELINE_ELEMENT_BEING_EDITED (clip))
    return TRUE;

  /* setting the in-point of a core child will shift the in-point of all
   * core children with an internal source */
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;
    DurationLimitData *data =
        _duration_limit_data_new (GES_TRACK_ELEMENT (child));

    if (_IS_CORE_INTERNAL_SOURCE_CHILD (child)) {
      if (GES_CLOCK_TIME_IS_LESS (child->maxduration, inpoint)) {
        GST_INFO_OBJECT (clip, "Cannot set the in-point from %"
            GST_TIME_FORMAT " to %" GST_TIME_FORMAT " because it would "
            "cause the in-point of its core child %" GES_FORMAT
            " to exceed its max-duration", GST_TIME_ARGS (_INPOINT (clip)),
            GST_TIME_ARGS (inpoint), GES_ARGS (child));
        g_set_error (error, GES_ERROR, GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT,
            "Cannot set the in-point of \"%s\" to %" GST_TIME_FORMAT
            " because it would exceed the max-duration of %" GST_TIME_FORMAT
            " for the child \"%s\"", GES_TIMELINE_ELEMENT_NAME (clip),
            GST_TIME_ARGS (inpoint), GST_TIME_ARGS (child->maxduration),
            child->name);

        _duration_limit_data_free (data);
        g_list_free_full (child_data, _duration_limit_data_free);
        return FALSE;
      }

      data->inpoint = inpoint;
    }

    child_data = g_list_prepend (child_data, data);
  }

  if (!_can_update_duration_limit (clip, child_data, error)) {
    GST_INFO_OBJECT (clip, "Cannot set the in-point from %" GST_TIME_FORMAT
        " to %" GST_TIME_FORMAT " because the duration-limit cannot be "
        "adjusted", GST_TIME_ARGS (_INPOINT (clip)), GST_TIME_ARGS (inpoint));
    return FALSE;
  }

  return TRUE;
}

/* Whether @clip can have its in-point set to @inpoint because none of
 * its children have a max-duration below it */
gboolean
ges_clip_can_set_inpoint_of_child (GESClip * clip, GESTrackElement * child,
    GstClockTime inpoint, GError ** error)
{
  /* don't bother checking if we are setting the value */
  if (clip->priv->setting_inpoint)
    return TRUE;

  if (GES_TIMELINE_ELEMENT_BEING_EDITED (child))
    return TRUE;

  if (!_IS_CORE_CHILD (child)) {
    /* no other sibling will move */
    GList *child_data;
    DurationLimitData *data = _duration_limit_data_new (child);
    data->inpoint = inpoint;

    child_data = _duration_limit_data_list_with_data (clip, data);

    if (!_can_update_duration_limit (clip, child_data, error)) {
      GST_INFO_OBJECT (clip, "Cannot set the in-point of non-core child %"
          GES_FORMAT " from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
          " because the duration-limit cannot be adjusted", GES_ARGS (child),
          GST_TIME_ARGS (_INPOINT (child)), GST_TIME_ARGS (inpoint));
      return FALSE;
    }

    return TRUE;
  }

  /* setting the in-point of a core child will shift the in-point of all
   * core children with an internal source */
  return _can_set_inpoint_of_core_children (clip, inpoint, error);
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

/****************************************************
 *                  max-duration                    *
 ****************************************************/

static void
_update_max_duration (GESContainer * container)
{
  GList *tmp;
  GstClockTime min = GST_CLOCK_TIME_NONE;
  GESClipPrivate *priv = GES_CLIP (container)->priv;

  if (priv->setting_max_duration)
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

gboolean
ges_clip_can_set_max_duration_of_child (GESClip * clip, GESTrackElement * child,
    GstClockTime max_duration, GError ** error)
{
  GList *child_data;
  DurationLimitData *data;

  if (clip->priv->setting_max_duration)
    return TRUE;

  data = _duration_limit_data_new (child);
  data->max_duration = max_duration;

  child_data = _duration_limit_data_list_with_data (clip, data);

  if (!_can_update_duration_limit (clip, child_data, error)) {
    GST_INFO_OBJECT (clip, "Cannot set the max-duration of child %"
        GES_FORMAT " from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT
        " because the duration-limit cannot be adjusted", GES_ARGS (child),
        GST_TIME_ARGS (_MAXDURATION (child)), GST_TIME_ARGS (max_duration));
    return FALSE;
  }

  return TRUE;
}

gboolean
ges_clip_can_set_max_duration_of_all_core (GESClip * clip,
    GstClockTime max_duration, GError ** error)
{
  GList *tmp;
  GList *child_data = NULL;

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;
    DurationLimitData *data =
        _duration_limit_data_new (GES_TRACK_ELEMENT (child));

    if (_IS_CORE_CHILD (child)) {
      /* don't check that it has an internal-source, since we are assuming
       * we will have one if the max-duration is valid */
      if (GES_CLOCK_TIME_IS_LESS (max_duration, child->inpoint)) {
        GST_INFO_OBJECT (clip, "Cannot set the max-duration from %"
            GST_TIME_FORMAT " to %" GST_TIME_FORMAT " because it would "
            "cause the in-point of its core child %" GES_FORMAT
            " to exceed its max-duration",
            GST_TIME_ARGS (child->maxduration),
            GST_TIME_ARGS (max_duration), GES_ARGS (child));
        g_set_error (error, GES_ERROR, GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT,
            "Cannot set the max-duration of the child \"%s\" under the "
            "clip \"%s\" to %" GST_TIME_FORMAT " because it would be "
            "below the in-point of %" GST_TIME_FORMAT " of the child",
            child->name, GES_TIMELINE_ELEMENT_NAME (clip),
            GST_TIME_ARGS (max_duration), GST_TIME_ARGS (child->inpoint));

        _duration_limit_data_free (data);
        g_list_free_full (child_data, _duration_limit_data_free);
        return FALSE;
      }
      data->max_duration = max_duration;
    }

    child_data = g_list_prepend (child_data, data);
  }

  if (!_can_update_duration_limit (clip, child_data, error)) {
    GST_INFO_OBJECT (clip, "Cannot set the max-duration of the core "
        "children to %" GST_TIME_FORMAT " because the duration-limit "
        "cannot be adjusted", GST_TIME_ARGS (max_duration));
    return FALSE;
  }

  return TRUE;
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

/****************************************************
 *               has-internal-source                *
 ****************************************************/

static void
_child_has_internal_source_changed (GESClip * self, GESTimelineElement * child)
{
  /* ignore non-core */
  /* if the track element is now registered to have no internal content,
   * we don't have to do anything
   * Note that the change in max-duration and in-point will already trigger
   * a change in the duration-limit, which can only increase since the
   * max-duration is now GST_CLOCK_TIME_NONE */
  if (!_IS_CORE_INTERNAL_SOURCE_CHILD (child))
    return;

  /* otherwise, we need to make its in-point match ours
   * Note that the duration-limit will be GST_CLOCK_TIME_NONE, so this
   * should not change the duration-limit */
  _set_inpoint0 (child, _INPOINT (self));
}

/****************************************************
 *                     active                       *
 ****************************************************/

gboolean
ges_clip_can_set_active_of_child (GESClip * clip, GESTrackElement * child,
    gboolean active, GError ** error)
{
  GESTrack *track = ges_track_element_get_track (child);
  gboolean is_core = _IS_CORE_CHILD (child);
  GList *child_data = NULL;
  DurationLimitData *data;

  if (clip->priv->setting_active)
    return TRUE;

  /* We want to ensure that each active non-core element has a
   * corresponding active core element in the same track */
  if (!track || is_core == active) {
    /* only the one child will change */
    data = _duration_limit_data_new (child);
    data->active = active;
    child_data = _duration_limit_data_list_with_data (clip, data);
  } else {
    GList *tmp;
    /* If we are core, make all the non-core elements in-active
     * If we are non-core, make the core element active */
    for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
      GESTrackElement *sibling = tmp->data;
      data = _duration_limit_data_new (sibling);

      if (sibling == child)
        data->active = active;

      if (ges_track_element_get_track (sibling) == track
          && _IS_CORE_CHILD (sibling) != is_core
          && ges_track_element_is_active (sibling) != active)
        data->active = active;

      child_data = g_list_prepend (child_data, data);
    }
  }

  if (!_can_update_duration_limit (clip, child_data, error)) {
    GST_INFO_OBJECT (clip, "Cannot set the active of child %" GES_FORMAT
        " from %i to %i because the duration-limit cannot be adjusted",
        GES_ARGS (child), ges_track_element_is_active (child), active);
    return FALSE;
  }

  return TRUE;
}

static void
_child_active_changed (GESClip * self, GESTrackElement * child)
{
  GList *tmp;
  GESTrack *track = ges_track_element_get_track (child);
  gboolean active = ges_track_element_is_active (child);
  gboolean is_core = _IS_CORE_CHILD (child);
  gboolean prev_prevent = self->priv->prevent_duration_limit_update;
  gboolean prev_prevent_outpoint = self->priv->prevent_children_outpoint_update;

  /* We want to ensure that each active non-core element has a
   * corresponding active core element in the same track */
  if (self->priv->setting_active || !track || is_core == active)
    return;

  self->priv->setting_active = TRUE;
  self->priv->prevent_duration_limit_update = TRUE;
  self->priv->prevent_children_outpoint_update = TRUE;

  /* If we are core, make all the non-core elements in-active
   * If we are non-core, make the core element active (should only be one) */
  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *sibling = tmp->data;

    if (ges_track_element_get_track (sibling) == track
        && _IS_CORE_CHILD (sibling) != is_core
        && ges_track_element_is_active (sibling) != active) {

      GST_INFO_OBJECT (self, "Setting active to %i for child %" GES_FORMAT
          " since the sibling %" GES_FORMAT " in the same track %"
          GST_PTR_FORMAT " has been set to %i", active, GES_ARGS (sibling),
          GES_ARGS (child), track, active);

      if (!ges_track_element_set_active (sibling, active))
        GST_ERROR_OBJECT (self, "Failed to set active for child %"
            GES_FORMAT, GES_ARGS (sibling));
    }
  }

  self->priv->setting_active = FALSE;
  self->priv->prevent_duration_limit_update = prev_prevent;
  self->priv->prevent_children_outpoint_update = prev_prevent_outpoint;
}

/****************************************************
 *                      track                       *
 ****************************************************/

static GESTrackElement *
_find_core_in_track (GESClip * clip, GESTrack * track)
{
  GList *tmp;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;
    if (_IS_CORE_CHILD (child) && ges_track_element_get_track (child) == track)
      return child;
  }
  return NULL;
}

static gboolean
_track_contains_non_core (GESClip * clip, GESTrack * track)
{
  GList *tmp;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;
    if (!_IS_CORE_CHILD (child) && ges_track_element_get_track (child) == track)
      return TRUE;
  }
  return FALSE;
}

gboolean
ges_clip_can_set_track_of_child (GESClip * clip, GESTrackElement * child,
    GESTrack * track, GError ** error)
{
  GList *child_data;
  DurationLimitData *data;
  GESTrack *current_track = ges_track_element_get_track (child);
  GESTrackElement *core = NULL;

  if (clip->priv->allow_any_track)
    return TRUE;

  if (current_track == track)
    return TRUE;

  /* NOTE: we consider the following error cases programming errors by
   * the user */
  if (current_track) {
    /* can not remove a core element from a track if a non-core one sits
     * above it */
    if (_IS_CORE_CHILD (child)
        && _track_contains_non_core (clip, current_track)) {
      GST_WARNING_OBJECT (clip, "Cannot move the core child %" GES_FORMAT
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
      GST_WARNING_OBJECT (clip, "Cannot move the child %" GES_FORMAT
          " to the track %" GST_PTR_FORMAT " because it is not part "
          "of a timeline", GES_ARGS (child), track);
      return FALSE;
    }
    if (track_timeline != clip_timeline) {
      GST_WARNING_OBJECT (clip, "Cannot move the child %" GES_FORMAT
          " to the track %" GST_PTR_FORMAT " because its timeline %"
          GST_PTR_FORMAT " does not match the clip's timeline %"
          GST_PTR_FORMAT, GES_ARGS (child), track, track_timeline,
          clip_timeline);
      return FALSE;
    }

    core = _find_core_in_track (clip, track);
    /* one core child per track, and other children (effects) can only be
     * placed in a track that already has a core child */
    if (_IS_CORE_CHILD (child)) {
      if (core) {
        GST_WARNING_OBJECT (clip, "Cannot move the core child %" GES_FORMAT
            " to the track %" GST_PTR_FORMAT " because it contains a "
            "core sibling %" GES_FORMAT, GES_ARGS (child), track,
            GES_ARGS (core));
        return FALSE;
      }
    } else {
      if (!core) {
        GST_WARNING_OBJECT (clip, "Cannot move the non-core child %"
            GES_FORMAT " to the track %" GST_PTR_FORMAT " because it "
            " does not contain a core sibling", GES_ARGS (child), track);
        return FALSE;
      }
    }
  }

  data = _duration_limit_data_new (child);
  gst_clear_object (&data->track);
  data->track = track ? gst_object_ref (track) : NULL;
  if (core && !ges_track_element_is_active (core)) {
    /* if core is set, then we are adding a non-core to a track containing
     * a core track element. If this happens, but the core is in-active
     * then we will make the non-core element also inactive upon setting
     * its track */
    data->active = FALSE;
  }

  child_data = _duration_limit_data_list_with_data (clip, data);

  if (!_can_update_duration_limit (clip, child_data, error)) {
    GST_INFO_OBJECT (clip, "Cannot move the child %" GES_FORMAT " from "
        "track %" GST_PTR_FORMAT " to track %" GST_PTR_FORMAT " because "
        "the duration-limit cannot be adjusted", GES_ARGS (child),
        current_track, track);
    return FALSE;
  }

  return TRUE;
}

static void
_update_active_for_track (GESClip * self, GESTrackElement * child)
{
  GESTrack *track = ges_track_element_get_track (child);
  GESTrackElement *core;
  gboolean active;
  gboolean prev_prevent = self->priv->prevent_duration_limit_update;
  gboolean prev_prevent_outpoint = self->priv->prevent_children_outpoint_update;

  if (self->priv->allow_any_track || _IS_CORE_CHILD (child) || !track)
    return;

  /* if we add a non-core to a track, but the core child is inactive, we
   * also need the non-core to be inactive */
  core = _find_core_in_track (self, track);

  if (!core) {
    GST_ERROR_OBJECT (self, "The non-core child %" GES_FORMAT " is in "
        "the track %" GST_PTR_FORMAT " with no core sibling",
        GES_ARGS (child), track);
    active = FALSE;
  } else {
    active = ges_track_element_is_active (core);
  }

  if (!active && ges_track_element_is_active (child)) {

    GST_INFO_OBJECT (self, "De-activating non-core child %" GES_FORMAT
        " since the core child in the same track %" GST_PTR_FORMAT " is "
        "not active", GES_ARGS (child), track);

    self->priv->setting_active = TRUE;
    self->priv->prevent_duration_limit_update = TRUE;
    self->priv->prevent_children_outpoint_update = TRUE;

    if (!ges_track_element_set_active (child, FALSE))
      GST_ERROR_OBJECT (self, "Failed to de-activate child %" GES_FORMAT,
          GES_ARGS (child));

    self->priv->setting_active = FALSE;
    self->priv->prevent_duration_limit_update = prev_prevent;
    self->priv->prevent_children_outpoint_update = prev_prevent_outpoint;
  }
}

#define _IS_PROP(prop) (g_strcmp0 (name, prop) == 0)

static void
_child_property_changed_cb (GESTimelineElement * child, GParamSpec * pspec,
    GESClip * self)
{
  gboolean update_limit = FALSE;
  gboolean update_outpoint = FALSE;
  const gchar *name = pspec->name;

  if (_IS_PROP ("track")) {
    update_limit = TRUE;
    update_outpoint = TRUE;
    _update_active_for_track (self, GES_TRACK_ELEMENT (child));
  } else if (_IS_PROP ("active")) {
    update_limit = TRUE;
    update_outpoint = TRUE;
    _child_active_changed (self, GES_TRACK_ELEMENT (child));
  } else if (_IS_PROP ("priority")) {
    update_limit = TRUE;
    update_outpoint = TRUE;
    _child_priority_changed (GES_CONTAINER (self), child);
  } else if (_IS_PROP ("in-point")) {
    /* update outpoint already handled by the track element */
    update_limit = _child_inpoint_changed (self, child);
  } else if (_IS_PROP ("max-duration")) {
    update_limit = TRUE;
    _child_max_duration_changed (GES_CONTAINER (self), child);
  } else if (_IS_PROP ("has-internal-source")) {
    _child_has_internal_source_changed (self, child);
  }

  if (update_limit)
    _update_duration_limit (self);
  if (update_outpoint)
    _update_children_outpoints (self);
}

/****************************************************
 *                time properties                   *
 ****************************************************/

gboolean
ges_clip_can_set_time_property_of_child (GESClip * clip,
    GESTrackElement * child, GObject * child_prop_object, GParamSpec * pspec,
    const GValue * value, GError ** error)
{
  if (_IS_TOP_EFFECT (child)) {
    gchar *prop_name =
        ges_base_effect_get_time_property_name (GES_BASE_EFFECT (child),
        child_prop_object, pspec);

    if (prop_name) {
      GList *child_data;
      DurationLimitData *data = _duration_limit_data_new (child);
      GValue *copy = g_new0 (GValue, 1);

      g_value_init (copy, pspec->value_type);
      g_value_copy (value, copy);

      g_hash_table_insert (data->time_property_values, prop_name, copy);

      child_data = _duration_limit_data_list_with_data (clip, data);

      if (!_can_update_duration_limit (clip, child_data, error)) {
        gchar *val_str = gst_value_serialize (value);
        GST_INFO_OBJECT (clip, "Cannot set the child-property %s of "
            "child %" GES_FORMAT " to %s because the duration-limit "
            "cannot be adjusted", prop_name, GES_ARGS (child), val_str);
        g_free (val_str);
        return FALSE;
      }
    }
  }
  return TRUE;
}

static void
_child_time_property_changed_cb (GESTimelineElement * child,
    GObject * prop_object, GParamSpec * pspec, GESClip * self)
{
  gchar *time_prop =
      ges_base_effect_get_time_property_name (GES_BASE_EFFECT (child),
      prop_object, pspec);
  if (time_prop) {
    g_free (time_prop);
    _update_duration_limit (self);
    _update_children_outpoints (self);
  }
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
  if (!_can_set_inpoint_of_core_children (GES_CLIP (element), inpoint, NULL)) {
    GST_WARNING_OBJECT (element, "Cannot set the in-point to %"
        GST_TIME_FORMAT, GST_TIME_ARGS (inpoint));
    return FALSE;
  }

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
  GList *child_data = NULL;
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

  /* check that the duration-limit can be changed */
  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;
    DurationLimitData *data = _duration_limit_data_new (child);

    if (_IS_CORE_INTERNAL_SOURCE_CHILD (child))
      data->max_duration = maxduration;

    child_data = g_list_prepend (child_data, data);
  }

  if (!_can_update_duration_limit (self, child_data, NULL)) {
    GST_WARNING_OBJECT (self, "Cannot set the max-duration from %"
        GST_TIME_FORMAT " to %" GST_TIME_FORMAT " because the "
        "duration-limit cannot be adjusted",
        GST_TIME_ARGS (element->maxduration), GST_TIME_ARGS (maxduration));
    return FALSE;
  }

  priv->prevent_duration_limit_update = TRUE;
  priv->setting_max_duration = TRUE;
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
  priv->setting_max_duration = FALSE;
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
  guint32 min_prio, max_prio, min_child_prio = G_MAXUINT32;
  gboolean prev_prevent = priv->prevent_duration_limit_update;
  gboolean prev_prevent_outpoint = priv->prevent_children_outpoint_update;
  GESContainer *container = GES_CONTAINER (element);

  for (tmp = container->children; tmp; tmp = g_list_next (tmp))
    min_child_prio = MIN (min_child_prio, _PRIORITY (tmp->data));

  /* send the new 'priority' to determine what the new 'min_prio' should
   * be for the clip */
  _get_priority_range_full (container, &min_prio, &max_prio, priority);

  /* offsets will remain constant for the children */
  priv->prevent_resort = TRUE;
  priv->prevent_duration_limit_update = TRUE;
  priv->prevent_children_outpoint_update = TRUE;
  priv->setting_priority = TRUE;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = tmp->data;
    guint32 track_element_prio = min_prio + (child->priority - min_child_prio);

    if (track_element_prio > max_prio) {
      GST_WARNING_OBJECT (container, "%s priority of %i, is outside of its "
          "containing layer space. (%d/%d) setting it to the maximum it can be",
          child->name, priority, min_prio, max_prio);

      track_element_prio = max_prio;
    }
    _set_priority0 (child, track_element_prio);
  }
  /* no need to re-sort the container since we maintained the relative
   * offsets. As such, the height and duration-limit remains the same as
   * well. */
  priv->prevent_resort = FALSE;
  priv->setting_priority = FALSE;
  priv->prevent_duration_limit_update = prev_prevent;
  priv->prevent_children_outpoint_update = prev_prevent_outpoint;

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

void
ges_clip_take_add_error (GESClip * clip, GError ** error)
{
  GESClipPrivate *priv = clip->priv;

  g_clear_error (error);
  if (error) {
    if (*error) {
      GST_ERROR_OBJECT (clip, "Error not handled: %s", (*error)->message);
      g_error_free (*error);
    }
    *error = priv->add_error;
  } else {
    g_clear_error (&priv->add_error);
  }
  priv->add_error = NULL;
}

void
ges_clip_set_add_error (GESClip * clip, GError * error)
{
  GESClipPrivate *priv = clip->priv;

  g_clear_error (&priv->add_error);
  priv->add_error = error;
}

static gboolean
_add_child (GESContainer * container, GESTimelineElement * element)
{
  gboolean ret = FALSE;
  GESClip *self = GES_CLIP (container);
  GESTrackElement *track_el = GES_TRACK_ELEMENT (element);
  GESClipClass *klass = GES_CLIP_GET_CLASS (self);
  guint32 min_prio, max_prio, new_prio;
  GESTrack *track;
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (container);
  GESClipPrivate *priv = self->priv;
  GESAsset *asset, *creator_asset;
  gboolean adding_scale_effect = FALSE;
  gboolean prev_prevent = priv->prevent_duration_limit_update;
  gboolean prev_prevent_outpoint = priv->prevent_children_outpoint_update;
  GList *tmp;
  GError *error = NULL;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (element), FALSE);

  if (element->timeline && element->timeline != timeline) {
    GST_WARNING_OBJECT (self, "Cannot add %" GES_FORMAT " as a child "
        "because its timeline is %" GST_PTR_FORMAT " rather than the "
        "clip's timeline %" GST_PTR_FORMAT, GES_ARGS (element),
        element->timeline, timeline);
    goto done;
  }

  asset = ges_extractable_get_asset (GES_EXTRACTABLE (self));
  creator_asset = ges_track_element_get_creator_asset (track_el);
  if (creator_asset && asset != creator_asset) {
    GST_WARNING_OBJECT (self,
        "Cannot add the track element %" GES_FORMAT " as a child "
        "because it is a core element created by another clip with a "
        "different asset to the current clip's asset", GES_ARGS (element));
    goto done;
  }

  track = ges_track_element_get_track (track_el);

  if (track && ges_track_get_timeline (track) != timeline) {
    /* really, an element in a track should have the same timeline as
     * the track, so we would have checked this with the
     * element->timeline check. But technically a user could get around
     * this, so we double check here. */
    GST_WARNING_OBJECT (self, "Cannot add %" GES_FORMAT " as a child "
        "because its track %" GST_PTR_FORMAT " is part of the timeline %"
        GST_PTR_FORMAT " rather than the clip's timeline %" GST_PTR_FORMAT,
        GES_ARGS (element), track, ges_track_get_timeline (track), timeline);
    goto done;
  }

  /* NOTE: notifies are currently frozen by ges_container_add */

  _get_priority_range (container, &min_prio, &max_prio);

  if (creator_asset) {
    /* NOTE: Core track elements that are base effects are added like any
     * other core elements. In particular, they are *not* added to the
     * list of added effects, so we do not increase nb_effects. */

    /* Set the core element to have the same in-point, which we don't
     * apply to effects */
    GstClockTime new_inpoint;
    if (ges_track_element_has_internal_source (track_el))
      new_inpoint = _INPOINT (self);
    else
      new_inpoint = 0;

    /* new priority is that of the lowest priority core child. Usually
     * each core child has the same priority.
     * Also must be lower than all effects */

    new_prio = min_prio;
    for (tmp = container->children; tmp; tmp = tmp->next) {
      if (_IS_CORE_CHILD (tmp->data))
        new_prio = MAX (new_prio, _PRIORITY (tmp->data));
      else if (_IS_TOP_EFFECT (tmp->data))
        new_prio = MAX (new_prio, _PRIORITY (tmp->data) + 1);
    }

    if (track && !priv->allow_any_track) {
      GList *child_data;
      DurationLimitData *data;
      GESTrackElement *core = _find_core_in_track (self, track);

      if (core) {
        GST_WARNING_OBJECT (self, "Cannot add the core child %" GES_FORMAT
            " because it is in the same track %" GST_PTR_FORMAT " as an "
            "existing core child %" GES_FORMAT, GES_ARGS (element), track,
            GES_ARGS (core));
        goto done;
      }

      data = _duration_limit_data_new (track_el);
      data->inpoint = new_inpoint;
      data->priority = new_prio;

      child_data = _duration_limit_data_list_with_data (self, data);

      if (!_can_update_duration_limit (self, child_data, &error)) {
        GST_INFO_OBJECT (self, "Cannot add core %" GES_FORMAT " as a "
            "child because the duration-limit cannot be adjusted",
            GES_ARGS (element));
        goto done;
      }
    }

    if (GES_CLOCK_TIME_IS_LESS (element->maxduration, new_inpoint)) {
      GST_INFO_OBJECT (self, "Can not set the in-point of the "
          "element %" GES_FORMAT " to %" GST_TIME_FORMAT " because its "
          "max-duration is %" GST_TIME_FORMAT, GES_ARGS (element),
          GST_TIME_ARGS (new_inpoint), GST_TIME_ARGS (element->maxduration));

      g_set_error (&error, GES_ERROR, GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT,
          "Cannot add the child \"%s\" to clip \"%s\" because its max-"
          "duration is %" GST_TIME_FORMAT ", which is less than the in-"
          "point of the clip %" GST_TIME_FORMAT, element->name,
          GES_TIMELINE_ELEMENT_NAME (self),
          GST_TIME_ARGS (element->maxduration), GST_TIME_ARGS (new_inpoint));

      goto done;
    }

    /* adding can fail if the max-duration of the element is smaller
     * than the current in-point of the clip */
    if (!_set_inpoint0 (element, new_inpoint)) {
      GST_WARNING_OBJECT (self, "Could not set the in-point of the "
          "element %" GES_FORMAT " to %" GST_TIME_FORMAT ". Not adding "
          "as a child", GES_ARGS (element), GST_TIME_ARGS (new_inpoint));
      goto done;
    }

    _set_priority0 (element, new_prio);

  } else if (GES_CLIP_CLASS_CAN_ADD_EFFECTS (klass) && _IS_TOP_EFFECT (element)) {
    /* Add the effect at the lowest priority among effects (just after
     * the core elements). Need to shift the core elements up by 1
     * to make room. */

    /* new priority is the lowest priority effect */
    if (priv->use_effect_priority) {
      new_prio = priv->effect_priority;
    } else {
      new_prio = min_prio;
      for (tmp = container->children; tmp; tmp = tmp->next) {
        if (_IS_TOP_EFFECT (tmp->data))
          new_prio = MAX (new_prio, _PRIORITY (tmp->data) + 1);
      }
    }

    if (GES_IS_EFFECT (element)) {
      GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (element));
      const gchar *bindesc = ges_asset_get_id (asset);

      adding_scale_effect = !strstr (bindesc, "gesvideoscale");
    }

    /* make sure higher than core */
    for (tmp = container->children; tmp; tmp = tmp->next) {
      if (_IS_CORE_CHILD (tmp->data))
        new_prio = MIN (new_prio, _PRIORITY (tmp->data));
    }

    if (track && !priv->allow_any_track) {
      GList *child_data, *tmp;
      DurationLimitData *data;
      GESTrackElement *core = _find_core_in_track (self, track);

      if (!core) {
        GST_WARNING_OBJECT (self, "Cannot add the effect %" GES_FORMAT
            " because its track %" GST_PTR_FORMAT " does not contain one "
            "of the clip's core children", GES_ARGS (element), track);
        goto done;
      }

      data = _duration_limit_data_new (track_el);
      data->priority = new_prio;
      if (!ges_track_element_is_active (core))
        data->active = FALSE;

      child_data = _duration_limit_data_list_with_data (self, data);

      for (tmp = child_data; tmp; tmp = tmp->next) {
        data = tmp->data;
        if (data->priority >= new_prio)
          data->priority++;
      }

      if (!_can_update_duration_limit (self, child_data, &error)) {
        GST_INFO_OBJECT (self, "Cannot add effect %" GES_FORMAT " as "
            "a child because the duration-limit cannot be adjusted",
            GES_ARGS (element));
        goto done;
      }
    }

    _update_active_for_track (self, track_el);

    priv->nb_effects++;

    GST_DEBUG_OBJECT (self, "Adding %ith effect: %" GES_FORMAT
        " Priority %i", priv->nb_effects, GES_ARGS (element), new_prio);

    if (adding_scale_effect) {
      GST_DEBUG_OBJECT (self, "Adding scaling effect to clip "
          "%" GES_FORMAT, GES_ARGS (self));
      priv->nb_scale_effects += 1;
    }

    /* changing priorities, and updating their offset */
    priv->prevent_resort = TRUE;
    priv->setting_priority = TRUE;
    priv->prevent_duration_limit_update = TRUE;
    priv->prevent_children_outpoint_update = TRUE;

    /* increase the priority of anything with a lower priority */
    for (tmp = container->children; tmp; tmp = tmp->next) {
      GESTimelineElement *child = tmp->data;
      if (child->priority >= new_prio)
        ges_timeline_element_set_priority (child, child->priority + 1);
    }
    _set_priority0 (element, new_prio);

    priv->prevent_resort = FALSE;
    priv->setting_priority = FALSE;
    priv->prevent_duration_limit_update = prev_prevent;
    priv->prevent_children_outpoint_update = prev_prevent_outpoint;
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
    goto done;
  }

  _set_start0 (element, GES_TIMELINE_ELEMENT_START (self));
  _set_duration0 (element, GES_TIMELINE_ELEMENT_DURATION (self));

  ret = TRUE;

done:
  if (error)
    ges_clip_set_add_error (self, error);

  return ret;
}

void
ges_clip_take_remove_error (GESClip * clip, GError ** error)
{
  GESClipPrivate *priv = clip->priv;

  g_clear_error (error);
  if (error) {
    if (*error) {
      GST_ERROR ("Error not handled: %s", (*error)->message);
      g_error_free (*error);
    }
    *error = priv->remove_error;
  } else {
    g_clear_error (&priv->remove_error);
  }
  priv->remove_error = NULL;
}

void
ges_clip_set_remove_error (GESClip * clip, GError * error)
{
  GESClipPrivate *priv = clip->priv;

  g_clear_error (&priv->remove_error);
  priv->remove_error = error;
}

gboolean
ges_clip_has_scale_effect (GESClip * clip)
{
  return clip->priv->nb_scale_effects > 0;
}

static gboolean
_remove_child (GESContainer * container, GESTimelineElement * element)
{
  GESTrackElement *el = GES_TRACK_ELEMENT (element);
  GESClip *self = GES_CLIP (container);
  GESClipPrivate *priv = self->priv;

  /* check that the duration-limit can be changed */
  /* If we are removing a core child, then all other children in the
   * same track will be removed from the track, which will make the
   * duration-limit increase, which is safe
   * Similarly, if it has no track, the duration-limit will not change */
  if (!priv->allow_any_remove && !_IS_CORE_CHILD (element) &&
      ges_track_element_get_track (el)) {
    GList *child_data = NULL;
    GList *tmp;
    GError *error = NULL;

    for (tmp = container->children; tmp; tmp = tmp->next) {
      GESTrackElement *child = tmp->data;
      if (child == el)
        continue;
      child_data =
          g_list_prepend (child_data, _duration_limit_data_new (child));
    }

    if (!_can_update_duration_limit (self, child_data, &error)) {
      ges_clip_set_remove_error (self, error);
      GST_INFO_OBJECT (self, "Cannot remove the child %" GES_FORMAT
          " because the duration-limit cannot be adjusted", GES_ARGS (el));
      return FALSE;
    }
  }

  /* NOTE: notifies are currently frozen by ges_container_add */
  if (_IS_TOP_EFFECT (element)) {
    GList *tmp;
    gboolean prev_prevent = priv->prevent_duration_limit_update;
    gboolean prev_prevent_outpoint = priv->prevent_children_outpoint_update;
    GST_DEBUG_OBJECT (container, "Resyncing effects priority.");

    /* changing priorities, so preventing a re-sort */
    priv->prevent_resort = TRUE;
    priv->setting_priority = TRUE;
    priv->prevent_duration_limit_update = TRUE;
    priv->prevent_children_outpoint_update = TRUE;
    for (tmp = container->children; tmp; tmp = tmp->next) {
      guint32 sibling_prio = GES_TIMELINE_ELEMENT_PRIORITY (tmp->data);
      if (sibling_prio > element->priority)
        ges_timeline_element_set_priority (GES_TIMELINE_ELEMENT (tmp->data),
            sibling_prio - 1);
    }
    priv->nb_effects--;
    priv->prevent_resort = FALSE;
    priv->setting_priority = FALSE;
    priv->prevent_duration_limit_update = prev_prevent;
    priv->prevent_children_outpoint_update = prev_prevent_outpoint;
    /* no need to re-sort the children since the rest keep the same
     * relative priorities */
    /* height may have changed */
    _compute_height (container);

    if (GES_IS_EFFECT (element)) {
      GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (element));
      const gchar *bindesc = ges_asset_get_id (asset);

      if (bindesc && !strstr (bindesc, "gesvideoscale")) {
        GST_DEBUG_OBJECT (self, "Removing scaling effect to clip "
            "%" GES_FORMAT, GES_ARGS (self));
        priv->nb_scale_effects -= 1;
      }
    }
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

  if (GES_IS_TIME_EFFECT (element))
    g_signal_connect (element, "deep-notify",
        G_CALLBACK (_child_time_property_changed_cb), self);

  if (_IS_CORE_CHILD (element))
    _update_max_duration (container);

  _update_duration_limit (self);
  _update_children_outpoints (self);
}

static void
_child_removed (GESContainer * container, GESTimelineElement * element)
{
  GESClip *self = GES_CLIP (container);

  g_signal_handlers_disconnect_by_func (element, _child_property_changed_cb,
      self);
  /* NOTE: we do not test if the effect is a time effect since technically
   * it can stop being a time effect, although this would be rare */
  g_signal_handlers_disconnect_by_func (element,
      _child_time_property_changed_cb, self);

  if (_IS_CORE_CHILD (element))
    _update_max_duration (container);

  _update_duration_limit (self);
  _update_children_outpoints (self);
  ges_track_element_update_outpoint (GES_TRACK_ELEMENT (element));
}

static void
add_clip_to_list (gpointer key, gpointer clip, GList ** list)
{
  *list = g_list_append (*list, gst_object_ref (clip));
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
  gboolean prev_prevent_outpoint_from =
      from_clip->priv->prevent_children_outpoint_update;
  gboolean prev_prevent_outpoint_to =
      to_clip->priv->prevent_children_outpoint_update;

  /* We need to bump the refcount to avoid the object to be destroyed */
  gst_object_ref (child);

  /* don't want to change tracks */
  ges_timeline_set_moving_track_elements (timeline, TRUE);

  from_clip->priv->prevent_duration_limit_update = TRUE;
  to_clip->priv->prevent_duration_limit_update = TRUE;
  from_clip->priv->prevent_children_outpoint_update = TRUE;
  to_clip->priv->prevent_children_outpoint_update = TRUE;

  from_clip->priv->allow_any_remove = TRUE;
  ges_container_remove (GES_CONTAINER (from_clip),
      GES_TIMELINE_ELEMENT (child));
  from_clip->priv->allow_any_remove = FALSE;

  to_clip->priv->allow_any_track = TRUE;
  if (!ges_container_add (GES_CONTAINER (to_clip),
          GES_TIMELINE_ELEMENT (child)))
    GST_ERROR ("%" GES_FORMAT " could not add child %p while"
        " transfering, this should never happen", GES_ARGS (to_clip), child);
  to_clip->priv->allow_any_track = FALSE;
  ges_timeline_set_moving_track_elements (timeline, FALSE);

  from_clip->priv->prevent_duration_limit_update = prev_prevent_from;
  to_clip->priv->prevent_duration_limit_update = prev_prevent_to;
  from_clip->priv->prevent_children_outpoint_update =
      prev_prevent_outpoint_from;
  to_clip->priv->prevent_children_outpoint_update = prev_prevent_outpoint_to;

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

    if (layer)
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
  gboolean prev_prevent_outpoint = clip->priv->prevent_children_outpoint_update;

  if (track == NULL)
    return;
  /* allow us to remove in any order */
  clip->priv->allow_any_track = TRUE;
  clip->priv->prevent_duration_limit_update = TRUE;
  clip->priv->prevent_children_outpoint_update = TRUE;

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
  clip->priv->prevent_children_outpoint_update = prev_prevent_outpoint;
  _update_duration_limit (clip);
  _update_children_outpoints (clip);
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

  self->priv->allow_any_remove = TRUE;

  g_list_free_full (self->priv->copied_track_elements, gst_object_unref);
  self->priv->copied_track_elements = NULL;
  g_clear_object (&self->priv->copied_layer);

  g_clear_error (&self->priv->add_error);
  self->priv->add_error = NULL;
  g_clear_error (&self->priv->remove_error);
  self->priv->remove_error = NULL;

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
   * #GESTimelineElement:max-duration, #GESTrackElement:active, and
   * #GESTrackElement:track properties of its children, as well as any
   * time effects. If there is no limit, this will be set to
   * #GST_CLOCK_TIME_NONE.
   *
   * Note that whilst a clip has no children in any tracks, the limit will
   * be unknown, and similarly set to #GST_CLOCK_TIME_NONE.
   *
   * If the duration-limit would ever go below the current
   * #GESTimelineElement:duration of the clip due to a change in the above
   * variables, its #GESTimelineElement:duration will be set to the new
   * limit.
   *
   * Since: 1.18
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
 * ges_clip_move_to_layer_full:
 * @clip: A #GESClip
 * @layer: The new layer
 * @error: (nullable): Return location for an error
 *
 * Moves a clip to a new layer. If the clip already exists in a layer, it
 * is first removed from its current layer before being added to the new
 * layer.
 *
 * Returns: %TRUE if @clip was successfully moved to @layer.
 * Since: 1.18
 */
gboolean
ges_clip_move_to_layer_full (GESClip * clip, GESLayer * layer, GError ** error)
{
  gboolean ret = FALSE;
  GESLayer *current_layer;
  GESTimelineElement *element;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

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
        (gint64) ges_layer_get_priority (layer), 0, GES_EDGE_NONE, 0, error);
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
 * ges_clip_move_to_layer:
 * @clip: A #GESClip
 * @layer: The new layer
 *
 * See ges_clip_move_to_layer_full(), which also gives an error.
 *
 * Returns: %TRUE if @clip was successfully moved to @layer.
 */
gboolean
ges_clip_move_to_layer (GESClip * clip, GESLayer * layer)
{
  return ges_clip_move_to_layer_full (clip, layer, NULL);
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
 * Since: 1.18
 */
GstClockTime
ges_clip_get_duration_limit (GESClip * clip)
{
  g_return_val_if_fail (GES_IS_CLIP (clip), GST_CLOCK_TIME_NONE);

  return clip->priv->duration_limit;
}

static gint
_cmp_children_by_priority (gconstpointer a_p, gconstpointer b_p)
{
  const GESTimelineElement *a = a_p, *b = b_p;
  if (a->priority > b->priority)
    return 1;
  else if (a->priority < b->priority)
    return -1;
  return 0;
}

/**
 * ges_clip_add_top_effect:
 * @clip: A #GESClip
 * @effect: A top effect to add
 * @index: The index to add @effect at, or -1 to add at the highest,
 *         see #ges_clip_get_top_effect_index for more information
 * @error: (nullable): Return location for an error
 *
 * Add a top effect to a clip at the given index.
 *
 * Unlike using ges_container_add(), this allows you to set the index
 * in advance. It will also check that no error occurred during the track
 * selection for the effect.
 *
 * Note, only subclasses of #GESClipClass that have
 * #GES_CLIP_CLASS_CAN_ADD_EFFECTS set to %TRUE (such as #GESSourceClip
 * and #GESBaseEffectClip) can have additional top effects added.
 *
 * Note, if the effect is a time effect, this may be refused if the clip
 * would not be able to adapt itself once the effect is added.
 *
 * Returns: %TRUE if @effect was successfully added to @clip at @index.
 * Since: 1.18
 */
gboolean
ges_clip_add_top_effect (GESClip * clip, GESBaseEffect * effect, gint index,
    GError ** error)
{
  GESClipPrivate *priv;
  GList *top_effects;
  GESTimelineElement *replace;
  GESTimeline *timeline;
  gboolean res;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  priv = clip->priv;

  if (index >= 0) {
    top_effects = ges_clip_get_top_effects (clip);
    replace = g_list_nth_data (top_effects, index);
    if (replace) {
      priv->use_effect_priority = TRUE;
      priv->effect_priority = replace->priority;
    }
    g_list_free_full (top_effects, gst_object_unref);
  }
  /* otherwise the default _add_child will place it at the lowest
   * priority / highest index */


  timeline = GES_TIMELINE_ELEMENT_TIMELINE (clip);
  if (timeline)
    ges_timeline_set_track_selection_error (timeline, FALSE, NULL);

  /* note, if several tracks are selected, this may lead to several
   * effects being added to the clip.
   * The first effect we are adding will use the set effect_priority.
   * The error on the timeline could be from any of the copies */
  ges_clip_set_add_error (clip, NULL);
  res = ges_container_add (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (effect));

  priv->use_effect_priority = FALSE;

  if (!res) {
    /* if adding fails, there should have been no track selection, which
     * means no other elements were added so the clip, so the adding error
     * for the effect, if any, should still be available on the clip */
    ges_clip_take_add_error (clip, error);
    return FALSE;
  }

  if (timeline && ges_timeline_take_track_selection_error (timeline, error))
    goto remove;

  return TRUE;

remove:
  if (!ges_container_remove (GES_CONTAINER (clip),
          GES_TIMELINE_ELEMENT (effect)))
    GST_ERROR_OBJECT (clip, "Failed to remove effect %" GES_FORMAT,
        GES_ARGS (effect));

  return FALSE;
}

static gboolean
_is_added_effect (GESClip * clip, GESBaseEffect * effect)
{
  if (GES_TIMELINE_ELEMENT_PARENT (effect) != GES_TIMELINE_ELEMENT (clip)) {
    GST_WARNING_OBJECT (clip, "The effect %" GES_FORMAT
        " does not belong to this clip", GES_ARGS (effect));
    return FALSE;
  }
  if (!_IS_TOP_EFFECT (effect)) {
    GST_WARNING_OBJECT (clip, "The effect %" GES_FORMAT " is not a top "
        "effect of this clip (it is a core element of the clip)",
        GES_ARGS (effect));
    return FALSE;
  }
  return TRUE;
}

/**
 * ges_clip_remove_top_effect:
 * @clip: A #GESClip
 * @effect: The top effect to remove
 * @error: (nullable): Return location for an error
 *
 * Remove a top effect from the clip.
 *
 * Note, if the effect is a time effect, this may be refused if the clip
 * would not be able to adapt itself once the effect is removed.
 *
 * Returns: %TRUE if @effect was successfully added to @clip at @index.
 * Since: 1.18
 */
gboolean
ges_clip_remove_top_effect (GESClip * clip, GESBaseEffect * effect,
    GError ** error)
{
  gboolean res;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);
  if (!_is_added_effect (clip, effect))
    return FALSE;

  ges_clip_set_remove_error (clip, NULL);
  res = ges_container_remove (GES_CONTAINER (clip),
      GES_TIMELINE_ELEMENT (effect));
  if (!res)
    ges_clip_take_remove_error (clip, error);

  return res;
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

  return g_list_sort (ret, _cmp_children_by_priority);
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
  GList *top_effects;
  gint ret;

  g_return_val_if_fail (GES_IS_CLIP (clip), -1);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), -1);
  if (!_is_added_effect (clip, effect))
    return -1;

  top_effects = ges_clip_get_top_effects (clip);
  ret = g_list_index (top_effects, effect);
  g_list_free_full (top_effects, gst_object_unref);

  return ret;
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
 * ges_clip_set_top_effect_index_full:
 * @clip: A #GESClip
 * @effect: An effect within @clip to move
 * @newindex: The index for @effect in @clip
 * @error: (nullable): Return location for an error
 *
 * Set the index of an effect within the clip. See
 * ges_clip_get_top_effect_index(). The new index must be an existing
 * index of the clip. The effect is moved to the new index, and the other
 * effects may be shifted in index accordingly to otherwise maintain the
 * ordering.
 *
 * Returns: %TRUE if @effect was successfully moved to @newindex.
 * Since: 1.18
 */
gboolean
ges_clip_set_top_effect_index_full (GESClip * clip, GESBaseEffect * effect,
    guint newindex, GError ** error)
{
  gint inc;
  GList *top_effects, *tmp;
  GList *child_data = NULL;
  guint32 current_prio, new_prio;
  GESTimelineElement *element, *replace;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  if (!_is_added_effect (clip, effect))
    return FALSE;

  element = GES_TIMELINE_ELEMENT (effect);

  top_effects = ges_clip_get_top_effects (clip);
  replace = g_list_nth_data (top_effects, newindex);
  g_list_free_full (top_effects, gst_object_unref);

  if (!replace) {
    GST_WARNING_OBJECT (clip, "Does not contain %u effects", newindex + 1);
    return FALSE;
  }

  if (replace == element)
    return TRUE;

  current_prio = element->priority;
  new_prio = replace->priority;

  if (current_prio < new_prio)
    inc = -1;
  else
    inc = +1;

  /* check that the duration-limit can be changed */
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;
    guint32 priority = child->priority;
    DurationLimitData *data =
        _duration_limit_data_new (GES_TRACK_ELEMENT (child));

    if (child == element)
      data->priority = new_prio;
    else if ((inc == +1 && priority >= new_prio && priority < current_prio)
        || (inc == -1 && priority <= new_prio && priority > current_prio))
      data->priority = child->priority + inc;

    child_data = g_list_prepend (child_data, data);
  }

  if (!_can_update_duration_limit (clip, child_data, error)) {
    GST_INFO_OBJECT (clip, "Cannot move top effect %" GES_FORMAT
        " to index %i because the duration-limit cannot adjust",
        GES_ARGS (effect), newindex);
    return FALSE;
  }

  GST_DEBUG_OBJECT (clip, "Setting top effect %" GST_PTR_FORMAT "priority: %i",
      effect, new_prio);

  /* prevent a re-sort of the list whilst we are traversing it! */
  clip->priv->prevent_resort = TRUE;
  clip->priv->setting_priority = TRUE;
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;
    guint32 priority = child->priority;

    if (child == element)
      continue;

    /* only need to change the priority for those between the new and old
     * index */
    if ((inc == +1 && priority >= new_prio && priority < current_prio)
        || (inc == -1 && priority <= new_prio && priority > current_prio))
      _set_priority0 (child, priority + inc);
  }
  _set_priority0 (element, new_prio);

  clip->priv->prevent_resort = FALSE;
  clip->priv->setting_priority = FALSE;
  _ges_container_sort_children (GES_CONTAINER (clip));
  /* height should have stayed the same */

  return TRUE;
}

/**
 * ges_clip_set_top_effect_index:
 * @clip: A #GESClip
 * @effect: An effect within @clip to move
 * @newindex: The index for @effect in @clip
 *
 * See ges_clip_set_top_effect_index_full(), which also gives an error.
 *
 * Returns: %TRUE if @effect was successfully moved to @newindex.
 */
gboolean
ges_clip_set_top_effect_index (GESClip * clip, GESBaseEffect * effect,
    guint newindex)
{
  return ges_clip_set_top_effect_index_full (clip, effect, newindex, NULL);
}

/**
 * ges_clip_split_full:
 * @clip: The #GESClip to split
 * @position: The timeline position at which to perform the split, between
 * the start and end of the clip
 * @error: Return location for an error
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
 *
 * Since: 1.18
 */
GESClip *
ges_clip_split_full (GESClip * clip, guint64 position, GError ** error)
{
  GList *tmp, *transitions = NULL;
  GESClip *new_object;
  gboolean no_core = FALSE;
  GstClockTime start, duration, old_duration, new_duration, new_inpoint;
  GESTimelineElement *element;
  GESTimeline *timeline;
  GHashTable *track_for_copy;
  guint32 layer_prio;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (clip->priv->layer, NULL);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (position), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

  element = GES_TIMELINE_ELEMENT (clip);
  timeline = element->timeline;

  duration = element->duration;
  start = element->start;

  if (position >= start + duration || position <= start) {
    GST_WARNING_OBJECT (clip, "Can not split %" GST_TIME_FORMAT
        " out of boundaries", GST_TIME_ARGS (position));
    return NULL;
  }

  layer_prio = ges_timeline_element_get_layer_priority (element);

  old_duration = position - start;
  new_duration = duration + start - position;
  /* convert the split position into an internal core time */
  new_inpoint = _convert_core_time (clip, position, FALSE, &no_core, error);

  /* if the split clip does not contain any active core elements with
   * an internal source, just set the in-point to 0 for the new_object */
  if (no_core)
    new_inpoint = 0;

  if (!GST_CLOCK_TIME_IS_VALID (new_inpoint))
    return NULL;

  if (timeline
      && !timeline_tree_can_move_element (timeline_get_tree (timeline), element,
          layer_prio, start, old_duration, error)) {
    GST_INFO_OBJECT (clip,
        "Can not split %" GES_FORMAT " at %" GST_TIME_FORMAT
        " as timeline would be in an illegal state.", GES_ARGS (clip),
        GST_TIME_ARGS (position));
    return NULL;
  }

  if (timeline
      && !timeline_tree_can_move_element (timeline_get_tree (timeline), element,
          layer_prio, position, new_duration, error)) {
    GST_INFO_OBJECT (clip,
        "Can not split %" GES_FORMAT " at %" GST_TIME_FORMAT
        " as timeline would be in an illegal state.", GES_ARGS (clip),
        GST_TIME_ARGS (position));
    return NULL;
  }

  GST_DEBUG_OBJECT (clip, "Spliting at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (position));

  /* Create the new Clip */
  new_object = GES_CLIP (ges_timeline_element_copy (element, FALSE));
  new_object->priv->prevent_duration_limit_update = TRUE;
  new_object->priv->prevent_children_outpoint_update = TRUE;

  GST_DEBUG_OBJECT (new_object, "New 'splitted' clip");
  /* Set new timing properties on the Clip */
  _set_start0 (GES_TIMELINE_ELEMENT (new_object), position);
  _set_inpoint0 (GES_TIMELINE_ELEMENT (new_object), new_inpoint);
  _set_duration0 (GES_TIMELINE_ELEMENT (new_object), new_duration);

  /* NOTE: it is technically possible that the new_object may shrink
   * later on in this method if the clip contains any non-linear time
   * effects, which cause the duration-limit to drop. However, this
   * should be safe since we have already checked with timeline-tree
   * that the split position is not in the middle of an overlap. This
   * means that the new_object should only be overlapping another
   * element on its end, which makes shrinking safe.
   *
   * The original clip, however, should not shrink if the time effects
   * obey the property that they do not depend on how much data they
   * receive, which should be true for the time effects supported by GES.
   */

  /* split binding before duration changes since shrinking can destroy
   * binding values */
  track_for_copy = g_hash_table_new_full (NULL, NULL,
      gst_object_unref, gst_object_unref);

  /* _add_child will add core elements at the lowest priority and new
   * non-core effects at the lowest effect priority, so we need to add the
   * highest priority children first to preserve the effect order. The
   * clip's children are already ordered by highest priority first. */
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *copy, *orig = tmp->data;
    GESTrack *track = ges_track_element_get_track (orig);
    GESAutoTransition *trans;
    gchar *meta;

    copy = ges_clip_copy_track_element_into (new_object, orig, new_inpoint);

    if (!copy)
      continue;

    if (track)
      g_hash_table_insert (track_for_copy, gst_object_ref (copy),
          gst_object_ref (track));

    meta = ges_meta_container_metas_to_string (GES_META_CONTAINER (orig));
    ges_meta_container_add_metas_from_string (GES_META_CONTAINER (copy), meta);
    g_free (meta);

    trans = timeline ?
        ges_timeline_get_auto_transition_at_edge (timeline, orig,
        GES_EDGE_END) : NULL;

    if (trans) {
      trans->frozen = TRUE;
      ges_auto_transition_set_source (trans, copy, GES_EDGE_START);
      transitions = g_list_append (transitions, trans);
    }
  }

  GES_TIMELINE_ELEMENT_SET_BEING_EDITED (clip);
  _set_duration0 (GES_TIMELINE_ELEMENT (clip), old_duration);
  GES_TIMELINE_ELEMENT_UNSET_BEING_EDITED (clip);

  /* We do not want the timeline to create again TrackElement-s */
  ges_clip_set_moving_from_layer (new_object, TRUE);
  /* adding to the same layer should not fail when moving */
  ges_layer_add_clip (clip->priv->layer, new_object);
  ges_clip_set_moving_from_layer (new_object, FALSE);

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

  for (tmp = transitions; tmp; tmp = tmp->next) {
    GESAutoTransition *trans = tmp->data;
    trans->frozen = FALSE;
    ges_auto_transition_update (trans);
  }

  g_hash_table_unref (track_for_copy);
  g_list_free_full (transitions, gst_object_unref);

  new_object->priv->prevent_duration_limit_update = FALSE;
  new_object->priv->prevent_children_outpoint_update = FALSE;
  _update_duration_limit (new_object);
  _update_children_outpoints (new_object);

  return new_object;
}

/**
 * ges_clip_split:
 * @clip: The #GESClip to split
 * @position: The timeline position at which to perform the split
 *
 * See ges_clip_split_full(), which also gives an error.
 *
 * Returns: (transfer none) (nullable): The newly created clip resulting
 * from the splitting @clip, or %NULL if @clip can't be split.
 */
GESClip *
ges_clip_split (GESClip * clip, guint64 position)
{
  return ges_clip_split_full (clip, position, NULL);
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
 * Returns: (transfer none) (nullable): The newly created element, or
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

/* Convert from an internal time of a child within a clip to a
 * ===========================================================
 * timeline time
 * =============
 *
 * Given an internal time T for some child in a clip, we want to know
 * what the corresponding time in the timeline is.
 *
 * If the time T is between the in-point and out-point of the child,
 * then we can convert to the timeline coordinates by answering:
 *
 * a) "What is the timeline time at which the internal data from the child
 * found at time T appears in the timeline output?"
 *
 * If the time T is after the out-point of the child, we instead want to
 * answer:
 *
 * b) "If we extended the clip indefinetly in the timeline, what would be
 * the timeline time at which the internal data from the child found at
 * time T would appear in the timeline output?"
 *
 * However, if the time T is before the in-point of the child, we instead
 * want to answer a more subtle question:
 *
 * c) "If we set the 'in-point' of the child to T, what would we need to
 * set the 'start' of the clip to such that the internal data from the
 * child currently found at the *beginning* of the clip would then appear
 * at the same timeline time?"
 *
 * E.g. consider the following children of a clip, all in the same track,
 * and all active:
 *                                T
 *                                :
 *          +=====================:======+
 *          |                   _/ \_    |
 *          |         source   ~(o_o)~   |
 *          |                   / @ \    |
 *          +=====================:======+
 *          i                     :
 *                                :
 *          +=====================:======+
 *          |       time-effect0  :      |  | g0
 *          +=====================:======+  v
 *                                :
 *          +=====================:======+
 *          |         overlay     :      |
 *          +=====================:======+
 *          i'                    :
 *                                :
 *          +=====================:======+
 *          |       time-effect1  :      |  | g1
 *          +=====================:======+  v
 *                                :
 * -------------------------------:-------------------timeline
 *          S                     X
 *
 * where i is the in-point of the source and i' is the in-point of the
 * overlay. Also, g0 is the sink_to_source translation function for the
 * first time effect, and g1 is the same for the second. S is the start of
 * the clip. The ~(o_o)~ figure is the data that appears in the source at
 * T.
 *
 * Essentially, question a) wants us to convert from the time T, where the
 * data is, which is in the internal time coordinates of the source, to
 * the timeline time X. First, we subtract i to convert from the internal
 * source coordinates of the source to the external source coordinates of
 * the source, then we apply the sink_to_source translation functions,
 * which act on external source coordinates, then add 'start' to finally
 * convert to the timeline coordinates. So overall we have
 *
 *   X = S + g1(g0(T - i))
 *
 * To answer b), T would be beyond the end of the clip. Since g1 and g0
 * can convert beyond the end time, we similarly compute
 *
 *   X = S + g1(g0(T - i))
 *
 * The user themselves should note that this could exceed the max-duration
 * of any of the children.
 *
 * Now consider
 *
 *    T
 *    :
 *    :     +============================+
 *    :      \_                          |
 *    :     _o)~        source           |
 *    :     @ \                          |
 *    :     +============================+
 *    :     i
 *    :
 *    :     +============================+
 *    :     |       time-effect0         |  | g0
 *    :     +============================+  v
 *    :
 *    :     +============================+
 *    :     |           overlay          |
 *    :     +============================+
 *    :     i'
 *    :
 *    :     +============================+
 *    :     |       time-effect1         |  | g1
 *    :     +============================+  v
 *    :
 * ---:-----------------------------------------------timeline
 *    X     S
 *
 * To do the same as a), we would need to be able to convert from T to X,
 * but this isn't defined since the children do not extend to here. More
 * specifically, the functions g0 and g1 are not defined for negative
 * times. Instead, we want to answer question c). That is, we want to know
 * what we should set the start of the clip to to keep the figure at the
 * same timeline position if we change the in-point of the source to T.
 *
 * First, if we set the in-point to T, then we would have
 *
 *          T
 *          :
 *          +============================+
 *          |   _/ \_                    |
 *          |  ~(o_o)~        source     |
 *          |   / @ \                    |
 *          +============================+
 *          :     i
 *          :     :
 *          +=====:======================+
 *          |     :       time-effect0   |  | g0
 *          +=====:======================+  v
 *          :     :
 *          +=====:======================+
 *          |     :           overlay    |
 *          +=====:======================+
 *          :     :
 *          +=====:======================+
 *          |     :       time-effect1   |  | g1
 *          +=====:======================+  v
 *          :     :
 * ---:-----:-----:-----------------------------------timeline
 *    X     S     Y
 *
 * In order to make the figure appear at 'start' again, we would need to
 * reduce the start of the clip by the difference between S and Y, where
 * Y is the conversion of the previous in-point i to the timeline time.
 *
 * Thus,
 *
 *   X = S - (Y - S)
 *     = S - (S + g1(g0(i - T)) - S)
 *     = S - g1(g0(i - T))
 *
 * If this would be negative, the conversion will not be possible.
 *
 * Note, we are relying on the *assumption* that the translation functions
 * *do not* change when we change the in-point. GESBaseEffect only claims
 * to support such time effects.
 *
 * Note that if g0 and g1 are simply identities, and we translate the
 * internal time using a) and b), we calculate
 *
 *   S + (T - i)
 *
 * and for c), we calculate
 *
 *   S - (i - T) = S + (T - i)
 *
 * In summary, if we are converting from internal time T to a timeline
 * time the return is
 *
 *   G(T) = {  S + g1(g0(T - i))   if T >= i,
 *          {  S - g1(g0(i - T))   otherwise.
 *
 * Note that the overlay did not play a role since it overall translates
 * all received times by the identity. Note that we could similarly want
 * to convert from an internal time in the overlay to the timeline time.
 * This would be given by
 *
 *   S + g1(T - i')   if T >= i',
 *   S - g1(i' - T)   otherwise.
 *
 *
 * Convert from a timeline time to an internal time of a child
 * ===========================================================
 * in a clip
 * =========
 *
 * We basically want to reverse the previous conversion. Specifically,
 * when the timeline time X is between the start and end of the clip we
 * want to answer:
 *
 * d) "What is the internal time at which the data from the child that
 * appears in the timeline at time X is created in the child?"
 *
 * If the time X is after the end of the clip, we instead want to answer:
 *
 * e) "If we extended the clip indefinetly in the timeline, what would be
 * the internal time at which the data from the child that appears in the
 * timeline at time T would be created in the child?"
 *
 * However, if the time X is before the start of the child, we instead
 * want to answer:
 *
 * f) "If we set the 'start' of the clip to X, what would we need to
 * set the 'in-point' of the clip to such that the internal data from the
 * child currently found at the *beginning* of the clip would then appear
 * at the same timeline time?"
 *
 * Following the same arguments, these would all be answered by
 *
 *   F(X) = {  i + f0(f1(X - S))   if X >= S,
 *          {  i - f0(f1(S - X))   otherwise.
 *
 * where f0 and f1 are the corresponding source_to_sink translation
 * functions, which should be close reverses of g0 and g1, respectively.
 *
 * Note that this does indeed reverse the internal to timeline conversion:
 *
 *   F(G(T)) = {  i + f0(f1(G(T) - S))   if G(T) >= S,
 *             {  i - f0(f1(S - G(T)))   otherwise.
 *
 * but, since g1 and g0 map from [0,inf) to [0,inf),
 *
 *   G(T) - S = {  + g1(g0(T - i))   if T >= i,
 *              {  - g1(g0(i - T))   otherwise.
 *            { >= 0                 if T >= i,
 *            { = 0                  if (T < i and g1(g0(i - T)) = 0)
 *            { < 0                  otherwise.
 *
 * =>   ( G(T) >= S  <==>  T >= i or (T < i and g1(g0(i - T)) = 0) )
 *
 * therefore
 *   F(G(T)) = {  i + f0(f1(g1(g0(T - i))))   if T >= i,
 *             {  i + f0(f1(0))               if T < i
 *             {                              and g1(g0(i - T)) = 0,
 *             {  i - f0(f1(g1(g0(i - T))))   otherwise
 *
 *           = {  i + f0(f1(g1(g0(T - i))))   if T >= i,
 *             {  i - f0(f1(g1(g0(i - T))))   otherwise
 *
 *           = T
 *
 * because f1 reverses g1, and f0 reverses g0.
 */

/* returns higher priority first */
static GList *
_active_time_effects_in_track_after_priority (GESClip * clip,
    GESTrack * track, guint32 priority)
{
  GList *tmp, *list = NULL;

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;

    if (GES_IS_TIME_EFFECT (child)
        && ges_track_element_get_track (child) == track
        && ges_track_element_is_active (child)
        && _PRIORITY (child) < priority)
      list = g_list_prepend (list, child);
  }

  return g_list_sort (list, _cmp_children_by_priority);
}

/**
 * ges_clip_get_timeline_time_from_internal_time:
 * @clip: A #GESClip
 * @child: An #GESTrackElement:active child of @clip with a
 * #GESTrackElement:track
 * @internal_time: A time in the internal time coordinates of @child
 * @error: (nullable): Return location for an error
 *
 * Convert the internal source time from the child to a timeline time.
 * This will take any time effects placed on the clip into account (see
 * #GESBaseEffect for what time effects are supported, and how to
 * declare them in GES).
 *
 * When @internal_time is above the #GESTimelineElement:in-point of
 * @child, this will return the timeline time at which the internal
 * content found at @internal_time appears in the output of the timeline's
 * track. For example, this would let you know where in the timeline a
 * particular scene in a media file would appear.
 *
 * This will be done assuming the clip has an indefinite end, so the
 * timeline time may be beyond the end of the clip, or even breaking its
 * #GESClip:duration-limit.
 *
 * If, instead, @internal_time is below the current
 * #GESTimelineElement:in-point of @child, this will return what you would
 * need to set the #GESTimelineElement:start of @clip to if you set the
 * #GESTimelineElement:in-point of @child to @internal_time and wanted to
 * keep the content of @child currently found at the current
 * #GESTimelineElement:start of @clip at the same timeline position. If
 * this would be negative, the conversion fails. This is useful for
 * determining what position to use in a #GES_EDIT_MODE_TRIM if you wish
 * to trim to a specific point in the internal content, such as a
 * particular scene in a media file.
 *
 * Note that whilst a clip has no time effects, this second return is
 * equivalent to finding the timeline time at which the content of @child
 * at @internal_time would be found in the timeline if it had indefinite
 * extent in both directions. However, with non-linear time effects this
 * second return will be more distinct.
 *
 * In either case, the returned time would be appropriate to use in
 * ges_timeline_element_edit() for #GES_EDIT_MODE_TRIM, and similar, if
 * you wish to use a particular internal point as a reference. For
 * example, you could choose to end a clip at a certain internal
 * 'out-point', similar to the #GESTimelineElement:in-point, by
 * translating the desired end time into the timeline coordinates, and
 * using this position to trim the end of a clip.
 *
 * See ges_clip_get_internal_time_from_timeline_time(), which performs the
 * reverse, or ges_clip_get_timeline_time_from_source_frame() which does
 * the same conversion, but using frame numbers.
 *
 * Returns: The time in the timeline coordinates corresponding to
 * @internal_time, or #GST_CLOCK_TIME_NONE if the conversion could not be
 * performed.
 *
 * Since: 1.18
 */
GstClockTime
ges_clip_get_timeline_time_from_internal_time (GESClip * clip,
    GESTrackElement * child, GstClockTime internal_time, GError ** error)
{
  GstClockTime inpoint, start, external_time;
  gboolean decrease;
  GESTrack *track;
  GList *tmp, *time_effects;

  g_return_val_if_fail (GES_IS_CLIP (clip), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (child), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (!error || !*error, GST_CLOCK_TIME_NONE);

  if (!g_list_find (GES_CONTAINER_CHILDREN (clip), child)) {
    GST_WARNING_OBJECT (clip, "The track element %" GES_FORMAT " is not "
        "a child of the clip", GES_ARGS (child));
    return GST_CLOCK_TIME_NONE;
  }

  track = ges_track_element_get_track (child);

  if (!track) {
    GST_WARNING_OBJECT (clip, "Cannot convert the internal time of the "
        "child %" GES_FORMAT " to a timeline time because it is not part "
        "of a track", GES_ARGS (child));
    return GST_CLOCK_TIME_NONE;
  }

  if (!ges_track_element_is_active (child)) {
    GST_WARNING_OBJECT (clip, "Cannot convert the internal time of the "
        "child %" GES_FORMAT " to a timeline time because it is not "
        "active in its track", GES_ARGS (child));
    return GST_CLOCK_TIME_NONE;
  }

  if (internal_time == GST_CLOCK_TIME_NONE)
    return GST_CLOCK_TIME_NONE;

  inpoint = _INPOINT (child);
  if (inpoint <= internal_time) {
    decrease = FALSE;
    external_time = internal_time - inpoint;
  } else {
    decrease = TRUE;
    external_time = inpoint - internal_time;
  }

  time_effects = _active_time_effects_in_track_after_priority (clip, track,
      _PRIORITY (child));

  /* currently ordered with highest priority (closest to the timeline)
   * first, with @child being at the *end* of the list.
   * Want to reverse this so we can convert from the child towards the
   * timeline */
  time_effects = g_list_reverse (time_effects);

  for (tmp = time_effects; tmp; tmp = tmp->next) {
    GESBaseEffect *effect = tmp->data;
    GHashTable *values = ges_base_effect_get_time_property_values (effect);

    external_time = ges_base_effect_translate_sink_to_source_time (effect,
        external_time, values);
    g_hash_table_unref (values);
  }

  g_list_free (time_effects);

  if (!GST_CLOCK_TIME_IS_VALID (external_time))
    return GST_CLOCK_TIME_NONE;

  start = _START (clip);

  if (!decrease)
    return start + external_time;

  if (external_time > start) {
    GST_INFO_OBJECT (clip, "Cannot convert the internal time %"
        GST_TIME_FORMAT " of the child %" GES_FORMAT " to a timeline "
        "time because it would lie before the start of the timeline",
        GST_TIME_ARGS (internal_time), GES_ARGS (child));

    g_set_error (error, GES_ERROR, GES_ERROR_NEGATIVE_TIME,
        "The internal time %" GST_TIME_FORMAT " of child \"%s\" "
        "would correspond to a negative start of -%" GST_TIME_FORMAT
        " for the clip \"%s\"", GST_TIME_ARGS (internal_time),
        GES_TIMELINE_ELEMENT_NAME (child),
        GST_TIME_ARGS (external_time - start),
        GES_TIMELINE_ELEMENT_NAME (clip));

    return GST_CLOCK_TIME_NONE;
  }

  return start - external_time;
}

/**
 * ges_clip_get_internal_time_from_timeline_time:
 * @clip: A #GESClip
 * @child: An #GESTrackElement:active child of @clip with a
 * #GESTrackElement:track
 * @timeline_time: A time in the timeline time coordinates
 * @error: (nullable): Return location for an error
 *
 * Convert the timeline time to an internal source time of the child.
 * This will take any time effects placed on the clip into account (see
 * #GESBaseEffect for what time effects are supported, and how to
 * declare them in GES).
 *
 * When @timeline_time is above the #GESTimelineElement:start of @clip,
 * this will return the internal time at which the content that appears at
 * @timeline_time in the output of the timeline is created in @child. For
 * example, if @timeline_time corresponds to the current seek position,
 * this would let you know which part of a media file is being read.
 *
 * This will be done assuming the clip has an indefinite end, so the
 * internal time may be beyond the current out-point of the child, or even
 * its #GESTimelineElement:max-duration.
 *
 * If, instead, @timeline_time is below the current
 * #GESTimelineElement:start of @clip, this will return what you would
 * need to set the #GESTimelineElement:in-point of @child to if you set
 * the #GESTimelineElement:start of @clip to @timeline_time and wanted
 * to keep the content of @child currently found at the current
 * #GESTimelineElement:start of @clip at the same timeline position. If
 * this would be negative, the conversion fails. This is useful for
 * determining what #GESTimelineElement:in-point would result from a
 * #GES_EDIT_MODE_TRIM to @timeline_time.
 *
 * Note that whilst a clip has no time effects, this second return is
 * equivalent to finding the internal time at which the content that
 * appears at @timeline_time in the timeline can be found in @child if it
 * had indefinite extent in both directions. However, with non-linear time
 * effects this second return will be more distinct.
 *
 * In either case, the returned time would be appropriate to use for the
 * #GESTimelineElement:in-point or #GESTimelineElement:max-duration of the
 * child.
 *
 * See ges_clip_get_timeline_time_from_internal_time(), which performs the
 * reverse.
 *
 * Returns: The time in the internal coordinates of @child corresponding
 * to @timeline_time, or #GST_CLOCK_TIME_NONE if the conversion could not
 * be performed.
 * Since: 1.18
 */
GstClockTime
ges_clip_get_internal_time_from_timeline_time (GESClip * clip,
    GESTrackElement * child, GstClockTime timeline_time, GError ** error)
{
  GstClockTime inpoint, start, external_time;
  gboolean decrease;
  GESTrack *track;
  GList *tmp, *time_effects;

  g_return_val_if_fail (GES_IS_CLIP (clip), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (child), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (!error || !*error, GST_CLOCK_TIME_NONE);

  if (!g_list_find (GES_CONTAINER_CHILDREN (clip), child)) {
    GST_WARNING_OBJECT (clip, "The track element %" GES_FORMAT " is not "
        "a child of the clip", GES_ARGS (child));
    return GST_CLOCK_TIME_NONE;
  }

  track = ges_track_element_get_track (child);

  if (!track) {
    GST_WARNING_OBJECT (clip, "Cannot convert the timeline time to an "
        "internal time of child %" GES_FORMAT " because it is not part "
        "of a track", GES_ARGS (child));
    return GST_CLOCK_TIME_NONE;
  }

  if (!ges_track_element_is_active (child)) {
    GST_WARNING_OBJECT (clip, "Cannot convert the timeline time to an "
        "internal time of child %" GES_FORMAT " because it is not active "
        "in its track", GES_ARGS (child));
    return GST_CLOCK_TIME_NONE;
  }

  if (timeline_time == GST_CLOCK_TIME_NONE)
    return GST_CLOCK_TIME_NONE;

  start = _START (clip);
  if (start <= timeline_time) {
    decrease = FALSE;
    external_time = timeline_time - start;
  } else {
    decrease = TRUE;
    external_time = start - timeline_time;
  }

  time_effects = _active_time_effects_in_track_after_priority (clip, track,
      _PRIORITY (child));

  /* currently ordered with highest priority (closest to the timeline)
   * first, with @child being at the *end* of the list, which is what we
   * want */

  for (tmp = time_effects; tmp; tmp = tmp->next) {
    GESBaseEffect *effect = tmp->data;
    GHashTable *values = ges_base_effect_get_time_property_values (effect);

    external_time = ges_base_effect_translate_source_to_sink_time (effect,
        external_time, values);
    g_hash_table_unref (values);
  }

  g_list_free (time_effects);

  if (!GST_CLOCK_TIME_IS_VALID (external_time))
    return GST_CLOCK_TIME_NONE;

  inpoint = _INPOINT (child);

  if (!decrease)
    return inpoint + external_time;

  if (external_time > inpoint) {
    GST_INFO_OBJECT (clip, "Cannot convert the timeline time %"
        GST_TIME_FORMAT " to an internal time of the child %"
        GES_FORMAT " because it would be before the element has any "
        "internal content", GST_TIME_ARGS (timeline_time), GES_ARGS (child));

    g_set_error (error, GES_ERROR, GES_ERROR_NEGATIVE_TIME,
        "The timeline time %" GST_TIME_FORMAT " would correspond to "
        "a negative in-point of -%" GST_TIME_FORMAT " for the child "
        "\"%s\" under clip \"%s\"", GST_TIME_ARGS (timeline_time),
        GST_TIME_ARGS (external_time - inpoint),
        GES_TIMELINE_ELEMENT_NAME (child), GES_TIMELINE_ELEMENT_NAME (clip));

    return GST_CLOCK_TIME_NONE;
  }

  return inpoint - external_time;
}

static GstClockTime
_convert_core_time (GESClip * clip, GstClockTime time, gboolean to_timeline,
    gboolean * no_core, GError ** error)
{
  GList *tmp;
  GstClockTime converted = GST_CLOCK_TIME_NONE;
  GstClockTime half_frame;
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (clip);
  GESClipAsset *asset =
      GES_CLIP_ASSET (ges_extractable_get_asset (GES_EXTRACTABLE (clip)));

  if (no_core)
    *no_core = TRUE;

  if (to_timeline)
    half_frame = timeline ? ges_timeline_get_frame_time (timeline, 1) : 0;
  else
    half_frame = ges_clip_asset_get_frame_time (asset, 1);
  half_frame = GST_CLOCK_TIME_IS_VALID (half_frame) ? half_frame / 2 : 0;

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *child = tmp->data;
    GESTrack *track = ges_track_element_get_track (child);

    if (_IS_CORE_CHILD (child) && track && ges_track_element_is_active (child)
        && ges_track_element_has_internal_source (child)) {
      GstClockTime tmp_time;
      GError *convert_error = NULL;

      if (no_core)
        *no_core = FALSE;

      if (to_timeline)
        tmp_time =
            ges_clip_get_timeline_time_from_internal_time (clip, child, time,
            &convert_error);
      else
        tmp_time =
            ges_clip_get_internal_time_from_timeline_time (clip, child, time,
            &convert_error);

      if (!GST_CLOCK_TIME_IS_VALID (converted)) {
        converted = tmp_time;
      } else if (!GST_CLOCK_TIME_IS_VALID (tmp_time)) {
        GST_WARNING_OBJECT (clip, "The calculated %s time for %s time %"
            GST_TIME_FORMAT " using core child %" GES_FORMAT " is not "
            "defined, but it had a definite value of %" GST_TIME_FORMAT
            " for another core child", to_timeline ? "timeline" : "internal",
            to_timeline ? "internal" : "timeline", GST_TIME_ARGS (time),
            GES_ARGS (child), GST_TIME_ARGS (converted));
      } else if (tmp_time != converted) {
        GstClockTime diff = (tmp_time > converted) ?
            tmp_time - converted : converted - tmp_time;

        if (diff > half_frame) {
          GST_WARNING_OBJECT (clip, "The calculated %s time for %s time %"
              GST_TIME_FORMAT " using core child %" GES_FORMAT " is %"
              GST_TIME_FORMAT ", which is different from the value of %"
              GST_TIME_FORMAT " calculated using a different core child",
              to_timeline ? "timeline" : "internal",
              to_timeline ? "internal" : "timeline", GST_TIME_ARGS (time),
              GES_ARGS (child), GST_TIME_ARGS (tmp_time),
              GST_TIME_ARGS (converted));
        }

        /* prefer result from video tracks */
        if (GES_IS_VIDEO_TRACK (track))
          converted = tmp_time;
      }
      if (convert_error) {
        if (error) {
          g_clear_error (error);
          *error = convert_error;
        } else {
          g_error_free (convert_error);
        }
      }
    }
  }

  return converted;
}

GstClockTime
ges_clip_get_core_internal_time_from_timeline_time (GESClip * clip,
    GstClockTime timeline_time, gboolean * no_core, GError ** error)
{
  return _convert_core_time (clip, timeline_time, FALSE, no_core, error);
}

/**
 * ges_clip_get_timeline_time_from_source_frame:
 * @clip: A #GESClip
 * @frame_number: The frame number to get the corresponding timestamp of
 * in the timeline coordinates
 * @error: (nullable): Return location for an error
 *
 * Convert the source frame number to a timeline time. This acts the same
 * as ges_clip_get_timeline_time_from_internal_time() using the core
 * children of the clip and using the frame number to specify the internal
 * position, rather than a timestamp.
 *
 * The returned timeline time can be used to seek or edit to a specific
 * frame.
 *
 * Note that you can get the frame timestamp of a particular clip asset
 * with ges_clip_asset_get_frame_time().
 *
 * Returns: The timestamp corresponding to @frame_number in the core
 * children of @clip, in the timeline coordinates, or #GST_CLOCK_TIME_NONE
 * if the conversion could not be performed.
 * Since: 1.18
 */
GstClockTime
ges_clip_get_timeline_time_from_source_frame (GESClip * clip,
    GESFrameNumber frame_number, GError ** error)
{
  GstClockTime timeline_time = GST_CLOCK_TIME_NONE;
  GstClockTime frame_ts;
  GESClipAsset *asset;

  g_return_val_if_fail (GES_IS_CLIP (clip), GST_CLOCK_TIME_NONE);
  g_return_val_if_fail (!error || !*error, GST_CLOCK_TIME_NONE);

  if (!GES_FRAME_NUMBER_IS_VALID (frame_number))
    return GST_CLOCK_TIME_NONE;

  asset = GES_CLIP_ASSET (ges_extractable_get_asset (GES_EXTRACTABLE (clip)));
  frame_ts = ges_clip_asset_get_frame_time (asset, frame_number);
  if (!GST_CLOCK_TIME_IS_VALID (frame_ts))
    return GST_CLOCK_TIME_NONE;

  timeline_time = _convert_core_time (clip, frame_ts, TRUE, NULL, error);

  if (error && *error) {
    g_clear_error (error);
    g_set_error (error, GES_ERROR, GES_ERROR_INVALID_FRAME_NUMBER,
        "Requested frame %" G_GINT64_FORMAT " would be outside the "
        "timeline.", frame_number);
  }

  return timeline_time;
}

/**
 * ges_clip_add_child_to_track:
 * @clip: A #GESClip
 * @child: A child of @clip
 * @track: The track to add @child to
 * @error: Return location for an error
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
 * Returns: (transfer none): The element that was added to @track, either
 * @child or a copy of child, or %NULL if the element could not be added.
 *
 * Since: 1.18
 */
GESTrackElement *
ges_clip_add_child_to_track (GESClip * clip, GESTrackElement * child,
    GESTrack * track, GError ** error)
{
  GESTimeline *timeline;
  GESTrackElement *el;
  GESTrack *current_track;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (child), NULL);
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);
  g_return_val_if_fail (!error || !*error, NULL);

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
    /* TODO: rather than add the effect at the next highest priority, we
     * want to add copied effect into the same EffectCollection, which all
     * share the same priority/index */
    if (_IS_TOP_EFFECT (child)) {
      clip->priv->use_effect_priority = TRUE;
      /* add at next lowest priority */
      clip->priv->effect_priority = GES_TIMELINE_ELEMENT_PRIORITY (child) + 1;
    }

    el = ges_clip_copy_track_element_into (clip, child, GST_CLOCK_TIME_NONE);

    clip->priv->use_effect_priority = FALSE;
    if (!el) {
      GST_ERROR_OBJECT (clip, "Could not add a copy of the track element %"
          GES_FORMAT " to the clip so cannot add it to the track %"
          GST_PTR_FORMAT, GES_ARGS (child), track);
      return NULL;
    }
  } else {
    el = child;
  }

  if (!ges_track_add_element_full (track, el, error)) {
    GST_INFO_OBJECT (clip, "Could not add the track element %"
        GES_FORMAT " to the track %" GST_PTR_FORMAT, GES_ARGS (el), track);
    if (el != child)
      ges_container_remove (GES_CONTAINER (clip), GES_TIMELINE_ELEMENT (el));
    return NULL;
  }

  /* call _child_track_changed now so that the "active" status of the
   * child can change. Note that this is needed because this method may
   * be called during ges_container_add, in which case "notify" for el
   * will be frozen. Thus, _update_active_for_track may not have been
   * called yet. It is important for us to call this now because when
   * the elements are un-frozen, we need to ensure the "active" status
   * is already set before the duration-limit is calculated */
  _update_active_for_track (clip, el);

  return el;
}
