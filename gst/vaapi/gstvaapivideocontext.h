/*
 *  gstvaapivideocontext.h - GStreamer/VA video context
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2013 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *  Copyright (C) 2013 Igalia
 *    Author: Víctor Manuel Jáquez Leal <vjaquez@igalia.com>
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

#ifndef GST_VAAPI_VIDEO_CONTEXT_H
#define GST_VAAPI_VIDEO_CONTEXT_H

#include <gst/vaapi/gstvaapidisplay.h>

#if GST_CHECK_VERSION(1,1,0)

#define GST_VAAPI_DISPLAY_CONTEXT_TYPE_NAME "gst.vaapi.Display"

/* Fake GstVideoContext symbols */
#define GST_VIDEO_CONTEXT(obj)          (GST_ELEMENT (obj))
#define GST_IS_VIDEO_CONTEXT(obj)       (GST_IS_ELEMENT (obj))
#define GstVideoContext                 GstElement
#define gst_video_context_prepare       gst_vaapi_video_context_prepare

G_GNUC_INTERNAL
GstContext *
gst_vaapi_video_context_new_with_display (GstVaapiDisplay * display,
    gboolean persistent);

G_GNUC_INTERNAL
gboolean
gst_vaapi_video_context_get_display (GstContext * context,
    GstVaapiDisplay ** display_ptr);

G_GNUC_INTERNAL
void
gst_vaapi_video_context_prepare (GstElement * element, const gchar ** types);

G_GNUC_INTERNAL
void
gst_vaapi_video_context_propagate (GstElement * element,
    GstVaapiDisplay * display);

#else
#include <gst/video/videocontext.h>

static inline void
gst_vaapi_video_context_prepare (GstVideoContext * context,
    const gchar ** types)
{
  gst_video_context_prepare (context, types);
}

static inline void
gst_vaapi_video_context_propagate (GstVideoContext * context,
    GstVaapiDisplay * display)
{
}

#endif

#endif /* GST_VAAPI_VIDEO_CONTEXT_H */
