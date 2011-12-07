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
#include "gesmarshal.h"
#include "ges-internal.h"

gboolean
ges_timeline_object_fill_track_object_func (GESTimelineObject * object,
    GESTrackObject * trackobj, GstElement * gnlobj);

gboolean
ges_timeline_object_create_track_objects_func (GESTimelineObject
    * object, GESTrack * track);

static void
track_object_start_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object);
static void
track_object_inpoint_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object);
static void
track_object_duration_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object);
static void
track_object_priority_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object);
static void update_height (GESTimelineObject * object);

void
tck_object_added_cb (GESTimelineObject * object,
    GESTrackObject * track_object, GList * track_objects);

static gint sort_track_effects (gpointer a, gpointer b,
    GESTimelineObject * object);
static void
get_layer_priorities (GESTimelineLayer * layer, guint32 * layer_min_gnl_prio,
    guint32 * layer_max_gnl_prio);

static gboolean
ges_timeline_object_set_start_internal (GESTimelineObject * object,
    guint64 start);
static gboolean ges_timeline_object_set_inpoint_internal (GESTimelineObject *
    object, guint64 inpoint);
static gboolean ges_timeline_object_set_duration_internal (GESTimelineObject *
    object, guint64 duration);
static gboolean ges_timeline_object_set_priority_internal (GESTimelineObject *
    object, guint32 priority);

static GESTimelineObject *ges_timeline_object_copy (GESTimelineObject * object,
    gboolean * deep);

G_DEFINE_ABSTRACT_TYPE (GESTimelineObject, ges_timeline_object,
    G_TYPE_INITIALLY_UNOWNED);

/* Mapping of relationship between a TimelineObject and the TrackObjects
 * it controls
 *
 * NOTE : how do we make this public in the future ?
 */
typedef struct
{
  GESTrackObject *object;
  gint64 start_offset;
  gint64 duration_offset;
  gint64 inpoint_offset;
  gint32 priority_offset;

  guint start_notifyid;
  guint duration_notifyid;
  guint inpoint_notifyid;
  guint priority_notifyid;

  /* track mapping ?? */
} ObjectMapping;

enum
{
  EFFECT_ADDED,
  EFFECT_REMOVED,
  TRACK_OBJECT_ADDED,
  TRACK_OBJECT_REMOVED,
  LAST_SIGNAL
};

static guint ges_timeline_object_signals[LAST_SIGNAL] = { 0 };

struct _GESTimelineObjectPrivate
{
  /*< public > */
  GESTimelineLayer *layer;

  /*< private > */
  /* A list of TrackObject controlled by this TimelineObject sorted by
   * priority */
  GList *trackobjects;

  /* Set to TRUE when the timelineobject is doing updates of track object
   * properties so we don't end up in infinite property update loops
   */
  gboolean ignore_notifies;
  gboolean is_moving;

  GList *mappings;

  guint nb_effects;

  /* The formats supported by this TimelineObject */
  GESTrackType supportedformats;
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
  PROP_SUPPORTED_FORMATS,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

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
    case PROP_SUPPORTED_FORMATS:
      g_value_set_flags (value, tobj->priv->supportedformats);
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
      ges_timeline_object_set_start_internal (tobj, g_value_get_uint64 (value));
      break;
    case PROP_INPOINT:
      ges_timeline_object_set_inpoint_internal (tobj,
          g_value_get_uint64 (value));
      break;
    case PROP_DURATION:
      ges_timeline_object_set_duration_internal (tobj,
          g_value_get_uint64 (value));
      break;
    case PROP_PRIORITY:
      ges_timeline_object_set_priority_internal (tobj,
          g_value_get_uint (value));
      break;
    case PROP_SUPPORTED_FORMATS:
      ges_timeline_object_set_supported_formats (tobj,
          g_value_get_flags (value));
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
  klass->track_object_added = NULL;
  klass->track_object_released = NULL;

  /**
   * GESTimelineObject:start
   *
   * The position of the object in the #GESTimelineLayer (in nanoseconds).
   */
  properties[PROP_START] = g_param_spec_uint64 ("start", "Start",
      "The position in the container", 0, G_MAXUINT64, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_START,
      properties[PROP_START]);

