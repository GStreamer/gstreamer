/*
 *  gstvaapivalue.h - GValue implementations specific to VA-API
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
 * GST_VAAPI_TYPE_POINT:
 *
 * A #GstVaapiPoint type that represents a 2D point coordinates.
 *
 * Return value: the GType of #GstVaapiPoint
 */
#define GST_VAAPI_TYPE_POINT gst_vaapi_point_get_type()

/**
 * GST_VAAPI_TYPE_RECTANGLE:
 *
 * A #GstVaapiRectangle type that represents a 2D rectangle position
 * and size.
 *
 * Return value: the GType of #GstVaapiRectangle
 */
#define GST_VAAPI_TYPE_RECTANGLE gst_vaapi_rectangle_get_type()

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
gst_vaapi_point_get_type(void) G_GNUC_CONST;

GType
gst_vaapi_rectangle_get_type(void) G_GNUC_CONST;

GType
gst_vaapi_render_mode_get_type(void) G_GNUC_CONST;

GType
gst_vaapi_rotation_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPI_VALUE_H */
