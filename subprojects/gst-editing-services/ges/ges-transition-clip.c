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
 * SECTION: gestransitionclip
 * @title: GESTransitionClip
 * @short_description: Transition from one clip to another in a GESLayer
 *
 * Creates an object that mixes together the two underlying objects, A and B.
 * The A object is assumed to have a higher prioirity (lower number) than the
 * B object. At the transition in point, only A will be visible, and by the
 * end only B will be visible.
 *
 * The shape of the video transition depends on the value of the "vtype"
 * property. The default value is "crossfade". For audio, only "crossfade" is
 * supported.
 *
 * The ID of the ExtractableType is the nickname of the vtype property value. Note
 * that this value can be changed after creation and the GESExtractable.asset value
 * will be updated when needed.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <ges/ges.h>
#include "ges-internal.h"

struct _GESTransitionClipPrivate
{
  GSList *video_transitions;

  const gchar *vtype_name;
};

enum
{
  PROP_VTYPE = 5,
};

static GESTrackElement *_create_track_element (GESClip
    * self, GESTrackType type);
static void _child_added (GESContainer * container,
    GESTimelineElement * element);
static void _child_removed (GESContainer * container,
    GESTimelineElement * element);

/* Internal methods */
static void
ges_transition_clip_update_vtype_internal (GESClip *
    self, GESVideoStandardTransitionType value, gboolean set_asset)
{
  GSList *tmp;
  guint index;
  GEnumClass *enum_class;
  const gchar *asset_id = NULL;
  GESTransitionClip *trself = GES_TRANSITION_CLIP (self);

  enum_class = g_type_class_peek (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);
  for (index = 0; index < enum_class->n_values; index++) {
    if (enum_class->values[index].value == value) {
      asset_id = enum_class->values[index].value_nick;
      break;
    }
  }

  if (asset_id == NULL) {
    GST_WARNING_OBJECT (self, "Wrong transition type value: %i can not set it",
        value);

    return;
  }

  for (tmp = trself->priv->video_transitions; tmp; tmp = tmp->next) {
    if (!ges_video_transition_set_transition_type
        (GES_VIDEO_TRANSITION (tmp->data), value))
      return;
  }

  trself->vtype = value;
  trself->priv->vtype_name = asset_id;

  if (set_asset) {
    GESAsset *asset =
        ges_asset_request (GES_TYPE_TRANSITION_CLIP, asset_id, NULL);

    /* We already checked the value, so we can be sure no error occurred */
    ges_extractable_set_asset (GES_EXTRACTABLE (self), asset);
    gst_object_unref (asset);
  }
}

/* GESExtractable interface overrides */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS;       /* Start ignoring GParameter deprecation */
static GParameter *
extractable_get_parameters_from_id (const gchar * id, guint * n_params)
{
  GEnumClass *enum_class =
      g_type_class_peek (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);
  GParameter *params = g_new0 (GParameter, 1);

  GEnumValue *value = g_enum_get_value_by_nick (enum_class, id);

  params[0].name = "vtype";
  g_value_init (&params[0].value, GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);
  g_value_set_enum (&params[0].value, value->value);
  *n_params = 1;

  return params;
}

G_GNUC_END_IGNORE_DEPRECATIONS; /* End ignoring GParameter deprecation */
static gchar *
extractable_check_id (GType type, const gchar * id)
{
  guint index;
  GEnumClass *enum_class;
  enum_class = g_type_class_peek (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);

  for (index = 0; index < enum_class->n_values; index++) {
    if (g_strcmp0 (enum_class->values[index].value_nick, id) == 0)
      return g_strdup (id);
  }

  return NULL;
}

static gchar *
extractable_get_id (GESExtractable * self)
{
  guint index;
  GEnumClass *enum_class;
  guint value = GES_TRANSITION_CLIP (self)->vtype;

  enum_class = g_type_class_peek (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);
  for (index = 0; index < enum_class->n_values; index++) {
    if (enum_class->values[index].value == value)
      return g_strdup (enum_class->values[index].value_nick);
  }

  return NULL;
}