  /**
   * GESTimelineObject:in-point
   *
   * The in-point at which this #GESTimelineObject will start outputting data
   * from its contents (in nanoseconds).
   *
   * Ex : an in-point of 5 seconds means that the first outputted buffer will
   * be the one located 5 seconds in the controlled resource.
   */
  properties[PROP_INPOINT] =
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_INPOINT,
      properties[PROP_INPOINT]);

  /**
   * GESTimelineObject:duration
   *
   * The duration (in nanoseconds) which will be used in the container #GESTrack
   * starting from 'in-point'.
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The duration to use", 0,
      G_MAXUINT64, GST_CLOCK_TIME_NONE, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_DURATION,
      properties[PROP_DURATION]);

  /**
   * GESTimelineObject:priority
   *
   * The layer priority of the timeline object.
   */
  properties[PROP_PRIORITY] = g_param_spec_uint ("priority", "Priority",
      "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PRIORITY,
      properties[PROP_PRIORITY]);

  /**
   * GESTimelineObject:height
   *
   * The span of layer priorities which this object occupies.
   */
  properties[PROP_HEIGHT] = g_param_spec_uint ("height", "Height",
      "The span of priorities this object occupies", 0, G_MAXUINT, 1,
      G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_HEIGHT,
      properties[PROP_HEIGHT]);

  /**
   * GESTimelineObject:supported-formats:
   *
   * The formats supported by the object.
   *
   * Since: 0.10.XX
   */
  properties[PROP_SUPPORTED_FORMATS] = g_param_spec_flags ("supported-formats",
      "Supported formats", "Formats supported by the file",
      GES_TYPE_TRACK_TYPE, GES_TRACK_TYPE_UNKNOWN,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  g_object_class_install_property (object_class, PROP_SUPPORTED_FORMATS,
      properties[PROP_SUPPORTED_FORMATS]);

  /**
   * GESTimelineObject:layer
   *
   * The GESTimelineLayer where this object is being used.
   */
  properties[PROP_LAYER] = g_param_spec_object ("layer", "Layer",
      "The GESTimelineLayer where this object is being used.",
      GES_TYPE_TIMELINE_LAYER, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_LAYER,
      properties[PROP_LAYER]);

  /**
   * GESTimelineObject::effect-added
   * @object: the #GESTimelineObject
   * @effect: the #GESTrackEffect that was added.
   *
   * Will be emitted after an effect was added to the object.
   *
   * Since: 0.10.2
   */
  ges_timeline_object_signals[EFFECT_ADDED] =
      g_signal_new ("effect-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, ges_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_EFFECT);

  /**
   * GESTimelineObject::effect-removed
   * @object: the #GESTimelineObject
   * @effect: the #GESTrackEffect that was added.
   *
   * Will be emitted after an effect was remove from the object.
   *
   * Since: 0.10.2
   */
  ges_timeline_object_signals[EFFECT_REMOVED] =
      g_signal_new ("effect-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, ges_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_EFFECT);

  /**
   * GESTimelineObject::track-object-added
   * @object: the #GESTimelineObject
   * @tckobj: the #GESTrackObject that was added.
   *
   * Will be emitted after a track object was added to the object.
   *
   * Since: 0.10.2
   */
  ges_timeline_object_signals[TRACK_OBJECT_ADDED] =
      g_signal_new ("track-object-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, ges_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_OBJECT);

  /**
   * GESTimelineObject::track-object-removed
   * @object: the #GESTimelineObject
   * @tckobj: the #GESTrackObject that was removed.
   *
   * Will be emitted after a track object was removed from @object.
   *
   * Since: 0.10.2
   */
  ges_timeline_object_signals[TRACK_OBJECT_REMOVED] =
      g_signal_new ("track-object-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, ges_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, GES_TYPE_TRACK_OBJECT);

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
  self->priv->nb_effects = 0;
  self->priv->is_moving = FALSE;
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
 * Returns: (transfer none): A #GESTrackObject. Returns NULL if the #GESTrackObject could not
 * be created.
 */

GESTrackObject *
ges_timeline_object_create_track_object (GESTimelineObject * object,
    GESTrack * track)
{
  GESTimelineObjectClass *class;
  GESTrackObject *res;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), NULL);
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);

  class = GES_TIMELINE_OBJECT_GET_CLASS (object);

  if (G_UNLIKELY (class->create_track_object == NULL)) {
    GST_ERROR ("No 'create_track_object' implementation available");
    return NULL;
  }

  res = class->create_track_object (object, track);
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

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);
  g_return_val_if_fail (GES_IS_TRACK (track), FALSE);

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
  gboolean ret;

  result = ges_timeline_object_create_track_object (object, track);
  if (!result) {
    GST_DEBUG ("Did not create track object");
    return FALSE;
  }
  ges_track_object_set_timeline_object (result, object);
  ret = ges_track_add_object (track, result);
  ges_timeline_object_add_track_object (object, result);
  return ret;
}

/**
 * ges_timeline_object_add_track_object:
 * @object: a #GESTimelineObject
 * @trobj: the GESTrackObject
 *
 * Add a track object to the timeline object. Should only be called by
 * subclasses implementing the create_track_objects (plural) vmethod.
 *
 * Takes a reference on @trobj.
 *
 * Returns: %TRUE on success, %FALSE on failure.
 */

gboolean
ges_timeline_object_add_track_object (GESTimelineObject * object, GESTrackObject
    * trobj)
{
  ObjectMapping *mapping;
  GList *tmp;
  guint max_prio, min_prio;
  GESTimelineObjectPrivate *priv = object->priv;
  gboolean is_effect = GES_IS_TRACK_EFFECT (trobj);
  GESTimelineObjectClass *klass = GES_TIMELINE_OBJECT_GET_CLASS (object);

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (trobj), FALSE);

  GST_LOG ("Got a TrackObject : %p , setting the timeline object as its"
      "creator. Is a TrackEffect %i", trobj, is_effect);

  if (!trobj)
    return FALSE;

  ges_track_object_set_timeline_object (trobj, object);

  g_object_ref (trobj);

  mapping = g_slice_new0 (ObjectMapping);
  mapping->object = trobj;
  priv->mappings = g_list_append (priv->mappings, mapping);

  GST_DEBUG ("Adding TrackObject to the list of controlled track objects");
  /* We steal the initial reference */

  GST_DEBUG ("Setting properties on newly created TrackObject");

  mapping->priority_offset = priv->nb_effects;

  /* If the trackobject is an effect:
   *  - We add it on top of the list of TrackEffect
   *  - We put all TrackObject present in the TimelineObject
   *    which are not TrackEffect on top of them
   *
   * FIXME: Let the full control over priorities to the user
   */
  if (is_effect) {
    GST_DEBUG
        ("Moving non on top effect under other TrackObject-s, nb effects %i",
        priv->nb_effects);
    for (tmp = g_list_nth (priv->trackobjects, priv->nb_effects); tmp;
        tmp = tmp->next) {
      GESTrackObject *tmpo = GES_TRACK_OBJECT (tmp->data);

      /* We make sure not to move the entire #TimelineObject */
      ges_track_object_set_locked (tmpo, FALSE);
      ges_track_object_set_priority (tmpo,
          ges_track_object_get_priority (tmpo) + 1);
      ges_track_object_set_locked (tmpo, TRUE);
    }

    priv->nb_effects++;

    /* emit 'effect-added' */
    g_signal_emit (object, ges_timeline_object_signals[EFFECT_ADDED], 0,
        GES_TRACK_EFFECT (trobj));
  }

  object->priv->trackobjects =
      g_list_insert_sorted_with_data (object->priv->trackobjects, trobj,
      (GCompareDataFunc) sort_track_effects, object);

  ges_track_object_set_start (trobj, object->start);
  ges_track_object_set_duration (trobj, object->duration);
  ges_track_object_set_inpoint (trobj, object->inpoint);

  if (klass->track_object_added) {
    GST_DEBUG ("Calling track_object_added subclass method");
    klass->track_object_added (object, trobj);
  } else {
    GST_DEBUG ("%s doesn't have any track_object_added vfunc implementation",
        G_OBJECT_CLASS_NAME (klass));
  }

  /* Listen to all property changes */
  mapping->start_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::start",
      G_CALLBACK (track_object_start_changed_cb), object);
  mapping->duration_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::duration",
      G_CALLBACK (track_object_duration_changed_cb), object);
  mapping->inpoint_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::inpoint",
      G_CALLBACK (track_object_inpoint_changed_cb), object);
  mapping->priority_notifyid =
      g_signal_connect (G_OBJECT (trobj), "notify::priority",
      G_CALLBACK (track_object_priority_changed_cb), object);

  get_layer_priorities (priv->layer, &min_prio, &max_prio);
  ges_track_object_set_priority (trobj, min_prio + object->priority
      + mapping->priority_offset);

  GST_DEBUG ("Returning trobj:%p", trobj);
  if (!GES_IS_TRACK_PARSE_LAUNCH_EFFECT (trobj)) {
    g_signal_emit (object, ges_timeline_object_signals[TRACK_OBJECT_ADDED], 0,
        GES_TRACK_OBJECT (trobj));
  }
  return TRUE;
}

