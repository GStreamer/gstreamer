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

#ifndef _GES_TRACK_AUDIO_BACKGROUND_SOURCE
#define _GES_TRACK_AUDIO_BACKGROUND_SOURCE

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-track-background-source.h>

G_BEGIN_DECLS

#define GES_TYPE_TRACK_AUDIO_BACKGROUND_SOURCE ges_track_abg_src_get_type()

#define GES_TRACK_AUDIO_BACKGROUND_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GES_TYPE_TRACK_AUDIO_BACKGROUND_SOURCE, GESTrackAudioBackgroundSource))

#define GES_TRACK_AUDIO_BACKGROUND_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST ((klass), GES_TYPE_TRACK_AUDIO_BACKGROUND_SOURCE, GESTrackAudioBackgroundSourceClass))

#define GES_IS_TRACK_AUDIO_BACKGROUND_SOURCE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GES_TYPE_TRACK_AUDIO_BACKGROUND_SOURCE))

#define GES_IS_TRACK_AUDIO_BACKGROUND_SOURCE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE ((klass), GES_TYPE_TRACK_AUDIO_BACKGROUND_SOURCE))

#define GES_TRACK_AUDIO_BACKGROUND_SOURCE_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS ((obj), GES_TYPE_TRACK_AUDIO_BACKGROUND_SOURCE, GESTrackAudioBackgroundSourceClass))

/** 
 * GESTrackAudioBackgroundSource:
 * @parent: parent
 *
 */

struct _GESTrackAudioBackgroundSource {
  GESTrackBackgroundSource parent;

  /*< public >*/
};

/**
 * GESTrackAudioBackgroundSourceClass:
 * @parent_class: parent class
 *
 */

struct _GESTrackAudioBackgroundSourceClass {
  GESTrackBackgroundSourceClass parent_class;

  /*< public >*/
};

GType ges_track_abg_src_get_type (void);

GESTrackAudioBackgroundSource* ges_track_audio_background_source_new (void);

G_END_DECLS

#endif /* _GES_TRACK_AUDIO_BACKGROUND_SOURCE */

