/*
 *  gstvaapipostproc.h - VA-API video post processing
 *
 *  Copyright (C) 2012 Intel Corporation
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this program; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
*/

#ifndef GST_VAAPIPOSTPROC_H
#define GST_VAAPIPOSTPROC_H

#include <gst/gst.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapisurface.h>
#include <gst/vaapi/gstvaapisurfacepool.h>

G_BEGIN_DECLS

#define GST_TYPE_VAAPIPOSTPROC \
    (gst_vaapipostproc_get_type())

#define GST_VAAPIPOSTPROC(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_TYPE_VAAPIPOSTPROC, \
                                GstVaapiPostproc))

#define GST_VAAPIPOSTPROC_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_TYPE_VAAPIPOSTPROC,    \
                             GstVaapiPostprocClass))

#define GST_IS_VAAPIPOSTPROC(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_VAAPIPOSTPROC))

#define GST_IS_VAAPIPOSTPROC_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_VAAPIPOSTPROC))

#define GST_VAAPIPOSTPROC_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_TYPE_VAAPIPOSTPROC,  \
                               GstVaapiPostprocClass))

typedef struct _GstVaapiPostproc                GstVaapiPostproc;
typedef struct _GstVaapiPostprocClass           GstVaapiPostprocClass;

/**
 * GstVaapiDeinterlaceMode:
 * @GST_VAAPI_DEINTERLACE_MODE_AUTO: Auto detect needs for deinterlacing.
 * @GST_VAAPI_DEINTERLACE_MODE_INTERLACED: Force deinterlacing.
 * @GST_VAAPI_DEINTERLACE_MODE_DISABLED: Never perform deinterlacing.
 */
typedef enum {
    GST_VAAPI_DEINTERLACE_MODE_AUTO = 0,
    GST_VAAPI_DEINTERLACE_MODE_INTERLACED,
    GST_VAAPI_DEINTERLACE_MODE_DISABLED,
} GstVaapiDeinterlaceMode;

/**
 * GstVaapiDeinterlaceMethod:
 * @GST_VAAPI_DEINTERLACE_METHOD_BOB: Basic bob deinterlacing algorithm.
 * @GST_VAAPI_DEINTERLACE_METHOD_WEAVE: Weave deinterlacing algorithm.
 * @GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE: Motion adaptive deinterlacing algorithm.
 * @GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED: Motion compensated deinterlacing algorithm.
 */
typedef enum {
    GST_VAAPI_DEINTERLACE_METHOD_BOB = 1,
    GST_VAAPI_DEINTERLACE_METHOD_WEAVE,
    GST_VAAPI_DEINTERLACE_METHOD_MOTION_ADAPTIVE,
    GST_VAAPI_DEINTERLACE_METHOD_MOTION_COMPENSATED,
} GstVaapiDeinterlaceMethod;

struct _GstVaapiPostproc {
    /*< private >*/
    GstElement                  parent_instance;

    GstPad                     *sinkpad;
    GstCaps                    *sinkpad_caps;
    GstPad                     *srcpad;
    GstCaps                    *srcpad_caps;
    GstCaps                    *allowed_caps;
    GstCaps                    *postproc_caps;

    GstVaapiDisplay            *display;
    guint                       surface_width;
    guint                       surface_height;

    /* Deinterlacing */
    gboolean                    deinterlace;
    GstVaapiDeinterlaceMode     deinterlace_mode;
    GstVaapiDeinterlaceMethod   deinterlace_method;
    GstClockTime                field_duration;
    gint                        fps_n;
    gint                        fps_d;
};

struct _GstVaapiPostprocClass {
    /*< private >*/
    GstElementClass             parent_class;
};

GType
gst_vaapipostproc_get_type(void) G_GNUC_CONST;

G_END_DECLS

#endif /* GST_VAAPIPOSTPROC_H */