/**
 * ges_timeline_object_release_track_object:
 * @object: a #GESTimelineObject
 * @trackobject: the #GESTrackObject to release
 *
 * Release the @trackobject from the control of @object.
 *
 * Returns: %TRUE if the @trackobject was properly released, else %FALSE.
 */
gboolean
ges_timeline_object_release_track_object (GESTimelineObject * object,
    GESTrackObject * trackobject)
{
  GList *tmp;
  ObjectMapping *mapping = NULL;
  GESTimelineObjectClass *klass = GES_TIMELINE_OBJECT_GET_CLASS (object);

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (trackobject), FALSE);

  GST_DEBUG ("object:%p, trackobject:%p", object, trackobject);

  if (!(g_list_find (object->priv->trackobjects, trackobject))) {
    GST_WARNING ("TrackObject isn't controlled by this object");
    return FALSE;
  }

  for (tmp = object->priv->mappings; tmp; tmp = tmp->next) {
    mapping = (ObjectMapping *) tmp->data;
    if (mapping->object == trackobject)
      break;
  }

  if (tmp && mapping) {

    /* Disconnect all notify listeners */
    g_signal_handler_disconnect (trackobject, mapping->start_notifyid);
    g_signal_handler_disconnect (trackobject, mapping->duration_notifyid);
    g_signal_handler_disconnect (trackobject, mapping->inpoint_notifyid);
    g_signal_handler_disconnect (trackobject, mapping->priority_notifyid);

    g_slice_free (ObjectMapping, mapping);

    object->priv->mappings = g_list_delete_link (object->priv->mappings, tmp);
  }

  object->priv->trackobjects =
      g_list_remove (object->priv->trackobjects, trackobject);

  if (GES_IS_TRACK_EFFECT (trackobject)) {
    /* emit 'object-removed' */
    g_signal_emit (object, ges_timeline_object_signals[EFFECT_REMOVED], 0,
        GES_TRACK_EFFECT (trackobject));
  } else
    g_signal_emit (object, ges_timeline_object_signals[TRACK_OBJECT_REMOVED], 0,
        GES_TRACK_OBJECT (trackobject));

  ges_track_object_set_timeline_object (trackobject, NULL);

  GST_DEBUG ("Removing reference to track object %p", trackobject);

  if (klass->track_object_released) {
    GST_DEBUG ("Calling track_object_released subclass method");
    klass->track_object_released (object, trackobject);
  }

  g_object_unref (trackobject);

  /* FIXME : resync properties ? */

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

