/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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
 * SECTION:ges-timeline-object
 * @short_description: Base Class for objects in a #GESTimelineLayer
 *
 * A #GESTimelineObject is a 'natural' object which controls one or more
 * #GESTrackObject(s) in one or more #GESTrack(s).
 *
 * Keeps a reference to the #GESTrackObject(s) it created and
 * sets/updates their properties.
 */

#include "ges-timeline-object.h"
#include "ges.h"
#include "ges-internal.h"

gboolean
ges_timeline_object_fill_track_object_func (GESTimelineObject * object,
    GESTrackObject * trackobj, GstElement * gnlobj);

gboolean
ges_timeline_object_create_track_objects_func (GESTimelineObject
    * object, GESTrack * track);

static void
track_object_priority_offset_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * obj);

G_DEFINE_ABSTRACT_TYPE (GESTimelineObject, ges_timeline_object, G_TYPE_OBJECT);

struct _GESTimelineObjectPrivate
{
  /*< public > */
  GESTimelineLayer *layer;

  /*< private > */
  /* A list of TrackObject controlled by this TimelineObject */
  GList *trackobjects;

};

enum
{
  PROP_0,
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_PRIORITY,
  PROP_HEIGHT,
  PROP_LAYER,
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
    case PROP_HEIGHT:
      g_value_set_uint (value, tobj->height);
      break;
    case PROP_LAYER:
      g_value_set_object (value, tobj->priv->layer);
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
ges_timeline_object_class_init (GESTimelineObjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineObjectPrivate));

  object_class->get_property = ges_timeline_object_get_property;
  object_class->set_property = ges_timeline_object_set_property;
  klass->create_track_objects = ges_timeline_object_create_track_objects_func;

  /**
   * GESTimelineObject:start
   *
   * The position of the object in the #GESTimelineLayer (in nanoseconds).
   */
  g_object_class_install_property (object_class, PROP_START,
      g_param_spec_uint64 ("start", "Start",
          "The position in the container", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE));

  /**
   * GESTimelineObject:in-point
   *
   * The in-point at which this #GESTimelineObject will start outputting data
   * from its contents (in nanoseconds).
   *
   * Ex : an in-point of 5 seconds means that the first outputted buffer will
   * be the one located 5 seconds in the controlled resource.
   */
  g_object_class_install_property (object_class, PROP_INPOINT,
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
          G_MAXUINT64, 0, G_PARAM_READWRITE));

  /**
   * GESTimelineObject:duration
   *
   * The duration (in nanoseconds) which will be used in the container #GESTrack
   * starting from 'in-point'.
   */
  g_object_class_install_property (object_class, PROP_DURATION,
      g_param_spec_uint64 ("duration", "Duration", "The duration to use",
          0, G_MAXUINT64, GST_CLOCK_TIME_NONE, G_PARAM_READWRITE));

  /**
   * GESTimelineObject:priority
   *
   * The layer priority of the timeline object.
   */

  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_uint ("priority", "Priority",
          "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

  /**
   * GESTimelineObject:height
   *
   * The span of layer priorities which this object occupies.
   */

  g_object_class_install_property (object_class, PROP_HEIGHT,
      g_param_spec_uint ("height", "Height",
          "The span of priorities this object occupies", 0, G_MAXUINT, 1,
          G_PARAM_READABLE));

  /* GESSimpleTimelineLayer:layer
   *
   * The GESTimelineLayer where this object is being used.
   */

  g_object_class_install_property (object_class, PROP_LAYER,
      g_param_spec_object ("layer", "Layer",
          "The GESTimelineLayer where this object is being used.",
          GES_TYPE_TIMELINE_LAYER, G_PARAM_READABLE));

  klass->need_fill_track = TRUE;
}

static void
ges_timeline_object_init (GESTimelineObject * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_OBJECT, GESTimelineObjectPrivate);
  self->duration = GST_SECOND;
  self->height = 1;
  self->priv->trackobjects = NULL;
  self->priv->layer = NULL;
}

