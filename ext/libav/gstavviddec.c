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

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavviddec.h"

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

#define MAX_TS_MASK 0xff

#define DEFAULT_LOWRES			0
#define DEFAULT_SKIPFRAME		0
#define DEFAULT_DIRECT_RENDERING	TRUE
#define DEFAULT_DEBUG_MV		FALSE
#define DEFAULT_MAX_THREADS		0

enum
{
  PROP_0,
  PROP_LOWRES,
  PROP_SKIPFRAME,
  PROP_DIRECT_RENDERING,
  PROP_DEBUG_MV,
  PROP_MAX_THREADS,
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
    AVCodecContext * context, gboolean force);

/* some sort of bufferpool handling, but different */
static int gst_ffmpegviddec_get_buffer (AVCodecContext * context,
    AVFrame * picture);
static int gst_ffmpegviddec_reget_buffer (AVCodecContext * context,
    AVFrame * picture);
static void gst_ffmpegviddec_release_buffer (AVCodecContext * context,
    AVFrame * picture);

static GstFlowReturn gst_ffmpegviddec_finish (GstVideoDecoder * decoder);
static void gst_ffmpegviddec_drain (GstFFMpegVidDec * ffmpegdec);

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
  ffmpegdec->picture = avcodec_alloc_frame ();
  ffmpegdec->opened = FALSE;
  ffmpegdec->skip_frame = ffmpegdec->lowres = 0;
  ffmpegdec->direct_rendering = DEFAULT_DIRECT_RENDERING;
  ffmpegdec->debug_mv = DEFAULT_DEBUG_MV;
  ffmpegdec->max_threads = DEFAULT_MAX_THREADS;
}

static void
gst_ffmpegviddec_finalize (GObject * object)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) object;

  if (ffmpegdec->context != NULL) {
    av_free (ffmpegdec->context);
    ffmpegdec->context = NULL;
  }

  avcodec_free_frame (&ffmpegdec->picture);

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
  ffmpegdec->is_realvideo = FALSE;

  GST_LOG_OBJECT (ffmpegdec, "Opened libav codec %s, id %d",
      oclass->in_plugin->name, oclass->in_plugin->id);

  switch (oclass->in_plugin->id) {
    case AV_CODEC_ID_RV10:
    case AV_CODEC_ID_RV30:
    case AV_CODEC_ID_RV20:
    case AV_CODEC_ID_RV40:
      ffmpegdec->is_realvideo = TRUE;
      break;
    default:
      GST_LOG_OBJECT (ffmpegdec, "Parser deactivated for format");
      break;
  }

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
    gst_ffmpegviddec_drain (ffmpegdec);
    GST_OBJECT_LOCK (ffmpegdec);
    if (!gst_ffmpegviddec_close (ffmpegdec, TRUE)) {
      GST_OBJECT_UNLOCK (ffmpegdec);
      return FALSE;
    }
  }

  gst_caps_replace (&ffmpegdec->last_caps, state->caps);

  /* set buffer functions */
  ffmpegdec->context->get_buffer = gst_ffmpegviddec_get_buffer;
  ffmpegdec->context->reget_buffer = gst_ffmpegviddec_reget_buffer;
  ffmpegdec->context->release_buffer = gst_ffmpegviddec_release_buffer;
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
  GstVideoCodecFrame *frame;
  gboolean mapped;
  GstVideoFrame vframe;
} GstFFMpegVidDecVideoFrame;

static GstFFMpegVidDecVideoFrame *
gst_ffmpegviddec_video_frame_new (GstVideoCodecFrame * frame)
{
  GstFFMpegVidDecVideoFrame *dframe;

  dframe = g_slice_new0 (GstFFMpegVidDecVideoFrame);
  dframe->frame = frame;

  return dframe;
}

static void
gst_ffmpegviddec_video_frame_free (GstFFMpegVidDecVideoFrame * frame)
{
  if (frame->mapped)
    gst_video_frame_unmap (&frame->vframe);
  gst_video_codec_frame_unref (frame->frame);
  g_slice_free (GstFFMpegVidDecVideoFrame, frame);
}

/* called when ffmpeg wants us to allocate a buffer to write the decoded frame
 * into. We try to give it memory from our pool */
