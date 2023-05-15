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
#include <libavutil/mastering_display_metadata.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavviddec.h"

GST_DEBUG_CATEGORY_STATIC (GST_CAT_PERFORMANCE);

#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58,132,100)
#define AV_CODEC_CAP_OTHER_THREADS AV_CODEC_CAP_AUTO_THREADS
#endif

#define GST_FFMPEG_VIDEO_CODEC_FRAME_FLAG_ALLOCATED (1<<15)

#define MAX_TS_MASK 0xff

#define DEFAULT_LOWRES			0
#define DEFAULT_SKIPFRAME		0
#define DEFAULT_DIRECT_RENDERING	TRUE
#define DEFAULT_MAX_THREADS		0
#define DEFAULT_OUTPUT_CORRUPT		TRUE
#define REQUIRED_POOL_MAX_BUFFERS       32
#define DEFAULT_STRIDE_ALIGN            31
#define DEFAULT_ALLOC_PARAM             { 0, DEFAULT_STRIDE_ALIGN, 0, 0, }
#define DEFAULT_THREAD_TYPE             0
#define DEFAULT_STD_COMPLIANCE   GST_AV_CODEC_COMPLIANCE_AUTO

enum
{
  PROP_0,
  PROP_LOWRES,
  PROP_SKIPFRAME,
  PROP_DIRECT_RENDERING,
  PROP_DEBUG_MV,
  PROP_MAX_THREADS,
  PROP_OUTPUT_CORRUPT,
  PROP_THREAD_TYPE,
  PROP_STD_COMPLIANCE,
  PROP_LAST
};

/* A number of function prototypes are given so we can refer to them later. */
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
    AVCodecContext * context, AVFrame * picture, GstBufferFlags flags);

/* some sort of bufferpool handling, but different */
static int gst_ffmpegviddec_get_buffer2 (AVCodecContext * context,
    AVFrame * picture, int flags);
static gboolean gst_ffmpegviddec_can_direct_render (GstFFMpegVidDec *
    ffmpegdec);

static GstFlowReturn gst_ffmpegviddec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_ffmpegviddec_drain (GstVideoDecoder * decoder);

static gboolean picture_changed (GstFFMpegVidDec * ffmpegdec,
    AVFrame * picture, gboolean one_field);
static gboolean context_changed (GstFFMpegVidDec * ffmpegdec,
    AVCodecContext * context);

G_DEFINE_ABSTRACT_TYPE (GstFFMpegVidDec, gst_ffmpegviddec,
    GST_TYPE_VIDEO_DECODER);

#define parent_class gst_ffmpegviddec_parent_class

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

static const GFlagsValue ffmpegdec_thread_types[] = {
  {0x0, "Auto", "auto"},
  {0x1, "Frame", "frame"},
  {0x2, "Slice", "slice"},
  {0, NULL, NULL},
};

#define GST_FFMPEGVIDDEC_TYPE_THREAD_TYPE (gst_ffmpegviddec_thread_type_get_type())
static GType
gst_ffmpegviddec_thread_type_get_type (void)
{
  static GType ffmpegdec_thread_type_type = 0;

  if (!ffmpegdec_thread_type_type) {
    ffmpegdec_thread_type_type =
        g_flags_register_static ("GstLibAVVidDecThreadType",
        ffmpegdec_thread_types);
  }
  return ffmpegdec_thread_type_type;
}

static GstCaps *
dup_caps_with_alternate (GstCaps * caps)
{
  GstCaps *with_alternate;
  GstCapsFeatures *features;

  with_alternate = gst_caps_copy (caps);
  features = gst_caps_features_new (GST_CAPS_FEATURE_FORMAT_INTERLACED, NULL);
  gst_caps_set_features_simple (with_alternate, features);

  gst_caps_set_simple (with_alternate, "interlace-mode", G_TYPE_STRING,
      "alternate", NULL);

  return with_alternate;
}

