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

#include "ges-timeline-element.h"
#include "ges-extractable.h"
#include "ges-meta-container.h"

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
  PROP_START,
  PROP_INPOINT,
  PROP_DURATION,
  PROP_MAX_DURATION,
  PROP_PRIORITY,
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
  }
}

static void
ges_timeline_element_init (GESTimelineElement * ges_timeline_element)
{

}

static void
ges_timeline_element_finalize (GObject * self)
{
  G_OBJECT_CLASS (ges_timeline_element_parent_class)->finalize (self);
}

static void
ges_timeline_element_class_init (GESTimelineElementClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = _get_property;
  object_class->set_property = _set_property;

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
   *
   * Since: 0.10.XX
   */
  properties[PROP_MAX_DURATION] =
      g_param_spec_uint64 ("max-duration", "Maximum duration",
      "The maximum duration of the object", 0, G_MAXUINT64, G_MAXUINT64,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT);

  /**
   * GESTimelineElement:priority:
   *
   * The layer priority of the object.
   */
  properties[PROP_PRIORITY] = g_param_spec_uint ("priority", "Priority",
      "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE);

  g_object_class_install_properties (object_class, PROP_LAST, properties);

  object_class->finalize = ges_timeline_element_finalize;

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

/*********************************************
 *            API implementation             *
 *********************************************/

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

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current start: %" GST_TIME_FORMAT,
      " new start: %" GST_TIME_FORMAT, GST_TIME_ARGS (start),
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (self)));

  if (klass->set_start) {
    if (klass->set_start (self, start)) {
      self->start = start;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START]);
    }

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

  GST_DEBUG_OBJECT (self, "current inpoint: %" GST_TIME_FORMAT,
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

  GST_DEBUG_OBJECT (self, "current duration: %" GST_TIME_FORMAT,
      " new duration: %" GST_TIME_FORMAT, GST_TIME_ARGS (maxduration),
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_MAX_DURATION (self)));

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

  GST_DEBUG_OBJECT (self, "current duration: %" GST_TIME_FORMAT,
      " new duration: %" GST_TIME_FORMAT, GST_TIME_ARGS (duration),
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_DURATION (self)));

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
    if (g_strcmp0 (specs[n]->name, "parent") &&
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


  if (deep) {
    if (klass->deep_copy)
      klass->deep_copy (self, ret);
    else
      GST_WARNING_OBJECT (self, "No deep_copy virtual method implementation"
          " on class %s. Can not finish the copy", G_OBJECT_CLASS_NAME (klass));
  }

  return ret;
}
