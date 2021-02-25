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

G_BEGIN_DECLS

#define GES_TYPE_SOURCE ges_source_get_type()
GES_DECLARE_TYPE(Source, source, SOURCE);

/**
 * GESSource:
 *
 * Base class for single-media sources
 */

struct _GESSource {
  /*< private >*/
  GESTrackElement parent;

  GESSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESSourceClass:
 */

struct _GESSourceClass {
  /*< private >*/
  GESTrackElementClass parent_class;

  /**
   * GESSourceClass::select_pad:
   * @source: The @source for which to check if @pad should be used or not
   * @pad: The pad to check
   *
   * Check whether @pad should be exposed/used.
   *
   * Returns: %TRUE if @pad should be used %FALSE otherwise.
   *
   * Since: 1.20
   */
  gboolean (*select_pad)(GESSource *source, GstPad *pad);

  /**
   * GESSourceClass::create_source:
   * @source: The #GESAudioSource
   *
   * Creates the GstElement to put in the source topbin. Other elements will be
   * queued, like a volume. In the case of a AudioUriSource, for example, the
   * subclass will return a decodebin, and we will append a volume.
   *
   * Returns: (transfer floating): The source element to use.
   *
   * Since: 1.20
   */
  GstElement*  (*create_source)           (GESSource * source);

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING - 2];
};

G_END_DECLS
