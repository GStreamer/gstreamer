/*
 *  gstvaapiencoder_mpeg2_priv.h - MPEG-2 encoder (private definitions)
 *
 *  Copyright (C) 2013 Intel Corporation
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

#include <glib.h>
#include <gst/vaapi/gstvaapiencoder.h>
#include <gst/vaapi/gstvaapiencoder_priv.h>

G_BEGIN_DECLS

#define GST_VAAPI_ENCODER_MPEG2(encoder)  \
        ((GstVaapiEncoderMpeg2 *)(encoder))
#define GST_VAAPI_ENCODER_MPEG2_CAST(encoder) \
        ((GstVaapiEncoderMpeg2 *)(encoder))

typedef enum
{
  GST_ENCODER_MPEG2_PROFILE_SIMPLE,
  GST_ENCODER_MPEG2_PROFILE_MAIN,
} GstEncoderMpeg2Level;

typedef enum
{
  GST_VAAPI_ENCODER_MPEG2_LEVEL_LOW,
  GST_VAAPI_ENCODER_MPEG2_LEVEL_MAIN,
  GST_VAAPI_ENCODER_MPEG2_LEVEL_HIGH
} GstVaapiEncoderMpeg2Level;

#define GST_VAAPI_ENCODER_MPEG2_DEFAULT_PROFILE      GST_ENCODER_MPEG2_PROFILE_MAIN
#define GST_VAAPI_ENCODER_MPEG2_DEFAULT_LEVEL        GST_VAAPI_ENCODER_MPEG2_LEVEL_HIGH

#define GST_VAAPI_ENCODER_MPEG2_MIN_CQP                 2
#define GST_VAAPI_ENCODER_MPEG2_MAX_CQP                 62
#define GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP             8

#define GST_VAAPI_ENCODER_MPEG2_MAX_GOP_SIZE            512
#define GST_VAAPI_ENCODER_MPEG2_DEFAULT_GOP_SIZE        30

#define GST_VAAPI_ENCODER_MPEG2_MAX_MAX_BFRAMES         16
#define GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES     2

#define GST_VAAPI_ENCODER_MPEG2_MAX_BITRATE             100*1024

#define START_CODE_PICUTRE      0x00000100
#define START_CODE_SLICE        0x00000101
#define START_CODE_USER         0x000001B2
#define START_CODE_SEQ          0x000001B3
#define START_CODE_EXT          0x000001B5
#define START_CODE_GOP          0x000001B8

#define CHROMA_FORMAT_RESERVED  0
#define CHROMA_FORMAT_420       1
#define CHROMA_FORMAT_422       2
#define CHROMA_FORMAT_444       3

struct _GstVaapiEncoderMpeg2
{
  GstVaapiEncoder parent;

  /* public */
  guint32 profile;
  guint32 level;
  guint32 bitrate;              /*kbps */
  guint32 cqp;
  guint32 intra_period;
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
