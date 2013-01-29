/*
 *  gstvaapivalue.h - GValue implementations specific to VA-API
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GST_VAAPI_VALUE_H
#define GST_VAAPI_VALUE_H

#include <glib-object.h>
#include <gst/vaapi/gstvaapitypes.h>

G_BEGIN_DECLS

/**
 * GST_VAAPI_TYPE_ID:
 *
 * A #GValue type that represents a VA identifier.
 *
 * Return value: the #GType of GstVaapiID
 */
#define GST_VAAPI_TYPE_ID gst_vaapi_id_get_type()

/**
 * GST_VAAPI_VALUE_HOLDS_ID:
 * @x: the #GValue to check
 *
 * Checks if the given #GValue contains a #GstVaapiID value.
 */
#define GST_VAAPI_VALUE_HOLDS_ID(x) (G_VALUE_HOLDS((x), GST_VAAPI_TYPE_ID))

/**
 * GST_VAAPI_TYPE_RENDER_MODE:
 *
 * A #GstVaapiRenderMode type that represents the VA display backend
 * rendering mode: overlay (2D engine) or textured-blit (3D engine).
 *
 * Return value: the #GType of GstVaapiRenderMode
 */
#define GST_VAAPI_TYPE_RENDER_MODE gst_vaapi_render_mode_get_type()

/**
 * GST_VAAPI_TYPE_ROTATION:
 *
 * A type that represents the VA display rotation.
 *
 * Return value: the #GType of GstVaapiRotation
 */
#define GST_VAAPI_TYPE_ROTATION gst_vaapi_rotation_get_type()

GType
gst_vaapi_id_get_type(void) G_GNUC_CONST;

GstVaapiID
gst_vaapi_value_get_id(const GValue *value);

void
gst_vaapi_value_set_id(GValue *value, GstVaapiID id);

GType
gst_vaapi_render_mode_get_type(void) G_GNUC_CONST;

GType
gst_vaapi_rotation_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPI_VALUE_H */
