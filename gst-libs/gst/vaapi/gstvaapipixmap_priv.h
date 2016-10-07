/*
 *  gstvaapipixmap_priv.h - Pixmap abstraction (private definitions)
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef GST_VAAPI_PIXMAP_PRIV_H
#define GST_VAAPI_PIXMAP_PRIV_H

#include "gstvaapiobject_priv.h"

G_BEGIN_DECLS

#define GST_VAAPI_PIXMAP_CLASS(klass) \
    ((GstVaapiPixmapClass *)(klass))

#define GST_VAAPI_PIXMAP_GET_CLASS(obj) \
    GST_VAAPI_PIXMAP_CLASS(GST_VAAPI_OBJECT_GET_CLASS(obj))

/**
 * GST_VAAPI_PIXMAP_FORMAT:
 * @pixmap: a #GstVaapiPixmap
 *
 * Macro that evaluates to the format in pixels of the @pixmap.
 */
#undef  GST_VAAPI_PIXMAP_FORMAT
#define GST_VAAPI_PIXMAP_FORMAT(pixmap) \
    (GST_VAAPI_PIXMAP(pixmap)->format)

/**
 * GST_VAAPI_PIXMAP_WIDTH:
 * @pixmap: a #GstVaapiPixmap
 *
 * Macro that evaluates to the width in pixels of the @pixmap.
 */
#undef  GST_VAAPI_PIXMAP_WIDTH
#define GST_VAAPI_PIXMAP_WIDTH(pixmap) \
    (GST_VAAPI_PIXMAP(pixmap)->width)

/**
 * GST_VAAPI_PIXMAP_HEIGHT:
 * @pixmap: a #GstVaapiPixmap
 *
 * Macro that evaluates to the height in pixels of the @pixmap.
 */
#undef  GST_VAAPI_PIXMAP_HEIGHT
#define GST_VAAPI_PIXMAP_HEIGHT(pixmap) \
    (GST_VAAPI_PIXMAP(pixmap)->height)

/* GstVaapiPixmapClass hooks */
typedef gboolean  (*GstVaapiPixmapCreateFunc)  (GstVaapiPixmap *pixmap);
typedef gboolean  (*GstVaapiPixmapRenderFunc)  (GstVaapiPixmap *pixmap,
    GstVaapiSurface *surface, const GstVaapiRectangle *crop_rect, guint flags);

/**
 * GstVaapiPixmap:
 *
 * Base class for system-dependent pixmaps.
 */
struct _GstVaapiPixmap {
    /*< private >*/
    GstVaapiObject parent_instance;

    /*< protected >*/
    GstVideoFormat      format;
    guint               width;
    guint               height;
    guint               use_foreign_pixmap      : 1;
};

/**
 * GstVaapiPixmapClass:
 * @create: virtual function to create a pixmap with width and height
 * @render: virtual function to render a #GstVaapiSurface into a pixmap
 *
 * Base class for system-dependent pixmaps.
 */
struct _GstVaapiPixmapClass {
    /*< private >*/
    GstVaapiObjectClass parent_class;

    /*< protected >*/
    GstVaapiPixmapCreateFunc    create;
    GstVaapiPixmapRenderFunc    render;
};

GstVaapiPixmap *
gst_vaapi_pixmap_new(const GstVaapiPixmapClass *pixmap_class,
    GstVaapiDisplay *display, GstVideoFormat format, guint width, guint height);

GstVaapiPixmap *
gst_vaapi_pixmap_new_from_native(const GstVaapiPixmapClass *pixmap_class,
    GstVaapiDisplay *display, gpointer native_pixmap);

/* Inline reference counting for core libgstvaapi library */
#ifdef IN_LIBGSTVAAPI_CORE
#define gst_vaapi_pixmap_ref_internal(pixmap) \
    ((gpointer)gst_vaapi_object_ref(GST_VAAPI_OBJECT(pixmap)))

#define gst_vaapi_pixmap_unref_internal(pixmap) \
    gst_vaapi_object_unref(GST_VAAPI_OBJECT(pixmap))

#define gst_vaapi_pixmap_replace_internal(old_pixmap_ptr, new_pixmap) \
    gst_vaapi_object_replace((GstVaapiObject **)(old_pixmap_ptr), \
        GST_VAAPI_OBJECT(new_pixmap))

#undef  gst_vaapi_pixmap_ref
#define gst_vaapi_pixmap_ref(pixmap) \
    gst_vaapi_pixmap_ref_internal((pixmap))

#undef  gst_vaapi_pixmap_unref
#define gst_vaapi_pixmap_unref(pixmap) \
    gst_vaapi_pixmap_unref_internal((pixmap))

#undef  gst_vaapi_pixmap_replace
#define gst_vaapi_pixmap_replace(old_pixmap_ptr, new_pixmap) \
    gst_vaapi_pixmap_replace_internal((old_pixmap_ptr), (new_pixmap))
#endif

G_END_DECLS

#endif /* GST_VAAPI_PIXMAP_PRIV_H */
