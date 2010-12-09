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
 * SECTION: ges-timeline-transition
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
 * and transitions. If you use #GESTimelineTransitions in another type of
 * #GESTimelineLayer, you will need to manage priorities yourself.
 */

#include "ges-internal.h"
#include "ges-timeline-transition.h"
#include "ges-track-video-transition.h"
#include "ges-track-audio-transition.h"

struct _GESTimelineTransitionPrivate
{
  /* Dummy variable */
  void *nothing;
};

enum
{
  PROP_VTYPE = 5,
};

G_DEFINE_TYPE (GESTimelineTransition, ges_timeline_transition,
    GES_TYPE_TIMELINE_OPERATION);

static GESTrackObject *ges_tl_transition_create_track_object (GESTimelineObject
    * self, GESTrack * track);

static void
ges_timeline_transition_update_vtype_internal (GESTimelineObject * self,
    GESVideoTransitionType value)
{
  GList *tmp, *trackobjects;
  GESTimelineTransition *trself = (GESTimelineTransition *) self;

  trackobjects = ges_timeline_object_get_track_objects (self);
  for (tmp = trackobjects; tmp; tmp = tmp->next) {
    GESTrackVideoTransition *obj;
    if (GES_IS_TRACK_VIDEO_TRANSITION (tmp->data)) {
      obj = (GESTrackVideoTransition *) tmp->data;
      if (!ges_track_video_transition_set_type (obj, value))
        return;
    }

    g_object_unref (GES_TRACK_OBJECT (tmp->data));
  }
  g_list_free (trackobjects);

  trself->vtype = value;
  return;
}

static void
ges_timeline_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  GESTimelineTransition *self = GES_TIMELINE_TRANSITION (object);
  switch (property_id) {
    case PROP_VTYPE:
      g_value_set_enum (value, self->vtype);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_transition_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GESTimelineObject *self = GES_TIMELINE_OBJECT (object);

  switch (property_id) {
    case PROP_VTYPE:
      ges_timeline_transition_update_vtype_internal (self,
          g_value_get_enum (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_timeline_transition_class_init (GESTimelineTransitionClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GESTimelineObjectClass *timobj_class = GES_TIMELINE_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GESTimelineTransitionPrivate));

  object_class->get_property = ges_timeline_transition_get_property;
  object_class->set_property = ges_timeline_transition_set_property;

  /**
   * GESTimelineTransition:vtype
   *
   * a #GESVideoTransitionType representing the wipe to use
   */
  g_object_class_install_property (object_class, PROP_VTYPE,
      g_param_spec_enum ("vtype", "VType",
          "The SMPTE video wipe to use, or 0 for crossfade",
          GES_VIDEO_TRANSITION_TYPE_TYPE, GES_VIDEO_TRANSITION_TYPE_CROSSFADE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));


  timobj_class->create_track_object = ges_tl_transition_create_track_object;
  timobj_class->need_fill_track = FALSE;
}

static void
ges_timeline_transition_init (GESTimelineTransition * self)
{

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GES_TYPE_TIMELINE_TRANSITION, GESTimelineTransitionPrivate);

  self->vtype = GES_VIDEO_TRANSITION_TYPE_NONE;
}

static GESTrackObject *
ges_tl_transition_create_track_object (GESTimelineObject * obj,
    GESTrack * track)
{
  GESTimelineTransition *transition = (GESTimelineTransition *) obj;
  GESTrackObject *res;

  GST_DEBUG ("Creating a GESTrackTransition");

  if (track->type == GES_TRACK_TYPE_VIDEO) {
    res = GES_TRACK_OBJECT (ges_track_video_transition_new ());
    ges_track_video_transition_set_type ((GESTrackVideoTransition *) res,
        transition->vtype);
  }

  else if (track->type == GES_TRACK_TYPE_AUDIO) {
    res = GES_TRACK_OBJECT (ges_track_audio_transition_new ());
  }

  else {
    GST_WARNING ("Transitions don't handle this track type");
    return NULL;
  }

  return res;
}

/**
 * ges_timeline_transition_new:
 * @vtype: the type of transition to create
 *
 */

GESTimelineTransition *
ges_timeline_transition_new (GESVideoTransitionType vtype)
{
  return g_object_new (GES_TYPE_TIMELINE_TRANSITION, "vtype", (gint) vtype,
      NULL);
}

/**
 * ges_timeline_transition_new_for_nick:
 * @nick: a string representing the type of transition to create
 */

GESTimelineTransition *
ges_timeline_transition_new_for_nick (gchar * nick)
{
  GEnumValue *value;
  GEnumClass *klass;
  GESTimelineTransition *ret = NULL;

  klass = G_ENUM_CLASS (g_type_class_ref (GES_VIDEO_TRANSITION_TYPE_TYPE));
  if (!klass)
    return NULL;

  value = g_enum_get_value_by_nick (klass, nick);
  if (value) {
    ret = g_object_new (GES_TYPE_TIMELINE_TRANSITION, "vtype",
        (gint) value->value, NULL);
  }

  g_type_class_unref (klass);
  return ret;
}
