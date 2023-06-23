/* gst-editing-services
 * Copyright (C) <2013> Thibault Saunier <thibault.saunier@collabora.com>
 *               <2013> Collabora Ltd.
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
 * SECTION:gestimelineelement
 * @title: GESTimelineElement
 * @short_description: Base Class for all elements with some temporal extent
 * within a #GESTimeline.
 *
 * A #GESTimelineElement will have some temporal extent in its
 * corresponding #GESTimelineElement:timeline, controlled by its
 * #GESTimelineElement:start and #GESTimelineElement:duration. This
 * determines when its content will be displayed, or its effect applied,
 * in the timeline. Several objects may overlap within a given
 * #GESTimeline, in which case their #GESTimelineElement:priority is used
 * to determine their ordering in the timeline. Priority is mostly handled
 * internally by #GESLayer-s and #GESClip-s.
 *
 * A timeline element can have a #GESTimelineElement:parent,
 * such as a #GESClip, which is responsible for controlling its timing.
 *
 * ## Editing
 *
 * Elements can be moved around in their #GESTimelineElement:timeline by
 * setting their #GESTimelineElement:start and
 * #GESTimelineElement:duration using ges_timeline_element_set_start()
 * and ges_timeline_element_set_duration(). Additionally, which parts of
 * the underlying content are played in the timeline can be adjusted by
 * setting the #GESTimelineElement:in-point using
 * ges_timeline_element_set_inpoint(). The library also provides
 * ges_timeline_element_edit(), with various #GESEditMode-s, which can
 * adjust these properties in a convenient way, as well as introduce
 * similar changes in neighbouring or later elements in the timeline.
 *
 * However, a timeline may refuse a change in these properties if they
 * would place the timeline in an unsupported configuration. See
 * #GESTimeline for its overlap rules.
 *
 * Additionally, an edit may be refused if it would place one of the
 * timing properties out of bounds (such as a negative time value for
 * #GESTimelineElement:start, or having insufficient internal
 * content to last for the desired #GESTimelineElement:duration).
 *
 * ## Time Coordinates
 *
 * There are three main sets of time coordinates to consider when using
 * timeline elements:
 *
 * + Timeline coordinates: these are the time coordinates used in the
 *   output of the timeline in its #GESTrack-s. Each track share the same
 *   coordinates, so there is only one set of coordinates for the
 *   timeline. These extend indefinitely from 0. The times used for
 *   editing (including setting #GESTimelineElement:start and
 *   #GESTimelineElement:duration) use these coordinates, since these
 *   define when an element is present and for how long the element lasts
 *   for in the timeline.
 * + Internal source coordinates: these are the time coordinates used
 *   internally at the element's output. This is only really defined for
 *   #GESTrackElement-s, where it refers to time coordinates used at the
 *   final source pad of the wrapped #GstElement-s. However, these
 *   coordinates may also be used in a #GESClip in reference to its
 *   children. In particular, these are the coordinates used for
 *   #GESTimelineElement:in-point and #GESTimelineElement:max-duration.
 * + Internal sink coordinates: these are the time coordinates used
 *   internally at the element's input. A #GESSource has no input, so
 *   these would be undefined. Otherwise, for most #GESTrackElement-s
 *   these will be the same set of coordinates as the internal source
 *   coordinates because the element does not change the timing
 *   internally. Only #GESBaseEffect can support elements where these
 *   are different. See #GESBaseEffect for more information.
 *
 * You can determine the timeline time for a given internal source time
 * in a #GESTrack in a #GESClip using
 * ges_clip_get_timeline_time_from_internal_time(), and vice versa using
 * ges_clip_get_internal_time_from_timeline_time(), for the purposes of
 * editing and setting timings properties.
 *
 * ## Children Properties
 *
 * If a timeline element owns another #GstObject and wishes to expose
 * some of its properties, it can do so by registering the property as one
 * of the timeline element's children properties using
 * ges_timeline_element_add_child_property(). The registered property of
 * the child can then be read and set using the
 * ges_timeline_element_get_child_property() and
 * ges_timeline_element_set_child_property() methods, respectively. Some
 * sub-classed objects will be created with pre-registered children
 * properties; for example, to expose part of an underlying #GstElement
 * that is used internally. The registered properties can be listed with
 * ges_timeline_element_list_children_properties().
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
  CHILD_PROPERTY_ADDED,
  CHILD_PROPERTY_REMOVED,
  LAST_SIGNAL
};

static guint ges_timeline_element_signals[LAST_SIGNAL] = { 0 };

static GParamSpec *properties[PROP_LAST] = { NULL, };

typedef struct
{
  GObject *child;
  GESTimelineElement *owner;
  gulong handler_id;
  GESTimelineElement *self;
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

/*********************************************
 *      Virtual methods implementation       *
 *********************************************/
static void
_set_child_property (GESTimelineElement * self G_GNUC_UNUSED, GObject * child,
    GParamSpec * pspec, GValue * value)
{
  if (G_VALUE_TYPE (value) != pspec->value_type
      && G_VALUE_TYPE (value) == G_TYPE_STRING)
    gst_util_set_object_arg (child, pspec->name, g_value_get_string (value));
  else
    g_object_set_property (child, pspec->name, value);
}

