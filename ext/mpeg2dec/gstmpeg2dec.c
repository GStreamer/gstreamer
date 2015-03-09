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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>

#include <inttypes.h>

#include "gstmpeg2dec.h"

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

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
GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

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
    GST_STATIC_CAPS ("video/x-raw, "
        "format = (string) { YV12, I420, Y42B, Y444 }, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], "
        "framerate = (fraction) [ 0/1, 2147483647/1 ]")
    );

#define gst_mpeg2dec_parent_class parent_class
G_DEFINE_TYPE (GstMpeg2dec, gst_mpeg2dec, GST_TYPE_VIDEO_DECODER);

static void gst_mpeg2dec_finalize (GObject * object);

/* GstVideoDecoder base class method */
static gboolean gst_mpeg2dec_open (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_close (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_start (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_stop (GstVideoDecoder * decoder);
static gboolean gst_mpeg2dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_mpeg2dec_flush (GstVideoDecoder * decoder);
static GstFlowReturn gst_mpeg2dec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_mpeg2dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_mpeg2dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

static void gst_mpeg2dec_clear_buffers (GstMpeg2dec * mpeg2dec);
static gboolean gst_mpeg2dec_crop_buffer (GstMpeg2dec * dec,
    GstVideoCodecFrame * in_frame, GstVideoFrame * in_vframe);

static void
gst_mpeg2dec_class_init (GstMpeg2decClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_mpeg2dec_finalize;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
  gst_element_class_set_static_metadata (element_class,
      "mpeg1 and mpeg2 video decoder", "Codec/Decoder/Video",
      "Uses libmpeg2 to decode MPEG video streams",
      "Wim Taymans <wim.taymans@chello.be>");

  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_mpeg2dec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_mpeg2dec_close);
  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_mpeg2dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_mpeg2dec_stop);
  video_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_mpeg2dec_flush);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_mpeg2dec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_handle_frame);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_mpeg2dec_finish);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_mpeg2dec_decide_allocation);

  GST_DEBUG_CATEGORY_INIT (mpeg2dec_debug, "mpeg2dec", 0,
      "MPEG-2 Video Decoder");
}

static void
gst_mpeg2dec_init (GstMpeg2dec * mpeg2dec)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (mpeg2dec), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (mpeg2dec), TRUE);

  /* initialize the mpeg2dec acceleration */
}

static void
gst_mpeg2dec_finalize (GObject * object)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (object);

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
gst_mpeg2dec_start (GstVideoDecoder * decoder)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;

  return TRUE;
}

static gboolean
gst_mpeg2dec_stop (GstVideoDecoder * decoder)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  mpeg2_reset (mpeg2dec->decoder, 0);
  mpeg2_skip (mpeg2dec->decoder, 1);

  gst_mpeg2dec_clear_buffers (mpeg2dec);

  if (mpeg2dec->input_state)
    gst_video_codec_state_unref (mpeg2dec->input_state);
  mpeg2dec->input_state = NULL;

  return TRUE;
}

static gboolean
gst_mpeg2dec_flush (GstVideoDecoder * decoder)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);

  /* reset the initial video state */
  mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
  mpeg2_reset (mpeg2dec->decoder, 1);
  mpeg2_skip (mpeg2dec->decoder, 1);

  gst_mpeg2dec_clear_buffers (mpeg2dec);

  return TRUE;
}

static GstFlowReturn
gst_mpeg2dec_finish (GstVideoDecoder * decoder)
{
  return GST_FLOW_OK;
}

