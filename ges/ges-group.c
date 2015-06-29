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
 * @short_description: Class that permits to group GESClip-s in a timeline,
 * letting the user manage it a single GESTimelineElement
 *
 * A #GESGroup is an object which controls one or more
 * #GESClips in one or more #GESLayer(s).
 *
 * To instanciate a group, you should use the ges_container_group method,
 * this will be responsible for deciding what subclass of #GESContainer
 * should be instaciated to group the various #GESTimelineElement passed
 * in parametter.
 */

#include "ges-group.h"
#include "ges.h"
#include "ges-internal.h"

#include <string.h>

#define parent_class ges_group_parent_class
G_DEFINE_TYPE (GESGroup, ges_group, GES_TYPE_CONTAINER);

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
      gint32 prio = ges_layer_get_priority (layer);

      min_layer_prio = MIN (prio, min_layer_prio);
      max_layer_prio = MAX (prio, max_layer_prio);
    } else if (GES_IS_GROUP (child)) {
      gint32 prio = _PRIORITY (child), height = GES_CONTAINER_HEIGHT (child);

      min_layer_prio = MIN (prio, min_layer_prio);
      max_layer_prio = MAX ((prio + height), max_layer_prio);
    }
  }

  if (min_layer_prio != _PRIORITY (group)) {
    group->priv->setting_value = TRUE;
    _set_priority0 (GES_TIMELINE_ELEMENT (group), min_layer_prio);
    group->priv->setting_value = FALSE;
    for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
      GESTimelineElement *child = tmp->data;
      guint32 child_prio = GES_IS_CLIP (child) ?
          ges_clip_get_layer_priority (GES_CLIP (child)) : _PRIORITY (child);

      _ges_container_set_priority_offset (container,
          child, min_layer_prio - child_prio);
    }
  }

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
  gint offset, layer_prio = ges_clip_get_layer_priority (GES_CLIP (clip));
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
  sigids->layer = new_layer;

  if (container->children_control_mode != GES_CHILDREN_UPDATE) {
    if (container->children_control_mode == GES_CHILDREN_INIBIT_SIGNAL_EMISSION) {
      container->children_control_mode = GES_CHILDREN_UPDATE;
      g_signal_stop_emission_by_name (clip, "notify::layer");
    }
    return;
  }

  if (new_layer && (layer_prio + offset < 0 ||
          (GES_TIMELINE_ELEMENT_TIMELINE (group) &&
              layer_prio + offset + GES_CONTAINER_HEIGHT (group) - 1 >
              g_list_length (GES_TIMELINE_ELEMENT_TIMELINE (group)->layers)))) {

    GST_INFO_OBJECT (container, "Trying to move to a layer outside of"
        "the timeline layers, moving back to old layer (prio %i)",
        _PRIORITY (group) - offset);

    container->children_control_mode = GES_CHILDREN_INIBIT_SIGNAL_EMISSION;
    ges_clip_move_to_layer (GES_CLIP (clip), old_layer);
    g_signal_stop_emission_by_name (clip, "notify::layer");

    return;
  }

  container->initiated_move = clip;
  _set_priority0 (GES_TIMELINE_ELEMENT (group), layer_prio + offset);
  container->initiated_move = NULL;
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
  GList *tmp;
  GstClockTime last_child_end = 0, oldstart = _START (group);
  GESContainer *container = GES_CONTAINER (group);
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (group);
  gboolean ret = TRUE, expending = (start < _START (group));

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");

    return FALSE;
  }

  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    if (expending) {
      /* If the start is bigger, we do not touch it (in case we are expending) */
      if (_START (child) > oldstart) {
        GST_DEBUG_OBJECT (child, "Skipping as not at begining of the group");
        continue;
      }

      ret &= ges_timeline_element_trim (child, start);
    } else {
      if (start > _END (child))
        ret &= ges_timeline_element_trim (child, _END (child));
      else if (_START (child) < start && _DURATION (child))
        ret &= ges_timeline_element_trim (child, start);

    }
  }

  for (tmp = GES_CONTAINER_CHILDREN (group); tmp; tmp = tmp->next) {
    if (_DURATION (tmp->data))
      last_child_end =
          MAX (GES_TIMELINE_ELEMENT_END (tmp->data), last_child_end);
  }

  GES_GROUP (group)->priv->setting_value = TRUE;
  _set_start0 (group, start);
  _set_duration0 (group, last_child_end - start);
  GES_GROUP (group)->priv->setting_value = FALSE;
  container->children_control_mode = GES_CHILDREN_UPDATE;

  return ret;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GList *tmp, *layers;
  gint diff = priority - _PRIORITY (element);
  GESContainer *container = GES_CONTAINER (element);

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
  } else if (priority + GES_CONTAINER_HEIGHT (container) - 1 >
      g_list_length (layers)) {
    GST_WARNING_OBJECT (container, "Trying to move to a layer outside of"
        "the timeline layers");
    return FALSE;
  }

  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    if (child != container->initiated_move) {
      if (GES_IS_CLIP (child)) {
        guint32 layer_prio =
            ges_clip_get_layer_priority (GES_CLIP (child)) + diff;

        GST_DEBUG_OBJECT (child, "moving from layer: %i to %i",
            ges_clip_get_layer_priority (GES_CLIP (child)), layer_prio);
        ges_clip_move_to_layer (GES_CLIP (child),
            g_list_nth_data (layers, layer_prio));
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
  GList *tmp;
  gint64 diff = start - _START (element);
  GESContainer *container = GES_CONTAINER (element);

  if (GES_GROUP (element)->priv->setting_value == TRUE)
    /* Let GESContainer update itself */
    return GES_TIMELINE_ELEMENT_CLASS (parent_class)->set_start (element,
        start);


  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    if (child != container->initiated_move &&
        (_END (child) > _START (element) || _END (child) > start)) {
      _set_start0 (child, _START (child) + diff);
    }
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;

  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  return FALSE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GList *tmp;
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
    container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
    for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
      GESTimelineElement *child = tmp->data;
      GstClockTime n_dur;

      if ((!expending && _END (child) > new_end) ||
          (expending && (_END (child) >= _END (element)))) {
        n_dur = MAX (0, ((gint64) (new_end - _START (child))));
        _set_duration0 (child, n_dur);
      }
    }
    container->children_control_mode = GES_CHILDREN_UPDATE;
  }

  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    if (_DURATION (tmp->data))
      last_child_end =
          MAX (GES_TIMELINE_ELEMENT_END (tmp->data), last_child_end);
  }

  priv->setting_value = TRUE;
  _set_duration0 (element, last_child_end - _START (element));
  priv->setting_value = FALSE;

  return FALSE;
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

  if (!GES_TIMELINE_ELEMENT_TIMELINE (group)) {
    timeline_add_group (GES_TIMELINE_ELEMENT_TIMELINE (child),
        GES_GROUP (group));
  }

  children = GES_CONTAINER_CHILDREN (group);

  for (tmp = children; tmp; tmp = tmp->next) {
    last_child_end = MAX (GES_TIMELINE_ELEMENT_END (tmp->data), last_child_end);
    first_child_start =
        MIN (GES_TIMELINE_ELEMENT_START (tmp->data), first_child_start);
  }

  priv->setting_value = TRUE;
  if (first_child_start != GES_TIMELINE_ELEMENT_START (group)) {
    group->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
    _set_start0 (GES_TIMELINE_ELEMENT (group), first_child_start);
  }

  if (last_child_end != GES_TIMELINE_ELEMENT_END (group)) {
    _set_duration0 (GES_TIMELINE_ELEMENT (group),
        last_child_end - first_child_start);
  }
  priv->setting_value = FALSE;

  group->children_control_mode = GES_CHILDREN_UPDATE;
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
}


