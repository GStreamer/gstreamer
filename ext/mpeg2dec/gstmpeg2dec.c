/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include <inttypes.h>

#include "gstmpeg2dec.h"

/* 16byte-aligns a buffer for libmpeg2 */
#define ALIGN_16(p) ((void *)(((uintptr_t)(p) + 15) & ~((uintptr_t)15)))

/* mpeg2dec changed a struct name after 0.3.1, here's a workaround */
/* mpeg2dec also only defined MPEG2_RELEASE after 0.3.1
   #if MPEG2_RELEASE < MPEG2_VERSION(0,3,2)
*/
#ifndef MPEG2_RELEASE
#define MPEG2_VERSION(a,b,c) ((((a)&0xff)<<16)|(((b)&0xff)<<8)|((c)&0xff))
#define MPEG2_RELEASE MPEG2_VERSION(0,3,1)
typedef picture_t mpeg2_picture_t;
typedef gint mpeg2_state_t;

#define STATE_BUFFER 0
#endif

GST_DEBUG_CATEGORY_STATIC (mpeg2dec_debug);
#define GST_CAT_DEFAULT (mpeg2dec_debug)

/* Send a warning message about decoding errors after receiving this many
 * STATE_INVALID return values from mpeg2_parse. -1 means never.
 */
#define WARN_THRESHOLD (5)

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) [ 1, 2 ], " "systemstream = (boolean) false")
    );

static GstStaticPadTemplate src_template_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) { YV12, I420, Y42B, Y444 }, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0/1, 2147483647/1 ]")
    );

GST_BOILERPLATE (GstMpeg2dec, gst_mpeg2dec, GstVideoDecoder,
    GST_TYPE_VIDEO_DECODER);

static void gst_mpeg2dec_finalize (GObject * object);

/* GstVideoDecoder base class method */
static gboolean gst_mpeg2dec_open (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_close (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_start (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_stop (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_mpeg2dec_reset (GstVideoDecoder * decoder, gboolean hard);
static GstFlowReturn gst_mpeg2dec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_mpeg2dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);

/* GstElement overload */
static void gst_mpeg2dec_set_index (GstElement * element, GstIndex * index);
static GstIndex *gst_mpeg2dec_get_index (GstElement * element);

static void gst_mpeg2dec_clear_buffers (GstMpeg2dec * mpeg2dec);
static gboolean gst_mpeg2dec_crop_buffer (GstMpeg2dec * dec, GstBuffer ** buf);


static void
gst_mpeg2dec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_static_pad_template (element_class,
      &src_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template_factory);
  gst_element_class_set_details_simple (element_class,
      "mpeg1 and mpeg2 video decoder", "Codec/Decoder/Video",
      "Uses libmpeg2 to decode MPEG video streams",
      "Wim Taymans <wim.taymans@chello.be>");
}

static void
gst_mpeg2dec_class_init (GstMpeg2decClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_mpeg2dec_finalize;

  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_mpeg2dec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_mpeg2dec_close);
  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_mpeg2dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_mpeg2dec_stop);
  video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_mpeg2dec_reset);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_mpeg2dec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_handle_frame);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_mpeg2dec_finish);

  element_class->set_index = gst_mpeg2dec_set_index;
  element_class->get_index = gst_mpeg2dec_get_index;

  GST_DEBUG_CATEGORY_INIT (mpeg2dec_debug, "mpeg2dec", 0,
      "MPEG-2 Video Decoder");
}

static void
gst_mpeg2dec_init (GstMpeg2dec * mpeg2dec, GstMpeg2decClass * klass)
{
  mpeg2dec->can_allocate_aligned = TRUE;
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (mpeg2dec), TRUE);

  /* initialize the mpeg2dec acceleration */
}

