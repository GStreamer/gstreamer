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
#include "ges-marker-list.h"

GST_DEBUG_CATEGORY_STATIC (tree_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT tree_debug

#define ELEMENT_EDGE_VALUE(e, edge) ((edge == GES_EDGE_END) ? _END (e) : _START (e))

typedef struct _SnappedPosition
{
  /* the element that was being snapped */
  GESTrackElement *element;
  /* the position of element, and whether it is a negative position */
  gboolean negative;
  GstClockTime position;
  /* the element that was snapped to */
  GESTrackElement *snapped_to;
  /* the snapped positioned */
  GstClockTime snapped;
  /* the distance below which two elements can snap */
  GstClockTime distance;
} SnappedPosition;

typedef enum
{
  EDIT_MOVE,
  EDIT_TRIM_START,
  EDIT_TRIM_END,
  EDIT_TRIM_INPOINT_ONLY,
} ElementEditMode;

typedef struct _EditData
{
  /* offsets to use */
  GstClockTime offset;
  gint64 layer_offset;
  /* actual values */
  GstClockTime duration;
  GstClockTime start;
  GstClockTime inpoint;
  guint32 layer_priority;
  /* mode */
  ElementEditMode mode;
} EditData;

typedef struct _PositionData
{
  guint32 layer_priority;
  GstClockTime start;
  GstClockTime end;
} PositionData;

/*  *INDENT-OFF* */
struct _TreeIterationData
{
  GNode *root;
  gboolean res;
  /* an error to set */
  GError **error;

  /* The element we are visiting */
  GESTimelineElement *element;
  /* the position data of the visited element */
  PositionData *pos_data;

  /* All the TrackElement currently moving: owned by data */
  GHashTable *moving;

  /* Elements overlaping on the start/end of @element */
  GESTimelineElement *overlaping_on_start;
  GESTimelineElement *overlaping_on_end;
  GstClockTime overlap_start_final_time;
  GstClockTime overlap_end_first_time;

  SnappedPosition *snap;
  GList *sources;
  GstClockTime position;
  GstClockTime negative;

  GESEdge edge;
  GList *neighbours;
} tree_iteration_data_init = {
   .root = NULL,
   .res = TRUE,
   .element = NULL,
   .pos_data = NULL,
   .moving = NULL,
   .overlaping_on_start = NULL,
   .overlaping_on_end = NULL,
   .overlap_start_final_time = GST_CLOCK_TIME_NONE,
   .overlap_end_first_time = GST_CLOCK_TIME_NONE,
   .snap = NULL,
   .sources = NULL,
   .position = GST_CLOCK_TIME_NONE,
   .negative = FALSE,
   .edge = GES_EDGE_NONE,
   .neighbours = NULL,
};
/*  *INDENT-ON* */

typedef struct _TreeIterationData TreeIterationData;

static EditData *
new_edit_data (ElementEditMode mode, GstClockTimeDiff offset,
    gint64 layer_offset)
{
  EditData *data = g_new (EditData, 1);

  data->start = GST_CLOCK_TIME_NONE;
  data->duration = GST_CLOCK_TIME_NONE;
  data->inpoint = GST_CLOCK_TIME_NONE;
  data->layer_priority = GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY;

  data->mode = mode;
  data->offset = offset;
  data->layer_offset = layer_offset;

  return data;
}

static SnappedPosition *
new_snapped_position (GstClockTime distance)
{
  SnappedPosition *snap;

  if (distance == 0)
    return NULL;

  snap = g_new0 (SnappedPosition, 1);
  snap->position = GST_CLOCK_TIME_NONE;
  snap->snapped = GST_CLOCK_TIME_NONE;
  snap->distance = distance;

  return snap;
}

static GHashTable *
new_edit_table ()
{
  return g_hash_table_new_full (NULL, NULL, NULL, g_free);
}

static GHashTable *
new_position_table ()
{
  return g_hash_table_new_full (NULL, NULL, NULL, g_free);
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
    gst_print ("Timeline: %p\n", node->data);
    return FALSE;
  }

  gst_print ("%*c- %" GES_FORMAT " - layer %" G_GINT32_FORMAT "\n",
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

  toplevel = ges_timeline_element_peak_toplevel (element);
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

/****************************************************
 *     GstClockTime with over/underflow checking    *
 ****************************************************/

static GstClockTime
_clock_time_plus (GstClockTime time, GstClockTime add)
{
  if (!GST_CLOCK_TIME_IS_VALID (time) || !GST_CLOCK_TIME_IS_VALID (add))
    return GST_CLOCK_TIME_NONE;

  if (time >= (G_MAXUINT64 - add)) {
    GST_ERROR ("The time %" G_GUINT64_FORMAT " would overflow when "
        "adding %" G_GUINT64_FORMAT, time, add);
    return GST_CLOCK_TIME_NONE;
  }
  return time + add;
}

static GstClockTime
_clock_time_minus (GstClockTime time, GstClockTime minus, gboolean * negative)
{
  if (negative)
    *negative = FALSE;

  if (!GST_CLOCK_TIME_IS_VALID (time) || !GST_CLOCK_TIME_IS_VALID (minus))
    return GST_CLOCK_TIME_NONE;

  if (time < minus) {
    if (negative) {
      *negative = TRUE;
      return minus - time;
    }
    /* otherwise don't allow negative */
    GST_INFO ("The time %" G_GUINT64_FORMAT " would underflow when "
        "subtracting %" G_GUINT64_FORMAT, time, minus);
    return GST_CLOCK_TIME_NONE;
  }
  return time - minus;
}

static GstClockTime
_clock_time_minus_diff (GstClockTime time, GstClockTimeDiff diff,
    gboolean * negative)
{
  if (negative)
    *negative = FALSE;

  if (!GST_CLOCK_TIME_IS_VALID (time))
    return GST_CLOCK_TIME_NONE;

  if (diff < 0)
    return _clock_time_plus (time, -diff);
  else
    return _clock_time_minus (time, diff, negative);
}

static GstClockTime
_abs_clock_time_distance (GstClockTime time1, GstClockTime time2)
{
  if (!GST_CLOCK_TIME_IS_VALID (time1) || !GST_CLOCK_TIME_IS_VALID (time2))
    return GST_CLOCK_TIME_NONE;
  if (time1 > time2)
    return time1 - time2;
  else
    return time2 - time1;
}

static void
get_start_end_from_offset (GESTimelineElement * element, ElementEditMode mode,
    GstClockTimeDiff offset, GstClockTime * start, gboolean * negative_start,
    GstClockTime * end, gboolean * negative_end)
{
  GstClockTime current_end =
      _clock_time_plus (element->start, element->duration);
  GstClockTime new_start = GST_CLOCK_TIME_NONE, new_end = GST_CLOCK_TIME_NONE;

  switch (mode) {
    case EDIT_MOVE:
      new_start =
          _clock_time_minus_diff (element->start, offset, negative_start);
      new_end = _clock_time_minus_diff (current_end, offset, negative_end);
      break;
    case EDIT_TRIM_START:
      new_start =
          _clock_time_minus_diff (element->start, offset, negative_start);
      new_end = current_end;
      if (negative_end)
        *negative_end = FALSE;
      break;
    case EDIT_TRIM_END:
      new_start = element->start;
      if (negative_start)
        *negative_start = FALSE;
      new_end = _clock_time_minus_diff (current_end, offset, negative_end);
      break;
    case EDIT_TRIM_INPOINT_ONLY:
      GST_ERROR_OBJECT (element, "Trim in-point only not handled");
      break;
  }
  if (start)
    *start = new_start;
  if (end)
    *end = new_end;
}

/****************************************************
 *                   Snapping                       *
 ****************************************************/

static void
snap_to_marker (GESTrackElement * element, GstClockTime position,
    gboolean negative, GstClockTime marker_timestamp,
    GESTrackElement * marker_parent, SnappedPosition * snap)
{
  GstClockTime distance;

  if (negative)
    distance = _clock_time_plus (position, marker_timestamp);
  else
    distance = _abs_clock_time_distance (position, marker_timestamp);

  if (GST_CLOCK_TIME_IS_VALID (distance) && distance <= snap->distance) {
    snap->negative = negative;
    snap->position = position;
    snap->distance = distance;
    snap->snapped = marker_timestamp;
    snap->element = element;
    snap->snapped_to = marker_parent;
  }
}

static void
snap_to_edge (GESTrackElement * element, GstClockTime position,
    gboolean negative, GESTrackElement * snap_to, GESEdge edge,
    SnappedPosition * snap)
{
  GstClockTime edge_pos = ELEMENT_EDGE_VALUE (snap_to, edge);
  GstClockTime distance;

  if (negative)
    distance = _clock_time_plus (position, edge_pos);
  else
    distance = _abs_clock_time_distance (position, edge_pos);

  if (GST_CLOCK_TIME_IS_VALID (distance) && distance <= snap->distance) {
    GESTimelineElement *parent = GES_TIMELINE_ELEMENT_PARENT (element);
    GESTimelineElement *snap_parent = GES_TIMELINE_ELEMENT_PARENT (snap_to);
    GST_LOG_OBJECT (element, "%s (under %s) snapped with %" GES_FORMAT
        "(under %s) from position %s%" GST_TIME_FORMAT " to %"
        GST_TIME_FORMAT, GES_TIMELINE_ELEMENT_NAME (element),
        parent ? parent->name : NULL, GES_ARGS (snap_to),
        snap_parent ? snap_parent->name : NULL, negative ? "-" : "",
        GST_TIME_ARGS (position), GST_TIME_ARGS (edge_pos));
    snap->negative = negative;
    snap->position = position;
    snap->distance = distance;
    snap->snapped = edge_pos;
    snap->element = element;
    snap->snapped_to = snap_to;
  }
}

static void
find_marker_snap (const GESMetaContainer * container, const gchar * key,
    const GValue * value, TreeIterationData * data)
{
  GESTrackElement *marker_parent, *moving;
  GESClip *parent_clip;
  GstClockTime timestamp;
  GESMarkerList *marker_list;
  GESMarker *marker;
  GESMarkerFlags flags;
  GObject *obj;

  if (!G_VALUE_HOLDS_OBJECT (value))
    return;

  obj = g_value_get_object (value);
  if (!GES_IS_MARKER_LIST (obj))
    return;

  marker_list = GES_MARKER_LIST (obj);

  g_object_get (marker_list, "flags", &flags, NULL);
  if (!(flags & GES_MARKER_FLAG_SNAPPABLE))
    return;

  marker_parent = GES_TRACK_ELEMENT ((gpointer) container);
  moving = GES_TRACK_ELEMENT (data->element);
  parent_clip = (GESClip *) GES_TIMELINE_ELEMENT_PARENT (marker_parent);

  /* Translate current position into the target clip's time domain */
  timestamp =
      ges_clip_get_internal_time_from_timeline_time (parent_clip, marker_parent,
      data->position, NULL);
  marker = ges_marker_list_get_closest (marker_list, timestamp);

  if (marker == NULL)
    return;

  /* Make timestamp timeline-relative again */
  g_object_get (marker, "position", &timestamp, NULL);
  timestamp =
      ges_clip_get_timeline_time_from_internal_time (parent_clip, marker_parent,
      timestamp, NULL);
  snap_to_marker (moving, data->position, data->negative, timestamp,
      marker_parent, data->snap);

  g_object_unref (marker);
}

static gboolean
find_snap (GNode * node, TreeIterationData * data)
{
  GESTimelineElement *element = node->data;
  GESTrackElement *track_el, *moving;

  /* Only snap to sources */
  /* Maybe we should allow snapping to anything that isn't an
   * auto-transition? */
  if (!GES_IS_SOURCE (element))
    return FALSE;

  /* don't snap to anything we are moving */
  if (g_hash_table_contains (data->moving, element))
    return FALSE;

  track_el = GES_TRACK_ELEMENT (element);
  moving = GES_TRACK_ELEMENT (data->element);
  snap_to_edge (moving, data->position, data->negative, track_el,
      GES_EDGE_END, data->snap);
  snap_to_edge (moving, data->position, data->negative, track_el,
      GES_EDGE_START, data->snap);

  ges_meta_container_foreach (GES_META_CONTAINER (element),
      (GESMetaForeachFunc) find_marker_snap, data);

  return FALSE;
}

static void
find_snap_for_element (GESTrackElement * element, GstClockTime position,
    gboolean negative, TreeIterationData * data)
{
  data->element = GES_TIMELINE_ELEMENT (element);
  data->position = position;
  data->negative = negative;
  g_node_traverse (data->root, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
      (GNodeTraverseFunc) find_snap, data);
}

/* find up to one source at the edge */
static gboolean
find_source_at_edge (GNode * node, TreeIterationData * data)
{
  GESEdge edge = data->edge;
  GESTimelineElement *element = node->data;
  GESTimelineElement *ancestor = data->element;

  if (!GES_IS_SOURCE (element))
    return FALSE;

  if (ELEMENT_EDGE_VALUE (element, edge) == ELEMENT_EDGE_VALUE (ancestor, edge)) {
    data->sources = g_list_append (data->sources, element);
    return TRUE;
  }
  return FALSE;
}

static gboolean
find_sources (GNode * node, TreeIterationData * data)
{
  GESTimelineElement *element = node->data;
  if (GES_IS_SOURCE (element))
    data->sources = g_list_append (data->sources, element);
  return FALSE;
}

/* Tries to find a new snap to the start or end edge of one of the
 * descendant sources of @element, depending on @mode, and updates @offset
 * by the size of the jump.
 * Any elements in @moving are not snapped to.
 */
static gboolean
timeline_tree_snap (GNode * root, GESTimelineElement * element,
    ElementEditMode mode, GstClockTimeDiff * offset, GHashTable * moving,
    SnappedPosition * snap)
{
  gboolean ret = FALSE;
  TreeIterationData data = tree_iteration_data_init;
  GList *tmp;
  GNode *node;

  if (!snap)
    return TRUE;

  /* get the sources we can snap to */
  data.root = root;
  data.moving = moving;
  data.sources = NULL;
  data.snap = snap;
  data.element = element;

  node = find_node (root, element);

  if (!node) {
    GST_ERROR_OBJECT (element, "Not being tracked");
    goto done;
  }

  switch (mode) {
    case EDIT_MOVE:
      /* can snap with any source below the element, if any */
      g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
          (GNodeTraverseFunc) find_sources, &data);
      break;
    case EDIT_TRIM_START:
      /* can only snap with sources at the start of the element.
       * only need one such source since all will share the same start.
       * if there is no source at the start edge, then snapping is not
       * possible */
      data.edge = GES_EDGE_START;
      g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
          (GNodeTraverseFunc) find_source_at_edge, &data);
      break;
    case EDIT_TRIM_END:
      /* can only snap with sources at the end of the element.
       * only need one such source since all will share the same end.
       * if there is no source at the end edge, then snapping is not
       * possible */
      data.edge = GES_EDGE_END;
      g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
          (GNodeTraverseFunc) find_source_at_edge, &data);
      break;
    case EDIT_TRIM_INPOINT_ONLY:
      GST_ERROR_OBJECT (element, "Trim in-point only not handled");
      goto done;
  }

  for (tmp = data.sources; tmp; tmp = tmp->next) {
    GESTrackElement *source = tmp->data;
    GstClockTime end, start;
    gboolean negative_end, negative_start;

    /* Allow negative start/end positions in case a snap makes them valid!
     * But we can still only snap to an existing edge in the timeline,
     * which should be a valid time */
    get_start_end_from_offset (GES_TIMELINE_ELEMENT (source), mode, *offset,
        &start, &negative_start, &end, &negative_end);

    if (!GST_CLOCK_TIME_IS_VALID (start)) {
      GST_INFO_OBJECT (element, "Cannot edit element %" GES_FORMAT
          " with offset %" G_GINT64_FORMAT " because it would result in "
          "an invalid start", GES_ARGS (element), *offset);
      goto done;
    }

    if (!GST_CLOCK_TIME_IS_VALID (end)) {
      GST_INFO_OBJECT (element, "Cannot edit element %" GES_FORMAT
          " with offset %" G_GINT64_FORMAT " because it would result in "
          "an invalid end", GES_ARGS (element), *offset);
      goto done;
    }

    switch (mode) {
      case EDIT_MOVE:
        /* try snap start and end */
        find_snap_for_element (source, end, negative_end, &data);
        find_snap_for_element (source, start, negative_start, &data);
        break;
      case EDIT_TRIM_START:
        /* only snap the start of the source */
        find_snap_for_element (source, start, negative_start, &data);
        break;
      case EDIT_TRIM_END:
        /* only snap the start of the source */
        find_snap_for_element (source, end, negative_end, &data);
        break;
      case EDIT_TRIM_INPOINT_ONLY:
        GST_ERROR_OBJECT (element, "Trim in-point only not handled");
        goto done;
    }
  }

  if (GST_CLOCK_TIME_IS_VALID (snap->snapped)) {
    if (snap->negative)
      *offset -= (snap->position + snap->snapped);
    else
      *offset += (snap->position - snap->snapped);
    GST_INFO_OBJECT (element, "Element %s under %s snapped with %" GES_FORMAT
        " from %s%" GST_TIME_FORMAT " to %" GST_TIME_FORMAT,
        GES_TIMELINE_ELEMENT_NAME (snap->element), element->name,
        GES_ARGS (snap->snapped_to), snap->negative ? "-" : "",
        GST_TIME_ARGS (snap->position), GST_TIME_ARGS (snap->snapped));
  } else {
    GST_INFO_OBJECT (element, "Nothing within snapping distance of %s",
        element->name);
  }

  ret = TRUE;

