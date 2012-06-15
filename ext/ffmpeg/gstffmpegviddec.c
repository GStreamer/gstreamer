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
#include <libavcodec/avcodec.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideodecoder.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"

typedef struct _GstFFMpegVidDec GstFFMpegVidDec;

#define MAX_TS_MASK 0xff

struct _GstFFMpegVidDec
{
  GstVideoDecoder parent;

  GstVideoCodecState *input_state;
  GstVideoCodecState *output_state;

  /* decoding */
  AVCodecContext *context;
  AVFrame *picture;
  gboolean opened;

  enum PixelFormat pix_fmt;
  gboolean waiting_for_key;

  /* for tracking DTS/PTS */
  gboolean has_b_frames;

  guint8 *padded;
  guint padded_size;

  GValue *par;                  /* pixel aspect ratio of incoming data */
  gboolean current_dr;          /* if direct rendering is enabled */
  gboolean extra_ref;           /* keep extra ref around in get/release */

  /* some properties */
  enum AVDiscard skip_frame;
  gint lowres;
  gboolean direct_rendering;
  gboolean do_padding;
  gboolean debug_mv;
  gboolean crop;
  int max_threads;

  gboolean is_realvideo;

  /* Can downstream allocate 16bytes aligned data. */
  gboolean can_allocate_aligned;
};

typedef struct _GstFFMpegVidDecClass GstFFMpegVidDecClass;

struct _GstFFMpegVidDecClass
{
  GstVideoDecoderClass parent_class;

  AVCodec *in_plugin;
};


#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegviddec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegVidDec))
#define GST_FFMPEGVIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegVidDecClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGVIDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

#define DEFAULT_LOWRES			0
#define DEFAULT_SKIPFRAME		0
#define DEFAULT_DIRECT_RENDERING	TRUE
#define DEFAULT_DO_PADDING		TRUE
#define DEFAULT_DEBUG_MV		FALSE
#define DEFAULT_CROP			TRUE
#define DEFAULT_MAX_THREADS		0

