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

#include <assert.h>
#include <string.h>
/* for stats file handling */
#include <stdio.h>
#include <glib/gstdio.h>
#include <errno.h>

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegenc.h"
#include "gstffmpegcfg.h"

#define DEFAULT_VIDEO_BITRATE 300000    /* in bps */
#define DEFAULT_VIDEO_GOP_SIZE 15
#define DEFAULT_AUDIO_BITRATE 128000

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
  ARG_0,
  ARG_BIT_RATE,
  ARG_GOP_SIZE,
  ARG_ME_METHOD,
  ARG_BUFSIZE,
  ARG_RTP_PAYLOAD_SIZE,
  ARG_CFG_BASE
};

#define GST_TYPE_ME_METHOD (gst_ffmpegenc_me_method_get_type())
static GType
gst_ffmpegenc_me_method_get_type (void)
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
        g_enum_register_static ("GstFFMpegEncMeMethod", ffmpegenc_me_methods);
  }
  return ffmpegenc_me_method_type;
}

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegenc_class_init (GstFFMpegEncClass * klass);
static void gst_ffmpegenc_base_init (GstFFMpegEncClass * klass);
static void gst_ffmpegenc_init (GstFFMpegEnc * ffmpegenc);
static void gst_ffmpegenc_finalize (GObject * object);

static gboolean gst_ffmpegenc_setcaps (GstPad * pad, GstCaps * caps);
static GstCaps *gst_ffmpegenc_getcaps (GstPad * pad);
static GstFlowReturn gst_ffmpegenc_chain_video (GstPad * pad,
    GstBuffer * buffer);
static GstFlowReturn gst_ffmpegenc_chain_audio (GstPad * pad,
    GstBuffer * buffer);
static gboolean gst_ffmpegenc_event_video (GstPad * pad, GstEvent * event);

static void gst_ffmpegenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstStateChangeReturn gst_ffmpegenc_change_state (GstElement * element,
    GstStateChange transition);

#define GST_FFENC_PARAMS_QDATA g_quark_from_static_string("ffenc-params")

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegenc_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegenc_base_init (GstFFMpegEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegEncClassParams *params;
  GstElementDetails details;
  GstPadTemplate *srctempl, *sinktempl;

  params =
      (GstFFMpegEncClassParams *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFENC_PARAMS_QDATA);
  g_assert (params != NULL);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s encoder",
      gst_ffmpeg_get_codecid_longname (params->in_plugin->id));
  details.klass = g_strdup_printf ("Codec/Encoder/%s",
      (params->in_plugin->type == CODEC_TYPE_VIDEO) ? "Video" : "Audio");
  details.description = g_strdup_printf ("FFMPEG %s encoder",
      params->in_plugin->name);
  details.author = "Wim Taymans <wim.taymans@chello.be>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  /* pad templates */
  sinktempl = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, params->sinkcaps);
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, params->srccaps);

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);

  klass->in_plugin = params->in_plugin;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;
  klass->sinkcaps = NULL;
}

