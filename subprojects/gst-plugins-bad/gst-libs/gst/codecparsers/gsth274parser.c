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

#include "gsth274parser.h"
#include <gst/base/gstbytereader.h>

const guint uuid_iso_iec_11578_size = 16;

GstH274ParserResult
gst_h274_parser_parse_registered_user_data (GstH274RegisteredUserData * rud,
    NalReader * nr, guint payload_size)
{
  guint8 *data = NULL;

  rud->data = NULL;
  rud->size = 0;

  if (payload_size < 2) {
    GST_WARNING ("Too small payload size %d", payload_size);
    return GST_H274_PARSER_BROKEN_DATA;
  }

  READ_UINT8 (nr, rud->country_code, 8);
  --payload_size;

  if (rud->country_code == 0xFF) {
    READ_UINT8 (nr, rud->country_code_extension, 8);
    --payload_size;
  } else {
    rud->country_code_extension = 0;
  }

  if (payload_size < 1) {
    GST_WARNING ("No more remaining payload data to store");
    return GST_H274_PARSER_BROKEN_DATA;
  }

  data = g_malloc (payload_size);
  for (guint i = 0; i < payload_size; ++i) {
    READ_UINT8 (nr, data[i], 8);
  }

  GST_MEMDUMP ("SEI user data", data, payload_size);

  rud->data = data;
  rud->size = payload_size;
  return GST_H274_PARSER_OK;

error:
  {
    GST_WARNING ("error parsing \"Registered User Data\"");
    g_free (data);
    return GST_H274_PARSER_ERROR;
  }
}

gboolean
gst_h274_write_sei_registered_user_data (NalWriter * nw,
    GstH274RegisteredUserData * rud)
{
  WRITE_UINT8 (nw, rud->country_code, 8);
  if (rud->country_code == 0xff)
    WRITE_UINT8 (nw, rud->country_code_extension, 8);

  WRITE_BYTES (nw, rud->data, rud->size);

  return TRUE;

error:
  return FALSE;
}

GstH274ParserResult
gst_h274_parser_parse_user_data_unregistered (GstH274UserDataUnregistered * udu,
    NalReader * nr, guint payload_size)
{
  guint8 *data = NULL;
  guint i;

  if (payload_size < uuid_iso_iec_11578_size) {
    GST_WARNING ("Too small payload size %d", payload_size);
    return GST_H274_PARSER_BROKEN_DATA;
  }

  for (i = 0; i < uuid_iso_iec_11578_size; ++i) {
    READ_UINT8 (nr, udu->uuid[i], 8);
  }
  payload_size -= uuid_iso_iec_11578_size;

  udu->size = payload_size;

  data = g_malloc (payload_size);
  for (i = 0; i < payload_size; ++i) {
    READ_UINT8 (nr, data[i], 8);
  }

  udu->data = data;

  return GST_H274_PARSER_OK;

error:
  {
    GST_WARNING ("error parsing \"User Data Unregistered\"");
    g_free (data);
    return GST_H274_PARSER_ERROR;
  }
}

gboolean
gst_h274_write_sei_user_data_unregistered (NalWriter * nw,
    GstH274UserDataUnregistered * udu)
{
  WRITE_BYTES (nw, udu->uuid, uuid_iso_iec_11578_size);
  WRITE_BYTES (nw, udu->data, udu->size);

  return TRUE;

error:
  return FALSE;
}