static int
gst_ffmpegviddec_get_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstVideoCodecFrame *frame;
  GstFFMpegVidDecVideoFrame *dframe;
  GstFFMpegVidDec *ffmpegdec;
  gint c;
  GstVideoInfo *info;
  GstFlowReturn ret;

  ffmpegdec = (GstFFMpegVidDec *) context->opaque;

  GST_DEBUG_OBJECT (ffmpegdec, "getting buffer picture %p", picture);

  /* apply the last info we have seen to this picture, when we get the
   * picture back from ffmpeg we can use this to correctly timestamp the output
   * buffer */
  picture->reordered_opaque = context->reordered_opaque;

  frame =
      gst_video_decoder_get_frame (GST_VIDEO_DECODER (ffmpegdec),
      picture->reordered_opaque);
  if (G_UNLIKELY (frame == NULL))
    goto no_frame;

  if (G_UNLIKELY (frame->output_buffer != NULL))
    goto duplicate_frame;

  /* GstFFMpegVidDecVideoFrame receives the frame ref */
  picture->opaque = dframe = gst_ffmpegviddec_video_frame_new (frame);

  GST_DEBUG_OBJECT (ffmpegdec, "storing opaque %p", dframe);

  ffmpegdec->context->pix_fmt = context->pix_fmt;

  /* see if we need renegotiation */
  if (G_UNLIKELY (!gst_ffmpegviddec_negotiate (ffmpegdec, context, FALSE)))
    goto negotiate_failed;

  if (!ffmpegdec->current_dr)
    goto no_dr;

  ret =
      gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (ffmpegdec),
      frame);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  /* Fill avpicture */
  info = &ffmpegdec->output_state->info;
  if (!gst_video_frame_map (&dframe->vframe, info, dframe->frame->output_buffer,
          GST_MAP_READWRITE))
    goto invalid_frame;
  dframe->mapped = TRUE;

  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_PLANES (info)) {
      picture->data[c] = GST_VIDEO_FRAME_PLANE_DATA (&dframe->vframe, c);
      picture->linesize[c] = GST_VIDEO_FRAME_PLANE_STRIDE (&dframe->vframe, c);

      /* libav does not allow stride changes currently, fall back to
       * non-direct rendering here:
       * https://bugzilla.gnome.org/show_bug.cgi?id=704769
       * https://bugzilla.libav.org/show_bug.cgi?id=556
       */
      if (ffmpegdec->stride[c] == -1) {
        ffmpegdec->stride[c] = picture->linesize[c];
      } else if (picture->linesize[c] != ffmpegdec->stride[c]) {
        GST_LOG_OBJECT (ffmpegdec,
            "No direct rendering, stride changed c=%d %d->%d", c,
            ffmpegdec->stride[c], picture->linesize[c]);

        for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
          picture->data[c] = NULL;
          picture->linesize[c] = 0;
        }
        gst_video_frame_unmap (&dframe->vframe);
        dframe->mapped = FALSE;
        gst_buffer_replace (&frame->output_buffer, NULL);
        ffmpegdec->current_dr = FALSE;

        goto no_dr;
      }
    } else {
      picture->data[c] = NULL;
      picture->linesize[c] = 0;
    }
    GST_LOG_OBJECT (ffmpegdec, "linesize %d, data %p", picture->linesize[c],
        picture->data[c]);
  }

  /* tell ffmpeg we own this buffer, tranfer the ref we have on the buffer to
   * the opaque data. */
  picture->type = FF_BUFFER_TYPE_USER;

  GST_LOG_OBJECT (ffmpegdec, "returned frame %p", frame->output_buffer);

  return 0;

  /* fallbacks */
negotiate_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "negotiate failed");
    goto fallback;
  }
no_dr:
  {
    GST_LOG_OBJECT (ffmpegdec, "direct rendering disabled, fallback alloc");
    goto fallback;
  }
alloc_failed:
  {
    /* alloc default buffer when we can't get one from downstream */
    GST_LOG_OBJECT (ffmpegdec, "alloc failed, fallback alloc");
    goto fallback;
  }
invalid_frame:
  {
    /* alloc default buffer when we can't get one from downstream */
    GST_LOG_OBJECT (ffmpegdec, "failed to map frame, fallback alloc");
    gst_buffer_unref (frame->output_buffer);
    frame->output_buffer = NULL;
    goto fallback;
  }