static void
gst_ffmpegenc_class_init (GstFFMpegEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ffmpegenc_set_property;
  gobject_class->get_property = gst_ffmpegenc_get_property;

  if (klass->in_plugin->type == CODEC_TYPE_VIDEO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
        g_param_spec_ulong ("bitrate", "Bit Rate",
            "Target Video Bitrate", 0, G_MAXULONG, DEFAULT_VIDEO_BITRATE,
            G_PARAM_READWRITE));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GOP_SIZE,
        g_param_spec_int ("gop_size", "GOP Size",
            "Number of frames within one GOP", 0, G_MAXINT,
            DEFAULT_VIDEO_GOP_SIZE, G_PARAM_READWRITE));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ME_METHOD,
        g_param_spec_enum ("me_method", "ME Method", "Motion Estimation Method",
            GST_TYPE_ME_METHOD, ME_EPZS, G_PARAM_READWRITE));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFSIZE,
        g_param_spec_ulong ("buffer_size", "Buffer Size",
            "Size of the video buffers", 0, G_MAXULONG, 0, G_PARAM_READWRITE));
    g_object_class_install_property (G_OBJECT_CLASS (klass),
        ARG_RTP_PAYLOAD_SIZE, g_param_spec_ulong ("rtp_payload_size",
            "RTP Payload Size", "Target GOB length", 0, G_MAXULONG, 0,
            G_PARAM_READWRITE));

    /* register additional properties, possibly dependent on the exact CODEC */
    gst_ffmpeg_cfg_install_property (klass, ARG_CFG_BASE);
  } else if (klass->in_plugin->type == CODEC_TYPE_AUDIO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
        g_param_spec_ulong ("bitrate", "Bit Rate",
            "Target Audio Bitrate", 0, G_MAXULONG, DEFAULT_AUDIO_BITRATE,
            G_PARAM_READWRITE));
  }

  gstelement_class->change_state = gst_ffmpegenc_change_state;

  gobject_class->finalize = gst_ffmpegenc_finalize;
}

static void
gst_ffmpegenc_init (GstFFMpegEnc * ffmpegenc)
{
  GstFFMpegEncClass *oclass =
      (GstFFMpegEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));

  /* setup pads */
  ffmpegenc->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_setcaps_function (ffmpegenc->sinkpad, gst_ffmpegenc_setcaps);
  gst_pad_set_getcaps_function (ffmpegenc->sinkpad, gst_ffmpegenc_getcaps);
  ffmpegenc->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (ffmpegenc->srcpad);

  /* ffmpeg objects */
  ffmpegenc->context = avcodec_alloc_context ();
  ffmpegenc->picture = avcodec_alloc_frame ();
  ffmpegenc->opened = FALSE;

  ffmpegenc->file = NULL;
  ffmpegenc->delay = g_queue_new ();

  if (oclass->in_plugin->type == CODEC_TYPE_VIDEO) {
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_video);
    /* so we know when to flush the buffers on EOS */
    gst_pad_set_event_function (ffmpegenc->sinkpad, gst_ffmpegenc_event_video);

    ffmpegenc->bitrate = DEFAULT_VIDEO_BITRATE;
    ffmpegenc->me_method = ME_EPZS;
    ffmpegenc->buffer_size = 512 * 1024;
    ffmpegenc->gop_size = DEFAULT_VIDEO_GOP_SIZE;
    ffmpegenc->rtp_payload_size = 0;

    ffmpegenc->lmin = 2;
    ffmpegenc->lmax = 31;
    ffmpegenc->max_key_interval = 0;

    gst_ffmpeg_cfg_set_defaults (ffmpegenc);
  } else if (oclass->in_plugin->type == CODEC_TYPE_AUDIO) {
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_audio);

    ffmpegenc->bitrate = DEFAULT_AUDIO_BITRATE;
  }

  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->srcpad);
}