done:
  g_list_free (data.sources);

  return ret;
}

/****************************************************
 *                 Check Overlaps                   *
 ****************************************************/

#define _SOURCE_FORMAT "\"%s\"%s%s%s"
#define _SOURCE_ARGS(element) \
  element->name, element->parent ? " (parent: \"" : "", \
  element->parent ? element->parent->name : "", \
  element->parent ? "\")" : ""

static void
set_full_overlap_error (GError ** error, GESTimelineElement * super,
    GESTimelineElement * sub, GESTrack * track)
{
  if (error) {
    gchar *track_name = gst_object_get_name (GST_OBJECT (track));
    g_set_error (error, GES_ERROR, GES_ERROR_INVALID_OVERLAP_IN_TRACK,
        "The source " _SOURCE_FORMAT " would totally overlap the "
        "source " _SOURCE_FORMAT " in the track \"%s\"", _SOURCE_ARGS (super),
        _SOURCE_ARGS (sub), track_name);
    g_free (track_name);
  }
}

static void
set_triple_overlap_error (GError ** error, GESTimelineElement * first,
    GESTimelineElement * second, GESTimelineElement * third, GESTrack * track)
{
  if (error) {
    gchar *track_name = gst_object_get_name (GST_OBJECT (track));
    g_set_error (error, GES_ERROR, GES_ERROR_INVALID_OVERLAP_IN_TRACK,
        "The sources " _SOURCE_FORMAT ", " _SOURCE_FORMAT " and "
        _SOURCE_FORMAT " would all overlap at the same point in the "
        "track \"%s\"", _SOURCE_ARGS (first), _SOURCE_ARGS (second),
        _SOURCE_ARGS (third), track_name);
    g_free (track_name);
  }
}

#define _ELEMENT_FORMAT \
  "%s (under %s) [%" GST_TIME_FORMAT " - %" GST_TIME_FORMAT "] " \
  "(layer: %" G_GUINT32_FORMAT ") (track :%" GST_PTR_FORMAT ")"
#define _E_ARGS e->name, e->parent ? e->parent->name : NULL, \
  GST_TIME_ARGS (start), GST_TIME_ARGS (end), layer_prio, track
#define _CMP_ARGS cmp->name, cmp->parent ? cmp->parent->name : NULL, \
  GST_TIME_ARGS (cmp_start), GST_TIME_ARGS (cmp_end), cmp_layer_prio, \
  cmp_track

