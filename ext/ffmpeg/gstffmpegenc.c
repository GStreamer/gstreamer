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
#include <libavcodec/avcodec.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"
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

static gboolean gst_ffmpegenc_setcaps (GstFFMpegEnc * ffmpegenc,
    GstCaps * caps);
static GstCaps *gst_ffmpegenc_getcaps (GstPad * pad, GstCaps * filter);
static GstFlowReturn gst_ffmpegenc_chain_video (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static GstFlowReturn gst_ffmpegenc_chain_audio (GstPad * pad,
    GstObject * parent, GstBuffer * buffer);
static gboolean gst_ffmpegenc_event_sink (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_ffmpegenc_event_src (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_ffmpegenc_query_sink (GstPad * pad, GstObject * parent,
    GstQuery * query);

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
  AVCodec *in_plugin;
  GstPadTemplate *srctempl = NULL, *sinktempl = NULL;
  GstCaps *srccaps = NULL, *sinkcaps = NULL;
  gchar *longname, *classification, *description;

  in_plugin =
      (AVCodec *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      GST_FFENC_PARAMS_QDATA);
  g_assert (in_plugin != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("FFmpeg %s encoder", in_plugin->long_name);
  classification = g_strdup_printf ("Codec/Encoder/%s",
      (in_plugin->type == AVMEDIA_TYPE_VIDEO) ? "Video" : "Audio");
  description = g_strdup_printf ("FFmpeg %s encoder", in_plugin->name);
  gst_element_class_set_details_simple (element_class, longname, classification,
      description,
      "Wim Taymans <wim.taymans@gmail.com>, "
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
  g_free (longname);
  g_free (classification);
  g_free (description);

  if (!(srccaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, TRUE))) {
    GST_DEBUG ("Couldn't get source caps for encoder '%s'", in_plugin->name);
    srccaps = gst_caps_new_empty_simple ("unknown/unknown");
  }

  if (in_plugin->type == AVMEDIA_TYPE_VIDEO) {
    sinkcaps = gst_caps_from_string ("video/x-raw");
  } else {
    sinkcaps = gst_ffmpeg_codectype_to_audio_caps (NULL,
        in_plugin->id, TRUE, in_plugin);
  }
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

  klass->in_plugin = in_plugin;
  klass->srctempl = srctempl;
  klass->sinktempl = sinktempl;
  klass->sinkcaps = NULL;

  return;
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

  if (klass->in_plugin->type == AVMEDIA_TYPE_VIDEO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
        g_param_spec_ulong ("bitrate", "Bit Rate",
            "Target Video Bitrate", 0, G_MAXULONG, DEFAULT_VIDEO_BITRATE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GOP_SIZE,
        g_param_spec_int ("gop-size", "GOP Size",
            "Number of frames within one GOP", 0, G_MAXINT,
            DEFAULT_VIDEO_GOP_SIZE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ME_METHOD,
        g_param_spec_enum ("me-method", "ME Method", "Motion Estimation Method",
            GST_TYPE_ME_METHOD, ME_EPZS,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /* FIXME 0.11: Make this property read-only */
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFSIZE,
        g_param_spec_ulong ("buffer-size", "Buffer Size",
            "Size of the video buffers. "
            "Note: Setting this property has no effect "
            "and is deprecated!", 0, G_MAXULONG, 0,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    g_object_class_install_property (G_OBJECT_CLASS (klass),
        ARG_RTP_PAYLOAD_SIZE, g_param_spec_ulong ("rtp-payload-size",
            "RTP Payload Size", "Target GOB length", 0, G_MAXULONG, 0,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

    /* register additional properties, possibly dependent on the exact CODEC */
    gst_ffmpeg_cfg_install_property (klass, ARG_CFG_BASE);
  } else if (klass->in_plugin->type == AVMEDIA_TYPE_AUDIO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
        g_param_spec_ulong ("bitrate", "Bit Rate",
            "Target Audio Bitrate", 0, G_MAXULONG, DEFAULT_AUDIO_BITRATE,
            G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
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
  gst_pad_set_query_function (ffmpegenc->sinkpad, gst_ffmpegenc_query_sink);
  ffmpegenc->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (ffmpegenc->srcpad);

  /* ffmpeg objects */
  ffmpegenc->context = avcodec_alloc_context ();
  ffmpegenc->picture = avcodec_alloc_frame ();
  ffmpegenc->opened = FALSE;

  ffmpegenc->file = NULL;
  ffmpegenc->delay = g_queue_new ();

  gst_pad_set_event_function (ffmpegenc->sinkpad, gst_ffmpegenc_event_sink);

  if (oclass->in_plugin->type == AVMEDIA_TYPE_VIDEO) {
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_video);
    /* so we know when to flush the buffers on EOS */
    gst_pad_set_event_function (ffmpegenc->srcpad, gst_ffmpegenc_event_src);

    ffmpegenc->bitrate = DEFAULT_VIDEO_BITRATE;
    ffmpegenc->me_method = ME_EPZS;
    ffmpegenc->buffer_size = 512 * 1024;
    ffmpegenc->gop_size = DEFAULT_VIDEO_GOP_SIZE;
    ffmpegenc->rtp_payload_size = 0;

    ffmpegenc->lmin = 2;
    ffmpegenc->lmax = 31;
    ffmpegenc->max_key_interval = 0;

    gst_ffmpeg_cfg_set_defaults (ffmpegenc);
  } else if (oclass->in_plugin->type == AVMEDIA_TYPE_AUDIO) {
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_audio);

    ffmpegenc->bitrate = DEFAULT_AUDIO_BITRATE;
  }

  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->srcpad);

  ffmpegenc->adapter = gst_adapter_new ();
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

  g_object_unref (ffmpegenc->adapter);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstCaps *
gst_ffmpegenc_get_possible_sizes (GstFFMpegEnc * ffmpegenc, GstPad * pad,
    GstCaps * caps)
{
  GstCaps *othercaps = NULL;
  GstCaps *tmpcaps = NULL;
  GstCaps *intersect = NULL;
  guint i;

  othercaps = gst_pad_peer_query_caps (ffmpegenc->srcpad, NULL);

  if (!othercaps)
    return gst_caps_copy (caps);

  intersect = gst_caps_intersect (othercaps,
      gst_pad_get_pad_template_caps (ffmpegenc->srcpad));
  gst_caps_unref (othercaps);

  if (gst_caps_is_empty (intersect))
    return intersect;

  if (gst_caps_is_any (intersect))
    return gst_caps_copy (caps);

  tmpcaps = gst_caps_new_empty ();

  for (i = 0; i < gst_caps_get_size (intersect); i++) {
    GstStructure *s = gst_caps_get_structure (intersect, i);
    const GValue *height = NULL;
    const GValue *width = NULL;
    const GValue *framerate = NULL;
    GstStructure *tmps;

    height = gst_structure_get_value (s, "height");
    width = gst_structure_get_value (s, "width");
    framerate = gst_structure_get_value (s, "framerate");

    tmps = gst_structure_new_empty ("video/x-raw");
    if (width)
      gst_structure_set_value (tmps, "width", width);
    if (height)
      gst_structure_set_value (tmps, "height", height);
    if (framerate)
      gst_structure_set_value (tmps, "framerate", framerate);

    tmpcaps = gst_caps_merge_structure (tmpcaps, tmps);
  }
  gst_caps_unref (intersect);

  intersect = gst_caps_intersect (caps, tmpcaps);
  gst_caps_unref (tmpcaps);

  return intersect;
}


static GstCaps *
gst_ffmpegenc_getcaps (GstPad * pad, GstCaps * filter)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) GST_PAD_PARENT (pad);
  GstFFMpegEncClass *oclass =
      (GstFFMpegEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);
  AVCodecContext *ctx = NULL;
  enum PixelFormat pixfmt;
  GstCaps *caps = NULL;
  GstCaps *finalcaps = NULL;
  gint i;

  GST_DEBUG_OBJECT (ffmpegenc, "getting caps, filter %" GST_PTR_FORMAT, filter);

  /* audio needs no special care */
  if (oclass->in_plugin->type == AVMEDIA_TYPE_AUDIO) {
    caps = gst_pad_get_pad_template_caps (pad);
    if (filter)
      caps = gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    else
      caps = gst_caps_copy (caps);

    GST_DEBUG_OBJECT (ffmpegenc, "audio caps, return intersected template %"
        GST_PTR_FORMAT, caps);

    return caps;
  }

  /* cached */
  if (oclass->sinkcaps) {
    caps = gst_ffmpegenc_get_possible_sizes (ffmpegenc, pad, oclass->sinkcaps);
    if (filter) {
      finalcaps =
          gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (caps);
    } else {
      finalcaps = caps;
    }
    GST_DEBUG_OBJECT (ffmpegenc,
        "return intersected cached caps %" GST_PTR_FORMAT, finalcaps);
    return finalcaps;
  }

  /* create cache etc. */

  /* shut up the logging while we autoprobe; we don't want warnings and
   * errors about unsupported formats */
  /* FIXME: if someone cares about this disabling the logging for other
   * instances/threads/..., one could investigate if there is a way to
   * set this as a struct member on the av context, and check it from the
   * log handler */
#ifndef GST_DISABLE_GST_DEBUG
  _shut_up_I_am_probing = TRUE;
#endif
  GST_DEBUG_OBJECT (ffmpegenc, "probing caps");
  i = pixfmt = 0;
  /* check pixfmt until deemed finished */
  for (pixfmt = 0;; pixfmt++) {
    GstCaps *tmpcaps;

    /* override looping all pixfmt if codec declares pixfmts;
     * these may not properly check and report supported pixfmt during _init */
    if (oclass->in_plugin->pix_fmts) {
      if ((pixfmt = oclass->in_plugin->pix_fmts[i++]) == PIX_FMT_NONE) {
        GST_DEBUG_OBJECT (ffmpegenc,
            "At the end of official pixfmt for this codec, breaking out");
        break;
      }
      GST_DEBUG_OBJECT (ffmpegenc,
          "Got an official pixfmt [%d], attempting to get caps", pixfmt);
      tmpcaps = gst_ffmpeg_pixfmt_to_caps (pixfmt, NULL, oclass->in_plugin->id);
      if (tmpcaps) {
        GST_DEBUG_OBJECT (ffmpegenc, "Got caps, breaking out");
        if (!caps)
          caps = gst_caps_new_empty ();
        gst_caps_append (caps, tmpcaps);
        continue;
      }
      GST_DEBUG_OBJECT (ffmpegenc,
          "Couldn't figure out caps without context, trying again with a context");
    }

    GST_DEBUG_OBJECT (ffmpegenc, "pixfmt :%d", pixfmt);
    if (pixfmt >= PIX_FMT_NB) {
      GST_WARNING ("Invalid pixfmt, breaking out");
      break;
    }

    /* need to start with a fresh codec_context each time around, since
     * codec_close may have released stuff causing the next pass to segfault */
    ctx = avcodec_alloc_context ();
    if (!ctx) {
      GST_DEBUG_OBJECT (ffmpegenc, "no context");
      break;
    }

    /* set some default properties */
    ctx->width = DEFAULT_WIDTH;
    ctx->height = DEFAULT_HEIGHT;
    ctx->time_base.num = 1;
    ctx->time_base.den = 25;
    ctx->ticks_per_frame = 1;
    ctx->bit_rate = DEFAULT_VIDEO_BITRATE;
    /* makes it silent */
    ctx->strict_std_compliance = -1;

    ctx->pix_fmt = pixfmt;

    GST_DEBUG ("Attempting to open codec");
    if (gst_ffmpeg_avcodec_open (ctx, oclass->in_plugin) >= 0 &&
        ctx->pix_fmt == pixfmt) {
      ctx->width = -1;
      if (!caps)
        caps = gst_caps_new_empty ();
      tmpcaps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type, ctx,
          oclass->in_plugin->id, TRUE);
      if (tmpcaps)
        gst_caps_append (caps, tmpcaps);
      else
        GST_LOG_OBJECT (ffmpegenc,
            "Couldn't get caps for oclass->in_plugin->name:%s",
            oclass->in_plugin->name);
      gst_ffmpeg_avcodec_close (ctx);
    } else {
      GST_DEBUG_OBJECT (ffmpegenc, "Opening codec failed with pixfmt : %d",
          pixfmt);
    }
    if (ctx->priv_data)
      gst_ffmpeg_avcodec_close (ctx);
    av_free (ctx);
  }
#ifndef GST_DISABLE_GST_DEBUG
  _shut_up_I_am_probing = FALSE;
#endif

  /* make sure we have something */
  if (!caps) {
    caps = gst_ffmpegenc_get_possible_sizes (ffmpegenc, pad,
        gst_pad_get_pad_template_caps (pad));
    if (filter) {
      finalcaps =
          gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
      gst_caps_unref (caps);
    } else {
      finalcaps = caps;
    }
    GST_DEBUG_OBJECT (ffmpegenc, "probing gave nothing, "
        "return intersected template %" GST_PTR_FORMAT, finalcaps);
    return finalcaps;
  }

  GST_DEBUG_OBJECT (ffmpegenc, "probed caps gave %" GST_PTR_FORMAT, caps);
  oclass->sinkcaps = gst_caps_copy (caps);

  finalcaps = gst_ffmpegenc_get_possible_sizes (ffmpegenc, pad, caps);
  gst_caps_unref (caps);

  if (filter) {
    caps = finalcaps;
    finalcaps =
        gst_caps_intersect_full (filter, caps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
  }

  return finalcaps;
}

static gboolean
gst_ffmpegenc_setcaps (GstFFMpegEnc * ffmpegenc, GstCaps * caps)
{
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  enum PixelFormat pix_fmt;
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
    ffmpegenc->context->ticks_per_frame = 1;
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
        * (ctx->time_base.den * ctx->ticks_per_frame / ctx->time_base.num))
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
  /* we may have failed mapping caps to a pixfmt,
   * and quite some codecs do not make up their own mind about that
   * in any case, _NONE can never work out later on */
  if (oclass->in_plugin->type == AVMEDIA_TYPE_VIDEO && pix_fmt == PIX_FMT_NONE) {
    GST_DEBUG_OBJECT (ffmpegenc, "ffenc_%s: Failed to determine input format",
        oclass->in_plugin->name);
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
  guint wanted_size =
      ffmpegenc->context->width * ffmpegenc->context->height * 6 +
      FF_MIN_BUFFER_SIZE;

  /* Above is the buffer size used by ffmpeg/ffmpeg.c */

  if (ffmpegenc->working_buf == NULL ||
      ffmpegenc->working_buf_size != wanted_size) {
    if (ffmpegenc->working_buf)
      g_free (ffmpegenc->working_buf);
    ffmpegenc->working_buf_size = wanted_size;
    ffmpegenc->working_buf = g_malloc (ffmpegenc->working_buf_size);
  }
  ffmpegenc->buffer_size = wanted_size;
}

static GstFlowReturn
gst_ffmpegenc_chain_video (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) parent;
  GstBuffer *outbuf;
  GstMapInfo map;
  gint ret_size = 0, frame_size;
  gboolean force_keyframe;

  if (G_UNLIKELY (!ffmpegenc->opened))
    goto not_negotiated;

  GST_DEBUG_OBJECT (ffmpegenc,
      "Received buffer of time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)));

  GST_OBJECT_LOCK (ffmpegenc);
  force_keyframe = ffmpegenc->force_keyframe;
  ffmpegenc->force_keyframe = FALSE;
  GST_OBJECT_UNLOCK (ffmpegenc);

  if (force_keyframe)
    ffmpegenc->picture->pict_type = FF_I_TYPE;

  gst_buffer_map (inbuf, &map, GST_MAP_READ);
  frame_size = gst_ffmpeg_avpicture_fill ((AVPicture *) ffmpegenc->picture,
      map.data,
      ffmpegenc->context->pix_fmt,
      ffmpegenc->context->width, ffmpegenc->context->height);
  g_return_val_if_fail (frame_size == map.size, GST_FLOW_ERROR);
  ffmpegenc->picture->pts =
      gst_ffmpeg_time_gst_to_ff (GST_BUFFER_TIMESTAMP (inbuf) /
      ffmpegenc->context->ticks_per_frame, ffmpegenc->context->time_base);

  ffmpegenc_setup_working_buf (ffmpegenc);

  ret_size = avcodec_encode_video (ffmpegenc->context,
      ffmpegenc->working_buf, ffmpegenc->working_buf_size, ffmpegenc->picture);

  gst_buffer_unmap (inbuf, &map);

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
  gst_buffer_fill (outbuf, 0, ffmpegenc->working_buf, ret_size);
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
  /* buggy codec may not set coded_frame */
  if (ffmpegenc->context->coded_frame) {
    if (!ffmpegenc->context->coded_frame->key_frame)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
  } else
    GST_WARNING_OBJECT (ffmpegenc, "codec did not provide keyframe info");

  gst_buffer_unref (inbuf);

  /* Reset frame type */
  if (ffmpegenc->picture->pict_type)
    ffmpegenc->picture->pict_type = 0;

  if (force_keyframe) {
    gst_pad_push_event (ffmpegenc->srcpad,
        gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM,
            gst_structure_new ("GstForceKeyUnit",
                "timestamp", G_TYPE_UINT64, GST_BUFFER_TIMESTAMP (outbuf),
                NULL)));
  }

  return gst_pad_push (ffmpegenc->srcpad, outbuf);

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (ffmpegenc, CORE, NEGOTIATION, (NULL),
        ("not configured to input format before data start"));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
gst_ffmpegenc_encode_audio (GstFFMpegEnc * ffmpegenc, guint8 * audio_in,
    guint in_size, guint max_size, GstClockTime timestamp,
    GstClockTime duration, gboolean discont)
{
  GstBuffer *outbuf;
  AVCodecContext *ctx;
  GstMapInfo map;
  gint res;
  GstFlowReturn ret;

  ctx = ffmpegenc->context;

  /* We need to provide at least ffmpegs minimal buffer size */
  outbuf = gst_buffer_new_and_alloc (max_size + FF_MIN_BUFFER_SIZE);
  gst_buffer_map (outbuf, &map, GST_MAP_WRITE);

  GST_LOG_OBJECT (ffmpegenc, "encoding buffer of max size %d", max_size);
  if (ffmpegenc->buffer_size != max_size)
    ffmpegenc->buffer_size = max_size;

  res = avcodec_encode_audio (ctx, map.data, max_size, (short *) audio_in);

  if (res < 0) {
    gst_buffer_unmap (outbuf, &map);
    GST_ERROR_OBJECT (ffmpegenc, "Failed to encode buffer: %d", res);
    gst_buffer_unref (outbuf);
    return GST_FLOW_OK;
  }
  GST_LOG_OBJECT (ffmpegenc, "got output size %d", res);
  gst_buffer_unmap (outbuf, &map);
  gst_buffer_resize (outbuf, 0, res);

  GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
  GST_BUFFER_DURATION (outbuf) = duration;
  if (discont)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);

  GST_LOG_OBJECT (ffmpegenc, "pushing size %d, timestamp %" GST_TIME_FORMAT,
      res, GST_TIME_ARGS (timestamp));

  ret = gst_pad_push (ffmpegenc->srcpad, outbuf);

  return ret;
}

static GstFlowReturn
gst_ffmpegenc_chain_audio (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstFFMpegEnc *ffmpegenc;
  GstFFMpegEncClass *oclass;
  AVCodecContext *ctx;
  GstClockTime timestamp, duration;
  gsize size, frame_size;
  gint osize;
  GstFlowReturn ret;
  gint out_size;
  gboolean discont;
  guint8 *in_data;

  ffmpegenc = (GstFFMpegEnc *) parent;
  oclass = (GstFFMpegEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  if (G_UNLIKELY (!ffmpegenc->opened))
    goto not_negotiated;

  ctx = ffmpegenc->context;

  size = gst_buffer_get_size (inbuf);
  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);
  discont = GST_BUFFER_IS_DISCONT (inbuf);

  GST_DEBUG_OBJECT (ffmpegenc,
      "Received time %" GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT, GST_TIME_ARGS (timestamp),
      GST_TIME_ARGS (duration), size);

  frame_size = ctx->frame_size;
  osize = av_get_bits_per_sample_format (ctx->sample_fmt) / 8;

  if (frame_size > 1) {
    /* we have a frame_size, feed the encoder multiples of this frame size */
    guint avail, frame_bytes;

    if (discont) {
      GST_LOG_OBJECT (ffmpegenc, "DISCONT, clear adapter");
      gst_adapter_clear (ffmpegenc->adapter);
      ffmpegenc->discont = TRUE;
    }

    if (gst_adapter_available (ffmpegenc->adapter) == 0) {
      /* lock on to new timestamp */
      GST_LOG_OBJECT (ffmpegenc, "taking buffer timestamp %" GST_TIME_FORMAT,
          GST_TIME_ARGS (timestamp));
      ffmpegenc->adapter_ts = timestamp;
      ffmpegenc->adapter_consumed = 0;
    } else {
      GstClockTime upstream_time;
      GstClockTime consumed_time;
      guint64 bytes;

      /* use timestamp at head of the adapter */
      consumed_time =
          gst_util_uint64_scale (ffmpegenc->adapter_consumed, GST_SECOND,
          ctx->sample_rate);
      timestamp = ffmpegenc->adapter_ts + consumed_time;
      GST_LOG_OBJECT (ffmpegenc, "taking adapter timestamp %" GST_TIME_FORMAT
          " and adding consumed time %" GST_TIME_FORMAT,
          GST_TIME_ARGS (ffmpegenc->adapter_ts), GST_TIME_ARGS (consumed_time));

      /* check with upstream timestamps, if too much deviation,
       * forego some timestamp perfection in favour of upstream syncing
       * (particularly in case these do not happen to come in multiple
       * of frame size) */
      upstream_time = gst_adapter_prev_timestamp (ffmpegenc->adapter, &bytes);
      if (GST_CLOCK_TIME_IS_VALID (upstream_time)) {
        GstClockTimeDiff diff;

        upstream_time +=
            gst_util_uint64_scale (bytes, GST_SECOND,
            ctx->sample_rate * osize * ctx->channels);
        diff = upstream_time - timestamp;
        /* relaxed difference, rather than half a sample or so ... */
        if (diff > GST_SECOND / 10 || diff < -GST_SECOND / 10) {
          GST_DEBUG_OBJECT (ffmpegenc, "adapter timestamp drifting, "
              "taking upstream timestamp %" GST_TIME_FORMAT,
              GST_TIME_ARGS (upstream_time));
          timestamp = upstream_time;
          /* samples corresponding to bytes */
          ffmpegenc->adapter_consumed = bytes / (osize * ctx->channels);
          ffmpegenc->adapter_ts = upstream_time -
              gst_util_uint64_scale (ffmpegenc->adapter_consumed, GST_SECOND,
              ctx->sample_rate);
          ffmpegenc->discont = TRUE;
        }
      }
    }

    GST_LOG_OBJECT (ffmpegenc, "pushing buffer in adapter");
    gst_adapter_push (ffmpegenc->adapter, inbuf);

    /* first see how many bytes we need to feed to the decoder. */
    frame_bytes = frame_size * osize * ctx->channels;
    avail = gst_adapter_available (ffmpegenc->adapter);

    GST_LOG_OBJECT (ffmpegenc, "frame_bytes %u, avail %u", frame_bytes, avail);

    /* while there is more than a frame size in the adapter, consume it */
    while (avail >= frame_bytes) {
      GST_LOG_OBJECT (ffmpegenc, "taking %u bytes from the adapter",
          frame_bytes);

      /* Note that we take frame_bytes and add frame_size.
       * Makes sense when resyncing because you don't have to count channels
       * or samplesize to divide by the samplerate */

      /* take an audio buffer out of the adapter */
      in_data = (guint8 *) gst_adapter_map (ffmpegenc->adapter, frame_bytes);
      ffmpegenc->adapter_consumed += frame_size;

      /* calculate timestamp and duration relative to start of adapter and to
       * the amount of samples we consumed */
      duration =
          gst_util_uint64_scale (ffmpegenc->adapter_consumed, GST_SECOND,
          ctx->sample_rate);
      duration -= (timestamp - ffmpegenc->adapter_ts);

      /* 4 times the input size should be big enough... */
      out_size = frame_bytes * 4;

      ret =
          gst_ffmpegenc_encode_audio (ffmpegenc, in_data, frame_bytes, out_size,
          timestamp, duration, ffmpegenc->discont);

      gst_adapter_unmap (ffmpegenc->adapter);
      gst_adapter_flush (ffmpegenc->adapter, frame_bytes);

      if (ret != GST_FLOW_OK)
        goto push_failed;

      /* advance the adapter timestamp with the duration */
      timestamp += duration;

      ffmpegenc->discont = FALSE;
      avail = gst_adapter_available (ffmpegenc->adapter);
    }
    GST_LOG_OBJECT (ffmpegenc, "%u bytes left in the adapter", avail);
  } else {
    GstMapInfo map;
    /* we have no frame_size, feed the encoder all the data and expect a fixed
     * output size */
    int coded_bps = av_get_bits_per_sample (oclass->in_plugin->id);

    GST_LOG_OBJECT (ffmpegenc, "coded bps %d, osize %d", coded_bps, osize);

    out_size = size / osize;
    if (coded_bps)
      out_size = (out_size * coded_bps) / 8;

    gst_buffer_map (inbuf, &map, GST_MAP_READ);
    in_data = map.data;
    size = map.size;
    ret = gst_ffmpegenc_encode_audio (ffmpegenc, in_data, size, out_size,
        timestamp, duration, discont);
    gst_buffer_unmap (inbuf, &map);
    gst_buffer_unref (inbuf);

    if (ret != GST_FLOW_OK)
      goto push_failed;
  }

  return GST_FLOW_OK;

  /* ERRORS */
not_negotiated:
  {
    GST_ELEMENT_ERROR (ffmpegenc, CORE, NEGOTIATION, (NULL),
        ("not configured to input format before data start"));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
push_failed:
  {
    GST_DEBUG_OBJECT (ffmpegenc, "Failed to push buffer %d (%s)", ret,
        gst_flow_get_name (ret));
    return ret;
  }
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
    gst_buffer_fill (outbuf, 0, ffmpegenc->working_buf, ret_size);
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);

    if (!ffmpegenc->context->coded_frame->key_frame)
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);

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
gst_ffmpegenc_event_sink (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) parent;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      gst_ffmpegenc_flush_buffers (ffmpegenc, TRUE);
      break;
      /* no flushing if flush received,
       * buffers in encoder are considered (in the) past */
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    {
      const GstStructure *s;

      s = gst_event_get_structure (event);
      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        ffmpegenc->picture->pict_type = FF_I_TYPE;
      }
      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      gboolean ret;

      gst_event_parse_caps (event, &caps);
      ret = gst_ffmpegenc_setcaps (ffmpegenc, caps);
      gst_event_unref (event);
      return ret;
    }
    default:
      break;
  }

  return gst_pad_push_event (ffmpegenc->srcpad, event);
}

static gboolean
gst_ffmpegenc_event_src (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) parent;
  gboolean forward = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CUSTOM_UPSTREAM:{
      const GstStructure *s;
      s = gst_event_get_structure (event);
      if (gst_structure_has_name (s, "GstForceKeyUnit")) {
        GST_OBJECT_LOCK (ffmpegenc);
        ffmpegenc->force_keyframe = TRUE;
        GST_OBJECT_UNLOCK (ffmpegenc);
        forward = FALSE;
        gst_event_unref (event);
      }
      break;
    }

    default:
      break;
  }

  if (forward)
    return gst_pad_push_event (ffmpegenc->sinkpad, event);
  else
    return TRUE;
}

static gboolean
gst_ffmpegenc_query_sink (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);
      caps = gst_ffmpegenc_getcaps (pad, filter);
      gst_query_set_caps_result (query, caps);
      gst_caps_unref (caps);
      res = TRUE;
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static void
gst_ffmpegenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegEnc *ffmpegenc;

  /* Get a pointer of the right type. */
  ffmpegenc = (GstFFMpegEnc *) (object);

  if (ffmpegenc->opened) {
    GST_WARNING_OBJECT (ffmpegenc,
        "Can't change properties once decoder is setup !");
    return;
  }

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
      gst_adapter_clear (ffmpegenc->adapter);

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


  GST_LOG ("Registering encoders");

  /* build global ffmpeg param/property info */
  gst_ffmpeg_cfg_init ();

  in_plugin = av_codec_next (NULL);
  while (in_plugin) {
    gchar *type_name;

    /* Skip non-AV codecs */
    if (in_plugin->type != AVMEDIA_TYPE_AUDIO &&
        in_plugin->type != AVMEDIA_TYPE_VIDEO)
      goto next;

    /* no quasi codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
        in_plugin->id == CODEC_ID_V210 ||
        in_plugin->id == CODEC_ID_V210X ||
        in_plugin->id == CODEC_ID_R210 ||
        in_plugin->id == CODEC_ID_ZLIB ||
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_BLURAY)) {
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

    /* only encoders */
    if (!in_plugin->encode) {
      goto next;
    }

    /* FIXME : We should have a method to know cheaply whether we have a mapping
     * for the given plugin or not */

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    if (!strcmp (in_plugin->name, "vorbis") ||
        !strcmp (in_plugin->name, "gif") || !strcmp (in_plugin->name, "flac")) {
      GST_LOG ("Ignoring encoder %s", in_plugin->name);
      goto next;
    }

    /* construct the type */
    type_name = g_strdup_printf ("ffenc_%s", in_plugin->name);

    type = g_type_from_name (type_name);

    if (!type) {

      /* create the glib type now */
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
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
