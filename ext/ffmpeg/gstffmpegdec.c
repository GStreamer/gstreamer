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

#include <libavcodec/avcodec.h>

#include <gst/gst.h>

#include "gstffmpeg.h"
#include "gstffmpegcodecmap.h"
#include "gstffmpegutils.h"

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

typedef struct _GstFFMpegAudDec GstFFMpegAudDec;

#define MAX_TS_MASK 0xff

/* for each incomming buffer we keep all timing info in a structure like this.
 * We keep a circular array of these structures around to store the timing info.
 * The index in the array is what we pass as opaque data (to pictures) and
 * pts (to parsers) so that ffmpeg can remember them for us. */
typedef struct
{
  gint idx;
  GstClockTime dts;
  GstClockTime pts;
  GstClockTime duration;
  gint64 offset;
} GstTSInfo;

struct _GstFFMpegAudDec
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  /* decoding */
  AVCodecContext *context;
  gboolean opened;

  /* current output format */
  gint channels, samplerate, depth;
  GstAudioChannelPosition ffmpeg_layout[64], gst_layout[64];

  gboolean discont;
  gboolean clear_ts;

  /* for tracking DTS/PTS */
  GstClockTime next_out;

  /* parsing */
  gboolean turnoff_parser;      /* used for turning off aac raw parsing
                                 * See bug #566250 */
  AVCodecParserContext *pctx;
  GstBuffer *pcache;

  /* clipping segment */
  GstSegment segment;

  GstTSInfo ts_info[MAX_TS_MASK + 1];
  gint ts_idx;

  /* reverse playback queue */
  GList *queued;

  /* prevent reopening the decoder on GST_EVENT_CAPS when caps are same as last time. */
  GstCaps *last_caps;
};

typedef struct _GstFFMpegAudDecClass GstFFMpegAudDecClass;

struct _GstFFMpegAudDecClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

#define GST_TS_INFO_NONE &ts_info_none
static const GstTSInfo ts_info_none = { -1, -1, -1, -1 };

static const GstTSInfo *
gst_ts_info_store (GstFFMpegAudDec * dec, GstClockTime dts, GstClockTime pts,
    GstClockTime duration, gint64 offset)
{
  gint idx = dec->ts_idx;
  dec->ts_info[idx].idx = idx;
  dec->ts_info[idx].dts = dts;
  dec->ts_info[idx].pts = pts;
  dec->ts_info[idx].duration = duration;
  dec->ts_info[idx].offset = offset;
  dec->ts_idx = (idx + 1) & MAX_TS_MASK;

  return &dec->ts_info[idx];
}

static const GstTSInfo *
gst_ts_info_get (GstFFMpegAudDec * dec, gint idx)
{
  if (G_UNLIKELY (idx < 0 || idx > MAX_TS_MASK))
    return GST_TS_INFO_NONE;

  return &dec->ts_info[idx];
}

#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegauddec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegAudDec))
#define GST_FFMPEGAUDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegAudDecClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGAUDDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

/* A number of function prototypes are given so we can refer to them later. */
static void gst_ffmpegauddec_base_init (GstFFMpegAudDecClass * klass);
static void gst_ffmpegauddec_class_init (GstFFMpegAudDecClass * klass);
static void gst_ffmpegauddec_init (GstFFMpegAudDec * ffmpegdec);
static void gst_ffmpegauddec_finalize (GObject * object);

static gboolean gst_ffmpegauddec_setcaps (GstFFMpegAudDec * ffmpegdec,
    GstCaps * caps);
static gboolean gst_ffmpegauddec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_ffmpegauddec_sink_query (GstPad * pad, GstObject * parent,
    GstQuery * query);
static GstFlowReturn gst_ffmpegauddec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buf);

static GstStateChangeReturn gst_ffmpegauddec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_ffmpegauddec_negotiate (GstFFMpegAudDec * ffmpegdec,
    gboolean force);

static void gst_ffmpegauddec_drain (GstFFMpegAudDec * ffmpegdec);

#define GST_FFDEC_PARAMS_QDATA g_quark_from_static_string("avdec-params")

static GstElementClass *parent_class = NULL;

static void
gst_ffmpegauddec_base_init (GstFFMpegAudDecClass * klass)
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
      "Codec/Decoder/Audio", description,
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
  srccaps = gst_ffmpeg_codectype_to_audio_caps (NULL,
      in_plugin->id, FALSE, in_plugin);
  if (!srccaps) {
    GST_DEBUG ("Couldn't get source caps for decoder '%s'", in_plugin->name);
    srccaps = gst_caps_from_string ("unknown/unknown");
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
}

static void
gst_ffmpegauddec_class_init (GstFFMpegAudDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_ffmpegauddec_finalize;

  gstelement_class->change_state = gst_ffmpegauddec_change_state;
}

static void
gst_ffmpegauddec_init (GstFFMpegAudDec * ffmpegdec)
{
  GstFFMpegAudDecClass *oclass;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* setup pads */
  ffmpegdec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_query_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegauddec_sink_query));
  gst_pad_set_event_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegauddec_sink_event));
  gst_pad_set_chain_function (ffmpegdec->sinkpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegauddec_chain));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);

  ffmpegdec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_fixed_caps (ffmpegdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->srcpad);

  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context ();
  ffmpegdec->pctx = NULL;
  ffmpegdec->pcache = NULL;
  ffmpegdec->opened = FALSE;

  gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);
}

