/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
 *               2011 Mathieu Duponchelle <mathieu.duponchelle@epitech.eu>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:ges-timeline-layer
 * @short_description: Non-overlapping sequence of #GESTimelineObject
 *
 * Responsible for the ordering of the various contained TimelineObject(s). A
 * timeline layer has a "priority" property, which is used to manage the
 * priorities of individual TimelineObjects. Two layers should not have the
 * same priority within a given timeline.
 */

#include "ges-internal.h"
#include "gesmarshal.h"
#include "ges-timeline-layer.h"
#include "ges.h"
#include "ges-timeline-source.h"

#define LAYER_HEIGHT 1000

static void
timeline_object_height_changed_cb (GESTimelineObject * obj,
    GESTrackEffect * tr_eff, GESTimelineObject * second_obj);

G_DEFINE_TYPE (GESTimelineLayer, ges_timeline_layer, G_TYPE_INITIALLY_UNOWNED);

struct _GESTimelineLayerPrivate
{
  /*< private > */
  GList *objects_start;         /* The TimelineObjects sorted by start and
                                 * priority */

  guint32 priority;             /* The priority of the layer within the
                                 * containing timeline */
  gboolean auto_transition;
};

enum
{
  PROP_0,
  PROP_PRIORITY,
  PROP_AUTO_TRANSITION,
  PROP_LAST
};

enum
{
  OBJECT_ADDED,
  OBJECT_REMOVED,
  LAST_SIGNAL
};

static guint ges_timeline_layer_signals[LAST_SIGNAL] = { 0 };

/* GObject standard vmethods */
static void
ges_timeline_layer_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);

  switch (property_id) {
    case PROP_PRIORITY:
      g_value_set_uint (value, layer->priv->priority);
      break;
    case PROP_AUTO_TRANSITION:
      g_value_set_boolean (value, layer->priv->auto_transition);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_layer_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);

  switch (property_id) {
    case PROP_PRIORITY:
      ges_timeline_layer_set_priority (layer, g_value_get_uint (value));
      break;
    case PROP_AUTO_TRANSITION:
      ges_timeline_layer_set_auto_transition (layer,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_layer_dispose (GObject * object)
{
  GESTimelineLayer *layer = GES_TIMELINE_LAYER (object);
  GESTimelineLayerPrivate *priv = layer->priv;

  GST_DEBUG ("Disposing layer");

  while (priv->objects_start)
    ges_timeline_layer_remove_object (layer,
        (GESTimelineObject *) priv->objects_start->data);

  G_OBJECT_CLASS (ges_timeline_layer_parent_class)->dispose (object);
}

static void
ges_timeline_layer_class_init (GESTimelineLayerClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineLayerPrivate));

  object_class->get_property = ges_timeline_layer_get_property;
  object_class->set_property = ges_timeline_layer_set_property;
  object_class->dispose = ges_timeline_layer_dispose;

  /**
   * GESTimelineLayer:priority
   *
   * The priority of the layer in the #GESTimeline. 0 is the highest
   * priority. Conceptually, a #GESTimeline is a stack of GESTimelineLayers,
   * and the priority of the layer represents its position in the stack. Two
   * layers should not have the same priority within a given GESTimeline.
   */
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_uint ("priority", "Priority",
          "The priority of the layer", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

  /**
   * GESTimelineLayer:auto-transition
   *
   * Sets whether transitions are added automagically when timeline objects overlap.
   */
  g_object_class_install_property (object_class, PROP_AUTO_TRANSITION,
      g_param_spec_boolean ("auto-transition", "Auto-Transition",
          "whether the transitions are added", FALSE, G_PARAM_READWRITE));

  /**
   * GESTimelineLayer::object-added
   * @layer: the #GESTimelineLayer
   * @object: the #GESTimelineObject that was added.
   *
   * Will be emitted after the object was added to the layer.
   */
  ges_timeline_layer_signals[OBJECT_ADDED] =
      g_signal_new ("object-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass, object_added),
      NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_OBJECT);

  /**
   * GESTimelineLayer::object-removed
   * @layer: the #GESTimelineLayer
   * @object: the #GESTimelineObject that was removed
   *
   * Will be emitted after the object was removed from the layer.
   */
  ges_timeline_layer_signals[OBJECT_REMOVED] =
      g_signal_new ("object-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, G_STRUCT_OFFSET (GESTimelineLayerClass,
          object_removed), NULL, NULL, ges_marshal_VOID__OBJECT, G_TYPE_NONE, 1,
      GES_TYPE_TIMELINE_OBJECT);
}

