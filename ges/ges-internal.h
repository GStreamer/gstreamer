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

#ifndef __GES_INTERNAL_H__
#define __GES_INTERNAL_H__

#include <gst/gst.h>
#include <gio/gio.h>

#include "ges-timeline.h"
#include "ges-track-object.h"

#if 0
#include "ges-asset.h"
#endif

GST_DEBUG_CATEGORY_EXTERN (_ges_debug);
#define GST_CAT_DEFAULT _ges_debug

gboolean
timeline_ripple_object         (GESTimeline *timeline, GESTrackObject *obj,
                                    GList * layers, GESEdge edge,
                                    guint64 position);

gboolean
timeline_slide_object          (GESTimeline *timeline, GESTrackObject *obj,
                                    GList * layers, GESEdge edge, guint64 position);

gboolean
timeline_roll_object           (GESTimeline *timeline, GESTrackObject *obj,
                                    GList * layers, GESEdge edge, guint64 position);

gboolean
timeline_trim_object           (GESTimeline *timeline, GESTrackObject * object,
                                    GList * layers, GESEdge edge, guint64 position);
gboolean
ges_timeline_trim_object_simple (GESTimeline * timeline, GESTrackObject * obj,
                                 GList * layers, GESEdge edge, guint64 position, gboolean snapping);

gboolean
ges_timeline_move_object_simple (GESTimeline * timeline, GESTrackObject * object,
                                 GList * layers, GESEdge edge, guint64 position);

gboolean
timeline_move_object           (GESTimeline *timeline, GESTrackObject * object,
                                    GList * layers, GESEdge edge, guint64 position);

gboolean
timeline_context_to_layer      (GESTimeline *timeline, gint offset);

#if 0
G_GNUC_INTERNAL void
ges_asset_cache_init (void);

G_GNUC_INTERNAL void
ges_asset_set_id (GESAsset *asset, const gchar *id);

G_GNUC_INTERNAL void
ges_asset_cache_put (GESAsset * asset, GSimpleAsyncResult *res);

G_GNUC_INTERNAL gboolean
ges_asset_cache_set_loaded(GType extractable_type, const gchar * id, GError *error);

GESAsset*
ges_asset_cache_lookup(GType extractable_type, const gchar * id);

gboolean
ges_asset_set_proxy (GESAsset *asset, const gchar *new_id);

G_GNUC_INTERNAL gboolean
ges_asset_request_id_update (GESAsset *asset, gchar **proposed_id,
    GError *error);

/* GESExtractable internall methods
 *
 * FIXME Check if that should be public later
 */
GType
ges_extractable_type_get_asset_type              (GType type);

G_GNUC_INTERNAL gchar *
ges_extractable_type_check_id                    (GType type, const gchar *id, GError **error);

GParameter *
ges_extractable_type_get_parameters_from_id      (GType type, const gchar *id,
                                                  guint *n_params);
GType
ges_extractable_get_real_extractable_type_for_id (GType type, const gchar * id);

gboolean ges_extractable_register_metas          (GType extractable_type, GESAsset *asset);
#endif
#endif /* __GES_INTERNAL_H__ */
