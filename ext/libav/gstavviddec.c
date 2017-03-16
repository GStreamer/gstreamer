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

#include <assert.h>
#include <string.h>

#include <libavcodec/avcodec.h>
#include <libavutil/stereo3d.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavviddec.h"

#define MAX_TS_MASK 0xff

#define DEFAULT_LOWRES			0
#define DEFAULT_SKIPFRAME		0
#define DEFAULT_DIRECT_RENDERING	TRUE
#define DEFAULT_DEBUG_MV		FALSE
#define DEFAULT_MAX_THREADS		0
#define DEFAULT_OUTPUT_CORRUPT		TRUE
#define REQUIRED_POOL_MAX_BUFFERS       32
#define DEFAULT_STRIDE_ALIGN            31
#define DEFAULT_ALLOC_PARAM             { 0, DEFAULT_STRIDE_ALIGN, 0, 0, }

enum
{
  PROP_0,
  PROP_LOWRES,
  PROP_SKIPFRAME,
  PROP_DIRECT_RENDERING,
  PROP_DEBUG_MV,
  PROP_MAX_THREADS,
  PROP_OUTPUT_CORRUPT,
  PROP_LAST
};

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegviddec_base_init (GstFFMpegVidDecClass * klass);
static void gst_ffmpegviddec_class_init (GstFFMpegVidDecClass * klass);
static void gst_ffmpegviddec_init (GstFFMpegVidDec * ffmpegdec);
static void gst_ffmpegviddec_finalize (GObject * object);

static gboolean gst_ffmpegviddec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static GstFlowReturn gst_ffmpegviddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_ffmpegviddec_start (GstVideoDecoder * decoder);
static gboolean gst_ffmpegviddec_stop (GstVideoDecoder * decoder);
static gboolean gst_ffmpegviddec_flush (GstVideoDecoder * decoder);
static gboolean gst_ffmpegviddec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);
static gboolean gst_ffmpegviddec_propose_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

static void gst_ffmpegviddec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegviddec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_ffmpegviddec_negotiate (GstFFMpegVidDec * ffmpegdec,
    AVCodecContext * context, AVFrame * picture);

/* some sort of bufferpool handling, but different */
static int gst_ffmpegviddec_get_buffer2 (AVCodecContext * context,
    AVFrame * picture, int flags);

static GstFlowReturn gst_ffmpegviddec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_ffmpegviddec_drain (GstVideoDecoder * decoder);

static gboolean picture_changed (GstFFMpegVidDec * ffmpegdec,
    AVFrame * picture);
static gboolean context_changed (GstFFMpegVidDec * ffmpegdec,
    AVCodecContext * context);

#define GST_FFDEC_PARAMS_QDATA g_quark_from_static_string("avdec-params")

static GstElementClass *parent_class = NULL;

#define GST_FFMPEGVIDDEC_TYPE_LOWRES (gst_ffmpegviddec_lowres_get_type())
static GType
gst_ffmpegviddec_lowres_get_type (void)
{
  static GType ffmpegdec_lowres_type = 0;

  if (!ffmpegdec_lowres_type) {
    static const GEnumValue ffmpegdec_lowres[] = {
      {0, "0", "full"},
      {1, "1", "1/2-size"},
      {2, "2", "1/4-size"},
      {0, NULL, NULL},
    };

    ffmpegdec_lowres_type =
        g_enum_register_static ("GstLibAVVidDecLowres", ffmpegdec_lowres);
  }

  return ffmpegdec_lowres_type;
}

#define GST_FFMPEGVIDDEC_TYPE_SKIPFRAME (gst_ffmpegviddec_skipframe_get_type())
static GType
gst_ffmpegviddec_skipframe_get_type (void)
{
  static GType ffmpegdec_skipframe_type = 0;

  if (!ffmpegdec_skipframe_type) {
    static const GEnumValue ffmpegdec_skipframe[] = {
      {0, "0", "Skip nothing"},
      {1, "1", "Skip B-frames"},
      {2, "2", "Skip IDCT/Dequantization"},
      {5, "5", "Skip everything"},
      {0, NULL, NULL},
    };

    ffmpegdec_skipframe_type =
        g_enum_register_static ("GstLibAVVidDecSkipFrame", ffmpegdec_skipframe);
  }

  return ffmpegdec_skipframe_type;
}

static void
gst_ffmpegviddec_base_init (GstFFMpegVidDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps, *srccaps;
  AVCodec *in_plugin;
  gchar *longname, *description;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFDEC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("libav %s decoder", in_plugin->long_name);
  description = g_strdup_printf ("libav %s decoder", in_plugin->name);
  gst_element_class_set_metadata (element_class, longname,
      "Codec/Decoder/Video", description,
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>, "
      "Edward Hervey <bilboed@bilboed.com>");
  g_free (longname);
  g_free (description);

  /* get the caps */
  sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, FALSE);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for decoder '%s'", in_plugin->name);
    sinkcaps = gst_caps_new_empty_simple ("unknown/unknown");
  }
  srccaps = gst_ffmpeg_codectype_to_video_caps (NULL,
      in_plugin->id, FALSE, in_plugin);
  if (!srccaps) {
    GST_DEBUG ("Couldn't get source caps for decoder '%s'", in_plugin->name);
    srccaps = gst_caps_from_string ("video/x-raw");
  }

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  gst_caps_unref (sinkcaps);
  gst_caps_unref (srccaps);

  klass->in_plugin = in_plugin;
}

