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

G_DEFINE_TYPE_WITH_CODE (GESTrackObject, ges_track_object, G_TYPE_OBJECT,
    _do_init);

enum
{
  PROP_0,
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_PRIORITY,
  PROP_ACTIVE
};

static gboolean
ges_track_object_create_gnl_object_func (GESTrackObject * object);

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
      g_value_set_uint (value, tobj->priority);
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
   * GESTrackObject:active
   *
   * Whether the object should be taken into account in the #GEStrack output.
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
  /* Sane default values */
  self->start = 0;
  self->inpoint = 0;
  self->duration = GST_SECOND;
  self->priority = 1;
}

gboolean
ges_track_object_set_start_internal (GESTrackObject * object, guint64 start)
{
  g_return_val_if_fail (object->gnlobject, FALSE);

  GST_DEBUG ("object:%p, start:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (start));

  if (G_UNLIKELY (start == object->start))
    return FALSE;

  g_object_set (object->gnlobject, "start", start, NULL);
  return TRUE;
};

gboolean
ges_track_object_set_inpoint_internal (GESTrackObject * object, guint64 inpoint)
{

  GST_DEBUG ("object:%p, inpoint:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (inpoint));

  g_return_val_if_fail (object->gnlobject, FALSE);

  if (G_UNLIKELY (inpoint == object->inpoint))
    return FALSE;

  g_object_set (object->gnlobject, "media-start", inpoint, NULL);

  return TRUE;
}

gboolean
ges_track_object_set_duration_internal (GESTrackObject * object,
    guint64 duration)
{
  GST_DEBUG ("object:%p, duration:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (duration));

  g_return_val_if_fail (object->gnlobject, FALSE);

  if (G_UNLIKELY (duration == object->duration))
    return FALSE;

  g_object_set (object->gnlobject, "duration", duration, "media-duration",
      duration, NULL);
  return TRUE;
}

gboolean
ges_track_object_set_priority_internal (GESTrackObject * object,
    guint32 priority)
{
  GST_DEBUG ("object:%p, priority:%d", object, priority);

  g_return_val_if_fail (object->gnlobject, FALSE);

  if (G_UNLIKELY (priority == object->priority))
    return FALSE;

  g_object_set (object->gnlobject, "priority", priority, NULL);
  return TRUE;
}

gboolean
ges_track_object_set_active (GESTrackObject * object, gboolean active)
{
  GST_DEBUG ("object:%p, active:%d", object, active);

  g_return_val_if_fail (object->gnlobject, FALSE);

  if (G_UNLIKELY (active == object->active))
    return FALSE;

  g_object_set (object->gnlobject, "active", active, NULL);
  return TRUE;
}

/* Callbacks from the GNonLin object */
void
gnlobject_start_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint64 start;
  g_object_get (gnlobject, "start", &start, NULL);

  GST_DEBUG ("gnlobject start : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (obj->start));

  if (start != obj->start) {
    obj->start = start;
    /* FIXME : emit changed */
  }
}

/* Callbacks from the GNonLin object */
void
gnlobject_media_start_cb (GstElement * gnlobject,
    GParamSpec * arg G_GNUC_UNUSED, GESTrackObject * obj)
{
  guint64 start;
  g_object_get (gnlobject, "media-start", &start, NULL);

  GST_DEBUG ("gnlobject in-point : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (obj->inpoint));

  if (start != obj->inpoint) {
    obj->inpoint = start;
    /* FIXME : emit changed */
  }
}

void
gnlobject_priority_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint32 priority;
  g_object_get (gnlobject, "priority", &priority, NULL);

  GST_DEBUG ("gnlobject priority : %d current : %d", priority, obj->priority);

  if (priority != obj->priority) {
    obj->priority = priority;
    /* FIXME : emit changed */
  }
}

void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint64 duration;
  g_object_get (gnlobject, "duration", &duration, NULL);

  GST_DEBUG ("gnlobject duration : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (duration), GST_TIME_ARGS (obj->duration));

  if (duration != obj->duration) {
    obj->duration = duration;
    /* FIXME : emit changed */
  }
}

void
gnlobject_active_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  gboolean active;
  g_object_get (gnlobject, "active", &active, NULL);

  GST_DEBUG ("gnlobject active : %d current : %d", active, obj->active);

  if (active != obj->active) {
    obj->active = active;
    /* FIXME : emit changed */
  }
}


/* default 'create_gnl_object' virtual method implementation */
static gboolean
ges_track_object_create_gnl_object_func (GESTrackObject * object)
{

  return FALSE;
}

static gboolean
ensure_gnl_object (GESTrackObject * object)
{
  GESTrackObjectClass *class;
  gboolean res;

  if (object->gnlobject && object->valid)
    return TRUE;

  /* 1. Create the GnlObject */
  GST_DEBUG ("Creating GnlObject");

  class = GES_TRACK_OBJECT_GET_CLASS (object);

  if (G_UNLIKELY (class->create_gnl_object == NULL)) {
    GST_ERROR ("No 'create_gnl_object' implementation !");
    return FALSE;
  }

  GST_DEBUG ("Calling virtual method");

  /* call the create_gnl_object virtual method */
  res = class->create_gnl_object (object);

  if (G_UNLIKELY (res && (object->gnlobject == NULL))) {
    GST_ERROR
        ("'create_gnl_object' implementation returned TRUE but no GnlObject is available");
    return FALSE;
  }

  /* Connect to property notifications */
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::start",
      G_CALLBACK (gnlobject_start_cb), object);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::media-start",
      G_CALLBACK (gnlobject_media_start_cb), object);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::duration",
      G_CALLBACK (gnlobject_duration_cb), object);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::priority",
      G_CALLBACK (gnlobject_priority_cb), object);
  g_signal_connect (G_OBJECT (object->gnlobject), "notify::active",
      G_CALLBACK (gnlobject_active_cb), object);

  /* 2. Fill in the GnlObject */
  if (res) {
    GST_DEBUG ("Got a valid GnlObject, now filling it in");

    res =
        ges_timeline_object_fill_track_object (object->timelineobj, object,
        object->gnlobject);
    if (res) {
      /* Set some properties on the GnlObject */
      g_object_set (object->gnlobject, "caps", object->track->caps, NULL);
    }
  }

  object->valid = res;

  GST_DEBUG ("Returning res:%d", res);

  return res;
}

gboolean
ges_track_object_set_track (GESTrackObject * object, GESTrack * track)
{
  GST_DEBUG ("object:%p, track:%p", object, track);

  object->track = track;

  if (object->track)
    return ensure_gnl_object (object);

  return TRUE;
}

void
ges_track_object_set_timeline_object (GESTrackObject * object,
    GESTimelineObject * tlobj)
{
  GST_DEBUG ("object:%p, timeline-object:%p", object, tlobj);

  object->timelineobj = tlobj;
}
