/* gst-editing-services
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
 *               <2013> Collabora Ltd.
 *
 * gst-editing-services is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * gst-editing-services is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * SECTION:gestimelineelement
 * @short_description: Base Class for all elements that will be in a way or
 * another inside a GESTimeline.
 *
 * The GESTimelineElement base class implements the notion of timing as well
 * as priority. A GESTimelineElement can have a parent object which will be
 * responsible for controlling its timing properties.
 */

#include "ges-timeline-element.h"
#include "ges-extractable.h"
#include "ges-meta-container.h"
#include "ges-internal.h"
#include <string.h>

/* maps type name quark => count */
static GData *object_name_counts = NULL;

static void
extractable_set_asset (GESExtractable * extractable, GESAsset * asset)
{
  GES_TIMELINE_ELEMENT (extractable)->asset = asset;
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->set_asset = extractable_set_asset;
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESTimelineElement, ges_timeline_element,
    G_TYPE_INITIALLY_UNOWNED,
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE, ges_extractable_interface_init)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER, NULL));

enum
{
  PROP_0,
  PROP_PARENT,
  PROP_TIMELINE,
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_MAX_DURATION,
  PROP_PRIORITY,
  PROP_NAME,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST] = { NULL, };

struct _GESTimelineElementPrivate
{
  gpointer dummy;
};

static void
_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTimelineElement *self = GES_TIMELINE_ELEMENT (object);

  switch (property_id) {
    case PROP_PARENT:
      g_value_take_object (value, self->parent);
      break;
    case PROP_TIMELINE:
      g_value_take_object (value, self->timeline);
      break;
    case PROP_START:
      g_value_set_uint64 (value, self->start);
      break;
    case PROP_INPOINT:
      g_value_set_uint64 (value, self->inpoint);
      break;
    case PROP_DURATION:
      g_value_set_uint64 (value, self->duration);
      break;
    case PROP_MAX_DURATION:
      g_value_set_uint64 (value, self->maxduration);
      break;
    case PROP_PRIORITY:
      g_value_set_uint (value, self->priority);
      break;
    case PROP_NAME:
      g_value_take_string (value, ges_timeline_element_get_name (self));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
  }
}