static gboolean
_set_child_property_full (GESTimelineElement * self, GObject * child,
    GParamSpec * pspec, const GValue * value, GError ** error)
{
  GES_TIMELINE_ELEMENT_GET_CLASS (self)->set_child_property (self, child,
      pspec, (GValue *) value);
  return TRUE;
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

GParamSpec **
ges_timeline_element_get_children_properties (GESTimelineElement * self,
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
      g_value_set_object (value, self->parent);
      break;
    case PROP_TIMELINE:
      g_value_set_object (value, self->timeline);
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

  if (handler->child != (GObject *) handler->self &&
      handler->child != (GObject *) handler->owner)
    gst_object_unref (handler->child);
  g_free (handler);
}

static gboolean
_get_natural_framerate (GESTimelineElement * self, gint * framerate_n,
    gint * framerate_d)
{
  GST_INFO_OBJECT (self, "No natural framerate");

  return FALSE;
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
   * The parent container of the element.
   */
  properties[PROP_PARENT] =
      g_param_spec_object ("parent", "Parent",
      "The parent container of the object", GES_TYPE_TIMELINE_ELEMENT,
      G_PARAM_READWRITE);

  /**
   * GESTimelineElement:timeline:
   *
   * The timeline that the element lies within.
   */
  properties[PROP_TIMELINE] =
      g_param_spec_object ("timeline", "Timeline",
      "The timeline the object is in", GES_TYPE_TIMELINE,
      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GESTimelineElement:start:
   *
   * The starting position of the element in the timeline (in nanoseconds
   * and in the time coordinates of the timeline). For example, for a
   * source element, this would determine the time at which it should
   * start outputting its internal content. For an operation element, this
   * would determine the time at which it should start applying its effect
   * to any source content.
   */
  properties[PROP_START] = g_param_spec_uint64 ("start", "Start",
      "The position in the timeline", 0, G_MAXUINT64, 0,
      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GESTimelineElement:in-point:
   *
   * The initial offset to use internally when outputting content (in
   * nanoseconds, but in the time coordinates of the internal content).
   *
   * For example, for a #GESVideoUriSource that references some media
   * file, the "internal content" is the media file data, and the
   * in-point would correspond to some timestamp in the media file.
   * When playing the timeline, and when the element is first reached at
   * timeline-time #GESTimelineElement:start, it will begin outputting the
   * data from the timestamp in-point **onwards**, until it reaches the
   * end of its #GESTimelineElement:duration in the timeline.
   *
   * For elements that have no internal content, this should be kept
   * as 0.
   */
  properties[PROP_INPOINT] =
      g_param_spec_uint64 ("in-point", "In-point", "The in-point", 0,
      G_MAXUINT64, 0, G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GESTimelineElement:duration:
   *
   * The duration that the element is in effect for in the timeline (a
   * time difference in nanoseconds using the time coordinates of the
   * timeline). For example, for a source element, this would determine
   * for how long it should output its internal content for. For an
   * operation element, this would determine for how long its effect
   * should be applied to any source content.
   */
  properties[PROP_DURATION] =
      g_param_spec_uint64 ("duration", "Duration", "The play duration", 0,
      G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GESTimelineElement:max-duration:
   *
   * The full duration of internal content that is available (a time
   * difference in nanoseconds using the time coordinates of the internal
   * content).
   *
   * This will act as a cap on the #GESTimelineElement:in-point of the
   * element (which is in the same time coordinates), and will sometimes
   * be used to limit the #GESTimelineElement:duration of the element in
   * the timeline.
   *
   * For example, for a #GESVideoUriSource that references some media
   * file, this would be the length of the media file.
   *
   * For elements that have no internal content, or whose content is
   * indefinite, this should be kept as #GST_CLOCK_TIME_NONE.
   */
  properties[PROP_MAX_DURATION] =
      g_param_spec_uint64 ("max-duration", "Maximum duration",
      "The maximum duration of the object", 0, G_MAXUINT64, GST_CLOCK_TIME_NONE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_EXPLICIT_NOTIFY);

  /**
   * GESTimelineElement:priority:
   *
   * The priority of the element.
   *
   * Deprecated: 1.10: Priority management is now done by GES itself.
   */
  properties[PROP_PRIORITY] = g_param_spec_uint ("priority", "Priority",
      "The priority of the object", 0, G_MAXUINT, 0, G_PARAM_READWRITE);

  /**
   * GESTimelineElement:name:
   *
   * The name of the element. This should be unique within its timeline.
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
   * @timeline_element: A #GESTtimelineElement
   * @prop_object: The child whose property has been set
   * @prop: The specification for the property that been set
   *
   * Emitted when a child of the element has one of its registered
   * properties set. See ges_timeline_element_add_child_property().
   * Note that unlike #GObject::notify, a child property name can not be
   * used as a signal detail.
   */
  ges_timeline_element_signals[DEEP_NOTIFY] =
      g_signal_new ("deep-notify", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST | G_SIGNAL_NO_RECURSE | G_SIGNAL_DETAILED |
      G_SIGNAL_NO_HOOKS, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 2, G_TYPE_OBJECT, G_TYPE_PARAM);

  /**
   * GESTimelineElement::child-property-added:
   * @timeline_element: A #GESTtimelineElement
   * @prop_object: The child whose property has been registered
   * @prop: The specification for the property that has been registered
   *
   * Emitted when the element has a new child property registered. See
   * ges_timeline_element_add_child_property().
   *
   * Note that some GES elements will be automatically created with
   * pre-registered children properties. You can use
   * ges_timeline_element_list_children_properties() to list these.
   *
   * Since: 1.18
   */
  ges_timeline_element_signals[CHILD_PROPERTY_ADDED] =
      g_signal_new ("child-property-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      G_TYPE_OBJECT, G_TYPE_PARAM);

  /**
   * GESTimelineElement::child-property-removed:
   * @timeline_element: A #GESTimelineElement
   * @prop_object: The child whose property has been unregistered
   * @prop: The specification for the property that has been unregistered
   *
   * Emitted when the element has a child property unregistered. See
   * ges_timeline_element_remove_child_property().
   *
   * Since: 1.18
   */
  ges_timeline_element_signals[CHILD_PROPERTY_REMOVED] =
      g_signal_new ("child-property-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL, G_TYPE_NONE, 2,
      G_TYPE_OBJECT, G_TYPE_PARAM);


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

  klass->list_children_properties =
      ges_timeline_element_get_children_properties;
  klass->lookup_child = _lookup_child;
  klass->set_child_property = _set_child_property;
  klass->set_child_property_full = _set_child_property_full;
  klass->get_natural_framerate = _get_natural_framerate;
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

    /* FIXME: should we really be allowing a user to set the name
     * "uriclip1" for, say, a GESTransition? The below code *does not*
     * capture this case (because the prefix does not match "transition").
     * If the user subsequently calls _set_name with name == NULL, on a
     * GESClip *for the first time*, then the GES library will
     * automatically choose the *same* name "uriclip1", but this is not
     * unique! */
    if (g_str_has_prefix (wanted_name, lowcase_type)) {
      guint64 tmpcount =
          g_ascii_strtoull (&wanted_name[strlen (lowcase_type)], NULL, 10);

      if (tmpcount > count) {
        count = tmpcount + 1;
        GST_DEBUG_OBJECT (self, "Using same naming %s but updated count to %i",
            wanted_name, count);
      } else if (tmpcount < count) {
        /* FIXME: this can unexpectedly change names given by the user
         * E.g. if "transition2" already exists, and a user then wants to
         * set a GESTransition to have the name "transition-custom" or
         * "transition 1 too many" then tmpcount would in fact be 0 or 1,
         * and the name would then be changed to "transition3"! */
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
 *       Internal and private helpers        *
 *********************************************/

GESTimelineElement *
ges_timeline_element_peak_toplevel (GESTimelineElement * self)
{
  GESTimelineElement *toplevel = self;

  while (toplevel->parent)
    toplevel = toplevel->parent;

  return toplevel;
}

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

static gboolean
emit_deep_notify_in_idle (EmitDeepNotifyInIdleData * data)
{
  g_signal_emit (data->self, ges_timeline_element_signals[DEEP_NOTIFY], 0,
      data->child, data->arg);

  gst_object_unref (data->child);
  g_param_spec_unref (data->arg);
  gst_object_unref (data->self);
  g_free (data);

  return FALSE;
}

static void
child_prop_changed_cb (GObject * child, GParamSpec * arg,
    GESTimelineElement * self)
{
  EmitDeepNotifyInIdleData *data;

  /* Emit "deep-notify" right away if in main thread */
  if (g_main_context_acquire (g_main_context_default ())) {
    g_main_context_release (g_main_context_default ());
    g_signal_emit (self, ges_timeline_element_signals[DEEP_NOTIFY], 0,
        child, arg);
    return;
  }

  data = g_new (EmitDeepNotifyInIdleData, 1);

  data->child = gst_object_ref (child);
  data->arg = g_param_spec_ref (arg);
  data->self = gst_object_ref (self);

  ges_idle_add ((GSourceFunc) emit_deep_notify_in_idle, data, NULL);
}

static gboolean
set_child_property_by_pspec (GESTimelineElement * self,
    GParamSpec * pspec, const GValue * value, GError ** error)
{
  GESTimelineElementClass *klass;
  GESTimelineElement *setter = self;
  ChildPropHandler *handler =
      g_hash_table_lookup (self->priv->children_props, pspec);

  if (!handler) {
    GST_ERROR_OBJECT (self, "The %s property doesn't exist", pspec->name);
    return FALSE;
  }

  if (handler->owner) {
    klass = GES_TIMELINE_ELEMENT_GET_CLASS (handler->owner);
    setter = handler->owner;
  } else {
    klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);
  }

  if (klass->set_child_property_full)
    return klass->set_child_property_full (setter, handler->child, pspec,
        value, error);

  g_assert (klass->set_child_property);
  klass->set_child_property (setter, handler->child, pspec, (GValue *) value);

  return TRUE;
}

gboolean
ges_timeline_element_add_child_property_full (GESTimelineElement * self,
    GESTimelineElement * owner, GParamSpec * pspec, GObject * child)
{
  gchar *signame;
  ChildPropHandler *handler;

  /* FIXME: allow the same pspec, provided the child is different. This
   * is important for containers that may have duplicate children
   * If this is changed, _remove_childs_child_property in ges-container.c
   * should be changed to reflect this.
   * We could hack around this by copying the pspec into a new instance
   * of GParamSpec, but there is no such GLib method, and it would break
   * the usage of get_..._from_pspec and set_..._from_pspec */
  if (g_hash_table_contains (self->priv->children_props, pspec)) {
    GST_INFO_OBJECT (self, "Child property already exists: %s", pspec->name);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Adding child property: %" GST_PTR_FORMAT "::%s",
      child, pspec->name);

  signame = g_strconcat ("notify::", pspec->name, NULL);
  handler = (ChildPropHandler *) g_new0 (ChildPropHandler, 1);
  handler->self = self;
  if (child == G_OBJECT (self) || child == G_OBJECT (owner))
    handler->child = child;
  else
    handler->child = gst_object_ref (child);
  handler->owner = owner;
  handler->handler_id =
      g_signal_connect (child, signame, G_CALLBACK (child_prop_changed_cb),
      self);
  g_hash_table_insert (self->priv->children_props, g_param_spec_ref (pspec),
      handler);

  g_signal_emit (self, ges_timeline_element_signals[CHILD_PROPERTY_ADDED], 0,
      child, pspec);

  g_free (signame);
  return TRUE;
}

GObject *
ges_timeline_element_get_child_from_child_property (GESTimelineElement * self,
    GParamSpec * pspec)
{
  ChildPropHandler *handler =
      g_hash_table_lookup (self->priv->children_props, pspec);
  if (handler)
    return handler->child;
  return NULL;
}


/*********************************************
 *            API implementation             *
 *********************************************/

/**
 * ges_timeline_element_set_parent:
 * @self: A #GESTimelineElement
 * @parent (nullable): New parent of @self
 *
 * Sets the #GESTimelineElement:parent for the element.
 *
 * This is used internally and you should normally not call this. A
 * #GESContainer will set the #GESTimelineElement:parent of its children
 * in ges_container_add() and ges_container_remove().
 *
 * Note, if @parent is not %NULL, @self must not already have a parent
 * set. Therefore, if you wish to switch parents, you will need to call
 * this function twice: first to set the parent to %NULL, and then to the
 * new parent.
 *
 * If @parent is not %NULL, you must ensure it already has a
 * (non-floating) reference to @self before calling this.
 *
 * Returns: %TRUE if @parent could be set for @self.
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
    /* FIXME: why are we sinking and then unreffing self when we do not
     * own it? */
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
    /* FIXME: why are we sinking and then unreffing self when we do not
     * own it? */
    gst_object_ref_sink (self);
    gst_object_unref (self);
    return FALSE;
  }
}

/**
 * ges_timeline_element_get_parent:
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:parent for the element.
 *
 * Returns: (transfer full) (nullable): The parent of @self, or %NULL if
 * @self has no parent.
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
 * @self: A #GESTimelineElement
 * @timeline (nullable): The #GESTimeline @self should be in
 *
 * Sets the #GESTimelineElement:timeline of the element.
 *
 * This is used internally and you should normally not call this. A
 * #GESClip will have its #GESTimelineElement:timeline set through its
 * #GESLayer. A #GESTrack will similarly take care of setting the
 * #GESTimelineElement:timeline of its #GESTrackElement-s. A #GESGroup
 * will adopt the same #GESTimelineElement:timeline as its children.
 *
 * If @timeline is %NULL, this will stop its current
 * #GESTimelineElement:timeline from tracking it, otherwise @timeline will
 * start tracking @self. Note, in the latter case, @self must not already
 * have a timeline set. Therefore, if you wish to switch timelines, you
 * will need to call this function twice: first to set the timeline to
 * %NULL, and then to the new timeline.
 *
 * Returns: %TRUE if @timeline could be set for @self.
 */
gboolean
ges_timeline_element_set_timeline (GESTimelineElement * self,
    GESTimeline * timeline)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (timeline == NULL || GES_IS_TIMELINE (timeline), FALSE);

  GST_DEBUG_OBJECT (self, "set timeline to %" GST_PTR_FORMAT, timeline);

  if (self->timeline == timeline)
    return TRUE;

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
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:timeline for the element.
 *
 * Returns: (transfer full) (nullable): The timeline of @self, or %NULL
 * if @self has no timeline.
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
 * @self: A #GESTimelineElement
 * @start: The desired start position of the element in its timeline
 *
 * Sets #GESTimelineElement:start for the element. If the element has a
 * parent, this will also move its siblings with the same shift.
 *
 * Whilst the element is part of a #GESTimeline, this is the same as
 * editing the element with ges_timeline_element_edit() under
 * #GES_EDIT_MODE_NORMAL with #GES_EDGE_NONE. In particular, the
 * #GESTimelineElement:start of the element may be snapped to a different
 * timeline time from the one given. In addition, setting may fail if it
 * would place the timeline in an unsupported configuration.
 *
 * Returns: %TRUE if @start could be set for @self.
 */
gboolean
ges_timeline_element_set_start (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;
  GESTimelineElement *toplevel_container, *parent;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (start), FALSE);

  if (self->start == start)
    return TRUE;

  GST_DEBUG_OBJECT (self, "current start: %" GST_TIME_FORMAT
      " new start: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (self)), GST_TIME_ARGS (start));

  if (self->timeline && !GES_TIMELINE_ELEMENT_BEING_EDITED (self))
    return ges_timeline_element_edit (self, NULL, -1, GES_EDIT_MODE_NORMAL,
        GES_EDGE_NONE, start);

  toplevel_container = ges_timeline_element_peak_toplevel (self);
  parent = self->parent;

  /* FIXME This should not belong to GESTimelineElement */
  /* only check if no timeline, otherwise the timeline-tree will handle this
   * check */
  if (!self->timeline && toplevel_container &&
      ((gint64) (_START (toplevel_container) + start - _START (self))) < 0 &&
      parent
      && GES_CONTAINER (parent)->children_control_mode == GES_CHILDREN_UPDATE) {
    GST_INFO_OBJECT (self,
        "Can not move the object as it would imply its "
        "container to have a negative start value");

    return FALSE;
  }

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);
  if (klass->set_start) {
    gint res = klass->set_start (self, start);
    if (res == FALSE)
      return FALSE;
    if (res == TRUE) {
      self->start = start;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_START]);
    }

    GST_DEBUG_OBJECT (self, "New start: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GES_TIMELINE_ELEMENT_START (self)));

    return TRUE;
  }

  GST_WARNING_OBJECT (self, "No set_start virtual method implementation"
      " on class %s. Can not set start %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (start));
  return FALSE;
}

/**
 * ges_timeline_element_set_inpoint:
 * @self: A #GESTimelineElement
 * @inpoint: The in-point, in internal time coordinates
 *
 * Sets #GESTimelineElement:in-point for the element. If the new in-point
 * is above the current #GESTimelineElement:max-duration of the element,
 * this method will fail.
 *
 * Returns: %TRUE if @inpoint could be set for @self.
 */
gboolean
ges_timeline_element_set_inpoint (GESTimelineElement * self,
    GstClockTime inpoint)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  GST_DEBUG_OBJECT (self, "current inpoint: %" GST_TIME_FORMAT
      " new inpoint: %" GST_TIME_FORMAT, GST_TIME_ARGS (self->inpoint),
      GST_TIME_ARGS (inpoint));

  if (G_UNLIKELY (inpoint == self->inpoint))
    return TRUE;

  if (GES_CLOCK_TIME_IS_LESS (self->maxduration, inpoint)) {
    GST_WARNING_OBJECT (self, "Can not set an in-point of %" GST_TIME_FORMAT
        " because it exceeds the element's max-duration: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (inpoint), GST_TIME_ARGS (self->maxduration));
    return FALSE;
  }

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->set_inpoint) {
    /* FIXME: Could we instead use g_object_freeze_notify() to prevent
     * duplicate notify signals? Rather than relying on the return value
     * being -1 for setting that succeeds but does not want a notify
     * signal because it will call this method on itself a second time. */
    if (!klass->set_inpoint (self, inpoint))
      return FALSE;

    self->inpoint = inpoint;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_INPOINT]);

    return TRUE;
  }

  GST_DEBUG_OBJECT (self, "No set_inpoint virtual method implementation"
      " on class %s. Can not set inpoint %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (inpoint));

  return FALSE;
}

/**
 * ges_timeline_element_set_max_duration:
 * @self: A #GESTimelineElement
 * @maxduration: The maximum duration, in internal time coordinates
 *
 * Sets #GESTimelineElement:max-duration for the element. If the new
 * maximum duration is below the current #GESTimelineElement:in-point of
 * the element, this method will fail.
 *
 * Returns: %TRUE if @maxduration could be set for @self.
 */
gboolean
ges_timeline_element_set_max_duration (GESTimelineElement * self,
    GstClockTime maxduration)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  GST_DEBUG_OBJECT (self, "current max-duration: %" GST_TIME_FORMAT
      " new max-duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (self->maxduration), GST_TIME_ARGS (maxduration));

  if (G_UNLIKELY (maxduration == self->maxduration))
    return TRUE;

  if (GES_CLOCK_TIME_IS_LESS (maxduration, self->inpoint)) {
    GST_WARNING_OBJECT (self, "Can not set a max-duration of %"
        GST_TIME_FORMAT " because it lies below the element's in-point: %"
        GST_TIME_FORMAT, GST_TIME_ARGS (maxduration),
        GST_TIME_ARGS (self->inpoint));
    return FALSE;
  }

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->set_max_duration) {
    if (!klass->set_max_duration (self, maxduration))
      return FALSE;
    self->maxduration = maxduration;
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MAX_DURATION]);

    return TRUE;
  }

  GST_DEBUG_OBJECT (self, "No set_max_duration virtual method implementation"
      " on class %s. Can not set max-duration  %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (maxduration));

  return FALSE;
}

/**
 * ges_timeline_element_set_duration:
 * @self: A #GESTimelineElement
 * @duration: The desired duration in its timeline
 *
 * Sets #GESTimelineElement:duration for the element.
 *
 * Whilst the element is part of a #GESTimeline, this is the same as
 * editing the element with ges_timeline_element_edit() under
 * #GES_EDIT_MODE_TRIM with #GES_EDGE_END. In particular, the
 * #GESTimelineElement:duration of the element may be snapped to a
 * different timeline time difference from the one given. In addition,
 * setting may fail if it would place the timeline in an unsupported
 * configuration, or the element does not have enough internal content to
 * last the desired duration.
 *
 * Returns: %TRUE if @duration could be set for @self.
 */
gboolean
ges_timeline_element_set_duration (GESTimelineElement * self,
    GstClockTime duration)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);

  if (duration == self->duration)
    return TRUE;

  if (self->timeline && !GES_TIMELINE_ELEMENT_BEING_EDITED (self))
    return ges_timeline_element_edit (self, NULL, -1, GES_EDIT_MODE_TRIM,
        GES_EDGE_END, self->start + duration);

  GST_DEBUG_OBJECT (self, "current duration: %" GST_TIME_FORMAT
      " new duration: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GES_TIMELINE_ELEMENT_DURATION (self)),
      GST_TIME_ARGS (duration));

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);
  if (klass->set_duration) {
    gint res = klass->set_duration (self, duration);
    if (res == FALSE)
      return FALSE;
    if (res == TRUE) {
      self->duration = duration;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DURATION]);
    }

    return TRUE;
  }

  GST_WARNING_OBJECT (self, "No set_duration virtual method implementation"
      " on class %s. Can not set duration %" GST_TIME_FORMAT,
      G_OBJECT_CLASS_NAME (klass), GST_TIME_ARGS (duration));
  return FALSE;
}