static void
gst_ffmpegviddec_class_init (GstFFMpegVidDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstVideoDecoderClass *viddec_class = GST_VIDEO_DECODER_CLASS (klass);
  int caps;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_ffmpegviddec_finalize;

  gobject_class->set_property = gst_ffmpegviddec_set_property;
  gobject_class->get_property = gst_ffmpegviddec_get_property;

  g_object_class_install_property (gobject_class, PROP_SKIPFRAME,
      g_param_spec_enum ("skip-frame", "Skip frames",
          "Which types of frames to skip during decoding",
          GST_FFMPEGVIDDEC_TYPE_SKIPFRAME, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_LOWRES,
      g_param_spec_enum ("lowres", "Low resolution",
          "At which resolution to decode images",
          GST_FFMPEGVIDDEC_TYPE_LOWRES, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DIRECT_RENDERING,
      g_param_spec_boolean ("direct-rendering", "Direct Rendering",
          "Enable direct rendering", DEFAULT_DIRECT_RENDERING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEBUG_MV,
      g_param_spec_boolean ("debug-mv", "Debug motion vectors",
          "Whether libav should print motion vectors on top of the image",
          DEFAULT_DEBUG_MV, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OUTPUT_CORRUPT,
      g_param_spec_boolean ("output-corrupt", "Output corrupt buffers",
          "Whether libav should output frames even if corrupted",
          DEFAULT_OUTPUT_CORRUPT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  caps = klass->in_plugin->capabilities;
  if (caps & (CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS)) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_THREADS,
        g_param_spec_int ("max-threads", "Maximum decode threads",
            "Maximum number of worker threads to spawn. (0 = auto)",
            0, G_MAXINT, DEFAULT_MAX_THREADS,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }

  viddec_class->set_format = gst_ffmpegviddec_set_format;
  viddec_class->handle_frame = gst_ffmpegviddec_handle_frame;
  viddec_class->start = gst_ffmpegviddec_start;
  viddec_class->stop = gst_ffmpegviddec_stop;
  viddec_class->flush = gst_ffmpegviddec_flush;
  viddec_class->finish = gst_ffmpegviddec_finish;
  viddec_class->drain = gst_ffmpegviddec_drain;
  viddec_class->decide_allocation = gst_ffmpegviddec_decide_allocation;
  viddec_class->propose_allocation = gst_ffmpegviddec_propose_allocation;
}

static void
gst_ffmpegviddec_init (GstFFMpegVidDec * ffmpegdec)
{
  GstFFMpegVidDecClass *klass =
      (GstFFMpegVidDecClass *) G_OBJECT_GET_CLASS (ffmpegdec);

  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context3 (klass->in_plugin);
  ffmpegdec->context->opaque = ffmpegdec;
  ffmpegdec->picture = av_frame_alloc ();
  ffmpegdec->opened = FALSE;
  ffmpegdec->skip_frame = ffmpegdec->lowres = 0;
  ffmpegdec->direct_rendering = DEFAULT_DIRECT_RENDERING;
  ffmpegdec->debug_mv = DEFAULT_DEBUG_MV;
  ffmpegdec->max_threads = DEFAULT_MAX_THREADS;
  ffmpegdec->output_corrupt = DEFAULT_OUTPUT_CORRUPT;

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (ffmpegdec));
  gst_video_decoder_set_use_default_pad_acceptcaps (GST_VIDEO_DECODER_CAST
      (ffmpegdec), TRUE);

  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (ffmpegdec), TRUE);
}

static void
gst_ffmpegviddec_finalize (GObject * object)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) object;

  av_frame_free (&ffmpegdec->picture);

  if (ffmpegdec->context != NULL) {
    gst_ffmpeg_avcodec_close (ffmpegdec->context);
    av_free (ffmpegdec->context);
    ffmpegdec->context = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ffmpegviddec_context_set_flags (AVCodecContext * context, guint flags,
    gboolean enable)
{
  g_return_if_fail (context != NULL);

  if (enable)
    context->flags |= flags;
  else
    context->flags &= ~flags;
}

/* with LOCK */
static gboolean
gst_ffmpegviddec_close (GstFFMpegVidDec * ffmpegdec, gboolean reset)
{
  GstFFMpegVidDecClass *oclass;
  gint i;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_LOG_OBJECT (ffmpegdec, "closing ffmpeg codec");

  gst_caps_replace (&ffmpegdec->last_caps, NULL);

  gst_ffmpeg_avcodec_close (ffmpegdec->context);
  ffmpegdec->opened = FALSE;

  for (i = 0; i < G_N_ELEMENTS (ffmpegdec->stride); i++)
    ffmpegdec->stride[i] = -1;

  gst_buffer_replace (&ffmpegdec->palette, NULL);

  if (ffmpegdec->context->extradata) {
    av_free (ffmpegdec->context->extradata);
    ffmpegdec->context->extradata = NULL;
  }
  if (reset) {
    if (avcodec_get_context_defaults3 (ffmpegdec->context,
            oclass->in_plugin) < 0) {
      GST_DEBUG_OBJECT (ffmpegdec, "Failed to set context defaults");
      return FALSE;
    }
    ffmpegdec->context->opaque = ffmpegdec;
  }
  return TRUE;
}

/* with LOCK */
static gboolean
gst_ffmpegviddec_open (GstFFMpegVidDec * ffmpegdec)
{
  GstFFMpegVidDecClass *oclass;
  gint i;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (gst_ffmpeg_avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0)
    goto could_not_open;

  for (i = 0; i < G_N_ELEMENTS (ffmpegdec->stride); i++)
    ffmpegdec->stride[i] = -1;

  ffmpegdec->opened = TRUE;

  GST_LOG_OBJECT (ffmpegdec, "Opened libav codec %s, id %d",
      oclass->in_plugin->name, oclass->in_plugin->id);

  gst_ffmpegviddec_context_set_flags (ffmpegdec->context,
      CODEC_FLAG_OUTPUT_CORRUPT, ffmpegdec->output_corrupt);

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    gst_ffmpegviddec_close (ffmpegdec, TRUE);
    GST_DEBUG_OBJECT (ffmpegdec, "avdec_%s: Failed to open libav codec",
        oclass->in_plugin->name);
    return FALSE;
  }
}

static void
gst_ffmpegviddec_get_palette (GstFFMpegVidDec * ffmpegdec,
    GstVideoCodecState * state)
{
  GstStructure *str = gst_caps_get_structure (state->caps, 0);
  const GValue *palette_v;
  GstBuffer *palette;

  /* do we have a palette? */
  if ((palette_v = gst_structure_get_value (str, "palette_data"))) {
    palette = gst_value_get_buffer (palette_v);
    GST_DEBUG ("got palette data %p", palette);
    if (gst_buffer_get_size (palette) >= AVPALETTE_SIZE) {
      gst_buffer_replace (&ffmpegdec->palette, palette);
    }
  }
}


static gboolean
gst_ffmpegviddec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstFFMpegVidDec *ffmpegdec;
  GstFFMpegVidDecClass *oclass;
  GstClockTime latency = GST_CLOCK_TIME_NONE;
  gboolean ret = FALSE;

  ffmpegdec = (GstFFMpegVidDec *) decoder;
  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (ffmpegdec->last_caps != NULL &&
      gst_caps_is_equal (ffmpegdec->last_caps, state->caps)) {
    return TRUE;
  }

  GST_DEBUG_OBJECT (ffmpegdec, "setcaps called");

  GST_OBJECT_LOCK (ffmpegdec);
  /* stupid check for VC1 */
  if ((oclass->in_plugin->id == AV_CODEC_ID_WMV3) ||
      (oclass->in_plugin->id == AV_CODEC_ID_VC1))
    oclass->in_plugin->id = gst_ffmpeg_caps_to_codecid (state->caps, NULL);

  /* close old session */
  if (ffmpegdec->opened) {
    GST_OBJECT_UNLOCK (ffmpegdec);
    gst_ffmpegviddec_finish (decoder);
    GST_OBJECT_LOCK (ffmpegdec);
    if (!gst_ffmpegviddec_close (ffmpegdec, TRUE)) {
      GST_OBJECT_UNLOCK (ffmpegdec);
      return FALSE;
    }
    ffmpegdec->pic_pix_fmt = 0;
    ffmpegdec->pic_width = 0;
    ffmpegdec->pic_height = 0;
    ffmpegdec->pic_par_n = 0;
    ffmpegdec->pic_par_d = 0;
    ffmpegdec->pic_interlaced = 0;
    ffmpegdec->pic_field_order = 0;
    ffmpegdec->pic_field_order_changed = FALSE;
    ffmpegdec->ctx_ticks = 0;
    ffmpegdec->ctx_time_n = 0;
    ffmpegdec->ctx_time_d = 0;
    ffmpegdec->cur_multiview_mode = GST_VIDEO_MULTIVIEW_MODE_NONE;
    ffmpegdec->cur_multiview_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
  }

  gst_caps_replace (&ffmpegdec->last_caps, state->caps);

  /* set buffer functions */
  ffmpegdec->context->get_buffer2 = gst_ffmpegviddec_get_buffer2;
  ffmpegdec->context->draw_horiz_band = NULL;

  /* reset coded_width/_height to prevent it being reused from last time when
   * the codec is opened again, causing a mismatch and possible
   * segfault/corruption. (Common scenario when renegotiating caps) */
  ffmpegdec->context->coded_width = 0;
  ffmpegdec->context->coded_height = 0;

  GST_LOG_OBJECT (ffmpegdec, "size %dx%d", ffmpegdec->context->width,
      ffmpegdec->context->height);

  /* FIXME : Create a method that takes GstVideoCodecState instead */
  /* get size and so */
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, state->caps, ffmpegdec->context);

  GST_LOG_OBJECT (ffmpegdec, "size after %dx%d", ffmpegdec->context->width,
      ffmpegdec->context->height);

  gst_ffmpegviddec_get_palette (ffmpegdec, state);

  if (!ffmpegdec->context->time_base.den || !ffmpegdec->context->time_base.num) {
    GST_DEBUG_OBJECT (ffmpegdec, "forcing 25/1 framerate");
    ffmpegdec->context->time_base.num = 1;
    ffmpegdec->context->time_base.den = 25;
  }

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;
  ffmpegdec->context->err_recognition = 1;

  /* for slow cpus */
  ffmpegdec->context->lowres = ffmpegdec->lowres;
  ffmpegdec->context->skip_frame = ffmpegdec->skip_frame;

  /* ffmpeg can draw motion vectors on top of the image (not every decoder
   * supports it) */
  ffmpegdec->context->debug_mv = ffmpegdec->debug_mv;

  {
    GstQuery *query;
    gboolean is_live;

    if (ffmpegdec->max_threads == 0) {
      if (!(oclass->in_plugin->capabilities & CODEC_CAP_AUTO_THREADS))
        ffmpegdec->context->thread_count = gst_ffmpeg_auto_max_threads ();
      else
        ffmpegdec->context->thread_count = 0;
    } else
      ffmpegdec->context->thread_count = ffmpegdec->max_threads;

    query = gst_query_new_latency ();
    is_live = FALSE;
    /* Check if upstream is live. If it isn't we can enable frame based
     * threading, which is adding latency */
    if (gst_pad_peer_query (GST_VIDEO_DECODER_SINK_PAD (ffmpegdec), query)) {
      gst_query_parse_latency (query, &is_live, NULL, NULL);
    }
    gst_query_unref (query);

    if (is_live)
      ffmpegdec->context->thread_type = FF_THREAD_SLICE;
    else
      ffmpegdec->context->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
  }

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegviddec_open (ffmpegdec))
    goto open_failed;

  if (ffmpegdec->input_state)
    gst_video_codec_state_unref (ffmpegdec->input_state);
  ffmpegdec->input_state = gst_video_codec_state_ref (state);

  if (ffmpegdec->input_state->info.fps_n) {
    GstVideoInfo *info = &ffmpegdec->input_state->info;
    latency = gst_util_uint64_scale_ceil (
        (ffmpegdec->context->has_b_frames) * GST_SECOND, info->fps_d,
        info->fps_n);
  }

  ret = TRUE;

done:
  GST_OBJECT_UNLOCK (ffmpegdec);

  if (GST_CLOCK_TIME_IS_VALID (latency))
    gst_video_decoder_set_latency (decoder, latency, latency);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to open");
    goto done;
  }
}

typedef struct
{
  GstFFMpegVidDec *ffmpegdec;
  GstVideoCodecFrame *frame;
  gboolean mapped;
  GstVideoFrame vframe;
  GstBuffer *buffer;
  AVBufferRef *avbuffer;
} GstFFMpegVidDecVideoFrame;

static GstFFMpegVidDecVideoFrame *
gst_ffmpegviddec_video_frame_new (GstFFMpegVidDec * ffmpegdec,
    GstVideoCodecFrame * frame)
{
  GstFFMpegVidDecVideoFrame *dframe;

  dframe = g_slice_new0 (GstFFMpegVidDecVideoFrame);
  dframe->ffmpegdec = ffmpegdec;
  dframe->frame = frame;

  GST_DEBUG_OBJECT (ffmpegdec, "new video frame %p", dframe);

  return dframe;
}

static void
gst_ffmpegviddec_video_frame_free (GstFFMpegVidDec * ffmpegdec,
    GstFFMpegVidDecVideoFrame * frame)
{
  GST_DEBUG_OBJECT (ffmpegdec, "free video frame %p", frame);

  if (frame->mapped)
    gst_video_frame_unmap (&frame->vframe);
  gst_video_decoder_release_frame (GST_VIDEO_DECODER (ffmpegdec), frame->frame);
  gst_buffer_replace (&frame->buffer, NULL);
  if (frame->avbuffer) {
    av_buffer_unref (&frame->avbuffer);
  }
  g_slice_free (GstFFMpegVidDecVideoFrame, frame);
}

static void
dummy_free_buffer (void *opaque, uint8_t * data)
{
  GstFFMpegVidDecVideoFrame *frame = opaque;

  gst_ffmpegviddec_video_frame_free (frame->ffmpegdec, frame);
}

/* This function prepares the pool configuration for direct rendering. To use
 * this method, the codec should support direct rendering and the pool should
 * support video meta and video alignment */