enum
{
  PROP_0,
  PROP_LOWRES,
  PROP_SKIPFRAME,
  PROP_DIRECT_RENDERING,
  PROP_DO_PADDING,
  PROP_DEBUG_MV,
  PROP_CROP,
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
static gboolean gst_ffmpegviddec_stop (GstVideoDecoder * decoder);
static gboolean gst_ffmpegviddec_reset (GstVideoDecoder * decoder,
    gboolean hard);

static void gst_ffmpegviddec_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_ffmpegviddec_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_ffmpegviddec_negotiate (GstFFMpegVidDec * ffmpegdec,
    gboolean force);

/* some sort of bufferpool handling, but different */
static int gst_ffmpegviddec_get_buffer (AVCodecContext * context,
    AVFrame * picture);
static void gst_ffmpegviddec_release_buffer (AVCodecContext * context,
    AVFrame * picture);

static GstFlowReturn gst_ffmpegviddec_finish (GstVideoDecoder * decoder);
static void gst_ffmpegviddec_drain (GstFFMpegVidDec * ffmpegdec);

#define GST_FFDEC_PARAMS_QDATA g_quark_from_static_string("ffdec-params")

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
        g_enum_register_static ("GstFFMpegVidDecLowres", ffmpegdec_lowres);
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
        g_enum_register_static ("GstFFMpegVidDecSkipFrame",
        ffmpegdec_skipframe);
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
  longname = g_strdup_printf ("FFmpeg %s decoder", in_plugin->long_name);
  description = g_strdup_printf ("FFmpeg %s decoder", in_plugin->name);
  gst_element_class_set_details_simple (element_class, longname,
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
    sinkcaps = gst_caps_from_string ("unknown/unknown");
  }
  srccaps =
      gst_caps_from_string
      ("video/x-raw-rgb; video/x-raw-yuv; video/x-raw-gray");

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
  g_object_class_install_property (gobject_class, PROP_DO_PADDING,
      g_param_spec_boolean ("do-padding", "Do Padding",
          "Add 0 padding before decoding data", DEFAULT_DO_PADDING,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEBUG_MV,
      g_param_spec_boolean ("debug-mv", "Debug motion vectors",
          "Whether ffmpeg should print motion vectors on top of the image",
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
  viddec_class->stop = gst_ffmpegviddec_stop;
  viddec_class->reset = gst_ffmpegviddec_reset;
  viddec_class->finish = gst_ffmpegviddec_finish;
}

static void
gst_ffmpegviddec_init (GstFFMpegVidDec * ffmpegdec)
{
  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context ();
  ffmpegdec->picture = avcodec_alloc_frame ();
  ffmpegdec->opened = FALSE;
  ffmpegdec->waiting_for_key = TRUE;
  ffmpegdec->skip_frame = ffmpegdec->lowres = 0;
  ffmpegdec->direct_rendering = DEFAULT_DIRECT_RENDERING;
  ffmpegdec->do_padding = DEFAULT_DO_PADDING;
  ffmpegdec->debug_mv = DEFAULT_DEBUG_MV;
  ffmpegdec->crop = DEFAULT_CROP;
  ffmpegdec->max_threads = DEFAULT_MAX_THREADS;

  /* We initially assume downstream can allocate 16 bytes aligned buffers */
  ffmpegdec->can_allocate_aligned = TRUE;
}

static void
gst_ffmpegviddec_finalize (GObject * object)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) object;

  if (ffmpegdec->context != NULL) {
    av_free (ffmpegdec->context);
    ffmpegdec->context = NULL;
  }

  if (ffmpegdec->picture != NULL) {
    av_free (ffmpegdec->picture);
    ffmpegdec->picture = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


/* with LOCK */
static void
gst_ffmpegviddec_close (GstFFMpegVidDec * ffmpegdec)
{
  if (!ffmpegdec->opened)
    return;

  GST_LOG_OBJECT (ffmpegdec, "closing ffmpeg codec");

  if (ffmpegdec->context->priv_data)
    gst_ffmpeg_avcodec_close (ffmpegdec->context);
  ffmpegdec->opened = FALSE;

  if (ffmpegdec->context->palctrl) {
    av_free (ffmpegdec->context->palctrl);
    ffmpegdec->context->palctrl = NULL;
  }

  if (ffmpegdec->context->extradata) {
    av_free (ffmpegdec->context->extradata);
    ffmpegdec->context->extradata = NULL;
  }
}

/* with LOCK */
static gboolean
gst_ffmpegviddec_open (GstFFMpegVidDec * ffmpegdec)
{
  GstFFMpegVidDecClass *oclass;

  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (gst_ffmpeg_avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0)
    goto could_not_open;

  ffmpegdec->opened = TRUE;
  ffmpegdec->is_realvideo = FALSE;

  GST_LOG_OBJECT (ffmpegdec, "Opened ffmpeg codec %s, id %d",
      oclass->in_plugin->name, oclass->in_plugin->id);

  switch (oclass->in_plugin->id) {
    case CODEC_ID_RV10:
    case CODEC_ID_RV30:
    case CODEC_ID_RV20:
    case CODEC_ID_RV40:
      ffmpegdec->is_realvideo = TRUE;
      break;
    default:
      GST_LOG_OBJECT (ffmpegdec, "Parser deactivated for format");
      break;
  }

  ffmpegdec->pix_fmt = PIX_FMT_NB;

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    gst_ffmpegviddec_close (ffmpegdec);
    GST_DEBUG_OBJECT (ffmpegdec, "ffdec_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    return FALSE;
  }
}

static gboolean
gst_ffmpegviddec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstFFMpegVidDec *ffmpegdec;
  GstFFMpegVidDecClass *oclass;
  gboolean ret = FALSE;

  ffmpegdec = (GstFFMpegVidDec *) decoder;
  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_DEBUG_OBJECT (ffmpegdec, "setcaps called");

  GST_OBJECT_LOCK (ffmpegdec);
  /* stupid check for VC1 */
  if ((oclass->in_plugin->id == CODEC_ID_WMV3) ||
      (oclass->in_plugin->id == CODEC_ID_VC1))
    oclass->in_plugin->id = gst_ffmpeg_caps_to_codecid (state->caps, NULL);

  /* close old session */
  if (ffmpegdec->opened) {
    GST_OBJECT_UNLOCK (ffmpegdec);
    gst_ffmpegviddec_drain (ffmpegdec);
    GST_OBJECT_LOCK (ffmpegdec);
    gst_ffmpegviddec_close (ffmpegdec);

    /* and reset the defaults that were set when a context is created */
    avcodec_get_context_defaults (ffmpegdec->context);
  }

  /* set buffer functions */
  ffmpegdec->context->get_buffer = gst_ffmpegviddec_get_buffer;
  ffmpegdec->context->release_buffer = gst_ffmpegviddec_release_buffer;
  ffmpegdec->context->draw_horiz_band = NULL;

  ffmpegdec->has_b_frames = FALSE;

  GST_LOG_OBJECT (ffmpegdec, "size %dx%d", ffmpegdec->context->width,
      ffmpegdec->context->height);

  /* FIXME : Create a method that takes GstVideoCodecState instead */
  /* get size and so */
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, state->caps, ffmpegdec->context);

  GST_LOG_OBJECT (ffmpegdec, "size after %dx%d", ffmpegdec->context->width,
      ffmpegdec->context->height);

  if (!ffmpegdec->context->time_base.den || !ffmpegdec->context->time_base.num) {
    GST_DEBUG_OBJECT (ffmpegdec, "forcing 25/1 framerate");
    ffmpegdec->context->time_base.num = 1;
    ffmpegdec->context->time_base.den = 25;
  }

  /* figure out if we can use direct rendering */
  ffmpegdec->current_dr = FALSE;
  ffmpegdec->extra_ref = FALSE;
  if (ffmpegdec->direct_rendering) {
    GST_DEBUG_OBJECT (ffmpegdec, "trying to enable direct rendering");
    if (oclass->in_plugin->capabilities & CODEC_CAP_DR1) {
      if (oclass->in_plugin->id == CODEC_ID_H264) {
        GST_DEBUG_OBJECT (ffmpegdec, "disable direct rendering setup for H264");
        /* does not work, many stuff reads outside of the planes */
        ffmpegdec->current_dr = FALSE;
        ffmpegdec->extra_ref = TRUE;
      } else if ((oclass->in_plugin->id == CODEC_ID_SVQ1) ||
          (oclass->in_plugin->id == CODEC_ID_VP5) ||
          (oclass->in_plugin->id == CODEC_ID_VP6) ||
          (oclass->in_plugin->id == CODEC_ID_VP6F) ||
          (oclass->in_plugin->id == CODEC_ID_VP6A)) {
        GST_DEBUG_OBJECT (ffmpegdec,
            "disable direct rendering setup for broken stride support");
        /* does not work, uses a incompatible stride. See #610613 */
        ffmpegdec->current_dr = FALSE;
        ffmpegdec->extra_ref = TRUE;
      } else {
        GST_DEBUG_OBJECT (ffmpegdec, "enabled direct rendering");
        ffmpegdec->current_dr = TRUE;
      }
    } else {
      GST_DEBUG_OBJECT (ffmpegdec, "direct rendering not supported");
    }
  }
  if (ffmpegdec->current_dr) {
    /* do *not* draw edges when in direct rendering, for some reason it draws
     * outside of the memory. */
    ffmpegdec->context->flags |= CODEC_FLAG_EMU_EDGE;
  }

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;
  ffmpegdec->context->error_recognition = 1;

  /* for slow cpus */
  ffmpegdec->context->lowres = ffmpegdec->lowres;
  ffmpegdec->context->skip_frame = ffmpegdec->skip_frame;

  /* ffmpeg can draw motion vectors on top of the image (not every decoder
   * supports it) */
  ffmpegdec->context->debug_mv = ffmpegdec->debug_mv;

  if (ffmpegdec->max_threads == 0) {
    if (!(oclass->in_plugin->capabilities & CODEC_CAP_AUTO_THREADS))
      ffmpegdec->context->thread_count = gst_ffmpeg_auto_max_threads ();
    else
      ffmpegdec->context->thread_count = 0;
  } else
    ffmpegdec->context->thread_count = ffmpegdec->max_threads;

  ffmpegdec->context->thread_type = FF_THREAD_SLICE;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegviddec_open (ffmpegdec))
    goto open_failed;