/**
 * ges_timeline_element_get_start:
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:start for the element.
 *
 * Returns: The start of @self (in nanoseconds).
 */
GstClockTime
ges_timeline_element_get_start (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->start;
}

/**
 * ges_timeline_element_get_inpoint:
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:in-point for the element.
 *
 * Returns: The in-point of @self (in nanoseconds).
 */
GstClockTime
ges_timeline_element_get_inpoint (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->inpoint;
}

/**
 * ges_timeline_element_get_duration:
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:duration for the element.
 *
 * Returns: The duration of @self (in nanoseconds).
 */
GstClockTime
ges_timeline_element_get_duration (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->duration;
}

/**
 * ges_timeline_element_get_max_duration:
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:max-duration for the element.
 *
 * Returns: The max-duration of @self (in nanoseconds).
 */
GstClockTime
ges_timeline_element_get_max_duration (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), GST_CLOCK_TIME_NONE);

  return self->maxduration;
}

/**
 * ges_timeline_element_get_priority:
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:priority for the element.
 *
 * Returns: The priority of @self.
 */
guint32
ges_timeline_element_get_priority (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), 0);

  return self->priority;
}

/**
 * ges_timeline_element_set_priority:
 * @self: A #GESTimelineElement
 * @priority: The priority
 *
 * Sets the priority of the element within the containing layer.
 *
 * Deprecated:1.10: All priority management is done by GES itself now.
 * To set #GESEffect priorities #ges_clip_set_top_effect_index should
 * be used.
 *
 * Returns: %TRUE if @priority could be set for @self.
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
 * @self: The #GESTimelineElement to ripple
 * @start: The new start time of @self in ripple mode
 *
 * Edits the start time of an element within its timeline in ripple mode.
 * See ges_timeline_element_edit() with #GES_EDIT_MODE_RIPPLE and
 * #GES_EDGE_NONE.
 *
 * Returns: %TRUE if the ripple edit of @self completed, %FALSE on
 * failure.
 */