static void
gst_ffmpegauddec_finalize (GObject * object)
{
  GstFFMpegAudDec *ffmpegdec = (GstFFMpegAudDec *) object;

  if (ffmpegdec->context != NULL)
    av_free (ffmpegdec->context);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_ffmpegauddec_reset_ts (GstFFMpegAudDec * ffmpegdec)
{
  ffmpegdec->next_out = GST_CLOCK_TIME_NONE;
}

/* with LOCK */
static void
gst_ffmpegauddec_close (GstFFMpegAudDec * ffmpegdec)
{
  if (!ffmpegdec->opened)
    return;

  GST_LOG_OBJECT (ffmpegdec, "closing libav codec");

  gst_caps_replace (&ffmpegdec->last_caps, NULL);

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

  if (ffmpegdec->pctx) {
    if (ffmpegdec->pcache) {
      gst_buffer_unref (ffmpegdec->pcache);
      ffmpegdec->pcache = NULL;
    }
    av_parser_close (ffmpegdec->pctx);
    ffmpegdec->pctx = NULL;
  }
}

/* with LOCK */
static gboolean
gst_ffmpegauddec_open (GstFFMpegAudDec * ffmpegdec)
{
  GstFFMpegAudDecClass *oclass;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (gst_ffmpeg_avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0)
    goto could_not_open;

  ffmpegdec->opened = TRUE;

  GST_LOG_OBJECT (ffmpegdec, "Opened libav codec %s, id %d",
      oclass->in_plugin->name, oclass->in_plugin->id);

  if (!ffmpegdec->turnoff_parser) {
    ffmpegdec->pctx = av_parser_init (oclass->in_plugin->id);
    if (ffmpegdec->pctx)
      GST_LOG_OBJECT (ffmpegdec, "Using parser %p", ffmpegdec->pctx);
    else
      GST_LOG_OBJECT (ffmpegdec, "No parser for codec");
  } else {
    GST_LOG_OBJECT (ffmpegdec, "Parser deactivated for format");
  }

  ffmpegdec->samplerate = 0;
  ffmpegdec->channels = 0;
  ffmpegdec->depth = 0;

  gst_ffmpegauddec_reset_ts (ffmpegdec);

  return TRUE;

  /* ERRORS */
could_not_open:
  {
    gst_ffmpegauddec_close (ffmpegdec);
    GST_DEBUG_OBJECT (ffmpegdec, "avdec_%s: Failed to open libav codec",
        oclass->in_plugin->name);
    return FALSE;
  }
}

static gboolean
gst_ffmpegauddec_setcaps (GstFFMpegAudDec * ffmpegdec, GstCaps * caps)
{
  GstFFMpegAudDecClass *oclass;
  GstStructure *structure;
  gboolean ret = TRUE;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  GST_DEBUG_OBJECT (ffmpegdec, "setcaps called");

  GST_OBJECT_LOCK (ffmpegdec);

  /* close old session */
  if (ffmpegdec->opened) {
    GST_OBJECT_UNLOCK (ffmpegdec);
    gst_ffmpegauddec_drain (ffmpegdec);
    GST_OBJECT_LOCK (ffmpegdec);
    gst_ffmpegauddec_close (ffmpegdec);

    /* and reset the defaults that were set when a context is created */
    avcodec_get_context_defaults (ffmpegdec->context);
  }

  /* default is to let format decide if it needs a parser */
  ffmpegdec->turnoff_parser = FALSE;

  /* get size and so */
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, caps, ffmpegdec->context);

  /* get pixel aspect ratio if it's set */
  structure = gst_caps_get_structure (caps, 0);

  /* for AAC we only use av_parse if not on stream-format==raw or ==loas */
  if (oclass->in_plugin->id == CODEC_ID_AAC
      || oclass->in_plugin->id == CODEC_ID_AAC_LATM) {
    const gchar *format = gst_structure_get_string (structure, "stream-format");

    if (format == NULL || strcmp (format, "raw") == 0) {
      ffmpegdec->turnoff_parser = TRUE;
    }
  }

  /* for FLAC, don't parse if it's already parsed */
  if (oclass->in_plugin->id == CODEC_ID_FLAC) {
    if (gst_structure_has_field (structure, "streamheader"))
      ffmpegdec->turnoff_parser = TRUE;
  }

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;
  ffmpegdec->context->error_recognition = 1;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegauddec_open (ffmpegdec))
    goto open_failed;

done:
  GST_OBJECT_UNLOCK (ffmpegdec);

  return ret;

  /* ERRORS */
open_failed:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "Failed to open");
    ret = FALSE;
    goto done;
  }
}