  if (ffmpegdec->input_state)
    gst_video_codec_state_unref (ffmpegdec->input_state);
  ffmpegdec->input_state = gst_video_codec_state_ref (state);

  ret = TRUE;

done:
  GST_OBJECT_UNLOCK (ffmpegdec);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to open");
    goto done;
  }
}

static GstFlowReturn
alloc_output_buffer (GstFFMpegVidDec * ffmpegdec, GstVideoCodecFrame * frame)
{
  GstFlowReturn ret;
  gint fsize;

  ret = GST_FLOW_ERROR;

  GST_LOG_OBJECT (ffmpegdec, "alloc output buffer");

  /* see if we need renegotiation */
  if (G_UNLIKELY (!gst_ffmpegviddec_negotiate (ffmpegdec, FALSE)))
    goto negotiate_failed;

  /* get the size of the gstreamer output buffer given a
   * width/height/format */
  fsize = GST_VIDEO_INFO_SIZE (&ffmpegdec->output_state->info);

  if (!ffmpegdec->context->palctrl && ffmpegdec->can_allocate_aligned) {
    GST_LOG_OBJECT (ffmpegdec, "calling pad_alloc");
    /* no pallete, we can use the buffer size to alloc */
    ret =
        gst_video_decoder_alloc_output_frame (GST_VIDEO_DECODER (ffmpegdec),
        frame);
    if (G_UNLIKELY (ret != GST_FLOW_OK))
      goto alloc_failed;

    /* If buffer isn't 128-bit aligned, create a memaligned one ourselves */
    if (((uintptr_t) GST_BUFFER_DATA (frame->output_buffer)) % 16) {
      GST_DEBUG_OBJECT (ffmpegdec,
          "Downstream can't allocate aligned buffers.");
      ffmpegdec->can_allocate_aligned = FALSE;
      gst_buffer_unref (frame->output_buffer);
      frame->output_buffer = new_aligned_buffer (fsize, NULL);
    }
  } else {
    GST_LOG_OBJECT (ffmpegdec,
        "not calling pad_alloc, we have a pallete or downstream can't give 16 byte aligned buffers.");
    /* for paletted data we can't use pad_alloc_buffer(), because
     * fsize contains the size of the palette, so the overall size
     * is bigger than ffmpegcolorspace's unit size, which will
     * prompt GstBaseTransform to complain endlessly ... */
    frame->output_buffer = new_aligned_buffer (fsize, NULL);
    ret = GST_FLOW_OK;
  }

  return ret;

  /* special cases */
negotiate_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "negotiate failed");
    return GST_FLOW_NOT_NEGOTIATED;
  }
