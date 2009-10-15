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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_VDP_MPEG_DEC_H__
#define __GST_VDP_MPEG_DEC_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

#include "gstvdpdevice.h"

G_BEGIN_DECLS

#define GST_TYPE_VDP_MPEG_DEC            (gst_vdp_mpeg_dec_get_type())
#define GST_VDP_MPEG_DEC(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_VDP_MPEG_DEC,GstVdpMpegDec))
#define GST_VDP_MPEG_DEC_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_VDP_MPEG_DEC,GstVdpMpegDecClass))
#define GST_IS_VDP_MPEG_DEC(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_VDP_MPEG_DEC))
#define GST_IS_VDP_MPEG_DEC_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_VDP_MPEG_DEC))

typedef enum {
  GST_VDP_MPEG_DEC_NEED_SEQUENCE,
  GST_VDP_MPEG_DEC_NEED_GOP,
  GST_VDP_MPEG_DEC_NEED_DATA
} GstVdpMpegDecState;

typedef struct _GstVdpMpegDec      GstVdpMpegDec;
typedef struct _GstVdpMpegDecClass GstVdpMpegDecClass;

struct _GstVdpMpegDec
{
  GstElement element;

  /* pads */
  GstPad *src;
  GstPad *sink;

  /* properties */
  gchar *display;
  
  gboolean yuv_output;

  GstVdpDevice *device;
  VdpDecoderProfile profile;
  VdpDecoder decoder;
  
  /* stream info */
  gint width, height;
  gint fps_n, fps_d;
  gboolean interlaced;
  gint version;

  /* decoder state */
  GstVdpMpegDecState state;
  
  /* currently decoded frame info */
  GstAdapter *adapter;
  VdpPictureInfoMPEG1Or2 vdp_info;
  guint64 frame_nr;
  GstClockTime duration;

  /* frame_nr from GOP */
  guint64 gop_frame;
  
  /* forward and backward reference */
  GstBuffer *f_buffer;
  GstBuffer *b_buffer;

  /* calculated timestamp, size and duration */
  GstClockTime next_timestamp;
  guint64 accumulated_size;
  guint64 accumulated_duration;

  /* seek data */
  GstSegment segment;
  gboolean seeking;
  gint64 byterate;

  /* mutex */
  GMutex *mutex;
  
};

struct _GstVdpMpegDecClass 
{
  GstElementClass element_class;  
};

GType gst_vdp_mpeg_dec_get_type (void);

G_END_DECLS

#endif /* __GST_VDP_MPEG_DEC_H__ */
