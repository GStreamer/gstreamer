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
 * @short_description: Base Class for objects in a GESLayer
 *
 * A #GESClip is a 'natural' object which controls one or more
 * #GESTrackElement(s) in one or more #GESTrack(s).
 *
 * Keeps a reference to the #GESTrackElement(s) it created and
 * sets/updates their properties.
 */

#include "ges-clip.h"
#include "ges.h"
#include "ges-internal.h"

#include <string.h>

GList *ges_clip_create_track_elements_func (GESClip * clip, GESTrackType type);
static gboolean _ripple (GESTimelineElement * element, GstClockTime start);
static gboolean _ripple_end (GESTimelineElement * element, GstClockTime end);
static gboolean _roll_start (GESTimelineElement * element, GstClockTime start);
static gboolean _roll_end (GESTimelineElement * element, GstClockTime end);
static gboolean _trim (GESTimelineElement * element, GstClockTime start);
static void _compute_height (GESContainer * container);

G_DEFINE_ABSTRACT_TYPE (GESClip, ges_clip, GES_TYPE_CONTAINER);

struct _GESClipPrivate
{
  /*< public > */
  GESLayer *layer;

  /*< private > */

  /* Set to TRUE when the clip is doing updates of track element
   * properties so we don't end up in infinite property update loops
   */
  gboolean is_moving;

  guint nb_effects;

  GList *copied_track_elements;
  GESLayer *copied_layer;

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

/****************************************************
 *              Listen to our children              *
 ****************************************************/

/* @min_priority: The absolute minimum priority a child of @container should have
 * @max_priority: The absolute maximum priority a child of @container should have
 */
static void
_get_priority_range (GESContainer * container, guint32 * min_priority,
    guint32 * max_priority)
{
  GESLayer *layer = GES_CLIP (container)->priv->layer;

  if (layer) {
    *min_priority = _PRIORITY (container) + layer->min_nle_priority;
    *max_priority = layer->max_nle_priority;
  } else {
    *min_priority = _PRIORITY (container) + MIN_NLE_PRIO;
    *max_priority = G_MAXUINT32;
  }
}

static void
_child_priority_changed_cb (GESTimelineElement * child,
    GParamSpec * arg G_GNUC_UNUSED, GESContainer * container)
{
  guint32 min_prio, max_prio;

  GST_DEBUG_OBJECT (container, "TimelineElement %p priority changed to %i",
      child, _PRIORITY (child));

  if (container->children_control_mode == GES_CHILDREN_IGNORE_NOTIFIES)
    return;

  /* Update mapping */
  _get_priority_range (container, &min_prio, &max_prio);

  _ges_container_set_priority_offset (container, child,
      min_prio - _PRIORITY (child));
}

/*****************************************************
 *                                                   *
 * GESTimelineElement virtual methods implementation *
 *                                                   *
 *****************************************************/

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GList *tmp;
  GESTimeline *timeline;
  GESContainer *container = GES_CONTAINER (element);

  GST_DEBUG_OBJECT (element, "Setting children start, (initiated_move: %"
      GST_PTR_FORMAT ")", container->initiated_move);

  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    if (child != container->initiated_move) {
      /* Make the snapping happen if in a timeline */
      timeline = GES_TIMELINE_ELEMENT_TIMELINE (child);
      if (timeline == NULL || ges_timeline_move_object_simple (timeline, child,
              NULL, GES_EDGE_NONE, start) == FALSE)
        _set_start0 (GES_TIMELINE_ELEMENT (child), start);

    }
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;

  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  GList *tmp;
  GESContainer *container = GES_CONTAINER (element);

  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    if (child != container->initiated_move) {
      _set_inpoint0 (child, inpoint);
    }
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;

  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GList *tmp;
  GESTimeline *timeline;

  GESContainer *container = GES_CONTAINER (element);

  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  for (tmp = container->children; tmp; tmp = g_list_next (tmp)) {
    GESTimelineElement *child = (GESTimelineElement *) tmp->data;

    if (child != container->initiated_move) {
      /* Make the snapping happen if in a timeline */
      timeline = GES_TIMELINE_ELEMENT_TIMELINE (child);
      if (timeline == NULL || ges_timeline_trim_object_simple (timeline, child,
              NULL, GES_EDGE_END, _START (child) + duration, TRUE) == FALSE)
        _set_duration0 (GES_TIMELINE_ELEMENT (child), duration);
    }
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;

  return TRUE;
}

static gboolean
_set_max_duration (GESTimelineElement * element, GstClockTime maxduration)
{
  GList *tmp;

  for (tmp = GES_CONTAINER (element)->children; tmp; tmp = g_list_next (tmp))
    ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (tmp->data),
        maxduration);