static void
ges_timeline_layer_init (GESTimelineLayer * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_LAYER, GESTimelineLayerPrivate);

  self->priv->priority = 0;
  self->priv->auto_transition = FALSE;
  self->min_gnl_priority = 0;
  self->max_gnl_priority = LAYER_HEIGHT;
}

/* Private methods and utils */
static gint
objects_start_compare (GESTimelineObject * a, GESTimelineObject * b)
{
  if (a->start == b->start) {
    if (a->priority < b->priority)
      return -1;
    if (a->priority > b->priority)
      return 1;
    return 0;
  }
  if (a->start < b->start)
    return -1;
  if (a->start > b->start)
    return 1;
  return 0;
}

static GList *
track_get_by_layer (GESTimelineLayer * layer, GESTrack * track)
{
  GESTrackObject *tckobj;
  guint32 layer_prio = layer->priv->priority;

  GList *tck_objects_list = NULL, *tmp = NULL, *return_list = NULL;

  tck_objects_list = ges_track_get_objects (track);
  for (tmp = tck_objects_list; tmp; tmp = tmp->next) {
    tckobj = GES_TRACK_OBJECT (tmp->data);

    if (tckobj->priority / LAYER_HEIGHT == layer_prio) {
      /*  We steal the reference from tck_objects_list */
      return_list = g_list_append (return_list, tmp->data);
    } else
      g_object_unref (tmp->data);
  }

  return return_list;
}

/* Compare:
 * @compared: The #GList of #GESTrackObjects that we compare with @track_object
 * @track_object: The #GESTrackObject that serves as a reference
 * @ahead: %TRUE if we are comparing frontward %FALSE if we are comparing
 * backward*/