static ObjectMapping *
find_object_mapping (GESTimelineObject * object, GESTrackObject * child)
{
  GList *tmp;

  for (tmp = object->priv->mappings; tmp; tmp = tmp->next) {
    ObjectMapping *map = (ObjectMapping *) tmp->data;
    if (map->object == child)
      return map;
  }

  return NULL;
}

static gboolean
ges_timeline_object_set_start_internal (GESTimelineObject * object,
    guint64 start)
{
  GList *tmp;
  GESTrackObject *tr;
  ObjectMapping *map;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);

  GST_DEBUG ("object:%p, start:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (start));

  object->priv->ignore_notifies = TRUE;

  for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackObject *) tmp->data;
    map = find_object_mapping (object, tr);

    if (ges_track_object_is_locked (tr)) {
      /* Move the child... */
      ges_track_object_set_start (tr, start + map->start_offset);
    } else {
      /* ... or update the offset */
      map->start_offset = start - tr->start;
    }
  }

  object->priv->ignore_notifies = FALSE;

  object->start = start;
  return TRUE;
}

/**
 * ges_timeline_object_set_start:
 * @object: a #GESTimelineObject
 * @start: the position in #GstClockTime
 *
 * Set the position of the object in its containing layer
 */
void
ges_timeline_object_set_start (GESTimelineObject * object, guint64 start)
{
  if (ges_timeline_object_set_start_internal (object, start))
#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_START]);
#else
    g_object_notify (G_OBJECT (object), "start");
#endif
}

static gboolean
ges_timeline_object_set_inpoint_internal (GESTimelineObject * object,
    guint64 inpoint)
{
  GList *tmp;
  GESTrackObject *tr;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);

  GST_DEBUG ("object:%p, inpoint:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (inpoint));

  for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackObject *) tmp->data;

    if (ges_track_object_is_locked (tr))
      /* call set_inpoint on each trackobject */
      ges_track_object_set_inpoint (tr, inpoint);
  }

  object->inpoint = inpoint;
  return TRUE;
}

/**
 * ges_timeline_object_set_inpoint:
 * @object: a #GESTimelineObject
 * @inpoint: the in-point in #GstClockTime
 *
 * Set the in-point, that is the moment at which the @object will start
 * outputting data from its contents.
 */
void
ges_timeline_object_set_inpoint (GESTimelineObject * object, guint64 inpoint)
{
  if (ges_timeline_object_set_inpoint_internal (object, inpoint))
#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_INPOINT]);
#else
    g_object_notify (G_OBJECT (object), "in-point");
#endif
}

static gboolean
ges_timeline_object_set_duration_internal (GESTimelineObject * object,
    guint64 duration)
{
  GList *tmp;
  GESTrackObject *tr;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);

  GST_DEBUG ("object:%p, duration:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (duration));

  for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackObject *) tmp->data;

    if (ges_track_object_is_locked (tr))
      /* call set_duration on each trackobject */
      ges_track_object_set_duration (tr, duration);
  }

  object->duration = duration;
  return TRUE;
}

/**
 * ges_timeline_object_set_duration:
 * @object: a #GESTimelineObject
 * @duration: the duration in #GstClockTime
 *
 * Set the duration of the object
 */
void
ges_timeline_object_set_duration (GESTimelineObject * object, guint64 duration)
{
  if (ges_timeline_object_set_duration_internal (object, duration))
#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_DURATION]);
#else
    g_object_notify (G_OBJECT (object), "duration");
#endif
}