static gboolean
check_overlap_with_element (GNode * node, TreeIterationData * data)
{
  GESTimelineElement *e = node->data, *cmp = data->element;
  GstClockTime start, end, cmp_start, cmp_end;
  guint32 layer_prio, cmp_layer_prio;
  GESTrack *track, *cmp_track;
  PositionData *pos_data;

  if (e == cmp)
    return FALSE;

  if (!GES_IS_SOURCE (e) || !GES_IS_SOURCE (cmp))
    return FALSE;

  /* get position of compared element */
  pos_data = data->pos_data;
  if (pos_data) {
    cmp_start = pos_data->start;
    cmp_end = pos_data->end;
    cmp_layer_prio = pos_data->layer_priority;
  } else {
    cmp_start = cmp->start;
    cmp_end = cmp_start + cmp->duration;
    cmp_layer_prio = ges_timeline_element_get_layer_priority (cmp);
  }

  /* get position of the node */
  if (data->moving)
    pos_data = g_hash_table_lookup (data->moving, e);
  else
    pos_data = NULL;

  if (pos_data) {
    start = pos_data->start;
    end = pos_data->end;
    layer_prio = pos_data->layer_priority;
  } else {
    start = e->start;
    end = start + e->duration;
    layer_prio = ges_timeline_element_get_layer_priority (e);
  }

  track = ges_track_element_get_track (GES_TRACK_ELEMENT (e));
  cmp_track = ges_track_element_get_track (GES_TRACK_ELEMENT (cmp));
  GST_LOG ("Checking overlap between " _ELEMENT_FORMAT " and "
      _ELEMENT_FORMAT, _CMP_ARGS, _E_ARGS);

  if (track != cmp_track || track == NULL || cmp_track == NULL) {
    GST_LOG (_ELEMENT_FORMAT " and " _ELEMENT_FORMAT " are not in the "
        "same track", _CMP_ARGS, _E_ARGS);
    return FALSE;
  }

  if (layer_prio != cmp_layer_prio) {
    GST_LOG (_ELEMENT_FORMAT " and " _ELEMENT_FORMAT " are not in the "
        "same layer", _CMP_ARGS, _E_ARGS);
    return FALSE;
  }

  if (start >= cmp_end || cmp_start >= end) {
    /* They do not overlap at all */
    GST_LOG (_ELEMENT_FORMAT " and " _ELEMENT_FORMAT " do not overlap",
        _CMP_ARGS, _E_ARGS);
    return FALSE;
  }

  if (cmp_start <= start && cmp_end >= end) {
    /* cmp fully overlaps e */
    GST_INFO (_ELEMENT_FORMAT " and " _ELEMENT_FORMAT " fully overlap",
        _CMP_ARGS, _E_ARGS);
    set_full_overlap_error (data->error, cmp, e, track);
    goto error;
  }

  if (cmp_start >= start && cmp_end <= end) {
    /* e fully overlaps cmp */
    GST_INFO (_ELEMENT_FORMAT " and " _ELEMENT_FORMAT " fully overlap",
        _CMP_ARGS, _E_ARGS);
    set_full_overlap_error (data->error, e, cmp, track);
    goto error;
  }

  if (cmp_start < end && cmp_start > start) {
    /* cmp_start is between the start and end of the node */
    GST_LOG (_ELEMENT_FORMAT " is overlapped at its start by "
        _ELEMENT_FORMAT ". Overlap ends at %" GST_TIME_FORMAT,
        _CMP_ARGS, _E_ARGS, GST_TIME_ARGS (end));
    if (data->overlaping_on_start) {
      GST_INFO (_ELEMENT_FORMAT " is overlapped by %s and %s on its start",
          _CMP_ARGS, data->overlaping_on_start->name, e->name);
      set_triple_overlap_error (data->error, cmp, e, data->overlaping_on_start,
          track);
      goto error;
    }
    if (GST_CLOCK_TIME_IS_VALID (data->overlap_end_first_time) &&
        end > data->overlap_end_first_time) {
      GST_INFO (_ELEMENT_FORMAT " overlaps %s on its start and %s on its "
          "end, but they already overlap each other", _CMP_ARGS, e->name,
          data->overlaping_on_end->name);
      set_triple_overlap_error (data->error, cmp, e, data->overlaping_on_end,
          track);
      goto error;
    }
    /* record the time at which the overlapped ends */
    data->overlap_start_final_time = end;
    data->overlaping_on_start = e;
  }

  if (cmp_end < end && cmp_end > start) {
    /* cmp_end is between the start and end of the node */
    GST_LOG (_ELEMENT_FORMAT " is overlapped at its end by "
        _ELEMENT_FORMAT ". Overlap starts at %" GST_TIME_FORMAT,
        _CMP_ARGS, _E_ARGS, GST_TIME_ARGS (start));

    if (data->overlaping_on_end) {
      GST_INFO (_ELEMENT_FORMAT " is overlapped by %s and %s on its end",
          _CMP_ARGS, data->overlaping_on_end->name, e->name);
      set_triple_overlap_error (data->error, cmp, e, data->overlaping_on_end,
          track);
      goto error;
    }
    if (GST_CLOCK_TIME_IS_VALID (data->overlap_start_final_time) &&
        start < data->overlap_start_final_time) {
      GST_INFO (_ELEMENT_FORMAT " overlaps %s on its end and %s on its "
          "start, but they already overlap each other", _CMP_ARGS, e->name,
          data->overlaping_on_start->name);
      set_triple_overlap_error (data->error, cmp, e, data->overlaping_on_start,
          track);
      goto error;
    }
    /* record the time at which the overlapped starts */
    data->overlap_end_first_time = start;
    data->overlaping_on_end = e;
  }

  return FALSE;

error:
  data->res = FALSE;
  return TRUE;
}

/* check and find the overlaps with the element at node */
static gboolean
check_all_overlaps_with_element (GNode * node, TreeIterationData * data)
{
  GESTimelineElement *element = node->data;
  if (GES_IS_SOURCE (element)) {
    data->element = element;
    data->overlaping_on_start = NULL;
    data->overlaping_on_end = NULL;
    data->overlap_start_final_time = GST_CLOCK_TIME_NONE;
    data->overlap_end_first_time = GST_CLOCK_TIME_NONE;
    if (data->moving)
      data->pos_data = g_hash_table_lookup (data->moving, element);
    else
      data->pos_data = NULL;

    g_node_traverse (data->root, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
        (GNodeTraverseFunc) check_overlap_with_element, data);

    return !data->res;
  }
  return FALSE;
}

static gboolean
check_moving_overlaps (GNode * node, TreeIterationData * data)
{
  if (g_hash_table_contains (data->moving, node->data))
    return check_all_overlaps_with_element (node, data);
  return FALSE;
}

/* whether the elements in moving can be moved to their corresponding
 * PositionData */
static gboolean
timeline_tree_can_move_elements (GNode * root, GHashTable * moving,
    GError ** error)
{
  TreeIterationData data = tree_iteration_data_init;

  if (ges_timeline_get_edit_apis_disabled (root->data)) {
    return TRUE;
  }

  data.moving = moving;
  data.root = root;
  data.res = TRUE;
  data.error = error;
  /* sufficient to check the leaves, which is all the track elements or
   * empty clips
   * should also be sufficient to only check the moving elements */
  g_node_traverse (root, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
      (GNodeTraverseFunc) check_moving_overlaps, &data);

  return data.res;
}

/****************************************************
 *               Setting Edit Data                  *
 ****************************************************/

static void
set_negative_start_error (GError ** error, GESTimelineElement * element,
    GstClockTime neg_start)
{
  g_set_error (error, GES_ERROR, GES_ERROR_NEGATIVE_TIME,
      "The element \"%s\" would have a negative start of -%"
      GST_TIME_FORMAT, element->name, GST_TIME_ARGS (neg_start));
}

static void
set_negative_duration_error (GError ** error, GESTimelineElement * element,
    GstClockTime neg_duration)
{
  g_set_error (error, GES_ERROR, GES_ERROR_NEGATIVE_TIME,
      "The element \"%s\" would have a negative duration of -%"
      GST_TIME_FORMAT, element->name, GST_TIME_ARGS (neg_duration));
}

static void
set_negative_inpoint_error (GError ** error, GESTimelineElement * element,
    GstClockTime neg_inpoint)
{
  g_set_error (error, GES_ERROR, GES_ERROR_NEGATIVE_TIME,
      "The element \"%s\" would have a negative in-point of -%"
      GST_TIME_FORMAT, element->name, GST_TIME_ARGS (neg_inpoint));
}

static void
set_negative_layer_error (GError ** error, GESTimelineElement * element,
    gint64 neg_layer)
{
  g_set_error (error, GES_ERROR, GES_ERROR_NEGATIVE_LAYER,
      "The element \"%s\" would have a negative layer priority of -%"
      G_GINT64_FORMAT, element->name, neg_layer);
}

static void
set_breaks_duration_limit_error (GError ** error, GESClip * clip,
    GstClockTime duration, GstClockTime duration_limit)
{
  g_set_error (error, GES_ERROR, GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT,
      "The clip \"%s\" would have a duration of %" GST_TIME_FORMAT
      " that would break its duration-limit of %" GST_TIME_FORMAT,
      GES_TIMELINE_ELEMENT_NAME (clip), GST_TIME_ARGS (duration),
      GST_TIME_ARGS (duration_limit));
}

static void
set_inpoint_breaks_max_duration_error (GError ** error,
    GESTimelineElement * element, GstClockTime inpoint,
    GstClockTime max_duration)
{
  g_set_error (error, GES_ERROR, GES_ERROR_NOT_ENOUGH_INTERNAL_CONTENT,
      "The element \"%s\" would have an in-point of %" GST_TIME_FORMAT
      " that would break its max-duration of %" GST_TIME_FORMAT,
      GES_TIMELINE_ELEMENT_NAME (element), GST_TIME_ARGS (inpoint),
      GST_TIME_ARGS (max_duration));
}

