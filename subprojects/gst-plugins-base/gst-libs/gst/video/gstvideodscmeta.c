/* GStreamer
 * Copyright (C) 2025 Fluendo S.A.
 *   Author: Diego Nieto <dnieto@fluendo.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstvideodscmeta.h"

#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_video_dsc_meta_debug_category_get()
static GstDebugCategory *
gst_video_dsc_meta_debug_category_get (void)
{
  static gsize cat_gonce = 0;

  if (g_once_init_enter (&cat_gonce)) {
    GstDebugCategory *cat = NULL;

    GST_DEBUG_CATEGORY_INIT (cat,
        "videodscmeta", 0, "Video Digitally Signed Content Meta");

    g_once_init_leave (&cat_gonce, (gsize) cat);
  }
  return (GstDebugCategory *) cat_gonce;
}
#endif /* GST_DISABLE_GST_DEBUG */

/**
 * SECTION:gstvideodscmeta
 * @title: GstVideoDSCMeta
 * @short_description: GstMeta for Digitally Signed Content SEI messages
 *
 * This meta carries Digitally Signed Content (DSC) SEI message information
 * for video streams. DSC allows the verification of the integrity and authenticity
 * of the video content.
 * 
 * The mechanism is realized through three supplemental enhancement information (SEI)
 * messages that enable attaching cryptographic signatures to flexible chunks of data
 * within a video stream at the Network Abstraction Layer (NAL) unit level.
 * 
 * The current implementation follows the specification defined in
 * https://www.jvet-experts.org/doc_end_user/documents/40_Geneva/wg11/JVET-AN1019-v1.zip
 * 
 * The main concepts and mechanisms are also described in the paper:
 * https://www.hhi.fraunhofer.de/fileadmin/Events/2025/IBC_2025/IBC2025PaperAuthentication_HHI.pdf
 * 
 * Three types of metadata are provided:
 * - Initialization: Contains hash method, key source, and verification settings
 * - Selection: Indicates which verification substream to use
 * - Verification: Contains the actual signature data for verification
 *
 * Since: 1.30
 */

/* ==================== Initialization Meta ==================== */

typedef struct
{
  const GstH274DigitallySignedContentInitialization *dsc_initialization;
} GstVideoDSCInitializationMetaParams;

GType
gst_video_dsc_initialization_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstVideoDSCInitializationMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_video_dsc_initialization_meta_transform (GstBuffer *
    dest, GstMeta * meta, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoDSCInitializationMeta *dmeta, *smeta;

  smeta = (GstVideoDSCInitializationMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    dmeta =
        gst_buffer_add_video_dsc_initialization_meta (dest,
        &smeta->dsc_initialization);

    if (!dmeta)
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_video_dsc_initialization_meta_init (GstMeta * meta,
    gpointer params, GstBuffer * buffer)
{
  GstVideoDSCInitializationMeta *dsc_meta =
      (GstVideoDSCInitializationMeta *) meta;
  GstVideoDSCInitializationMetaParams *p =
      (GstVideoDSCInitializationMetaParams *) params;

  gst_h274_dsc_initialization_copy (&dsc_meta->dsc_initialization,
      p->dsc_initialization);

  return TRUE;
}

static void
gst_video_dsc_initialization_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoDSCInitializationMeta *dsc_meta =
      (GstVideoDSCInitializationMeta *) meta;
  gst_h274_dsc_initialization_free (&dsc_meta->dsc_initialization);
}

const GstMetaInfo *
gst_video_dsc_initialization_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VIDEO_DSC_INITIALIZATION_META_API_TYPE,
        "GstVideoDSCInitializationMeta",
        sizeof (GstVideoDSCInitializationMeta),
        gst_video_dsc_initialization_meta_init,
        gst_video_dsc_initialization_meta_free,
        gst_video_dsc_initialization_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & info, (GstMetaInfo *) meta);
  }

  return info;
}