alloc_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "pad_alloc failed %d (%s)", ret,
        gst_flow_get_name (ret));
    return ret;
  }
}

static int
gst_ffmpegviddec_get_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstVideoCodecFrame *frame;
  GstFFMpegVidDec *ffmpegdec;
  gint width, height;
  gint coded_width, coded_height;
  gint res;
  gint c;
  GstVideoInfo *info;
  GstFlowReturn ret;


  ffmpegdec = (GstFFMpegVidDec *) context->opaque;

  GST_DEBUG_OBJECT (ffmpegdec, "getting buffer");

  /* apply the last info we have seen to this picture, when we get the
   * picture back from ffmpeg we can use this to correctly timestamp the output
   * buffer */
  picture->reordered_opaque = context->reordered_opaque;

  frame =
      gst_video_decoder_get_frame (GST_VIDEO_DECODER (ffmpegdec),
      picture->reordered_opaque);
  if (G_UNLIKELY (frame == NULL))
    goto no_frame;

  picture->opaque = frame;

  if (!ffmpegdec->current_dr) {
    GST_LOG_OBJECT (ffmpegdec, "direct rendering disabled, fallback alloc");
    res = avcodec_default_get_buffer (context, picture);

    GST_LOG_OBJECT (ffmpegdec, "linsize %d %d %d", picture->linesize[0],
        picture->linesize[1], picture->linesize[2]);
    GST_LOG_OBJECT (ffmpegdec, "data %u %u %u", 0,
        (guint) (picture->data[1] - picture->data[0]),
        (guint) (picture->data[2] - picture->data[0]));
    return res;
  }

  /* take width and height before clipping */
  width = context->width;
  height = context->height;
  coded_width = context->coded_width;
  coded_height = context->coded_height;

  GST_LOG_OBJECT (ffmpegdec, "dimension %dx%d, coded %dx%d", width, height,
      coded_width, coded_height);

  /* this is the size ffmpeg needs for the buffer */
  avcodec_align_dimensions (context, &width, &height);
  GST_LOG_OBJECT (ffmpegdec, "Aligned dimensions %dx%d", width, height);

  if (width != context->width || height != context->height) {
    /* We can't alloc if we need to clip the output buffer later */
    GST_LOG_OBJECT (ffmpegdec, "we need clipping, fallback alloc");
    return avcodec_default_get_buffer (context, picture);
  }

  /* alloc with aligned dimensions for ffmpeg */
  ret = alloc_output_buffer (ffmpegdec, frame);
  if (G_UNLIKELY (ret != GST_FLOW_OK)) {
    /* alloc default buffer when we can't get one from downstream */
    GST_LOG_OBJECT (ffmpegdec, "alloc failed, fallback alloc");
    return avcodec_default_get_buffer (context, picture);
  }

  /* Fill avpicture */
  info = &ffmpegdec->output_state->info;
  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_PLANES (info)) {
      picture->data[c] =
          GST_BUFFER_DATA (frame->output_buffer) +
          GST_VIDEO_INFO_PLANE_OFFSET (info, c);
      picture->linesize[c] = GST_VIDEO_INFO_PLANE_STRIDE (info, c);
    } else {
      picture->data[c] = NULL;
      picture->linesize[c] = 0;
    }
  }
  GST_DEBUG_OBJECT (ffmpegdec, "from GstVideoInfo data %p %p %p",
      picture->data[0], picture->data[1], picture->data[2]);
  GST_DEBUG_OBJECT (ffmpegdec, "from GstVideoInfo linesize %d %d %d",
      picture->linesize[0], picture->linesize[1], picture->linesize[2]);

  /* tell ffmpeg we own this buffer, tranfer the ref we have on the buffer to
   * the opaque data. */
  picture->type = FF_BUFFER_TYPE_USER;
  picture->age = 256 * 256 * 256 * 64;

  GST_LOG_OBJECT (ffmpegdec, "returned frame %p", frame->output_buffer);

  return 0;

