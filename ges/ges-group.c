/* GStreamer Editing Services
 * Copyright (C) 2013 Thibault Saunier <thibault.saunier@collabora.com>
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
 * SECTION:gesgroup
 * @title: GESGroup
 * @short_description: Class for a collection of #GESContainer-s within
 * a single timeline.
 *
 * A #GESGroup controls one or more #GESContainer-s (usually #GESClip-s,
 * but it can also control other #GESGroup-s). Its children must share
 * the same #GESTimeline, but can otherwise lie in separate #GESLayer-s
 * and have different timings.
 *
 * To initialise a group, you may want to use ges_container_group(),
 * and similarly use ges_container_ungroup() to dispose of it.
 *
 * A group will maintain the relative #GESTimelineElement:start times of
 * its children, as well as their relative layer #GESLayer:priority.
 * Therefore, if one of its children has its #GESTimelineElement:start
 * set, all other children will have their #GESTimelineElement:start
 * shifted by the same amount. Similarly, if one of its children moves to
 * a new layer, the other children will also change layers to maintain the
 * difference in their layer priorities. For example, if a child moves
 * from a layer with #GESLayer:priority 1 to a layer with priority 3, then
 * another child that was in a layer with priority 0 will move to the
 * layer with priority 2.
 *
 * The #GESGroup:start of a group refers to the earliest start
 * time of its children. If the group's #GESGroup:start is set, all the
 * children will be shifted equally such that the earliest start time
 * will match the set value. The #GESGroup:duration of a group is the
 * difference between the earliest start time and latest end time of its
 * children. If the group's #GESGroup:duration is increased, the children
 * whose end time matches the end of the group will be extended
 * accordingly. If it is decreased, then any child whose end time exceeds
 * the new end time will also have their duration decreased accordingly.
 *
 * A group may span several layers, but for methods such as
 * ges_timeline_element_get_layer_priority() and
 * ges_timeline_element_edit() a group is considered to have a layer
 * priority that is the highest #GESLayer:priority (numerically, the
 * smallest) of all the layers it spans.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-group.h"
#include "ges.h"
#include "ges-internal.h"

#include <string.h>

#define parent_class ges_group_parent_class
#define GES_CHILDREN_INIBIT_SIGNAL_EMISSION (GES_CHILDREN_LAST + 1)
#define GES_GROUP_SIGNALS_IDS_DATA_KEY_FORMAT "ges-group-signals-ids-%p"

struct _GESGroupPrivate
{
  gboolean reseting_start;

  guint32 max_layer_prio;

  /* This is used while were are setting ourselve a proper timing value,
   * in this case the value should always be kept */
  gboolean setting_value;
};

typedef struct
{
  GESLayer *layer;
  gulong child_clip_changed_layer_sid;
  gulong child_priority_changed_sid;
  gulong child_group_priority_changed_sid;
} ChildSignalIds;

enum
{
  PROP_0,
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_MAX_DURATION,
  PROP_PRIORITY,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST] = { NULL, };

G_DEFINE_TYPE_WITH_PRIVATE (GESGroup, ges_group, GES_TYPE_CONTAINER);

/****************************************************
 *              Our listening of children           *
 ****************************************************/
