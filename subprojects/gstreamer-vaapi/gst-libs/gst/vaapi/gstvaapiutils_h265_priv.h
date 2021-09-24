/*
 *  gstvaapiutils_h265_priv.h - H.265 related utilities
 *
 *  Copyright (C) 2015 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifndef GST_VAAPI_UTILS_H265_PRIV_H
#define GST_VAAPI_UTILS_H265_PRIV_H

#include "gstvaapiutils_h265.h"
#include "gstvaapisurface.h"

G_BEGIN_DECLS

/**
 * GstVaapiH265LevelLimits:
 * @level: the #GstVaapiLevelH265
 * @level_idc: the H.265 level_idc value
 * @MaxLumaPs: the maximum luma picture size
 * @MaxCPBTierMain: the maximum CPB size for Main tier(kbits)
 * @MaxCPBTierHigh: the maximum CPB size for High tier(kbits)
 * @MaxSliceSegPic: the maximum slice segments per picture
 * @MaxTileRows: the maximum number of Tile Rows
 * @MaxTileColumns: the maximum number of Tile Columns
 * @MaxLumaSr: the maximum luma sample rate (samples/sec)
 * @MaxBRTierMain: the maximum video bit rate for Main Tier(kbps)
 * @MaxBRTierHigh: the maximum video bit rate for High Tier(kbps)
 * @MinCr: the mimimum compression ratio
 *
 * The data structure that describes the limits of an H.265 level.
 */
typedef struct {
  GstVaapiLevelH265 level;
  guint8 level_idc;
  guint32 MaxLumaPs;
  guint32 MaxCPBTierMain;
  guint32 MaxCPBTierHigh;
  guint32 MaxSliceSegPic;
  guint32 MaxTileRows;
  guint32 MaxTileColumns;
  guint32 MaxLumaSr;
  guint32 MaxBRTierMain;
  guint32 MaxBRTierHigh;
  guint32 MinCr;
} GstVaapiH265LevelLimits;

/* Returns GstVaapiProfile from H.265 profile_idc value */
G_GNUC_INTERNAL
GstVaapiProfile
gst_vaapi_utils_h265_get_profile (GstH265SPS * sps);

/* Returns H.265 profile_idc value from GstVaapiProfile */
G_GNUC_INTERNAL
guint8
gst_vaapi_utils_h265_get_profile_idc (GstVaapiProfile profile);

/* Returns GstVaapiLevelH265 from H.265 level_idc value */
G_GNUC_INTERNAL
GstVaapiLevelH265
gst_vaapi_utils_h265_get_level (guint8 level_idc);

/* Returns H.265 level_idc value from GstVaapiLevelH265 */
G_GNUC_INTERNAL
guint8
gst_vaapi_utils_h265_get_level_idc (GstVaapiLevelH265 level);

/* Returns level limits as specified in Table A-1 of the H.265 standard */
G_GNUC_INTERNAL
const GstVaapiH265LevelLimits *
gst_vaapi_utils_h265_get_level_limits (GstVaapiLevelH265 level);

/* Returns the Table A-1 specification */
G_GNUC_INTERNAL
const GstVaapiH265LevelLimits *
gst_vaapi_utils_h265_get_level_limits_table (guint * out_length_ptr);

/* Returns GstVaapiChromaType from H.265 chroma_format_idc value */
G_GNUC_INTERNAL
GstVaapiChromaType
gst_vaapi_utils_h265_get_chroma_type (guint chroma_format_idc,
    guint luma_bit_depth, guint chroma_bit_depth);

/* Returns H.265 chroma_format_idc value from GstVaapiChromaType */
G_GNUC_INTERNAL
guint
gst_vaapi_utils_h265_get_chroma_format_idc (GstVaapiChromaType chroma_type);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_H265_PRIV_H */
