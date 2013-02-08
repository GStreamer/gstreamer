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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:ges-track-element
 * @short_description: Base Class for objects contained in a GESTrack
 *
 * #GESTrackElement is the Base Class for any object that can be contained in a
 * #GESTrack.
 *
 * It contains the basic information as to the location of the object within
 * its container, like the start position, the inpoint, the duration and the
 * priority.
 */

#include "ges-internal.h"
#include "ges-extractable.h"
#include "ges-track-element.h"
#include "ges-clip.h"
#include "ges-meta-container.h"
#include <gobject/gvaluecollector.h>

G_DEFINE_ABSTRACT_TYPE (GESTrackElement, ges_track_element,
    GES_TYPE_TIMELINE_ELEMENT);

struct _GESTrackElementPrivate
{
  GESTrackType track_type;

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

  GESClip *timelineobj;
  GESTrack *track;

  gboolean valid;

  gboolean locked;              /* If TRUE, then moves in sync with its controlling
                                 * GESClip */
};

enum
{
  PROP_0,
  PROP_ACTIVE,
  PROP_LOCKED,
  PROP_TRACK_TYPE,
  PROP_TRACK,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum
{
  DEEP_NOTIFY,
  LAST_SIGNAL
};

static guint ges_track_element_signals[LAST_SIGNAL] = { 0 };

static GstElement *ges_track_element_create_gnl_object_func (GESTrackElement *
    object);

static void gnlobject_start_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackElement * obj);

static void gnlobject_media_start_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackElement * obj);

static void gnlobject_priority_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackElement * obj);

static void gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackElement * obj);

static void gnlobject_active_cb (GstElement * gnlobject, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackElement * obj);

static void connect_properties_signals (GESTrackElement * object);
static void connect_signal (gpointer key, gpointer value, gpointer user_data);
static void gst_element_prop_changed_cb (GstElement * element, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackElement * obj);

static gboolean _set_start (GESTimelineElement * element, GstClockTime start);
static gboolean _set_inpoint (GESTimelineElement * element,
    GstClockTime inpoint);
static gboolean _set_duration (GESTimelineElement * element,
    GstClockTime duration);
static gboolean _set_priority (GESTimelineElement * element, guint32 priority);
static void _deep_copy (GESTimelineElement * element,
    GESTimelineElement * copy);

static inline void
ges_track_element_set_locked_internal (GESTrackElement * object,
    gboolean locked);

static GParamSpec **default_list_children_properties (GESTrackElement * object,
    guint * n_properties);