static gboolean
set_layer_priority (GESTimelineElement * element, EditData * data,
    GError ** error)
{
  gint64 layer_offset = data->layer_offset;
  guint32 layer_prio = ges_timeline_element_get_layer_priority (element);

  if (!layer_offset)
    return TRUE;

  if (layer_prio == GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY) {
    GST_INFO_OBJECT (element, "Cannot shift %s to a new layer because it "
        "has no layer priority", element->name);
    return FALSE;
  }

  if (layer_offset > (gint64) layer_prio) {
    GST_INFO_OBJECT (element, "%s would have a negative layer priority (%"
        G_GUINT32_FORMAT " - %" G_GINT64_FORMAT ")", element->name,
        layer_prio, layer_offset);
    set_negative_layer_error (error, element,
        layer_offset - (gint64) layer_prio);
    return FALSE;
  }
  if ((layer_prio - (gint64) layer_offset) >= G_MAXUINT32) {
    GST_ERROR_OBJECT (element, "%s would have an overflowing layer priority",
        element->name);
    return FALSE;
  }

  data->layer_priority = (guint32) (layer_prio - (gint64) layer_offset);

  if (ges_timeline_layer_priority_in_gap (element->timeline,
          data->layer_priority)) {
    GST_ERROR_OBJECT (element, "Edit layer %" G_GUINT32_FORMAT " would "
        "be within a gap in the timeline layers", data->layer_priority);
    return FALSE;
  }

  GST_INFO_OBJECT (element, "%s will move to layer %" G_GUINT32_FORMAT,
      element->name, data->layer_priority);

  return TRUE;
}

#define _CHECK_END(element, start, duration) \
  if (!GST_CLOCK_TIME_IS_VALID (_clock_time_plus (start, duration))) { \
    GST_INFO_OBJECT (element, "Cannot edit %s because it would result in " \
        "an invalid end", element->name); \
    return FALSE; \
  }

static gboolean
set_edit_move_values (GESTimelineElement * element, EditData * data,
    GError ** error)
{
  gboolean negative = FALSE;
  GstClockTime new_start =
      _clock_time_minus_diff (element->start, data->offset, &negative);
  if (negative || !GST_CLOCK_TIME_IS_VALID (new_start)) {
    GST_INFO_OBJECT (element, "Cannot move %" GES_FORMAT " with offset %"
        G_GINT64_FORMAT " because it would result in an invalid start",
        GES_ARGS (element), data->offset);
    if (negative)
      set_negative_start_error (error, element, new_start);
    return FALSE;
  }
  _CHECK_END (element, new_start, element->duration);
  data->start = new_start;

  if (GES_IS_GROUP (element))
    return TRUE;

  GST_INFO_OBJECT (element, "%s will move by setting start to %"
      GST_TIME_FORMAT, element->name, GST_TIME_ARGS (data->start));

  return set_layer_priority (element, data, error);
}

static gboolean
set_edit_trim_start_clip_inpoints (GESClip * clip, EditData * clip_data,
    GHashTable * edit_table, GError ** error)
{
  gboolean ret = FALSE;
  GList *tmp;
  GstClockTime duration_limit;
  GstClockTime clip_inpoint;
  GstClockTime new_start = clip_data->start;
  gboolean no_core = FALSE;
  GHashTable *child_inpoints;

  child_inpoints = g_hash_table_new_full (NULL, NULL, gst_object_unref, g_free);

  clip_inpoint = ges_clip_get_core_internal_time_from_timeline_time (clip,
      new_start, &no_core, error);

  if (no_core) {
    GST_INFO_OBJECT (clip, "Clip %" GES_FORMAT " has no active core "
        "children with an internal source. Not setting in-point during "
        "trim to start", GES_ARGS (clip));
    clip_inpoint = GES_TIMELINE_ELEMENT_INPOINT (clip);
  } else if (!GST_CLOCK_TIME_IS_VALID (clip_inpoint)) {
    GST_INFO_OBJECT (clip, "Cannot trim start of %" GES_FORMAT
        " with offset %" G_GINT64_FORMAT " because it would result in an "
        "invalid in-point for its core children", GES_ARGS (clip),
        clip_data->offset);
    goto done;
  } else {
    GST_INFO_OBJECT (clip, "Clip %" GES_FORMAT " will have its in-point "
        " set to %" GST_TIME_FORMAT " because its start is being trimmed "
        "to %" GST_TIME_FORMAT, GES_ARGS (clip),
        GST_TIME_ARGS (clip_inpoint), GST_TIME_ARGS (new_start));
    clip_data->inpoint = clip_inpoint;
  }

  /* need to set in-point of active non-core children to keep their
   * internal content at the same timeline position */
  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTimelineElement *child = tmp->data;
    GESTrackElement *el = tmp->data;
    GstClockTime new_inpoint = child->inpoint;
    GstClockTime *inpoint_p;

    if (ges_track_element_has_internal_source (el)) {
      if (ges_track_element_is_core (el)) {
        new_inpoint = clip_inpoint;
      } else if (ges_track_element_is_active (el)) {
        EditData *data;

        if (g_hash_table_contains (edit_table, child)) {
          GST_ERROR_OBJECT (child, "Already set to be edited");
          goto done;
        }

        new_inpoint = ges_clip_get_internal_time_from_timeline_time (clip, el,
            new_start, error);

        if (!GST_CLOCK_TIME_IS_VALID (new_inpoint)) {
          GST_INFO_OBJECT (clip, "Cannot trim start of %" GES_FORMAT
              " to %" GST_TIME_FORMAT " because it would result in an "
              "invalid in-point for the non-core child %" GES_FORMAT,
              GES_ARGS (clip), GST_TIME_ARGS (new_start), GES_ARGS (child));
          goto done;
        }

        GST_INFO_OBJECT (child, "Setting track element %s to trim "
            "in-point to %" GST_TIME_FORMAT " since the parent clip %"
            GES_FORMAT " is being trimmed to start %" GST_TIME_FORMAT,
            child->name, GST_TIME_ARGS (new_inpoint), GES_ARGS (clip),
            GST_TIME_ARGS (new_start));

        data = new_edit_data (EDIT_TRIM_INPOINT_ONLY, 0, 0);
        data->inpoint = new_inpoint;
        g_hash_table_insert (edit_table, child, data);
      }
    }

    if (GES_CLOCK_TIME_IS_LESS (child->maxduration, new_inpoint)) {
      GST_INFO_OBJECT (clip, "Cannot trim start of %" GES_FORMAT
          " to %" GST_TIME_FORMAT " because it would result in an "
          "in-point of %" GST_TIME_FORMAT " for the child %" GES_FORMAT
          ", which breaks its max-duration", GES_ARGS (clip),
          GST_TIME_ARGS (new_start), GST_TIME_ARGS (new_inpoint),
          GES_ARGS (child));

      set_inpoint_breaks_max_duration_error (error, child, new_inpoint,
          child->maxduration);
      goto done;
    }

    inpoint_p = g_new (GstClockTime, 1);
    *inpoint_p = new_inpoint;
    g_hash_table_insert (child_inpoints, gst_object_ref (child), inpoint_p);
  }

  duration_limit =
      ges_clip_duration_limit_with_new_children_inpoints (clip, child_inpoints);

  if (GES_CLOCK_TIME_IS_LESS (duration_limit, clip_data->duration)) {
    GST_INFO_OBJECT (clip, "Cannot trim start of %" GES_FORMAT
        " to %" GST_TIME_FORMAT " because it would result in a "
        "duration of %" GST_TIME_FORMAT " that breaks its new "
        "duration-limit of %" GST_TIME_FORMAT, GES_ARGS (clip),
        GST_TIME_ARGS (new_start), GST_TIME_ARGS (clip_data->duration),
        GST_TIME_ARGS (duration_limit));

    set_breaks_duration_limit_error (error, clip, clip_data->duration,
        duration_limit);
    goto done;
  }

  ret = TRUE;

done:
  g_hash_table_unref (child_inpoints);

  return ret;
}

/* trim the start of a clip or a track element */
static gboolean
set_edit_trim_start_values (GESTimelineElement * element, EditData * data,
    GHashTable * edit_table, GError ** error)
{
  gboolean negative = FALSE;
  GstClockTime new_duration;
  GstClockTime new_start =
      _clock_time_minus_diff (element->start, data->offset, &negative);

  if (negative || !GST_CLOCK_TIME_IS_VALID (new_start)) {
    GST_INFO_OBJECT (element, "Cannot trim start of %" GES_FORMAT
        " with offset %" G_GINT64_FORMAT " because it would result in an "
        "invalid start", GES_ARGS (element), data->offset);
    if (negative)
      set_negative_start_error (error, element, new_start);
    return FALSE;
  }

  new_duration =
      _clock_time_minus_diff (element->duration, -data->offset, &negative);

  if (negative || !GST_CLOCK_TIME_IS_VALID (new_duration)) {
    GST_INFO_OBJECT (element, "Cannot trim start of %" GES_FORMAT
        " with offset %" G_GINT64_FORMAT " because it would result in an "
        "invalid duration", GES_ARGS (element), data->offset);
    if (negative)
      set_negative_duration_error (error, element, new_duration);
    return FALSE;
  }
  _CHECK_END (element, new_start, new_duration);

  data->start = new_start;
  data->duration = new_duration;

  if (GES_IS_GROUP (element))
    return TRUE;

  if (GES_IS_CLIP (element)) {
    if (!set_edit_trim_start_clip_inpoints (GES_CLIP (element), data,
            edit_table, error))
      return FALSE;
  } else if (GES_IS_TRACK_ELEMENT (element)
      && ges_track_element_has_internal_source (GES_TRACK_ELEMENT (element))) {
    GstClockTime new_inpoint =
        _clock_time_minus_diff (element->inpoint, data->offset, &negative);

    if (negative || !GST_CLOCK_TIME_IS_VALID (new_inpoint)) {
      GST_INFO_OBJECT (element, "Cannot trim start of %" GES_FORMAT
          " with offset %" G_GINT64_FORMAT " because it would result in "
          "an invalid in-point", GES_ARGS (element), data->offset);
      if (negative)
        set_negative_inpoint_error (error, element, new_inpoint);
      return FALSE;
    }
  }

  GST_INFO_OBJECT (element, "%s will trim start by setting start to %"
      GST_TIME_FORMAT ", in-point to %" GST_TIME_FORMAT " and duration "
      "to %" GST_TIME_FORMAT, element->name, GST_TIME_ARGS (data->start),
      GST_TIME_ARGS (data->inpoint), GST_TIME_ARGS (data->duration));

  return set_layer_priority (element, data, error);
}

