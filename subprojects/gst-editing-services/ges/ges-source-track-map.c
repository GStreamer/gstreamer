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

/**
 * SECTION: gessourcetrackmap
 * @title: GESSourceTrackMap
 * @short_description: A routing of a URI's sources to the tracks they land in
 *
 * A #GESSourceTrackMap describes which sources of a #GESUriClip's URI should be
 * used, and, for each of them, the #GESTrack(s) its source should be placed
 * into. It is set on a clip with ges_uri_clip_set_source_track_map().
 *
 * Only the sources present in the map produce a core child. A source may be
 * routed to one track, or to several (its source is then copied into each), or
 * to none (%NULL, "selected but unplaced": the core child is created but put in
 * no track).
 *
 * A #GESSourceTrackMap is immutable: once built it cannot be changed, so it can
 * be shared safely between threads and between clips. Build one with a
 * #GESSourceTrackMapBuilder (ges_source_track_map_builder_new(),
 * ges_source_track_map_builder_add(),
 * ges_source_track_map_builder_build()) or, in C, all at once with
 * ges_source_track_map_new_full().
 *
 * Since: 1.30
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-internal.h"
#include "ges-source-track-map.h"
#include "ges-track.h"
#include "ges-uri-asset.h"

struct _GESSourceTrackMap
{
  /* ordered list of the mapped sources (GESUriSourceAsset, owned); holds every
   * mapped source, including one mapped to no track (unplaced) */
  GList *sources;
  /* GESUriSourceAsset -> GPtrArray of GESTrack (all owned). A source routed to
   * one or more tracks has its (non-empty) array here; an unplaced source has
   * no entry. */
  GHashTable *tracks;
};

G_DEFINE_BOXED_TYPE (GESSourceTrackMap, ges_source_track_map,
    ges_source_track_map_ref, ges_source_track_map_unref);

static void
_clear (gpointer data)
{
  GESSourceTrackMap *self = data;

  g_list_free_full (self->sources, gst_object_unref);
  g_hash_table_unref (self->tracks);
}

/**
 * ges_source_track_map_new_full:
 * @entries: (array zero-terminated=1): an array of #GESSourceTrackMapEntry routings,
 *   terminated by an entry whose #GESSourceTrackMapEntry.source is %NULL
 *
 * Creates a #GESSourceTrackMap from an inline array of routings, e.g.:
 *
 * ``` c
 * ges_source_track_map_new_full ((GESSourceTrackMapEntry[]) {
 *     {.source = stream0, .track = atrack0},
 *     {.source = stream1, .track = atrack1},
 *     {0},
 * });
 * ```
 *
 * Returns: (transfer full): a new #GESSourceTrackMap.
 *
 * Since: 1.30
 */
GESSourceTrackMap *
ges_source_track_map_new_full (const GESSourceTrackMapEntry * entries)
{
  GESSourceTrackMapBuilder *builder = ges_source_track_map_builder_new ();
  const GESSourceTrackMapEntry *entry;

  for (entry = entries; entry && entry->source; entry++)
    ges_source_track_map_builder_add (builder, entry->source, entry->track);

  return ges_source_track_map_builder_build (builder);
}

/**
 * ges_source_track_map_ref:
 * @self: a #GESSourceTrackMap
 *
 * Returns: (transfer full): @self
 *
 * Since: 1.30
 */
GESSourceTrackMap *
ges_source_track_map_ref (GESSourceTrackMap * self)
{
  g_return_val_if_fail (self, NULL);

  return g_atomic_rc_box_acquire (self);
}

/**
 * ges_source_track_map_unref:
 * @self: (transfer full): a #GESSourceTrackMap
 *
 * Since: 1.30
 */
void
ges_source_track_map_unref (GESSourceTrackMap * self)
{
  g_return_if_fail (self);

  g_atomic_rc_box_release_full (self, _clear);
}

