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

GST_DEBUG_CATEGORY_STATIC (mpeg2dec_debug);
#define GST_CAT_DEFAULT mpeg2dec_debug
GST_DEBUG_CATEGORY_STATIC (CAT_PERFORMANCE);

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
GST_ELEMENT_REGISTER_DEFINE (mpeg2dec, "mpeg2dec", GST_RANK_SECONDARY,
    GST_TYPE_MPEG2DEC);

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

  gst_element_class_add_static_pad_template (element_class,
      &src_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template_factory);
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
  GST_DEBUG_CATEGORY_GET (CAT_PERFORMANCE, "GST_PERFORMANCE");
}

static void
gst_mpeg2dec_init (GstMpeg2dec * mpeg2dec)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (mpeg2dec), TRUE);
  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (mpeg2dec), TRUE);
  gst_video_decoder_set_use_default_pad_acceptcaps (GST_VIDEO_DECODER_CAST
      (mpeg2dec), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (mpeg2dec));

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

  if (mpeg2dec->downstream_pool) {
    gst_buffer_pool_set_active (mpeg2dec->downstream_pool, FALSE);
    gst_object_unref (mpeg2dec->downstream_pool);
  }

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

  if (mpeg2dec->downstream_pool)
    gst_buffer_pool_set_active (mpeg2dec->downstream_pool, FALSE);

  return TRUE;
}

static GstFlowReturn
gst_mpeg2dec_finish (GstVideoDecoder * decoder)
{
  return GST_FLOW_OK;
}

static GstBufferPool *
gst_mpeg2dec_create_generic_pool (GstAllocator * allocator,
    GstAllocationParams * params, GstCaps * caps, guint size, guint min,
    guint max, GstStructure ** out_config)
{
  GstBufferPool *pool;
  GstStructure *config;

  pool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_allocator (config, allocator, params);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  *out_config = config;
  return pool;
}

