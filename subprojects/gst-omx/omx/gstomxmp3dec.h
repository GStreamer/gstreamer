/*
 * Copyright (C) 2014, Sebastian Dröge <sebastian@centricular.com>
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

#ifndef __GST_OMX_MP3_DEC_H__
#define __GST_OMX_MP3_DEC_H__

#include <gst/gst.h>
#include "gstomxaudiodec.h"

G_BEGIN_DECLS

#define GST_TYPE_OMX_MP3_DEC \
  (gst_omx_mp3_dec_get_type())
#define GST_OMX_MP3_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_MP3_DEC,GstOMXMP3Dec))
#define GST_OMX_MP3_DEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_MP3_DEC,GstOMXMP3DecClass))
#define GST_OMX_MP3_DEC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_OMX_MP3_DEC,GstOMXMP3DecClass))
#define GST_IS_OMX_MP3_DEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_MP3_DEC))
#define GST_IS_OMX_MP3_DEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_MP3_DEC))

typedef struct _GstOMXMP3Dec GstOMXMP3Dec;
typedef struct _GstOMXMP3DecClass GstOMXMP3DecClass;

struct _GstOMXMP3Dec
{
  GstOMXAudioDec parent;
  gint spf;
};

struct _GstOMXMP3DecClass
{
  GstOMXAudioDecClass parent_class;
};

GType gst_omx_mp3_dec_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_MP3_DEC_H__ */

