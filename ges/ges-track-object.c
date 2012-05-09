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
#include "gesmarshal.h"
#include "ges-track-object.h"
#include "ges-timeline-object.h"
#include <gobject/gvaluecollector.h>

G_DEFINE_ABSTRACT_TYPE (GESTrackObject, ges_track_object,
    G_TYPE_INITIALLY_UNOWNED);

struct _GESTrackObjectPrivate
{
  /* These fields are only used before the gnlobject is available */
  guint64 pending_start;
  guint64 pending_inpoint;
  guint64 pending_duration;
  guint32 pending_priority;
  gboolean pending_active;

  GstElement *gnlobject;        /* The GnlObject */
  GstElement *element;          /* The element contained in the gnlobject (can be NULL) */

  /* We keep a link between properties name and elements internally
   * The hashtable should look like
   * {GParamaSpec ---> element,}*/
  GHashTable *properties_hashtable;

  GESTimelineObject *timelineobj;
  GESTrack *track;

  gboolean valid;

  guint64 maxduration;

  gboolean locked;              /* If TRUE, then moves in sync with its controlling
                                 * GESTimelineObject */
};

enum
{
  PROP_0,
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_PRIORITY,
  PROP_ACTIVE,
  PROP_LOCKED,
  PROP_MAX_DURATION,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum
{
  DEEP_NOTIFY,
  LAST_SIGNAL
};

static guint ges_track_object_signals[LAST_SIGNAL] = { 0 };

static GstElement *ges_track_object_create_gnl_object_func (GESTrackObject *
    object);

static void gnlobject_start_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

static void gnlobject_media_start_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

static void gnlobject_priority_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

static void gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

static void gnlobject_active_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

static void connect_properties_signals (GESTrackObject * object);
static void connect_signal (gpointer key, gpointer value, gpointer user_data);
static void gst_element_prop_changed_cb (GstElement * element, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj);

static inline gboolean
ges_track_object_set_start_internal (GESTrackObject * object, guint64 start);
static inline gboolean
ges_track_object_set_inpoint_internal (GESTrackObject * object,
    guint64 inpoint);
static inline gboolean ges_track_object_set_duration_internal (GESTrackObject *
    object, guint64 duration);
static inline gboolean ges_track_object_set_priority_internal (GESTrackObject *
    object, guint32 priority);
static inline void
ges_track_object_set_locked_internal (GESTrackObject * object, gboolean locked);

static GParamSpec **default_list_children_properties (GESTrackObject * object,
    guint * n_properties);

static void
ges_track_object_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrackObject *tobj = GES_TRACK_OBJECT (object);

