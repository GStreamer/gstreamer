/* GStreamer Editing Services
 * Copyright (C) 2010 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2010 Nokia Corporation
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
 * SECTION:ges-track-audio-transition
 * @short_description: implements audio crossfade transitino
 */

#include "ges-internal.h"
#include "ges-track-object.h"
#include "ges-track-audio-transition.h"

G_DEFINE_TYPE (GESTrackAudioTransition, ges_track_audio_transition,
    GES_TYPE_TRACK_TRANSITION);

enum
{
  PROP_0,
};

static void ges_track_audio_transition_dispose (GObject * object);

static void ges_track_audio_transition_finalize (GObject * object);

static void ges_track_audio_transition_get_property (GObject * object, guint
    property_id, GValue * value, GParamSpec * pspec);

static void ges_track_audio_transition_set_property (GObject * object, guint
    property_id, const GValue * value, GParamSpec * pspec);

static void
ges_track_audio_transition_class_init (GESTrackAudioTransitionClass * klass)
{
  GObjectClass *object_class;
  GESTrackTransitionClass *bg_class;

  object_class = G_OBJECT_CLASS (klass);
  bg_class = GES_TRACK_TRANSITION_CLASS (klass);

  object_class->get_property = ges_track_audio_transition_get_property;
  object_class->set_property = ges_track_audio_transition_set_property;
  object_class->dispose = ges_track_audio_transition_dispose;
  object_class->finalize = ges_track_audio_transition_finalize;

}

static void
ges_track_audio_transition_init (GESTrackAudioTransition * self)
{
}

static void
ges_track_audio_transition_dispose (GObject * object)
{
  G_OBJECT_CLASS (ges_track_audio_transition_parent_class)->dispose (object);
}

static void
ges_track_audio_transition_finalize (GObject * object)
{
  G_OBJECT_CLASS (ges_track_audio_transition_parent_class)->finalize (object);
}

static void
ges_track_audio_transition_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
ges_track_audio_transition_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec)
{
  switch (property_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

GESTrackAudioTransition *
ges_track_audio_transition_new (void)
{
  return g_object_new (GES_TYPE_TRACK_AUDIO_TRANSITION, NULL);
}
