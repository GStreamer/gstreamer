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
 * SECTION:gestrackelement
 * @title: GESTrackElement
 * @short_description: Base Class for the elements of a #GESTrack
 *
 * A #GESTrackElement is a #GESTimelineElement that specifically belongs
 * to a single #GESTrack of its #GESTimelineElement:timeline. Its
 * #GESTimelineElement:start and #GESTimelineElement:duration specify its
 * temporal extent in the track. Specifically, a track element wraps some
 * nleobject, such as an #nlesource or #nleoperation, which can be
 * retrieved with ges_track_element_get_nleobject(), and its
 * #GESTimelineElement:start, #GESTimelineElement:duration,
 * #GESTimelineElement:in-point, #GESTimelineElement:priority and
 * #GESTrackElement:active properties expose the corresponding nleobject
 * properties. When a track element is added to a track, its nleobject is
 * added to the corresponding #nlecomposition that the track wraps.
 *
 * Most users will not have to work directly with track elements since a
 * #GESClip will automatically create track elements for its timeline's
 * tracks and take responsibility for updating them. The only track
 * elements that are not automatically created by clips, but a user is
 * likely to want to create, are #GESEffect-s.
 *
 * ## Control Bindings for Children Properties
 *
 * You can set up control bindings for a track element child property
 * using ges_track_element_set_control_source(). A
 * #GstTimedValueControlSource should specify the timed values using the
 * internal source coordinates (see #GESTimelineElement). By default,
 * these will be updated to lie between the #GESTimelineElement:in-point
 * and out-point of the element. This can be switched off by setting
 * #GESTrackElement:auto-clamp-control-sources to %FALSE.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-extractable.h"
#include "ges-track-element.h"
#include "ges-clip.h"
#include "ges-meta-container.h"

struct _GESTrackElementPrivate
{
  GESTrackType track_type;

  GstElement *nleobject;        /* The NleObject */
  GstElement *element;          /* The element contained in the nleobject (can be NULL) */

  GESTrack *track;
  gboolean has_internal_source_forbidden;
  gboolean has_internal_source;

  gboolean layer_active;

  GHashTable *bindings_hashtable;       /* We need this if we want to be able to serialize
                                           and deserialize keyframes */
  GESAsset *creator_asset;

  GstClockTime outpoint;
  gboolean freeze_control_sources;
  gboolean auto_clamp_control_sources;
};

enum
{
  PROP_0,
  PROP_ACTIVE,
  PROP_TRACK_TYPE,
  PROP_TRACK,
  PROP_HAS_INTERNAL_SOURCE,
  PROP_AUTO_CLAMP_CONTROL_SOURCES,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

enum
{
  CONTROL_BINDING_ADDED,
  CONTROL_BINDING_REMOVED,
  LAST_SIGNAL
};

static guint ges_track_element_signals[LAST_SIGNAL] = { 0 };

static void ges_track_element_set_asset (GESExtractable * extractable,
    GESAsset * asset);

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->set_asset = ges_track_element_set_asset;
  iface->asset_type = GES_TYPE_TRACK_ELEMENT_ASSET;
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GESTrackElement, ges_track_element,
    GES_TYPE_TIMELINE_ELEMENT, G_ADD_PRIVATE (GESTrackElement)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

static GstElement *ges_track_element_create_gnl_object_func (GESTrackElement *
    object);

static gboolean _set_start (GESTimelineElement * element, GstClockTime start);
static gboolean _set_inpoint (GESTimelineElement * element,
    GstClockTime inpoint);
static gboolean _set_duration (GESTimelineElement * element,
    GstClockTime duration);
static gboolean _set_max_duration (GESTimelineElement * element,
    GstClockTime max_duration);
static gboolean _set_priority (GESTimelineElement * element, guint32 priority);
GESTrackType _get_track_types (GESTimelineElement * object);

static gboolean
_lookup_child (GESTrackElement * object,
    const gchar * prop_name, GstElement ** element, GParamSpec ** pspec)
{
  return
      GES_TIMELINE_ELEMENT_GET_CLASS (object)->lookup_child
      (GES_TIMELINE_ELEMENT (object), prop_name, (GObject **) element, pspec);
}

static gboolean
strv_find_str (const gchar ** strv, const char *str)
{
  guint i;

  if (strv == NULL)
    return FALSE;

  for (i = 0; strv[i]; i++) {
    if (g_strcmp0 (strv[i], str) == 0)
      return TRUE;
  }

  return FALSE;
}

static guint32
_get_layer_priority (GESTimelineElement * element)
{
  if (!element->parent)
    return GES_TIMELINE_ELEMENT_NO_LAYER_PRIORITY;

  return ges_timeline_element_get_layer_priority (element->parent);
}

static gboolean
_get_natural_framerate (GESTimelineElement * self, gint * framerate_n,
    gint * framerate_d)
{
  GESAsset *asset = ges_extractable_get_asset (GES_EXTRACTABLE (self));

  /* FIXME: asset should **never** be NULL */
  if (asset &&
      ges_track_element_asset_get_natural_framerate (GES_TRACK_ELEMENT_ASSET
          (asset), framerate_n, framerate_d))
    return TRUE;

  if (self->parent)
    return ges_timeline_element_get_natural_framerate (self->parent,
        framerate_n, framerate_d);

  return FALSE;
}

static void
ges_track_element_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GESTrackElement *track_element = GES_TRACK_ELEMENT (object);

  switch (property_id) {
    case PROP_ACTIVE:
      g_value_set_boolean (value, ges_track_element_is_active (track_element));
      break;
    case PROP_TRACK_TYPE:
      g_value_set_flags (value, track_element->priv->track_type);
      break;
    case PROP_TRACK:
      g_value_set_object (value, track_element->priv->track);
      break;
    case PROP_HAS_INTERNAL_SOURCE:
      g_value_set_boolean (value,
          ges_track_element_has_internal_source (track_element));
      break;
    case PROP_AUTO_CLAMP_CONTROL_SOURCES:
      g_value_set_boolean (value,
          ges_track_element_get_auto_clamp_control_sources (track_element));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_element_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTrackElement *track_element = GES_TRACK_ELEMENT (object);

  switch (property_id) {
    case PROP_ACTIVE:
      ges_track_element_set_active (track_element, g_value_get_boolean (value));
      break;
    case PROP_TRACK_TYPE:
      ges_track_element_set_track_type (track_element,
          g_value_get_flags (value));
      break;
    case PROP_HAS_INTERNAL_SOURCE:
      ges_track_element_set_has_internal_source (track_element,
          g_value_get_boolean (value));
      break;
    case PROP_AUTO_CLAMP_CONTROL_SOURCES:
      ges_track_element_set_auto_clamp_control_sources (track_element,
          g_value_get_boolean (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_element_dispose (GObject * object)
{
  GESTrackElement *element = GES_TRACK_ELEMENT (object);
  GESTrackElementPrivate *priv = element->priv;

  if (priv->bindings_hashtable)
    g_hash_table_destroy (priv->bindings_hashtable);

  if (priv->nleobject) {
    GstState cstate;

    if (priv->track != NULL) {
      g_error ("%p Still in %p, this means that you forgot"
          " to remove it from the GESTrack it is contained in. You always need"
          " to remove a GESTrackElement from its track before dropping the last"
          " reference\n"
          "This problem may also be caused by a refcounting bug in"
          " the application or GES itself.", object, priv->track);
      gst_element_get_state (priv->nleobject, &cstate, NULL, 0);
      if (cstate != GST_STATE_NULL)
        gst_element_set_state (priv->nleobject, GST_STATE_NULL);
    }

    g_object_set_qdata (G_OBJECT (priv->nleobject),
        NLE_OBJECT_TRACK_ELEMENT_QUARK, NULL);
    gst_object_unref (priv->nleobject);
    priv->nleobject = NULL;
  }

  G_OBJECT_CLASS (ges_track_element_parent_class)->dispose (object);
}

static void
ges_track_element_set_asset (GESExtractable * extractable, GESAsset * asset)
{
  GESTrackElementClass *class;
  GstElement *nleobject;
  gchar *tmp;
  GESTrackElement *object = GES_TRACK_ELEMENT (extractable);

  if (ges_track_element_get_track_type (object) == GES_TRACK_TYPE_UNKNOWN) {
    ges_track_element_set_track_type (object,
        ges_track_element_asset_get_track_type (GES_TRACK_ELEMENT_ASSET
            (asset)));
  }

  class = GES_TRACK_ELEMENT_GET_CLASS (object);
  g_assert (class->create_gnl_object);

  nleobject = class->create_gnl_object (object);
  if (G_UNLIKELY (nleobject == NULL)) {
    GST_ERROR_OBJECT (object, "Could not create NleObject");

    return;
  }

  tmp = g_strdup_printf ("%s:%s", G_OBJECT_TYPE_NAME (object),
      GST_OBJECT_NAME (nleobject));
  gst_object_set_name (GST_OBJECT (nleobject), tmp);
  g_free (tmp);

  if (!object->priv->nleobject) {
    object->priv->nleobject = gst_object_ref (nleobject);
    g_object_set_qdata (G_OBJECT (nleobject), NLE_OBJECT_TRACK_ELEMENT_QUARK,
        object);
  }

  /* Set some properties on the NleObject */
  g_object_set (object->priv->nleobject,
      "start", GES_TIMELINE_ELEMENT_START (object),
      "inpoint", GES_TIMELINE_ELEMENT_INPOINT (object),
      "duration", GES_TIMELINE_ELEMENT_DURATION (object),
      "priority", GES_TIMELINE_ELEMENT_PRIORITY (object),
      "active", object->active & object->priv->layer_active, NULL);
}

static void
ges_track_element_constructed (GObject * object)
{
  GESTrackElement *self = GES_TRACK_ELEMENT (object);

  if (self->priv->track_type == GES_TRACK_TYPE_UNKNOWN)
    ges_track_element_set_track_type (GES_TRACK_ELEMENT (object),
        GES_TRACK_ELEMENT_GET_CLASS (object)->ABI.abi.default_track_type);

  /* set the default has-internal-source */
  ges_track_element_set_has_internal_source (self,
      GES_TRACK_ELEMENT_CLASS_DEFAULT_HAS_INTERNAL_SOURCE
      (GES_TRACK_ELEMENT_GET_CLASS (object)));

  G_OBJECT_CLASS (ges_track_element_parent_class)->constructed (object);
}

static void
ges_track_element_class_init (GESTrackElementClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineElementClass *element_class = GES_TIMELINE_ELEMENT_CLASS (klass);

  object_class->get_property = ges_track_element_get_property;
  object_class->set_property = ges_track_element_set_property;
  object_class->dispose = ges_track_element_dispose;
  object_class->constructed = ges_track_element_constructed;

  /**
   * GESTrackElement:active:
   *
   * Whether the effect of the element should be applied in its
   * #GESTrackElement:track. If set to %FALSE, it will not be used in
   * the output of the track.
   */
  properties[PROP_ACTIVE] =
      g_param_spec_boolean ("active", "Active", "Use object in output", TRUE,
      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (object_class, PROP_ACTIVE,
      properties[PROP_ACTIVE]);

  /**
   * GESTrackElement:track-type:
   *
   * The track type of the element, which determines the type of track the
   * element can be added to (see #GESTrack:track-type). This should
   * correspond to the type of data that the element can produce or
   * process.
   */
  properties[PROP_TRACK_TYPE] = g_param_spec_flags ("track-type", "Track Type",
      "The track type of the object", GES_TYPE_TRACK_TYPE,
      GES_TRACK_TYPE_UNKNOWN, G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
      G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (object_class, PROP_TRACK_TYPE,
      properties[PROP_TRACK_TYPE]);

  /**
   * GESTrackElement:track:
   *
   * The track that this element belongs to, or %NULL if it does not
   * belong to a track.
   */
  properties[PROP_TRACK] = g_param_spec_object ("track", "Track",
      "The track the object is in", GES_TYPE_TRACK, G_PARAM_READABLE);
  g_object_class_install_property (object_class, PROP_TRACK,
      properties[PROP_TRACK]);

  /**
   * GESTrackElement:has-internal-source:
   *
   * This property is used to determine whether the 'internal time'
   * properties of the element have any meaning. In particular, unless
   * this is set to %TRUE, the #GESTimelineElement:in-point and
   * #GESTimelineElement:max-duration can not be set to any value other
   * than the default 0 and #GST_CLOCK_TIME_NONE, respectively.
   *
   * If an element has some *internal* *timed* source #GstElement that it
   * reads stream data from as part of its function in a #GESTrack, then
   * you'll likely want to set this to %TRUE to allow the
   * #GESTimelineElement:in-point and #GESTimelineElement:max-duration to
   * be set.
   *
   * The default value is determined by the #GESTrackElementClass
   * @default_has_internal_source class property. For most
   * #GESSourceClass-es, this will be %TRUE, with the exception of those
   * that have a potentially *static* source, such as #GESImageSourceClass
   * and #GESTitleSourceClass. Otherwise, this will usually be %FALSE.
   *
   * For most #GESOperation-s you will likely want to leave this set to
   * %FALSE. The exception may be for an operation that reads some stream
   * data from some private internal source as part of manipulating the
   * input data from the usual linked upstream #GESTrackElement.
   *
   * For example, you may want to set this to %TRUE for a
   * #GES_TRACK_TYPE_VIDEO operation that wraps a #textoverlay that reads
   * from a subtitle file and places its text on top of the received video
   * data. The #GESTimelineElement:in-point of the element would be used
   * to shift the initial seek time on the #textoverlay away from 0, and
   * the #GESTimelineElement:max-duration could be set to reflect the
   * time at which the subtitle file runs out of data.
   *
   * Note that GES can not support track elements that have both internal
   * content and manipulate the timing of their data streams (time
   * effects).
   *
   * Since: 1.18
   */
  properties[PROP_HAS_INTERNAL_SOURCE] =
      g_param_spec_boolean ("has-internal-source", "Has Internal Source",
      "Whether the element has some internal source of stream data", FALSE,
      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (object_class, PROP_HAS_INTERNAL_SOURCE,
      properties[PROP_HAS_INTERNAL_SOURCE]);

  /**
   * GESTrackElement:auto-clamp-control-sources:
   *
   * Whether the control sources on the element (see
   * ges_track_element_set_control_source()) will be automatically
   * updated whenever the #GESTimelineElement:in-point or out-point of the
   * element change in value.
   *
   * See ges_track_element_clamp_control_source() for how this is done
   * per control source.
   *
   * Default value: %TRUE
   *
   * Since: 1.18
   */
  properties[PROP_AUTO_CLAMP_CONTROL_SOURCES] =
      g_param_spec_boolean ("auto-clamp-control-sources",
      "Auto-Clamp Control Sources", "Whether to automatically update the "
      "control sources with a change in in-point or out-point", TRUE,
      G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY);
  g_object_class_install_property (object_class,
      PROP_AUTO_CLAMP_CONTROL_SOURCES,
      properties[PROP_AUTO_CLAMP_CONTROL_SOURCES]);

  /**
   * GESTrackElement::control-binding-added:
   * @track_element: A #GESTrackElement
   * @control_binding: The control binding that has been added
   *
   * This is emitted when a control binding is added to a child property
   * of the track element.
   */
  ges_track_element_signals[CONTROL_BINDING_ADDED] =
      g_signal_new ("control-binding-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_CONTROL_BINDING);

  /**
   * GESTrackElement::control-binding-removed:
   * @track_element: A #GESTrackElement
   * @control_binding: The control binding that has been removed
   *
   * This is emitted when a control binding is removed from a child
   * property of the track element.
   */
  ges_track_element_signals[CONTROL_BINDING_REMOVED] =
      g_signal_new ("control-binding-removed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
      G_TYPE_NONE, 1, GST_TYPE_CONTROL_BINDING);

  element_class->set_start = _set_start;
  element_class->set_duration = _set_duration;
  element_class->set_inpoint = _set_inpoint;
  element_class->set_max_duration = _set_max_duration;
  element_class->set_priority = _set_priority;
  element_class->get_track_types = _get_track_types;
  element_class->deep_copy = ges_track_element_copy_properties;
  element_class->get_layer_priority = _get_layer_priority;
  element_class->get_natural_framerate = _get_natural_framerate;

  klass->create_gnl_object = ges_track_element_create_gnl_object_func;
  klass->lookup_child = _lookup_child;
  klass->ABI.abi.default_track_type = GES_TRACK_TYPE_UNKNOWN;
}

static void
ges_track_element_init (GESTrackElement * self)
{
  GESTrackElementPrivate *priv = self->priv =
      ges_track_element_get_instance_private (self);

  /* Sane default values */
  GES_TIMELINE_ELEMENT_START (self) = 0;
  GES_TIMELINE_ELEMENT_INPOINT (self) = 0;
  GES_TIMELINE_ELEMENT_DURATION (self) = GST_SECOND;
  GES_TIMELINE_ELEMENT_PRIORITY (self) = 0;
  self->active = TRUE;
  self->priv->layer_active = TRUE;

  priv->bindings_hashtable = g_hash_table_new_full (g_str_hash, g_str_equal,
      g_free, NULL);

  /* NOTE: make sure we set this flag to TRUE so that
   *   g_object_new (, "has-internal-source", TRUE, "in-point", 10, NULL);
   * can succeed. The problem is that "in-point" will always be set before
   * has-internal-source is set, so we first assume that it is TRUE.
   * Note that if we call
   *   g_object_new (, "has-internal-source", FALSE, "in-point", 10, NULL);
   * then "in-point" will be allowed to be set, but then when
   * "has-internal-source" is later set to TRUE, this will set the
   * "in-point" back to 0.
   * This is particularly needed for the ges_timeline_element_copy method
   * because it calls g_object_new_with_properties.
   */
  self->priv->has_internal_source = TRUE;

  self->priv->outpoint = GST_CLOCK_TIME_NONE;
  self->priv->auto_clamp_control_sources = TRUE;
}

static gfloat
interpolate_values_for_position (GstTimedValue * first_value,
    GstTimedValue * second_value, guint64 position, gboolean absolute)
{
  gfloat diff;
  GstClockTime interval;
  gfloat value_at_pos;

  g_assert (second_value || first_value);

  if (first_value == NULL)
    return second_value->value;

  if (second_value == NULL)
    return first_value->value;

  diff = second_value->value - first_value->value;
  interval = second_value->timestamp - first_value->timestamp;

  /* FIXME: properly support non-linear timed control sources */
  if (position > first_value->timestamp)
    value_at_pos =
        first_value->value + ((float) (position -
            first_value->timestamp) / (float) interval) * diff;
  else
    value_at_pos =
        first_value->value - ((float) (first_value->timestamp -
            position) / (float) interval) * diff;

  if (!absolute)
    value_at_pos = CLAMP (value_at_pos, 0.0, 1.0);

  return value_at_pos;
}

static void
_update_control_source (GstTimedValueControlSource * source, gboolean absolute,
    GstClockTime inpoint, GstClockTime outpoint)
{
  GList *values, *tmp;
  GstTimedValue *last, *first, *prev = NULL, *next = NULL;
  gfloat value_at_pos;

  if (inpoint == outpoint) {
    gst_timed_value_control_source_unset_all (source);
    return;
  }

  values = gst_timed_value_control_source_get_all (source);

  if (g_list_length (values) == 0)
    return;

  first = values->data;

  for (tmp = values->next; tmp; tmp = tmp->next) {
    next = tmp->data;

    if (next->timestamp == inpoint) {
      /* just leave this value in place */
      first = NULL;
      break;
    }
    if (next->timestamp > inpoint)
      break;
  }
  g_list_free (values);

  if (first) {
    value_at_pos =
        interpolate_values_for_position (first, next, inpoint, absolute);
    gst_timed_value_control_source_unset (source, first->timestamp);
    gst_timed_value_control_source_set (source, inpoint, value_at_pos);
  }

  if (GST_CLOCK_TIME_IS_VALID (outpoint)) {
    values = gst_timed_value_control_source_get_all (source);

    last = g_list_last (values)->data;

    for (tmp = g_list_last (values)->prev; tmp; tmp = tmp->prev) {
      prev = tmp->data;

      if (prev->timestamp == outpoint) {
        /* leave this value in place */
        last = NULL;
        break;
      }
      if (prev->timestamp < outpoint)
        break;
    }
    g_list_free (values);

    if (last) {
      value_at_pos =
          interpolate_values_for_position (prev, last, outpoint, absolute);

      gst_timed_value_control_source_unset (source, last->timestamp);
      gst_timed_value_control_source_set (source, outpoint, value_at_pos);
    }
  }

  values = gst_timed_value_control_source_get_all (source);

  for (tmp = values; tmp; tmp = tmp->next) {
    GstTimedValue *value = tmp->data;
    if (value->timestamp < inpoint)
      gst_timed_value_control_source_unset (source, value->timestamp);
    else if (GST_CLOCK_TIME_IS_VALID (outpoint) && value->timestamp > outpoint)
      gst_timed_value_control_source_unset (source, value->timestamp);
  }
  g_list_free (values);
}

static void
_update_control_bindings (GESTrackElement * self, GstClockTime inpoint,
    GstClockTime outpoint)
{
  gchar *name;
  GstControlBinding *binding;
  GstControlSource *source;
  gboolean absolute;
  gpointer value, key;
  GHashTableIter iter;

  if (self->priv->freeze_control_sources)
    return;

  g_hash_table_iter_init (&iter, self->priv->bindings_hashtable);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    binding = value;
    name = key;
    g_object_get (binding, "control-source", &source, "absolute", &absolute,
        NULL);

    if (!GST_IS_TIMED_VALUE_CONTROL_SOURCE (source)) {
      GST_INFO_OBJECT (self, "Not updating %s because it does not have a"
          " timed value control source", name);
      gst_object_unref (source);
      continue;
    }

    _update_control_source (GST_TIMED_VALUE_CONTROL_SOURCE (source), absolute,
        inpoint, outpoint);
    gst_object_unref (source);
  }
}

static gboolean
_set_start (GESTimelineElement * element, GstClockTime start)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);

  g_return_val_if_fail (object->priv->nleobject, FALSE);

  g_object_set (object->priv->nleobject, "start", start, NULL);

  return TRUE;
}

static void
ges_track_element_update_outpoint_full (GESTrackElement * self,
    GstClockTime inpoint, GstClockTime duration)
{
  GstClockTime current_inpoint = _INPOINT (self);
  gboolean increase = (inpoint > current_inpoint);
  GstClockTime outpoint = GST_CLOCK_TIME_NONE;
  GESTimelineElement *parent = GES_TIMELINE_ELEMENT_PARENT (self);
  GESTrackElementPrivate *priv = self->priv;

  if (GES_IS_CLIP (parent) && ges_track_element_get_track (self)
      && ges_track_element_is_active (self)
      && GST_CLOCK_TIME_IS_VALID (duration)) {
    outpoint =
        ges_clip_get_internal_time_from_timeline_time (GES_CLIP (parent), self,
        _START (self) + duration, NULL);

    if (!GST_CLOCK_TIME_IS_VALID (outpoint))
      GST_ERROR_OBJECT (self, "Got an invalid out-point");
    else if (increase)
      outpoint += (inpoint - current_inpoint);
    else
      outpoint -= (current_inpoint - inpoint);
  }

  if ((priv->outpoint != outpoint || inpoint != current_inpoint)
      && self->priv->auto_clamp_control_sources)
    _update_control_bindings (self, inpoint, outpoint);

  priv->outpoint = outpoint;
}

void
ges_track_element_update_outpoint (GESTrackElement * self)
{
  GESTimelineElement *el = GES_TIMELINE_ELEMENT (self);
  ges_track_element_update_outpoint_full (self, el->inpoint, el->duration);
}

static gboolean
_set_inpoint (GESTimelineElement * element, GstClockTime inpoint)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);
  GESTimelineElement *parent = element->parent;
  GError *error = NULL;

  g_return_val_if_fail (object->priv->nleobject, FALSE);
  if (inpoint && !object->priv->has_internal_source) {
    GST_WARNING_OBJECT (element, "Cannot set an in-point for a track "
        "element that is not registered with internal content");
    return FALSE;
  }

  if (GES_IS_CLIP (parent)
      && !ges_clip_can_set_inpoint_of_child (GES_CLIP (parent), object,
          inpoint, &error)) {
    GST_WARNING_OBJECT (element, "Cannot set an in-point of %"
        GST_TIME_FORMAT " because the parent clip %" GES_FORMAT
        " would not allow it%s%s", GST_TIME_ARGS (inpoint),
        GES_ARGS (parent), error ? ": " : "", error ? error->message : "");
    g_clear_error (&error);
    return FALSE;
  }

  g_object_set (object->priv->nleobject, "inpoint", inpoint, NULL);

  ges_track_element_update_outpoint_full (object, inpoint, element->duration);

  return TRUE;
}

static gboolean
_set_duration (GESTimelineElement * element, GstClockTime duration)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);
  GESTrackElementPrivate *priv = object->priv;

  g_return_val_if_fail (priv->nleobject, FALSE);

  g_object_set (priv->nleobject, "duration", duration, NULL);

  ges_track_element_update_outpoint_full (object, element->inpoint, duration);

  return TRUE;
}

static gboolean
_set_max_duration (GESTimelineElement * element, GstClockTime max_duration)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);
  GESTimelineElement *parent = element->parent;
  GError *error = NULL;

  if (GST_CLOCK_TIME_IS_VALID (max_duration)
      && !object->priv->has_internal_source) {
    GST_WARNING_OBJECT (element, "Cannot set a max-duration for a track "
        "element that is not registered with internal content");
    return FALSE;
  }

  if (GES_IS_CLIP (parent)
      && !ges_clip_can_set_max_duration_of_child (GES_CLIP (parent), object,
          max_duration, &error)) {
    GST_WARNING_OBJECT (element, "Cannot set a max-duration of %"
        GST_TIME_FORMAT " because the parent clip %" GES_FORMAT
        " would not allow it%s%s", GST_TIME_ARGS (max_duration),
        GES_ARGS (parent), error ? ": " : "", error ? error->message : "");
    g_clear_error (&error);
    return FALSE;
  }

  return TRUE;
}


static gboolean
_set_priority (GESTimelineElement * element, guint32 priority)
{
  GESTrackElement *object = GES_TRACK_ELEMENT (element);
  GESTimelineElement *parent = element->parent;
  GError *error = NULL;

  g_return_val_if_fail (object->priv->nleobject, FALSE);

  if (priority < MIN_NLE_PRIO) {
    GST_INFO_OBJECT (element, "Priority (%d) < MIN_NLE_PRIO, setting it to %d",
        priority, MIN_NLE_PRIO);
    priority = MIN_NLE_PRIO;
  }

  GST_DEBUG_OBJECT (object, "priority:%" G_GUINT32_FORMAT, priority);

  if (G_UNLIKELY (priority == _PRIORITY (object)))
    return FALSE;

  if (GES_IS_CLIP (parent)
      && !ges_clip_can_set_priority_of_child (GES_CLIP (parent), object,
          priority, &error)) {
    GST_WARNING_OBJECT (element, "Cannot set a priority of %"
        G_GUINT32_FORMAT " because the parent clip %" GES_FORMAT
        " would not allow it%s%s", priority, GES_ARGS (parent),
        error ? ": " : "", error ? error->message : "");
    g_clear_error (&error);
    return FALSE;
  }

  g_object_set (object->priv->nleobject, "priority", priority, NULL);

  return TRUE;
}

GESTrackType
_get_track_types (GESTimelineElement * object)
{
  return ges_track_element_get_track_type (GES_TRACK_ELEMENT (object));
}

/**
 * ges_track_element_set_active:
 * @object: A #GESTrackElement
 * @active: Whether @object should be active in its track
 *
 * Sets #GESTrackElement:active for the element.
 *
 * Returns: %TRUE if the property was *toggled*.
 */
gboolean
ges_track_element_set_active (GESTrackElement * object, gboolean active)
{
  GESTimelineElement *parent;
  GError *error = NULL;
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);
  g_return_val_if_fail (object->priv->nleobject, FALSE);

  GST_DEBUG_OBJECT (object, "object:%p, active:%d", object, active);

  if (G_UNLIKELY (active == object->active))
    return FALSE;

  parent = GES_TIMELINE_ELEMENT_PARENT (object);
  if (GES_IS_CLIP (parent)
      && !ges_clip_can_set_active_of_child (GES_CLIP (parent), object, active,
          &error)) {
    GST_WARNING_OBJECT (object,
        "Cannot set active to %i because the parent clip %" GES_FORMAT
        " would not allow it%s%s", active, GES_ARGS (parent), error ? ": " : "",
        error ? error->message : "");
    g_clear_error (&error);
    return FALSE;
  }

  g_object_set (object->priv->nleobject, "active",
      active & object->priv->layer_active, NULL);

  object->active = active;
  if (GES_TRACK_ELEMENT_GET_CLASS (object)->active_changed)
    GES_TRACK_ELEMENT_GET_CLASS (object)->active_changed (object, active);

  g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_ACTIVE]);

  return TRUE;
}