/* trim the end of a clip or a track element */
static gboolean
set_edit_trim_end_values (GESTimelineElement * element, EditData * data,
    GError ** error)
{
  gboolean negative = FALSE;
  GstClockTime new_duration =
      _clock_time_minus_diff (element->duration, data->offset, &negative);
  if (negative || !GST_CLOCK_TIME_IS_VALID (new_duration)) {
    GST_INFO_OBJECT (element, "Cannot trim end of %" GES_FORMAT
        " with offset %" G_GINT64_FORMAT " because it would result in an "
        "invalid duration", GES_ARGS (element), data->offset);
    if (negative)
      set_negative_duration_error (error, element, new_duration);
    return FALSE;
  }
  _CHECK_END (element, element->start, new_duration);

  if (GES_IS_CLIP (element)) {
    GESClip *clip = GES_CLIP (element);
    GstClockTime limit = ges_clip_get_duration_limit (clip);

    if (GES_CLOCK_TIME_IS_LESS (limit, new_duration)) {
      GST_INFO_OBJECT (element, "Cannot trim end of %" GES_FORMAT
          " with offset %" G_GINT64_FORMAT " because the duration would "
          "exceed the clip's duration-limit %" G_GINT64_FORMAT,
          GES_ARGS (element), data->offset, limit);

      set_breaks_duration_limit_error (error, clip, new_duration, limit);
      return FALSE;
    }
  }

  data->duration = new_duration;

  if (GES_IS_GROUP (element))
    return TRUE;

  GST_INFO_OBJECT (element, "%s will trim end by setting duration to %"
      GST_TIME_FORMAT, element->name, GST_TIME_ARGS (data->duration));

  return set_layer_priority (element, data, error);
}

static gboolean
set_edit_values (GESTimelineElement * element, EditData * data,
    GHashTable * edit_table, GError ** error)
{
  switch (data->mode) {
    case EDIT_MOVE:
      return set_edit_move_values (element, data, error);
    case EDIT_TRIM_START:
      return set_edit_trim_start_values (element, data, edit_table, error);
    case EDIT_TRIM_END:
      return set_edit_trim_end_values (element, data, error);
    case EDIT_TRIM_INPOINT_ONLY:
      GST_ERROR_OBJECT (element, "Trim in-point only not handled");
      return FALSE;
  }
  return FALSE;
}

static gboolean
add_clips_to_list (GNode * node, GList ** list)
{
  GESTimelineElement *element = node->data;
  GESTimelineElement *clip = NULL;

  if (GES_IS_CLIP (element))
    clip = element;
  else if (GES_IS_CLIP (element->parent))
    clip = element->parent;

  if (clip && !g_list_find (*list, clip))
    *list = g_list_append (*list, clip);

  return FALSE;
}

static gboolean
replace_group_with_clip_edits (GNode * root, GESTimelineElement * group,
    GHashTable * edit_table, GError ** err)
{
  gboolean ret = TRUE;
  GList *tmp, *clips = NULL;
  GNode *node = find_node (root, group);
  GstClockTime new_end, new_start;
  ElementEditMode mode;
  gint64 layer_offset;

  if (!node) {
    GST_ERROR_OBJECT (group, "Not being tracked");
    goto error;
  }

  /* new context for the lifespan of group_data */
  {
    EditData *group_edit = g_hash_table_lookup (edit_table, group);

    if (!group_edit) {
      GST_ERROR_OBJECT (group, "Edit data for group was missing");
      goto error;
    }

    group_edit->start = group->start;
    group_edit->duration = group->duration;

    /* should only set the start and duration fields, table should not be
     * needed, so we pass NULL */
    if (!set_edit_values (group, group_edit, NULL, err))
      goto error;

    new_start = group_edit->start;
    new_end = _clock_time_plus (group_edit->start, group_edit->duration);

    if (!GST_CLOCK_TIME_IS_VALID (new_start)
        || !GST_CLOCK_TIME_IS_VALID (new_end)) {
      GST_ERROR_OBJECT (group, "Edit data gave an invalid start or end");
      goto error;
    }

    layer_offset = group_edit->layer_offset;
    mode = group_edit->mode;

    /* can traverse leaves to find all the clips since they are at _most_
     * one step above the track elements */
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
        (GNodeTraverseFunc) add_clips_to_list, &clips);

    if (!clips) {
      GST_INFO_OBJECT (group, "Contains no clips, so cannot be edited");
      goto error;
    }

    if (!g_hash_table_remove (edit_table, group)) {
      GST_ERROR_OBJECT (group, "Could not replace the group in the edit list");
      goto error;
    }
    /* removing the group from the table frees group_edit */
  }

  for (tmp = clips; tmp; tmp = tmp->next) {
    GESTimelineElement *clip = tmp->data;
    gboolean edit = FALSE;
    GstClockTimeDiff offset = G_MAXINT64;
    ElementEditMode clip_mode = mode;

    /* if at the edge of the group and being trimmed forward or backward */
    if (mode == EDIT_MOVE) {
      /* same offset as the group */
      edit = TRUE;
      offset = group->start - new_start;

      GST_INFO_OBJECT (clip, "Setting clip %s to moving with offset %"
          G_GINT64_FORMAT " since an ancestor group %" GES_FORMAT
          " is moving to %" GST_TIME_FORMAT, clip->name, offset,
          GES_ARGS (group), GST_TIME_ARGS (new_start));

    } else if ((mode == EDIT_TRIM_START)
        && (clip->start <= new_start || clip->start == group->start)) {
      /* trim to same start */
      edit = TRUE;
      offset = clip->start - new_start;

      GST_INFO_OBJECT (clip, "Setting clip %s to trim start with offset %"
          G_GINT64_FORMAT " since an ancestor group %" GES_FORMAT " is "
          "being trimmed to start %" GST_TIME_FORMAT, clip->name, offset,
          GES_ARGS (group), GST_TIME_ARGS (new_start));

    } else if (mode == EDIT_TRIM_END
        && (_END (clip) >= new_end || _END (clip) == _END (group))) {
      /* trim to same end */
      edit = TRUE;
      offset = _END (clip) - new_end;

      GST_INFO_OBJECT (clip, "Setting clip %s to trim end with offset %"
          G_GINT64_FORMAT " since an ancestor group %" GES_FORMAT " is "
          "being trimmed to end %" GST_TIME_FORMAT, clip->name, offset,
          GES_ARGS (group), GST_TIME_ARGS (new_end));

    } else if (layer_offset) {
      /* still need to move layer */
      edit = TRUE;
      clip_mode = EDIT_MOVE;
      offset = 0;
    }
    if (edit) {
      EditData *clip_data;

      if (layer_offset)
        GST_INFO_OBJECT (clip, "Setting clip %s to move to new layer with "
            "offset %" G_GINT64_FORMAT " since an ancestor group %"
            GES_FORMAT " is being moved with the same offset", clip->name,
            layer_offset, GES_ARGS (group));

      if (g_hash_table_contains (edit_table, clip)) {
        GST_ERROR_OBJECT (clip, "Already set to be edited");
        goto error;
      }
      clip_data = new_edit_data (clip_mode, offset, layer_offset);
      g_hash_table_insert (edit_table, clip, clip_data);
      if (!set_edit_values (clip, clip_data, edit_table, err))
        goto error;
    }
  }

done:
  g_list_free (clips);
  return ret;

error:
  ret = FALSE;
  goto done;
}

/* set the edit values for the entries in @edits
 * any groups in @edits will be replaced by their clip children */
static gboolean
timeline_tree_set_element_edit_values (GNode * root, GHashTable * edits,
    GError ** err)
{
  gboolean ret = TRUE;
  GESTimelineElement *element;
  EditData *edit_data;
  /* content of edit table may change when group edits are replaced by
   * clip edits and clip edits introduce edits for non-core children */
  GList *tmp, *elements = g_hash_table_get_keys (edits);

  for (tmp = elements; tmp; tmp = tmp->next) {
    gboolean res;
    element = tmp->data;
    edit_data = g_hash_table_lookup (edits, element);
    if (!edit_data) {
      GST_ERROR_OBJECT (element, "No edit data for the element");
      goto error;
    }
    if (GES_IS_GROUP (element))
      res = replace_group_with_clip_edits (root, element, edits, err);
    else
      res = set_edit_values (element, edit_data, edits, err);
    if (!res)
      goto error;
  }

done:
  g_list_free (elements);

  return ret;

error:
  ret = FALSE;
  goto done;
}

/* set the moving PositionData by using their parent clips.
 * @edit_table should already have had its values set, and any group edits
 * replaced by clip edits. */
static void
set_moving_positions_from_edits (GHashTable * moving, GHashTable * edit_table)
{
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, moving);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GESTimelineElement *element = key;
    PositionData *pos = value;
    GESTimelineElement *parent;
    EditData *edit;

    /* a track element will end up with the same start and end as its clip */
    /* if no parent, act as own parent */
    parent = element->parent ? element->parent : element;
    edit = g_hash_table_lookup (edit_table, parent);

    if (edit && GST_CLOCK_TIME_IS_VALID (edit->start))
      pos->start = edit->start;
    else
      pos->start = element->start;

    if (edit && GST_CLOCK_TIME_IS_VALID (edit->duration))
      pos->end = pos->start + edit->duration;
    else
      pos->end = pos->start + element->duration;

    if (edit && edit->layer_priority != GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY)
      pos->layer_priority = edit->layer_priority;
    else
      pos->layer_priority = ges_timeline_element_get_layer_priority (element);
  }
}

