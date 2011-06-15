/* GStreamer h264 parser
 * Copyright (C) 2005 Michal Benes <michal.benes@itonis.tv>
 *
 * gsth264parse.h:
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


#ifndef __GST_H264_PARSE_H__
#define __GST_H264_PARSE_H__

#include <gst/gst.h>
#include <gst/base/gstadapter.h>

G_BEGIN_DECLS

#define GST_TYPE_H264PARSE \
  (gst_h264_parse_get_type())
#define GST_H264PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_H264PARSE,GstH264Parse))
#define GST_H264PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_H264PARSE,GstH264ParseClass))
#define GST_IS_H264PARSE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_H264PARSE))
#define GST_IS_H264PARSE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_H264PARSE))

typedef struct _GstH264Parse GstH264Parse;
typedef struct _GstH264ParseClass GstH264ParseClass;

typedef struct _GstNalList GstNalList;

typedef struct _GstH264Sps GstH264Sps;
typedef struct _GstH264Pps GstH264Pps;

#define MAX_SPS_COUNT	32
#define MAX_PPS_COUNT   256

#define CLOCK_BASE 9LL
#define CLOCK_FREQ (CLOCK_BASE * 10000)

#define MPEGTIME_TO_GSTTIME(time) (gst_util_uint64_scale ((time), \
            GST_MSECOND/10, CLOCK_BASE))
#define GSTTIME_TO_MPEGTIME(time) (gst_util_uint64_scale ((time), \
            CLOCK_BASE, GST_MSECOND/10))

struct _GstH264Parse
{
  GstElement element;

  GstPad *sinkpad;
  GstPad *srcpad;

  gboolean split_packetized;
  gboolean merge;
  guint nal_length_size;
  guint format;

  guint interval;
  GstClockTime last_report;

  GstSegment segment;
  gboolean packetized;
  gboolean discont;

  gint width, height;
  gint fps_num, fps_den;

  /* gather/decode queues for reverse playback */
  GList *gather;
  GstBuffer *prev;
  GstNalList *decode;
  gint decode_len;
  gboolean have_sps;
  gboolean have_pps;
  gboolean have_i_frame;

  GstAdapter *adapter;

  /* SPS: sequential parameter set */ 
  GstH264Sps *sps_buffers[MAX_SPS_COUNT];
  GstH264Sps *sps; /* Current SPS */ 
  /* PPS: sequential parameter set */ 
  GstH264Pps *pps_buffers[MAX_PPS_COUNT];
  GstH264Pps *pps; /* Current PPS */ 

  /* slice header */ 
  guint8 first_mb_in_slice;
  guint8 slice_type;
  guint8 pps_id;
  guint32 frame_num;
  gboolean field_pic_flag;
  gboolean bottom_field_flag;

  /* SEI: supplemental enhancement messages */ 
  /* buffering period */ 
  guint32 initial_cpb_removal_delay[32];
  /* picture timing */ 
  guint32 sei_cpb_removal_delay;
  guint32 sei_dpb_output_delay;
  guint8 sei_pic_struct;
  guint8 sei_ct_type; 
  /* And more... */ 

  /* cached timestamps */ 
  GstClockTime dts;
  GstClockTime last_outbuf_dts;
  GstClockTime ts_trn_nb; /* dts of last buffering period */ 
  GstClockTime cur_duration; /* duration of the current access unit */ 

  /* for debug purpose */ 
  guint32 frame_cnt;

  /* NALU AU */
  GstAdapter *picture_adapter;
  gboolean picture_start;
  gint idr_offset;

  /* codec data NALUs to be inserted into stream */
  GSList  *codec_nals;
  /* SPS and PPS NALUs collected from stream to form codec_data in caps */
  GstBuffer *sps_nals[MAX_SPS_COUNT];
  GstBuffer *pps_nals[MAX_PPS_COUNT];

  GstCaps *src_caps;

  GstEvent *pending_segment;
  GList *pending_events;
};

struct _GstH264ParseClass
{
  GstElementClass parent_class;
};

GType gst_h264_parse_get_type (void);

G_END_DECLS

#endif /* __GST_H264_PARSE_H__ */