static void
gst_ffmpegvideodec_prepare_dr_pool (GstFFMpegVidDec * ffmpegdec,
    GstBufferPool * pool, GstVideoInfo * info, GstStructure * config)
{
  GstAllocationParams params;
  GstVideoAlignment align;
  GstAllocator *allocator = NULL;
  gint width, height;
  gint linesize_align[4];
  gint i;
  guint edge;
  gsize max_align;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = GST_VIDEO_INFO_HEIGHT (info);

  /* let ffmpeg find the alignment and padding */
  avcodec_align_dimensions2 (ffmpegdec->context, &width, &height,
      linesize_align);

  if (ffmpegdec->context->flags & CODEC_FLAG_EMU_EDGE)
    edge = 0;
  else
    edge = avcodec_get_edge_width ();

  /* increase the size for the padding */
  width += edge << 1;
  height += edge << 1;

  align.padding_top = edge;
  align.padding_left = edge;
  align.padding_right = width - GST_VIDEO_INFO_WIDTH (info) - edge;
  align.padding_bottom = height - GST_VIDEO_INFO_HEIGHT (info) - edge;

  /* add extra padding to match libav buffer allocation sizes */
  align.padding_bottom++;

  gst_buffer_pool_config_get_allocator (config, &allocator, &params);

  max_align = DEFAULT_STRIDE_ALIGN;
  max_align |= params.align;

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++) {
    if (linesize_align[i] > 0)
      max_align |= linesize_align[i] - 1;
  }

  for (i = 0; i < GST_VIDEO_MAX_PLANES; i++)
    align.stride_align[i] = max_align;

  params.align = max_align;

  gst_buffer_pool_config_set_allocator (config, allocator, &params);

  GST_DEBUG_OBJECT (ffmpegdec, "aligned dimension %dx%d -> %dx%d "
      "padding t:%u l:%u r:%u b:%u, stride_align %d:%d:%d:%d",
      GST_VIDEO_INFO_WIDTH (info),
      GST_VIDEO_INFO_HEIGHT (info), width, height, align.padding_top,
      align.padding_left, align.padding_right, align.padding_bottom,
      align.stride_align[0], align.stride_align[1], align.stride_align[2],
      align.stride_align[3]);

  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
  gst_buffer_pool_config_set_video_alignment (config, &align);
}

static void
gst_ffmpegviddec_ensure_internal_pool (GstFFMpegVidDec * ffmpegdec,
    AVFrame * picture)
{
  GstAllocationParams params = DEFAULT_ALLOC_PARAM;
  GstVideoInfo info;
  GstVideoFormat format;
  GstCaps *caps;
  GstStructure *config;
  gint i;

  if (ffmpegdec->internal_pool != NULL &&
      ffmpegdec->pool_width == picture->width &&
      ffmpegdec->pool_height == picture->height &&
      ffmpegdec->pool_format == picture->format)
    return;

  GST_DEBUG_OBJECT (ffmpegdec, "Updating internal pool (%i, %i)",
      picture->width, picture->height);

  format = gst_ffmpeg_pixfmt_to_videoformat (picture->format);
  gst_video_info_set_format (&info, format, picture->width, picture->height);

  for (i = 0; i < G_N_ELEMENTS (ffmpegdec->stride); i++)
    ffmpegdec->stride[i] = -1;

  if (ffmpegdec->internal_pool)
    gst_object_unref (ffmpegdec->internal_pool);

  ffmpegdec->internal_pool = gst_video_buffer_pool_new ();
  config = gst_buffer_pool_get_config (ffmpegdec->internal_pool);

  caps = gst_video_info_to_caps (&info);
  gst_buffer_pool_config_set_params (config, caps, info.size, 2, 0);
  gst_buffer_pool_config_set_allocator (config, NULL, &params);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);

  gst_ffmpegvideodec_prepare_dr_pool (ffmpegdec,
      ffmpegdec->internal_pool, &info, config);
  /* generic video pool never fails */
  gst_buffer_pool_set_config (ffmpegdec->internal_pool, config);
  gst_caps_unref (caps);

  gst_buffer_pool_set_active (ffmpegdec->internal_pool, TRUE);

  /* Remember pool size so we can detect changes */
  ffmpegdec->pool_width = picture->width;
  ffmpegdec->pool_height = picture->height;
  ffmpegdec->pool_format = picture->format;
  ffmpegdec->pool_info = info;
}

static gboolean
gst_ffmpegviddec_can_direct_render (GstFFMpegVidDec * ffmpegdec)
{
  GstFFMpegVidDecClass *oclass;

  if (!ffmpegdec->direct_rendering)
    return FALSE;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  return ((oclass->in_plugin->capabilities & CODEC_CAP_DR1) == CODEC_CAP_DR1);
}

/* called when ffmpeg wants us to allocate a buffer to write the decoded frame
 * into. We try to give it memory from our pool */
static int
gst_ffmpegviddec_get_buffer2 (AVCodecContext * context, AVFrame * picture,
    int flags)
{
  GstVideoCodecFrame *frame;
  GstFFMpegVidDecVideoFrame *dframe;
  GstFFMpegVidDec *ffmpegdec;
  gint c;
  GstFlowReturn ret;

  ffmpegdec = (GstFFMpegVidDec *) context->opaque;

  GST_DEBUG_OBJECT (ffmpegdec, "getting buffer picture %p", picture);

  /* apply the last info we have seen to this picture, when we get the
   * picture back from ffmpeg we can use this to correctly timestamp the output
   * buffer */
  GST_DEBUG_OBJECT (ffmpegdec, "opaque value SN %d",
      (gint32) picture->reordered_opaque);

  frame =
      gst_video_decoder_get_frame (GST_VIDEO_DECODER (ffmpegdec),
      picture->reordered_opaque);
  if (G_UNLIKELY (frame == NULL))
    goto no_frame;

  /* now it has a buffer allocated, so it is real and will also
   * be _released */
  GST_VIDEO_CODEC_FRAME_FLAG_UNSET (frame,
      GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

  if (G_UNLIKELY (frame->output_buffer != NULL))
    goto duplicate_frame;

  /* GstFFMpegVidDecVideoFrame receives the frame ref */
  if (picture->opaque) {
    dframe = picture->opaque;
    dframe->frame = frame;
  } else {
    picture->opaque = dframe =
        gst_ffmpegviddec_video_frame_new (ffmpegdec, frame);
  }

  GST_DEBUG_OBJECT (ffmpegdec, "storing opaque %p", dframe);

  if (!gst_ffmpegviddec_can_direct_render (ffmpegdec))
    goto no_dr;

  gst_ffmpegviddec_ensure_internal_pool (ffmpegdec, picture);

  ret = gst_buffer_pool_acquire_buffer (ffmpegdec->internal_pool,
      &frame->output_buffer, NULL);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  /* piggy-backed alloc'ed on the frame,
   * and there was much rejoicing and we are grateful.
   * Now take away buffer from frame, we will give it back later when decoded.
   * This allows multiple request for a buffer per frame; unusual but possible. */
  gst_buffer_replace (&dframe->buffer, frame->output_buffer);
  gst_buffer_replace (&frame->output_buffer, NULL);

  /* Fill avpicture */
  if (!gst_video_frame_map (&dframe->vframe, &ffmpegdec->pool_info,
          dframe->buffer, GST_MAP_READWRITE))
    goto map_failed;
  dframe->mapped = TRUE;

  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_PLANES (&ffmpegdec->pool_info)) {
      picture->data[c] = GST_VIDEO_FRAME_PLANE_DATA (&dframe->vframe, c);
      picture->linesize[c] = GST_VIDEO_FRAME_PLANE_STRIDE (&dframe->vframe, c);

      if (ffmpegdec->stride[c] == -1)
        ffmpegdec->stride[c] = picture->linesize[c];

      /* libav does not allow stride changes, decide allocation should check
       * before replacing the internal pool with a downstream pool.
       * https://bugzilla.gnome.org/show_bug.cgi?id=704769
       * https://bugzilla.libav.org/show_bug.cgi?id=556
       */
      g_assert (picture->linesize[c] == ffmpegdec->stride[c]);
    } else {
      picture->data[c] = NULL;
      picture->linesize[c] = 0;
    }
    GST_LOG_OBJECT (ffmpegdec, "linesize %d, data %p", picture->linesize[c],
        picture->data[c]);
  }

  picture->buf[0] = av_buffer_create (NULL, 0, dummy_free_buffer, dframe, 0);

  GST_LOG_OBJECT (ffmpegdec, "returned frame %p", dframe->buffer);

  return 0;

no_dr:
  {
    int c;
    int ret = avcodec_default_get_buffer2 (context, picture, flags);

    GST_LOG_OBJECT (ffmpegdec, "direct rendering disabled, fallback alloc");

    for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
      ffmpegdec->stride[c] = picture->linesize[c];
    }
    /* Wrap our buffer around the default one to be able to have a callback
     * when our data can be freed. Just putting our data into the first free
     * buffer might not work if there are too many allocated already
     */
    if (picture->buf[0]) {
      dframe->avbuffer = picture->buf[0];
      picture->buf[0] =
          av_buffer_create (picture->buf[0]->data, picture->buf[0]->size,
          dummy_free_buffer, dframe, 0);
    } else {
      picture->buf[0] =
          av_buffer_create (NULL, 0, dummy_free_buffer, dframe, 0);
    }

    return ret;
  }
alloc_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, RESOURCE, FAILED,
        ("Unable to allocate memory"),
        ("The downstream pool failed to allocated buffer."));
    return -1;
  }
map_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, RESOURCE, OPEN_READ_WRITE,
        ("Cannot access memory for read and write operation."),
        ("The video memory allocated from downstream pool could not mapped for"
            "read and write."));
    return -1;
  }
duplicate_frame:
  {
    GST_WARNING_OBJECT (ffmpegdec, "already alloc'ed output buffer for frame");
    gst_video_codec_frame_unref (frame);
    return -1;
  }
no_frame:
  {
    GST_WARNING_OBJECT (ffmpegdec, "Couldn't get codec frame !");
    return -1;
  }
}

