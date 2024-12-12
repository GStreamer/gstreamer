/*
 * GStreamer
 *
 * unit test for h266parse
 *
 * Copyright (C) 2024 Intel Corporation
 *    Author: He Junyan <junyan.he@intel.com>
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

#include <gst/check/check.h>
#include <gst/video/video-sei.h>
#include "parser.h"

#define SRC_CAPS_TMPL   "video/x-h266, parsed=(boolean)false"
#define SINK_CAPS_TMPL  "video/x-h266, parsed=(boolean)true"

#define structure_get_int(s,f) \
    (g_value_get_int(gst_structure_get_value(s,f)))
#define fail_unless_structure_field_int_equals(s,field,num) \
    fail_unless_equals_int (structure_get_int(s,field), num)

#define structure_get_string(s,f) \
    (g_value_get_string(gst_structure_get_value(s,f)))
#define fail_unless_structure_field_string_equals(s,field,name) \
    fail_unless_equals_string (structure_get_string(s,field), name)

GstStaticPadTemplate sinktemplate_bs_au = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SINK_CAPS_TMPL
        ", stream-format = (string) byte-stream, alignment = (string) au"));

GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (SRC_CAPS_TMPL));

static const gchar *ctx_suite;
static gboolean ctx_codec_data;

/* Extract from standard ITU stream VPS_A_4.bit. */
static const guint8 h266_vps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x71, 0x10, 0x40, 0x00, 0x4c, 0x01, 0x80,
  0x80, 0x22, 0x23, 0xc0, 0x00, 0x33, 0xc0, 0x84, 0x02, 0x10, 0x06, 0x82,
  0x01, 0xe1, 0x59
};

static const guint8 h266_sps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x79, 0x01, 0x0d, 0x22, 0x23, 0xc0, 0x00,
  0x40, 0x34, 0x40, 0xf2, 0x35, 0x00, 0x23, 0xd1, 0x1b, 0xa2, 0x11, 0xa2,
  0x14, 0x99, 0x19, 0x84, 0xd9, 0x58, 0xc1, 0x02, 0x09, 0xe0, 0x06, 0x8b,
  0x88, 0x88, 0x88, 0x97, 0xc4, 0x44, 0x4b, 0xa8, 0x88, 0x89, 0x77, 0x11,
  0x11, 0x2e, 0x48, 0x88, 0x89, 0x72, 0xc4, 0x44, 0x4b, 0x9a, 0x22, 0x22,
  0x5c, 0xf1, 0x11, 0x15, 0xbf, 0x27, 0xe5, 0xff, 0x2f, 0xea, 0x5f, 0xdc,
  0xbf, 0x92, 0x5f, 0xcb, 0x2f, 0xe6, 0x97, 0xf3, 0xcb, 0xf8, 0x89, 0x7d,
  0x44, 0x4b, 0xee, 0x22, 0x5f, 0x24, 0x44, 0xbe, 0x58, 0x89, 0x7c, 0xd1,
  0x12, 0xf9, 0xe2, 0x21, 0xa2, 0xea, 0xa1, 0xc9, 0x7d, 0x42, 0xd2, 0xea,
  0xa1, 0x69, 0x7d, 0x43, 0x12, 0xea, 0xa1, 0x89, 0x7c, 0x90, 0xc4, 0xba,
  0x92, 0x18, 0x97, 0xd4, 0x39, 0x5b, 0xf2, 0x7e, 0x5f, 0xf2, 0xfe, 0xa5,
  0xfd, 0xcb, 0xf9, 0x25, 0xff, 0x2f, 0xea, 0x5f, 0xf2, 0xfe, 0xa5, 0xff,
  0x2f, 0xea, 0x5f, 0xdc, 0xbf, 0x92, 0x5f, 0xf2, 0xfe, 0xae, 0x3f, 0xbe,
  0xbb, 0x18, 0x81
};

static const guint8 h266_pps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x81, 0x00, 0x00, 0x34, 0x40, 0xf2, 0x29,
  0x08, 0x01, 0x67, 0xb2, 0x16, 0x59, 0x62
};

static const guint8 h266_prefix_aps[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x89, 0x20, 0xd2, 0x80, 0x02, 0x88, 0x00,
  0x84, 0x80
};

