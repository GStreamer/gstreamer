/* GStreamer Editing Services
 * Copyright (C) 2026 Igalia S.L.
 * Copyright (C) 2026 Thibault Saunier <tsaunier@igalia.com>
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
 * You should have received to this library a copy of the GNU Library
 * General Public License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#pragma once

#include <glib-object.h>
#include <ges/ges-types.h>

G_BEGIN_DECLS

/**
 * GESSourceTrackMap:
 *
 * An immutable routing of a #GESUriClip's URI sources to the #GESTrack(s) they
 * are placed into, set on a clip with ges_uri_clip_set_source_track_map(). Build
 * one with a #GESSourceTrackMapBuilder or, in C, ges_source_track_map_new_full().
 *
 * Since: 1.30
 */

#define GES_TYPE_SOURCE_TRACK_MAP (ges_source_track_map_get_type ())

GES_API
GType ges_source_track_map_get_type (void);

/**
 * GESSourceTrackMapEntry:
 * @source: (transfer none): a #GESUriSourceAsset describing one source of a URI
 * @track: (transfer none) (nullable): the #GESTrack the source should
 *   be placed into, or %NULL to select the source without placing it (its
 *   core source is created but put in no track)
 *
 * A single source-to-track routing, used to build a #GESSourceTrackMap through
 * ges_source_track_map_new_full().
 *
 * Since: 1.30
 */
typedef struct _GESSourceTrackMapEntry
{
  GESUriSourceAsset *source;
  GESTrack *track;
} GESSourceTrackMapEntry;

GES_API
GESSourceTrackMap *ges_source_track_map_new_full (const GESSourceTrackMapEntry *
    entries) G_GNUC_WARN_UNUSED_RESULT;

GES_API
GESSourceTrackMap *ges_source_track_map_ref (GESSourceTrackMap * self);

GES_API
void ges_source_track_map_unref (GESSourceTrackMap * self);

GES_API
gboolean ges_source_track_map_contains (GESSourceTrackMap * self,
    GESUriSourceAsset * source);

GES_API
GList *ges_source_track_map_get_tracks (GESSourceTrackMap * self,
    GESUriSourceAsset * source);

GES_API
GList *ges_source_track_map_get_sources (GESSourceTrackMap * self);

GES_API
guint ges_source_track_map_get_size (GESSourceTrackMap * self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GESSourceTrackMap, ges_source_track_map_unref)

/**
 * GESSourceTrackMapBuilder:
 *
 * An incremental builder for an immutable #GESSourceTrackMap. Create it with
 * ges_source_track_map_builder_new(), add routings with
 * ges_source_track_map_builder_add(), then finish with
 * ges_source_track_map_builder_build(), which consumes the builder and
 * returns the immutable map.
 *
 * Since: 1.30
 */
#define GES_TYPE_SOURCE_TRACK_MAP_BUILDER (ges_source_track_map_builder_get_type ())

GES_API
GType ges_source_track_map_builder_get_type (void);

GES_API
GESSourceTrackMapBuilder *ges_source_track_map_builder_new (void)
    G_GNUC_WARN_UNUSED_RESULT;

GES_API
GESSourceTrackMapBuilder *ges_source_track_map_builder_add
    (GESSourceTrackMapBuilder * builder, GESUriSourceAsset * source,
    GESTrack * track);

GES_API
GESSourceTrackMap *ges_source_track_map_builder_build
    (GESSourceTrackMapBuilder * builder) G_GNUC_WARN_UNUSED_RESULT;

GES_API
GESSourceTrackMapBuilder *ges_source_track_map_builder_copy
    (GESSourceTrackMapBuilder * builder);

GES_API
void ges_source_track_map_builder_free (GESSourceTrackMapBuilder * builder);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (GESSourceTrackMapBuilder,
    ges_source_track_map_builder_free)

G_END_DECLS
