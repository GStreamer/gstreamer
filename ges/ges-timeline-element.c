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
 * @title: GESTimelineElement
 * @short_description: Base Class for all elements that will be in a way or
 * another inside a GESTimeline.
 *
 * The GESTimelineElement base class implements the notion of timing as well
 * as priority. A GESTimelineElement can have a parent object which will be
 * responsible for controlling its timing properties.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-utils.h"
#include "ges-timeline-element.h"
#include "ges-extractable.h"
#include "ges-meta-container.h"
#include "ges-internal.h"
#include "ges-effect.h"

#include <string.h>
#include <gobject/gvaluecollector.h>

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
  PROP_SERIALIZE,
  PROP_LAST
};

enum
{
  DEEP_NOTIFY,
  LAST_SIGNAL
};

static guint ges_timeline_element_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[PROP_LAST] = { NULL, };

typedef struct
{
  GObject *child;
  gulong handler_id;
} ChildPropHandler;

struct _GESTimelineElementPrivate
{
  gboolean serialize;

  /* We keep a link between properties name and elements internally
   * The hashtable should look like
   * {GParamaSpec ---> child}*/
  GHashTable *children_props;

  GESTimelineElement *copied_from;

  GESTimelineElementFlags flags;
};

typedef struct
{
  GObject *child;
  GParamSpec *arg;
  GESTimelineElement *self;
} EmitDeepNotifyInIdleData;

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESTimelineElement, ges_timeline_element,
    G_TYPE_INITIALLY_UNOWNED, G_ADD_PRIVATE (GESTimelineElement)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE, ges_extractable_interface_init)
    G_IMPLEMENT_INTERFACE (GES_TYPE_META_CONTAINER, NULL));

static void
_set_child_property (GESTimelineElement * self G_GNUC_UNUSED, GObject * child,
    GParamSpec * pspec, GValue * value)
{
  g_object_set_property (child, pspec->name, value);
}

static gboolean
_lookup_child (GESTimelineElement * self, const gchar * prop_name,
    GObject ** child, GParamSpec ** pspec)
{
  GHashTableIter iter;
  gpointer key, value;
  gchar **names, *name, *classename;
  gboolean res;

  classename = NULL;
  res = FALSE;

  names = g_strsplit (prop_name, "::", 2);
  if (names[1] != NULL) {
    classename = names[0];
    name = names[1];
  } else
    name = names[0];

  g_hash_table_iter_init (&iter, self->priv->children_props);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (g_strcmp0 (G_PARAM_SPEC (key)->name, name) == 0) {
      ChildPropHandler *handler = (ChildPropHandler *) value;
      if (classename == NULL ||
          g_strcmp0 (G_OBJECT_TYPE_NAME (G_OBJECT (handler->child)),
              classename) == 0 ||
          g_strcmp0 (g_type_name (G_PARAM_SPEC (key)->owner_type),
              classename) == 0) {
        GST_DEBUG_OBJECT (self, "The %s property from %s has been found", name,
            classename);
        if (child)
          *child = gst_object_ref (handler->child);

        if (pspec)
          *pspec = g_param_spec_ref (key);
        res = TRUE;
        break;
      }
    }
  }
  g_strfreev (names);

  return res;
}

static GParamSpec **
default_list_children_properties (GESTimelineElement * self,
    guint * n_properties)
{
  GParamSpec **pspec, *spec;
  GHashTableIter iter;
  gpointer key, value;

  guint i = 0;

  *n_properties = g_hash_table_size (self->priv->children_props);
  pspec = g_new (GParamSpec *, *n_properties);

  g_hash_table_iter_init (&iter, self->priv->children_props);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    spec = G_PARAM_SPEC (key);
    pspec[i] = g_param_spec_ref (spec);
    i++;
  }

  return pspec;
}

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
    case PROP_SERIALIZE:
      g_value_set_boolean (value, self->priv->serialize);
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
    case PROP_SERIALIZE:
      self->priv->serialize = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (self, property_id, pspec);
  }
}

static void
ges_timeline_element_dispose (GObject * object)
{
  GESTimelineElement *self = GES_TIMELINE_ELEMENT (object);

  if (self->priv->children_props) {
    g_hash_table_unref (self->priv->children_props);
    self->priv->children_props = NULL;
  }

  g_clear_object (&self->priv->copied_from);

  G_OBJECT_CLASS (ges_timeline_element_parent_class)->dispose (object);
}