static void
gst_ffmpegviddec_class_init (GstFFMpegVidDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_ffmpegviddec_set_property;
  gobject_class->get_property = gst_ffmpegviddec_get_property;

  /**
   * GstFFMpegVidDec:std-compliance:
   *
   * Specifies standard compliance mode to use
   *
   * Since: 1.22
   */
  g_object_class_install_property (gobject_class, PROP_STD_COMPLIANCE,
      g_param_spec_enum ("std-compliance", "Standard Compliance",
          "Standard compliance mode to use", GST_TYPE_AV_CODEC_COMPLIANCE,
          DEFAULT_STD_COMPLIANCE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

static void
gst_ffmpegviddec_subclass_init (GstFFMpegVidDecClass * klass,
    gconstpointer class_data)
{
  GstVideoDecoderClass *viddec_class = GST_VIDEO_DECODER_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstPadTemplate *sinktempl, *srctempl;
  GstCaps *sinkcaps, *srccaps;
  const AVCodec *in_plugin;
  gchar *longname, *description;
  int caps;

  in_plugin = class_data;
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
  gst_caps_append (srccaps, dup_caps_with_alternate (srccaps));

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  gst_caps_unref (sinkcaps);
  gst_caps_unref (srccaps);

  klass->in_plugin = in_plugin;

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
#ifndef GST_REMOVE_DEPRECATED
  g_object_class_install_property (gobject_class, PROP_DEBUG_MV,
      g_param_spec_boolean ("debug-mv", "Debug motion vectors",
          "Whether to print motion vectors on top of the image "
          "(deprecated, non-functional)", FALSE,
          G_PARAM_DEPRECATED | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif
  g_object_class_install_property (gobject_class, PROP_OUTPUT_CORRUPT,
      g_param_spec_boolean ("output-corrupt", "Output corrupt buffers",
          "Whether libav should output frames even if corrupted",
          DEFAULT_OUTPUT_CORRUPT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  caps = klass->in_plugin->capabilities;
  if (caps & (AV_CODEC_CAP_FRAME_THREADS | AV_CODEC_CAP_SLICE_THREADS)) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_THREADS,
        g_param_spec_int ("max-threads", "Maximum decode threads",
            "Maximum number of worker threads to spawn. (0 = auto)",
            0, G_MAXINT, DEFAULT_MAX_THREADS,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_THREAD_TYPE,
        g_param_spec_flags ("thread-type", "Thread type",
            "Multithreading methods to use",
            GST_FFMPEGVIDDEC_TYPE_THREAD_TYPE,
            DEFAULT_THREAD_TYPE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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

  GST_DEBUG_CATEGORY_GET (GST_CAT_PERFORMANCE, "GST_PERFORMANCE");

  gst_type_mark_as_plugin_api (GST_FFMPEGVIDDEC_TYPE_LOWRES, 0);
  gst_type_mark_as_plugin_api (GST_FFMPEGVIDDEC_TYPE_SKIPFRAME, 0);
  gst_type_mark_as_plugin_api (GST_FFMPEGVIDDEC_TYPE_THREAD_TYPE, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_AV_CODEC_COMPLIANCE, 0);
  gst_type_mark_as_plugin_api (GST_TYPE_FFMPEGVIDDEC, 0);
}

static void
gst_ffmpegviddec_init (GstFFMpegVidDec * ffmpegdec)
{
}

static void
gst_ffmpegviddec_subinit (GstFFMpegVidDec * ffmpegdec)
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
  ffmpegdec->max_threads = DEFAULT_MAX_THREADS;
  ffmpegdec->output_corrupt = DEFAULT_OUTPUT_CORRUPT;
  ffmpegdec->thread_type = DEFAULT_THREAD_TYPE;
  ffmpegdec->std_compliance = DEFAULT_STD_COMPLIANCE;

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_DECODER_SINK_PAD (ffmpegdec));
  gst_video_decoder_set_use_default_pad_acceptcaps (GST_VIDEO_DECODER_CAST
      (ffmpegdec), TRUE);

  gst_video_decoder_set_needs_format (GST_VIDEO_DECODER (ffmpegdec), TRUE);
}

static void
gst_ffmpegviddec_finalize (GObject * object)
{
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (object);

  av_frame_free (&ffmpegdec->picture);
  avcodec_free_context (&ffmpegdec->context);

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

static void
gst_ffmpegviddec_context_set_flags2 (AVCodecContext * context, guint flags,
    gboolean enable)
{
  g_return_if_fail (context != NULL);

  if (enable)
    context->flags2 |= flags;
  else
    context->flags2 &= ~flags;
}

/* with LOCK */
static gboolean
gst_ffmpegviddec_close (GstFFMpegVidDec * ffmpegdec, gboolean reset)
{
  GstFFMpegVidDecClass *oclass;
  guint i;

  oclass = GST_FFMPEGVIDDEC_GET_CLASS (ffmpegdec);

  GST_LOG_OBJECT (ffmpegdec, "closing ffmpeg codec");

  gst_caps_replace (&ffmpegdec->last_caps, NULL);

  gst_ffmpeg_avcodec_close (ffmpegdec->context);
  ffmpegdec->opened = FALSE;

  for (i = 0; i < G_N_ELEMENTS (ffmpegdec->stride); i++)
    ffmpegdec->stride[i] = -1;

  gst_buffer_replace (&ffmpegdec->palette, NULL);

  av_freep (&ffmpegdec->context->extradata);
  if (reset) {
    avcodec_free_context (&ffmpegdec->context);
    ffmpegdec->context = avcodec_alloc_context3 (oclass->in_plugin);
    if (ffmpegdec->context == NULL) {
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
  guint i;

  oclass = GST_FFMPEGVIDDEC_GET_CLASS (ffmpegdec);

  if (gst_ffmpeg_avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0)
    goto could_not_open;

  for (i = 0; i < G_N_ELEMENTS (ffmpegdec->stride); i++)
    ffmpegdec->stride[i] = -1;

  ffmpegdec->opened = TRUE;

  GST_LOG_OBJECT (ffmpegdec, "Opened libav codec %s, id %d",
      oclass->in_plugin->name, oclass->in_plugin->id);

  gst_ffmpegviddec_context_set_flags (ffmpegdec->context,
      AV_CODEC_FLAG_OUTPUT_CORRUPT, ffmpegdec->output_corrupt);

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
gst_ffmpegviddec_needs_reset (GstFFMpegVidDec * ffmpegdec,
    GstVideoCodecState * state)
{
  GstCaps *last_caps, *new_caps;
  gboolean needs_reset;

  if (ffmpegdec->last_caps == NULL)
    return TRUE;

  last_caps = gst_caps_copy (ffmpegdec->last_caps);
  new_caps = gst_caps_copy (state->caps);

  /* Simply ignore framerate for now, this could easily be evolved per CODEC if
   * future issue are found.*/
  gst_structure_remove_field (gst_caps_get_structure (last_caps, 0),
      "framerate");
  gst_structure_remove_field (gst_caps_get_structure (new_caps, 0),
      "framerate");

  needs_reset = !gst_caps_is_equal (last_caps, new_caps);

  gst_caps_unref (last_caps);
  gst_caps_unref (new_caps);

  return needs_reset;
}

static gboolean
gst_ffmpegviddec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstFFMpegVidDec *ffmpegdec;
  GstFFMpegVidDecClass *oclass;
  GstClockTime latency = GST_CLOCK_TIME_NONE;
  gboolean ret = FALSE;
  gboolean is_live;
  GstQuery *query;

  ffmpegdec = GST_FFMPEGVIDDEC (decoder);
  oclass = GST_FFMPEGVIDDEC_GET_CLASS (ffmpegdec);

  GST_DEBUG_OBJECT (ffmpegdec, "setcaps called");

  GST_OBJECT_LOCK (ffmpegdec);

  if (!gst_ffmpegviddec_needs_reset (ffmpegdec, state)) {
    gst_caps_replace (&ffmpegdec->last_caps, state->caps);
    goto update_state;
  }

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


  query = gst_query_new_latency ();
  is_live = FALSE;
  /* Check if upstream is live. If it isn't we can enable frame based
   * threading, which is adding latency */
  if (gst_pad_peer_query (GST_VIDEO_DECODER_SINK_PAD (ffmpegdec), query)) {
    gst_query_parse_latency (query, &is_live, NULL, NULL);
  }
  gst_query_unref (query);

  if (ffmpegdec->thread_type) {
    GST_DEBUG_OBJECT (ffmpegdec, "Use requested thread type 0x%x",
        ffmpegdec->thread_type);
    ffmpegdec->context->thread_type = ffmpegdec->thread_type;
  } else {
    if (is_live)
      ffmpegdec->context->thread_type = FF_THREAD_SLICE;
    else
      ffmpegdec->context->thread_type = FF_THREAD_SLICE | FF_THREAD_FRAME;
  }

  if (ffmpegdec->max_threads == 0) {
    /* When thread type is FF_THREAD_FRAME, extra latency is introduced equal
     * to one frame per thread. We thus need to calculate the thread count ourselves */
    if ((!(oclass->in_plugin->capabilities & AV_CODEC_CAP_OTHER_THREADS)) ||
        (ffmpegdec->context->thread_type & FF_THREAD_FRAME))
      ffmpegdec->context->thread_count =
          MIN (gst_ffmpeg_auto_max_threads (), 16);
    else
      ffmpegdec->context->thread_count = 0;
  } else
    ffmpegdec->context->thread_count = ffmpegdec->max_threads;

  if (ffmpegdec->std_compliance == GST_AV_CODEC_COMPLIANCE_AUTO) {
    /* Normal yields lower latency, but fails some compliance check */
    if (is_live || ffmpegdec->context->thread_type == FF_THREAD_SLICE) {
      ffmpegdec->context->strict_std_compliance =
          GST_AV_CODEC_COMPLIANCE_NORMAL;
    } else {
      ffmpegdec->context->strict_std_compliance =
          GST_AV_CODEC_COMPLIANCE_STRICT;
    }
  } else {
    ffmpegdec->context->strict_std_compliance = ffmpegdec->std_compliance;
  }

  if (oclass->in_plugin->id == AV_CODEC_ID_H264) {
    GstStructure *s = gst_caps_get_structure (state->caps, 0);
    const char *alignment;
    gboolean nal_aligned;

    alignment = gst_structure_get_string (s, "alignment");
    nal_aligned = !g_strcmp0 (alignment, "nal");
    if (nal_aligned) {
      if (ffmpegdec->context->thread_type == FF_THREAD_FRAME)
        goto nal_only_slice;
      ffmpegdec->context->thread_type = FF_THREAD_SLICE;
    }

    gst_ffmpegviddec_context_set_flags2 (ffmpegdec->context,
        AV_CODEC_FLAG2_CHUNKS, nal_aligned);
    gst_video_decoder_set_subframe_mode (decoder, nal_aligned);
  }

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegviddec_open (ffmpegdec))
    goto open_failed;

update_state:
  if (ffmpegdec->input_state)
    gst_video_codec_state_unref (ffmpegdec->input_state);
  ffmpegdec->input_state = gst_video_codec_state_ref (state);

  /* Use the framerate values stored in the decoder for calculating latency. The
   * upstream framerate might not be set but we still want to report a latency
   * if needed. */
  if (ffmpegdec->context->time_base.den && ffmpegdec->context->ticks_per_frame) {
    gint fps_n =
        ffmpegdec->context->time_base.den / ffmpegdec->context->ticks_per_frame;
    gint fps_d = ffmpegdec->context->time_base.num;
    latency = gst_util_uint64_scale_ceil (
        (ffmpegdec->context->has_b_frames) * GST_SECOND, fps_d, fps_n);

    if (ffmpegdec->context->thread_type & FF_THREAD_FRAME) {
      latency +=
          gst_util_uint64_scale_ceil (ffmpegdec->context->thread_count *
          GST_SECOND, fps_d, fps_n);
    }
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
nal_only_slice:
  {
    GST_ERROR_OBJECT (ffmpegdec,
        "Can't do NAL aligned H.264 with frame threading.");
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

  dframe = g_new0 (GstFFMpegVidDecVideoFrame, 1);
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
  GST_VIDEO_CODEC_FRAME_FLAG_UNSET (frame->frame,
      GST_FFMPEG_VIDEO_CODEC_FRAME_FLAG_ALLOCATED);
  gst_video_decoder_release_frame (GST_VIDEO_DECODER (ffmpegdec), frame->frame);
  gst_buffer_replace (&frame->buffer, NULL);
  if (frame->avbuffer) {
    av_buffer_unref (&frame->avbuffer);
  }
  g_free (frame);
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
  gint linesize_align[AV_NUM_DATA_POINTERS];
  gint i;
  gsize max_align;

  width = GST_VIDEO_INFO_WIDTH (info);
  height = MAX (GST_VIDEO_INFO_HEIGHT (info), ffmpegdec->context->coded_height);

  /* let ffmpeg find the alignment and padding */
  avcodec_align_dimensions2 (ffmpegdec->context, &width, &height,
      linesize_align);

  align.padding_top = 0;
  align.padding_left = 0;
  align.padding_right = width - GST_VIDEO_INFO_WIDTH (info);
  align.padding_bottom = height - GST_VIDEO_INFO_HEIGHT (info);

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
    AVFrame * picture, GstVideoInterlaceMode interlace_mode)
{
  GstAllocationParams params = DEFAULT_ALLOC_PARAM;
  GstVideoInfo info;
  GstVideoFormat format;
  GstCaps *caps;
  GstStructure *config;
  guint i;

  format = gst_ffmpeg_pixfmt_to_videoformat (picture->format);

  if (ffmpegdec->internal_pool != NULL &&
      GST_VIDEO_INFO_FORMAT (&ffmpegdec->pool_info) == format &&
      ffmpegdec->pool_width == picture->width &&
      ffmpegdec->pool_height == picture->height &&
      ffmpegdec->pool_format == picture->format)
    return;

  GST_DEBUG_OBJECT (ffmpegdec, "Updating internal pool (%i, %i)",
      picture->width, picture->height);

  /* if we are negotiating from get_buffer, then renegotiate later in order
   * to potentially use a downstream pool */
  if (gst_ffmpegviddec_can_direct_render (ffmpegdec))
    gst_pad_mark_reconfigure (GST_VIDEO_DECODER_SRC_PAD (ffmpegdec));

  format = gst_ffmpeg_pixfmt_to_videoformat (picture->format);

  if (interlace_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE) {
    gst_video_info_set_interlaced_format (&info, format, interlace_mode,
        picture->width, 2 * picture->height);
  } else {
    gst_video_info_set_format (&info, format, picture->width, picture->height);
  }

  /* If we have not yet been negotiated, a NONE format here would
   * result in invalid initial dimension alignments, and potential
   * out of bounds writes.
   */
  ffmpegdec->context->pix_fmt = picture->format;

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
  ffmpegdec->pool_height =
      MAX (picture->height, ffmpegdec->context->coded_height);
  ffmpegdec->pool_format = picture->format;
  ffmpegdec->pool_info = info;
}

static gboolean
gst_ffmpegviddec_can_direct_render (GstFFMpegVidDec * ffmpegdec)
{
  GstFFMpegVidDecClass *oclass;

  if (!ffmpegdec->direct_rendering)
    return FALSE;

  oclass = GST_FFMPEGVIDDEC_GET_CLASS (ffmpegdec);
  return ((oclass->in_plugin->capabilities & AV_CODEC_CAP_DR1) ==
      AV_CODEC_CAP_DR1);
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
  guint c;
  GstFlowReturn ret;
  int create_buffer_flags = 0;

  ffmpegdec = GST_FFMPEGVIDDEC (context->opaque);

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
  GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
      GST_FFMPEG_VIDEO_CODEC_FRAME_FLAG_ALLOCATED);
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

  gst_ffmpegviddec_ensure_internal_pool (ffmpegdec, picture,
      GST_BUFFER_FLAG_IS_SET (frame->input_buffer,
          GST_VIDEO_BUFFER_FLAG_ONEFIELD) ? GST_VIDEO_INTERLACE_MODE_ALTERNATE :
      GST_VIDEO_INTERLACE_MODE_PROGRESSIVE);

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

  if ((flags & AV_GET_BUFFER_FLAG_REF) == AV_GET_BUFFER_FLAG_REF) {
    /* decoder might reuse this AVFrame and it would result to no more
     * get_buffer() call if the AVFrame's AVBuffer is writable
     * (meaning that the refcount of AVBuffer == 1).
     * To enforce get_buffer() for the every output frame, set read-only flag here
     */
    create_buffer_flags = AV_BUFFER_FLAG_READONLY;
  }
  picture->buf[0] = av_buffer_create (NULL,
      0, dummy_free_buffer, dframe, create_buffer_flags);

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
picture_changed (GstFFMpegVidDec * ffmpegdec, AVFrame * picture,
    gboolean one_field)
{
  gint pic_field_order = 0;

  if (one_field) {
    pic_field_order = ffmpegdec->pic_field_order;
  } else if (picture->interlaced_frame) {
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
    AVFrame * picture, gboolean one_field)
{
  gint pic_field_order = 0;

  if (picture->interlaced_frame) {
    if (picture->repeat_pict)
      pic_field_order |= GST_VIDEO_BUFFER_FLAG_RFF;
    if (picture->top_field_first)
      pic_field_order |= GST_VIDEO_BUFFER_FLAG_TFF;
  }

  if (!picture_changed (ffmpegdec, picture, one_field)
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
  GstVideoCodecFrame *out_frame;
  GstFFMpegVidDecVideoFrame *out_dframe;

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

  /* Special case for some encoders which provide an 1:2 pixel aspect ratio
   * for HEVC interlaced content, possibly to work around decoders that don't
   * support field-based interlacing. Add some defensive checks to check for
   * a "common" aspect ratio. */
  out_dframe = ffmpegdec->picture->opaque;
  out_frame = out_dframe->frame;

  if (demuxer_num == 1 && demuxer_denom == 1 &&
      decoder_num == 1 && decoder_denom == 2 &&
      GST_BUFFER_FLAG_IS_SET (out_frame->input_buffer,
          GST_VIDEO_BUFFER_FLAG_ONEFIELD) &&
      gst_video_is_common_aspect_ratio (ffmpegdec->pic_width,
          ffmpegdec->pic_height, 1, 2) &&
      !gst_video_is_common_aspect_ratio (ffmpegdec->pic_width,
          ffmpegdec->pic_height, 1, 1)) {
    GST_WARNING_OBJECT (ffmpegdec,
        "PAR 1/2 makes the aspect ratio of "
        "a %d x %d frame uncommon. Switching to 1/1",
        ffmpegdec->pic_width, ffmpegdec->pic_height);
    goto use_demuxer_par;
  }

  /* Both the demuxer and the decoder provide a PAR. If one of
   * the two PARs is 1:1 and the other one is not, use the one
   * that is not 1:1. */
  if (demuxer_num == demuxer_denom && decoder_num != decoder_denom) {
    goto use_decoder_par;
  }

  if (decoder_num == decoder_denom && demuxer_num != demuxer_denom) {
    goto use_demuxer_par;
  }

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
mastering_display_metadata_av_to_gst (AVMasteringDisplayMetadata * av,
    GstVideoMasteringDisplayInfo * gst)
{
  const guint64 chroma_scale = 50000;
  const guint64 luma_scale = 10000;
  gint i;

  /* Use only complete mastering meta */
  if (!av->has_primaries || !av->has_luminance)
    return FALSE;

  for (i = 0; i < G_N_ELEMENTS (gst->display_primaries); i++) {
    gst->display_primaries[i].x = (guint16) gst_util_uint64_scale (chroma_scale,
        av->display_primaries[i][0].num, av->display_primaries[i][0].den);
    gst->display_primaries[i].y = (guint16) gst_util_uint64_scale (chroma_scale,
        av->display_primaries[i][1].num, av->display_primaries[i][1].den);
  }

  gst->white_point.x = (guint16) gst_util_uint64_scale (chroma_scale,
      av->white_point[0].num, av->white_point[0].den);
  gst->white_point.y = (guint16) gst_util_uint64_scale (chroma_scale,
      av->white_point[1].num, av->white_point[1].den);


  gst->max_display_mastering_luminance =
      (guint32) gst_util_uint64_scale (luma_scale,
      av->max_luminance.num, av->max_luminance.den);
  gst->min_display_mastering_luminance =
      (guint32) gst_util_uint64_scale (luma_scale,
      av->min_luminance.num, av->min_luminance.den);

  return TRUE;
}

static gboolean
content_light_metadata_av_to_gst (AVContentLightMetadata * av,
    GstVideoContentLightLevel * gst)
{
  gst->max_content_light_level = av->MaxCLL;
  gst->max_frame_average_light_level = av->MaxFALL;

  return TRUE;
}

static gboolean
gst_ffmpegviddec_negotiate (GstFFMpegVidDec * ffmpegdec,
    AVCodecContext * context, AVFrame * picture, GstBufferFlags flags)
{
  GstVideoFormat fmt;
  GstVideoInfo *in_info, *out_info;
  GstVideoCodecState *output_state;
  gint fps_n, fps_d;
  GstClockTime latency;
  GstStructure *in_s;
  GstVideoInterlaceMode interlace_mode;
  gint caps_height;
  gboolean one_field = !!(flags & GST_VIDEO_BUFFER_FLAG_ONEFIELD);

  if (!update_video_context (ffmpegdec, context, picture, one_field))
    return TRUE;

  caps_height = ffmpegdec->pic_height;

  fmt = gst_ffmpeg_pixfmt_to_videoformat (ffmpegdec->pic_pix_fmt);
  if (G_UNLIKELY (fmt == GST_VIDEO_FORMAT_UNKNOWN))
    goto unknown_format;

  /* set the interlaced flag */
  in_s = gst_caps_get_structure (ffmpegdec->input_state->caps, 0);
  if (flags & GST_VIDEO_BUFFER_FLAG_ONEFIELD) {
    /* TODO: we don't get that information from ffmpeg, so copy it from
     * the parser */
    interlace_mode = GST_VIDEO_INTERLACE_MODE_ALTERNATE;
    caps_height = 2 * caps_height;
  } else if (!gst_structure_has_field (in_s, "interlace-mode")) {
    if (ffmpegdec->pic_interlaced) {
      if (ffmpegdec->pic_field_order_changed ||
          (ffmpegdec->pic_field_order & GST_VIDEO_BUFFER_FLAG_RFF)) {
        interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
      } else {
        interlace_mode = GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
      }
    } else {
      interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    }
  } else {
    GstVideoInfo info;

    gst_video_info_from_caps (&info, ffmpegdec->input_state->caps);
    interlace_mode = info.interlace_mode;
  }

  if (interlace_mode == GST_VIDEO_INTERLACE_MODE_ALTERNATE)
    output_state =
        gst_video_decoder_set_interlaced_output_state (GST_VIDEO_DECODER
        (ffmpegdec), fmt, interlace_mode, ffmpegdec->pic_width, caps_height,
        ffmpegdec->input_state);
  else
    output_state =
        gst_video_decoder_set_output_state (GST_VIDEO_DECODER
        (ffmpegdec), fmt, ffmpegdec->pic_width, caps_height,
        ffmpegdec->input_state);
  if (ffmpegdec->output_state)
    gst_video_codec_state_unref (ffmpegdec->output_state);
  ffmpegdec->output_state = output_state;

  in_info = &ffmpegdec->input_state->info;
  out_info = &ffmpegdec->output_state->info;

  out_info->interlace_mode = interlace_mode;
  if (!gst_structure_has_field (in_s, "interlace-mode")
      && interlace_mode == GST_VIDEO_INTERLACE_MODE_INTERLEAVED) {
    if ((ffmpegdec->pic_field_order & GST_VIDEO_BUFFER_FLAG_TFF))
      GST_VIDEO_INFO_FIELD_ORDER (out_info) =
          GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST;
    else
      GST_VIDEO_INFO_FIELD_ORDER (out_info) =
          GST_VIDEO_FIELD_ORDER_BOTTOM_FIELD_FIRST;
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
    out_info->colorimetry.primaries =
        gst_video_color_primaries_from_iso (context->color_primaries);
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.transfer == GST_VIDEO_TRANSFER_UNKNOWN) {
    out_info->colorimetry.transfer =
        gst_video_transfer_function_from_iso (context->color_trc);
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.matrix == GST_VIDEO_COLOR_MATRIX_UNKNOWN) {
    out_info->colorimetry.matrix =
        gst_video_color_matrix_from_iso (context->colorspace);
  }

  if (!gst_structure_has_field (in_s, "colorimetry")
      || in_info->colorimetry.range == GST_VIDEO_COLOR_RANGE_UNKNOWN) {
    if (context->color_range == AVCOL_RANGE_JPEG) {
      out_info->colorimetry.range = GST_VIDEO_COLOR_RANGE_0_255;
    } else if (context->color_range == AVCOL_RANGE_MPEG) {
      out_info->colorimetry.range = GST_VIDEO_COLOR_RANGE_16_235;
    } else {
      out_info->colorimetry.range = GST_VIDEO_COLOR_RANGE_UNKNOWN;
    }
  }

  /* try to find a good framerate */
  if ((in_info->fps_d && in_info->fps_n)) {
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

  if (GST_VIDEO_INFO_FLAG_IS_SET (in_info, GST_VIDEO_FLAG_VARIABLE_FPS)) {
    GST_LOG_OBJECT (ffmpegdec, "setting framerate: %d/%d", in_info->fps_n,
        in_info->fps_d);
    out_info->fps_n = in_info->fps_n;
    out_info->fps_d = in_info->fps_d;
  } else {
    GST_LOG_OBJECT (ffmpegdec, "setting framerate: %d/%d", fps_n, fps_d);
    out_info->fps_n = fps_n;
    out_info->fps_d = fps_d;
  }

  /* calculate and update par now */
  gst_ffmpegviddec_update_par (ffmpegdec, in_info, out_info);

  GST_VIDEO_INFO_MULTIVIEW_MODE (out_info) = ffmpegdec->cur_multiview_mode;
  GST_VIDEO_INFO_MULTIVIEW_FLAGS (out_info) = ffmpegdec->cur_multiview_flags;

  /* To passing HDR information to caps directly */
  if (output_state->caps == NULL) {
    output_state->caps = gst_video_info_to_caps (out_info);
  } else {
    output_state->caps = gst_caps_make_writable (output_state->caps);
  }

  if (flags & GST_VIDEO_BUFFER_FLAG_ONEFIELD) {
    /* TODO: we don't get that information from ffmpeg, so copy it from
     * the parser */
    gst_caps_features_add (gst_caps_get_features (ffmpegdec->output_state->caps,
            0), GST_CAPS_FEATURE_FORMAT_INTERLACED);
  }

  if (!gst_structure_has_field (in_s, "mastering-display-info")) {
    AVFrameSideData *sd = av_frame_get_side_data (picture,
        AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
    GstVideoMasteringDisplayInfo minfo;

    if (sd
        && mastering_display_metadata_av_to_gst ((AVMasteringDisplayMetadata *)
            sd->data, &minfo)) {
      GST_LOG_OBJECT (ffmpegdec, "update mastering display info: "
          "Red(%u, %u) "
          "Green(%u, %u) "
          "Blue(%u, %u) "
          "White(%u, %u) "
          "max_luminance(%u) "
          "min_luminance(%u) ",
          minfo.display_primaries[0].x, minfo.display_primaries[0].y,
          minfo.display_primaries[1].x, minfo.display_primaries[1].y,
          minfo.display_primaries[2].x, minfo.display_primaries[2].y,
          minfo.white_point.x, minfo.white_point.y,
          minfo.max_display_mastering_luminance,
          minfo.min_display_mastering_luminance);

      if (!gst_video_mastering_display_info_add_to_caps (&minfo,
              output_state->caps)) {
        GST_WARNING_OBJECT (ffmpegdec,
            "Couldn't set mastering display info to caps");
      }
    }
  }

  if (!gst_structure_has_field (in_s, "content-light-level")) {
    AVFrameSideData *sd = av_frame_get_side_data (picture,
        AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
    GstVideoContentLightLevel cll;

    if (sd && content_light_metadata_av_to_gst ((AVContentLightMetadata *)
            sd->data, &cll)) {
      GST_LOG_OBJECT (ffmpegdec, "update content light level: "
          "maxCLL:(%u), maxFALL:(%u)", cll.max_content_light_level,
          cll.max_frame_average_light_level);

      if (!gst_video_content_light_level_add_to_caps (&cll, output_state->caps)) {
        GST_WARNING_OBJECT (ffmpegdec,
            "Couldn't set content light level to caps");
      }
    }
  }

  if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (ffmpegdec)))
    goto negotiate_failed;

  /* The decoder is configured, we now know the true latency */
  if (fps_n) {
    latency =
        gst_util_uint64_scale_ceil (ffmpegdec->context->has_b_frames *
        GST_SECOND, fps_d, fps_n);
    if (ffmpegdec->context->thread_type & FF_THREAD_FRAME) {
      latency +=
          gst_util_uint64_scale_ceil (ffmpegdec->context->thread_count *
          GST_SECOND, fps_d, fps_n);
    }
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
  guint c;

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

/*
 * Returns: whether a frame was decoded
 */
static gboolean
gst_ffmpegviddec_video_frame (GstFFMpegVidDec * ffmpegdec,
    GstVideoCodecFrame * frame, GstFlowReturn * ret)
{
  gint res;
  gboolean got_frame = FALSE;
  gboolean mode_switch;
  GstVideoCodecFrame *out_frame;
  GstFFMpegVidDecVideoFrame *out_dframe;
  GstBufferPool *pool;

  *ret = GST_FLOW_OK;

  /* in case we skip frames */
  ffmpegdec->picture->pict_type = -1;

  /* run QoS code, we don't stop decoding the frame when we are late because
   * else we might skip a reference frame */
  gst_ffmpegviddec_do_qos (ffmpegdec, frame, &mode_switch);

  res = avcodec_receive_frame (ffmpegdec->context, ffmpegdec->picture);

  /* No frames available at this time */
  if (res == AVERROR (EAGAIN)) {
    GST_DEBUG_OBJECT (ffmpegdec, "Need more data");
    goto beach;
  } else if (res == AVERROR_EOF) {
    *ret = GST_FLOW_EOS;
    GST_DEBUG_OBJECT (ffmpegdec, "Context was entirely flushed");
    goto beach;
  } else if (res < 0) {
    GST_VIDEO_DECODER_ERROR (ffmpegdec, 1, STREAM, DECODE, (NULL),
        ("Video decoding error"), *ret);
    goto beach;
  }

  got_frame = TRUE;

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
#if LIBAVUTIL_VERSION_MAJOR < 58
  GST_DEBUG_OBJECT (ffmpegdec, "picture: num %d",
      ffmpegdec->picture->coded_picture_number);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: display %d",
      ffmpegdec->picture->display_picture_number);
#endif
  GST_DEBUG_OBJECT (ffmpegdec, "picture: opaque %p",
      ffmpegdec->picture->opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: reordered opaque %" G_GUINT64_FORMAT,
      (guint64) ffmpegdec->picture->reordered_opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "repeat_pict:%d",
      ffmpegdec->picture->repeat_pict);
  GST_DEBUG_OBJECT (ffmpegdec, "corrupted frame: %d",
      !!(ffmpegdec->picture->flags & AV_FRAME_FLAG_CORRUPT));

  if (!gst_ffmpegviddec_negotiate (ffmpegdec, ffmpegdec->context,
          ffmpegdec->picture, GST_BUFFER_FLAGS (out_frame->input_buffer)))
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
      g_assert ((gint) vmeta->width == GST_VIDEO_INFO_WIDTH (info));
      g_assert ((gint) vmeta->height == GST_VIDEO_INFO_HEIGHT (info));
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

  {
    AVFrameSideData *side_data =
        av_frame_get_side_data (ffmpegdec->picture, AV_FRAME_DATA_A53_CC);
    if (side_data) {
      GST_LOG_OBJECT (ffmpegdec,
          "Found CC side data of type AV_FRAME_DATA_A53_CC, size %d",
          (int) side_data->size);
      GST_MEMDUMP ("A53 CC", side_data->data, side_data->size);

      /* do not add closed caption meta if it already exists */
      if (!gst_buffer_get_meta (out_frame->input_buffer,
              GST_VIDEO_CAPTION_META_API_TYPE)) {
        out_frame->output_buffer =
            gst_buffer_make_writable (out_frame->output_buffer);
        gst_buffer_add_video_caption_meta (out_frame->output_buffer,
            GST_VIDEO_CAPTION_TYPE_CEA708_RAW, side_data->data,
            side_data->size);
      } else {
        GST_LOG_OBJECT (ffmpegdec,
            "Closed caption meta already exists: will not add new caption meta");
      }
    }
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
        GST_VIDEO_CODEC_FRAME_FLAG_UNSET (tmp,
            GST_FFMPEG_VIDEO_CODEC_FRAME_FLAG_ALLOCATED);
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

  if (frame)
    GST_VIDEO_CODEC_FRAME_FLAG_UNSET (frame,
        GST_FFMPEG_VIDEO_CODEC_FRAME_FLAG_ALLOCATED);

  if (gst_video_decoder_get_subframe_mode (GST_VIDEO_DECODER (ffmpegdec)))
    gst_video_decoder_have_last_subframe (GST_VIDEO_DECODER (ffmpegdec),
        out_frame);

  /* FIXME: Ideally we would remap the buffer read-only now before pushing but
   * libav might still have a reference to it!
   */
  if (GST_BUFFER_FLAG_IS_SET (out_frame->input_buffer,
          GST_VIDEO_BUFFER_FLAG_ONEFIELD)) {
    GST_BUFFER_FLAG_SET (out_frame->output_buffer,
        GST_VIDEO_BUFFER_FLAG_ONEFIELD);
    if (GST_BUFFER_FLAG_IS_SET (out_frame->input_buffer,
            GST_VIDEO_BUFFER_FLAG_TFF)) {
      GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    }
  }
  *ret =
      gst_video_decoder_finish_frame (GST_VIDEO_DECODER (ffmpegdec), out_frame);

beach:
  GST_DEBUG_OBJECT (ffmpegdec, "return flow %s, got frame: %d",
      gst_flow_get_name (*ret), got_frame);
  return got_frame;

  /* special cases */
no_output:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "no output buffer");
    GST_VIDEO_CODEC_FRAME_FLAG_UNSET (frame,
        GST_FFMPEG_VIDEO_CODEC_FRAME_FLAG_ALLOCATED);
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (ffmpegdec), out_frame);
    goto beach;
  }

negotiation_error:
  {
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (ffmpegdec), out_frame);
    if (GST_PAD_IS_FLUSHING (GST_VIDEO_DECODER_SRC_PAD (ffmpegdec))) {
      *ret = GST_FLOW_FLUSHING;
      goto beach;
    }
    GST_WARNING_OBJECT (ffmpegdec, "Error negotiating format");
    *ret = GST_FLOW_NOT_NEGOTIATED;
    goto beach;
  }
}


 /* Returns: Whether a frame was decoded */
static gboolean
gst_ffmpegviddec_frame (GstFFMpegVidDec * ffmpegdec, GstVideoCodecFrame * frame,
    GstFlowReturn * ret)
{
  gboolean got_frame = FALSE;

  if (G_UNLIKELY (ffmpegdec->context->codec == NULL))
    goto no_codec;

  *ret = GST_FLOW_OK;
#if LIBAVCODEC_VERSION_MAJOR >= 60
  ffmpegdec->context->frame_num++;
#else
  ffmpegdec->context->frame_number++;
#endif

  got_frame = gst_ffmpegviddec_video_frame (ffmpegdec, frame, ret);

  return got_frame;

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
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean got_frame = FALSE;

  if (!ffmpegdec->opened)
    return GST_FLOW_OK;

  GST_VIDEO_DECODER_STREAM_UNLOCK (ffmpegdec);
  if (avcodec_send_packet (ffmpegdec->context, NULL)) {
    GST_VIDEO_DECODER_STREAM_LOCK (ffmpegdec);
    goto send_packet_failed;
  }
  GST_VIDEO_DECODER_STREAM_LOCK (ffmpegdec);

  do {
    got_frame = gst_ffmpegviddec_frame (ffmpegdec, NULL, &ret);
  } while (got_frame && ret == GST_FLOW_OK);

  GST_VIDEO_DECODER_STREAM_UNLOCK (ffmpegdec);
  avcodec_flush_buffers (ffmpegdec->context);
  GST_VIDEO_DECODER_STREAM_LOCK (ffmpegdec);

  /* FFMpeg will return AVERROR_EOF if it's internal was fully drained
   * then we are translating it to GST_FLOW_EOS. However, because this behavior
   * is fully internal stuff of this implementation and gstvideodecoder
   * baseclass doesn't convert this GST_FLOW_EOS to GST_FLOW_OK,
   * convert this flow returned here */
  if (ret == GST_FLOW_EOS)
    ret = GST_FLOW_OK;

done:
  return ret;

send_packet_failed:
  GST_WARNING_OBJECT (ffmpegdec, "send packet failed, could not drain decoder");
  goto done;
}

static GstFlowReturn
gst_ffmpegviddec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
{
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (decoder);
  guint8 *data;
  gint size;
  gboolean got_frame;
  GstMapInfo minfo;
  GstFlowReturn ret = GST_FLOW_OK;
  AVPacket packet;

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
  if (!GST_VIDEO_CODEC_FRAME_FLAG_IS_SET (frame,
          GST_FFMPEG_VIDEO_CODEC_FRAME_FLAG_ALLOCATED))
    GST_VIDEO_CODEC_FRAME_FLAG_SET (frame,
        GST_VIDEO_CODEC_FRAME_FLAG_DECODE_ONLY);

  data = minfo.data;
  size = minfo.size;

  if (size > 0 && (!GST_MEMORY_IS_ZERO_PADDED (minfo.memory)
          || (minfo.maxsize - minfo.size) < AV_INPUT_BUFFER_PADDING_SIZE)) {
    /* add padding */
    if (ffmpegdec->padded_size < size + AV_INPUT_BUFFER_PADDING_SIZE) {
      ffmpegdec->padded_size = size + AV_INPUT_BUFFER_PADDING_SIZE;
      ffmpegdec->padded = g_realloc (ffmpegdec->padded, ffmpegdec->padded_size);
      GST_LOG_OBJECT (ffmpegdec, "resized padding buffer to %d",
          ffmpegdec->padded_size);
    }
    GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, ffmpegdec,
        "Copy input to add padding");
    memcpy (ffmpegdec->padded, data, size);
    memset (ffmpegdec->padded + size, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    data = ffmpegdec->padded;
  }

  /* now decode the frame */
  gst_avpacket_init (&packet, data, size);

  if (!packet.size)
    goto done;

  if (ffmpegdec->palette) {
    guint8 *pal;

    pal = av_packet_new_side_data (&packet, AV_PKT_DATA_PALETTE,
        AVPALETTE_SIZE);
    gst_buffer_extract (ffmpegdec->palette, 0, pal, AVPALETTE_SIZE);
    GST_DEBUG_OBJECT (ffmpegdec, "copy pal %p %p", &packet, pal);
  }

  /* save reference to the timing info */
  ffmpegdec->context->reordered_opaque = (gint64) frame->system_frame_number;
  ffmpegdec->picture->reordered_opaque = (gint64) frame->system_frame_number;

  GST_DEBUG_OBJECT (ffmpegdec, "stored opaque values idx %d",
      frame->system_frame_number);

  /* This might call into get_buffer() from another thread,
   * which would cause a deadlock. Release the lock here
   * and taking it again later seems safe
   * See https://bugzilla.gnome.org/show_bug.cgi?id=726020
   */
  GST_VIDEO_DECODER_STREAM_UNLOCK (ffmpegdec);
  if (avcodec_send_packet (ffmpegdec->context, &packet) < 0) {
    GST_VIDEO_DECODER_STREAM_LOCK (ffmpegdec);
    av_packet_free_side_data (&packet);
    goto send_packet_failed;
  }
  av_packet_free_side_data (&packet);
  GST_VIDEO_DECODER_STREAM_LOCK (ffmpegdec);

  do {
    /* decode a frame of audio/video now */
    got_frame = gst_ffmpegviddec_frame (ffmpegdec, frame, &ret);

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (ffmpegdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      break;
    }
  } while (got_frame);

done:
  gst_buffer_unmap (frame->input_buffer, &minfo);
  gst_video_codec_frame_unref (frame);

  return ret;

send_packet_failed:
  {
    GST_VIDEO_DECODER_ERROR (decoder, 1, STREAM, DECODE,
        ("Failed to send data for decoding"), ("Invalid input packet"), ret);
    goto done;
  }
}

static gboolean
gst_ffmpegviddec_start (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (decoder);
  GstFFMpegVidDecClass *oclass;

  oclass = GST_FFMPEGVIDDEC_GET_CLASS (ffmpegdec);

  GST_OBJECT_LOCK (ffmpegdec);
  avcodec_free_context (&ffmpegdec->context);
  ffmpegdec->context = avcodec_alloc_context3 (oclass->in_plugin);
  if (ffmpegdec->context == NULL) {
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
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (decoder);

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
  GstFlowReturn flow_ret;

  flow_ret = gst_ffmpegviddec_drain (decoder);

  /* note that finish can and should clean up more drastically,
   * but drain is also invoked on e.g. packet loss in GAP handling */
  gst_ffmpegviddec_flush (decoder);

  return flow_ret;
}

static gboolean
gst_ffmpegviddec_flush (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (decoder);

  if (ffmpegdec->opened) {
    GST_LOG_OBJECT (decoder, "flushing buffers");
    GST_VIDEO_DECODER_STREAM_UNLOCK (ffmpegdec);
    avcodec_flush_buffers (ffmpegdec->context);
    GST_VIDEO_DECODER_STREAM_LOCK (ffmpegdec);
  }

  return TRUE;
}

static gboolean
gst_ffmpegviddec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (decoder);
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
        guint i;

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
          ffmpegdec->pool_width = GST_VIDEO_INFO_WIDTH (&state->info);
          ffmpegdec->pool_height =
              MAX (GST_VIDEO_INFO_HEIGHT (&state->info),
              ffmpegdec->context->coded_height);
          ffmpegdec->pool_info = state->info;
          gst_structure_free (config);
          goto done;
        }
      }
    }
  }

  if (have_videometa && ffmpegdec->internal_pool
      && gst_ffmpeg_pixfmt_to_videoformat (ffmpegdec->pool_format) ==
      GST_VIDEO_INFO_FORMAT (&state->info)
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
  params.padding = AV_INPUT_BUFFER_PADDING_SIZE;
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
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (object);

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
#ifndef GST_REMOVE_DEPRECATED
    case PROP_DEBUG_MV:
      /* non-functional */
      break;
#endif
    case PROP_MAX_THREADS:
      ffmpegdec->max_threads = g_value_get_int (value);
      break;
    case PROP_OUTPUT_CORRUPT:
      ffmpegdec->output_corrupt = g_value_get_boolean (value);
      break;
    case PROP_THREAD_TYPE:
      ffmpegdec->thread_type = g_value_get_flags (value);
      break;
    case PROP_STD_COMPLIANCE:
      ffmpegdec->std_compliance = g_value_get_enum (value);
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
  GstFFMpegVidDec *ffmpegdec = GST_FFMPEGVIDDEC (object);

  switch (prop_id) {
    case PROP_LOWRES:
      g_value_set_enum (value, ffmpegdec->lowres);
      break;
    case PROP_SKIPFRAME:
      g_value_set_enum (value, ffmpegdec->skip_frame);
      break;
    case PROP_DIRECT_RENDERING:
      g_value_set_boolean (value, ffmpegdec->direct_rendering);
      break;
#ifndef GST_REMOVE_DEPRECATED
    case PROP_DEBUG_MV:
      g_value_set_boolean (value, FALSE);
      break;
#endif
    case PROP_MAX_THREADS:
      g_value_set_int (value, ffmpegdec->max_threads);
      break;
    case PROP_OUTPUT_CORRUPT:
      g_value_set_boolean (value, ffmpegdec->output_corrupt);
      break;
    case PROP_THREAD_TYPE:
      g_value_set_flags (value, ffmpegdec->thread_type);
      break;
    case PROP_STD_COMPLIANCE:
      g_value_set_enum (value, ffmpegdec->std_compliance);
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
    NULL,
    NULL,
    (GClassInitFunc) gst_ffmpegviddec_subclass_init,
    NULL,
    NULL,
    sizeof (GstFFMpegVidDec),
    0,
    (GInstanceInitFunc) gst_ffmpegviddec_subinit,
  };
  GType type;
  AVCodec *in_plugin;
  gint rank;
  void *i = 0;

  GST_LOG ("Registering decoders");

  while ((in_plugin = (AVCodec *) av_codec_iterate (&i))) {
    gchar *type_name;
    gchar *plugin_name;

    /* only video decoders */
    if (!av_codec_is_decoder (in_plugin)
        || in_plugin->type != AVMEDIA_TYPE_VIDEO)
      continue;

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
      continue;
    }

    /* No decoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library decoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      continue;
    }

    /* Skip hardware or hybrid (hardware with software fallback) */
    if ((in_plugin->capabilities & AV_CODEC_CAP_HARDWARE) ==
        AV_CODEC_CAP_HARDWARE) {
      GST_DEBUG
          ("Ignoring hardware decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    if ((in_plugin->capabilities & AV_CODEC_CAP_HYBRID) == AV_CODEC_CAP_HYBRID) {
      GST_DEBUG
          ("Ignoring hybrid decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    /* No vdpau plugins until we can figure out how to properly use them
     * outside of ffmpeg. */
    if (g_str_has_suffix (in_plugin->name, "_vdpau")) {
      GST_DEBUG
          ("Ignoring VDPAU decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    if (g_str_has_suffix (in_plugin->name, "_xvmc")) {
      GST_DEBUG
          ("Ignoring XVMC decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    if (strstr (in_plugin->name, "vaapi")) {
      GST_DEBUG
          ("Ignoring VAAPI decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    if (g_str_has_suffix (in_plugin->name, "_qsv")) {
      GST_DEBUG
          ("Ignoring qsv decoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    /* MPEG1VIDEO : the mpeg2video decoder is preferred */
    /* MP1 : Use MP3 for decoding */
    /* MP2 : Use MP3 for decoding */
    /* Theora: Use libtheora based theoradec */
    /* CDG: use cdgdec */
    /* AV1: Use av1dec, dav1ddec or any of the hardware decoders.
     *      Also ffmpeg's decoder only works with hardware support!
     */
    if (!strcmp (in_plugin->name, "theora") ||
        !strcmp (in_plugin->name, "mpeg1video") ||
        strstr (in_plugin->name, "crystalhd") != NULL ||
        !strcmp (in_plugin->name, "ass") ||
        !strcmp (in_plugin->name, "srt") ||
        !strcmp (in_plugin->name, "pgssub") ||
        !strcmp (in_plugin->name, "dvdsub") ||
        !strcmp (in_plugin->name, "dvbsub") ||
        !strcmp (in_plugin->name, "cdgraphics") ||
        !strcmp (in_plugin->name, "av1")) {
      GST_LOG ("Ignoring decoder %s", in_plugin->name);
      continue;
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
      typeinfo.class_data = in_plugin;
      type =
          g_type_register_static (GST_TYPE_FFMPEGVIDDEC, type_name, &typeinfo,
          0);
    }

    /* (Ronald) MPEG-4 gets a higher priority because it has been well-
     * tested and by far outperforms divxdec/xviddec - so we prefer it.
     * msmpeg4v3 same, as it outperforms divxdec for divx3 playback. */
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
  }

  GST_LOG ("Finished Registering decoders");

  return TRUE;
}