static gboolean
ges_timeline_object_set_priority_internal (GESTimelineObject * object,
    guint priority)
{
  GList *tmp;
  GESTrackObject *tr;
  ObjectMapping *map;
  GESTimelineObjectPrivate *priv;
  guint32 layer_min_gnl_prio, layer_max_gnl_prio;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);

  GST_DEBUG ("object:%p, priority:%" G_GUINT32_FORMAT, object, priority);

  priv = object->priv;
  priv->ignore_notifies = TRUE;

  object->priv->ignore_notifies = TRUE;

  get_layer_priorities (priv->layer, &layer_min_gnl_prio, &layer_max_gnl_prio);

  for (tmp = priv->trackobjects; tmp; tmp = g_list_next (tmp)) {
    tr = (GESTrackObject *) tmp->data;
    map = find_object_mapping (object, tr);

    if (ges_track_object_is_locked (tr)) {
      guint32 real_tck_prio;

      /* Move the child... */
      real_tck_prio = layer_min_gnl_prio + priority + map->priority_offset;

      if (real_tck_prio > layer_max_gnl_prio) {
        GST_WARNING ("%p priority of %i, is outside of the its containing "
            "layer space. (%d/%d) setting it to the maximum it can be", object,
            priority, layer_min_gnl_prio, layer_max_gnl_prio);

        real_tck_prio = layer_max_gnl_prio;
      }

      ges_track_object_set_priority (tr, real_tck_prio);

    } else {
      /* ... or update the offset */
      map->priority_offset = tr->priority - layer_min_gnl_prio + priority;
    }
  }

  priv->trackobjects = g_list_sort_with_data (priv->trackobjects,
      (GCompareDataFunc) sort_track_effects, object);
  priv->ignore_notifies = FALSE;

  object->priority = priority;
  return TRUE;
}

/**
 * ges_timeline_object_set_moving_from_layer:
 * @object: a #GESTimelineObject
 * @is_moving: %TRUE if you want to start moving @object to another layer
 * %FALSE when you finished moving it.
 *
 * Sets the object in a moving to layer state. You might rather use the
 * ges_timeline_object_move_to_layer function to move #GESTimelineObject-s
 * from a layer to another.
 **/
void
ges_timeline_object_set_moving_from_layer (GESTimelineObject * object,
    gboolean is_moving)
{
  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  object->priv->is_moving = is_moving;
}

/**
 * ges_timeline_object_is_moving_from_layer:
 * @object: a #GESTimelineObject
 *
 * Tells you if the object is currently moving from a layer to another.
 * You might rather use the ges_timeline_object_move_to_layer function to
 * move #GESTimelineObject-s from a layer to another.
 *
 *
 * Returns: %TRUE if @object is currently moving from its current layer
 * %FALSE otherwize
 **/
gboolean
ges_timeline_object_is_moving_from_layer (GESTimelineObject * object)
{
  return object->priv->is_moving;
}

/**
 * ges_timeline_object_move_to_layer:
 * @object: a #GESTimelineObject
 * @layer: the new #GESTimelineLayer
 *
 * Moves @object to @layer. If @object is not in any layer, it adds it to
 * @layer, else, it removes it from its current layer, and adds it to @layer.
 *
 * Returns: %TRUE if @object could be moved %FALSE otherwize
 */
gboolean
ges_timeline_object_move_to_layer (GESTimelineObject * object, GESTimelineLayer
    * layer)
{
  gboolean ret;
  GESTimelineLayer *current_layer;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);
  g_return_val_if_fail (GES_IS_TIMELINE_LAYER (layer), FALSE);

  current_layer = object->priv->layer;

  if (current_layer == NULL) {
    GST_DEBUG ("Not moving %p, only adding it to %p", object, layer);

    return ges_timeline_layer_add_object (layer, object);
  }

  object->priv->is_moving = TRUE;
  g_object_ref (object);
  ret = ges_timeline_layer_remove_object (current_layer, object);

  if (!ret) {
    g_object_unref (object);
    return FALSE;
  }

  ret = ges_timeline_layer_add_object (layer, object);
  object->priv->is_moving = FALSE;

  g_object_unref (object);

  return ret;
}

/**
 * ges_timeline_object_set_priority:
 * @object: a #GESTimelineObject
 * @priority: the priority
 *
 * Sets the priority of the object within the containing layer
 */
