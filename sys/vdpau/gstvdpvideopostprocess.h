/*
 * GStreamer
 * Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
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

#ifndef __GST_VDP_VIDEO_POST_PROCESS_H__
#define __GST_VDP_VIDEO_POST_PROCESS_H__

#include <gst/gst.h>

#include "gstvdpdevice.h"
#include "gstvdpvideobufferpool.h"

G_BEGIN_DECLS

#define MAX_PICTURES 6

typedef struct _GstVdpPicture GstVdpPicture;

struct _GstVdpPicture
{
  GstBuffer *buf;
  VdpVideoMixerPictureStructure structure;
  GstClockTime timestamp;
};

typedef enum
{
  GST_VDP_DEINTERLACE_MODE_AUTO,
  GST_VDP_DEINTERLACE_MODE_INTERLACED,
  GST_VDP_DEINTERLACE_MODE_DISABLED
} GstVdpDeinterlaceModes;

typedef enum
{
  GST_VDP_DEINTERLACE_METHOD_BOB,
  GST_VDP_DEINTERLACE_METHOD_TEMPORAL,
  GST_VDP_DEINTERLACE_METHOD_TEMPORAL_SPATIAL
} GstVdpDeinterlaceMethods;

#define GST_TYPE_VDP_VIDEO_POST_PROCESS            (gst_vdp_vpp_get_type())
#define GST_VDP_VIDEO_POST_PROCESS(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDP_VIDEO_POST_PROCESS,GstVdpVideoPostProcess))
#define GST_VDP_VIDEO_POST_PROCESS_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDP_VIDEO_POST_PROCESS,GstVdpVideoPostProcessClass))
#define GST_IS_VDP_VIDEO_POST_PROCESS(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDP_VIDEO_POST_PROCESS))
#define GST_IS_VDP_VIDEO_POST_PROCESS_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDP_VIDEO_POST_PROCESS))

typedef struct _GstVdpVideoPostProcess      GstVdpVideoPostProcess;
typedef struct _GstVdpVideoPostProcessClass GstVdpVideoPostProcessClass;

struct _GstVdpVideoPostProcess
{
  GstElement element;

  GstPad *sinkpad, *srcpad;
  
  gboolean native_input;
  VdpChromaType chroma_type;
  gint width, height;
  guint32 fourcc;
  GstBufferPool *vpool;

  gboolean got_par;
  gint par_n, par_d;
  
  gboolean interlaced;
  GstClockTime field_duration;

  GstSegment segment;
  GstClockTime earliest_time;
  gboolean discont;

  GstVdpDevice *device;
  VdpVideoMixer mixer;

  GstVdpPicture future_pictures[MAX_PICTURES];
  guint n_future_pictures;
  
  GstVdpPicture past_pictures[MAX_PICTURES];
  guint n_past_pictures;
  
  gboolean force_aspect_ratio;
  GstVdpDeinterlaceModes mode;
  GstVdpDeinterlaceMethods method;

  /* properties */
  gchar *display;
  gfloat noise_reduction;
  gfloat sharpening;
  gboolean inverse_telecine;
};

struct _GstVdpVideoPostProcessClass 
{
  GstElementClass element_class;  
};

GType gst_vdp_vpp_get_type (void);

G_END_DECLS

#endif /* __GST_VDP_VIDEO_POST_PROCESS_H__ */