static void
_update_our_values (GESGroup * group)
{
  GList *tmp;
  GESContainer *container = GES_CONTAINER (group);
  guint32 min_layer_prio = G_MAXINT32, max_layer_prio = 0;

  for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
    GESContainer *child = tmp->data;

    if (GES_IS_CLIP (child)) {
      GESLayer *layer = ges_clip_get_layer (GES_CLIP (child));
      gint32 prio;

      if (!layer)
        continue;

      prio = ges_layer_get_priority (layer);

      min_layer_prio = MIN (prio, min_layer_prio);
      max_layer_prio = MAX (prio, max_layer_prio);
    } else if (GES_IS_GROUP (child)) {
      gint32 prio = _PRIORITY (child), height = GES_CONTAINER_HEIGHT (child);

      min_layer_prio = MIN (prio, min_layer_prio);
      max_layer_prio = MAX ((prio + height - 1), max_layer_prio);
    }
  }

  if (min_layer_prio != _PRIORITY (group)) {
    group->priv->setting_value = TRUE;
    _set_priority0 (GES_TIMELINE_ELEMENT (group), min_layer_prio);
    group->priv->setting_value = FALSE;
    for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
      GESTimelineElement *child = tmp->data;
      guint32 child_prio = GES_IS_CLIP (child) ?
          GES_TIMELINE_ELEMENT_LAYER_PRIORITY (child) : _PRIORITY (child);

      _ges_container_set_priority_offset (container,
          child, min_layer_prio - child_prio);
    }
  }

  /* FIXME: max_layer_prio not used elsewhere
   * We could use it to inform our parent group when our maximum has
   * changed (which we don't currently do, to allow it to change its
   * height) */
  group->priv->max_layer_prio = max_layer_prio;
  _ges_container_set_height (GES_CONTAINER (group),
      max_layer_prio - min_layer_prio + 1);
}

static void
_child_priority_changed_cb (GESLayer * layer,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineElement * clip)
{
  GESContainer *container = GES_CONTAINER (GES_TIMELINE_ELEMENT_PARENT (clip));

  gint layer_prio = ges_layer_get_priority (layer);
  gint offset = _ges_container_get_priority_offset (container, clip);

  if (container->children_control_mode != GES_CHILDREN_UPDATE) {
    GST_DEBUG_OBJECT (container, "Ignoring updated");
    return;
  }

  if (layer_prio + offset == _PRIORITY (container))
    return;

  /* FIXME: we shouldn't be moving our children to a new layer when a
   * layer changes its priority in a timeline */
  container->initiated_move = clip;
  _set_priority0 (GES_TIMELINE_ELEMENT (container), layer_prio + offset);
  container->initiated_move = NULL;
}

static void
_child_clip_changed_layer_cb (GESTimelineElement * clip,
    GParamSpec * arg G_GNUC_UNUSED, GESGroup * group)
{
  ChildSignalIds *sigids;
  gchar *signals_ids_key;
  GESLayer *old_layer, *new_layer;
  gint offset, layer_prio = GES_TIMELINE_ELEMENT_LAYER_PRIORITY (clip);
  GESContainer *container = GES_CONTAINER (group);

  offset = _ges_container_get_priority_offset (container, clip);
  signals_ids_key =
      g_strdup_printf (GES_GROUP_SIGNALS_IDS_DATA_KEY_FORMAT, clip);
  sigids = g_object_get_data (G_OBJECT (group), signals_ids_key);
  g_free (signals_ids_key);
  old_layer = sigids->layer;

  new_layer = ges_clip_get_layer (GES_CLIP (clip));

  if (sigids->child_priority_changed_sid) {
    g_signal_handler_disconnect (old_layer, sigids->child_priority_changed_sid);
    sigids->child_priority_changed_sid = 0;
  }

  if (new_layer) {
    sigids->child_priority_changed_sid =
        g_signal_connect (new_layer, "notify::priority",
        (GCallback) _child_priority_changed_cb, clip);
  }
  /* sigids takes ownership of new_layer, we take ownership of old_layer */
  sigids->layer = new_layer;

  if (container->children_control_mode != GES_CHILDREN_UPDATE) {
    if (container->children_control_mode == GES_CHILDREN_INIBIT_SIGNAL_EMISSION) {
      container->children_control_mode = GES_CHILDREN_UPDATE;
      g_signal_stop_emission_by_name (clip, "notify::layer");
    }
    gst_clear_object (&old_layer);
    return;
  }

  if (new_layer && old_layer && (layer_prio + offset < 0 ||
          (GES_TIMELINE_ELEMENT_TIMELINE (group) &&
              layer_prio + offset + GES_CONTAINER_HEIGHT (group) - 1 >
              g_list_length (GES_TIMELINE_ELEMENT_TIMELINE (group)->layers)))) {

    GST_INFO_OBJECT (container,
        "Trying to move to a layer %" GST_PTR_FORMAT " outside of"
        "the timeline layers, moving back to old layer (prio %i)", new_layer,
        _PRIORITY (group) - offset);

    container->children_control_mode = GES_CHILDREN_INIBIT_SIGNAL_EMISSION;
    ges_clip_move_to_layer (GES_CLIP (clip), old_layer);
    g_signal_stop_emission_by_name (clip, "notify::layer");

    gst_clear_object (&old_layer);
    return;
  }

  if (!new_layer || !old_layer) {
    _update_our_values (group);
    gst_clear_object (&old_layer);
    return;
  }

  container->initiated_move = clip;
  _set_priority0 (GES_TIMELINE_ELEMENT (group), layer_prio + offset);
  container->initiated_move = NULL;
  gst_clear_object (&old_layer);
}