void
ges_timeline_object_set_priority (GESTimelineObject * object, guint priority)
{
  if (ges_timeline_object_set_priority_internal (object, priority))
#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_PRIORITY]);
#else
    g_object_notify (G_OBJECT (object), "priority");
#endif
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
 * Note: If many objects match, then the one with the highest priority will be
 * returned.
 *
 * Returns: (transfer full): The #GESTrackObject used by @track, else %NULL.
 * Unref after usage.
 */

GESTrackObject *
ges_timeline_object_find_track_object (GESTimelineObject * object,
    GESTrack * track, GType type)
{
  GESTrackObject *ret = NULL;
  GList *tmp;
  GESTrackObject *otmp;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), NULL);
  g_return_val_if_fail (GES_IS_TRACK (track), NULL);

  for (tmp = object->priv->trackobjects; tmp; tmp = g_list_next (tmp)) {
    otmp = (GESTrackObject *) tmp->data;

    if (ges_track_object_get_track (otmp) == track) {
      if ((type != G_TYPE_NONE) &&
          !G_TYPE_CHECK_INSTANCE_TYPE (tmp->data, type))
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
 * Get the #GESTimelineLayer to which this object belongs.
 *
 * Returns: (transfer full): The #GESTimelineLayer where this @object is being
 * used, or %NULL if it is not used on any layer. The caller should unref it
 * usage.
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
 * @object: a #GESTimelineObject
 *
 * Get the list of #GESTrackObject contained in @object
 *
 * Returns: (transfer full) (element-type GESTrackObject): The list of
 * trackobject contained in @object.
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

static gint
sort_track_effects (gpointer a, gpointer b, GESTimelineObject * object)
{
  guint prio_offset_a, prio_offset_b;
  ObjectMapping *map_a, *map_b;
  GESTrackObject *obj_a, *obj_b;

  obj_a = GES_TRACK_OBJECT (a);
  obj_b = GES_TRACK_OBJECT (b);

  map_a = find_object_mapping (object, obj_a);
  map_b = find_object_mapping (object, obj_b);

  prio_offset_a = map_a->priority_offset;
  prio_offset_b = map_b->priority_offset;

  if ((gint) prio_offset_a > (guint) prio_offset_b)
    return 1;
  if ((guint) prio_offset_a < (guint) prio_offset_b)
    return -1;

  return 0;
}

/**
 * ges_timeline_object_get_top_effects:
 * @object: The origin #GESTimelineObject
 *
 * Get effects applied on @object
 *
 * Returns: (transfer full) (element-type GESTrackObject): a #GList of the
 * #GESTrackEffect that are applied on @object order by ascendant priorities.
 * The refcount of the objects will be increased. The user will have to
 * unref each #GESTrackEffect and free the #GList.
 *
 * Since: 0.10.2
 */
GList *
ges_timeline_object_get_top_effects (GESTimelineObject * object)
{
  GList *tmp, *ret;
  guint i;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), NULL);

  ret = NULL;

  for (tmp = object->priv->trackobjects, i = 0; i < object->priv->nb_effects;
      tmp = tmp->next, i++) {
    ret = g_list_append (ret, tmp->data);
    g_object_ref (tmp->data);
  }

  return ret;
}

/**
 * ges_timeline_object_get_top_effect_position:
 * @object: The origin #GESTimelineObject
 * @effect: The #GESTrackEffect we want to get the top position from
 *
 * Gets the top position of an effect.
 *
 * Returns: The top position of the effect, -1 if something went wrong.
 *
 * Since: 0.10.2
 */
gint
ges_timeline_object_get_top_effect_position (GESTimelineObject * object,
    GESTrackEffect * effect)
{
  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), -1);

  return find_object_mapping (object,
      GES_TRACK_OBJECT (effect))->priority_offset;
}

/**
 * ges_timeline_object_set_top_effect_priority:
 * @object: The origin #GESTimelineObject
 * @effect: The #GESTrackEffect to move
 * @newpriority: the new position at which to move the @effect inside this
 * #GESTimelineObject
 *
 * This is a convenience method that lets you set the priority of a top effect.
 *
 * Returns: %TRUE if @effect was successfuly moved, %FALSE otherwise.
 *
 * Since: 0.10.2
 */
gboolean
ges_timeline_object_set_top_effect_priority (GESTimelineObject * object,
    GESTrackEffect * effect, guint newpriority)
{
  gint inc;
  GList *tmp;
  guint current_prio;
  GESTrackObject *tck_obj;
  GESTimelineObjectPrivate *priv;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), FALSE);

  tck_obj = GES_TRACK_OBJECT (effect);
  priv = object->priv;
  current_prio = ges_track_object_get_priority (tck_obj);

  /*  We don't change the priority */
  if (current_prio == newpriority ||
      (G_UNLIKELY (ges_track_object_get_timeline_object (tck_obj) != object)))
    return FALSE;

  if (newpriority > (object->priv->nb_effects - 1)) {
    GST_DEBUG ("You are trying to make %p not a top effect", effect);
    return FALSE;
  }

  if (current_prio > object->priv->nb_effects) {
    GST_DEBUG ("%p is not a top effect");
    return FALSE;
  }

  if (tck_obj->priority < newpriority)
    inc = -1;
  else
    inc = +1;

  ges_track_object_set_priority (tck_obj, newpriority);
  for (tmp = priv->trackobjects; tmp; tmp = tmp->next) {
    GESTrackObject *tmpo = GES_TRACK_OBJECT (tmp->data);
    guint tck_priority = ges_track_object_get_priority (tmpo);

    if ((inc == +1 && tck_priority >= newpriority) ||
        (inc == -1 && tck_priority <= newpriority)) {
      ges_track_object_set_priority (tmpo, tck_priority + inc);
    }
  }

  priv->trackobjects = g_list_sort_with_data (priv->trackobjects,
      (GCompareDataFunc) sort_track_effects, object);

  return TRUE;
}