static gboolean
gst_mpeg2dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstMpeg2dec *dec = GST_MPEG2DEC (decoder);
  GstVideoCodecState *state;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean update_allocator;

  /* Set allocation parameters to guarantee 16-byte aligned output buffers */
  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    update_allocator = TRUE;
  } else {
    allocator = NULL;
    gst_allocation_params_init (&params);
    update_allocator = FALSE;
  }

  params.align = MAX (params.align, 15);

  if (update_allocator)
    gst_query_set_nth_allocation_param (query, 0, allocator, &params);
  else
    gst_query_add_allocation_param (query, allocator, &params);
  if (allocator)
    gst_object_unref (allocator);

  /* Now chain up to the parent class to guarantee that we can
   * get a buffer pool from the query */
  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  state = gst_video_decoder_get_output_state (decoder);

  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  dec->has_cropping = FALSE;
  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    dec->has_cropping =
        gst_query_find_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE,
        NULL);
  }

  if (dec->has_cropping) {
    GstCaps *caps;

    /* Calculate uncropped size */
    size = MAX (size, dec->decoded_info.size);
    caps = gst_video_info_to_caps (&dec->decoded_info);
    gst_buffer_pool_config_set_params (config, caps, size, min, max);
    gst_caps_unref (caps);
  }

  gst_buffer_pool_set_config (pool, config);

  gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);

  gst_object_unref (pool);
  gst_video_codec_state_unref (state);

  return TRUE;
}

static GstFlowReturn
gst_mpeg2dec_crop_buffer (GstMpeg2dec * dec, GstVideoCodecFrame * in_frame,
    GstVideoFrame * input_vframe)
{
  GstVideoCodecState *state;
  GstVideoInfo *info;
  GstVideoInfo *dinfo;
  guint c, n_planes;
  GstVideoFrame output_frame;
  GstFlowReturn ret;

  state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (dec));
  info = &state->info;
  dinfo = &dec->decoded_info;

  GST_CAT_LOG_OBJECT (GST_CAT_PERFORMANCE, dec,
      "Copying input buffer %ux%u (%" G_GSIZE_FORMAT ") to output buffer "
      "%ux%u (%" G_GSIZE_FORMAT ")", dinfo->width, dinfo->height,
      dinfo->size, info->width, info->height, info->size);

  ret =
      gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (dec),
      in_frame);
  if (ret != GST_FLOW_OK)
    goto beach;

  if (!gst_video_frame_map (&output_frame, info, in_frame->output_buffer,
          GST_MAP_WRITE))
    goto map_fail;

  n_planes = GST_VIDEO_FRAME_N_PLANES (&output_frame);
  for (c = 0; c < n_planes; c++) {
    guint w, h, j;
    guint8 *sp, *dp;
    gint ss, ds;

    sp = GST_VIDEO_FRAME_PLANE_DATA (input_vframe, c);
    dp = GST_VIDEO_FRAME_PLANE_DATA (&output_frame, c);

    ss = GST_VIDEO_FRAME_PLANE_STRIDE (input_vframe, c);
    ds = GST_VIDEO_FRAME_PLANE_STRIDE (&output_frame, c);

    w = MIN (ABS (ss), ABS (ds));
    h = GST_VIDEO_FRAME_COMP_HEIGHT (&output_frame, c);

    GST_CAT_DEBUG (GST_CAT_PERFORMANCE, "copy plane %u, w:%u h:%u ", c, w, h);

    for (j = 0; j < h; j++) {
      memcpy (dp, sp, w);
      dp += ds;
      sp += ss;
    }
  }

  gst_video_frame_unmap (&output_frame);

  GST_BUFFER_FLAGS (in_frame->output_buffer) =
      GST_BUFFER_FLAGS (input_vframe->buffer);

beach:
  gst_video_codec_state_unref (state);

  return ret;

map_fail:
  {
    GST_ERROR_OBJECT (dec, "Failed to map output frame");
    gst_video_codec_state_unref (state);
    return GST_FLOW_ERROR;
  }
}

static void
frame_user_data_destroy_notify (GstBuffer * buf)
{
  GST_DEBUG ("Releasing buffer %p", buf);
  if (buf)
    gst_buffer_unref (buf);
}

