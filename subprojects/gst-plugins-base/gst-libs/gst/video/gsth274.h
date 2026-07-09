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

#pragma once

#include <gst/gst.h>
#include <gst/video/video-prelude.h>

typedef struct _GstH274RegisteredUserData       GstH274RegisteredUserData;
typedef struct _GstH274UserDataUnregistered     GstH274UserDataUnregistered;
typedef struct _GstH274DigitallySignedContentInitialization GstH274DigitallySignedContentInitialization;
typedef struct _GstH274DigitallySignedContentSelection GstH274DigitallySignedContentSelection;
typedef struct _GstH274DigitallySignedContentVerification GstH274DigitallySignedContentVerification;

/**
 * GstH274RegisteredUserData:
 * @country_code: an itu_t_t35_country_code.
 * @country_code_extension: an itu_t_t35_country_code_extension_byte.
 *   Should be ignored when @country_code is not 0xff
 * @size: the size of @data in bytes
 * @data: (array) (element-type guint8): the data of itu_t_t35_payload_byte
 *   excluding @country_code and @country_code_extension
 *
 * The User data registered by Rec. ITU-T T.35 SEI message.
 *
 * Since: 1.30
 */
struct _GstH274RegisteredUserData
{
  guint8 country_code;
  guint8 country_code_extension;
  guint size;
  const guint8 *data;
};

/**
 * GstH274UserDataUnregistered:
 * @uuid: an uuid_iso_iec_11578.
 * @data: the data of user_data_payload_byte
 * @size: (array) (element-type guint8): the size of @data in bytes
 *
 * The User data unregistered SEI message syntax.
 *
 * Since: 1.30
 */
struct _GstH274UserDataUnregistered
{
  guint8 uuid[16];
  const guint8 *data;
  guint size;
};

/**
 * GstH274DigitallySignedContentInitialization:
 * @id: Initialization ID (dsci_id)
 * @hash_method_type: Hash method type (dsci_hash_method_type)
 * @key_retrieval_mode_idc: Key retrieval mode (dsci_key_retrieval_mode_idc)
 * @use_key_register_idx_flag: Whether key register index is used (dsci_use_key_register_idx_flag)
 * @key_register_idx: Key register index (dsci_key_register_idx)
 * @content_uuid_present_flag: Whether content UUID is present (dsci_content_uuid_present_flag)
 * @content_uuid: Content UUID array (dsci_content_uuid)
 * @num_verification_substreams: Number of verification substreams (dsci_num_verification_substreams_minus1 + 1)
 * @ref_substream_flag: (array) (element-type guint8): Reference substream flags (dsci_ref_substream_flag), flattened 2D array
 * @ref_substream_flag_len: Number of entries in @ref_substream_flag
 *   (num_verification_substreams * (num_verification_substreams - 1)) / 2
 * @vss_implicit_association_mode_flag: VSS implicit association mode flag (dsci_vss_implicit_association_mode_flag)
 * @signed_content_start_flag: Signed content start flag (dsci_signed_content_start_flag)
 * @sei_signing_flag: SEI signing flag (dsci_sei_signing_flag)
 * @key_source_uri: Key source URI string (dsci_key_source_uri)
 *
 * Structure defining the H274 digitally signed content initialization.
 *
 * Since: 1.30
 */
struct _GstH274DigitallySignedContentInitialization
{
  guint8 id;
  guint8 hash_method_type;
  guint32 key_retrieval_mode_idc;
  guint8 use_key_register_idx_flag;
  guint32 key_register_idx;
  guint8 content_uuid_present_flag;
  guint8 content_uuid[16];
  guint32 num_verification_substreams;
  guint8 *ref_substream_flag;   /* flattened 2D boolean array */
  gsize ref_substream_flag_len; /* number of entries in ref_substream_flag */
  guint8 vss_implicit_association_mode_flag;
  guint8 signed_content_start_flag;
  guint8 sei_signing_flag;
  gchar *key_source_uri;
};

/**
 * GstH274DigitallySignedContentSelection:
 * @id: Selection ID (dscs_id)
 * @verification_substream_id: Verification substream ID (dscs_verification_substream_id)
 *
 * Structure defining the H274 digitally signed content selection.
 *
 * Since: 1.30
 */
struct _GstH274DigitallySignedContentSelection
{
  guint8 id;
  guint8 verification_substream_id;
};

/**
 * GstH274DigitallySignedContentVerification:
 * @id: Verification ID (dscv_id)
 * @verification_substream_id: Verification substream ID (dscv_verification_substream_id)
 * @signature_length_in_octets_minus1: Length of signature (dscv_signature_length_in_octets_minus1 + 1)
 * @signature: (array) (element-type guint8): Signature data array (dscv_signature)
 * @signed_content_end_flag: Signed content end flag (dscv_signed_content_end_flag)
 *
 * Structure defining the H274 digitally signed content verification.
 *
 * Since: 1.30
 */
struct _GstH274DigitallySignedContentVerification
{
  guint8 id;
  guint8 verification_substream_id;
  guint32 signature_length_in_octets_minus1;
  guint8 *signature;
  guint8 signed_content_end_flag;
};

/**
 * GST_TYPE_H274_REGISTERED_USER_DATA:
 *
 * The GType for #GstH274RegisteredUserData.
 *
 * Since: 1.30
 */
#define GST_TYPE_H274_REGISTERED_USER_DATA (gst_h274_registered_user_data_get_type())
GST_VIDEO_API
GType gst_h274_registered_user_data_get_type (void);