static gboolean
gst_mpeg2dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstMpeg2dec *dec = GST_MPEG2DEC (decoder);
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config, *down_config = NULL;
  GstAllocator *allocator;
  GstAllocationParams params;
  gboolean update_allocator;
  gboolean has_videometa = FALSE;
  GstCaps *caps;

  /* Get rid of ancient pool */
  if (dec->downstream_pool) {
    gst_buffer_pool_set_active (dec->downstream_pool, FALSE);
    gst_object_unref (dec->downstream_pool);
    dec->downstream_pool = NULL;
  }

  /* Get negotiated allocation caps */
  gst_query_parse_allocation (query, &caps, NULL);

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

  /* Now chain up to the parent class to guarantee that we can
   * get a buffer pool from the query */
  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query)) {
    if (allocator)
      gst_object_unref (allocator);
    return FALSE;
  }

  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
    has_videometa = TRUE;
  }

  if (dec->need_alignment) {
    /* If downstream does not support video meta, we will have to copy, keep
     * the downstream pool to avoid double copying */
    if (!has_videometa) {
      dec->downstream_pool = pool;
      pool = NULL;
      down_config = config;
      config = NULL;
      min = 2;
      max = 0;
    }

    /* In case downstream support video meta, but the downstream pool does not
     * have alignment support, discard downstream pool and use video pool */
    else if (!gst_buffer_pool_has_option (pool,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT)) {
      gst_object_unref (pool);
      pool = NULL;
      gst_structure_free (config);
      config = NULL;
    }

    if (!pool)
      pool = gst_mpeg2dec_create_generic_pool (allocator, &params, caps, size,
          min, max, &config);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &dec->valign);
  }

  if (allocator)
    gst_object_unref (allocator);

  /* If we are copying out, we'll need to setup and activate the other pool */
  if (dec->downstream_pool) {
    if (!gst_buffer_pool_set_config (dec->downstream_pool, down_config)) {
      down_config = gst_buffer_pool_get_config (dec->downstream_pool);
      if (!gst_buffer_pool_config_validate_params (down_config, caps, size, min,
              max)) {
        gst_structure_free (down_config);
        goto config_failed;
      }

      if (!gst_buffer_pool_set_config (dec->downstream_pool, down_config))
        goto config_failed;
    }

    if (!gst_buffer_pool_set_active (dec->downstream_pool, TRUE))
      goto activate_failed;
  }

  /* Now configure the pool, if the pool had made some changes, it will
   * return FALSE. Validate the changes ...*/
  if (!gst_buffer_pool_set_config (pool, config)) {
    config = gst_buffer_pool_get_config (pool);

    /* Check basic params */
    if (!gst_buffer_pool_config_validate_params (config, caps, size, min, max)) {
      gst_structure_free (config);
      goto config_failed;
    }

    /* If needed, check that resulting alignment is still valid */
    if (dec->need_alignment) {
      GstVideoAlignment valign;

      if (!gst_buffer_pool_config_get_video_alignment (config, &valign)) {
        gst_structure_free (config);
        goto config_failed;
      }

      if (valign.padding_left != 0 || valign.padding_top != 0
          || valign.padding_right < dec->valign.padding_right
          || valign.padding_bottom < dec->valign.padding_bottom) {
        gst_structure_free (config);
        goto config_failed;
      }
    }

    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }

  /* For external pools, we need to check strides */
  if (!GST_IS_VIDEO_BUFFER_POOL (pool) && has_videometa) {
    GstBuffer *buffer;
    const GstVideoFormatInfo *finfo;
    GstVideoMeta *vmeta;
    gint uv_stride;

    if (!gst_buffer_pool_set_active (pool, TRUE))
      goto activate_failed;

    if (gst_buffer_pool_acquire_buffer (pool, &buffer, NULL) != GST_FLOW_OK) {
      gst_buffer_pool_set_active (pool, FALSE);
      goto acquire_failed;
    }

    vmeta = gst_buffer_get_video_meta (buffer);
    finfo = gst_video_format_get_info (vmeta->format);

    /* Check that strides are compatible. In this case, we can scale the
     * stride directly since all the pixel strides for the formats we support
     * is 1 */
    uv_stride = GST_VIDEO_FORMAT_INFO_SCALE_WIDTH (finfo, 1, vmeta->stride[0]);
    if (uv_stride != vmeta->stride[1] || uv_stride != vmeta->stride[2]) {
      gst_buffer_pool_set_active (pool, FALSE);
      gst_object_unref (pool);

      pool = gst_mpeg2dec_create_generic_pool (allocator, &params, caps, size,
          min, max, &config);

      if (dec->need_alignment) {
        gst_buffer_pool_config_add_option (config,
            GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
        gst_buffer_pool_config_set_video_alignment (config, &dec->valign);
      }

      /* Generic pool don't fail on _set_config() */
      gst_buffer_pool_set_config (pool, config);
    }

    gst_buffer_unref (buffer);
  }

  gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  gst_object_unref (pool);

  return TRUE;

config_failed:
  gst_object_unref (pool);
  GST_ELEMENT_ERROR (dec, RESOURCE, SETTINGS,
      ("Failed to configure buffer pool"),
      ("Configuration is most likely invalid, please report this issue."));
  return FALSE;

activate_failed:
  gst_object_unref (pool);
  GST_ELEMENT_ERROR (dec, RESOURCE, SETTINGS,
      ("Failed to activate buffer pool"), (NULL));
  return FALSE;

acquire_failed:
  gst_object_unref (pool);
  GST_ELEMENT_ERROR (dec, RESOURCE, SETTINGS,
      ("Failed to acquire a buffer"), (NULL));
  return FALSE;
}

