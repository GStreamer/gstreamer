/*
 *  gstvaapiprofilecaps.h - VA config attributes as gstreamer capabilities
 *
 *  Copyright (C) 2019 Igalia, S.L.
 *    Author: Víctor Jáquez <vjaquez@igalia.com>
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

#ifndef GST_VAAPI_PROFILE_CAPS_H
#define GST_VAAPI_PROFILE_CAPS_H

#include <gst/gst.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiprofile.h>

G_BEGIN_DECLS

gboolean
gst_vaapi_profile_caps_append_decoder (GstVaapiDisplay * display,
    GstVaapiProfile profile, GstStructure * structure);

gboolean
gst_vaapi_mem_type_supports (guint va_mem_types, guint mem_type);

G_END_DECLS

#endif /* GST_VAAPI_PROFILE_CAPS_H */
