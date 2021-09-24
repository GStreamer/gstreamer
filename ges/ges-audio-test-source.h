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
#include <ges/ges-audio-source.h>

G_BEGIN_DECLS

#define GES_TYPE_AUDIO_TEST_SOURCE ges_audio_test_source_get_type()
GES_DECLARE_TYPE(AudioTestSource, audio_test_source, AUDIO_TEST_SOURCE);

/**
 * GESAudioTestSource:
 *
 * ### Children Properties
 *
 *  {{ libs/GESAudioTestSource-children-props.md }}
 */

struct _GESAudioTestSource {
  GESAudioSource parent;

  /*< private >*/
  GESAudioTestSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESAudioTestSourceClass {
  /*< private >*/
  GESAudioSourceClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API
void ges_audio_test_source_set_freq(GESAudioTestSource *self,
                                          gdouble freq);

GES_API
void ges_audio_test_source_set_volume(GESAudioTestSource *self,
                                            gdouble volume);

GES_API
double ges_audio_test_source_get_freq(GESAudioTestSource *self);
GES_API
double ges_audio_test_source_get_volume(GESAudioTestSource *self);
G_END_DECLS
