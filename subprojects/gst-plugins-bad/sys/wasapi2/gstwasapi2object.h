/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include "gstwasapi2util.h"

G_BEGIN_DECLS

#define GST_TYPE_WASAPI2_OBJECT (gst_wasapi2_object_get_type ())
G_DECLARE_FINAL_TYPE (GstWasapi2Object, gst_wasapi2_object,
    GST, WASAPI2_OBJECT, GstObject);

GstWasapi2Object * gst_wasapi2_object_new (GstWasapi2EndpointClass device_class,
                                           const gchar * device_id,
                                           guint target_pid);

GstCaps *          gst_wasapi2_object_get_caps (GstWasapi2Object * object);

IAudioClient *     gst_wasapi2_object_get_handle (GstWasapi2Object * object);

gboolean           gst_wasapi2_object_is_endpoint_muted (GstWasapi2Object * object);

gboolean           gst_wasapi2_object_auto_routing_supported (GstWasapi2Object * object);

G_END_DECLS