gboolean
ges_timeline_element_ripple (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (start), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->ripple)
    return klass->ripple (self, start);

  return ges_timeline_element_edit (self, NULL, -1, GES_EDIT_MODE_RIPPLE,
      GES_EDGE_NONE, start);

  return FALSE;
}

/**
 * ges_timeline_element_ripple_end:
 * @self: The #GESTimelineElement to ripple
 * @end: The new end time of @self in ripple mode
 *
 * Edits the end time of an element within its timeline in ripple mode.
 * See ges_timeline_element_edit() with #GES_EDIT_MODE_RIPPLE and
 * #GES_EDGE_END.
 *
 * Returns: %TRUE if the ripple edit of @self completed, %FALSE on
 * failure.
 */
gboolean
ges_timeline_element_ripple_end (GESTimelineElement * self, GstClockTime end)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (end), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->ripple_end)
    return klass->ripple_end (self, end);

  return ges_timeline_element_edit (self, NULL, -1, GES_EDIT_MODE_RIPPLE,
      GES_EDGE_END, end);
}

/**
 * ges_timeline_element_roll_start:
 * @self: The #GESTimelineElement to roll
 * @start: The new start time of @self in roll mode
 *
 * Edits the start time of an element within its timeline in roll mode.
 * See ges_timeline_element_edit() with #GES_EDIT_MODE_ROLL and
 * #GES_EDGE_START.
 *
 * Returns: %TRUE if the roll edit of @self completed, %FALSE on failure.
 */
