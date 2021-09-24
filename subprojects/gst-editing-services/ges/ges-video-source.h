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

#define GES_TYPE_VIDEO_SOURCE ges_video_source_get_type()
GES_DECLARE_TYPE(VideoSource, video_source, VIDEO_SOURCE);

/**
 * GESVideoSource:
 *
 * Base class for video sources
 */

struct _GESVideoSource {
  /*< private >*/
  GESSource parent;

  GESVideoSourcePrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESVideoSourceClass:
 * @create_source: method to return the GstElement to put in the source topbin.
 * Other elements will be queued, like a videoscale.
 * In the case of a VideoUriSource, for example, the subclass will return a decodebin,
 * and we will append a videoscale.
 */
struct _GESVideoSourceClass {
  /*< private >*/
  GESSourceClass parent_class;

  /*< public >*/
  /**
   * GESVideoSource::create_element:
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
  union {
    gpointer _ges_reserved[GES_PADDING];
    struct {
      gboolean disable_scale_in_compositor;
      gboolean (*needs_converters)(GESVideoSource *self);
      gboolean (*get_natural_size)(GESVideoSource* self, gint* width, gint* height);
      gboolean (*create_filters)(GESVideoSource *self, GPtrArray *filters, gboolean needs_converters);
    } abi;
  } ABI;
};

GES_API
gboolean ges_video_source_get_natural_size(GESVideoSource* self, gint* width, gint* height);

G_END_DECLS