static void
ges_track_element_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrackElement *tobj = GES_TRACK_ELEMENT (object);

  switch (property_id) {
    case PROP_ACTIVE:
      g_value_set_boolean (value, ges_track_element_is_active (tobj));
      break;
    case PROP_LOCKED:
      g_value_set_boolean (value, ges_track_element_is_locked (tobj));
      break;
    case PROP_TRACK_TYPE:
      g_value_set_flags (value, tobj->priv->track_type);
      break;
    case PROP_TRACK:
      g_value_set_object (value, tobj->priv->track);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_element_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTrackElement *tobj = GES_TRACK_ELEMENT (object);

  switch (property_id) {
    case PROP_ACTIVE:
      ges_track_element_set_active (tobj, g_value_get_boolean (value));
      break;
    case PROP_LOCKED:
      ges_track_element_set_locked_internal (tobj, g_value_get_boolean (value));
      break;
    case PROP_TRACK_TYPE:
      tobj->priv->track_type = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_element_dispose (GObject * object)
{
  GESTrackElementPrivate *priv = GES_TRACK_ELEMENT (object)->priv;

  if (priv->properties_hashtable)
    g_hash_table_destroy (priv->properties_hashtable);

  if (priv->gnlobject) {
    GstState cstate;

    if (priv->track != NULL) {
      GST_ERROR_OBJECT (object, "Still in %p, this means that you forgot"
          " to remove it from the GESTrack it is contained in. You always need"
          " to remove a GESTrackElement from its track before dropping the last"
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

  G_OBJECT_CLASS (ges_track_element_parent_class)->dispose (object);
}

static void
ges_track_element_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_element_parent_class)->finalize (object);
}

static void
ges_track_element_class_init (GESTrackElementClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTrackElementPrivate));

  object_class->get_property = ges_track_element_get_property;
  object_class->set_property = ges_track_element_set_property;
  object_class->dispose = ges_track_element_dispose;
  object_class->finalize = ges_track_element_finalize;


  /**
   * GESTrackElement:active:
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
   * GESTrackElement:locked:
   *
   * If %TRUE, then moves in sync with its controlling #GESClip
   */
  properties[PROP_LOCKED] =
      g_param_spec_boolean ("locked", "Locked",
      "Moves in sync with its controling Clip", TRUE, G_PARAM_READWRITE);
  g_object_class_install_property (object_class, PROP_LOCKED,
      properties[PROP_LOCKED]);

  properties[PROP_TRACK_TYPE] = g_param_spec_flags ("track-type", "Track Type",
      "The track type of the object", GES_TYPE_TRACK_TYPE,
      GES_TRACK_TYPE_UNKNOWN, G_PARAM_READWRITE | G_PARAM_CONSTRUCT);
  g_object_class_install_property (object_class, PROP_TRACK_TYPE,
      properties[PROP_TRACK_TYPE]);

  properties[PROP_TRACK] = g_param_spec_object ("track", "Track",
      "The track the object is in", GES_TYPE_TRACK, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_TRACK,
      properties[PROP_TRACK]);


  /**
   * GESTrackElement::deep-notify:
   * @track_element: a #GESTrackElement
   * @prop_object: the object that originated the signal
   * @prop: the property that changed
   *
   * The deep notify signal is used to be notified of property changes of all
   * the childs of @track_element
   *
   * Since: 0.10.2
   */
  ges_track_element_signals[DEEP_NOTIFY] =
      g_signal_new ("deep-notify", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED |
      G_SIGNAL_NO_HOOKS, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, GST_TYPE_ELEMENT, G_TYPE_PARAM);

  element_class->set_start = _set_start;
  element_class->set_duration = _set_duration;
  element_class->set_inpoint = _set_inpoint;
  element_class->set_priority = _set_priority;
  element_class->deep_copy = _deep_copy;

  klass->create_gnl_object = ges_track_element_create_gnl_object_func;
  /*  There is no 'get_props_hashtable' default implementation */
  klass->get_props_hastable = NULL;
  klass->list_children_properties = default_list_children_properties;
}

static void
ges_track_element_init (GESTrackElement * self)
{
  GESTrackElementPrivate *priv = self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRACK_ELEMENT, GESTrackElementPrivate);

  /* Sane default values */
  priv->pending_start = 0;
  priv->pending_inpoint = 0;
  priv->pending_duration = GST_SECOND;
  priv->pending_priority = 1;
  priv->pending_active = TRUE;
  priv->locked = TRUE;
  priv->properties_hashtable = NULL;
}

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);

  GST_DEBUG ("object:%p, start:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (start));

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (start == _START (object)))
      return FALSE;

    g_object_set (object->priv->gnlobject, "start", start, NULL);
  } else
    object->priv->pending_start = start;

  return TRUE;
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);

  GST_DEBUG ("object:%p, inpoint:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (inpoint));

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (inpoint == _INPOINT (object)))

      return FALSE;

    g_object_set (object->priv->gnlobject, "media-start", inpoint, NULL);
  } else
    object->priv->pending_inpoint = inpoint;

  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);
  GESTrackElementPrivate *priv = object->priv;

  GST_DEBUG ("object:%p, duration:%" GST_TIME_FORMAT,
      object, GST_TIME_ARGS (duration));

  if (GST_CLOCK_TIME_IS_VALID (_MAXDURATION (element)) &&
      duration > _INPOINT (object) + _MAXDURATION (element))
    duration = _MAXDURATION (element) - _INPOINT (object);

  if (priv->gnlobject != NULL) {
    if (G_UNLIKELY (duration == _DURATION (object)))
      return FALSE;

    g_object_set (priv->gnlobject, "duration", duration,
        "media-duration", duration, NULL);
  } else
    priv->pending_duration = duration;

  return TRUE;
}