static void
gst_mpeg2dec_finalize (GObject * object)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (object);

  if (mpeg2dec->index) {
    gst_object_unref (mpeg2dec->index);
    mpeg2dec->index = NULL;
    mpeg2dec->index_id = 0;
  }

  if (mpeg2dec->decoder) {
    GST_DEBUG_OBJECT (mpeg2dec, "closing decoder");
    mpeg2_close (mpeg2dec->decoder);
    mpeg2dec->decoder = NULL;
  }

  gst_mpeg2dec_clear_buffers (mpeg2dec);
  g_free (mpeg2dec->dummybuf[3]);
  mpeg2dec->dummybuf[3] = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_mpeg2dec_open (GstVideoDecoder * decoder)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  mpeg2_accel (MPEG2_ACCEL_DETECT);
  if ((mpeg2dec->decoder = mpeg2_init ()) == NULL)
    return FALSE;
  mpeg2dec->info = mpeg2_info (mpeg2dec->decoder);

  return TRUE;
}

static gboolean
gst_mpeg2dec_close (GstVideoDecoder * decoder)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  if (mpeg2dec->decoder) {
    mpeg2_close (mpeg2dec->decoder);
    mpeg2dec->decoder = NULL;
    mpeg2dec->info = NULL;
  }
  gst_mpeg2dec_clear_buffers (mpeg2dec);

  return TRUE;
}

static gboolean
gst_mpeg2dec_start (GstVideoDecoder * decoder)
{
  return gst_mpeg2dec_reset (decoder, TRUE);
}

static gboolean
gst_mpeg2dec_stop (GstVideoDecoder * decoder)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  if (mpeg2dec->input_state) {
    gst_video_codec_state_unref (mpeg2dec->input_state);
    mpeg2dec->input_state = NULL;
  }
  return gst_mpeg2dec_reset (decoder, TRUE);
}

static gboolean
gst_mpeg2dec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  /* Save input state to be used as reference for output state */
  if (mpeg2dec->input_state)
    gst_video_codec_state_unref (mpeg2dec->input_state);
  mpeg2dec->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static gboolean
gst_mpeg2dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  GST_DEBUG_OBJECT (mpeg2dec, "%s", hard ? "hard" : "soft");

  GST_OBJECT_LOCK (mpeg2dec);
  if (mpeg2dec->index) {
    gst_object_unref (mpeg2dec->index);
    mpeg2dec->index = NULL;
    mpeg2dec->index_id = 0;
  }
  GST_OBJECT_UNLOCK (mpeg2dec);

  /* reset the initial video state */
  mpeg2dec->width = -1;
  mpeg2dec->height = -1;
  mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
  mpeg2dec->frame_period = 0;
  mpeg2dec->next_time = -1;
  mpeg2dec->offset = 0;
  mpeg2dec->can_allocate_aligned = TRUE;
  mpeg2_reset (mpeg2dec->decoder, hard);
  mpeg2_skip (mpeg2dec->decoder, 1);

  gst_mpeg2dec_clear_buffers (mpeg2dec);

  return TRUE;
}

static GstFlowReturn
gst_mpeg2dec_finish (GstVideoDecoder * decoder)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  if (mpeg2dec->index && mpeg2dec->closed) {
    gst_index_commit (mpeg2dec->index, mpeg2dec->index_id);
  }

  return GST_FLOW_OK;
}

static void
gst_mpeg2dec_set_index (GstElement * element, GstIndex * index)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  GST_OBJECT_LOCK (mpeg2dec);
  if (mpeg2dec->index)
    gst_object_unref (mpeg2dec->index);
  mpeg2dec->index = NULL;
  mpeg2dec->index_id = 0;
  if (index) {
    mpeg2dec->index = gst_object_ref (index);
  }
  GST_OBJECT_UNLOCK (mpeg2dec);
  /* object lock might be taken again */
  if (index)
    gst_index_get_writer_id (index, GST_OBJECT (element), &mpeg2dec->index_id);
}

static GstIndex *
gst_mpeg2dec_get_index (GstElement * element)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  return (mpeg2dec->index) ? gst_object_ref (mpeg2dec->index) : NULL;
}