static void
_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineElement *self = GES_TIMELINE_ELEMENT (object);

  switch (property_id) {
    case PROP_PARENT:
      ges_timeline_element_set_parent (self, g_value_get_object (value));
      break;
    case PROP_TIMELINE:
      ges_timeline_element_set_timeline (self, g_value_get_object (value));
      break;
    case PROP_START:
      ges_timeline_element_set_start (self, g_value_get_uint64 (value));
      break;
    case PROP_INPOINT:
      ges_timeline_element_set_inpoint (self, g_value_get_uint64 (value));
      break;
    case PROP_DURATION:
      ges_timeline_element_set_duration (self, g_value_get_uint64 (value));
      break;
    case PROP_PRIORITY:
      ges_timeline_element_set_priority (self, g_value_get_uint (value));
      break;
    case PROP_MAX_DURATION:
      ges_timeline_element_set_max_duration (self, g_value_get_uint64 (value));
      break;
    case PROP_NAME:
      ges_timeline_element_set_name (self, g_value_get_string (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
  }
}

static void
ges_timeline_element_finalize (GObject * self)
{
  G_OBJECT_CLASS (ges_timeline_element_parent_class)->finalize (self);
}

static void
ges_timeline_element_init (GESTimelineElement * ges_timeline_element)
{
}

static void
ges_timeline_element_class_init (GESTimelineElementClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;

  /**
   * GESTimelineElement:parent:
   *
   * The parent container of the object
   */
  properties[PROP_PARENT] =
      g_param_spec_object ("parent", "Parent",
      "The parent container of the object", GES_TYPE_TIMELINE_ELEMENT,
      G_PARAM_READWRITE);

  /**
   * GESTimelineElement:timeline:
   *
   * The timeline in which @element is
   */
  properties[PROP_TIMELINE] =
      g_param_spec_object ("timeline", "Timeline",
      "The timeline the object is in", GES_TYPE_TIMELINE, G_PARAM_READWRITE);

  /**
   * GESTimelineElement:start:
   *
   * The position of the object in its container (in nanoseconds).
   */
  properties[PROP_START] = g_param_spec_uint64 ("start", "Start",
      "The position in the container", 0, G_MAXUINT64, 0, G_PARAM_READWRITE);

  /**
   * GESTimelineElement:in-point:
   *
   * The in-point at which this #GESTimelineElement will start outputting data
   * from its contents (in nanoseconds).
   *
   * Ex : an in-point of 5 seconds means that the first outputted buffer will
   * be the one located 5 seconds in the controlled resource.
   */
  properties[PROP_INPOINT] =
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE);

  /**
   * GESTimelineElement:duration:
   *
   * The duration (in nanoseconds) which will be used in the container
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The duration to use", 0,
      G_MAXUINT64, GST_CLOCK_TIME_NONE, G_PARAM_READWRITE);

  /**
   * GESTimelineElement:max-duration:
   *
   * The maximum duration (in nanoseconds) of the #GESTimelineElement.
   */
  properties[PROP_MAX_DURATION] =
      g_param_spec_uint64 ("max-duration", "Maximum duration",
      "The maximum duration of the object", 0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  /**
   * GESTimelineElement:priority:
   *
   * The priority of the object.
   */
  properties[PROP_PRIORITY] = g_param_spec_uint ("priority", "Priority",
      "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE);

  /**
   * GESTimelineElement:name:
   *
   * The name of the object
   */
  properties[PROP_NAME] =
      g_param_spec_string ("name", "Name", "The name of the timeline object",
      NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, PROP_LAST, properties);

  object_class->finalize = ges_timeline_element_finalize;

  klass->set_parent = NULL;
  klass->set_start = NULL;
  klass->set_inpoint = NULL;
  klass->set_duration = NULL;
  klass->set_max_duration = NULL;
  klass->set_priority = NULL;

  klass->ripple = NULL;
  klass->ripple_end = NULL;
  klass->roll_start = NULL;
  klass->roll_end = NULL;
  klass->trim = NULL;
}

static gboolean
_set_name_default (GESTimelineElement * self)
{
  const gchar *type_name;
  gint count;
  gchar *name;
  GQuark q;
  guint i, l;

  if (!object_name_counts) {
    g_datalist_init (&object_name_counts);
  }

  q = g_type_qname (G_OBJECT_TYPE (self));
  count = GPOINTER_TO_INT (g_datalist_id_get_data (&object_name_counts, q));
  g_datalist_id_set_data (&object_name_counts, q, GINT_TO_POINTER (count + 1));

  /* GstFooSink -> foosink<N> */
  type_name = g_quark_to_string (q);
  if (strncmp (type_name, "GES", 3) == 0)
    type_name += 3;
  /* give the 20th "uriclip" element and the first "uriclip2" (if needed in the future)
   * different names */
  l = strlen (type_name);
  if (l > 0 && g_ascii_isdigit (type_name[l - 1])) {
    name = g_strdup_printf ("%s-%d", type_name, count);
  } else {
    name = g_strdup_printf ("%s%d", type_name, count);
  }

  l = strlen (name);
  for (i = 0; i < l; i++)
    name[i] = g_ascii_tolower (name[i]);

  g_free (self->name);
  if (G_UNLIKELY (self->parent != NULL))
    goto had_parent;

  self->name = name;

  return TRUE;

had_parent:
  {
    GST_WARNING ("parented objects can't be renamed");
    return FALSE;
  }
}

/*********************************************
 *            API implementation             *
 *********************************************/

/**
 * ges_timeline_element_set_parent:
 * @self: a #GESTimelineElement
 * @parent: new parent of self
 *
 * Sets the parent of @self to @parent. The object's reference count will
 * be incremented, and any floating reference will be removed (see gst_object_ref_sink()).
 *
 * Returns: %TRUE if @parent could be set or %FALSE when @self
 * already had a parent or @self and @parent are the same.
 */
gboolean
ges_timeline_element_set_parent (GESTimelineElement * self,
    GESTimelineElement * parent)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (parent == NULL
      || GES_IS_TIMELINE_ELEMENT (parent), FALSE);

  if (self == parent) {
    GST_INFO_OBJECT (self, "Trying to add %p in itself, not a good idea!",
        self);

    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "set parent to %" GST_PTR_FORMAT, parent);

  if (self->parent != NULL && parent != NULL)
    goto had_parent;

  if (GES_TIMELINE_ELEMENT_GET_CLASS (self)->set_parent) {
    if (!GES_TIMELINE_ELEMENT_GET_CLASS (self)->set_parent (self, parent))
      return FALSE;
  }

  self->parent = parent;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PARENT]);
  return TRUE;

  /* ERROR handling */