static gboolean
gst_ffmpegauddec_negotiate (GstFFMpegAudDec * ffmpegdec, gboolean force)
{
  GstFFMpegAudDecClass *oclass;
  GstCaps *caps;
  gint depth;
  GstAudioChannelPosition pos[64] = { 0, };

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  depth = av_smp_format_depth (ffmpegdec->context->sample_fmt);
  gst_ffmpeg_channel_layout_to_gst (ffmpegdec->context, pos);

  if (!force && ffmpegdec->samplerate ==
      ffmpegdec->context->sample_rate &&
      ffmpegdec->channels == ffmpegdec->context->channels &&
      ffmpegdec->depth == depth)
    return TRUE;

  GST_DEBUG_OBJECT (ffmpegdec,
      "Renegotiating audio from %dHz@%dchannels (%d) to %dHz@%dchannels (%d)",
      ffmpegdec->samplerate, ffmpegdec->channels,
      ffmpegdec->depth,
      ffmpegdec->context->sample_rate, ffmpegdec->context->channels, depth);

  ffmpegdec->samplerate = ffmpegdec->context->sample_rate;
  ffmpegdec->channels = ffmpegdec->context->channels;
  ffmpegdec->depth = depth;
  memcpy (ffmpegdec->ffmpeg_layout, pos,
      sizeof (GstAudioChannelPosition) * ffmpegdec->context->channels);

  /* Get GStreamer channel layout */
  memcpy (ffmpegdec->gst_layout,
      ffmpegdec->ffmpeg_layout,
      sizeof (GstAudioChannelPosition) * ffmpegdec->channels);
  gst_audio_channel_positions_to_valid_order (ffmpegdec->gst_layout,
      ffmpegdec->channels);

  caps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type,
      ffmpegdec->context, oclass->in_plugin->id, FALSE);

  if (caps == NULL)
    goto no_caps;

  GST_LOG_OBJECT (ffmpegdec, "output caps %" GST_PTR_FORMAT, caps);

  if (!gst_pad_set_caps (ffmpegdec->srcpad, caps))
    goto caps_failed;

  gst_caps_unref (caps);

  return TRUE;

  /* ERRORS */
no_caps:
  {
#ifdef HAVE_LIBAV_UNINSTALLED
    /* using internal ffmpeg snapshot */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for libav codec '%s'.",
            oclass->in_plugin->name), (NULL));
#else
    /* using external ffmpeg */
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION,
        ("Could not find GStreamer caps mapping for libav codec '%s', and "
            "you are using an external libavcodec. This is most likely due to "
            "a packaging problem and/or libavcodec having been upgraded to a "
            "version that is not compatible with this version of "
            "gstreamer-libav. Make sure your gstreamer-libav and libavcodec "
            "packages come from the same source/repository.",
            oclass->in_plugin->name), (NULL));
#endif
    return FALSE;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("Could not set caps for libav decoder (%s), not fixed?",
            oclass->in_plugin->name));
    gst_caps_unref (caps);

    return FALSE;
  }
}

static void
clear_queued (GstFFMpegAudDec * ffmpegdec)
{
  g_list_foreach (ffmpegdec->queued, (GFunc) gst_mini_object_unref, NULL);
  g_list_free (ffmpegdec->queued);
  ffmpegdec->queued = NULL;
}