static void
gst_ffmpegenc_finalize (GObject * object)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) object;

  gst_ffmpeg_cfg_finalize (ffmpegenc);

  /* close old session */
  if (ffmpegenc->opened) {
    gst_ffmpeg_avcodec_close (ffmpegenc->context);
    ffmpegenc->opened = FALSE;
  }

  /* clean up remaining allocated data */
  av_free (ffmpegenc->context);
  av_free (ffmpegenc->picture);

  g_queue_free (ffmpegenc->delay);
  g_free (ffmpegenc->filename);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_ffmpegenc_getcaps (GstPad * pad)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) GST_PAD_PARENT (pad);
  GstFFMpegEncClass *oclass =
      (GstFFMpegEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);
  AVCodecContext *ctx;
  enum PixelFormat pixfmt;
  GstCaps *caps = NULL;

  /* audio needs no special care */
  if (oclass->in_plugin->type == CODEC_TYPE_AUDIO) {
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  /* cached */
  if (oclass->sinkcaps) {
    return gst_caps_copy (oclass->sinkcaps);
  }

  /* create cache etc. */
  ctx = avcodec_alloc_context ();
  if (!ctx) {
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  /* set some default properties */
  ctx->width = DEFAULT_WIDTH;
  ctx->height = DEFAULT_HEIGHT;
  ctx->time_base.num = DEFAULT_FRAME_RATE_BASE;
  ctx->time_base.den = 25 * DEFAULT_FRAME_RATE_BASE;
  ctx->bit_rate = DEFAULT_VIDEO_BITRATE;
  /* makes it silent */
  ctx->strict_std_compliance = -1;

  /* shut up the logging while we autoprobe; we don't want warnings and
   * errors about unsupported formats */
  /* FIXME: if someone cares about this disabling the logging for other
   * instances/threads/..., one could investigate if there is a way to
   * set this as a struct member on the av context, and check it from the
   * log handler */
#ifndef GST_DISABLE_GST_DEBUG
  _shut_up_I_am_probing = TRUE;
#endif
  for (pixfmt = 0; pixfmt < PIX_FMT_NB; pixfmt++) {
    GstCaps *tmpcaps;

    ctx->pix_fmt = pixfmt;
    if (gst_ffmpeg_avcodec_open (ctx, oclass->in_plugin) >= 0 &&
        ctx->pix_fmt == pixfmt) {
      ctx->width = -1;
      if (!caps)
        caps = gst_caps_new_empty ();
      tmpcaps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type, ctx,
          oclass->in_plugin->id);
      if (tmpcaps)
        gst_caps_append (caps, tmpcaps);
      else
        GST_LOG_OBJECT (ffmpegenc,
            "Couldn't get caps for oclass->in_plugin->name:%s",
            oclass->in_plugin->name);
      gst_ffmpeg_avcodec_close (ctx);
    }
    if (ctx->priv_data)
      gst_ffmpeg_avcodec_close (ctx);
  }
  av_free (ctx);
#ifndef GST_DISABLE_GST_DEBUG
  _shut_up_I_am_probing = FALSE;
#endif

  /* make sure we have something */
  if (!caps) {
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  oclass->sinkcaps = gst_caps_copy (caps);

  return caps;
}

static gboolean
gst_ffmpegenc_setcaps (GstPad * pad, GstCaps * caps)
{
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  enum PixelFormat pix_fmt;
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) GST_PAD_PARENT (pad);
  GstFFMpegEncClass *oclass =
      (GstFFMpegEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  /* close old session */
  if (ffmpegenc->opened) {
    gst_ffmpeg_avcodec_close (ffmpegenc->context);
    ffmpegenc->opened = FALSE;
  }

  /* set defaults */
  avcodec_get_context_defaults (ffmpegenc->context);

  /* if we set it in _getcaps we should set it also in _link */
  ffmpegenc->context->strict_std_compliance = -1;

  /* user defined properties */
  ffmpegenc->context->bit_rate = ffmpegenc->bitrate;
  ffmpegenc->context->bit_rate_tolerance = ffmpegenc->bitrate;
  ffmpegenc->context->gop_size = ffmpegenc->gop_size;
  ffmpegenc->context->me_method = ffmpegenc->me_method;
  GST_DEBUG_OBJECT (ffmpegenc, "Setting avcontext to bitrate %lu, gop_size %d",
      ffmpegenc->bitrate, ffmpegenc->gop_size);

  /* RTP payload used for GOB production (for Asterisk) */
  if (ffmpegenc->rtp_payload_size) {
    ffmpegenc->context->rtp_mode = 1;
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
    ffmpegenc->picture->interlaced_frame = TRUE;
    /* if this is not the case, a filter element should be used to swap fields */
    ffmpegenc->picture->top_field_first = TRUE;
  }

  /* some other defaults */
  ffmpegenc->context->rc_strategy = 2;
  ffmpegenc->context->b_frame_strategy = 0;
  ffmpegenc->context->coder_type = 0;
  ffmpegenc->context->context_model = 0;
  ffmpegenc->context->scenechange_threshold = 0;
  ffmpegenc->context->inter_threshold = 0;

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
      if (!ffmpegenc->file) {
        GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, OPEN_WRITE,
            (("Could not open file \"%s\" for writing."), ffmpegenc->filename),
            GST_ERROR_SYSTEM);
        return FALSE;
      }
      break;
    case CODEC_FLAG_PASS2:
    {                           /* need to read the whole stats file ! */
      gsize size;

      if (!g_file_get_contents (ffmpegenc->filename,
              &ffmpegenc->context->stats_in, &size, NULL)) {
        GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, READ,
            (("Could not get contents of file \"%s\"."), ffmpegenc->filename),
            GST_ERROR_SYSTEM);
        return FALSE;
      }

      break;
    }
    default:
      break;
  }

  /* fetch pix_fmt and so on */
  gst_ffmpeg_caps_with_codectype (oclass->in_plugin->type,
      caps, ffmpegenc->context);
  if (!ffmpegenc->context->time_base.den) {
    ffmpegenc->context->time_base.den = 25;
    ffmpegenc->context->time_base.num = 1;
  } else if ((oclass->in_plugin->id == CODEC_ID_MPEG4)
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
        * (ctx->time_base.den / ctx->time_base.num))
        : ffmpegenc->max_key_interval;
  }

  /* open codec */
  if (gst_ffmpeg_avcodec_open (ffmpegenc->context, oclass->in_plugin) < 0) {
    if (ffmpegenc->context->priv_data)
      gst_ffmpeg_avcodec_close (ffmpegenc->context);
    if (ffmpegenc->context->stats_in)
      g_free (ffmpegenc->context->stats_in);
    GST_DEBUG_OBJECT (ffmpegenc, "ffenc_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    return FALSE;
  }

  /* second pass stats buffer no longer needed */
  if (ffmpegenc->context->stats_in)
    g_free (ffmpegenc->context->stats_in);

  /* is the colourspace correct? */
  if (pix_fmt != ffmpegenc->context->pix_fmt) {
    gst_ffmpeg_avcodec_close (ffmpegenc->context);
    GST_DEBUG_OBJECT (ffmpegenc,
        "ffenc_%s: AV wants different colourspace (%d given, %d wanted)",
        oclass->in_plugin->name, pix_fmt, ffmpegenc->context->pix_fmt);
    return FALSE;
  }

  /* some codecs support more than one format, first auto-choose one */
  GST_DEBUG_OBJECT (ffmpegenc, "picking an output format ...");
  allowed_caps = gst_pad_get_allowed_caps (ffmpegenc->srcpad);
  if (!allowed_caps) {
    GST_DEBUG_OBJECT (ffmpegenc, "... but no peer, using template caps");
    /* we need to copy because get_allowed_caps returns a ref, and
     * get_pad_template_caps doesn't */
    allowed_caps =
        gst_caps_copy (gst_pad_get_pad_template_caps (ffmpegenc->srcpad));
  }
  GST_DEBUG_OBJECT (ffmpegenc, "chose caps %" GST_PTR_FORMAT, allowed_caps);
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, allowed_caps, ffmpegenc->context);

  /* try to set this caps on the other side */
  other_caps = gst_ffmpeg_codecid_to_caps (oclass->in_plugin->id,
      ffmpegenc->context, TRUE);

  if (!other_caps) {
    gst_ffmpeg_avcodec_close (ffmpegenc->context);
    GST_DEBUG ("Unsupported codec - no caps found");
    return FALSE;
  }

  icaps = gst_caps_intersect (allowed_caps, other_caps);
  gst_caps_unref (allowed_caps);
  gst_caps_unref (other_caps);
  if (gst_caps_is_empty (icaps)) {
    gst_caps_unref (icaps);
    return FALSE;
  }

  if (gst_caps_get_size (icaps) > 1) {
    GstCaps *newcaps;

    newcaps =
        gst_caps_new_full (gst_structure_copy (gst_caps_get_structure (icaps,
                0)), NULL);
    gst_caps_unref (icaps);
    icaps = newcaps;
  }

  if (!gst_pad_set_caps (ffmpegenc->srcpad, icaps)) {
    gst_ffmpeg_avcodec_close (ffmpegenc->context);
    gst_caps_unref (icaps);
    return FALSE;
  }
  gst_caps_unref (icaps);

  /* success! */
  ffmpegenc->opened = TRUE;

  return TRUE;
}