fallback:
  {
    int c;
    int ret = avcodec_default_get_buffer (context, picture);

    for (c = 0; c < AV_NUM_DATA_POINTERS; c++)
      ffmpegdec->stride[c] = picture->linesize[c];

    return ret;
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

static int
gst_ffmpegviddec_reget_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstVideoCodecFrame *frame;
  GstFFMpegVidDecVideoFrame *dframe;
  GstFFMpegVidDec *ffmpegdec;

  ffmpegdec = (GstFFMpegVidDec *) context->opaque;

  GST_DEBUG_OBJECT (ffmpegdec, "regetting buffer picture %p", picture);

  picture->reordered_opaque = context->reordered_opaque;

  /* if there is no opaque, we didn't yet attach any frame to it. What usually
   * happens is that avcodec_default_reget_buffer will call the getbuffer
   * function. */
  dframe = picture->opaque;
  if (dframe == NULL)
    goto done;

  frame =
      gst_video_decoder_get_frame (GST_VIDEO_DECODER (ffmpegdec),
      picture->reordered_opaque);
  if (G_UNLIKELY (frame == NULL))
    goto no_frame;

  if (G_UNLIKELY (frame->output_buffer != NULL))
    goto duplicate_frame;

  /* replace the frame, this one contains the pts/dts for the correspoding input
   * buffer, which we need after decoding. */
  gst_video_codec_frame_unref (dframe->frame);
  dframe->frame = frame;

done:
  return avcodec_default_reget_buffer (context, picture);

  /* ERRORS */
no_frame:
  {
    GST_WARNING_OBJECT (ffmpegdec, "Couldn't get codec frame !");
    return -1;
  }
duplicate_frame:
  {
    GST_WARNING_OBJECT (ffmpegdec, "already alloc'ed output buffer for frame");
    return -1;
  }
}

/* called when ffmpeg is done with our buffer */
static void
gst_ffmpegviddec_release_buffer (AVCodecContext * context, AVFrame * picture)
{
  gint i;
  GstFFMpegVidDecVideoFrame *frame;
  GstFFMpegVidDec *ffmpegdec;

  ffmpegdec = (GstFFMpegVidDec *) context->opaque;
  frame = (GstFFMpegVidDecVideoFrame *) picture->opaque;
  GST_DEBUG_OBJECT (ffmpegdec, "release frame %d",
      frame->frame->system_frame_number);

  /* check if it was our buffer */
  if (picture->type != FF_BUFFER_TYPE_USER) {
    GST_DEBUG_OBJECT (ffmpegdec, "default release buffer");
    avcodec_default_release_buffer (context, picture);
  }

  /* we remove the opaque data now */
  picture->opaque = NULL;

  gst_ffmpegviddec_video_frame_free (frame);

  /* zero out the reference in ffmpeg */
  for (i = 0; i < 4; i++) {
    picture->data[i] = NULL;
    picture->linesize[i] = 0;
  }
}