static gboolean
picture_changed (GstFFMpegVidDec * ffmpegdec, AVFrame * picture)
{
  gint pic_field_order = 0;

  if (picture->interlaced_frame) {
    if (picture->repeat_pict)
      pic_field_order |= GST_VIDEO_BUFFER_FLAG_RFF;
    if (picture->top_field_first)
      pic_field_order |= GST_VIDEO_BUFFER_FLAG_TFF;
  }

  return !(ffmpegdec->pic_width == picture->width
      && ffmpegdec->pic_height == picture->height
      && ffmpegdec->pic_pix_fmt == picture->format
      && ffmpegdec->pic_par_n == picture->sample_aspect_ratio.num
      && ffmpegdec->pic_par_d == picture->sample_aspect_ratio.den
      && ffmpegdec->pic_interlaced == picture->interlaced_frame
      && ffmpegdec->pic_field_order == pic_field_order
      && ffmpegdec->cur_multiview_mode == ffmpegdec->picture_multiview_mode
      && ffmpegdec->cur_multiview_flags == ffmpegdec->picture_multiview_flags);
}

static gboolean
context_changed (GstFFMpegVidDec * ffmpegdec, AVCodecContext * context)
{
  return !(ffmpegdec->ctx_ticks == context->ticks_per_frame
      && ffmpegdec->ctx_time_n == context->time_base.num
      && ffmpegdec->ctx_time_d == context->time_base.den);
}

static gboolean
update_video_context (GstFFMpegVidDec * ffmpegdec, AVCodecContext * context,
    AVFrame * picture)
{
  gint pic_field_order = 0;

  if (picture->interlaced_frame) {
    if (picture->repeat_pict)
      pic_field_order |= GST_VIDEO_BUFFER_FLAG_RFF;
    if (picture->top_field_first)
      pic_field_order |= GST_VIDEO_BUFFER_FLAG_TFF;
  }

  if (!picture_changed (ffmpegdec, picture)
      && !context_changed (ffmpegdec, context))
    return FALSE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "Renegotiating video from %dx%d@ %d:%d PAR %d/%d fps pixfmt %d to %dx%d@ %d:%d PAR %d/%d fps pixfmt %d",
      ffmpegdec->pic_width, ffmpegdec->pic_height,
      ffmpegdec->pic_par_n, ffmpegdec->pic_par_d,
      ffmpegdec->ctx_time_n, ffmpegdec->ctx_time_d,
      ffmpegdec->pic_pix_fmt,
      picture->width, picture->height,
      picture->sample_aspect_ratio.num,
      picture->sample_aspect_ratio.den,
      context->time_base.num, context->time_base.den, picture->format);

  ffmpegdec->pic_pix_fmt = picture->format;
  ffmpegdec->pic_width = picture->width;
  ffmpegdec->pic_height = picture->height;
  ffmpegdec->pic_par_n = picture->sample_aspect_ratio.num;
  ffmpegdec->pic_par_d = picture->sample_aspect_ratio.den;
  ffmpegdec->cur_multiview_mode = ffmpegdec->picture_multiview_mode;
  ffmpegdec->cur_multiview_flags = ffmpegdec->picture_multiview_flags;

  /* Remember if we have interlaced content and the field order changed
   * at least once. If that happens, we must be interlace-mode=mixed
   */
  if (ffmpegdec->pic_field_order_changed ||
      (ffmpegdec->pic_field_order != pic_field_order &&
          ffmpegdec->pic_interlaced))
    ffmpegdec->pic_field_order_changed = TRUE;

  ffmpegdec->pic_field_order = pic_field_order;
  ffmpegdec->pic_interlaced = picture->interlaced_frame;

  if (!ffmpegdec->pic_interlaced)
    ffmpegdec->pic_field_order_changed = FALSE;

  ffmpegdec->ctx_ticks = context->ticks_per_frame;
  ffmpegdec->ctx_time_n = context->time_base.num;
  ffmpegdec->ctx_time_d = context->time_base.den;

  return TRUE;
}

static void
gst_ffmpegviddec_update_par (GstFFMpegVidDec * ffmpegdec,
    GstVideoInfo * in_info, GstVideoInfo * out_info)
{
  gboolean demuxer_par_set = FALSE;
  gboolean decoder_par_set = FALSE;
  gint demuxer_num = 1, demuxer_denom = 1;
  gint decoder_num = 1, decoder_denom = 1;

  if (in_info->par_n && in_info->par_d) {
    demuxer_num = in_info->par_n;
    demuxer_denom = in_info->par_d;
    demuxer_par_set = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec, "Demuxer PAR: %d:%d", demuxer_num,
        demuxer_denom);
  }

  if (ffmpegdec->pic_par_n && ffmpegdec->pic_par_d) {
    decoder_num = ffmpegdec->pic_par_n;
    decoder_denom = ffmpegdec->pic_par_d;
    decoder_par_set = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec, "Decoder PAR: %d:%d", decoder_num,
        decoder_denom);
  }

  if (!demuxer_par_set && !decoder_par_set)
    goto no_par;

  if (demuxer_par_set && !decoder_par_set)
    goto use_demuxer_par;

  if (decoder_par_set && !demuxer_par_set)
    goto use_decoder_par;

  /* Both the demuxer and the decoder provide a PAR. If one of
   * the two PARs is 1:1 and the other one is not, use the one
   * that is not 1:1. */
  if (demuxer_num == demuxer_denom && decoder_num != decoder_denom)
    goto use_decoder_par;

  if (decoder_num == decoder_denom && demuxer_num != demuxer_denom)
    goto use_demuxer_par;

  /* Both PARs are non-1:1, so use the PAR provided by the demuxer */
  goto use_demuxer_par;

use_decoder_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Setting decoder provided pixel-aspect-ratio of %u:%u", decoder_num,
        decoder_denom);
    out_info->par_n = decoder_num;
    out_info->par_d = decoder_denom;
    return;
  }
use_demuxer_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Setting demuxer provided pixel-aspect-ratio of %u:%u", demuxer_num,
        demuxer_denom);
    out_info->par_n = demuxer_num;
    out_info->par_d = demuxer_denom;
    return;
  }
no_par:
  {
    GST_DEBUG_OBJECT (ffmpegdec,
        "Neither demuxer nor codec provide a pixel-aspect-ratio");
    out_info->par_n = 1;
    out_info->par_d = 1;
    return;
  }
}

static GstVideoMultiviewMode
stereo_av_to_gst (enum AVStereo3DType type)
{
  switch (type) {
    case AV_STEREO3D_SIDEBYSIDE:
      return GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE;
    case AV_STEREO3D_TOPBOTTOM:
      return GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM;
    case AV_STEREO3D_FRAMESEQUENCE:
      return GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME;
    case AV_STEREO3D_CHECKERBOARD:
      return GST_VIDEO_MULTIVIEW_MODE_CHECKERBOARD;
    case AV_STEREO3D_SIDEBYSIDE_QUINCUNX:
      return GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE_QUINCUNX;
    case AV_STEREO3D_LINES:
      return GST_VIDEO_MULTIVIEW_MODE_ROW_INTERLEAVED;
    case AV_STEREO3D_COLUMNS:
      return GST_VIDEO_MULTIVIEW_MODE_COLUMN_INTERLEAVED;
    default:
      break;
  }

  return GST_VIDEO_MULTIVIEW_MODE_NONE;
}

