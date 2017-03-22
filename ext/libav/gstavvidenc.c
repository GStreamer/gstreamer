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
/* for stats file handling */
#include <stdio.h>
#include <glib/gstdio.h>
#include <errno.h>

#include <libavcodec/avcodec.h>
#include <libavutil/stereo3d.h>

#include <gst/gst.h>
#include <gst/video/gstvideometa.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavvidenc.h"
#include "gstavcfg.h"

#define DEFAULT_VIDEO_BITRATE 300000    /* in bps */
#define DEFAULT_VIDEO_GOP_SIZE 15

#define DEFAULT_WIDTH 352
#define DEFAULT_HEIGHT 288


#define VIDEO_BUFFER_SIZE (1024*1024)

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_BIT_RATE,
  PROP_GOP_SIZE,
  PROP_ME_METHOD,
  PROP_BUFSIZE,
  PROP_RTP_PAYLOAD_SIZE,
  PROP_MAX_THREADS,
  PROP_COMPLIANCE,
  PROP_CFG_BASE,
};

#define GST_TYPE_ME_METHOD (gst_ffmpegvidenc_me_method_get_type())
static GType
gst_ffmpegvidenc_me_method_get_type (void)
{
  static GType ffmpegenc_me_method_type = 0;
  static GEnumValue ffmpegenc_me_methods[] = {
    {ME_ZERO, "None (Very low quality)", "zero"},
    {ME_FULL, "Full (Slow, unmaintained)", "full"},
    {ME_LOG, "Logarithmic (Low quality, unmaintained)", "logarithmic"},
    {ME_PHODS, "phods (Low quality, unmaintained)", "phods"},
    {ME_EPZS, "EPZS (Best quality, Fast)", "epzs"},
    {ME_X1, "X1 (Experimental)", "x1"},
    {0, NULL, NULL},
  };
  if (!ffmpegenc_me_method_type) {
    ffmpegenc_me_method_type =
        g_enum_register_static ("GstLibAVVidEncMeMethod", ffmpegenc_me_methods);
  }
  return ffmpegenc_me_method_type;
}

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegvidenc_class_init (GstFFMpegVidEncClass * klass);
static void gst_ffmpegvidenc_base_init (GstFFMpegVidEncClass * klass);
static void gst_ffmpegvidenc_init (GstFFMpegVidEnc * ffmpegenc);
static void gst_ffmpegvidenc_finalize (GObject * object);

static gboolean gst_ffmpegvidenc_start (GstVideoEncoder * encoder);
static gboolean gst_ffmpegvidenc_stop (GstVideoEncoder * encoder);
static GstFlowReturn gst_ffmpegvidenc_finish (GstVideoEncoder * encoder);
static gboolean gst_ffmpegvidenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state);
static gboolean gst_ffmpegvidenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query);
static gboolean gst_ffmpegvidenc_flush (GstVideoEncoder * encoder);

static GstFlowReturn gst_ffmpegvidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame);

static void gst_ffmpegvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

