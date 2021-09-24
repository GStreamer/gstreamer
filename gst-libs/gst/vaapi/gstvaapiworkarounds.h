/*
 *  gstvaapiworkaround.h - GStreamer/VA workarounds
 *
 *  Copyright (C) 2011-2012 Intel Corporation
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

#ifndef GST_VAAPI_WORKAROUNDS_H
#define GST_VAAPI_WORKAROUNDS_H

G_BEGIN_DECLS

/*
 * Workaround to expose H.263 Baseline decode profile for drivers that
 * support MPEG-4:2 Simple profile decoding.
 */
#define WORKAROUND_H263_BASELINE_DECODE_PROFILE (1)

/*
 * Workaround for qtdemux that does not report profiles for
 * video/x-h263. Assume H.263 Baseline profile in this case.
 */
#define WORKAROUND_QTDEMUX_NO_H263_PROFILES (1)

G_END_DECLS

#endif /* GST_VAAPI_WORKAROUNDS_H */