/**
 * ges_track_element_set_has_internal_source:
 * @object: A #GESTrackElement
 * @has_internal_source: Whether the @object should be allowed to have its
 * 'internal time' properties set.
 *
 * Sets #GESTrackElement:has-internal-source for the element. If this is
 * set to %FALSE, this method will also set the
 * #GESTimelineElement:in-point of the element to 0 and its
 * #GESTimelineElement:max-duration to #GST_CLOCK_TIME_NONE.
 *
 * Returns: %FALSE if @has_internal_source is forbidden for @object and
 * %TRUE in any other case.
 *
 * Since: 1.18
 */
gboolean
ges_track_element_set_has_internal_source (GESTrackElement * object,
    gboolean has_internal_source)
{
  GESTimelineElement *element;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  GST_DEBUG_OBJECT (object, "object:%p, has-internal-source: %s", object,
      has_internal_source ? "TRUE" : "FALSE");

  if (has_internal_source && object->priv->has_internal_source_forbidden) {
    GST_WARNING_OBJECT (object, "Setting an internal source for this "
        "element is forbidden");
    return FALSE;
  }

  if (G_UNLIKELY (has_internal_source == object->priv->has_internal_source))
    return TRUE;

  object->priv->has_internal_source = has_internal_source;

  if (!has_internal_source) {
    element = GES_TIMELINE_ELEMENT (object);
    ges_timeline_element_set_inpoint (element, 0);
    ges_timeline_element_set_max_duration (element, GST_CLOCK_TIME_NONE);
  }

  g_object_notify_by_pspec (G_OBJECT (object),
      properties[PROP_HAS_INTERNAL_SOURCE]);

  return TRUE;
}

