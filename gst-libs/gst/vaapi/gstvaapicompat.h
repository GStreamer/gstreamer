/*
 *  gstvapicompat.h - VA-API compatibility glue
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012 Intel Corporation
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

#ifndef GST_VAAPI_COMPAT_H
#define GST_VAAPI_COMPAT_H

#include <va/va.h>

#if VA_CHECK_VERSION(1,0,0)
#define VA_ROI_RC_QP_DELTA_SUPPORT(x) x->bits.roi_rc_qp_delta_support
#define VA_ENC_PACKED_HEADER_H264_SEI VAEncPackedHeaderRawData
#else
#define VA_ROI_RC_QP_DELTA_SUPPORT(x) x->bits.roi_rc_qp_delat_support
#define VA_ENC_PACKED_HEADER_H264_SEI VAEncPackedHeaderH264_SEI
#endif

#include <va/va_compat.h>
#include <va/va_drmcommon.h>

#endif /* GST_VAAPI_COMPAT_H */