/**
 * ges_timeline_object_create_track_object:
 * @object: The origin #GESTimelineObject
 * @track: The #GESTrack to create a #GESTrackObject for.
 *
 * Creates a #GESTrackObject for the provided @track. The timeline object
 * keep a reference to the newly created trackobject, you therefore need to
 * call @ges_timeline_object_release_track_object when you are done with it.
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
  ges_timeline_object_add_track_object (object, res);
  return res;

}

/**
 * ges_timeline_object_create_track_objects:
 * @object: The origin #GESTimelineObject
 * @track: The #GESTrack to create each #GESTrackObject for.
 *
 * Creates all #GESTrackObjects supported by this object and adds them to the
 * provided track. The track is responsible for calling
 * #ges_timeline_release_track_object on these objects when it is finished
 * with them.
 *
 * Returns: %TRUE if each track object was created successfully, or %FALSE if an
 * error occured.
 */

gboolean
ges_timeline_object_create_track_objects (GESTimelineObject * object,
    GESTrack * track)
{
  GESTimelineObjectClass *klass;

  klass = GES_TIMELINE_OBJECT_GET_CLASS (object);

  if (!(klass->create_track_objects)) {
    GST_WARNING ("no GESTimelineObject::create_track_objects implentation");
    return FALSE;
  }

  return klass->create_track_objects (object, track);
}

/*
 * default implementation of GESTimelineObjectClass::create_track_objects
 */
gboolean
ges_timeline_object_create_track_objects_func (GESTimelineObject * object,
    GESTrack * track)
{
  GESTrackObject *result;

  result = ges_timeline_object_create_track_object (object, track);
  if (!result) {
    GST_WARNING ("couldn't create track object");
    return FALSE;
  }
  return ges_track_add_object (track, result);
}

/**
 * ges_timeline_object_add_track_object:
 * @object: a #GESTimelineObject
 * @trobj: the GESTrackObject
 *
 * Add a track object to the timeline object. Should only be called by
 * subclasses implementing the create_track_objects (plural) vmethod.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */

gboolean
ges_timeline_object_add_track_object (GESTimelineObject * object, GESTrackObject
    * trobj)
{
  GST_LOG ("Got a TrackObject : %p , setting the timeline object as its"
      "creator", trobj);

  if (!trobj)
    return FALSE;

  ges_track_object_set_timeline_object (trobj, object);

  GST_DEBUG ("Adding TrackObject to the list of controlled track objects");
  /* We steal the initial reference */
  object->priv->trackobjects =
      g_list_append (object->priv->trackobjects, trobj);

  GST_DEBUG ("Setting properties on newly created TrackObject");

  ges_track_object_set_start_internal (trobj, object->start);
  ges_track_object_set_priority_internal (trobj, object->priority);
  ges_track_object_set_duration_internal (trobj, object->duration);
  ges_track_object_set_inpoint_internal (trobj, object->inpoint);

  GST_DEBUG ("Returning trobj:%p", trobj);

  g_signal_connect (G_OBJECT (trobj), "notify::priority-offset", G_CALLBACK
      (track_object_priority_offset_changed_cb), object);

  return TRUE;
}

/**
 * ges_timeline_object_release_track_object:
 * @object: a #GESTimelineObject
 * @trobj: the #GESTrackObject to release
 */
gboolean
ges_timeline_object_release_track_object (GESTimelineObject * object,
    GESTrackObject * trobj)
{
  GST_DEBUG ("object:%p, trackobject:%p", object, trobj);

  if (!(g_list_find (object->priv->trackobjects, trobj))) {
    GST_WARNING ("TrackObject isn't controlled by this object");
    return FALSE;
  }

  /* FIXME : Do we need to tell the subclasses ?
   * If so, add a new virtual-method */

  object->priv->trackobjects =
      g_list_remove (object->priv->trackobjects, trobj);

  ges_track_object_set_timeline_object (trobj, NULL);

  g_object_unref (trobj);

  return TRUE;
}

void
ges_timeline_object_set_layer (GESTimelineObject * object,
    GESTimelineLayer * layer)
{
  GST_DEBUG ("object:%p, layer:%p", object, layer);

  object->priv->layer = layer;
}