static gboolean
gst_ffmpegviddec_negotiate (GstFFMpegVidDec * ffmpegdec,
    AVCodecContext * context, AVFrame * picture)
{
  GstVideoFormat fmt;
  GstVideoInfo *in_info, *out_info;
  GstVideoCodecState *output_state;
  gint fps_n, fps_d;
  GstClockTime latency;
  GstStructure *in_s;

  if (!update_video_context (ffmpegdec, context, picture))
    return TRUE;

  fmt = gst_ffmpeg_pixfmt_to_videoformat (ffmpegdec->pic_pix_fmt);
  if (G_UNLIKELY (fmt == GST_VIDEO_FORMAT_UNKNOWN))
    goto unknown_format;

  output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (ffmpegdec), fmt,
      ffmpegdec->pic_width, ffmpegdec->pic_height, ffmpegdec->input_state);
  if (ffmpegdec->output_state)
    gst_video_codec_state_unref (ffmpegdec->output_state);
  ffmpegdec->output_state = output_state;

  in_info = &ffmpegdec->input_state->info;
  out_info = &ffmpegdec->output_state->info;

  /* set the interlaced flag */
  in_s = gst_caps_get_structure (ffmpegdec->input_state->caps, 0);

  if (!gst_structure_has_field (in_s, "interlace-mode")) {
    if (ffmpegdec->pic_interlaced) {
      if (ffmpegdec->pic_field_order_changed ||
          (ffmpegdec->pic_field_order & GST_VIDEO_BUFFER_FLAG_RFF)) {
        out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      } else {
        out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
        if ((ffmpegdec->pic_field_order & GST_VIDEO_BUFFER_FLAG_TFF))
          GST_VIDEO_INFO_FIELD_ORDER (out_info) =
              GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST;
        else
          GST_VIDEO_INFO_FIELD_ORDER (out_info) =
              GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST;
      }
    } else {
      out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    }
  }

  if (!gst_structure_has_field (in_s, "chroma-site")) {
    switch (context->chroma_sample_location) {
      case AVCHROMA_LOC_LEFT:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_MPEG2;
        break;
      case AVCHROMA_LOC_CENTER:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_JPEG;
        break;
      case AVCHROMA_LOC_TOPLEFT:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_DV;
        break;
      case AVCHROMA_LOC_TOP:
        out_info->chroma_site = GST_VIDEO_CHROMA_SITE_V_COSITED;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.primaries == GST_VIDEO_COLOR_PRIMARIES_UNKNOWN) {
    switch (context->color_primaries) {
      case AVCOL_PRI_BT709:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT709;
        break;
      case AVCOL_PRI_BT470M:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470M;
        break;
      case AVCOL_PRI_BT470BG:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT470BG;
        break;
      case AVCOL_PRI_SMPTE170M:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE170M;
        break;
      case AVCOL_PRI_SMPTE240M:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_SMPTE240M;
        break;
      case AVCOL_PRI_FILM:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_FILM;
        break;
      case AVCOL_PRI_BT2020:
        out_info->colorimetry.primaries = GST_VIDEO_COLOR_PRIMARIES_BT2020;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.transfer == GST_VIDEO_TRANSFER_UNKNOWN) {
    switch (context->color_trc) {
      case AVCOL_TRC_BT2020_10:
      case AVCOL_TRC_BT709:
      case AVCOL_TRC_SMPTE170M:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_BT709;
        break;
      case AVCOL_TRC_GAMMA22:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA22;
        break;
      case AVCOL_TRC_GAMMA28:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA28;
        break;
      case AVCOL_TRC_SMPTE240M:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_SMPTE240M;
        break;
      case AVCOL_TRC_LINEAR:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_GAMMA10;
        break;
      case AVCOL_TRC_LOG:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_LOG100;
        break;
      case AVCOL_TRC_LOG_SQRT:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_LOG316;
        break;
      case AVCOL_TRC_BT2020_12:
        out_info->colorimetry.transfer = GST_VIDEO_TRANSFER_BT2020_12;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN) {
    switch (context->colorspace) {
      case AVCOL_SPC_RGB:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_RGB;
        break;
      case AVCOL_SPC_BT709:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT709;
        break;
      case AVCOL_SPC_FCC:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_FCC;
        break;
      case AVCOL_SPC_BT470BG:
      case AVCOL_SPC_SMPTE170M:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT601;
        break;
      case AVCOL_SPC_SMPTE240M:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_SMPTE240M;
        break;
      case AVCOL_SPC_BT2020_NCL:
        out_info->colorimetry.matrix = GST_VIDEO_COLOR_MATRIX_BT2020;
        break;
      default:
        break;
    }
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_UNKNOWN) {
    if (context->color_range == AVCOL_RANGE_JPEG) {
      out_info->colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    } else {
      out_info->colorimetry.range = GST_VIDEO_COLOR_RANGE_16_235;
    }
  }

  /* try to find a good framerate */
  if ((in_info->fps_d && in_info->fps_n) ||
      GST_VIDEO_INFO_FLAG_IS_SET (in_info, GST_VIDEO_FLAG_VARIABLE_FPS)) {
    /* take framerate from input when it was specified (#313970) */
    fps_n = in_info->fps_n;
    fps_d = in_info->fps_d;
  } else {
    fps_n = ffmpegdec->ctx_time_d / ffmpegdec->ctx_ticks;
    fps_d = ffmpegdec->ctx_time_n;

    if (!fps_d) {
      GST_LOG_OBJECT (ffmpegdec, "invalid framerate: %d/0, -> %d/1", fps_n,
          fps_n);
      fps_d = 1;
    }
    if (gst_util_fraction_compare (fps_n, fps_d, 1000, 1) > 0) {
      GST_LOG_OBJECT (ffmpegdec, "excessive framerate: %d/%d, -> 0/1", fps_n,
          fps_d);
      fps_n = 0;
      fps_d = 1;
    }
  }

  GST_LOG_OBJECT (ffmpegdec, "setting framerate: %d/%d", fps_n, fps_d);
  out_info->fps_n = fps_n;
  out_info->fps_d = fps_d;

  /* calculate and update par now */
  gst_ffmpegviddec_update_par (ffmpegdec, in_info, out_info);

  GST_VIDEO_INFO_MULTIVIEW_MODE (out_info) = ffmpegdec->cur_multiview_mode;
  GST_VIDEO_INFO_MULTIVIEW_FLAGS (out_info) = ffmpegdec->cur_multiview_flags;

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (ffmpegdec)))
    goto negotiate_failed;

  /* The decoder is configured, we now know the true latency */
  if (fps_n) {
    latency =
        gst_util_uint64_scale_ceil (ffmpegdec->context->has_b_frames *
        GST_SECOND, fps_d, fps_n);
    gst_video_decoder_set_latency (GST_VIDEO_DECODER (ffmpegdec), latency,
        latency);
  }

  return TRUE;

  /* ERRORS */
unknown_format:
  {
    GST_ERROR_OBJECT (ffmpegdec,
        "decoder requires a video format unsupported by GStreamer");
    return FALSE;
  }
negotiate_failed:
  {
    /* Reset so we try again next time even if force==FALSE */
    ffmpegdec->pic_pix_fmt = 0;
    ffmpegdec->pic_width = 0;
    ffmpegdec->pic_height = 0;
    ffmpegdec->pic_par_n = 0;
    ffmpegdec->pic_par_d = 0;
    ffmpegdec->pic_interlaced = 0;
    ffmpegdec->pic_field_order = 0;
    ffmpegdec->pic_field_order_changed = FALSE;
    ffmpegdec->ctx_ticks = 0;
    ffmpegdec->ctx_time_n = 0;
    ffmpegdec->ctx_time_d = 0;

    GST_ERROR_OBJECT (ffmpegdec, "negotiation failed");
    return FALSE;
  }
}

/* perform qos calculations before decoding the next frame.
 *
 * Sets the skip_frame flag and if things are really bad, skips to the next
 * keyframe.
 *
 */
static void
gst_ffmpegviddec_do_qos (GstFFMpegVidDec * ffmpegdec,
    GstVideoCodecFrame * frame, gboolean * mode_switch)
{
  GstClockTimeDiff diff;
  GstSegmentFlags skip_flags =
      GST_VIDEO_DECODER_INPUT_SEGMENT (ffmpegdec).flags;

  *mode_switch = FALSE;

  if (frame == NULL)
    return;

  if (skip_flags & GST_SEGMENT_FLAG_TRICKMODE_KEY_UNITS) {
    ffmpegdec->context->skip_frame = AVDISCARD_NONKEY;
    *mode_switch = TRUE;
  } else if (skip_flags & GST_SEGMENT_FLAG_TRICKMODE) {
    ffmpegdec->context->skip_frame = AVDISCARD_NONREF;
    *mode_switch = TRUE;
  }

  if (*mode_switch == TRUE) {
    /* We've already switched mode, we can return straight away
     * without any further calculation */
    return;
  }

  diff =
      gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (ffmpegdec),
      frame);

  /* if we don't have timing info, then we don't do QoS */
  if (G_UNLIKELY (diff == G_MAXINT64)) {
    /* Ensure the skipping strategy is the default one */
    ffmpegdec->context->skip_frame = ffmpegdec->skip_frame;
    return;
  }

  GST_DEBUG_OBJECT (ffmpegdec, "decoding time %" G_GINT64_FORMAT, diff);

  if (diff > 0 && ffmpegdec->context->skip_frame != AVDISCARD_DEFAULT) {
    ffmpegdec->context->skip_frame = AVDISCARD_DEFAULT;
    *mode_switch = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec, "QOS: normal mode");
  }

  else if (diff <= 0 && ffmpegdec->context->skip_frame != AVDISCARD_NONREF) {
    ffmpegdec->context->skip_frame = AVDISCARD_NONREF;
    *mode_switch = TRUE;
    GST_DEBUG_OBJECT (ffmpegdec,
        "QOS: hurry up, diff %" G_GINT64_FORMAT " >= 0", diff);
  }
}

/* get an outbuf buffer with the current picture */
static GstFlowReturn
get_output_buffer (GstFFMpegVidDec * ffmpegdec, GstVideoCodecFrame * frame)
{
  GstFlowReturn ret = GST_FLOW_OK;
  AVFrame pic, *outpic;
  GstVideoFrame vframe;
  GstVideoInfo *info;
  gint c;

  GST_LOG_OBJECT (ffmpegdec, "get output buffer");

  if (!ffmpegdec->output_state)
    goto not_negotiated;

  ret =
      gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (ffmpegdec),
      frame);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto alloc_failed;

  /* original ffmpeg code does not handle odd sizes correctly.
   * This patched up version does */
  /* Fill avpicture */
  info = &ffmpegdec->output_state->info;
  if (!gst_video_frame_map (&vframe, info, frame->output_buffer,
          GST_MAP_READ | GST_MAP_WRITE))
    goto map_failed;

  memset (&pic, 0, sizeof (pic));
  pic.format = ffmpegdec->pic_pix_fmt;
  pic.width = GST_VIDEO_FRAME_WIDTH (&vframe);
  pic.height = GST_VIDEO_FRAME_HEIGHT (&vframe);
  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_PLANES (info)) {
      pic.data[c] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, c);
      pic.linesize[c] = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, c);
      GST_LOG_OBJECT (ffmpegdec, "[%i] linesize %d, data %p", c,
          pic.linesize[c], pic.data[c]);
    } else {
      pic.data[c] = NULL;
      pic.linesize[c] = 0;
    }
  }

  outpic = ffmpegdec->picture;

  if (av_frame_copy (&pic, outpic) != 0) {
    GST_ERROR_OBJECT (ffmpegdec, "Failed to copy output frame");
    ret = GST_FLOW_ERROR;
  }

  gst_video_frame_unmap (&vframe);

  ffmpegdec->picture->reordered_opaque = -1;

  return ret;

  /* special cases */
alloc_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, RESOURCE, FAILED,
        ("Unable to allocate memory"),
        ("The downstream pool failed to allocated buffer."));
    return ret;
  }
map_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, RESOURCE, OPEN_READ_WRITE,
        ("Cannot access memory for read and write operation."),
        ("The video memory allocated from downstream pool could not mapped for"
            "read and write."));
    return ret;
  }
not_negotiated:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "not negotiated");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static void
gst_avpacket_init (AVPacket * packet, guint8 * data, guint size)
{
  memset (packet, 0, sizeof (AVPacket));
  packet->data = data;
  packet->size = size;
}

/* gst_ffmpegviddec_[video|audio]_frame:
 * ffmpegdec:
 * data: pointer to the data to decode
 * size: size of data in bytes
 * in_timestamp: incoming timestamp.
 * in_duration: incoming duration.
 * in_offset: incoming offset (frame number).
 * ret: Return flow.
 *
 * Returns: number of bytes used in decoding. The check for successful decode is
 *   outbuf being non-NULL.
 */