static void
ges_timeline_element_finalize (GObject * self)
{
  GESTimelineElement *tle = GES_TIMELINE_ELEMENT (self);

  g_free (tle->name);

  G_OBJECT_CLASS (ges_timeline_element_parent_class)->finalize (self);
}

static void
_child_prop_handler_free (ChildPropHandler * handler)
{
  g_object_freeze_notify (handler->child);
  if (handler->handler_id)
    g_signal_handler_disconnect (handler->child, handler->handler_id);
  g_object_thaw_notify (handler->child);
  gst_object_unref (handler->child);
  g_slice_free (ChildPropHandler, handler);
}

static void
ges_timeline_element_init (GESTimelineElement * self)
{
  self->priv = ges_timeline_element_get_instance_private (self);

  self->priv->serialize = TRUE;

  self->priv->children_props =
      g_hash_table_new_full ((GHashFunc) ges_pspec_hash, ges_pspec_equal,
      (GDestroyNotify) g_param_spec_unref,
      (GDestroyNotify) _child_prop_handler_free);
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
   *
   * Setting GESTimelineElement priorities is deprecated
   * as all priority management is done by GES itself now.
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

  /**
   * GESTimelineElement:serialize:
   *
   * Whether the element should be serialized.
   */
  properties[PROP_SERIALIZE] = g_param_spec_boolean ("serialize", "Serialize",
      "Whether the element should be serialized", TRUE,
      G_PARAM_READWRITE | GES_PARAM_NO_SERIALIZATION);

  g_object_class_install_properties (object_class, PROP_LAST, properties);

  /**
   * GESTimelineElement::deep-notify:
   * @timeline_element: a #GESTtimelineElement
   * @prop_object: the object that originated the signal
   * @prop: the property that changed
   *
   * The deep notify signal is used to be notified of property changes of all
   * the childs of @timeline_element
   */
  ges_timeline_element_signals[DEEP_NOTIFY] =
      g_signal_new ("deep-notify", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED |
      G_SIGNAL_NO_HOOKS, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_PARAM);

  object_class->dispose = ges_timeline_element_dispose;
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

  klass->list_children_properties = default_list_children_properties;
  klass->lookup_child = _lookup_child;
  klass->set_child_property = _set_child_property;
}

static void
_set_name (GESTimelineElement * self, const gchar * wanted_name)
{
  const gchar *type_name;
  gchar *lowcase_type;
  gint count;
  GQuark q;
  guint i, l;
  gchar *name = NULL;

  if (!object_name_counts) {
    g_datalist_init (&object_name_counts);
  }

  q = g_type_qname (G_OBJECT_TYPE (self));
  count = GPOINTER_TO_INT (g_datalist_id_get_data (&object_name_counts, q));

  /* GstFooSink -> foosink<N> */
  type_name = g_quark_to_string (q);
  if (strncmp (type_name, "GES", 3) == 0)
    type_name += 3;

  lowcase_type = g_strdup (type_name);
  l = strlen (lowcase_type);
  for (i = 0; i < l; i++)
    lowcase_type[i] = g_ascii_tolower (lowcase_type[i]);

  if (wanted_name == NULL) {
    /* give the 20th "uriclip" element and the first "uriclip2" (if needed in the future)
     * different names */
    l = strlen (type_name);
    if (l > 0 && g_ascii_isdigit (type_name[l - 1])) {
      name = g_strdup_printf ("%s-%d", lowcase_type, count++);
    } else {
      name = g_strdup_printf ("%s%d", lowcase_type, count++);
    }
  } else {
    /* If the wanted name uses the same 'namespace' as default, make
     * sure it does not badly interfere with our counting system */

    if (g_str_has_prefix (wanted_name, lowcase_type)) {
      guint64 tmpcount =
          g_ascii_strtoull (&wanted_name[strlen (lowcase_type)], NULL, 10);

      if (tmpcount > count) {
        count = tmpcount + 1;
        GST_DEBUG_OBJECT (self, "Using same naming %s but updated count to %i",
            wanted_name, count);
      } else if (tmpcount < count) {
        name = g_strdup_printf ("%s%d", lowcase_type, count);
        count++;
        GST_DEBUG_OBJECT (self, "Name %s already allocated, giving: %s instead"
            " New count is %i", wanted_name, name, count);
      } else {
        count++;
        GST_DEBUG_OBJECT (self, "Perfect name, just bumping object count");
      }
    }

    if (name == NULL)
      name = g_strdup (wanted_name);
  }

  g_free (lowcase_type);
  g_datalist_id_set_data (&object_name_counts, q, GINT_TO_POINTER (count));

  g_free (self->name);
  self->name = name;
}

