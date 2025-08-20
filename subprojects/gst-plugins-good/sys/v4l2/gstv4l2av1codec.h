/*
 * Copyright (C) 2022 Synaptics Incorporated
 *    Author: Hsia-Jun(Randy) Li <randy.li@synaptics.com>
 * Copyright (C) 2025 Qualcomm Technologies, Inc. and/or its subsidiaries.
 *    Author: Deepa Guthyappa Madivalara <deepa.madivalara@oss.qualcomm.com>
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

#ifndef __GST_V4L2_AV1_CODEC_H__
#define __GST_V4L2_AV1_CODEC_H__

#include "gstv4l2codec.h"

G_BEGIN_DECLS

const GstV4l2Codec * gst_v4l2_av1_get_codec (void);

G_END_DECLS

#endif