/**
 * ges_source_track_map_contains:
 * @self: a #GESSourceTrackMap
 * @source: (transfer none): a #GESUriSourceAsset
 *
 * Returns: whether @source is routed by @self.
 *
 * Since: 1.30
 */
gboolean
ges_source_track_map_contains (GESSourceTrackMap * self,
    GESUriSourceAsset * source)
{
  g_return_val_if_fail (self, FALSE);

  return g_list_find (self->sources, source) != NULL;
}

/**
 * ges_source_track_map_get_tracks:
 * @self: a #GESSourceTrackMap
 * @source: (transfer none): a #GESUriSourceAsset
 *
 * Get the #GESTrack-s @source is routed to.
 *
 * Returns: (transfer full) (element-type GESTrack): the list of #GESTrack-s
 * @source is routed to, or %NULL if @source is unplaced or not in @self. Free
 * with g_list_free_full (list, gst_object_unref).
 *
 * Since: 1.30
 */
GList *
ges_source_track_map_get_tracks (GESSourceTrackMap * self,
    GESUriSourceAsset * source)
{
  GPtrArray *tracks;
  GList *res = NULL;
  guint i;

  g_return_val_if_fail (self, NULL);

  tracks = g_hash_table_lookup (self->tracks, source);
  if (tracks) {
    for (i = 0; i < tracks->len; i++)
      res =
          g_list_prepend (res, gst_object_ref (g_ptr_array_index (tracks, i)));
  }

  return g_list_reverse (res);
}

/**
 * ges_source_track_map_get_sources:
 * @self: a #GESSourceTrackMap
 *
 * Get the sources routed by @self, in the order they were added.
 *
 * Returns: (transfer full) (element-type GESUriSourceAsset): the list of mapped
 * #GESUriSourceAsset-s. Free with g_list_free_full (list, gst_object_unref).
 *
 * Since: 1.30
 */
GList *
ges_source_track_map_get_sources (GESSourceTrackMap * self)
{
  g_return_val_if_fail (self, NULL);

  return g_list_copy_deep (self->sources, (GCopyFunc) gst_object_ref, NULL);
}

/**
 * ges_source_track_map_get_size:
 * @self: a #GESSourceTrackMap
 *
 * Get the number of sources routed by @self.
 *
 * Returns: the number of mapped sources.
 *
 * Since: 1.30
 */
guint
ges_source_track_map_get_size (GESSourceTrackMap * self)
{
  g_return_val_if_fail (self, 0);

  return g_list_length (self->sources);
}

/* ---------------------------------------------------------------------------
 * Builder
 * ------------------------------------------------------------------------ */

struct _GESSourceTrackMapBuilder
{
  /* same shape as the map, but mutable while building */
  GList *sources;
  GHashTable *tracks;           /* GESUriSourceAsset -> GPtrArray of GESTrack */
};

G_DEFINE_BOXED_TYPE (GESSourceTrackMapBuilder, ges_source_track_map_builder,
    ges_source_track_map_builder_copy, ges_source_track_map_builder_free);

/**
 * ges_source_track_map_builder_new:
 *
 * Creates a new #GESSourceTrackMapBuilder. Add routings with
 * ges_source_track_map_builder_add(), then finish with
 * ges_source_track_map_builder_build().
 *
 * Returns: (transfer full): a new #GESSourceTrackMapBuilder.
 *
 * Since: 1.30
 */
GESSourceTrackMapBuilder *
ges_source_track_map_builder_new (void)
{
  GESSourceTrackMapBuilder *builder = g_new0 (GESSourceTrackMapBuilder, 1);

  builder->tracks = g_hash_table_new_full (NULL, NULL, gst_object_unref,
      (GDestroyNotify) g_ptr_array_unref);

  return builder;
}