static gboolean
update_video_context (GstFFMpegVidDec * ffmpegdec, AVCodecContext * context,
    gboolean force)
{
  if (!force && ffmpegdec->ctx_width == context->width
      && ffmpegdec->ctx_height == context->height
      && ffmpegdec->ctx_ticks == context->ticks_per_frame
      && ffmpegdec->ctx_time_n == context->time_base.num
      && ffmpegdec->ctx_time_d == context->time_base.den
      && ffmpegdec->ctx_pix_fmt == context->pix_fmt
      && ffmpegdec->ctx_par_n == context->sample_aspect_ratio.num
      && ffmpegdec->ctx_par_d == context->sample_aspect_ratio.den)
    return FALSE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "Renegotiating video from %dx%d@ %d:%d PAR %d/%d fps to %dx%d@ %d:%d PAR %d/%d fps pixfmt %d",
      ffmpegdec->ctx_width, ffmpegdec->ctx_height,
      ffmpegdec->ctx_par_n, ffmpegdec->ctx_par_d,
      ffmpegdec->ctx_time_n, ffmpegdec->ctx_time_d,
      context->width, context->height,
      context->sample_aspect_ratio.num,
      context->sample_aspect_ratio.den,
      context->time_base.num, context->time_base.den, context->pix_fmt);

  ffmpegdec->ctx_width = context->width;
  ffmpegdec->ctx_height = context->height;
  ffmpegdec->ctx_ticks = context->ticks_per_frame;
  ffmpegdec->ctx_time_n = context->time_base.num;
  ffmpegdec->ctx_time_d = context->time_base.den;
  ffmpegdec->ctx_pix_fmt = context->pix_fmt;
  ffmpegdec->ctx_par_n = context->sample_aspect_ratio.num;
  ffmpegdec->ctx_par_d = context->sample_aspect_ratio.den;

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

  if (ffmpegdec->ctx_par_n && ffmpegdec->ctx_par_d) {
    decoder_num = ffmpegdec->ctx_par_n;
    decoder_denom = ffmpegdec->ctx_par_d;
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

static gboolean
gst_ffmpegviddec_negotiate (GstFFMpegVidDec * ffmpegdec,
    AVCodecContext * context, gboolean force)
{
  GstVideoFormat fmt;
  GstVideoInfo *in_info, *out_info;
  GstVideoCodecState *output_state;
  gint fps_n, fps_d;

  if (!update_video_context (ffmpegdec, context, force))
    return TRUE;

  fmt = gst_ffmpeg_pixfmt_to_videoformat (ffmpegdec->ctx_pix_fmt);
  if (G_UNLIKELY (fmt == GST_VIDEO_FORMAT_UNKNOWN))
    goto unknown_format;

  output_state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (ffmpegdec), fmt,
      ffmpegdec->ctx_width, ffmpegdec->ctx_height, ffmpegdec->input_state);
  if (ffmpegdec->output_state)
    gst_video_codec_state_unref (ffmpegdec->output_state);
  ffmpegdec->output_state = output_state;

  in_info = &ffmpegdec->input_state->info;
  out_info = &ffmpegdec->output_state->info;

  /* set the interlaced flag */
  if (ffmpegdec->ctx_interlaced)
    out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_MIXED;
  else
    out_info->interlace_mode = GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;

  /* try to find a good framerate */
  if (in_info->fps_d) {
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

  gst_video_decoder_negotiate (GST_VIDEO_DECODER (ffmpegdec));

  return TRUE;

  /* ERRORS */
unknown_format:
  {
    GST_ERROR_OBJECT (ffmpegdec,
        "decoder requires a video format unsupported by GStreamer");
    return FALSE;
  }
}

/* perform qos calculations before decoding the next frame.
 *
 * Sets the skip_frame flag and if things are really bad, skips to the next
 * keyframe.
 *
 * Returns TRUE if the frame should be decoded, FALSE if the frame can be dropped
 * entirely.
 */
static gboolean
gst_ffmpegviddec_do_qos (GstFFMpegVidDec * ffmpegdec,
    GstVideoCodecFrame * frame, gboolean * mode_switch)
{
  GstClockTimeDiff diff;

  *mode_switch = FALSE;

  if (frame == NULL)
    goto no_qos;

  diff =
      gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (ffmpegdec),
      frame);

  /* if we don't have timing info, then we don't do QoS */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (diff)))
    goto no_qos;

  GST_DEBUG_OBJECT (ffmpegdec, "decoding time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (diff));

  if (diff > 0)
    goto normal_mode;

  if (diff <= 0) {
    goto skip_frame;
  }

no_qos:
  return TRUE;

normal_mode:
  {
    if (ffmpegdec->context->skip_frame != AVDISCARD_DEFAULT) {
      ffmpegdec->context->skip_frame = AVDISCARD_DEFAULT;
      *mode_switch = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec, "QOS: normal mode");
    }
    return TRUE;
  }
skip_frame:
  {
    if (ffmpegdec->context->skip_frame != AVDISCARD_NONREF) {
      ffmpegdec->context->skip_frame = AVDISCARD_NONREF;
      *mode_switch = TRUE;
      GST_DEBUG_OBJECT (ffmpegdec,
          "QOS: hurry up, diff %" G_GINT64_FORMAT " >= 0", diff);
    }
    return FALSE;
  }
}