static const guint8 h266_idr[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0x41, 0xc4, 0x02, 0x54, 0x03, 0xf0, 0xfc,
  0x85, 0x88, 0x65, 0x35, 0x93, 0x02, 0xab, 0xa3, 0xe2, 0xbf, 0xd5, 0x30,
  0x65, 0x5f, 0x6c, 0x93, 0xfe, 0x37, 0x2f, 0x23, 0x19, 0x6c, 0x6c, 0x64,
  0x0a, 0xfa, 0x04, 0x31, 0x0c, 0xd5, 0x0a, 0x6f, 0x70, 0x15, 0x26, 0x27,
  0xef, 0x2a, 0x32, 0x0a, 0x98, 0x08, 0xc1, 0x79, 0x83, 0xb2, 0x13, 0x99,
  0xf5, 0xfd, 0x2e, 0xeb, 0xf9, 0x44, 0xa6, 0x8a, 0xc3, 0x8e, 0x36, 0x89,
  0x06, 0x76, 0x4f, 0x0b, 0xe0, 0x81, 0x3a, 0x9b, 0xa2, 0x1a, 0x44, 0xea,
  0xff, 0x51, 0xe3, 0x98, 0x4b, 0x88, 0xb9, 0x38, 0x2a, 0xbd, 0x76, 0x4c,
  0x69, 0x52, 0x5a, 0x07, 0x23, 0xb0, 0xa8, 0xc2, 0x25, 0xc6, 0x94, 0x95,
  0x94, 0x80, 0xb7, 0x2e, 0x05, 0x2b, 0x36, 0x68, 0x5f, 0x12, 0x27, 0xac,
  0x9c, 0xa8, 0xe2, 0xc5, 0x16, 0x6c, 0x02, 0xd6, 0x78, 0x98, 0x71, 0x2d,
  0x3a, 0x62, 0x4c, 0x51, 0x8e, 0x5c, 0x4a, 0xfd, 0xc1, 0xeb, 0x47, 0x04,
  0xee, 0xed, 0x48, 0x3e, 0xd4, 0xc6, 0xc3, 0x04, 0xb7, 0xd6, 0x20, 0x97,
  0xe4, 0xd2, 0x5e, 0x09, 0x13, 0x57, 0xac, 0xf7, 0x66, 0xef, 0x95, 0x77,
  0x36, 0x80, 0x24, 0x51, 0xff, 0xf1, 0xa5, 0xab, 0x02, 0x01, 0xfc, 0xba,
  0xfd, 0x39, 0xb0, 0x41, 0xf0, 0x40, 0xb6, 0xb6, 0x9b, 0xde, 0x01, 0xa7,
  0xc5, 0xa4, 0x07, 0xfd, 0x85, 0x9c, 0x0b, 0xcd, 0xb0, 0x7d, 0x62, 0x75,
  0x43, 0x87, 0x37, 0xac, 0xc6, 0xbc, 0x6a, 0xbe, 0x89, 0x65, 0xd7, 0x05,
  0xde, 0x1c, 0xa3, 0x4d, 0xf7, 0x2e, 0x0a, 0xdb, 0x77, 0x46, 0x09, 0x30,
  0x04, 0x76, 0x41, 0x6d, 0x9b, 0xac, 0xd2, 0x44, 0x73, 0xcb, 0xc1, 0x1f,
  0x7f, 0x18, 0x70, 0x26, 0x53, 0xe6, 0xf2, 0xc2, 0xfa, 0x22, 0xff, 0x5e,
  0x75, 0x63, 0x14, 0x12, 0x2c, 0x08, 0x11, 0x20, 0xd2, 0x26, 0x2d, 0x34,
  0xa8, 0x10, 0xda, 0x11, 0x4c, 0x16, 0xa5, 0x48, 0xce, 0x84, 0xb5, 0x41,
  0xdd, 0x17, 0xea, 0xd0, 0x07, 0x1b, 0xe1, 0x6d, 0x46, 0x11, 0xbe, 0x1e,
  0xbd, 0x13, 0x64, 0x29, 0x8d, 0xeb, 0x4a, 0x44, 0x2f, 0xda, 0x85, 0x99,
  0x3d, 0x54, 0x08, 0x14, 0xd5, 0x0b, 0x99, 0xc8, 0x1f, 0xc4, 0x34, 0xe8,
  0xdc, 0x1e, 0x70, 0x1b, 0xd4, 0x59, 0xb8, 0x2c, 0x63, 0x8b, 0x8a, 0xc4,
  0x46, 0x5f, 0x0c, 0xd1, 0x0e, 0x53, 0x1a, 0x56, 0x4a, 0x9a, 0x18, 0x83,
  0x85, 0x34, 0xbd, 0xde, 0xbf, 0x27, 0xb7, 0xf3, 0xda, 0x76, 0xc4, 0xb5,
  0xa0, 0xcd, 0x5f, 0x1d, 0xb7, 0x1c, 0x5f, 0xfd, 0x0a, 0x00, 0x45, 0xcd,
  0x3e, 0x59, 0x23, 0x6c, 0x40, 0x53, 0x39, 0x9c, 0xd9, 0x76, 0x24, 0xfb,
  0x75, 0x18, 0xc7, 0xf8, 0x65, 0x0f, 0xbb, 0xa7, 0xf5, 0xb3, 0x52, 0xfa,
  0x9d, 0xe7, 0x61, 0xbf, 0xbd, 0x7d, 0xa6, 0xe1, 0x3e, 0x82, 0x94, 0x54,
  0x82, 0x64, 0x5b, 0xf3, 0x59, 0x05, 0x3d, 0x0e, 0x04, 0xcc, 0xc3, 0xca,
  0x63, 0xb8, 0xd0, 0x27, 0x1b, 0x0d, 0xad, 0xc1, 0x6f, 0x04, 0x89, 0x57,
  0xef, 0xd1, 0x62, 0xfe, 0xe8, 0x40, 0xc6, 0xe2, 0x22, 0x1b, 0x8b, 0x2e,
  0xee, 0x0d, 0x32, 0x5c, 0x14, 0x82, 0x68, 0x23, 0x75, 0xbb, 0x68, 0x86,
  0x25, 0x59, 0x65, 0x1f, 0x93, 0xd9, 0xf8, 0xfa, 0xe9, 0x82, 0x16, 0xd8,
  0xa3, 0x9c, 0xb4, 0x23, 0x5b, 0x8b, 0x3f, 0x65, 0xfc, 0x9e, 0xe3, 0xcf,
  0x22, 0x01, 0x8c, 0xfd, 0x3d, 0x75, 0x9d, 0xbd, 0x72, 0x22, 0x0b, 0x0a,
  0x9b, 0x08, 0x86, 0xf7, 0xd4, 0xd5, 0x7a, 0xea, 0x70, 0x1e, 0xc5, 0x48,
  0x25, 0x84, 0xec, 0xdd, 0x8a, 0xc8, 0xa3, 0xa9, 0x47, 0x5e, 0x7e, 0x7d,
  0x81, 0x91, 0x2d, 0x51, 0x2d, 0x9d, 0x00, 0x2b, 0xf9, 0xc6, 0x11, 0x09,
  0xe9, 0x70, 0x37, 0xba, 0x54, 0x90, 0x1d, 0x67, 0x7a, 0x9f, 0x8e, 0x22,
  0xe9, 0xc6, 0x1d, 0xec, 0x9d, 0x64, 0xc7, 0x84, 0xcf, 0xe0, 0xe5, 0xdf,
  0xaf, 0xd6, 0xab, 0xb6, 0xf7, 0xe8, 0x5d, 0x51, 0x2d, 0x99, 0x2a, 0x8d,
  0x40, 0x65, 0x7f, 0x1f, 0xe8, 0xf6, 0x54, 0x2a, 0xfe, 0x6e, 0xc0, 0xa1,
  0x1d, 0x98, 0x22, 0xeb, 0x7f, 0x12, 0x98, 0xa9, 0x3b, 0xec, 0xae, 0xbf,
  0x9f, 0xe1, 0x24, 0xa2, 0xeb, 0xd9, 0x9d, 0xfe, 0x50, 0x38, 0x02, 0x13,
  0x62, 0x8d, 0x89, 0x85, 0x52, 0x28, 0xee, 0x8a, 0x12, 0x8d, 0x92, 0xba,
  0xfb, 0x55, 0x0c, 0xf4, 0x07, 0x32, 0x54, 0x7e, 0x1e, 0xed, 0x83, 0x8e,
  0x64, 0x4a, 0xdc, 0x53, 0x6f, 0x28, 0x68, 0x40, 0xa0, 0x71, 0x5c, 0x58,
  0x28, 0x66, 0x13, 0x86, 0xb5, 0x98, 0x41, 0xcf, 0x5a, 0xd6, 0x5c, 0x37,
  0x18, 0x9e, 0xbe, 0xd6, 0x9b, 0x4b, 0xf6, 0xdf, 0x86, 0xe5, 0x64, 0x78,
  0x0e, 0xd1, 0x44, 0x94, 0x88, 0x44, 0xbd, 0x62, 0x95, 0x01, 0x95, 0x43,
  0x90, 0xad, 0x34, 0x07, 0x7c, 0x86, 0xb2, 0x6a, 0x27, 0xff, 0xd0, 0x88,
  0x2f, 0x60, 0x92, 0xa3, 0x3d, 0x66, 0xa6, 0xf3, 0x65, 0x7c, 0xac, 0x49,
  0x5e, 0xdb, 0xfb, 0xea, 0xc2, 0x02, 0xab, 0x18, 0xb4, 0x99, 0x35, 0x8d,
  0x15, 0x46, 0x8a, 0x57, 0x85, 0xa8, 0x17, 0x6a, 0x4b, 0xbd, 0x3c, 0xfa,
  0xc4, 0xe6, 0x4c, 0xba, 0x3a, 0x77, 0x84, 0xe8, 0xf9, 0xbe, 0xdd, 0x5f,
  0x18, 0xf7, 0x37, 0xd3, 0x6e, 0xf3, 0xfa, 0x9a, 0x4b, 0x83, 0xb0, 0x6d,
  0xdd, 0xce, 0xf5, 0x33, 0x3b, 0xd2, 0x08, 0x00, 0x51, 0x97, 0xe8, 0xf3,
  0x69, 0x89, 0xf2, 0xc5, 0xdd, 0x84, 0x5c, 0x0c, 0x78, 0xbc, 0x65, 0x14,
  0x4f, 0x4d, 0xec, 0xf5, 0xe1, 0xf2, 0x4b, 0x59, 0xb1, 0xcb, 0xb2, 0xd6,
  0x05, 0x90, 0xe7, 0x0e, 0x2a, 0x88, 0x02, 0x87, 0x10, 0xb4, 0x84, 0x34,
  0x79, 0x75, 0x07, 0x32, 0xb0, 0x50, 0x24, 0x02, 0xfe, 0xc2, 0x2d, 0x82,
  0x90, 0x31, 0x32, 0x27, 0x0c, 0xac, 0xe6, 0xa8, 0xe5, 0xe7, 0x96, 0xc6,
  0xb6, 0xd8, 0x8f, 0xd4, 0x63, 0x4a, 0xb7, 0xdc, 0x50, 0x13, 0x50, 0x27,
  0x64, 0x87, 0xf6, 0x9e, 0x35, 0x78, 0xe2, 0xc6, 0x6c, 0xf5, 0xf5, 0x91,
  0xdb, 0x37, 0x13, 0x63, 0x4d, 0xd5, 0x5b, 0xfd, 0x87, 0x8c, 0x8c, 0x14,
  0x46, 0x77, 0xfa, 0x92, 0x16, 0x8f, 0x04, 0x2e, 0xa0, 0x74, 0xa5, 0xb7,
  0xfb, 0x2b, 0x2a, 0xee, 0x4a, 0xea, 0x26, 0x11, 0x06, 0xba, 0xeb, 0x77,
  0xe3, 0xf1, 0xfd, 0x61, 0x8e, 0x75, 0x15, 0xf0, 0xf4, 0x99, 0xf8, 0xec,
  0xf2, 0xd9, 0xe3, 0x05, 0xe7, 0x3c, 0x3b, 0xee, 0xc4, 0x85, 0x09, 0xb9,
  0xd9, 0x55, 0x48, 0x3f, 0xb7, 0xe6, 0x26, 0x1f, 0x68, 0x58, 0x38, 0xfe,
  0x60, 0x68, 0xd3, 0x73, 0xd4, 0x6a, 0x13, 0xc5, 0x9e, 0x65, 0x72, 0xbd,
  0xb4, 0x37, 0xf9, 0x66, 0x44, 0x28, 0x1c, 0x6b, 0xef, 0xcc, 0x85, 0x6e,
  0x31, 0x66, 0xc1, 0x0f, 0x2d, 0x14, 0xbc, 0xcd, 0x4f, 0x50, 0xad, 0xc4,
  0xfd, 0x41, 0xf4, 0x13, 0xe7, 0x34, 0x67, 0x79, 0xa5, 0x76, 0x10, 0x1b,
  0x57, 0xd3, 0xc2, 0x37, 0xc8, 0x9a, 0x5f, 0x34, 0xbf, 0xa0, 0xa9, 0x70,
  0x85, 0x5a, 0xa7, 0x5f, 0xc2, 0xf3, 0x77, 0x52, 0xbd, 0x17, 0x4d, 0x44,
  0x67, 0xde, 0xdb, 0xa7, 0x78, 0x02, 0x27, 0xe3, 0x01, 0xfb, 0x22, 0xcc,
  0x26, 0xe3, 0xd7, 0xa4, 0x7a, 0x05, 0x14, 0x76, 0xe0, 0x3d, 0x28, 0x94,
  0x49, 0x6c, 0xc5, 0xe2, 0x46, 0x0f, 0x74, 0x9a, 0x86, 0x4c, 0xa8, 0x18,
  0xfe, 0xcf, 0xd4, 0x8f, 0x76, 0x49, 0xb6, 0xc3, 0x72, 0x71, 0xbd, 0xd7,
  0xb2, 0xe5, 0x5f, 0xad, 0x6b, 0xc4, 0x5a, 0xbb, 0x43, 0xbd, 0x0a, 0xc5,
  0x64, 0x6b, 0x9b, 0xbd, 0x7b, 0x37, 0x59, 0x92, 0x92, 0xfa, 0xc8, 0x59,
  0xfa, 0x8b, 0xa0, 0xf5, 0xd7, 0x8f, 0x2b, 0x9d, 0x4f, 0x32, 0x67, 0x4f,
  0xfa, 0x62, 0x31, 0x85, 0x04, 0x59, 0x7a, 0x6f, 0xa9, 0x45, 0xbf, 0xb6,
  0x8b, 0xc7, 0x77, 0x62, 0xed, 0x2d, 0x44, 0xdf, 0x90, 0x32, 0x1d, 0xd4,
  0x77, 0xd2, 0x39, 0xd2, 0x59, 0xc7, 0x81, 0x8b, 0x73, 0x8f, 0xcc, 0x38,
  0x0e, 0xf6, 0xcd, 0x75, 0x39, 0xb3, 0xc8, 0x60, 0x23, 0xf8, 0x9a, 0xda,
  0xad, 0xad, 0x82, 0x14, 0x71, 0xa5, 0x37, 0x88, 0x91, 0x0c
};

