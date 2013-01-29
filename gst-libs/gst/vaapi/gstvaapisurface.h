/*
 *  gstvaapisurface.h - VA surface abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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

#ifndef GST_VAAPI_SURFACE_H
#define GST_VAAPI_SURFACE_H

#include <gst/vaapi/gstvaapiobject.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiimage.h>
#include <gst/vaapi/gstvaapisubpicture.h>
#include <gst/video/gstsurfacebuffer.h>
#include <gst/video/video-overlay-composition.h>

G_BEGIN_DECLS

/**
 * GST_VAAPI_SURFACE_CAPS_NAME:
 *
 * Generic caps type for VA surfaces.
 */
#define GST_VAAPI_SURFACE_CAPS_NAME GST_VIDEO_CAPS_SURFACE

/**
 * GST_VAAPI_SURFACE_CAPS:
 *
 * Generic caps for VA surfaces.
 */
#define GST_VAAPI_SURFACE_CAPS                  \
    GST_VAAPI_SURFACE_CAPS_NAME ", "            \
    "type = vaapi, "                            \
    "opengl = (boolean) { true, false }, "      \
    "width  = (int) [ 1, MAX ], "               \
    "height = (int) [ 1, MAX ], "               \
    "framerate = (fraction) [ 0, MAX ]"

/**
 * GstVaapiChromaType:
 * @GST_VAAPI_CHROMA_TYPE_YUV420: 4:2:0 chroma format
 * @GST_VAAPI_CHROMA_TYPE_YUV422: 4:2:2 chroma format
 * @GST_VAAPI_CHROMA_TYPE_YUV444: 4:4:4 chroma format
 *
 * The set of all chroma types for #GstVaapiSurface.
 */
typedef enum {
    GST_VAAPI_CHROMA_TYPE_YUV420 = 1,
    GST_VAAPI_CHROMA_TYPE_YUV422,
    GST_VAAPI_CHROMA_TYPE_YUV444
} GstVaapiChromaType;

/**
 * GstVaapiSurfaceStatus:
 * @GST_VAAPI_SURFACE_STATUS_IDLE:
 *   the surface is not being rendered or displayed
 * @GST_VAAPI_SURFACE_STATUS_RENDERING:
 *   the surface is used for rendering (decoding to the surface in progress)
 * @GST_VAAPI_SURFACE_STATUS_DISPLAYING:
 *   the surface is being displayed to screen
 * @GST_VAAPI_SURFACE_STATUS_SKIPPED:
 *   indicates a skipped frame during encode
 *
 * The set of all surface status for #GstVaapiSurface.
 */
typedef enum {
    GST_VAAPI_SURFACE_STATUS_IDLE       = 1 << 0,
    GST_VAAPI_SURFACE_STATUS_RENDERING  = 1 << 1,
    GST_VAAPI_SURFACE_STATUS_DISPLAYING = 1 << 2,
    GST_VAAPI_SURFACE_STATUS_SKIPPED    = 1 << 3
} GstVaapiSurfaceStatus;

/**
 * GstVaapiSurfaceRenderFlags
 * @GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD:
 *   selects the top field of the surface
 * @GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD:
 *   selects the bottom field of the surface
 * @GST_VAAPI_PICTURE_STRUCTURE_FRAME:
 *   selects the entire surface
 * @GST_VAAPI_COLOR_STANDARD_ITUR_BT_601:
 *   uses ITU-R BT.601 standard for color space conversion
 * @GST_VAAPI_COLOR_STANDARD_ITUR_BT_709:
 *   uses ITU-R BT.709 standard for color space conversion
 *
 * The set of all render flags for gst_vaapi_window_put_surface().
 */
typedef enum {
    GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD       = 1 << 0,
    GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD    = 1 << 1,
    GST_VAAPI_PICTURE_STRUCTURE_FRAME           =
    (
        GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD |
        GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD
    ),
    GST_VAAPI_COLOR_STANDARD_ITUR_BT_601        = 1 << 2,
    GST_VAAPI_COLOR_STANDARD_ITUR_BT_709        = 1 << 3,
} GstVaapiSurfaceRenderFlags;

#define GST_VAAPI_TYPE_SURFACE \
    (gst_vaapi_surface_get_type())

#define GST_VAAPI_SURFACE(obj)                          \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_SURFACE, \
                                GstVaapiSurface))

#define GST_VAAPI_SURFACE_CLASS(klass)                  \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_SURFACE,    \
                             GstVaapiSurfaceClass))

#define GST_VAAPI_IS_SURFACE(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_SURFACE))

#define GST_VAAPI_IS_SURFACE_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_SURFACE))

#define GST_VAAPI_SURFACE_GET_CLASS(obj)                \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_SURFACE,  \
                               GstVaapiSurfaceClass))

typedef struct _GstVaapiSurface                 GstVaapiSurface;
typedef struct _GstVaapiSurfacePrivate          GstVaapiSurfacePrivate;
typedef struct _GstVaapiSurfaceClass            GstVaapiSurfaceClass;

/**
 * GstVaapiSurface:
 *
 * A VA surface wrapper.
 */
struct _GstVaapiSurface {
    /*< private >*/
    GstVaapiObject parent_instance;

    GstVaapiSurfacePrivate *priv;
};

/**
 * GstVaapiSurfaceClass:
 *
 * A VA surface wrapper class.
 */
struct _GstVaapiSurfaceClass {
    /*< private >*/
    GstVaapiObjectClass parent_class;
};

GType
gst_vaapi_surface_get_type(void) G_GNUC_CONST;

GstVaapiSurface *
gst_vaapi_surface_new(
    GstVaapiDisplay    *display,
    GstVaapiChromaType  chroma_type,
    guint               width,
    guint               height
);

GstVaapiID
gst_vaapi_surface_get_id(GstVaapiSurface *surface);

GstVaapiChromaType
gst_vaapi_surface_get_chroma_type(GstVaapiSurface *surface);

guint
gst_vaapi_surface_get_width(GstVaapiSurface *surface);

guint
gst_vaapi_surface_get_height(GstVaapiSurface *surface);

void
gst_vaapi_surface_get_size(
    GstVaapiSurface *surface,
    guint           *pwidth,
    guint           *pheight
);

GstVaapiImage *
gst_vaapi_surface_derive_image(GstVaapiSurface *surface);

gboolean
gst_vaapi_surface_get_image(GstVaapiSurface *surface, GstVaapiImage *image);

gboolean
gst_vaapi_surface_put_image(GstVaapiSurface *surface, GstVaapiImage *image);

gboolean
gst_vaapi_surface_associate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect
);

gboolean
gst_vaapi_surface_deassociate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture
);

gboolean
gst_vaapi_surface_sync(GstVaapiSurface *surface);

gboolean
gst_vaapi_surface_query_status(
    GstVaapiSurface       *surface,
    GstVaapiSurfaceStatus *pstatus
);

gboolean
gst_vaapi_surface_set_subpictures_from_composition(
    GstVaapiSurface            *surface,
    GstVideoOverlayComposition *composition,
    gboolean                    propagate_context
);

G_END_DECLS

#endif /* GST_VAAPI_SURFACE_H */