static void
_child_removed (GESContainer * group, GESTimelineElement * child)
{
  GList *children;
  GstClockTime first_child_start;
  gchar *signals_ids_key;
  ChildSignalIds *sigids;
  GESGroupPrivate *priv = GES_GROUP (group)->priv;

  _ges_container_sort_children (group);

  children = GES_CONTAINER_CHILDREN (group);

  signals_ids_key =
      g_strdup_printf (GES_GROUP_SIGNALS_IDS_DATA_KEY_FORMAT, child);
  sigids = g_object_get_data (G_OBJECT (group), signals_ids_key);
  _disconnect_signals (GES_GROUP (group), child, sigids);
  g_free (signals_ids_key);
  if (children == NULL) {
    GST_FIXME_OBJECT (group, "Auto destroy myself?");
    timeline_remove_group (GES_TIMELINE_ELEMENT_TIMELINE (group),
        GES_GROUP (group));
    return;
  }

  priv->setting_value = TRUE;
  first_child_start = GES_TIMELINE_ELEMENT_START (children->data);
  if (first_child_start > GES_TIMELINE_ELEMENT_START (group)) {
    group->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
    _set_start0 (GES_TIMELINE_ELEMENT (group), first_child_start);
    group->children_control_mode = GES_CHILDREN_UPDATE;
  }
  priv->setting_value = FALSE;
}