GstH274ParserResult
gst_h274_parser_parse_dsci (GstH274DigitallySignedContentInitialization * dsci,
    NalReader * nr, guint payload_size)
{
  guint i, j;
  guint bytes_read = 0;
  guint start_pos;

  GST_LOG ("parsing \"Digitally Signed Content Initialization\"");

  memset (dsci, 0, sizeof (*dsci));

  start_pos = nal_reader_get_pos (nr) / 8;

  READ_UINT8 (nr, dsci->id, 8);
  READ_UINT8 (nr, dsci->hash_method_type, 8);
  READ_UE (nr, dsci->key_retrieval_mode_idc);

  if (dsci->key_retrieval_mode_idc == 1) {
    READ_UINT8 (nr, dsci->use_key_register_idx_flag, 1);
    if (dsci->use_key_register_idx_flag) {
      READ_UE (nr, dsci->key_register_idx);
    }
  }

  READ_UINT8 (nr, dsci->content_uuid_present_flag, 1);
  if (dsci->content_uuid_present_flag) {
    for (i = 0; i < 16; i++) {
      READ_UINT8 (nr, dsci->content_uuid[i], 8);
    }
  }

  READ_UE (nr, dsci->num_verification_substreams);
  dsci->num_verification_substreams += 1;       // It's minus1 in the bitstream
  CHECK_ALLOWED_MIN (dsci->num_verification_substreams, 1);

  {
    guint total_flags =
        (dsci->num_verification_substreams *
        (dsci->num_verification_substreams - 1)) / 2;
    guint idx = 0;

    dsci->ref_substream_flag_len = total_flags;
    dsci->ref_substream_flag =
        (total_flags > 0) ? g_malloc0 (total_flags) : NULL;

    for (i = 1; i < dsci->num_verification_substreams; i++) {
      for (j = 0; j < i; j++) {
        guint8 flag;
        READ_UINT8 (nr, flag, 1);
        if (idx < total_flags)
          dsci->ref_substream_flag[idx++] = flag;
      }
    }
  }

  READ_UINT8 (nr, dsci->vss_implicit_association_mode_flag, 1);
  READ_UINT8 (nr, dsci->signed_content_start_flag, 1);
  READ_UINT8 (nr, dsci->sei_signing_flag, 1);

  while (!nal_reader_is_byte_aligned (nr)) {
    if (!nal_reader_skip (nr, 1))
      goto error;
  }

  // Calculate remaining bytes for key_source_uri
  bytes_read = (nal_reader_get_pos (nr) / 8) - start_pos;

  if (bytes_read < payload_size) {
    guint remaining = payload_size - bytes_read;
    guint8 *uri = g_malloc0 (remaining + 1);

    for (i = 0; i < remaining; i++) {
      READ_UINT8 (nr, uri[i], 8);
      if (uri[i] == 0) {
        // Null terminator found
        dsci->key_source_uri = (gchar *) uri;
        break;
      }
    }

    if (i == remaining) {
      GST_WARNING ("key_source_uri not null-terminated within payload");
      g_free (uri);
      goto error;
    }
  } else if (bytes_read == payload_size) {
    // No URI present - this is valid
    dsci->key_source_uri = NULL;
  } else {
    // bytes_read > payload_size - we read too much, this is an error
    GST_WARNING ("Read more bytes (%u) than payload size (%u)",
        bytes_read, payload_size);
    goto error;
  }

  return GST_H274_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Digitally Signed Content Initialization\"");

  gst_h274_dsc_initialization_free (dsci);

  return GST_H274_PARSER_ERROR;
}

gboolean
gst_h274_write_sei_dsci (NalWriter * nw,
    GstH274DigitallySignedContentInitialization * dsc_init)
{
  guint i, j;
  WRITE_UINT8 (nw, dsc_init->id, 8);
  WRITE_UINT8 (nw, dsc_init->hash_method_type, 8);
  WRITE_UE (nw, dsc_init->key_retrieval_mode_idc);
  if (dsc_init->key_retrieval_mode_idc == 1) {
    WRITE_UINT8 (nw, dsc_init->use_key_register_idx_flag, 1);
    if (dsc_init->use_key_register_idx_flag) {
      WRITE_UE (nw, dsc_init->key_register_idx);
    }
  }
  WRITE_UINT8 (nw, dsc_init->content_uuid_present_flag, 1);
  if (dsc_init->content_uuid_present_flag) {
    for (i = 0; i < 16; i++) {
      WRITE_UINT8 (nw, dsc_init->content_uuid[i], 8);
    }
  }
  CHECK_ALLOWED_MIN (dsc_init->num_verification_substreams, 1);
  WRITE_UE (nw, dsc_init->num_verification_substreams - 1);
  {
    guint expected_len =
        (dsc_init->num_verification_substreams *
        (dsc_init->num_verification_substreams - 1)) / 2;
    guint idx = 0;

    if (expected_len > 0 &&
        (!dsc_init->ref_substream_flag ||
            dsc_init->ref_substream_flag_len < expected_len))
      goto error;

    for (i = 1; i < dsc_init->num_verification_substreams; i++) {
      for (j = 0; j < i; j++) {
        WRITE_UINT8 (nw, dsc_init->ref_substream_flag[idx++], 1);
      }
    }
  }
  WRITE_UINT8 (nw, dsc_init->vss_implicit_association_mode_flag, 1);
  WRITE_UINT8 (nw, dsc_init->signed_content_start_flag, 1);
  WRITE_UINT8 (nw, dsc_init->sei_signing_flag, 1);
  while (!nal_writer_is_byte_aligned (nw)) {
    WRITE_UINT8 (nw, 0, 1);
  }
  if (dsc_init->key_source_uri != NULL) {
    gsize str_len = strlen ((const char *) (dsc_init->key_source_uri));

    for (i = 0; i < str_len; i++) {
      WRITE_UINT8 (nw, dsc_init->key_source_uri[i], 8);
    }

    WRITE_UINT8 (nw, 0, 8);
  }

  return TRUE;

error:
  return FALSE;
}

