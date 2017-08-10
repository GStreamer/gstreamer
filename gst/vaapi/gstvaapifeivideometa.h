/*
 *  gstvaapifeivideometa.h - Gstreamer/VA video meta
 *
 *  Copyright (C) 2016-2017 Intel Corporation
 *    Author: Yi A Wang <yi.a.wang@intel.com>
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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

#ifndef GST_VAAPI_FEI_VIDEO_META_H
#define GST_VAAPI_FEI_VIDEO_META_H

#include <va/va.h>
#include <gst/vaapi/gstvaapifei_objects.h>
G_BEGIN_DECLS

typedef struct _GstVaapiFeiVideoMeta GstVaapiFeiVideoMeta;
typedef struct _GstVaapiFeiVideoMetaHolder GstVaapiFeiVideoMetaHolder;

#define GST_VAAPI_FEI_VIDEO_META(obj) \
  ((GstVaapiFeiVideoMeta *) (obj))
#define GST_VAAPI_IS_FEI_VIDEO_META(obj) \
  (GST_VAAPI_FEI_VIDEO_META (obj) != NULL)

struct _GstVaapiFeiVideoMetaHolder
{
  GstMeta base;
  GstVaapiFeiVideoMeta *meta;
};

struct _GstVaapiFeiVideoMeta {
  GstVaapiEncFeiMbCode *mbcode;
  GstVaapiEncFeiMv *mv;
  GstVaapiEncFeiMvPredictor *mvpred;
  GstVaapiEncFeiMbControl *mbcntrl;
  GstVaapiEncFeiQp *qp;
  GstVaapiEncFeiDistortion *dist;

  GstBuffer *buffer;
  gint ref_count;
};

#define GST_VAAPI_FEI_VIDEO_META_API_TYPE \
  gst_vaapi_fei_video_meta_api_get_type ()

GType
gst_vaapi_fei_video_meta_api_get_type (void) G_GNUC_CONST;

GstVaapiFeiVideoMeta *
gst_vaapi_fei_video_meta_new (void);

GstVaapiFeiVideoMeta *
gst_vaapi_fei_video_meta_ref (GstVaapiFeiVideoMeta * meta);

void
gst_vaapi_fei_video_meta_unref (GstVaapiFeiVideoMeta * meta);

GstVaapiFeiVideoMeta *
gst_buffer_get_vaapi_fei_video_meta (GstBuffer * buffer);

void
gst_buffer_set_vaapi_fei_video_meta (GstBuffer * buffer, GstVaapiFeiVideoMeta * meta);

G_END_DECLS

#endif /* GST_VAAPI_FEI_VIDEO_META_H */
