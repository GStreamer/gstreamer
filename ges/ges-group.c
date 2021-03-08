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

struct _GESGroupPrivate
{
  gboolean reseting_start;

  guint32 max_layer_prio;

  gboolean updating_priority;
  /* This is used while were are setting ourselve a proper timing value,
   * in this case the value should always be kept */
  gboolean setting_value;
  GHashTable *child_sigids;
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

  for (tmp = container->children; tmp; tmp = tmp->next) {
    GESContainer *child = tmp->data;

    if (GES_IS_CLIP (child)) {
      GESLayer *layer = ges_clip_get_layer (GES_CLIP (child));
      guint32 prio;

      if (!layer)
        continue;

      prio = ges_layer_get_priority (layer);

      min_layer_prio = MIN (prio, min_layer_prio);
      max_layer_prio = MAX (prio, max_layer_prio);
      gst_object_unref (layer);
    } else if (GES_IS_GROUP (child)) {
      guint32 prio = _PRIORITY (child), height = GES_CONTAINER_HEIGHT (child);

      min_layer_prio = MIN (prio, min_layer_prio);
      max_layer_prio = MAX ((prio + height - 1), max_layer_prio);
    }
  }

  if (min_layer_prio != _PRIORITY (group)) {
    group->priv->updating_priority = TRUE;
    _set_priority0 (GES_TIMELINE_ELEMENT (group), min_layer_prio);
    group->priv->updating_priority = FALSE;
  }

  /* FIXME: max_layer_prio not used elsewhere
   * We could use it to inform our parent group when our maximum has
   * changed (which we don't currently do, to allow it to change its
   * height) */
  group->priv->max_layer_prio = max_layer_prio;
  _ges_container_set_height (container, max_layer_prio - min_layer_prio + 1);
}

/* layer changed its priority in the timeline */
static void
_child_priority_changed_cb (GESLayer * layer,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineElement * clip)
{
  _update_our_values (GES_GROUP (clip->parent));
}

static void
_child_clip_changed_layer_cb (GESClip * clip, GParamSpec * arg G_GNUC_UNUSED,
    GESGroup * group)
{
  ChildSignalIds *sigids;

  sigids = g_hash_table_lookup (group->priv->child_sigids, clip);

  g_assert (sigids);

  if (sigids->layer) {
    g_signal_handler_disconnect (sigids->layer,
        sigids->child_priority_changed_sid);
    sigids->child_priority_changed_sid = 0;
    gst_object_unref (sigids->layer);
  }

  sigids->layer = ges_clip_get_layer (GES_CLIP (clip));

  if (sigids->layer) {
    sigids->child_priority_changed_sid =
        g_signal_connect (sigids->layer, "notify::priority",
        (GCallback) _child_priority_changed_cb, clip);
  }

  _update_our_values (group);
}

static void
_child_group_priority_changed (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESGroup * group)
{
  _update_our_values (group);
}

/****************************************************
 *              GESTimelineElement vmethods         *
 ****************************************************/

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GList *layers;
  GESTimeline *timeline = element->timeline;

  if (GES_GROUP (element)->priv->updating_priority == TRUE
      || GES_TIMELINE_ELEMENT_BEING_EDITED (element))
    return TRUE;

  layers = timeline ? timeline->layers : NULL;
  if (layers == NULL) {
    GST_WARNING_OBJECT (element, "Not any layer in the timeline, not doing"
        "anything, timeline: %" GST_PTR_FORMAT, timeline);

    return FALSE;
  }

  /* FIXME: why are we not shifting ->max_layer_prio? */

  return timeline_tree_move (timeline_get_tree (timeline),
      element, (gint64) (element->priority) - (gint64) priority, 0,
      GES_EDGE_NONE, 0, NULL);
}

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp, *children;
  gint64 diff = start - _START (element);
  GESContainer *container = GES_CONTAINER (element);

  if (GES_GROUP (element)->priv->setting_value == TRUE)
    /* Let GESContainer update itself */
    return GES_TIMELINE_ELEMENT_CLASS (parent_class)->set_start (element,
        start);

  /* get copy of children, since GESContainer may resort the group */
  children = ges_container_get_children (container, FALSE);
  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = children; tmp; tmp = tmp->next)
    _set_start0 (tmp->data, _START (tmp->data) + diff);
  container->children_control_mode = GES_CHILDREN_UPDATE;
  g_list_free_full (children, gst_object_unref);

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
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (group);
  g_return_val_if_fail (GES_IS_CONTAINER (child), FALSE);

  if (timeline && child->timeline != timeline) {
    GST_WARNING_OBJECT (group, "Cannot add child %" GES_FORMAT
        " because it belongs to timeline %" GST_PTR_FORMAT
        " rather than the group's timeline %" GST_PTR_FORMAT,
        GES_ARGS (child), child->timeline, timeline);
    return FALSE;
  }

  return TRUE;
}