no_frame:
  GST_WARNING_OBJECT (ffmpegdec, "Couldn't get codec frame !");
  return -1;
}

static void
gst_ffmpegviddec_release_buffer (AVCodecContext * context, AVFrame * picture)
{
  gint i;
  GstVideoCodecFrame *frame;
  GstFFMpegVidDec *ffmpegdec;

  ffmpegdec = (GstFFMpegVidDec *) context->opaque;
  frame = (GstVideoCodecFrame *) picture->opaque;
  GST_DEBUG_OBJECT (ffmpegdec, "release frame %d", frame->system_frame_number);

  /* check if it was our buffer */
  if (picture->type != FF_BUFFER_TYPE_USER) {
    GST_DEBUG_OBJECT (ffmpegdec, "default release buffer");
    avcodec_default_release_buffer (context, picture);
  }

  /* we remove the opaque data now */
  picture->opaque = NULL;

  gst_video_codec_frame_unref (frame);

  /* zero out the reference in ffmpeg */
  for (i = 0; i < 4; i++) {
    picture->data[i] = NULL;
    picture->linesize[i] = 0;
  }
}

static gboolean
gst_ffmpegviddec_negotiate (GstFFMpegVidDec * ffmpegdec, gboolean force)
{
  GstVideoInfo *info;
  AVCodecContext *context = ffmpegdec->context;
  GstVideoFormat fmt;
  GstVideoCodecState *output_format;

  info = &ffmpegdec->input_state->info;
  if (!force && GST_VIDEO_INFO_WIDTH (info) == context->width
      && GST_VIDEO_INFO_HEIGHT (info) == context->height
      && GST_VIDEO_INFO_PAR_N (info) == context->sample_aspect_ratio.num
      && GST_VIDEO_INFO_PAR_D (info) == context->sample_aspect_ratio.den
      && ffmpegdec->pix_fmt == context->pix_fmt)
    return TRUE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "Renegotiating video from %dx%d@ (PAR %d:%d, %d/%d fps) to %dx%d@ (PAR %d:%d, %d/%d fps)",
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info),
      GST_VIDEO_INFO_PAR_N (info), GST_VIDEO_INFO_PAR_D (info),
      GST_VIDEO_INFO_FPS_N (info), GST_VIDEO_INFO_FPS_D (info),
      context->width, context->height,
      context->sample_aspect_ratio.num,
      context->sample_aspect_ratio.den, -1, -1);

  /* Remember current pix_fmt */
  ffmpegdec->pix_fmt = context->pix_fmt;
  fmt = gst_ffmpeg_pixfmt_to_videoformat (context->pix_fmt);

  if (G_UNLIKELY (fmt == GST_VIDEO_FORMAT_UNKNOWN))
    goto unknown_format;

  output_format =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (ffmpegdec), fmt,
      ffmpegdec->context->width, ffmpegdec->context->height,
      ffmpegdec->input_state);
  if (context->sample_aspect_ratio.num) {
    GST_VIDEO_INFO_PAR_N (&output_format->info) =
        context->sample_aspect_ratio.num;
    GST_VIDEO_INFO_PAR_D (&output_format->info) =
        context->sample_aspect_ratio.den;
  }
  if (ffmpegdec->output_state)
    gst_video_codec_state_unref (ffmpegdec->output_state);
  ffmpegdec->output_state = output_format;

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
    if (ffmpegdec->waiting_for_key)
      goto skipping;
    goto skip_frame;
  }

no_qos:
  return TRUE;

skipping:
  {
    return FALSE;
  }
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