static gint
gst_ffmpegviddec_video_frame (GstFFMpegVidDec * ffmpegdec,
    guint8 * data, guint size, gint * have_data, GstVideoCodecFrame * frame,
    GstFlowReturn * ret)
{
  gint len = -1;
  gboolean mode_switch;
  GstVideoCodecFrame *out_frame;
  GstFFMpegVidDecVideoFrame *out_dframe;
  AVPacket packet;
  GstBufferPool *pool;

  *ret = GST_FLOW_OK;

  /* in case we skip frames */
  ffmpegdec->picture->pict_type = -1;

  /* run QoS code, we don't stop decoding the frame when we are late because
   * else we might skip a reference frame */
  gst_ffmpegviddec_do_qos (ffmpegdec, frame, &mode_switch);

  if (frame) {
    /* save reference to the timing info */
    ffmpegdec->context->reordered_opaque = (gint64) frame->system_frame_number;
    ffmpegdec->picture->reordered_opaque = (gint64) frame->system_frame_number;

    GST_DEBUG_OBJECT (ffmpegdec, "stored opaque values idx %d",
        frame->system_frame_number);
  }

  /* now decode the frame */
  gst_avpacket_init (&packet, data, size);

  if (ffmpegdec->palette) {
    guint8 *pal;

    pal = av_packet_new_side_data (&packet, AV_PKT_DATA_PALETTE,
        AVPALETTE_SIZE);
    gst_buffer_extract (ffmpegdec->palette, 0, pal, AVPALETTE_SIZE);
    GST_DEBUG_OBJECT (ffmpegdec, "copy pal %p %p", &packet, pal);
  }

  /* This might call into get_buffer() from another thread,
   * which would cause a deadlock. Release the lock here
   * and taking it again later seems safe
   * See https://bugzilla.gnome.org/show_bug.cgi?id=726020
   */
  GST_VIDEO_DECODER_STREAM_UNLOCK (ffmpegdec);
  len = avcodec_decode_video2 (ffmpegdec->context,
      ffmpegdec->picture, have_data, &packet);
  GST_VIDEO_DECODER_STREAM_LOCK (ffmpegdec);

  GST_DEBUG_OBJECT (ffmpegdec, "after decode: len %d, have_data %d",
      len, *have_data);

  /* when we are in skip_frame mode, don't complain when ffmpeg returned
   * no data because we told it to skip stuff. */
  if (len < 0 && (mode_switch || ffmpegdec->context->skip_frame))
    len = 0;

  /* no data, we're done */
  if (len < 0 || *have_data == 0)
    goto beach;

  /* get the output picture timing info again */
  out_dframe = ffmpegdec->picture->opaque;
  out_frame = gst_video_codec_frame_ref (out_dframe->frame);

  /* also give back a buffer allocated by the frame, if any */
  gst_buffer_replace (&out_frame->output_buffer, out_dframe->buffer);
  gst_buffer_replace (&out_dframe->buffer, NULL);

  /* Extract auxilliary info not stored in the main AVframe */
  {
    GstVideoInfo *in_info = &ffmpegdec->input_state->info;
    /* Take multiview mode from upstream if present */
    ffmpegdec->picture_multiview_mode = GST_VIDEO_INFO_MULTIVIEW_MODE (in_info);
    ffmpegdec->picture_multiview_flags =
        GST_VIDEO_INFO_MULTIVIEW_FLAGS (in_info);

    /* Otherwise, see if there's info in the frame */
    if (ffmpegdec->picture_multiview_mode == GST_VIDEO_MULTIVIEW_MODE_NONE) {
      AVFrameSideData *side_data =
          av_frame_get_side_data (ffmpegdec->picture, AV_FRAME_DATA_STEREO3D);
      if (side_data) {
        AVStereo3D *stereo = (AVStereo3D *) side_data->data;
        ffmpegdec->picture_multiview_mode = stereo_av_to_gst (stereo->type);
        if (stereo->flags & AV_STEREO3D_FLAG_INVERT) {
          ffmpegdec->picture_multiview_flags =
              GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST;
        } else {
          ffmpegdec->picture_multiview_flags = GST_VIDEO_MULTIVIEW_FLAGS_NONE;
        }
      }
    }
  }

  GST_DEBUG_OBJECT (ffmpegdec,
      "pts %" G_GUINT64_FORMAT " duration %" G_GUINT64_FORMAT,
      out_frame->pts, out_frame->duration);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: pts %" G_GUINT64_FORMAT,
      (guint64) ffmpegdec->picture->pts);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: num %d",
      ffmpegdec->picture->coded_picture_number);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: display %d",
      ffmpegdec->picture->display_picture_number);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: opaque %p",
      ffmpegdec->picture->opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: reordered opaque %" G_GUINT64_FORMAT,
      (guint64) ffmpegdec->picture->reordered_opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "repeat_pict:%d",
      ffmpegdec->picture->repeat_pict);
  GST_DEBUG_OBJECT (ffmpegdec, "corrupted frame: %d",
      ! !(ffmpegdec->picture->flags & AV_FRAME_FLAG_CORRUPT));

  if (!gst_ffmpegviddec_negotiate (ffmpegdec, ffmpegdec->context,
          ffmpegdec->picture))
    goto negotiation_error;

  pool = gst_video_decoder_get_buffer_pool (GST_VIDEO_DECODER (ffmpegdec));
  if (G_UNLIKELY (out_frame->output_buffer == NULL)) {
    *ret = get_output_buffer (ffmpegdec, out_frame);
  } else if (G_UNLIKELY (out_frame->output_buffer->pool != pool)) {
    GstBuffer *tmp = out_frame->output_buffer;
    out_frame->output_buffer = NULL;
    *ret = get_output_buffer (ffmpegdec, out_frame);
    gst_buffer_unref (tmp);
  }
#ifndef G_DISABLE_ASSERT
  else {
    GstVideoMeta *vmeta = gst_buffer_get_video_meta (out_frame->output_buffer);
    if (vmeta) {
      GstVideoInfo *info = &ffmpegdec->output_state->info;
      g_assert (vmeta->width == GST_VIDEO_INFO_WIDTH (info));
      g_assert (vmeta->height == GST_VIDEO_INFO_HEIGHT (info));
    }
  }
#endif
  gst_object_unref (pool);

  if (G_UNLIKELY (*ret != GST_FLOW_OK))
    goto no_output;

  /* Mark corrupted frames as corrupted */
  if (ffmpegdec->picture->flags & AV_FRAME_FLAG_CORRUPT)
    GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_BUFFER_FLAG_CORRUPTED);

  if (ffmpegdec->pic_interlaced) {
    /* set interlaced flags */
    if (ffmpegdec->picture->repeat_pict)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_VIDEO_BUFFER_FLAG_RFF);
    if (ffmpegdec->picture->top_field_first)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    if (ffmpegdec->picture->interlaced_frame)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer,
          GST_VIDEO_BUFFER_FLAG_INTERLACED);
  }

  /* cleaning time */
  /* so we decoded this frame, frames preceding it in decoding order
   * that still do not have a buffer allocated seem rather useless,
   * and can be discarded, due to e.g. misparsed bogus frame
   * or non-keyframe in skipped decoding, ...
   * In any case, not likely to be seen again, so discard those,
   * before they pile up and/or mess with timestamping */
  {
    GList *l, *ol;
    GstVideoDecoder *dec = GST_VIDEO_DECODER (ffmpegdec);
    gboolean old = TRUE;

    ol = l = gst_video_decoder_get_frames (dec);
    while (l) {
      GstVideoCodecFrame *tmp = l->data;

      if (tmp == frame)
        old = FALSE;

      if (old && GST_VIDEO_CODEC_FRAME_IS_DECODE_ONLY (tmp)) {
        GST_LOG_OBJECT (dec,
            "discarding ghost frame %p (#%d) PTS:%" GST_TIME_FORMAT " DTS:%"
            GST_TIME_FORMAT, tmp, tmp->system_frame_number,
            GST_TIME_ARGS (tmp->pts), GST_TIME_ARGS (tmp->dts));
        /* drop extra ref and remove from frame list */
        gst_video_decoder_release_frame (dec, tmp);
      } else {
        /* drop extra ref we got */
        gst_video_codec_frame_unref (tmp);
      }
      l = l->next;
    }
    g_list_free (ol);
  }

  av_frame_unref (ffmpegdec->picture);

  /* FIXME: Ideally we would remap the buffer read-only now before pushing but
   * libav might still have a reference to it!
   */
  *ret =
      gst_video_decoder_finish_frame (GST_VIDEO_DECODER (ffmpegdec), out_frame);

beach:
  GST_DEBUG_OBJECT (ffmpegdec, "return flow %s, len %d",
      gst_flow_get_name (*ret), len);
  return len;

  /* special cases */
no_output:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "no output buffer");
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (ffmpegdec), out_frame);
    len = -1;
    goto beach;
  }

negotiation_error:
  {
    if (GST_PAD_IS_FLUSHING (GST_VIDEO_DECODER_SRC_PAD (ffmpegdec))) {
      *ret = GST_FLOW_FLUSHING;
      goto beach;
    }
    GST_WARNING_OBJECT (ffmpegdec, "Error negotiating format");
    *ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }
}


/* gst_ffmpegviddec_frame:
 * ffmpegdec:
 * data: pointer to the data to decode
 * size: size of data in bytes
 * got_data: 0 if no data was decoded, != 0 otherwise.
 * in_time: timestamp of data
 * in_duration: duration of data
 * ret: GstFlowReturn to return in the chain function
 *
 * Decode the given frame and pushes it downstream.
 *
 * Returns: Number of bytes used in decoding, -1 on error/failure.
 */