#define GST_FFENC_PARAMS_QDATA g_quark_from_static_string("avenc-params")

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegvidenc_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegvidenc_base_init (GstFFMpegVidEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  AVCodec *in_plugin;
  GstPadTemplate *srctempl = NULL, *sinktempl = NULL;
  GstCaps *srccaps = NULL, *sinkcaps = NULL;
  gchar *longname, *description;
  const gchar *classification;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFENC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("libav %s encoder", in_plugin->long_name);
  description = g_strdup_printf ("libav %s encoder", in_plugin->name);
  classification =
      gst_ffmpeg_codecid_is_image (in_plugin->id) ? "Codec/Encoder/Image" :
      "Codec/Encoder/Video";
  gst_element_class_set_metadata (element_class, longname,
      classification, description,
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  g_free (longname);
  g_free (description);

  if (!(srccaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", in_plugin->name);
    srccaps = gst_caps_new_empty_simple ("unknown/unknown");
  }

  sinkcaps = gst_ffmpeg_codectype_to_video_caps (NULL,
      in_plugin->id, TRUE, in_plugin);
  if (!sinkcaps) {
    GST_DEBUG ("Couldn't get sink caps for encoder '%s'", in_plugin->name);
    sinkcaps = gst_caps_new_empty_simple ("unknown/unknown");
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
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;

  return;
}

static void
gst_ffmpegvidenc_class_init (GstFFMpegVidEncClass * klass)
{
  GObjectClass *gobject_class;
  GstVideoEncoderClass *venc_class;
  int caps;

  gobject_class = (GObjectClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ffmpegvidenc_set_property;
  gobject_class->get_property = gst_ffmpegvidenc_get_property;

  /* FIXME: could use -1 for a sensible per-codec default based on
   * e.g. input resolution and framerate */
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BIT_RATE,
      g_param_spec_int ("bitrate", "Bit Rate",
          "Target Video Bitrate", 0, G_MAXINT, DEFAULT_VIDEO_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_GOP_SIZE,
      g_param_spec_int ("gop-size", "GOP Size",
          "Number of frames within one GOP", 0, G_MAXINT,
          DEFAULT_VIDEO_GOP_SIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_ME_METHOD,
      g_param_spec_enum ("me-method", "ME Method", "Motion Estimation Method",
          GST_TYPE_ME_METHOD, ME_EPZS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BUFSIZE,
      g_param_spec_int ("buffer-size", "Buffer Size",
          "Size of the video buffers", 0, G_MAXINT, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (G_OBJECT_CLASS (klass),
      PROP_RTP_PAYLOAD_SIZE, g_param_spec_int ("rtp-payload-size",
          "RTP Payload Size", "Target GOB length", 0, G_MAXINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  caps = klass->in_plugin->capabilities;
  if (caps & (CODEC_CAP_FRAME_THREADS | CODEC_CAP_SLICE_THREADS)) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MAX_THREADS,
        g_param_spec_int ("max-threads", "Maximum encode threads",
            "Maximum number of worker threads to spawn. (0 = auto)",
            0, G_MAXINT, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_COMPLIANCE,
      g_param_spec_enum ("compliance", "Compliance",
          "Adherence of the encoder to the specifications",
          GST_TYPE_FFMPEG_COMPLIANCE, FFMPEG_DEFAULT_COMPLIANCE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* register additional properties, possibly dependent on the exact CODEC */
  gst_ffmpeg_cfg_install_property (klass, PROP_CFG_BASE);

  venc_class->start = gst_ffmpegvidenc_start;
  venc_class->stop = gst_ffmpegvidenc_stop;
  venc_class->finish = gst_ffmpegvidenc_finish;
  venc_class->handle_frame = gst_ffmpegvidenc_handle_frame;
  venc_class->set_format = gst_ffmpegvidenc_set_format;
  venc_class->propose_allocation = gst_ffmpegvidenc_propose_allocation;
  venc_class->flush = gst_ffmpegvidenc_flush;

  gobject_class->finalize = gst_ffmpegvidenc_finalize;
}

static void
gst_ffmpegvidenc_init (GstFFMpegVidEnc * ffmpegenc)
{
  GstFFMpegVidEncClass *klass =
      (GstFFMpegVidEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (ffmpegenc));

  /* ffmpeg objects */
  ffmpegenc->context = avcodec_alloc_context3 (klass->in_plugin);
  ffmpegenc->picture = av_frame_alloc ();
  ffmpegenc->opened = FALSE;

  ffmpegenc->file = NULL;

  ffmpegenc->bitrate = DEFAULT_VIDEO_BITRATE;
  ffmpegenc->me_method = ME_EPZS;
  ffmpegenc->buffer_size = 512 * 1024;
  ffmpegenc->gop_size = DEFAULT_VIDEO_GOP_SIZE;
  ffmpegenc->rtp_payload_size = 0;
  ffmpegenc->compliance = FFMPEG_DEFAULT_COMPLIANCE;
  ffmpegenc->max_threads = 0;

  ffmpegenc->lmin = 2;
  ffmpegenc->lmax = 31;
  ffmpegenc->max_key_interval = 0;

  gst_ffmpeg_cfg_set_defaults (ffmpegenc);
}

static void
gst_ffmpegvidenc_finalize (GObject * object)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) object;

  gst_ffmpeg_cfg_finalize (ffmpegenc);

  /* clean up remaining allocated data */
  av_frame_free (&ffmpegenc->picture);
  gst_ffmpeg_avcodec_close (ffmpegenc->context);
  av_free (ffmpegenc->context);

  g_free (ffmpegenc->filename);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
gst_ffmpegvidenc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  GstVideoCodecState *output_format;
  enum AVPixelFormat pix_fmt;
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;
  GstFFMpegVidEncClass *oclass =
      (GstFFMpegVidEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  /* close old session */
  if (ffmpegenc->opened) {
    gst_ffmpeg_avcodec_close (ffmpegenc->context);
    ffmpegenc->opened = FALSE;
    if (avcodec_get_context_defaults3 (ffmpegenc->context,
            oclass->in_plugin) < 0) {
      GST_DEBUG_OBJECT (ffmpegenc, "Failed to set context defaults");
      return FALSE;
    }
  }

  /* if we set it in _getcaps we should set it also in _link */
  ffmpegenc->context->strict_std_compliance = ffmpegenc->compliance;

  /* user defined properties */
  ffmpegenc->context->bit_rate = ffmpegenc->bitrate;
  ffmpegenc->context->bit_rate_tolerance = ffmpegenc->bitrate;
  ffmpegenc->context->gop_size = ffmpegenc->gop_size;
  ffmpegenc->context->me_method = ffmpegenc->me_method;
  GST_DEBUG_OBJECT (ffmpegenc, "Setting avcontext to bitrate %d, gop_size %d",
      ffmpegenc->bitrate, ffmpegenc->gop_size);

  if (ffmpegenc->max_threads == 0) {
    if (!(oclass->in_plugin->capabilities & CODEC_CAP_AUTO_THREADS))
      ffmpegenc->context->thread_count = gst_ffmpeg_auto_max_threads ();
    else
      ffmpegenc->context->thread_count = 0;
  } else
    ffmpegenc->context->thread_count = ffmpegenc->max_threads;

  /* RTP payload used for GOB production (for Asterisk) */
  if (ffmpegenc->rtp_payload_size) {
    ffmpegenc->context->rtp_payload_size = ffmpegenc->rtp_payload_size;
  }

  /* additional avcodec settings */
  /* first fill in the majority by copying over */
  gst_ffmpeg_cfg_fill_context (ffmpegenc, ffmpegenc->context);

  /* then handle some special cases */
  ffmpegenc->context->lmin = (ffmpegenc->lmin * FF_QP2LAMBDA + 0.5);
  ffmpegenc->context->lmax = (ffmpegenc->lmax * FF_QP2LAMBDA + 0.5);

  if (ffmpegenc->interlaced) {
    ffmpegenc->context->flags |=
        CODEC_FLAG_INTERLACED_DCT | CODEC_FLAG_INTERLACED_ME;
  }

  /* some other defaults */
  ffmpegenc->context->rc_strategy = 2;
  ffmpegenc->context->b_frame_strategy = 0;
  ffmpegenc->context->coder_type = 0;
  ffmpegenc->context->context_model = 0;
  ffmpegenc->context->scenechange_threshold = 0;

  /* and last but not least the pass; CBR, 2-pass, etc */
  ffmpegenc->context->flags |= ffmpegenc->pass;
  switch (ffmpegenc->pass) {
      /* some additional action depends on type of pass */
    case CODEC_FLAG_QSCALE:
      ffmpegenc->context->global_quality
          = ffmpegenc->picture->quality = FF_QP2LAMBDA * ffmpegenc->quantizer;
      break;
    case CODEC_FLAG_PASS1:     /* need to prepare a stats file */
      /* we don't close when changing caps, fingers crossed */
      if (!ffmpegenc->file)
        ffmpegenc->file = g_fopen (ffmpegenc->filename, "w");
      if (!ffmpegenc->file)
        goto open_file_err;
      break;
    case CODEC_FLAG_PASS2:
    {                           /* need to read the whole stats file ! */
      gsize size;

      if (!g_file_get_contents (ffmpegenc->filename,
              &ffmpegenc->context->stats_in, &size, NULL))
        goto file_read_err;

      break;
    }
    default:
      break;
  }

  GST_DEBUG_OBJECT (ffmpegenc, "Extracting common video information");
  /* fetch pix_fmt, fps, par, width, height... */
  gst_ffmpeg_videoinfo_to_context (&state->info, ffmpegenc->context);

  /* sanitize time base */
  if (ffmpegenc->context->time_base.num <= 0
      || ffmpegenc->context->time_base.den <= 0)
    goto insane_timebase;

  if ((oclass->in_plugin->id == AV_CODEC_ID_MPEG4)
      && (ffmpegenc->context->time_base.den > 65535)) {
    /* MPEG4 Standards do not support time_base denominator greater than
     * (1<<16) - 1 . We therefore scale them down.
     * Agreed, it will not be the exact framerate... but the difference
     * shouldn't be that noticeable */
    ffmpegenc->context->time_base.num =
        (gint) gst_util_uint64_scale_int (ffmpegenc->context->time_base.num,
        65535, ffmpegenc->context->time_base.den);
    ffmpegenc->context->time_base.den = 65535;
    GST_LOG_OBJECT (ffmpegenc, "MPEG4 : scaled down framerate to %d / %d",
        ffmpegenc->context->time_base.den, ffmpegenc->context->time_base.num);
  }

  pix_fmt = ffmpegenc->context->pix_fmt;

  /* max-key-interval may need the framerate set above */
  if (ffmpegenc->max_key_interval) {
    AVCodecContext *ctx;

    /* override gop-size */
    ctx = ffmpegenc->context;
    ctx->gop_size = (ffmpegenc->max_key_interval < 0) ?
        (-ffmpegenc->max_key_interval
        * (ctx->time_base.den * ctx->ticks_per_frame / ctx->time_base.num))
        : ffmpegenc->max_key_interval;
  }

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (ffmpegenc, "picking an output format ...");
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ffmpegenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_pad_get_pad_template_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));
  }
  GST_DEBUG_OBJECT (ffmpegenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, allowed_caps, ffmpegenc->context);

  /* open codec */
  if (gst_ffmpeg_avcodec_open (ffmpegenc->context, oclass->in_plugin) < 0) {
    gst_caps_unref (allowed_caps);
    goto open_codec_fail;
  }

  /* is the colourspace correct? */
  if (pix_fmt != ffmpegenc->context->pix_fmt) {
    gst_caps_unref (allowed_caps);
    goto pix_fmt_err;
  }

  /* we may have failed mapping caps to a pixfmt,
   * and quite some codecs do not make up their own mind about that
   * in any case, _NONE can never work out later on */
  if (pix_fmt == AV_PIX_FMT_NONE) {
    gst_caps_unref (allowed_caps);
    goto bad_input_fmt;
  }

  /* second pass stats buffer no longer needed */
  g_free (ffmpegenc->context->stats_in);

  /* try to set this caps on the other side */
  other_caps = gst_ffmpeg_codecid_to_caps (oclass->in_plugin->id,
      ffmpegenc->context, TRUE);

  if (!other_caps) {
    gst_caps_unref (allowed_caps);
    goto unsupported_codec;
  }

  icaps = gst_caps_intersect (allowed_caps, other_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (other_caps);
  if (gst_caps_is_empty (icaps)) {
    gst_caps_unref (icaps);
    goto unsupported_codec;
  }
  icaps = gst_caps_fixate (icaps);

  GST_DEBUG_OBJECT (ffmpegenc, "codec flags 0x%08x", ffmpegenc->context->flags);

  /* Store input state and set output state */
  if (ffmpegenc->input_state)
    gst_video_codec_state_unref (ffmpegenc->input_state);
  ffmpegenc->input_state = gst_video_codec_state_ref (state);

  output_format = gst_video_encoder_set_output_state (encoder, icaps, state);
  gst_video_codec_state_unref (output_format);

  /* Store some tags */
  {
    GstTagList *tags = gst_tag_list_new_empty ();
    const gchar *codec;

    gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_NOMINAL_BITRATE,
        (guint) ffmpegenc->context->bit_rate, NULL);

    if ((codec =
            gst_ffmpeg_get_codecid_longname (ffmpegenc->context->codec_id)))
      gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE, GST_TAG_VIDEO_CODEC, codec,
          NULL);

    gst_video_encoder_merge_tags (encoder, tags, GST_TAG_MERGE_REPLACE);
    gst_tag_list_unref (tags);
  }

  /* success! */
  ffmpegenc->opened = TRUE;

  return TRUE;

  /* ERRORS */
open_file_err:
  {
    GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, OPEN_WRITE,
        (("Could not open file \"%s\" for writing."), ffmpegenc->filename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }
file_read_err:
  {
    GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, READ,
        (("Could not get contents of file \"%s\"."), ffmpegenc->filename),
        GST_ERROR_SYSTEM);
    return FALSE;
  }

insane_timebase:
  {
    GST_ERROR_OBJECT (ffmpegenc, "Rejecting time base %d/%d",
        ffmpegenc->context->time_base.den, ffmpegenc->context->time_base.num);
    goto cleanup_stats_in;
  }
unsupported_codec:
  {
    GST_DEBUG ("Unsupported codec - no caps found");
    goto cleanup_stats_in;
  }
open_codec_fail:
  {
    GST_DEBUG_OBJECT (ffmpegenc, "avenc_%s: Failed to open libav codec",
        oclass->in_plugin->name);
    goto close_codec;
  }

pix_fmt_err:
  {
    GST_DEBUG_OBJECT (ffmpegenc,
        "avenc_%s: AV wants different colourspace (%d given, %d wanted)",
        oclass->in_plugin->name, pix_fmt, ffmpegenc->context->pix_fmt);
    goto close_codec;
  }

bad_input_fmt:
  {
    GST_DEBUG_OBJECT (ffmpegenc, "avenc_%s: Failed to determine input format",
        oclass->in_plugin->name);
    goto close_codec;
  }
close_codec:
  {
    gst_ffmpeg_avcodec_close (ffmpegenc->context);
    if (avcodec_get_context_defaults3 (ffmpegenc->context,
            oclass->in_plugin) < 0)
      GST_DEBUG_OBJECT (ffmpegenc, "Failed to set context defaults");
    goto cleanup_stats_in;
  }
cleanup_stats_in:
  {
    g_free (ffmpegenc->context->stats_in);
    return FALSE;
  }
}


static gboolean
gst_ffmpegvidenc_propose_allocation (GstVideoEncoder * encoder,
    GstQuery * query)
{
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);

  return GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
      query);
}

static void
gst_ffmpegvidenc_free_avpacket (gpointer pkt)
{
  av_packet_unref ((AVPacket *) pkt);
  g_slice_free (AVPacket, pkt);
}

typedef struct
{
  GstBuffer *buffer;
  GstVideoFrame vframe;
} BufferInfo;

static void
buffer_info_free (void *opaque, guint8 * data)
{
  BufferInfo *info = opaque;

  gst_video_frame_unmap (&info->vframe);
  gst_buffer_unref (info->buffer);
  g_slice_free (BufferInfo, info);
}

static enum AVStereo3DType
stereo_gst_to_av (GstVideoMultiviewMode mview_mode)
{
  switch (mview_mode) {
    case GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE:
      return AV_STEREO3D_SIDEBYSIDE;
    case GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM:
      return AV_STEREO3D_TOPBOTTOM;
    case GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME:
      return AV_STEREO3D_FRAMESEQUENCE;
    case GST_VIDEO_MULTIVIEW_MODE_CHECKERBOARD:
      return AV_STEREO3D_CHECKERBOARD;
    case GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE_QUINCUNX:
      return AV_STEREO3D_SIDEBYSIDE_QUINCUNX;
    case GST_VIDEO_MULTIVIEW_MODE_ROW_INTERLEAVED:
      return AV_STEREO3D_LINES;
    case GST_VIDEO_MULTIVIEW_MODE_COLUMN_INTERLEAVED:
      return AV_STEREO3D_COLUMNS;
    default:
      break;
  }
  GST_WARNING ("Unsupported multiview mode - no mapping in libav");
  return AV_STEREO3D_2D;
}

static GstFlowReturn
gst_ffmpegvidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;
  GstBuffer *outbuf;
  gint ret = 0, c;
  GstVideoInfo *info = &ffmpegenc->input_state->info;
  AVPacket *pkt;
  int have_data = 0;
  BufferInfo *buffer_info;

  if (ffmpegenc->interlaced) {
    ffmpegenc->picture->interlaced_frame = TRUE;
    /* if this is not the case, a filter element should be used to swap fields */
    ffmpegenc->picture->top_field_first =
        GST_BUFFER_FLAG_IS_SET (frame->input_buffer, GST_VIDEO_BUFFER_FLAG_TFF);
  }

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (info) != GST_VIDEO_MULTIVIEW_MODE_NONE) {
    AVStereo3D *stereo = av_stereo3d_create_side_data (ffmpegenc->picture);
    stereo->type = stereo_gst_to_av (GST_VIDEO_INFO_MULTIVIEW_MODE (info));

    if (GST_VIDEO_INFO_MULTIVIEW_FLAGS (info) &
        GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST) {
      stereo->flags = AV_STEREO3D_FLAG_INVERT;
    }
  }

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame))
    ffmpegenc->picture->pict_type = AV_PICTURE_TYPE_I;

  buffer_info = g_slice_new0 (BufferInfo);
  buffer_info->buffer = gst_buffer_ref (frame->input_buffer);

  if (!gst_video_frame_map (&buffer_info->vframe, info, frame->input_buffer,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (encoder, "Failed to map input buffer");
    gst_buffer_unref (buffer_info->buffer);
    g_slice_free (BufferInfo, buffer_info);
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

  /* Fill avpicture */
  ffmpegenc->picture->buf[0] =
      av_buffer_create (NULL, 0, buffer_info_free, buffer_info, 0);
  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_COMPONENTS (info)) {
      ffmpegenc->picture->data[c] =
          GST_VIDEO_FRAME_PLANE_DATA (&buffer_info->vframe, c);
      ffmpegenc->picture->linesize[c] =
          GST_VIDEO_FRAME_COMP_STRIDE (&buffer_info->vframe, c);
    } else {
      ffmpegenc->picture->data[c] = NULL;
      ffmpegenc->picture->linesize[c] = 0;
    }
  }

  ffmpegenc->picture->format = ffmpegenc->context->pix_fmt;
  ffmpegenc->picture->width = GST_VIDEO_FRAME_WIDTH (&buffer_info->vframe);
  ffmpegenc->picture->height = GST_VIDEO_FRAME_HEIGHT (&buffer_info->vframe);

  ffmpegenc->picture->pts =
      gst_ffmpeg_time_gst_to_ff (frame->pts /
      ffmpegenc->context->ticks_per_frame, ffmpegenc->context->time_base);

  have_data = 0;
  pkt = g_slice_new0 (AVPacket);

  ret =
      avcodec_encode_video2 (ffmpegenc->context, pkt, ffmpegenc->picture,
      &have_data);

  av_frame_unref (ffmpegenc->picture);

  if (ret < 0 || !have_data)
    g_slice_free (AVPacket, pkt);

  if (ret < 0)
    goto encode_fail;

  /* Encoder needs more data */
  if (!have_data) {
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_OK;
  }

  /* save stats info if there is some as well as a stats file */
  if (ffmpegenc->file && ffmpegenc->context->stats_out)
    if (fprintf (ffmpegenc->file, "%s", ffmpegenc->context->stats_out) < 0)
      GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, WRITE,
          (("Could not write to file \"%s\"."), ffmpegenc->filename),
          GST_ERROR_SYSTEM);

  gst_video_codec_frame_unref (frame);

  /* Get oldest frame */
  frame = gst_video_encoder_get_oldest_frame (encoder);

  outbuf =
      gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, pkt->data,
      pkt->size, 0, pkt->size, pkt, gst_ffmpegvidenc_free_avpacket);
  frame->output_buffer = outbuf;

  if (pkt->flags & AV_PKT_FLAG_KEY)
    GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
  else
    GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);

  return gst_video_encoder_finish_frame (encoder, frame);

  /* ERRORS */
