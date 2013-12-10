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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstvtdec
 *
 * Apple VideoToolbox based decoder.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v filesrc location=file.mov ! qtdemux ! queue ! h264parse ! vtdec ! videoconvert ! autovideosink
 * ]|
 * Decode h264 video from a mov file.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include "vtdec.h"
#include "vtutil.h"
#include "corevideobuffer.h"
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_vtdec_debug_category);
#define GST_CAT_DEFAULT gst_vtdec_debug_category

static void gst_vtdec_finalize (GObject * object);

static gboolean gst_vtdec_start (GstVideoDecoder * decoder);
static gboolean gst_vtdec_stop (GstVideoDecoder * decoder);
static gboolean gst_vtdec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_vtdec_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_vtdec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_vtdec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

static gboolean gst_vtdec_create_session (GstVtdec * vtdec);
static void gst_vtdec_invalidate_session (GstVtdec * vtdec);
static CMSampleBufferRef cm_sample_buffer_from_gst_buffer (GstVtdec * vtdec,
    GstBuffer * buf);
static GstFlowReturn gst_vtdec_push_frames_if_needed (GstVtdec * vtdec,
    gboolean drain, gboolean flush);
static CMFormatDescriptionRef create_format_description (GstVtdec * vtdec,
    CMVideoCodecType cm_format);
static CMFormatDescriptionRef
create_format_description_from_codec_data (GstVtdec * vtdec,
    CMVideoCodecType cm_format, GstBuffer * codec_data);
static void gst_vtdec_session_output_callback (void
    *decompression_output_ref_con, void *source_frame_ref_con, OSStatus status,
    VTDecodeInfoFlags info_flags, CVImageBufferRef image_buffer, CMTime pts,
    CMTime duration);

static GstStaticPadTemplate gst_vtdec_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, stream-format=avc, alignment=au;"
        "video/mpeg, mpegversion=2;" "image/jpeg")
    );

#ifdef HAVE_IOS
#define GST_VTDEC_VIDEO_FORMAT_STR "NV12"
#define GST_VTDEC_VIDEO_FORMAT GST_VIDEO_FORMAT_NV12
#define GST_VTDEC_CV_VIDEO_FORMAT kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
#else
#define GST_VTDEC_VIDEO_FORMAT_STR "UYVY"
#define GST_VTDEC_VIDEO_FORMAT GST_VIDEO_FORMAT_UYVY
#define GST_VTDEC_CV_VIDEO_FORMAT kCVPixelFormatType_422YpCbCr8
#endif

#define VIDEO_SRC_CAPS \
    GST_VIDEO_CAPS_MAKE("{" GST_VTDEC_VIDEO_FORMAT_STR "}")

G_DEFINE_TYPE_WITH_CODE (GstVtdec, gst_vtdec, GST_TYPE_VIDEO_DECODER,
    GST_DEBUG_CATEGORY_INIT (gst_vtdec_debug_category, "vtdec", 0,
        "debug category for vtdec element"));

static void
gst_vtdec_class_init (GstVtdecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  /* Setting up pads and setting metadata should be moved to
     base_class_init if you intend to subclass this class. */
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_static_pad_template_get (&gst_vtdec_sink_template));
  gst_element_class_add_pad_template (GST_ELEMENT_CLASS (klass),
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          gst_caps_from_string (VIDEO_SRC_CAPS)));

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS (klass),
      "Apple VideoToolbox decoder",
      "Codec/Decoder/Video",
      "Apple VideoToolbox Decoder",
      "Ole André Vadla Ravnås <oleavr@soundrop.com>; "
      "Alessandro Decina <alessandro.d@gmail.com>");

  gobject_class->finalize = gst_vtdec_finalize;
  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_vtdec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_vtdec_stop);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_vtdec_set_format);
  video_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_vtdec_flush);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_vtdec_finish);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_vtdec_handle_frame);
}

static void
gst_vtdec_init (GstVtdec * vtdec)
{
  vtdec->reorder_queue = g_async_queue_new ();
}

void
gst_vtdec_finalize (GObject * object)
{
  GstVtdec *vtdec = GST_VTDEC (object);

  GST_DEBUG_OBJECT (vtdec, "finalize");

  g_async_queue_unref (vtdec->reorder_queue);

  G_OBJECT_CLASS (gst_vtdec_parent_class)->finalize (object);
}

static gboolean
gst_vtdec_start (GstVideoDecoder * decoder)
{
  GstVtdec *vtdec = GST_VTDEC (decoder);

  GST_DEBUG_OBJECT (vtdec, "start");

  return TRUE;
}

