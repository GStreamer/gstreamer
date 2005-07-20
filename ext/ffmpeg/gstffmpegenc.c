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

#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"

typedef struct _GstFFMpegEnc GstFFMpegEnc;

struct _GstFFMpegEnc
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  AVCodecContext *context;
  AVFrame *picture;
  gboolean opened;
  GstBuffer *cache;

  /* cache */
  gulong bitrate;
  gint me_method;
  gint gop_size;
  gulong buffer_size;
};

typedef struct _GstFFMpegEncClass GstFFMpegEncClass;

struct _GstFFMpegEncClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
  GstCaps *sinkcaps;
};

typedef struct
{
  AVCodec *in_plugin;
  GstCaps *srccaps, *sinkcaps;
} GstFFMpegEncClassParams;

#define GST_TYPE_FFMPEGENC \
  (gst_ffmpegenc_get_type())
#define GST_FFMPEGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGENC,GstFFMpegEnc))
#define GST_FFMPEGENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGENC,GstFFMpegEncClass))
#define GST_IS_FFMPEGENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGENC))
#define GST_IS_FFMPEGENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGENC))

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
  ARG_BUFSIZE
      /* FILL ME */
};

#define GST_TYPE_ME_METHOD (gst_ffmpegenc_me_method_get_type())
static GType
gst_ffmpegenc_me_method_get_type (void)
{
  static GType ffmpegenc_me_method_type = 0;
  static GEnumValue ffmpegenc_me_methods[] = {
    {ME_ZERO, "0", "zero"},
    {ME_FULL, "1", "full"},
    {ME_LOG, "2", "logarithmic"},
    {ME_PHODS, "3", "phods"},
    {ME_EPZS, "4", "epzs"},
    {ME_X1, "5", "x1"},
    {0, NULL, NULL},
  };
  if (!ffmpegenc_me_method_type) {
    ffmpegenc_me_method_type =
        g_enum_register_static ("GstFFMpegEncMeMethod", ffmpegenc_me_methods);
  }
  return ffmpegenc_me_method_type;
}

static GHashTable *enc_global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void gst_ffmpegenc_class_init (GstFFMpegEncClass * klass);
static void gst_ffmpegenc_base_init (GstFFMpegEncClass * klass);
static void gst_ffmpegenc_init (GstFFMpegEnc * ffmpegenc);
static void gst_ffmpegenc_dispose (GObject * object);

static GstPadLinkReturn gst_ffmpegenc_link (GstPad * pad,
    const GstCaps * caps);
static GstCaps * gst_ffmpegenc_getcaps (GstPad * pad);
static void gst_ffmpegenc_chain_video (GstPad * pad, GstData * _data);
static void gst_ffmpegenc_chain_audio (GstPad * pad, GstData * _data);

static void gst_ffmpegenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static GstElementStateReturn gst_ffmpegenc_change_state (GstElement * element);

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegenc_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegenc_base_init (GstFFMpegEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegEncClassParams *params;
  GstElementDetails details;
  GstPadTemplate *srctempl, *sinktempl;

  params = g_hash_table_lookup (enc_global_plugins,
      GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));
  /* HACK: if we don't have a GType yet, our params are stored at position 0 */
  if (!params) {
    params = g_hash_table_lookup (enc_global_plugins, GINT_TO_POINTER (0));
  }
  g_assert (params);

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

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  if (klass->in_plugin->type == CODEC_TYPE_VIDEO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
        g_param_spec_ulong ("bitrate", "Bit Rate",
            "Target Video Bitrate", 0, G_MAXULONG, 300000, G_PARAM_READWRITE));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_GOP_SIZE,
        g_param_spec_int ("gop_size", "GOP Size",
            "Number of frames within one GOP",
            0, G_MAXINT, 15, G_PARAM_READWRITE));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_ME_METHOD,
        g_param_spec_enum ("me_method", "ME Method",
            "Motion Estimation Method",
            GST_TYPE_ME_METHOD, ME_LOG, G_PARAM_READWRITE));
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BUFSIZE,
        g_param_spec_ulong ("buffer_size", "Buffer Size",
            "Size of the video buffers", 0, G_MAXULONG, 0, G_PARAM_READWRITE));
  } else if (klass->in_plugin->type == CODEC_TYPE_AUDIO) {
    g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BIT_RATE,
        g_param_spec_ulong ("bitrate", "Bit Rate",
            "Target Audio Bitrate", 0, G_MAXULONG, 128000, G_PARAM_READWRITE));
  }

  gobject_class->set_property = gst_ffmpegenc_set_property;
  gobject_class->get_property = gst_ffmpegenc_get_property;

  gstelement_class->change_state = gst_ffmpegenc_change_state;

  gobject_class->dispose = gst_ffmpegenc_dispose;
}

