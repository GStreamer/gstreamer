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
 * SECTION: ges-timeline-standard-transition
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
 * and transitions. If you use #GESTimelineStandardTransitions in another type of
 * #GESTimelineLayer, you will need to manage priorities yourself.
 */

#include <ges/ges.h>
#include "ges-internal.h"

struct _GESTimelineStandardTransitionPrivate
{
  GSList *track_video_transitions;
};

enum
{
  PROP_VTYPE = 5,
};

G_DEFINE_TYPE (GESTimelineStandardTransition, ges_timeline_standard_transition,
    GES_TYPE_TIMELINE_TRANSITION);

static GESTrackObject *ges_tl_transition_create_track_object (GESTimelineObject
    * self, GESTrack * track);
static void
ges_timeline_standard_transition_track_object_added (GESTimelineObject * obj,
    GESTrackObject * tckobj);
static void
ges_timeline_standard_transition_track_object_released (GESTimelineObject * obj,
    GESTrackObject * tckobj);

static void
ges_timeline_standard_transition_update_vtype_internal (GESTimelineObject *
    self, GESVideoStandardTransitionType value)
{
  GSList *tmp;
  GESTimelineStandardTransition *trself =
      GES_TIMELINE_STANDARD_TRANSITION (self);

  for (tmp = trself->priv->track_video_transitions; tmp; tmp = tmp->next) {
    if (!ges_track_video_transition_set_transition_type
        (GES_TRACK_VIDEO_TRANSITION (tmp->data), value))
      return;
  }

  trself->vtype = value;
}

static void
ges_timeline_standard_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESTimelineStandardTransition *self =
      GES_TIMELINE_STANDARD_TRANSITION (object);
  switch (property_id) {
    case PROP_VTYPE:
      g_value_set_enum (value, self->vtype);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_standard_transition_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  GESTimelineObject *self = GES_TIMELINE_OBJECT (object);

  switch (property_id) {
    case PROP_VTYPE:
      ges_timeline_standard_transition_update_vtype_internal (self,
          g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_standard_transition_class_init (GESTimelineStandardTransitionClass
    * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  g_type_class_add_private (klass,
      sizeof (GESTimelineStandardTransitionPrivate));

  object_class->get_property = ges_timeline_standard_transition_get_property;
  object_class->set_property = ges_timeline_standard_transition_set_property;

  /**
   * GESTimelineStandardTransition:vtype
   *
   * a #GESVideoStandardTransitionType representing the wipe to use
   */
  g_object_class_install_property (object_class, PROP_VTYPE,
      g_param_spec_enum ("vtype", "VType",
          "The SMPTE video wipe to use, or 0 for crossfade",
          GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE,
          GES_VIDEO_STANDARD_TRANSITION_TYPE_CROSSFADE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));


  timobj_class->create_track_object = ges_tl_transition_create_track_object;
  timobj_class->need_fill_track = FALSE;
  timobj_class->track_object_added =
      ges_timeline_standard_transition_track_object_added;
  timobj_class->track_object_released =
      ges_timeline_standard_transition_track_object_released;
}

static void
ges_timeline_standard_transition_init (GESTimelineStandardTransition * self)
{

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_STANDARD_TRANSITION,
      GESTimelineStandardTransitionPrivate);

  self->vtype = GES_VIDEO_STANDARD_TRANSITION_TYPE_NONE;
}

static void
ges_timeline_standard_transition_track_object_released (GESTimelineObject * obj,
    GESTrackObject * tckobj)
{
  GESTimelineStandardTransitionPrivate *priv =
      GES_TIMELINE_STANDARD_TRANSITION (obj)->priv;

  /* If this is called, we should be sure the tckobj exists */
  if (GES_IS_TRACK_VIDEO_TRANSITION (tckobj)) {
    GST_DEBUG ("GESTrackVideoTransition %p released from %p", tckobj, obj);
    priv->track_video_transitions =
        g_slist_remove (priv->track_video_transitions, tckobj);
    g_object_unref (tckobj);
  }
}

static void
ges_timeline_standard_transition_track_object_added (GESTimelineObject * obj,
    GESTrackObject * tckobj)
{
  GESTimelineStandardTransitionPrivate *priv =
      GES_TIMELINE_STANDARD_TRANSITION (obj)->priv;

  if (GES_IS_TRACK_VIDEO_TRANSITION (tckobj)) {
    GST_DEBUG ("GESTrackVideoTransition %p added to %p", tckobj, obj);
    priv->track_video_transitions =
        g_slist_prepend (priv->track_video_transitions, g_object_ref (tckobj));
  }
}

static GESTrackObject *
ges_tl_transition_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTimelineStandardTransition *transition =
      (GESTimelineStandardTransition *) obj;
  GESTrackObject *res;
  GESTrackType supportedformats;

  GST_DEBUG ("Creating a GESTrackTransition");

  supportedformats = ges_timeline_object_get_supported_formats (obj);
  if (track->type == GES_TRACK_TYPE_VIDEO &&
      (supportedformats == GES_TRACK_TYPE_UNKNOWN ||
          supportedformats & GES_TRACK_TYPE_VIDEO)) {
    res = GES_TRACK_OBJECT (ges_track_video_transition_new ());
    ges_track_video_transition_set_transition_type ((GESTrackVideoTransition *)
        res, transition->vtype);

  } else if (track->type == GES_TRACK_TYPE_AUDIO &&
      (supportedformats == GES_TRACK_TYPE_UNKNOWN ||
          supportedformats & GES_TRACK_TYPE_AUDIO)) {
    res = GES_TRACK_OBJECT (ges_track_audio_transition_new ());
  }

  else {
    GST_WARNING ("Transitions don't handle this track type");
    return NULL;
  }

  return res;
}

/**
 * ges_timeline_standard_transition_new:
 * @vtype: the type of transition to create
 *
 * Creates a new #GESTimelineStandardTransition.
 *
 * Returns: a newly created #GESTimelineStandardTransition, or %NULL if something
 * went wrong.
 */
GESTimelineStandardTransition *
ges_timeline_standard_transition_new (GESVideoStandardTransitionType vtype)
{
  return g_object_new (GES_TYPE_TIMELINE_STANDARD_TRANSITION, "vtype",
      (gint) vtype, NULL);
}

/**
 * ges_timeline_standard_transition_new_for_nick:
 * @nick: a string representing the type of transition to create
 *
 * Creates a new #GESTimelineStandardTransition for the provided @nick.
 *
 * Returns: The newly created #GESTimelineStandardTransition, or %NULL if something
 * went wrong
 */

GESTimelineStandardTransition *
ges_timeline_standard_transition_new_for_nick (gchar * nick)
{
  GEnumValue *value;
  GEnumClass *klass;
  GESTimelineStandardTransition *ret = NULL;

  klass =
      G_ENUM_CLASS (g_type_class_ref (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE));
  if (!klass)
    return NULL;

  value = g_enum_get_value_by_nick (klass, nick);
  if (value) {
    ret = g_object_new (GES_TYPE_TIMELINE_STANDARD_TRANSITION, "vtype",
        (gint) value->value, NULL);
  }

  g_type_class_unref (klass);
  return ret;
}