static gboolean
gst_vtdec_stop (GstVideoDecoder * decoder)
{
  GstVtdec *vtdec = GST_VTDEC (decoder);

  if (vtdec->session)
    gst_vtdec_invalidate_session (vtdec);

  GST_DEBUG_OBJECT (vtdec, "stop");

  return TRUE;
}

static gboolean
gst_vtdec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstStructure *structure;
  CMVideoCodecType cm_format;
  CMFormatDescriptionRef format_description = NULL;
  const char *caps_name;
  GstVtdec *vtdec = GST_VTDEC (decoder);

  GST_DEBUG_OBJECT (vtdec, "set_format");

  structure = gst_caps_get_structure (state->caps, 0);
  caps_name = gst_structure_get_name (structure);
  if (!strcmp (caps_name, "video/x-h264") && state->codec_data == NULL) {
    GST_INFO_OBJECT (vtdec, "no codec data, wait for one");
    return TRUE;
  }

  if (vtdec->session)
    gst_vtdec_invalidate_session (vtdec);

  vtdec->reorder_queue_frame_delay = 0;

  if (!strcmp (caps_name, "video/x-h264")) {
    cm_format = kCMVideoCodecType_H264;
    vtdec->reorder_queue_frame_delay = 16;
  } else if (!strcmp (caps_name, "video/mpeg")) {
    cm_format = kCMVideoCodecType_MPEG2Video;
  } else if (!strcmp (caps_name, "image/jpeg")) {
    cm_format = kCMVideoCodecType_JPEG;
  }

  gst_video_info_from_caps (&vtdec->video_info, state->caps);

  if (state->codec_data) {
    format_description = create_format_description_from_codec_data (vtdec,
        cm_format, state->codec_data);
  } else {
    format_description = create_format_description (vtdec, cm_format);
  }

  if (vtdec->format_description)
    CFRelease (vtdec->format_description);
  vtdec->format_description = format_description;

  if (!gst_vtdec_create_session (vtdec))
    return FALSE;

  gst_video_decoder_set_output_state (decoder,
      GST_VTDEC_VIDEO_FORMAT, vtdec->video_info.width, vtdec->video_info.height,
      state);

  return TRUE;
}

static gboolean
gst_vtdec_flush (GstVideoDecoder * decoder)
{
  GstVtdec *vtdec = GST_VTDEC (decoder);

  GST_DEBUG_OBJECT (vtdec, "flush");

  gst_vtdec_push_frames_if_needed (vtdec, FALSE, TRUE);

  return TRUE;
}

static GstFlowReturn
gst_vtdec_finish (GstVideoDecoder * decoder)
{
  GstVtdec *vtdec = GST_VTDEC (decoder);

  GST_DEBUG_OBJECT (vtdec, "finish");

  return gst_vtdec_push_frames_if_needed (vtdec, TRUE, FALSE);
}

static GstFlowReturn
gst_vtdec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  OSStatus status;
  CMSampleBufferRef cm_sample_buffer = NULL;
  VTDecodeFrameFlags input_flags, output_flags;
  GstVtdec *vtdec = GST_VTDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  int decode_frame_number = frame->decode_frame_number;

  if (vtdec->format_description == NULL) {
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto out;
  }

  GST_LOG_OBJECT (vtdec, "got input frame %d", decode_frame_number);

  ret = gst_vtdec_push_frames_if_needed (vtdec, FALSE, FALSE);
  if (ret != GST_FLOW_OK)
    return ret;

  /* don't bother enabling kVTDecodeFrame_EnableTemporalProcessing at all since
   * it's not mandatory for the underlying VT codec to respect it. KISS and do
   * reordering ourselves.
   */
  input_flags = kVTDecodeFrame_EnableAsynchronousDecompression;
  output_flags = 0;

  cm_sample_buffer =
      cm_sample_buffer_from_gst_buffer (vtdec, frame->input_buffer);
  status =
      VTDecompressionSessionDecodeFrame (vtdec->session, cm_sample_buffer,
      input_flags, frame, NULL);
  if (status != noErr && FALSE)
    goto error;

  GST_LOG_OBJECT (vtdec, "submitted input frame %d", decode_frame_number);

out:
  if (cm_sample_buffer)
    CFRelease (cm_sample_buffer);
  return ret;

error:
  GST_ELEMENT_ERROR (vtdec, STREAM, DECODE, (NULL),
      ("VTDecompressionSessionDecodeFrame returned %d", status));
  ret = GST_FLOW_ERROR;
  goto out;
}