static GstFlowReturn
gst_mpeg2dec_crop_buffer (GstMpeg2dec * dec, GstBuffer ** buf)
{
  GstVideoInfo *info;
  GstVideoFormat format;
  GstBuffer *inbuf = *buf;
  GstBuffer *outbuf;
  guint c;

  info = &gst_video_decoder_get_output_state (GST_VIDEO_DECODER (dec))->info;
  format = GST_VIDEO_INFO_FORMAT (info);

  GST_LOG_OBJECT (dec, "Copying input buffer %ux%u (%u) to output buffer "
      "%ux%u (%u)", dec->decoded_width, dec->decoded_height,
      GST_BUFFER_SIZE (inbuf), info->width, info->height, info->size);

  outbuf = gst_video_decoder_alloc_output_buffer (GST_VIDEO_DECODER (dec));

  for (c = 0; c < 3; c++) {
    const guint8 *src;
    guint8 *dest;
    guint stride_in, stride_out;
    guint c_height, c_width, line;

    src =
        GST_BUFFER_DATA (inbuf) +
        gst_video_format_get_component_offset (format, c, dec->decoded_width,
        dec->decoded_height);
    dest =
        GST_BUFFER_DATA (outbuf) +
        gst_video_format_get_component_offset (format, c, info->width,
        dec->height);
    stride_out = gst_video_format_get_row_stride (format, c, info->width);
    stride_in = gst_video_format_get_row_stride (format, c, dec->decoded_width);
    c_height = gst_video_format_get_component_height (format, c, info->height);
    c_width = gst_video_format_get_component_width (format, c, info->width);

    GST_DEBUG ("stride_in:%d _out:%d c_width:%d c_height:%d",
        stride_in, stride_out, c_width, c_height);

    if (stride_in == stride_out && stride_in == c_width) {
      /* FAST PATH */
      memcpy (dest, src, c_height * stride_out);
      dest += stride_out * c_height;
      src += stride_out * c_height;
    } else {
      for (line = 0; line < c_height; line++) {
        memcpy (dest, src, c_width);
        dest += stride_out;
        src += stride_in;
      }
    }
  }

  gst_buffer_unref (*buf);
  *buf = outbuf;

  return GST_FLOW_OK;
}

static void
gst_mpeg2dec_alloc_sized_buf (GstMpeg2dec * mpeg2dec, guint size,
    GstBuffer ** obuf)
{
  if (mpeg2dec->can_allocate_aligned
      && mpeg2dec->decoded_width == mpeg2dec->width
      && mpeg2dec->decoded_height == mpeg2dec->height) {

    *obuf =
        gst_video_decoder_alloc_output_buffer (GST_VIDEO_DECODER (mpeg2dec));

    /* libmpeg2 needs 16 byte aligned buffers... test for this here
     * and if it fails only a single time create our own buffers from
     * there on below that are correctly aligned */
    if (((uintptr_t) GST_BUFFER_DATA (*obuf)) % 16 == 0) {
      GST_LOG_OBJECT (mpeg2dec, "return 16 byte aligned buffer");
      return;
    }

    GST_DEBUG_OBJECT (mpeg2dec,
        "can't get 16 byte aligned buffers, creating our own ones");
    gst_buffer_unref (*obuf);
    mpeg2dec->can_allocate_aligned = FALSE;
  }

  /* can't use gst_pad_alloc_buffer() here because the output buffer will
   * either be cropped later or be bigger than expected (for the alignment),
   * and basetransform-based elements will complain about the wrong unit size
   * when not operating in passthrough mode */
  *obuf = gst_buffer_new_and_alloc (size + 15);
  GST_BUFFER_DATA (*obuf) = (guint8 *) ALIGN_16 (GST_BUFFER_DATA (*obuf));
  GST_BUFFER_SIZE (*obuf) = size;
}

typedef struct
{
  GstBuffer *buffer;
  gint id;
} GstMpeg2DecBuffer;