static GstFlowReturn
gst_mpeg2dec_crop_buffer (GstMpeg2dec * dec, GstVideoCodecFrame * in_frame,
    GstVideoFrame * input_vframe)
{
  GstVideoCodecState *state;
  GstVideoInfo *info;
  GstVideoInfo *dinfo;
  GstVideoFrame output_frame;
  GstFlowReturn ret;
  GstBuffer *buffer = NULL;

  state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (dec));
  info = &state->info;
  dinfo = &dec->decoded_info;

  GST_CAT_LOG_OBJECT (CAT_PERFORMANCE, dec,
      "Copying input buffer %ux%u (%" G_GSIZE_FORMAT ") to output buffer "
      "%ux%u (%" G_GSIZE_FORMAT ")", dinfo->width, dinfo->height,
      dinfo->size, info->width, info->height, info->size);

  ret = gst_buffer_pool_acquire_buffer (dec->downstream_pool, &buffer, NULL);
  if (ret != GST_FLOW_OK)
    goto beach;

  if (!gst_video_frame_map (&output_frame, info, buffer, GST_MAP_WRITE))
    goto map_fail;

  if (in_frame->output_buffer)
    gst_buffer_unref (in_frame->output_buffer);
  in_frame->output_buffer = buffer;

  if (!gst_video_frame_copy (&output_frame, input_vframe))
    goto copy_failed;

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

copy_failed:
  {
    GST_ERROR_OBJECT (dec, "Failed to copy output frame");
    gst_video_frame_unmap (&output_frame);
    gst_video_codec_state_unref (state);
    return GST_FLOW_ERROR;
  }
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
  GstVideoInfo *vinfo;
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

  gst_video_alignment_reset (&mpeg2dec->valign);

  if (sequence->picture_width < sequence->width ||
      sequence->picture_height < sequence->height) {
    GST_DEBUG_OBJECT (mpeg2dec, "we need to crop");
    mpeg2dec->valign.padding_right = sequence->width - sequence->picture_width;
    mpeg2dec->valign.padding_bottom =
        sequence->height - sequence->picture_height;
    mpeg2dec->need_alignment = TRUE;
  } else if (sequence->picture_width == sequence->width ||
      sequence->picture_height == sequence->height) {
    GST_DEBUG_OBJECT (mpeg2dec, "no cropping needed");
    mpeg2dec->need_alignment = FALSE;
  } else {
    goto invalid_picture;
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
    guint pixel_width, pixel_height;

    if (mpeg2_guess_aspect (sequence, &pixel_width, &pixel_height)) {
      vinfo->par_n = pixel_width;
      vinfo->par_d = pixel_height;
    }
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
        /* 8-255 reserved */
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
        /* 8-255 reserved */
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
        /* 9-255 reserved */
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

  /* Save the padded video information */
  mpeg2dec->decoded_info = *vinfo;
  gst_video_info_align (&mpeg2dec->decoded_info, &mpeg2dec->valign);

  /* Mpeg2dec has 2 frame latency to produce a picture and 1 frame latency in
   * it's parser */
  latency = gst_util_uint64_scale (3 * GST_SECOND, vinfo->fps_d, vinfo->fps_n);
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

invalid_picture:
  {
    GST_ERROR_OBJECT (mpeg2dec, "Picture dimension bigger then frame: "
        "%d x %d is bigger then %d x %d", sequence->picture_width,
        sequence->picture_height, sequence->width, sequence->height);
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
  GstVideoDecoder *decoder = (GstVideoDecoder *) mpeg2dec;
  GstFlowReturn ret;
  gint type;
  const gchar *type_str = NULL;
  gboolean key_frame = FALSE;
  const mpeg2_picture_t *picture = info->current_picture;
  GstVideoFrame vframe;
  guint8 *buf[3];

  ret = gst_video_decoder_allocate_output_frame (decoder, frame);
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
      GST_BUFFER_FLAG_SET (frame->output_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    }
    if (!(picture->flags & PIC_FLAG_PROGRESSIVE_FRAME)) {
      GST_BUFFER_FLAG_SET (frame->output_buffer,
          GST_VIDEO_BUFFER_FLAG_INTERLACED);
    }
    if (picture->flags & PIC_FLAG_REPEAT_FIRST_FIELD) {
      GST_BUFFER_FLAG_SET (frame->output_buffer, GST_VIDEO_BUFFER_FLAG_RFF);
    }
  }

  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_PICTURE && key_frame) {
    mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_KEYFRAME;
  }

  GST_DEBUG_OBJECT (mpeg2dec,
      "picture: %s %s %s %s %s fields:%d ts:%"
      GST_TIME_FORMAT,
      (picture->flags & PIC_FLAG_PROGRESSIVE_FRAME ? "prog" : "    "),
      (picture->flags & PIC_FLAG_TOP_FIELD_FIRST ? "tff" : "   "),
      (picture->flags & PIC_FLAG_REPEAT_FIRST_FIELD ? "rff" : "   "),
      (picture->flags & PIC_FLAG_SKIP ? "skip" : "    "),
      (picture->flags & PIC_FLAG_COMPOSITE_DISPLAY ? "composite" : "         "),
      picture->nb_fields, GST_TIME_ARGS (frame->pts));

  if (!gst_video_frame_map (&vframe, &mpeg2dec->decoded_info,
          frame->output_buffer, GST_MAP_READ | GST_MAP_WRITE))
    goto map_fail;

  buf[0] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0);
  buf[1] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 1);
  buf[2] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, 2);

  GST_DEBUG_OBJECT (mpeg2dec, "set_buf: %p %p %p, frame %i",
      buf[0], buf[1], buf[2], frame->system_frame_number);

  /* Note: We use a non-null 'id' value to make the distinction
   * between the dummy buffers (which have an id of NULL) and the
   * ones we did */
  mpeg2_stride (mpeg2dec->decoder, vframe.info.stride[0]);
  mpeg2_set_buf (mpeg2dec->decoder, buf,
      GINT_TO_POINTER (frame->system_frame_number + 1));
  gst_mpeg2dec_save_buffer (mpeg2dec, frame->system_frame_number, &vframe);

  return ret;