/**
 * GST_TYPE_H274_USER_DATA_UNREGISTERED:
 *
 * The GType for #GstH274UserDataUnregistered.
 *
 * Since: 1.30
 */
#define GST_TYPE_H274_USER_DATA_UNREGISTERED (gst_h274_user_data_unregistered_get_type())
GST_VIDEO_API
GType gst_h274_user_data_unregistered_get_type (void);

/**
 * GST_TYPE_H274_DIGITALLY_SIGNED_CONTENT_INITIALIZATION:
 *
 * The GType for #GstH274DigitallySignedContentInitialization.
 *
 * Since: 1.30
 */
#define GST_TYPE_H274_DIGITALLY_SIGNED_CONTENT_INITIALIZATION (gst_h274_digitally_signed_content_initialization_get_type())
GST_VIDEO_API
GType gst_h274_digitally_signed_content_initialization_get_type (void);

/**
 * GST_TYPE_H274_DIGITALLY_SIGNED_CONTENT_SELECTION:
 *
 * The GType for #GstH274DigitallySignedContentSelection.
 *
 * Since: 1.30
 */
#define GST_TYPE_H274_DIGITALLY_SIGNED_CONTENT_SELECTION (gst_h274_digitally_signed_content_selection_get_type())
GST_VIDEO_API
GType gst_h274_digitally_signed_content_selection_get_type (void);

/**
 * GST_TYPE_H274_DIGITALLY_SIGNED_CONTENT_VERIFICATION:
 *
 * The GType for #GstH274DigitallySignedContentVerification.
 *
 * Since: 1.30
 */
#define GST_TYPE_H274_DIGITALLY_SIGNED_CONTENT_VERIFICATION (gst_h274_digitally_signed_content_verification_get_type())
GST_VIDEO_API
GType gst_h274_digitally_signed_content_verification_get_type (void);

/**
 * gst_h274_user_data_registered_copy:
 * @dst_rud: (out): Destination User Data Registered structure
 * @src_rud: (in): Source User Data Registered structure
 *
 * Copy User Data Registered structure
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_user_data_registered_copy (GstH274RegisteredUserData *dst_rud,
    const GstH274RegisteredUserData * src_rud);

/**
 * gst_h274_user_data_unregistered_copy:
 * @dst_udu: (out): Destination User Data Unregistered structure
 * @src_udu: (in): Source User Data Unregistered structure
 *
 * Copy User Data Unregistered structure
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_user_data_unregistered_copy (GstH274UserDataUnregistered *dst_udu,
    const GstH274UserDataUnregistered * src_udu);

/**
 * gst_h274_dsc_initialization_copy:
 * @dst_dsc_init: (out): Destination DSC Initialization structure
 * @src_dsc_init: (in): Source DSC Initialization structure
 *
 * Copy DSC Initialization structure
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_dsc_initialization_copy (GstH274DigitallySignedContentInitialization *dst_dsc_init,
    const GstH274DigitallySignedContentInitialization * src_dsc_init);

/**
 * gst_h274_dsc_selection_copy:
 * @dst_dsc_sel: (out): Destination DSC Selection structure
 * @src_dsc_sel: (in): Source DSC Selection structure
 *
 * Copy DSC Selection structure
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_dsc_selection_copy (GstH274DigitallySignedContentSelection *dst_dsc_sel,
    const GstH274DigitallySignedContentSelection * src_dsc_sel);

/**
 * gst_h274_dsc_verification_copy:
 * @dst_dsc_ver: (out): Destination DSC Verification structure
 * @src_dsc_ver: (in): Source DSC Verification structure
 *
 * Copy DSC Verification structure
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_dsc_verification_copy (GstH274DigitallySignedContentVerification *dst_dsc_ver,
    const GstH274DigitallySignedContentVerification * src_dsc_ver);

/**
 * gst_h274_user_data_registered_free:
 * @rud: #GstH274RegisteredUserData structure holding registered user data to free
 *
 * Free resources allocated for registered user data SEI message
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_user_data_registered_free (GstH274RegisteredUserData * rud);

/**
 * gst_h274_user_data_unregistered_free:
 * @udu: #GstH274UserDataUnregistered structure holding unregistered user data to free
 *
 * Free resources allocated for user data unregistered SEI message
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_user_data_unregistered_free (GstH274UserDataUnregistered * udu);

/**
 * gst_h274_dsc_initialization_free:
 * @dsci: #GstH274DigitallySignedContentInitialization structure holding digitally signed content initialization data to free
 *
 * Free resources allocated for digitally signed content initialization SEI message
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_dsc_initialization_free (GstH274DigitallySignedContentInitialization * dsci);

/**
 * gst_h274_dsc_selection_free:
 * @dscs: #GstH274DigitallySignedContentSelection structure holding digitally signed content selection data to free
 *
 * Free resources allocated for digitally signed content selection SEI message
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_dsc_selection_free (GstH274DigitallySignedContentSelection * dscs);

/**
 * gst_h274_dsc_verification_free:
 * @dscv: #GstH274DigitallySignedContentVerification structure holding digitally signed content verification data to free
 *
 * Free resources allocated for digitally signed content verification SEI message
 *
 * Since: 1.30
 */
GST_VIDEO_API
void gst_h274_dsc_verification_free (GstH274DigitallySignedContentVerification * dscv);
