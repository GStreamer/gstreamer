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

#ifndef GST_USE_UNSTABLE_API
#warning "The H.274 parsing library is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/gst.h>
#include <gst/video/gsth274.h>
#include <gst/codecparsers/codecparsers-prelude.h>

#include "nalutils.h"

/**
 * GstH274ParserResult:
 *
 * The result of parsing H274 data.
 *
 * @GST_H274_PARSER_OK: The parsing succeeded.
 * @GST_H274_PARSER_BROKEN_DATA: The data to parse is broken.
 * @GST_H274_PARSER_BROKEN_LINK: The link to structure needed for the parsing
 *  couldn't be found.
 * @GST_H274_PARSER_ERROR: An error accured when parsing.
 * @GST_H274_PARSER_NO_NAL: No nal found during the parsing.
 * @GST_H274_PARSER_NO_NAL_END: Start of the nal found, but not the end.
 *
 * Since: 1.30
 */
typedef enum
{
  GST_H274_PARSER_OK,
  GST_H274_PARSER_BROKEN_DATA,
  GST_H274_PARSER_BROKEN_LINK,
  GST_H274_PARSER_ERROR,
  GST_H274_PARSER_NO_NAL,
  GST_H274_PARSER_NO_NAL_END
} GstH274ParserResult;

/**
 * gst_h274_parser_parse_registered_user_data:
 * @rud: #GstH274RegisteredUserData structure to hold parsed registered user data
 * @nr: a NalReader to read from
 * @payload_size: size of the payload in bytes   
 *
 * Parse registered user data SEI message
 *
 * Returns: #GstH274ParserResult
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
GstH274ParserResult gst_h274_parser_parse_registered_user_data (GstH274RegisteredUserData * rud, NalReader * nr, guint payload_size);

/**
 * gst_h274_parser_parse_user_data_unregistered:
 * @udu: #GstH274UserDataUnregistered structure to hold parsed unregistered user data
 * @nr: a NalReader to read from
 * @payload_size: size of the payload in bytes
 * 
 * Parse user data unregistered SEI message
 *
 * Returns: #GstH274ParserResult
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
GstH274ParserResult gst_h274_parser_parse_user_data_unregistered (GstH274UserDataUnregistered * udu, NalReader * nr, guint payload_size);

/**
 * gst_h274_parser_parse_dsci:
 * @dsc_init: #GstH274DigitallySignedContentInitialization structure to hold parsed digitally signed content initialization data
 * @nr: a NalReader to read from
 * @payload_size: size of the payload in bytes
 * 
 * Parse digitally signed content initialization SEI message
 *
 * Returns: #GstH274ParserResult
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
GstH274ParserResult gst_h274_parser_parse_dsci (GstH274DigitallySignedContentInitialization * dsc_init, NalReader * nr, guint payload_size); 

/**
 * gst_h274_parser_parse_dscs:
 * @dsc_sel: #GstH274DigitallySignedContentSelection structure to hold parsed digitally signed content selection data
 * @nr: a NalReader to read from
 * 
 * Parse digitally signed content selection SEI message
 *
 * Returns: #GstH274ParserResult
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
GstH274ParserResult gst_h274_parser_parse_dscs (GstH274DigitallySignedContentSelection * dsc_sel, NalReader * nr);

/**
 * gst_h274_parser_parse_dscv:
 * @dsc_ver: #GstH274DigitallySignedContentVerification structure to hold parsed digitally signed content verification data
 * @nr: a NalReader to read from
 * 
 * Parse digitally signed content verification SEI message
 *
 * Returns: #GstH274ParserResult
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
GstH274ParserResult gst_h274_parser_parse_dscv (GstH274DigitallySignedContentVerification * dsc_ver, NalReader * nr);

/**
 * gst_h274_dsci_get_payload_size:
 * @dsc_init: a #GstH274DigitallySignedContentInitialization
 *
 * Calculates the payload size in bytes for a Digitally Signed Content
 * Initialization SEI message.
 *
 * Returns: the payload size in bytes
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
guint32 gst_h274_dsci_get_payload_size (GstH274DigitallySignedContentInitialization * dsc_init);

/**
 * gst_h274_dscs_get_payload_size:
 * @dsc_sel: a #GstH274DigitallySignedContentSelection
 *
 * Calculates the payload size in bytes for a Digitally Signed Content
 * Selection SEI message.
 *
 * Returns: the payload size in bytes
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
guint32 gst_h274_dscs_get_payload_size (GstH274DigitallySignedContentSelection * dsc_sel);

/**
 * gst_h274_dscv_get_payload_size:
 * @dsc_ver: a #GstH274DigitallySignedContentVerification
 * @need_align: (out): set to %TRUE if byte-alignment trailing bits are needed
 *
 * Calculates the payload size in bytes for a Digitally Signed Content
 * Verification SEI message.
 *
 * Returns: the payload size in bytes
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
guint32 gst_h274_dscv_get_payload_size (GstH274DigitallySignedContentVerification * dsc_ver, gboolean * need_align);

/**
 * gst_h274_write_sei_registered_user_data:
 * @nw: a NalWriter to write to
 * @rud: #GstH274RegisteredUserData structure holding registered user data to write
 * 
 * Write registered user data SEI message
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
gboolean gst_h274_write_sei_registered_user_data (NalWriter * nw, GstH274RegisteredUserData * rud);

/** 
 * gst_h274_write_sei_user_data_unregistered:
 * @nw: a NalWriter to write to
 * @udu: #GstH274UserDataUnregistered structure holding unregistered user data to write
 * 
 * Write user data unregistered SEI message
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
gboolean gst_h274_write_sei_user_data_unregistered (NalWriter * nw, GstH274UserDataUnregistered * udu);

/** 
 * gst_h274_write_sei_dsci:
 * @nw: a NalWriter to write to
 * @dsc_init: #GstH274DigitallySignedContentInitialization structure holding digitally signed content initialization data to write
 * 
 * Write digitally signed content initialization SEI message
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
gboolean gst_h274_write_sei_dsci (NalWriter * nw, GstH274DigitallySignedContentInitialization * dsc_init);

/** 
 * gst_h274_write_sei_dscs:
 * @nw: a NalWriter to write to
 * @dsc_sel: #GstH274DigitallySignedContentSelection structure holding digitally signed content selection data to write
 * 
 * Write digitally signed content selection SEI message
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
gboolean gst_h274_write_sei_dscs (NalWriter * nw, GstH274DigitallySignedContentSelection * dsc_sel);

/** 
 * gst_h274_write_sei_dscv:
 * @nw: a NalWriter to write to
 * @dsc_ver: #GstH274DigitallySignedContentVerification structure holding digitally signed content verification data to write
 * 
 * Write digitally signed content verification SEI message
 *
 * Returns: %TRUE if successful, %FALSE otherwise
 *
 * Since: 1.30
 */
GST_CODEC_PARSERS_API
gboolean gst_h274_write_sei_dscv (NalWriter * nw, GstH274DigitallySignedContentVerification * dsc_ver);