  return TRUE;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GList *tmp;
  guint32 min_prio, max_prio;

  GESContainer *container = GES_CONTAINER (element);

  _get_priority_range (container, &min_prio, &max_prio);

  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
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

    /* We need to remove our current priority from @min_prio
     * as it is the absolute minimum priority @child could have
     * before we set @container to @priority.
     */
    track_element_prio = min_prio - _PRIORITY (container) + priority - off;

    if (track_element_prio > max_prio) {
      GST_WARNING ("%p priority of %i, is outside of the its containing "
          "layer space. (%d/%d) setting it to the maximum it can be",
          container, priority, min_prio - _PRIORITY (container) + priority,
          max_prio);

      track_element_prio = max_prio;
    }
    _set_priority0 (child, track_element_prio);
  }
  container->children_control_mode = GES_CHILDREN_UPDATE;
  _compute_height (container);

  return TRUE;
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
  GList *tmp;
  guint max_prio, min_prio;
  GESClipPrivate *priv = GES_CLIP (container)->priv;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (element), FALSE);

  /* First make sure we work with a sorted list of GESTimelineElement-s */
  _ges_container_sort_children (container);

  /* If the TrackElement is an effect:
   *  - We add it on top of the list of TrackEffect
   *  - We put all TrackObject present in the TimelineObject
   *    which are not BaseEffect on top of them
   * FIXME: Let the full control over priorities to the user
   */
  _get_priority_range (container, &min_prio, &max_prio);
  if (GES_IS_BASE_EFFECT (element)) {
    GESChildrenControlMode mode = container->children_control_mode;

    GST_DEBUG_OBJECT (container, "Adding %ith effect: %" GST_PTR_FORMAT
        " Priority %i", priv->nb_effects + 1, element,
        min_prio + priv->nb_effects);

    tmp = g_list_nth (GES_CONTAINER_CHILDREN (container), priv->nb_effects);
    container->children_control_mode = GES_CHILDREN_UPDATE_OFFSETS;
    for (; tmp; tmp = tmp->next) {
      ges_timeline_element_set_priority (GES_TIMELINE_ELEMENT (tmp->data),
          GES_TIMELINE_ELEMENT_PRIORITY (tmp->data) + 1);
    }

    _set_priority0 (element, min_prio + priv->nb_effects);
    container->children_control_mode = mode;
    priv->nb_effects++;
  } else {
    /* We add the track element on top of the effect list */
    _set_priority0 (element, min_prio + priv->nb_effects);
  }

  /* We set the timing value of the child to ours, we avoid infinite loop
   * making sure the container ignore notifies from the child */
  container->children_control_mode = GES_CHILDREN_IGNORE_NOTIFIES;
  _set_start0 (element, GES_TIMELINE_ELEMENT_START (container));
  _set_inpoint0 (element, GES_TIMELINE_ELEMENT_INPOINT (container));
  _set_duration0 (element, GES_TIMELINE_ELEMENT_DURATION (container));
  container->children_control_mode = GES_CHILDREN_UPDATE;

  return TRUE;
}

static gboolean
_remove_child (GESContainer * container, GESTimelineElement * element)
{
  if (GES_IS_BASE_EFFECT (element))
    GES_CLIP (container)->priv->nb_effects--;

  GST_FIXME_OBJECT (container, "We should set other children prios");

  return TRUE;
}

static void
_child_added (GESContainer * container, GESTimelineElement * element)
{
  g_signal_connect (G_OBJECT (element), "notify::priority",
      G_CALLBACK (_child_priority_changed_cb), container);

  _child_priority_changed_cb (element, NULL, container);
  _compute_height (container);
}

static void
_child_removed (GESContainer * container, GESTimelineElement * element)
{
  g_signal_handlers_disconnect_by_func (element, _child_priority_changed_cb,
      container);

  _compute_height (container);
}

