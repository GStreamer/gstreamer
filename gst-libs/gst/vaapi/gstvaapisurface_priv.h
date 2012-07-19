/*
 *  gstvaapisurface_priv.h - VA surface abstraction (private data)
 *
 *  Copyright (C) 2011 Intel Corporation
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

#include <gst/vaapi/gstvaapicontext.h>
#include <gst/vaapi/gstvaapisurface.h>

G_GNUC_INTERNAL
void
gst_vaapi_surface_set_parent_context(
    GstVaapiSurface *surface,
    GstVaapiContext *context
);

G_GNUC_INTERNAL
GstVaapiContext *
gst_vaapi_surface_get_parent_context(GstVaapiSurface *surface);

#endif /* GST_VAAPI_SURFACE_PRIV_H */
