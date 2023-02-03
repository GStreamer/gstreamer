/*
* Copyright(c) 2019 Intel Corporation
*     Authors: Jun Tian <jun.tian@intel.com> Xavier Hallade <xavier.hallade@intel.com>
* SPDX - License - Identifier: LGPL-2.1-or-later
*/

#ifndef _GST_SVTAV1ENC_H_
#define _GST_SVTAV1ENC_H_

#include <string.h>
#include <gst/video/video.h>
#include <gst/video/gstvideoencoder.h>

#include <EbSvtAv1.h>
#include <EbSvtAv1Enc.h>

G_BEGIN_DECLS

#define GST_TYPE_SVTAV1ENC (gst_svtav1enc_get_type())

G_DECLARE_FINAL_TYPE (GstSvtAv1Enc, gst_svtav1enc, GST, SVTAV1ENC, GstVideoEncoder);

GST_ELEMENT_REGISTER_DECLARE (svtav1enc);

G_END_DECLS
#endif