void
ges_track_element_set_has_internal_source_is_forbidden (GESTrackElement *
    element)
{
  element->priv->has_internal_source_forbidden = TRUE;
}

/**
 * ges_track_element_set_track_type:
 * @object: A #GESTrackElement
 * @type: The new track-type for @object
 *
 * Sets the #GESTrackElement:track-type for the element.
 */
void
ges_track_element_set_track_type (GESTrackElement * object, GESTrackType type)
{
  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  if (object->priv->track_type != type) {
    object->priv->track_type = type;
    g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_TRACK_TYPE]);
  }
}

/**
 * ges_track_element_get_track_type:
 * @object: A #GESTrackElement
 *
 * Gets the #GESTrackElement:track-type for the element.
 *
 * Returns: The track-type of @object.
 */
GESTrackType
ges_track_element_get_track_type (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), GES_TRACK_TYPE_UNKNOWN);

  return object->priv->track_type;
}

/* default 'create_gnl_object' virtual method implementation */
static GstElement *
ges_track_element_create_gnl_object_func (GESTrackElement * self)
{
  GESTrackElementClass *klass = NULL;
  GstElement *child = NULL;
  GstElement *nleobject;

  klass = GES_TRACK_ELEMENT_GET_CLASS (self);

  if (G_UNLIKELY (self->priv->nleobject != NULL))
    goto already_have_nleobject;

  if (G_UNLIKELY (klass->nleobject_factorytype == NULL))
    goto no_nlefactory;

  GST_DEBUG ("Creating a supporting nleobject of type '%s'",
      klass->nleobject_factorytype);

  nleobject = gst_element_factory_make (klass->nleobject_factorytype, NULL);

  if (G_UNLIKELY (nleobject == NULL))
    goto no_nleobject;

  self->priv->nleobject = gst_object_ref (nleobject);
  g_object_set_qdata (G_OBJECT (nleobject), NLE_OBJECT_TRACK_ELEMENT_QUARK,
      self);

  if (klass->create_element) {
    GST_DEBUG ("Calling subclass 'create_element' vmethod");
    child = klass->create_element (self);

    if (G_UNLIKELY (!child))
      goto child_failure;

    if (!gst_bin_add (GST_BIN (nleobject), child))
      goto add_failure;

    GST_DEBUG ("Successfully got the element to put in the nleobject");
    self->priv->element = child;
  }

  GST_DEBUG ("done");
  return nleobject;


  /* ERROR CASES */

already_have_nleobject:
  {
    GST_ERROR ("Already controlling a NleObject %s",
        GST_ELEMENT_NAME (self->priv->nleobject));
    return NULL;
  }

no_nlefactory:
  {
    GST_ERROR ("No GESTrackElement::nleobject_factorytype implementation!");
    return NULL;
  }

no_nleobject:
  {
    GST_ERROR ("Error creating a nleobject of type '%s'",
        klass->nleobject_factorytype);
    return NULL;
  }

child_failure:
  {
    GST_ERROR ("create_element returned NULL");
    gst_object_unref (nleobject);
    return NULL;
  }

add_failure:
  {
    GST_ERROR ("Error adding the contents to the nleobject");
    gst_object_unref (child);
    gst_object_unref (nleobject);
    return NULL;
  }
}