static void
give_edits_same_offset (GHashTable * edits, GstClockTimeDiff offset,
    gint64 layer_offset)
{
  GHashTableIter iter;
  gpointer value;

  g_hash_table_iter_init (&iter, edits);
  while (g_hash_table_iter_next (&iter, NULL, &value)) {
    EditData *edit_data = value;
    edit_data->offset = offset;
    edit_data->layer_offset = layer_offset;
  }
}

/****************************************************
 *         Initialise Edit Data and Moving          *
 ****************************************************/

static gboolean
add_track_elements_to_moving (GNode * node, GHashTable * track_elements)
{
  GESTimelineElement *element = node->data;
  if (GES_IS_TRACK_ELEMENT (element)) {
    GST_LOG_OBJECT (element, "%s set as moving", element->name);
    g_hash_table_insert (track_elements, element, g_new0 (PositionData, 1));
  }
  return FALSE;
}

/* add all the track elements found under the elements in @edits to @moving,
 * but does not set their position data */
static gboolean
timeline_tree_add_edited_to_moving (GNode * root, GHashTable * edits,
    GHashTable * moving)
{
  GHashTableIter iter;
  gpointer key;

  g_hash_table_iter_init (&iter, edits);
  while (g_hash_table_iter_next (&iter, &key, NULL)) {
    GESTimelineElement *element = key;
    GNode *node = find_node (root, element);
    if (!node) {
      GST_ERROR_OBJECT (element, "Not being tracked");
      return FALSE;
    }
    g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
        (GNodeTraverseFunc) add_track_elements_to_moving, moving);
  }

  return TRUE;
}

/* check we can handle the top and all of its children */
static gboolean
check_types (GESTimelineElement * element, gboolean is_top)
{
  if (!GES_IS_CLIP (element) && !GES_IS_GROUP (element)
      && !GES_IS_TRACK_ELEMENT (element)) {
    GST_ERROR_OBJECT (element, "Cannot handle a GESTimelineElement of the "
        "type %s", G_OBJECT_TYPE_NAME (element));
    return FALSE;
  }
  if (!is_top && element->parent) {
    if ((GES_IS_CLIP (element) && !GES_IS_GROUP (element->parent))
        || (GES_IS_GROUP (element) && !GES_IS_GROUP (element->parent))
        || (GES_IS_TRACK_ELEMENT (element) && !GES_IS_CLIP (element->parent))) {
      GST_ERROR_OBJECT (element, "A parent of type %s is not handled",
          G_OBJECT_TYPE_NAME (element->parent));
      return FALSE;
    }
  }
  if (GES_IS_CONTAINER (element)) {
    GList *tmp;
    for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
      if (!check_types (tmp->data, FALSE))
        return FALSE;
    }
  }

  return TRUE;
}

/* @edits: The table to add the edit to
 * @element: The element to edit
 * @mode: The mode for editing @element
 *
 * Adds an edit for @element it to the table with its EditData only set
 * with @mode.
 *
 * The offsets for the edit will have to be set later.
 */
static gboolean
add_element_edit (GHashTable * edits, GESTimelineElement * element,
    ElementEditMode mode)
{
  if (!check_types (element, TRUE))
    return FALSE;

  if (g_hash_table_contains (edits, element)) {
    GST_ERROR_OBJECT (element, "Already set to be edited");
    return FALSE;
  }

  switch (mode) {
    case EDIT_MOVE:
      GST_LOG_OBJECT (element, "%s set to move", element->name);
      break;
    case EDIT_TRIM_START:
      GST_LOG_OBJECT (element, "%s set to trim start", element->name);
      break;
    case EDIT_TRIM_END:
      GST_LOG_OBJECT (element, "%s set to trim end", element->name);
      break;
    case EDIT_TRIM_INPOINT_ONLY:
      GST_ERROR_OBJECT (element, "%s set to trim in-point only", element->name);
      return FALSE;
  }

  g_hash_table_insert (edits, element, new_edit_data (mode, 0, 0));

  return TRUE;
}

/********************************************
 *   Check against current configuration    *
 ********************************************/

/* can move with no snapping or change in parent! */
gboolean
timeline_tree_can_move_element (GNode * root,
    GESTimelineElement * element, guint32 priority, GstClockTime start,
    GstClockTime duration, GError ** error)
{
  gboolean ret = FALSE;
  guint32 layer_prio = ges_timeline_element_get_layer_priority (element);
  GstClockTime distance, new_end;
  GHashTable *move_edits, *trim_edits, *moving;
  GHashTableIter iter;
  gpointer key, value;

  if (ges_timeline_get_edit_apis_disabled (root->data)) {
    return TRUE;
  }

  if (layer_prio == GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY
      && priority != layer_prio) {
    GST_INFO_OBJECT (element, "Cannot move to a layer when no layer "
        "priority to begin with");
    return FALSE;
  }

  distance = _abs_clock_time_distance (start, element->start);
  if ((GstClockTimeDiff) distance >= G_MAXINT64) {
    GST_WARNING_OBJECT (element, "Move in start from %" GST_TIME_FORMAT
        " to %" GST_TIME_FORMAT " is too large to perform",
        GST_TIME_ARGS (element->start), GST_TIME_ARGS (start));
    return FALSE;
  }

  distance = _abs_clock_time_distance (duration, element->duration);
  if ((GstClockTimeDiff) distance >= G_MAXINT64) {
    GST_WARNING_OBJECT (element, "Move in duration from %" GST_TIME_FORMAT
        " to %" GST_TIME_FORMAT " is too large to perform",
        GST_TIME_ARGS (element->duration), GST_TIME_ARGS (duration));
    return FALSE;
  }

  new_end = _clock_time_plus (start, duration);
  if (!GST_CLOCK_TIME_IS_VALID (new_end)) {
    GST_WARNING_OBJECT (element, "Move in start and duration to %"
        GST_TIME_FORMAT " and %" GST_TIME_FORMAT " would produce an "
        "invalid end", GST_TIME_ARGS (start), GST_TIME_ARGS (duration));
    return FALSE;
  }

  /* treat as an EDIT_MOVE to the new priority, except on the element
   * rather than the toplevel, followed by an EDIT_TRIM_END */
  move_edits = new_edit_table ();
  trim_edits = new_edit_table ();
  moving = new_position_table ();

  if (!add_element_edit (move_edits, element, EDIT_MOVE))
    goto done;
  /* moving should remain the same */
  if (!add_element_edit (trim_edits, element, EDIT_TRIM_END))
    goto done;

  if (!timeline_tree_add_edited_to_moving (root, move_edits, moving)
      || !timeline_tree_add_edited_to_moving (root, trim_edits, moving))
    goto done;

  /* no snapping */
  give_edits_same_offset (move_edits, element->start - start,
      (gint64) layer_prio - (gint64) priority);
  give_edits_same_offset (trim_edits, element->duration - duration, 0);

  /* assume both edits can be performed if each could occur individually */
  /* should not effect duration or in-point */
  if (!timeline_tree_set_element_edit_values (root, move_edits, error))
    goto done;
  /* should not effect start or in-point or layer */
  if (!timeline_tree_set_element_edit_values (root, trim_edits, error))
    goto done;

  /* merge the two edits into moving positions */
  g_hash_table_iter_init (&iter, moving);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GESTimelineElement *el = key;
    PositionData *pos_data = value;
    EditData *move = NULL;
    EditData *trim = NULL;

    if (el->parent) {
      move = g_hash_table_lookup (move_edits, el->parent);
      trim = g_hash_table_lookup (trim_edits, el->parent);
    }

    if (!move)
      move = g_hash_table_lookup (move_edits, el);
    if (!trim)
      trim = g_hash_table_lookup (trim_edits, el);

    /* should always have move with a valid start */
    if (!move || !GST_CLOCK_TIME_IS_VALID (move->start)) {
      GST_ERROR_OBJECT (el, "Element set to moving but neither it nor its "
          "parent are being edited");
      goto done;
    }
    /* may not have trim if element is a group and the child is away
     * from the edit position, but if we do it should have a valid duration */
    if (trim && !GST_CLOCK_TIME_IS_VALID (trim->duration)) {
      GST_ERROR_OBJECT (el, "Element set to trim end but neither it nor its "
          "parent is being trimmed");
      goto done;
    }

    pos_data->start = move->start;

    if (move->layer_priority != GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY)
      pos_data->layer_priority = move->layer_priority;
    else
      pos_data->layer_priority = ges_timeline_element_get_layer_priority (el);

    if (trim)
      pos_data->end = pos_data->start + trim->duration;
    else
      pos_data->end = pos_data->start + el->duration;
  }

  /* check overlaps */
  if (!timeline_tree_can_move_elements (root, moving, error))
    goto done;

  ret = TRUE;

done:
  g_hash_table_unref (trim_edits);
  g_hash_table_unref (move_edits);
  g_hash_table_unref (moving);

  return ret;
}

/********************************************
 *         Perform Element Edit             *
 ********************************************/