static void
gst_mpeg2dec_clear_buffers (GstMpeg2dec * mpeg2dec)
{
  GList *l;
  while ((l = g_list_first (mpeg2dec->buffers))) {
    GstMpeg2DecBuffer *mbuf = l->data;
    gst_buffer_unref (mbuf->buffer);
    g_slice_free (GstMpeg2DecBuffer, mbuf);
    mpeg2dec->buffers = g_list_delete_link (mpeg2dec->buffers, l);
  }
}

static void
gst_mpeg2dec_save_buffer (GstMpeg2dec * mpeg2dec, GstBuffer * buffer, gint id)
{
  GstMpeg2DecBuffer *mbuf;

  mbuf = g_slice_new0 (GstMpeg2DecBuffer);
  mbuf->buffer = gst_buffer_ref (buffer);
  mbuf->id = id;

  mpeg2dec->buffers = g_list_prepend (mpeg2dec->buffers, mbuf);
}

static gint
gst_mpeg2dec_buffer_compare (GstMpeg2DecBuffer * mbuf, gconstpointer id)
{
  if (mbuf->id == GPOINTER_TO_INT (id))
    return 0;
  return -1;
}

static void
gst_mpeg2dec_discard_buffer (GstMpeg2dec * mpeg2dec, gint id)
{
  GList *l = g_list_find_custom (mpeg2dec->buffers, GINT_TO_POINTER (id),
      (GCompareFunc) gst_mpeg2dec_buffer_compare);

  if (l) {
    GstMpeg2DecBuffer *mbuf = l->data;
    gst_buffer_unref (mbuf->buffer);
    g_slice_free (GstMpeg2DecBuffer, mbuf);
    mpeg2dec->buffers = g_list_delete_link (mpeg2dec->buffers, l);
  } else {
    GST_WARNING ("Could not find buffer %u, will be leaked until next reset");
  }
}

static void
gst_mpeg2dec_alloc_buffer (GstMpeg2dec * mpeg2dec, gint64 offset,
    GstVideoCodecFrame * frame)
{
  guint8 *buf[3];

  gst_mpeg2dec_alloc_sized_buf (mpeg2dec, mpeg2dec->size,
      &frame->output_buffer);

  buf[0] = GST_BUFFER_DATA (frame->output_buffer);
  buf[1] = buf[0] + mpeg2dec->u_offs;
  buf[2] = buf[0] + mpeg2dec->v_offs;

  GST_DEBUG_OBJECT (mpeg2dec, "set_buf: %p %p %p, frame %i",
      buf[0], buf[1], buf[2], frame->system_frame_number);

  mpeg2_set_buf (mpeg2dec->decoder, buf,
      GINT_TO_POINTER (frame->system_frame_number));
  gst_mpeg2dec_save_buffer (mpeg2dec, frame->output_buffer,
      frame->system_frame_number);

  /* we store the original byteoffset of this picture in the stream here
   * because we need it for indexing */
  GST_BUFFER_OFFSET (frame->output_buffer) = offset;
}