static GstFlowReturn
flush_queued (GstFFMpegAudDec * ffmpegdec)
{
  GstFlowReturn res = GST_FLOW_OK;

  while (ffmpegdec->queued) {
    GstBuffer *buf = GST_BUFFER_CAST (ffmpegdec->queued->data);

    GST_LOG_OBJECT (ffmpegdec, "pushing buffer %p, offset %"
        G_GUINT64_FORMAT ", timestamp %"
        GST_TIME_FORMAT ", duration %" GST_TIME_FORMAT, buf,
        GST_BUFFER_OFFSET (buf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

    /* iterate ouput queue an push downstream */
    res = gst_pad_push (ffmpegdec->srcpad, buf);

    ffmpegdec->queued =
        g_list_delete_link (ffmpegdec->queued, ffmpegdec->queued);
  }
  return res;
}

static void
gst_avpacket_init (AVPacket * packet, guint8 * data, guint size)
{
  memset (packet, 0, sizeof (AVPacket));
  packet->data = data;
  packet->size = size;
}

/* returns TRUE if buffer is within segment, else FALSE.
 * if Buffer is on segment border, it's timestamp and duration will be clipped */
static gboolean
clip_audio_buffer (GstFFMpegAudDec * dec, GstBuffer * buf, GstClockTime in_ts,
    GstClockTime in_dur)
{
  GstClockTime stop;
  gint64 diff;
  guint64 ctime, cstop;
  gboolean res = TRUE;
  gsize size, offset;

  size = gst_buffer_get_size (buf);
  offset = 0;

  GST_LOG_OBJECT (dec,
      "timestamp:%" GST_TIME_FORMAT ", duration:%" GST_TIME_FORMAT
      ", size %" G_GSIZE_FORMAT, GST_TIME_ARGS (in_ts), GST_TIME_ARGS (in_dur),
      size);

  /* can't clip without TIME segment */
  if (G_UNLIKELY (dec->segment.format != GST_FORMAT_TIME))
    goto beach;

  /* we need a start time */
  if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (in_ts)))
    goto beach;

  /* trust duration */
  stop = in_ts + in_dur;

  res = gst_segment_clip (&dec->segment, GST_FORMAT_TIME, in_ts, stop, &ctime,
      &cstop);
  if (G_UNLIKELY (!res))
    goto out_of_segment;

  /* see if some clipping happened */
  if (G_UNLIKELY ((diff = ctime - in_ts) > 0)) {
    /* bring clipped time to bytes */
    diff =
        gst_util_uint64_scale_int (diff, dec->samplerate,
        GST_SECOND) * (dec->depth * dec->channels);

    GST_DEBUG_OBJECT (dec, "clipping start to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (ctime), diff);

    offset += diff;
    size -= diff;
  }
  if (G_UNLIKELY ((diff = stop - cstop) > 0)) {
    /* bring clipped time to bytes */
    diff =
        gst_util_uint64_scale_int (diff, dec->samplerate,
        GST_SECOND) * (dec->depth * dec->channels);

    GST_DEBUG_OBJECT (dec, "clipping stop to %" GST_TIME_FORMAT " %"
        G_GINT64_FORMAT " bytes", GST_TIME_ARGS (cstop), diff);

    size -= diff;
  }
  gst_buffer_resize (buf, offset, size);
  GST_BUFFER_TIMESTAMP (buf) = ctime;
  GST_BUFFER_DURATION (buf) = cstop - ctime;

beach:
  GST_LOG_OBJECT (dec, "%sdropping", (res ? "not " : ""));
  return res;

  /* ERRORS */
out_of_segment:
  {
    GST_LOG_OBJECT (dec, "out of segment");
    goto beach;
  }
}

static gint
gst_ffmpegauddec_audio_frame (GstFFMpegAudDec * ffmpegdec,
    AVCodec * in_plugin, guint8 * data, guint size,
    const GstTSInfo * dec_info, GstBuffer ** outbuf, GstFlowReturn * ret)
{
  gint len = -1;
  gint have_data = AVCODEC_MAX_AUDIO_FRAME_SIZE;
  GstClockTime out_pts, out_duration;
  GstMapInfo map;
  gint64 out_offset;
  int16_t *odata;
  AVPacket packet;

  GST_DEBUG_OBJECT (ffmpegdec,
      "size:%d, offset:%" G_GINT64_FORMAT ", dts:%" GST_TIME_FORMAT ", pts:%"
      GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT ", ffmpegdec->next_out:%"
      GST_TIME_FORMAT, size, dec_info->offset, GST_TIME_ARGS (dec_info->dts),
      GST_TIME_ARGS (dec_info->pts), GST_TIME_ARGS (dec_info->duration),
      GST_TIME_ARGS (ffmpegdec->next_out));

  *outbuf = new_aligned_buffer (AVCODEC_MAX_AUDIO_FRAME_SIZE);

  gst_buffer_map (*outbuf, &map, GST_MAP_WRITE);
  odata = (int16_t *) map.data;

  gst_avpacket_init (&packet, data, size);
  len = avcodec_decode_audio3 (ffmpegdec->context, odata, &have_data, &packet);

  GST_DEBUG_OBJECT (ffmpegdec,
      "Decode audio: len=%d, have_data=%d", len, have_data);

  if (len >= 0 && have_data > 0) {
    GstAudioFormat fmt;

    /* Buffer size */
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_resize (*outbuf, 0, have_data);

    GST_DEBUG_OBJECT (ffmpegdec, "Creating output buffer");
    if (!gst_ffmpegauddec_negotiate (ffmpegdec, FALSE)) {
      gst_buffer_unref (*outbuf);
      *outbuf = NULL;
      len = -1;
      goto beach;
    }

    /*
     * Timestamps:
     *
     *  1) Copy input timestamp if valid
     *  2) else interpolate from previous input timestamp
     */
    /* always take timestamps from the input buffer if any */
    if (GST_CLOCK_TIME_IS_VALID (dec_info->pts)) {
      out_pts = dec_info->pts;
    } else {
      out_pts = ffmpegdec->next_out;
    }

    /*
     * Duration:
     *
     *  1) calculate based on number of samples
     */
    out_duration = gst_util_uint64_scale (have_data, GST_SECOND,
        ffmpegdec->depth * ffmpegdec->channels * ffmpegdec->samplerate);

    /* offset:
     *
     * Just copy
     */
    out_offset = dec_info->offset;

    GST_DEBUG_OBJECT (ffmpegdec,
        "Buffer created. Size:%d , pts:%" GST_TIME_FORMAT " , duration:%"
        GST_TIME_FORMAT, have_data,
        GST_TIME_ARGS (out_pts), GST_TIME_ARGS (out_duration));

    GST_BUFFER_PTS (*outbuf) = out_pts;
    GST_BUFFER_DURATION (*outbuf) = out_duration;
    GST_BUFFER_OFFSET (*outbuf) = out_offset;

    /* the next timestamp we'll use when interpolating */
    if (GST_CLOCK_TIME_IS_VALID (out_pts))
      ffmpegdec->next_out = out_pts + out_duration;

    /* now see if we need to clip the buffer against the segment boundaries. */
    if (G_UNLIKELY (!clip_audio_buffer (ffmpegdec, *outbuf, out_pts,
                out_duration)))
      goto clipped;


    /* Reorder channels to the GStreamer channel order */
    /* Only the width really matters here... and it's stored as depth */
    fmt =
        gst_audio_format_build_integer (TRUE, G_BYTE_ORDER,
        ffmpegdec->depth * 8, ffmpegdec->depth * 8);

    gst_audio_buffer_reorder_channels (*outbuf, fmt,
        ffmpegdec->channels, ffmpegdec->ffmpeg_layout, ffmpegdec->gst_layout);
  } else {
    gst_buffer_unmap (*outbuf, &map);
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
  }

beach:
  GST_DEBUG_OBJECT (ffmpegdec, "return flow %d, out %p, len %d",
      *ret, *outbuf, len);
  return len;

  /* ERRORS */
clipped:
  {
    GST_DEBUG_OBJECT (ffmpegdec, "buffer clipped");
    gst_buffer_unref (*outbuf);
    *outbuf = NULL;
    goto beach;
  }
}

/* gst_ffmpegauddec_frame:
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
gst_ffmpegauddec_frame (GstFFMpegAudDec * ffmpegdec,
    guint8 * data, guint size, gint * got_data, const GstTSInfo * dec_info,
    GstFlowReturn * ret)
{
  GstFFMpegAudDecClass *oclass;
  GstBuffer *outbuf = NULL;
  gint have_data = 0, len = 0;

  if (G_UNLIKELY (ffmpegdec->context->codec == NULL))
    goto no_codec;

  GST_LOG_OBJECT (ffmpegdec, "data:%p, size:%d, id:%d", data, size,
      dec_info->idx);

  *ret = GST_FLOW_OK;
  ffmpegdec->context->frame_number++;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  len =
      gst_ffmpegauddec_audio_frame (ffmpegdec, oclass->in_plugin, data, size,
      dec_info, &outbuf, ret);

  /* if we did not get an output buffer and we have a pending discont, don't
   * clear the input timestamps, we will put them on the next buffer because
   * else we might create the first buffer with a very big timestamp gap. */
  if (outbuf == NULL && ffmpegdec->discont) {
    GST_DEBUG_OBJECT (ffmpegdec, "no buffer but keeping timestamp");
    ffmpegdec->clear_ts = FALSE;
  }

  if (outbuf)
    have_data = 1;

  if (len < 0 || have_data < 0) {
    GST_WARNING_OBJECT (ffmpegdec,
        "avdec_%s: decoding error (len: %d, have_data: %d)",
        oclass->in_plugin->name, len, have_data);
    *got_data = 0;
    goto beach;
  } else if (len == 0 && have_data == 0) {
    *got_data = 0;
    goto beach;
  } else {
    /* this is where I lost my last clue on ffmpeg... */
    *got_data = 1;
  }

  if (outbuf) {
    GST_LOG_OBJECT (ffmpegdec,
        "Decoded data, now pushing buffer %p with offset %" G_GINT64_FORMAT
        ", timestamp %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT,
        outbuf, GST_BUFFER_OFFSET (outbuf),
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (outbuf)),
        GST_TIME_ARGS (GST_BUFFER_DURATION (outbuf)));

    /* mark pending discont */
    if (ffmpegdec->discont) {
      GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DISCONT);
      ffmpegdec->discont = FALSE;
    }
    if (ffmpegdec->segment.rate > 0.0) {
      /* and off we go */
      *ret = gst_pad_push (ffmpegdec->srcpad, outbuf);
    } else {
      /* reverse playback, queue frame till later when we get a discont. */
      GST_DEBUG_OBJECT (ffmpegdec, "queued frame");
      ffmpegdec->queued = g_list_prepend (ffmpegdec->queued, outbuf);
      *ret = GST_FLOW_OK;
    }
  } else {
    GST_DEBUG_OBJECT (ffmpegdec, "We didn't get a decoded buffer");
  }

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
gst_ffmpegauddec_drain (GstFFMpegAudDec * ffmpegdec)
{
  GstFFMpegAudDecClass *oclass;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  if (oclass->in_plugin->capabilities & CODEC_CAP_DELAY) {
    gint have_data, len, try = 0;

    GST_LOG_OBJECT (ffmpegdec,
        "codec has delay capabilities, calling until libav has drained everything");

    do {
      GstFlowReturn ret;

      len =
          gst_ffmpegauddec_frame (ffmpegdec, NULL, 0, &have_data, &ts_info_none,
          &ret);
      if (len < 0 || have_data == 0)
        break;
    } while (try++ < 10);
  }
  if (ffmpegdec->segment.rate < 0.0) {
    /* if we have some queued frames for reverse playback, flush them now */
    flush_queued (ffmpegdec);
  }
}

