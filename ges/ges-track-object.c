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
 * SECTION:ges-track-object
 * @short_description: Base Class for objects contained in a #GESTrack
 *
 * #GESTrackObject is the Base Class for any object that can be contained in a
 * #GESTrack.
 *
 * It contains the basic information as to the location of the object within
 * its container, like the start position, the in-point, the duration and the
 * priority.
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-timeline-object.h"

static GQuark _start_quark;
static GQuark _inpoint_quark;
static GQuark _duration_quark;
static GQuark _priority_quark;

#define _do_init \
{ \
  _start_quark = g_quark_from_static_string ("start"); \
  _inpoint_quark = g_quark_from_static_string ("inpoint"); \
  _duration_quark = g_quark_from_static_string ("duration"); \
  _priority_quark = g_quark_from_static_string ("priority"); \
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESTrackObject, ges_track_object,
    G_TYPE_OBJECT, _do_init);

struct _GESTrackObjectPrivate
{
  /* cache the base priority and offset */
  guint32 base_priority;
  guint32 priority_offset;

  /* These fields are only used before the gnlobject is available */
  guint64 pending_start;
  guint64 pending_inpoint;
  guint64 pending_duration;
  guint32 pending_gnl_priority;
  gboolean pending_active;

  GstElement *gnlobject;        /* The GnlObject */
  GstElement *element;          /* The element contained in the gnlobject (can be NULL) */

  GESTimelineObject *timelineobj;
  GESTrack *track;

  gboolean valid;

};

enum
{
  PROP_0,
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_PRIORITY,
  PROP_PRIORITY_OFFSET,
  PROP_ACTIVE
};

static GstElement *ges_track_object_create_gnl_object_func (GESTrackObject *
    object);

void gnlobject_start_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

void gnlobject_media_start_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

void gnlobject_priority_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

void gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

void gnlobject_active_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

static gboolean ges_track_object_update_priority (GESTrackObject * object);

static void
ges_track_object_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrackObject *tobj = GES_TRACK_OBJECT (object);

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
      g_value_set_uint (value, tobj->priv->base_priority);
      break;
    case PROP_PRIORITY_OFFSET:
      g_value_set_uint (value, tobj->priv->priority_offset);
      break;
    case PROP_ACTIVE:
      g_value_set_boolean (value, tobj->active);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_object_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTrackObject *tobj = GES_TRACK_OBJECT (object);

  switch (property_id) {
    case PROP_START:
      ges_track_object_set_start_internal (tobj, g_value_get_uint64 (value));
      break;
    case PROP_INPOINT:
      ges_track_object_set_inpoint_internal (tobj, g_value_get_uint64 (value));
      break;
    case PROP_DURATION:
      ges_track_object_set_duration_internal (tobj, g_value_get_uint64 (value));
      break;
    case PROP_PRIORITY:
      ges_track_object_set_priority_internal (tobj, g_value_get_uint (value));
      break;
    case PROP_PRIORITY_OFFSET:
      ges_track_object_set_priority_offset_internal (tobj, g_value_get_uint
          (value));
      break;
    case PROP_ACTIVE:
      ges_track_object_set_active (tobj, g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_object_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_object_parent_class)->dispose (object);
}

static void
ges_track_object_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_object_parent_class)->finalize (object);
}

