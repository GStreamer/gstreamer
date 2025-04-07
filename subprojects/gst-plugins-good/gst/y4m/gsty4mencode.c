/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2006> Mark Nauwelaerts <mnauw@users.sourceforge.net>
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
/**
 * SECTION:element-y4menc
 * @title: y4menc
 *
 * Creates a YU4MPEG2 raw video stream as defined by the mjpegtools project.
 *
 * ## Example launch line
 *
 * (write everything in one line, without the backslash characters)
 * |[
 * gst-launch-1.0 videotestsrc num-buffers=250 \
 * ! 'video/x-raw,format=(string)I420,width=320,height=240,framerate=(fraction)25/1' \
 * ! y4menc ! filesink location=test.yuv
 * ]|
 *
 */

/* see mjpegtools/yuv4mpeg.h for yuv4mpeg format */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsty4mencode.h"

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0
};

static GstStaticPadTemplate y4mencode_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-yuv4mpeg, " "y4mversion = (int) 2")
    );

static GstStaticPadTemplate y4mencode_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ IYUV, I420, Y42B, Y41B, Y444 }"))
    );

GST_DEBUG_CATEGORY (y4menc_debug);
#define GST_CAT_DEFAULT y4menc_debug

static void gst_y4m_encode_reset (GstY4mEncode * filter);

static gboolean gst_y4m_encode_start (GstVideoEncoder * encoder);
static GstFlowReturn gst_y4m_encode_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);
static gboolean gst_y4m_encode_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);

#define gst_y4m_encode_parent_class parent_class
G_DEFINE_TYPE (GstY4mEncode, gst_y4m_encode, GST_TYPE_VIDEO_ENCODER);
GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (y4menc, "y4menc", GST_RANK_PRIMARY,
    GST_TYPE_Y4M_ENCODE, GST_DEBUG_CATEGORY_INIT (y4menc_debug, "y4menc", 0,
        "y4menc element"));

static void
gst_y4m_encode_class_init (GstY4mEncodeClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoEncoderClass *venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class,
      &y4mencode_src_factory);
  gst_element_class_add_static_pad_template (element_class,
      &y4mencode_sink_factory);

  gst_element_class_set_static_metadata (element_class,
      "YUV4MPEG video encoder", "Codec/Encoder/Video",
      "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
      "Wim Taymans <wim.taymans@gmail.com>");

  venc_class->start = gst_y4m_encode_start;
  venc_class->set_format = gst_y4m_encode_set_format;
  venc_class->handle_frame = gst_y4m_encode_handle_frame;
}

static void
gst_y4m_encode_init (GstY4mEncode * filter)
{
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (filter));

  /* init properties */
  gst_y4m_encode_reset (filter);
}

static void
gst_y4m_encode_reset (GstY4mEncode * filter)
{
  filter->header = FALSE;
  gst_video_info_init (&filter->info);
}

static gboolean
gst_y4m_encode_start (GstVideoEncoder * encoder)
{
  gst_y4m_encode_reset (GST_Y4M_ENCODE (encoder));

  return TRUE;
}