static void
add_clip_to_list (gpointer key, gpointer clip, GList ** list)
{
  *list = g_list_prepend (*list, gst_object_ref (clip));
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
    if (tmpclip != clip) {
      /* We need to bump the refcount to avoid the object to be destroyed */
      gst_object_ref (track_element);
      ges_container_remove (container, GES_TIMELINE_ELEMENT (track_element));
      ges_container_add (GES_CONTAINER (tmpclip),
          GES_TIMELINE_ELEMENT (track_element));
      gst_object_unref (track_element);
    }
  }
  g_list_free_full (children, gst_object_unref);
  g_hash_table_foreach (_tracktype_clip, (GHFunc) add_clip_to_list, &ret);
  g_hash_table_unref (_tracktype_clip);

  return ret;
}

static GESContainer *
_group (GList * containers)
{
  CheckTrack *tracks = NULL;
  GESTimeline *timeline = NULL;
  GESTrackType supported_formats;
  GESLayer *layer = NULL;
  GList *tmp, *tmpclip, *tmpelement;
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
      if (start != _START (tmpcontainer) ||
          inpoint != _INPOINT (tmpcontainer) ||
          duration != _DURATION (tmpcontainer) || clip->priv->layer != layer) {
        GST_INFO ("All children must have the same start, inpoint, duration "
            " and be in the same layer");

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

  /* And now pass all TrackElements to the first clip,
   * and remove others from the layer (updating the supported formats) */
  ret = containers->data;
  supported_formats = GES_CLIP (ret)->priv->supportedformats;
  for (tmpclip = containers->next; tmpclip; tmpclip = tmpclip->next) {
    GESClip *cclip = tmpclip->data;
    GList *children = ges_container_get_children (GES_CONTAINER (cclip), FALSE);

    for (tmpelement = children; tmpelement; tmpelement = tmpelement->next) {
      GESTimelineElement *celement = GES_TIMELINE_ELEMENT (tmpelement->data);

      ges_container_remove (GES_CONTAINER (cclip), celement);
      ges_container_add (ret, celement);

      supported_formats = supported_formats |
          ges_track_element_get_track_type (GES_TRACK_ELEMENT (celement));
    }
    g_list_free_full (children, gst_object_unref);

    ges_layer_remove_clip (layer, tmpclip->data);
  }

  ges_clip_set_supported_formats (GES_CLIP (ret), supported_formats);

done:
  if (tracks)
    g_free (tracks);


  return ret;

}

static gboolean
_edit (GESContainer * container, GList * layers,
    gint new_layer_priority, GESEditMode mode, GESEdge edge, guint64 position)
{
  GList *tmp;
  gboolean ret = TRUE;
  GESLayer *layer;

  if (!G_UNLIKELY (GES_CONTAINER_CHILDREN (container))) {
    GST_WARNING_OBJECT (container, "Trying to edit, but not containing"
        "any TrackElement yet.");
    return FALSE;
  }

  for (tmp = GES_CONTAINER_CHILDREN (container); tmp; tmp = g_list_next (tmp)) {
    if (GES_IS_SOURCE (tmp->data) || GES_IS_TRANSITION (tmp->data)) {
      ret &= ges_track_element_edit (tmp->data, layers, mode, edge, position);
      break;
    }
  }

  /* Moving to layer */
  if (new_layer_priority == -1) {
    GST_DEBUG_OBJECT (container, "Not moving new prio %d", new_layer_priority);
  } else {
    gint priority_offset;

    layer = GES_CLIP (container)->priv->layer;
    if (layer == NULL) {
      GST_WARNING_OBJECT (container, "Not in any layer yet, not moving");

      return FALSE;
    }
    priority_offset = new_layer_priority - ges_layer_get_priority (layer);

    ret &= timeline_context_to_layer (layer->timeline, priority_offset);
  }

  return ret;
}

static void
_deep_copy (GESTimelineElement * element, GESTimelineElement * copy)
{
  GList *tmp;
  GESClip *self = GES_CLIP (element), *ccopy = GES_CLIP (copy);

  for (tmp = GES_CONTAINER_CHILDREN (element); tmp; tmp = tmp->next) {
    ccopy->priv->copied_track_elements =
        g_list_append (ccopy->priv->copied_track_elements,
        ges_timeline_element_copy (tmp->data, TRUE));
  }

  if (self->priv->copied_layer)
    ccopy->priv->copied_layer = g_object_ref (self->priv->copied_layer);
  else if (self->priv->layer)
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

  ges_clip_set_moving_from_layer (nclip, TRUE);
  if (self->priv->copied_layer)
    ges_layer_add_clip (self->priv->copied_layer, nclip);
  ges_clip_set_moving_from_layer (nclip, FALSE);

  ges_timeline_element_set_start (GES_TIMELINE_ELEMENT (nclip), paste_position);

  for (tmp = self->priv->copied_track_elements; tmp; tmp = tmp->next) {
    GESTrackElement *new_trackelement, *trackelement =
        GES_TRACK_ELEMENT (tmp->data);

    new_trackelement =
        GES_TRACK_ELEMENT (ges_timeline_element_copy (GES_TIMELINE_ELEMENT
            (trackelement), FALSE));
    if (new_trackelement == NULL) {
      GST_WARNING_OBJECT (trackelement, "Could not create a copy");
      continue;
    }

    ges_container_add (GES_CONTAINER (nclip),
        GES_TIMELINE_ELEMENT (new_trackelement));

    ges_track_element_copy_properties (GES_TIMELINE_ELEMENT (trackelement),
        GES_TIMELINE_ELEMENT (new_trackelement));

    ges_track_element_copy_bindings (trackelement, new_trackelement,
        GST_CLOCK_TIME_NONE);
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
ges_clip_finalize (GObject * object)
{
  GESClip *self = GES_CLIP (object);

  g_list_free_full (self->priv->copied_track_elements, g_object_unref);

  G_OBJECT_CLASS (ges_clip_parent_class)->finalize (object);
}

static void
ges_clip_class_init (GESClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESContainerClass *container_class = GES_CONTAINER_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESClipPrivate));

  object_class->get_property = ges_clip_get_property;
  object_class->set_property = ges_clip_set_property;
  object_class->finalize = ges_clip_finalize;
  klass->create_track_elements = ges_clip_create_track_elements_func;
  klass->create_track_element = NULL;

  /**
   * GESClip:supported-formats:
   *
   * The formats supported by the clip.
   */
  properties[PROP_SUPPORTED_FORMATS] = g_param_spec_flags ("supported-formats",
      "Supported formats", "Formats supported by the file",
      GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_AUDIO | GES_TRACK_TYPE_VIDEO,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_SUPPORTED_FORMATS,
      properties[PROP_SUPPORTED_FORMATS]);

  /**
   * GESClip:layer:
   *
   * The GESLayer where this clip is being used. If you want to connect to its
   * notify signal you should connect to it with g_signal_connect_after as the
   * signal emission can be stop in the first fase.
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

  container_class->add_child = _add_child;
  container_class->remove_child = _remove_child;
  container_class->child_removed = _child_removed;
  container_class->child_added = _child_added;
  container_class->ungroup = _ungroup;
  container_class->group = _group;
  container_class->grouping_priority = G_MAXUINT;
  container_class->edit = _edit;
}

static void
ges_clip_init (GESClip * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_CLIP, GESClipPrivate);
  /* FIXME, check why it was done this way _DURATION (self) = GST_SECOND; */
  self->priv->layer = NULL;
  self->priv->nb_effects = 0;
  self->priv->is_moving = FALSE;
}

/**
 * ges_clip_create_track_element:
 * @clip: The origin #GESClip
 * @type: The #GESTrackType to create a #GESTrackElement for.
 *
 * Creates a #GESTrackElement for the provided @type. The clip
 * keep a reference to the newly created trackelement, you therefore need to
 * call @ges_container_remove when you are done with it.
 *
 * Returns: (transfer none) (nullable): A #GESTrackElement. Returns NULL if
 * the #GESTrackElement could not be created.
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
 * @clip: The origin #GESClip
 * @type: The #GESTrackType to create each #GESTrackElement for.
 *
 * Creates all #GESTrackElements supported by this clip for the track type.
 *
 * Returns: (element-type GESTrackElement) (transfer full): A #GList of
 * newly created #GESTrackElement-s
 */

GList *
ges_clip_create_track_elements (GESClip * clip, GESTrackType type)
{
  GList *result = NULL, *tmp, *children;
  GESClipClass *klass;
  guint max_prio, min_prio;

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

    if (!GES_IS_BASE_EFFECT (child) && !ges_track_element_get_track (child) &&
        ges_track_element_get_track_type (child) & type) {

      GST_DEBUG_OBJECT (clip, "Removing for reusage: %" GST_PTR_FORMAT, child);
      result = g_list_prepend (result, g_object_ref (child));
      ges_container_remove (GES_CONTAINER (clip), tmp->data);
    }
  }
  g_list_free_full (children, gst_object_unref);

  if (!result) {
    result = klass->create_track_elements (clip, type);
  }

  _get_priority_range (GES_CONTAINER (clip), &min_prio, &max_prio);
  for (tmp = result; tmp; tmp = tmp->next) {
    GESTimelineElement *elem = tmp->data;

    _set_start0 (elem, GES_TIMELINE_ELEMENT_START (clip));
    _set_inpoint0 (elem, GES_TIMELINE_ELEMENT_INPOINT (clip));
    _set_duration0 (elem, GES_TIMELINE_ELEMENT_DURATION (clip));

    if (GST_CLOCK_TIME_IS_VALID (GES_TIMELINE_ELEMENT_MAX_DURATION (clip)))
      ges_timeline_element_set_max_duration (GES_TIMELINE_ELEMENT (elem),
          GES_TIMELINE_ELEMENT_MAX_DURATION (clip));

    _set_priority0 (elem, min_prio + clip->priv->nb_effects);

    ges_container_add (GES_CONTAINER (clip), elem);
  }

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
  if (!clip->priv->is_moving)
    g_object_notify_by_pspec (G_OBJECT (clip), properties[PROP_LAYER]);
}

