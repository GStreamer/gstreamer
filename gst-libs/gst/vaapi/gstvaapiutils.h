/*
 *  gstvaapiutils.h - VA-API utilities
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
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

#ifndef GST_VAAPI_UTILS_H
#define GST_VAAPI_UTILS_H

#include <glib.h>
#include <gst/video/video.h>
#include <va/va.h>

/** calls vaInitialize() redirecting the logging mechanism */
G_GNUC_INTERNAL
gboolean
vaapi_initialize (VADisplay dpy);

/** Check VA status for success or print out an error */
G_GNUC_INTERNAL
gboolean
vaapi_check_status (VAStatus status, const gchar *msg);

/** Maps VA buffer */
G_GNUC_INTERNAL
gpointer
vaapi_map_buffer (VADisplay dpy, VABufferID buf_id);

/** Unmaps VA buffer */
G_GNUC_INTERNAL
void
vaapi_unmap_buffer (VADisplay dpy, VABufferID buf_id, void **pbuf);

/** Creates and maps VA buffer */
G_GNUC_INTERNAL
gboolean
vaapi_create_buffer (VADisplay dpy, VAContextID ctx, int type, guint size,
    gconstpointer data, VABufferID * buf_id, gpointer * mapped_data);

G_GNUC_INTERNAL
gboolean
vaapi_create_n_elements_buffer (VADisplay dpy, VAContextID ctx, int type,
    guint size, gconstpointer data, VABufferID * buf_id, gpointer * mapped_data,
    int num_elements);

/** Destroy VA buffer */
G_GNUC_INTERNAL
void
vaapi_destroy_buffer (VADisplay dpy, VABufferID * buf_id);

/** Return a string representation of a VAProfile */
G_GNUC_INTERNAL
const gchar *
string_of_VAProfile (VAProfile profile);

/** Return a string representation of a VAEntrypoint */
G_GNUC_INTERNAL
const gchar *
string_of_VAEntrypoint (VAEntrypoint entrypoint);

/* Return a string representation of a VADisplayAttributeType */
G_GNUC_INTERNAL
const gchar *
string_of_VADisplayAttributeType (VADisplayAttribType attribute_type);

/* Return a string representation of a VA chroma format */
G_GNUC_INTERNAL
const gchar *
string_of_va_chroma_format (guint chroma_format);

G_GNUC_INTERNAL
const gchar *
string_of_VARateControl (guint rate_control);

G_GNUC_INTERNAL
guint
to_GstVaapiChromaType (guint va_rt_format);

G_GNUC_INTERNAL
guint
from_GstVaapiChromaType (guint chroma_type);

G_GNUC_INTERNAL
guint
from_GstVaapiSubpictureFlags (guint flags);

G_GNUC_INTERNAL
guint
to_GstVaapiSubpictureFlags (guint va_flags);

G_GNUC_INTERNAL
guint
from_GstVideoOverlayFormatFlags (guint ovl_flags);

G_GNUC_INTERNAL
guint
to_GstVideoOverlayFormatFlags (guint flags);

G_GNUC_INTERNAL
guint
from_GstVaapiSurfaceRenderFlags (guint flags);

G_GNUC_INTERNAL
guint
to_GstVaapiSurfaceStatus (guint va_flags);

G_GNUC_INTERNAL
guint
from_GstVaapiRotation (guint value);

G_GNUC_INTERNAL
guint
to_GstVaapiRotation (guint value);

G_GNUC_INTERNAL
guint
from_GstVaapiRateControl (guint value);

G_GNUC_INTERNAL
guint
to_GstVaapiRateControl (guint value);

G_GNUC_INTERNAL
guint
from_GstVaapiDeinterlaceMethod (guint value);

G_GNUC_INTERNAL
guint
from_GstVaapiDeinterlaceFlags (guint flags);

G_GNUC_INTERNAL
guint
from_GstVaapiScaleMethod (guint value);

G_GNUC_INTERNAL
guint
to_GstVaapiScaleMethod (guint flags);

G_GNUC_INTERNAL
void
from_GstVideoOrientationMethod (guint value, guint * va_mirror,
    guint * va_rotation);

G_GNUC_INTERNAL
guint
from_GstVaapiBufferMemoryType (guint type);

G_GNUC_INTERNAL
guint
to_GstVaapiBufferMemoryType (guint va_type);

G_GNUC_INTERNAL
guint
from_GstVideoColorimetry (const GstVideoColorimetry *const colorimetry);

G_GNUC_INTERNAL
guint
from_GstVideoColorRange (const GstVideoColorRange value);

#endif /* GST_VAAPI_UTILS_H */