static GstFlowReturn
gst_mpeg2dec_alloc_sized_buf (GstMpeg2dec * mpeg2dec, guint size,
    GstVideoCodecFrame * frame, GstBuffer ** buffer)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecState *state;

  state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (mpeg2dec));

  if (!mpeg2dec->need_cropping || mpeg2dec->has_cropping) {
    /* need parsed input, but that might be slightly bogus,
     * so avoid giving up altogether and mark it as error */
    if (frame->output_buffer) {
      gst_buffer_replace (&frame->output_buffer, NULL);
      GST_VIDEO_DECODER_ERROR (mpeg2dec, 1, STREAM, DECODE,
          ("decoding error"), ("Input not correctly parsed"), ret);
    }
    ret =
        gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (mpeg2dec),
        frame);
    *buffer = frame->output_buffer;
  } else {
    GstAllocationParams params = { 0, 15, 0, 0 };

    *buffer = gst_buffer_new_allocate (NULL, size, &params);
    gst_video_codec_frame_set_user_data (frame, *buffer,
        (GDestroyNotify) frame_user_data_destroy_notify);
  }

  gst_video_codec_state_unref (state);

  return ret;
}

typedef struct
{
  gint id;
  GstVideoFrame frame;
} GstMpeg2DecBuffer;

static void
gst_mpeg2dec_clear_buffers (GstMpeg2dec * mpeg2dec)
{
  GList *l;
  while ((l = g_list_first (mpeg2dec->buffers))) {
    GstMpeg2DecBuffer *mbuf = l->data;
    gst_video_frame_unmap (&mbuf->frame);
    g_slice_free (GstMpeg2DecBuffer, mbuf);
    mpeg2dec->buffers = g_list_delete_link (mpeg2dec->buffers, l);
  }
}

static void
gst_mpeg2dec_save_buffer (GstMpeg2dec * mpeg2dec, gint id,
    GstVideoFrame * frame)
{
  GstMpeg2DecBuffer *mbuf;

  GST_LOG_OBJECT (mpeg2dec, "Saving local info for frame %d", id);

  mbuf = g_slice_new0 (GstMpeg2DecBuffer);
  mbuf->id = id;
  mbuf->frame = *frame;

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
    gst_video_frame_unmap (&mbuf->frame);
    g_slice_free (GstMpeg2DecBuffer, mbuf);
    mpeg2dec->buffers = g_list_delete_link (mpeg2dec->buffers, l);
    GST_LOG_OBJECT (mpeg2dec, "Discarded local info for frame %d", id);
  } else {
    GST_WARNING ("Could not find buffer %d, will be leaked until next reset",
        id);
  }
}

static GstVideoFrame *
gst_mpeg2dec_get_buffer (GstMpeg2dec * mpeg2dec, gint id)
{
  GList *l = g_list_find_custom (mpeg2dec->buffers, GINT_TO_POINTER (id),
      (GCompareFunc) gst_mpeg2dec_buffer_compare);

  if (l) {
    GstMpeg2DecBuffer *mbuf = l->data;
    return &mbuf->frame;
  }

  return NULL;
}

static GstFlowReturn
gst_mpeg2dec_alloc_buffer (GstMpeg2dec * mpeg2dec, GstVideoCodecFrame * frame,
    GstBuffer ** buffer)
{
  GstFlowReturn ret;
  GstVideoFrame vframe;
  guint8 *buf[3];

  ret =
      gst_mpeg2dec_alloc_sized_buf (mpeg2dec, mpeg2dec->decoded_info.size,
      frame, buffer);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto beach;

  if (mpeg2dec->need_cropping && mpeg2dec->has_cropping) {
    GstVideoCropMeta *crop;
    GstVideoCodecState *state;
    GstVideoInfo *vinfo;

    state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (mpeg2dec));
    vinfo = &state->info;

    crop = gst_buffer_add_video_crop_meta (frame->output_buffer);
    /* we can do things slightly more efficient when we know that
     * downstream understands clipping */
    crop->x = 0;
    crop->y = 0;
    crop->width = vinfo->width;
    crop->height = vinfo->height;

    gst_video_codec_state_unref (state);
  }

  if (!gst_video_frame_map (&vframe, &mpeg2dec->decoded_info, *buffer,
          GST_MAP_READ | GST_MAP_WRITE))
    goto map_fail;

  buf[0] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
  buf[1] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 1);
  buf[2] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 2);

  GST_DEBUG_OBJECT (mpeg2dec, "set_buf: %p %p %p, frame %i",
      buf[0], buf[1], buf[2], frame->system_frame_number);

  /* Note: We use a non-null 'id' value to make the distinction
   * between the dummy buffers (which have an id of NULL) and the
   * ones we did */
  mpeg2_set_buf (mpeg2dec->decoder, buf,
      GINT_TO_POINTER (frame->system_frame_number + 1));
  gst_mpeg2dec_save_buffer (mpeg2dec, frame->system_frame_number, &vframe);