static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);

  GST_DEBUG ("object:%p, priority:%" G_GUINT32_FORMAT, object, priority);

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (priority == _PRIORITY (object)))
      return FALSE;

    g_object_set (object->priv->gnlobject, "priority", priority, NULL);
  } else
    object->priv->pending_priority = priority;

  return TRUE;
}

/**
 * ges_track_element_set_active:
 * @object: a #GESTrackElement
 * @active: visibility
 *
 * Sets the usage of the @object. If @active is %TRUE, the object will be used for
 * playback and rendering, else it will be ignored.
 *
 * Returns: %TRUE if the property was toggled, else %FALSE
 */
gboolean
ges_track_element_set_active (GESTrackElement * object, gboolean active)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  GST_DEBUG ("object:%p, active:%d", object, active);

  if (object->priv->gnlobject != NULL) {
    if (G_UNLIKELY (active == object->active))
      return FALSE;

    g_object_set (object->priv->gnlobject, "active", active, NULL);
  } else
    object->priv->pending_active = active;
  return TRUE;
}

void
ges_track_element_set_track_type (GESTrackElement * object, GESTrackType type)
{
  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  if (object->priv->track_type != type) {
    object->priv->track_type = type;
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_TRACK_TYPE]);
  }
}

GESTrackType
ges_track_element_get_track_type (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), GES_TRACK_TYPE_UNKNOWN);

  return object->priv->track_type;
}

/* Callbacks from the GNonLin object */
static void
gnlobject_start_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackElement * obj)
{
  guint64 start;

  g_object_get (gnlobject, "start", &start, NULL);

  GST_DEBUG ("gnlobject start : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (start), GST_TIME_ARGS (_START (obj)));

  if (start != _START (obj)) {
    _START (obj) = start;
    g_object_notify (G_OBJECT (obj), "start");
  }
}

static void
gst_element_prop_changed_cb (GstElement * element, GParamSpec * arg
    G_GNUC_UNUSED, GESTrackElement * obj)
{
  g_signal_emit (obj, ges_track_element_signals[DEEP_NOTIFY], 0,
      GST_ELEMENT (element), arg);
}

static void
connect_signal (gpointer key, gpointer value, gpointer user_data)
{
  gchar *signame = g_strconcat ("notify::", G_PARAM_SPEC (key)->name, NULL);

  g_signal_connect (G_OBJECT (value),
      signame, G_CALLBACK (gst_element_prop_changed_cb),
      GES_TRACK_ELEMENT (user_data));

  g_free (signame);
}

static void
connect_properties_signals (GESTrackElement * object)
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
    GParamSpec * arg G_GNUC_UNUSED, GESTrackElement * obj)
{
  guint64 inpoint;

  g_object_get (gnlobject, "media-start", &inpoint, NULL);

  GST_DEBUG ("gnlobject in-point : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (inpoint), GST_TIME_ARGS (_INPOINT (obj)));

  if (inpoint != _INPOINT (obj)) {
    _INPOINT (obj) = inpoint;
    g_object_notify (G_OBJECT (obj), "in-point");
  }
}

static void
gnlobject_priority_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackElement * obj)
{
  guint32 priority;

  g_object_get (gnlobject, "priority", &priority, NULL);

  GST_DEBUG ("gnlobject priority : %d current : %d", priority, _PRIORITY (obj));

  if (priority != _PRIORITY (obj)) {
    _PRIORITY (obj) = priority;
    g_object_notify (G_OBJECT (obj), "priority");
  }
}

static void
gnlobject_duration_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackElement * obj)
{
  guint64 duration;
  GESTrackElementClass *klass;

  klass = GES_TRACK_ELEMENT_GET_CLASS (obj);

  g_object_get (gnlobject, "duration", &duration, NULL);

  GST_DEBUG_OBJECT (gnlobject, "duration : %" GST_TIME_FORMAT " current : %"
      GST_TIME_FORMAT, GST_TIME_ARGS (duration),
      GST_TIME_ARGS (_DURATION (obj)));

  if (duration != _DURATION (obj)) {
    _DURATION (obj) = duration;
    if (klass->duration_changed)
      klass->duration_changed (obj, duration);
    g_object_notify (G_OBJECT (obj), "duration");
  }
}