/*********************************************
 *            API implementation             *
 *********************************************/

/**
 * ges_timeline_element_set_parent:
 * @self: a #GESTimelineElement
 * @parent: new parent of self
 *
 * Sets the parent of @self to @parent. The parents needs to already
 * own a hard reference on @self.
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
    gst_object_ref_sink (self);
    gst_object_unref (self);
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
    gst_object_ref_sink (self);
    gst_object_unref (self);
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
 * Returns: (transfer full) (nullable): parent of @self, this can be %NULL if
 * @self has no parent. unref after usage.
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
      if (!timeline_remove_element (self->timeline, self)) {
        GST_INFO_OBJECT (self, "Could not remove from"
            " currently set timeline %" GST_PTR_FORMAT, self->timeline);
        return FALSE;
      }
    }
  } else {
    if (!timeline_add_element (timeline, self)) {
      GST_INFO_OBJECT (self, "Could not add to timeline %" GST_PTR_FORMAT,
          self);
      return FALSE;
    }
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
 * Returns: (transfer full) (nullable): timeline of @self, this can be %NULL if
 * @self has no timeline. unref after usage.
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
 * Set the position of the object in its containing layer.
 *
 * Note that if the snapping-distance property of the timeline containing
 * @self is set, @self will properly snap to the edges around @start.
 *
 * Returns: %TRUE if @start could be set.
 */
gboolean
ges_timeline_element_set_start (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;
  GESTimelineElement *toplevel_container, *parent;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  if (self->start == start)
    return TRUE;

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current start: %" GST_TIME_FORMAT
      " new start: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (self)), GST_TIME_ARGS (start));

  toplevel_container = ges_timeline_element_get_toplevel_parent (self);
  parent = self->parent;

  /* FIXME This should not belong to GESTimelineElement */
  if (toplevel_container &&
      ((gint64) (_START (toplevel_container) + start - _START (self))) < 0 &&
      parent
      && GES_CONTAINER (parent)->children_control_mode == GES_CHILDREN_UPDATE) {
    GST_INFO_OBJECT (self,
        "Can not move the object as it would imply its "
        "container to have a negative start value");

    gst_object_unref (toplevel_container);
    return FALSE;
  }

  gst_object_unref (toplevel_container);
  if (klass->set_start) {
    gint res = klass->set_start (self, start);
    if (res == TRUE) {
      self->start = start;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START]);
    }

    GST_DEBUG_OBJECT (self, "New start: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (self)));

    return ! !res;
  }

  GST_WARNING_OBJECT (self, "No set_start virtual method implementation"
      " on class %s. Can not set start %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (start));
  return FALSE;
}

/**
 * ges_timeline_element_set_inpoint:
 * @self: a #GESTimelineElement
 * @inpoint: the in-point in #GstClockTime
 *
 * Set the in-point, that is the moment at which the @self will start
 * outputting data from its contents.
 *
 * Returns: %TRUE if @inpoint could be set.
 */
gboolean
ges_timeline_element_set_inpoint (GESTimelineElement * self,
    GstClockTime inpoint)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  GST_DEBUG_OBJECT (self, "current inpoint: %" GST_TIME_FORMAT
      " new inpoint: %" GST_TIME_FORMAT, GST_TIME_ARGS (inpoint),
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_INPOINT (self)));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->set_inpoint) {
    gboolean res = klass->set_inpoint (self, inpoint);
    if (res == TRUE) {
      self->inpoint = inpoint;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INPOINT]);
    }

    return ! !res;
  }

  GST_DEBUG_OBJECT (self, "No set_inpoint virtual method implementation"
      " on class %s. Can not set inpoint %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (inpoint));

  return FALSE;
}

/**
 * ges_timeline_element_set_max_duration:
 * @self: a #GESTimelineElement
 * @maxduration: the maximum duration in #GstClockTime
 *
 * Set the maximun duration of the object
 *
 * Returns: %TRUE if @maxduration could be set.
 */