static void
ffmpegenc_setup_working_buf (GstFFMpegEnc * ffmpegenc)
{
  if (ffmpegenc->working_buf == NULL ||
      ffmpegenc->working_buf_size != ffmpegenc->buffer_size) {
    if (ffmpegenc->working_buf)
      g_free (ffmpegenc->working_buf);
    ffmpegenc->working_buf_size = ffmpegenc->buffer_size;
    ffmpegenc->working_buf = g_malloc (ffmpegenc->working_buf_size);
  }
}

static GstFlowReturn
gst_ffmpegenc_chain_video (GstPad * pad, GstBuffer * inbuf)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) (GST_PAD_PARENT (pad));
  GstBuffer *outbuf;
  gint ret_size = 0, frame_size;

  GST_DEBUG_OBJECT (ffmpegenc,
      "Received buffer of time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)));

  frame_size = gst_ffmpeg_avpicture_fill ((AVPicture *) ffmpegenc->picture,
      GST_BUFFER_DATA (inbuf),
      ffmpegenc->context->pix_fmt,
      ffmpegenc->context->width, ffmpegenc->context->height);
  g_return_val_if_fail (frame_size == GST_BUFFER_SIZE (inbuf), GST_FLOW_ERROR);

  ffmpegenc->picture->pts =
      gst_ffmpeg_time_gst_to_ff (GST_BUFFER_TIMESTAMP (inbuf),
      ffmpegenc->context->time_base);

  ffmpegenc_setup_working_buf (ffmpegenc);

  ret_size = avcodec_encode_video (ffmpegenc->context,
      ffmpegenc->working_buf, ffmpegenc->working_buf_size, ffmpegenc->picture);

  if (ret_size < 0) {
#ifndef GST_DISABLE_GST_DEBUG
    GstFFMpegEncClass *oclass =
        (GstFFMpegEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));
    GST_ERROR_OBJECT (ffmpegenc,
        "ffenc_%s: failed to encode buffer", oclass->in_plugin->name);
#endif /* GST_DISABLE_GST_DEBUG */
    gst_buffer_unref (inbuf);
    return GST_FLOW_OK;
  }

  /* handle b-frame delay when no output, so we don't output empty frames;
   * timestamps and so can permute a bit between coding and display order
   * but keyframes should still end up with the proper metadata */
  g_queue_push_tail (ffmpegenc->delay, inbuf);
  if (ret_size)
    inbuf = g_queue_pop_head (ffmpegenc->delay);
  else
    return GST_FLOW_OK;

  /* save stats info if there is some as well as a stats file */
  if (ffmpegenc->file && ffmpegenc->context->stats_out)
    if (fprintf (ffmpegenc->file, "%s", ffmpegenc->context->stats_out) < 0)
      GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, WRITE,
          (("Could not write to file \"%s\"."), ffmpegenc->filename),
          GST_ERROR_SYSTEM);

  outbuf = gst_buffer_new_and_alloc (ret_size);
  memcpy (GST_BUFFER_DATA (outbuf), ffmpegenc->working_buf, ret_size);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
  if (!ffmpegenc->context->coded_frame->key_frame)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ffmpegenc->srcpad));

  gst_buffer_unref (inbuf);

  return gst_pad_push (ffmpegenc->srcpad, outbuf);
}