static const guint8 h266_suffix_sei[] = {
  0x00, 0x00, 0x00, 0x01, 0x00, 0xc1, 0x84, 0x32, 0x00, 0x00, 0x5b, 0x2b,
  0xe9, 0x56, 0x1e, 0x7f, 0xc7, 0x4e, 0x8b, 0xbe, 0xd4, 0xa1, 0xca, 0x83,
  0x27, 0xbe, 0xb8, 0xc3, 0x79, 0xc7, 0xd5, 0xbe, 0x9c, 0x72, 0x08, 0x20,
  0xab, 0x90, 0xbf, 0x55, 0x11, 0x57, 0xbd, 0xa0, 0x97, 0x11, 0xef, 0x0f,
  0xf7, 0x77, 0xd5, 0xa4, 0x13, 0x30, 0x2c, 0x10, 0xb5, 0xf0, 0x80
};

/* A single access unit comprising of VPS, SPS, PPS, APS and IDR frame */
static gboolean
verify_buffer_bs_au (buffer_verify_data_s * vdata, GstBuffer * buffer)
{
  GstMapInfo map;

  fail_unless (ctx_sink_template == &sinktemplate_bs_au);

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  fail_unless (map.size > 4);

  if (vdata->buffer_counter == 0) {
    guint8 *data = map.data;

    /* VPS, SPS, PPS */
    fail_unless (map.size == vdata->data_to_verify_size + ctx_headers[0].size +
        ctx_headers[1].size + ctx_headers[2].size + ctx_headers[3].size);

    fail_unless (memcmp (data, ctx_headers[0].data, ctx_headers[0].size) == 0);
    data += ctx_headers[0].size;
    fail_unless (memcmp (data, ctx_headers[1].data, ctx_headers[1].size) == 0);
    data += ctx_headers[1].size;
    fail_unless (memcmp (data, ctx_headers[2].data, ctx_headers[2].size) == 0);
    data += ctx_headers[2].size;
    fail_unless (memcmp (data, ctx_headers[3].data, ctx_headers[3].size) == 0);
    data += ctx_headers[3].size;

    /* IDR frame */
    fail_unless (memcmp (data, vdata->data_to_verify,
            vdata->data_to_verify_size) == 0);
  } else {
    /* IDR frame */
    fail_unless (map.size == vdata->data_to_verify_size);

    fail_unless (memcmp (map.data, vdata->data_to_verify, map.size) == 0);
  }

  gst_buffer_unmap (buffer, &map);
  return TRUE;
}