static void
gnlobject_active_cb (GstElement * gnlobject, GParamSpec * arg G_GNUC_UNUSED,
    GESTrackElement * obj)
{
  gboolean active;
  GESTrackElementClass *klass;

  klass = GES_TRACK_ELEMENT_GET_CLASS (obj);

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
ges_track_element_create_gnl_object_func (GESTrackElement * self)
{
  GESTrackElementClass *klass = NULL;
  GstElement *child = NULL;
  GstElement *gnlobject;

  klass = GES_TRACK_ELEMENT_GET_CLASS (self);

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
    GST_ERROR ("No GESTrackElement::gnlobject_factorytype implementation!");
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
ensure_gnl_object (GESTrackElement * object)
{
  GESTrackElementClass *class;
  GstElement *gnlobject;
  GHashTable *props_hash;
  gboolean res = TRUE;

  if (object->priv->gnlobject && object->priv->valid)
    return FALSE;

  /* 1. Create the GnlObject */
  GST_DEBUG ("Creating GnlObject");

  class = GES_TRACK_ELEMENT_GET_CLASS (object);

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
      res = ges_clip_fill_track_element (object->priv->timelineobj,
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
ges_track_element_set_track (GESTrackElement * object, GESTrack * track)
{
  gboolean ret = TRUE;
  GST_DEBUG ("object:%p, track:%p", object, track);

  object->priv->track = track;

  if (object->priv->track) {
    /* If we already have a gnlobject, we just set its caps properly */
    if (object->priv->gnlobject) {
      g_object_set (object->priv->gnlobject,
          "caps", ges_track_get_caps (object->priv->track), NULL);
    } else {
      ret = ensure_gnl_object (object);
    }
  }

  g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_TRACK]);
  return ret;
}

/**
 * ges_track_element_get_track:
 * @object: a #GESTrackElement
 *
 * Get the #GESTrack to which this object belongs.
 *
 * Returns: (transfer none): The #GESTrack to which this object belongs. Can be %NULL if it
 * is not in any track
 */
GESTrack *
ges_track_element_get_track (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->track;
}

/**
 * ges_track_element_set_clip:
 * @object: The #GESTrackElement to set the parent to
 * @clipect: The #GESClip, parent of @clip or %NULL
 *
 * Set the #GESClip to which @object belongs.
 */
void
ges_track_element_set_clip (GESTrackElement * object, GESClip * clipect)
{
  GST_DEBUG ("object:%p, clip:%p", object, clipect);

  object->priv->timelineobj = clipect;
}

/**
 * ges_track_element_get_clip:
 * @object: a #GESTrackElement
 *
 * Get the #GESClip which is controlling this track element
 *
 * Returns: (transfer none): the #GESClip which is controlling
 * this track element
 */
GESClip *
ges_track_element_get_clip (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->timelineobj;
}

/**
 * ges_track_element_get_gnlobject:
 * @object: a #GESTrackElement
 *
 * Get the GNonLin object this object is controlling.
 *
 * Returns: (transfer none): the GNonLin object this object is controlling.
 */
GstElement *
ges_track_element_get_gnlobject (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->gnlobject;
}

/**
 * ges_track_element_get_element:
 * @object: a #GESTrackElement
 *
 * Get the #GstElement this track element is controlling within GNonLin.
 *
 * Returns: (transfer none): the #GstElement this track element is controlling
 * within GNonLin.
 */
GstElement *
ges_track_element_get_element (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->element;
}

static inline void
ges_track_element_set_locked_internal (GESTrackElement * object,
    gboolean locked)
{
  object->priv->locked = locked;
}

/**
 * ges_track_element_set_locked:
 * @object: a #GESTrackElement
 * @locked: whether the object is lock to its parent
 *
 * Set the locking status of the @object in relationship to its controlling
 * #GESClip. If @locked is %TRUE, then this object will move synchronously
 * with its controlling #GESClip.
 */
void
ges_track_element_set_locked (GESTrackElement * object, gboolean locked)
{
  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  GST_DEBUG_OBJECT (object, "%s object", locked ? "Locking" : "Unlocking");

  ges_track_element_set_locked_internal (object, locked);
  g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_LOCKED]);

}