  switch (property_id) {
    case PROP_START:
      g_value_set_uint64 (value, ges_track_object_get_start (tobj));
      break;
    case PROP_INPOINT:
      g_value_set_uint64 (value, ges_track_object_get_inpoint (tobj));
      break;
    case PROP_DURATION:
      g_value_set_uint64 (value, ges_track_object_get_duration (tobj));
      break;
    case PROP_PRIORITY:
      g_value_set_uint (value, ges_track_object_get_priority (tobj));
      break;
    case PROP_ACTIVE:
      g_value_set_boolean (value, ges_track_object_is_active (tobj));
      break;
    case PROP_LOCKED:
      g_value_set_boolean (value, ges_track_object_is_locked (tobj));
      break;
    case PROP_MAX_DURATION:
      g_value_set_uint64 (value, tobj->priv->maxduration);
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
    case PROP_LOCKED:
      ges_track_object_set_locked_internal (tobj, g_value_get_boolean (value));
      break;
    case PROP_MAX_DURATION:
      ges_track_object_set_max_duration (tobj, g_value_get_uint64 (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_object_dispose (GObject * object)
{
  GESTrackObjectPrivate *priv = GES_TRACK_OBJECT (object)->priv;

  if (priv->properties_hashtable)
    g_hash_table_destroy (priv->properties_hashtable);

  if (priv->gnlobject) {
    GstState cstate;

    if (priv->track != NULL) {
      GST_ERROR_OBJECT (object, "Still in %p, this means that you forgot"
          " to remove it from the GESTrack it is contained in. You always need"
          " to remove a GESTrackObject from its track before dropping the last"
          " reference\n"
          "This problem may also be caused by a refcounting bug in"
          " the application or GES itself.", priv->track);
      gst_element_get_state (priv->gnlobject, &cstate, NULL, 0);
      if (cstate != GST_STATE_NULL)
        gst_element_set_state (priv->gnlobject, GST_STATE_NULL);
    }

    gst_object_unref (priv->gnlobject);
    priv->gnlobject = NULL;
  }

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
  properties[PROP_START] = g_param_spec_uint64 ("start", "Start",
      "The position in the container", 0, G_MAXUINT64, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_START,
      properties[PROP_START]);

  /**
   * GESTrackObject:in-point
   *
   * The in-point at which this #GESTrackObject will start outputting data
   * from its contents (in nanoseconds).
   *
   * Ex: an in-point of 5 seconds means that the first outputted buffer will
   * be the one located 5 seconds in the controlled resource.
   */
  properties[PROP_INPOINT] =
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_INPOINT,
      properties[PROP_INPOINT]);

  /**
   * GESTrackObject:duration
   *
   * The duration (in nanoseconds) which will be used in the container #GESTrack
   * starting from 'in-point'.
   *
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The duration to use", 0,
      G_MAXUINT64, GST_SECOND, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_DURATION,
      properties[PROP_DURATION]);

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
  properties[PROP_PRIORITY] = g_param_spec_uint ("priority", "Priority",
      "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_PRIORITY,
      properties[PROP_PRIORITY]);

  /**
   * GESTrackObject:active
   *
   * Whether the object should be taken into account in the #GESTrack output.
   * If #FALSE, then its contents will not be used in the resulting track.
   */
  properties[PROP_ACTIVE] =
      g_param_spec_boolean ("active", "Active", "Use object in output", TRUE,
      G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_ACTIVE,
      properties[PROP_ACTIVE]);

  /**
   * GESTrackObject:locked
   *
   * If %TRUE, then moves in sync with its controlling #GESTimelineObject
   */
  properties[PROP_LOCKED] =
      g_param_spec_boolean ("locked", "Locked",
      "Moves in sync with its controling TimelineObject", TRUE,
      G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_LOCKED,
      properties[PROP_LOCKED]);

  /**
   * GESTrackObject:max-duration:
   *
   * The maximum duration (in nanoseconds) of the #GESTrackObject.
   *
   * Since: 0.10.XX
   */
  g_object_class_install_property (object_class, PROP_MAX_DURATION,
      g_param_spec_uint64 ("max-duration", "Maximum duration",
          "The maximum duration of the object", 0, G_MAXUINT64, G_MAXUINT64,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  /**
   * GESTrackObject::deep-notify:
   * @track_object: a #GESTrackObject
   * @prop_object: the object that originated the signal
   * @prop: the property that changed
   *
   * The deep notify signal is used to be notified of property changes of all
   * the childs of @track_object
   *
   * Since: 0.10.2
   */
  ges_track_object_signals[DEEP_NOTIFY] =
      g_signal_new ("deep-notify", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED |
      G_SIGNAL_NO_HOOKS, 0, NULL, NULL, gst_marshal_VOID__OBJECT_PARAM,
      G_TYPE_NONE, 2, GST_TYPE_ELEMENT, G_TYPE_PARAM);

  klass->create_gnl_object = ges_track_object_create_gnl_object_func;
  /*  There is no 'get_props_hashtable' default implementation */
  klass->get_props_hastable = NULL;
  klass->list_children_properties = default_list_children_properties;
}

static void
ges_track_object_init (GESTrackObject * self)
{
  GESTrackObjectPrivate *priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_OBJECT, GESTrackObjectPrivate);

  /* Sane default values */
  priv->pending_start = 0;
  priv->pending_inpoint = 0;
  priv->pending_duration = GST_SECOND;
  priv->pending_priority = 1;
  priv->pending_active = TRUE;
  priv->locked = TRUE;
  priv->properties_hashtable = NULL;
  priv->maxduration = GST_CLOCK_TIME_NONE;
}

static inline gboolean
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

/**
 * ges_track_object_set_start:
 * @object: a #GESTrackObject
 * @start: the start position (in #GstClockTime)
 *
 * Sets the position of the object in the container #GESTrack.
 */
void
ges_track_object_set_start (GESTrackObject * object, guint64 start)
{
  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  if (ges_track_object_set_start_internal (object, start))
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_START]);
}

static inline gboolean
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

/**
 * ges_track_object_set_inpoint:
 * @object: a #GESTrackObject
 * @inpoint: the in-point (in #GstClockTime)
 *
 * Set the offset within the contents of this #GESTrackObject
 */
void
ges_track_object_set_inpoint (GESTrackObject * object, guint64 inpoint)
{
  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  if (ges_track_object_set_inpoint_internal (object, inpoint))
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_INPOINT]);
}