/* get an outbuf buffer with the current picture */
static GstFlowReturn
get_output_buffer (GstFFMpegVidDec * ffmpegdec, GstVideoCodecFrame * frame)
{
  GstFlowReturn ret = GST_FLOW_OK;
  AVPicture pic, *outpic;
  GstVideoFrame vframe;
  GstVideoInfo *info;
  gint c;

  GST_LOG_OBJECT (ffmpegdec, "get output buffer");

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
    goto alloc_failed;

  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_PLANES (info)) {
      pic.data[c] = GST_VIDEO_FRAME_PLANE_DATA (&vframe, c);
      pic.linesize[c] = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, c);
    } else {
      pic.data[c] = NULL;
      pic.linesize[c] = 0;
    }
    GST_LOG_OBJECT (ffmpegdec, "linesize %d, data %p", pic.linesize[c],
        pic.data[c]);
  }

  outpic = (AVPicture *) ffmpegdec->picture;

  av_picture_copy (&pic, outpic, ffmpegdec->context->pix_fmt,
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

  gst_video_frame_unmap (&vframe);

  ffmpegdec->picture->reordered_opaque = -1;

  return ret;

  /* special cases */
alloc_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "pad_alloc failed");
    return ret;
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
    guint8 * data, guint size, GstVideoCodecFrame * frame, GstFlowReturn * ret)
{
  gint len = -1;
  gint have_data;
  gboolean mode_switch;
  gboolean decode;
  gint skip_frame = AVDISCARD_DEFAULT;
  GstVideoCodecFrame *out_frame;
  GstFFMpegVidDecVideoFrame *out_dframe;
  AVPacket packet;

  *ret = GST_FLOW_OK;

  /* in case we skip frames */
  ffmpegdec->picture->pict_type = -1;

  /* run QoS code, we don't stop decoding the frame when we are late because
   * else we might skip a reference frame */
  decode = gst_ffmpegviddec_do_qos (ffmpegdec, frame, &mode_switch);

  if (ffmpegdec->is_realvideo && data != NULL) {
    gint slice_count;
    gint i;

    /* setup the slice table for realvideo */
    if (ffmpegdec->context->slice_offset == NULL)
      ffmpegdec->context->slice_offset = g_malloc (sizeof (guint32) * 1000);

    slice_count = (*data++) + 1;
    ffmpegdec->context->slice_count = slice_count;

    for (i = 0; i < slice_count; i++) {
      data += 4;
      ffmpegdec->context->slice_offset[i] = GST_READ_UINT32_LE (data);
      data += 4;
    }
  }

  if (!decode) {
    /* no decoding needed, save previous skip_frame value and brutely skip
     * decoding everything */
    skip_frame = ffmpegdec->context->skip_frame;
    ffmpegdec->context->skip_frame = AVDISCARD_NONREF;
  }

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

  len = avcodec_decode_video2 (ffmpegdec->context,
      ffmpegdec->picture, &have_data, &packet);

  /* restore previous state */
  if (!decode)
    ffmpegdec->context->skip_frame = skip_frame;

  GST_DEBUG_OBJECT (ffmpegdec, "after decode: len %d, have_data %d",
      len, have_data);

  /* when we are in skip_frame mode, don't complain when ffmpeg returned
   * no data because we told it to skip stuff. */
  if (len < 0 && (mode_switch || ffmpegdec->context->skip_frame))
    len = 0;

  /* no data, we're done */
  if (len < 0 || have_data <= 0)
    goto beach;

  /* get the output picture timing info again */
  out_dframe = ffmpegdec->picture->opaque;
  out_frame = gst_video_codec_frame_ref (out_dframe->frame);

  GST_DEBUG_OBJECT (ffmpegdec,
      "pts %" G_GUINT64_FORMAT " duration %" G_GUINT64_FORMAT,
      out_frame->pts, out_frame->duration);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: pts %" G_GUINT64_FORMAT,
      (guint64) ffmpegdec->picture->pts);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: num %d",
      ffmpegdec->picture->coded_picture_number);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: ref %d",
      ffmpegdec->picture->reference);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: display %d",
      ffmpegdec->picture->display_picture_number);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: opaque %p",
      ffmpegdec->picture->opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "picture: reordered opaque %" G_GUINT64_FORMAT,
      (guint64) ffmpegdec->picture->reordered_opaque);
  GST_DEBUG_OBJECT (ffmpegdec, "repeat_pict:%d",
      ffmpegdec->picture->repeat_pict);
  GST_DEBUG_OBJECT (ffmpegdec, "interlaced_frame:%d (current:%d)",
      ffmpegdec->picture->interlaced_frame, ffmpegdec->ctx_interlaced);

  if (G_UNLIKELY (ffmpegdec->picture->interlaced_frame !=
          ffmpegdec->ctx_interlaced)) {
    GST_WARNING ("Change in interlacing ! picture:%d, recorded:%d",
        ffmpegdec->picture->interlaced_frame, ffmpegdec->ctx_interlaced);
    ffmpegdec->ctx_interlaced = ffmpegdec->picture->interlaced_frame;
    if (!gst_ffmpegviddec_negotiate (ffmpegdec, ffmpegdec->context, TRUE))
      goto negotiation_error;
  }

  if (G_UNLIKELY (out_frame->output_buffer == NULL))
    *ret = get_output_buffer (ffmpegdec, out_frame);

  if (G_UNLIKELY (*ret != GST_FLOW_OK))
    goto no_output;

  if (ffmpegdec->ctx_interlaced) {
    /* set interlaced flags */
    if (ffmpegdec->picture->repeat_pict)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_VIDEO_BUFFER_FLAG_RFF);
    if (ffmpegdec->picture->top_field_first)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
    if (ffmpegdec->picture->interlaced_frame)
      GST_BUFFER_FLAG_SET (out_frame->output_buffer,
          GST_VIDEO_BUFFER_FLAG_INTERLACED);
  }

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
    guint8 * data, guint size, gint * got_data, GstVideoCodecFrame * frame,
    GstFlowReturn * ret)
{
  GstFFMpegVidDecClass *oclass;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (ffmpegdec->context->codec == NULL))
    goto no_codec;

  GST_LOG_OBJECT (ffmpegdec, "data:%p, size:%d", data, size);

  *ret = GST_FLOW_OK;
  ffmpegdec->context->frame_number++;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  len = gst_ffmpegviddec_video_frame (ffmpegdec, data, size, frame, ret);

  if (frame && frame->output_buffer)
    have_data = 1;

  if (len < 0 || have_data < 0) {
    GST_WARNING_OBJECT (ffmpegdec,
        "avdec_%s: decoding error (len: %d, have_data: %d)",
        oclass->in_plugin->name, len, have_data);
    *got_data = 0;
    goto beach;
  }
  if (len == 0 && have_data == 0) {
    *got_data = 0;
    goto beach;
  }

  /* this is where I lost my last clue on ffmpeg... */
  *got_data = 1;