static void
compare (GList * compared, GESTrackObject * track_object, gboolean ahead)
{
  GList *tmp;
  gint64 start, duration, compared_start, compared_duration, end, compared_end,
      tr_start, tr_duration;
  GESTimelineStandardTransition *trans = NULL;
  GESTrack *track;
  GESTimelineLayer *layer;
  GESTimelineObject *object, *compared_object, *first_object, *second_object;
  gint priority;

  g_return_if_fail (compared);

  GST_DEBUG ("Recalculating transitions");

  object = ges_track_object_get_timeline_object (track_object);

  if (!object) {
    GST_WARNING ("Trackobject not in a timeline object: "
        "Can not calulate transitions");

    return;
  }

  compared_object = ges_track_object_get_timeline_object (compared->data);
  layer = ges_timeline_object_get_layer (object);

  start = ges_track_object_get_start (track_object);
  duration = ges_track_object_get_duration (track_object);
  compared_start = ges_track_object_get_start (compared->data);
  compared_duration = ges_track_object_get_duration (compared->data);
  end = start + duration;
  compared_end = compared_start + compared_duration;

  if (ahead) {
    /* Make sure we remove the last transition we created it is not needed
     * FIXME make it a smarter way */
    if (compared->prev && GES_IS_TRACK_TRANSITION (compared->prev->data)) {
      trans = GES_TIMELINE_STANDARD_TRANSITION
          (ges_track_object_get_timeline_object (compared->prev->data));
      g_object_get (compared->prev->data, "start", &tr_start, "duration",
          &tr_duration, NULL);
      if (tr_start >= compared_start && tr_start + tr_duration <= compared_end)
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));
      trans = NULL;
    }

    for (tmp = compared->next; tmp; tmp = tmp->next) {
      /* If we have a transitionmnmnm we recaluculuculate its values */
      if (GES_IS_TRACK_TRANSITION (tmp->data)) {
        g_object_get (tmp->data, "start", &tr_start, "duration", &tr_duration,
            NULL);

        if (tr_start + tr_duration == compared_start + compared_duration) {
          GESTimelineObject *tlobj;
          tlobj = ges_track_object_get_timeline_object (tmp->data);

          trans = GES_TIMELINE_STANDARD_TRANSITION (tlobj);
          break;
        }
      }
    }

    if (compared_end <= start) {
      if (trans) {
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));
        g_object_get (compared_object, "priority", &priority, NULL);
        g_object_set (object, "priority", priority, NULL);
      }

      goto clean;
    } else if (start > compared_start && end < compared_end) {
      if (trans) {
        /* Transition not needed anymore */
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));
      }
      goto clean;
    } else if (start <= compared_start) {
      if (trans) {
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));
      }

      goto clean;
    }

  } else {
    if (compared->next && GES_IS_TRACK_TRANSITION (compared->next->data)) {
      trans = GES_TIMELINE_STANDARD_TRANSITION
          (ges_track_object_get_timeline_object (compared->next->data));
      g_object_get (compared->prev->data, "start", &tr_start, "duration",
          &tr_duration, NULL);
      if (tr_start >= compared_start && tr_start + tr_duration <= compared_end)
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));
      trans = NULL;
    }
    for (tmp = compared->prev; tmp; tmp = tmp->prev) {
      if GES_IS_TRACK_TRANSITION
        (tmp->data) {
        g_object_get (tmp->data, "start", &tr_start, "duration", &tr_duration,
            NULL);
        if (tr_start == compared_start) {
          trans = GES_TIMELINE_STANDARD_TRANSITION
              (ges_track_object_get_timeline_object (tmp->data));
          break;
        }
        }
    }

    if (start + duration <= compared_start) {
      if (trans) {
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));
        g_object_get (object, "priority", &priority, NULL);
        g_object_set (compared_object, "priority", priority, NULL);
      }
      goto clean;

    } else if (start > compared_start) {
      if (trans)
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));

      goto clean;
    } else if (start < compared_start && end > compared_end) {
      if (trans) {
        ges_timeline_layer_remove_object (layer, GES_TIMELINE_OBJECT (trans));
      }

      goto clean;
    }
  }

  if (!trans) {
    gint height;

    trans =
        ges_timeline_standard_transition_new_for_nick ((gchar *) "crossfade");
    track = ges_track_object_get_track (track_object);

    ges_timeline_object_set_supported_formats (GES_TIMELINE_OBJECT (trans),
        track->type);

    ges_timeline_layer_add_object (layer, GES_TIMELINE_OBJECT (trans));

    if (ahead) {
      first_object = ges_track_object_get_timeline_object (compared->data);
      second_object = object;
    } else {
      second_object = ges_track_object_get_timeline_object (compared->data);
      first_object = object;
    }

    g_object_get (first_object, "priority", &priority, "height", &height, NULL);
    g_object_set (second_object, "priority", priority + height, NULL);
    g_signal_connect (first_object, "notify::height",
        (GCallback) timeline_object_height_changed_cb, second_object);
  }

  if (ahead) {
    g_object_set (trans, "start", start, "duration",
        compared_duration + compared_start - start, NULL);
  } else {
    g_object_set (trans, "start", compared_start, "duration",
        start + duration - compared_start, NULL);
  }

clean:
  g_object_unref (layer);
}

static void
calculate_next_transition_with_list (GESTrackObject * track_object,
    GList * tckobjs_in_layer, GESTimelineLayer * layer)
{
  GList *compared;

  if (!(compared = g_list_find (tckobjs_in_layer, track_object)))
    return;

  if (compared == NULL)
    /* This is the last TrackObject of the Track */
    return;

  do {
    compared = compared->next;
    if (compared == NULL)
      return;
  } while (!GES_IS_TRACK_SOURCE (compared->data));

  compare (compared, track_object, FALSE);
}


static void
calculate_next_transition (GESTrackObject * track_object,
    GESTimelineLayer * layer)
{
  GESTrack *track;
  GList *tckobjs_in_layer;

  if ((track = ges_track_object_get_track (track_object))) {
    tckobjs_in_layer = track_get_by_layer (layer, track);
    calculate_next_transition_with_list (track_object, tckobjs_in_layer, layer);

    g_list_foreach (tckobjs_in_layer, (GFunc) g_object_unref, NULL);
    g_list_free (tckobjs_in_layer);
  }
}

