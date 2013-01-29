/*
 *  gstvaapiutils.h - VA-API utilities
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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

#include "config.h"
#include <glib.h>
#include <va/va.h>

/** Check VA status for success or print out an error */
G_GNUC_INTERNAL
gboolean
vaapi_check_status(VAStatus status, const char *msg);

/** Maps VA buffer */
G_GNUC_INTERNAL
void *
vaapi_map_buffer(VADisplay dpy, VABufferID buf_id);

/** Unmaps VA buffer */
G_GNUC_INTERNAL
void
vaapi_unmap_buffer(VADisplay dpy, VABufferID buf_id, void **pbuf);

/** Creates and maps VA buffer */
G_GNUC_INTERNAL
gboolean
vaapi_create_buffer(
    VADisplay     dpy,
    VAContextID   ctx,
    int           type,
    unsigned int  size,
    gconstpointer data,
    VABufferID   *buf_id,
    gpointer     *mapped_data
);

/** Destroy VA buffer */
G_GNUC_INTERNAL
void
vaapi_destroy_buffer(VADisplay dpy, VABufferID *buf_id);

/** Return a string representation of a VAProfile */
G_GNUC_INTERNAL
const char *string_of_VAProfile(VAProfile profile);

/** Return a string representation of a VAEntrypoint */
G_GNUC_INTERNAL
const char *string_of_VAEntrypoint(VAEntrypoint entrypoint);

/* Return a string representation of a VADisplayAttributeType */
G_GNUC_INTERNAL
const char *
string_of_VADisplayAttributeType(VADisplayAttribType attribute_type);

G_GNUC_INTERNAL
guint
from_GstVaapiSubpictureFlags(guint flags);

G_GNUC_INTERNAL
guint
to_GstVaapiSubpictureFlags(guint va_flags);

G_GNUC_INTERNAL
guint
from_GstVideoOverlayFormatFlags(guint ovl_flags);

G_GNUC_INTERNAL
guint
to_GstVideoOverlayFormatFlags(guint flags);

G_GNUC_INTERNAL
guint
from_GstVaapiSurfaceRenderFlags(guint flags);

G_GNUC_INTERNAL
guint
to_GstVaapiSurfaceStatus(guint va_flags);

G_GNUC_INTERNAL
guint
from_GstVaapiRotation(guint value);

G_GNUC_INTERNAL
guint
to_GstVaapiRotation(guint value);

#endif /* GST_VAAPI_UTILS_H */
