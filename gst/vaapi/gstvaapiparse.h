/*
 *  gstvaapiparse.h - Recent enough GStreamer video parsers
 *
 *  Copyright (C) 2014 Intel Corporation
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

#ifndef GST_VAAPI_PARSE_H
#define GST_VAAPI_PARSE_H

/* vaapiparse_h264 */
#define _GstH264Parse                   _GstVaapiH264Parse
#define GstH264Parse                    GstVaapiH264Parse
#define _GstH264ParseClass              _GstVaapiH264ParseClass
#define GstH264ParseClass               GstVaapiH264ParseClass
#define gst_h264_parse                  gst_vaapi_h264_parse
#define gst_h264_parse_init             gst_vaapi_h264_parse_init
#define gst_h264_parse_class_init       gst_vaapi_h264_parse_class_init
#define gst_h264_parse_parent_class     gst_vaapi_h264_parse_parent_class
#define gst_h264_parse_get_type         gst_vaapi_h264_parse_get_type

#endif /* GST_VAAPI_PARSE_H */
