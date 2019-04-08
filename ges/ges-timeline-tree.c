/* GStreamer Editing Services
 * Copyright (C) 2019 Igalia S.L
 *     Author: 2019 Thibault Saunier <tsaunier@igalia.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-timeline-tree.h"
#include "ges-internal.h"

GST_DEBUG_CATEGORY_STATIC (tree_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT tree_debug

#define ELEMENT_EDGE_VALUE(e, edge) ((edge == GES_EDGE_END) ? ((GstClockTimeDiff) _END (e)) : ((GstClockTimeDiff) _START (e)))
typedef struct
{
  GstClockTime distance;
  gboolean on_end_only;
  gboolean on_start_only;

  GESEdge edge;
  GESTimelineElement *element;

  GESTimelineElement *moving_element;
  GESEdge moving_edge;
  GstClockTimeDiff diff;
} SnappingData;

/*  *INDENT-OFF* */
struct _TreeIterationData
{
  GNode *root;
  gboolean res;

  GstClockTimeDiff start_diff;
  GstClockTimeDiff inpoint_diff;
  GstClockTimeDiff duration_diff;
  gint64 priority_diff;

  /* The element we are visiting */
  GESTimelineElement *element;

  /* All the TrackElement currently moving */
  GList *movings;

  /* Elements overlaping on the start/end of @element */
  GESTimelineElement *overlaping_on_start;
  GESTimelineElement *overlaping_on_end;

  /* Timestamp after which elements will be rippled */
  GstClockTime ripple_time;

  SnappingData *snapping;

  /* The edge being trimmed or rippled */
  GESEdge edge;
  GHashTable *moved_clips;

  GList *neighbours;
} tree_iteration_data_init = {
   .root = NULL,
   .res = TRUE,
   .start_diff = 0,
   .inpoint_diff = 0,
   .duration_diff = 0,
   .priority_diff = 0,
   .element = NULL,
   .movings = NULL,
   .overlaping_on_start = NULL,
   .overlaping_on_end = NULL,
   .ripple_time = GST_CLOCK_TIME_NONE,
   .snapping = NULL,
   .edge = GES_EDGE_NONE,
   .moved_clips = NULL,
   .neighbours = NULL,
};
/*  *INDENT-ON* */

typedef struct _TreeIterationData TreeIterationData;

static void
clean_iteration_data (TreeIterationData * data)
{
  g_list_free (data->neighbours);
  g_list_free (data->movings);
  if (data->moved_clips)
    g_hash_table_unref (data->moved_clips);
}

void
timeline_tree_init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (tree_debug, "gestree",
      GST_DEBUG_FG_YELLOW, "timeline tree");
}


static gboolean
print_node (GNode * node, gpointer unused_data)
{
  if (G_NODE_IS_ROOT (node)) {
    g_print ("Timeline: %p\n", node->data);
    return FALSE;
  }

  g_print ("%*c- %" GES_FORMAT " - layer %" G_GINT32_FORMAT "\n",
      2 * g_node_depth (node), ' ', GES_ARGS (node->data),
      ges_timeline_element_get_layer_priority (node->data));

  return FALSE;
}

void
timeline_tree_debug (GNode * root)
{
  g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_ALL, -1,
      (GNodeTraverseFunc) print_node, NULL);
}

static inline GESTimelineElement *
get_toplevel_container (gpointer element)
{
  GESTimelineElement *ret =
      ges_timeline_element_get_toplevel_parent ((GESTimelineElement
          *) (element));

  /*  We own a ref to the elements ourself */
  gst_object_unref (ret);
  return ret;
}

static gboolean
timeline_tree_can_move_element_internal (GNode * root,
    GESTimelineElement * element,
    gint64 priority,
    GstClockTimeDiff start,
    GstClockTimeDiff inpoint,
    GstClockTimeDiff duration,
    GList * moving_track_elements,
    GstClockTime ripple_time, SnappingData * snapping, GESEdge edge);

static GNode *
find_node (GNode * root, gpointer element)
{
  return g_node_find (root, G_IN_ORDER, G_TRAVERSE_ALL, element);
}

static void
timeline_element_parent_cb (GESTimelineElement * child, GParamSpec * arg
    G_GNUC_UNUSED, GNode * root)
{
  GNode *new_parent_node = NULL, *node = find_node (root, child);

  if (child->parent)
    new_parent_node = find_node (root, child->parent);

  if (!new_parent_node)
    new_parent_node = root;

  g_node_unlink (node);
  g_node_prepend (new_parent_node, node);
}

void
timeline_tree_track_element (GNode * root, GESTimelineElement * element)
{
  GNode *node;
  GNode *parent;
  GESTimelineElement *toplevel;

  if (find_node (root, element)) {
    return;
  }

  g_signal_connect (element, "notify::parent",
      G_CALLBACK (timeline_element_parent_cb), root);

  toplevel = get_toplevel_container (element);
  if (toplevel == element) {
    GST_DEBUG ("Tracking toplevel element %" GES_FORMAT, GES_ARGS (element));

    node = g_node_prepend_data (root, element);
  } else {
    parent = find_node (root, element->parent);
    GST_LOG ("%" GES_FORMAT "parent is %" GES_FORMAT, GES_ARGS (element),
        GES_ARGS (element->parent));
    g_assert (parent);
    node = g_node_prepend_data (parent, element);
  }

  if (GES_IS_CONTAINER (element)) {
    GList *tmp;

    for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
      GNode *child_node = find_node (root, tmp->data);

      if (child_node) {
        g_node_unlink (child_node);
        g_node_prepend (node, child_node);
      } else {
        timeline_tree_track_element (root, tmp->data);
      }
    }
  }

  timeline_update_duration (root->data);
}