guint32
ges_clip_get_layer_priority (GESClip * clip)
{
  if (clip->priv->layer == NULL)
    return -1;

  return ges_layer_get_priority (clip->priv->layer);
}

/**
 * ges_clip_set_moving_from_layer:
 * @clip: a #GESClip
 * @is_moving: %TRUE if you want to start moving @clip to another layer
 * %FALSE when you finished moving it.
 *
 * Sets the clip in a moving to layer state. You might rather use the
 * ges_clip_move_to_layer function to move #GESClip-s
 * from a layer to another.
 **/
void
ges_clip_set_moving_from_layer (GESClip * clip, gboolean is_moving)
{
  g_return_if_fail (GES_IS_CLIP (clip));

  clip->priv->is_moving = is_moving;
}

/**
 * ges_clip_is_moving_from_layer:
 * @clip: a #GESClip
 *
 * Tells you if the clip is currently moving from a layer to another.
 * You might rather use the ges_clip_move_to_layer function to
 * move #GESClip-s from a layer to another.
 *
 *
 * Returns: %TRUE if @clip is currently moving from its current layer
 * %FALSE otherwize
 **/
gboolean
ges_clip_is_moving_from_layer (GESClip * clip)
{
  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);

  return clip->priv->is_moving;
}

/**
 * ges_clip_move_to_layer:
 * @clip: a #GESClip
 * @layer: the new #GESLayer
 *
 * Moves @clip to @layer. If @clip is not in any layer, it adds it to
 * @layer, else, it removes it from its current layer, and adds it to @layer.
 *
 * Returns: %TRUE if @clip could be moved %FALSE otherwize
 */
