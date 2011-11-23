/* GStreamer
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
 * Copyright (C) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>.
 * Copyright (C) 2011 Nokia Corporation. All rights reserved.
 *   Contact: Stefan Kost <stefan.kost@nokia.com>
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

#ifndef GST_USE_UNSTABLE_API
#warning "GstBaseVideoDecoder is unstable API and may change in future."
#warning "You can define GST_USE_UNSTABLE_API to avoid this warning."
#endif

#include <gst/video/gstbasevideocodec.h>

G_BEGIN_DECLS

#define GST_TYPE_BASE_VIDEO_DECODER \
  (gst_base_video_decoder_get_type())
#define GST_BASE_VIDEO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_VIDEO_DECODER,GstBaseVideoDecoder))
#define GST_BASE_VIDEO_DECODER_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_VIDEO_DECODER,GstBaseVideoDecoderClass))
#define GST_BASE_VIDEO_DECODER_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_BASE_VIDEO_DECODER,GstBaseVideoDecoderClass))
#define GST_IS_BASE_VIDEO_DECODER(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_VIDEO_DECODER))
#define GST_IS_BASE_VIDEO_DECODER_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_VIDEO_DECODER))

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
 * GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA:
 *
 * Returned while parsing to indicate more data is needed.
 **/
#define GST_BASE_VIDEO_DECODER_FLOW_NEED_DATA GST_FLOW_CUSTOM_SUCCESS

/**
 * GST_BASE_VIDEO_DECODER_FLOW_DROPPED:
 *
 * Returned when the event/buffer should be dropped.
 */
#define GST_BASE_VIDEO_DECODER_FLOW_DROPPED GST_FLOW_CUSTOM_SUCCESS_1

typedef struct _GstBaseVideoDecoder GstBaseVideoDecoder;
typedef struct _GstBaseVideoDecoderClass GstBaseVideoDecoderClass;


/* do not use this one, use macro below */
GstFlowReturn _gst_base_video_decoder_error (GstBaseVideoDecoder *dec, gint weight,
                                             GQuark domain, gint code,
                                             gchar *txt, gchar *debug,
                                             const gchar *file, const gchar *function,
                                             gint line);

/**
 * GST_BASE_VIDEO_DECODER_ERROR:
 * @el:     the base video decoder element that generates the error
 * @weight: element defined weight of the error, added to error count
 * @domain: like CORE, LIBRARY, RESOURCE or STREAM (see #gstreamer-GstGError)
 * @code:   error code defined for that domain (see #gstreamer-GstGError)
 * @text:   the message to display (format string and args enclosed in
 *          parentheses)
 * @debug:  debugging information for the message (format string and args
 *          enclosed in parentheses)
 * @ret:    variable to receive return value
 *
 * Utility function that audio decoder elements can use in case they encountered
 * a data processing error that may be fatal for the current "data unit" but
 * need not prevent subsequent decoding.  Such errors are counted and if there
 * are too many, as configured in the context's max_errors, the pipeline will
 * post an error message and the application will be requested to stop further
 * media processing.  Otherwise, it is considered a "glitch" and only a warning
 * is logged. In either case, @ret is set to the proper value to
 * return to upstream/caller (indicating either GST_FLOW_ERROR or GST_FLOW_OK).
 */
#define GST_BASE_AUDIO_DECODER_ERROR(el, w, domain, code, text, debug, ret) \
G_STMT_START {                                                              \
  gchar *__txt = _gst_element_error_printf text;                            \
  gchar *__dbg = _gst_element_error_printf debug;                           \
  GstBaseVideoDecoder *dec = GST_BASE_VIDEO_DECODER (el);                   \
  ret = _gst_base_video_decoder_error (dec, w, GST_ ## domain ## _ERROR,    \
      GST_ ## domain ## _ERROR_ ## code, __txt, __dbg, __FILE__,            \
      GST_FUNCTION, __LINE__);                                              \
} G_STMT_END


/**
 * GstBaseVideoDecoder:
 *
 * The opaque #GstBaseVideoDecoder data structure.
 */
struct _GstBaseVideoDecoder
{
  GstBaseVideoCodec base_video_codec;

  /*< protected >*/
  gboolean          sink_clipping;
  gboolean          do_byte_time;
  gboolean          packetized;
  gint              max_errors;

  /* parse tracking */
  /* input data */
  GstAdapter       *input_adapter;
  /* assembles current frame */
  GstAdapter       *output_adapter;

  /*< private >*/
  /* FIXME move to real private part ?
   * (and introduce a context ?) */
  /* ... being tracked here;
   * only available during parsing */
  /* FIXME remove and add parameter to method */
  GstVideoFrame    *current_frame;
  /* events that should apply to the current frame */
  GList            *current_frame_events;
  /* relative offset of input data */
  guint64           input_offset;
  /* relative offset of frame */
  guint64           frame_offset;
  /* tracking ts and offsets */
  GList            *timestamps;
  /* whether parsing is in sync */
  gboolean          have_sync;

  /* maybe sort-of protected ? */