static void
_child_added (GESContainer * group, GESTimelineElement * child)
{
  GESGroup *self = GES_GROUP (group);
  ChildSignalIds *sigids;

  /* NOTE: notifies are currently frozen by ges_container_add */
  if (!GES_TIMELINE_ELEMENT_TIMELINE (group) && child->timeline) {
    timeline_add_group (child->timeline, self);
    timeline_emit_group_added (child->timeline, self);
  }

  _update_our_values (self);

  sigids = g_new0 (ChildSignalIds, 1);
  /* doesn't take a ref to child since no data */
  g_hash_table_insert (self->priv->child_sigids, child, sigids);

  if (GES_IS_CLIP (child)) {
    sigids->layer = ges_clip_get_layer (GES_CLIP (child));

    sigids->child_clip_changed_layer_sid = g_signal_connect (child,
        "notify::layer", (GCallback) _child_clip_changed_layer_cb, group);

    if (sigids->layer) {
      sigids->child_priority_changed_sid = g_signal_connect (sigids->layer,
          "notify::priority", (GCallback) _child_priority_changed_cb, child);
    }
  } else if (GES_IS_GROUP (child)) {
    sigids->child_group_priority_changed_sid = g_signal_connect (child,
        "notify::priority", (GCallback) _child_group_priority_changed, group);
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
}

static void
_child_removed (GESContainer * group, GESTimelineElement * child)
{
  GESGroup *self = GES_GROUP (group);
  ChildSignalIds *sigids;

  /* NOTE: notifies are currently frozen by ges_container_add */
  _ges_container_sort_children (group);

  sigids = g_hash_table_lookup (self->priv->child_sigids, child);

  g_assert (sigids);
  _disconnect_signals (self, child, sigids);
  g_hash_table_remove (self->priv->child_sigids, child);

  if (group->children == NULL) {
    GST_FIXME_OBJECT (group, "Auto destroy myself?");
    if (GES_TIMELINE_ELEMENT_TIMELINE (group))
      timeline_remove_group (GES_TIMELINE_ELEMENT_TIMELINE (group), self);
    return;
  }

  _update_our_values (GES_GROUP (group));
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
  GESContainer *ret = GES_CONTAINER (ges_group_new ());

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
ges_group_dispose (GObject * object)
{
  GESGroup *self = GES_GROUP (object);

  g_clear_pointer (&self->priv->child_sigids, g_hash_table_unref);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
ges_group_class_init (GESGroupClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESContainerClass *container_class = GES_CONTAINER_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  object_class->get_property = ges_group_get_property;
  object_class->set_property = ges_group_set_property;
  object_class->dispose = ges_group_dispose;

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
_free_sigid (gpointer mem)
{
  ChildSignalIds *sigids = mem;
  gst_clear_object (&sigids->layer);
  g_free (mem);
}

static void
ges_group_init (GESGroup * self)
{
  self->priv = ges_group_get_instance_private (self);

  self->priv->child_sigids =
      g_hash_table_new_full (NULL, NULL, NULL, _free_sigid);
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