static void
calculate_transitions (GESTrackObject * track_object)
{
  GList *tckobjs_in_layer, *compared;
  GESTimelineLayer *layer;
  GESTimelineObject *tlobj;

  GESTrack *track = ges_track_object_get_track (track_object);

  if (track == NULL)
    return;

  tlobj = ges_track_object_get_timeline_object (track_object);
  layer = ges_timeline_object_get_layer (tlobj);
  tckobjs_in_layer = track_get_by_layer (layer, track);
  if (!(compared = g_list_find (tckobjs_in_layer, track_object)))
    return;
  do {
    compared = compared->prev;

    if (compared == NULL) {
      /* Nothing before, let's check after */
      calculate_next_transition_with_list (track_object, tckobjs_in_layer,
          layer);
      goto done;

    }
  } while (!GES_IS_TRACK_SOURCE (compared->data));

  compare (compared, track_object, TRUE);

  calculate_next_transition_with_list (track_object, tckobjs_in_layer, layer);

done:
  g_list_foreach (tckobjs_in_layer, (GFunc) g_object_unref, NULL);
  g_list_free (tckobjs_in_layer);
}


static void
look_for_transition (GESTrackObject * track_object, GESTimelineLayer * layer)
{
  GESTrack *track;
  GList *track_objects, *tmp, *cur;

  track = ges_track_object_get_track (track_object);
  track_objects = ges_track_get_objects (track);

  cur = g_list_find (track_objects, track_object);

  for (tmp = cur->next; tmp; tmp = tmp->next) {
    if (GES_IS_TRACK_SOURCE (tmp->data)) {
      break;
    }
    if (GES_IS_TRACK_AUDIO_TRANSITION (tmp->data)
        || GES_IS_TRACK_VIDEO_TRANSITION (tmp->data)) {
      ges_timeline_layer_remove_object (layer,
          ges_track_object_get_timeline_object (tmp->data));
    }
  }

  for (tmp = cur->prev; tmp; tmp = tmp->prev) {
    if (GES_IS_TRACK_SOURCE (tmp->data)) {
      break;
    }
    if (GES_IS_TRACK_AUDIO_TRANSITION (tmp->data)
        || GES_IS_TRACK_VIDEO_TRANSITION (tmp->data)) {
      ges_timeline_layer_remove_object (layer,
          ges_track_object_get_timeline_object (tmp->data));
    }
  }
  g_list_foreach (track_objects, (GFunc) g_object_unref, NULL);
  g_list_free (track_objects);
}

/**
 * ges_timeline_layer_resync_priorities:
 * @layer: a #GESTimelineLayer
 *
 * Resyncs the priorities of the objects controlled by @layer.
 * This method
 */
static gboolean
ges_timeline_layer_resync_priorities (GESTimelineLayer * layer)
{
  GList *tmp;
  GESTimelineObject *obj;

  GST_DEBUG ("Resync priorities of %p", layer);

  /* TODO : Inhibit composition updates while doing this.
   * Ideally we want to do it from an even higher level, but here will
   * do in the meantime. */

  for (tmp = layer->priv->objects_start; tmp; tmp = tmp->next) {
    obj = GES_TIMELINE_OBJECT (tmp->data);
    ges_timeline_object_set_priority (obj, GES_TIMELINE_OBJECT_PRIORITY (obj));
  }

  return TRUE;
}

/* Callbacks */

static void
track_object_duration_cb (GESTrackObject * track_object,
    GParamSpec * arg G_GNUC_UNUSED)
{
  GESTimelineLayer *layer;
  GESTimelineObject *tlobj;

  tlobj = ges_track_object_get_timeline_object (track_object);
  layer = ges_timeline_object_get_layer (tlobj);
  if (G_LIKELY (GES_IS_TRACK_SOURCE (track_object)))
    calculate_next_transition (track_object, layer);
}