static void
ges_track_element_add_child_props (GESTrackElement * self,
    GstElement * child, const gchar ** wanted_categories,
    const gchar ** blacklist, const gchar ** whitelist)
{
  GstElementFactory *factory;
  const gchar *klass;
  GParamSpec **parray;
  GObjectClass *gobject_klass;
  gchar **categories;
  guint i;

  factory = gst_element_get_factory (child);
  /* FIXME: handle NULL factory */
  klass = gst_element_factory_get_metadata (factory,
      GST_ELEMENT_METADATA_KLASS);

  if (strv_find_str (blacklist, GST_OBJECT_NAME (factory))) {
    GST_DEBUG_OBJECT (self, "%s blacklisted", GST_OBJECT_NAME (factory));
    return;
  }

  GST_DEBUG_OBJECT (self, "Looking at element '%s' of klass '%s'",
      GST_ELEMENT_NAME (child), klass);

  categories = g_strsplit (klass, "/", 0);

  for (i = 0; categories[i]; i++) {
    if ((!wanted_categories ||
            strv_find_str (wanted_categories, categories[i]))) {
      guint i, nb_specs;

      gobject_klass = G_OBJECT_GET_CLASS (child);
      parray = g_object_class_list_properties (gobject_klass, &nb_specs);
      for (i = 0; i < nb_specs; i++) {
        if ((!whitelist && (parray[i]->flags & G_PARAM_WRITABLE))
            || (strv_find_str (whitelist, parray[i]->name))) {
          ges_timeline_element_add_child_property (GES_TIMELINE_ELEMENT
              (self), parray[i], G_OBJECT (child));
        }
      }
      g_free (parray);

      GST_DEBUG
          ("%d configurable properties of '%s' added to property hashtable",
          nb_specs, GST_ELEMENT_NAME (child));
      break;
    }
  }

  g_strfreev (categories);
}