encode_fail:
  {
#ifndef GST_DISABLE_GST_DEBUG
    GstFFMpegVidEncClass *oclass =
        (GstFFMpegVidEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));
    GST_ERROR_OBJECT (ffmpegenc,
        "avenc_%s: failed to encode buffer", oclass->in_plugin->name);
#endif /* GST_DISABLE_GST_DEBUG */
    /* avoid frame (and ts etc) piling up */
    return gst_video_encoder_finish_frame (encoder, frame);
  }
}

static GstFlowReturn
gst_ffmpegvidenc_flush_buffers (GstFFMpegVidEnc * ffmpegenc, gboolean send)
{
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstBuffer *outbuf;
  gint ret;
  AVPacket *pkt;
  int have_data = 0;

  GST_DEBUG_OBJECT (ffmpegenc, "flushing buffers with sending %d", send);

  /* no need to empty codec if there is none */
  if (!ffmpegenc->opened)
    goto done;

  while ((frame =
          gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (ffmpegenc)))) {
    pkt = g_slice_new0 (AVPacket);
    have_data = 0;

    ret = avcodec_encode_video2 (ffmpegenc->context, pkt, NULL, &have_data);

    if (ret < 0) {              /* there should be something, notify and give up */
#ifndef GST_DISABLE_GST_DEBUG
      GstFFMpegVidEncClass *oclass =
          (GstFFMpegVidEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));
      GST_WARNING_OBJECT (ffmpegenc,
          "avenc_%s: failed to flush buffer", oclass->in_plugin->name);
#endif /* GST_DISABLE_GST_DEBUG */
      g_slice_free (AVPacket, pkt);
      gst_video_codec_frame_unref (frame);
      break;
    }

    /* save stats info if there is some as well as a stats file */
    if (ffmpegenc->file && ffmpegenc->context->stats_out)
      if (fprintf (ffmpegenc->file, "%s", ffmpegenc->context->stats_out) < 0)
        GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, WRITE,
            (("Could not write to file \"%s\"."), ffmpegenc->filename),
            GST_ERROR_SYSTEM);

    if (send && have_data) {
      outbuf =
          gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, pkt->data,
          pkt->size, 0, pkt->size, pkt, gst_ffmpegvidenc_free_avpacket);
      frame->output_buffer = outbuf;

      if (pkt->flags & AV_PKT_FLAG_KEY)
        GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
      else
        GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);

      flow_ret =
          gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (ffmpegenc), frame);
    } else {
      /* no frame attached, so will be skipped and removed from frame list */
      gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (ffmpegenc), frame);
    }
  }