static GstFlowReturn
gst_ffmpegenc_chain_audio (GstPad * pad, GstBuffer * inbuf)
{
  GstBuffer *outbuf = NULL, *subbuf;
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) (GST_OBJECT_PARENT (pad));
  gint size, ret_size = 0, in_size, frame_size;
  GstFlowReturn ret;

  size = GST_BUFFER_SIZE (inbuf);

  /* FIXME: events (discont (flush!) and eos (close down) etc.) */

  frame_size = ffmpegenc->context->frame_size * 2 *
      ffmpegenc->context->channels;
  in_size = size;
  if (ffmpegenc->cache)
    in_size += GST_BUFFER_SIZE (ffmpegenc->cache);

  GST_DEBUG_OBJECT (ffmpegenc,
      "Received buffer of time %" GST_TIME_FORMAT " and size %d (cache: %d)",
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)), size, in_size - size);

  while (1) {
    /* do we have enough data for one frame? */
    if (in_size / (2 * ffmpegenc->context->channels) <
        ffmpegenc->context->frame_size) {
      if (in_size > size) {
        /* this is panic! we got a buffer, but still don't have enough
         * data. Merge them and retry in the next cycle... */
        ffmpegenc->cache = gst_buffer_join (ffmpegenc->cache, inbuf);
      } else if (in_size == size) {
        /* exactly the same! how wonderful */
        ffmpegenc->cache = inbuf;
      } else if (in_size > 0) {
        ffmpegenc->cache = gst_buffer_create_sub (inbuf, size - in_size,
            in_size);
        GST_BUFFER_DURATION (ffmpegenc->cache) =
            GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (ffmpegenc->cache) /
            size;
        GST_BUFFER_TIMESTAMP (ffmpegenc->cache) =
            GST_BUFFER_TIMESTAMP (inbuf) +
            (GST_BUFFER_DURATION (inbuf) * (size - in_size) / size);
        gst_buffer_unref (inbuf);
      } else {
        gst_buffer_unref (inbuf);
      }
      return GST_FLOW_OK;
    }

    /* create the frame */
    if (in_size > size) {
      /* merge */
      subbuf = gst_buffer_create_sub (inbuf, 0, frame_size - (in_size - size));
      GST_BUFFER_DURATION (subbuf) =
          GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (subbuf) / size;
      subbuf = gst_buffer_join (ffmpegenc->cache, subbuf);
      ffmpegenc->cache = NULL;
    } else {
      subbuf = gst_buffer_create_sub (inbuf, size - in_size, frame_size);
      GST_BUFFER_DURATION (subbuf) =
          GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (subbuf) / size;
      GST_BUFFER_TIMESTAMP (subbuf) =
          GST_BUFFER_TIMESTAMP (inbuf) + (GST_BUFFER_DURATION (inbuf) *
          (size - in_size) / size);
    }

    outbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (subbuf));
    ret_size = avcodec_encode_audio (ffmpegenc->context,
        GST_BUFFER_DATA (outbuf), GST_BUFFER_SIZE (outbuf), (const short int *)
        GST_BUFFER_DATA (subbuf));

    if (ret_size < 0) {
      GST_ERROR_OBJECT (ffmpegenc, "Failed to encode buffer");
      gst_buffer_unref (inbuf);
      gst_buffer_unref (outbuf);
      gst_buffer_unref (subbuf);
      return GST_FLOW_OK;
    }

    GST_BUFFER_SIZE (outbuf) = ret_size;
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (subbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (subbuf);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ffmpegenc->srcpad));
    gst_buffer_unref (subbuf);

    ret = gst_pad_push (ffmpegenc->srcpad, outbuf);

    in_size -= frame_size;
  }

  return ret;
}

