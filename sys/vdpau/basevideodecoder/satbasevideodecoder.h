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

#ifndef _SAT_BASE_VIDEO_DECODER_H_
#define _SAT_BASE_VIDEO_DECODER_H_

#include "satbasevideoutils.h"
#include "satvideoframe.h"

G_BEGIN_DECLS

#define SAT_TYPE_BASE_VIDEO_DECODER \
  (sat_base_video_decoder_get_type())
#define SAT_BASE_VIDEO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),SAT_TYPE_BASE_VIDEO_DECODER,SatBaseVideoDecoder))
#define SAT_BASE_VIDEO_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),SAT_TYPE_BASE_VIDEO_DECODER,SatBaseVideoDecoderClass))
#define SAT_BASE_VIDEO_DECODER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),SAT_TYPE_BASE_VIDEO_DECODER,SatBaseVideoDecoderClass))
#define GST_IS_BASE_VIDEO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),SAT_TYPE_BASE_VIDEO_DECODER))
#define GST_IS_BASE_VIDEO_DECODER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),SAT_TYPE_BASE_VIDEO_DECODER))

/**
 * SAT_BASE_VIDEO_DECODER_SINK_NAME:
 *
 * The name of the templates for the sink pad.
 */
#define SAT_BASE_VIDEO_DECODER_SINK_NAME    "sink"
/**
 * SAT_BASE_VIDEO_DECODER_SRC_NAME:
 *
 * The name of the templates for the source pad.
 */
#define SAT_BASE_VIDEO_DECODER_SRC_NAME     "src"

/**
 * SAT_BASE_VIDEO_CODEC_SRC_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the source #GstPad object of the element.
 */
#define SAT_BASE_VIDEO_DECODER_SRC_PAD(obj)         (((SatBaseVideoDecoder *) (obj))->srcpad)

/**
 * SAT_BASE_VIDEO_CODEC_SINK_PAD:
 * @obj: base video codec instance
 *
 * Gives the pointer to the sink #GstPad object of the element.
 */
#define SAT_BASE_VIDEO_DECODER_SINK_PAD(obj)        (((SatBaseVideoDecoder *) (obj))->sinkpad)

/**
 *  * SAT_BASE_VIDEO_DECODER_FLOW_NEED_DATA:
 *   *
 *    */
#define SAT_BASE_VIDEO_DECODER_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS


typedef enum _SatBaseVideoDecoderScanResult SatBaseVideoDecoderScanResult;

enum _SatBaseVideoDecoderScanResult
{
  SAT_BASE_VIDEO_DECODER_SCAN_RESULT_OK,
  SAT_BASE_VIDEO_DECODER_SCAN_RESULT_LOST_SYNC,
  SAT_BASE_VIDEO_DECODER_SCAN_RESULT_NEED_DATA
};

typedef struct _SatBaseVideoDecoder SatBaseVideoDecoder;
typedef struct _SatBaseVideoDecoderClass SatBaseVideoDecoderClass;

struct _SatBaseVideoDecoder
{
  GstElement element;

  /*< private >*/
	GstPad *sinkpad;
  GstPad *srcpad;
  GstAdapter *input_adapter;

  gboolean have_sync;
  gboolean discont;

  SatVideoState state;
  GstSegment segment;

  guint64 presentation_frame_number;
  guint64 system_frame_number;

  GstCaps *caps;
  gboolean have_src_caps;

  SatVideoFrame *current_frame;

  gint distance_from_sync;
  gint reorder_depth;

  GstClockTime buffer_timestamp;

  GstClockTime timestamp_offset;

  gdouble proportion;
  GstClockTime earliest_time;

  guint64 input_offset;
  guint64 frame_offset;
  GstClockTime last_timestamp;

  guint64 base_picture_number;

  gint field_index;

  gboolean is_delta_unit;

  GList *timestamps;
  gboolean have_segment;

  /* properties */
  gboolean sink_clipping;
  gboolean packetized;
  
};

struct _SatBaseVideoDecoderClass
{
	GstElementClass element_class;

  gboolean (*start) (SatBaseVideoDecoder *coder);
  gboolean (*stop)  (SatBaseVideoDecoder *coder);
  gboolean (*flush) (SatBaseVideoDecoder *coder);

	gboolean (*set_sink_caps) (SatBaseVideoDecoder *base_video_decoder,
	    GstCaps *caps);
	
  GstPad *(*create_srcpad) (SatBaseVideoDecoder * base_video_decoder,
	    SatBaseVideoDecoderClass *base_video_decoder_class);

	
  gint (*scan_for_sync) (SatBaseVideoDecoder *coder, GstAdapter *adapter);

  SatBaseVideoDecoderScanResult (*scan_for_packet_end)
    (SatBaseVideoDecoder *coder, GstAdapter *adapter, guint *size, gboolean at_eos);
  
  GstFlowReturn (*parse_data) (SatBaseVideoDecoder *decoder,
      GstBuffer *buf, gboolean at_eos);

	
	SatVideoFrame *(*create_frame) (SatBaseVideoDecoder *coder);
  GstFlowReturn (*handle_frame) (SatBaseVideoDecoder *coder, SatVideoFrame *frame,
				 GstClockTimeDiff deadline);
  GstFlowReturn (*shape_output) (SatBaseVideoDecoder *coder,
	    GstBuffer *buf);

};

GType sat_base_video_decoder_get_type (void);

SatVideoFrame *sat_base_video_decoder_get_current_frame (SatBaseVideoDecoder
    *base_video_decoder);

GstFlowReturn sat_base_video_decoder_finish_frame (SatBaseVideoDecoder *base_video_decoder,
    SatVideoFrame *frame);
void sat_base_video_decoder_skip_frame (SatBaseVideoDecoder * base_video_decoder,
    SatVideoFrame * frame);

void
sat_base_video_decoder_frame_start (SatBaseVideoDecoder *base_video_decoder,
    GstBuffer *buf);
GstFlowReturn
sat_base_video_decoder_have_frame (SatBaseVideoDecoder *base_video_decoder,
    SatVideoFrame **new_frame);

SatVideoState * sat_base_video_decoder_get_state (SatBaseVideoDecoder *base_video_decoder);
void sat_base_video_decoder_set_state (SatBaseVideoDecoder *base_video_decoder,
    SatVideoState *state);

void sat_base_video_decoder_lost_sync (SatBaseVideoDecoder *base_video_decoder);

void sat_base_video_decoder_update_src_caps (SatBaseVideoDecoder *base_video_decoder);

G_END_DECLS

#endif

