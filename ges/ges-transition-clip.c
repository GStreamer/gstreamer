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
 * SECTION: ges-transition-clip
 * @short_description: Transition from one clip to another in a
 * #GESTimelineLayer
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
 * #GESSimpleTimelineLayer will automatically manage the priorities of sources
 * and transitions. If you use #GESTransitionClips in another type of
 * #GESTimelineLayer, you will need to manage priorities yourself.
 *
 * The ID of the ExtractableType is the nickname of the vtype property value. Note
 * that this value can be changed after creation and the GESExtractable.asset value
 * will be updated when needed.
 */

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
static void
ges_transition_clip_track_element_added (GESClip * obj,
    GESTrackElement * trackelement);
static void ges_transition_clip_track_element_released (GESClip * obj,
    GESTrackElement * trackelement);

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
    /* We already checked the value, so we can be sure no error will accured */
    ges_extractable_set_asset (GES_EXTRACTABLE (self),
        ges_asset_request (GES_TYPE_TRANSITION_CLIP, asset_id, NULL));
  }
}

/* GESExtractable interface overrides */
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

static void
extractable_set_asset (GESExtractable * self, GESAsset * asset)
{
  GEnumClass *enum_class;
  GESVideoStandardTransitionType value;
  GESTransitionClip *trans = GES_TRANSITION_CLIP (self);
  const gchar *vtype = ges_asset_get_id (asset);

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
  }
}

static void
ges_extractable_interface_init (GESExtractableInterface * iface)
{
  iface->check_id = (GESExtractableCheckId) extractable_check_id;
  iface->get_id = extractable_get_id;
  iface->get_parameters_from_id = extractable_get_parameters_from_id;
  iface->can_update_asset = TRUE;
  iface->set_asset = extractable_set_asset;
}

G_DEFINE_TYPE_WITH_CODE (GESTransitionClip,
    ges_transition_clip, GES_TYPE_BASE_TRANSITION_CLIP,
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

static void
ges_transition_clip_class_init (GESTransitionClipClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESClipClass *timobj_class = GES_CLIP_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTransitionClipPrivate));

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


  timobj_class->create_track_element = _create_track_element;
  timobj_class->need_fill_track = FALSE;
  timobj_class->track_element_added = ges_transition_clip_track_element_added;
  timobj_class->track_element_released =
      ges_transition_clip_track_element_released;
}

static void
ges_transition_clip_init (GESTransitionClip * self)
{

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TRANSITION_CLIP, GESTransitionClipPrivate);

  self->vtype = GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE;
  self->priv->vtype_name = NULL;
}

static void
ges_transition_clip_track_element_released (GESClip * obj,
    GESTrackElement * trackelement)
{
  GESTransitionClipPrivate *priv = GES_TRANSITION_CLIP (obj)->priv;

  /* If this is called, we should be sure the trackelement exists */
  if (GES_IS_VIDEO_TRANSITION (trackelement)) {
    GST_DEBUG ("GESVideoTransition %p released from %p", trackelement, obj);
    priv->video_transitions =
        g_slist_remove (priv->video_transitions, trackelement);
    g_object_unref (trackelement);
  }
}

static void
ges_transition_clip_track_element_added (GESClip * obj,
    GESTrackElement * trackelement)
{
  GESTransitionClipPrivate *priv = GES_TRANSITION_CLIP (obj)->priv;

  if (GES_IS_VIDEO_TRANSITION (trackelement)) {
    GST_DEBUG ("GESVideoTransition %p added to %p", trackelement, obj);
    priv->video_transitions =
        g_slist_prepend (priv->video_transitions, g_object_ref (trackelement));
  }
}

static GESTrackElement *
_create_track_element (GESClip * obj, GESTrackType type)
{
  GESTransitionClip *transition = (GESTransitionClip *) obj;
  GESTrackElement *res = NULL;
  GESTrackType supportedformats;

  GST_DEBUG ("Creating a GESTransition");

  supportedformats = ges_clip_get_supported_formats (obj);
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
 * Returns: a newly created #GESTransitionClip, or %NULL if something
 * went wrong.
 */
GESTransitionClip *
ges_transition_clip_new (GESVideoStandardTransitionType vtype)
{
  return g_object_new (GES_TYPE_TRANSITION_CLIP, "vtype", (gint) vtype, NULL);
}

/**
 * ges_transition_clip_new_for_nick:
 * @nick: a string representing the type of transition to create
 *
 * Creates a new #GESTransitionClip for the provided @nick.
 *
 * Returns: The newly created #GESTransitionClip, or %NULL if something
 * went wrong
 */

GESTransitionClip *
ges_transition_clip_new_for_nick (gchar * nick)
{
  GEnumValue *value;
  GEnumClass *klass;
  GESTransitionClip *ret = NULL;

  klass =
      G_ENUM_CLASS (g_type_class_ref (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE));
  if (!klass)
    return NULL;

  value = g_enum_get_value_by_nick (klass, nick);
  if (value) {
    ret = g_object_new (GES_TYPE_TRANSITION_CLIP, "vtype",
        (gint) value->value, NULL);
  }

  g_type_class_unref (klass);
  return ret;
}