static gboolean
gst_mpeg2dec_negotiate_format (GstMpeg2dec * mpeg2dec)
{
  GstVideoCodecState *new_state;
  GstVideoFormat format;
  const mpeg2_info_t *info;
  const mpeg2_sequence_t *sequence;
  gboolean ret = FALSE;
  GstVideoInfo *vinfo;

  info = mpeg2_info (mpeg2dec->decoder);
  sequence = info->sequence;

  if (sequence->width != sequence->chroma_width &&
      sequence->height != sequence->chroma_height) {
    format = GST_VIDEO_FORMAT_I420;
  } else if ((sequence->width == sequence->chroma_width &&
          sequence->height != sequence->chroma_height) ||
      (sequence->width != sequence->chroma_width &&
          sequence->height == sequence->chroma_height)) {
    format = GST_VIDEO_FORMAT_Y42B;
  } else {
    format = GST_VIDEO_FORMAT_Y444;
  }

  new_state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (mpeg2dec),
      format, mpeg2dec->width, mpeg2dec->height, mpeg2dec->input_state);

  vinfo = &new_state->info;

  /* Ensure interlace caps are set, needed if not using mpegvideoparse */
  if (mpeg2dec->interlaced)
    GST_VIDEO_INFO_INTERLACE_MODE (vinfo) =
        GST_VIDEO_INTERLACE_MODE_INTERLEAVED;

  mpeg2dec->size = gst_video_format_get_size (format,
      mpeg2dec->decoded_width, mpeg2dec->decoded_height);
  mpeg2dec->u_offs = gst_video_format_get_component_offset (format, 1,
      mpeg2dec->decoded_width, mpeg2dec->decoded_height);
  mpeg2dec->v_offs = gst_video_format_get_component_offset (format, 2,
      mpeg2dec->decoded_width, mpeg2dec->decoded_height);

  /* If we don't have a valid upstream PAR override it */
  if (GST_VIDEO_INFO_PAR_N (vinfo) == 1 &&
      GST_VIDEO_INFO_PAR_D (vinfo) == 1 &&
      mpeg2dec->pixel_width != 0 && mpeg2dec->pixel_height != 0) {
    GST_DEBUG_OBJECT (mpeg2dec, "Setting PAR %d x %d",
        mpeg2dec->pixel_width, mpeg2dec->pixel_height);
    GST_VIDEO_INFO_PAR_N (vinfo) = mpeg2dec->pixel_width;
    GST_VIDEO_INFO_PAR_D (vinfo) = mpeg2dec->pixel_height;
  }

  if (new_state) {
    gst_video_codec_state_unref (new_state);
    ret = TRUE;
  }

  return ret;
}

static void
init_dummybuf (GstMpeg2dec * mpeg2dec)
{
  g_free (mpeg2dec->dummybuf[3]);

  /* libmpeg2 needs 16 byte aligned buffers... care for this here */
  mpeg2dec->dummybuf[3] = g_malloc0 (mpeg2dec->size + 15);
  mpeg2dec->dummybuf[0] = ALIGN_16 (mpeg2dec->dummybuf[3]);
  mpeg2dec->dummybuf[1] = mpeg2dec->dummybuf[0] + mpeg2dec->u_offs;
  mpeg2dec->dummybuf[2] = mpeg2dec->dummybuf[0] + mpeg2dec->v_offs;
}