/**
 * ges_track_element_add_children_props:
 * @self: A #GESTrackElement
 * @element: The child object to retrieve properties from
 * @wanted_categories: (array zero-terminated=1) (transfer none) (allow-none):
 * An array of element factory "klass" categories to whitelist, or %NULL
 * to accept all categories
 * @blacklist: (array zero-terminated=1) (transfer none) (allow-none): A
 * blacklist of element factory names, or %NULL to not blacklist any
 * element factory
 * @whitelist: (array zero-terminated=1) (transfer none) (allow-none): A
 * whitelist of element property names, or %NULL to whitelist all
 * writeable properties
 *
 * Adds all the properties of a #GstElement that match the criteria as
 * children properties of the track element. If the name of @element's
 * #GstElementFactory is not in @blacklist, and the factory's
 * #GST_ELEMENT_METADATA_KLASS contains at least one member of
 * @wanted_categories (e.g. #GST_ELEMENT_FACTORY_KLASS_DECODER), then
 * all the properties of @element that are also in @whitelist are added as
 * child properties of @self using
 * ges_timeline_element_add_child_property().
 *
 * This is intended to be used by subclasses when constructing.
 */
void
ges_track_element_add_children_props (GESTrackElement * self,
    GstElement * element, const gchar ** wanted_categories,
    const gchar ** blacklist, const gchar ** whitelist)
{
  GValue item = { 0, };
  GstIterator *it;
  gboolean done = FALSE;

  if (!GST_IS_BIN (element)) {
    ges_track_element_add_child_props (self, element, wanted_categories,
        blacklist, whitelist);
    return;
  }

  /*  We go over child elements recursively, and add writable properties to the
   *  hashtable */
  it = gst_bin_iterate_recurse (GST_BIN (element));
  while (!done) {
    switch (gst_iterator_next (it, &item)) {
      case GST_ITERATOR_OK:
      {
        GstElement *child = g_value_get_object (&item);
        ges_track_element_add_child_props (self, child, wanted_categories,
            blacklist, whitelist);
        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        /* FIXME, properly restart the process */
        GST_DEBUG ("iterator resync");
        gst_iterator_resync (it);
        break;

      case GST_ITERATOR_DONE:
        GST_DEBUG ("iterator done");
        done = TRUE;
        break;

      default:
        break;
    }
    g_value_unset (&item);
  }
  gst_iterator_free (it);
}

/* INTERNAL USAGE */
gboolean
ges_track_element_set_track (GESTrackElement * object, GESTrack * track,
    GError ** error)
{
  GESTimelineElement *parent = GES_TIMELINE_ELEMENT_PARENT (object);

  g_return_val_if_fail (object->priv->nleobject, FALSE);

  GST_DEBUG_OBJECT (object, "new track: %" GST_PTR_FORMAT, track);

  if (GES_IS_CLIP (parent)
      && !ges_clip_can_set_track_of_child (GES_CLIP (parent), object, track,
          error)) {
    GST_INFO_OBJECT (object,
        "The parent clip %" GES_FORMAT " would not allow the track to be "
        "set to %" GST_PTR_FORMAT, GES_ARGS (parent), track);
    return FALSE;
  }

  object->priv->track = track;

  if (object->priv->track) {
    ges_track_element_set_track_type (object, track->type);

    g_object_set (object->priv->nleobject,
        "caps", ges_track_get_caps (object->priv->track), NULL);
  }

  g_object_notify_by_pspec (G_OBJECT (object), properties[PROP_TRACK]);
  return TRUE;
}

void
ges_track_element_set_layer_active (GESTrackElement * element, gboolean active)
{
  if (element->priv->layer_active == active)
    return;

  element->priv->layer_active = active;
  g_object_set (element->priv->nleobject, "active", active & element->active,
      NULL);
}

/**
 * ges_track_element_get_all_control_bindings
 * @trackelement: A #GESTrackElement
 *
 * Get all the control bindings that have been created for the children
 * properties of the track element using
 * ges_track_element_set_control_source(). The keys used in the returned
 * hash table are the child property names that were passed to
 * ges_track_element_set_control_source(), and their values are the
 * corresponding created #GstControlBinding.
 *
 * Returns: (element-type gchar* GstControlBinding*)(transfer none): A
 * hash table containing all child-property-name/control-binding pairs
 * for @trackelement.
 */
GHashTable *
ges_track_element_get_all_control_bindings (GESTrackElement * trackelement)
{
  GESTrackElementPrivate *priv = GES_TRACK_ELEMENT (trackelement)->priv;

  return priv->bindings_hashtable;
}

/**
 * ges_track_element_get_track:
 * @object: A #GESTrackElement
 *
 * Get the #GESTrackElement:track for the element.
 *
 * Returns: (transfer none) (nullable): The track that @object belongs to,
 * or %NULL if it does not belong to a track.
 */
GESTrack *
ges_track_element_get_track (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->track;
}

/**
 * ges_track_element_get_gnlobject:
 * @object: A #GESTrackElement
 *
 * Get the GNonLin object this object is controlling.
 *
 * Returns: (transfer none): The GNonLin object this object is controlling.
 *
 * Deprecated: use #ges_track_element_get_nleobject instead.
 */
GstElement *
ges_track_element_get_gnlobject (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->nleobject;
}

/**
 * ges_track_element_get_nleobject:
 * @object: A #GESTrackElement
 *
 * Get the nleobject that this element wraps.
 *
 * Returns: (transfer none): The nleobject that @object wraps.
 *
 * Since: 1.6
 */
GstElement *
ges_track_element_get_nleobject (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->nleobject;
}

/**
 * ges_track_element_get_element:
 * @object: A #GESTrackElement
 *
 * Get the #GstElement that the track element's underlying nleobject
 * controls.
 *
 * Returns: (transfer none) (nullable): The #GstElement being controlled by the
 * nleobject that @object wraps.
 */
GstElement *
ges_track_element_get_element (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  return object->priv->element;
}

/**
 * ges_track_element_is_active:
 * @object: A #GESTrackElement
 *
 * Gets #GESTrackElement:active for the element.
 *
 * Returns: %TRUE if @object is active in its track.
 */
gboolean
ges_track_element_is_active (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);
  g_return_val_if_fail (object->priv->nleobject, FALSE);

  return object->active;
}