static gint
gst_ffmpegviddec_frame (GstFFMpegVidDec * ffmpegdec,
    guint8 * data, guint size, gint * have_data, GstVideoCodecFrame * frame,
    GstFlowReturn * ret)
{
  GstFFMpegVidDecClass *oclass;
  gint len = 0;

  if (G_UNLIKELY (ffmpegdec->context->codec == NULL))
    goto no_codec;

  GST_LOG_OBJECT (ffmpegdec, "data:%p, size:%d", data, size);

  *ret = GST_FLOW_OK;
  ffmpegdec->context->frame_number++;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  len =
      gst_ffmpegviddec_video_frame (ffmpegdec, data, size, have_data, frame,
      ret);

  if (len < 0) {
    GST_WARNING_OBJECT (ffmpegdec,
        "avdec_%s: decoding error (len: %d, have_data: %d)",
        oclass->in_plugin->name, len, *have_data);
  }

  return len;

  /* ERRORS */
no_codec:
  {
    GST_ERROR_OBJECT (ffmpegdec, "no codec context");
    *ret = GST_FLOW_NOT_NEGOTIATED;
    return -1;
  }
}

static GstFlowReturn
gst_ffmpegviddec_drain (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;
  GstFFMpegVidDecClass *oclass;

  if (!ffmpegdec->opened)
    return GST_FLOW_OK;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
    gint have_data, len;
    GstFlowReturn ret;

    GST_LOG_OBJECT (ffmpegdec,
        "codec has delay capabilities, calling until ffmpeg has drained everything");

    do {
      len = gst_ffmpegviddec_frame (ffmpegdec, NULL, 0, &have_data, NULL, &ret);
    } while (len >= 0 && have_data == 1 && ret == GST_FLOW_OK);
  }

  return GST_FLOW_OK;
}

