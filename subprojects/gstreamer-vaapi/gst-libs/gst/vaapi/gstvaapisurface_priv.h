/*
 *  gstvaapisurface_priv.h - VA surface abstraction (private data)
 *
 *  Copyright (C) 2011-2013 Intel Corporation
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

#ifndef GST_VAAPI_SURFACE_PRIV_H
#define GST_VAAPI_SURFACE_PRIV_H

#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

/**
 * GstVaapiSurface:
 *
 * A VA surface wrapper.
 */
struct _GstVaapiSurface
{
  /*< private >*/
  GstMiniObject mini_object;
  GstVaapiDisplay *display;
  GstVaapiID object_id;

  GstVaapiBufferProxy *extbuf_proxy;
  GstVideoFormat format;
  guint width;
  guint height;
  GstVaapiChromaType chroma_type;
  GPtrArray *subpictures;
};

/**
 * GST_VAAPI_SURFACE_DISPLAY:
 * @surface: a #GstVaapiSurface
 *
 * Macro that evaluates to the @surface's display.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef GST_VAAPI_SURFACE_DISPLAY
#define GST_VAAPI_SURFACE_DISPLAY(surface) \
  (GST_VAAPI_SURFACE (surface)->display)

/**
 * GST_VAAPI_SURFACE_ID:
 * @surface: a #GstVaapiSurface
 *
 * Macro that evaluates to the @surface's ID.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef GST_VAAPI_SURFACE_ID
#define GST_VAAPI_SURFACE_ID(surface) \
  (GST_VAAPI_SURFACE (surface)->object_id)

/**
 * GST_VAAPI_SURFACE_CHROMA_TYPE:
 * @surface: a #GstVaapiSurface
 *
 * Macro that evaluates to the @surface chroma type.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_CHROMA_TYPE
#define GST_VAAPI_SURFACE_CHROMA_TYPE(surface) \
  (GST_VAAPI_SURFACE (surface)->chroma_type)

/**
 * GST_VAAPI_SURFACE_FORMAT:
 * @surface: a #GstVaapiSurface
 *
 * Macro that evaluates to the @surface format.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_FORMAT
#define GST_VAAPI_SURFACE_FORMAT(surface) \
  (GST_VAAPI_SURFACE (surface)->format)

/**
 * GST_VAAPI_SURFACE_WIDTH:
 * @surface: a #GstVaapiSurface
 *
 * Macro that evaluates to the @surface width in pixels.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_WIDTH
#define GST_VAAPI_SURFACE_WIDTH(surface) \
  (GST_VAAPI_SURFACE (surface)->width)

/**
 * GST_VAAPI_SURFACE_HEIGHT:
 * @surface: a #GstVaapiSurface
 *
 * Macro that evaluates to the @surface height in pixels.
 *
 * This is an internal macro that does not do any run-time type check.
 */
#undef  GST_VAAPI_SURFACE_HEIGHT
#define GST_VAAPI_SURFACE_HEIGHT(surface) \
  (GST_VAAPI_SURFACE (surface)->height)

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_PRIV_H */