static void
gst_ffmpegauddec_flush_pcache (GstFFMpegAudDec * ffmpegdec)
{
  if (ffmpegdec->pctx) {
    gint size, bsize;
    guint8 *data;
    guint8 bdata[FF_INPUT_BUFFER_PADDING_SIZE];

    bsize = FF_INPUT_BUFFER_PADDING_SIZE;
    memset (bdata, 0, bsize);

    /* parse some dummy data to work around some ffmpeg weirdness where it keeps
     * the previous pts around */
    av_parser_parse2 (ffmpegdec->pctx, ffmpegdec->context,
        &data, &size, bdata, bsize, -1, -1, -1);
    ffmpegdec->pctx->pts = -1;
    ffmpegdec->pctx->dts = -1;
  }

  if (ffmpegdec->pcache) {
    gst_buffer_unref (ffmpegdec->pcache);
    ffmpegdec->pcache = NULL;
  }
}

static gboolean
gst_ffmpegauddec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFFMpegAudDec *ffmpegdec;
  gboolean ret = FALSE;

  ffmpegdec = (GstFFMpegAudDec *) parent;

  GST_DEBUG_OBJECT (ffmpegdec, "Handling %s event",
      GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
    {
      gst_ffmpegauddec_drain (ffmpegdec);
      break;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      if (ffmpegdec->opened) {
        avcodec_flush_buffers (ffmpegdec->context);
      }
      gst_ffmpegauddec_reset_ts (ffmpegdec);
      gst_ffmpegauddec_flush_pcache (ffmpegdec);
      gst_segment_init (&ffmpegdec->segment, GST_FORMAT_TIME);
      clear_queued (ffmpegdec);
      break;
    }
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      if (!ffmpegdec->last_caps
          || !gst_caps_is_equal (ffmpegdec->last_caps, caps)) {
        ret = gst_ffmpegauddec_setcaps (ffmpegdec, caps);
        if (ret) {
          gst_caps_replace (&ffmpegdec->last_caps, caps);
        }
      } else {
        ret = TRUE;
      }

      gst_event_unref (event);
      goto done;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment segment;

      gst_event_copy_segment (event, &segment);

      switch (segment.format) {
        case GST_FORMAT_TIME:
          /* fine, our native segment format */
          break;
        case GST_FORMAT_BYTES:
        {
          gint bit_rate;

          bit_rate = ffmpegdec->context->bit_rate;

          /* convert to time or fail */
          if (!bit_rate)
            goto no_bitrate;

          GST_DEBUG_OBJECT (ffmpegdec, "bitrate: %d", bit_rate);

          /* convert values to TIME */
          if (segment.start != -1)
            segment.start =
                gst_util_uint64_scale_int (segment.start, GST_SECOND, bit_rate);
          if (segment.stop != -1)
            segment.stop =
                gst_util_uint64_scale_int (segment.stop, GST_SECOND, bit_rate);
          if (segment.time != -1)
            segment.time =
                gst_util_uint64_scale_int (segment.time, GST_SECOND, bit_rate);

          /* unref old event */
          gst_event_unref (event);

          /* create new converted time segment */
          segment.format = GST_FORMAT_TIME;
          /* FIXME, bitrate is not good enough too find a good stop, let's
           * hope start and time were 0... meh. */
          segment.stop = -1;
          event = gst_event_new_segment (&segment);
          break;
        }
        default:
          /* invalid format */
          goto invalid_format;
      }

      GST_DEBUG_OBJECT (ffmpegdec, "SEGMENT in time %" GST_SEGMENT_FORMAT,
          &segment);

      /* and store the values */
      gst_segment_copy_into (&segment, &ffmpegdec->segment);
      break;
    }
    default:
      break;
  }

  /* and push segment downstream */
  ret = gst_pad_push_event (ffmpegdec->srcpad, event);