had_parent:
  {
    GST_WARNING_OBJECT (self, "set parent failed, object already had a parent");
    return FALSE;
  }
}

/**
 * ges_timeline_element_get_parent:
 * @self: a #GESTimelineElement
 *
 * Returns the parent of @self. This function increases the refcount
 * of the parent object so you should gst_object_unref() it after usage.
 *
 * Returns: (transfer full): parent of @self, this can be %NULL if @self
 *   has no parent. unref after usage.
 */
GESTimelineElement *
ges_timeline_element_get_parent (GESTimelineElement * self)
{
  GESTimelineElement *result = NULL;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  result = self->parent;
  if (G_LIKELY (result))
    gst_object_ref (result);

  return result;
}

/**
 * ges_timeline_element_set_timeline:
 * @self: a #GESTimelineElement
 * @timeline: The #GESTimeline @self is in
 *
 * Sets the timeline of @self to @timeline.
 *
 * Returns: %TRUE if @timeline could be set or %FALSE when @timeline
 * already had a timeline.
 */
gboolean
ges_timeline_element_set_timeline (GESTimelineElement * self,
    GESTimeline * timeline)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (timeline == NULL || GES_IS_TIMELINE (timeline), FALSE);

  GST_DEBUG_OBJECT (self, "set timeline to %" GST_PTR_FORMAT, timeline);

  if (timeline != NULL && G_UNLIKELY (self->timeline != NULL))
    goto had_timeline;

  if (timeline == NULL) {
    if (self->timeline) {
      if (!timeline_remove_element (self->timeline, self))
        return FALSE;
    }
  } else {
    if (!timeline_add_element (timeline, self))
      return FALSE;
  }

  self->timeline = timeline;

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_TIMELINE]);
  return TRUE;

  /* ERROR handling */
had_timeline:
  {
    GST_DEBUG_OBJECT (self, "set timeline failed, object already had a "
        "timeline");
    return FALSE;
  }
}

/**
 * ges_timeline_element_get_timeline:
 * @self: a #GESTimelineElement
 *
 * Returns the timeline of @self. This function increases the refcount
 * of the timeline so you should gst_object_unref() it after usage.
 *
 * Returns: (transfer full): timeline of @self, this can be %NULL if @self
 *   has no timeline. unref after usage.
 */
GESTimeline *
ges_timeline_element_get_timeline (GESTimelineElement * self)
{
  GESTimeline *result = NULL;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  result = self->timeline;
  if (G_LIKELY (result))
    gst_object_ref (result);

  return result;
}

/**
 * ges_timeline_element_set_start:
 * @self: a #GESTimelineElement
 * @start: the position in #GstClockTime
 *
 * Set the position of the object in its containing layer
 *
 * Note that if the timeline snap-distance property of the timeline containing
 * @self is set, @self will properly snap to its neighboors.
 */
void
ges_timeline_element_set_start (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;
  GESTimelineElement *toplevel_container;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current start: %" GST_TIME_FORMAT
      " new start: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (self)), GST_TIME_ARGS (start));

  toplevel_container = ges_timeline_element_get_toplevel_parent (self);

  if (((gint64) (_START (toplevel_container) + start - _START (self))) < 0) {
    GST_INFO_OBJECT (self, "Can not move the object as it would imply its"
        "container to have a negative start value");

    gst_object_unref (toplevel_container);
    return;
  }

  gst_object_unref (toplevel_container);
  if (klass->set_start) {
    if (klass->set_start (self, start)) {
      self->start = start;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START]);
    }

    GST_DEBUG_OBJECT (self, "New start: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (self)));
    return;
  }

  GST_WARNING_OBJECT (self, "No set_start virtual method implementation"
      " on class %s. Can not set start %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (start));
}