gboolean
ges_clip_move_to_layer (GESClip * clip, GESLayer * layer)
{
  gboolean ret;
  GESLayer *current_layer;

  g_return_val_if_fail (GES_IS_CLIP (clip), FALSE);
  g_return_val_if_fail (GES_IS_LAYER (layer), FALSE);

  current_layer = clip->priv->layer;

  if (current_layer == NULL) {
    GST_DEBUG ("Not moving %p, only adding it to %p", clip, layer);

    return ges_layer_add_clip (layer, clip);
  }

  GST_DEBUG_OBJECT (clip, "moving to layer %p, priority: %d", layer,
      ges_layer_get_priority (layer));

  clip->priv->is_moving = TRUE;
  gst_object_ref (clip);
  ret = ges_layer_remove_clip (current_layer, clip);

  if (!ret) {
    gst_object_unref (clip);
    return FALSE;
  }

  ret = ges_layer_add_clip (layer, clip);
  clip->priv->is_moving = FALSE;

  gst_object_unref (clip);
  g_object_notify_by_pspec (G_OBJECT (clip), properties[PROP_LAYER]);


  return ret && (clip->priv->layer == layer);
}

/**
 * ges_clip_find_track_element:
 * @clip: a #GESClip
 * @track: (allow-none): a #GESTrack or NULL
 * @type: a #GType indicating the type of track element you are looking
 * for or %G_TYPE_NONE if you do not care about the track type.
 *
 * Finds the #GESTrackElement controlled by @clip that is used in @track. You
 * may optionally specify a GType to further narrow search criteria.
 *
 * Note: If many objects match, then the one with the highest priority will be
 * returned.
 *
 * Returns: (transfer full) (nullable): The #GESTrackElement used by @track,
 * else %NULL. Unref after usage
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
 * @clip: a #GESClip
 *
 * Get the #GESLayer to which this clip belongs.
 *
 * Returns: (transfer full) (nullable): The #GESLayer where this @clip is being
 * used, or %NULL if it is not used on any layer. The caller should unref it
 * usage.
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
 * @clip: The origin #GESClip
 *
 * Get effects applied on @clip
 *
 * Returns: (transfer full) (element-type GESTrackElement): a #GList of the
 * #GESBaseEffect that are applied on @clip order by ascendant priorities.
 * The refcount of the objects will be increased. The user will have to
 * unref each #GESBaseEffect and free the #GList.
 */
