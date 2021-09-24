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

#pragma once

#include <glib-object.h>
#include <ges/ges-types.h>
#include <ges/ges-audio-source.h>

G_BEGIN_DECLS

typedef struct _GESUriSource GESUriSource;
#define GES_TYPE_AUDIO_URI_SOURCE ges_audio_uri_source_get_type()
GES_DECLARE_TYPE(AudioUriSource, audio_uri_source, AUDIO_URI_SOURCE);

/**
 * GESAudioUriSource:
 *
 * ### Children Properties
 *
 *  {{ libs/GESVideoUriSource-children-props.md }}
 */
struct _GESAudioUriSource {
  /*< private >*/
  GESAudioSource parent;

  gchar *uri;

  GESUriSource *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

struct _GESAudioUriSourceClass {
  /*< private >*/
  GESAudioSourceClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

G_END_DECLS
