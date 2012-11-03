/*
 * GStreamer
 *
 * Copyright (C) 2012 Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
#  include <config.h>
#endif

#include "uvc_h264.h"

GType
uvc_h264_slicemode_get_type (void)
{
  static GType type = 0;

  static const GEnumValue types[] = {
    {UVC_H264_SLICEMODE_IGNORED, "Ignored", "ignored"},
    {UVC_H264_SLICEMODE_BITSPERSLICE, "Bits per slice", "bits/slice"},
    {UVC_H264_SLICEMODE_MBSPERSLICE, "MBs per Slice", "MBs/slice"},
    {UVC_H264_SLICEMODE_SLICEPERFRAME, "Slice Per Frame", "slice/frame"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("UvcH264SliceMode", types);
  }
  return type;
}

GType
uvc_h264_usagetype_get_type (void)
{
  static GType type = 0;

  static const GEnumValue types[] = {
    {UVC_H264_USAGETYPE_REALTIME, "Realtime (video conferencing)", "realtime"},
    {UVC_H264_USAGETYPE_BROADCAST, "Broadcast", "broadcast"},
    {UVC_H264_USAGETYPE_STORAGE, "Storage", "storage"},
    {UVC_H264_USAGETYPE_UCCONFIG_0, "UCConfig 0", "ucconfig0"},
    {UVC_H264_USAGETYPE_UCCONFIG_1, "UCConfig 1", "ucconfig1"},
    {UVC_H264_USAGETYPE_UCCONFIG_2Q, "UCConfig 2Q", "ucconfig2q"},
    {UVC_H264_USAGETYPE_UCCONFIG_2S, "UCConfig 2S", "ucconfig2s"},
    {UVC_H264_USAGETYPE_UCCONFIG_3, "UCConfig 3", "ucconfig3"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("UvcH264UsageType", types);
  }
  return type;
}

GType
uvc_h264_ratecontrol_get_type (void)
{
  static GType type = 0;

  static const GEnumValue types[] = {
    {UVC_H264_RATECONTROL_CBR, "Constant bit rate", "cbr"},
    {UVC_H264_RATECONTROL_VBR, "Variable bit rate", "vbr"},
    {UVC_H264_RATECONTROL_CONST_QP, "Constant QP", "qp"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("UvcH264RateControl", types);
  }
  return type;
}

GType
uvc_h264_streamformat_get_type (void)
{
  static GType type = 0;

  static const GEnumValue types[] = {
    {UVC_H264_STREAMFORMAT_ANNEXB, "Byte stream format (Annex B)", "byte"},
    {UVC_H264_STREAMFORMAT_NAL, "NAL stream format", "nal"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("UvcH264StreamFormat", types);
  }
  return type;
}

GType
uvc_h264_entropy_get_type (void)
{
  static GType type = 0;

  static const GEnumValue types[] = {
    {UVC_H264_ENTROPY_CAVLC, "CAVLC", "cavlc"},
    {UVC_H264_ENTROPY_CABAC, "CABAC", "cabac"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("UvcH264Entropy", types);
  }
  return type;
}