gboolean
ges_timeline_element_set_max_duration (GESTimelineElement * self,
    GstClockTime maxduration)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current duration: %" GST_TIME_FORMAT
      " new duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_MAX_DURATION (self)),
      GST_TIME_ARGS (maxduration));

  if (klass->set_max_duration) {
    if (klass->set_max_duration (self, maxduration) == FALSE)
      return FALSE;
  }

  self->maxduration = maxduration;
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_DURATION]);
  return TRUE;
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
 *
 * Returns: %TRUE if @duration could be set.
 */
gboolean
ges_timeline_element_set_duration (GESTimelineElement * self,
    GstClockTime duration)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current duration: %" GST_TIME_FORMAT
      " new duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_DURATION (self)),
      GST_TIME_ARGS (duration));

  if (klass->set_duration) {
    gboolean res = klass->set_duration (self, duration);
    if (res == TRUE) {
      self->duration = duration;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
    }

    return ! !res;
  }

  GST_WARNING_OBJECT (self, "No set_duration virtual method implementation"
      " on class %s. Can not set duration %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (duration));
  return FALSE;
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
 *
 * Deprecated: All priority management is done by GES itself now.
 * To set #GESEffect priorities #ges_clip_set_top_effect_index should
 * be used.
 *
 * Returns: %TRUE if @priority could be set.
 */
gboolean
ges_timeline_element_set_priority (GESTimelineElement * self, guint32 priority)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "current priority: %d new priority: %d",
      self->priority, priority);

  if (klass->set_priority) {
    gboolean res = klass->set_priority (self, priority);
    if (res) {
      self->priority = priority;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PRIORITY]);
    }

    return res;
  }

  GST_WARNING_OBJECT (self, "No set_priority virtual method implementation"
      " on class %s. Can not set priority %d", G_OBJECT_CLASS_NAME (klass),
      priority);
  return FALSE;
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

#if GLIB_CHECK_VERSION(2, 53, 1)
  {
    gint i;
    GValue *values;
    const gchar **names;
    values = g_malloc0 (sizeof (GValue) * n_specs);
    names = g_malloc0 (sizeof (gchar *) * n_specs);

    for (i = 0; i < n_params; i++) {
      values[i] = params[i].value;
      names[i] = params[i].name;
    }

    ret =
        GES_TIMELINE_ELEMENT (g_object_new_with_properties (G_OBJECT_TYPE
            (self), n_params, names, values));
    g_free (names);
    g_free (values);
  }
#else
  ret = g_object_newv (G_OBJECT_TYPE (self), n_params, params);