  /* combine to yield (presentation) ts */
  GstClockTime      timestamp_offset;
  int               field_index;

  /* last outgoing ts */
  GstClockTime      last_timestamp;
  gint              error_count;

  /* reverse playback */
  /* collect input */
  GList            *gather;
  /* to-be-parsed */
  GList            *parse;
  /* collected parsed frames */
  GList            *parse_gather;
  /* frames to be handled == decoded */
  GList            *decode;
  /* collected output */
  GList            *queued;
  gboolean          process;

  /* no comment ... */
  guint64           base_picture_number;
  int               reorder_depth;
  int               distance_from_sync;

  /* qos messages: frames dropped/processed */
  guint             dropped;
  guint             processed;

  /* FIXME before moving to base */
  void             *padding[GST_PADDING_LARGE];
};

/**
 * GstBaseVideoDecoderClass:
 * @start:          Optional.
 *                  Called when the element starts processing.
 *                  Allows opening external resources.
 * @stop:           Optional.
 *                  Called when the element stops processing.
 *                  Allows closing external resources.
 * @set_format:     Notifies subclass of incoming data format (caps).
 * @scan_for_sync:  Optional.
 *                  Allows subclass to obtain sync for subsequent parsing
 *                  by custom means (above an beyond scanning for specific
 *                  marker and mask).
 * @parse_data:     Required for non-packetized input.
 *                  Allows chopping incoming data into manageable units (frames)
 *                  for subsequent decoding.
 * @reset:          Optional.
 *                  Allows subclass (codec) to perform post-seek semantics reset.
 * @handle_frame:   Provides input data frame to subclass.
 * @finish:         Optional.
 *                  Called to request subclass to dispatch any pending remaining
 *                  data (e.g. at EOS).
 *
 * Subclasses can override any of the available virtual methods or not, as
 * needed. At minimum @handle_frame needs to be overridden, and @set_format
 * and likely as well.  If non-packetized input is supported or expected,
 * @parse needs to be overridden as well.
 */
struct _GstBaseVideoDecoderClass
{
  GstBaseVideoCodecClass base_video_codec_class;

  gboolean      (*start)          (GstBaseVideoDecoder *coder);

  gboolean      (*stop)           (GstBaseVideoDecoder *coder);

  int           (*scan_for_sync)  (GstBaseVideoDecoder *decoder, gboolean at_eos,
                                   int offset, int n);

  GstFlowReturn (*parse_data)     (GstBaseVideoDecoder *decoder, gboolean at_eos);

  gboolean      (*set_format)     (GstBaseVideoDecoder *coder, GstVideoState * state);

  gboolean      (*reset)          (GstBaseVideoDecoder *coder);

  GstFlowReturn (*finish)         (GstBaseVideoDecoder *coder);

  GstFlowReturn (*handle_frame)   (GstBaseVideoDecoder *coder, GstVideoFrame *frame);


  /*< private >*/
  guint32       capture_mask;
  guint32       capture_pattern;

  /* FIXME before moving to base */
  void         *padding[GST_PADDING_LARGE];
};

void             gst_base_video_decoder_class_set_capture_pattern (GstBaseVideoDecoderClass *klass,
                                    guint32 mask, guint32 pattern);

GstVideoFrame   *gst_base_video_decoder_get_frame (GstBaseVideoDecoder *coder,
                                    int frame_number);
GstVideoFrame   *gst_base_video_decoder_get_oldest_frame (GstBaseVideoDecoder *coder);

void             gst_base_video_decoder_add_to_frame (GstBaseVideoDecoder *base_video_decoder,
                                    int n_bytes);
void             gst_base_video_decoder_lost_sync (GstBaseVideoDecoder *base_video_decoder);
GstFlowReturn    gst_base_video_decoder_have_frame (GstBaseVideoDecoder *base_video_decoder);

void             gst_base_video_decoder_set_sync_point (GstBaseVideoDecoder *base_video_decoder);
gboolean         gst_base_video_decoder_set_src_caps (GstBaseVideoDecoder *base_video_decoder);
GstBuffer       *gst_base_video_decoder_alloc_src_buffer (GstBaseVideoDecoder * base_video_decoder);
GstFlowReturn    gst_base_video_decoder_alloc_src_frame (GstBaseVideoDecoder *base_video_decoder,
                                    GstVideoFrame *frame);
GstVideoState   *gst_base_video_decoder_get_state (GstBaseVideoDecoder *base_video_decoder);
GstClockTimeDiff gst_base_video_decoder_get_max_decode_time (
                                    GstBaseVideoDecoder *base_video_decoder,
                                    GstVideoFrame *frame);
GstFlowReturn    gst_base_video_decoder_finish_frame (GstBaseVideoDecoder *base_video_decoder,
                                    GstVideoFrame *frame);
GstFlowReturn    gst_base_video_decoder_drop_frame (GstBaseVideoDecoder *dec,
                                    GstVideoFrame *frame);
GType            gst_base_video_decoder_get_type (void);

G_END_DECLS

#endif