void
timeline_tree_stop_tracking_element (GNode * root, GESTimelineElement * element)
{
  GNode *node = find_node (root, element);

  node = find_node (root, element);

  /* Move children to the parent */
  while (node->children) {
    GNode *tmp = node->children;
    g_node_unlink (tmp);
    g_node_prepend (node->parent, tmp);
  }

  g_assert (node);
  GST_DEBUG ("Stop tracking %" GES_FORMAT, GES_ARGS (element));
  g_signal_handlers_disconnect_by_func (element, timeline_element_parent_cb,
      root);

  g_node_destroy (node);
  timeline_update_duration (root->data);
}

static inline gboolean
check_can_move_to_layer (GESTimelineElement * element,
    gint layer_priority_offset)
{
  return (((gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (element) -
          layer_priority_offset) >= 0);
}

/*  *INDENT-OFF* */
#define CHECK_AND_SNAP(diff_val,moving_edge_,edge_) \
if (snapping->distance >= ABS(diff_val) && ABS(diff_val) <= ABS(snapping->diff)) { \
  snapping->element = element; \
  snapping->edge = edge_; \
  snapping->moving_element = moving_elem; \
  snapping->moving_edge = moving_edge_; \
  snapping->diff = (diff_val); \
  GST_LOG("Snapping %" GES_FORMAT "with %" GES_FORMAT " - diff: %" G_GINT64_FORMAT "", GES_ARGS (moving_elem), GES_ARGS(element), (diff_val)); \
}

static void
check_snapping (GESTimelineElement * element, GESTimelineElement * moving_elem,
    SnappingData * snapping, GstClockTime start, GstClockTime end,
    GstClockTime moving_start, GstClockTime moving_end)
{
  GstClockTimeDiff snap_end_end_diff;
  GstClockTimeDiff snap_end_start_diff;

  if (element == moving_elem)
    return;

  if (!snapping || (
      GES_IS_CLIP (element->parent) && element->parent == moving_elem->parent))
    return;

  snap_end_end_diff = (GstClockTimeDiff) moving_end - (GstClockTimeDiff) end;
  snap_end_start_diff = (GstClockTimeDiff) moving_end - (GstClockTimeDiff) start;

  GST_DEBUG("Moving [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT "] element [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT "]", moving_start, moving_end, start, end);
  /* Prefer snapping end */
  if (!snapping->on_start_only) {
    CHECK_AND_SNAP(snap_end_end_diff, GES_EDGE_END, GES_EDGE_END)
    else CHECK_AND_SNAP(snap_end_start_diff, GES_EDGE_END, GES_EDGE_START)
  }

  if (!snapping->on_end_only) {
    GstClockTimeDiff snap_start_end_diff = GST_CLOCK_DIFF(end, moving_start);
    GstClockTimeDiff snap_start_start_diff = GST_CLOCK_DIFF(start, moving_start);

    CHECK_AND_SNAP(snap_start_end_diff, GES_EDGE_START, GES_EDGE_END)
    else CHECK_AND_SNAP(snap_start_start_diff, GES_EDGE_START, GES_EDGE_START)
  }
}
#undef CHECK_AND_SNAP
/*  *INDENT-ON* */

static gboolean
check_track_elements_overlaps_and_values (GNode * node,
    TreeIterationData * data)
{
  GESTimelineElement *e = (GESTimelineElement *) node->data;
  GstClockTimeDiff moving_start, moving_end, start, inpoint, end, duration;
  gint64 priority = ((gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (e));
  gint64 moving_priority =
      (gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (data->element) -
      data->priority_diff;

  gboolean can_overlap = e != data->element, in_movings, rippling, moving;

  if (!GES_IS_SOURCE (e))
    return FALSE;

  in_movings = ! !g_list_find (data->movings, e);
  rippling = e != data->element && !in_movings
      && (GST_CLOCK_TIME_IS_VALID (data->ripple_time)
      && e->start >= data->ripple_time);
  moving = in_movings || rippling || e == data->element;

  start = (GstClockTimeDiff) e->start;
  inpoint = (GstClockTimeDiff) e->inpoint;
  end = (GstClockTimeDiff) e->start + e->duration;
  duration = e->duration;
  moving_start = GST_CLOCK_DIFF (data->start_diff, data->element->start);
  moving_end =
      GST_CLOCK_DIFF (data->duration_diff,
      moving_start + data->element->duration);

  if (moving) {
    if (rippling) {
      if (data->edge == GES_EDGE_END) {
        /* Moving as rippled from the end of a previous element */
        start -= data->duration_diff;
      } else
        start -= data->start_diff;
    } else {
      start -= data->start_diff;
      if (GES_TIMELINE_ELEMENT_GET_CLASS (e)->set_inpoint)
        inpoint -= data->inpoint_diff;
      duration -= data->duration_diff;
    }
    end = start + duration;
    priority -= data->priority_diff;

    GST_DEBUG ("%s %" GES_FORMAT "to [%" G_GINT64_FORMAT "(%"
        G_GINT64_FORMAT ") - %" G_GINT64_FORMAT "] - layer: %" G_GINT64_FORMAT,
        rippling ? "Rippling" : "Moving", GES_ARGS (e), start, inpoint,
        duration, priority);
  }

  /* Not in the same track */
  if (ges_track_element_get_track ((GESTrackElement *) node->data) !=
      ges_track_element_get_track ((GESTrackElement *) data->element)) {
    GST_LOG ("%" GES_FORMAT " and %" GES_FORMAT
        " are not in the same track", GES_ARGS (node->data),
        GES_ARGS (data->element));
    can_overlap = FALSE;
  }

  /* Not in the same layer */
  if (priority != moving_priority) {
    GST_LOG ("%" GST_PTR_FORMAT " and %" GST_PTR_FORMAT
        " are not on the same layer (%d != %" G_GINT64_FORMAT ")", node->data,
        data->element, GES_TIMELINE_ELEMENT_LAYER_PRIORITY (e),
        moving_priority);
    can_overlap = FALSE;
  }

  if (start < 0) {
    GST_INFO ("%" GES_FORMAT "start would be %" G_GINT64_FORMAT " < 0",
        GES_ARGS (e), start);
    goto error;
  }

  if (duration < 0) {
    GST_INFO ("%" GES_FORMAT "duration would be %" G_GINT64_FORMAT " < 0",
        GES_ARGS (e), duration);
    goto error;
  }

  if (priority < 0) {
    GST_INFO ("%" GES_FORMAT "priority would be %" G_GINT64_FORMAT " < 0",
        GES_ARGS (e), priority);
    goto error;
  }

  if (inpoint < 0) {
    GST_INFO ("%" GES_FORMAT " can't set inpoint %" G_GINT64_FORMAT,
        GES_ARGS (e), inpoint);
    goto error;
  }

  if (inpoint + duration > e->maxduration) {
    GST_INFO ("%" GES_FORMAT " inpoint + duration %" G_GINT64_FORMAT
        " > max_duration %" G_GINT64_FORMAT,
        GES_ARGS (e), inpoint + duration, e->maxduration);
    goto error;
  }

  if (!moving)
    check_snapping (e, data->element, data->snapping, start, end, moving_start,
        moving_end);

  if (!can_overlap)
    return FALSE;

  if (start > moving_end || moving_start > end) {
    /* They do not overlap at all */
    GST_LOG ("%" GES_FORMAT " and %" GES_FORMAT
        " do not overlap at all.",
        GES_ARGS (node->data), GES_ARGS (data->element));
    return FALSE;
  }

  if ((moving_start <= start && moving_end >= end) ||
      (moving_start >= start && moving_end <= end)) {
    GST_INFO ("Fully overlaped: %s<%p> [%" G_GINT64_FORMAT " - %"
        G_GINT64_FORMAT "] and %s<%p> [%" G_GINT64_FORMAT " - %" G_GINT64_FORMAT
        " (%" G_GINT64_FORMAT ")]", e->name, e, start, end, data->element->name,
        data->element, moving_start, moving_end, data->duration_diff);

    goto error;
  }

  if (moving_start < end && moving_start > start) {
    GST_LOG ("Overlap start: %s<%p> [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT
        "] and %s<%p> [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT " (%"
        G_GINT64_FORMAT ")]", e->name, e, start, end, data->element->name,
        data->element, moving_start, moving_end, data->duration_diff);
    if (data->overlaping_on_start) {
      GST_INFO ("Clip is overlapped by %s and %s at its start",
          data->overlaping_on_start->name, e->name);
      goto error;
    }

    data->overlaping_on_start = node->data;
  } else if (moving_end > end && end > moving_start) {
    GST_LOG ("Overlap end: %s<%p> [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT
        "] and %s<%p> [%" G_GINT64_FORMAT "-%" G_GINT64_FORMAT " (%"
        G_GINT64_FORMAT ")]", e->name, e, start, end, data->element->name,
        data->element, moving_start, moving_end, data->duration_diff);

    if (data->overlaping_on_end) {
      GST_INFO ("Clip is overlapped by %s and %s at its end",
          data->overlaping_on_end->name, e->name);
      goto error;
    }
    data->overlaping_on_end = node->data;
  }

  return FALSE;

error:
  data->res = FALSE;
  return TRUE;
}

static gboolean
check_can_move_children (GNode * node, TreeIterationData * data)
{
  GESTimelineElement *element = node->data;
  GstClockTimeDiff start = GST_CLOCK_DIFF (data->start_diff, element->start);
  GstClockTime inpoint = GST_CLOCK_DIFF (data->inpoint_diff, element->inpoint);
  GstClockTime duration =
      GST_CLOCK_DIFF (data->duration_diff, element->duration);
  gint64 priority =
      (gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (element) -
      data->priority_diff;
  if (element == data->element)
    return FALSE;

  data->res =
      timeline_tree_can_move_element_internal (data->root, node->data, priority,
      start, inpoint, duration, data->movings, data->ripple_time,
      data->snapping, data->edge);

  return !data->res;
}

static gboolean
timeline_tree_can_move_element_from_data (GNode * root,
    TreeIterationData * data)
{
  GNode *node = find_node (root, data->element);

  g_assert (node);
  if (G_NODE_IS_LEAF (node)) {
    if (GES_IS_SOURCE (node->data)) {
      g_node_traverse (root, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
          (GNodeTraverseFunc) check_track_elements_overlaps_and_values, data);

      return data->res;
    }

    return TRUE;
  }

  g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_LEAFS, -1,
      (GNodeTraverseFunc) check_can_move_children, data);

  return data->res;
}

static gboolean
add_element_to_list (GNode * node, GList ** elements)
{
  *elements = g_list_prepend (*elements, node->data);

  return FALSE;
}

static gboolean
timeline_tree_can_move_element_internal (GNode * root,
    GESTimelineElement * element, gint64 priority, GstClockTimeDiff start,
    GstClockTimeDiff inpoint, GstClockTimeDiff duration,
    GList * moving_track_elements, GstClockTime ripple_time,
    SnappingData * snapping, GESEdge edge)
{
  gboolean res;
  TreeIterationData data = tree_iteration_data_init;

  data.root = root;
  data.start_diff = GST_CLOCK_DIFF (start, element->start);
  data.inpoint_diff = GST_CLOCK_DIFF (inpoint, element->inpoint);
  data.duration_diff = GST_CLOCK_DIFF (duration, element->duration);
  data.priority_diff =
      (gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (element) - priority;
  data.element = element;
  data.movings = g_list_copy (moving_track_elements);
  data.ripple_time = ripple_time;
  data.snapping = snapping;
  data.edge = edge;

  if (GES_IS_SOURCE (element))
    data.priority_diff =
        GES_TIMELINE_ELEMENT_LAYER_PRIORITY (element) - priority;

  res = timeline_tree_can_move_element_from_data (root, &data);
  clean_iteration_data (&data);

  return res;
}

gboolean
timeline_tree_can_move_element (GNode * root,
    GESTimelineElement * element, guint32 priority, GstClockTime start,
    GstClockTime duration, GList * moving_track_elements)
{
  GESTimelineElement *toplevel;
  GstClockTimeDiff start_offset, duration_offset;
  gint64 priority_diff;
  gboolean res;
  GList *local_moving_track_elements = g_list_copy (moving_track_elements);

  toplevel = get_toplevel_container (element);
  if (ELEMENT_FLAG_IS_SET (element, GES_TIMELINE_ELEMENT_SET_SIMPLE) ||
      ELEMENT_FLAG_IS_SET (toplevel, GES_TIMELINE_ELEMENT_SET_SIMPLE))
    return TRUE;

  start_offset = GST_CLOCK_DIFF (start, element->start);
  duration_offset = GST_CLOCK_DIFF (duration, element->duration);
  priority_diff =
      (gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (toplevel) -
      (gint64) priority;

  g_node_traverse (find_node (root, toplevel), G_IN_ORDER,
      G_TRAVERSE_LEAVES, -1, (GNodeTraverseFunc) add_element_to_list,
      &local_moving_track_elements);

  res = timeline_tree_can_move_element_internal (root, toplevel,
      GES_TIMELINE_ELEMENT_LAYER_PRIORITY (toplevel) - priority_diff,
      GST_CLOCK_DIFF (start_offset, toplevel->start),
      toplevel->inpoint,
      GST_CLOCK_DIFF (duration_offset, toplevel->duration),
      local_moving_track_elements, GST_CLOCK_TIME_NONE, NULL, GES_EDGE_NONE);

  g_list_free (local_moving_track_elements);
  return res;
}

static void
move_to_new_layer (GESTimelineElement * elem, gint layer_priority_offset)
{
  guint32 nprio =
      GES_TIMELINE_ELEMENT_LAYER_PRIORITY (elem) - layer_priority_offset;
  GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (elem);

  if (!layer_priority_offset)
    return;

  GST_DEBUG ("%s moving from %" G_GUINT32_FORMAT " to %" G_GUINT32_FORMAT " (%"
      G_GUINT32_FORMAT ")", elem->name, elem->priority, nprio,
      layer_priority_offset);
  if (GES_IS_CLIP (elem)) {
    GESLayer *layer = ges_timeline_get_layer (timeline, nprio);

    if (layer == NULL) {
      do {
        layer = ges_timeline_append_layer (timeline);
      } while (ges_layer_get_priority (layer) < nprio);
    } else {
      gst_object_unref (layer);
    }

    ges_clip_move_to_layer (GES_CLIP (elem), layer);
  } else if (GES_IS_GROUP (elem)) {
    ges_timeline_element_set_priority (elem, nprio);
  } else {
    g_assert_not_reached ();
  }
}

gboolean
timeline_tree_ripple (GNode * root, gint64 layer_priority_offset,
    GstClockTimeDiff offset, GESTimelineElement * rippled_element,
    GESEdge edge, GstClockTime snapping_distance)
{
  GNode *node;
  GHashTableIter iter;
  GESTimelineElement *elem;
  GstClockTimeDiff start, duration;
  gboolean res = TRUE;
  GHashTable *to_move = g_hash_table_new (g_direct_hash, g_direct_equal);
  GList *moving_track_elements = NULL;
  SnappingData snapping = {
    .distance = snapping_distance,
    .on_end_only = edge == GES_EDGE_END,
    .on_start_only = FALSE,
    .element = NULL,
    .edge = GES_EDGE_NONE,
    .diff = (GstClockTimeDiff) snapping_distance,
  };
  gint64 new_layer_priority =
      ((gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (rippled_element)) -
      layer_priority_offset;
  GESTimelineElement *ripple_toplevel =
      get_toplevel_container (rippled_element);
  GstClockTimeDiff ripple_time = ELEMENT_EDGE_VALUE (rippled_element, edge);

  if (edge == GES_EDGE_END) {
    if (ripple_toplevel != rippled_element) {
      GST_FIXME ("Trying to ripple end %" GES_FORMAT " but in %" GES_FORMAT
          " we do not know how to do that yet!",
          GES_ARGS (rippled_element), GES_ARGS (ripple_toplevel));
      goto error;
    }
  } else {
    g_node_traverse (find_node (root, ripple_toplevel), G_IN_ORDER,
        G_TRAVERSE_LEAVES, -1, (GNodeTraverseFunc) add_element_to_list,
        &moving_track_elements);
  }

  GST_INFO ("Moving %" GES_FORMAT " with offset %" G_GINT64_FORMAT "",
      GES_ARGS (ripple_toplevel), offset);

  if (edge == GES_EDGE_END) {
    start = _START (rippled_element);
    duration = GST_CLOCK_DIFF (offset, _DURATION (rippled_element));
  } else {
    start = GST_CLOCK_DIFF (offset, _START (rippled_element));
    duration = _DURATION (rippled_element);
  }

  if (!timeline_tree_can_move_element_internal (root, rippled_element,
          new_layer_priority, start, rippled_element->inpoint, duration, NULL,
          ripple_time, snapping_distance ? &snapping : NULL, edge)) {
    goto error;
  }

  if (snapping_distance) {
    if (snapping.element) {
      offset =
          GST_CLOCK_DIFF (ELEMENT_EDGE_VALUE (snapping.element, snapping.edge),
          ELEMENT_EDGE_VALUE (snapping.moving_element, snapping.moving_edge));

      if (edge == GES_EDGE_END) {
        start = _START (rippled_element);
        duration = GST_CLOCK_DIFF (offset, _DURATION (rippled_element));
      } else {
        start = GST_CLOCK_DIFF (offset, _START (rippled_element));
        duration = _DURATION (rippled_element);
      }

      GST_INFO ("Snapping on %" GES_FORMAT "%s %" G_GINT64_FORMAT "",
          GES_ARGS (snapping.element),
          snapping.edge == GES_EDGE_END ? "end" : "start",
          ELEMENT_EDGE_VALUE (snapping.element, snapping.edge));
      if (!timeline_tree_can_move_element_internal (root, rippled_element,
              new_layer_priority, start, rippled_element->inpoint, duration,
              NULL, ripple_time, NULL, edge)) {
        goto error;
      }
    }

    ges_timeline_emit_snapping (root->data, rippled_element, snapping.element,
        snapping.element ? ELEMENT_EDGE_VALUE (snapping.element,
            snapping.edge) : GST_CLOCK_TIME_NONE);
  }

  /* Make sure we can ripple all toplevels after the rippled element */
  for (node = root->children; node; node = node->next) {
    GESTimelineElement *toplevel = get_toplevel_container (node->data);

    if (GES_TIMELINE_ELEMENT_START (toplevel) < ripple_time
        && (edge == GES_EDGE_END || toplevel != ripple_toplevel))
      continue;

    if (!timeline_tree_can_move_element_internal (root, node->data,
            ((gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (node->data)) -
            layer_priority_offset,
            GST_CLOCK_DIFF (offset, _START (node->data)),
            _INPOINT (node->data),
            _DURATION (node->data), moving_track_elements, ripple_time, NULL,
            GES_EDGE_NONE)) {
      goto error;
    }

    if (!check_can_move_to_layer (toplevel, layer_priority_offset)) {
      GST_INFO ("%" GES_FORMAT " would land in a layer with negative priority",
          GES_ARGS (toplevel));
      goto error;
    }

    g_hash_table_add (to_move, toplevel);
  }

  if (edge == GES_EDGE_END) {
    if (!check_can_move_to_layer (rippled_element, layer_priority_offset)) {
      GST_INFO ("%" GES_FORMAT " would land in a layer with negative priority",
          GES_ARGS (rippled_element));

      goto error;
    }

    if (duration < 0) {
      GST_INFO ("Would set duration to  %" G_GINT64_FORMAT " <= 0", duration);
      goto error;
    }

    ELEMENT_SET_FLAG (rippled_element, GES_TIMELINE_ELEMENT_SET_SIMPLE);
    ges_timeline_element_set_duration (rippled_element, duration);
    ELEMENT_UNSET_FLAG (rippled_element, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  }

  g_hash_table_iter_init (&iter, to_move);
  while (g_hash_table_iter_next (&iter, (gpointer *) & elem, NULL)) {
    GST_LOG ("Moving %" GES_FORMAT " to %" G_GINT64_FORMAT " - layer %"
        G_GINT64_FORMAT "", GES_ARGS (elem),
        GES_TIMELINE_ELEMENT_START (elem) - offset,
        (gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (elem) -
        layer_priority_offset);

    ELEMENT_SET_FLAG (elem, GES_TIMELINE_ELEMENT_SET_SIMPLE);
    ges_timeline_element_set_start (elem,
        GST_CLOCK_DIFF (offset, GES_TIMELINE_ELEMENT_START (elem)));
    move_to_new_layer (elem, layer_priority_offset);
    ELEMENT_UNSET_FLAG (elem, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  }

  ELEMENT_SET_FLAG (rippled_element, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  if (edge == GES_EDGE_END)
    move_to_new_layer (rippled_element, layer_priority_offset);
  ELEMENT_UNSET_FLAG (rippled_element, GES_TIMELINE_ELEMENT_SET_SIMPLE);

  timeline_tree_create_transitions (root, ges_timeline_find_auto_transition);
  timeline_update_transition (root->data);
  timeline_update_duration (root->data);

done:
  g_hash_table_unref (to_move);
  g_list_free (moving_track_elements);
  return res;

error:
  res = FALSE;
  goto done;
}

static gboolean
check_trim_child (GNode * node, TreeIterationData * data)
{
  GESTimelineElement *e = node->data;
  GstClockTimeDiff n_start = GST_CLOCK_DIFF (data->start_diff, e->start);
  GstClockTimeDiff n_inpoint = GST_CLOCK_DIFF (data->inpoint_diff, e->inpoint);
  GstClockTimeDiff n_duration = data->edge == GES_EDGE_END ?
      GST_CLOCK_DIFF (data->duration_diff, e->duration) :
      GST_CLOCK_DIFF (n_start, (GstClockTimeDiff) e->start + e->duration);

  if (!timeline_tree_can_move_element_internal (data->root, e,
          (gint64) ges_timeline_element_get_layer_priority (e) -
          data->priority_diff, n_start, n_inpoint, n_duration, NULL,
          GST_CLOCK_TIME_NONE, data->snapping, GES_EDGE_NONE))
    goto error;

  if (GES_IS_CLIP (e->parent))
    g_hash_table_add (data->moved_clips, e->parent);
  else if (GES_IS_CLIP (e))
    g_hash_table_add (data->moved_clips, e);

  return FALSE;

error:
  data->res = FALSE;

  return TRUE;
}

static gboolean
timeline_tree_can_trim_element_internal (GNode * root, TreeIterationData * data)
{
  g_node_traverse (find_node (root, data->element), G_IN_ORDER,
      G_TRAVERSE_LEAVES, -1, (GNodeTraverseFunc) check_trim_child, data);

  return data->res;
}

static void
trim_simple (GESTimelineElement * element, GstClockTimeDiff offset,
    GESEdge edge)
{
  ELEMENT_SET_FLAG (element, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  if (edge == GES_EDGE_END) {
    ges_timeline_element_set_duration (element, GST_CLOCK_DIFF (offset,
            element->duration));
  } else {
    ges_timeline_element_set_start (element, GST_CLOCK_DIFF (offset,
            element->start));
    ges_timeline_element_set_inpoint (element, GST_CLOCK_DIFF (offset,
            element->inpoint));
    ges_timeline_element_set_duration (element, element->duration + offset);
  }
  GST_LOG ("Trimmed %" GES_FORMAT, GES_ARGS (element));
  ELEMENT_UNSET_FLAG (element, GES_TIMELINE_ELEMENT_SET_SIMPLE);
}

#define SET_TRIMMING_DATA(data, _edge, offset) G_STMT_START { \
  data.edge = (_edge);                                           \
  data.start_diff = (_edge) == GES_EDGE_END ? 0 : (offset); \
  data.inpoint_diff = (_edge) == GES_EDGE_END ? 0 : (offset); \
  data.duration_diff = (_edge) == GES_EDGE_END ? (offset) : -(offset); \
} G_STMT_END


gboolean
timeline_tree_trim (GNode * root, GESTimelineElement * element,
    gint64 layer_priority_offset, GstClockTimeDiff offset, GESEdge edge,
    GstClockTime snapping_distance)
{
  GHashTableIter iter;
  gboolean res = TRUE;
  GESTimelineElement *elem;
  gint64 new_layer_priority =
      ((gint64) GES_TIMELINE_ELEMENT_LAYER_PRIORITY (element)) -
      layer_priority_offset;
  SnappingData snapping = {
    .distance = snapping_distance,
    .on_end_only = edge == GES_EDGE_END,
    .on_start_only = edge != GES_EDGE_END,
    .element = NULL,
    .edge = GES_EDGE_NONE,
    .diff = (GstClockTimeDiff) snapping_distance,
  };
  TreeIterationData data = tree_iteration_data_init;

  data.root = root;
  data.element = element;
  data.priority_diff =
      (gint64) ges_timeline_element_get_layer_priority (element) -
      new_layer_priority;
  data.snapping = snapping_distance ? &snapping : NULL;
  data.moved_clips = g_hash_table_new (g_direct_hash, g_direct_equal);

  SET_TRIMMING_DATA (data, edge, offset);
  GST_INFO ("%" GES_FORMAT " trimming %s with offset %" G_GINT64_FORMAT "",
      GES_ARGS (element), edge == GES_EDGE_END ? "end" : "start", offset);
  g_node_traverse (find_node (root, element), G_IN_ORDER,
      G_TRAVERSE_LEAVES, -1, (GNodeTraverseFunc) add_element_to_list,
      &data.movings);

  if (!timeline_tree_can_trim_element_internal (root, &data)) {
    GST_INFO ("Can not trim object.");
    goto error;
  }

  if (snapping_distance) {
    if (snapping.element) {
      offset =
          GST_CLOCK_DIFF (ELEMENT_EDGE_VALUE (snapping.element, snapping.edge),
          ELEMENT_EDGE_VALUE (snapping.moving_element, snapping.moving_edge));

      GST_INFO ("Snapping on %" GES_FORMAT "%s %" G_GINT64_FORMAT
          " -- offset: %" G_GINT64_FORMAT "", GES_ARGS (snapping.element),
          snapping.edge == GES_EDGE_END ? "end" : "start",
          ELEMENT_EDGE_VALUE (snapping.element, snapping.edge), offset);
    }

    ges_timeline_emit_snapping (root->data, element,
        snapping.element,
        snapping.element ? ELEMENT_EDGE_VALUE (snapping.element,
            snapping.edge) : GST_CLOCK_TIME_NONE);
  }

  g_hash_table_iter_init (&iter, data.moved_clips);
  while (g_hash_table_iter_next (&iter, (gpointer *) & elem, NULL))
    trim_simple (elem, offset, edge);

  timeline_tree_create_transitions (root, ges_timeline_find_auto_transition);
  timeline_update_transition (root->data);
  timeline_update_duration (root->data);

done:
  clean_iteration_data (&data);
  return res;

error:
  res = FALSE;
  goto done;
}

gboolean
timeline_tree_move (GNode * root, GESTimelineElement * element,
    gint64 layer_priority_offset, GstClockTimeDiff offset, GESEdge edge,
    GstClockTime snapping_distance)
{
  gboolean res = TRUE;
  GESTimelineElement *toplevel = get_toplevel_container (element);
  TreeIterationData data = tree_iteration_data_init;
  SnappingData snapping = {
    .distance = snapping_distance,
    .on_end_only = edge == GES_EDGE_END,
    .on_start_only = edge == GES_EDGE_END,
    .element = NULL,
    .edge = GES_EDGE_NONE,
    .diff = (GstClockTimeDiff) snapping_distance,
  };

  data.root = root;
  data.element = edge == GES_EDGE_END ? element : toplevel;
  data.edge = edge;
  data.priority_diff = layer_priority_offset;
  data.snapping = snapping_distance ? &snapping : NULL;
  data.start_diff = edge == GES_EDGE_END ? 0 : offset;
  data.duration_diff = edge == GES_EDGE_END ? offset : 0;

  GST_INFO ("%" GES_FORMAT
      " moving %s with offset %" G_GINT64_FORMAT ", (snaping distance: %"
      G_GINT64_FORMAT ")", GES_ARGS (element),
      edge == GES_EDGE_END ? "end" : "start", offset, snapping_distance);
  g_node_traverse (find_node (root, data.element), G_IN_ORDER,
      G_TRAVERSE_LEAVES, -1, (GNodeTraverseFunc) add_element_to_list,
      &data.movings);

  if (!timeline_tree_can_move_element_from_data (root, &data)) {
    GST_INFO ("Can not move object.");
    goto error;
  }

  if (snapping_distance) {
    if (snapping.element) {
      gint64 noffset =
          GST_CLOCK_DIFF (ELEMENT_EDGE_VALUE (snapping.element, snapping.edge),
          ELEMENT_EDGE_VALUE (snapping.moving_element, snapping.moving_edge));

      GST_INFO ("Snapping %" GES_FORMAT " (%s) with %" GES_FORMAT
          "%s %" G_GINT64_FORMAT " -- offset: %" G_GINT64_FORMAT
          " (previous offset: %" G_GINT64_FORMAT ")",
          GES_ARGS (snapping.moving_element),
          snapping.moving_edge == GES_EDGE_END ? "end" : "start",
          GES_ARGS (snapping.element),
          snapping.edge == GES_EDGE_END ? "end" : "start",
          ELEMENT_EDGE_VALUE (snapping.element, snapping.edge), noffset,
          offset);
      offset = noffset;
      data.start_diff = edge == GES_EDGE_END ? 0 : offset;
      data.duration_diff = edge == GES_EDGE_END ? offset : 0;
      data.snapping = NULL;
      if (!timeline_tree_can_move_element_from_data (root, &data)) {
        GST_INFO ("Can not move object.");
        goto error;
      }
    }

    ges_timeline_emit_snapping (root->data, element,
        snapping.element,
        snapping.element ? ELEMENT_EDGE_VALUE (snapping.element,
            snapping.edge) : GST_CLOCK_TIME_NONE);
  }

  if (!check_can_move_to_layer (toplevel, layer_priority_offset)) {
    GST_INFO ("%" GES_FORMAT " would land in a layer with negative priority",
        GES_ARGS (toplevel));
    goto error;
  }

  ELEMENT_SET_FLAG (toplevel, GES_TIMELINE_ELEMENT_SET_SIMPLE);
  if (edge == GES_EDGE_END)
    ges_timeline_element_set_duration (element, GST_CLOCK_DIFF (offset,
            element->duration));
  else
    ges_timeline_element_set_start (toplevel, GST_CLOCK_DIFF (offset,
            toplevel->start));
  move_to_new_layer (toplevel, layer_priority_offset);
  ELEMENT_UNSET_FLAG (toplevel, GES_TIMELINE_ELEMENT_SET_SIMPLE);

  timeline_tree_create_transitions (root, ges_timeline_find_auto_transition);
  timeline_update_transition (root->data);
  timeline_update_duration (root->data);

  GST_LOG ("Moved %" GES_FORMAT, GES_ARGS (element));

done:
  clean_iteration_data (&data);
  return res;

error:
  res = FALSE;
  goto done;
}

static gboolean
find_neighbour (GNode * node, TreeIterationData * data)
{
  gboolean in_same_track = FALSE;
  GList *tmp;

  if (!GES_IS_SOURCE (node->data)) {
    return FALSE;
  }


  for (tmp = GES_CONTAINER_CHILDREN (data->element); tmp; tmp = tmp->next) {
    if (tmp->data == node->data)
      return FALSE;

    if (ges_track_element_get_track (node->data) ==
        ges_track_element_get_track (tmp->data)) {
      in_same_track = TRUE;
    }
  }

  if (!in_same_track) {
    return FALSE;
  }

  if (ELEMENT_EDGE_VALUE (node->data,
          data->edge == GES_EDGE_START ? GES_EDGE_END : GES_EDGE_START) ==
      ELEMENT_EDGE_VALUE (data->element, data->edge)) {
    if (!g_list_find (data->neighbours,
            GES_TIMELINE_ELEMENT_PARENT (node->data)))
      data->neighbours =
          g_list_prepend (data->neighbours,
          GES_TIMELINE_ELEMENT_PARENT (node->data));
  }

  return FALSE;
}

gboolean
timeline_tree_roll (GNode * root, GESTimelineElement * element,
    GstClockTimeDiff offset, GESEdge edge, GstClockTime snapping_distance)
{
  gboolean res = TRUE;
  GList *tmp;
  GESEdge neighbour_edge;
  TreeIterationData data = tree_iteration_data_init;
  SnappingData snapping = {
    .distance = snapping_distance,
    .on_end_only = edge == GES_EDGE_END,
    .on_start_only = edge == GES_EDGE_END,
    .element = NULL,
    .edge = GES_EDGE_NONE,
    .moving_edge = GES_EDGE_NONE,
    .diff = (GstClockTimeDiff) snapping_distance,
  };

  data.root = root;
  data.element = element;
  data.edge = edge;
  data.snapping = snapping_distance ? &snapping : NULL;
  data.start_diff = edge == GES_EDGE_END ? 0 : offset;
  data.inpoint_diff = edge == GES_EDGE_END ? 0 : offset;
  data.duration_diff = edge == GES_EDGE_END ? offset : -offset;
  data.ripple_time = GST_CLOCK_TIME_NONE;
  neighbour_edge = data.edge == GES_EDGE_END ? GES_EDGE_START : GES_EDGE_END;

  SET_TRIMMING_DATA (data, edge, offset);
  g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_LEAFS, -1,
      (GNodeTraverseFunc) find_neighbour, &data);

  if (data.neighbours == NULL) {
    GST_INFO ("%s doesn't have any direct neighbour on edge %s",
        element->name, ges_edge_name (edge));

    return timeline_tree_trim (root, element, 0, offset, edge,
        snapping_distance);
  }

  GST_INFO ("Trimming %" GES_FORMAT " %s to %" G_GINT64_FORMAT "",
      GES_ARGS (data.element), ges_edge_name (edge), offset);

  if (!timeline_tree_can_move_element_from_data (root, &data))
    goto error;


  if (snapping_distance) {
    if (snapping.element) {
      gint64 noffset =
          GST_CLOCK_DIFF (ELEMENT_EDGE_VALUE (snapping.element, snapping.edge),
          ELEMENT_EDGE_VALUE (snapping.moving_element, snapping.moving_edge));

      GST_INFO ("Snapping %" GES_FORMAT " (%s) with %" GES_FORMAT
          "%s %" G_GINT64_FORMAT " -- offset: %" G_GINT64_FORMAT
          " (previous offset: %" G_GINT64_FORMAT ")",
          GES_ARGS (snapping.moving_element),
          snapping.moving_edge == GES_EDGE_END ? "end" : "start",
          GES_ARGS (snapping.element),
          snapping.edge == GES_EDGE_END ? "end" : "start",
          ELEMENT_EDGE_VALUE (snapping.element, snapping.edge), noffset,
          offset);
      offset = noffset;

      SET_TRIMMING_DATA (data, edge, offset);

      if (!timeline_tree_can_move_element_from_data (root, &data)) {
        GST_INFO ("Can not move object.");
        goto error;
      }
    }
  }

  if (snapping_distance && snapping.element) {
    ges_timeline_emit_snapping (root->data, element,
        snapping.element,
        snapping.element ? ELEMENT_EDGE_VALUE (snapping.element,
            snapping.edge) : GST_CLOCK_TIME_NONE);
  }

  data.snapping = NULL;
  SET_TRIMMING_DATA (data, neighbour_edge, offset);
  for (tmp = data.neighbours; tmp; tmp = tmp->next) {
    data.element = tmp->data;

    GST_INFO ("Trimming %" GES_FORMAT " %s to %" G_GINT64_FORMAT "",
        GES_ARGS (data.element), ges_edge_name (data.edge), offset);
    if (!timeline_tree_can_move_element_from_data (root, &data)) {
      GST_INFO ("Can not move object.");
      goto error;
    }
  }

  trim_simple (element, offset, edge);
  for (tmp = data.neighbours; tmp; tmp = tmp->next)
    trim_simple (tmp->data, offset, data.edge);

done:
  timeline_update_duration (root->data);
  g_list_free (data.neighbours);
  return res;

error:
  res = FALSE;
  goto done;
}

static void
create_transition_if_needed (GESTimeline * timeline, GESTrackElement * prev,
    GESTrackElement * next, GESTreeGetAutoTransitionFunc get_auto_transition)
{
  GstClockTime duration = _END (prev) - _START (next);
  GESAutoTransition *trans =
      get_auto_transition (timeline, prev, next, duration);

  if (!trans) {
    GESLayer *layer = ges_timeline_get_layer (timeline,
        GES_TIMELINE_ELEMENT_LAYER_PRIORITY (prev));
    gst_object_unref (layer);

    GST_INFO ("Creating transition [%" G_GINT64_FORMAT " - %" G_GINT64_FORMAT
        "]", _START (next), duration);
    ges_timeline_create_transition (timeline, prev, next, NULL, layer,
        _START (next), duration);
  }
}

static gboolean
create_transitions (GNode * node,
    GESTreeGetAutoTransitionFunc get_auto_transition)
{
  TreeIterationData data = tree_iteration_data_init;
  GESTimeline *timeline;
  GESLayer *layer;

  if (G_NODE_IS_ROOT (node))
    return FALSE;

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (node->data);
  data.element = node->data;
  if (!GES_IS_SOURCE (node->data))
    return FALSE;

  if (!timeline) {
    GST_INFO ("%" GES_FORMAT " not in timeline yet", GES_ARGS (node->data));

    return FALSE;
  }

  layer =
      ges_timeline_get_layer (timeline,
      GES_TIMELINE_ELEMENT_LAYER_PRIORITY (node->data));
  gst_object_unref (layer);

  if (!ges_layer_get_auto_transition (layer))
    return FALSE;

  g_node_traverse (g_node_get_root (node), G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
      (GNodeTraverseFunc) check_track_elements_overlaps_and_values, &data);

  if (data.overlaping_on_start)
    create_transition_if_needed (timeline,
        GES_TRACK_ELEMENT (data.overlaping_on_start), node->data,
        get_auto_transition);

  if (data.overlaping_on_end)
    create_transition_if_needed (timeline, node->data,
        GES_TRACK_ELEMENT (data.overlaping_on_end), get_auto_transition);

  return FALSE;
}

void
timeline_tree_create_transitions (GNode * root,
    GESTreeGetAutoTransitionFunc get_auto_transition)
{
  g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_LEAFS, -1,
      (GNodeTraverseFunc) create_transitions, get_auto_transition);
}

static gboolean
compute_duration (GNode * node, GstClockTime * duration)
{
  *duration = MAX (_END (node->data), *duration);

  return FALSE;
}

GstClockTime
timeline_tree_get_duration (GNode * root)
{
  GstClockTime duration = 0;

  if (root->children)
    g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_LEAFS, -1,
        (GNodeTraverseFunc) compute_duration, &duration);

  return duration;
}