#endif

  while (n_params--)
    g_value_unset (&params[n_params].value);

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

  if (deep) {
    ret->priv->copied_from = gst_object_ref (self);
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
 * @name: (allow-none): The name @self should take (if avalaible<)
 *
 * Sets the name of object, or gives @self a guaranteed unique name (if name is NULL).
 * This function makes a copy of the provided name, so the caller retains ownership
 * of the name it sent.
 */
gboolean
ges_timeline_element_set_name (GESTimelineElement * self, const gchar * name)
{
  gboolean result = TRUE, readd_to_timeline = FALSE;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  if (name != NULL && !g_strcmp0 (name, self->name)) {
    GST_DEBUG_OBJECT (self, "Same name!");
    return TRUE;
  }

  /* parented objects cannot be renamed */
  if (self->timeline != NULL && name) {
    GESTimelineElement *tmp = ges_timeline_get_element (self->timeline, name);

    if (tmp) {
      gst_object_unref (tmp);
      goto had_timeline;
    }

    timeline_remove_element (self->timeline, self);
    readd_to_timeline = TRUE;
  }

  _set_name (self, name);

  if (readd_to_timeline)
    timeline_add_element (self->timeline, self);

  return result;

  /* error */
had_timeline:
  {
    GST_WARNING ("Object %s already in a timeline can't be renamed to %s",
        self->name, name);
    return FALSE;
  }
}

static gboolean
emit_deep_notify_in_idle (EmitDeepNotifyInIdleData * data)
{
  g_signal_emit (data->self, ges_timeline_element_signals[DEEP_NOTIFY], 0,
      data->child, data->arg);

  gst_object_unref (data->child);
  g_param_spec_unref (data->arg);
  gst_object_unref (data->self);
  g_slice_free (EmitDeepNotifyInIdleData, data);

  return FALSE;
}

static void
child_prop_changed_cb (GObject * child, GParamSpec * arg
    G_GNUC_UNUSED, GESTimelineElement * self)
{
  EmitDeepNotifyInIdleData *data;

  /* Emit "deep-notify" right away if in main thread */
  if (g_main_context_acquire (g_main_context_default ())) {
    g_main_context_release (g_main_context_default ());
    g_signal_emit (self, ges_timeline_element_signals[DEEP_NOTIFY], 0,
        child, arg);
    return;
  }

  data = g_slice_new (EmitDeepNotifyInIdleData);

  data->child = gst_object_ref (child);
  data->arg = g_param_spec_ref (arg);
  data->self = gst_object_ref (self);

  g_idle_add ((GSourceFunc) emit_deep_notify_in_idle, data);
}

gboolean
ges_timeline_element_add_child_property (GESTimelineElement * self,
    GParamSpec * pspec, GObject * child)
{
  gchar *signame;
  ChildPropHandler *handler;

  if (g_hash_table_contains (self->priv->children_props, pspec)) {
    GST_INFO_OBJECT (self, "Child property already exists: %s", pspec->name);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Adding child property: %" GST_PTR_FORMAT "::%s",
      child, pspec->name);

  signame = g_strconcat ("notify::", pspec->name, NULL);
  handler = (ChildPropHandler *) g_slice_new0 (ChildPropHandler);
  handler->child = gst_object_ref (child);
  handler->handler_id =
      g_signal_connect (child, signame, G_CALLBACK (child_prop_changed_cb),
      self);
  g_hash_table_insert (self->priv->children_props, g_param_spec_ref (pspec),
      handler);

  g_free (signame);

  return TRUE;
}

/**
 * ges_timeline_element_get_child_property_by_pspec:
 * @self: a #GESTrackElement
 * @pspec: The #GParamSpec that specifies the property you want to get
 * @value: (out): return location for the value
 *
 * Gets a property of a child of @self.
 */
void
ges_timeline_element_get_child_property_by_pspec (GESTimelineElement * self,
    GParamSpec * pspec, GValue * value)
{
  ChildPropHandler *handler;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  handler = g_hash_table_lookup (self->priv->children_props, pspec);
  if (!handler)
    goto not_found;

  g_object_get_property (G_OBJECT (handler->child), pspec->name, value);

  return;

not_found:
  {
    GST_ERROR_OBJECT (self, "The %s property doesn't exist", pspec->name);
    return;
  }
}

/**
 * ges_timeline_element_set_child_property_by_pspec:
 * @self: a #GESTimelineElement
 * @pspec: The #GParamSpec that specifies the property you want to set
 * @value: the value
 *
 * Sets a property of a child of @self.
 */
void
ges_timeline_element_set_child_property_by_pspec (GESTimelineElement * self,
    GParamSpec * pspec, const GValue * value)
{
  ChildPropHandler *handler;
  GESTimelineElementClass *klass;

  g_return_if_fail (GES_IS_TRACK_ELEMENT (self));

  handler = g_hash_table_lookup (self->priv->children_props, pspec);

  if (!handler)
    goto not_found;

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);
  g_assert (klass->set_child_property);
  klass->set_child_property (self, handler->child, pspec, (GValue *) value);

  return;

not_found:
  {
    GST_ERROR ("The %s property doesn't exist", pspec->name);
    return;
  }
}

/**
 * ges_timeline_element_set_child_property:
 * @self: The origin #GESTimelineElement
 * @property_name: The name of the property
 * @value: the value
 *
 * Sets a property of a child of @self
 *
 * Note that #ges_timeline_element_set_child_property is really
 * intended for language bindings, #ges_timeline_element_set_child_properties
 * is much more convenient for C programming.
 *
 * Returns: %TRUE if the property was set, %FALSE otherwize
 */
gboolean
ges_timeline_element_set_child_property (GESTimelineElement * self,
    const gchar * property_name, const GValue * value)
{
  GParamSpec *pspec;
  GESTimelineElementClass *klass;
  GObject *child;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  if (!ges_timeline_element_lookup_child (self, property_name, &child, &pspec))
    goto not_found;

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);
  g_assert (klass->set_child_property);
  klass->set_child_property (self, child, pspec, (GValue *) value);

  gst_object_unref (child);
  g_param_spec_unref (pspec);

  return TRUE;

not_found:
  {
    GST_WARNING_OBJECT (self, "The %s property doesn't exist", property_name);

    return FALSE;
  }
}