static gboolean
perform_element_edit (GESTimelineElement * element, EditData * edit)
{
  gboolean ret = FALSE;
  guint32 layer_prio = ges_timeline_element_get_layer_priority (element);

  switch (edit->mode) {
    case EDIT_MOVE:
      GST_INFO_OBJECT (element, "Moving %s from %" GST_TIME_FORMAT " to %"
          GST_TIME_FORMAT, element->name, GST_TIME_ARGS (element->start),
          GST_TIME_ARGS (edit->start));
      break;
    case EDIT_TRIM_START:
      GST_INFO_OBJECT (element, "Trimming %s start from %" GST_TIME_FORMAT
          " to %" GST_TIME_FORMAT, element->name,
          GST_TIME_ARGS (element->start), GST_TIME_ARGS (edit->start));
      break;
    case EDIT_TRIM_END:
      GST_INFO_OBJECT (element, "Trimming %s end from %" GST_TIME_FORMAT
          " to %" GST_TIME_FORMAT, element->name,
          GST_TIME_ARGS (_END (element)),
          GST_TIME_ARGS (element->start + edit->duration));
      break;
    case EDIT_TRIM_INPOINT_ONLY:
      GST_INFO_OBJECT (element, "Trimming %s in-point from %"
          GST_TIME_FORMAT " to %" GST_TIME_FORMAT, element->name,
          GST_TIME_ARGS (element->inpoint), GST_TIME_ARGS (edit->inpoint));
      break;
  }

  if (!GES_IS_CLIP (element) && !GES_IS_TRACK_ELEMENT (element)) {
    GST_ERROR_OBJECT (element, "Cannot perform edit on group");
    return FALSE;
  }

  if (!GES_IS_CLIP (element)
      && edit->layer_priority != GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY) {
    GST_ERROR_OBJECT (element, "Cannot move an element that is not a "
        "clip to a new layer");
    return FALSE;
  }

  GES_TIMELINE_ELEMENT_SET_BEING_EDITED (element);
  if (GST_CLOCK_TIME_IS_VALID (edit->start)) {
    if (!ges_timeline_element_set_start (element, edit->start)) {
      GST_ERROR_OBJECT (element, "Failed to set the start");
      goto done;
    }
  }
  if (GST_CLOCK_TIME_IS_VALID (edit->inpoint)) {
    if (!ges_timeline_element_set_inpoint (element, edit->inpoint)) {
      GST_ERROR_OBJECT (element, "Failed to set the in-point");
      goto done;
    }
  }
  if (GST_CLOCK_TIME_IS_VALID (edit->duration)) {
    if (!ges_timeline_element_set_duration (element, edit->duration)) {
      GST_ERROR_OBJECT (element, "Failed to set the duration");
      goto done;
    }
  }
  if (edit->layer_priority != GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY) {
    GESTimeline *timeline = GES_TIMELINE_ELEMENT_TIMELINE (element);
    GESLayer *layer = ges_timeline_get_layer (timeline, edit->layer_priority);

    GST_INFO_OBJECT (element, "Moving %s from layer %" G_GUINT32_FORMAT
        " to layer %" G_GUINT32_FORMAT, element->name, layer_prio,
        edit->layer_priority);

    if (layer == NULL) {
      /* make sure we won't loop forever */
      if (ges_timeline_layer_priority_in_gap (timeline, edit->layer_priority)) {
        GST_ERROR_OBJECT (element, "Requested layer %" G_GUINT32_FORMAT
            " is within a gap in the timeline layers", edit->layer_priority);
        goto done;
      }

      do {
        layer = ges_timeline_append_layer (timeline);
      } while (ges_layer_get_priority (layer) < edit->layer_priority);
    } else {
      gst_object_unref (layer);
    }

    if (!ges_clip_move_to_layer (GES_CLIP (element), layer)) {
      GST_ERROR_OBJECT (element, "Failed to move layers");
      goto done;
    }
  }

  ret = TRUE;

done:
  GES_TIMELINE_ELEMENT_UNSET_BEING_EDITED (element);

  return ret;
}

/* perform all the element edits found in @edits.
 * These should only be clips of track elements. */
static gboolean
timeline_tree_perform_edits (GNode * root, GHashTable * edits)
{
  gboolean no_errors = TRUE;
  GHashTableIter iter;
  gpointer key, value;

  /* freeze the auto-transitions whilst we edit */
  ges_timeline_freeze_auto_transitions (root->data, TRUE);

  g_hash_table_iter_init (&iter, edits);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (GES_IS_TRACK_ELEMENT (key))
      ges_track_element_freeze_control_sources (GES_TRACK_ELEMENT (key), TRUE);
  }

  g_hash_table_iter_init (&iter, edits);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GESTimelineElement *element = key;
    EditData *edit_data = value;
    if (!perform_element_edit (element, edit_data))
      no_errors = FALSE;
  }

  g_hash_table_iter_init (&iter, edits);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (GES_IS_TRACK_ELEMENT (key))
      ges_track_element_freeze_control_sources (GES_TRACK_ELEMENT (key), FALSE);
  }

  /* allow the transitions to update if they can */
  ges_timeline_freeze_auto_transitions (root->data, FALSE);

  timeline_tree_create_transitions (root, ges_timeline_find_auto_transition);
  timeline_update_duration (root->data);

  return no_errors;
}

#define _REPLACE_TRACK_ELEMENT_WITH_PARENT(element) \
  element = (GES_IS_TRACK_ELEMENT (element) && element->parent) ? element->parent : element

/********************************************
 *                 Ripple                   *
 ********************************************/