static GstFlowReturn
handle_sequence (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime latency;

  if (info->sequence->frame_period == 0) {
    GST_WARNING_OBJECT (mpeg2dec, "Frame period is 0!");
    ret = GST_FLOW_ERROR;
    goto done;
  }

  mpeg2dec->width = info->sequence->picture_width;
  mpeg2dec->height = info->sequence->picture_height;
  mpeg2dec->decoded_width = info->sequence->width;
  mpeg2dec->decoded_height = info->sequence->height;

  mpeg2dec->pixel_width = info->sequence->pixel_width;
  mpeg2dec->pixel_height = info->sequence->pixel_height;
  GST_DEBUG_OBJECT (mpeg2dec, "pixel_width:%d pixel_height:%d",
      mpeg2dec->pixel_width, mpeg2dec->pixel_height);

  /* mpeg2 video can only be from 16x16 to 4096x4096. Everything
   * else is a corrupted files */
  if (mpeg2dec->width > 4096 || mpeg2dec->width < 16 ||
      mpeg2dec->height > 4096 || mpeg2dec->height < 16) {
    GST_ERROR_OBJECT (mpeg2dec, "Invalid frame dimensions: %d x %d",
        mpeg2dec->width, mpeg2dec->height);
    return GST_FLOW_ERROR;
  }

  /* set framerate */
  mpeg2dec->fps_n = 27000000;
  mpeg2dec->fps_d = info->sequence->frame_period;
  mpeg2dec->frame_period =
      gst_util_uint64_scale_ceil (info->sequence->frame_period, GST_USECOND,
      27);

  /* Mpeg2dec has 2 frame latency to produce a picture and 1 frame latency in
   * it's parser */
  latency = 3 * mpeg2dec->frame_period;
  gst_video_decoder_set_latency (GST_VIDEO_DECODER (mpeg2dec), latency,
      latency);

  mpeg2dec->interlaced =
      !(info->sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE);

  GST_DEBUG_OBJECT (mpeg2dec,
      "sequence flags: %d, frame period: %d (%g), frame rate: %d/%d",
      info->sequence->flags, info->sequence->frame_period,
      (double) (mpeg2dec->frame_period) / GST_SECOND, mpeg2dec->fps_n,
      mpeg2dec->fps_d);
  GST_DEBUG_OBJECT (mpeg2dec, "profile: %02x, colour_primaries: %d",
      info->sequence->profile_level_id, info->sequence->colour_primaries);
  GST_DEBUG_OBJECT (mpeg2dec, "transfer chars: %d, matrix coef: %d",
      info->sequence->transfer_characteristics,
      info->sequence->matrix_coefficients);
  GST_DEBUG_OBJECT (mpeg2dec,
      "FLAGS: CONSTRAINED_PARAMETERS:%d, PROGRESSIVE_SEQUENCE:%d",
      info->sequence->flags & SEQ_FLAG_CONSTRAINED_PARAMETERS,
      info->sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE);
  GST_DEBUG_OBJECT (mpeg2dec, "FLAGS: LOW_DELAY:%d, COLOUR_DESCRIPTION:%d",
      info->sequence->flags & SEQ_FLAG_LOW_DELAY,
      info->sequence->flags & SEQ_FLAG_COLOUR_DESCRIPTION);

  if (!gst_mpeg2dec_negotiate_format (mpeg2dec))
    goto negotiate_failed;

  mpeg2_custom_fbuf (mpeg2dec->decoder, 1);

  init_dummybuf (mpeg2dec);

  /* Pump in some null buffers, because otherwise libmpeg2 doesn't
   * initialise the discard_fbuf->id */
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  gst_mpeg2dec_clear_buffers (mpeg2dec);

done:
  return ret;

negotiate_failed:
  {
    GST_ELEMENT_ERROR (mpeg2dec, CORE, NEGOTIATION, (NULL), (NULL));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static GstFlowReturn
handle_picture (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info,
    GstVideoCodecFrame * frame)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint type;
  const gchar *type_str = NULL;
  gboolean key_frame = FALSE;
  const mpeg2_picture_t *picture = info->current_picture;

  gst_mpeg2dec_alloc_buffer (mpeg2dec, mpeg2dec->offset, frame);

  type = picture->flags & PIC_MASK_CODING_TYPE;
  switch (type) {
    case PIC_FLAG_CODING_TYPE_I:
      key_frame = TRUE;
      mpeg2_skip (mpeg2dec->decoder, 0);
      type_str = "I";
      break;
    case PIC_FLAG_CODING_TYPE_P:
      type_str = "P";
      break;
    case PIC_FLAG_CODING_TYPE_B:
      type_str = "B";
      break;
    default:
      gst_video_codec_frame_ref (frame);
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
      GST_VIDEO_DECODER_ERROR (mpeg2dec, 1, STREAM, DECODE,
          ("decoding error"), ("Invalid picture type"), ret);
      return ret;
  }

  GST_DEBUG_OBJECT (mpeg2dec, "handle picture type %s", type_str);
  GST_DEBUG_OBJECT (mpeg2dec, "picture %s, frame %i, offset %" G_GINT64_FORMAT,
      key_frame ? ", kf," : "    ", frame->system_frame_number,
      GST_BUFFER_OFFSET (frame->output_buffer));

  if (picture->flags & PIC_FLAG_TOP_FIELD_FIRST) {
    GST_VIDEO_CODEC_FRAME_FLAG_SET (frame, GST_VIDEO_CODEC_FRAME_FLAG_TFF);
  }
#if MPEG2_RELEASE >= MPEG2_VERSION(0,5,0)
  /* repeat field introduced in 0.5.0 */
  if (picture->flags & PIC_FLAG_REPEAT_FIRST_FIELD) {
    GST_VIDEO_CODEC_FRAME_FLAG_SET (frame, GST_VIDEO_CODEC_FRAME_FLAG_RFF);
  }
#endif

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_PICTURE && key_frame) {
    mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_KEYFRAME;
  }

  GST_DEBUG_OBJECT (mpeg2dec,
      "picture: %s %s %s %s %s fields:%d off:%" G_GINT64_FORMAT " ts:%"
      GST_TIME_FORMAT,
      (picture->flags & PIC_FLAG_PROGRESSIVE_FRAME ? "prog" : "    "),
      (picture->flags & PIC_FLAG_TOP_FIELD_FIRST ? "tff" : "   "),
#if MPEG2_RELEASE >= MPEG2_VERSION(0,5,0)
      (picture->flags & PIC_FLAG_REPEAT_FIRST_FIELD ? "rff" : "   "),
#else
      "unknown rff",
#endif
      (picture->flags & PIC_FLAG_SKIP ? "skip" : "    "),
      (picture->flags & PIC_FLAG_COMPOSITE_DISPLAY ? "composite" : "         "),
      picture->nb_fields, mpeg2dec->offset, GST_TIME_ARGS (frame->pts));

  return GST_FLOW_OK;
}