static void
_child_group_priority_changed (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESGroup * group)
{
  gint offset;
  GESContainer *container = GES_CONTAINER (group);

  if (container->children_control_mode != GES_CHILDREN_UPDATE) {
    GST_DEBUG_OBJECT (group, "Ignoring updated");
    return;
  }

  offset = _ges_container_get_priority_offset (container, child);

  if (_PRIORITY (group) < offset ||
      (GES_TIMELINE_ELEMENT_TIMELINE (group) &&
          _PRIORITY (group) + offset + GES_CONTAINER_HEIGHT (group) >
          g_list_length (GES_TIMELINE_ELEMENT_TIMELINE (group)->layers))) {

    GST_WARNING_OBJECT (container, "Trying to move to a layer outside of"
        "the timeline layers");

    return;
  }

  container->initiated_move = child;
  _set_priority0 (GES_TIMELINE_ELEMENT (group), _PRIORITY (child) + offset);
  container->initiated_move = NULL;
}

/****************************************************
 *              GESTimelineElement vmethods         *
 ****************************************************/
static gboolean
_trim (GESTimelineElement * group, GstClockTime start)
{
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (group);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");

    return FALSE;
  }

  return timeline_tree_trim (timeline_get_tree (timeline), group,
      0, GST_CLOCK_DIFF (start, _START (group)), GES_EDGE_START,
      ges_timeline_get_snapping_distance (timeline));
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GList *tmp, *layers;
  gint diff = priority - _PRIORITY (element);
  GESContainer *container = GES_CONTAINER (element);
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (element);

  if (GES_GROUP (element)->priv->setting_value == TRUE)
    return TRUE;

  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;

  layers = GES_TIMELINE_ELEMENT_TIMELINE (element) ?
      GES_TIMELINE_ELEMENT_TIMELINE (element)->layers : NULL;
  if (layers == NULL) {
    GST_WARNING_OBJECT (element, "Not any layer in the timeline, not doing"
        "anything, timeline: %" GST_PTR_FORMAT,
        GES_TIMELINE_ELEMENT_TIMELINE (element));

    return FALSE;
  }

  /* FIXME: why are we not shifting ->max_layer_prio? */

  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    if (child != container->initiated_move
        || ELEMENT_FLAG_IS_SET (container, GES_TIMELINE_ELEMENT_SET_SIMPLE)) {
      if (GES_IS_CLIP (child)) {
        guint32 layer_prio = GES_TIMELINE_ELEMENT_LAYER_PRIORITY (child) + diff;
        GESLayer *layer =
            g_list_nth_data (GES_TIMELINE_GET_LAYERS (timeline), layer_prio);

        if (layer == NULL) {
          do {
            layer = ges_timeline_append_layer (timeline);
          } while (ges_layer_get_priority (layer) < layer_prio);
        }

        GST_DEBUG ("%" GES_FORMAT "moving from layer: %i to %i",
            GES_ARGS (child), GES_TIMELINE_ELEMENT_LAYER_PRIORITY (child),
            layer_prio);
        ges_clip_move_to_layer (GES_CLIP (child), g_list_nth_data (layers,
                layer_prio));
      } else if (GES_IS_GROUP (child)) {
        GST_DEBUG_OBJECT (child, "moving from %i to %i",
            _PRIORITY (child), diff + _PRIORITY (child));
        ges_timeline_element_set_priority (child, diff + _PRIORITY (child));
      }
    }
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;

  return TRUE;
}

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp, *children;
  gint64 diff = start - _START (element);
  GESTimeline *timeline;
  GESContainer *container = GES_CONTAINER (element);
  GESTimelineElement *toplevel =
      ges_timeline_element_get_toplevel_parent (element);

  gst_object_unref (toplevel);
  if (GES_GROUP (element)->priv->setting_value == TRUE)
    /* Let GESContainer update itself */
    return GES_TIMELINE_ELEMENT_CLASS (parent_class)->set_start (element,
        start);

  if (ELEMENT_FLAG_IS_SET (element, GES_TIMELINE_ELEMENT_SET_SIMPLE) ||
      ELEMENT_FLAG_IS_SET (toplevel, GES_TIMELINE_ELEMENT_SET_SIMPLE)) {
    /* get copy of children, since GESContainer may resort the group */
    children = ges_container_get_children (container, FALSE);
    container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
    for (tmp = children; tmp; tmp = tmp->next)
      _set_start0 (tmp->data, _START (tmp->data) + diff);
    container->children_control_mode = GES_CHILDREN_UPDATE;
    g_list_free_full (children, gst_object_unref);
    return TRUE;
  }

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (element);
  if (timeline)
    return ges_timeline_move_object_simple (timeline, element, NULL,
        GES_EDGE_NONE, start);

  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  if (inpoint != 0) {
    GST_WARNING_OBJECT (element, "The in-point of a group has no meaning,"
        " it can not be set to a non-zero value");
    return FALSE;
  }
  return TRUE;
}

