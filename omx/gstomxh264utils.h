/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifndef __GST_OMX_H264_UTILS_H__
#define __GST_OMX_H264_UTILS_H__

#include "gstomx.h"

G_BEGIN_DECLS

OMX_VIDEO_AVCPROFILETYPE gst_omx_h264_utils_get_profile_from_str (const
    gchar * profile);
OMX_VIDEO_AVCLEVELTYPE gst_omx_h264_utils_get_level_from_str (const gchar *
    level);

const gchar * gst_omx_h264_utils_get_profile_from_enum (OMX_VIDEO_AVCPROFILETYPE e);

G_END_DECLS
#endif /* __GST_OMX_H264_UTILS_H__ */