static void
gst_ffmpegenc_flush_buffers (GstFFMpegEnc * ffmpegenc, gboolean send)
{
  GstBuffer *outbuf, *inbuf;
  gint ret_size;

  GST_DEBUG_OBJECT (ffmpegenc, "flushing buffers with sending %d", send);

  /* no need to empty codec if there is none */
  if (!ffmpegenc->opened)
    goto flush;

  while (!g_queue_is_empty (ffmpegenc->delay)) {

    ffmpegenc_setup_working_buf (ffmpegenc);

    ret_size = avcodec_encode_video (ffmpegenc->context,
        ffmpegenc->working_buf, ffmpegenc->working_buf_size, NULL);

    if (ret_size < 0) {         /* there should be something, notify and give up */
#ifndef GST_DISABLE_GST_DEBUG
      GstFFMpegEncClass *oclass =
          (GstFFMpegEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));
      GST_WARNING_OBJECT (ffmpegenc,
          "ffenc_%s: failed to flush buffer", oclass->in_plugin->name);
#endif /* GST_DISABLE_GST_DEBUG */
      break;
    }

    /* save stats info if there is some as well as a stats file */
    if (ffmpegenc->file && ffmpegenc->context->stats_out)
      if (fprintf (ffmpegenc->file, "%s", ffmpegenc->context->stats_out) < 0)
        GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, WRITE,
            (("Could not write to file \"%s\"."), ffmpegenc->filename),
            GST_ERROR_SYSTEM);

    /* handle b-frame delay when no output, so we don't output empty frames */
    inbuf = g_queue_pop_head (ffmpegenc->delay);

    outbuf = gst_buffer_new_and_alloc (ret_size);
    memcpy (GST_BUFFER_DATA (outbuf), ffmpegenc->working_buf, ret_size);
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);

    if (!ffmpegenc->context->coded_frame->key_frame)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    gst_buffer_set_caps (outbuf, GST_PAD_CAPS (ffmpegenc->srcpad));

    gst_buffer_unref (inbuf);

    if (send)
      gst_pad_push (ffmpegenc->srcpad, outbuf);
    else
      gst_buffer_unref (outbuf);
  }

