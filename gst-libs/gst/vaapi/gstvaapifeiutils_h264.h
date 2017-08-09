/*
 *  gstvaapifeiutils_h264.h - FEI related utilities for H264
 *
 *  Copyright (C) 2016-2018 Intel Corporation
 *    Author: Wang, Yi <yi.a.wang@intel.com>
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

#ifndef GST_VAAPI_FEI_UTILS_H264_H
#define GST_VAAPI_FEI_UTILS_H264_H

#include <va/va.h>

G_BEGIN_DECLS

typedef struct _GstVaapiFeiInfoToPakH264 GstVaapiFeiInfoToPakH264;

/* Structure useful for FEI ENC+PAK mode */
struct _GstVaapiFeiInfoToPakH264
{
  VAEncSequenceParameterBufferH264 h264_enc_sps;
  VAEncPictureParameterBufferH264 h264_enc_pps;
  GArray *h264_slice_headers;
  guint h264_slice_num;
};

/******************* Common FEI enum definition for all codecs ***********/
/* FeiFixme: This should be a common fei mode for all codecs,
 * move to a common header file */
#define GST_VAAPI_FEI_MODE_DEFAULT GST_VAAPI_FEI_MODE_ENC_PAK
typedef enum
{
  GST_VAAPI_FEI_MODE_ENC     = (1 << 0),
  GST_VAAPI_FEI_MODE_PAK     = (1 << 1),
  GST_VAAPI_FEI_MODE_ENC_PAK = (1 << 2)
} GstVaapiFeiMode;
/**
* GST_VAAPI_TYPE_FEI_MODE:
*
* A type that represents the fei encoding mode.
*
* Return value: the #GType of GstVaapiFeiMode
*/
#define GST_VAAPI_TYPE_FEI_MODE (gst_vaapi_fei_mode_get_type())


/******************* H264 Specific FEI enum definitions  ***********/

typedef enum
{
  GST_VAAPI_FEI_H264_FULL_SEARCH_PATH = 0,
  GST_VAAPI_FEI_H264_DIAMOND_SEARCH_PATH,
} GstVaapiFeiH264SearchPath;

typedef enum
{
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_NONE = 0,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_TINY,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_SMALL,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_DIAMOND,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_LARGE_DIAMOND,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_EXHAUSTIVE,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_HORI_DIAMOND,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_HORI_LARGE_DIAMOND,
  GST_VAAPI_FEI_H264_SEARCH_WINDOW_HORI_EXHAUSTIVE,
} GstVaapiFeiH264SearchWindow;

typedef enum
{
  GST_VAAPI_FEI_H264_INTEGER_ME = 0,
  GST_VAAPI_FEI_H264_HALF_ME = 1,
  GST_VAAPI_FEI_H264_QUARTER_ME = 3,
} GstVaapiFeiH264SubPelMode;

typedef enum
{
  GST_VAAPI_FEI_H264_SAD_NONE_TRANS = 0,
  GST_VAAPI_FEI_H264_SAD_HAAR_TRANS = 2,
} GstVaapiFeiH264SadMode;

typedef enum
{
  GST_VAAPI_FEI_H264_DISABLE_INTRA_NONE   = 0,
  GST_VAAPI_FEI_H264_DISABLE_INTRA_16x16  = (1 << 0),
  GST_VAAPI_FEI_H264_DISABLE_INTRA_8x8    = (1 << 1),
  GST_VAAPI_FEI_H264_DISABLE_INTRA_4x4    = (1 << 2),
} GstVaapiFeiH264IntraPartMask;

typedef enum
{
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_NONE   = 0,
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_16x16  = (1 << 1),
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_2x16x8 = (1 << 2),
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_2x8x16 = (1 << 3),
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_1x8x8  = (1 << 4),
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_2x8x4  = (1 << 5),
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_2x4x8  = (1 << 6),
  GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_4x4x4  = (1 << 7),
} GstVaapiFeiH264SubMbPartMask;

#define GST_VAAPI_FEI_H264_SEARCH_PATH_DEFAULT         \
    GST_VAAPI_FEI_H264_FULL_SEARCH_PATH