gboolean
ges_timeline_element_roll_start (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (start), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->roll_start)
    return klass->roll_start (self, start);

  return ges_timeline_element_edit (self, NULL, -1, GES_EDIT_MODE_ROLL,
      GES_EDGE_START, start);
}

/**
 * ges_timeline_element_roll_end:
 * @self: The #GESTimelineElement to roll
 * @end: The new end time of @self in roll mode
 *
 * Edits the end time of an element within its timeline in roll mode.
 * See ges_timeline_element_edit() with #GES_EDIT_MODE_ROLL and
 * #GES_EDGE_END.
 *
 * Returns: %TRUE if the roll edit of @self completed, %FALSE on failure.
 */
gboolean
ges_timeline_element_roll_end (GESTimelineElement * self, GstClockTime end)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (end), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->roll_end)
    return klass->roll_end (self, end);

  return ges_timeline_element_edit (self, NULL, -1, GES_EDIT_MODE_ROLL,
      GES_EDGE_END, end);
}

/**
 * ges_timeline_element_trim:
 * @self: The #GESTimelineElement to trim
 * @start: The new start time of @self in trim mode
 *
 * Edits the start time of an element within its timeline in trim mode.
 * See ges_timeline_element_edit() with #GES_EDIT_MODE_TRIM and
 * #GES_EDGE_START.
 *
 * Returns: %TRUE if the trim edit of @self completed, %FALSE on failure.
 */
gboolean
ges_timeline_element_trim (GESTimelineElement * self, GstClockTime start)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (start), FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  if (klass->trim)
    return klass->trim (self, start);

  return ges_timeline_element_edit (self, NULL, -1, GES_EDIT_MODE_TRIM,
      GES_EDGE_START, start);
}

/**
 * ges_timeline_element_copy:
 * @self: The #GESTimelineElement to copy
 * @deep: Whether the copy is needed for pasting
 *
 * Create a copy of @self. All the properties of @self are copied into
 * a new element, with the exception of #GESTimelineElement:parent,
 * #GESTimelineElement:timeline and #GESTimelineElement:name. Other data,
 * such the list of a #GESContainer's children, is **not** copied.
 *
 * If @deep is %TRUE, then the new element is prepared so that it can be
 * used in ges_timeline_element_paste() or ges_timeline_paste_element().
 * In the case of copying a #GESContainer, this ensures that the children
 * of @self will also be pasted. The new element should not be used for
 * anything else and can only be used **once** in a pasting operation. In
 * particular, the new element itself is not an actual 'deep' copy of
 * @self, but should be thought of as an intermediate object used for a
 * single paste operation.
 *
 * Returns: (transfer floating): The newly create element, copied from @self.
 */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS;       /* Start ignoring GParameter deprecation */