done:

  return ret;

  /* ERRORS */
no_bitrate:
  {
    GST_WARNING_OBJECT (ffmpegdec, "no bitrate to convert BYTES to TIME");
    gst_event_unref (event);
    goto done;
  }
invalid_format:
  {
    GST_WARNING_OBJECT (ffmpegdec, "unknown format received in NEWSEGMENT");
    gst_event_unref (event);
    goto done;
  }
}

static gboolean
gst_ffmpegauddec_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstFFMpegAudDec *ffmpegdec;
  gboolean ret = FALSE;

  ffmpegdec = (GstFFMpegAudDec *) parent;

  GST_DEBUG_OBJECT (ffmpegdec, "Handling %s query",
      GST_QUERY_TYPE_NAME (query));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstPadTemplate *templ;

      ret = FALSE;
      if ((templ = GST_PAD_PAD_TEMPLATE (pad))) {
        GstCaps *tcaps;

        if ((tcaps = GST_PAD_TEMPLATE_CAPS (templ))) {
          GstCaps *caps;

          gst_query_parse_accept_caps (query, &caps);
          gst_query_set_accept_caps_result (query,
              gst_caps_is_subset (caps, tcaps));
          ret = TRUE;
        }
      }
      break;
    }
    default:
      ret = gst_pad_query_default (pad, parent, query);
      break;
  }
  return ret;
}