void
tck_object_added_cb (GESTimelineObject * object,
    GESTrackObject * track_object, GList * track_objects)
{
  gint64 duration, start, inpoint, position;
  GList *tmp;
  gboolean locked;

  ges_track_object_set_locked (track_object, FALSE);
  g_object_get (object, "start", &position, NULL);
  for (tmp = track_objects; tmp; tmp = tmp->next) {
    if (ges_track_object_get_track (track_object)->type ==
        ges_track_object_get_track (tmp->data)->type) {
      locked = ges_track_object_is_locked (tmp->data);
      ges_track_object_set_locked (tmp->data, FALSE);
      g_object_get (tmp->data, "duration", &duration, "start", &start,
          "in-point", &inpoint, NULL);
      g_object_set (tmp->data, "duration",
          duration - (duration + start - position), NULL);
      g_object_set (track_object, "start", position, "in-point",
          duration - (duration + start - inpoint - position), "duration",
          duration + start - position, NULL);
      ges_track_object_set_locked (tmp->data, locked);
      ges_track_object_set_locked (track_object, locked);
    }
  }
}

/**
 * ges_timeline_object_split:
 * @object: the #GESTimelineObject to split
 * @position: The position at which to split the @object (in nanosecond)
 *
 * The function modifies @object, and creates another #GESTimelineObject so
 * we have two clips at the end, splitted at the time specified by @position.
 *
 * Returns: (transfer full): The newly created #GESTimelineObject resulting from
 * the splitting
 *
 * Since: 0.10.XX
 */
GESTimelineObject *
ges_timeline_object_split (GESTimelineObject * object, gint64 position)
{
  GList *track_objects, *tmp;
  GESTimelineLayer *layer;
  GESTimelineObject *new_object;
  gint64 duration, start, inpoint;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), NULL);

  g_object_get (object, "duration", &duration, "start", &start, "in-point",
      &inpoint, NULL);

  track_objects = ges_timeline_object_get_track_objects (object);
  layer = ges_timeline_object_get_layer (object);

  new_object = ges_timeline_object_copy (object, FALSE);

  if (g_list_length (track_objects) == 2) {
    g_object_set (new_object, "start", position, NULL);
    g_signal_connect (G_OBJECT (new_object), "track-object-added",
        G_CALLBACK (tck_object_added_cb), track_objects);
  } else {
    for (tmp = track_objects; tmp; tmp = tmp->next) {
      g_object_set (tmp->data, "duration",
          duration - (duration + start - position), NULL);
      g_object_set (new_object, "start", position, "in-point",
          duration - (duration + start - position), "duration",
          (duration + start - position), NULL);
      g_object_set (object, "duration",
          duration - (duration + start - position), NULL);
    }
  }

  ges_timeline_layer_add_object (layer, new_object);

  return new_object;
}

/* TODO implement the deep parameter, and make it public */
static GESTimelineObject *
ges_timeline_object_copy (GESTimelineObject * object, gboolean * deep)
{
  GESTimelineObject *ret = NULL;
  GParameter *params;
  GParamSpec **specs;
  guint n, n_specs, n_params;

  g_return_val_if_fail (GES_IS_TIMELINE_OBJECT (object), NULL);

  specs =
      g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &n_specs);
  params = g_new0 (GParameter, n_specs);
  n_params = 0;

  for (n = 0; n < n_specs; ++n) {
    if (strcmp (specs[n]->name, "parent") &&
        (specs[n]->flags & G_PARAM_READWRITE) == G_PARAM_READWRITE) {
      params[n_params].name = g_intern_string (specs[n]->name);
      g_value_init (&params[n_params].value, specs[n]->value_type);
      g_object_get_property (G_OBJECT (object), specs[n]->name,
          &params[n_params].value);
      ++n_params;
    }
  }

  ret = g_object_newv (G_TYPE_FROM_INSTANCE (object), n_params, params);

  if (GES_IS_TIMELINE_FILE_SOURCE (ret)) {
    GList *tck_objects;
    tck_objects = ges_timeline_object_get_track_objects (object);
    if (g_list_length (tck_objects) == 1) {
      GESTrackType type;
      type = ges_track_object_get_track (tck_objects->data)->type;
      ges_timeline_filesource_set_supported_formats (GES_TIMELINE_FILE_SOURCE
          (ret), type);
    }
    g_list_free (tck_objects);
  }

  g_free (specs);
  g_free (params);

  return ret;
}

/**
 * ges_timeline_object_objects_set_locked:
 * @object: the #GESTimelineObject
 * @locked: whether the #GESTrackObject contained in @object are locked to it.
 *
 * Set the locking status of all the #GESTrackObject contained in @object to @locked.
 * See the ges_track_object_set_locked documentation for more details.
 *
 * Since: 0.10.XX
 */
void
ges_timeline_object_objects_set_locked (GESTimelineObject * object,
    gboolean locked)
{
  GList *tmp;

  g_return_if_fail (GES_IS_TIMELINE_OBJECT (object));

  for (tmp = object->priv->mappings; tmp; tmp = g_list_next (tmp)) {
    ges_track_object_set_locked (((ObjectMapping *) tmp->data)->object, locked);
  }
}

