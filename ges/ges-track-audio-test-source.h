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

#ifndef _GES_TRACK_AUDIO_TEST_SOURCE
#define _GES_TRACK_AUDIO_TEST_SOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_AUDIO_TEST_SOURCE ges_track_audio_test_source_get_type()

#define GES_TRACK_AUDIO_TEST_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_AUDIO_TEST_SOURCE, GESTrackAudioTestSource))

#define GES_TRACK_AUDIO_TEST_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_AUDIO_TEST_SOURCE, GESTrackAudioTestSourceClass))

#define GES_IS_TRACK_AUDIO_TEST_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_AUDIO_TEST_SOURCE))

#define GES_IS_TRACK_AUDIO_TEST_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_AUDIO_TEST_SOURCE))

#define GES_TRACK_AUDIO_TEST_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_AUDIO_TEST_SOURCE, GESTrackAudioTestSourceClass))

typedef struct _GESTrackAudioTestSourcePrivate GESTrackAudioTestSourcePrivate;


/**
 * GESTrackAudioTestSource:
 *
 */

struct _GESTrackAudioTestSource {
  GESTrackSource parent;

  /*< private >*/
  GESTrackAudioTestSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESTrackAudioTestSourceClass {
  /*< private >*/
  GESTrackSourceClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GType ges_track_audio_test_source_get_type (void);


void ges_track_audio_test_source_set_freq(GESTrackAudioTestSource *self,
                                          gdouble freq);

void ges_track_audio_test_source_set_volume(GESTrackAudioTestSource *self,
                                            gdouble volume);

double ges_track_audio_test_source_get_freq(GESTrackAudioTestSource *self);
double ges_track_audio_test_source_get_volume(GESTrackAudioTestSource *self);

GESTrackAudioTestSource* ges_track_audio_test_source_new (void);
G_END_DECLS

#endif /* _GES_TRACK_AUDIO_TEST_SOURCE */

