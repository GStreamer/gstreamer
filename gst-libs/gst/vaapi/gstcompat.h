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

/* GstPad */
#define GST_PAD_CHAIN_FUNCTION_ARGS \
    GstPad *pad, GstObject *parent, GstBuffer *buffer
#define GST_PAD_EVENT_FUNCTION_ARGS \
    GstPad *pad, GstObject *parent, GstEvent *event
#define GST_PAD_QUERY_FUNCTION_ARGS \
    GstPad *pad, GstObject *parent, GstQuery *query
#define GST_PAD_QUERY_FUNCTION_CALL(func, pad, parent, query) \
    (func)(pad, parent, query)

/* Misc helpers */
#define GST_MAKE_FORMAT_STRING(FORMAT) \
    "format=(string)" G_STRINGIFY(FORMAT)

/* ------------------------------------------------------------------------ */
/* --- GStreamer = 0.10                                                 --- */
/* ------------------------------------------------------------------------ */

#else

/* GstMemory */
typedef enum {
    GST_MEMORY_FLAG_READONLY = GST_MINI_OBJECT_FLAG_READONLY
} GstMemoryFlags;

typedef enum {
    GST_MAP_READ        = 1 << 0,
    GST_MAP_WRITE       = 1 << 1
} GstMapFlags;

typedef struct {
    GstMapFlags         flags;
    guint8             *data;
    gsize               size;
} GstMapInfo;

/* GstBuffer */
#undef  gst_buffer_new_wrapped
#define gst_buffer_new_wrapped(data, size) \
    gst_compat_buffer_new_wrapped_full(0, data, size, 0, size, data, g_free)
#undef  gst_buffer_new_wrapped_full
#define gst_buffer_new_wrapped_full(flags, data, maxsize, ofs, size, ud, udd) \
    gst_compat_buffer_new_wrapped_full(flags, data, maxsize, ofs, size, ud, udd)
#undef  gst_buffer_get_size
#define gst_buffer_get_size(buffer)     gst_compat_buffer_get_size(buffer)
#undef  gst_buffer_map
#define gst_buffer_map(buffer, mip, f)  gst_compat_buffer_map(buffer, mip, f)
#undef  gst_buffer_unmap
#define gst_buffer_unmap(buffer, mip)   gst_compat_buffer_unmap(buffer, mip)
#undef  gst_buffer_extract
#define gst_buffer_extract(buffer, offset, dest, size) \
    gst_compat_buffer_extract(buffer, offset, dest, size)

static inline GstBuffer *
gst_compat_buffer_new_wrapped_full(GstMemoryFlags flags, gpointer data,
    gsize maxsize, gsize offset, gsize size, gpointer user_data,
    GDestroyNotify notify)
{
    GstBuffer *buffer;

    /* XXX: unsupported */
    g_return_val_if_fail(user_data == NULL, NULL);
    g_return_val_if_fail(notify == NULL, NULL);
    g_return_val_if_fail(maxsize >= size, NULL);

    buffer = gst_buffer_new();
    if (!buffer)
        return NULL;

    GST_BUFFER_DATA(buffer) = data + offset;
    GST_BUFFER_SIZE(buffer) = size;
    return buffer;
}

static inline gsize
gst_compat_buffer_get_size(GstBuffer *buffer)
{
    return GST_BUFFER_SIZE(buffer);
}

static inline gboolean
gst_compat_buffer_map(GstBuffer *buffer, GstMapInfo *mip, GstMapFlags flags)
{
    mip->flags = flags;
    mip->data  = GST_BUFFER_DATA(buffer);
    mip->size  = GST_BUFFER_SIZE(buffer);
    return TRUE;
}

static inline void
gst_compat_buffer_unmap(GstBuffer *buffer, GstMapInfo *mip)
{
}

static inline gsize
gst_compat_buffer_extract(GstBuffer *buffer, gsize offset, gpointer dest,
    gsize size)
{
    gsize esize;

    if (!buffer || !dest || offset >= GST_BUFFER_SIZE(buffer))
        return 0;

    esize = MIN(size, GST_BUFFER_SIZE(buffer) - offset);
    memcpy(dest, GST_BUFFER_DATA(buffer) + offset, esize);
    return esize;
}

/* GstAdapter */
#include <gst/base/gstadapter.h>

#undef  gst_adapter_map
#define gst_adapter_map(adapter, size)  gst_compat_adapter_map(adapter, size)
#undef  gst_adapter_unmap
#define gst_adapter_unmap(adapter)      gst_compat_adapter_unmap(adapter)

static inline gconstpointer
gst_compat_adapter_map(GstAdapter *adapter, gsize size)
{
    return gst_adapter_peek(adapter, size);
}

static inline void
gst_compat_adapter_unmap(GstAdapter *adapter)
{
}

/* GstCaps */
#undef  gst_caps_merge
#define gst_caps_merge(caps1, caps2)    gst_compat_caps_merge(caps1, caps2)
#undef  gst_caps_merge_structure
#define gst_caps_merge_structure(caps, structure) \
    gst_compat_caps_merge_structure(caps, structure)

static inline GstCaps *
gst_compat_caps_merge(GstCaps *caps1, GstCaps *caps2)
{
    (gst_caps_merge)(caps1, caps2);
    return caps1;
}

static inline GstCaps *
gst_compat_caps_merge_structure(GstCaps *caps, GstStructure *structure)
{
    (gst_caps_merge_structure)(caps, structure);
    return caps;
}

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

typedef enum {
    GST_VIDEO_BUFFER_FLAG_TFF           = GST_VIDEO_BUFFER_TFF,
    GST_VIDEO_BUFFER_FLAG_RFF           = GST_VIDEO_BUFFER_RFF,
    GST_VIDEO_BUFFER_FLAG_ONEFIELD      = GST_VIDEO_BUFFER_ONEFIELD
} GstVideoBufferFlags;

/* GstPad */
#undef  GST_FLOW_EOS
#define GST_FLOW_EOS GST_FLOW_UNEXPECTED
#undef  GST_FLOW_FLUSHING
#define GST_FLOW_FLUSHING GST_FLOW_WRONG_STATE

#define GST_PAD_CHAIN_FUNCTION_ARGS \
    GstPad *pad, GstBuffer *buffer
#define GST_PAD_EVENT_FUNCTION_ARGS \
    GstPad *pad, GstEvent *event

static inline gboolean
gst_compat_pad_start_task(GstPad *pad, GstTaskFunction func, gpointer user_data,
    GDestroyNotify notify)
{
    g_return_val_if_fail(notify == NULL, FALSE);

    return gst_pad_start_task(pad, func, user_data);
}

#undef  gst_pad_start_task
#define gst_pad_start_task(pad, func, user_data, notify) \
    gst_compat_pad_start_task(pad, func, user_data, notify)

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

/* GstQuery */
#define GST_PAD_QUERY_FUNCTION_ARGS \
    GstPad *pad, GstQuery *query
#define GST_PAD_QUERY_FUNCTION_CALL(func, pad, parent, query) \
    (func)(pad, query)

/* Misc helpers */
#define GST_MAKE_FORMAT_STRING(FORMAT) \
    "format=(fourcc)" G_STRINGIFY(FORMAT)

#endif

#endif /* GST_COMPAT_H */