flush:
  {
    /* make sure that we empty the queue, is still needed if we had to break */
    while (!g_queue_is_empty (ffmpegenc->delay))
      gst_buffer_unref (g_queue_pop_head (ffmpegenc->delay));
  }
}

static gboolean
gst_ffmpegenc_event_video (GstPad * pad, GstEvent * event)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_ffmpegenc_flush_buffers (ffmpegenc, TRUE);
      break;
      /* no flushing if flush received,
       * buffers in encoder are considered (in the) past */
    default:
      break;
  }

  return gst_pad_push_event (ffmpegenc->srcpad, event);
}

static void
gst_ffmpegenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegEnc *ffmpegenc;

  /* Get a pointer of the right type. */
  ffmpegenc = (GstFFMpegEnc *) (object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    case ARG_BIT_RATE:
      ffmpegenc->bitrate = g_value_get_ulong (value);
      break;
    case ARG_GOP_SIZE:
      ffmpegenc->gop_size = g_value_get_int (value);
      break;
    case ARG_ME_METHOD:
      ffmpegenc->me_method = g_value_get_enum (value);
      break;
    case ARG_BUFSIZE:
      ffmpegenc->buffer_size = g_value_get_ulong (value);
      break;
    case ARG_RTP_PAYLOAD_SIZE:
      ffmpegenc->rtp_payload_size = g_value_get_ulong (value);
      break;
    default:
      if (!gst_ffmpeg_cfg_set_property (object, value, pspec))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_ffmpegenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegEnc *ffmpegenc;

  /* It's not null if we got it, but it might not be ours */
  ffmpegenc = (GstFFMpegEnc *) (object);

  switch (prop_id) {
    case ARG_BIT_RATE:
      g_value_set_ulong (value, ffmpegenc->bitrate);
      break;
    case ARG_GOP_SIZE:
      g_value_set_int (value, ffmpegenc->gop_size);
      break;
    case ARG_ME_METHOD:
      g_value_set_enum (value, ffmpegenc->me_method);
      break;
    case ARG_BUFSIZE:
      g_value_set_ulong (value, ffmpegenc->buffer_size);
      break;
    case ARG_RTP_PAYLOAD_SIZE:
      g_value_set_ulong (value, ffmpegenc->rtp_payload_size);
      break;
    default:
      if (!gst_ffmpeg_cfg_get_property (object, value, pspec))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_ffmpegenc_change_state (GstElement * element, GstStateChange transition)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) element;
  GstStateChangeReturn result;

  switch (transition) {
    default:
      break;
  }

  result = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_ffmpegenc_flush_buffers (ffmpegenc, FALSE);
      if (ffmpegenc->opened) {
        gst_ffmpeg_avcodec_close (ffmpegenc->context);
        ffmpegenc->opened = FALSE;
      }
      if (ffmpegenc->cache) {
        gst_buffer_unref (ffmpegenc->cache);
        ffmpegenc->cache = NULL;
      }
      if (ffmpegenc->file) {
        fclose (ffmpegenc->file);
        ffmpegenc->file = NULL;
      }
      if (ffmpegenc->working_buf) {
        g_free (ffmpegenc->working_buf);
        ffmpegenc->working_buf = NULL;
      }
      break;
    default:
      break;
  }
  return result;
}