static gboolean
_set_max_duration (GESTimelineElement * element, GstClockTime max_duration)
{
  if (GST_CLOCK_TIME_IS_VALID (max_duration)) {
    GST_WARNING_OBJECT (element, "The max-duration of a group has no "
        "meaning, it can not be set to a valid GstClockTime value");
    return FALSE;
  }
  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GList *tmp, *children;
  GstClockTime last_child_end = 0, new_end;
  GESContainer *container = GES_CONTAINER (element);
  GESGroupPrivate *priv = GES_GROUP (element)->priv;

  if (priv->setting_value == TRUE)
    /* Let GESContainer update itself */
    return GES_TIMELINE_ELEMENT_CLASS (parent_class)->set_duration (element,
        duration);

  if (element->timeline
      && !timeline_tree_can_move_element (timeline_get_tree (element->timeline),
          element, _PRIORITY (element), element->start, duration, NULL)) {
    return FALSE;
  }

  if (container->initiated_move == NULL) {
    gboolean expending = (_DURATION (element) < duration);

    new_end = _START (element) + duration;
    /* get copy of children, since GESContainer may resort the group */
    children = ges_container_get_children (container, FALSE);
    container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
    for (tmp = children; tmp; tmp = tmp->next) {
      GESTimelineElement *child = tmp->data;
      GstClockTime n_dur;

      if ((!expending && _END (child) > new_end) ||
          (expending && (_END (child) >= _END (element)))) {
        n_dur = MAX (0, ((gint64) (new_end - _START (child))));
        _set_duration0 (child, n_dur);
      }
    }
    container->children_control_mode = GES_CHILDREN_UPDATE;
    g_list_free_full (children, gst_object_unref);
  }

  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    if (_DURATION (tmp->data))
      last_child_end =
          MAX (GES_TIMELINE_ELEMENT_END (tmp->data), last_child_end);
  }

  priv->setting_value = TRUE;
  _set_duration0 (element, last_child_end - _START (element));
  priv->setting_value = FALSE;

  return -1;
}

/****************************************************
 *                                                  *
 *  GESContainer virtual methods implementation     *
 *                                                  *
 ****************************************************/

static gboolean
_add_child (GESContainer * group, GESTimelineElement * child)
{
  g_return_val_if_fail (GES_IS_CONTAINER (child), FALSE);

  return TRUE;
}

