/* GStreamer
 * Copyright (C) 2026 Fluendo S.A.
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

#include "gsth274.h"

GType
gst_h274_registered_user_data_get_type (void)
{
  static GType gst_h274_registered_user_data_type = 0;

  if (g_once_init_enter (&gst_h274_registered_user_data_type)) {
    GType type = g_boxed_type_register_static ("GstH274RegisteredUserData",
        (GBoxedCopyFunc) gst_h274_user_data_registered_copy,
        (GBoxedFreeFunc) gst_h274_user_data_registered_free);
    g_once_init_leave (&gst_h274_registered_user_data_type, type);
  }
  return gst_h274_registered_user_data_type;
}

GType
gst_h274_user_data_unregistered_get_type (void)
{
  static GType gst_h274_user_data_unregistered_type = 0;

  if (g_once_init_enter (&gst_h274_user_data_unregistered_type)) {
    GType type = g_boxed_type_register_static ("GstH274UserDataUnregistered",
        (GBoxedCopyFunc) gst_h274_user_data_unregistered_copy,
        (GBoxedFreeFunc) gst_h274_user_data_unregistered_free);
    g_once_init_leave (&gst_h274_user_data_unregistered_type, type);
  }
  return gst_h274_user_data_unregistered_type;
}

GType
gst_h274_digitally_signed_content_initialization_get_type (void)
{
  static GType gst_h274_dsc_init_type = 0;

  if (g_once_init_enter (&gst_h274_dsc_init_type)) {
    GType type =
        g_boxed_type_register_static
        ("GstH274DigitallySignedContentInitialization",
        (GBoxedCopyFunc) gst_h274_dsc_initialization_copy,
        (GBoxedFreeFunc) gst_h274_dsc_initialization_free);
    g_once_init_leave (&gst_h274_dsc_init_type, type);
  }
  return gst_h274_dsc_init_type;
}

GType
gst_h274_digitally_signed_content_selection_get_type (void)
{
  static GType gst_h274_dsc_sel_type = 0;

  if (g_once_init_enter (&gst_h274_dsc_sel_type)) {
    GType type =
        g_boxed_type_register_static ("GstH274DigitallySignedContentSelection",
        (GBoxedCopyFunc) gst_h274_dsc_selection_copy,
        (GBoxedFreeFunc) gst_h274_dsc_selection_free);
    g_once_init_leave (&gst_h274_dsc_sel_type, type);
  }
  return gst_h274_dsc_sel_type;
}

GType
gst_h274_digitally_signed_content_verification_get_type (void)
{
  static GType gst_h274_dsc_ver_type = 0;

  if (g_once_init_enter (&gst_h274_dsc_ver_type)) {
    GType type =
        g_boxed_type_register_static
        ("GstH274DigitallySignedContentVerification",
        (GBoxedCopyFunc) gst_h274_dsc_verification_copy,
        (GBoxedFreeFunc) gst_h274_dsc_verification_free);
    g_once_init_leave (&gst_h274_dsc_ver_type, type);
  }
  return gst_h274_dsc_ver_type;
}

void
gst_h274_user_data_registered_copy (GstH274RegisteredUserData * dst_rud,
    const GstH274RegisteredUserData * src_rud)
{
  g_return_if_fail (dst_rud != NULL);
  g_return_if_fail (src_rud != NULL);

  dst_rud->country_code = src_rud->country_code;
  dst_rud->country_code_extension = src_rud->country_code_extension;
  dst_rud->size = src_rud->size;

  if (src_rud->size) {
    dst_rud->data = g_memdup2 (src_rud->data, src_rud->size);
  } else {
    dst_rud->data = NULL;
  }
}

void
gst_h274_user_data_unregistered_copy (GstH274UserDataUnregistered * dst_udu,
    const GstH274UserDataUnregistered * src_udu)
{
  g_return_if_fail (dst_udu != NULL);
  g_return_if_fail (src_udu != NULL);

  memcpy (dst_udu->uuid, src_udu->uuid, 16);
  dst_udu->size = src_udu->size;

  if (src_udu->size) {
    dst_udu->data = g_memdup2 (src_udu->data, src_udu->size);
  } else {
    dst_udu->data = NULL;
  }
}

void
gst_h274_dsc_initialization_copy (GstH274DigitallySignedContentInitialization *
    dst_dsc_init,
    const GstH274DigitallySignedContentInitialization * src_dsc_init)
{
  g_return_if_fail (dst_dsc_init != NULL);
  g_return_if_fail (src_dsc_init != NULL);

  dst_dsc_init->id = src_dsc_init->id;
  dst_dsc_init->hash_method_type = src_dsc_init->hash_method_type;
  dst_dsc_init->key_retrieval_mode_idc = src_dsc_init->key_retrieval_mode_idc;
  dst_dsc_init->use_key_register_idx_flag =
      src_dsc_init->use_key_register_idx_flag;
  dst_dsc_init->key_register_idx = src_dsc_init->key_register_idx;
  dst_dsc_init->content_uuid_present_flag =
      src_dsc_init->content_uuid_present_flag;
  memcpy (dst_dsc_init->content_uuid, src_dsc_init->content_uuid, 16);
  dst_dsc_init->num_verification_substreams =
      src_dsc_init->num_verification_substreams;
  dst_dsc_init->vss_implicit_association_mode_flag =
      src_dsc_init->vss_implicit_association_mode_flag;
  dst_dsc_init->signed_content_start_flag =
      src_dsc_init->signed_content_start_flag;
  dst_dsc_init->sei_signing_flag = src_dsc_init->sei_signing_flag;

  dst_dsc_init->ref_substream_flag_len = src_dsc_init->ref_substream_flag_len;
  if (src_dsc_init->ref_substream_flag &&
      src_dsc_init->ref_substream_flag_len > 0) {
    dst_dsc_init->ref_substream_flag =
        g_memdup2 (src_dsc_init->ref_substream_flag,
        src_dsc_init->ref_substream_flag_len);
  } else {
    dst_dsc_init->ref_substream_flag = NULL;
    dst_dsc_init->ref_substream_flag_len = 0;
  }

  dst_dsc_init->key_source_uri = NULL;
  if (src_dsc_init->key_source_uri) {
    dst_dsc_init->key_source_uri =
        g_strdup ((const char *) src_dsc_init->key_source_uri);
  }
}

void
gst_h274_dsc_selection_copy (GstH274DigitallySignedContentSelection *
    dst_dsc_sel, const GstH274DigitallySignedContentSelection * src_dsc_sel)
{
  g_return_if_fail (dst_dsc_sel != NULL);
  g_return_if_fail (src_dsc_sel != NULL);

  dst_dsc_sel->id = src_dsc_sel->id;
  dst_dsc_sel->verification_substream_id =
      src_dsc_sel->verification_substream_id;
}

void
gst_h274_dsc_verification_copy (GstH274DigitallySignedContentVerification *
    dst_dsc_ver, const GstH274DigitallySignedContentVerification * src_dsc_ver)
{
  g_return_if_fail (dst_dsc_ver != NULL);
  g_return_if_fail (src_dsc_ver != NULL);

  dst_dsc_ver->id = src_dsc_ver->id;
  dst_dsc_ver->verification_substream_id =
      src_dsc_ver->verification_substream_id;
  dst_dsc_ver->signature_length_in_octets_minus1 =
      src_dsc_ver->signature_length_in_octets_minus1;

  if (src_dsc_ver->signature
      && src_dsc_ver->signature_length_in_octets_minus1 + 1 > 0) {
    dst_dsc_ver->signature =
        g_memdup2 (src_dsc_ver->signature,
        src_dsc_ver->signature_length_in_octets_minus1 + 1);
  } else {
    dst_dsc_ver->signature = NULL;
  }

  dst_dsc_ver->signed_content_end_flag = src_dsc_ver->signed_content_end_flag;
}

void
gst_h274_user_data_registered_free (GstH274RegisteredUserData * rud)
{
  g_free ((gpointer) rud->data);
  rud->data = NULL;
  rud->size = 0;
}

void
gst_h274_user_data_unregistered_free (GstH274UserDataUnregistered * udu)
{
  g_free ((gpointer) udu->data);
  udu->data = NULL;
  udu->size = 0;
}

void
gst_h274_dsc_initialization_free (GstH274DigitallySignedContentInitialization *
    dsci)
{
  g_free (dsci->ref_substream_flag);
  dsci->ref_substream_flag = NULL;
  dsci->ref_substream_flag_len = 0;

  g_free (dsci->key_source_uri);
  dsci->key_source_uri = NULL;
}

void
gst_h274_dsc_selection_free (GstH274DigitallySignedContentSelection * dscs)
{
  /* Nothing to free */
}

void
gst_h274_dsc_verification_free (GstH274DigitallySignedContentVerification *
    dscv)
{
  g_free (dscv->signature);
  dscv->signature = NULL;
  dscv->signature_length_in_octets_minus1 = 0;
}
