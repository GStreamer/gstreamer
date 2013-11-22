/*
 *  gstvaapicontext.h - VA context abstraction
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

#ifndef GST_VAAPI_CONTEXT_H
#define GST_VAAPI_CONTEXT_H

#include <gst/vaapi/gstvaapiobject.h>
#include <gst/vaapi/gstvaapiprofile.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/video/video-overlay-composition.h>

G_BEGIN_DECLS

#define GST_VAAPI_CONTEXT(obj) \
    ((GstVaapiContext *)(obj))

typedef struct _GstVaapiContext                 GstVaapiContext;
typedef struct _GstVaapiContextInfo             GstVaapiContextInfo;

/**
 * GstVaapiContextInfo:
 *
 * Structure holding VA context info like encoded size, decoder
 * profile and entry-point to use, and maximum number of reference
 * frames reported by the bitstream.
 */
struct _GstVaapiContextInfo {
    GstVaapiProfile     profile;
    GstVaapiEntrypoint  entrypoint;
    guint               width;
    guint               height;
    guint               ref_frames;
};

G_GNUC_INTERNAL
GstVaapiContext *
gst_vaapi_context_new(
    GstVaapiDisplay    *display,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height
);

G_GNUC_INTERNAL
GstVaapiContext *
gst_vaapi_context_new_full(GstVaapiDisplay *display,
    const GstVaapiContextInfo *cip);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_reset(
    GstVaapiContext    *context,
    GstVaapiProfile     profile,
    GstVaapiEntrypoint  entrypoint,
    guint               width,
    guint               height
);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_reset_full(GstVaapiContext *context,
    const GstVaapiContextInfo *new_cip);

G_GNUC_INTERNAL
GstVaapiID
gst_vaapi_context_get_id(GstVaapiContext *context);

G_GNUC_INTERNAL
GstVaapiProfile
gst_vaapi_context_get_profile(GstVaapiContext *context);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_set_profile(GstVaapiContext *context, GstVaapiProfile profile);

G_GNUC_INTERNAL
GstVaapiEntrypoint
gst_vaapi_context_get_entrypoint(GstVaapiContext *context);

G_GNUC_INTERNAL
void
gst_vaapi_context_get_size(
    GstVaapiContext *context,
    guint           *pwidth,
    guint           *pheight
);

G_GNUC_INTERNAL
GstVaapiSurfaceProxy *
gst_vaapi_context_get_surface_proxy(GstVaapiContext *context);

G_GNUC_INTERNAL
guint
gst_vaapi_context_get_surface_count(GstVaapiContext *context);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_apply_composition(
    GstVaapiContext            *context,
    GstVideoOverlayComposition *composition
);

G_END_DECLS

#endif /* GST_VAAPI_CONTEXT_H */