static void
_child_added (GESContainer * group, GESTimelineElement * child)
{
  GList *children, *tmp;
  gchar *signals_ids_key;
  ChildSignalIds *signals_ids;

  GESGroupPrivate *priv = GES_GROUP (group)->priv;
  GstClockTime last_child_end = 0, first_child_start = G_MAXUINT64;

  /* NOTE: notifies are currently frozen by ges_container_add */
  if (!GES_TIMELINE_ELEMENT_TIMELINE (group)
      && GES_TIMELINE_ELEMENT_TIMELINE (child)) {
    timeline_add_group (GES_TIMELINE_ELEMENT_TIMELINE (child),
        GES_GROUP (group));
    timeline_emit_group_added (GES_TIMELINE_ELEMENT_TIMELINE (child),
        GES_GROUP (group));
  }
  /* FIXME: we should otherwise check that the child is part of the same
   * timeline */

  children = GES_CONTAINER_CHILDREN (group);

  for (tmp = children; tmp; tmp = tmp->next) {
    last_child_end = MAX (GES_TIMELINE_ELEMENT_END (tmp->data), last_child_end);
    first_child_start =
        MIN (GES_TIMELINE_ELEMENT_START (tmp->data), first_child_start);
  }

  priv->setting_value = TRUE;
  ELEMENT_SET_FLAG (group, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  if (first_child_start != GES_TIMELINE_ELEMENT_START (group)) {
    _set_start0 (GES_TIMELINE_ELEMENT (group), first_child_start);
  }

  if (last_child_end != GES_TIMELINE_ELEMENT_END (group)) {
    _set_duration0 (GES_TIMELINE_ELEMENT (group),
        last_child_end - first_child_start);
  }
  ELEMENT_UNSET_FLAG (group, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  priv->setting_value = FALSE;

  _update_our_values (GES_GROUP (group));

  signals_ids_key =
      g_strdup_printf (GES_GROUP_SIGNALS_IDS_DATA_KEY_FORMAT, child);
  signals_ids = g_malloc0 (sizeof (ChildSignalIds));
  g_object_set_data_full (G_OBJECT (group), signals_ids_key,
      signals_ids, g_free);
  g_free (signals_ids_key);
  if (GES_IS_CLIP (child)) {
    GESLayer *layer = ges_clip_get_layer (GES_CLIP (child));

    signals_ids->child_clip_changed_layer_sid =
        g_signal_connect (child, "notify::layer",
        (GCallback) _child_clip_changed_layer_cb, group);

    if (layer) {
      signals_ids->child_priority_changed_sid = g_signal_connect (layer,
          "notify::priority", (GCallback) _child_priority_changed_cb, child);
    }
    signals_ids->layer = layer;

  } else if (GES_IS_GROUP (child), group) {
    signals_ids->child_group_priority_changed_sid =
        g_signal_connect (child, "notify::priority",
        (GCallback) _child_group_priority_changed, group);
  }
}

static void
_disconnect_signals (GESGroup * group, GESTimelineElement * child,
    ChildSignalIds * sigids)
{
  if (sigids->child_group_priority_changed_sid) {
    g_signal_handler_disconnect (child,
        sigids->child_group_priority_changed_sid);
    sigids->child_group_priority_changed_sid = 0;
  }

  if (sigids->child_clip_changed_layer_sid) {
    g_signal_handler_disconnect (child, sigids->child_clip_changed_layer_sid);
    sigids->child_clip_changed_layer_sid = 0;
  }

  if (sigids->child_priority_changed_sid) {
    g_signal_handler_disconnect (sigids->layer,
        sigids->child_priority_changed_sid);
    sigids->child_priority_changed_sid = 0;
  }
  gst_clear_object (&(sigids->layer));
}


static void
_child_removed (GESContainer * group, GESTimelineElement * child)
{
  GList *children, *tmp;
  GstClockTime first_child_start;
  gchar *signals_ids_key;
  ChildSignalIds *sigids;
  guint32 new_priority = G_MAXUINT32;
  GESGroupPrivate *priv = GES_GROUP (group)->priv;

  /* NOTE: notifies are currently frozen by ges_container_add */
  _ges_container_sort_children (group);

  children = GES_CONTAINER_CHILDREN (group);

  signals_ids_key =
      g_strdup_printf (GES_GROUP_SIGNALS_IDS_DATA_KEY_FORMAT, child);
  sigids = g_object_get_data (G_OBJECT (group), signals_ids_key);
  _disconnect_signals (GES_GROUP (group), child, sigids);
  g_free (signals_ids_key);
  if (children == NULL) {
    GST_FIXME_OBJECT (group, "Auto destroy myself?");
    if (GES_TIMELINE_ELEMENT_TIMELINE (group))
      timeline_remove_group (GES_TIMELINE_ELEMENT_TIMELINE (group),
          GES_GROUP (group));
    return;
  }

  for (tmp = children; tmp; tmp = tmp->next)
    new_priority =
        MIN (new_priority, ges_timeline_element_get_priority (tmp->data));

  priv->setting_value = TRUE;
  ELEMENT_SET_FLAG (group, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  if (_PRIORITY (group) != new_priority)
    ges_timeline_element_set_priority (GES_TIMELINE_ELEMENT (group),
        new_priority);
  first_child_start = GES_TIMELINE_ELEMENT_START (children->data);
  if (first_child_start > GES_TIMELINE_ELEMENT_START (group)) {
    _set_start0 (GES_TIMELINE_ELEMENT (group), first_child_start);
  }
  ELEMENT_UNSET_FLAG (group, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  priv->setting_value = FALSE;
}

static GList *
_ungroup (GESContainer * group, gboolean recursive)
{
  GPtrArray *children_array;
  GESTimeline *timeline;
  GList *children, *tmp, *ret = NULL;

  /* We choose 16 just as an arbitrary value */
  children_array = g_ptr_array_sized_new (16);
  timeline = GES_TIMELINE_ELEMENT_TIMELINE (group);

  children = ges_container_get_children (group, FALSE);
  for (tmp = children; tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    gst_object_ref (child);
    ges_container_remove (group, child);
    g_ptr_array_add (children_array, child);
    ret = g_list_append (ret, child);
  }

  if (timeline)
    timeline_emit_group_removed (timeline, (GESGroup *) group, children_array);
  g_ptr_array_free (children_array, TRUE);
  g_list_free_full (children, gst_object_unref);

  /* No need to remove from the timeline here, this will be done in _child_removed */

  return ret;
}

static GESContainer *
_group (GList * containers)
{
  GList *tmp;
  GESTimeline *timeline = NULL;
  GESContainer *ret = g_object_new (GES_TYPE_GROUP, NULL);

  if (!containers)
    return ret;

  for (tmp = containers; tmp; tmp = tmp->next) {
    if (!timeline) {
      timeline = GES_TIMELINE_ELEMENT_TIMELINE (tmp->data);
    } else if (timeline != GES_TIMELINE_ELEMENT_TIMELINE (tmp->data)) {
      g_object_unref (ret);

      return NULL;
    }

    if (!ges_container_add (ret, tmp->data)) {
      GST_INFO ("%" GES_FORMAT " could not add child %p while"
          " grouping", GES_ARGS (ret), tmp->data);
      g_object_unref (ret);

      return NULL;
    }
  }

  /* No need to add to the timeline here, this will be done in _child_added */

  return ret;
}

/****************************************************
 *                                                  *
 *    GObject virtual methods implementation        *
 *                                                  *
 ****************************************************/
static void
ges_group_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineElement *self = GES_TIMELINE_ELEMENT (object);

  switch (property_id) {
    case PROP_START:
      g_value_set_uint64 (value, self->start);
      break;
    case PROP_INPOINT:
      g_value_set_uint64 (value, self->inpoint);
      break;
    case PROP_DURATION:
      g_value_set_uint64 (value, self->duration);
      break;
    case PROP_MAX_DURATION:
      g_value_set_uint64 (value, self->maxduration);
      break;
    case PROP_PRIORITY:
      g_value_set_uint (value, self->priority);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
  }
}

static void
ges_group_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineElement *self = GES_TIMELINE_ELEMENT (object);

  switch (property_id) {
    case PROP_START:
      ges_timeline_element_set_start (self, g_value_get_uint64 (value));
      break;
    case PROP_INPOINT:
      ges_timeline_element_set_inpoint (self, g_value_get_uint64 (value));
      break;
    case PROP_DURATION:
      ges_timeline_element_set_duration (self, g_value_get_uint64 (value));
      break;
    case PROP_PRIORITY:
      ges_timeline_element_set_priority (self, g_value_get_uint (value));
      break;
    case PROP_MAX_DURATION:
      ges_timeline_element_set_max_duration (self, g_value_get_uint64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
  }
}

static void
ges_group_class_init (GESGroupClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESContainerClass *container_class = GES_CONTAINER_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  object_class->get_property = ges_group_get_property;
  object_class->set_property = ges_group_set_property;

  element_class->trim = _trim;
  element_class->set_duration = _set_duration;
  element_class->set_inpoint = _set_inpoint;
  element_class->set_max_duration = _set_max_duration;
  element_class->set_start = _set_start;
  element_class->set_priority = _set_priority;

  /* We override start, inpoint, duration and max-duration from GESTimelineElement
   * in order to makes sure those fields are not serialized.
   */
  /**
   * GESGroup:start:
   *
   * An overwrite of the #GESTimelineElement:start property. For a
   * #GESGroup, this is the earliest #GESTimelineElement:start time
   * amongst its children.
   */
  properties[PROP_START] = g_param_spec_uint64 ("start", "Start",
      "The position in the container", 0, G_MAXUINT64, 0,
      G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESGroup:in-point:
   *
   * An overwrite of the #GESTimelineElement:in-point property. This has
   * no meaning for a group and should not be set.
   */
  properties[PROP_INPOINT] =
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESGroup:duration:
   *
   * An overwrite of the #GESTimelineElement:duration property. For a
   * #GESGroup, this is the difference between the earliest
   * #GESTimelineElement:start time and the latest end time (given by
   * #GESTimelineElement:start + #GESTimelineElement:duration) amongst
   * its children.
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The duration to use", 0,
      G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESGroup:max-duration:
   *
   * An overwrite of the #GESTimelineElement:max-duration property. This
   * has no meaning for a group and should not be set.
   */
  properties[PROP_MAX_DURATION] =
      g_param_spec_uint64 ("max-duration", "Maximum duration",
      "The maximum duration of the object", 0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESGroup:priority:
   *
   * An overwrite of the #GESTimelineElement:priority property.
   * Setting #GESTimelineElement priorities is deprecated as all priority
   * management is now done by GES itself.
   */
  properties[PROP_PRIORITY] = g_param_spec_uint ("priority", "Priority",
      "The priority of the object", 0, G_MAXUINT, 0,
      G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  g_object_class_install_properties (object_class, PROP_LAST, properties);

  container_class->add_child = _add_child;
  container_class->child_added = _child_added;
  container_class->child_removed = _child_removed;
  container_class->ungroup = _ungroup;
  container_class->group = _group;
  container_class->grouping_priority = 0;
}

static void
ges_group_init (GESGroup * self)
{
  self->priv = ges_group_get_instance_private (self);

  self->priv->setting_value = FALSE;
}

/****************************************************
 *                                                  *
 *              API implementation                  *
 *                                                  *
 ****************************************************/

/**
 * ges_group_new:
 *
 * Created a new empty group. You may wish to use
 * ges_container_group() instead, which can return a different
 * #GESContainer subclass if possible.
 *
 * Returns: (transfer floating): The new empty group.
 */
GESGroup *
ges_group_new (void)
{
  GESGroup *res;
  GESAsset *asset = ges_asset_request (GES_TYPE_GROUP, NULL, NULL);

  res = GES_GROUP (ges_asset_extract (asset, NULL));
  gst_object_unref (asset);

  return res;
}