gboolean
timeline_tree_ripple (GNode * root, GESTimelineElement * element,
    gint64 layer_priority_offset, GstClockTimeDiff offset, GESEdge edge,
    GstClockTime snapping_distance, GError ** error)
{
  gboolean res = TRUE;
  GNode *node;
  GESTimelineElement *ripple_toplevel;
  GstClockTime ripple_time;
  GHashTable *edits = new_edit_table ();
  GHashTable *moving = new_position_table ();
  ElementEditMode mode;
  SnappedPosition *snap = new_snapped_position (snapping_distance);

  _REPLACE_TRACK_ELEMENT_WITH_PARENT (element);

  ripple_toplevel = ges_timeline_element_peak_toplevel (element);

  /* if EDGE_END:
   *   TRIM_END the element, and MOVE all toplevels whose start is after
   *   the current end of the element by the same amount
   * otherwise:
   *   MOVE the topevel of the element, and all other toplevel elements
   *   whose start is after the current start of the element */

  switch (edge) {
    case GES_EDGE_END:
      GST_INFO_OBJECT (element, "Rippling end with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      mode = EDIT_TRIM_END;
      break;
    case GES_EDGE_START:
      GST_INFO_OBJECT (element, "Rippling start with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      mode = EDIT_MOVE;
      break;
    case GES_EDGE_NONE:
      GST_INFO_OBJECT (element, "Rippling with toplevel with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      element = ripple_toplevel;
      mode = EDIT_MOVE;
      break;
    default:
      GST_WARNING_OBJECT (element, "Edge not supported");
      goto done;
  }

  ripple_time = ELEMENT_EDGE_VALUE (element, edge);

  /* add edits */
  if (!add_element_edit (edits, element, mode))
    goto error;

  for (node = root->children; node; node = node->next) {
    GESTimelineElement *toplevel = node->data;
    if (toplevel == ripple_toplevel)
      continue;

    if (toplevel->start >= ripple_time) {
      if (!add_element_edit (edits, toplevel, EDIT_MOVE))
        goto error;
    }
  }

  if (!timeline_tree_add_edited_to_moving (root, edits, moving))
    goto error;

  /* snap */
  if (!timeline_tree_snap (root, element, mode, &offset, moving, snap))
    goto error;

  /* check and set edits using snapped values */
  give_edits_same_offset (edits, offset, layer_priority_offset);
  if (!timeline_tree_set_element_edit_values (root, edits, error))
    goto error;

  /* check overlaps */
  set_moving_positions_from_edits (moving, edits);
  if (!timeline_tree_can_move_elements (root, moving, error))
    goto error;

  /* emit snapping now. Edits should only fail if a programming error
   * occured */
  if (snap)
    ges_timeline_emit_snapping (root->data, snap->element, snap->snapped_to,
        snap->snapped);

  res = timeline_tree_perform_edits (root, edits);

done:
  g_hash_table_unref (edits);
  g_hash_table_unref (moving);
  g_free (snap);
  return res;

error:
  res = FALSE;
  goto done;
}

/********************************************
 *                  Trim                    *
 ********************************************/

gboolean
timeline_tree_trim (GNode * root, GESTimelineElement * element,
    gint64 layer_priority_offset, GstClockTimeDiff offset, GESEdge edge,
    GstClockTime snapping_distance, GError ** error)
{
  gboolean res = TRUE;
  GHashTable *edits = new_edit_table ();
  GHashTable *moving = new_position_table ();
  ElementEditMode mode;
  SnappedPosition *snap = new_snapped_position (snapping_distance);

  _REPLACE_TRACK_ELEMENT_WITH_PARENT (element);

  /* TODO: 2.0 remove this warning and simply fail if no edge is specified */
  if (edge == GES_EDGE_NONE) {
    g_warning ("No edge specified for trimming. Defaulting to GES_EDGE_START");
    edge = GES_EDGE_START;
  }

  switch (edge) {
    case GES_EDGE_END:
      GST_INFO_OBJECT (element, "Trimming end with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      mode = EDIT_TRIM_END;
      break;
    case GES_EDGE_START:
      GST_INFO_OBJECT (element, "Trimming start with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      mode = EDIT_TRIM_START;
      break;
    default:
      GST_WARNING_OBJECT (element, "Edge not supported");
      goto done;
  }

  /* add edits */
  if (!add_element_edit (edits, element, mode))
    goto error;

  if (!timeline_tree_add_edited_to_moving (root, edits, moving))
    goto error;

  /* snap */
  if (!timeline_tree_snap (root, element, mode, &offset, moving, snap))
    goto error;

  /* check and set edits using snapped values */
  give_edits_same_offset (edits, offset, layer_priority_offset);
  if (!timeline_tree_set_element_edit_values (root, edits, error))
    goto error;

  /* check overlaps */
  set_moving_positions_from_edits (moving, edits);
  if (!timeline_tree_can_move_elements (root, moving, error)) {
    goto error;
  }

  /* emit snapping now. Edits should only fail if a programming error
   * occured */
  if (snap)
    ges_timeline_emit_snapping (root->data, snap->element, snap->snapped_to,
        snap->snapped);

  res = timeline_tree_perform_edits (root, edits);

done:
  g_hash_table_unref (edits);
  g_hash_table_unref (moving);
  g_free (snap);
  return res;

error:
  res = FALSE;
  goto done;
}

/********************************************
 *                  Move                    *
 ********************************************/

gboolean
timeline_tree_move (GNode * root, GESTimelineElement * element,
    gint64 layer_priority_offset, GstClockTimeDiff offset, GESEdge edge,
    GstClockTime snapping_distance, GError ** error)
{
  gboolean res = TRUE;
  GHashTable *edits = new_edit_table ();
  GHashTable *moving = new_position_table ();
  ElementEditMode mode;
  SnappedPosition *snap = new_snapped_position (snapping_distance);

  _REPLACE_TRACK_ELEMENT_WITH_PARENT (element);

  switch (edge) {
    case GES_EDGE_END:
      GST_INFO_OBJECT (element, "Moving end with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      mode = EDIT_TRIM_END;
      break;
    case GES_EDGE_START:
      GST_INFO_OBJECT (element, "Moving start with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      mode = EDIT_MOVE;
      break;
    case GES_EDGE_NONE:
      GST_INFO_OBJECT (element, "Moving with toplevel with offset %"
          G_GINT64_FORMAT " and layer offset %" G_GINT64_FORMAT, offset,
          layer_priority_offset);
      element = ges_timeline_element_peak_toplevel (element);
      mode = EDIT_MOVE;
      break;
    default:
      GST_WARNING_OBJECT (element, "Edge not supported");
      goto done;
  }

  /* add edits */
  if (!add_element_edit (edits, element, mode))
    goto error;

  if (!timeline_tree_add_edited_to_moving (root, edits, moving))
    goto error;

  /* snap */
  if (!timeline_tree_snap (root, element, mode, &offset, moving, snap))
    goto error;

  /* check and set edits using snapped values */
  give_edits_same_offset (edits, offset, layer_priority_offset);
  if (!timeline_tree_set_element_edit_values (root, edits, error))
    goto error;

  /* check overlaps */
  set_moving_positions_from_edits (moving, edits);
  if (!timeline_tree_can_move_elements (root, moving, error)) {
    goto error;
  }

  /* emit snapping now. Edits should only fail if a programming error
   * occured */
  if (snap)
    ges_timeline_emit_snapping (root->data, snap->element, snap->snapped_to,
        snap->snapped);

  res = timeline_tree_perform_edits (root, edits);

done:
  g_hash_table_unref (edits);
  g_hash_table_unref (moving);
  g_free (snap);
  return res;

error:
  res = FALSE;
  goto done;
}

/********************************************
 *                  Roll                    *
 ********************************************/

static gboolean
is_descendant (GESTimelineElement * element, GESTimelineElement * ancestor)
{
  GESTimelineElement *parent = element;
  while ((parent = parent->parent)) {
    if (parent == ancestor)
      return TRUE;
  }
  return FALSE;
}

static gboolean
find_neighbour (GNode * node, TreeIterationData * data)
{
  GList *tmp;
  gboolean in_same_track = FALSE;
  GESTimelineElement *edge_element, *element = node->data;

  if (!GES_IS_SOURCE (element))
    return FALSE;

  /* if the element is controlled by the trimmed element (a group or a
   * clip) it is not a neighbour */
  if (is_descendant (element, data->element))
    return FALSE;

  /* test if we share a track with one of the sources at the edge */
  for (tmp = data->sources; tmp; tmp = tmp->next) {
    if (ges_track_element_get_track (GES_TRACK_ELEMENT (element)) ==
        ges_track_element_get_track (tmp->data)) {
      in_same_track = TRUE;
      break;
    }
  }

  if (!in_same_track)
    return FALSE;

  /* get the most toplevel element whose edge touches the position */
  edge_element = NULL;
  while (element && ELEMENT_EDGE_VALUE (element, data->edge) == data->position) {
    edge_element = element;
    element = element->parent;
  }

  if (edge_element && !g_list_find (data->neighbours, edge_element))
    data->neighbours = g_list_prepend (data->neighbours, edge_element);

  return FALSE;
}

static gboolean
find_sources_at_position (GNode * node, TreeIterationData * data)
{
  GESTimelineElement *element = node->data;

  if (!GES_IS_SOURCE (element))
    return FALSE;

  if (ELEMENT_EDGE_VALUE (element, data->edge) == data->position)
    data->sources = g_list_append (data->sources, element);

  return FALSE;
}

gboolean
timeline_tree_roll (GNode * root, GESTimelineElement * element,
    GstClockTimeDiff offset, GESEdge edge, GstClockTime snapping_distance,
    GError ** error)
{
  gboolean res = TRUE;
  GList *tmp;
  GNode *node;
  TreeIterationData data = tree_iteration_data_init;
  GHashTable *edits = new_edit_table ();
  GHashTable *moving = new_position_table ();
  ElementEditMode mode;
  SnappedPosition *snap = new_snapped_position (snapping_distance);

  _REPLACE_TRACK_ELEMENT_WITH_PARENT (element);

  /* if EDGE_END:
   *   TRIM_END the element, and TRIM_START the neighbouring clips to the
   *   end edge
   * otherwise:
   *   TRIM_START the element, and TRIM_END the neighbouring clips to the
   *   start edge */

  switch (edge) {
    case GES_EDGE_END:
      GST_INFO_OBJECT (element, "Rolling end with offset %"
          G_GINT64_FORMAT, offset);
      mode = EDIT_TRIM_END;
      break;
    case GES_EDGE_START:
      GST_INFO_OBJECT (element, "Rolling start with offset %"
          G_GINT64_FORMAT, offset);
      mode = EDIT_TRIM_START;
      break;
    case GES_EDGE_NONE:
      GST_WARNING_OBJECT (element, "Need to select an edge when rolling.");
      goto done;
    default:
      GST_WARNING_OBJECT (element, "Edge not supported");
      goto done;
  }

  /* add edits */
  if (!add_element_edit (edits, element, mode))
    goto error;

  /* first, find all the sources at the edge */
  node = find_node (root, element);
  if (!node) {
    GST_ERROR_OBJECT (element, "Not being tracked");
    goto error;
  }

  data.element = element;
  data.edge = (edge == GES_EDGE_END) ? GES_EDGE_END : GES_EDGE_START;
  data.position = ELEMENT_EDGE_VALUE (element, data.edge);
  data.sources = NULL;

  g_node_traverse (node, G_IN_ORDER, G_TRAVERSE_LEAVES, -1,
      (GNodeTraverseFunc) find_sources_at_position, &data);

  /* find elements that whose opposite edge touches the edge of the
   * element and shares a track with one of the found sources */
  data.edge = (edge == GES_EDGE_END) ? GES_EDGE_START : GES_EDGE_END;
  data.neighbours = NULL;

  g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_LEAVES, -1,
      (GNodeTraverseFunc) find_neighbour, &data);

  for (tmp = data.neighbours; tmp; tmp = tmp->next) {
    GESTimelineElement *clip = tmp->data;
    ElementEditMode opposite =
        (mode == EDIT_TRIM_END) ? EDIT_TRIM_START : EDIT_TRIM_END;
    if (!add_element_edit (edits, clip, opposite))
      goto error;
  }

  if (!timeline_tree_add_edited_to_moving (root, edits, moving))
    goto error;

  /* snap */
  if (!timeline_tree_snap (root, element, mode, &offset, moving, snap))
    goto error;

  /* check and set edits using snapped values */
  give_edits_same_offset (edits, offset, 0);
  if (!timeline_tree_set_element_edit_values (root, edits, error))
    goto error;

  /* check overlaps */
  set_moving_positions_from_edits (moving, edits);
  if (!timeline_tree_can_move_elements (root, moving, error)) {
    goto error;
  }

  /* emit snapping now. Edits should only fail if a programming error
   * occured */
  if (snap)
    ges_timeline_emit_snapping (root->data, snap->element, snap->snapped_to,
        snap->snapped);

  res = timeline_tree_perform_edits (root, edits);

done:
  g_hash_table_unref (edits);
  g_hash_table_unref (moving);
  g_list_free (data.neighbours);
  g_list_free (data.sources);
  g_free (snap);
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
  } else {
    GST_INFO ("Already have transition %" GST_PTR_FORMAT " between %" GES_FORMAT
        " and %" GES_FORMAT, trans, GES_ARGS (prev), GES_ARGS (next));
  }
}

static gboolean
create_transitions (GNode * node,
    GESTreeGetAutoTransitionFunc get_auto_transition)
{
  TreeIterationData data = tree_iteration_data_init;
  GESTimeline *timeline;
  GESLayer *layer;

  if (!GES_IS_SOURCE (node->data))
    return FALSE;

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (node->data);

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

  GST_LOG_OBJECT (node->data, "Checking for overlaps");
  data.root = g_node_get_root (node);
  check_all_overlaps_with_element (node, &data);

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
timeline_tree_create_transitions_for_track_element (GNode * root,
    GESTrackElement * element, GESTreeGetAutoTransitionFunc get_auto_transition)
{
  GNode *node = find_node (root, element);
  g_assert (node);

  create_transitions (node, get_auto_transition);
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

static gboolean
reset_layer_activness (GNode * node, GESLayer * layer)
{
  GESTrack *track;


  if (!GES_IS_TRACK_ELEMENT (node->data))
    return FALSE;

  track = ges_track_element_get_track (node->data);
  if (!track || (ges_timeline_element_get_layer_priority (node->data) !=
          ges_layer_get_priority (layer)))
    return FALSE;

  ges_track_element_set_layer_active (node->data,
      ges_layer_get_active_for_track (layer, track));

  return FALSE;
}

void
timeline_tree_reset_layer_active (GNode * root, GESLayer * layer)
{
  g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_LEAFS, -1,
      (GNodeTraverseFunc) reset_layer_activness, layer);
}

static gboolean
set_is_smart_rendering (GNode * node, gboolean * is_rendering_smartly)
{
  if (!GES_IS_SOURCE (node->data))
    return FALSE;

  ges_source_set_rendering_smartly (GES_SOURCE (node->data),
      *is_rendering_smartly);
  return FALSE;
}

void
timeline_tree_set_smart_rendering (GNode * root, gboolean rendering_smartly)
{
  g_node_traverse (root, G_PRE_ORDER, G_TRAVERSE_LEAFS, -1,
      (GNodeTraverseFunc) set_is_smart_rendering, &rendering_smartly);
}