static inline gboolean
ges_track_object_set_duration_internal (GESTrackObject * object,
    guint64 duration)
{
  GESTrackObjectPrivate *priv = object->priv;

  GST_DEBUG ("object:%p, duration:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (duration));

  if (GST_CLOCK_TIME_IS_VALID (priv->maxduration) &&
      duration > object->inpoint + priv->maxduration)
    duration = priv->maxduration - object->inpoint;

  if (priv->gnlobject != NULL) {
    if (G_UNLIKELY (duration == object->duration))
      return FALSE;

    g_object_set (priv->gnlobject, "duration", duration,
        "media-duration", duration, NULL);
  } else
    priv->pending_duration = duration;

  return TRUE;
}

/**
 * ges_track_object_set_duration:
 * @object: a #GESTrackObject
 * @duration: the duration (in #GstClockTime)
 *
 * Set the duration which will be used in the container #GESTrack
 * starting from the 'in-point'
 */
void
ges_track_object_set_duration (GESTrackObject * object, guint64 duration)
{
  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  if (ges_track_object_set_duration_internal (object, duration))
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_DURATION]);
}

static inline gboolean
ges_track_object_set_priority_internal (GESTrackObject * object,
    guint32 priority)
{
  GST_DEBUG ("object:%p, priority:%" G_GUINT32_FORMAT, object, priority);

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (priority == object->priority))
      return FALSE;

    g_object_set (object->priv->gnlobject, "priority", priority, NULL);
  } else
    object->priv->pending_priority = priority;
  return TRUE;
}

/**
 * ges_track_object_set_priority:
 * @object: a #GESTrackObject
 * @priority: the priority
 *
 * Sets the priority of the object withing the containing #GESTrack.
 * If two objects intersect over the same region of time, the priority
 * property is used to decide which one takes precedence.
 *
 * The highest priority (that supercedes everything) is 0, and then
 * lowering priorities go in increasing numerical value (with G_MAXUINT32
 * being the lowest priority).
 */
void
ges_track_object_set_priority (GESTrackObject * object, guint32 priority)
{
  if (ges_track_object_set_priority_internal (object, priority))
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_PRIORITY]);
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
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), FALSE);

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
static void
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
  }
}

static void
gst_element_prop_changed_cb (GstElement * element, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackObject * obj)
{
  g_signal_emit (obj, ges_track_object_signals[DEEP_NOTIFY], 0,
      GST_ELEMENT (element), arg);
}

static void
connect_signal (gpointer key, gpointer value, gpointer user_data)
{
  gchar *signame = g_strconcat ("notify::", G_PARAM_SPEC (key)->name, NULL);

  g_signal_connect (G_OBJECT (value),
      signame, G_CALLBACK (gst_element_prop_changed_cb),
      GES_TRACK_OBJECT (user_data));

  g_free (signame);
}

static void
connect_properties_signals (GESTrackObject * object)
{
  if (G_UNLIKELY (!object->priv->properties_hashtable)) {
    GST_WARNING ("The properties_hashtable hasn't been set");
    return;
  }

  g_hash_table_foreach (object->priv->properties_hashtable,
      (GHFunc) connect_signal, object);

}

/* Callbacks from the GNonLin object */
static void
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
  }
}

static void
gnlobject_priority_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint32 priority;
  GESTrackObjectClass *klass;

  klass = GES_TRACK_OBJECT_GET_CLASS (obj);

  g_object_get (gnlobject, "priority", &priority, NULL);

  GST_DEBUG ("gnlobject priority : %d current : %d", priority, obj->priority);

  if (priority != obj->priority) {
    obj->priority = priority;
    if (klass->gnl_priority_changed)
      klass->gnl_priority_changed (obj, priority);
  }
}