/**
 * ges_track_element_has_internal_source:
 * @object: A #GESTrackElement
 *
 * Gets #GESTrackElement:has-internal-source for the element.
 *
 * Returns: %TRUE if @object can have its 'internal time' properties set.
 *
 * Since: 1.18
 */
gboolean
ges_track_element_has_internal_source (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  return object->priv->has_internal_source;
}

/**
 * ges_track_element_lookup_child:
 * @object: Object to lookup the property in
 * @prop_name: Name of the property to look up. You can specify the name of the
 *     class as such: "ClassName::property-name", to guarantee that you get the
 *     proper GParamSpec in case various GstElement-s contain the same property
 *     name. If you don't do so, you will get the first element found, having
 *     this property and the and the corresponding GParamSpec.
 * @element: (out) (allow-none) (transfer full): pointer to a #GstElement that
 *     takes the real object to set property on
 * @pspec: (out) (allow-none) (transfer full): pointer to take the specification
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
 * Deprecated: Use #ges_timeline_element_lookup_child
 */
gboolean
ges_track_element_lookup_child (GESTrackElement * object,
    const gchar * prop_name, GstElement ** element, GParamSpec ** pspec)
{
  return ges_timeline_element_lookup_child (GES_TIMELINE_ELEMENT (object),
      prop_name, ((GObject **) element), pspec);
}

/**
 * ges_track_element_set_child_property_by_pspec: (skip):
 * @object: A #GESTrackElement
 * @pspec: The #GParamSpec that specifies the property you want to set
 * @value: The value
 *
 * Sets a property of a child of @object.
 *
 * Deprecated: Use #ges_timeline_element_set_child_property_by_spec
 */
void
ges_track_element_set_child_property_by_pspec (GESTrackElement * object,
    GParamSpec * pspec, GValue * value)
{
  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  ges_timeline_element_set_child_property_by_pspec (GES_TIMELINE_ELEMENT
      (object), pspec, value);

  return;
}

/**
 * ges_track_element_set_child_property_valist: (skip):
 * @object: The #GESTrackElement parent object
 * @first_property_name: The name of the first property to set
 * @var_args: Value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Sets a property of a child of @object. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 *
 * Deprecated: Use #ges_timeline_element_set_child_property_valist
 */
void
ges_track_element_set_child_property_valist (GESTrackElement * object,
    const gchar * first_property_name, va_list var_args)
{
  ges_timeline_element_set_child_property_valist (GES_TIMELINE_ELEMENT (object),
      first_property_name, var_args);
}

/**
 * ges_track_element_set_child_properties: (skip):
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
 * Deprecated: Use #ges_timeline_element_set_child_properties
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
 * ges_track_element_get_child_property_valist: (skip):
 * @object: The #GESTrackElement parent object
 * @first_property_name: The name of the first property to get
 * @var_args: Value for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Gets a property of a child of @object. If there are various child elements
 * that have the same property name, you can distinguish them using the following
 * syntax: 'ClasseName::property_name' as property name. If you don't, the
 * corresponding property of the first element found will be set.
 *
 * Deprecated: Use #ges_timeline_element_get_child_property_valist
 */
void
ges_track_element_get_child_property_valist (GESTrackElement * object,
    const gchar * first_property_name, va_list var_args)
{
  ges_timeline_element_get_child_property_valist (GES_TIMELINE_ELEMENT (object),
      first_property_name, var_args);
}

/**
 * ges_track_element_list_children_properties:
 * @object: The #GESTrackElement to get the list of children properties from
 * @n_properties: (out): return location for the length of the returned array
 *
 * Gets an array of #GParamSpec* for all configurable properties of the
 * children of @object.
 *
 * Returns: (transfer full) (array length=n_properties): An array of #GParamSpec* which should be freed after use or
 * %NULL if something went wrong.
 *
 * Deprecated: Use #ges_timeline_element_list_children_properties
 */
GParamSpec **
ges_track_element_list_children_properties (GESTrackElement * object,
    guint * n_properties)
{
  return
      ges_timeline_element_list_children_properties (GES_TIMELINE_ELEMENT
      (object), n_properties);
}

/**
 * ges_track_element_get_child_properties: (skip):
 * @object: The origin #GESTrackElement
 * @first_property_name: The name of the first property to get
 * @...: return location for the first property, followed optionally by more
 * name/return location pairs, followed by NULL
 *
 * Gets properties of a child of @object.
 *
 * Deprecated: Use #ges_timeline_element_get_child_properties
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
 * ges_track_element_get_child_property_by_pspec: (skip):
 * @object: A #GESTrackElement
 * @pspec: The #GParamSpec that specifies the property you want to get
 * @value: (out): return location for the value
 *
 * Gets a property of a child of @object.
 *
 * Deprecated: Use #ges_timeline_element_get_child_property_by_pspec
 */
void
ges_track_element_get_child_property_by_pspec (GESTrackElement * object,
    GParamSpec * pspec, GValue * value)
{
  ges_timeline_element_get_child_property_by_pspec (GES_TIMELINE_ELEMENT
      (object), pspec, value);
}

/**
 * ges_track_element_set_child_property: (skip):
 * @object: The origin #GESTrackElement
 * @property_name: The name of the property
 * @value: The value
 *
 * Sets a property of a GstElement contained in @object.
 *
 * Note that #ges_track_element_set_child_property is really
 * intended for language bindings, #ges_track_element_set_child_properties
 * is much more convenient for C programming.
 *
 * Returns: %TRUE if the property was set, %FALSE otherwise.
 *
 * Deprecated: use #ges_timeline_element_set_child_property instead
 */
gboolean
ges_track_element_set_child_property (GESTrackElement * object,
    const gchar * property_name, GValue * value)
{
  return ges_timeline_element_set_child_property (GES_TIMELINE_ELEMENT (object),
      property_name, value);
}

/**
 * ges_track_element_get_child_property: (skip):
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
 * Returns: %TRUE if the property was found, %FALSE otherwise.
 *
 * Deprecated: Use #ges_timeline_element_get_child_property
 */