GESTimelineElement *
ges_timeline_element_copy (GESTimelineElement * self, gboolean deep)
{
  GESAsset *asset;
  GParamSpec **specs;
  GESTimelineElementClass *klass;
  guint n, n_specs;

  GESTimelineElement *ret = NULL;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  specs = g_object_class_list_properties (G_OBJECT_GET_CLASS (self), &n_specs);

  asset = ges_extractable_get_asset (GES_EXTRACTABLE (self));
  g_assert (asset);
  ret = GES_TIMELINE_ELEMENT (ges_asset_extract (asset, NULL));
  for (n = 0; n < n_specs; ++n) {
    /* We do not want the timeline or the name to be copied */
    if (g_strcmp0 (specs[n]->name, "parent") &&
        g_strcmp0 (specs[n]->name, "timeline") &&
        g_strcmp0 (specs[n]->name, "name") &&
        (specs[n]->flags & G_PARAM_READWRITE) == G_PARAM_READWRITE &&
        (specs[n]->flags & G_PARAM_CONSTRUCT_ONLY) == 0) {
      GValue v = G_VALUE_INIT;
      g_value_init (&v, specs[n]->value_type);
      g_object_get_property (G_OBJECT (self), specs[n]->name, &v);

      g_object_set_property (G_OBJECT (ret), specs[n]->name, &v);
      g_value_reset (&v);
    }
  }

  g_free (specs);
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

G_GNUC_END_IGNORE_DEPRECATIONS; /* End ignoring GParameter deprecation */

/**
 * ges_timeline_element_get_toplevel_parent:
 * @self: The #GESTimelineElement to get the toplevel parent from
 *
 * Gets the toplevel #GESTimelineElement:parent of the element.
 *
 * Returns: (transfer full): The toplevel parent of @self.
 */
GESTimelineElement *
ges_timeline_element_get_toplevel_parent (GESTimelineElement * self)
{
  GESTimelineElement *toplevel;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  toplevel = ges_timeline_element_peak_toplevel (self);

  return gst_object_ref (toplevel);
}

/**
 * ges_timeline_element_get_name:
 * @self: A #GESTimelineElement
 *
 * Gets the #GESTimelineElement:name for the element.
 *
 * Returns: (transfer full) (nullable): The name of @self.
 */
gchar *
ges_timeline_element_get_name (GESTimelineElement * self)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), NULL);

  return g_strdup (self->name);
}

/**
 * ges_timeline_element_set_name:
 * @self: A #GESTimelineElement
 * @name: (nullable): The name @self should take
 *
 * Sets the #GESTimelineElement:name for the element. If %NULL is given
 * for @name, then the library will instead generate a new name based on
 * the type name of the element, such as the name "uriclip3" for a
 * #GESUriClip, and will set that name instead.
 *
 * If @self already has a #GESTimelineElement:timeline, you should not
 * call this function with @name set to %NULL.
 *
 * You should ensure that, within each #GESTimeline, every element has a
 * unique name. If you call this function with @name as %NULL, then
 * the library should ensure that the set generated name is unique from
 * previously **generated** names. However, if you choose a @name that
 * interferes with the naming conventions of the library, the library will
 * attempt to ensure that the generated names will not conflict with the
 * chosen name, which may lead to a different name being set instead, but
 * the uniqueness between generated and user-chosen names is not
 * guaranteed.
 *
 * Returns: %TRUE if @name or a generated name for @self could be set.
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

    /* FIXME: if tmp == self then this means that we setting the name of
     * self to its existing name. There is no need to throw an error */
    if (tmp) {
      gst_object_unref (tmp);
      goto had_timeline;
    }

    timeline_remove_element (self->timeline, self);
    readd_to_timeline = TRUE;
  }
  /* FIXME: if self already has a timeline and name is NULL, then it also
   * needs to be re-added to the timeline (or, at least its entry in
   * timeline->priv->all_elements needs its key to be updated) using the
   * new generated name */

  _set_name (self, name);

  /* FIXME: the set name may not always be unique in a given timeline, see
   * _set_name(). This can cause timeline_add_element to fail! */
  if (readd_to_timeline)
    timeline_add_element (self->timeline, self);

  return result;

  /* error */
had_timeline:
  {
    /* FIXME: message is misleading. We are here if some other object in
     * the timeline was added under @name (see above) */
    GST_WARNING ("Object %s already in a timeline can't be renamed to %s",
        self->name, name);
    return FALSE;
  }
}

/**
 * ges_timeline_element_add_child_property:
 * @self: A #GESTimelineElement
 * @pspec: The specification for the property to add
 * @child: The #GstObject who the property belongs to
 *
 * Register a property of a child of the element to allow it to be
 * written with ges_timeline_element_set_child_property() and read with
 * ges_timeline_element_get_child_property(). A change in the property
 * will also appear in the #GESTimelineElement::deep-notify signal.
 *
 * @pspec should be unique from other children properties that have been
 * registered on @self.
 *
 * Returns: %TRUE if the property was successfully registered.
 */
gboolean
ges_timeline_element_add_child_property (GESTimelineElement * self,
    GParamSpec * pspec, GObject * child)
{
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (G_IS_PARAM_SPEC (pspec), FALSE);
  g_return_val_if_fail (G_IS_OBJECT (child), FALSE);

  return ges_timeline_element_add_child_property_full (self, NULL, pspec,
      child);
}

/**
 * ges_timeline_element_get_child_property_by_pspec:
 * @self: A #GESTimelineElement
 * @pspec: The specification of a registered child property to get
 * @value: (out): The return location for the value
 *
 * Gets the property of a child of the element. Specifically, the property
 * corresponding to the @pspec used in
 * ges_timeline_element_add_child_property() is copied into @value.
 */
void
ges_timeline_element_get_child_property_by_pspec (GESTimelineElement * self,
    GParamSpec * pspec, GValue * value)
{
  ChildPropHandler *handler;

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));
  g_return_if_fail (G_IS_PARAM_SPEC (pspec));

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
 * @self: A #GESTimelineElement
 * @pspec: The specification of a registered child property to set
 * @value: The value to set the property to
 *
 * Sets the property of a child of the element. Specifically, the property
 * corresponding to the @pspec used in
 * ges_timeline_element_add_child_property() is set to @value.
 */
void
ges_timeline_element_set_child_property_by_pspec (GESTimelineElement * self,
    GParamSpec * pspec, const GValue * value)
{
  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));
  g_return_if_fail (G_IS_PARAM_SPEC (pspec));

  set_child_property_by_pspec (self, pspec, value, NULL);
}

/**
 * ges_timeline_element_set_child_property_full:
 * @self: A #GESTimelineElement
 * @property_name: The name of the child property to set
 * @value: The value to set the property to
 * @error: (nullable): Return location for an error
 *
 * Sets the property of a child of the element.
 *
 * @property_name can either be in the format "prop-name" or
 * "TypeName::prop-name", where "prop-name" is the name of the property
 * to set (as used in g_object_set()), and "TypeName" is the type name of
 * the child (as returned by G_OBJECT_TYPE_NAME()). The latter format is
 * useful when two children of different types share the same property
 * name.
 *
 * The first child found with the given "prop-name" property that was
 * registered with ges_timeline_element_add_child_property() (and of the
 * type "TypeName", if it was given) will have the corresponding
 * property set to @value. Other children that may have also matched the
 * property name (and type name) are left unchanged!
 *
 * Returns: %TRUE if the property was found and set.
 * Since: 1.18
 */
gboolean
ges_timeline_element_set_child_property_full (GESTimelineElement * self,
    const gchar * property_name, const GValue * value, GError ** error)
{
  GParamSpec *pspec;
  GObject *child;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  if (!ges_timeline_element_lookup_child (self, property_name, &child, &pspec))
    goto not_found;

  return set_child_property_by_pspec (self, pspec, value, error);

not_found:
  {
    GST_WARNING_OBJECT (self, "The %s property doesn't exist", property_name);

    return FALSE;
  }
}

