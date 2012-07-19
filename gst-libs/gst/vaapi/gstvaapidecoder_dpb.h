/*
 *  gstvaapidecoder_dpb.h - Decoded Picture Buffer
 *
 *  Copyright (C) 2012 Intel Corporation
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

#ifndef GST_VAAPI_DECODER_DPB_H
#define GST_VAAPI_DECODER_DPB_H

#include <gst/vaapi/gstvaapidecoder_objects.h>

G_BEGIN_DECLS

typedef struct _GstVaapiDpb             GstVaapiDpb;
typedef struct _GstVaapiDpbClass        GstVaapiDpbClass;
typedef struct _GstVaapiDpbMpeg2        GstVaapiDpbMpeg2;
typedef struct _GstVaapiDpbMpeg2Class   GstVaapiDpbMpeg2Class;

/* ------------------------------------------------------------------------- */
/* --- Base Decoded Picture Buffer                                       --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_DPB \
    (gst_vaapi_dpb_get_type())

#define GST_VAAPI_DPB_CAST(obj) \
    ((GstVaapiDpb *)(obj))

#define GST_VAAPI_DPB(obj)                              \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                  \
                                GST_VAAPI_TYPE_DPB,     \
                                GstVaapiDpb))

#define GST_VAAPI_DPB_CLASS(klass)                      \
    (G_TYPE_CHECK_CLASS_CAST((klass),                   \
                             GST_VAAPI_TYPE_DPB,        \
                             GstVaapiDpbClass))

#define GST_VAAPI_IS_DPB(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DPB))

#define GST_VAAPI_IS_DPB_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DPB))

#define GST_VAAPI_DPB_GET_CLASS(obj)                    \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                   \
                               GST_VAAPI_TYPE_DPB,      \
                               GstVaapiDpbClass))

/**
 * GstVaapiDpb:
 *
 * A decoded picture buffer (DPB) object.
 */
struct _GstVaapiDpb {
    /*< private >*/
    GstMiniObject       parent_instance;

    /*< protected >*/
    GstVaapiPicture   **pictures;
    guint               num_pictures;
    guint               max_pictures;
};

/**
 * GstVaapiDpbClass:
 *
 * The #GstVaapiDpb base class.
 */
struct _GstVaapiDpbClass {
    /*< private >*/
    GstMiniObjectClass  parent_class;

    /*< protected >*/
    void              (*flush)  (GstVaapiDpb *dpb);
    gboolean          (*add)    (GstVaapiDpb *dpb, GstVaapiPicture *picture);
};

G_GNUC_INTERNAL
GType
gst_vaapi_dpb_get_type(void) G_GNUC_CONST;

G_GNUC_INTERNAL
void
gst_vaapi_dpb_flush(GstVaapiDpb *dpb);

G_GNUC_INTERNAL
gboolean
gst_vaapi_dpb_add(GstVaapiDpb *dpb, GstVaapiPicture *picture);

G_GNUC_INTERNAL
guint
gst_vaapi_dpb_size(GstVaapiDpb *dpb);

static inline gpointer
gst_vaapi_dpb_ref(gpointer ptr)
{
    return gst_mini_object_ref(GST_MINI_OBJECT(ptr));
}

static inline void
gst_vaapi_dpb_unref(gpointer ptr)
{
    gst_mini_object_unref(GST_MINI_OBJECT(ptr));
}

/* ------------------------------------------------------------------------- */
/* --- MPEG-2 Decoded Picture Buffer                                     --- */
/* ------------------------------------------------------------------------- */

#define GST_VAAPI_TYPE_DPB_MPEG2 \
    (gst_vaapi_dpb_mpeg2_get_type())

#define GST_VAAPI_DPB_MPEG2_CAST(obj) \
    ((GstVaapiDpbMpeg2 *)(obj))

#define GST_VAAPI_DPB_MPEG2(obj)                                \
    (G_TYPE_CHECK_INSTANCE_CAST((obj),                          \
                                GST_VAAPI_TYPE_DPB_MPEG2,       \
                                GstVaapiDpbMpeg2))

#define GST_VAAPI_DPB_MPEG2_CLASS(klass)                        \
    (G_TYPE_CHECK_CLASS_CAST((klass),                           \
                             GST_VAAPI_TYPE_DPB_MPEG2,          \
                             GstVaapiDpbMpeg2Class))

#define GST_VAAPI_IS_DPB_MPEG2(obj) \
    (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_VAAPI_TYPE_DPB_MPEG2))

#define GST_VAAPI_IS_DPB_MPEG2_CLASS(klass) \
    (G_TYPE_CHECK_CLASS_TYPE((klass), GST_VAAPI_TYPE_DPB_MPEG2))

#define GST_VAAPI_DPB_MPEG2_GET_CLASS(obj)                      \
    (G_TYPE_INSTANCE_GET_CLASS((obj),                           \
                               GST_VAAPI_TYPE_DPB_MPEG2,        \
                               GstVaapiDpbMpeg2Class))

/**
 * GstVaapiDpbMpeg2:
 *
 * A decoded picture buffer (DPB_MPEG2) object.
 */
struct _GstVaapiDpbMpeg2 {
    /*< private >*/
    GstVaapiDpb         parent_instance;
};

/**
 * GstVaapiDpbMpeg2Class:
 *
 * The #GstVaapiDpbMpeg2 base class.
 */
struct _GstVaapiDpbMpeg2Class {
    /*< private >*/
    GstVaapiDpbClass    parent_class;
};

G_GNUC_INTERNAL
GType
gst_vaapi_dpb_mpeg2_get_type(void) G_GNUC_CONST;

G_GNUC_INTERNAL
GstVaapiDpb *
gst_vaapi_dpb_mpeg2_new(void);

G_GNUC_INTERNAL
void
gst_vaapi_dpb_mpeg2_get_references(
    GstVaapiDpb        *dpb,
    GstVaapiPicture    *picture,
    GstVaapiPicture   **prev_picture_ptr,
    GstVaapiPicture   **next_picture_ptr
);

G_END_DECLS

#endif /* GST_VAAPI_DECODER_DPB */
