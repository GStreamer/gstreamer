/*
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#ifndef __GST_OMX_VP8_DEC_H__
#define __GST_OMX_VP8_DEC_H__

#include <gst/gst.h>
#include "gstomxvideodec.h"

G_BEGIN_DECLS

#define GST_TYPE_OMX_VP8_DEC \
  (gst_omx_vp8_dec_get_type())
#define GST_OMX_VP8_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_VP8_DEC,GstOMXVP8Dec))
#define GST_OMX_VP8_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_VP8_DEC,GstOMXVP8DecClass))
#define GST_OMX_VP8_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_OMX_VP8_DEC,GstOMXVP8DecClass))
#define GST_IS_OMX_VP8_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_VP8_DEC))
#define GST_IS_OMX_VP8_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_VP8_DEC))

typedef struct _GstOMXVP8Dec GstOMXVP8Dec;
typedef struct _GstOMXVP8DecClass GstOMXVP8DecClass;

struct _GstOMXVP8Dec
{
  GstOMXVideoDec parent;
};

struct _GstOMXVP8DecClass
{
  GstOMXVideoDecClass parent_class;
};

GType gst_omx_vp8_dec_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_VP8_DEC_H__ */

