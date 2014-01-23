/*
 *  gstvaapicontext_overlay.h - VA context abstraction (overlay composition)
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
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

#ifndef GST_VAAPI_CONTEXT_OVERLAY_H
#define GST_VAAPI_CONTEXT_OVERLAY_H

#include <gst/video/video-overlay-composition.h>
#include "gstvaapicontext.h"

G_BEGIN_DECLS

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_overlay_init (GstVaapiContext * context);

G_GNUC_INTERNAL
void
gst_vaapi_context_overlay_finalize (GstVaapiContext * context);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_overlay_reset (GstVaapiContext * context);

G_GNUC_INTERNAL
gboolean
gst_vaapi_context_apply_composition (GstVaapiContext * context,
    GstVideoOverlayComposition * composition);

G_END_DECLS

#endif /* GST_VAAPI_CONTEXT_OVERLAY_H */