GList *
ges_clip_get_top_effects (GESClip * clip)
{
  GList *tmp, *ret;
  guint i;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);

  GST_DEBUG_OBJECT (clip, "Getting the %i top effects", clip->priv->nb_effects);
  ret = NULL;

  for (tmp = GES_CONTAINER_CHILDREN (clip), i = 0;
      i < clip->priv->nb_effects; tmp = tmp->next, i++) {
    ret = g_list_append (ret, gst_object_ref (tmp->data));
  }

  return g_list_sort (ret, (GCompareFunc) element_start_compare);
}

/**
 * ges_clip_get_top_effect_index:
 * @clip: The origin #GESClip
 * @effect: The #GESBaseEffect we want to get the top index from
 *
 * Gets the index position of an effect.
 *
 * Returns: The top index of the effect, -1 if something went wrong.
 */
gint
ges_clip_get_top_effect_index (GESClip * clip, GESBaseEffect * effect)
{
  guint max_prio, min_prio;

  g_return_val_if_fail (GES_IS_CLIP (clip), -1);
  g_return_val_if_fail (GES_IS_BASE_EFFECT (effect), -1);

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
 * @clip: The origin #GESClip
 * @effect: The #GESBaseEffect to move
 * @newindex: the new index at which to move the @effect inside this
 * #GESClip
 *
 * This is a convenience method that lets you set the index of a top effect.
 *
 * Returns: %TRUE if @effect was successfuly moved, %FALSE otherwise.
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

  track_element = GES_TRACK_ELEMENT (effect);
  current_prio = _PRIORITY (track_element);

  _get_priority_range (GES_CONTAINER (clip), &min_prio, &max_prio);

  newindex = newindex + min_prio;
  /*  We don't change the priority */
  if (current_prio == newindex ||
      (G_UNLIKELY (GES_CLIP (GES_TIMELINE_ELEMENT_PARENT (track_element)) !=
              clip)))
    return FALSE;

  if (newindex > (clip->priv->nb_effects - 1 + min_prio)) {
    GST_DEBUG ("You are trying to make %p not a top effect", effect);
    return FALSE;
  }

  if (current_prio > clip->priv->nb_effects + min_prio) {
    GST_ERROR ("%p is not a top effect", effect);
    return FALSE;
  }

  _ges_container_sort_children (GES_CONTAINER (clip));
  if (_PRIORITY (track_element) < newindex)
    inc = -1;
  else
    inc = +1;

  GST_DEBUG_OBJECT (clip, "Setting top effect %" GST_PTR_FORMAT "priority: %i",
      effect, newindex);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = tmp->next) {
    GESTrackElement *tmpo = GES_TRACK_ELEMENT (tmp->data);
    guint tck_priority = _PRIORITY (tmpo);

    if (tmpo == track_element)
      continue;

    if ((inc == +1 && tck_priority >= newindex) ||
        (inc == -1 && tck_priority <= newindex)) {
      _set_priority0 (GES_TIMELINE_ELEMENT (tmpo), tck_priority + inc);
    }
  }
  _set_priority0 (GES_TIMELINE_ELEMENT (track_element), newindex);

  return TRUE;
}

