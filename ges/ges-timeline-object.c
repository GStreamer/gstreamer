/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <bilboed@bilboed.com>
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

#include "ges-timeline-object.h"
#include "ges.h"
#include "ges-internal.h"

/**
 * GESTimelineObject
 *
 * Responsible for creating the TrackObject(s) for given TimelineTrack(s)
 *
 * Keeps a reference to the TrackObject(s) it created and sets/updates their properties.
 */


G_DEFINE_TYPE (GESTimelineObject, ges_timeline_object, G_TYPE_OBJECT);

enum
{
  PROP_0,
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_PRIORITY,
};

static void
ges_timeline_object_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineObject *tobj = GES_TIMELINE_OBJECT (object);

  switch (property_id) {
    case PROP_START:
      g_value_set_uint64 (value, tobj->start);
      break;
    case PROP_INPOINT:
      g_value_set_uint64 (value, tobj->inpoint);
      break;
    case PROP_DURATION:
      g_value_set_uint64 (value, tobj->duration);
      break;
    case PROP_PRIORITY:
      g_value_set_uint (value, tobj->priority);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_object_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineObject *tobj = GES_TIMELINE_OBJECT (object);

  switch (property_id) {
    case PROP_START:
      ges_timeline_object_set_start (tobj, g_value_get_uint64 (value));
      break;
    case PROP_INPOINT:
      ges_timeline_object_set_inpoint (tobj, g_value_get_uint64 (value));
      break;
    case PROP_DURATION:
      ges_timeline_object_set_duration (tobj, g_value_get_uint64 (value));
      break;
    case PROP_PRIORITY:
      ges_timeline_object_set_priority (tobj, g_value_get_uint (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_object_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_object_parent_class)->dispose (object);
}

static void
ges_timeline_object_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_timeline_object_parent_class)->finalize (object);
}