GST_START_TEST (test_parse_normal)
{
  gst_parser_test_normal (h266_idr, sizeof (h266_idr));
}

GST_END_TEST;

GST_START_TEST (test_parse_drain_single)
{
  gst_parser_test_drain_single (h266_idr, sizeof (h266_idr));
}

GST_END_TEST;

GST_START_TEST (test_parse_split)
{
  gst_parser_test_split (h266_idr, sizeof (h266_idr));
}

GST_END_TEST;

GST_START_TEST (test_parse_detect_stream)
{
  GstCaps *caps;
  GstStructure *s;

  caps = gst_parser_test_get_output_caps (h266_idr, sizeof (h266_idr), NULL);
  fail_unless (caps != NULL);

  /* Check that the negotiated caps are as expected */
  GST_DEBUG ("output caps: %" GST_PTR_FORMAT, caps);
  s = gst_caps_get_structure (caps, 0);
  fail_unless (gst_structure_has_name (s, "video/x-h266"));
  fail_unless_structure_field_int_equals (s, "width", 208);
  fail_unless_structure_field_int_equals (s, "height", 120);
  fail_unless_structure_field_string_equals (s, "stream-format", "byte-stream");
  fail_unless_structure_field_string_equals (s, "alignment", "au");
  fail_unless_structure_field_string_equals (s, "profile",
      "multilayer-main-10");
  fail_unless_structure_field_string_equals (s, "tier", "main");
  fail_unless_structure_field_string_equals (s, "level", "2.1");
  gst_caps_unref (caps);
}