static void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackObject * obj)
{
  guint64 duration;
  GESTrackObjectClass *klass;

  klass = GES_TRACK_OBJECT_GET_CLASS (obj);

  g_object_get (gnlobject, "duration", &duration, NULL);

  GST_DEBUG_OBJECT (gnlobject, "duration : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (duration), GST_TIME_ARGS (obj->duration));

  if (duration != obj->duration) {
    obj->duration = duration;
    if (klass->duration_changed)
      klass->duration_changed (obj, duration);
  }
}

static void
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
  GHashTable *props_hash;
  gboolean res = TRUE;

  if (object->priv->gnlobject && object->priv->valid)
    return FALSE;

  /* 1. Create the GnlObject */
  GST_DEBUG ("Creating GnlObject");

  class = GES_TRACK_OBJECT_GET_CLASS (object);

  if (G_UNLIKELY (class->create_gnl_object == NULL)) {
    GST_ERROR ("No 'create_gnl_object' implementation !");
    goto done;
  }

  GST_DEBUG ("Calling virtual method");

  /* 2. Fill in the GnlObject */
  if (object->priv->gnlobject == NULL) {

    /* call the create_gnl_object virtual method */
    gnlobject = class->create_gnl_object (object);

    if (G_UNLIKELY (gnlobject == NULL)) {
      GST_ERROR
          ("'create_gnl_object' implementation returned TRUE but no GnlObject is available");
      goto done;
    }

    GST_DEBUG_OBJECT (object, "Got a valid GnlObject, now filling it in");

    object->priv->gnlobject = gst_object_ref (gnlobject);

    if (object->priv->timelineobj)
      res = ges_timeline_object_fill_track_object (object->priv->timelineobj,
          object, object->priv->gnlobject);
    else
      res = TRUE;

    if (res) {
      /* Connect to property notifications */
      /* FIXME : remember the signalids so we can remove them later on !!! */
      g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::start",
          G_CALLBACK (gnlobject_start_cb), object);
      g_signal_connect (G_OBJECT (object->priv->gnlobject),
          "notify::media-start", G_CALLBACK (gnlobject_media_start_cb), object);
      g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::duration",
          G_CALLBACK (gnlobject_duration_cb), object);
      g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::priority",
          G_CALLBACK (gnlobject_priority_cb), object);
      g_signal_connect (G_OBJECT (object->priv->gnlobject), "notify::active",
          G_CALLBACK (gnlobject_active_cb), object);

      /* Set some properties on the GnlObject */
      g_object_set (object->priv->gnlobject,
          "duration", object->priv->pending_duration,
          "media-duration", object->priv->pending_duration,
          "start", object->priv->pending_start,
          "media-start", object->priv->pending_inpoint,
          "priority", object->priv->pending_priority,
          "active", object->priv->pending_active, NULL);

      if (object->priv->track != NULL)
        g_object_set (object->priv->gnlobject,
            "caps", ges_track_get_caps (object->priv->track), NULL);

      /*  We feed up the props_hashtable if possible */
      if (class->get_props_hastable) {
        props_hash = class->get_props_hastable (object);

        if (props_hash == NULL) {
          GST_DEBUG ("'get_props_hastable' implementation returned TRUE but no"
              "properties_hashtable is available");
        } else {
          object->priv->properties_hashtable = props_hash;
          connect_properties_signals (object);
        }
      }
    }
  }

done:
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

  if (object->priv->track) {
    /* If we already have a gnlobject, we just set its caps properly */
    if (object->priv->gnlobject) {
      g_object_set (object->priv->gnlobject,
          "caps", ges_track_get_caps (object->priv->track), NULL);
      return TRUE;
    } else {
      return ensure_gnl_object (object);
    }
  }

  return TRUE;
}

/**
 * ges_track_object_get_track:
 * @object: a #GESTrackObject
 *
 * Get the #GESTrack to which this object belongs.
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

/**
 * ges_track_object_set_timeline_object:
 * @object: The #GESTrackObject to set the parent to
 * @tlobject: The #GESTimelineObject, parent of @tlobj or %NULL
 *
 * Set the #GESTimelineObject to which @object belongs.
 */
void
ges_track_object_set_timeline_object (GESTrackObject * object,
    GESTimelineObject * tlobject)
{
  GST_DEBUG ("object:%p, timeline-object:%p", object, tlobject);

  object->priv->timelineobj = tlobject;
}

/**
 * ges_track_object_get_timeline_object:
 * @object: a #GESTrackObject
 *
 * Get the #GESTimelineObject which is controlling this track object
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

/**
 * ges_track_object_get_gnlobject:
 * @object: a #GESTrackObject
 *
 * Get the GNonLin object this object is controlling.
 *
 * Returns: (transfer none): the GNonLin object this object is controlling.
 */
GstElement *
ges_track_object_get_gnlobject (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), NULL);

  return object->priv->gnlobject;
}

/**
 * ges_track_object_get_element:
 * @object: a #GESTrackObject
 *
 * Get the #GstElement this track object is controlling within GNonLin.
 *
 * Returns: (transfer none): the #GstElement this track object is controlling
 * within GNonLin.
 */
GstElement *
ges_track_object_get_element (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), NULL);

  return object->priv->element;
}

static inline void
ges_track_object_set_locked_internal (GESTrackObject * object, gboolean locked)
{
  object->priv->locked = locked;
}