static void
gst_ffmpegenc_init (GstFFMpegEnc * ffmpegenc)
{
  GstFFMpegEncClass *oclass =
      (GstFFMpegEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));

  /* setup pads */
  ffmpegenc->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_link_function (ffmpegenc->sinkpad, gst_ffmpegenc_link);
  gst_pad_set_getcaps_function (ffmpegenc->sinkpad, gst_ffmpegenc_getcaps);
  ffmpegenc->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_explicit_caps (ffmpegenc->srcpad);

  /* ffmpeg objects */
  ffmpegenc->context = avcodec_alloc_context ();
  ffmpegenc->picture = avcodec_alloc_frame ();
  ffmpegenc->opened = FALSE;

  if (oclass->in_plugin->type == CODEC_TYPE_VIDEO) {
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_video);

    ffmpegenc->bitrate = 300000;
    ffmpegenc->buffer_size = 512 * 1024;
    ffmpegenc->gop_size = 15;
  } else if (oclass->in_plugin->type == CODEC_TYPE_AUDIO) {
    gst_pad_set_chain_function (ffmpegenc->sinkpad, gst_ffmpegenc_chain_audio);

    ffmpegenc->bitrate = 128000;
  }

  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->sinkpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegenc), ffmpegenc->srcpad);
}

static void
gst_ffmpegenc_dispose (GObject * object)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) object;

  /* close old session */
  if (ffmpegenc->opened) {
    avcodec_close (ffmpegenc->context);
    ffmpegenc->opened = FALSE;
  }

  /* clean up remaining allocated data */
  av_free (ffmpegenc->context);
  av_free (ffmpegenc->picture);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstCaps *
gst_ffmpegenc_getcaps (GstPad * pad)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) gst_pad_get_parent (pad);
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
  ctx->width = 384;
  ctx->height = 288;
  ctx->time_base.num = DEFAULT_FRAME_RATE_BASE;
  ctx->time_base.den = 25 * DEFAULT_FRAME_RATE_BASE;
  ctx->bit_rate = 350 * 1000;
  /* makes it silent */
  ctx->strict_std_compliance = -1;

  for (pixfmt = 0; pixfmt < PIX_FMT_NB; pixfmt++) {
    ctx->pix_fmt = pixfmt;
    if (avcodec_open (ctx, oclass->in_plugin) >= 0 &&
        ctx->pix_fmt == pixfmt) {
      ctx->width = -1;
      if (!caps)
        caps = gst_caps_new_empty ();
      gst_caps_append (caps,
          gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type, ctx));
      avcodec_close (ctx);
    }
    if (ctx->priv_data)
      avcodec_close (ctx);
  }
  av_free (ctx);

  /* make sure we have something */
  if (!caps) {
    return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }

  oclass->sinkcaps = gst_caps_copy (caps);

  return caps;
}