/* figure out if the current picture is a keyframe, return TRUE if that is
 * the case. */
static gboolean
check_keyframe (GstFFMpegVidDec * ffmpegdec)
{
  GstFFMpegVidDecClass *oclass;
  gboolean is_itype = FALSE;
  gboolean is_reference = FALSE;
  gboolean iskeyframe;

  /* figure out if we are dealing with a keyframe */
  oclass = (GstFFMpegVidDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* remember that we have B frames, we need this for the DTS -> PTS conversion
   * code */
  if (!ffmpegdec->has_b_frames && ffmpegdec->picture->pict_type == FF_B_TYPE) {
    GST_DEBUG_OBJECT (ffmpegdec, "we have B frames");
    ffmpegdec->has_b_frames = TRUE;
    /* FIXME : set latency on base video class */
    /* Emit latency message to recalculate it */
    gst_element_post_message (GST_ELEMENT_CAST (ffmpegdec),
        gst_message_new_latency (GST_OBJECT_CAST (ffmpegdec)));
  }

  is_itype = (ffmpegdec->picture->pict_type == FF_I_TYPE);
  is_reference = (ffmpegdec->picture->reference == 1);

  iskeyframe = (is_itype || is_reference || ffmpegdec->picture->key_frame)
      || (oclass->in_plugin->id == CODEC_ID_INDEO3)
      || (oclass->in_plugin->id == CODEC_ID_MSZH)
      || (oclass->in_plugin->id == CODEC_ID_ZLIB)
      || (oclass->in_plugin->id == CODEC_ID_VP3)
      || (oclass->in_plugin->id == CODEC_ID_HUFFYUV);

  GST_LOG_OBJECT (ffmpegdec,
      "current picture: type: %d, is_keyframe:%d, is_itype:%d, is_reference:%d",
      ffmpegdec->picture->pict_type, iskeyframe, is_itype, is_reference);

  return iskeyframe;
}

/* get an outbuf buffer with the current picture */
static GstFlowReturn
get_output_buffer (GstFFMpegVidDec * ffmpegdec, GstVideoCodecFrame * frame)
{
  GstFlowReturn ret = GST_FLOW_OK;
  AVPicture pic, *outpic;
  GstVideoInfo *info;
  gint c;

  GST_LOG_OBJECT (ffmpegdec, "get output buffer");

  ret = alloc_output_buffer (ffmpegdec, frame);
  if (G_UNLIKELY (ret != GST_FLOW_OK))
    goto alloc_failed;

  /* original ffmpeg code does not handle odd sizes correctly.
   * This patched up version does */
  /* Fill avpicture */
  info = &ffmpegdec->output_state->info;
  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_COMPONENTS (info)) {
      pic.data[c] =
          GST_BUFFER_DATA (frame->output_buffer) +
          GST_VIDEO_INFO_COMP_OFFSET (info, c);
      pic.linesize[c] = GST_VIDEO_INFO_COMP_STRIDE (info, c);
    } else {
      pic.data[c] = NULL;
      pic.linesize[c] = 0;
    }
  }

  outpic = (AVPicture *) ffmpegdec->picture;

  GST_LOG_OBJECT (ffmpegdec, "linsize %d %d %d", outpic->linesize[0],
      outpic->linesize[1], outpic->linesize[2]);
  GST_LOG_OBJECT (ffmpegdec, "data %u %u %u", 0,
      (guint) (outpic->data[1] - outpic->data[0]),
      (guint) (outpic->data[2] - outpic->data[0]));

  av_picture_copy (&pic, outpic, ffmpegdec->context->pix_fmt,
      GST_VIDEO_INFO_WIDTH (info), GST_VIDEO_INFO_HEIGHT (info));

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
  gboolean iskeyframe;
  gboolean mode_switch;
  gboolean decode;
  gint skip_frame = AVDISCARD_DEFAULT;
  GstVideoCodecFrame *out_frame;
  AVPacket packet;

  *ret = GST_FLOW_OK;

  ffmpegdec->context->opaque = ffmpegdec;

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
  out_frame = ffmpegdec->picture->opaque;

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
      ffmpegdec->picture->interlaced_frame,
      GST_VIDEO_INFO_IS_INTERLACED (&ffmpegdec->input_state->info));

  if (G_UNLIKELY (ffmpegdec->input_state
          && ffmpegdec->picture->interlaced_frame !=
          GST_VIDEO_INFO_IS_INTERLACED (&ffmpegdec->input_state->info))) {
    GST_WARNING ("Change in interlacing ! picture:%d, recorded:%d",
        ffmpegdec->picture->interlaced_frame,
        GST_VIDEO_INFO_IS_INTERLACED (&ffmpegdec->input_state->info));
    if (ffmpegdec->picture->interlaced_frame)
      GST_VIDEO_INFO_INTERLACE_MODE (&ffmpegdec->input_state->info) =
          GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
    else
      GST_VIDEO_INFO_INTERLACE_MODE (&ffmpegdec->input_state->info) =
          GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    if (!gst_ffmpegviddec_negotiate (ffmpegdec, TRUE))
      goto negotiation_error;
  }

  if (G_UNLIKELY (out_frame->output_buffer == NULL))
    *ret = get_output_buffer (ffmpegdec, out_frame);

  if (G_UNLIKELY (*ret != GST_FLOW_OK))
    goto no_output;

  if (G_UNLIKELY (ffmpegdec->input_state
          && ffmpegdec->picture->interlaced_frame !=
          GST_VIDEO_INFO_IS_INTERLACED (&ffmpegdec->input_state->info))) {
    GST_WARNING ("Change in interlacing ! picture:%d, recorded:%d",
        ffmpegdec->picture->interlaced_frame,
        GST_VIDEO_INFO_IS_INTERLACED (&ffmpegdec->input_state->info));
    if (ffmpegdec->picture->interlaced_frame)
      GST_VIDEO_INFO_INTERLACE_MODE (&ffmpegdec->input_state->info) =
          GST_VIDEO_INTERLACE_MODE_INTERLEAVED;
    else
      GST_VIDEO_INFO_INTERLACE_MODE (&ffmpegdec->input_state->info) =
          GST_VIDEO_INTERLACE_MODE_PROGRESSIVE;
    if (!gst_ffmpegviddec_negotiate (ffmpegdec, TRUE))
      goto negotiation_error;
  }

  /* check if we are dealing with a keyframe here, this will also check if we
   * are dealing with B frames. */
  iskeyframe = check_keyframe (ffmpegdec);

  /* when we're waiting for a keyframe, see if we have one or drop the current
   * non-keyframe */
  if (G_UNLIKELY (ffmpegdec->waiting_for_key)) {
    if (G_LIKELY (!iskeyframe))
      goto drop_non_keyframe;

    /* we have a keyframe, we can stop waiting for one */
    ffmpegdec->waiting_for_key = FALSE;
  }
