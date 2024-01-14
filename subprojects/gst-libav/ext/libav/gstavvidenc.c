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
#include <libavutil/opt.h>

#include "gstav.h"
#include "gstavcodecmap.h"
#include "gstavutils.h"
#include "gstavvidenc.h"
#include "gstavcfg.h"


enum
{
  PROP_0,
  PROP_QUANTIZER,
  PROP_PASS,
  PROP_FILENAME,
  PROP_CFG_BASE,
};

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

#define GST_TYPE_FFMPEG_PASS (gst_ffmpeg_pass_get_type ())
static GType
gst_ffmpeg_pass_get_type (void)
{
  static GType ffmpeg_pass_type = 0;

  if (!ffmpeg_pass_type) {
    static const GEnumValue ffmpeg_passes[] = {
      {0, "Constant Bitrate Encoding", "cbr"},
      {AV_CODEC_FLAG_QSCALE, "Constant Quantizer", "quant"},
      {AV_CODEC_FLAG_PASS1, "VBR Encoding - Pass 1", "pass1"},
      {AV_CODEC_FLAG_PASS2, "VBR Encoding - Pass 2", "pass2"},
      {0, NULL, NULL},
    };

    ffmpeg_pass_type =
        g_enum_register_static ("GstLibAVEncPass", ffmpeg_passes);
  }

  return ffmpeg_pass_type;
}

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

  gobject_class = (GObjectClass *) klass;
  venc_class = (GstVideoEncoderClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_ffmpegvidenc_set_property;
  gobject_class->get_property = gst_ffmpegvidenc_get_property;

  g_object_class_install_property (gobject_class, PROP_QUANTIZER,
      g_param_spec_float ("quantizer", "Constant Quantizer",
          "Constant Quantizer", 0, 30, 0.01f,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_PASS,
      g_param_spec_enum ("pass", "Encoding pass/type",
          "Encoding pass/type", GST_TYPE_FFMPEG_PASS, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  g_object_class_install_property (gobject_class, PROP_FILENAME,
      g_param_spec_string ("multipass-cache-file", "Multipass Cache File",
          "Filename for multipass cache file", "stats.log",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT));

  /* register additional properties, possibly dependent on the exact CODEC */
  gst_ffmpeg_cfg_install_properties (gobject_class, klass->in_plugin,
      PROP_CFG_BASE, AV_OPT_FLAG_ENCODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM);

  venc_class->start = gst_ffmpegvidenc_start;
  venc_class->stop = gst_ffmpegvidenc_stop;
  venc_class->finish = gst_ffmpegvidenc_finish;
  venc_class->handle_frame = gst_ffmpegvidenc_handle_frame;
  venc_class->set_format = gst_ffmpegvidenc_set_format;
  venc_class->propose_allocation = gst_ffmpegvidenc_propose_allocation;
  venc_class->flush = gst_ffmpegvidenc_flush;

  gobject_class->finalize = gst_ffmpegvidenc_finalize;

  gst_type_mark_as_plugin_api (GST_TYPE_FFMPEG_PASS, 0);
}

static void
gst_ffmpegvidenc_init (GstFFMpegVidEnc * ffmpegenc)
{
  GstFFMpegVidEncClass *klass =
      (GstFFMpegVidEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  GST_PAD_SET_ACCEPT_TEMPLATE (GST_VIDEO_ENCODER_SINK_PAD (ffmpegenc));

  ffmpegenc->context = avcodec_alloc_context3 (klass->in_plugin);
  ffmpegenc->refcontext = avcodec_alloc_context3 (klass->in_plugin);
  ffmpegenc->picture = av_frame_alloc ();
  ffmpegenc->opened = FALSE;
  ffmpegenc->file = NULL;
}

static void
gst_ffmpegvidenc_finalize (GObject * object)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) object;

  /* clean up remaining allocated data */
  av_frame_free (&ffmpegenc->picture);
  gst_ffmpeg_avcodec_close (ffmpegenc->context);
  gst_ffmpeg_avcodec_close (ffmpegenc->refcontext);
  av_freep (&ffmpegenc->context);
  av_freep (&ffmpegenc->refcontext);
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

  ffmpegenc->need_reopen = FALSE;

  /* close old session */
  if (ffmpegenc->opened) {
    avcodec_free_context (&ffmpegenc->context);
    ffmpegenc->opened = FALSE;
    ffmpegenc->context = avcodec_alloc_context3 (oclass->in_plugin);
    if (ffmpegenc->context == NULL) {
      GST_DEBUG_OBJECT (ffmpegenc, "Failed to set context defaults");
      return FALSE;
    }
  }

  /* additional avcodec settings */
  gst_ffmpeg_cfg_fill_context (G_OBJECT (ffmpegenc), ffmpegenc->context);

  if (GST_VIDEO_INFO_IS_INTERLACED (&state->info))
    ffmpegenc->context->flags |=
        AV_CODEC_FLAG_INTERLACED_DCT | AV_CODEC_FLAG_INTERLACED_ME;

  /* and last but not least the pass; CBR, 2-pass, etc */
  ffmpegenc->context->flags |= ffmpegenc->pass;
  switch (ffmpegenc->pass) {
      /* some additional action depends on type of pass */
    case AV_CODEC_FLAG_QSCALE:
      ffmpegenc->context->global_quality
          = ffmpegenc->picture->quality = FF_QP2LAMBDA * ffmpegenc->quantizer;
      break;
    case AV_CODEC_FLAG_PASS1:  /* need to prepare a stats file */
      /* we don't close when changing caps, fingers crossed */
      if (!ffmpegenc->file)
        ffmpegenc->file = g_fopen (ffmpegenc->filename, "w");
      if (!ffmpegenc->file)
        goto open_file_err;
      break;
    case AV_CODEC_FLAG_PASS2:
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
  ffmpegenc->pts_offset = GST_CLOCK_TIME_NONE;
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
    avcodec_free_context (&ffmpegenc->context);
    ffmpegenc->context = avcodec_alloc_context3 (oclass->in_plugin);
    if (ffmpegenc->context == NULL)
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
  g_free (pkt);
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
  g_free (info);
}

static enum AVStereo3DType
stereo_gst_to_av (GstVideoMultiviewMode mview_mode)
{
  switch (mview_mode) {
    case GST_VIDEO_MULTIVIEW_MODE_MONO:
      /* Video is not stereoscopic (and metadata has to be there). */
      return AV_STEREO3D_2D;
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

static void
gst_ffmpegvidenc_add_cc (GstBuffer * buffer, AVFrame * picture)
{
  GstVideoCaptionMeta *cc_meta;
  gpointer iter = NULL;

  while ((cc_meta =
          (GstVideoCaptionMeta *) gst_buffer_iterate_meta_filtered (buffer,
              &iter, GST_VIDEO_CAPTION_META_API_TYPE))) {
    AVFrameSideData *sd;

    if (cc_meta->caption_type != GST_VIDEO_CAPTION_TYPE_CEA708_RAW)
      continue;

    sd = av_frame_new_side_data (picture, AV_FRAME_DATA_A53_CC, cc_meta->size);
    memcpy (sd->data, cc_meta->data, cc_meta->size);
  }
}

static GstFlowReturn
gst_ffmpegvidenc_send_frame (GstFFMpegVidEnc * ffmpegenc,
    GstVideoCodecFrame * frame)
{
  GstVideoInfo *info = &ffmpegenc->input_state->info;
  BufferInfo *buffer_info;
  guint c;
  gint res;
  GstFlowReturn ret = GST_FLOW_ERROR;
  AVFrame *picture = NULL;

  if (!frame)
    goto send_frame;

  picture = ffmpegenc->picture;

  gst_ffmpegvidenc_add_cc (frame->input_buffer, picture);

  if (GST_VIDEO_INFO_IS_INTERLACED (&ffmpegenc->input_state->info)) {
    const gboolean top_field_first =
        GST_BUFFER_FLAG_IS_SET (frame->input_buffer, GST_VIDEO_BUFFER_FLAG_TFF)
        || GST_VIDEO_INFO_FIELD_ORDER (&ffmpegenc->input_state->info) ==
        GST_VIDEO_FIELD_ORDER_TOP_FIELD_FIRST;
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 31, 100)
    picture->flags |= AV_FRAME_FLAG_INTERLACED;
    if (top_field_first) {
      picture->flags |= AV_FRAME_FLAG_TOP_FIELD_FIRST;
    }
#else
    picture->interlaced_frame = TRUE;
    picture->top_field_first = top_field_first;
#endif
    picture->repeat_pict =
        GST_BUFFER_FLAG_IS_SET (frame->input_buffer, GST_VIDEO_BUFFER_FLAG_RFF);
  }

  if (GST_VIDEO_INFO_MULTIVIEW_MODE (info) != GST_VIDEO_MULTIVIEW_MODE_NONE) {
    AVStereo3D *stereo = av_stereo3d_create_side_data (picture);
    stereo->type = stereo_gst_to_av (GST_VIDEO_INFO_MULTIVIEW_MODE (info));

    if (GST_VIDEO_INFO_MULTIVIEW_FLAGS (info) &
        GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST) {
      stereo->flags = AV_STEREO3D_FLAG_INVERT;
    }
  }

  if (GST_VIDEO_CODEC_FRAME_IS_FORCE_KEYFRAME (frame))
    picture->pict_type = AV_PICTURE_TYPE_I;

  buffer_info = g_new0 (BufferInfo, 1);
  buffer_info->buffer = gst_buffer_ref (frame->input_buffer);

  if (!gst_video_frame_map (&buffer_info->vframe, info, frame->input_buffer,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (ffmpegenc, "Failed to map input buffer");
    gst_buffer_unref (buffer_info->buffer);
    g_free (buffer_info);
    gst_video_codec_frame_unref (frame);
    goto done;
  }

  /* Fill avpicture */
  picture->buf[0] =
      av_buffer_create (NULL, 0, buffer_info_free, buffer_info, 0);
  for (c = 0; c < AV_NUM_DATA_POINTERS; c++) {
    if (c < GST_VIDEO_INFO_N_COMPONENTS (info)) {
      picture->data[c] = GST_VIDEO_FRAME_PLANE_DATA (&buffer_info->vframe, c);
      picture->linesize[c] =
          GST_VIDEO_FRAME_COMP_STRIDE (&buffer_info->vframe, c);
    } else {
      picture->data[c] = NULL;
      picture->linesize[c] = 0;
    }
  }

  picture->format = ffmpegenc->context->pix_fmt;
  picture->width = GST_VIDEO_FRAME_WIDTH (&buffer_info->vframe);
  picture->height = GST_VIDEO_FRAME_HEIGHT (&buffer_info->vframe);

  if (ffmpegenc->pts_offset == GST_CLOCK_TIME_NONE) {
    ffmpegenc->pts_offset = frame->pts;
  }

  if (frame->pts == GST_CLOCK_TIME_NONE) {
    picture->pts = AV_NOPTS_VALUE;
  } else if (frame->pts < ffmpegenc->pts_offset) {
    GST_ERROR_OBJECT (ffmpegenc, "PTS is going backwards");
    picture->pts = AV_NOPTS_VALUE;
  } else {
#if LIBAVCODEC_VERSION_INT >= AV_VERSION_INT(60, 31, 100)
    const gint ticks_per_frame = (ffmpegenc->context->codec_descriptor
        && ffmpegenc->context->
        codec_descriptor->props & AV_CODEC_PROP_FIELDS) ? 2 : 1;
#else
    const gint ticks_per_frame = ffmpegenc->context->ticks_per_frame;
#endif
    picture->pts =
        gst_ffmpeg_time_gst_to_ff ((frame->pts - ffmpegenc->pts_offset) /
        ticks_per_frame, ffmpegenc->context->time_base);
  }

send_frame:
  if (!picture) {
    GstFFMpegVidEncClass *oclass =
        (GstFFMpegVidEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));

    /* If AV_CODEC_CAP_ENCODER_FLUSH wasn't set, we need to re-open
     * encoder */
    if (!(oclass->in_plugin->capabilities & AV_CODEC_CAP_ENCODER_FLUSH)) {
      GST_DEBUG_OBJECT (ffmpegenc, "Encoder needs reopen later");

      /* we will reopen later handle_frame() */
      ffmpegenc->need_reopen = TRUE;
    }
  }

  res = avcodec_send_frame (ffmpegenc->context, picture);

  if (picture)
    av_frame_unref (picture);

  if (res == 0)
    ret = GST_FLOW_OK;
  else if (res == AVERROR_EOF)
    ret = GST_FLOW_EOS;

done:
  return ret;
}

static GstFlowReturn
gst_ffmpegvidenc_receive_packet (GstFFMpegVidEnc * ffmpegenc,
    gboolean * got_packet, gboolean send)
{
  AVPacket *pkt;
  GstBuffer *outbuf;
  GstVideoCodecFrame *frame;
  gint res;
  GstFlowReturn ret = GST_FLOW_OK;

  *got_packet = FALSE;

  pkt = g_new0 (AVPacket, 1);

  res = avcodec_receive_packet (ffmpegenc->context, pkt);

  if (res == AVERROR (EAGAIN)) {
    g_free (pkt);
    goto done;
  } else if (res == AVERROR_EOF) {
    g_free (pkt);
    ret = GST_FLOW_EOS;
    goto done;
  } else if (res < 0) {
    ret = GST_FLOW_ERROR;
    goto done;
  }

  *got_packet = TRUE;

  /* save stats info if there is some as well as a stats file */
  if (ffmpegenc->file && ffmpegenc->context->stats_out)
    if (fprintf (ffmpegenc->file, "%s", ffmpegenc->context->stats_out) < 0)
      GST_ELEMENT_ERROR (ffmpegenc, RESOURCE, WRITE,
          (("Could not write to file \"%s\"."), ffmpegenc->filename),
          GST_ERROR_SYSTEM);

  /* Get oldest frame */
  frame = gst_video_encoder_get_oldest_frame (GST_VIDEO_ENCODER (ffmpegenc));

  if (send) {
    outbuf =
        gst_buffer_new_wrapped_full (GST_MEMORY_FLAG_READONLY, pkt->data,
        pkt->size, 0, pkt->size, pkt, gst_ffmpegvidenc_free_avpacket);
    frame->output_buffer = outbuf;

    if (pkt->flags & AV_PKT_FLAG_KEY)
      GST_VIDEO_CODEC_FRAME_SET_SYNC_POINT (frame);
    else
      GST_VIDEO_CODEC_FRAME_UNSET_SYNC_POINT (frame);
  }

  /* calculate the DTS by taking the PTS/DTS difference from the ffmpeg side
   * and applying it to our PTS. We don't use the ffmpeg timestamps verbatim
   * because they're too inaccurate and in the framerate time_base
   */
  if (pkt->dts != AV_NOPTS_VALUE) {
    gint64 pts_dts_diff = pkt->dts - pkt->pts;
    if (pts_dts_diff < 0) {
      GstClockTime gst_pts_dts_diff = gst_ffmpeg_time_ff_to_gst (-pts_dts_diff,
          ffmpegenc->context->time_base);

      if (gst_pts_dts_diff > frame->pts)
        frame->pts = 0;
      else
        frame->dts = frame->pts - gst_pts_dts_diff;
    } else {
      frame->dts = frame->pts +
          gst_ffmpeg_time_ff_to_gst (pts_dts_diff,
          ffmpegenc->context->time_base);
    }
  }

  ret = gst_video_encoder_finish_frame (GST_VIDEO_ENCODER (ffmpegenc), frame);

done:
  return ret;
}

static GstFlowReturn
gst_ffmpegvidenc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;
  GstFlowReturn ret;
  gboolean got_packet;

  /* endoder was drained or flushed, and ffmpeg encoder doesn't support
   * flushing. We need to re-open encoder then */
  if (ffmpegenc->need_reopen) {
    gboolean reopen_ret;
    GstVideoCodecState *input_state;

    GST_DEBUG_OBJECT (ffmpegenc, "Open encoder again");

    if (!ffmpegenc->input_state) {
      GST_ERROR_OBJECT (ffmpegenc,
          "Cannot re-open encoder without input state");
      return GST_FLOW_NOT_NEGOTIATED;
    }

    input_state = gst_video_codec_state_ref (ffmpegenc->input_state);
    reopen_ret = gst_ffmpegvidenc_set_format (encoder, input_state);
    gst_video_codec_state_unref (input_state);

    if (!reopen_ret) {
      GST_ERROR_OBJECT (ffmpegenc, "Couldn't re-open encoder");
      return GST_FLOW_NOT_NEGOTIATED;
    }
  }

  ret = gst_ffmpegvidenc_send_frame (ffmpegenc, frame);

  if (ret != GST_FLOW_OK)
    goto encode_fail;

  gst_video_codec_frame_unref (frame);

  do {
    ret = gst_ffmpegvidenc_receive_packet (ffmpegenc, &got_packet, TRUE);
    if (ret != GST_FLOW_OK)
      break;
  } while (got_packet);

done:
  return ret;

  /* We choose to be error-resilient */
encode_fail:
  {
#ifndef GST_DISABLE_GST_DEBUG
    GstFFMpegVidEncClass *oclass =
        (GstFFMpegVidEncClass *) (G_OBJECT_GET_CLASS (ffmpegenc));
    GST_ERROR_OBJECT (ffmpegenc,
        "avenc_%s: failed to encode buffer", oclass->in_plugin->name);
#endif /* GST_DISABLE_GST_DEBUG */
    /* avoid frame (and ts etc) piling up */
    ret = gst_video_encoder_finish_frame (encoder, frame);
    goto done;
  }
}

static GstFlowReturn
gst_ffmpegvidenc_flush_buffers (GstFFMpegVidEnc * ffmpegenc, gboolean send)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gboolean got_packet;

  GST_DEBUG_OBJECT (ffmpegenc, "flushing buffers with sending %d", send);

  /* no need to empty codec if there is none */
  if (!ffmpegenc->opened)
    goto done;

  ret = gst_ffmpegvidenc_send_frame (ffmpegenc, NULL);

  if (ret != GST_FLOW_OK)
    goto done;

  do {
    ret = gst_ffmpegvidenc_receive_packet (ffmpegenc, &got_packet, send);
    if (ret != GST_FLOW_OK)
      break;
  } while (got_packet);
  avcodec_flush_buffers (ffmpegenc->context);
  ffmpegenc->pts_offset = GST_CLOCK_TIME_NONE;

done:
  /* FFMpeg will return AVERROR_EOF if it's internal was fully drained
   * then we are translating it to GST_FLOW_EOS. However, because this behavior
   * is fully internal stuff of this implementation and gstvideoencoder
   * baseclass doesn't convert this GST_FLOW_EOS to GST_FLOW_OK,
   * convert this flow returned here */
  if (ret == GST_FLOW_EOS)
    ret = GST_FLOW_OK;

  return ret;
}

static void
gst_ffmpegvidenc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidEnc *ffmpegenc;

  ffmpegenc = (GstFFMpegVidEnc *) (object);

  if (ffmpegenc->opened) {
    GST_WARNING_OBJECT (ffmpegenc,
        "Can't change properties once decoder is setup !");
    return;
  }

  switch (prop_id) {
    case PROP_QUANTIZER:
      ffmpegenc->quantizer = g_value_get_float (value);
      break;
    case PROP_PASS:
      ffmpegenc->pass = g_value_get_enum (value);
      break;
    case PROP_FILENAME:
      g_free (ffmpegenc->filename);
      ffmpegenc->filename = g_value_dup_string (value);
      break;
    default:
      if (!gst_ffmpeg_cfg_set_property (ffmpegenc->refcontext, value, pspec))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ffmpegvidenc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstFFMpegVidEnc *ffmpegenc;

  ffmpegenc = (GstFFMpegVidEnc *) (object);

  switch (prop_id) {
    case PROP_QUANTIZER:
      g_value_set_float (value, ffmpegenc->quantizer);
      break;
    case PROP_PASS:
      g_value_set_enum (value, ffmpegenc->pass);
      break;
    case PROP_FILENAME:
      g_value_take_string (value, g_strdup (ffmpegenc->filename));
      break;
    default:
      if (!gst_ffmpeg_cfg_get_property (ffmpegenc->refcontext, value, pspec))
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_ffmpegvidenc_flush (GstVideoEncoder * encoder)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;

  if (ffmpegenc->opened) {
    avcodec_flush_buffers (ffmpegenc->context);
    ffmpegenc->pts_offset = GST_CLOCK_TIME_NONE;
  }

  return TRUE;
}

static gboolean
gst_ffmpegvidenc_start (GstVideoEncoder * encoder)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;
  GstFFMpegVidEncClass *oclass =
      (GstFFMpegVidEncClass *) G_OBJECT_GET_CLASS (ffmpegenc);

  ffmpegenc->opened = FALSE;
  ffmpegenc->need_reopen = FALSE;

  /* close old session */
  avcodec_free_context (&ffmpegenc->context);
  ffmpegenc->context = avcodec_alloc_context3 (oclass->in_plugin);
  if (ffmpegenc->context == NULL) {
    GST_DEBUG_OBJECT (ffmpegenc, "Failed to set context defaults");
    return FALSE;
  }

  gst_video_encoder_set_min_pts (encoder, GST_SECOND * 60 * 60 * 1000);

  return TRUE;
}

static gboolean
gst_ffmpegvidenc_stop (GstVideoEncoder * encoder)
{
  GstFFMpegVidEnc *ffmpegenc = (GstFFMpegVidEnc *) encoder;

  gst_ffmpegvidenc_flush_buffers (ffmpegenc, FALSE);
  gst_ffmpeg_avcodec_close (ffmpegenc->context);
  ffmpegenc->opened = FALSE;
  ffmpegenc->need_reopen = FALSE;

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
  void *i = 0;

  GST_LOG ("Registering encoders");

  while ((in_plugin = (AVCodec *) av_codec_iterate (&i))) {
    gchar *type_name;

    /* Skip non-AV codecs */
    if (in_plugin->type != AVMEDIA_TYPE_VIDEO)
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

    /* No encoders depending on external libraries (we don't build them, but
     * people who build against an external ffmpeg might have them.
     * We have native gstreamer plugins for all of those libraries anyway. */
    if (!strncmp (in_plugin->name, "lib", 3)) {
      GST_DEBUG
          ("Not using external library encoder %s. Use the gstreamer-native ones instead.",
          in_plugin->name);
      continue;
    }

    /* Skip hardware or hybrid (hardware with software fallback) */
    if ((in_plugin->capabilities & AV_CODEC_CAP_HARDWARE) ==
        AV_CODEC_CAP_HARDWARE) {
      GST_DEBUG
          ("Ignoring hardware encoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    if ((in_plugin->capabilities & AV_CODEC_CAP_HYBRID) == AV_CODEC_CAP_HYBRID) {
      GST_DEBUG
          ("Ignoring hybrid encoder %s. We can't handle this outside of ffmpeg",
          in_plugin->name);
      continue;
    }

    /* only video encoders */
    if (!av_codec_is_encoder (in_plugin)
        || in_plugin->type != AVMEDIA_TYPE_VIDEO)
      continue;

    /* FIXME : We should have a method to know cheaply whether we have a mapping
     * for the given plugin or not */

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    if (!strcmp (in_plugin->name, "gif")) {
      GST_LOG ("Ignoring encoder %s", in_plugin->name);
      continue;
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
  }

  GST_LOG ("Finished registering encoders");

  return TRUE;
}