gboolean
ges_timeline_object_fill_track_object (GESTimelineObject * object,
    GESTrackObject * trackobj, GstElement * gnlobj)
{
  GESTimelineObjectClass *class;
  gboolean res = TRUE;

  GST_DEBUG ("object:%p, trackobject:%p, gnlobject:%p",
      object, trackobj, gnlobj);

  class = GES_TIMELINE_OBJECT_GET_CLASS (object);

  if (class->need_fill_track) {
    if (G_UNLIKELY (class->fill_track_object == NULL)) {
      GST_WARNING ("No 'fill_track_object' implementation available");
      return FALSE;
    }

    res = class->fill_track_object (object, trackobj, gnlobj);
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

  if (G_LIKELY (object->priv->trackobjects)) {
    GList *tmp;

    for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp))
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

  if (G_LIKELY (object->priv->trackobjects)) {
    GList *tmp;

    for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp))
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

  if (G_LIKELY (object->priv->trackobjects)) {
    GList *tmp;

    for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp))
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

  if (G_LIKELY (object->priv->trackobjects)) {
    GList *tmp;

    for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp))
      /* call set_priority_internal on each trackobject */
      ges_track_object_set_priority_internal (GES_TRACK_OBJECT (tmp->data),
          priority);

  }

  object->priority = priority;

}

/**
 * ges_timeline_object_find_track_object:
 * @object: a #GESTimelineObject
 * @track: a #GESTrack or NULL
 * @type: a #GType indicating the type of track object you are looking
 * for or %G_TYPE_NONE if you do not care about the track type.
 *
 * Finds the #GESTrackObject controlled by @object that is used in @track. You
 * may optionally specify a GType to further narrow search criteria.
 *
 * Note: The reference count of the returned #GESTrackObject will be increased,
 * unref when done with it.
 *
 * Returns: The #GESTrackObject used by @track, else #NULL.
 */

GESTrackObject *
ges_timeline_object_find_track_object (GESTimelineObject * object,
    GESTrack * track, GType type)
{
  GESTrackObject *ret = NULL;

  if (G_LIKELY (object->priv->trackobjects)) {
    GList *tmp;

    for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp))
      if (GES_TRACK_OBJECT (tmp->data)->track == track) {
        if ((type != G_TYPE_NONE) && !G_TYPE_CHECK_INSTANCE_TYPE (tmp->data,
                type))
          continue;

        ret = GES_TRACK_OBJECT (tmp->data);
        g_object_ref (ret);
        break;
      }
  }

  return ret;
}

/**
 * ges_timeline_object_get_layer:
 * @object: a #GESTimelineObject
 *
 * Note: The reference count of the returned #GESTimelineLayer will be increased,
 * The user is responsible for unreffing it.
 *
 * Returns: The #GESTimelineLayer where this @object is being used, #NULL if 
 * it is not used on any layer.
 */
GESTimelineLayer *
ges_timeline_object_get_layer (GESTimelineObject * object)
{
  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), NULL);

  if (object->priv->layer != NULL)
    g_object_ref (G_OBJECT (object->priv->layer));

  return object->priv->layer;
}

/**
 * ges_timeline_object_get_track_objects:
 * @obj: a #GESTimelineObject
 *
 * Returns: The list of trackobject contained in @object.
 * The user is responsible for unreffing the contained objects 
 * and freeing the list.
 */
GList *
ges_timeline_object_get_track_objects (GESTimelineObject * object)
{
  GList *ret;
  GList *tmp;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), NULL);

  ret = g_list_copy (object->priv->trackobjects);

  for (tmp = ret; tmp; tmp = tmp->next) {
    g_object_ref (tmp->data);
  }

  return ret;
}

static void
track_object_priority_offset_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * obj)
{
  guint new, old;

  /* all track objects have height 1 */
  new = GES_TRACK_OBJECT_PRIORITY_OFFSET (child) + 1;
  old = GES_TIMELINE_OBJECT_HEIGHT (obj);

  GST_LOG ("object %p, new=%d, old=%d", obj, new, old);

  if (new > old) {
    obj->height = new;
    GST_LOG ("emitting notify signal");
    g_object_notify ((GObject *) obj, "height");
  }
}