beach:
  return ret;

map_fail:
  {
    GST_ERROR_OBJECT (mpeg2dec, "Failed to map frame");
    return GST_FLOW_ERROR;
  }
}

static void
init_dummybuf (GstMpeg2dec * mpeg2dec)
{
  g_free (mpeg2dec->dummybuf[3]);

  /* libmpeg2 needs 16 byte aligned buffers... care for this here */
  mpeg2dec->dummybuf[3] = g_malloc0 (mpeg2dec->decoded_info.size + 15);
  mpeg2dec->dummybuf[0] = ALIGN_16 (mpeg2dec->dummybuf[3]);
  mpeg2dec->dummybuf[1] =
      mpeg2dec->dummybuf[0] +
      GST_VIDEO_INFO_PLANE_OFFSET (&mpeg2dec->decoded_info, 1);
  mpeg2dec->dummybuf[2] =
      mpeg2dec->dummybuf[0] +
      GST_VIDEO_INFO_PLANE_OFFSET (&mpeg2dec->decoded_info, 2);
}

static GstFlowReturn
handle_sequence (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime latency;
  const mpeg2_sequence_t *sequence;
  GstVideoCodecState *state;
  GstVideoInfo *dinfo = &mpeg2dec->decoded_info;
  GstVideoInfo *vinfo;
  GstVideoInfo pre_crop_info;
  GstVideoFormat format;

  sequence = info->sequence;

  if (sequence->frame_period == 0)
    goto invalid_frame_period;

  /* mpeg2 video can only be from 16x16 to 4096x4096. Everything
   * else is a corrupted file */
  if (sequence->width > 4096 || sequence->width < 16 ||
      sequence->height > 4096 || sequence->height < 16)
    goto invalid_size;

  GST_DEBUG_OBJECT (mpeg2dec,
      "widthxheight: %dx%d , decoded_widthxheight: %dx%d",
      sequence->picture_width, sequence->picture_height, sequence->width,
      sequence->height);

  if (sequence->picture_width != sequence->width ||
      sequence->picture_height != sequence->height) {
    GST_DEBUG_OBJECT (mpeg2dec, "we need to crop");
    mpeg2dec->need_cropping = TRUE;
  } else {
    GST_DEBUG_OBJECT (mpeg2dec, "no cropping needed");
    mpeg2dec->need_cropping = FALSE;
  }

  /* get subsampling */
  if (sequence->chroma_width < sequence->width) {
    /* horizontally subsampled */
    if (sequence->chroma_height < sequence->height) {
      /* and vertically subsamples */
      format = GST_VIDEO_FORMAT_I420;
    } else {
      format = GST_VIDEO_FORMAT_Y42B;
    }
  } else {
    /* not subsampled */
    format = GST_VIDEO_FORMAT_Y444;
  }

  state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (mpeg2dec),
      format, sequence->picture_width, sequence->picture_height,
      mpeg2dec->input_state);
  vinfo = &state->info;

  /* If we don't have a valid upstream PAR override it */
  if (GST_VIDEO_INFO_PAR_N (vinfo) == 1 &&
      GST_VIDEO_INFO_PAR_D (vinfo) == 1 &&
      sequence->pixel_width != 0 && sequence->pixel_height != 0) {
#if MPEG2_RELEASE >= MPEG2_VERSION(0,5,0)
    guint pixel_width, pixel_height;
    if (mpeg2_guess_aspect (sequence, &pixel_width, &pixel_height)) {
      vinfo->par_n = pixel_width;
      vinfo->par_d = pixel_height;
    }
#else
    vinfo->par_n = sequence->pixel_width;
    vinfo->par_d = sequence->pixel_height;
#endif
    GST_DEBUG_OBJECT (mpeg2dec, "Setting PAR %d x %d",
        vinfo->par_n, vinfo->par_d);
  }
  vinfo->fps_n = 27000000;
  vinfo->fps_d = sequence->frame_period;

  if (!(sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE))
    vinfo->interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
  else
    vinfo->interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  vinfo->chroma_site = GST_VIDEO_CHROMA_SITE_MPEG2;
  vinfo->colorimetry.range = GST_VIDEO_COLOR_RANGE_16_235;

  if (sequence->flags & SEQ_FLAG_COLOUR_DESCRIPTION) {
    /* do color description */
    switch (sequence->colour_primaries) {
      case 1:
        vinfo->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
        break;
      case 4:
        vinfo->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470M;
        break;
      case 5:
        vinfo->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470BG;
        break;
      case 6:
        vinfo->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
        break;
      case 7:
        vinfo->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE240M;
        break;
        /* 0 forbidden */
        /* 2 unspecified */
        /* 3 reserved */
        /* 8-255 reseved */
      default:
        vinfo->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_UNKNOWN;
        break;
    }
    /* matrix coefficients */
    switch (sequence->matrix_coefficients) {
      case 1:
        vinfo->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        break;
      case 4:
        vinfo->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_FCC;
        break;
      case 5:
      case 6:
        vinfo->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        break;
      case 7:
        vinfo->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
        break;
        /* 0 forbidden */
        /* 2 unspecified */
        /* 3 reserved */
        /* 8-255 reseved */
      default:
        vinfo->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_UNKNOWN;
        break;
    }
    /* transfer characteristics */
    switch (sequence->transfer_characteristics) {
      case 1:
        vinfo->colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case 4:
        vinfo->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA22;
        break;
      case 5:
        vinfo->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA28;
        break;
      case 6:
        vinfo->colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case 7:
        vinfo->colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE240M;
        break;
      case 8:
        vinfo->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA10;
        break;
        /* 0 forbidden */
        /* 2 unspecified */
        /* 3 reserved */
        /* 9-255 reseved */
      default:
        vinfo->colorimetry.transfer = GST_VIDEO_TRANSFER_UNKNOWN;
        break;
    }
  }

  GST_DEBUG_OBJECT (mpeg2dec,
      "sequence flags: %d, frame period: %d, frame rate: %d/%d",
      sequence->flags, sequence->frame_period, vinfo->fps_n, vinfo->fps_d);
  GST_DEBUG_OBJECT (mpeg2dec, "profile: %02x, colour_primaries: %d",
      sequence->profile_level_id, sequence->colour_primaries);
  GST_DEBUG_OBJECT (mpeg2dec, "transfer chars: %d, matrix coef: %d",
      sequence->transfer_characteristics, sequence->matrix_coefficients);
  GST_DEBUG_OBJECT (mpeg2dec,
      "FLAGS: CONSTRAINED_PARAMETERS:%d, PROGRESSIVE_SEQUENCE:%d",
      sequence->flags & SEQ_FLAG_CONSTRAINED_PARAMETERS,
      sequence->flags & SEQ_FLAG_PROGRESSIVE_SEQUENCE);
  GST_DEBUG_OBJECT (mpeg2dec, "FLAGS: LOW_DELAY:%d, COLOUR_DESCRIPTION:%d",
      sequence->flags & SEQ_FLAG_LOW_DELAY,
      sequence->flags & SEQ_FLAG_COLOUR_DESCRIPTION);

  /* we store the codec size before cropping */
  *dinfo = *vinfo;
  gst_video_info_set_format (&pre_crop_info, format, sequence->width,
      sequence->height);
  dinfo->width = sequence->width;
  dinfo->height = sequence->height;
  dinfo->size = pre_crop_info.size;
  memcpy (dinfo->stride, pre_crop_info.stride, sizeof (pre_crop_info.stride));
  memcpy (dinfo->offset, pre_crop_info.offset, sizeof (pre_crop_info.offset));

  /* Mpeg2dec has 2 frame latency to produce a picture and 1 frame latency in
   * it's parser */
  latency = gst_util_uint64_scale (3, vinfo->fps_d, vinfo->fps_n);
  gst_video_decoder_set_latency (GST_VIDEO_DECODER (mpeg2dec), latency,
      latency);

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (mpeg2dec)))
    goto negotiation_fail;

  gst_video_codec_state_unref (state);

  mpeg2_custom_fbuf (mpeg2dec->decoder, 1);

  init_dummybuf (mpeg2dec);

  /* Pump in some null buffers, because otherwise libmpeg2 doesn't
   * initialise the discard_fbuf->id */
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  mpeg2_set_buf (mpeg2dec->decoder, mpeg2dec->dummybuf, NULL);
  gst_mpeg2dec_clear_buffers (mpeg2dec);

  return ret;