done:

  return flow_ret;
}


static void
gst_ffmpegvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidEnc *ffmpegenc;

  /* Get a pointer of the right type. */
  ffmpegenc = (GstFFMpegVidEnc *) (object);

  if (ffmpegenc->opened) {
    GST_WARNING_OBJECT (ffmpegenc,
        "Can't change properties once decoder is setup !");
    return;
  }

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case PROP_BIT_RATE:
      ffmpegenc->bitrate = g_value_get_int (value);
      break;
    case PROP_GOP_SIZE:
      ffmpegenc->gop_size = g_value_get_int (value);
      break;
    case PROP_ME_METHOD:
      ffmpegenc->me_method = g_value_get_enum (value);
      break;
    case PROP_BUFSIZE:
      break;
    case PROP_RTP_PAYLOAD_SIZE:
      ffmpegenc->rtp_payload_size = g_value_get_int (value);
      break;
    case PROP_COMPLIANCE:
      ffmpegenc->compliance = g_value_get_enum (value);
      break;
    case PROP_MAX_THREADS:
      ffmpegenc->max_threads = g_value_get_int (value);
      break;
    default:
      if (!gst_ffmpeg_cfg_set_property (object, value, pspec))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ffmpegvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidEnc *ffmpegenc;

  /* It's not null if we got it, but it might not be ours */
  ffmpegenc = (GstFFMpegVidEnc *) (object);

  switch (prop_id) {
    case PROP_BIT_RATE:
      g_value_set_int (value, ffmpegenc->bitrate);
      break;
    case PROP_GOP_SIZE:
      g_value_set_int (value, ffmpegenc->gop_size);
      break;
    case PROP_ME_METHOD:
      g_value_set_enum (value, ffmpegenc->me_method);
      break;
    case PROP_BUFSIZE:
      g_value_set_int (value, ffmpegenc->buffer_size);
      break;
    case PROP_RTP_PAYLOAD_SIZE:
      g_value_set_int (value, ffmpegenc->rtp_payload_size);
      break;
    case PROP_COMPLIANCE:
      g_value_set_enum (value, ffmpegenc->compliance);
      break;
    case PROP_MAX_THREADS:
      g_value_set_int (value, ffmpegenc->max_threads);
      break;
    default:
      if (!gst_ffmpeg_cfg_get_property (object, value, pspec))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_ffmpegvidenc_flush (GstVideoEncoder * encoder)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;

  if (ffmpegenc->opened)
    avcodec_flush_buffers (ffmpegenc->context);

  return TRUE;
}