gboolean
ges_track_element_get_child_property (GESTrackElement * object,
    const gchar * property_name, GValue * value)
{
  return ges_timeline_element_get_child_property (GES_TIMELINE_ELEMENT (object),
      property_name, value);
}

void
ges_track_element_copy_properties (GESTimelineElement * element,
    GESTimelineElement * elementcopy)
{
  GParamSpec **specs;
  guint n, n_specs;
  GValue val = { 0 };
  GESTrackElement *copy = GES_TRACK_ELEMENT (elementcopy);

  specs =
      ges_track_element_list_children_properties (GES_TRACK_ELEMENT (element),
      &n_specs);
  for (n = 0; n < n_specs; ++n) {
    if ((specs[n]->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE)
      continue;
    if (specs[n]->flags & G_PARAM_CONSTRUCT_ONLY)
      continue;
    g_value_init (&val, specs[n]->value_type);
    ges_track_element_get_child_property_by_pspec (GES_TRACK_ELEMENT (element),
        specs[n], &val);
    ges_track_element_set_child_property_by_pspec (copy, specs[n], &val);
    g_value_unset (&val);
  }

  g_free (specs);
}

void
ges_track_element_set_creator_asset (GESTrackElement * self,
    GESAsset * creator_asset)
{
  self->priv->creator_asset = creator_asset;
}

GESAsset *
ges_track_element_get_creator_asset (GESTrackElement * self)
{
  return self->priv->creator_asset;
}

static void
_split_binding (GESTrackElement * element, GESTrackElement * new_element,
    guint64 position, GstTimedValueControlSource * source,
    GstTimedValueControlSource * new_source, gboolean absolute)
{
  GstTimedValue *last_value = NULL;
  gboolean past_position = FALSE;
  GList *values, *tmp;

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (source));

  for (tmp = values; tmp; tmp = tmp->next) {
    GstTimedValue *value = tmp->data;

    if (value->timestamp > position && !past_position) {
      gfloat value_at_pos;

      /* FIXME We should be able to use gst_control_source_get_value so
       * all modes are handled. Right now that method only works if the value
       * we are looking for is between two actual keyframes which is not enough
       * in our case. bug #706621 */
      value_at_pos =
          interpolate_values_for_position (last_value, value, position,
          absolute);

      past_position = TRUE;

      gst_timed_value_control_source_set (new_source, position, value_at_pos);
      gst_timed_value_control_source_set (new_source, value->timestamp,
          value->value);

      gst_timed_value_control_source_unset (source, value->timestamp);
      gst_timed_value_control_source_set (source, position, value_at_pos);
    } else if (past_position) {
      gst_timed_value_control_source_set (new_source, value->timestamp,
          value->value);
      gst_timed_value_control_source_unset (source, value->timestamp);
    }
    last_value = value;

  }
  g_list_free (values);
}

static void
_copy_binding (GESTrackElement * element, GESTrackElement * new_element,
    guint64 position, GstTimedValueControlSource * source,
    GstTimedValueControlSource * new_source, gboolean absolute)
{
  GList *values, *tmp;

  values =
      gst_timed_value_control_source_get_all (GST_TIMED_VALUE_CONTROL_SOURCE
      (source));
  for (tmp = values; tmp; tmp = tmp->next) {
    GstTimedValue *value = tmp->data;

    gst_timed_value_control_source_set (new_source, value->timestamp,
        value->value);
  }
  g_list_free (values);
}

/* position == GST_CLOCK_TIME_NONE means that we do a simple copy
 * other position means that the function will do a splitting
 * and thus interpollate the values in the element and new_element
 */
void
ges_track_element_copy_bindings (GESTrackElement * element,
    GESTrackElement * new_element, guint64 position)
{
  GParamSpec **specs;
  guint n, n_specs;
  gboolean absolute;
  GstControlBinding *binding;
  GstTimedValueControlSource *source, *new_source;

  specs =
      ges_track_element_list_children_properties (GES_TRACK_ELEMENT (element),
      &n_specs);
  for (n = 0; n < n_specs; ++n) {
    GstInterpolationMode mode;

    binding = ges_track_element_get_control_binding (element, specs[n]->name);
    if (!binding)
      continue;

    g_object_get (binding, "control-source", &source, "absolute", &absolute,
        NULL);
    if (!GST_IS_TIMED_VALUE_CONTROL_SOURCE (source)) {
      GST_FIXME_OBJECT (element,
          "Implement support for control source type: %s",
          G_OBJECT_TYPE_NAME (source));
      gst_object_unref (source);
      continue;
    }

    g_object_get (source, "mode", &mode, NULL);

    new_source =
        GST_TIMED_VALUE_CONTROL_SOURCE (gst_interpolation_control_source_new
        ());
    g_object_set (new_source, "mode", mode, NULL);

    if (GST_CLOCK_TIME_IS_VALID (position))
      _split_binding (element, new_element, position, source, new_source,
          absolute);
    else
      _copy_binding (element, new_element, position, source, new_source,
          absolute);

    /* We only manage direct (absolute) bindings, see TODO in set_control_source */
    if (absolute)
      ges_track_element_set_control_source (new_element,
          GST_CONTROL_SOURCE (new_source), specs[n]->name, "direct-absolute");
    else
      ges_track_element_set_control_source (new_element,
          GST_CONTROL_SOURCE (new_source), specs[n]->name, "direct");

    gst_object_unref (source);
    gst_object_unref (new_source);
  }

  g_free (specs);
}

/**
 * ges_track_element_edit:
 * @object: The #GESTrackElement to edit
 * @layers: (element-type GESLayer) (nullable): A whitelist of layers
 * where the edit can be performed, %NULL allows all layers in the
 * timeline
 * @mode: The edit mode
 * @edge: The edge of @object where the edit should occur
 * @position: The edit position: a new location for the edge of @object
 * (in nanoseconds)
 *
 * Edits the element within its track.
 *
 * Returns: %TRUE if the edit of @object completed, %FALSE on failure.
 *
 * Deprecated: 1.18: use #ges_timeline_element_edit instead.
 */
gboolean
ges_track_element_edit (GESTrackElement * object,
    GList * layers, GESEditMode mode, GESEdge edge, guint64 position)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  return ges_timeline_element_edit (GES_TIMELINE_ELEMENT (object),
      layers, -1, mode, edge, position);
}

/**
 * ges_track_element_remove_control_binding:
 * @object: A #GESTrackElement
 * @property_name: The name of the child property to remove the control
 * binding from
 *
 * Removes the #GstControlBinding that was created for the specified child
 * property of the track element using
 * ges_track_element_set_control_source(). The given @property_name must
 * be the same name of the child property that was passed to
 * ges_track_element_set_control_source().
 *
 * Returns: %TRUE if the control binding was removed from the specified
 * child property of @object, or %FALSE if an error occurred.
 */
gboolean
ges_track_element_remove_control_binding (GESTrackElement * object,
    const gchar * property_name)
{
  GESTrackElementPrivate *priv;
  GstControlBinding *binding;
  GstObject *target;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  priv = GES_TRACK_ELEMENT (object)->priv;
  binding =
      (GstControlBinding *) g_hash_table_lookup (priv->bindings_hashtable,
      property_name);

  if (binding) {
    g_object_get (binding, "object", &target, NULL);
    GST_DEBUG_OBJECT (object, "Removing binding %p for property %s", binding,
        property_name);

    gst_object_ref (binding);
    gst_object_remove_control_binding (target, binding);

    g_signal_emit (object, ges_track_element_signals[CONTROL_BINDING_REMOVED],
        0, binding);

    gst_object_unref (target);
    gst_object_unref (binding);
    g_hash_table_remove (priv->bindings_hashtable, property_name);

    return TRUE;
  }

  return FALSE;
}

/**
 * ges_track_element_set_control_source:
 * @object: A #GESTrackElement
 * @source: The control source to bind the child property to
 * @property_name: The name of the child property to control
 * @binding_type: The type of binding to create ("direct" or
 * "direct-absolute")
 *
 * Creates a #GstControlBinding for the specified child property of the
 * track element using the given control source. The given @property_name
 * should refer to an existing child property of the track element, as
 * used in ges_timeline_element_lookup_child().
 *
 * If @binding_type is "direct", then the control binding is created with
 * gst_direct_control_binding_new() using the given control source. If
 * @binding_type is "direct-absolute", it is created with
 * gst_direct_control_binding_new_absolute() instead.
 *
 * Returns: %TRUE if the specified child property could be bound to
 * @source, or %FALSE if an error occurred.
 */