invalid_frame_period:
  {
    GST_WARNING_OBJECT (mpeg2dec, "Frame period is 0!");
    return GST_FLOW_ERROR;
  }
invalid_size:
  {
    GST_ERROR_OBJECT (mpeg2dec, "Invalid frame dimensions: %d x %d",
        sequence->width, sequence->height);
    return GST_FLOW_ERROR;
  }

negotiation_fail:
  {
    GST_WARNING_OBJECT (mpeg2dec, "Failed to negotiate with downstream");
    gst_video_codec_state_unref (state);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
handle_picture (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info,
    GstVideoCodecFrame * frame)
{
  GstFlowReturn ret;
  gint type;
  const gchar *type_str = NULL;
  gboolean key_frame = FALSE;
  const mpeg2_picture_t *picture = info->current_picture;
  GstBuffer *buffer;

  ret = gst_mpeg2dec_alloc_buffer (mpeg2dec, frame, &buffer);
  if (ret != GST_FLOW_OK)
    return ret;

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
      ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
      GST_VIDEO_DECODER_ERROR (mpeg2dec, 1, STREAM, DECODE,
          ("decoding error"), ("Invalid picture type"), ret);
      return ret;
  }

  GST_DEBUG_OBJECT (mpeg2dec, "handle picture type %s", type_str);
  GST_DEBUG_OBJECT (mpeg2dec, "picture %s, frame %i",
      key_frame ? ", kf," : "    ", frame->system_frame_number);

  if (GST_VIDEO_INFO_IS_INTERLACED (&mpeg2dec->decoded_info)) {
    /* This implies SEQ_FLAG_PROGRESSIVE_SEQUENCE is not set */
    if (picture->flags & PIC_FLAG_TOP_FIELD_FIRST) {
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    }
    if (!(picture->flags & PIC_FLAG_PROGRESSIVE_FRAME)) {
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_INTERLACED);
    }
#if MPEG2_RELEASE >= MPEG2_VERSION(0,5,0)
    /* repeat field introduced in 0.5.0 */
    if (picture->flags & PIC_FLAG_REPEAT_FIRST_FIELD) {
      GST_BUFFER_FLAG_SET (buffer, GST_VIDEO_BUFFER_FLAG_RFF);
    }
#endif
  }

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_PICTURE && key_frame) {
    mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_KEYFRAME;
  }

  GST_DEBUG_OBJECT (mpeg2dec,
      "picture: %s %s %s %s %s fields:%d ts:%"
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
      picture->nb_fields, GST_TIME_ARGS (frame->pts));

  return ret;
}

