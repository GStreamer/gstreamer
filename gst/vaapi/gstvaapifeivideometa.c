/*
 *  gstvaapifeivideometa.c - Gst VA FEI video meta
 *
 *  Copyright (C) 2016-2017 Intel Corporation
 *    Author: Yi A Wang <yi.a.wang@intel.com>
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>*
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

/**
 * SECTION:gstvaapifeivideometa
 * @short_description: VA FEI video meta for GStreamer
 */
#include "gstcompat.h"
#include "gstvaapifeivideometa.h"

static void
gst_vaapi_fei_video_meta_finalize (GstVaapiFeiVideoMeta * meta)
{
  if (meta->mbcode)
    gst_vaapi_fei_codec_object_unref (GST_VAAPI_FEI_CODEC_OBJECT
        (meta->mbcode));
  if (meta->mv)
    gst_vaapi_fei_codec_object_unref (GST_VAAPI_FEI_CODEC_OBJECT (meta->mv));
  if (meta->mvpred)
    gst_vaapi_fei_codec_object_unref (GST_VAAPI_FEI_CODEC_OBJECT
        (meta->mvpred));
  if (meta->mbcntrl)
    gst_vaapi_fei_codec_object_unref (GST_VAAPI_FEI_CODEC_OBJECT
        (meta->mbcntrl));
  if (meta->qp)
    gst_vaapi_fei_codec_object_unref (GST_VAAPI_FEI_CODEC_OBJECT (meta->qp));
  if (meta->dist)
    gst_vaapi_fei_codec_object_unref (GST_VAAPI_FEI_CODEC_OBJECT (meta->dist));
}

static void
gst_vaapi_fei_video_meta_init (GstVaapiFeiVideoMeta * meta)
{
}

static inline GstVaapiFeiVideoMeta *
_gst_vaapi_fei_video_meta_create (void)
{
  return g_slice_new0 (GstVaapiFeiVideoMeta);
}

static inline void
_gst_vaapi_fei_video_meta_destroy (GstVaapiFeiVideoMeta * meta)
{
  g_slice_free1 (sizeof (*meta), meta);
}

GstVaapiFeiVideoMeta *
gst_vaapi_fei_video_meta_new (void)
{
  GstVaapiFeiVideoMeta *meta;

  meta = _gst_vaapi_fei_video_meta_create ();
  if (!meta)
    return NULL;
  gst_vaapi_fei_video_meta_init (meta);
  return meta;
}

static inline void
_gst_vaapi_fei_video_meta_free (GstVaapiFeiVideoMeta * meta)
{
  g_atomic_int_inc (&meta->ref_count);

  gst_vaapi_fei_video_meta_finalize (meta);

  if (G_LIKELY (g_atomic_int_dec_and_test (&meta->ref_count)))
    _gst_vaapi_fei_video_meta_destroy (meta);
}

/**
 * gst_vaapi_fei_video_meta_ref:
 * @meta: a #GstVaapiFeiVideoMeta
 *
 * Atomically increases the reference count of the given @meta by one.
 *
 * Returns: The same @meta argument
 */
GstVaapiFeiVideoMeta *
gst_vaapi_fei_video_meta_ref (GstVaapiFeiVideoMeta * meta)
{
  g_return_val_if_fail (meta != NULL, NULL);

  g_atomic_int_inc (&meta->ref_count);
  return meta;
}

/**
 * gst_vaapi_fei_video_meta_unref:
 * @meta: a #GstVaapiFeiVideoMeta
 *
 * Atomically decreases the reference count of the @meta by one. If
 * the reference count reaches zero, the object will be free'd.
 */
void
gst_vaapi_fei_video_meta_unref (GstVaapiFeiVideoMeta * meta)
{
  g_return_if_fail (meta != NULL);
  g_return_if_fail (meta->ref_count > 0);
  if (g_atomic_int_dec_and_test (&meta->ref_count))
    _gst_vaapi_fei_video_meta_free (meta);
}


GType
gst_vaapi_fei_video_meta_api_get_type (void)
{
  static gsize g_type;
  static const gchar *tags[] = { "memory", NULL };

  if (g_once_init_enter (&g_type)) {
    GType type = gst_meta_api_type_register ("GstVaapiFeiVideoMetaAPI", tags);
    g_once_init_leave (&g_type, type);
  }
  return g_type;
}


#define GST_VAAPI_FEI_VIDEO_META_HOLDER(meta) \
  ((GstVaapiFeiVideoMetaHolder *) (meta))

static gboolean
gst_vaapi_fei_video_meta_holder_init (GstVaapiFeiVideoMetaHolder * meta,
    gpointer params, GstBuffer * buffer)
{
  meta->meta = NULL;
  return TRUE;
}

static void
gst_vaapi_fei_video_meta_holder_free (GstVaapiFeiVideoMetaHolder * meta,
    GstBuffer * buffer)
{
  if (meta->meta)
    gst_vaapi_fei_video_meta_unref (meta->meta);
}


#define GST_VAAPI_FEI_VIDEO_META_INFO gst_vaapi_fei_video_meta_info_get ()
static const GstMetaInfo *
gst_vaapi_fei_video_meta_info_get (void)
{
  static gsize g_meta_info;

  if (g_once_init_enter (&g_meta_info)) {
    gsize meta_info =
        GPOINTER_TO_SIZE (gst_meta_register (GST_VAAPI_FEI_VIDEO_META_API_TYPE,
            "GstVaapiFeiVideoMeta", sizeof (GstVaapiFeiVideoMetaHolder),
            (GstMetaInitFunction) gst_vaapi_fei_video_meta_holder_init,
            (GstMetaFreeFunction) gst_vaapi_fei_video_meta_holder_free,
            NULL));
    g_once_init_leave (&g_meta_info, meta_info);
  }
  return GSIZE_TO_POINTER (g_meta_info);
}

GstVaapiFeiVideoMeta *
gst_buffer_get_vaapi_fei_video_meta (GstBuffer * buffer)
{
  GstVaapiFeiVideoMeta *meta;
  GstMeta *m;

  g_return_val_if_fail (GST_IS_BUFFER (buffer), NULL);

  m = gst_buffer_get_meta (buffer, GST_VAAPI_FEI_VIDEO_META_API_TYPE);
  if (!m)
    return NULL;

  meta = GST_VAAPI_FEI_VIDEO_META_HOLDER (m)->meta;
  if (meta)
    meta->buffer = buffer;
  return meta;
}

void
gst_buffer_set_vaapi_fei_video_meta (GstBuffer * buffer,
    GstVaapiFeiVideoMeta * meta)
{
  GstMeta *m = NULL;

  g_return_if_fail (GST_IS_BUFFER (buffer));
  g_return_if_fail (GST_VAAPI_IS_FEI_VIDEO_META (meta));

  m = gst_buffer_add_meta (buffer, GST_VAAPI_FEI_VIDEO_META_INFO, NULL);

  if (m)
    GST_VAAPI_FEI_VIDEO_META_HOLDER (m)->meta =
        gst_vaapi_fei_video_meta_ref (meta);
  return;
}