/**
* ges_timeline_element_get_child_property:
* @self: The origin #GESTimelineElement
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
* Note that #ges_timeline_element_get_child_property is really
* intended for language bindings, #ges_timeline_element_get_child_properties
* is much more convenient for C programming.
*
* Returns: %TRUE if the property was found, %FALSE otherwize
*/
gboolean
ges_timeline_element_get_child_property (GESTimelineElement * self,
    const gchar * property_name, GValue * value)
{
  GParamSpec *pspec;
  GObject *child;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  if (!ges_timeline_element_lookup_child (self, property_name, &child, &pspec))
    goto not_found;

  if (G_VALUE_TYPE (value) == G_TYPE_INVALID)
    g_value_init (value, pspec->value_type);

  g_object_get_property (child, pspec->name, value);

  gst_object_unref (child);
  g_param_spec_unref (pspec);

  return TRUE;

not_found:
  {
    GST_WARNING_OBJECT (self, "The %s property doesn't exist", property_name);

    return FALSE;
  }
}

/**
 * ges_timeline_element_lookup_child:
 * @self: object to lookup the property in
 * @prop_name: name of the property to look up. You can specify the name of the
 *     class as such: "ClassName::property-name", to guarantee that you get the
 *     proper GParamSpec in case various GstElement-s contain the same property
 *     name. If you don't do so, you will get the first element found, having
 *     this property and the and the corresponding GParamSpec.
 * @child: (out) (allow-none) (transfer full): pointer to a #GstElement that
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
 */
gboolean
ges_timeline_element_lookup_child (GESTimelineElement * self,
    const gchar * prop_name, GObject ** child, GParamSpec ** pspec)
{
  GESTimelineElementClass *class;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  class = GES_TIMELINE_ELEMENT_GET_CLASS (self);
  g_return_val_if_fail (class->lookup_child, FALSE);

  return class->lookup_child (self, prop_name, child, pspec);
}

/**
 * ges_timeline_element_set_child_property_valist:
 * @self: The #GESTimelineElement parent object
 * @first_property_name: The name of the first property to set
 * @var_args: value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Sets a property of a child of @self. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 */
void
ges_timeline_element_set_child_property_valist (GESTimelineElement * self,
    const gchar * first_property_name, va_list var_args)
{
  const gchar *name;
  GParamSpec *pspec;
  GObject *child;

  gchar *error = NULL;
  GValue value = { 0, };

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  name = first_property_name;

  /* Note: This part is in big part copied from the gst_child_object_set_valist
   * method. */

  /* iterate over pairs */
  while (name) {
    if (!ges_timeline_element_lookup_child (self, name, &child, &pspec))
      goto not_found;

    G_VALUE_COLLECT_INIT (&value, pspec->value_type, var_args,
        G_VALUE_NOCOPY_CONTENTS, &error);

    if (error)
      goto cant_copy;

    g_object_set_property (child, pspec->name, &value);

    gst_object_unref (child);
    g_param_spec_unref (pspec);
    g_value_unset (&value);

    name = va_arg (var_args, gchar *);
  }
  return;

not_found:
  {
    GST_WARNING_OBJECT (self, "No property %s in OBJECT\n", name);
    return;
  }
cant_copy:
  {
    GST_WARNING_OBJECT (self, "error copying value %s in %p: %s", pspec->name,
        self, error);

    gst_object_unref (child);
    g_param_spec_unref (pspec);
    g_value_unset (&value);
    return;
  }
}

/**
 * ges_timeline_element_set_child_properties:
 * @self: The #GESTimelineElement parent object
 * @first_property_name: The name of the first property to set
 * @...: value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Sets a property of a child of @self. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 */
void
ges_timeline_element_set_child_properties (GESTimelineElement * self,
    const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  va_start (var_args, first_property_name);
  ges_timeline_element_set_child_property_valist (self, first_property_name,
      var_args);
  va_end (var_args);
}

/**
 * ges_timeline_element_get_child_property_valist:
 * @self: The #GESTimelineElement parent object
 * @first_property_name: The name of the first property to get
 * @var_args: value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Gets a property of a child of @self. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 */
void
ges_timeline_element_get_child_property_valist (GESTimelineElement * self,
    const gchar * first_property_name, va_list var_args)
{
  const gchar *name;
  gchar *error = NULL;
  GValue value = { 0, };
  GParamSpec *pspec;
  GObject *child;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  name = first_property_name;

  /* This part is in big part copied from the gst_child_object_get_valist method */
  while (name) {
    if (!ges_timeline_element_lookup_child (self, name, &child, &pspec))
      goto not_found;

    g_value_init (&value, pspec->value_type);
    g_object_get_property (child, pspec->name, &value);
    gst_object_unref (child);
    g_param_spec_unref (pspec);

    G_VALUE_LCOPY (&value, var_args, 0, &error);
    if (error)
      goto cant_copy;
    g_value_unset (&value);
    name = va_arg (var_args, gchar *);
  }
  return;

not_found:
  {
    GST_WARNING_OBJECT (self, "no child property %s", name);
    return;
  }
cant_copy:
  {
    GST_WARNING_OBJECT (self, "error copying value %s in %s", pspec->name,
        error);

    g_value_unset (&value);
    return;
  }
}