/**
 * ges_timeline_element_set_child_property:
 * @self: A #GESTimelineElement
 * @property_name: The name of the child property to set
 * @value: The value to set the property to
 *
 * See ges_timeline_element_set_child_property_full(), which also gives an
 * error.
 *
 * Note that ges_timeline_element_set_child_properties() may be more
 * convenient for C programming.
 *
 * Returns: %TRUE if the property was found and set.
 */
gboolean
ges_timeline_element_set_child_property (GESTimelineElement * self,
    const gchar * property_name, const GValue * value)
{
  return ges_timeline_element_set_child_property_full (self, property_name,
      value, NULL);
}

/**
 * ges_timeline_element_get_child_property:
 * @self: A #GESTimelineElement
 * @property_name: The name of the child property to get
 * @value: (out): The return location for the value
 *
 * Gets the property of a child of the element.
 *
 * @property_name can either be in the format "prop-name" or
 * "TypeName::prop-name", where "prop-name" is the name of the property
 * to get (as used in g_object_get()), and "TypeName" is the type name of
 * the child (as returned by G_OBJECT_TYPE_NAME()). The latter format is
 * useful when two children of different types share the same property
 * name.
 *
 * The first child found with the given "prop-name" property that was
 * registered with ges_timeline_element_add_child_property() (and of the
 * type "TypeName", if it was given) will have the corresponding
 * property copied into @value.
 *
 * Note that ges_timeline_element_get_child_properties() may be more
 * convenient for C programming.
 *
 * Returns: %TRUE if the property was found and copied to @value.
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

  /* FIXME: since GLib 2.60, g_object_get_property() will automatically
   * initialize the type */
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
 * @self: A #GESTimelineElement
 * @prop_name: The name of a child property
 * @child: (out) (optional) (transfer full): The return location for the
 * found child
 * @pspec: (out) (optional) (transfer full): The return location for the
 * specification of the child property
 *
 * Looks up a child property of the element.
 *
 * @prop_name can either be in the format "prop-name" or
 * "TypeName::prop-name", where "prop-name" is the name of the property
 * to look up (as used in g_object_get()), and "TypeName" is the type name
 * of the child (as returned by G_OBJECT_TYPE_NAME()). The latter format is
 * useful when two children of different types share the same property
 * name.
 *
 * The first child found with the given "prop-name" property that was
 * registered with ges_timeline_element_add_child_property() (and of the
 * type "TypeName", if it was given) will be passed to @child, and the
 * registered specification of this property will be passed to @pspec.
 *
 * Returns: %TRUE if a child corresponding to the property was found, in
 * which case @child and @pspec are set.
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
 * @self: A #GESTimelineElement
 * @first_property_name: The name of the first child property to set
 * @var_args: The value for the first property, followed optionally by more
 * name/value pairs, followed by %NULL
 *
 * Sets several of the children properties of the element. See
 * ges_timeline_element_set_child_property().
 */
void
ges_timeline_element_set_child_property_valist (GESTimelineElement * self,
    const gchar * first_property_name, va_list var_args)
{
  const gchar *name;
  GParamSpec *pspec;

  gchar *error = NULL;
  GValue value = { 0, };

  g_return_if_fail (GES_IS_TIMELINE_ELEMENT (self));

  name = first_property_name;

  /* Note: This part is in big part copied from the gst_child_object_set_valist
   * method. */

  /* iterate over pairs */
  while (name) {
    if (!ges_timeline_element_lookup_child (self, name, NULL, &pspec))
      goto not_found;

    G_VALUE_COLLECT_INIT (&value, pspec->value_type, var_args,
        G_VALUE_NOCOPY_CONTENTS, &error);

    if (error)
      goto cant_copy;

    set_child_property_by_pspec (self, pspec, &value, NULL);

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

    g_param_spec_unref (pspec);
    g_value_unset (&value);
    return;
  }
}

/**
 * ges_timeline_element_set_child_properties:
 * @self: A #GESTimelineElement
 * @first_property_name: The name of the first child property to set
 * @...: The value for the first property, followed optionally by more
 * name/value pairs, followed by %NULL
 *
 * Sets several of the children properties of the element. See
 * ges_timeline_element_set_child_property().
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
 * @self: A #GESTimelineElement
 * @first_property_name: The name of the first child property to get
 * @var_args: The return location for the first property, followed
 * optionally by more name/return location pairs, followed by %NULL
 *
 * Gets several of the children properties of the element. See
 * ges_timeline_element_get_child_property().
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
 * @self: A #GESTimelineElement
 * @n_properties: (out): The return location for the length of the
 * returned array
 *
 * Get a list of children properties of the element, which is a list of
 * all the specifications passed to
 * ges_timeline_element_add_child_property().
 *
 * Returns: (transfer full) (array length=n_properties): An array of
 * #GParamSpec corresponding to the child properties of @self, or %NULL if
 * something went wrong.
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
 * @self: A #GESTimelineElement
 * @first_property_name: The name of the first child property to get
 * @...: The return location for the first property, followed
 * optionally by more name/return location pairs, followed by %NULL
 *
 * Gets several of the children properties of the element. See
 * ges_timeline_element_get_child_property().
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

/**
 * ges_timeline_element_remove_child_property:
 * @self: A #GESTimelineElement
 * @pspec: The specification for the property to remove
 *
 * Remove a child property from the element. @pspec should be a
 * specification that was passed to
 * ges_timeline_element_add_child_property(). The corresponding property
 * will no longer be registered as a child property for the element.
 *
 * Returns: %TRUE if the property was successfully un-registered for @self.
 */
gboolean
ges_timeline_element_remove_child_property (GESTimelineElement * self,
    GParamSpec * pspec)
{
  gpointer key, value;
  GParamSpec *found_pspec;
  ChildPropHandler *handler;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (G_IS_PARAM_SPEC (pspec), FALSE);

  if (!g_hash_table_lookup_extended (self->priv->children_props, pspec,
          &key, &value)) {
    GST_WARNING_OBJECT (self, "No child property with pspec %p (%s) found",
        pspec, pspec->name);
    return FALSE;
  }
  g_hash_table_steal (self->priv->children_props, pspec);
  found_pspec = G_PARAM_SPEC (key);
  handler = (ChildPropHandler *) value;

  g_signal_emit (self, ges_timeline_element_signals[CHILD_PROPERTY_REMOVED], 0,
      handler->child, found_pspec);

  g_param_spec_unref (found_pspec);
  _child_prop_handler_free (handler);

  return TRUE;
}