static GstFlowReturn
handle_slice (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame;
  const mpeg2_picture_t *picture;
  gboolean key_frame = FALSE;
  GstVideoCodecState *state;

  GST_DEBUG_OBJECT (mpeg2dec,
      "fbuf:%p display_picture:%p current_picture:%p fbuf->id:%d",
      info->display_fbuf, info->display_picture, info->current_picture,
      GPOINTER_TO_INT (info->display_fbuf->id) - 1);

  /* Note, the fbuf-id is shifted by 1 to make the difference between
   * NULL values (used by dummy buffers) and 'real' values */
  frame = gst_video_decoder_get_frame (GST_VIDEO_DECODER (mpeg2dec),
      GPOINTER_TO_INT (info->display_fbuf->id) - 1);
  if (!frame)
    goto no_frame;
  picture = info->display_picture;
  key_frame = (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I;

  GST_DEBUG_OBJECT (mpeg2dec, "picture flags: %d, type: %d, keyframe: %d",
      picture->flags, picture->flags & PIC_MASK_CODING_TYPE, key_frame);

  if (key_frame) {
    mpeg2_skip (mpeg2dec->decoder, 0);
  }

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_KEYFRAME && key_frame)
    mpeg2dec->discont_state = MPEG2DEC_DISC_NONE;

  if (picture->flags & PIC_FLAG_SKIP) {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer because of skip flag");
    ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
    mpeg2_skip (mpeg2dec->decoder, 1);
    return ret;
  }

  if (mpeg2dec->discont_state != MPEG2DEC_DISC_NONE) {
    GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, discont state %d",
        mpeg2dec->discont_state);
    ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
    return ret;
  }

  state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (mpeg2dec));

  /* do cropping if the target region is smaller than the input one */
  if (mpeg2dec->need_cropping && !mpeg2dec->has_cropping) {
    GstVideoFrame *vframe;

    if (gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (mpeg2dec),
            frame) < 0) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer crop, too late");
      ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
      goto beach;
    }

    GST_DEBUG_OBJECT (mpeg2dec, "cropping buffer");
    vframe = gst_mpeg2dec_get_buffer (mpeg2dec, frame->system_frame_number);
    g_assert (vframe != NULL);
    ret = gst_mpeg2dec_crop_buffer (mpeg2dec, frame, vframe);
  }

  ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (mpeg2dec), frame);

