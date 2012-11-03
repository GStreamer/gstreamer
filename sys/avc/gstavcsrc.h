/* GStreamer
 * Copyright (C) 2011 FIXME <fixme@example.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef _GST_AVC_SRC_H_
#define _GST_AVC_SRC_H_

#include <gst/base/gstbasesrc.h>
#include <AVCVideoServices/AVCVideoServices.h>
using namespace AVS;

G_BEGIN_DECLS

#define GST_TYPE_AVC_SRC   (gst_avc_src_get_type())
#define GST_AVC_SRC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_AVC_SRC,GstAVCSrc))
#define GST_AVC_SRC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_AVC_SRC,GstAVCSrcClass))
#define GST_IS_AVC_SRC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_AVC_SRC))
#define GST_IS_AVC_SRC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_AVC_SRC))

typedef struct _GstAVCSrc GstAVCSrc;
typedef struct _GstAVCSrcClass GstAVCSrcClass;

struct _GstAVCSrc
{
  GstBaseSrc base_avcsrc;

  GstPad *srcpad;

  AVCDeviceController *pAVCDeviceController;
  AVCDevice *pAVCDevice;
  AVCDeviceStream *pAVCDeviceStream;
  int deviceIndex;

  guint64 packets_enqueued;
  guint64 packets_dequeued;

  GstAtomicQueue *queue;
  GCond *cond;
  GMutex *queue_lock;
  gboolean unlock;
};

struct _GstAVCSrcClass
{
  GstBaseSrcClass base_avcsrc_class;
};

GType gst_avc_src_get_type (void);

G_END_DECLS

#endif