/**
 * ges_source_track_map_builder_add:
 * @builder: a #GESSourceTrackMapBuilder
 * @source: (transfer none): a #GESUriSourceAsset of the clip's URI
 * @track: (transfer none) (nullable): a #GESTrack @source should be
 *   placed into, or %NULL to select @source without placing it (unplaced)
 *
 * Routes @source to @track. Called more than once with the same @source and
 * different tracks, @source is placed into each of them. Returns @builder
 * so calls can be chained.
 *
 * Returns: (transfer none): @builder.
 *
 * Since: 1.30
 */
GESSourceTrackMapBuilder *
ges_source_track_map_builder_add (GESSourceTrackMapBuilder * builder,
    GESUriSourceAsset * source, GESTrack * track)
{
  g_return_val_if_fail (builder, NULL);
  g_return_val_if_fail (GES_IS_URI_SOURCE_ASSET (source), builder);
  g_return_val_if_fail (track == NULL || GES_IS_TRACK (track), builder);

  if (!g_list_find (builder->sources, source))
    builder->sources =
        g_list_append (builder->sources, gst_object_ref (source));

  if (track) {
    GPtrArray *tracks = g_hash_table_lookup (builder->tracks, source);

    if (!tracks) {
      tracks = g_ptr_array_new_with_free_func (gst_object_unref);
      g_hash_table_insert (builder->tracks, gst_object_ref (source), tracks);
    }

    if (!g_ptr_array_find (tracks, track, NULL))
      g_ptr_array_add (tracks, gst_object_ref (track));
  }

  return builder;
}

/**
 * ges_source_track_map_builder_build:
 * @builder: (transfer full): a #GESSourceTrackMapBuilder
 *
 * Finishes the builder, returning the immutable #GESSourceTrackMap and freeing
 * @builder (it must not be used afterwards).
 *
 * Returns: (transfer full): a new #GESSourceTrackMap.
 *
 * Since: 1.30
 */
GESSourceTrackMap *
ges_source_track_map_builder_build (GESSourceTrackMapBuilder * builder)
{
  GESSourceTrackMap *self;

  g_return_val_if_fail (builder, NULL);

  self = g_atomic_rc_box_new0 (GESSourceTrackMap);
  /* steal the accumulated contents */
  self->sources = builder->sources;
  self->tracks = builder->tracks;
  g_free (builder);

  return self;
}

/**
 * ges_source_track_map_builder_copy:
 * @builder: a #GESSourceTrackMapBuilder
 *
 * Copies @builder, producing an independent builder in the same building state.
 *
 * Returns: (transfer full): a copy of @builder.
 *
 * Since: 1.30
 */
GESSourceTrackMapBuilder *
ges_source_track_map_builder_copy (GESSourceTrackMapBuilder * builder)
{
  GESSourceTrackMapBuilder *copy;
  GList *tmp;
  GHashTableIter iter;
  gpointer key, value;

  g_return_val_if_fail (builder, NULL);

  copy = ges_source_track_map_builder_new ();

  for (tmp = builder->sources; tmp; tmp = tmp->next)
    copy->sources = g_list_append (copy->sources, gst_object_ref (tmp->data));

  g_hash_table_iter_init (&iter, builder->tracks);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    GESUriSourceAsset *source = key;
    GPtrArray *tracks = value;
    guint i;

    for (i = 0; i < tracks->len; i++)
      ges_source_track_map_builder_add (copy, source,
          g_ptr_array_index (tracks, i));
  }

  return copy;
}

/**
 * ges_source_track_map_builder_free:
 * @builder: (transfer full): a #GESSourceTrackMapBuilder
 *
 * Frees a #GESSourceTrackMapBuilder, discarding the routings accumulated in it.
 * Use ges_source_track_map_builder_build() to obtain the map instead.
 *
 * Since: 1.30
 */
void
ges_source_track_map_builder_free (GESSourceTrackMapBuilder * builder)
{
  if (!builder)
    return;

  g_list_free_full (builder->sources, gst_object_unref);
  g_hash_table_unref (builder->tracks);
  g_free (builder);
}