/**
 * ges_timeline_element_set_inpoint:
 * @self: a #GESTimelineElement
 * @inpoint: the in-point in #GstClockTime
 *
 * Set the in-point, that is the moment at which the @self will start
 * outputting data from its contents.
 */
void
ges_timeline_element_set_inpoint (GESTimelineElement * self,
    GstClockTime inpoint)
{
  GESTimelineElementClass *klass;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  GST_DEBUG_OBJECT (self, "current inpoint: %" GST_TIME_FORMAT
      " new inpoint: %" GST_TIME_FORMAT, GST_TIME_ARGS (inpoint),
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_INPOINT (self)));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->set_inpoint) {
    if (klass->set_inpoint (self, inpoint)) {
      self->inpoint = inpoint;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INPOINT]);
    }

    return;
  }

  GST_WARNING_OBJECT (self, "No set_inpoint virtual method implementation"
      " on class %s. Can not set inpoint %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (inpoint));
}

/**
 * ges_timeline_element_set_max_duration:
 * @self: a #GESTimelineElement
 * @maxduration: the maximum duration in #GstClockTime
 *
 * Set the maximun duration of the object
 */
void
ges_timeline_element_set_max_duration (GESTimelineElement * self,
    GstClockTime maxduration)
{
  GESTimelineElementClass *klass;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current duration: %" GST_TIME_FORMAT
      " new duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_MAX_DURATION (self)),
      GST_TIME_ARGS (maxduration));

  if (klass->set_max_duration) {
    if (klass->set_max_duration (self, maxduration) == FALSE)
      return;
  }

  self->maxduration = maxduration;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_DURATION]);
}

/**
 * ges_timeline_element_set_duration:
 * @self: a #GESTimelineElement
 * @duration: the duration in #GstClockTime
 *
 * Set the duration of the object
 *
 * Note that if the timeline snap-distance property of the timeline containing
 * @self is set, @self will properly snap to its neighboors.
 */
void
ges_timeline_element_set_duration (GESTimelineElement * self,
    GstClockTime duration)
{
  GESTimelineElementClass *klass;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current duration: %" GST_TIME_FORMAT
      " new duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_DURATION (self)),
      GST_TIME_ARGS (duration));

  if (klass->set_duration) {
    if (klass->set_duration (self, duration)) {
      self->duration = duration;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
    }

    return;
  }

  GST_WARNING_OBJECT (self, "No set_duration virtual method implementation"
      " on class %s. Can not set duration %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (duration));
}

/**
 * ges_timeline_element_get_start:
 * @self: a #GESTimelineElement
 *
 * Returns: The @start of @self
 */
GstClockTime
ges_timeline_element_get_start (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->start;
}

/**
 * ges_timeline_element_get_inpoint:
 * @self: a #GESTimelineElement
 *
 * Returns: The @inpoint of @self
 */
GstClockTime
ges_timeline_element_get_inpoint (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->inpoint;
}

/**
 * ges_timeline_element_get_duration:
 * @self: a #GESTimelineElement
 *
 * Returns: The @duration of @self
 */
GstClockTime
ges_timeline_element_get_duration (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->duration;
}

/**
 * ges_timeline_element_get_max_duration:
 * @self: a #GESTimelineElement
 *
 * Returns: The @maxduration of @self
 */
GstClockTime
ges_timeline_element_get_max_duration (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->maxduration;
}

/**
 * ges_timeline_element_get_priority:
 * @self: a #GESTimelineElement
 *
 * Returns: The @priority of @self
 */
guint32
ges_timeline_element_get_priority (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), 0);

  return self->priority;
}

/**
 * ges_timeline_element_set_priority:
 * @self: a #GESTimelineElement
 * @priority: the priority
 *
 * Sets the priority of the object within the containing layer
 */