static GstFlowReturn
handle_slice (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame;
  const mpeg2_picture_t *picture;
  gboolean key_frame = FALSE;

  GST_DEBUG_OBJECT (mpeg2dec, "picture slice/end %p %p %p %p",
      info->display_fbuf, info->display_picture, info->current_picture,
      info->display_fbuf->id);

  frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (mpeg2dec),
      GPOINTER_TO_INT (info->display_fbuf->id));
  picture = info->display_picture;
  key_frame = (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I;

  if (G_UNLIKELY (!frame)) {
    GST_WARNING ("display buffer does not have a valid frame");
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (mpeg2dec, "picture flags: %d, type: %d, keyframe: %d",
      picture->flags, picture->flags & PIC_MASK_CODING_TYPE, key_frame);

  if (key_frame) {
    mpeg2_skip (mpeg2dec->decoder, 0);
  }

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_KEYFRAME && key_frame)
    mpeg2dec->discont_state = MPEG2DEC_DISC_NONE;

  if (mpeg2dec->index) {
    gst_index_add_association (mpeg2dec->index, mpeg2dec->index_id,
        (key_frame ? GST_ASSOCIATION_FLAG_KEY_UNIT :
            GST_ASSOCIATION_FLAG_DELTA_UNIT),
        GST_FORMAT_BYTES, GST_BUFFER_OFFSET (frame->output_buffer),
        GST_FORMAT_TIME, frame->pts, 0);
  }

  if (picture->flags & PIC_FLAG_SKIP) {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer because of skip flag");
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
    mpeg2_skip (mpeg2dec->decoder, 1);
    return GST_FLOW_OK;
  }

  if (mpeg2dec->discont_state != MPEG2DEC_DISC_NONE) {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, discont state %d",
        mpeg2dec->discont_state);
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
    return GST_FLOW_OK;
  }

  /* do cropping if the target region is smaller than the input one */
  if (mpeg2dec->decoded_width != mpeg2dec->width ||
      mpeg2dec->decoded_height != mpeg2dec->height) {
    if (gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (mpeg2dec),
            frame) < 0) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer crop, too late");
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
      return GST_FLOW_OK;
    }

    ret = gst_mpeg2dec_crop_buffer (mpeg2dec, &frame->output_buffer);
  }

  GST_DEBUG_OBJECT (mpeg2dec, "cropping buffer");
  gst_video_decoder_finish_frame (GST_VIDEO_DECODER (mpeg2dec), frame);

  return ret;
}