/**
 * ges_track_object_set_locked:
 * @object: a #GESTrackObject
 * @locked: whether the object is lock to its parent
 *
 * Set the locking status of the @object in relationship to its controlling
 * #GESTimelineObject. If @locked is %TRUE, then this object will move synchronously
 * with its controlling #GESTimelineObject.
 */
void
ges_track_object_set_locked (GESTrackObject * object, gboolean locked)
{
  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  GST_DEBUG_OBJECT (object, "%s object", locked ? "Locking" : "Unlocking");

  ges_track_object_set_locked_internal (object, locked);
  g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_LOCKED]);

}

/**
 * ges_track_object_is_locked:
 * @object: a #GESTrackObject
 *
 * Let you know if object us locked or not (moving synchronously).
 *
 * Returns: %TRUE if the object is moving synchronously to its controlling
 * #GESTimelineObject, else %FALSE.
 */
gboolean
ges_track_object_is_locked (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), FALSE);

  return object->priv->locked;
}

/**
 * ges_track_object_get_start:
 * @object: a #GESTrackObject
 *
 * Get the position of the object in the container #GESTrack.
 *
 * Returns: the start position (in #GstClockTime) or #GST_CLOCK_TIME_NONE
 * if something went wrong.
 *
 * Since: 0.10.2
 */
guint64
ges_track_object_get_start (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), GST_CLOCK_TIME_NONE);

  if (G_UNLIKELY (object->priv->gnlobject == NULL))
    return object->priv->pending_start;
  else
    return object->start;
}

/**
 * ges_track_object_get_inpoint:
 * @object: a #GESTrackObject
 *
 * Get the offset within the contents of this #GESTrackObject
 *
 * Returns: the in-point (in #GstClockTime) or #GST_CLOCK_TIME_NONE
 * if something went wrong.
 *
 * Since: 0.10.2
 */
guint64
ges_track_object_get_inpoint (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), GST_CLOCK_TIME_NONE);

  if (G_UNLIKELY (object->priv->gnlobject == NULL))
    return object->priv->pending_inpoint;
  else
    return object->inpoint;
}

/**
 * ges_track_object_get_duration:
 * @object: a #GESTrackObject
 *
 * Get the duration which will be used in the container #GESTrack
 * starting from the 'in-point'
 *
 * Returns: the duration (in #GstClockTime) or #GST_CLOCK_TIME_NONE
 * if something went wrong.
 *
 * Since: 0.10.2
 */
guint64
ges_track_object_get_duration (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), GST_CLOCK_TIME_NONE);

  if (G_UNLIKELY (object->priv->gnlobject == NULL))
    return object->priv->pending_duration;
  else
    return object->duration;
}

/**
 * ges_track_object_get_priority:
 * @object: a #GESTrackObject
 *
 * Get the priority of the object withing the containing #GESTrack.
 *
 * Returns: the priority of @object or -1 if something went wrong
 *
 * Since: 0.10.2
 */
guint32
ges_track_object_get_priority (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), -1);

  if (G_UNLIKELY (object->priv->gnlobject == NULL))
    return object->priv->pending_priority;
  else
    return object->priority;
}

/**
 * ges_track_object_is_active:
 * @object: a #GESTrackObject
 *
 * Lets you know if @object will be used for playback and rendering,
 * or not.
 *
 * Returns: %TRUE if @object is active, %FALSE otherwize
 *
 * Since: 0.10.2
 */
gboolean
ges_track_object_is_active (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), FALSE);

  if (G_UNLIKELY (object->priv->gnlobject == NULL))
    return object->priv->pending_active;
  else
    return object->active;
}

/**
 * ges_track_object_lookup_child:
 * @object: object to lookup the property in
 * @prop_name: name of the property to look up. You can specify the name of the
 *     class as such: "ClassName::property-name", to guarantee that you get the
 *     proper GParamSpec in case various GstElement-s contain the same property
 *     name. If you don't do so, you will get the first element found, having
 *     this property and the and the corresponding GParamSpec.
 * @element: (out) (allow-none) (transfer full): pointer to a #GstElement that
 *     takes the real object to set property on
 * @pspec: (out) (allow-none) (transfer full): pointer to take the #GParamSpec
 *     describing the property
 *
 * Looks up which @element and @pspec would be effected by the given @name. If various
 * contained elements have this property name you will get the first one, unless you
 * specify the class name in @name.
 *
 * Returns: TRUE if @element and @pspec could be found. FALSE otherwise. In that
 * case the values for @pspec and @element are not modified. Unref @element after
 * usage.
 *
 * Since: 0.10.2
 */