GST_END_TEST;

static Suite *
h266parse_suite (void)
{
  Suite *s = suite_create (ctx_suite);
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_parse_normal);
  tcase_add_test (tc_chain, test_parse_drain_single);
  tcase_add_test (tc_chain, test_parse_split);
  tcase_add_test (tc_chain, test_parse_detect_stream);

  return s;
}

/* helper methods for GstHasness based tests */
static inline GstBuffer *
wrap_buffer (const guint8 * buf, gsize size, GstClockTime pts,
    GstBufferFlags flags)
{
  GstBuffer *buffer;

  buffer = gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY,
      (gpointer) buf, size, 0, size, NULL, NULL);
  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_FLAGS (buffer) |= flags;

  return buffer;
}

static inline GstBuffer *
composite_buffer (GstClockTime pts, GstBufferFlags flags, gint count, ...)
{
  va_list vl;
  gint i;
  const guint8 *data;
  gsize size;
  GstBuffer *buffer;

  va_start (vl, count);

  buffer = gst_buffer_new ();
  for (i = 0; i < count; i++) {
    data = va_arg (vl, guint8 *);
    size = va_arg (vl, gsize);

    buffer = gst_buffer_append (buffer, wrap_buffer (data, size, 0, 0));
  }
  GST_BUFFER_PTS (buffer) = pts;
  GST_BUFFER_FLAGS (buffer) |= flags;

  va_end (vl);

  return buffer;
}