static void
track_object_removed_cb (GESTrack * track, GESTrackObject * track_object)
{
  GList *track_objects, *tmp, *cur;
  GESTimelineLayer *layer;

  track_objects = ges_track_get_objects (track);
  cur = g_list_find (track_objects, track_object);
  for (tmp = cur->next; tmp; tmp = tmp->next) {
    if (GES_IS_TRACK_SOURCE (tmp->data)) {
      break;
    }
    if (GES_IS_TRACK_AUDIO_TRANSITION (tmp->data)
        || GES_IS_TRACK_VIDEO_TRANSITION (tmp->data)) {
      layer =
          ges_timeline_object_get_layer (ges_track_object_get_timeline_object
          (tmp->data));
      if (ges_timeline_layer_get_auto_transition (layer)) {
        ges_track_enable_update (track, FALSE);
        ges_timeline_layer_remove_object (layer,
            ges_track_object_get_timeline_object (tmp->data));
        ges_track_enable_update (track, TRUE);
      }
      g_object_unref (layer);
    }
  }

  for (tmp = cur->prev; tmp; tmp = tmp->prev) {
    if (GES_IS_TRACK_SOURCE (tmp->data)) {
      break;
    }
    if (GES_IS_TRACK_AUDIO_TRANSITION (tmp->data)
        || GES_IS_TRACK_VIDEO_TRANSITION (tmp->data)) {
      layer =
          ges_timeline_object_get_layer (ges_track_object_get_timeline_object
          (tmp->data));
      if (ges_timeline_layer_get_auto_transition (layer)) {
        ges_track_enable_update (track, FALSE);
        ges_timeline_layer_remove_object (layer,
            ges_track_object_get_timeline_object (tmp->data));
        ges_track_enable_update (track, TRUE);
      }
      g_object_unref (layer);
    }
  }
  g_object_unref (track_object);
}

static void
track_object_changed_cb (GESTrackObject * track_object,
    GParamSpec * arg G_GNUC_UNUSED)
{
  if (G_LIKELY (GES_IS_TRACK_SOURCE (track_object)))
    calculate_transitions (track_object);
}

static void
track_object_added_cb (GESTrack * track, GESTrackObject * track_object,
    GESTimelineLayer * layer)
{
  if (GES_IS_TRACK_SOURCE (track_object)) {
    g_signal_connect (G_OBJECT (track_object), "notify::start",
        G_CALLBACK (track_object_changed_cb), NULL);
    g_signal_connect (G_OBJECT (track_object), "notify::duration",
        G_CALLBACK (track_object_duration_cb), NULL);
    calculate_transitions (track_object);
  }

}

static void
track_removed_cb (GESTrack * track, GESTrackObject * track_object,
    GESTimelineLayer * layer)
{
  g_signal_handlers_disconnect_by_func (track, track_object_added_cb, layer);
  g_signal_handlers_disconnect_by_func (track, track_object_removed_cb, NULL);
}

static void
track_added_cb (GESTrack * track, GESTrackObject * track_object,
    GESTimelineLayer * layer)
{
  g_signal_connect (track, "track-object-removed",
      (GCallback) track_object_removed_cb, NULL);
  g_signal_connect (track, "track-object-added",
      (GCallback) track_object_added_cb, NULL);
}

static void
timeline_object_height_changed_cb (GESTimelineObject * obj,
    GESTrackEffect * tr_eff, GESTimelineObject * second_obj)
{
  gint priority, height;
  g_object_get (obj, "height", &height, "priority", &priority, NULL);
  g_object_set (second_obj, "priority", priority + height, NULL);
}

static void
start_calculating_transitions (GESTimelineLayer * layer)
{
  GList *tmp, *tracks = ges_timeline_get_tracks (layer->timeline);

  g_signal_connect (layer->timeline, "track-added", G_CALLBACK (track_added_cb),
      layer);
  g_signal_connect (layer->timeline, "track-removed",
      G_CALLBACK (track_removed_cb), layer);

  for (tmp = tracks; tmp; tmp = tmp->next) {
    g_signal_connect (G_OBJECT (tmp->data), "track-object-added",
        G_CALLBACK (track_object_added_cb), layer);
    g_signal_connect (G_OBJECT (tmp->data), "track-object-removed",
        G_CALLBACK (track_object_removed_cb), NULL);
  }

  g_list_free_full (tracks, g_object_unref);

  /* FIXME calculate all the transitions at that time */
}

/* Public methods */
/**
 * ges_timeline_layer_remove_object:
 * @layer: a #GESTimelineLayer
 * @object: the #GESTimelineObject to remove
 *
 * Removes the given @object from the @layer and unparents it.
 * Unparenting it means the reference owned by @layer on the @object will be
 * removed. If you wish to use the @object after this function, make sure you
 * call g_object_ref() before removing it from the @layer.
 *
 * Returns: TRUE if the object could be removed, FALSE if the layer does
 * not want to remove the object.
 */