gboolean
ges_track_object_lookup_child (GESTrackObject * object, const gchar * prop_name,
    GstElement ** element, GParamSpec ** pspec)
{
  GHashTableIter iter;
  gpointer key, value;
  gchar **names, *name, *classename;
  gboolean res;
  GESTrackObjectPrivate *priv;

  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), FALSE);

  priv = object->priv;

  classename = NULL;
  res = FALSE;

  names = g_strsplit (prop_name, "::", 2);
  if (names[1] != NULL) {
    classename = names[0];
    name = names[1];
  } else
    name = names[0];

  g_hash_table_iter_init (&iter, priv->properties_hashtable);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (g_strcmp0 (G_PARAM_SPEC (key)->name, name) == 0) {
      if (classename == NULL ||
          g_strcmp0 (G_OBJECT_TYPE_NAME (G_OBJECT (value)), classename) == 0) {
        GST_DEBUG ("The %s property from %s has been found", name, classename);
        if (element)
          *element = g_object_ref (value);

        *pspec = g_param_spec_ref (key);
        res = TRUE;
        break;
      }
    }
  }
  g_strfreev (names);

  return res;
}

/**
 * ges_track_object_set_child_property_by_pspec:
 * @object: a #GESTrackObject
 * @pspec: The #GParamSpec that specifies the property you want to set
 * @value: the value
 *
 * Sets a property of a child of @object.
 *
 * Since: 0.10.2
 */
void
ges_track_object_set_child_property_by_pspec (GESTrackObject * object,
    GParamSpec * pspec, GValue * value)
{
  GstElement *element;
  GESTrackObjectPrivate *priv;

  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  priv = object->priv;

  if (!priv->properties_hashtable)
    goto prop_hash_not_set;

  element = g_hash_table_lookup (priv->properties_hashtable, pspec);
  if (!element)
    goto not_found;

  g_object_set_property (G_OBJECT (element), pspec->name, value);

  return;

not_found:
  {
    GST_ERROR ("The %s property doesn't exist", pspec->name);
    return;
  }
prop_hash_not_set:
  {
    GST_DEBUG ("The child properties haven't been set on %p", object);
    return;
  }
}

/**
 * ges_track_object_set_child_property_valist:
 * @object: The #GESTrackObject parent object
 * @first_property_name: The name of the first property to set
 * @var_args: value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Sets a property of a child of @object. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 *
 * Since: 0.10.2
 */
void
ges_track_object_set_child_property_valist (GESTrackObject * object,
    const gchar * first_property_name, va_list var_args)
{
  const gchar *name;
  GParamSpec *pspec;
  GstElement *element;

  gchar *error = NULL;
  GValue value = { 0, };

  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  name = first_property_name;

  /* Note: This part is in big part copied from the gst_child_object_set_valist
   * method. */

  /* iterate over pairs */
  while (name) {
    if (!ges_track_object_lookup_child (object, name, &element, &pspec))
      goto not_found;

#if GLIB_CHECK_VERSION(2,23,3)
    G_VALUE_COLLECT_INIT (&value, pspec->value_type, var_args,
        G_VALUE_NOCOPY_CONTENTS, &error);
#else
    g_value_init (&value, G_PARAM_SPEC_VALUE_TYPE (pspec));
    G_VALUE_COLLECT (&value, var_args, G_VALUE_NOCOPY_CONTENTS, &error);
#endif

    if (error)
      goto cant_copy;

    g_object_set_property (G_OBJECT (element), pspec->name, &value);

    g_object_unref (element);
    g_value_unset (&value);

    name = va_arg (var_args, gchar *);
  }
  return;

not_found:
  {
    GST_WARNING ("No property %s in OBJECT\n", name);
    return;
  }
cant_copy:
  {
    GST_WARNING ("error copying value %s in object %p: %s", pspec->name, object,
        error);
    g_value_unset (&value);
    return;
  }
}

/**
 * ges_track_object_set_child_property:
 * @object: The #GESTrackObject parent object
 * @first_property_name: The name of the first property to set
 * @...: value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Sets a property of a child of @object. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 *
 * Since: 0.10.2
 */
void
ges_track_object_set_child_property (GESTrackObject * object,
    const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  va_start (var_args, first_property_name);
  ges_track_object_set_child_property_valist (object, first_property_name,
      var_args);
  va_end (var_args);
}

/**
 * ges_track_object_get_child_property_valist:
 * @object: The #GESTrackObject parent object
 * @first_property_name: The name of the first property to get
 * @var_args: value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Gets a property of a child of @object. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 *
 * Since: 0.10.2
 */