static GstFlowReturn
gst_ffmpegauddec_chain (GstPad * pad, GstObject * parent, GstBuffer * inbuf)
{
  GstFFMpegAudDec *ffmpegdec;
  GstFFMpegAudDecClass *oclass;
  guint8 *data, *bdata;
  GstMapInfo map;
  gint size, bsize, len, have_data;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime in_pts, in_dts, in_duration;
  gboolean discont;
  gint64 in_offset;
  const GstTSInfo *in_info;
  const GstTSInfo *dec_info;

  ffmpegdec = (GstFFMpegAudDec *) parent;

  if (G_UNLIKELY (!ffmpegdec->opened))
    goto not_negotiated;

  discont = GST_BUFFER_IS_DISCONT (inbuf);

  /* The discont flags marks a buffer that is not continuous with the previous
   * buffer. This means we need to clear whatever data we currently have. We let
   * ffmpeg continue with the data that it has. We currently drain the old
   * frames that might be inside the decoder and we clear any partial data in
   * the pcache, we might be able to remove the drain and flush too. */
  if (G_UNLIKELY (discont)) {
    GST_DEBUG_OBJECT (ffmpegdec, "received DISCONT");
    /* drain what we have queued */
    gst_ffmpegauddec_drain (ffmpegdec);
    gst_ffmpegauddec_flush_pcache (ffmpegdec);
    ffmpegdec->discont = TRUE;
    gst_ffmpegauddec_reset_ts (ffmpegdec);
  }
  /* by default we clear the input timestamp after decoding each frame so that
   * interpollation can work. */
  ffmpegdec->clear_ts = TRUE;

  oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* parse cache joining. If there is cached data */
  if (ffmpegdec->pcache) {
    /* join with previous data */
    GST_LOG_OBJECT (ffmpegdec, "join parse cache");
    inbuf = gst_buffer_append (ffmpegdec->pcache, inbuf);
    /* no more cached data, we assume we can consume the complete cache */
    ffmpegdec->pcache = NULL;
  }

  in_dts = GST_BUFFER_DTS (inbuf);
  in_pts = GST_BUFFER_PTS (inbuf);
  in_duration = GST_BUFFER_DURATION (inbuf);
  in_offset = GST_BUFFER_OFFSET (inbuf);

  /* get handle to timestamp info, we can pass this around to ffmpeg */
  in_info =
      gst_ts_info_store (ffmpegdec, in_dts, in_pts, in_duration, in_offset);

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %u, offset:%" G_GUINT64_FORMAT ", ts:%"
      GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT ", info %d",
      gst_buffer_get_size (inbuf), GST_BUFFER_OFFSET (inbuf),
      GST_TIME_ARGS (in_pts), GST_TIME_ARGS (in_duration), in_info->idx);

  /* workarounds, functions write to buffers:
   *  libavcodec/svq1.c:svq1_decode_frame writes to the given buffer.
   *  libavcodec/svq3.c:svq3_decode_slice_header too.
   * ffmpeg devs know about it and will fix it (they said). */
  if (oclass->in_plugin->id == CODEC_ID_SVQ1 ||
      oclass->in_plugin->id == CODEC_ID_SVQ3) {
    inbuf = gst_buffer_make_writable (inbuf);
  }

  gst_buffer_map (inbuf, &map, GST_MAP_READ);

  bdata = map.data;
  bsize = map.size;

  GST_LOG_OBJECT (ffmpegdec,
      "Received new data of size %u, offset:%" G_GUINT64_FORMAT ", dts:%"
      GST_TIME_FORMAT ", pts:%" GST_TIME_FORMAT ", dur:%" GST_TIME_FORMAT
      ", info %d", bsize, in_offset, GST_TIME_ARGS (in_dts),
      GST_TIME_ARGS (in_pts), GST_TIME_ARGS (in_duration), in_info->idx);

  do {
    /* parse, if at all possible */
    if (ffmpegdec->pctx) {
      gint res;

      GST_LOG_OBJECT (ffmpegdec,
          "Calling av_parser_parse2 with offset %" G_GINT64_FORMAT ", ts:%"
          GST_TIME_FORMAT " size %d", in_offset, GST_TIME_ARGS (in_pts), bsize);

      /* feed the parser. We pass the timestamp info so that we can recover all
       * info again later */
      res = av_parser_parse2 (ffmpegdec->pctx, ffmpegdec->context,
          &data, &size, bdata, bsize, in_info->idx, in_info->idx, in_offset);

      GST_LOG_OBJECT (ffmpegdec,
          "parser returned res %d and size %d, id %" G_GINT64_FORMAT, res, size,
          (gint64) ffmpegdec->pctx->pts);

      /* store pts for decoding */
      if (ffmpegdec->pctx->pts != AV_NOPTS_VALUE && ffmpegdec->pctx->pts != -1)
        dec_info = gst_ts_info_get (ffmpegdec, ffmpegdec->pctx->pts);
      else {
        /* ffmpeg sometimes loses track after a flush, help it by feeding a
         * valid start time */
        ffmpegdec->pctx->pts = in_info->idx;
        ffmpegdec->pctx->dts = in_info->idx;
        dec_info = in_info;
      }

      GST_LOG_OBJECT (ffmpegdec, "consuming %d bytes. id %d", size,
          dec_info->idx);

      if (res) {
        /* there is output, set pointers for next round. */
        bsize -= res;
        bdata += res;
      } else {
        /* Parser did not consume any data, make sure we don't clear the
         * timestamp for the next round */
        ffmpegdec->clear_ts = FALSE;
      }

      /* if there is no output, we must break and wait for more data. also the
       * timestamp in the context is not updated. */
      if (size == 0) {
        if (bsize > 0)
          continue;
        else
          break;
      }
    } else {
      data = bdata;
      size = bsize;

      dec_info = in_info;
    }

    /* decode a frame of audio now */
    len =
        gst_ffmpegauddec_frame (ffmpegdec, data, size, &have_data, dec_info,
        &ret);

    if (ret != GST_FLOW_OK) {
      GST_LOG_OBJECT (ffmpegdec, "breaking because of flow ret %s",
          gst_flow_get_name (ret));
      /* bad flow return, make sure we discard all data and exit */
      bsize = 0;
      break;
    }
    if (!ffmpegdec->pctx) {
      if (len == 0 && !have_data) {
        /* nothing was decoded, this could be because no data was available or
         * because we were skipping frames.
         * If we have no context we must exit and wait for more data, we keep the
         * data we tried. */
        GST_LOG_OBJECT (ffmpegdec, "Decoding didn't return any data, breaking");
        break;
      } else if (len < 0) {
        /* a decoding error happened, we must break and try again with next data. */
        GST_LOG_OBJECT (ffmpegdec, "Decoding error, breaking");
        bsize = 0;
        break;
      }
      /* prepare for the next round, for codecs with a context we did this
       * already when using the parser. */
      bsize -= len;
      bdata += len;
    } else {
      if (len == 0) {
        /* nothing was decoded, this could be because no data was available or
         * because we were skipping frames. Since we have a parser we can
         * continue with the next frame */
        GST_LOG_OBJECT (ffmpegdec,
            "Decoding didn't return any data, trying next");
      } else if (len < 0) {
        /* we have a context that will bring us to the next frame */
        GST_LOG_OBJECT (ffmpegdec, "Decoding error, trying next");
      }
    }

    /* make sure we don't use the same old timestamp for the next frame and let
     * the interpollation take care of it. */
    if (ffmpegdec->clear_ts) {
      in_dts = GST_CLOCK_TIME_NONE;
      in_pts = GST_CLOCK_TIME_NONE;
      in_duration = GST_CLOCK_TIME_NONE;
      in_offset = GST_BUFFER_OFFSET_NONE;
      in_info = GST_TS_INFO_NONE;
    } else {
      ffmpegdec->clear_ts = TRUE;
    }

    GST_LOG_OBJECT (ffmpegdec, "Before (while bsize>0).  bsize:%d , bdata:%p",
        bsize, bdata);
  } while (bsize > 0);

  gst_buffer_unmap (inbuf, &map);

  /* keep left-over */
  if (ffmpegdec->pctx && bsize > 0) {
    in_pts = GST_BUFFER_PTS (inbuf);
    in_dts = GST_BUFFER_DTS (inbuf);
    in_offset = GST_BUFFER_OFFSET (inbuf);

    GST_LOG_OBJECT (ffmpegdec,
        "Keeping %d bytes of data with offset %" G_GINT64_FORMAT ", pts %"
        GST_TIME_FORMAT, bsize, in_offset, GST_TIME_ARGS (in_pts));

    ffmpegdec->pcache = gst_buffer_copy_region (inbuf, GST_BUFFER_COPY_ALL,
        gst_buffer_get_size (inbuf) - bsize, bsize);
    /* we keep timestamp, even though all we really know is that the correct
     * timestamp is not below the one from inbuf */
    GST_BUFFER_PTS (ffmpegdec->pcache) = in_pts;
    GST_BUFFER_DTS (ffmpegdec->pcache) = in_dts;
    GST_BUFFER_OFFSET (ffmpegdec->pcache) = in_offset;
  } else if (bsize > 0) {
    GST_DEBUG_OBJECT (ffmpegdec, "Dropping %d bytes of data", bsize);
  }
  gst_buffer_unref (inbuf);

  return ret;

  /* ERRORS */