beach:
  return len;

  /* ERRORS */
no_codec:
  {
    GST_ERROR_OBJECT (ffmpegdec, "no codec context");
    *ret = GST_FLOW_NOT_NEGOTIATED;
    return -1;
  }
}

static void
gst_ffmpegviddec_drain (GstFFMpegVidDec * ffmpegdec)
{
  GstFFMpegVidDecClass *oclass;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
    gint have_data, len, try = 0;

    GST_LOG_OBJECT (ffmpegdec,
        "codec has delay capabilities, calling until ffmpeg has drained everything");

    do {
      GstFlowReturn ret;

      len = gst_ffmpegviddec_frame (ffmpegdec, NULL, 0, &have_data, NULL, &ret);
      if (len < 0 || have_data == 0)
        break;
    } while (try++ < 10);
  }
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
    GST_ERROR_OBJECT (ffmpegdec, "Failed to map buffer");
    return GST_FLOW_ERROR;
  }

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
    GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, ffmpegdec,
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
      GST_CAT_TRACE_OBJECT (GST_CAT_PERFORMANCE, ffmpegdec,
          "Add temporary input padding");
      memcpy (tmp_padding, data + size, FF_INPUT_BUFFER_PADDING_SIZE);
      memset (data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    /* decode a frame of audio/video now */
    len =
        gst_ffmpegviddec_frame (ffmpegdec, data, size, &have_data, frame, &ret);

    if (do_padding) {
      memcpy (data + size, tmp_padding, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (ffmpegdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      /* bad flow retun, make sure we discard all data and exit */
      bsize = 0;
      break;
    }

    if (len == 0 && !have_data) {
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

  return TRUE;
}

static GstFlowReturn
gst_ffmpegviddec_finish (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;

  gst_ffmpegviddec_drain (ffmpegdec);

  return GST_FLOW_OK;
}

static gboolean
gst_ffmpegviddec_flush (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;

  if (ffmpegdec->opened)
    avcodec_flush_buffers (ffmpegdec->context);

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
  gboolean have_videometa, have_alignment;
  GstAllocator *allocator = NULL;
  GstAllocationParams params = { 0, 15, 0, 0, };

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
    return FALSE;

  state = gst_video_decoder_get_output_state (decoder);

  if (gst_query_get_n_allocation_params (query) > 0) {
    gst_query_parse_nth_allocation_param (query, 0, &allocator, &params);
    params.align = MAX (params.align, 15);
  } else {
    gst_query_add_allocation_param (query, allocator, &params);
  }

  gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, state->caps, size, min, max);
  /* we are happy with the default allocator but we would like to have 16 bytes
   * aligned and padded memory */
  gst_buffer_pool_config_set_allocator (config, allocator, &params);

  have_videometa =
      gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  if (have_videometa)
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

  have_alignment =
      gst_buffer_pool_has_option (pool, GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);

  /* we can only enable the alignment if downstream supports the
   * videometa api */
  if (have_alignment && have_videometa) {
    GstVideoAlignment align;
    gint width, height;
    gint linesize_align[4];
    gint i;
    guint edge;

    width = GST_VIDEO_INFO_WIDTH (&state->info);
    height = GST_VIDEO_INFO_HEIGHT (&state->info);
    /* let ffmpeg find the alignment and padding */
    avcodec_align_dimensions2 (ffmpegdec->context, &width, &height,
        linesize_align);
    edge =
        ffmpegdec->context->
        flags & CODEC_FLAG_EMU_EDGE ? 0 : avcodec_get_edge_width ();
    /* increase the size for the padding */
    width += edge << 1;
    height += edge << 1;

    align.padding_top = edge;
    align.padding_left = edge;
    align.padding_right = width - GST_VIDEO_INFO_WIDTH (&state->info) - edge;
    align.padding_bottom = height - GST_VIDEO_INFO_HEIGHT (&state->info) - edge;

    /* add extra padding to match libav buffer allocation sizes */
    align.padding_bottom++;

    for (i = 0; i < GST_VIDEO_MAX_PLANES; i++)
      align.stride_align[i] =
          (linesize_align[i] > 0 ? linesize_align[i] - 1 : 0);

    GST_DEBUG_OBJECT (ffmpegdec, "aligned dimension %dx%d -> %dx%d "
        "padding t:%u l:%u r:%u b:%u, stride_align %d:%d:%d:%d",
        GST_VIDEO_INFO_WIDTH (&state->info),
        GST_VIDEO_INFO_HEIGHT (&state->info), width, height, align.padding_top,
        align.padding_left, align.padding_right, align.padding_bottom,
        align.stride_align[0], align.stride_align[1], align.stride_align[2],
        align.stride_align[3]);

    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_ALIGNMENT);
    gst_buffer_pool_config_set_video_alignment (config, &align);

    if (ffmpegdec->direct_rendering) {
      GstFFMpegVidDecClass *oclass;

      GST_DEBUG_OBJECT (ffmpegdec, "trying to enable direct rendering");

      oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

      if (oclass->in_plugin->capabilities & CODEC_CAP_DR1) {
        GST_DEBUG_OBJECT (ffmpegdec, "enabled direct rendering");
        ffmpegdec->current_dr = TRUE;
      } else {
        GST_DEBUG_OBJECT (ffmpegdec, "direct rendering not supported");
      }
    }
  } else {
    GST_DEBUG_OBJECT (ffmpegdec,
        "alignment or videometa not supported, disable direct rendering");

    /* disable direct rendering. This will make us use the fallback ffmpeg
     * picture allocation code with padding etc. We will then do the final
     * copy (with cropping) into a buffer from our pool */
    ffmpegdec->current_dr = FALSE;
  }

  /* and store */
  gst_buffer_pool_set_config (pool, config);

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
  params.align = 15;
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

    /* no quasi-codecs, please */
    if (in_plugin->id == AV_CODEC_ID_RAWVIDEO ||
        in_plugin->id == AV_CODEC_ID_V210 ||
        in_plugin->id == AV_CODEC_ID_V210X ||
        in_plugin->id == AV_CODEC_ID_R210 ||
        (in_plugin->id >= AV_CODEC_ID_PCM_S16LE &&
            in_plugin->id <= AV_CODEC_ID_PCM_BLURAY)) {
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
    plugin_name = g_strdup ((gchar *) in_plugin->name);
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
      case AV_CODEC_ID_MPEG4:
      case AV_CODEC_ID_MSMPEG4V3:
      case AV_CODEC_ID_H264:
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