static void
update_height (GESTimelineObject * object)
{
  GList *tmp;
  guint32 min_prio = G_MAXUINT32, max_prio = 0;

  /* Go over all childs and check if height has changed */
  for (tmp = object->priv->trackobjects; tmp; tmp = tmp->next) {
    guint tck_priority =
        ges_track_object_get_priority (GES_TRACK_OBJECT (tmp->data));

    if (tck_priority < min_prio)
      min_prio = tck_priority;
    if (tck_priority > max_prio)
      max_prio = tck_priority;
  }

  /* FIXME : We only grow the height */
  if (object->height < (max_prio - min_prio + 1)) {
    object->height = max_prio - min_prio + 1;
    GST_DEBUG ("Updating height %i", object->height);
#if GLIB_CHECK_VERSION(2,26,0)
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_HEIGHT]);
#else
    g_object_notify (G_OBJECT (object), "height");
#endif
  }
}

/**
 * ges_timeline_object_set_supported_formats:
 * @self: the #GESTimelineObject to set supported formats on
 * @supportedformats: the #GESTrackType defining formats supported by @self
 *
 * Sets the formats supported by the file.
 *
 * Since: 0.10.XX
 */
void
ges_timeline_object_set_supported_formats (GESTimelineObject * self,
    GESTrackType supportedformats)
{
  self->priv->supportedformats = supportedformats;
}

/**
 * ges_timeline_object_get_supported_formats:
 * @self: the #GESTimelineObject
 *
 * Get the formats supported by @self.
 *
 * Returns: The formats supported by @self.
 *
 * Since: 0.10.XX
 */
GESTrackType
ges_timeline_object_get_supported_formats (GESTimelineObject * self)
{
  return self->priv->supportedformats;
}

/*
 * PROPERTY NOTIFICATIONS FROM TRACK OBJECTS
 */

static void
track_object_start_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object)
{
  ObjectMapping *map;

  if (object->priv->ignore_notifies)
    return;

  map = find_object_mapping (object, child);
  if (G_UNLIKELY (map == NULL))
    /* something massively screwed up if we get this */
    return;

  if (!ges_track_object_is_locked (child)) {
    /* Update the internal start_offset */
    map->start_offset = object->start - child->start;
  } else {
    /* Or update the parent start */
    ges_timeline_object_set_start (object, child->start + map->start_offset);
  }
}

static void
track_object_inpoint_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object)
{
  if (object->priv->ignore_notifies)
    return;

}

static void
track_object_duration_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object)
{
  if (object->priv->ignore_notifies)
    return;

}

static void
track_object_priority_changed_cb (GESTrackObject * child,
    GParamSpec * arg G_GNUC_UNUSED, GESTimelineObject * object)
{
  ObjectMapping *map;
  guint32 layer_min_gnl_prio, layer_max_gnl_prio;

  guint tck_priority = ges_track_object_get_priority (child);

  GST_DEBUG ("TrackObject %p priority changed to %i", child,
      ges_track_object_get_priority (child));

  if (object->priv->ignore_notifies)
    return;

  update_height (object);
  map = find_object_mapping (object, child);
  get_layer_priorities (object->priv->layer, &layer_min_gnl_prio,
      &layer_max_gnl_prio);

  if (G_UNLIKELY (map == NULL))
    /* something massively screwed up if we get this */
    return;

  if (!ges_track_object_is_locked (child)) {
    if (tck_priority < layer_min_gnl_prio || tck_priority > layer_max_gnl_prio) {
      GST_WARNING ("%p priority of %i, is outside of its containing "
          "layer space. (%d/%d). This is a bug in the program.", object,
          tck_priority, layer_min_gnl_prio, layer_max_gnl_prio);
    }

    /* Update the internal priority_offset */
    map->priority_offset =
        tck_priority - (layer_min_gnl_prio + object->priority);

  } else if (tck_priority < layer_min_gnl_prio + object->priority) {
    /* Or update the parent priority, the object priority is always the
     * highest priority (smaller number) */
    if (tck_priority - layer_min_gnl_prio < 0 ||
        layer_max_gnl_prio - tck_priority < 0) {

      GST_WARNING ("%p priority of %i, is outside of its containing "
          "layer space. (%d/%d). This is a bug in the program.", object,
          tck_priority, layer_min_gnl_prio, layer_max_gnl_prio);
      return;
    }

    ges_timeline_object_set_priority (object,
        tck_priority - layer_min_gnl_prio);
  }

  GST_DEBUG ("object %p priority %d child %p priority %d", object,
      object->priority, child, ges_track_object_get_priority (child));
}

static void
get_layer_priorities (GESTimelineLayer * layer, guint32 * layer_min_gnl_prio,
    guint32 * layer_max_gnl_prio)
{
  if (layer) {
    *layer_min_gnl_prio = layer->min_gnl_priority;
    *layer_max_gnl_prio = layer->max_gnl_priority;
  } else {
    *layer_min_gnl_prio = 0;
    *layer_max_gnl_prio = G_MAXUINT32;
  }
}
