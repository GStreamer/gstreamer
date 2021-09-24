/*
 *  gstvaapiencoder_mpeg2_priv.h - MPEG-2 encoder (private definitions)
 *
 *  Copyright (C) 2013-2014 Intel Corporation
 *    Author: Guangxin Xu <guangxin.xu@intel.com>
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

#ifndef GST_VAAPI_ENCODER_MPEG2_PRIV_H
#define GST_VAAPI_ENCODER_MPEG2_PRIV_H

#include "gstvaapiencoder_priv.h"
#include "gstvaapiutils_mpeg2.h"

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_MPEG2_CAST(encoder) \
  ((GstVaapiEncoderMpeg2 *) (encoder))

#define START_CODE_PICUTRE      0x00000100
#define START_CODE_SLICE        0x00000101
#define START_CODE_USER         0x000001B2
#define START_CODE_SEQ          0x000001B3
#define START_CODE_EXT          0x000001B5
#define START_CODE_GOP          0x000001B8

struct _GstVaapiEncoderMpeg2
{
  GstVaapiEncoder parent_instance;

  GstVaapiProfile profile;
  GstVaapiLevelMPEG2 level;
  guint8 profile_idc;
  guint8 level_idc;
  guint32 cqp; /* quantizer value for CQP mode */
  guint32 ip_period;

  /* re-ordering */
  GQueue b_frames;
  gboolean dump_frames;
  gboolean new_gop;

  /* reference list */
  GstVaapiSurfaceProxy *forward;
  GstVaapiSurfaceProxy *backward;
  guint32 frame_num;            /* same value picture header, but it's not mod by 1024 */
};

G_END_DECLS

#endif /* GST_VAAPI_ENCODER_MPEG2_PRIV_H */