static gboolean
gst_vtdec_create_session (GstVtdec * vtdec)
{
  CFMutableDictionaryRef output_image_buffer_attrs;
  VTDecompressionOutputCallbackRecord callback;
  OSStatus status;

  output_image_buffer_attrs =
      CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_i32 (output_image_buffer_attrs,
      kCVPixelBufferPixelFormatTypeKey, GST_VTDEC_CV_VIDEO_FORMAT);
  gst_vtutil_dict_set_i32 (output_image_buffer_attrs, kCVPixelBufferWidthKey,
      vtdec->video_info.width);
  gst_vtutil_dict_set_i32 (output_image_buffer_attrs, kCVPixelBufferHeightKey,
      vtdec->video_info.height);
  gst_vtutil_dict_set_i32 (output_image_buffer_attrs,
      kCVPixelBufferBytesPerRowAlignmentKey, 2 * vtdec->video_info.width);

  callback.decompressionOutputCallback = gst_vtdec_session_output_callback;
  callback.decompressionOutputRefCon = vtdec;

  status = VTDecompressionSessionCreate (NULL, vtdec->format_description,
      NULL, output_image_buffer_attrs, &callback, &vtdec->session);

  CFRelease (output_image_buffer_attrs);

  if (status != noErr) {
    GST_ELEMENT_ERROR (vtdec, RESOURCE, FAILED, (NULL),
        ("VTDecompressionSessionCreate returned %d", status));
    return FALSE;
  }

  return TRUE;
}

static void
gst_vtdec_invalidate_session (GstVtdec * vtdec)
{
  g_return_if_fail (vtdec->session);

  VTDecompressionSessionInvalidate (vtdec->session);
  CFRelease (vtdec->session);
  vtdec->session = NULL;
}

static CMFormatDescriptionRef
create_format_description (GstVtdec * vtdec, CMVideoCodecType cm_format)
{
  OSStatus status;
  CMFormatDescriptionRef format_description;

  status = CMVideoFormatDescriptionCreate (NULL,
      cm_format, vtdec->video_info.width, vtdec->video_info.height,
      NULL, &format_description);
  if (status != noErr)
    return NULL;

  return format_description;
}

static CMFormatDescriptionRef
create_format_description_from_codec_data (GstVtdec * vtdec,
    CMVideoCodecType cm_format, GstBuffer * codec_data)
{
  CMFormatDescriptionRef fmt_desc;
  CFMutableDictionaryRef extensions, par, atoms;
  GstMapInfo map;
  OSStatus status;

  /* Extensions dict */
  extensions =
      CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_string (extensions,
      CFSTR ("CVImageBufferChromaLocationBottomField"), "left");
  gst_vtutil_dict_set_string (extensions,
      CFSTR ("CVImageBufferChromaLocationTopField"), "left");
  gst_vtutil_dict_set_boolean (extensions, CFSTR ("FullRangeVideo"), FALSE);

  /* CVPixelAspectRatio dict */
  par = CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_i32 (par, CFSTR ("HorizontalSpacing"),
      vtdec->video_info.par_n);
  gst_vtutil_dict_set_i32 (par, CFSTR ("VerticalSpacing"),
      vtdec->video_info.par_d);
  gst_vtutil_dict_set_object (extensions, CFSTR ("CVPixelAspectRatio"),
      (CFTypeRef *) par);

  /* SampleDescriptionExtensionAtoms dict */
  gst_buffer_map (codec_data, &map, GST_MAP_READ);
  atoms = CFDictionaryCreateMutable (NULL, 0, &kCFTypeDictionaryKeyCallBacks,
      &kCFTypeDictionaryValueCallBacks);
  gst_vtutil_dict_set_data (atoms, CFSTR ("avcC"), map.data, map.size);
  gst_vtutil_dict_set_object (extensions,
      CFSTR ("SampleDescriptionExtensionAtoms"), (CFTypeRef *) atoms);
  gst_buffer_unmap (codec_data, &map);

  status = CMVideoFormatDescriptionCreate (NULL,
      cm_format, vtdec->video_info.width, vtdec->video_info.height,
      extensions, &fmt_desc);

  if (status == noErr)
    return fmt_desc;
  else
    return NULL;
}