void
ges_track_object_get_child_property_valist (GESTrackObject * object,
    const gchar * first_property_name, va_list var_args)
{
  const gchar *name;
  gchar *error = NULL;
  GValue value = { 0, };
  GParamSpec *pspec;
  GstElement *element;

  g_return_if_fail (G_IS_OBJECT (object));

  name = first_property_name;

  /* This part is in big part copied from the gst_child_object_get_valist method */
  while (name) {
    if (!ges_track_object_lookup_child (object, name, &element, &pspec))
      goto not_found;

    g_value_init (&value, pspec->value_type);
    g_object_get_property (G_OBJECT (element), pspec->name, &value);
    g_object_unref (element);

    G_VALUE_LCOPY (&value, var_args, 0, &error);
    if (error)
      goto cant_copy;
    g_value_unset (&value);
    name = va_arg (var_args, gchar *);
  }
  return;

not_found:
  {
    GST_WARNING ("no property %s in object", name);
    return;
  }
cant_copy:
  {
    GST_WARNING ("error copying value %s in object %p: %s", pspec->name, object,
        error);
    g_value_unset (&value);
    return;
  }
}

/**
 * ges_track_object_list_children_properties:
 * @object: The #GESTrackObject to get the list of children properties from
 * @n_properties: return location for the length of the returned array
 *
 * Gets an array of #GParamSpec* for all configurable properties of the
 * children of @object.
 *
 * Returns: (transfer full) (array): an array of #GParamSpec* which should be freed after use or
 * %NULL if something went wrong
 *
 * Since: 0.10.2
 */
GParamSpec **
ges_track_object_list_children_properties (GESTrackObject * object,
    guint * n_properties)
{
  GESTrackObjectClass *class;

  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), NULL);

  class = GES_TRACK_OBJECT_GET_CLASS (object);

  return class->list_children_properties (object, n_properties);
}

/**
 * ges_track_object_get_child_property:
 * @object: The origin #GESTrackObject
 * @first_property_name: The name of the first property to get
 * @...: return location for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Gets properties of a child of @object.
 *
 * Since: 0.10.2
 */
void
ges_track_object_get_child_property (GESTrackObject * object,
    const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  va_start (var_args, first_property_name);
  ges_track_object_get_child_property_valist (object, first_property_name,
      var_args);
  va_end (var_args);
}

/**
 * ges_track_object_get_child_property_by_pspec:
 * @object: a #GESTrackObject
 * @pspec: The #GParamSpec that specifies the property you want to get
 * @value: return location for the value
 *
 * Gets a property of a child of @object.
 *
 * Since: 0.10.2
 */
void
ges_track_object_get_child_property_by_pspec (GESTrackObject * object,
    GParamSpec * pspec, GValue * value)
{
  GstElement *element;
  GESTrackObjectPrivate *priv;

  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  priv = object->priv;

  if (!priv->properties_hashtable)
    goto prop_hash_not_set;

  element = g_hash_table_lookup (priv->properties_hashtable, pspec);
  if (!element)
    goto not_found;

  g_object_get_property (G_OBJECT (element), pspec->name, value);

  return;

not_found:
  {
    GST_ERROR ("The %s property doesn't exist", pspec->name);
    return;
  }
prop_hash_not_set:
  {
    GST_ERROR ("The child properties haven't been set on %p", object);
    return;
  }
}

static GParamSpec **
default_list_children_properties (GESTrackObject * object, guint * n_properties)
{
  GParamSpec **pspec, *spec;
  GHashTableIter iter;
  gpointer key, value;

  guint i = 0;

  if (!object->priv->properties_hashtable)
    goto prop_hash_not_set;

  *n_properties = g_hash_table_size (object->priv->properties_hashtable);
  pspec = g_new (GParamSpec *, *n_properties);

  g_hash_table_iter_init (&iter, object->priv->properties_hashtable);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    spec = G_PARAM_SPEC (key);
    pspec[i] = g_param_spec_ref (spec);
    i++;
  }

  return pspec;

prop_hash_not_set:
  {
    *n_properties = 0;
    GST_ERROR ("The child properties haven't been set on %p", object);
    return NULL;
  }
}

/**
 * ges_track_object_get_max_duration:
 * @object: The #GESTrackObject to retrieve max duration from
 *
 * Get the max duration of @object.
 *
 * Returns: The max duration of @object
 *
 * Since: 0.10.XX
 */
guint64
ges_track_object_get_max_duration (GESTrackObject * object)
{
  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), 0);

  return object->priv->maxduration;
}

/**
 * ges_track_object_set_max_duration:
 * @object: The #GESTrackObject to retrieve max duration from
 * @maxduration: The maximum duration of @object
 *
 * Returns: Set the max duration of @object
 *
 * Since: 0.10.XX
 */