/**
 * ges_timeline_element_get_track_types:
 * @self: A #GESTimelineElement
 *
 * Gets the track types that the element can interact with, i.e. the type
 * of #GESTrack it can exist in, or will create #GESTrackElement-s for.
 *
 * Returns: The track types that @self supports.
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
 * @paste_position: The position in the timeline @element should be pasted
 * to, i.e. the #GESTimelineElement:start value for the pasted element.
 *
 * Paste an element inside the same timeline and layer as @self. @self
 * **must** be the return of ges_timeline_element_copy() with `deep=TRUE`,
 * and it should not be changed before pasting.
 * @self is not placed in the timeline, instead a new element is created,
 * alike to the originally copied element. Note that the originally
 * copied element must stay within the same timeline and layer, at both
 * the point of copying and pasting.
 *
 * Pasting may fail if it would place the timeline in an unsupported
 * configuration.
 *
 * After calling this function @element should not be used. In particular,
 * @element can **not** be pasted again. Instead, you can copy the
 * returned element and paste that copy (although, this is only possible
 * if the paste was successful).
 *
 * See also ges_timeline_paste_element().
 *
 * Returns: (transfer full) (nullable): The newly created element, or
 * %NULL if pasting fails.
 *
 * Since: 1.6.0
 */
GESTimelineElement *
ges_timeline_element_paste (GESTimelineElement * self,
    GstClockTime paste_position)
{
  GESTimelineElement *res;
  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (paste_position), FALSE);

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

  return res ? g_object_ref_sink (res) : res;
}

/**
 * ges_timeline_element_get_layer_priority:
 * @self: A #GESTimelineElement
 *
 * Gets the priority of the layer the element is in. A #GESGroup may span
 * several layers, so this would return the highest priority (numerically,
 * the smallest) amongst them.
 *
 * Returns: The priority of the layer @self is in, or
 * #GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY if @self does not exist in a
 * layer.
 *
 * Since: 1.16
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

/**
 * ges_timeline_element_edit_full:
 * @self: The #GESTimelineElement to edit
 * @new_layer_priority: The priority/index of the layer @self should be
 * moved to. -1 means no move
 * @mode: The edit mode
 * @edge: The edge of @self where the edit should occur
 * @position: The edit position: a new location for the edge of @self
 * (in nanoseconds) in the timeline coordinates
 * @error: Return location for an error
 *
 * Edits the element within its timeline by adjusting its
 * #GESTimelineElement:start, #GESTimelineElement:duration or
 * #GESTimelineElement:in-point, and potentially doing the same for
 * other elements in the timeline. See #GESEditMode for details about each
 * edit mode. An edit may fail if it would place one of these properties
 * out of bounds, or if it would place the timeline in an unsupported
 * configuration.
 *
 * Note that if you act on a #GESTrackElement, this will edit its parent
 * #GESClip instead. Moreover, for any #GESTimelineElement, if you select
 * #GES_EDGE_NONE for #GES_EDIT_MODE_NORMAL or #GES_EDIT_MODE_RIPPLE, this
 * will edit the toplevel instead, but still in such a way as to make the
 * #GESTimelineElement:start of @self reach the edit @position.
 *
 * Note that if the element's timeline has a
 * #GESTimeline:snapping-distance set, then the edit position may be
 * snapped to the edge of some element under the edited element.
 *
 * @new_layer_priority can be used to switch @self, and other elements
 * moved by the edit, to a new layer. New layers may be be created if the
 * the corresponding layer priority/index does not yet exist for the
 * timeline.
 *
 * Returns: %TRUE if the edit of @self completed, %FALSE on failure.
 * Since: 1.18
 */

gboolean
ges_timeline_element_edit_full (GESTimelineElement * self,
    gint64 new_layer_priority, GESEditMode mode, GESEdge edge, guint64 position,
    GError ** error)
{
  GESTimeline *timeline;
  guint32 layer_prio;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (GST_CLOCK_TIME_IS_VALID (position), FALSE);
  g_return_val_if_fail (!error || !*error, FALSE);

  timeline = GES_TIMELINE_ELEMENT_TIMELINE (self);
  g_return_val_if_fail (timeline, FALSE);

  layer_prio = GES_TIMELINE_ELEMENT_LAYER_PRIORITY (self);

  if (new_layer_priority < 0)
    new_layer_priority = layer_prio;

  GST_DEBUG_OBJECT (self, "Editing %s at edge %s to position %"
      GST_TIME_FORMAT " under %s mode, and to layer %" G_GINT64_FORMAT,
      self->name, ges_edge_name (edge), GST_TIME_ARGS (position),
      ges_edit_mode_name (mode), new_layer_priority);

  return ges_timeline_edit (timeline, self, new_layer_priority, mode,
      edge, position, error);
}

/**
 * ges_timeline_element_edit:
 * @self: The #GESTimelineElement to edit
 * @layers: (element-type GESLayer) (nullable): A whitelist of layers
 * where the edit can be performed, %NULL allows all layers in the
 * timeline.
 * @new_layer_priority: The priority/index of the layer @self should be
 * moved to. -1 means no move
 * @mode: The edit mode
 * @edge: The edge of @self where the edit should occur
 * @position: The edit position: a new location for the edge of @self
 * (in nanoseconds) in the timeline coordinates
 *
 * See ges_timeline_element_edit_full(), which also gives an error.
 *
 * Note that the @layers argument is currently ignored, so you should
 * just pass %NULL.
 *
 * Returns: %TRUE if the edit of @self completed, %FALSE on failure.
 *
 * Since: 1.18
 */

/* FIXME: handle the layers argument. Currently we always treat it as if
 * it is NULL in the ges-timeline code */
gboolean
ges_timeline_element_edit (GESTimelineElement * self, GList * layers,
    gint64 new_layer_priority, GESEditMode mode, GESEdge edge, guint64 position)
{
  return ges_timeline_element_edit_full (self, new_layer_priority, mode, edge,
      position, NULL);
}

/**
 * ges_timeline_element_get_natural_framerate:
 * @self: The #GESTimelineElement to get "natural" framerate from
 * @framerate_n: (out): The framerate numerator
 * @framerate_d: (out): The framerate denominator
 *
 * Get the "natural" framerate of @self. This is to say, for example
 * for a #GESVideoUriSource the framerate of the source.
 *
 * Note that a #GESAudioSource may also have a natural framerate if it derives
 * from the same #GESSourceClip asset as a #GESVideoSource, and its value will
 * be that of the video source. For example, if the uri of a #GESUriClip points
 * to a file that contains both a video and audio stream, then the corresponding
 * #GESAudioUriSource will share the natural framerate of the corresponding
 * #GESVideoUriSource.
 *
 * Returns: Whether @self has a natural framerate or not, @framerate_n
 * and @framerate_d will be set to, respectively, 0 and -1 if it is
 * not the case.
 *
 * Since: 1.18
 */
gboolean
ges_timeline_element_get_natural_framerate (GESTimelineElement * self,
    gint * framerate_n, gint * framerate_d)
{
  GESTimelineElementClass *klass;

  g_return_val_if_fail (GES_IS_TIMELINE_ELEMENT (self), FALSE);
  g_return_val_if_fail (framerate_n && framerate_d, FALSE);

  klass = GES_TIMELINE_ELEMENT_GET_CLASS (self);

  *framerate_n = 0;
  *framerate_d = -1;
  return klass->get_natural_framerate (self, framerate_n, framerate_d);
}
