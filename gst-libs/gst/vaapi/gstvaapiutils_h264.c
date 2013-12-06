/*
 *  gstvaapiutils_h264.c - H.264 related utilities
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

#include "sysdeps.h"
#include <gst/codecparsers/gsth264parser.h>
#include "gstvaapiutils_h264.h"

/** Returns GstVaapiProfile from H.264 profile_idc value */
GstVaapiProfile
gst_vaapi_utils_h264_get_profile (guint8 profile_idc)
{
  GstVaapiProfile profile;

  switch (profile_idc) {
    case GST_H264_PROFILE_BASELINE:
      profile = GST_VAAPI_PROFILE_H264_BASELINE;
      break;
    case GST_H264_PROFILE_MAIN:
      profile = GST_VAAPI_PROFILE_H264_MAIN;
      break;
    case GST_H264_PROFILE_HIGH:
      profile = GST_VAAPI_PROFILE_H264_HIGH;
      break;
    case GST_H264_PROFILE_HIGH10:
      profile = GST_VAAPI_PROFILE_H264_HIGH10;
      break;
    default:
      g_assert (0 && "unsupported profile_idc value");
      profile = GST_VAAPI_PROFILE_UNKNOWN;
      break;
  }
  return profile;
}

/** Returns H.264 profile_idc value from GstVaapiProfile */
guint8
gst_vaapi_utils_h264_get_profile_idc (GstVaapiProfile profile)
{
  guint8 profile_idc;

  switch (profile) {
    case GST_VAAPI_PROFILE_H264_BASELINE:
    case GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE:
      profile_idc = GST_H264_PROFILE_BASELINE;
      break;
    case GST_VAAPI_PROFILE_H264_MAIN:
      profile_idc = GST_H264_PROFILE_MAIN;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH:
      profile_idc = GST_H264_PROFILE_HIGH;
      break;
    case GST_VAAPI_PROFILE_H264_HIGH10:
      profile_idc = GST_H264_PROFILE_HIGH10;
      break;
    default:
      g_assert (0 && "unsupported GstVaapiProfile value");
      profile_idc = 0;
      break;
  }
  return profile_idc;
}

/** Returns GstVaapiChromaType from H.264 chroma_format_idc value */
GstVaapiChromaType
gst_vaapi_utils_h264_get_chroma_type (guint chroma_format_idc)
{
  GstVaapiChromaType chroma_type;

  switch (chroma_format_idc) {
    case 0:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV400;
      break;
    case 1:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV420;
      break;
    case 2:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV422;
      break;
    case 3:
      chroma_type = GST_VAAPI_CHROMA_TYPE_YUV444;
      break;
    default:
      g_assert (0 && "unsupported chroma_format_idc value");
      chroma_type = (GstVaapiChromaType) 0;
      break;
  }
  return chroma_type;
}

/** Returns H.264 chroma_format_idc value from GstVaapiChromaType */
guint
gst_vaapi_utils_h264_get_chroma_format_idc (GstVaapiChromaType chroma_type)
{
  guint chroma_format_idc;

  switch (chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV400:
      chroma_format_idc = 0;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV420:
      chroma_format_idc = 1;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
      chroma_format_idc = 2;
      break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
      chroma_format_idc = 3;
      break;
    default:
      g_assert (0 && "unsupported GstVaapiChromaType value");
      chroma_format_idc = 1;
      break;
  }
  return chroma_format_idc;
}