#if 0
  /* FIXME : How should we properly handle this with base classes */
  frame->n_fields = ffmpegdec->picture->repeat_pict;
#endif

  /* palette is not part of raw video frame in gst and the size
   * of the outgoing buffer needs to be adjusted accordingly */
  if (ffmpegdec->context->palctrl != NULL)
    GST_BUFFER_SIZE (out_frame->output_buffer) -= AVPALETTE_SIZE;

  /* mark as keyframe or delta unit */
  if (ffmpegdec->picture->top_field_first)
    GST_VIDEO_CODEC_FRAME_FLAG_SET (out_frame, GST_VIDEO_CODEC_FRAME_FLAG_TFF);


  *ret =
      gst_video_decoder_finish_frame (GST_VIDEO_DECODER (ffmpegdec), out_frame);

beach:
  GST_DEBUG_OBJECT (ffmpegdec, "return flow %d, len %d", *ret, len);
  return len;

  /* special cases */
drop_non_keyframe:
  {
    GST_WARNING_OBJECT (ffmpegdec, "Dropping non-keyframe (seek/init)");
    gst_video_decoder_drop_frame (GST_VIDEO_DECODER (ffmpegdec), out_frame);
    goto beach;
  }

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
        "ffdec_%s: decoding error (len: %d, have_data: %d)",
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
  gint size, bsize, len, have_data;
  GstFlowReturn ret = GST_FLOW_OK;

  /* do early keyframe check pretty bad to rely on the keyframe flag in the
   * source for this as it might not even be parsed (UDP/file/..).  */
  if (G_UNLIKELY (ffmpegdec->waiting_for_key)) {
    GST_DEBUG_OBJECT (ffmpegdec, "waiting for keyframe");
    if (!GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
      goto skip_keyframe;

    GST_DEBUG_OBJECT (ffmpegdec, "got keyframe");
    ffmpegdec->waiting_for_key = FALSE;
  }

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %u, pts:%"
      GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (frame->input_buffer),
      GST_TIME_ARGS (frame->pts), GST_TIME_ARGS (frame->duration));

  bdata = GST_BUFFER_DATA (frame->input_buffer);
  bsize = GST_BUFFER_SIZE (frame->input_buffer);

  if (ffmpegdec->do_padding) {
    /* add padding */
    if (ffmpegdec->padded_size < bsize + FF_INPUT_BUFFER_PADDING_SIZE) {
      ffmpegdec->padded_size = bsize + FF_INPUT_BUFFER_PADDING_SIZE;
      ffmpegdec->padded = g_realloc (ffmpegdec->padded, ffmpegdec->padded_size);
      GST_LOG_OBJECT (ffmpegdec, "resized padding buffer to %d",
          ffmpegdec->padded_size);
    }
    memcpy (ffmpegdec->padded, bdata, bsize);
    memset (ffmpegdec->padded + bsize, 0, FF_INPUT_BUFFER_PADDING_SIZE);

    bdata = ffmpegdec->padded;
  }

  do {
    guint8 tmp_padding[FF_INPUT_BUFFER_PADDING_SIZE];

    /* parse, if at all possible */
    data = bdata;
    size = bsize;

    if (ffmpegdec->do_padding) {
      /* add temporary padding */
      memcpy (tmp_padding, data + size, FF_INPUT_BUFFER_PADDING_SIZE);
      memset (data + size, 0, FF_INPUT_BUFFER_PADDING_SIZE);
    }

    /* decode a frame of audio/video now */
    len =
        gst_ffmpegviddec_frame (ffmpegdec, data, size, &have_data, frame, &ret);

    if (ffmpegdec->do_padding) {
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

    GST_LOG_OBJECT (ffmpegdec, "Before (while bsize>0).  bsize:%d , bdata:%p",
        bsize, bdata);
  } while (bsize > 0);

  if (bsize > 0)
    GST_DEBUG_OBJECT (ffmpegdec, "Dropping %d bytes of data", bsize);

  return ret;

  /* ERRORS */
skip_keyframe:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "skipping non keyframe");
    return gst_video_decoder_drop_frame (decoder, frame);
  }
}