gboolean
ges_timeline_layer_remove_object (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GESTimelineLayer *tl_obj_layer;
  GList *trackobjects, *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);

  GST_DEBUG ("layer:%p, object:%p", layer, object);

  tl_obj_layer = ges_timeline_object_get_layer (object);
  if (G_UNLIKELY (tl_obj_layer != layer)) {
    GST_WARNING ("TimelineObject doesn't belong to this layer");

    if (tl_obj_layer != NULL)
      g_object_unref (tl_obj_layer);

    return FALSE;
  }
  g_object_unref (tl_obj_layer);

  if (layer->priv->auto_transition && GES_IS_TIMELINE_SOURCE (object)) {
    trackobjects = ges_timeline_object_get_track_objects (object);

    for (tmp = trackobjects; tmp; tmp = tmp->next) {
      look_for_transition (tmp->data, layer);
    }

    g_list_foreach (trackobjects, (GFunc) g_object_unref, NULL);
    g_list_free (trackobjects);
  }

  /* emit 'object-removed' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_REMOVED], 0, object);

  /* inform the object it's no longer in a layer */
  ges_timeline_object_set_layer (object, NULL);

  /* Remove it from our list of controlled objects */
  layer->priv->objects_start =
      g_list_remove (layer->priv->objects_start, object);

  /* Remove our reference to the object */
  g_object_unref (object);

  return TRUE;
}

/**
 * ges_timeline_layer_set_priority:
 * @layer: a #GESTimelineLayer
 * @priority: the priority to set
 *
 * Sets the layer to the given @priority. See the documentation of the
 * priority property for more information.
 */
void
ges_timeline_layer_set_priority (GESTimelineLayer * layer, guint priority)
{
  g_return_if_fail (GES_IS_TIMELINE_LAYER (layer));

  GST_DEBUG ("layer:%p, priority:%d", layer, priority);

  if (priority != layer->priv->priority) {
    layer->priv->priority = priority;
    layer->min_gnl_priority = (priority * LAYER_HEIGHT);
    layer->max_gnl_priority = ((priority + 1) * LAYER_HEIGHT) - 1;

    ges_timeline_layer_resync_priorities (layer);
  }
}

/**
 * ges_timeline_layer_get_auto_transition:
 * @layer: a #GESTimelineLayer
 *
 * Gets whether transitions are automatically added when objects
 * overlap or not.
 *
 * Returns: %TRUE if transitions are automatically added, else %FALSE.
 */
gboolean
ges_timeline_layer_get_auto_transition (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), 0);

  return layer->priv->auto_transition;
}

/**
 * ges_timeline_layer_set_auto_transition:
 * @layer: a #GESTimelineLayer
 * @auto_transition: whether the auto_transition is active
 *
 * Sets the layer to the given @auto_transition. See the documentation of the
 * property auto_transition for more information.
 */
void
ges_timeline_layer_set_auto_transition (GESTimelineLayer * layer,
    gboolean auto_transition)
{

  g_return_if_fail (GES_IS_TIMELINE_LAYER (layer));

  if (auto_transition && layer->timeline)
    start_calculating_transitions (layer);

  layer->priv->auto_transition = auto_transition;
}

/**
 * ges_timeline_layer_get_priority:
 * @layer: a #GESTimelineLayer
 *
 * Get the priority of @layer within the timeline.
 *
 * Returns: The priority of the @layer within the timeline.
 */
guint
ges_timeline_layer_get_priority (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), 0);

  return layer->priv->priority;
}

/**
 * ges_timeline_layer_get_objects:
 * @layer: a #GESTimelineLayer
 *
 * Get the timeline objects this layer contains.
 *
 * Returns: (transfer full) (element-type GESTimelineObject): a #GList of
 * timeline objects. The user is responsible for
 * unreffing the contained objects and freeing the list.
 */

GList *
ges_timeline_layer_get_objects (GESTimelineLayer * layer)
{
  GList *ret = NULL;
  GList *tmp;
  GESTimelineLayerClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), NULL);

  klass = GES_TIMELINE_LAYER_GET_CLASS (layer);

  if (klass->get_objects) {
    return klass->get_objects (layer);
  }

  for (tmp = layer->priv->objects_start; tmp; tmp = tmp->next) {
    ret = g_list_prepend (ret, tmp->data);
    g_object_ref (tmp->data);
  }

  ret = g_list_reverse (ret);
  return ret;
}