GstH274ParserResult
gst_h274_parser_parse_dscs (GstH274DigitallySignedContentSelection * dscs,
    NalReader * nr)
{
  GST_LOG ("parsing \"Digitally Signed Content Selection\"");

  memset (dscs, 0, sizeof (*dscs));

  READ_UINT8 (nr, dscs->id, 8);
  READ_UINT8 (nr, dscs->verification_substream_id, 8);

  return GST_H274_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Digitally Signed Content Selection\"");
  return GST_H274_PARSER_ERROR;
}

gboolean
gst_h274_write_sei_dscs (NalWriter * nw,
    GstH274DigitallySignedContentSelection * dsc_sel)
{
  WRITE_UINT8 (nw, dsc_sel->id, 8);
  WRITE_UINT8 (nw, dsc_sel->verification_substream_id, 8);

  return TRUE;

error:
  return FALSE;
}

GstH274ParserResult
gst_h274_parser_parse_dscv (GstH274DigitallySignedContentVerification * dscv,
    NalReader * nr)
{
  guint i;

  GST_LOG ("parsing \"Digitally Signed Content Verification\"");

  memset (dscv, 0, sizeof (*dscv));

  READ_UINT8 (nr, dscv->id, 8);
  READ_UINT8 (nr, dscv->verification_substream_id, 8);

  READ_UINT32 (nr, dscv->signature_length_in_octets_minus1, 24);
  CHECK_ALLOWED_MIN (dscv->signature_length_in_octets_minus1 + 1, 1);

  dscv->signature =
      (dscv->signature_length_in_octets_minus1 + 1 >
      0) ? g_malloc (dscv->signature_length_in_octets_minus1 + 1) : NULL;

  for (i = 0; i < dscv->signature_length_in_octets_minus1 + 1; i++) {
    READ_UINT8 (nr, dscv->signature[i], 8);
  }

  if (dscv->verification_substream_id == 0) {
    READ_UINT8 (nr, dscv->signed_content_end_flag, 1);
  }

  return GST_H274_PARSER_OK;

error:
  GST_WARNING ("error parsing \"Digitally Signed Content Verification\"");

  gst_h274_dsc_verification_free (dscv);

  return GST_H274_PARSER_ERROR;
}

gboolean
gst_h274_write_sei_dscv (NalWriter * nw,
    GstH274DigitallySignedContentVerification * dsc_ver)
{
  gint i;
  WRITE_UINT8 (nw, dsc_ver->id, 8);
  WRITE_UINT8 (nw, dsc_ver->verification_substream_id, 8);
  CHECK_ALLOWED_MIN (dsc_ver->signature_length_in_octets_minus1 + 1, 1);
  WRITE_UINT32 (nw, dsc_ver->signature_length_in_octets_minus1, 24);

  {
    if (!dsc_ver->signature
        || dsc_ver->signature_length_in_octets_minus1 + 1 <= 0)
      goto error;

    for (i = 0; i < (gint) dsc_ver->signature_length_in_octets_minus1 + 1; i++) {
      WRITE_UINT8 (nw, dsc_ver->signature[i], 8);
    }
  }
  if (0 == dsc_ver->verification_substream_id) {
    WRITE_UINT8 (nw, dsc_ver->signed_content_end_flag, 1);
  }

  return TRUE;

error:
  return FALSE;
}