static gint
compare_gparamspec (GParamSpec ** a, GParamSpec ** b, gpointer udata)
{
  return g_strcmp0 ((*a)->name, (*b)->name);
}


/**
 * ges_timeline_element_list_children_properties:
 * @self: The #GESTimelineElement to get the list of children properties from
 * @n_properties: (out): return location for the length of the returned array
 *
 * Gets an array of #GParamSpec* for all configurable properties of the
 * children of @self.
 *
 * Returns: (transfer full) (array length=n_properties): an array of #GParamSpec* which should be freed after use or
 * %NULL if something went wrong
 */
GParamSpec **
ges_timeline_element_list_children_properties (GESTimelineElement * self,
    guint * n_properties)
{
  GParamSpec **ret;
  GESTimelineElementClass *class;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  class = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (!class->list_children_properties) {
    GST_INFO_OBJECT (self, "No %s->list_children_properties implementation",
        G_OBJECT_TYPE_NAME (self));

    *n_properties = 0;
    return NULL;
  }

  ret = class->list_children_properties (self, n_properties);
  g_qsort_with_data (ret, *n_properties, sizeof (GParamSpec *),
      (GCompareDataFunc) compare_gparamspec, NULL);

  return ret;
}

/**
 * ges_timeline_element_get_child_properties:
 * @self: The origin #GESTimelineElement
 * @first_property_name: The name of the first property to get
 * @...: return location for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Gets properties of a child of @self.
 */
void
ges_timeline_element_get_child_properties (GESTimelineElement * self,
    const gchar * first_property_name, ...)
{
  va_list var_args;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  va_start (var_args, first_property_name);
  ges_timeline_element_get_child_property_valist (self, first_property_name,
      var_args);
  va_end (var_args);
}

gboolean
ges_timeline_element_remove_child_property (GESTimelineElement * self,
    GParamSpec * pspec)
{
  return g_hash_table_remove (self->priv->children_props, pspec);
}

/**
 * ges_timeline_element_get_track_types:
 * @self: A #GESTimelineElement
 *
 * Gets all the TrackTypes @self will interact with
 *
 * Since: 1.6.0
 */
GESTrackType
ges_timeline_element_get_track_types (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), 0);
  g_return_val_if_fail (GES_TIMELINE_ELEMENT_GET_CLASS (self)->get_track_types,
      0);

  return GES_TIMELINE_ELEMENT_GET_CLASS (self)->get_track_types (self);
}

/**
 * ges_timeline_element_paste:
 * @self: The #GESTimelineElement to paste
 * @paste_position: The position in the timeline the element should
 * be copied to, meaning it will become the start of @self
 *
 * Paste @self inside the timeline. @self must have been created
 * using ges_timeline_element_copy with recurse=TRUE set,
 * otherwise it will fail.
 *
 * Returns: (transfer none): Paste @self copying the element
 *
 * Since: 1.6.0
 */
GESTimelineElement *
ges_timeline_element_paste (GESTimelineElement * self,
    GstClockTime paste_position)
{
  GESTimelineElement *res;
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  if (!self->priv->copied_from) {
    GST_ERROR_OBJECT (self, "Is not being 'deeply' copied!");

    return NULL;
  }

  if (!GES_TIMELINE_ELEMENT_GET_CLASS (self)->paste) {
    GST_ERROR_OBJECT (self, "No paste vmethod implemented");

    return NULL;
  }

  res = GES_TIMELINE_ELEMENT_GET_CLASS (self)->paste (self,
      self->priv->copied_from, paste_position);

  g_clear_object (&self->priv->copied_from);

  return g_object_ref (res);
}

/**
 * ges_timeline_element_get_layer_priority:
 * @self: A #GESTimelineElement
 *
 * Returns: The priority of the first layer the element is in (note that only
 * groups can span over several layers). %GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY
 * means that the element is not in a layer.
 */
