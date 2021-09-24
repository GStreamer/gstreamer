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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-transition.h>

G_BEGIN_DECLS

#define GES_TYPE_AUDIO_TRANSITION ges_audio_transition_get_type()
GES_DECLARE_TYPE(AudioTransition, audio_transition, AUDIO_TRANSITION);

/**
 * GESAudioTransition:
 *
 */

struct _GESAudioTransition {
  GESTransition parent;

  /*< private >*/
  GESAudioTransitionPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESAudioTransitionClass {
  GESTransitionClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_DEPRECATED
GESAudioTransition* ges_audio_transition_new (void);

G_END_DECLS