static gboolean
gst_ffmpegvidenc_start (GstVideoEncoder * encoder)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;
  GstFFMpegVidEncClass *oclass =
      (GstFFMpegVidEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  /* close old session */
  gst_ffmpeg_avcodec_close (ffmpegenc->context);
  if (avcodec_get_context_defaults3 (ffmpegenc->context, oclass->in_plugin) < 0) {
    GST_DEBUG_OBJECT (ffmpegenc, "Failed to set context defaults");
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_ffmpegvidenc_stop (GstVideoEncoder * encoder)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;

  gst_ffmpegvidenc_flush_buffers (ffmpegenc, FALSE);
  gst_ffmpeg_avcodec_close (ffmpegenc->context);
  ffmpegenc->opened = FALSE;

  if (ffmpegenc->file) {
    fclose (ffmpegenc->file);
    ffmpegenc->file = NULL;
  }
  if (ffmpegenc->input_state) {
    gst_video_codec_state_unref (ffmpegenc->input_state);
    ffmpegenc->input_state = NULL;
  }

  return TRUE;
}

static GstFlowReturn
gst_ffmpegvidenc_finish (GstVideoEncoder * encoder)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;

  return gst_ffmpegvidenc_flush_buffers (ffmpegenc, TRUE);
}

gboolean
gst_ffmpegvidenc_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegVidEncClass),
    (GBaseInitFunc) gst_ffmpegvidenc_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegvidenc_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegVidEnc),
    0,
    (GInstanceInitFunc) gst_ffmpegvidenc_init,
  };
  GType type;
  AVCodec *in_plugin;


  GST_LOG ("Registering encoders");

  /* build global ffmpeg param/property info */
  gst_ffmpeg_cfg_init ();

  in_plugin = av_codec_next (NULL);
  while (in_plugin) {
    gchar *type_name;

    /* Skip non-AV codecs */
    if (in_plugin->type != AVMEDIA_TYPE_VIDEO)
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

    /* No encoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library encoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      goto next;
    }

    if (strstr (in_plugin->name, "vaapi")) {
      GST_DEBUG
          ("Ignoring VAAPI encoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    if (strstr (in_plugin->name, "nvenc")) {
      GST_DEBUG
          ("Ignoring nvenc encoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    if (g_str_has_suffix (in_plugin->name, "_qsv")) {
      GST_DEBUG
          ("Ignoring qsv encoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      goto next;
    }

    /* only video encoders */
    if (!av_codec_is_encoder (in_plugin)
        || in_plugin->type != AVMEDIA_TYPE_VIDEO)
      goto next;

    /* FIXME : We should have a method to know cheaply whether we have a mapping
     * for the given plugin or not */

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    if (!strcmp (in_plugin->name, "gif")) {
      GST_LOG ("Ignoring encoder %s", in_plugin->name);
      goto next;
    }

    /* construct the type */
    type_name = g_strdup_printf ("avenc_%s", in_plugin->name);

    type = g_type_from_name (type_name);

    if (!type) {

      /* create the glib type now */
      type =
          g_type_register_static (GST_TYPE_VIDEO_ENCODER, type_name, &typeinfo,
          0);
      g_type_set_qdata (type, GST_FFENC_PARAMS_QDATA, (gpointer) in_plugin);

      {
        static const GInterfaceInfo preset_info = {
          NULL,
          NULL,
          NULL
        };
        g_type_add_interface_static (type, GST_TYPE_PRESET, &preset_info);
      }
    }

    if (!gst_element_register (plugin, type_name, GST_RANK_SECONDARY, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    in_plugin = av_codec_next (in_plugin);
  }

  GST_LOG ("Finished registering encoders");

  return TRUE;
}