gboolean
gst_ffmpegenc_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegEncClass),
    (GBaseInitFunc) gst_ffmpegenc_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegenc_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegEnc),
    0,
    (GInstanceInitFunc) gst_ffmpegenc_init,
  };
  GType type;
  AVCodec *in_plugin;

  in_plugin = first_avcodec;

  GST_LOG ("Registering encoders");

  /* build global ffmpeg param/property info */
  gst_ffmpeg_cfg_init ();

  while (in_plugin) {
    gchar *type_name;
    GstCaps *srccaps = NULL, *sinkcaps = NULL;
    GstFFMpegEncClassParams *params;

    /* no quasi codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
        in_plugin->id == CODEC_ID_ZLIB ||
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_S24DAUD)) {
      goto next;
    }

    /* only encoders */
    if (!in_plugin->encode) {
      goto next;
    }

    /* no codecs for which we're GUARANTEED to have better alternatives */
    if (!strcmp (in_plugin->name, "vorbis") ||
        !strcmp (in_plugin->name, "gif") || !strcmp (in_plugin->name, "flac")) {
      GST_LOG ("Ignoring encoder %s", in_plugin->name);
      goto next;
    }

    /* name */
    if (!gst_ffmpeg_get_codecid_longname (in_plugin->id)) {
      GST_WARNING ("Add a longname mapping for encoder %s (%d) please",
          in_plugin->name, in_plugin->id);
      goto next;
    }

    /* first make sure we've got a supported type */
    srccaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, TRUE);
    if (in_plugin->type == CODEC_TYPE_VIDEO) {
      sinkcaps = gst_caps_from_string
          ("video/x-raw-rgb; video/x-raw-yuv; video/x-raw-gray");
    } else {
      sinkcaps =
          gst_ffmpeg_codectype_to_caps (in_plugin->type, NULL, in_plugin->id);
    }
    if (!sinkcaps || !srccaps) {
      GST_WARNING ("Couldn't get either source/sink caps for encoder %s",
          in_plugin->name);
      goto next;
    }
    /* construct the type */
    type_name = g_strdup_printf ("ffenc_%s", in_plugin->name);

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      goto next;
    }

    params = g_new0 (GstFFMpegEncClassParams, 1);
    params->in_plugin = in_plugin;
    params->srccaps = gst_caps_ref (srccaps);
    params->sinkcaps = gst_caps_ref (sinkcaps);

    /* create the glib type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
    g_type_set_qdata (type, GST_FFENC_PARAMS_QDATA, (gpointer) params);

    if (!gst_element_register (plugin, type_name, GST_RANK_NONE, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

  next:
    if (sinkcaps)
      gst_caps_unref (sinkcaps);
    if (srccaps)
      gst_caps_unref (srccaps);
    in_plugin = in_plugin->next;
  }

  GST_LOG ("Finished registering encoders");

  return TRUE;
}