static void
ges_track_object_class_init (GESTrackObjectClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackObjectPrivate));

  object_class->get_property = ges_track_object_get_property;
  object_class->set_property = ges_track_object_set_property;
  object_class->dispose = ges_track_object_dispose;
  object_class->finalize = ges_track_object_finalize;

  /**
   * GESTrackObject:start
   *
   * The position of the object in the container #GESTrack (in nanoseconds).
   */
  g_object_class_install_property (object_class, PROP_START,
      g_param_spec_uint64 ("start", "Start",
          "The position in the container", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE));

  /**
   * GESTrackObject:in-point
   *
   * The in-point at which this #GESTrackObject will start outputting data
   * from its contents (in nanoseconds).
   *
   * Ex : an in-point of 5 seconds means that the first outputted buffer will
   * be the one located 5 seconds in the controlled resource.
   */
  g_object_class_install_property (object_class, PROP_INPOINT,
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
          G_MAXUINT64, 0, G_PARAM_READWRITE));

  /**
   * GESTrackObject:duration
   *
   * The duration (in nanoseconds) which will be used in the container #GESTrack
   * starting from 'in-point'.
   *
   */
  g_object_class_install_property (object_class, PROP_DURATION,
      g_param_spec_uint64 ("duration", "Duration", "The duration to use",
          0, G_MAXUINT64, GST_SECOND, G_PARAM_READWRITE));

  /**
   * GESTrackObject:priority
   *
   * The priority of the object within the containing #GESTrack.
   * If two objects intersect over the same region of time, the @priority
   * property is used to decide which one takes precedence.
   *
   * The highest priority (that supercedes everything) is 0, and then lowering
   * priorities go in increasing numerical value (with #G_MAXUINT64 being the
   * lowest priority).
   */
  g_object_class_install_property (object_class, PROP_PRIORITY,
      g_param_spec_uint ("priority", "Priority",
          "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE));

  /**
   * GESTrackObject:priority-offset
   *
   * The priority of the object relative to its parent #GESTimelineObject.
   */
  g_object_class_install_property (object_class, PROP_PRIORITY_OFFSET,
      g_param_spec_uint ("priority-offset", "Priority Offset",
          "An offset from the base priority", 0, G_MAXUINT, 0,
          G_PARAM_READWRITE));

  /**
   * GESTrackObject:active
   *
   * Whether the object should be taken into account in the #GESTrack output.
   * If #FALSE, then its contents will not be used in the resulting track.
   */
  g_object_class_install_property (object_class, PROP_ACTIVE,
      g_param_spec_boolean ("active", "Active", "Use object in output",
          TRUE, G_PARAM_READWRITE));

  klass->create_gnl_object = ges_track_object_create_gnl_object_func;
}

static void
ges_track_object_init (GESTrackObject * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_OBJECT, GESTrackObjectPrivate);

  /* Sane default values */
  self->priv->pending_start = 0;
  self->priv->pending_inpoint = 0;
  self->priv->pending_duration = GST_SECOND;
  self->priv->pending_gnl_priority = 1;
  self->priv->pending_active = TRUE;
}

gboolean
ges_track_object_set_start_internal (GESTrackObject * object, guint64 start)
{
  GST_DEBUG ("object:%p, start:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (start));

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (start == object->start))
      return FALSE;

    g_object_set (object->priv->gnlobject, "start", start, NULL);
  } else
    object->priv->pending_start = start;
  return TRUE;
};

gboolean
ges_track_object_set_inpoint_internal (GESTrackObject * object, guint64 inpoint)
{

  GST_DEBUG ("object:%p, inpoint:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (inpoint));

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (inpoint == object->inpoint))
      return FALSE;

    g_object_set (object->priv->gnlobject, "media-start", inpoint, NULL);
  } else
    object->priv->pending_inpoint = inpoint;

  return TRUE;
}

gboolean
ges_track_object_set_duration_internal (GESTrackObject * object,
    guint64 duration)
{
  GST_DEBUG ("object:%p, duration:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (duration));

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (duration == object->duration))
      return FALSE;

    g_object_set (object->priv->gnlobject, "duration", duration,
        "media-duration", duration, NULL);
  } else
    object->priv->pending_duration = duration;
  return TRUE;
}

/* NOTE: we handle priority differently than other properties! the gnlpriority
 * is object->base_priority + object->priority_offset! A change to either one
 * will trigger an update to the gnonlin priority and a subsequent property
 * notification.
 */

gboolean
ges_track_object_set_priority_internal (GESTrackObject * object,
    guint32 priority)
{
  guint32 save;
  save = object->priv->base_priority;
  GST_DEBUG ("object:%p, priority:%d", object, priority);

  object->priv->base_priority = priority;
  if (!ges_track_object_update_priority (object)) {
    object->priv->base_priority = save;
    return FALSE;
  }
  return TRUE;
}

gboolean
ges_track_object_set_priority_offset_internal (GESTrackObject * object,
    guint32 priority_offset)
{
  guint32 save;
  save = object->priv->priority_offset;
  GST_DEBUG ("object:%p, offset:%d", object, priority_offset);

  object->priv->priority_offset = priority_offset;
  if (!ges_track_object_update_priority (object)) {
    object->priv->base_priority = save;
    return FALSE;
  }
  return TRUE;
}

static gboolean
ges_track_object_update_priority (GESTrackObject * object)
{
  guint32 priority, offset, gnl;

  priority = object->priv->base_priority;
  offset = object->priv->priority_offset;
  gnl = priority + offset;
  GST_DEBUG ("object:%p, base:%d, offset:%d: gnl:%d", object, priority, offset,
      gnl);

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (gnl == object->gnl_priority))
      return FALSE;

    g_object_set (object->priv->gnlobject, "priority", gnl, NULL);
  } else
    object->priv->pending_gnl_priority = gnl;
  return TRUE;
}

/**
 * ges_track_object_set_active:
 * @object: a #GESTrackObject
 * @active: visibility
 *
 * Sets the usage of the @object. If @active is %TRUE, the object will be used for
 * playback and rendering, else it will be ignored.
 *
 * Returns: %TRUE if the property was toggled, else %FALSE
 */