/**
 * ges_clip_split:
 * @clip: the #GESClip to split
 * @position: a #GstClockTime representing the timeline position at which to split
 *
 * The function modifies @clip, and creates another #GESClip so we have two
 * clips at the end, splitted at the time specified by @position, as a position
 * in the timeline (not in the clip to be split). For example, if
 * ges_clip_split is called on a 4-second clip playing from 0:01.00 until
 * 0:05.00, with a split position of 0:02.00, this will result in one clip of 1
 * second and one clip of 3 seconds, not in two clips of 2 seconds.
 *
 * The newly created clip will be added to the same layer as @clip is in. This
 * implies that @clip must be in a #GESLayer for the operation to be possible.
 *
 * This method supports clips playing at a different tempo than one second per
 * second. For example, splitting a clip with a #GESEffect 'pitch tempo=1.5'
 * four seconds after it starts, will set the inpoint of the new clip to six
 * seconds after that of the clip to split. For this, the rate-changing
 * property must be registered using @ges_effect_class_register_rate_property;
 * for the 'pitch' plugin, this is already done.
 *
 * Returns: (transfer none) (nullable): The newly created #GESClip resulting
 * from the splitting or %NULL if the clip can't be split.
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
  new_duration = duration + start - position;
  old_duration = position - start;
  _set_start0 (GES_TIMELINE_ELEMENT (new_object), position);
  _set_inpoint0 (GES_TIMELINE_ELEMENT (new_object),
      inpoint + old_duration * media_duration_factor);
  _set_duration0 (GES_TIMELINE_ELEMENT (new_object), new_duration);

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

    /* Set 'new' track element timing propeties */
    _set_start0 (GES_TIMELINE_ELEMENT (new_trackelement), position);
    _set_inpoint0 (GES_TIMELINE_ELEMENT (new_trackelement),
        inpoint + old_duration * media_duration_factor);
    _set_duration0 (GES_TIMELINE_ELEMENT (new_trackelement), new_duration);

    ges_container_add (GES_CONTAINER (new_object),
        GES_TIMELINE_ELEMENT (new_trackelement));
    ges_track_element_copy_properties (GES_TIMELINE_ELEMENT (trackelement),
        GES_TIMELINE_ELEMENT (new_trackelement));

    ges_track_element_copy_bindings (trackelement, new_trackelement,
        position - start + inpoint);
  }

  _set_duration0 (GES_TIMELINE_ELEMENT (clip), old_duration);

  return new_object;
}

/**
 * ges_clip_set_supported_formats:
 * @clip: the #GESClip to set supported formats on
 * @supportedformats: the #GESTrackType defining formats supported by @clip
 *
 * Sets the formats supported by the file.
 */
void
ges_clip_set_supported_formats (GESClip * clip, GESTrackType supportedformats)
{
  g_return_if_fail (GES_IS_CLIP (clip));

  clip->priv->supportedformats = supportedformats;
}

