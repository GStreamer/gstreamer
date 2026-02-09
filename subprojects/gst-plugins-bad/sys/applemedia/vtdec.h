/* GStreamer
 * Copyright (C) 2010, 2013 Ole André Vadla Ravnås <oleavr@soundrop.com>
 * Copyright (C) 2012, 2013 Alessandro Decina <alessandro.d@gmail.com>
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

#ifndef _GST_VTDEC_H_
#define _GST_VTDEC_H_

#include <gst/base/gstqueuearray.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <TargetConditionals.h>
#include <CoreMedia/CoreMedia.h>
#include <VideoToolbox/VideoToolbox.h>
#include "videotexturecache.h"
#include "glcontexthelper.h"
#if defined(APPLEMEDIA_MOLTENVK)
#include <gst/vulkan/vulkan.h>
#endif
#include <gst/codecparsers/gsth264parser.h>
#include <gst/codecparsers/gstav1parser.h>

G_BEGIN_DECLS

#define GST_TYPE_VTDEC   (gst_vtdec_get_type())
#define GST_VTDEC(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VTDEC,GstVtdec))
#define GST_VTDEC_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VTDEC,GstVtdecClass))
#define GST_IS_VTDEC(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VTDEC))
#define GST_IS_VTDEC_CLASS(obj)   (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VTDEC))

typedef struct _GstVtdec GstVtdec;
typedef struct _GstVtdecClass GstVtdecClass;

#define GST_VTDEC_DPB_MAX_SIZE 16

typedef enum
{
    NoneSupported   = 0,
    Av1Supported    = 1 << 0,
    Vp9Supported    = 1 << 1,
} SupplementalSupport;

struct _GstVtdec
{
  GstVideoDecoder base_vtdec;
  GstVideoCodecState *input_state;
  GstVideoInfo video_info;
  CMFormatDescriptionRef format_description;
  VTDecompressionSessionRef session;
  GstVideoTextureCache *texture_cache;
  GstGLContextHelper *ctxh;

  GstVecDeque *reorder_queue;
  gint dbp_size;
  GMutex queue_mutex;
  GCond queue_cond;

  GstFlowReturn downstream_ret;
  gboolean is_flushing;
  gboolean is_draining;
  gboolean pause_task;

#if defined(APPLEMEDIA_MOLTENVK)
  GstVulkanInstance *instance;
  GstVulkanDevice *device;
#endif

  gboolean require_hardware;
  SupplementalSupport codec_support;

  gboolean av1_needs_sequence_header;  /* TRUE if we need to wait for sequence header OBU before creating session */
  GstBuffer *av1_sequence_header_obu;  /* Store the sequence header OBU for format description */

  GstBuffer *vp9_vpcc;
};

struct _GstVtdecClass
{
  GstVideoDecoderClass base_vtdec_class;
};

GType gst_vtdec_get_type (void);
GST_ELEMENT_REGISTER_DECLARE (vtdec);
#if !TARGET_OS_WATCH
GST_ELEMENT_REGISTER_DECLARE (vtdec_hw);
#endif

gboolean gst_vtdec_register_elements (GstPlugin * plugin);

G_END_DECLS

#endif