/**
 * ges_track_element_is_locked:
 * @object: a #GESTrackElement
 *
 * Let you know if object us locked or not (moving synchronously).
 *
 * Returns: %TRUE if the object is moving synchronously to its controlling
 * #GESClip, else %FALSE.
 */
gboolean
ges_track_element_is_locked (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  return object->priv->locked;
}


/**
 * ges_track_element_is_active:
 * @object: a #GESTrackElement
 *
 * Lets you know if @object will be used for playback and rendering,
 * or not.
 *
 * Returns: %TRUE if @object is active, %FALSE otherwize
 *
 * Since: 0.10.2
 */
gboolean
ges_track_element_is_active (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  if (G_UNLIKELY (object->priv->gnlobject == NULL))
    return object->priv->pending_active;
  else
    return object->active;
}

/**
 * ges_track_element_lookup_child:
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
ges_track_element_lookup_child (GESTrackElement * object,
    const gchar * prop_name, GstElement ** element, GParamSpec ** pspec)
{
  GHashTableIter iter;
  gpointer key, value;
  gchar **names, *name, *classename;
  gboolean res;
  GESTrackElementPrivate *priv;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  priv = object->priv;

  if (!priv->properties_hashtable)
    goto prop_hash_not_set;

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

prop_hash_not_set:
  {
    GST_WARNING_OBJECT (object, "The child properties haven't been set yet");
    return FALSE;
  }
}

/**
 * ges_track_element_set_child_property_by_pspec:
 * @object: a #GESTrackElement
 * @pspec: The #GParamSpec that specifies the property you want to set
 * @value: the value
 *
 * Sets a property of a child of @object.
 *
 * Since: 0.10.2
 */