gboolean
ges_track_element_set_control_source (GESTrackElement * object,
    GstControlSource * source,
    const gchar * property_name, const gchar * binding_type)
{
  gboolean ret = FALSE;
  GESTrackElementPrivate *priv;
  GstElement *element;
  GstControlBinding *binding;
  gboolean direct, direct_absolute;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);
  priv = GES_TRACK_ELEMENT (object)->priv;

  if (G_UNLIKELY (!(GST_IS_CONTROL_SOURCE (source)))) {
    GST_WARNING
        ("You need to provide a non-null control source to build a new control binding");
    return FALSE;
  }

  if (!ges_track_element_lookup_child (object, property_name, &element, NULL)) {
    GST_WARNING ("You need to provide a valid and controllable property name");
    return FALSE;
  }

  /* TODO : update this according to new types of bindings */
  direct = !g_strcmp0 (binding_type, "direct");
  direct_absolute = !g_strcmp0 (binding_type, "direct-absolute");

  if (!direct && !direct_absolute) {
    GST_WARNING_OBJECT (object, "Binding type must be in "
        "[direct, direct-absolute]");
    goto done;
  }

  /* First remove existing binding */
  if (ges_track_element_remove_control_binding (object, property_name))
    GST_LOG_OBJECT (object, "Removed old binding for property %s",
        property_name);

  if (direct_absolute)
    binding = gst_direct_control_binding_new_absolute (GST_OBJECT (element),
        property_name, source);
  else
    binding = gst_direct_control_binding_new (GST_OBJECT (element),
        property_name, source);

  gst_object_add_control_binding (GST_OBJECT (element), binding);
  /* FIXME: maybe we should force the
   * "ChildTypeName:property-name"
   * format convention for child property names in bindings_hashtable.
   * Currently the table may also contain
   * "property-name"
   * as keys.
   */
  g_hash_table_insert (priv->bindings_hashtable, g_strdup (property_name),
      binding);

  if (GST_IS_TIMED_VALUE_CONTROL_SOURCE (source)
      && priv->auto_clamp_control_sources) {
    /* Make sure we have the control source used by the binding */
    g_object_get (binding, "control-source", &source, NULL);

    _update_control_source (GST_TIMED_VALUE_CONTROL_SOURCE (source),
        direct_absolute, _INPOINT (object), priv->outpoint);

    gst_object_unref (source);
  }

  g_signal_emit (object, ges_track_element_signals[CONTROL_BINDING_ADDED],
      0, binding);

  ret = TRUE;

done:
  gst_object_unref (element);

  return ret;
}

/**
 * ges_track_element_get_control_binding:
 * @object: A #GESTrackElement
 * @property_name: The name of the child property to return the control
 * binding of
 *
 * Gets the control binding that was created for the specified child
 * property of the track element using
 * ges_track_element_set_control_source(). The given @property_name must
 * be the same name of the child property that was passed to
 * ges_track_element_set_control_source().
 *
 * Returns: (transfer none) (nullable): The control binding that was
 * created for the specified child property of @object, or %NULL if
 * @property_name does not correspond to any control binding.
 */
GstControlBinding *
ges_track_element_get_control_binding (GESTrackElement * object,
    const gchar * property_name)
{
  GESTrackElementPrivate *priv;
  GstControlBinding *binding;

  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), NULL);

  priv = GES_TRACK_ELEMENT (object)->priv;

  binding =
      (GstControlBinding *) g_hash_table_lookup (priv->bindings_hashtable,
      property_name);
  return binding;
}

/**
 * ges_track_element_clamp_control_source:
 * @object: A #GESTrackElement
 * @property_name: The name of the child property to clamp the control
 * source of
 *
 * Clamp the #GstTimedValueControlSource for the specified child property
 * to lie between the #GESTimelineElement:in-point and out-point of the
 * element. The out-point is the #GES_TIMELINE_ELEMENT_END of the element
 * translated from the timeline coordinates to the internal source
 * coordinates of the element.
 *
 * If the property does not have a #GstTimedValueControlSource set by
 * ges_track_element_set_control_source(), nothing happens. Otherwise, if
 * a timed value for the control source lies before the in-point of the
 * element, or after its out-point, then it will be removed. At the
 * in-point and out-point times, a new interpolated value will be placed.
 *
 * Since: 1.18
 */
void
ges_track_element_clamp_control_source (GESTrackElement * object,
    const gchar * property_name)
{
  GstControlBinding *binding;
  GstControlSource *source;
  gboolean absolute;

  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  binding = ges_track_element_get_control_binding (object, property_name);

  if (!binding)
    return;

  g_object_get (binding, "control-source", &source, "absolute", &absolute,
      NULL);

  if (!GST_IS_TIMED_VALUE_CONTROL_SOURCE (source)) {
    gst_object_unref (source);
    return;
  }

  _update_control_source (GST_TIMED_VALUE_CONTROL_SOURCE (source), absolute,
      _INPOINT (object), object->priv->outpoint);
  gst_object_unref (source);
}

/**
 * ges_track_element_set_auto_clamp_control_sources:
 * @object: A #GESTrackElement
 * @auto_clamp: Whether to automatically clamp the control sources for the
 * child properties of @object
 *
 * Sets #GESTrackElement:auto-clamp-control-sources. If set to %TRUE, this
 * will immediately clamp all the control sources.
 *
 * Since: 1.18
 */
void
ges_track_element_set_auto_clamp_control_sources (GESTrackElement * object,
    gboolean auto_clamp)
{
  g_return_if_fail (GES_IS_TRACK_ELEMENT (object));

  if (auto_clamp == object->priv->auto_clamp_control_sources)
    return;

  object->priv->auto_clamp_control_sources = auto_clamp;
  if (auto_clamp)
    _update_control_bindings (object, _INPOINT (object),
        object->priv->outpoint);

  g_object_notify_by_pspec (G_OBJECT (object),
      properties[PROP_AUTO_CLAMP_CONTROL_SOURCES]);
}

/**
 * ges_track_element_get_auto_clamp_control_sources:
 * @object: A #GESTrackElement
 *
 * Gets #GESTrackElement:auto-clamp-control-sources.
 *
 * Returns: Whether the control sources for the child properties of
 * @object are automatically clamped.
 * Since: 1.18
 */
gboolean
ges_track_element_get_auto_clamp_control_sources (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  return object->priv->auto_clamp_control_sources;
}

void
ges_track_element_freeze_control_sources (GESTrackElement * object,
    gboolean freeze)
{
  object->priv->freeze_control_sources = freeze;
  if (!freeze && object->priv->auto_clamp_control_sources)
    _update_control_bindings (object, _INPOINT (object),
        object->priv->outpoint);
}

/**
 * ges_track_element_is_core:
 * @object: A #GESTrackElement
 *
 * Get whether the given track element is a core track element. That is,
 * it was created by the @create_track_elements #GESClipClass method for
 * some #GESClip.
 *
 * Note that such a track element can only be added to a clip that shares
 * the same #GESAsset as the clip that created it. For example, you are
 * allowed to move core children between clips that resulted from
 * ges_container_ungroup(), but you could not move the core child from a
 * #GESUriClip to a #GESTitleClip or another #GESUriClip with a different
 * #GESUriClip:uri.
 *
 * Moreover, if a core track element is added to a clip, it will always be
 * added as a core child. Therefore, if this returns %TRUE, then @element
 * will be a core child of its parent clip.
 *
 * Returns: %TRUE if @element is a core track element.
 * Since: 1.18
 */
gboolean
ges_track_element_is_core (GESTrackElement * object)
{
  g_return_val_if_fail (GES_IS_TRACK_ELEMENT (object), FALSE);

  return (ges_track_element_get_creator_asset (object) != NULL);
}