gboolean
ges_track_object_set_active (GESTrackObject * object, gboolean active)
{
  GST_DEBUG ("object:%p, active:%d", object, active);

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (active == object->active))
      return FALSE;

    g_object_set (object->priv->gnlobject, "active", active, NULL);
  } else
    object->priv->pending_active = active;
  return TRUE;
}

/* Callbacks from the GNonLin object */
void
gnlobject_start_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint64 start;
  GESTrackObjectClass *klass;

  klass = GES_TRACK_OBJECT_GET_CLASS (obj);

  g_object_get (gnlobject, "start", &start, NULL);

  GST_DEBUG ("gnlobject start : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (obj->start));

  if (start != obj->start) {
    obj->start = start;
    if (klass->start_changed)
      klass->start_changed (obj, start);
    /* FIXME : emit changed */
  }
}

/* Callbacks from the GNonLin object */
void
gnlobject_media_start_cb (GstElement * gnlobject,
    GParamSpec * arg G_GNUC_UNUSED, GESTrackObject * obj)
{
  guint64 start;
  GESTrackObjectClass *klass;

  klass = GES_TRACK_OBJECT_GET_CLASS (obj);

  g_object_get (gnlobject, "media-start", &start, NULL);

  GST_DEBUG ("gnlobject in-point : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (obj->inpoint));

  if (start != obj->inpoint) {
    obj->inpoint = start;
    if (klass->media_start_changed)
      klass->media_start_changed (obj, start);
    /* FIXME : emit changed */
  }
}

void
gnlobject_priority_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint32 priority;
  GESTrackObjectClass *klass;

  klass = GES_TRACK_OBJECT_GET_CLASS (obj);

  g_object_get (gnlobject, "priority", &priority, NULL);

  GST_DEBUG ("gnlobject priority : %d current : %d", priority,
      obj->gnl_priority);

  if (priority != obj->gnl_priority) {
    obj->gnl_priority = priority;
    if (klass->gnl_priority_changed)
      klass->gnl_priority_changed (obj, priority);
    /* FIXME : emit changed */
  }
}

void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint64 duration;
  GESTrackObjectClass *klass;

  klass = GES_TRACK_OBJECT_GET_CLASS (obj);

  g_object_get (gnlobject, "duration", &duration, NULL);

  GST_DEBUG ("gnlobject duration : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (duration), GST_TIME_ARGS (obj->duration));

  if (duration != obj->duration) {
    obj->duration = duration;
    if (klass->duration_changed)
      klass->duration_changed (obj, duration);
    /* FIXME : emit changed */
  }
}

void
gnlobject_active_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  gboolean active;
  GESTrackObjectClass *klass;

  klass = GES_TRACK_OBJECT_GET_CLASS (obj);

  g_object_get (gnlobject, "active", &active, NULL);

  GST_DEBUG ("gnlobject active : %d current : %d", active, obj->active);

  if (active != obj->active) {
    obj->active = active;
    if (klass->active_changed)
      klass->active_changed (obj, active);
    /* FIXME : emit changed */
  }
}


/* default 'create_gnl_object' virtual method implementation */
static GstElement *
ges_track_object_create_gnl_object_func (GESTrackObject * self)
{
  GESTrackObjectClass *klass = NULL;
  GstElement *child = NULL;
  GstElement *gnlobject;

  klass = GES_TRACK_OBJECT_GET_CLASS (self);

  if (G_UNLIKELY (self->priv->gnlobject != NULL))
    goto already_have_gnlobject;

  if (G_UNLIKELY (klass->gnlobject_factorytype == NULL))
    goto no_gnlfactory;

  GST_DEBUG ("Creating a supporting gnlobject of type '%s'",
      klass->gnlobject_factorytype);

  gnlobject = gst_element_factory_make (klass->gnlobject_factorytype, NULL);

  if (G_UNLIKELY (gnlobject == NULL))
    goto no_gnlobject;

  if (klass->create_element) {
    GST_DEBUG ("Calling subclass 'create_element' vmethod");
    child = klass->create_element (self);

    if (G_UNLIKELY (!child))
      goto child_failure;

    if (!gst_bin_add (GST_BIN (gnlobject), child))
      goto add_failure;

    GST_DEBUG ("Succesfully got the element to put in the gnlobject");
    self->priv->element = child;
  }

  GST_DEBUG ("done");
  return gnlobject;


  /* ERROR CASES */

already_have_gnlobject:
  {
    GST_ERROR ("Already controlling a GnlObject %s",
        GST_ELEMENT_NAME (self->priv->gnlobject));
    return NULL;
  }

no_gnlfactory:
  {
    GST_ERROR ("No GESTrackObject::gnlobject_factorytype implementation!");
    return NULL;
  }

no_gnlobject:
  {
    GST_ERROR ("Error creating a gnlobject of type '%s'",
        klass->gnlobject_factorytype);
    return NULL;
  }

child_failure:
  {
    GST_ERROR ("create_element returned NULL");
    gst_object_unref (gnlobject);
    return NULL;
  }

add_failure:
  {
    GST_ERROR ("Error adding the contents to the gnlobject");
    gst_object_unref (child);
    gst_object_unref (gnlobject);
    return NULL;
  }
}