static GstPadLinkReturn
gst_ffmpegenc_link (GstPad * pad, const GstCaps * caps)
{
  GstCaps *other_caps;
  GstCaps *allowed_caps;
  GstCaps *icaps;
  enum PixelFormat pix_fmt;
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) gst_pad_get_parent (pad);
  GstFFMpegEncClass *oclass =
      (GstFFMpegEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  /* close old session */
  if (ffmpegenc->opened) {
    avcodec_close (ffmpegenc->context);
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

  /* general properties */
  ffmpegenc->context->qmin = 3;
  ffmpegenc->context->qmax = 15;
  ffmpegenc->context->max_qdiff = 3;

  /* fetch pix_fmt and so on */
  gst_ffmpeg_caps_with_codectype (oclass->in_plugin->type,
      caps, ffmpegenc->context);

  /* FIXME: prevent nego errors because of fixed-caps */
  if (!ffmpegenc->context->time_base.den)
    ffmpegenc->context->time_base.den = DEFAULT_FRAME_RATE_BASE * 25;

  pix_fmt = ffmpegenc->context->pix_fmt;

  /* open codec */
  if (avcodec_open (ffmpegenc->context, oclass->in_plugin) < 0) {
    if (ffmpegenc->context->priv_data)
      avcodec_close (ffmpegenc->context);
    GST_DEBUG ("ffenc_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    return GST_PAD_LINK_REFUSED;
  }

  /* is the colourspace correct? */
  if (pix_fmt != ffmpegenc->context->pix_fmt) {
    avcodec_close (ffmpegenc->context);
    GST_DEBUG ("ffenc_%s: AV wants different colourspace (%d given, %d wanted)",
        oclass->in_plugin->name, pix_fmt, ffmpegenc->context->pix_fmt);
    return GST_PAD_LINK_REFUSED;
  }

  /* some codecs support more than one format, first auto-choose one */
  allowed_caps = gst_pad_get_allowed_caps (ffmpegenc->srcpad);
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, allowed_caps, ffmpegenc->context);

  /* try to set this caps on the other side */
  other_caps = gst_ffmpeg_codecid_to_caps (oclass->in_plugin->id,
      ffmpegenc->context, TRUE);

  if (!other_caps) {
    avcodec_close (ffmpegenc->context);
    GST_DEBUG ("Unsupported codec - no caps found");
    return GST_PAD_LINK_REFUSED;
  }

  icaps = gst_caps_intersect (allowed_caps, other_caps);
  gst_caps_free (allowed_caps);
  gst_caps_free (other_caps);
  if (gst_caps_is_empty (icaps)) {
    gst_caps_free (icaps);
    return GST_PAD_LINK_REFUSED;
  }

  if (gst_caps_get_size (icaps) > 1) {
    GstCaps *newcaps;

    newcaps =
        gst_caps_new_full (gst_structure_copy (gst_caps_get_structure (icaps,
            0)), NULL);
    gst_caps_free (icaps);
    icaps = newcaps;
  }

  /* FIXME set_explicit_caps is not supposed to be used in a pad link
   * function. */
  if (!gst_pad_set_explicit_caps (ffmpegenc->srcpad, icaps)) {
    avcodec_close (ffmpegenc->context);
    gst_caps_free (icaps);
    return GST_PAD_LINK_REFUSED;
  }
  gst_caps_free (icaps);

  /* success! */
  ffmpegenc->opened = TRUE;

  return GST_PAD_LINK_OK;
}

static void
gst_ffmpegenc_chain_video (GstPad * pad, GstData * _data)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) (gst_pad_get_parent (pad));
  GstBuffer *inbuf = GST_BUFFER (_data), *outbuf;
  GstFFMpegEncClass *oclass =
      (GstFFMpegEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));
  gint ret_size = 0, frame_size;
  const AVRational bq = { 1, 1000000000 };

  /* FIXME: events (discont (flush!) and eos (close down) etc.) */

  GST_DEBUG_OBJECT (ffmpegenc,
      "Received buffer of time %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)));

  frame_size = gst_ffmpeg_avpicture_fill ((AVPicture *) ffmpegenc->picture,
      GST_BUFFER_DATA (inbuf),
      ffmpegenc->context->pix_fmt,
      ffmpegenc->context->width, ffmpegenc->context->height);
  g_return_if_fail (frame_size == GST_BUFFER_SIZE (inbuf));

  ffmpegenc->picture->pts =
      av_rescale_q (GST_BUFFER_TIMESTAMP (inbuf),
          bq, ffmpegenc->context->time_base);

  outbuf = gst_buffer_new_and_alloc (ffmpegenc->buffer_size);
  ret_size = avcodec_encode_video (ffmpegenc->context,
      GST_BUFFER_DATA (outbuf),
      GST_BUFFER_MAXSIZE (outbuf), ffmpegenc->picture);

  if (ret_size < 0) {
    GST_ERROR_OBJECT (ffmpegenc,
        "ffenc_%s: failed to encode buffer", oclass->in_plugin->name);
    gst_buffer_unref (inbuf);
    gst_buffer_unref (outbuf);
    return;
  }

  GST_BUFFER_SIZE (outbuf) = ret_size;
  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (inbuf);
  if (ffmpegenc->context->coded_frame->key_frame)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);
  else
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_DELTA_UNIT);
  gst_pad_push (ffmpegenc->srcpad, GST_DATA (outbuf));

  gst_buffer_unref (inbuf);
}

