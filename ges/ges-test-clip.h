/* GStreamer Editing Services
 * Copyright (C) 2009 Brandon Lewis <brandon.lewis@collabora.co.uk>
 *               2009 Nokia Corporation
 * Copyright (C) 2020 Igalia S.L
 *     Author: 2020 Thibault Saunier <tsaunier@igalia.com>
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
#include <ges/ges-enums.h>
#include <ges/ges-types.h>
#include <ges/ges-source-clip.h>
#include <ges/ges-track.h>

G_BEGIN_DECLS

#define GES_TYPE_TEST_CLIP ges_test_clip_get_type()
GES_DECLARE_TYPE(TestClip, test_clip, TEST_CLIP);

/**
 * GESTestClip:
 */

struct _GESTestClip {

  GESSourceClip parent;

  /*< private >*/
  GESTestClipPrivate *priv;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

/**
 * GESTestClipClass:
 */

struct _GESTestClipClass {
  /*< private >*/
  GESSourceClipClass parent_class;

  /* Padding for API extension */
  gpointer _ges_reserved[GES_PADDING];
};

GES_API void
ges_test_clip_set_mute (GESTestClip * self, gboolean mute);

GES_API void
ges_test_clip_set_vpattern (GESTestClip * self,
    GESVideoTestPattern vpattern);

GES_API void
ges_test_clip_set_frequency (GESTestClip * self, gdouble freq);

GES_API void
ges_test_clip_set_volume (GESTestClip * self,
    gdouble volume);


GES_API GESVideoTestPattern
ges_test_clip_get_vpattern (GESTestClip * self);

GES_API
gboolean ges_test_clip_is_muted (GESTestClip * self);
GES_API
gdouble ges_test_clip_get_frequency (GESTestClip * self);
GES_API
gdouble ges_test_clip_get_volume (GESTestClip * self);

GES_API
GESTestClip* ges_test_clip_new (void);
GES_API
GESTestClip* ges_test_clip_new_for_nick(gchar * nick);

G_END_DECLS