void
ges_timeline_element_set_priority (GESTimelineElement * self, guint32 priority)
{
  GESTimelineElementClass *klass;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current priority: %d new priority: %d",
      self->priority, priority);

  if (klass->set_priority) {
    if (klass->set_priority (self, priority)) {
      self->priority = priority;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIORITY]);
    }
    return;
  }

  GST_WARNING_OBJECT (self, "No set_priority virtual method implementation"
      " on class %s. Can not set priority %d", G_OBJECT_CLASS_NAME (klass),
      priority);
}

/**
 * ges_timeline_element_ripple:
 * @self: The #GESTimelineElement to ripple.
 * @start: The new start of @self in ripple mode.
 *
 * Edits @self in ripple mode. It allows you to modify the
 * start of @self and move the following neighbours accordingly.
 * This will change the overall timeline duration.
 *
 * Returns: %TRUE if the self as been rippled properly, %FALSE if an error
 * occured
 */
gboolean
ges_timeline_element_ripple (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->ripple)
    return klass->ripple (self, start);

  GST_WARNING_OBJECT (self, "No ripple virtual method implementation"
      " on class %s. Can not ripple to %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (start));

  return FALSE;
}

/**
 * ges_timeline_element_ripple_end:
 * @self: The #GESTimelineElement to ripple.
 * @end: The new end (start + duration) of @self in ripple mode. It will
 *       basically only change the duration of @self.
 *
 * Edits @self in ripple mode. It allows you to modify the
 * duration of a @self and move the following neighbours accordingly.
 * This will change the overall timeline duration.
 *
 * Returns: %TRUE if the self as been rippled properly, %FALSE if an error
 * occured
 */
gboolean
ges_timeline_element_ripple_end (GESTimelineElement * self, GstClockTime end)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->ripple_end) {
    return klass->ripple_end (self, end);
  }

  GST_WARNING_OBJECT (self, "No ripple virtual method implementation"
      " on class %s. Can not ripple end to %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (end));

  return FALSE;
}

/**
 * ges_timeline_element_roll_start:
 * @self: The #GESTimelineElement to roll
 * @start: The new start of @self in roll mode, it will also adapat
 * the in-point of @self according
 *
 * Edits @self in roll mode. It allows you to modify the
 * start and inpoint of a @self and "resize" (basicly change the duration
 * in this case) of the previous neighbours accordingly.
 * This will not change the overall timeline duration.
 *
 * Returns: %TRUE if the self as been roll properly, %FALSE if an error
 * occured
 */
gboolean
ges_timeline_element_roll_start (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->roll_start) {
    return klass->roll_start (self, start);
  }

  GST_WARNING_OBJECT (self, "No ripple virtual method implementation"
      " on class %s. Can not roll to %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (start));

  return FALSE;
}

/**
 * ges_timeline_element_roll_end:
 * @self: The #GESTimelineElement to roll.
 * @end: The new end (start + duration) of @self in roll mode
 *
 * Edits @self in roll mode. It allows you to modify the
 * duration of a @self and trim (basicly change the start + inpoint
 * in this case) the following neighbours accordingly.
 * This will not change the overall timeline duration.
 *
 * Returns: %TRUE if the self as been rolled properly, %FALSE if an error
 * occured
 */
gboolean
ges_timeline_element_roll_end (GESTimelineElement * self, GstClockTime end)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->roll_end)
    return klass->roll_end (self, end);

  GST_WARNING_OBJECT (self, "No ripple virtual method implementation"
      " on class %s. Can not roll end to %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (end));

  return FALSE;
}

/**
 * ges_timeline_element_trim:
 * @self: The #GESTimelineElement to trim.
 * @start: The new start of @self in trim mode, will adapt the inpoint
 * of @self accordingly
 *
 * Edits @self in trim mode. It allows you to modify the
 * inpoint and start of @self.
 * This will not change the overall timeline duration.
 *
 * Note that to trim the end of an self you can just set its duration. The same way
 * as this method, it will take into account the snapping-distance property of the
 * timeline in which @self is.
 *
 * Returns: %TRUE if the self as been trimmed properly, %FALSE if an error
 * occured
 */
gboolean
ges_timeline_element_trim (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->trim)
    return klass->trim (self, start);

  GST_WARNING_OBJECT (self, "No ripple virtual method implementation"
      " on class %s. Can not trim to %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (start));

  return FALSE;
}