void
ges_track_element_set_child_property_by_pspec (GESTrackElement * object,
    GParamSpec * pspec, GValue * value)
{
  GstElement *element;
  GESTrackElementPrivate *priv;

  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

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
 * ges_track_element_set_child_property_valist:
 * @object: The #GESTrackElement parent object
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
ges_track_element_set_child_property_valist (GESTrackElement * object,
    const gchar * first_property_name, va_list var_args)
{
  const gchar *name;
  GParamSpec *pspec;
  GstElement *element;

  gchar *error = NULL;
  GValue value = { 0, };

  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  name = first_property_name;

  /* Note: This part is in big part copied from the gst_child_object_set_valist
   * method. */

  /* iterate over pairs */
  while (name) {
    if (!ges_track_element_lookup_child (object, name, &element, &pspec))
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
 * ges_track_element_set_child_properties:
 * @object: The #GESTrackElement parent object
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
ges_track_element_set_child_properties (GESTrackElement * object,
    const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  va_start (var_args, first_property_name);
  ges_track_element_set_child_property_valist (object, first_property_name,
      var_args);
  va_end (var_args);
}

/**
 * ges_track_element_get_child_property_valist:
 * @object: The #GESTrackElement parent object
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
ges_track_element_get_child_property_valist (GESTrackElement * object,
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
    if (!ges_track_element_lookup_child (object, name, &element, &pspec))
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
 * ges_track_element_list_children_properties:
 * @object: The #GESTrackElement to get the list of children properties from
 * @n_properties: (out): return location for the length of the returned array
 *
 * Gets an array of #GParamSpec* for all configurable properties of the
 * children of @object.
 *
 * Returns: (transfer full) (array length=n_properties): an array of #GParamSpec* which should be freed after use or
 * %NULL if something went wrong
 *
 * Since: 0.10.2
 */
GParamSpec **
ges_track_element_list_children_properties (GESTrackElement * object,
    guint * n_properties)
{
  GESTrackElementClass *class;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  class = GES_TRACK_ELEMENT_GET_CLASS (object);

  return class->list_children_properties (object, n_properties);
}

/**
 * ges_track_element_get_child_properties:
 * @object: The origin #GESTrackElement
 * @first_property_name: The name of the first property to get
 * @...: return location for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Gets properties of a child of @object.
 *
 * Since: 0.10.2
 */
void
ges_track_element_get_child_properties (GESTrackElement * object,
    const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  va_start (var_args, first_property_name);
  ges_track_element_get_child_property_valist (object, first_property_name,
      var_args);
  va_end (var_args);
}

/**
 * ges_track_element_get_child_property_by_pspec:
 * @object: a #GESTrackElement
 * @pspec: The #GParamSpec that specifies the property you want to get
 * @value: (out): return location for the value
 *
 * Gets a property of a child of @object.
 *
 * Since: 0.10.2
 */
void
ges_track_element_get_child_property_by_pspec (GESTrackElement * object,
    GParamSpec * pspec, GValue * value)
{
  GstElement *element;
  GESTrackElementPrivate *priv;

  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

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

/**
 * ges_track_element_set_child_property:
 * @object: The origin #GESTrackElement
 * @property_name: The name of the property
 * @value: the value
 *
 * Sets a property of a GstElement contained in @object.
 *
 * Note that #ges_track_element_set_child_property is really
 * intended for language bindings, #ges_track_element_set_child_properties
 * is much more convenient for C programming.
 *
 * Returns: %TRUE if the property was set, %FALSE otherwize
 */
gboolean
ges_track_element_set_child_property (GESTrackElement * object,
    const gchar * property_name, GValue * value)
{
  GParamSpec *pspec;
  GstElement *element;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  if (!ges_track_element_lookup_child (object, property_name, &element, &pspec))
    goto not_found;

  g_object_set_property (G_OBJECT (element), pspec->name, value);

  g_object_unref (element);
  g_param_spec_unref (pspec);

  return TRUE;

not_found:
  {
    GST_WARNING_OBJECT (object, "The %s property doesn't exist", property_name);

    return FALSE;
  }
}

/**
* ges_track_element_get_child_property:
* @object: The origin #GESTrackElement
* @property_name: The name of the property
* @value: (out): return location for the property value, it will
* be initialized if it is initialized with 0
*
* In general, a copy is made of the property contents and
* the caller is responsible for freeing the memory by calling
* g_value_unset().
*
* Gets a property of a GstElement contained in @object.
*
* Note that #ges_track_element_get_child_property is really
* intended for language bindings, #ges_track_element_get_child_properties
* is much more convenient for C programming.
*
* Returns: %TRUE if the property was found, %FALSE otherwize
*/
gboolean
ges_track_element_get_child_property (GESTrackElement * object,
    const gchar * property_name, GValue * value)
{
  GParamSpec *pspec;
  GstElement *element;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  if (!ges_track_element_lookup_child (object, property_name, &element, &pspec))
    goto not_found;

  if (G_VALUE_TYPE (value) == G_TYPE_INVALID)
    g_value_init (value, pspec->value_type);

  g_object_get_property (G_OBJECT (element), pspec->name, value);

  g_object_unref (element);
  g_param_spec_unref (pspec);

  return TRUE;

not_found:
  {
    GST_WARNING_OBJECT (object, "The %s property doesn't exist", property_name);

    return FALSE;
  }
}

static GParamSpec **
default_list_children_properties (GESTrackElement * object,
    guint * n_properties)
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
    GST_DEBUG_OBJECT (object, "No child properties have been set yet");
    return NULL;
  }
}

void
_deep_copy (GESTimelineElement * element, GESTimelineElement * elementcopy)
{
  GParamSpec **specs;
  guint n, n_specs;
  GValue val = { 0 };
  GESTrackElement *copy = GES_TRACK_ELEMENT (elementcopy);

  ensure_gnl_object (copy);
  specs =
      ges_track_element_list_children_properties (GES_TRACK_ELEMENT (element),
      &n_specs);
  for (n = 0; n < n_specs; ++n) {
    g_value_init (&val, specs[n]->value_type);
    g_object_get_property (G_OBJECT (element), specs[n]->name, &val);
    ges_track_element_set_child_property_by_pspec (copy, specs[n], &val);
    g_value_unset (&val);
  }

  g_free (specs);
}

/**
 * ges_track_element_edit:
 * @object: the #GESTrackElement to edit
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
ges_track_element_edit (GESTrackElement * object,
    GList * layers, GESEditMode mode, GESEdge edge, guint64 position)
{
  GESTrack *track = ges_track_element_get_track (object);
  GESTimeline *timeline;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

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