static gboolean
extractable_set_asset (GESExtractable * self, GESAsset * asset)
{
  GEnumClass *enum_class;
  GESVideoStandardTransitionType value;
  GESTransitionClip *trans = GES_TRANSITION_CLIP (self);
  const gchar *vtype = ges_asset_get_id (asset);
  GESAsset *prev_asset = ges_extractable_get_asset (self);
  GList *tmp;

  if (!(ges_clip_get_supported_formats (GES_CLIP (self)) &
          GES_TRACK_TYPE_VIDEO)) {
    return FALSE;
  }

  /* Update the transition type if we actually changed it */
  if (g_strcmp0 (vtype, trans->priv->vtype_name)) {
    guint index;

    value = GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE;
    enum_class = g_type_class_peek (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);

    /* Find the in value in use */
    for (index = 0; index < enum_class->n_values; index++) {
      if (g_strcmp0 (enum_class->values[index].value_nick, vtype) == 0) {
        value = enum_class->values[index].value;
        break;
      }
    }
    ges_transition_clip_update_vtype_internal (GES_CLIP (self), value, FALSE);
    g_object_notify (G_OBJECT (self), "vtype");
  }

  if (!prev_asset)
    return TRUE;

  for (tmp = GES_CONTAINER_CHILDREN (self); tmp; tmp = tmp->next) {
    if (ges_track_element_get_creator_asset (tmp->data) == prev_asset)
      ges_track_element_set_creator_asset (tmp->data, asset);
  }

  return TRUE;
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_id = extractable_get_id;
  iface->get_parameters_from_id = extractable_get_parameters_from_id;
  iface->can_update_asset = TRUE;
  iface->set_asset_full = extractable_set_asset;
}

G_DEFINE_TYPE_WITH_CODE (GESTransitionClip,
    ges_transition_clip, GES_TYPE_BASE_TRANSITION_CLIP,
    G_ADD_PRIVATE (GESTransitionClip)
    G_IMPLEMENT_INTERFACE (GES_TYPE_EXTRACTABLE,
        ges_extractable_interface_init));

