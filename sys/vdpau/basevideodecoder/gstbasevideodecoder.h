/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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

#ifndef _GST_BASE_VIDEO_DECODER_H_
#define _GST_BASE_VIDEO_DECODER_H_

#include "gstbasevideoutils.h"
#include "gstvideoframe.h"

G_BEGIN_DECLS

#define GST_TYPE_BASE_VIDEO_DECODER           (gst_base_video_decoder_get_type())
#define GST_BASE_VIDEO_DECODER(obj)           (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_BASE_VIDEO_DECODER, GstBaseVideoDecoder))
#define GST_BASE_VIDEO_DECODER_CLASS(klass)   (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_BASE_VIDEO_DECODER, GstBaseVideoDecoderClass))
#define GST_BASE_VIDEO_DECODER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), GST_TYPE_BASE_VIDEO_DECODER, GstBaseVideoDecoderClass))
#define GST_IS_BASE_VIDEO_DECODER(obj)        (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_BASE_VIDEO_DECODER))
#define GST_IS_BASE_VIDEO_DECODER_CLASS(obj)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_BASE_VIDEO_DECODER))

/**
 * GST_BASE_VIDEO_DECODER_SINK_NAME:
 *
 * The name of the templates for the sink pad.
 */
#define GST_BASE_VIDEO_DECODER_SINK_NAME    "sink"
/**
 * GST_BASE_VIDEO_DECODER_SRC_NAME:
 *
 * The name of the templates for the source pad.
 */
#define GST_BASE_VIDEO_DECODER_SRC_NAME     "src"

/**
 * GST_BASE_VIDEO_CODEC_SRC_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 */
#define GST_BASE_VIDEO_DECODER_SRC_PAD(obj)         (((GstBaseVideoDecoder *) (obj))->srcpad)

/**
 * GST_BASE_VIDEO_CODEC_SINK_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 */
#define GST_BASE_VIDEO_DECODER_SINK_PAD(obj)        (((GstBaseVideoDecoder *) (obj))->sinkpad)

/**
 *  * GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA:
 *   *
 *    */
#define GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS


typedef enum _GstBaseVideoDecoderScanResult GstBaseVideoDecoderScanResult;

enum _GstBaseVideoDecoderScanResult
{
  GST_BASE_VIDEO_DECODER_SCAN_RESULT_OK,
  GST_BASE_VIDEO_DECODER_SCAN_RESULT_LOST_SYNC,
  GST_BASE_VIDEO_DECODER_SCAN_RESULT_NEED_DATA
};

typedef struct _GstBaseVideoDecoder GstBaseVideoDecoder;
typedef struct _GstBaseVideoDecoderClass GstBaseVideoDecoderClass;

struct _GstBaseVideoDecoder
{
  GstElement element;

  /*< private >*/
	GstPad *sinkpad;
  GstPad *srcpad;
  GstAdapter *input_adapter;

  gboolean have_sync;
  gboolean discont;

  GstVideoState state;
  GstSegment segment;

  GstCaps *caps;
  gboolean have_src_caps;

  GstVideoFrame *current_frame;

  GList *timestamps;
  guint64 field_index;
  GstClockTime timestamp_offset;
  GstClockTime last_timestamp;

  gdouble proportion;
  GstClockTime earliest_time;

  guint64 input_offset;
  guint64 current_buf_offset;
  guint64 prev_buf_offset;

  gboolean have_segment;

  /* properties */
  gboolean sink_clipping;
  gboolean packetized; 
};

struct _GstBaseVideoDecoderClass
{
	GstElementClass element_class;

  gboolean (*start) (GstBaseVideoDecoder *coder);
  gboolean (*stop)  (GstBaseVideoDecoder *coder);
  gboolean (*flush) (GstBaseVideoDecoder *coder);

	gboolean (*set_sink_caps) (GstBaseVideoDecoder *base_video_decoder,
	    GstCaps *caps);
	
  GstPad *(*create_srcpad) (GstBaseVideoDecoder * base_video_decoder,
	    GstBaseVideoDecoderClass *base_video_decoder_class);

	
  gint (*scan_for_sync) (GstBaseVideoDecoder *coder, GstAdapter *adapter);

  GstBaseVideoDecoderScanResult (*scan_for_packet_end)
    (GstBaseVideoDecoder *coder, GstAdapter *adapter, guint *size, gboolean at_eos);
  
  GstFlowReturn (*parse_data) (GstBaseVideoDecoder *decoder,
      GstBuffer *buf, gboolean at_eos, GstVideoFrame *frame);

	
	GstVideoFrame *(*create_frame) (GstBaseVideoDecoder *coder);
  GstFlowReturn (*handle_frame) (GstBaseVideoDecoder *coder, GstVideoFrame *frame,
				 GstClockTimeDiff deadline);
  GstFlowReturn (*shape_output) (GstBaseVideoDecoder *coder,
	    GstBuffer *buf);

};

GType gst_base_video_decoder_get_type (void);

GstVideoFrame *gst_base_video_decoder_get_frame (GstBaseVideoDecoder *coder,
    gint frame_number);
GstVideoFrame *gst_base_video_decoder_get_oldest_frame (GstBaseVideoDecoder *coder);

GstFlowReturn gst_base_video_decoder_finish_frame (GstBaseVideoDecoder *base_video_decoder,
    GstVideoFrame *frame);
void gst_base_video_decoder_skip_frame (GstBaseVideoDecoder * base_video_decoder,
    GstVideoFrame * frame);

GstFlowReturn
gst_base_video_decoder_have_frame (GstBaseVideoDecoder *base_video_decoder,
    gboolean include_current_buf, GstVideoFrame **new_frame);

GstVideoState gst_base_video_decoder_get_state (GstBaseVideoDecoder *base_video_decoder);
void gst_base_video_decoder_set_state (GstBaseVideoDecoder *base_video_decoder,
    GstVideoState state);
gboolean gst_base_video_decoder_set_src_caps (GstBaseVideoDecoder * base_video_decoder);

void gst_base_video_decoder_lost_sync (GstBaseVideoDecoder *base_video_decoder);

G_END_DECLS

#endif

