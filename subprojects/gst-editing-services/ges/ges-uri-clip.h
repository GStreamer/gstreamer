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
#include <ges/ges-source-clip.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_URI_CLIP ges_uri_clip_get_type()
GES_DECLARE_TYPE(UriClip, uri_clip, URI_CLIP);

struct _GESUriClip {
  GESSourceClip parent;

  /*< private >*/
  GESUriClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESUriClipClass:
 */

struct _GESUriClipClass {
  /*< private >*/
  GESSourceClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API void
ges_uri_clip_set_mute (GESUriClip * self, gboolean mute);

GES_API void
ges_uri_clip_set_is_image (GESUriClip * self,
    gboolean is_image);

GES_API
gboolean ges_uri_clip_is_muted (GESUriClip * self);
GES_API
gboolean ges_uri_clip_is_image (GESUriClip * self);
GES_API
const gchar *ges_uri_clip_get_uri (GESUriClip * self);

GES_API
GESUriClip* ges_uri_clip_new (const gchar *uri) G_GNUC_WARN_UNUSED_RESULT;

G_END_DECLS