static GstFlowReturn
gst_mpeg2dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);
  GstBuffer *buf = frame->input_buffer;
  guint32 size;
  guint8 *data, *end;
  const mpeg2_info_t *info;
  mpeg2_state_t state;
  gboolean done = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (mpeg2dec, "received buffer, timestamp %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->duration));

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);

  info = mpeg2dec->info;
  end = data + size;

  mpeg2dec->offset = GST_BUFFER_OFFSET (buf);

  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer");
  mpeg2_buffer (mpeg2dec->decoder, data, end);
  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer done");

  while (!done) {
    GST_LOG_OBJECT (mpeg2dec, "calling parse");
    state = mpeg2_parse (mpeg2dec->decoder);
    GST_DEBUG_OBJECT (mpeg2dec, "parse state %d", state);

    switch (state) {
#if MPEG2_RELEASE >= MPEG2_VERSION (0, 5, 0)
      case STATE_SEQUENCE_MODIFIED:
        GST_DEBUG_OBJECT (mpeg2dec, "sequence modified");
        /* fall through */
#endif
      case STATE_SEQUENCE:
        ret = handle_sequence (mpeg2dec, info);
        /* if there is an error handling the sequence
         * reset the decoder, maybe something more elegant
         * could be done.
         */
        if (ret == GST_FLOW_ERROR) {
          GST_VIDEO_DECODER_ERROR (decoder, 1, STREAM, DECODE,
              ("decoding error"), ("Bad sequence header"), ret);
          gst_video_decoder_drop_frame (decoder, frame);
          gst_mpeg2dec_reset (decoder, 0);
          goto done;
        }
        break;
      case STATE_SEQUENCE_REPEATED:
        GST_DEBUG_OBJECT (mpeg2dec, "sequence repeated");
        break;
      case STATE_GOP:
        GST_DEBUG_OBJECT (mpeg2dec, "gop");
        break;
      case STATE_PICTURE:
        ret = handle_picture (mpeg2dec, info, frame);
        break;
      case STATE_SLICE_1ST:
        GST_LOG_OBJECT (mpeg2dec, "1st slice of frame encountered");
        break;
      case STATE_PICTURE_2ND:
        GST_LOG_OBJECT (mpeg2dec,
            "Second picture header encountered. Decoding 2nd field");
        break;
#if MPEG2_RELEASE >= MPEG2_VERSION (0, 4, 0)
      case STATE_INVALID_END:
        GST_DEBUG_OBJECT (mpeg2dec, "invalid end");
#endif
      case STATE_END:
        GST_DEBUG_OBJECT (mpeg2dec, "end");
      case STATE_SLICE:
        if (info->display_fbuf && info->display_fbuf->id) {
          ret = handle_slice (mpeg2dec, info);
        } else {
          GST_DEBUG_OBJECT (mpeg2dec, "no picture to display");
        }
        if (info->discard_fbuf && info->discard_fbuf->id)
          gst_mpeg2dec_discard_buffer (mpeg2dec,
              GPOINTER_TO_INT (info->discard_fbuf->id));
        if (state != STATE_SLICE) {
          gst_mpeg2dec_clear_buffers (mpeg2dec);
        }
        break;
      case STATE_BUFFER:
        done = TRUE;
        break;
        /* error */
      case STATE_INVALID:
        GST_VIDEO_DECODER_ERROR (decoder, 1, STREAM, DECODE,
            ("decoding error"), ("Reached libmpeg2 invalid state"), ret);
        continue;
      default:
        GST_ERROR_OBJECT (mpeg2dec, "Unknown libmpeg2 state %d, FIXME", state);
        ret = GST_FLOW_OK;
        gst_video_codec_frame_unref (frame);
        goto done;
    }

    if (ret != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (mpeg2dec, "exit loop, reason %s",
          gst_flow_get_name (ret));
      break;
    }
  }

  gst_video_codec_frame_unref (frame);

done:
  return ret;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  if (!gst_element_register (plugin, "mpeg2dec", GST_RANK_PRIMARY,
          GST_TYPE_MPEG2DEC))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "mpeg2dec",
    "LibMpeg2 decoder", plugin_init, VERSION, "GPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