GstVideoDSCInitializationMeta *
gst_buffer_add_video_dsc_initialization_meta (GstBuffer *
    buffer,
    const GstH274DigitallySignedContentInitialization * dsc_initialization)
{
  GstVideoDSCInitializationMeta *meta;
  GstVideoDSCInitializationMetaParams params = {
    .dsc_initialization = dsc_initialization,
  };

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (dsc_initialization != NULL, NULL);

  GST_DEBUG ("Adding DSC Initialization Meta: id=%u, hash_method_type=%u, "
      "key_retrieval_mode_idc=%u, use_key_register_idx_flag=%d, "
      "key_register_idx=%u, content_uuid_present_flag=%d, "
      "num_verification_substreams=%u, ref_substream_flag_len=%" G_GSIZE_FORMAT
      ", vss_implicit_association_mode_flag=%d, "
      "signed_content_start_flag=%d, sei_signing_flag=%d, key_source_uri=%s",
      dsc_initialization->id, dsc_initialization->hash_method_type,
      dsc_initialization->key_retrieval_mode_idc,
      dsc_initialization->use_key_register_idx_flag,
      dsc_initialization->key_register_idx,
      dsc_initialization->content_uuid_present_flag,
      dsc_initialization->num_verification_substreams,
      dsc_initialization->ref_substream_flag_len,
      dsc_initialization->vss_implicit_association_mode_flag,
      dsc_initialization->signed_content_start_flag,
      dsc_initialization->sei_signing_flag,
      GST_STR_NULL (dsc_initialization->key_source_uri));

  if (dsc_initialization->content_uuid_present_flag) {
    GST_MEMDUMP ("Content UUID", dsc_initialization->content_uuid, 16);
  }

  params.dsc_initialization = dsc_initialization;

  meta = (GstVideoDSCInitializationMeta *)
      gst_buffer_add_meta (buffer,
      GST_VIDEO_DSC_INITIALIZATION_META_INFO, &params);

  return meta;
}

/* ==================== Selection Meta ==================== */

typedef struct
{
  const GstH274DigitallySignedContentSelection *dsc_selection;
} GstVideoDSCSelectionMetaParams;

GType
gst_video_dsc_selection_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstVideoDSCSelectionMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_video_dsc_selection_meta_transform (GstBuffer * dest,
    GstMeta * meta, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoDSCSelectionMeta *dmeta, *smeta;

  smeta = (GstVideoDSCSelectionMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    dmeta = gst_buffer_add_video_dsc_selection_meta (dest,
        &smeta->dsc_selection);

    if (!dmeta)
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_video_dsc_selection_meta_init (GstMeta * meta,
    gpointer params, GstBuffer * buffer)
{
  GstVideoDSCSelectionMeta *dsc_meta = (GstVideoDSCSelectionMeta *) meta;
  GstVideoDSCSelectionMetaParams *p = (GstVideoDSCSelectionMetaParams *) params;

  gst_h274_dsc_selection_copy (&dsc_meta->dsc_selection, p->dsc_selection);

  return TRUE;
}

static void
gst_video_dsc_selection_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoDSCSelectionMeta *dsc_meta = (GstVideoDSCSelectionMeta *) meta;
  gst_h274_dsc_selection_free (&dsc_meta->dsc_selection);
}

const GstMetaInfo *
gst_video_dsc_selection_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VIDEO_DSC_SELECTION_META_API_TYPE,
        "GstVideoDSCSelectionMeta",
        sizeof (GstVideoDSCSelectionMeta),
        gst_video_dsc_selection_meta_init,
        gst_video_dsc_selection_meta_free,
        gst_video_dsc_selection_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & info, (GstMetaInfo *) meta);
  }

  return info;
}

GstVideoDSCSelectionMeta *
gst_buffer_add_video_dsc_selection_meta (GstBuffer * buffer,
    const GstH274DigitallySignedContentSelection * dsc_selection)
{
  GstVideoDSCSelectionMeta *meta;
  GstVideoDSCSelectionMetaParams params = {
    .dsc_selection = dsc_selection,
  };

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (dsc_selection != NULL, NULL);

  GST_DEBUG ("Adding DSC Selection Meta: id=%u, verification_substream_id=%u",
      dsc_selection->id, dsc_selection->verification_substream_id);

  params.dsc_selection = dsc_selection;

  meta = (GstVideoDSCSelectionMeta *)
      gst_buffer_add_meta (buffer, GST_VIDEO_DSC_SELECTION_META_INFO, &params);

  return meta;
}