static void
gst_ffmpegenc_chain_audio (GstPad * pad, GstData * _data)
{
  GstBuffer *inbuf = GST_BUFFER (_data);
  GstBuffer *outbuf = NULL, *subbuf;
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) (gst_pad_get_parent (pad));
  gint size, ret_size = 0, in_size, frame_size;

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
        ffmpegenc->cache = gst_buffer_merge (ffmpegenc->cache, inbuf);
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

      return;
    }

    /* create the frame */
    if (in_size > size) {
      /* merge */
      subbuf = gst_buffer_create_sub (inbuf, 0, frame_size - (in_size - size));
      GST_BUFFER_DURATION (subbuf) =
          GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (subbuf) / size;
      subbuf = gst_buffer_merge (ffmpegenc->cache, subbuf);
      ffmpegenc->cache = NULL;
    } else {
      subbuf = gst_buffer_create_sub (inbuf, size - in_size, frame_size);
      GST_BUFFER_DURATION (subbuf) =
          GST_BUFFER_DURATION (inbuf) * GST_BUFFER_SIZE (subbuf) / size;
      GST_BUFFER_TIMESTAMP (subbuf) =
          GST_BUFFER_TIMESTAMP (inbuf) + (GST_BUFFER_DURATION (inbuf) *
          (size - in_size) / size);
    }

    outbuf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (inbuf));
    ret_size = avcodec_encode_audio (ffmpegenc->context,
        GST_BUFFER_DATA (outbuf),
        GST_BUFFER_MAXSIZE (outbuf), (const short int *)
        GST_BUFFER_DATA (subbuf));

    if (ret_size < 0) {
      GST_ERROR_OBJECT (ffmpegenc, "Failed to encode buffer");
      gst_buffer_unref (inbuf);
      gst_buffer_unref (outbuf);
      gst_buffer_unref (subbuf);
      return;
    }

    GST_BUFFER_SIZE (outbuf) = ret_size;
    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (subbuf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (subbuf);
    gst_pad_push (ffmpegenc->srcpad, GST_DATA (outbuf));

    in_size -= frame_size;
    gst_buffer_unref (subbuf);
  }
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
    default:
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
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_ffmpegenc_change_state (GstElement * element)
{
  GstFFMpegEnc *ffmpegenc = (GstFFMpegEnc *) element;
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      if (ffmpegenc->opened) {
        avcodec_close (ffmpegenc->context);
        ffmpegenc->opened = FALSE;
      }
      if (ffmpegenc->cache) {
        gst_buffer_unref (ffmpegenc->cache);
        ffmpegenc->cache = NULL;
      }
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
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

  enc_global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    gchar *type_name;
    GstCaps *srccaps, *sinkcaps;
    GstFFMpegEncClassParams *params;

    /* no quasi codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
        in_plugin->id == CODEC_ID_ZLIB ||
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_ALAW)) {
      goto next;
    }

    /* only encoders */
    if (!in_plugin->encode) {
      goto next;
    }

    /* name */
    if (!gst_ffmpeg_get_codecid_longname (in_plugin->id)) {
      g_warning ("Add encoder %s (%d) please",
          in_plugin->name, in_plugin->id);
      goto next;
    }

    /* first make sure we've got a supported type */
    srccaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, TRUE);
    if (in_plugin->type == CODEC_TYPE_VIDEO) {
      sinkcaps = gst_caps_from_string ("video/x-raw-rgb; video/x-raw-yuv");
    } else {
      sinkcaps = gst_ffmpeg_codectype_to_caps (in_plugin->type, NULL);
    }
    if (!sinkcaps || !srccaps)
      goto next;

    /* construct the type */
    type_name = g_strdup_printf ("ffenc_%s", in_plugin->name);

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      goto next;
    }

    params = g_new0 (GstFFMpegEncClassParams, 1);
    params->in_plugin = in_plugin;
    params->srccaps = srccaps;
    params->sinkcaps = sinkcaps;

    g_hash_table_insert (enc_global_plugins,
        GINT_TO_POINTER (0), (gpointer) params);

    /* create the glib type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
    if (!gst_element_register (plugin, type_name, GST_RANK_NONE, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

    g_hash_table_insert (enc_global_plugins,
        GINT_TO_POINTER (type), (gpointer) params);

  next:
    in_plugin = in_plugin->next;
  }
  g_hash_table_remove (enc_global_plugins, GINT_TO_POINTER (0));

  return TRUE;
}