beach:
  gst_video_codec_state_unref (state);
  return ret;

no_frame:
  {
    GST_DEBUG ("display buffer does not have a valid frame");
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_mpeg2dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (decoder);
  GstBuffer *buf = frame->input_buffer;
  GstMapInfo minfo;
  const mpeg2_info_t *info;
  mpeg2_state_t state;
  gboolean done = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_LOG_OBJECT (mpeg2dec, "received frame %d, timestamp %"
      GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT,
      frame->system_frame_number,
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->duration));

  gst_buffer_ref (buf);
  if (!gst_buffer_map (buf, &minfo, GST_MAP_READ)) {
    GST_ERROR_OBJECT (mpeg2dec, "Failed to map input buffer");
    return GST_FLOW_ERROR;
  }

  info = mpeg2dec->info;

  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer");
  mpeg2_buffer (mpeg2dec->decoder, minfo.data, minfo.data + minfo.size);
  GST_LOG_OBJECT (mpeg2dec, "calling mpeg2_buffer done");

  while (!done) {
    GST_LOG_OBJECT (mpeg2dec, "calling parse");
    state = mpeg2_parse (mpeg2dec->decoder);
    GST_DEBUG_OBJECT (mpeg2dec, "parse state %d", state);

    switch (state) {
#if MPEG2_RELEASE >= MPEG2_VERSION (0, 5, 0)
      case STATE_SEQUENCE_MODIFIED:
        GST_DEBUG_OBJECT (mpeg2dec, "sequence modified");
        mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
        gst_mpeg2dec_clear_buffers (mpeg2dec);
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
          gst_mpeg2dec_flush (decoder);
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
        GST_DEBUG_OBJECT (mpeg2dec, "display_fbuf:%p, discard_fbuf:%p",
            info->display_fbuf, info->discard_fbuf);
        if (info->display_fbuf && info->display_fbuf->id) {
          ret = handle_slice (mpeg2dec, info);
        } else {
          GST_DEBUG_OBJECT (mpeg2dec, "no picture to display");
        }
        if (info->discard_fbuf && info->discard_fbuf->id)
          gst_mpeg2dec_discard_buffer (mpeg2dec,
              GPOINTER_TO_INT (info->discard_fbuf->id) - 1);
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
  gst_buffer_unmap (buf, &minfo);
  gst_buffer_unref (buf);
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
    mpeg2dec,
    "LibMpeg2 decoder", plugin_init, VERSION, "GPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