/**
 * ges_timeline_element_copy:
 * @self: The #GESTimelineElement to copy
 * @deep: whether we want to create the elements @self contains or not
 *
 * Copies @self
 *
 * Returns: (transfer floating): The newly create #GESTimelineElement, copied from @self
 */
GESTimelineElement *
ges_timeline_element_copy (GESTimelineElement * self, gboolean deep)
{
  GESAsset *asset;
  GParameter *params;
  GParamSpec **specs;
  GESTimelineElementClass *klass;
  guint n, n_specs, n_params;

  GESTimelineElement *ret = NULL;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (self), &n_specs);
  params = g_new0 (GParameter, n_specs);
  n_params = 0;

  for (n = 0; n < n_specs; ++n) {
    /* We do not want the timeline or the name to be copied */
    if (g_strcmp0 (specs[n]->name, "parent") &&
        g_strcmp0 (specs[n]->name, "timeline") &&
        g_strcmp0 (specs[n]->name, "name") &&
        (specs[n]->flags & G_PARAM_READWRITE) == G_PARAM_READWRITE) {
      params[n_params].name = g_intern_string (specs[n]->name);
      g_value_init (&params[n_params].value, specs[n]->value_type);
      g_object_get_property (G_OBJECT (self), specs[n]->name,
          &params[n_params].value);
      ++n_params;
    }
  }

  ret = g_object_newv (G_OBJECT_TYPE (self), n_params, params);

  g_free (specs);
  g_free (params);


  asset = ges_extractable_get_asset (GES_EXTRACTABLE (self));
  if (asset)
    ges_extractable_set_asset (GES_EXTRACTABLE (ret), asset);
  if (deep) {
    if (klass->deep_copy)
      klass->deep_copy (self, ret);
    else
      GST_WARNING_OBJECT (self, "No deep_copy virtual method implementation"
          " on class %s. Can not finish the copy", G_OBJECT_CLASS_NAME (klass));
  }

  return ret;
}

/**
 * ges_timeline_element_get_toplevel_parent:
 * @self: The #GESTimelineElement to get the toplevel parent from
 *
 * Gets the toplevel #GESTimelineElement controlling @self
 *
 * Returns: (transfer full): The toplevel controlling parent of @self
 */
GESTimelineElement *
ges_timeline_element_get_toplevel_parent (GESTimelineElement * self)
{
  GESTimelineElement *toplevel = self;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  while (GES_TIMELINE_ELEMENT_PARENT (toplevel))
    toplevel = GES_TIMELINE_ELEMENT_PARENT (toplevel);

  return gst_object_ref (toplevel);
}

/**
 * ges_timeline_element_get_name:
 * @self: a #GESTimelineElement
 *
 *
 * Returns a copy of the name of @self.
 * Caller should g_free() the return value after usage.
 *
 * Returns: (transfer full): The name of @self
 */
gchar *
ges_timeline_element_get_name (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  return g_strdup (self->name);
}

/**
 * ges_timeline_element_set_name:
 * @self: a #GESTimelineElement
 *
 *
 * Sets the name of object, or gives @self a guaranteed unique name (if name is NULL).
 * This function makes a copy of the provided name, so the caller retains ownership
 * of the name it sent.
 */
gboolean
ges_timeline_element_set_name (GESTimelineElement * self, const gchar * name)
{
  gboolean result, readd_to_timeline = FALSE;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  /* parented objects cannot be renamed */
  if (self->timeline != NULL) {
    GESTimelineElement *tmp = ges_timeline_get_element (self->timeline, name);

    if (tmp) {
      gst_object_unref (tmp);
      goto had_timeline;
    }

    timeline_remove_element (self->timeline, self);
    readd_to_timeline = TRUE;
  }

  if (name != NULL) {
    g_free (self->name);
    self->name = g_strdup (name);
    result = TRUE;
  } else {
    result = _set_name_default (self);
  }

  if (readd_to_timeline)
    timeline_add_element (self->timeline, self);

  return result;

  /* error */
had_timeline:
  {
    GST_WARNING ("Objects already in a timeline can't be renamed");
    return FALSE;
  }
}