static gboolean
gst_ffmpegviddec_stop (GstVideoDecoder * decoder)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;

  GST_OBJECT_LOCK (ffmpegdec);
  gst_ffmpegviddec_close (ffmpegdec);
  GST_OBJECT_UNLOCK (ffmpegdec);
  g_free (ffmpegdec->padded);
  ffmpegdec->padded = NULL;
  ffmpegdec->padded_size = 0;
  ffmpegdec->can_allocate_aligned = TRUE;
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
gst_ffmpegviddec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstFFMpegVidDec *ffmpegdec = (GstFFMpegVidDec *) decoder;

  if (ffmpegdec->opened) {
    if (!hard)
      gst_ffmpegviddec_drain (ffmpegdec);
    avcodec_flush_buffers (ffmpegdec->context);
  }

  return TRUE;
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
    case PROP_DO_PADDING:
      ffmpegdec->do_padding = g_value_get_boolean (value);
      break;
    case PROP_DEBUG_MV:
      ffmpegdec->debug_mv = ffmpegdec->context->debug_mv =
          g_value_get_boolean (value);
      break;
    case PROP_CROP:
      ffmpegdec->crop = g_value_get_boolean (value);
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
    case PROP_DO_PADDING:
      g_value_set_boolean (value, ffmpegdec->do_padding);
      break;
    case PROP_DEBUG_MV:
      g_value_set_boolean (value, ffmpegdec->context->debug_mv);
      break;
    case PROP_CROP:
      g_value_set_boolean (value, ffmpegdec->crop);
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

    /* only decoders */
    if (!in_plugin->decode) {
      goto next;
    }

    /* no quasi-codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
        in_plugin->id == CODEC_ID_V210 ||
        in_plugin->id == CODEC_ID_V210X ||
        in_plugin->id == CODEC_ID_R210 ||
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_BLURAY)) {
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
    type_name = g_strdup_printf ("ffdec_%s", plugin_name);
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
      case CODEC_ID_MPEG4:
      case CODEC_ID_MSMPEG4V3:
      case CODEC_ID_H264:
      case CODEC_ID_RV10:
      case CODEC_ID_RV20:
      case CODEC_ID_RV30:
      case CODEC_ID_RV40:
        rank = GST_RANK_PRIMARY;
        break;
        /* DVVIDEO: we have a good dv decoder, fast on both ppc as well as x86.
         * They say libdv's quality is better though. leave as secondary.
         * note: if you change this, see the code in gstdv.c in good/ext/dv.
         */
      case CODEC_ID_DVVIDEO:
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