not_negotiated:
  {
    oclass = (GstFFMpegAudDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("avdec_%s: input format was not set before data start",
            oclass->in_plugin->name));
    gst_buffer_unref (inbuf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstStateChangeReturn
gst_ffmpegauddec_change_state (GstElement * element, GstStateChange transition)
{
  GstFFMpegAudDec *ffmpegdec = (GstFFMpegAudDec *) element;
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_OBJECT_LOCK (ffmpegdec);
      gst_ffmpegauddec_close (ffmpegdec);
      GST_OBJECT_UNLOCK (ffmpegdec);
      clear_queued (ffmpegdec);
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_ffmpegauddec_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegAudDecClass),
    (GBaseInitFunc) gst_ffmpegauddec_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegauddec_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegAudDec),
    0,
    (GInstanceInitFunc) gst_ffmpegauddec_init,
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
    if (!in_plugin->decode || in_plugin->type != AVMEDIA_TYPE_AUDIO) {
      goto next;
    }

    /* no quasi-codecs, please */
    if (in_plugin->id >= CODEC_ID_PCM_S16LE &&
        in_plugin->id <= CODEC_ID_PCM_BLURAY) {
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

    GST_DEBUG ("Trying plugin %s [%s]", in_plugin->name, in_plugin->long_name);

    /* no codecs for which we're GUARANTEED to have better alternatives */
    /* MP1 : Use MP3 for decoding */
    /* MP2 : Use MP3 for decoding */
    /* Theora: Use libtheora based theoradec */
    if (!strcmp (in_plugin->name, "vorbis") ||
        !strcmp (in_plugin->name, "wavpack") ||
        !strcmp (in_plugin->name, "mp1") ||
        !strcmp (in_plugin->name, "mp2") ||
        !strcmp (in_plugin->name, "libfaad") ||
        !strcmp (in_plugin->name, "mpeg4aac") ||
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
      type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);
      g_type_set_qdata (type, GST_FFDEC_PARAMS_QDATA, (gpointer) in_plugin);
    }

    /* (Ronald) MPEG-4 gets a higher priority because it has been well-
     * tested and by far outperforms divxdec/xviddec - so we prefer it.
     * msmpeg4v3 same, as it outperforms divxdec for divx3 playback.
     * VC1/WMV3 are not working and thus unpreferred for now. */
    switch (in_plugin->id) {
      case CODEC_ID_RA_144:
      case CODEC_ID_RA_288:
      case CODEC_ID_COOK:
        rank = GST_RANK_PRIMARY;
        break;
        /* SIPR: decoder should have a higher rank than realaudiodec.
         */
      case CODEC_ID_SIPR:
        rank = GST_RANK_SECONDARY;
        break;
      case CODEC_ID_MP3:
        rank = GST_RANK_NONE;
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