static gboolean
ensure_gnl_object (GESTrackObject * object)
{
  GESTrackObjectClass *class;
  GstElement *gnlobject;
  gboolean res = TRUE;

  if (object->priv->gnlobject && object->priv->valid)
    return FALSE;

  /* 1. Create the GnlObject */
  GST_DEBUG ("Creating GnlObject");

  class = GES_TRACK_OBJECT_GET_CLASS (object);

  if (G_UNLIKELY (class->create_gnl_object == NULL)) {
    GST_ERROR ("No 'create_gnl_object' implementation !");
    return FALSE;
  }

  GST_DEBUG ("Calling virtual method");

  /* call the create_gnl_object virtual method */
  gnlobject = class->create_gnl_object (object);

  if (G_UNLIKELY (gnlobject == NULL)) {
    GST_ERROR
        ("'create_gnl_object' implementation returned TRUE but no GnlObject is available");
    return FALSE;
  }

  object->priv->gnlobject = gnlobject;

  /* Connect to property notifications */
  g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::start",
      G_CALLBACK (gnlobject_start_cb), object);
  g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::media-start",
      G_CALLBACK (gnlobject_media_start_cb), object);
  g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::duration",
      G_CALLBACK (gnlobject_duration_cb), object);
  g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::priority",
      G_CALLBACK (gnlobject_priority_cb), object);
  g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::active",
      G_CALLBACK (gnlobject_active_cb), object);

  /* 2. Fill in the GnlObject */
  if (gnlobject) {
    GST_DEBUG ("Got a valid GnlObject, now filling it in");

    res =
        ges_timeline_object_fill_track_object (object->priv->timelineobj,
        object, object->priv->gnlobject);
    if (res) {
      /* Set some properties on the GnlObject */
      g_object_set (object->priv->gnlobject,
          "caps", ges_track_get_caps (object->priv->track),
          "duration", object->priv->pending_duration,
          "media-duration", object->priv->pending_duration,
          "start", object->priv->pending_start,
          "media-start", object->priv->pending_inpoint,
          "priority", object->priv->pending_gnl_priority,
          "active", object->priv->pending_active, NULL);

    }
  }

  object->priv->valid = res;

  GST_DEBUG ("Returning res:%d", res);

  return res;
}

/* INTERNAL USAGE */
gboolean
ges_track_object_set_track (GESTrackObject * object, GESTrack * track)
{
  GST_DEBUG ("object:%p, track:%p", object, track);

  object->priv->track = track;

  if (object->priv->track)
    return ensure_gnl_object (object);

  return TRUE;
}

/**
 * ges_track_object_get_track:
 * @object: a #GESTrackObject
 *
 * Returns: (transfer none): The #GESTrack to which this object belongs. Can be %NULL if it
 * is not in any track
 */
GESTrack *
ges_track_object_get_track (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), NULL);

  return object->priv->track;
}


void
ges_track_object_set_timeline_object (GESTrackObject * object,
    GESTimelineObject * tlobj)
{
  GST_DEBUG ("object:%p, timeline-object:%p", object, tlobj);

  object->priv->timelineobj = tlobj;
}

/**
 * ges_track_object_get_timeline_object:
 * @object: a #GESTrackObject
 *
 * Returns: (transfer none): the #GESTimelineObject which is controlling
 * this track object
 */
GESTimelineObject *
ges_track_object_get_timeline_object (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), NULL);

  return object->priv->timelineobj;
}

guint32
ges_track_object_get_priority_offset (GESTrackObject * object)
{
  return object->priv->priority_offset;
}

/**
 * ges_track_object_get_gnlobject:
 * @object: a #GESTrackObject
 *
 * Returns: (transfer none): the GNonLin object this object is controlling.
 */
GstElement *
ges_track_object_get_gnlobject (GESTrackObject * object)
{
  return object->priv->gnlobject;
}

/**
 * ges_track_object_get_element:
 * @object: a #GESTrackObject
 *
 * Returns: (transfer none): the #GstElement this track object is controlling
 * within GNonLin.
 */
GstElement *
ges_track_object_get_element (GESTrackObject * object)
{
  return object->priv->element;
}
