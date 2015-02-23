/* GStreamer Editing Services
 *
 * Copyright (C) <2015> Thibault Saunier <tsaunier@gnome.org>
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

#ifndef __GES_STRUCTURED_INTERFACE__
#define __GES_STRUCTURED_INTERFACE__

#include <ges/ges.h>

typedef gboolean (*ActionFromStructureFunc)   (GESTimeline * timeline,
                                               GstStructure * structure,
                                               GError ** error);

G_GNUC_INTERNAL gboolean
_ges_add_remove_keyframe_from_struct          (GESTimeline * timeline,
                                               GstStructure * structure,
                                               GError ** error);
G_GNUC_INTERNAL gboolean
_ges_add_clip_from_struct                     (GESTimeline * timeline,
                                               GstStructure * structure,
                                               GError ** error);

G_GNUC_INTERNAL gboolean
_ges_container_add_child_from_struct          (GESTimeline * timeline,
                                               GstStructure * structure,
                                               GError ** error);

G_GNUC_INTERNAL gboolean
_ges_set_child_property_from_struct           (GESTimeline * timeline,
                                               GstStructure * structure,
                                               GError ** error);

G_GNUC_INTERNAL GESAsset *
_ges_get_asset_from_timeline                  (GESTimeline * timeline,
                                               GType type,
                                               const gchar * id,
                                               GError **error);
G_GNUC_INTERNAL GESLayer *
_ges_get_layer_by_priority                    (GESTimeline * timeline,
                                               gint priority);

#endif /* __GES_STRUCTURED_INTERFACE__*/