static gboolean
gst_y4m_encode_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstY4mEncode *y4menc;
  GstVideoInfo *info, out_info;
  GstVideoCodecState *output_state;
  gint width, height;
  GstVideoFormat format;
  gsize cr_h;

  y4menc = GST_Y4M_ENCODE (encoder);
  info = &state->info;

  format = GST_VIDEO_INFO_FORMAT (info);
  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  gst_video_info_set_format (&out_info, format, width, height);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      y4menc->colorspace = "420";
      out_info.stride[0] = width;
      out_info.stride[1] = GST_ROUND_UP_2 (width) / 2;
      out_info.stride[2] = out_info.stride[1];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      cr_h = GST_ROUND_UP_2 (height) / 2;
      if (GST_VIDEO_INFO_IS_INTERLACED (info))
        cr_h = GST_ROUND_UP_2 (height);
      out_info.offset[2] = out_info.offset[1] + out_info.stride[1] * cr_h;
      out_info.size = out_info.offset[2] + out_info.stride[2] * cr_h;
      break;
    case GST_VIDEO_FORMAT_Y42B:
      y4menc->colorspace = "422";
      out_info.stride[0] = width;
      out_info.stride[1] = GST_ROUND_UP_2 (width) / 2;
      out_info.stride[2] = out_info.stride[1];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      out_info.offset[2] = out_info.offset[1] + out_info.stride[1] * height;
      /* simplification of ROUNDUP4(w)*h + 2*(ROUNDUP8(w)/2)*h */
      out_info.size = out_info.offset[2] + out_info.stride[2] * height;
      break;
    case GST_VIDEO_FORMAT_Y41B:
      y4menc->colorspace = "411";
      out_info.stride[0] = width;
      out_info.stride[1] = GST_ROUND_UP_2 (width) / 4;
      out_info.stride[2] = out_info.stride[1];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      out_info.offset[2] = out_info.offset[1] + out_info.stride[1] * height;
      /* simplification of ROUNDUP4(w)*h + 2*((ROUNDUP16(w)/4)*h */
      out_info.size = (width + (GST_ROUND_UP_2 (width) / 2)) * height;
      break;
    case GST_VIDEO_FORMAT_Y444:
      y4menc->colorspace = "444";
      out_info.stride[0] = width;
      out_info.stride[1] = out_info.stride[0];
      out_info.stride[2] = out_info.stride[0];
      out_info.offset[0] = 0;
      out_info.offset[1] = out_info.stride[0] * height;
      out_info.offset[2] = out_info.offset[1] * 2;
      out_info.size = out_info.stride[0] * height * 3;
      break;
    default:
      goto invalid_format;
  }

  y4menc->info = *info;
  y4menc->out_info = out_info;
  y4menc->padded = !gst_video_info_is_equal (info, &out_info);

  output_state =
      gst_video_encoder_set_output_state (encoder,
      gst_static_pad_template_get_caps (&y4mencode_src_factory), state);
  gst_video_codec_state_unref (output_state);

  return gst_video_encoder_negotiate (encoder);

invalid_format:
  {
    GST_ERROR_OBJECT (y4menc, "Invalid format");
    return FALSE;
  }
}

static inline GstBuffer *
gst_y4m_encode_get_stream_header (GstY4mEncode * filter, gboolean tff)
{
  gpointer header;
  GstBuffer *buf;
  gchar interlaced;
  gsize len;

  if (GST_VIDEO_INFO_IS_INTERLACED (&filter->info)) {
    if (tff)
      interlaced = 't';
    else
      interlaced = 'b';
  } else {
    interlaced = 'p';
  }

  header = g_strdup_printf ("YUV4MPEG2 C%s W%d H%d I%c F%d:%d A%d:%d\n",
      filter->colorspace, GST_VIDEO_INFO_WIDTH (&filter->info),
      GST_VIDEO_INFO_HEIGHT (&filter->info), interlaced,
      GST_VIDEO_INFO_FPS_N (&filter->info),
      GST_VIDEO_INFO_FPS_D (&filter->info),
      GST_VIDEO_INFO_PAR_N (&filter->info),
      GST_VIDEO_INFO_PAR_D (&filter->info));
  len = strlen (header);

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, header, len, 0, len, header, g_free));

  return buf;
}

static inline GstBuffer *
gst_y4m_encode_get_frame_header (GstY4mEncode * filter)
{
  gpointer header;
  GstBuffer *buf;
  gsize len;

  header = g_strdup_printf ("FRAME\n");
  len = strlen (header);

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, header, len, 0, len, header, g_free));

  return buf;
}