static void
ges_timeline_object_class_init (GESTimelineObjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = ges_timeline_object_get_property;
  object_class->set_property = ges_timeline_object_set_property;
  object_class->dispose = ges_timeline_object_dispose;
  object_class->finalize = ges_timeline_object_finalize;

  g_object_class_install_property (object_class, PROP_START,
      g_param_spec_uint64 ("start", "Start",
          "The position in the container", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_INPOINT,
      g_param_spec_uint64 ("inpoint", "In-point", "The in-point", 0,
          G_MAXUINT64, 0, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_DURATION,
      g_param_spec_uint64 ("duration", "Duration", "The duration to use",
          0, G_MAXUINT64, GST_SECOND, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_uint ("priority", "Priority",
          "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE));
}

static void
ges_timeline_object_init (GESTimelineObject * self)
{
}

GESTimelineObject *
ges_timeline_object_new (void)
{
  return g_object_new (GES_TYPE_TIMELINE_OBJECT, NULL);
}

/**
 * ges_timeline_object_create_track_object:
 * @object: The origin #GESTimelineObject
 * @track: The #GESTrack to create a #GESTrackObject for.
 *
 * Creates a #GESTrackObject for the provided @track.
 *
 * Returns: A #GESTrackObject. Returns NULL if the #GESTrackObject could not
 * be created.
 */

GESTrackObject *
ges_timeline_object_create_track_object (GESTimelineObject * object,
    GESTrack * track)
{
  GESTimelineObjectClass *class;
  GESTrackObject *res;

  class = GES_TIMELINE_OBJECT_GET_CLASS (object);

  if (G_UNLIKELY (class->create_track_object == NULL)) {
    GST_ERROR ("No 'create_track_object' implementation available");
    return NULL;
  }

  res = class->create_track_object (object, track);

  if (res) {
    GST_DEBUG
        ("Got a TrackObject : %p , setting the timeline object as its creator");
    ges_track_object_set_timeline_object (res, object);

    GST_DEBUG ("Adding TrackObject to the list of controlled track objects");
    object->trackobjects = g_list_append (object->trackobjects, res);

  }

  GST_DEBUG ("Returning res:%p", res);

  return res;
}

gboolean
ges_timeline_object_release_track_object (GESTimelineObject * object,
    GESTrackObject * trobj)
{
  GST_DEBUG ("object:%p, trackobject:%p", object, trobj);

  if (!(g_list_find (object->trackobjects, trobj))) {
    GST_WARNING ("TrackObject isn't controlled by this object");
    return FALSE;
  }

  /* FIXME : Do we need to tell the subclasses ? If so, add a new virtual-method */

  object->trackobjects = g_list_remove (object->trackobjects, trobj);

  ges_track_object_set_timeline_object (trobj, NULL);

  return TRUE;
}

void
ges_timeline_object_set_layer (GESTimelineObject * object,
    GESTimelineLayer * layer)
{
  GST_DEBUG ("object:%p, layer:%p", object, layer);

  object->layer = layer;
}

gboolean
ges_timeline_object_fill_track_object (GESTimelineObject * object,
    GESTrackObject * trackobj, GstElement * gnlobj)
{
  GESTimelineObjectClass *class;
  gboolean res;

  GST_DEBUG ("object:%p, trackobject:%p, gnlobject:%p",
      object, trackobj, gnlobj);

  class = GES_TIMELINE_OBJECT_GET_CLASS (object);

  if (G_UNLIKELY (class->fill_track_object == NULL)) {
    GST_WARNING ("No 'fill_track_object' implementation available");
    return FALSE;
  }

  res = class->fill_track_object (object, trackobj, gnlobj);

  if (G_LIKELY (res)) {
    GST_DEBUG ("Setting properties");

    ges_track_object_set_start_internal (trackobj, object->start);
    ges_track_object_set_inpoint_internal (trackobj, object->inpoint);
    ges_track_object_set_duration_internal (trackobj, object->duration);
    ges_track_object_set_priority_internal (trackobj, object->priority);

  }

  GST_DEBUG ("Returning res:%d", res);

  return res;
}

gboolean
ges_timeline_object_fill_track_object_func (GESTimelineObject * object,
    GESTrackObject * trackobj, GstElement * gnlobj)
{
  GST_WARNING ("No 'fill_track_object' implementation !");

  return FALSE;
}

void
ges_timeline_object_set_start (GESTimelineObject * object, guint64 start)
{
  GST_DEBUG ("object:%p, start:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (start));

  if (G_LIKELY (object->trackobjects)) {
    GList *tmp;

    for (tmp = object->trackobjects; tmp; tmp = g_list_next (tmp))
      /* call set_start_internal on each trackobject */
      ges_track_object_set_start_internal (GES_TRACK_OBJECT (tmp->data), start);

  }

  object->start = start;

}

void
ges_timeline_object_set_inpoint (GESTimelineObject * object, guint64 inpoint)
{
  GST_DEBUG ("object:%p, inpoint:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (inpoint));

  if (G_LIKELY (object->trackobjects)) {
    GList *tmp;

    for (tmp = object->trackobjects; tmp; tmp = g_list_next (tmp))
      /* call set_inpoint_internal on each trackobject */
      ges_track_object_set_inpoint_internal (GES_TRACK_OBJECT (tmp->data),
          inpoint);

  }

  object->inpoint = inpoint;


}

void
ges_timeline_object_set_duration (GESTimelineObject * object, guint64 duration)
{
  GST_DEBUG ("object:%p, duration:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (duration));

  if (G_LIKELY (object->trackobjects)) {
    GList *tmp;

    for (tmp = object->trackobjects; tmp; tmp = g_list_next (tmp))
      /* call set_duration_internal on each trackobject */
      ges_track_object_set_duration_internal (GES_TRACK_OBJECT (tmp->data),
          duration);

  }

  object->duration = duration;


}

void
ges_timeline_object_set_priority (GESTimelineObject * object, guint priority)
{
  GST_DEBUG ("object:%p, priority:%d", object, priority);

  if (G_LIKELY (object->trackobjects)) {
    GList *tmp;

    for (tmp = object->trackobjects; tmp; tmp = g_list_next (tmp))
      /* call set_priority_internal on each trackobject */
      ges_track_object_set_priority_internal (GES_TRACK_OBJECT (tmp->data),
          priority);

  }

  object->priority = priority;

}
