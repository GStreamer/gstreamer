/* GStreamer Editing Services
 * Copyright (C) 2010 Edward Hervey <edward.hervey@collabora.co.uk>
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

G_BEGIN_DECLS

GES_API
GESTimeline * ges_timeline_new_audio_video (void) G_GNUC_WARN_UNUSED_RESULT;
GES_API
gboolean ges_pspec_equal (gconstpointer key_spec_1, gconstpointer key_spec_2);
GES_API
guint ges_pspec_hash (gconstpointer key_spec);


G_END_DECLS
