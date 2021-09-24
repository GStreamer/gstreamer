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
#include <gst/gst.h>
#include <ges/ges-types.h>
#include <ges/ges-track-element.h>
#include <ges/ges-source.h>

G_BEGIN_DECLS

#define GES_TYPE_AUDIO_SOURCE ges_audio_source_get_type()
GES_DECLARE_TYPE(AudioSource, audio_source, AUDIO_SOURCE);

/**
 * GESAudioSource:
 *
 * Base class for audio sources
 */

struct _GESAudioSource {
  /*< private >*/
  GESSource parent;

  GESAudioSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESAudioSourceClass:
 */
struct _GESAudioSourceClass {
  /*< private >*/
  GESSourceClass parent_class;

  /*< public >*/
  /**
   * GESAudioSource::create_element:
   * @object: The #GESTrackElement
   *
   * Returns: (transfer floating): the #GstElement that the underlying nleobject
   * controls.
   *
   * Deprecated: 1.20: Use #GESSourceClass::create_element instead.
   */
  GstElement*  (*create_source)           (GESTrackElement * object);

  /*< private >*/
  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

G_END_DECLS