static GList *
_ungroup (GESContainer * group, gboolean recursive)
{
  GList *children, *tmp, *ret = NULL;

  children = ges_container_get_children (group, FALSE);
  for (tmp = children; tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;

    gst_object_ref (child);
    ges_container_remove (group, child);
    ret = g_list_append (ret, child);
  }
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

    ges_container_add (ret, tmp->data);
  }

  /* No need to add to the timeline here, this will be done in _child_added */

  return ret;
}

static gboolean
_paste (GESTimelineElement * element, GESTimelineElement * ref,
    GstClockTime paste_position)
{
  if (GES_TIMELINE_ELEMENT_CLASS (parent_class)->paste (element,
          ref, paste_position)) {

    if (GES_CONTAINER_CHILDREN (element))
      timeline_add_group (GES_TIMELINE_ELEMENT_TIMELINE (GES_CONTAINER_CHILDREN
              (element)->data), GES_GROUP (element));

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

  g_type_class_add_private (klass, sizeof (GESGroupPrivate));

  object_class->get_property = ges_group_get_property;
  object_class->set_property = ges_group_set_property;

  element_class->trim = _trim;
  element_class->set_duration = _set_duration;
  element_class->set_inpoint = _set_inpoint;
  element_class->set_start = _set_start;
  element_class->set_priority = _set_priority;
  element_class->paste = _paste;

  /* We override start, inpoint, duration and max-duration from GESTimelineElement
   * in order to makes sure those fields are not serialized.
   */
  /**
   * GESGroup:start:
   *
   * The position of the object in its container (in nanoseconds).
   */
  properties[PROP_START] = g_param_spec_uint64 ("start", "Start",
      "The position in the container", 0, G_MAXUINT64, 0,
      G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESGroup:in-point:
   *
   * The in-point at which this #GESGroup will start outputting data
   * from its contents (in nanoseconds).
   *
   * Ex : an in-point of 5 seconds means that the first outputted buffer will
   * be the one located 5 seconds in the controlled resource.
   */
  properties[PROP_INPOINT] =
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESGroup:duration:
   *
   * The duration (in nanoseconds) which will be used in the container
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The duration to use", 0,
      G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESGroup:max-duration:
   *
   * The maximum duration (in nanoseconds) of the #GESGroup.
   */
  properties[PROP_MAX_DURATION] =
      g_param_spec_uint64 ("max-duration", "Maximum duration",
      "The maximum duration of the object", 0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | GES_PARAM_NO_SERIALIZATION);

  /**
   * GESTGroup:priority:
   *
   * The priority of the object.
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
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_GROUP, GESGroupPrivate);

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
 * Created a new empty #GESGroup, if you want to group several container
 * together, it is recommanded to use the #ges_container_group method so the
 * proper subclass is selected.
 *
 * Returns: The new empty group.
 */
GESGroup *
ges_group_new (void)
{
  return g_object_new (GES_TYPE_GROUP, NULL);
}