void
ges_track_object_set_max_duration (GESTrackObject * object, guint64 maxduration)
{
  g_return_if_fail (GES_IS_TRACK_OBJECT (object));

  object->priv->maxduration = maxduration;
}

/**
 * ges_track_object_copy:
 * @object: The #GESTrackObject to copy
 * @deep: whether we want to create the gnlobject and copy it properties
 *
 * Copies @object
 *
 * Returns: (transfer full): The newly create #GESTrackObject, copied from @object
 *
 * Since: 0.10.XX
 */
GESTrackObject *
ges_track_object_copy (GESTrackObject * object, gboolean deep)
{
  GESTrackObject *ret = NULL;
  GParameter *params;
  GParamSpec **specs;
  guint n, n_specs, n_params;
  GValue val = { 0 };

  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), NULL);

  specs =
      g_object_class_list_properties (G_OBJECT_GET_CLASS (object), &n_specs);
  params = g_new0 (GParameter, n_specs);
  n_params = 0;

  for (n = 0; n < n_specs; ++n) {
    if (g_strcmp0 (specs[n]->name, "parent") &&
        (specs[n]->flags & G_PARAM_READWRITE) == G_PARAM_READWRITE) {
      params[n_params].name = g_intern_string (specs[n]->name);
      g_value_init (&params[n_params].value, specs[n]->value_type);
      g_object_get_property (G_OBJECT (object), specs[n]->name,
          &params[n_params].value);
      ++n_params;
    }
  }

  ret = g_object_newv (G_TYPE_FROM_INSTANCE (object), n_params, params);
  g_free (specs);
  g_free (params);
  specs = NULL;
  params = NULL;

  if (deep == FALSE)
    return ret;

  ensure_gnl_object (ret);
  specs = ges_track_object_list_children_properties (object, &n_specs);
  for (n = 0; n < n_specs; ++n) {
    g_value_init (&val, specs[n]->value_type);
    g_object_get_property (G_OBJECT (object), specs[n]->name, &val);
    ges_track_object_set_child_property_by_pspec (ret, specs[n], &val);
    g_value_unset (&val);
  }

  g_free (specs);
  g_free (params);

  return ret;
}

/**
 * ges_track_object_edit:
 * @object: the #GESTrackObject to edit
 * @layers: (element-type GESTimelineLayer): The layers you want the edit to
 *  happen in, %NULL means that the edition is done in all the
 *  #GESTimelineLayers contained in the current timeline.
 *      FIXME: This is not implemented yet.
 * @mode: The #GESEditMode in which the editition will happen.
 * @edge: The #GESEdge the edit should happen on.
 * @position: The position at which to edit @object (in nanosecond)
 *
 * Edit @object in the different exisiting #GESEditMode modes. In the case of
 * slide, and roll, you need to specify a #GESEdge
 *
 * Returns: %TRUE if the object as been edited properly, %FALSE if an error
 * occured
 *
 * Since: 0.10.XX
 */
gboolean
ges_track_object_edit (GESTrackObject * object,
    GList * layers, GESEditMode mode, GESEdge edge, guint64 position)
{
  GESTrack *track = ges_track_object_get_track (object);
  GESTimeline *timeline;

  g_return_val_if_fail (GES_IS_TRACK_OBJECT (object), FALSE);

  if (G_UNLIKELY (!track)) {
    GST_WARNING_OBJECT (object, "Trying to edit in %d mode but not in"
        "any Track yet.", mode);
    return FALSE;
  }

  timeline = GES_TIMELINE (ges_track_get_timeline (track));

  if (G_UNLIKELY (!timeline)) {
    GST_WARNING_OBJECT (object, "Trying to edit in %d mode but not in"
        "track %p no in any timeline yet.", mode, track);
    return FALSE;
  }

  switch (mode) {
    case GES_EDIT_MODE_NORMAL:
      timeline_move_object (timeline, object, layers, edge, position);
      break;
    case GES_EDIT_MODE_TRIM:
      timeline_trim_object (timeline, object, layers, edge, position);
      break;
    case GES_EDIT_MODE_RIPPLE:
      timeline_ripple_object (timeline, object, layers, edge, position);
      break;
    case GES_EDIT_MODE_ROLL:
      timeline_roll_object (timeline, object, layers, edge, position);
      break;
    case GES_EDIT_MODE_SLIDE:
      timeline_slide_object (timeline, object, layers, edge, position);
      break;
    default:
      GST_ERROR ("Unkown edit mode: %d", mode);
      return FALSE;
  }

  return TRUE;
}