static void
ges_transition_clip_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESTransitionClip *self = GES_TRANSITION_CLIP (object);
  switch (property_id) {
    case PROP_VTYPE:
      g_value_set_enum (value, self->vtype);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_transition_clip_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESClip *self = GES_CLIP (object);

  switch (property_id) {
    case PROP_VTYPE:
      ges_transition_clip_update_vtype_internal (self,
          g_value_get_enum (value), TRUE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static gboolean
_lookup_child (GESTimelineElement * self, const gchar * prop_name,
    GObject ** child, GParamSpec ** pspec)
{
  GESTimelineElementClass *element_klass =
      g_type_class_peek (GES_TYPE_TIMELINE_ELEMENT);

  /* Bypass the container implementation as we handle children properties directly */
  /* FIXME Implement a syntax to precisely get properties by path */
  if (element_klass->lookup_child (self, prop_name, child, pspec))
    return TRUE;

  return FALSE;
}


static void
ges_transition_clip_class_init (GESTransitionClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESClipClass *timobj_class = GES_CLIP_CLASS (klass);
  GESContainerClass *container_class = GES_CONTAINER_CLASS (klass);

  object_class->get_property = ges_transition_clip_get_property;
  object_class->set_property = ges_transition_clip_set_property;

  /**
   * GESTransitionClip:vtype:
   *
   * a #GESVideoStandardTransitionType representing the wipe to use
   */
  g_object_class_install_property (object_class, PROP_VTYPE,
      g_param_spec_enum ("vtype", "VType",
          "The SMPTE video wipe to use, or 0 for crossfade",
          GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE,
          GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

  GES_TIMELINE_ELEMENT_CLASS (klass)->lookup_child = _lookup_child;
  container_class->child_added = _child_added;
  container_class->child_removed = _child_removed;

  timobj_class->create_track_element = _create_track_element;
}

static void
ges_transition_clip_init (GESTransitionClip * self)
{

  self->priv = ges_transition_clip_get_instance_private (self);

  self->vtype = GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE;
  self->priv->vtype_name = NULL;
}

static void
_child_removed (GESContainer * container, GESTimelineElement * element)
{
  GESTransitionClipPrivate *priv = GES_TRANSITION_CLIP (container)->priv;

  /* If this is called, we should be sure the trackelement exists */
  if (GES_IS_VIDEO_TRANSITION (element)) {
    GST_DEBUG_OBJECT (container, "%" GST_PTR_FORMAT " removed", element);
    priv->video_transitions = g_slist_remove (priv->video_transitions, element);
    gst_object_unref (element);
  }
  /* call parent method */
  GES_CONTAINER_CLASS (ges_transition_clip_parent_class)->child_removed
      (container, element);
}

static void
_child_added (GESContainer * container, GESTimelineElement * element)
{
  GESTransitionClipPrivate *priv = GES_TRANSITION_CLIP (container)->priv;

  if (GES_IS_VIDEO_TRANSITION (element)) {
    GObjectClass *eklass = G_OBJECT_GET_CLASS (element);

    GST_DEBUG_OBJECT (container, "%" GST_PTR_FORMAT " added", element);
    priv->video_transitions =
        g_slist_prepend (priv->video_transitions, gst_object_ref (element));

    ges_timeline_element_add_child_property (GES_TIMELINE_ELEMENT (container),
        g_object_class_find_property (eklass, "invert"), G_OBJECT (element));
    ges_timeline_element_add_child_property (GES_TIMELINE_ELEMENT (container),
        g_object_class_find_property (eklass, "border"), G_OBJECT (element));
  }
  /* call parent method */
  GES_CONTAINER_CLASS (ges_transition_clip_parent_class)->child_added
      (container, element);
}

static GESTrackElement *
_create_track_element (GESClip * clip, GESTrackType type)
{
  GESTransitionClip *transition = (GESTransitionClip *) clip;
  GESTrackElement *res = NULL;
  GESTrackType supportedformats;

  GST_DEBUG ("Creating a GESTransition");

  supportedformats = ges_clip_get_supported_formats (clip);
  if (type == GES_TRACK_TYPE_VIDEO) {
    if (supportedformats == GES_TRACK_TYPE_UNKNOWN ||
        supportedformats & GES_TRACK_TYPE_VIDEO) {
      GESVideoTransition *trans;

      trans = ges_video_transition_new ();
      ges_video_transition_set_transition_type (trans, transition->vtype);

      res = GES_TRACK_ELEMENT (trans);
    } else {
      GST_DEBUG ("Not creating transition as video track not on"
          " supportedformats");
    }

  } else if (type == GES_TRACK_TYPE_AUDIO) {

    if (supportedformats == GES_TRACK_TYPE_UNKNOWN ||
        supportedformats & GES_TRACK_TYPE_AUDIO)
      res = GES_TRACK_ELEMENT (ges_audio_transition_new ());
    else
      GST_DEBUG ("Not creating transition as audio track"
          " not on supportedformats");

  } else
    GST_WARNING ("Transitions don't handle this track type");

  return res;
}

/**
 * ges_transition_clip_new:
 * @vtype: the type of transition to create
 *
 * Creates a new #GESTransitionClip.
 *
 * Returns: (transfer floating) (nullable): a newly created #GESTransitionClip,
 * or %NULL if something went wrong.
 */
GESTransitionClip *
ges_transition_clip_new (GESVideoStandardTransitionType vtype)
{
  GEnumValue *value;
  GEnumClass *klass;
  GESTransitionClip *ret = NULL;

  klass =
      G_ENUM_CLASS (g_type_class_ref (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE));
  if (!klass) {
    GST_ERROR ("Could not find the StandarTransitionType enum class");
    return NULL;
  }

  value = g_enum_get_value (klass, vtype);
  if (!value) {
    GST_ERROR ("Could not find enum value for %i", vtype);
    return NULL;
  }

  ret = ges_transition_clip_new_for_nick (((gchar *) value->value_nick));
  g_type_class_unref (klass);

  return ret;
}

/**
 * ges_transition_clip_new_for_nick:
 * @nick: a string representing the type of transition to create
 *
 * Creates a new #GESTransitionClip for the provided @nick.
 *
 * Returns: (transfer floating) (nullable): The newly created #GESTransitionClip,
 * or %NULL if something went wrong
 */

GESTransitionClip *
ges_transition_clip_new_for_nick (gchar * nick)
{
  GESTransitionClip *ret = NULL;
  GESAsset *asset = ges_asset_request (GES_TYPE_TRANSITION_CLIP, nick, NULL);

  if (asset != NULL) {
    ret = GES_TRANSITION_CLIP (ges_asset_extract (asset, NULL));

    gst_object_unref (asset);
  } else
    GST_WARNING ("No asset found for nick: %s", nick);

  return ret;
}