/* ==================== Verification Meta ==================== */

typedef struct
{
  const GstH274DigitallySignedContentVerification *dsc_verification;
} GstVideoDSCVerificationMetaParams;

GType
gst_video_dsc_verification_meta_api_get_type (void)
{
  static GType type = 0;
  static const gchar *tags[] = { GST_META_TAG_VIDEO_STR, NULL };

  if (g_once_init_enter (&type)) {
    GType _type =
        gst_meta_api_type_register ("GstVideoDSCVerificationMetaAPI", tags);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
gst_video_dsc_verification_meta_transform (GstBuffer * dest,
    GstMeta * meta, GstBuffer * buffer, GQuark type, gpointer data)
{
  GstVideoDSCVerificationMeta *dmeta, *smeta;

  smeta = (GstVideoDSCVerificationMeta *) meta;

  if (GST_META_TRANSFORM_IS_COPY (type)) {
    dmeta = gst_buffer_add_video_dsc_verification_meta (dest,
        &smeta->dsc_verification);

    if (!dmeta)
      return FALSE;
  }
  return TRUE;
}

static gboolean
gst_video_dsc_verification_meta_init (GstMeta * meta,
    gpointer params, GstBuffer * buffer)
{
  GstVideoDSCVerificationMeta *dsc_meta = (GstVideoDSCVerificationMeta *) meta;
  GstVideoDSCVerificationMetaParams *p =
      (GstVideoDSCVerificationMetaParams *) params;

  gst_h274_dsc_verification_copy (&dsc_meta->dsc_verification,
      p->dsc_verification);

  return TRUE;
}

static void
gst_video_dsc_verification_meta_free (GstMeta * meta, GstBuffer * buffer)
{
  GstVideoDSCVerificationMeta *dsc_meta = (GstVideoDSCVerificationMeta *) meta;
  gst_h274_dsc_verification_free (&dsc_meta->dsc_verification);
}

const GstMetaInfo *
gst_video_dsc_verification_meta_get_info (void)
{
  static const GstMetaInfo *info = NULL;

  if (g_once_init_enter ((GstMetaInfo **) & info)) {
    const GstMetaInfo *meta =
        gst_meta_register (GST_VIDEO_DSC_VERIFICATION_META_API_TYPE,
        "GstVideoDSCVerificationMeta",
        sizeof (GstVideoDSCVerificationMeta),
        gst_video_dsc_verification_meta_init,
        gst_video_dsc_verification_meta_free,
        gst_video_dsc_verification_meta_transform);
    g_once_init_leave ((GstMetaInfo **) & info, (GstMetaInfo *) meta);
  }

  return info;
}

GstVideoDSCVerificationMeta *
gst_buffer_add_video_dsc_verification_meta (GstBuffer *
    buffer, const GstH274DigitallySignedContentVerification * dsc_verification)
{
  GstVideoDSCVerificationMeta *meta;
  GstVideoDSCVerificationMetaParams params = {
    .dsc_verification = dsc_verification,
  };

  g_return_val_if_fail (buffer != NULL, NULL);
  g_return_val_if_fail (dsc_verification != NULL, NULL);

  GST_DEBUG
      ("Adding DSC Verification Meta: id=%u, verification_substream_id=%u, "
      "signature_length_in_octets_minus1=%u",
      dsc_verification->id, dsc_verification->verification_substream_id,
      dsc_verification->signature_length_in_octets_minus1);

  if (dsc_verification->signature
      && dsc_verification->signature_length_in_octets_minus1 + 1 > 0) {
    for (guint i = 0;
        i < dsc_verification->signature_length_in_octets_minus1 + 1; i++) {
      guint8 byte = dsc_verification->signature[i];
      GST_DEBUG ("signature[%u] = %u (0x%02x)", i, byte, byte);
    }
  }

  params.dsc_verification = dsc_verification;

  meta = (GstVideoDSCVerificationMeta *)
      gst_buffer_add_meta (buffer,
      GST_VIDEO_DSC_VERIFICATION_META_INFO, &params);

  return meta;
}
