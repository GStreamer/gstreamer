/* GStreamer Editing Services

 * Copyright (C) <2019> Mathieu Duponchelle <mathieu@centricular.com>
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

G_BEGIN_DECLS

#define GES_TYPE_MARKER ges_marker_get_type ()

/**
 * GESMarker:
 *
 * A timed #GESMetaContainer object.
 *
 * Since: 1.18
 */
GES_API
G_DECLARE_FINAL_TYPE (GESMarker, ges_marker, GES, MARKER, GObject)
#define GES_TYPE_MARKER_LIST ges_marker_list_get_type ()

/**
 * GESMarkerList:
 *
 * A list of #GESMarker
 *
 * Since: 1.18
 */
GES_API
G_DECLARE_FINAL_TYPE (GESMarkerList, ges_marker_list, GES, MARKER_LIST, GObject)

GES_API
GESMarkerList * ges_marker_list_new (void) G_GNUC_WARN_UNUSED_RESULT;

GES_API
GESMarker * ges_marker_list_add (GESMarkerList * list, GstClockTime position);

GES_API
gboolean ges_marker_list_remove (GESMarkerList * list, GESMarker *marker);

GES_API
guint ges_marker_list_size (GESMarkerList * list);


GES_API
GList * ges_marker_list_get_markers (GESMarkerList *list) G_GNUC_WARN_UNUSED_RESULT;

GES_API
gboolean ges_marker_list_move (GESMarkerList *list, GESMarker *marker, GstClockTime position);

G_END_DECLS
