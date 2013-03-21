/*
 *  gstcompat.h - Compatibility glue for GStreamer
 *
 *  Copyright (C) 2013 Intel Corporation
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

#ifndef GST_COMPAT_H
#define GST_COMPAT_H

#include <gst/gst.h>

/* ------------------------------------------------------------------------ */
/* --- GStreamer >= 1.0                                                 --- */
/* ------------------------------------------------------------------------ */

#if GST_CHECK_VERSION(1,0,0)
#include <gst/video/gstvideometa.h>

/* GstStructure */
#undef  gst_structure_get_fourcc
#define gst_structure_get_fourcc(structure, fieldname, value) \
    gst_compat_structure_get_fourcc(structure, fieldname, value)

static inline gboolean
gst_compat_structure_get_fourcc(const GstStructure *structure,
    const gchar *fieldname, guint32 *value)
{
    const gchar *s = gst_structure_get_string(structure, fieldname);

    if (!s || strlen(s) != 4)
        return FALSE;

    *value = GST_MAKE_FOURCC(s[0], s[1], s[2], s[3]);
    return TRUE;
}

/* GstTypeFind */
#undef  GstTypeFindPeekFunction
#define GstTypeFindPeekFunction         GstCompatTypeFindPeekFunction
#undef  GstTypeFindSuggestFunction
#define GstTypeFindSuggestFunction      GstCompatTypeFindSuggestFunction

typedef const guint8 *(*GstCompatTypeFindPeekFunction)(gpointer, gint64, guint);
typedef void (*GstCompatTypeFindSuggestFunction)(gpointer, guint, GstCaps *);

/* ------------------------------------------------------------------------ */
/* --- GStreamer = 0.10                                                 --- */
/* ------------------------------------------------------------------------ */

#else

/* GstVideoOverlayComposition */
#include <gst/video/video-overlay-composition.h>

#undef  gst_video_overlay_rectangle_get_pixels_unscaled_raw
#define gst_video_overlay_rectangle_get_pixels_unscaled_raw(rect, flags) \
    gst_compat_video_overlay_rectangle_get_pixels_unscaled_raw(rect, flags)

#ifndef HAVE_GST_VIDEO_OVERLAY_HWCAPS
#define gst_video_overlay_rectangle_get_flags(rect) (0)
#define gst_video_overlay_rectangle_get_global_alpha(rect) (1.0f)
#endif

static inline GstBuffer *
gst_compat_video_overlay_rectangle_get_pixels_unscaled_raw(
    GstVideoOverlayRectangle *rect, GstVideoOverlayFormatFlags flags)
{
    guint width, height, stride;

    /* Try to retrieve the original buffer that was passed to
       gst_video_overlay_rectangle_new_argb(). This will only work if
       there was no previous user that required pixels with non native
       alpha type */
    return gst_video_overlay_rectangle_get_pixels_unscaled_argb(rect,
        &width, &height, &stride, flags);
}

/* GstElement */
#undef  gst_element_class_set_static_metadata
#define gst_element_class_set_static_metadata(klass, name, path, desc, author) \
    gst_compat_element_class_set_static_metadata(klass, name, path, desc, author)

static inline void
gst_compat_element_class_set_static_metadata(GstElementClass *klass,
    const gchar *name, const char *path, const gchar *desc, const gchar *author)
{
    gst_element_class_set_details_simple(klass, name, path, desc, author);
}

/* GstTypeFind */
#undef  GstTypeFindPeekFunction
#define GstTypeFindPeekFunction         GstCompatTypeFindPeekFunction
#undef  GstTypeFindSuggestFunction
#define GstTypeFindSuggestFunction      GstCompatTypeFindSuggestFunction

typedef guint8 *(*GstCompatTypeFindPeekFunction)(gpointer, gint64, guint);
typedef void (*GstCompatTypeFindSuggestFunction)(gpointer, guint, const GstCaps *);

#endif

#endif /* GST_COMPAT_H */