map_fail:
  {
    GST_ELEMENT_ERROR (mpeg2dec, RESOURCE, WRITE, ("Failed to map frame"),
        (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
handle_slice (GstMpeg2dec * mpeg2dec, const mpeg2_info_t * info)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstVideoCodecFrame *frame;
  const mpeg2_picture_t *picture;
  gboolean key_frame = FALSE;
  gboolean bidirect_frame = FALSE;
  gboolean closed_gop = FALSE;

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
  bidirect_frame =
      (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_B;
  closed_gop = (info->gop->flags & GOP_FLAG_CLOSED_GOP);

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

  /* Skip B-frames if GOP is not closed and waiting for the first keyframe. */
  if (mpeg2dec->discont_state != MPEG2DEC_DISC_NONE) {
    if (bidirect_frame && !closed_gop) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer, discont state %d",
          mpeg2dec->discont_state);
      ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
      return ret;
    }
  }

  /* do cropping if the target region is smaller than the input one */
  if (mpeg2dec->downstream_pool) {
    GstVideoFrame *vframe;

    if (gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (mpeg2dec),
            frame) < 0) {
      GST_DEBUG_OBJECT (mpeg2dec, "dropping buffer crop, too late");
      return gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
    }

    GST_DEBUG_OBJECT (mpeg2dec, "Doing a crop copy of the decoded buffer");

    vframe = gst_mpeg2dec_get_buffer (mpeg2dec, frame->system_frame_number);
    g_assert (vframe != NULL);
    ret = gst_mpeg2dec_crop_buffer (mpeg2dec, frame, vframe);

    if (ret != GST_FLOW_OK) {
      gst_video_decoder_drop_frame (GST_VIDEO_DECODER (mpeg2dec), frame);
      return ret;
    }
  }

  ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (mpeg2dec), frame);

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
    gst_buffer_unref (buf);
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
      case STATE_SEQUENCE_MODIFIED:
        GST_DEBUG_OBJECT (mpeg2dec, "sequence modified");
        mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
        gst_mpeg2dec_clear_buffers (mpeg2dec);
        /* fall through */
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
      case STATE_INVALID_END:
        GST_DEBUG_OBJECT (mpeg2dec, "invalid end");
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
  return GST_ELEMENT_REGISTER (mpeg2dec, plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    mpeg2dec,
    "LibMpeg2 decoder", plugin_init, VERSION, "GPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
