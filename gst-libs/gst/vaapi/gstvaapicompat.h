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

/* Compatibility glue with VA-API < 0.31 */
#if !VA_CHECK_VERSION(0,31,0)
#undef  vaSyncSurface
#define vaSyncSurface(dpy, s)   (vaSyncSurface)((dpy), VA_INVALID_ID, (s))
#undef  vaPutImage
#define vaPutImage              vaPutImage2
#undef  vaAssociateSubpicture
#define vaAssociateSubpicture   vaAssociateSubpicture2
#endif

#if VA_CHECK_VERSION(1,0,0)
#define VA_ROI_RC_QP_DELTA_SUPPORT(x) x->bits.roi_rc_qp_delta_support
#define VA_ENC_PACKED_HEADER_H264_SEI VAEncPackedHeaderRawData
#else
#define VA_ROI_RC_QP_DELTA_SUPPORT(x) x->bits.roi_rc_qp_delat_support
#define VA_ENC_PACKED_HEADER_H264_SEI VAEncPackedHeaderH264_SEI
#endif

/* Compatibility glue with VA-API 0.34 */
#if VA_CHECK_VERSION(0,34,0)
# include <va/va_compat.h>
#endif

#if VA_CHECK_VERSION(0,36,0)
#include <va/va_drmcommon.h>
#endif

/* VA-API < 0.37 doesn't include sub core APIs in va.h */
#if !VA_CHECK_VERSION(0,37,0)
#ifdef HAVE_VA_VA_DEC_HEVC_H
# include <va/va_dec_hevc.h>
#endif
#ifdef HAVE_VA_VA_DEC_JPEG_H
# include <va/va_dec_jpeg.h>
#endif
#ifdef HAVE_VA_VA_DEC_VP8_H
# include <va/va_dec_vp8.h>
#endif
#ifdef HAVE_VA_VA_DEC_VP9_H
# include <va/va_dec_vp9.h>
#endif
#ifdef HAVE_VA_VA_ENC_HEVC_H
# include <va/va_enc_hevc.h>
#endif
#ifdef HAVE_VA_VA_ENC_H264_H
# include <va/va_enc_h264.h>
#endif
#ifdef HAVE_VA_VA_ENC_JPEG_H
# include <va/va_enc_jpeg.h>
#endif
#ifdef HAVE_VA_VA_ENC_MPEG2_H
# include <va/va_enc_mpeg2.h>
#endif
#ifdef HAVE_VA_VA_ENC_VP8_H
# include <va/va_enc_vp8.h>
#endif
#ifdef HAVE_VA_VA_ENC_VP9_H
# include <va/va_enc_vp9.h>
#endif
#ifdef HAVE_VA_VA_VPP_H
# include <va/va_vpp.h>
#endif
#endif

#endif /* GST_VAAPI_COMPAT_H */