#define GST_VAAPI_FEI_H264_SEARCH_WINDOW_DEFAULT       \
    GST_VAAPI_FEI_H264_SEARCH_WINDOW_NONE
#define GST_VAAPI_FEI_H264_SUB_PEL_MODE_DEFAULT        \
    GST_VAAPI_FEI_H264_INTEGER_ME
#define GST_VAAPI_FEI_H264_SAD_MODE_DEFAULT            \
    GST_VAAPI_FEI_H264_SAD_NONE_TRANS
#define GST_VAAPI_FEI_H264_INTRA_PART_MASK_DEFAULT     \
    GST_VAAPI_FEI_H264_DISABLE_INTRA_NONE
#define GST_VAAPI_FEI_H264_SUB_MB_PART_MASK_DEFAULT    \
    GST_VAAPI_FEI_H264_DISABLE_SUB_MB_PART_MASK_NONE
#define GST_VAAPI_FEI_H264_SEARCH_PATH_LENGTH_DEFAULT  32
#define GST_VAAPI_FEI_H264_REF_WIDTH_DEFAULT           32
#define GST_VAAPI_FEI_H264_REF_HEIGHT_DEFAULT          32

/**
* GST_VAAPI_TYPE_FEI_H264_SEARCH_PATH:
*
* A type that represents the fei control param: search path.
*
* Return value: the #GType of GstVaapiFeiSearchPath
*/
#define GST_VAAPI_TYPE_FEI_H264_SEARCH_PATH gst_vaapi_fei_h264_search_path_get_type()

/**
* GST_VAAPI_TYPE_FEI_H264_SEARCH_WINDOW:
*
* A type that represents the fei control param: search window.
*
* Return value: the #GType of GstVaapiFeiSearchWindow
*/
#define GST_VAAPI_TYPE_FEI_H264_SEARCH_WINDOW gst_vaapi_fei_h264_search_window_get_type()

/**
* GST_VAAPI_TYPE_FEI_H264_SAD_MODE:
*
* A type that represents the fei control param: sad mode.
*
* Return value: the #GType of GstVaapiFeiSadMode
*/
#define GST_VAAPI_TYPE_FEI_H264_SAD_MODE gst_vaapi_fei_h264_sad_mode_get_type()

/**
* GST_VAAPI_TYPE_FEI_H264_INTRA_PART_MASK:
*
* A type that represents the fei control param: intra part mask.
*
* Return value: the #GType of GstVaapiFeiIntaPartMask
*/
#define GST_VAAPI_TYPE_FEI_H264_INTRA_PART_MASK gst_vaapi_fei_h264_intra_part_mask_get_type()

/**
* GST_VAAPI_TYPE_FEI_H264_SUB_PEL_MODE:
*
* A type that represents the fei control param: sub pel mode.
*
* Return value: the #GType of GstVaapiFeiSubPelMode
*/
#define GST_VAAPI_TYPE_FEI_H264_SUB_PEL_MODE gst_vaapi_fei_h264_sub_pel_mode_get_type()

/**
* GST_VAAPI_TYPE_FEI_H264_SUB_MB_PART_MASK:
*
* A type that represents the fei control param: sub maroclock partition mask.
*
* Return value: the #GType of GstVaapiFeiH264SubMbPartMask
*/
#define GST_VAAPI_TYPE_FEI_H264_SUB_MB_PART_MASK gst_vaapi_fei_h264_sub_mb_part_mask_get_type()

GType
gst_vaapi_fei_mode_get_type (void)
    G_GNUC_CONST;

GType
gst_vaapi_fei_h264_search_path_get_type (void)
    G_GNUC_CONST;

GType
gst_vaapi_fei_h264_search_window_get_type (void)
    G_GNUC_CONST;

GType
gst_vaapi_fei_h264_sad_mode_get_type (void)
    G_GNUC_CONST;

GType
gst_vaapi_fei_h264_sub_pel_mode_get_type (void)
    G_GNUC_CONST;

GType
gst_vaapi_fei_h264_intra_part_mask_get_type (void)
    G_GNUC_CONST;

GType
gst_vaapi_fei_h264_sub_mb_part_mask_get_type (void)
    G_GNUC_CONST;

G_END_DECLS
#endif /* GST_VAAPI_UTILS_FEI_H264_H */
