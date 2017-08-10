/*
 *  gstvaapiutils_h264_priv.h - H.264 related utilities
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

#ifndef GST_VAAPI_UTILS_H264_PRIV_H
#define GST_VAAPI_UTILS_H264_PRIV_H

#include "gstvaapiutils_h264.h"
#include "gstvaapisurface.h"

G_BEGIN_DECLS

/**
 * GstVaapiH264LevelLimits:
 * @level: the #GstVaapiLevelH264
 * @level_idc: the H.264 level_idc value
 * @MaxMBPS: the maximum macroblock processing rate (MB/sec)
 * @MaxFS: the maximum frame size (MBs)
 * @MaxDpbMbs: the maxium decoded picture buffer size (MBs)
 * @MaxBR: the maximum video bit rate (kbps)
 * @MaxCPB: the maximum CPB size (kbits)
 * @MinCR: the minimum Compression Ratio
 *
 * The data structure that describes the limits of an H.264 level.
 */
typedef struct {
  GstVaapiLevelH264 level;
  guint8 level_idc;
  guint32 MaxMBPS;
  guint32 MaxFS;
  guint32 MaxDpbMbs;
  guint32 MaxBR;
  guint32 MaxCPB;
  guint32 MinCR;
} GstVaapiH264LevelLimits;

/* Returns GstVaapiProfile from H.264 profile_idc value */
G_GNUC_INTERNAL
GstVaapiProfile
gst_vaapi_utils_h264_get_profile (guint8 profile_idc);

/* Returns H.264 profile_idc value from GstVaapiProfile */
G_GNUC_INTERNAL
guint8
gst_vaapi_utils_h264_get_profile_idc (GstVaapiProfile profile);

/* Returns GstVaapiLevelH264 from H.264 level_idc value */
G_GNUC_INTERNAL
GstVaapiLevelH264
gst_vaapi_utils_h264_get_level (guint8 level_idc);

/* Returns H.264 level_idc value from GstVaapiLevelH264 */
G_GNUC_INTERNAL
guint8
gst_vaapi_utils_h264_get_level_idc (GstVaapiLevelH264 level);

/* Returns level limits as specified in Table A-1 of the H.264 standard */
G_GNUC_INTERNAL
const GstVaapiH264LevelLimits *
gst_vaapi_utils_h264_get_level_limits (GstVaapiLevelH264 level);

/* Returns the Table A-1 specification */
G_GNUC_INTERNAL
const GstVaapiH264LevelLimits *
gst_vaapi_utils_h264_get_level_limits_table (guint * out_length_ptr);

/* Returns GstVaapiChromaType from H.264 chroma_format_idc value */
G_GNUC_INTERNAL
GstVaapiChromaType
gst_vaapi_utils_h264_get_chroma_type (guint chroma_format_idc);

/* Returns H.264 chroma_format_idc value from GstVaapiChromaType */
G_GNUC_INTERNAL
guint
gst_vaapi_utils_h264_get_chroma_format_idc (GstVaapiChromaType chroma_type);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_H264_PRIV_H */