#define pull_and_check_full(h, data, size, pts, flags) \
{ \
  GstBuffer *b = gst_harness_pull (h); \
  gst_check_buffer_data (b, data, size); \
  fail_unless_equals_clocktime (GST_BUFFER_PTS (b), pts); \
  if (flags) \
    fail_unless (GST_BUFFER_FLAG_IS_SET (b, flags)); \
  gst_buffer_unref (b); \
}

#define pull_and_check(h, data, pts, flags) \
  pull_and_check_full (h, data, sizeof (data), pts, flags)

#define pull_and_drop(h) \
  G_STMT_START { \
    GstBuffer *b = gst_harness_pull (h); \
    gst_buffer_unref (b); \
  } G_STMT_END

#define pull_and_check_composite(h, pts, flags, ...) \
  G_STMT_START { \
    GstMapInfo info; \
    GstBuffer *cb; \
    \
    cb = composite_buffer (0, 0, __VA_ARGS__); \
    gst_buffer_map (cb, &info, GST_MAP_READ); \
    \
    pull_and_check_full (h, info.data, info.size, pts, flags); \
    \
    gst_buffer_unmap (cb, &info); \
    gst_buffer_unref (cb); \
  } G_STMT_END


static inline void
bytestream_push_all_nals (GstHarness * h)
{
  GstBuffer *buf;

  buf = wrap_buffer (h266_vps, sizeof (h266_vps), 10, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  buf = wrap_buffer (h266_sps, sizeof (h266_sps), 10, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  buf = wrap_buffer (h266_pps, sizeof (h266_pps), 10, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  buf = wrap_buffer (h266_prefix_aps, sizeof (h266_prefix_aps), 10, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 10, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  buf = wrap_buffer (h266_suffix_sei, sizeof (h266_suffix_sei), 10, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
}

static inline void
bytestream_push_all_nals_as_au (GstHarness * h)
{
  GstBuffer *buf;

  buf = composite_buffer (10, 0, 6, h266_vps, sizeof (h266_vps),
      h266_sps, sizeof (h266_sps), h266_pps, sizeof (h266_pps),
      h266_prefix_aps, sizeof (h266_prefix_aps), h266_idr, sizeof (h266_idr),
      h266_suffix_sei, sizeof (h266_suffix_sei));
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
}

#define bytestream_set_caps(h, in_align, out_align) \
  gst_harness_set_caps_str (h, \
      "video/x-h266, parsed=(boolean)false, stream-format=byte-stream, alignment=" in_align ", framerate=30/1", \
      "video/x-h266, parsed=(boolean)true, stream-format=byte-stream, alignment=" out_align)

static void
test_headers_outalign_nal (GstHarness * h)
{
  /* VPS + SPS + PPS + APS + slice + SEI */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 6);

  /* parser must have inserted AUD before the headers, with the same PTS */
  pull_and_check (h, h266_vps, 10, 0);
  pull_and_check (h, h266_sps, 10, 0);
  pull_and_check (h, h266_pps, 10, 0);

  /* FIXME The timestamp should be 10 really, but base parse refuse to repeat
   * the same TS for two consecutive calls to _finish_frame(), see [0] for
   * more details. It's not a huge issue, the decoder can fix it for now.
   *
   * [0] https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/287
   */
  pull_and_check (h, h266_prefix_aps, -1, 0);
  pull_and_check (h, h266_idr, -1, 0);
  pull_and_check (h, h266_suffix_sei, -1, 0);
}

static void
test_flow_outalign_nal (GstHarness * h)
{
  GstBuffer *buf;

  /* drop the first AU - tested separately */
  fail_unless (gst_harness_buffers_in_queue (h) > 0);
  while (gst_harness_buffers_in_queue (h) > 0)
    pull_and_drop (h);

  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 100, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check (h, h266_idr, 100, 0);

  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 200, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check (h, h266_idr, 200, 0);
}

GST_START_TEST (test_headers_nal_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "nal", "nal");
  bytestream_push_all_nals (h);
  test_headers_outalign_nal (h);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_headers_au_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "au", "nal");
  bytestream_push_all_nals_as_au (h);
  test_headers_outalign_nal (h);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_headers_au_au)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "au", "au");
  bytestream_push_all_nals_as_au (h);

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check_composite (h, 10, 0, 6, h266_vps, sizeof (h266_vps),
      h266_sps, sizeof (h266_sps), h266_pps, sizeof (h266_pps),
      h266_prefix_aps, sizeof (h266_prefix_aps), h266_idr, sizeof (h266_idr),
      h266_suffix_sei, sizeof (h266_suffix_sei));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_flow_nal_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "nal", "nal");
  bytestream_push_all_nals (h);
  test_flow_outalign_nal (h);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_flow_au_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "au", "nal");
  bytestream_push_all_nals_as_au (h);
  test_flow_outalign_nal (h);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_flow_nal_au)
{
  GstHarness *h = gst_harness_new ("h266parse");
  GstBuffer *buf;

  bytestream_set_caps (h, "nal", "au");
  bytestream_push_all_nals (h);

  /* special case because we have latency */

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 100, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  /* drop the first AU - tested separately */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_drop (h);

  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 200, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check (h, h266_idr, 100, 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_flow_au_au)
{
  GstHarness *h = gst_harness_new ("h266parse");
  GstBuffer *buf;

  bytestream_set_caps (h, "au", "au");
  bytestream_push_all_nals_as_au (h);

  /* drop the first AU - tested separately */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_drop (h);

  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 100, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check (h, h266_idr, 100, 0);

  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 200, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check (h, h266_idr, 200, 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_latency_nal_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "nal", "nal");
  bytestream_push_all_nals (h);

  fail_unless_equals_clocktime (gst_harness_query_latency (h), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_latency_au_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "au", "nal");
  bytestream_push_all_nals_as_au (h);

  fail_unless_equals_clocktime (gst_harness_query_latency (h), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_latency_nal_au)
{
  GstHarness *h = gst_harness_new ("h266parse");
  GstBuffer *buf;

  bytestream_set_caps (h, "nal", "au");
  bytestream_push_all_nals (h);

  /* special case because we have latency;
   * the first buffer needs to be pushed out
   * before we can correctly query the latency */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);
  buf = wrap_buffer (h266_idr, sizeof (h266_idr), 100, 0);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);

  /* our input caps declare framerate=30fps, so the latency must be 1/30 sec */
  fail_unless_equals_clocktime (gst_harness_query_latency (h),
      gst_util_uint64_scale (GST_SECOND, 1, 30));

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_latency_au_au)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "au", "au");
  bytestream_push_all_nals_as_au (h);

  fail_unless_equals_clocktime (gst_harness_query_latency (h), 0);

  gst_harness_teardown (h);
}

