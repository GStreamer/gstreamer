/*
 *  gstvaapiutils_mpeg2_priv.h - MPEG-2 related utilities
 *
 *  Copyright (C) 2011-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

#ifndef GST_VAAPI_UTILS_MPEG2_PRIV_H
#define GST_VAAPI_UTILS_MPEG2_PRIV_H

#include "gstvaapiutils_mpeg2.h"
#include "gstvaapisurface.h"

G_BEGIN_DECLS

/**
 * GstVaapiMPEG2LevelLimits:
 * @level: the #GstVaapiLevelMPEG2
 * @level_idc: the MPEG-2 level indication value
 * @horizontal_size_value: the maximum number of samples per line
 * @vertical_size_value: the maximum number of lines per frame
 * @frame_rate_value: the maximum number of frames per second
 * @sample_rate: the maximum number of samples per second (for luminance)
 * @bit_rate: the maximum bit rate (kbps)
 * @vbv_buffer_size: the VBV buffer size requirements (bits)
 *
 * The data structure that describes the limits of an MPEG-2 level.
 */
typedef struct {
  GstVaapiLevelMPEG2 level;
  guint8 level_idc;
  guint16 horizontal_size_value;
  guint16 vertical_size_value;
  guint32 frame_rate_value;
  guint32 sample_rate;
  guint32 bit_rate;
  guint32 vbv_buffer_size;
} GstVaapiMPEG2LevelLimits;

/* Returns GstVaapiProfile from MPEG-2 profile_idc value */
G_GNUC_INTERNAL
GstVaapiProfile
gst_vaapi_utils_mpeg2_get_profile (guint8 profile_idc);

/* Returns MPEG-2 profile_idc value from GstVaapiProfile */
G_GNUC_INTERNAL
guint8
gst_vaapi_utils_mpeg2_get_profile_idc (GstVaapiProfile profile);

/* Returns GstVaapiLevelMPEG2 from MPEG-2 level_idc value */
G_GNUC_INTERNAL
GstVaapiLevelMPEG2
gst_vaapi_utils_mpeg2_get_level (guint8 level_idc);

/* Returns MPEG-2 level_idc value from GstVaapiLevelMPEG2 */
G_GNUC_INTERNAL
guint8
gst_vaapi_utils_mpeg2_get_level_idc (GstVaapiLevelMPEG2 level);

/* Returns level limits as specified in Table A-1 of the MPEG-2 standard */
G_GNUC_INTERNAL
const GstVaapiMPEG2LevelLimits *
gst_vaapi_utils_mpeg2_get_level_limits (GstVaapiLevelMPEG2 level);

/* Returns the Table A-1 specification */
G_GNUC_INTERNAL
const GstVaapiMPEG2LevelLimits *
gst_vaapi_utils_mpeg2_get_level_limits_table (guint * out_length_ptr);

/* Returns GstVaapiChromaType from MPEG-2 chroma_format_idc value */
G_GNUC_INTERNAL
GstVaapiChromaType
gst_vaapi_utils_mpeg2_get_chroma_type (guint chroma_format_idc);

/* Returns MPEG-2 chroma_format_idc value from GstVaapiChromaType */
G_GNUC_INTERNAL
guint
gst_vaapi_utils_mpeg2_get_chroma_format_idc (GstVaapiChromaType chroma_type);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_MPEG2_PRIV_H */
