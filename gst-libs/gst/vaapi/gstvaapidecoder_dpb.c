/*
 *  gstvaapidecoder_dpb.c - Decoded Picture Buffer
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

#include "sysdeps.h"
#include "gstvaapidecoder_dpb.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/* ------------------------------------------------------------------------- */
/* --- Common Decoded Picture Buffer utilities                           --- */
/* ------------------------------------------------------------------------- */

static GstVaapiDpb *
dpb_new(GType type, guint max_pictures)
{
    GstMiniObject *obj;
    GstVaapiDpb *dpb;

    g_return_val_if_fail(max_pictures > 0, NULL);

    obj = gst_mini_object_new(type);
    if (!obj)
        return NULL;

    dpb = GST_VAAPI_DPB_CAST(obj);
    dpb->pictures = g_new0(GstVaapiPicture *, max_pictures);
    if (!dpb->pictures)
        goto error;
    dpb->max_pictures = max_pictures;
    return dpb;

error:
    gst_mini_object_unref(obj);
    return NULL;
}

static gint
dpb_get_oldest(GstVaapiDpb *dpb, gboolean output)
{
    gint i, lowest_pts_index;

    for (i = 0; i < dpb->num_pictures; i++) {
        if ((GST_VAAPI_PICTURE_IS_OUTPUT(dpb->pictures[i]) ^ output) == 0)
            break;
    }
    if (i == dpb->num_pictures)
        return -1;

    lowest_pts_index = i++;
    for (; i < dpb->num_pictures; i++) {
        GstVaapiPicture * const picture = dpb->pictures[i];
        if ((GST_VAAPI_PICTURE_IS_OUTPUT(picture) ^ output) != 0)
            continue;
        if (picture->poc < dpb->pictures[lowest_pts_index]->poc)
            lowest_pts_index = i;
    }
    return lowest_pts_index;
}

static void
dpb_remove_index(GstVaapiDpb *dpb, guint index)
{
    GstVaapiPicture ** const pictures = dpb->pictures;
    guint num_pictures = --dpb->num_pictures;

    if (index != num_pictures)
        gst_vaapi_picture_replace(&pictures[index], pictures[num_pictures]);
    gst_vaapi_picture_replace(&pictures[num_pictures], NULL);
}

static inline gboolean
dpb_output(GstVaapiDpb *dpb, GstVaapiPicture *picture)
{
    return gst_vaapi_picture_output(picture);
}

static gboolean
dpb_bump(GstVaapiDpb *dpb)
{
    gint index;
    gboolean success;

    index = dpb_get_oldest(dpb, FALSE);
    if (index < 0)
        return FALSE;

    success = dpb_output(dpb, dpb->pictures[index]);
    if (!GST_VAAPI_PICTURE_IS_REFERENCE(dpb->pictures[index]))
        dpb_remove_index(dpb, index);
    return success;
}

static void
dpb_clear(GstVaapiDpb *dpb)
{
    guint i;

    for (i = 0; i < dpb->num_pictures; i++)
        gst_vaapi_picture_replace(&dpb->pictures[i], NULL);
    dpb->num_pictures = 0;
}

/* ------------------------------------------------------------------------- */
/* --- Base Decoded Picture Buffer                                       --- */
/* ------------------------------------------------------------------------- */

G_DEFINE_TYPE(GstVaapiDpb, gst_vaapi_dpb, GST_TYPE_MINI_OBJECT)

static void
gst_vaapi_dpb_base_flush(GstVaapiDpb *dpb)
{
    while (dpb_bump(dpb))
        ;
    dpb_clear(dpb);
}

static gboolean
gst_vaapi_dpb_base_add(GstVaapiDpb *dpb, GstVaapiPicture *picture)
{
    guint i;

    // Remove all unused pictures
    i = 0;
    while (i < dpb->num_pictures) {
        GstVaapiPicture * const picture = dpb->pictures[i];
        if (GST_VAAPI_PICTURE_IS_OUTPUT(picture) &&
            !GST_VAAPI_PICTURE_IS_REFERENCE(picture))
            dpb_remove_index(dpb, i);
        else
            i++;
    }

    // Store reference decoded picture into the DPB
    if (GST_VAAPI_PICTURE_IS_REFERENCE(picture)) {
        while (dpb->num_pictures == dpb->max_pictures) {
            if (!dpb_bump(dpb))
                return FALSE;
        }
    }

    // Store non-reference decoded picture into the DPB
    else {
        if (GST_VAAPI_PICTURE_IS_SKIPPED(picture))
            return TRUE;
        while (dpb->num_pictures == dpb->max_pictures) {
            for (i = 0; i < dpb->num_pictures; i++) {
                if (!GST_VAAPI_PICTURE_IS_OUTPUT(picture) &&
                    dpb->pictures[i]->poc < picture->poc)
                    break;
            }
            if (i == dpb->num_pictures)
                return dpb_output(dpb, picture);
            if (!dpb_bump(dpb))
                return FALSE;
        }
    }
    gst_vaapi_picture_replace(&dpb->pictures[dpb->num_pictures++], picture);
    return TRUE;
}

static void
gst_vaapi_dpb_finalize(GstMiniObject *object)
{
    GstVaapiDpb * const dpb = GST_VAAPI_DPB_CAST(object);
    GstMiniObjectClass *parent_class;

    if (dpb->pictures) {
        dpb_clear(dpb);
        g_free(dpb->pictures);
    }

    parent_class = GST_MINI_OBJECT_CLASS(gst_vaapi_dpb_parent_class);
    if (parent_class->finalize)
        parent_class->finalize(object);
}

