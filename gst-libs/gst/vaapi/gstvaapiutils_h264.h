/*
 *  gstvaapiutils_h264.h - H.264 related utilities
 *
 *  Copyright (C) 2011-2013 Intel Corporation
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

#ifndef GST_VAAPI_UTILS_H264_H
#define GST_VAAPI_UTILS_H264_H

#include <va/va.h>
#include <gst/vaapi/gstvaapiprofile.h>
#include <gst/vaapi/gstvaapisurface.h>

G_BEGIN_DECLS

/**
 * GstVaapiLevelH264:
 * @GST_VAAPI_LEVEL_H264_L1: H.264 level 1.
 * @GST_VAAPI_LEVEL_H264_L1_1: H.264 level 1.1.
 * @GST_VAAPI_LEVEL_H264_L1_2: H.264 level 1.2.
 * @GST_VAAPI_LEVEL_H264_L1_3: H.264 level 1.3.
 * @GST_VAAPI_LEVEL_H264_L2: H.264 level 2.
 * @GST_VAAPI_LEVEL_H264_L2_1: H.264 level 2.1.
 * @GST_VAAPI_LEVEL_H264_L2_2: H.264 level 2.2.
 * @GST_VAAPI_LEVEL_H264_L3: H.264 level 3.
 * @GST_VAAPI_LEVEL_H264_L3_1: H.264 level 3.1.
 * @GST_VAAPI_LEVEL_H264_L3_2: H.264 level 3.2.
 * @GST_VAAPI_LEVEL_H264_L4: H.264 level 4.
 * @GST_VAAPI_LEVEL_H264_L4_1: H.264 level 4.1.
 * @GST_VAAPI_LEVEL_H264_L4_2: H.264 level 4.2.
 * @GST_VAAPI_LEVEL_H264_L5: H.264 level 5.
 * @GST_VAAPI_LEVEL_H264_L5_1: H.264 level 5.1.
 * @GST_VAAPI_LEVEL_H264_L5_2: H.264 level 5.2.
 *
 * The set of all levels for #GstVaapiLevelH264.
 */
typedef enum
{
  GST_VAAPI_LEVEL_H264_L1 = 1,
  GST_VAAPI_LEVEL_H264_L1b,
  GST_VAAPI_LEVEL_H264_L1_1,
  GST_VAAPI_LEVEL_H264_L1_2,
  GST_VAAPI_LEVEL_H264_L1_3,
  GST_VAAPI_LEVEL_H264_L2,
  GST_VAAPI_LEVEL_H264_L2_1,
  GST_VAAPI_LEVEL_H264_L2_2,
  GST_VAAPI_LEVEL_H264_L3,
  GST_VAAPI_LEVEL_H264_L3_1,
  GST_VAAPI_LEVEL_H264_L3_2,
  GST_VAAPI_LEVEL_H264_L4,
  GST_VAAPI_LEVEL_H264_L4_1,
  GST_VAAPI_LEVEL_H264_L4_2,
  GST_VAAPI_LEVEL_H264_L5,
  GST_VAAPI_LEVEL_H264_L5_1,
  GST_VAAPI_LEVEL_H264_L5_2,
} GstVaapiLevelH264;

/**
 * GstVaapiH264LevelLimits:
 * @level: the #GstVaapiLevelH264
 * @level_idc: the H.264 level_idc value
 * @MaxMBPS: the maximum macroblock processing rate (MB/sec)
 * @MaxFS: the maximum frame size (MBs)
 * @MaxDpbMbs: the maxium decoded picture buffer size (MBs)
 * @MaxBR: the maximum video bit rate (kbps)
 *
 * The data structure that describes the limits of an H.264 level.
 */
typedef struct
{
  GstVaapiLevelH264 level;
  guint8 level_idc;
  guint32 MaxMBPS;
  guint32 MaxFS;
  guint32 MaxDpbMbs;
  guint32 MaxBR;
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
gst_vaapi_utils_h264_get_level_limits_table (guint *out_length_ptr);

/* Returns GstVaapiChromaType from H.264 chroma_format_idc value */
G_GNUC_INTERNAL
GstVaapiChromaType
gst_vaapi_utils_h264_get_chroma_type (guint chroma_format_idc);

/* Returns H.264 chroma_format_idc value from GstVaapiChromaType */
G_GNUC_INTERNAL
guint
gst_vaapi_utils_h264_get_chroma_format_idc (GstVaapiChromaType chroma_type);

G_END_DECLS

#endif /* GST_VAAPI_UTILS_H264_H */