static GstFlowReturn
gst_ffmpegviddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;
  guint8 *data, *bdata;
  gint size, len, have_data, bsize;
  GstMapInfo minfo;
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean do_padding;

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %" G_GSIZE_FORMAT ", dts %" GST_TIME_FORMAT
      ", pts:%" GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT,
      gst_buffer_get_size (frame->input_buffer), GST_TIME_ARGS (frame->dts),
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->duration));

  if (!gst_buffer_map (frame->input_buffer, &minfo, GST_MAP_READ)) {
    GST_ELEMENT_ERROR (ffmpegdec, STREAM, DECODE, ("Decoding problem"),
        ("Failed to map buffer for reading"));
    return GST_FLOW_ERROR;
  }

  /* treat frame as void until a buffer is requested for it */
  GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
      GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

  bdata = minfo.data;
  bsize = minfo.size;

  if (bsize > 0 && (!GST_MEMORY_IS_ZERO_PADDED (minfo.memory)
          || (minfo.maxsize - minfo.size) < FF_INPUT_BUFFER_PADDING_SIZE)) {
    /* add padding */
    if (ffmpegdec->padded_size < bsize + FF_INPUT_BUFFER_PADDING_SIZE) {
      ffmpegdec->padded_size = bsize + FF_INPUT_BUFFER_PADDING_SIZE;
      ffmpegdec->padded = g_realloc (ffmpegdec->padded, ffmpegdec->padded_size);
      GST_LOG_OBJECT (ffmpegdec, "resized padding buffer to %d",
          ffmpegdec->padded_size);
    }
    GST_CAT_TRACE_OBJECT (CAT_PERFORMANCE, ffmpegdec,
        "Copy input to add padding");
    memcpy (ffmpegdec->padded, bdata, bsize);
    memset (ffmpegdec->padded + bsize, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    bdata = ffmpegdec->padded;
    do_padding = TRUE;
  } else {
    do_padding = FALSE;
  }

  do {
    guint8 tmp_padding[FF_INPUT_BUFFER_PADDING_SIZE];

    /* parse, if at all possible */
    data = bdata;
    size = bsize;

    if (do_padding) {
      /* add temporary padding */
      GST_CAT_TRACE_OBJECT (CAT_PERFORMANCE, ffmpegdec,
          "Add temporary input padding");
      memcpy (tmp_padding, data + size, FF_INPUT_BUFFER_PADDING_SIZE);
      memset (data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    /* decode a frame of audio/video now */
    len =
        gst_ffmpegviddec_frame (ffmpegdec, data, size, &have_data, frame, &ret);

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (ffmpegdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      /* bad flow return, make sure we discard all data and exit */
      bsize = 0;
      break;
    }

    if (do_padding) {
      memcpy (data + size, tmp_padding, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    if (len == 0 && have_data == 0) {
      /* nothing was decoded, this could be because no data was available or
       * because we were skipping frames.
       * If we have no context we must exit and wait for more data, we keep the
       * data we tried. */
      GST_LOG_OBJECT (ffmpegdec, "Decoding didn't return any data, breaking");
      break;
    }

    if (len < 0) {
      /* a decoding error happened, we must break and try again with next data. */
      GST_LOG_OBJECT (ffmpegdec, "Decoding error, breaking");
      bsize = 0;
      break;
    }

    /* prepare for the next round, for codecs with a context we did this
     * already when using the parser. */
    bsize -= len;
    bdata += len;

    do_padding = TRUE;

    GST_LOG_OBJECT (ffmpegdec, "Before (while bsize>0).  bsize:%d , bdata:%p",
        bsize, bdata);
  } while (bsize > 0);

  if (bsize > 0)
    GST_DEBUG_OBJECT (ffmpegdec, "Dropping %d bytes of data", bsize);

  gst_buffer_unmap (frame->input_buffer, &minfo);
  gst_video_codec_frame_unref (frame);

  return ret;
}

static gboolean
gst_ffmpegviddec_start (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;
  GstFFMpegVidDecClass *oclass;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_OBJECT_LOCK (ffmpegdec);
  gst_ffmpeg_avcodec_close (ffmpegdec->context);
  if (avcodec_get_context_defaults3 (ffmpegdec->context, oclass->in_plugin) < 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to set context defaults");
    GST_OBJECT_UNLOCK (ffmpegdec);
    return FALSE;
  }
  ffmpegdec->context->opaque = ffmpegdec;
  GST_OBJECT_UNLOCK (ffmpegdec);

  return TRUE;
}

static gboolean
gst_ffmpegviddec_stop (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;

  GST_OBJECT_LOCK (ffmpegdec);
  gst_ffmpegviddec_close (ffmpegdec, FALSE);
  GST_OBJECT_UNLOCK (ffmpegdec);
  g_free (ffmpegdec->padded);
  ffmpegdec->padded = NULL;
  ffmpegdec->padded_size = 0;
  if (ffmpegdec->input_state)
    gst_video_codec_state_unref (ffmpegdec->input_state);
  ffmpegdec->input_state = NULL;
  if (ffmpegdec->output_state)
    gst_video_codec_state_unref (ffmpegdec->output_state);
  ffmpegdec->output_state = NULL;

  if (ffmpegdec->internal_pool)
    gst_object_unref (ffmpegdec->internal_pool);
  ffmpegdec->internal_pool = NULL;

  ffmpegdec->pic_pix_fmt = 0;
  ffmpegdec->pic_width = 0;
  ffmpegdec->pic_height = 0;
  ffmpegdec->pic_par_n = 0;
  ffmpegdec->pic_par_d = 0;
  ffmpegdec->pic_interlaced = 0;
  ffmpegdec->pic_field_order = 0;
  ffmpegdec->pic_field_order_changed = FALSE;
  ffmpegdec->ctx_ticks = 0;
  ffmpegdec->ctx_time_n = 0;
  ffmpegdec->ctx_time_d = 0;

  ffmpegdec->pool_width = 0;
  ffmpegdec->pool_height = 0;
  ffmpegdec->pool_format = 0;

  return TRUE;
}

static GstFlowReturn
gst_ffmpegviddec_finish (GstVideoDecoder * decoder)
{
  gst_ffmpegviddec_drain (decoder);
  /* note that finish can and should clean up more drastically,
   * but drain is also invoked on e.g. packet loss in GAP handling */
  gst_ffmpegviddec_flush (decoder);

  return GST_FLOW_OK;
}

static gboolean
gst_ffmpegviddec_flush (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;

  if (ffmpegdec->opened) {
    GST_LOG_OBJECT (decoder, "flushing buffers");
    avcodec_flush_buffers (ffmpegdec->context);
  }

  return TRUE;
}

static gboolean
gst_ffmpegviddec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;
  GstVideoCodecState *state;
  GstBufferPool *pool;
  guint size, min, max;
  GstStructure *config;
  gboolean have_pool, have_videometa, have_alignment, update_pool = FALSE;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = DEFAULT_ALLOC_PARAM;

  have_pool = (gst_query_get_n_allocation_pools (query) != 0);

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  state = gst_video_decoder_get_output_state (decoder);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    params.align = MAX (params.align, DEFAULT_STRIDE_ALIGN);
  } else {
    gst_query_add_allocation_param (query, allocator, &params);
  }

  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  /* Don't use pool that can't grow, as we don't know how many buffer we'll
   * need, otherwise we may stall */
  if (max != 0 && max < REQUIRED_POOL_MAX_BUFFERS) {
    gst_object_unref (pool);
    pool = gst_video_buffer_pool_new ();
    max = 0;
    update_pool = TRUE;
    have_pool = FALSE;

    /* if there is an allocator, also drop it, as it might be the reason we
     * have this limit. Default will be used */
    if (allocator) {
      gst_object_unref (allocator);
      allocator = NULL;
    }
  }

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, state->caps, size, min, max);
  gst_buffer_pool_config_set_allocator (config, allocator, &params);

  have_videometa =
      gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  if (have_videometa)
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

  have_alignment =
      gst_buffer_pool_has_option (pool, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  /* If we have videometa, we never have to copy */
  if (have_videometa && have_pool && have_alignment &&
      gst_ffmpegviddec_can_direct_render (ffmpegdec)) {
    GstStructure *config_copy = gst_structure_copy (config);

    gst_ffmpegvideodec_prepare_dr_pool (ffmpegdec, pool, &state->info,
        config_copy);

    /* FIXME validate and retry */
    if (gst_buffer_pool_set_config (pool, config_copy)) {
      GstFlowReturn ret;
      GstBuffer *tmp;

      gst_buffer_pool_set_active (pool, TRUE);
      ret = gst_buffer_pool_acquire_buffer (pool, &tmp, NULL);
      if (ret == GST_FLOW_OK) {
        GstVideoMeta *vmeta = gst_buffer_get_video_meta (tmp);
        gboolean same_stride = TRUE;
        gint i;

        for (i = 0; i < vmeta->n_planes; i++) {
          if (vmeta->stride[i] != ffmpegdec->stride[i]) {
            same_stride = FALSE;
            break;
          }
        }

        gst_buffer_unref (tmp);

        if (same_stride) {
          if (ffmpegdec->internal_pool)
            gst_object_unref (ffmpegdec->internal_pool);
          ffmpegdec->internal_pool = gst_object_ref (pool);
          ffmpegdec->pool_info = state->info;
          gst_structure_free (config);
          goto done;
        }
      }
    }
  }

  if (have_videometa && ffmpegdec->internal_pool
      && ffmpegdec->pool_width == state->info.width
      && ffmpegdec->pool_height == state->info.height) {
    update_pool = TRUE;
    gst_object_unref (pool);
    pool = gst_object_ref (ffmpegdec->internal_pool);
    gst_structure_free (config);
    goto done;
  }

  /* configure */
  if (!gst_buffer_pool_set_config (pool, config)) {
    gboolean working_pool = FALSE;
    config = gst_buffer_pool_get_config (pool);

    if (gst_buffer_pool_config_validate_params (config, state->caps, size, min,
            max)) {
      working_pool = gst_buffer_pool_set_config (pool, config);
    } else {
      gst_structure_free (config);
    }

    if (!working_pool) {
      gst_object_unref (pool);
      pool = gst_video_buffer_pool_new ();
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_set_params (config, state->caps, size, min, max);
      gst_buffer_pool_config_set_allocator (config, NULL, &params);
      gst_buffer_pool_set_config (pool, config);
      update_pool = TRUE;
    }
  }

done:
  /* and store */
  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);

  gst_object_unref (pool);
  if (allocator)
    gst_object_unref (allocator);
  gst_video_codec_state_unref (state);

  return TRUE;
}

static gboolean
gst_ffmpegviddec_propose_allocation (GstVideoDecoder * decoder,
    GstQuery * query)
{
  GstAllocationParams params;

  gst_allocation_params_init (&params);
  params.flags = GST_MEMORY_FLAG_ZERO_PADDED;
  params.align = DEFAULT_STRIDE_ALIGN;
  params.padding = FF_INPUT_BUFFER_PADDING_SIZE;
  /* we would like to have some padding so that we don't have to
   * memcpy. We don't suggest an allocator. */
  gst_query_add_allocation_param (query, NULL, &params);

  return GST_VIDEO_DECODER_CLASS (parent_class)->propose_allocation (decoder,
      query);
}

static void
gst_ffmpegviddec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) object;

  switch (prop_id) {
    case PROP_LOWRES:
      ffmpegdec->lowres = ffmpegdec->context->lowres = g_value_get_enum (value);
      break;
    case PROP_SKIPFRAME:
      ffmpegdec->skip_frame = ffmpegdec->context->skip_frame =
          g_value_get_enum (value);
      break;
    case PROP_DIRECT_RENDERING:
      ffmpegdec->direct_rendering = g_value_get_boolean (value);
      break;
    case PROP_DEBUG_MV:
      ffmpegdec->debug_mv = ffmpegdec->context->debug_mv =
          g_value_get_boolean (value);
      break;
    case PROP_MAX_THREADS:
      ffmpegdec->max_threads = g_value_get_int (value);
      break;
    case PROP_OUTPUT_CORRUPT:
      ffmpegdec->output_corrupt = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ffmpegviddec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) object;

  switch (prop_id) {
    case PROP_LOWRES:
      g_value_set_enum (value, ffmpegdec->context->lowres);
      break;
    case PROP_SKIPFRAME:
      g_value_set_enum (value, ffmpegdec->context->skip_frame);
      break;
    case PROP_DIRECT_RENDERING:
      g_value_set_boolean (value, ffmpegdec->direct_rendering);
      break;
    case PROP_DEBUG_MV:
      g_value_set_boolean (value, ffmpegdec->context->debug_mv);
      break;
    case PROP_MAX_THREADS:
      g_value_set_int (value, ffmpegdec->max_threads);
      break;
    case PROP_OUTPUT_CORRUPT:
      g_value_set_boolean (value, ffmpegdec->output_corrupt);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_ffmpegviddec_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegVidDecClass),
    (GBaseInitFunc) gst_ffmpegviddec_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegviddec_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegVidDec),
    0,
    (GInstanceInitFunc) gst_ffmpegviddec_init,
  };
  GType type;
  AVCodec *in_plugin;
  gint rank;

  in_plugin = av_codec_next (NULL);

  GST_LOG ("Registering decoders");

  while (in_plugin) {
    gchar *type_name;
    gchar *plugin_name;

    /* only video decoders */
    if (!av_codec_is_decoder (in_plugin)
        || in_plugin->type != AVMEDIA_TYPE_VIDEO)
      goto next;

    /* no quasi codecs, please */
    if (in_plugin->id == AV_CODEC_ID_RAWVIDEO ||
        in_plugin->id == AV_CODEC_ID_V210 ||
        in_plugin->id == AV_CODEC_ID_V210X ||
        in_plugin->id == AV_CODEC_ID_V308 ||
        in_plugin->id == AV_CODEC_ID_V408 ||
        in_plugin->id == AV_CODEC_ID_V410 ||
        in_plugin->id == AV_CODEC_ID_R210
        || in_plugin->id == AV_CODEC_ID_AYUV
        || in_plugin->id == AV_CODEC_ID_Y41P
        || in_plugin->id == AV_CODEC_ID_012V
        || in_plugin->id == AV_CODEC_ID_YUV4
#if AV_VERSION_INT (LIBAVCODEC_VERSION_MAJOR, LIBAVCODEC_VERSION_MINOR, LIBAVCODEC_VERSION_MICRO) >= \
        AV_VERSION_INT (57,4,0)
        || in_plugin->id == AV_CODEC_ID_WRAPPED_AVFRAME
#endif
        || in_plugin->id == AV_CODEC_ID_ZLIB) {
      goto next;
    }

    /* No decoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library decoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      goto next;
    }

    /* No vdpau plugins until we can figure out how to properly use them
     * outside of ffmpeg. */
    if (g_str_has_suffix (in_plugin->name, "_vdpau")) {
      GST_DEBUG
          ("Ignoring VDPAU decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    if (g_str_has_suffix (in_plugin->name, "_xvmc")) {
      GST_DEBUG
          ("Ignoring XVMC decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    if (strstr (in_plugin->name, "vaapi")) {
      GST_DEBUG
          ("Ignoring VAAPI decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    if (g_str_has_suffix (in_plugin->name, "_qsv")) {
      GST_DEBUG
          ("Ignoring qsv decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    /* MPEG1VIDEO : the mpeg2video decoder is preferred */
    /* MP1 : Use MP3 for decoding */
    /* MP2 : Use MP3 for decoding */
    /* Theora: Use libtheora based theoradec */
    if (!strcmp (in_plugin->name, "gif") ||
        !strcmp (in_plugin->name, "theora") ||
        !strcmp (in_plugin->name, "mpeg1video") ||
        strstr (in_plugin->name, "crystalhd") != NULL ||
        !strcmp (in_plugin->name, "ass") ||
        !strcmp (in_plugin->name, "srt") ||
        !strcmp (in_plugin->name, "pgssub") ||
        !strcmp (in_plugin->name, "dvdsub") ||
        !strcmp (in_plugin->name, "dvbsub")) {
      GST_LOG ("Ignoring decoder %s", in_plugin->name);
      goto next;
    }

    /* construct the type */
    if (!strcmp (in_plugin->name, "hevc")) {
      plugin_name = g_strdup ("h265");
    } else {
      plugin_name = g_strdup ((gchar *) in_plugin->name);
    }
    g_strdelimit (plugin_name, NULL, '_');
    type_name = g_strdup_printf ("avdec_%s", plugin_name);
    g_free (plugin_name);

    type = g_type_from_name (type_name);

    if (!type) {
      /* create the gtype now */
      type =
          g_type_register_static (GST_TYPE_VIDEO_DECODER, type_name, &typeinfo,
          0);
      g_type_set_qdata (type, GST_FFDEC_PARAMS_QDATA, (gpointer) in_plugin);
    }

    /* (Ronald) MPEG-4 gets a higher priority because it has been well-
     * tested and by far outperforms divxdec/xviddec - so we prefer it.
     * msmpeg4v3 same, as it outperforms divxdec for divx3 playback.
     * VC1/WMV3 are not working and thus unpreferred for now. */
    switch (in_plugin->id) {
      case AV_CODEC_ID_MPEG1VIDEO:
      case AV_CODEC_ID_MPEG2VIDEO:
      case AV_CODEC_ID_MPEG4:
      case AV_CODEC_ID_MSMPEG4V3:
      case AV_CODEC_ID_H264:
      case AV_CODEC_ID_HEVC:
      case AV_CODEC_ID_RV10:
      case AV_CODEC_ID_RV20:
      case AV_CODEC_ID_RV30:
      case AV_CODEC_ID_RV40:
        rank = GST_RANK_PRIMARY;
        break;
        /* DVVIDEO: we have a good dv decoder, fast on both ppc as well as x86.
         * They say libdv's quality is better though. leave as secondary.
         * note: if you change this, see the code in gstdv.c in good/ext/dv.
         */
      case AV_CODEC_ID_DVVIDEO:
        rank = GST_RANK_SECONDARY;
        break;
      default:
        rank = GST_RANK_MARGINAL;
        break;
    }
    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_warning ("Failed to register %s", type_name);
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = av_codec_next (in_plugin);
  }

  GST_LOG ("Finished Registering decoders");

  return TRUE;
}