static void
gst_vaapi_dpb_init(GstVaapiDpb *dpb)
{
    dpb->pictures     = NULL;
    dpb->num_pictures = 0;
    dpb->max_pictures = 0;
}

static void
gst_vaapi_dpb_class_init(GstVaapiDpbClass *klass)
{
    GstMiniObjectClass * const object_class = GST_MINI_OBJECT_CLASS(klass);

    object_class->finalize = gst_vaapi_dpb_finalize;
    klass->flush           = gst_vaapi_dpb_base_flush;
    klass->add             = gst_vaapi_dpb_base_add;
}

void
gst_vaapi_dpb_flush(GstVaapiDpb *dpb)
{
    GstVaapiDpbClass *klass;

    g_return_if_fail(GST_VAAPI_IS_DPB(dpb));

    klass = GST_VAAPI_DPB_GET_CLASS(dpb);
    if (G_UNLIKELY(!klass || !klass->add))
        return;
    klass->flush(dpb);
}

gboolean
gst_vaapi_dpb_add(GstVaapiDpb *dpb, GstVaapiPicture *picture)
{
    GstVaapiDpbClass *klass;

    g_return_val_if_fail(GST_VAAPI_IS_DPB(dpb), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_PICTURE(picture), FALSE);

    klass = GST_VAAPI_DPB_GET_CLASS(dpb);
    if (G_UNLIKELY(!klass || !klass->add))
        return FALSE;
    return klass->add(dpb, picture);
}

guint
gst_vaapi_dpb_size(GstVaapiDpb *dpb)
{
    g_return_val_if_fail(GST_VAAPI_IS_DPB(dpb), 0);

    return dpb->num_pictures;
}

/* ------------------------------------------------------------------------- */
/* --- MPEG-2 Decoded Picture Buffer                                     --- */
/* ------------------------------------------------------------------------- */

/* At most two reference pictures for MPEG-2 */
#define MAX_MPEG2_REFERENCES 2

G_DEFINE_TYPE(GstVaapiDpbMpeg2, gst_vaapi_dpb_mpeg2, GST_VAAPI_TYPE_DPB)

static gboolean
gst_vaapi_dpb_mpeg2_add(GstVaapiDpb *dpb, GstVaapiPicture *picture)
{
    GstVaapiPicture *ref_picture;
    gint index = -1;

    g_return_val_if_fail(GST_VAAPI_IS_DPB_MPEG2(dpb), FALSE);

    /*
     * Purpose: only store reference decoded pictures into the DPB
     *
     * This means:
     * - non-reference decoded pictures are output immediately
     * - ... thus causing older reference pictures to be output, if not already
     * - the oldest reference picture is replaced with the new reference picture
     */
    if (G_LIKELY(dpb->num_pictures == MAX_MPEG2_REFERENCES)) {
        index = (dpb->pictures[0]->poc > dpb->pictures[1]->poc);
        ref_picture = dpb->pictures[index];
        if (!GST_VAAPI_PICTURE_IS_OUTPUT(ref_picture)) {
            if (!dpb_output(dpb, ref_picture))
                return FALSE;
        }
    }

    if (!GST_VAAPI_PICTURE_IS_REFERENCE(picture))
        return dpb_output(dpb, picture);

    if (index < 0)
        index = dpb->num_pictures++;
    gst_vaapi_picture_replace(&dpb->pictures[index], picture);
    return TRUE;
}

static void
gst_vaapi_dpb_mpeg2_init(GstVaapiDpbMpeg2 *dpb)
{
}

static void
gst_vaapi_dpb_mpeg2_class_init(GstVaapiDpbMpeg2Class *klass)
{
    GstVaapiDpbClass * const dpb_class = GST_VAAPI_DPB_CLASS(klass);

    dpb_class->add = gst_vaapi_dpb_mpeg2_add;
}

GstVaapiDpb *
gst_vaapi_dpb_mpeg2_new(void)
{
    return dpb_new(GST_VAAPI_TYPE_DPB_MPEG2, MAX_MPEG2_REFERENCES);
}

void
gst_vaapi_dpb_mpeg2_get_references(
    GstVaapiDpb        *dpb,
    GstVaapiPicture    *picture,
    GstVaapiPicture   **prev_picture_ptr,
    GstVaapiPicture   **next_picture_ptr
)
{
    GstVaapiPicture *ref_picture, *ref_pictures[MAX_MPEG2_REFERENCES];
    GstVaapiPicture **picture_ptr;
    guint i, index;

    g_return_if_fail(GST_VAAPI_IS_DPB_MPEG2(dpb));
    g_return_if_fail(GST_VAAPI_IS_PICTURE(picture));

    ref_pictures[0] = NULL;
    ref_pictures[1] = NULL;
    for (i = 0; i < dpb->num_pictures; i++) {
        ref_picture = dpb->pictures[i];
        index       = ref_picture->poc > picture->poc;
        picture_ptr = &ref_pictures[index];
        if (!*picture_ptr || ((*picture_ptr)->poc > ref_picture->poc) == index)
            *picture_ptr = ref_picture;
    }

    if (prev_picture_ptr)
        *prev_picture_ptr = ref_pictures[0];
    if (next_picture_ptr)
        *next_picture_ptr = ref_pictures[1];
}