/**
 * ges_timeline_layer_is_empty:
 * @layer: The #GESTimelineLayer to check
 *
 * Convenience method to check if @layer is empty (doesn't contain any object),
 * or not.
 *
 * Returns: %TRUE if @layer is empty, %FALSE if it already contains at least
 * one #GESTimelineObject
 */
gboolean
ges_timeline_layer_is_empty (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);

  return (layer->priv->objects_start == NULL);
}

/**
 * ges_timeline_layer_add_object:
 * @layer: a #GESTimelineLayer
 * @object: (transfer full): the #GESTimelineObject to add.
 *
 * Adds the given object to the layer. Sets the object's parent, and thus
 * takes ownership of the object.
 *
 * An object can only be added to one layer.
 *
 * Calling this method will construct and properly set all the media related
 * elements on @object. If you need to know when those objects (actually #GESTrackObject)
 * are constructed, you should connect to the object::track-object-added signal which
 * is emited right after those elements are ready to be used.
 *
 * Returns: TRUE if the object was properly added to the layer, or FALSE
 * if the @layer refuses to add the object.
 */
gboolean
ges_timeline_layer_add_object (GESTimelineLayer * layer,
    GESTimelineObject * object)
{
  GESTimelineLayer *tl_obj_layer;
  guint32 maxprio, minprio, prio;

  GST_DEBUG ("layer:%p, object:%p", layer, object);

  tl_obj_layer = ges_timeline_object_get_layer (object);

  if (G_UNLIKELY (tl_obj_layer)) {
    GST_WARNING ("TimelineObject %p already belongs to another layer", object);
    g_object_unref (tl_obj_layer);
    return FALSE;
  }

  g_object_ref_sink (object);

  /* Take a reference to the object and store it stored by start/priority */
  layer->priv->objects_start =
      g_list_insert_sorted (layer->priv->objects_start, object,
      (GCompareFunc) objects_start_compare);

  /* Inform the object it's now in this layer */
  ges_timeline_object_set_layer (object, layer);

  GST_DEBUG ("current object priority : %d, layer min/max : %d/%d",
      GES_TIMELINE_OBJECT_PRIORITY (object),
      layer->min_gnl_priority, layer->max_gnl_priority);

  /* Set the priority. */
  maxprio = layer->max_gnl_priority;
  minprio = layer->min_gnl_priority;
  prio = GES_TIMELINE_OBJECT_PRIORITY (object);
  if (minprio + prio > (maxprio)) {
    GST_WARNING ("%p is out of the layer %p space, setting its priority to "
        "setting its priority %d to failthe maximum priority of the layer %d",
        object, layer, prio, maxprio - minprio);
    ges_timeline_object_set_priority (object, LAYER_HEIGHT - 1);
  }
  /* If the object has an acceptable priority, we just let it with its current
   * priority */

  ges_timeline_layer_resync_priorities (layer);

  /* emit 'object-added' */
  g_signal_emit (layer, ges_timeline_layer_signals[OBJECT_ADDED], 0, object);

  return TRUE;
}

/**
 * ges_timeline_layer_new:
 *
 * Creates a new #GESTimelineLayer.
 *
 * Returns: A new #GESTimelineLayer
 */
GESTimelineLayer *
ges_timeline_layer_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_LAYER, NULL);
}

/**
 * ges_timeline_layer_get_timeline:
 * @layer: The #GESTimelineLayer to get the parent #GESTimeline from
 *
 * Get the #GESTimeline in which #GESTimelineLayer currently is.
 *
 * Returns: (transfer none):  the #GESTimeline in which #GESTimelineLayer
 * currently is or %NULL if not in any timeline yet.
 */
GESTimeline *
ges_timeline_layer_get_timeline (GESTimelineLayer * layer)
{
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), NULL);

  return layer->timeline;
}

void
ges_timeline_layer_set_timeline (GESTimelineLayer * layer,
    GESTimeline * timeline)
{
  GST_DEBUG ("layer:%p, timeline:%p", layer, timeline);

  if (layer->priv->auto_transition == TRUE) {
    if (layer->timeline != NULL) {
      g_signal_handlers_disconnect_by_func (layer->timeline, track_added_cb,
          layer);
      g_signal_handlers_disconnect_by_func (layer->timeline, track_removed_cb,
          layer);
    }

    layer->timeline = timeline;
    if (timeline != NULL)
      start_calculating_transitions (layer);

  } else
    layer->timeline = timeline;
}