GST_END_TEST;

static void
test_discont_outalign_nal (GstHarness * h)
{
  GstBuffer *buf;

  /* drop the first AU - tested separately */
  fail_unless (gst_harness_buffers_in_queue (h) > 0);
  while (gst_harness_buffers_in_queue (h) > 0)
    pull_and_drop (h);

  buf =
      wrap_buffer (h266_idr, sizeof (h266_idr), 1000, GST_BUFFER_FLAG_DISCONT);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check (h, h266_idr, 1000, GST_BUFFER_FLAG_DISCONT);
}

GST_START_TEST (test_discont_nal_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "nal", "nal");
  bytestream_push_all_nals (h);
  test_discont_outalign_nal (h);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_discont_au_nal)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "au", "nal");
  bytestream_push_all_nals_as_au (h);
  test_discont_outalign_nal (h);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_discont_au_au)
{
  GstHarness *h = gst_harness_new ("h266parse");
  GstBuffer *buf;

  bytestream_set_caps (h, "au", "au");
  bytestream_push_all_nals_as_au (h);

  /* drop the first AU - tested separately */
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_drop (h);

  buf =
      wrap_buffer (h266_idr, sizeof (h266_idr), 1000, GST_BUFFER_FLAG_DISCONT);
  fail_unless_equals_int (gst_harness_push (h, buf), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_check (h, h266_idr, 1000, GST_BUFFER_FLAG_DISCONT);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_parse_skip_to_4bytes_sc)
{
  GstHarness *h;
  GstBuffer *buf1, *buf2;
  const guint8 initial_bytes[] = { 0x00, 0x00, 0x00, 0x00, 0x01, h266_vps[4] };
  GstMapInfo map;

  h = gst_harness_new ("h266parse");

  gst_harness_set_caps_str (h, "video/x-h266, stream-format=byte-stream",
      "video/x-h266, stream-format=byte-stream, alignment=nal");

  /* padding bytes, four bytes start code and 1 of the two identification
   * bytes. */
  buf1 = wrap_buffer (initial_bytes, sizeof (initial_bytes), 100, 0);

  /* The second contains the an VPS, starting from second NAL identification
   * byte and is followed by an SPS, IDR to ensure that the NAL end can be
   * found */
  buf2 = composite_buffer (100, 0, 6, h266_vps + 5, sizeof (h266_vps) - 5,
      h266_sps, sizeof (h266_sps), h266_pps, sizeof (h266_pps),
      h266_prefix_aps, sizeof (h266_prefix_aps), h266_idr, sizeof (h266_idr),
      h266_suffix_sei, sizeof (h266_suffix_sei));

  fail_unless_equals_int (gst_harness_push (h, buf1), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  fail_unless_equals_int (gst_harness_push (h, buf2), GST_FLOW_OK);
  fail_unless (gst_harness_buffers_in_queue (h) >= 5);

  buf1 = gst_harness_pull (h);
  gst_buffer_map (buf1, &map, GST_MAP_READ);
  fail_unless_equals_int (gst_buffer_get_size (buf1), sizeof (h266_vps));
  gst_buffer_unmap (buf1, &map);
  gst_buffer_unref (buf1);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_parse_sc_with_half_nal)
{
  GstHarness *h;
  GstBuffer *buf1, *buf2;
  GstMapInfo map;

  h = gst_harness_new ("h266parse");

  gst_harness_set_caps_str (h, "video/x-h266, stream-format=byte-stream",
      "video/x-h266, stream-format=byte-stream, alignment=nal");

  buf1 = composite_buffer (100, 0, 5, h266_vps, sizeof (h266_vps),
      h266_sps, sizeof (h266_sps), h266_pps, sizeof (h266_pps),
      h266_prefix_aps, sizeof (h266_prefix_aps), h266_idr, 20);
  buf2 = composite_buffer (100, 0, 2, h266_idr + 20, sizeof (h266_idr) - 20,
      h266_suffix_sei, sizeof (h266_suffix_sei));

  fail_unless_equals_int (gst_harness_push (h, buf1), GST_FLOW_OK);
  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 4);

  fail_unless_equals_int (gst_harness_push (h, buf2), GST_FLOW_OK);
  fail_unless (gst_harness_buffers_in_queue (h) >= 5);

  buf1 = gst_harness_pull (h);
  gst_buffer_map (buf1, &map, GST_MAP_READ);
  fail_unless_equals_int (gst_buffer_get_size (buf1), sizeof (h266_vps));
  gst_buffer_unmap (buf1, &map);
  gst_buffer_unref (buf1);

  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_drain)
{
  GstHarness *h = gst_harness_new ("h266parse");

  bytestream_set_caps (h, "nal", "au");
  bytestream_push_all_nals (h);

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 0);

  gst_harness_push_event (h, gst_event_new_eos ());

  fail_unless_equals_int (gst_harness_buffers_in_queue (h), 1);
  pull_and_drop (h);

  gst_harness_teardown (h);
}