static CMSampleBufferRef
cm_sample_buffer_from_gst_buffer (GstVtdec * vtdec, GstBuffer * buf)
{
  OSStatus status;
  CMBlockBufferRef bbuf = NULL;
  CMSampleBufferRef sbuf = NULL;
  GstMapInfo map;
  CMSampleTimingInfo sample_timing;
  CMSampleTimingInfo time_array[1];

  g_return_val_if_fail (vtdec->format_description, NULL);

  gst_buffer_map (buf, &map, GST_MAP_READ);

  /* create a block buffer,  the CoreMedia equivalent of GstMemory */
  status = CMBlockBufferCreateWithMemoryBlock (NULL,
      map.data, (gint64) map.size, kCFAllocatorNull, NULL, 0, (gint64) map.size,
      FALSE, &bbuf);

  gst_buffer_unmap (buf, &map);

  if (status != noErr)
    goto block_error;

  /* create a sample buffer, the CoreMedia equivalent of GstBuffer */
  sample_timing.duration = CMTimeMake (GST_BUFFER_DURATION (buf), 1);
  sample_timing.presentationTimeStamp = CMTimeMake (GST_BUFFER_PTS (buf), 1);
  sample_timing.decodeTimeStamp = CMTimeMake (GST_BUFFER_DTS (buf), 1);
  time_array[0] = sample_timing;

  status =
      CMSampleBufferCreate (NULL, bbuf, TRUE, 0, 0, vtdec->format_description,
      1, 1, time_array, 0, NULL, &sbuf);
  if (status != noErr)
    goto sample_error;

out:
  return sbuf;

block_error:
  GST_ELEMENT_ERROR (vtdec, RESOURCE, FAILED, (NULL),
      ("CMBlockBufferCreateWithMemoryBlock returned %d", status));
  goto out;

sample_error:
  GST_ELEMENT_ERROR (vtdec, RESOURCE, FAILED, (NULL),
      ("CMSampleBufferCreate returned %d", status));

  if (bbuf)
    CFRelease (bbuf);

  goto out;
}

static gint
sort_frames_by_pts (gconstpointer f1, gconstpointer f2, gpointer user_data)
{
  GstVideoCodecFrame *frame1, *frame2;
  GstClockTime pts1, pts2;

  frame1 = (GstVideoCodecFrame *) f1;
  frame2 = (GstVideoCodecFrame *) f2;
  pts1 = GST_BUFFER_PTS (frame1->output_buffer);
  pts2 = GST_BUFFER_PTS (frame2->output_buffer);

  if (!GST_CLOCK_TIME_IS_VALID (pts1) || !GST_CLOCK_TIME_IS_VALID (pts2))
    return 0;

  if (pts1 < pts2)
    return -1;
  else if (pts1 == pts2)
    return 0;
  else
    return 1;
}

static void
gst_vtdec_session_output_callback (void *decompression_output_ref_con,
    void *source_frame_ref_con, OSStatus status, VTDecodeInfoFlags info_flags,
    CVImageBufferRef image_buffer, CMTime pts, CMTime duration)
{
  GstVtdec *vtdec = (GstVtdec *) decompression_output_ref_con;
  GstVideoCodecFrame *frame = (GstVideoCodecFrame *) source_frame_ref_con;
  GstBuffer *buf;
  GstVideoCodecState *state;

  GST_LOG_OBJECT (vtdec, "got output frame %p %d", frame,
      frame->decode_frame_number);

  if (status != noErr) {
    GST_ERROR_OBJECT (vtdec, "Error decoding frame %d", status);
    goto drop;
  }

  if (info_flags & kVTDecodeInfo_FrameDropped)
    goto drop;

  /* FIXME: use gst_video_decoder_allocate_output_buffer */
  state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (vtdec));
  buf = gst_core_video_buffer_new (image_buffer, &state->info);
  gst_video_codec_state_unref (state);
  frame->output_buffer = buf;

  gst_buffer_copy_into (buf, frame->input_buffer,
      GST_BUFFER_COPY_METADATA | GST_BUFFER_COPY_FLAGS, 0, -1);
  GST_BUFFER_PTS (buf) = pts.value;
  GST_BUFFER_DURATION (buf) = duration.value;

  g_async_queue_push_sorted (vtdec->reorder_queue, frame,
      sort_frames_by_pts, NULL);

  return;

drop:
  GST_WARNING_OBJECT (vtdec, "Frame dropped %p %d", frame,
      frame->decode_frame_number);
  gst_video_decoder_drop_frame (GST_VIDEO_DECODER (vtdec), frame);
}

static GstFlowReturn
gst_vtdec_push_frames_if_needed (GstVtdec * vtdec, gboolean drain,
    gboolean flush)
{
  GstVideoCodecFrame *frame;
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoDecoder *decoder = GST_VIDEO_DECODER (vtdec);

  if (drain)
    VTDecompressionSessionWaitForAsynchronousFrames (vtdec->session);

  /* push a buffer if there are enough frames to guarantee that we push in PTS
   * order
   */
  while ((g_async_queue_length (vtdec->reorder_queue) >=
          vtdec->reorder_queue_frame_delay) || drain || flush) {
    frame = (GstVideoCodecFrame *) g_async_queue_try_pop (vtdec->reorder_queue);
    /* we need to check this in case reorder_queue_frame_delay=0 (jpeg for
     * example) or we're draining/flushing
     */
    if (frame) {
      if (flush)
        gst_video_decoder_drop_frame (decoder, frame);
      else
        ret = gst_video_decoder_finish_frame (decoder, frame);
    }

    if (!frame || ret != GST_FLOW_OK)
      break;
  }

  return ret;
}