static GstBuffer *
gst_y4m_encode_copy_buffer (GstY4mEncode * filter, GstBuffer * inbuf)
{
  GstVideoFrame in_frame, out_frame;
  GstBuffer *outbuf = NULL;
  gssize size;
  gboolean copied;

  if (!gst_video_frame_map (&in_frame, &filter->info, inbuf, GST_MAP_READ))
    goto invalid_buffer;

  /* TODO: use a bufferpool */
  size = GST_VIDEO_INFO_SIZE (&filter->out_info);
  outbuf = gst_buffer_new_allocate (NULL, size, NULL);
  if (!outbuf) {
    gst_video_frame_unmap (&in_frame);
    goto invalid_buffer;
  }

  if (!gst_video_frame_map (&out_frame, &filter->out_info, outbuf,
          GST_MAP_WRITE))
    goto invalid_buffer;

  copied = gst_video_frame_copy (&out_frame, &in_frame);

  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&in_frame);

  if (!copied)
    goto invalid_buffer;

  return outbuf;

invalid_buffer:
  {
    GST_ELEMENT_WARNING (filter, STREAM, FORMAT, (NULL),
        ("invalid video buffer"));
    if (outbuf)
      gst_buffer_unref (outbuf);
    return NULL;
  }
}

static gboolean
gst_y4m_encode_buffer_has_padding (GstY4mEncode * y4enc, GstBuffer * inbuf)
{
  GstVideoMeta *vmeta = gst_buffer_get_video_meta (inbuf);
  const GstVideoInfo *out_info = &y4enc->out_info;
  int i;

  if (!vmeta)
    return y4enc->padded;

  for (i = 0; i < vmeta->n_planes; i++) {
    if (vmeta->offset[i] != GST_VIDEO_INFO_PLANE_OFFSET (out_info, i))
      return TRUE;
    if (vmeta->stride[i] != GST_VIDEO_INFO_PLANE_STRIDE (out_info, i))
      return TRUE;
  }

  if (vmeta->alignment.padding_bottom != 0 ||
      vmeta->alignment.padding_left != 0 ||
      vmeta->alignment.padding_right != 0 || vmeta->alignment.padding_top != 0)
    return TRUE;

  return FALSE;
}

static GstFlowReturn
gst_y4m_encode_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstY4mEncode *filter = GST_Y4M_ENCODE (encoder);
  GstBuffer *outbuf;

  /* check we got some decent info from caps */
  if (GST_VIDEO_INFO_FORMAT (&filter->info) == GST_VIDEO_FORMAT_UNKNOWN)
    goto not_negotiated;

  if (G_UNLIKELY (!filter->header)) {
    gboolean tff = FALSE;

    if (GST_VIDEO_INFO_IS_INTERLACED (&filter->info)) {
      tff =
          GST_BUFFER_FLAG_IS_SET (frame->input_buffer,
          GST_VIDEO_BUFFER_FLAG_TFF);
    }
    frame->output_buffer = gst_y4m_encode_get_stream_header (filter, tff);
    filter->header = TRUE;
    frame->output_buffer =
        gst_buffer_append (frame->output_buffer,
        gst_y4m_encode_get_frame_header (filter));
  } else {
    frame->output_buffer = gst_y4m_encode_get_frame_header (filter);
  }

  if (gst_y4m_encode_buffer_has_padding (filter, frame->input_buffer)) {
    outbuf = gst_y4m_encode_copy_buffer (filter, frame->input_buffer);
    if (!outbuf) {
      gst_video_encoder_drop_frame (encoder, frame);
      return GST_FLOW_ERROR;
    }
  } else {
    outbuf = gst_buffer_copy (frame->input_buffer);
  }
  frame->output_buffer = gst_buffer_append (frame->output_buffer, outbuf);

  GST_DEBUG_OBJECT (filter, "output buffer %" GST_PTR_FORMAT,
      frame->output_buffer);

  return gst_video_encoder_finish_frame (encoder, frame);

not_negotiated:
  {
    GST_ELEMENT_ERROR (filter, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated"));

    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return GST_ELEMENT_REGISTER (y4menc, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    y4menc,
    "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