guint32
ges_timeline_element_get_layer_priority (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self),
      GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY);

  if (!GES_TIMELINE_ELEMENT_GET_CLASS (self)->get_layer_priority)
    return self->priority;

  return GES_TIMELINE_ELEMENT_GET_CLASS (self)->get_layer_priority (self);
}

/* Internal */
gdouble
ges_timeline_element_get_media_duration_factor (GESTimelineElement * self)
{
  gdouble media_duration_factor;
  GESEffectClass *class;
  GList *props;

  media_duration_factor = 1.0;

  class = GES_EFFECT_CLASS (g_type_class_ref (GES_TYPE_EFFECT));

  for (props = class->rate_properties; props != NULL; props = props->next) {
    GObject *child;
    GParamSpec *pspec;
    if (ges_timeline_element_lookup_child (self, props->data, &child, &pspec)) {
      if (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_FLOAT) {
        gfloat rate_change;
        g_object_get (child, pspec->name, &rate_change, NULL);
        media_duration_factor *= rate_change;
      } else if (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_DOUBLE) {
        gdouble rate_change;
        g_object_get (child, pspec->name, &rate_change, NULL);
        media_duration_factor *= rate_change;
      } else {
        GST_WARNING_OBJECT (self,
            "Rate property %s in child %" GST_PTR_FORMAT
            " is of unsupported type %s", pspec->name, child,
            G_VALUE_TYPE_NAME (pspec->value_type));
      }

      gst_object_unref (child);
      g_param_spec_unref (pspec);

      GST_DEBUG_OBJECT (self,
          "Added rate changing property %s, set to value %lf",
          (const char *) props->data, media_duration_factor);
    }
  }

  g_type_class_unref (class);
  return media_duration_factor;
}

/* Internal */
GESTimelineElement *
ges_timeline_element_get_copied_from (GESTimelineElement * self)
{
  GESTimelineElement *copied_from = self->priv->copied_from;
  self->priv->copied_from = NULL;
  return copied_from;
}

GESTimelineElementFlags
ges_timeline_element_flags (GESTimelineElement * self)
{
  return self->priv->flags;
}

void
ges_timeline_element_set_flags (GESTimelineElement * self,
    GESTimelineElementFlags flags)
{
  self->priv->flags = flags;

}

/**
 * ges_timeline_element_edit:
 * @self: the #GESClip to edit
 * @layers: (element-type GESLayer): The layers you want the edit to
 * happen in, %NULL means that the edition is done in all the
 * #GESLayers contained in the current timeline.
 * @new_layer_priority: The priority of the layer @self should land in.
 * If the layer you're trying to move the element to doesn't exist, it will
 * be created automatically. -1 means no move.
 * @mode: The #GESEditMode in which the editition will happen.
 * @edge: The #GESEdge the edit should happen on.
 * @position: The position at which to edit @self (in nanosecond)
 *
 * Edit @self in the different exisiting #GESEditMode modes. In the case of
 * slide, and roll, you need to specify a #GESEdge
 *
 * Returns: %TRUE if @self as been edited properly, %FALSE if an error
 * occured
 */
gboolean
ges_timeline_element_edit (GESTimelineElement * self, GList * layers,
    gint64 new_layer_priority, GESEditMode mode, GESEdge edge, guint64 position)
{
  GESTimeline *timeline;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (self);
  switch (mode) {
    case GES_EDIT_MODE_RIPPLE:
      return timeline_ripple_object (timeline, self,
          new_layer_priority <
          0 ? GES_TIMELINE_ELEMENT_LAYER_PRIORITY (self) :
          new_layer_priority, layers, edge, position);
    case GES_EDIT_MODE_TRIM:
      return timeline_trim_object (timeline, self,
          new_layer_priority <
          0 ? GES_TIMELINE_ELEMENT_LAYER_PRIORITY (self) :
          new_layer_priority, layers, edge, position);
    case GES_EDIT_MODE_NORMAL:
      return timeline_move_object (timeline, self,
          new_layer_priority <
          0 ? GES_TIMELINE_ELEMENT_LAYER_PRIORITY (self) :
          new_layer_priority, layers, edge, position);
    case GES_EDIT_MODE_ROLL:
      return timeline_roll_object (timeline, self, layers, edge, position);
    case GES_EDIT_MODE_SLIDE:
      GST_ERROR_OBJECT (self, "Sliding not implemented.");
      return FALSE;
  }
  return FALSE;
}