/**
 * ges_clip_get_supported_formats:
 * @clip: the #GESClip
 *
 * Get the formats supported by @clip.
 *
 * Returns: The formats supported by @clip.
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
  gboolean ret = TRUE;
  GESTimeline *timeline;
  GESClip *clip = GES_CLIP (element);

  timeline = ges_layer_get_timeline (clip->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  if (start > _END (element))
    start = _END (element);

  if (GES_CONTAINER_CHILDREN (element)) {
    GESTrackElement *track_element =
        GES_TRACK_ELEMENT (GES_CONTAINER_CHILDREN (element)->data);

    ret = timeline_ripple_object (timeline, track_element, NULL, GES_EDGE_NONE,
        start);
  }

  return ret;
}

static gboolean
_ripple_end (GESTimelineElement * element, GstClockTime end)
{
  gboolean ret = TRUE;
  GESTimeline *timeline;
  GESClip *clip = GES_CLIP (element);

  timeline = ges_layer_get_timeline (clip->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  if (GES_CONTAINER_CHILDREN (element)) {
    GESTrackElement *track_element =
        GES_TRACK_ELEMENT (GES_CONTAINER_CHILDREN (element)->data);

    ret = timeline_ripple_object (timeline, track_element, NULL, GES_EDGE_END,
        end);
  }

  return ret;
}

gboolean
_roll_start (GESTimelineElement * element, GstClockTime start)
{
  gboolean ret = TRUE;
  GESTimeline *timeline;

  GESClip *clip = GES_CLIP (element);

  timeline = ges_layer_get_timeline (clip->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  if (GES_CONTAINER_CHILDREN (element)) {
    GESTrackElement *track_element =
        GES_TRACK_ELEMENT (GES_CONTAINER_CHILDREN (element)->data);

    ret = timeline_roll_object (timeline, track_element, NULL, GES_EDGE_START,
        start);
  }

  return ret;
}

gboolean
_roll_end (GESTimelineElement * element, GstClockTime end)
{
  gboolean ret = TRUE;
  GESTimeline *timeline;

  GESClip *clip = GES_CLIP (element);

  timeline = ges_layer_get_timeline (clip->priv->layer);
  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }


  if (GES_CONTAINER_CHILDREN (element)) {
    GESTrackElement *track_element =
        GES_TRACK_ELEMENT (GES_CONTAINER_CHILDREN (element)->data);

    ret = timeline_roll_object (timeline, track_element,
        NULL, GES_EDGE_END, end);
  }

  return ret;
}

gboolean
_trim (GESTimelineElement * element, GstClockTime start)
{
  gboolean ret = TRUE;
  GESTimeline *timeline;

  GESClip *clip = GES_CLIP (element);

  timeline = ges_layer_get_timeline (clip->priv->layer);

  if (timeline == NULL) {
    GST_DEBUG ("Not in a timeline yet");
    return FALSE;
  }

  if (GES_CONTAINER_CHILDREN (element)) {
    GESTrackElement *track_element =
        GES_TRACK_ELEMENT (GES_CONTAINER_CHILDREN (element)->data);

    GST_DEBUG_OBJECT (element, "Trimming child: %" GST_PTR_FORMAT,
        track_element);
    ret = timeline_trim_object (timeline, track_element, NULL, GES_EDGE_START,
        start);
  }

  return ret;
}

/**
 * ges_clip_add_asset:
 * @clip: a #GESClip
 * @asset: a #GESAsset with #GES_TYPE_TRACK_ELEMENT as extractable_type
 *
 * Extracts a #GESTrackElement from @asset and adds it to the @clip.
 * Should only be called in order to add operations to a #GESClip,
 * ni other cases TrackElement are added automatically when adding the
 * #GESClip/#GESAsset to a layer.
 *
 * Takes a reference on @track_element.
 *
 * Returns: (transfer none)(allow-none): Created #GESTrackElement or NULL
 * if an error happened
 */
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
 * @clip: a #GESClip
 * @track: (allow-none): a #GESTrack or NULL
 * @track_type: a #GESTrackType indicating the type of tracks in which elements
 * should be searched.
 * @type: a #GType indicating the type of track element you are looking
 * for or %G_TYPE_NONE if you do not care about the track type.
 *
 * Finds all the #GESTrackElement controlled by @clip that is used in @track. You
 * may optionally specify a GType to further narrow search criteria.
 *
 * Returns: (transfer full) (element-type GESTrackElement): a #GList of the
 * #GESTrackElement contained in @clip.
 * The refcount of the objects will be increased. The user will have to
 * unref each #GESTrackElement and free the #GList.
 */

GList *
ges_clip_find_track_elements (GESClip * clip, GESTrack * track,
    GESTrackType track_type, GType type)
{
  GList *tmp;
  GESTrack *tmptrack;
  GESTrackElement *otmp;
  GESTrackElement *foundElement;

  GList *ret = NULL;

  g_return_val_if_fail (GES_IS_CLIP (clip), NULL);
  g_return_val_if_fail (!(track == NULL && type == G_TYPE_NONE &&
          track_type == GES_TRACK_TYPE_UNKNOWN), NULL);

  for (tmp = GES_CONTAINER_CHILDREN (clip); tmp; tmp = g_list_next (tmp)) {
    otmp = (GESTrackElement *) tmp->data;

    if ((type != G_TYPE_NONE) && !G_TYPE_CHECK_INSTANCE_TYPE (tmp->data, type))
      continue;

    tmptrack = ges_track_element_get_track (otmp);
    if (((track != NULL && tmptrack == track)) ||
        (track_type != GES_TRACK_TYPE_UNKNOWN
            && ges_track_element_get_track_type (otmp) == track_type)) {

      foundElement = GES_TRACK_ELEMENT (tmp->data);

      ret = g_list_append (ret, gst_object_ref (foundElement));
    }
  }

  return ret;
}