GST_END_TEST;

static Suite *
h266parse_harnessed_suite (void)
{
  Suite *s = suite_create ("h266parse");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_headers_nal_nal);
  tcase_add_test (tc_chain, test_headers_au_nal);
  tcase_add_test (tc_chain, test_headers_au_au);

  tcase_add_test (tc_chain, test_flow_nal_nal);
  tcase_add_test (tc_chain, test_flow_au_nal);
  tcase_add_test (tc_chain, test_flow_nal_au);
  tcase_add_test (tc_chain, test_flow_au_au);

  tcase_add_test (tc_chain, test_latency_nal_nal);
  tcase_add_test (tc_chain, test_latency_au_nal);
  tcase_add_test (tc_chain, test_latency_nal_au);
  tcase_add_test (tc_chain, test_latency_au_au);

  tcase_add_test (tc_chain, test_discont_nal_nal);
  tcase_add_test (tc_chain, test_discont_au_nal);
  tcase_add_test (tc_chain, test_discont_au_au);

  tcase_add_test (tc_chain, test_parse_skip_to_4bytes_sc);
  tcase_add_test (tc_chain, test_parse_sc_with_half_nal);

  tcase_add_test (tc_chain, test_drain);

  return s;
}

int
main (int argc, char **argv)
{
  int nf = 0;

  Suite *s;

  gst_check_init (&argc, &argv);

  /* init test context */
  ctx_factory = "h266parse";
  ctx_sink_template = &sinktemplate_bs_au;
  ctx_src_template = &srctemplate;
  /* no timing info to parse */
  ctx_headers[0].data = h266_vps;
  ctx_headers[0].size = sizeof (h266_vps);
  ctx_headers[1].data = h266_sps;
  ctx_headers[1].size = sizeof (h266_sps);
  ctx_headers[2].data = h266_pps;
  ctx_headers[2].size = sizeof (h266_pps);
  ctx_headers[3].data = h266_prefix_aps;
  ctx_headers[3].size = sizeof (h266_prefix_aps);
  ctx_verify_buffer = verify_buffer_bs_au;

  /* discard initial vps/sps/pps buffers */
  ctx_discard = 0;
  /* no timing info to parse */
  ctx_no_metadata = TRUE;
  ctx_codec_data = FALSE;

  ctx_suite = "h266parse_to_bs_au";
  s = h266parse_suite ();
  nf += gst_check_run_suite (s, ctx_suite, __FILE__ "_to_bs_au.c");

  ctx_suite = "h266parse_harnessed";
  s = h266parse_harnessed_suite ();
  nf += gst_check_run_suite (s, ctx_suite, __FILE__ "_harnessed.c");

  return nf;
}
