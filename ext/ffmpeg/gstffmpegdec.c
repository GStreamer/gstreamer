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

typedef struct _GstFFMpegDec GstFFMpegDec;

struct _GstFFMpegDec
{
  GstElement element;

  /* We need to keep track of our pads, so we do so here. */
  GstPad *srcpad;
  GstPad *sinkpad;

  /* decoding */
  AVCodecContext *context;
  AVFrame *picture;
  gboolean opened;
  union {
    struct {
      gint width, height, fps, fps_base;
    } video;
    struct {
      gint channels, samplerate;
    } audio;
  } format;
  guint64 next_ts;

  /* parsing */
  AVCodecParserContext *pctx;
  GstBuffer *pcache;

  GValue *par;		/* pixel aspect ratio of incoming data */
};

typedef struct _GstFFMpegDecClass GstFFMpegDecClass;

struct _GstFFMpegDecClass
{
  GstElementClass parent_class;

  AVCodec *in_plugin;
  GstPadTemplate *srctempl, *sinktempl;
};

typedef struct _GstFFMpegDecClassParams GstFFMpegDecClassParams;

struct _GstFFMpegDecClassParams
{
  AVCodec *in_plugin;
  GstCaps *srccaps, *sinkcaps;
};

#define GST_TYPE_FFMPEGDEC \
  (gst_ffmpegdec_get_type())
#define GST_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDEC,GstFFMpegDec))
#define GST_FFMPEGDEC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDEC,GstFFMpegDecClass))
#define GST_IS_FFMPEGDEC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDEC))
#define GST_IS_FFMPEGDEC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDEC))

enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  /* FILL ME */
};

static GHashTable *global_plugins;

/* A number of functon prototypes are given so we can refer to them later. */
static void gst_ffmpegdec_base_init (GstFFMpegDecClass * klass);
static void gst_ffmpegdec_class_init (GstFFMpegDecClass * klass);
static void gst_ffmpegdec_init (GstFFMpegDec * ffmpegdec);
static void gst_ffmpegdec_dispose (GObject * object);

static gboolean gst_ffmpegdec_query (GstPad * pad, GstQueryType type,
    GstFormat * fmt, gint64 * value);
static gboolean gst_ffmpegdec_event (GstPad * pad, GstEvent * event);

static GstPadLinkReturn gst_ffmpegdec_connect (GstPad * pad,
    const GstCaps * caps);
static void gst_ffmpegdec_chain (GstPad * pad, GstData * data);

static GstElementStateReturn gst_ffmpegdec_change_state (GstElement * element);

#if 0
/* some sort of bufferpool handling, but different */
static int gst_ffmpegdec_get_buffer (AVCodecContext * context,
    AVFrame * picture);
static void gst_ffmpegdec_release_buffer (AVCodecContext * context,
    AVFrame * picture);
#endif

static GstElementClass *parent_class = NULL;

/*static guint gst_ffmpegdec_signals[LAST_SIGNAL] = { 0 }; */

static void
gst_ffmpegdec_base_init (GstFFMpegDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstFFMpegDecClassParams *params;
  GstElementDetails details;
  GstPadTemplate *sinktempl, *srctempl;

  params = g_hash_table_lookup (global_plugins,
      GINT_TO_POINTER (G_OBJECT_CLASS_TYPE (gobject_class)));
  if (!params)
    params = g_hash_table_lookup (global_plugins, GINT_TO_POINTER (0));
  g_assert (params);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("FFMPEG %s decoder",
      gst_ffmpeg_get_codecid_longname (params->in_plugin->id));
  details.klass = g_strdup_printf ("Codec/Decoder/%s",
      (params->in_plugin->type == CODEC_TYPE_VIDEO) ? "Video" : "Audio");
  details.description = g_strdup_printf ("FFMPEG %s decoder",
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
}

static void
gst_ffmpegdec_class_init (GstFFMpegDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->dispose = gst_ffmpegdec_dispose;
  gstelement_class->change_state = gst_ffmpegdec_change_state;
}

static void
gst_ffmpegdec_init (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  /* setup pads */
  ffmpegdec->sinkpad = gst_pad_new_from_template (oclass->sinktempl, "sink");
  gst_pad_set_link_function (ffmpegdec->sinkpad, gst_ffmpegdec_connect);
  gst_pad_set_chain_function (ffmpegdec->sinkpad, gst_ffmpegdec_chain);
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->sinkpad);

  ffmpegdec->srcpad = gst_pad_new_from_template (oclass->srctempl, "src");
  gst_pad_use_explicit_caps (ffmpegdec->srcpad);
  gst_pad_set_event_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_event));
  gst_pad_set_query_function (ffmpegdec->srcpad,
      GST_DEBUG_FUNCPTR (gst_ffmpegdec_query));
  gst_element_add_pad (GST_ELEMENT (ffmpegdec), ffmpegdec->srcpad);

  /* some ffmpeg data */
  ffmpegdec->context = avcodec_alloc_context ();
  ffmpegdec->picture = avcodec_alloc_frame ();

  ffmpegdec->pctx = NULL;
  ffmpegdec->pcache = NULL;

  ffmpegdec->par = NULL;
  ffmpegdec->opened = FALSE;

  GST_FLAG_SET (ffmpegdec, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_ffmpegdec_dispose (GObject * object)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) object;

  G_OBJECT_CLASS (parent_class)->dispose (object);
  /* old session should have been closed in element_class->dispose */
  g_assert (!ffmpegdec->opened);

  /* clean up remaining allocated data */
  av_free (ffmpegdec->context);
  av_free (ffmpegdec->picture);
}

static gboolean
gst_ffmpegdec_query (GstPad * pad, GstQueryType type,
    GstFormat * fmt, gint64 * value)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) gst_pad_get_parent (pad);
  GstPad *peer = GST_PAD_PEER (ffmpegdec->sinkpad);
  GstFormat bfmt = GST_FORMAT_BYTES;

  if (!peer)
    return FALSE;
  else if (gst_pad_query (peer, type, fmt, value))
    return TRUE;
  /* ok, do bitrate calc... */
  else if ((type != GST_QUERY_POSITION && type != GST_QUERY_TOTAL) ||
           *fmt != GST_FORMAT_TIME || ffmpegdec->context->bit_rate == 0 ||
           !gst_pad_query (peer, type, &bfmt, value))
    return FALSE;

  if (ffmpegdec->pcache && type == GST_QUERY_POSITION)
    *value -= GST_BUFFER_SIZE (ffmpegdec->pcache);
  *value *= GST_SECOND / ffmpegdec->context->bit_rate;

  return TRUE;
}

static gboolean
gst_ffmpegdec_event (GstPad * pad, GstEvent * event)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) gst_pad_get_parent (pad);
  GstPad *peer = GST_PAD_PEER (ffmpegdec->sinkpad);

  if (!peer)
    return FALSE;
  else if (gst_pad_send_event (peer, event))
    return TRUE;
  else 
    return FALSE; /* .. */
}

static void
gst_ffmpegdec_close (GstFFMpegDec *ffmpegdec)
{
  if (!ffmpegdec->opened)
    return;

  if (ffmpegdec->par) {
    g_free (ffmpegdec->par);
    ffmpegdec->par = NULL;
  }

  if (ffmpegdec->context->priv_data)
    avcodec_close (ffmpegdec->context);
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

static gboolean
gst_ffmpegdec_open (GstFFMpegDec *ffmpegdec)
{
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));

  ffmpegdec->opened = TRUE;
  if (avcodec_open (ffmpegdec->context, oclass->in_plugin) < 0) {
    gst_ffmpegdec_close (ffmpegdec);
    GST_DEBUG ("ffdec_%s: Failed to open FFMPEG codec",
        oclass->in_plugin->name);
    return FALSE;
  }

  /* open a parser if we can - exclude mpeg4, because it is already
   * framed (divx), mp3 because it doesn't work (?) and mjpeg because
   * of $(see mpeg4)... */
  if (oclass->in_plugin->id != CODEC_ID_MPEG4 &&
      oclass->in_plugin->id != CODEC_ID_MJPEG &&
      oclass->in_plugin->id != CODEC_ID_MP3)
    ffmpegdec->pctx = av_parser_init (oclass->in_plugin->id);

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      ffmpegdec->format.video.width = 0;
      ffmpegdec->format.video.height = 0;
      ffmpegdec->format.video.fps = 0;
      ffmpegdec->format.video.fps_base = 0;
      break;
    case CODEC_TYPE_AUDIO:
      ffmpegdec->format.audio.samplerate = 0;
      ffmpegdec->format.audio.channels = 0;
      break;
    default:
      break;
  }
  ffmpegdec->next_ts = 0;

  return TRUE;
}

static GstPadLinkReturn
gst_ffmpegdec_connect (GstPad * pad, const GstCaps * caps)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) (gst_pad_get_parent (pad));
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  GstStructure *structure;
  const GValue *par;

  /* close old session */
  gst_ffmpegdec_close (ffmpegdec);

  /* set defaults */
  avcodec_get_context_defaults (ffmpegdec->context);

#if 0
  /* set buffer functions */
  ffmpegdec->context->get_buffer = gst_ffmpegdec_get_buffer;
  ffmpegdec->context->release_buffer = gst_ffmpegdec_release_buffer;
#endif

  /* get size and so */
  gst_ffmpeg_caps_with_codecid (oclass->in_plugin->id,
      oclass->in_plugin->type, caps, ffmpegdec->context);

  /* get pixel aspect ratio if it's set */
  structure = gst_caps_get_structure (caps, 0);
  par = gst_structure_get_value (structure, "pixel-aspect-ratio");
  if (par) {
    GST_DEBUG_OBJECT (ffmpegdec, "sink caps have pixel-aspect-ratio");
    ffmpegdec->par = g_new0 (GValue, 1);
    gst_value_init_and_copy (ffmpegdec->par, par);
  }

  /* we dont send complete frames - FIXME: we need a 'framed' property
   * in caps */
  if (oclass->in_plugin->capabilities & CODEC_CAP_TRUNCATED &&
      (ffmpegdec->context->codec_id == CODEC_ID_MPEG1VIDEO ||
          ffmpegdec->context->codec_id == CODEC_ID_MPEG2VIDEO))
    ffmpegdec->context->flags |= CODEC_FLAG_TRUNCATED;

  /* do *not* draw edges */
  ffmpegdec->context->flags |= CODEC_FLAG_EMU_EDGE;

  /* workaround encoder bugs */
  ffmpegdec->context->workaround_bugs |= FF_BUG_AUTODETECT;

  /* open codec - we don't select an output pix_fmt yet,
   * simply because we don't know! We only get it
   * during playback... */
  if (!gst_ffmpegdec_open (ffmpegdec)) {
    if (ffmpegdec->par) {
      g_free (ffmpegdec->par);
      ffmpegdec->par = NULL;
    }
    return GST_PAD_LINK_REFUSED;
  }

  return GST_PAD_LINK_OK;
}

#if 0
static int
gst_ffmpegdec_get_buffer (AVCodecContext * context, AVFrame * picture)
{
  GstBuffer *buf = NULL;
  gulong bufsize = 0;

  switch (context->codec_type) {
    case CODEC_TYPE_VIDEO:
      bufsize = avpicture_get_size (context->pix_fmt,
          context->width, context->height);
      buf = gst_buffer_new_and_alloc (bufsize);
      gst_ffmpeg_avpicture_fill ((AVPicture *) picture,
          GST_BUFFER_DATA (buf),
          context->pix_fmt, context->width, context->height);
      break;

    case CODEC_TYPE_AUDIO:
    default:
      g_assert (0);
      break;
  }

  /* tell ffmpeg we own this buffer
   *
   * we also use an evil hack (keep buffer in base[0])
   * to keep a reference to the buffer in release_buffer(),
   * so that we can ref() it here and unref() it there
   * so that we don't need to copy data */
  picture->type = FF_BUFFER_TYPE_USER;
  picture->age = G_MAXINT;
  picture->base[0] = (int8_t *) buf;
  gst_buffer_ref (buf);

  return 0;
}

static void
gst_ffmpegdec_release_buffer (AVCodecContext * context, AVFrame * picture)
{
  gint i;
  GstBuffer *buf = GST_BUFFER (picture->base[0]);

  gst_buffer_unref (buf);

  /* zero out the reference in ffmpeg */
  for (i = 0; i < 4; i++) {
    picture->data[i] = NULL;
    picture->linesize[i] = 0;
  }
}
#endif

static gboolean
gst_ffmpegdec_negotiate (GstFFMpegDec * ffmpegdec)
{
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  GstCaps *caps;

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      if (ffmpegdec->format.video.width == ffmpegdec->context->width &&
          ffmpegdec->format.video.height == ffmpegdec->context->height &&
          ffmpegdec->format.video.fps == ffmpegdec->context->frame_rate &&
          ffmpegdec->format.video.fps_base ==
              ffmpegdec->context->frame_rate_base)
        return TRUE;
      break;
    case CODEC_TYPE_AUDIO:
      if (ffmpegdec->format.audio.samplerate ==
              ffmpegdec->context->sample_rate &&
          ffmpegdec->format.audio.channels == ffmpegdec->context->channels)
        return TRUE;
      break;
    default:
      break;
  }

  caps = gst_ffmpeg_codectype_to_caps (oclass->in_plugin->type,
      ffmpegdec->context);

  /* add in pixel-aspect-ratio if we have it,
   * prefer ffmpeg par over sink par (since it's provided
   * by the codec, which is more often correct).
   */
 if (caps) {
   if (ffmpegdec->context->sample_aspect_ratio.num &&
       ffmpegdec->context->sample_aspect_ratio.den) {
     GST_DEBUG ("setting ffmpeg provided pixel-aspect-ratio");
     gst_structure_set (gst_caps_get_structure (caps, 0),
         "pixel-aspect-ratio", GST_TYPE_FRACTION,
         ffmpegdec->context->sample_aspect_ratio.num,
         ffmpegdec->context->sample_aspect_ratio.den,
         NULL);
    } else if (ffmpegdec->par) {
      GST_DEBUG ("passing on pixel-aspect-ratio from sink");
      gst_structure_set (gst_caps_get_structure (caps, 0),
          "pixel-aspect-ratio", GST_TYPE_FRACTION,
           gst_value_get_fraction_numerator (ffmpegdec->par),
           gst_value_get_fraction_denominator (ffmpegdec->par),
           NULL);
    }
  }

  if (caps == NULL ||
      !gst_pad_set_explicit_caps (ffmpegdec->srcpad, caps)) {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("Failed to link ffmpeg decoder (%s) to next element",
        oclass->in_plugin->name));

    if (caps != NULL)
      gst_caps_free (caps);

    return FALSE;
  }

  gst_caps_free (caps);

  return TRUE;
}

static gint
gst_ffmpegdec_frame (GstFFMpegDec * ffmpegdec,
    guint8 * data, guint size, gint * got_data, guint64 * in_ts)
{
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  GstBuffer *outbuf = NULL;
  gint have_data, len = 0;

  ffmpegdec->context->frame_number++;

  switch (oclass->in_plugin->type) {
    case CODEC_TYPE_VIDEO:
      len = avcodec_decode_video (ffmpegdec->context,
          ffmpegdec->picture, &have_data, data, size);
      GST_DEBUG ("Decode video: len=%d, have_data=%d", len, have_data);

      if (len >= 0 && have_data > 0) {
        /* libavcodec constantly crashes on stupid buffer allocation
         * errors inside. This drives me crazy, so we let it allocate
         * it's own buffers and copy to our own buffer afterwards... */
        AVPicture pic;
        gint fsize = gst_ffmpeg_avpicture_get_size (ffmpegdec->context->pix_fmt,
            ffmpegdec->context->width, ffmpegdec->context->height);

        outbuf = gst_buffer_new_and_alloc (fsize);

        /* original ffmpeg code does not handle odd sizes correctly.
         * This patched up version does */
        gst_ffmpeg_avpicture_fill (&pic, GST_BUFFER_DATA (outbuf),
            ffmpegdec->context->pix_fmt,
            ffmpegdec->context->width, ffmpegdec->context->height);

        /* the original convert function did not do the right thing, this
         * is a patched up version that adjust widht/height so that the
         * ffmpeg one works correctly. */
        gst_ffmpeg_img_convert (&pic, ffmpegdec->context->pix_fmt,
            (AVPicture *) ffmpegdec->picture,
            ffmpegdec->context->pix_fmt,
            ffmpegdec->context->width, 
            ffmpegdec->context->height);

        /* note that ffmpeg sometimes gets the FPS wrong.
         * For B-frame containing movies, we get all pictures delayed
         * except for the I frames, so we synchronize only on I frames
         * and keep an internal counter based on FPS for the others. */
        if (ffmpegdec->picture->pict_type == FF_I_TYPE &&
            GST_CLOCK_TIME_IS_VALID (*in_ts) &&
            ffmpegdec->context->frame_rate > 0) {
          ffmpegdec->next_ts = *in_ts;
        }
        GST_BUFFER_TIMESTAMP (outbuf) = ffmpegdec->next_ts;
        GST_BUFFER_DURATION (outbuf) = GST_SECOND *
            ffmpegdec->context->frame_rate_base /
            ffmpegdec->context->frame_rate;
        ffmpegdec->next_ts += GST_BUFFER_DURATION (outbuf);
      }
      break;

    case CODEC_TYPE_AUDIO:
      outbuf = gst_buffer_new_and_alloc (AVCODEC_MAX_AUDIO_FRAME_SIZE);
      len = avcodec_decode_audio (ffmpegdec->context,
          (int16_t *) GST_BUFFER_DATA (outbuf), &have_data, data, size);
      GST_DEBUG ("Decode audio: len=%d, have_data=%d", len, have_data);

      if (len >= 0 && have_data > 0) {
        GST_BUFFER_SIZE (outbuf) = have_data;
        if (GST_CLOCK_TIME_IS_VALID (*in_ts)) {
          ffmpegdec->next_ts = *in_ts;
        }
        GST_BUFFER_TIMESTAMP (outbuf) = ffmpegdec->next_ts;
        GST_BUFFER_DURATION (outbuf) = (have_data * GST_SECOND) /
            (2 * ffmpegdec->context->channels *
            ffmpegdec->context->sample_rate);
        ffmpegdec->next_ts += GST_BUFFER_DURATION (outbuf);
        *in_ts += GST_BUFFER_DURATION (outbuf);
      } else {
        gst_buffer_unref (outbuf);
      }
      break;
    default:
      g_assert (0);
      break;
  }

  if (len < 0 || have_data < 0) {
    GST_ERROR_OBJECT (ffmpegdec,
        "ffdec_%s: decoding error (len: %d, have_data: %d)",
        oclass->in_plugin->name, len, have_data);
    *got_data = 0;
    return len;
  } else if (len == 0 && have_data == 0) {
    *got_data = 0;
    return 0;
  } else {
    *got_data = 1;
  }

  if (have_data) {
    GST_DEBUG ("Decoded data, now pushing");

    if (!gst_ffmpegdec_negotiate (ffmpegdec)) {
      gst_buffer_unref (outbuf);
      return -1;
    }

    if (GST_PAD_IS_USABLE (ffmpegdec->srcpad))
      gst_pad_push (ffmpegdec->srcpad, GST_DATA (outbuf));
    else
      gst_buffer_unref (outbuf);
  }

  return len;
}

static void
gst_ffmpegdec_handle_event (GstFFMpegDec * ffmpegdec, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH:
      avcodec_flush_buffers (ffmpegdec->context);
      goto forward;
    case GST_EVENT_DISCONTINUOUS: {
      gint64 value;

      if (gst_event_discont_get_value (event, GST_FORMAT_TIME, &value)) {
        ffmpegdec->next_ts = value;
        GST_DEBUG_OBJECT (ffmpegdec, "Discont to time %" GST_TIME_FORMAT,
            GST_TIME_ARGS (value));
      } else if (ffmpegdec->context->bit_rate &&
          gst_event_discont_get_value (event, GST_FORMAT_BYTES, &value)) {
        gboolean new_media;

        ffmpegdec->next_ts = value * GST_SECOND / ffmpegdec->context->bit_rate;
        GST_DEBUG_OBJECT (ffmpegdec,
            "Discont to byte %lld, time %" GST_TIME_FORMAT,
            value, GST_TIME_ARGS (ffmpegdec->next_ts));
        new_media = GST_EVENT_DISCONT_NEW_MEDIA (event);
        gst_event_unref (event);
        event = gst_event_new_discontinuous (new_media,
            GST_FORMAT_TIME, ffmpegdec->next_ts, GST_FORMAT_UNDEFINED);
      } else {
        GST_WARNING_OBJECT (ffmpegdec,
            "Received discont with no useful value...");
      }
      avcodec_flush_buffers (ffmpegdec->context);
      /* fall-through */
    }
    default:
    forward:
      gst_pad_event_default (ffmpegdec->sinkpad, event);
      return;
  }
}

static void
gst_ffmpegdec_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *inbuf;
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) (gst_pad_get_parent (pad));
  GstFFMpegDecClass *oclass =
      (GstFFMpegDecClass *) (G_OBJECT_GET_CLASS (ffmpegdec));
  guint8 *bdata, *data;
  gint bsize, size, len, have_data;
  guint64 in_ts;

  /* event handling */
  if (GST_IS_EVENT (_data)) {
    gst_ffmpegdec_handle_event (ffmpegdec, GST_EVENT (_data));
    return;
  }

  inbuf = GST_BUFFER (_data);
  in_ts = GST_BUFFER_TIMESTAMP (inbuf);

  if (!ffmpegdec->opened) {
    GST_ELEMENT_ERROR (ffmpegdec, CORE, NEGOTIATION, (NULL),
        ("ffdec_%s: input format was not set before data start",
            oclass->in_plugin->name));
    return;
  }

  GST_DEBUG ("Received new data of size %d, time %" GST_TIME_FORMAT,
      GST_BUFFER_SIZE (inbuf), GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (inbuf)));

  /* parse cache joining */
  if (ffmpegdec->pcache) {
    inbuf = gst_buffer_join (ffmpegdec->pcache, inbuf);
    ffmpegdec->pcache = NULL;
    bdata = GST_BUFFER_DATA (inbuf);
    bsize = GST_BUFFER_SIZE (inbuf);
  }
  /* workarounds, functions write to buffers:
   *  libavcodec/svq1.c:svq1_decode_frame writes to the given buffer.
   *  libavcodec/svq3.c:svq3_decode_slice_header too.
   * ffmpeg devs know about it and will fix it (they said). */
  else if (oclass->in_plugin->id == CODEC_ID_SVQ1 ||
      oclass->in_plugin->id == CODEC_ID_SVQ3) {
    inbuf = gst_buffer_copy_on_write (inbuf);
    bdata = GST_BUFFER_DATA (inbuf);
    bsize = GST_BUFFER_SIZE (inbuf);
  } else {
    bdata = GST_BUFFER_DATA (inbuf);
    bsize = GST_BUFFER_SIZE (inbuf);
  }

  do {
    /* parse, if at all possible */
    if (bsize == 0) {
      data = NULL;
      size = 0;
    } else if (ffmpegdec->pctx) {
      gint res;

      res = av_parser_parse (ffmpegdec->pctx, ffmpegdec->context,
          &data, &size, bdata, bsize,
          in_ts / (GST_SECOND / AV_TIME_BASE),
          in_ts / (GST_SECOND / AV_TIME_BASE));

      if (res == 0 || size == 0)
        break;
      else {
        bsize -= res;
        bdata += res;
      }
    } else {
      data = bdata;
      size = bsize;
    }

    if ((len = gst_ffmpegdec_frame (ffmpegdec, data, size,
             &have_data, &in_ts)) < 0)
      break;

    if (!ffmpegdec->pctx) {
      bsize -= len;
      bdata += len;
    }

    if (!have_data) {
      break;
    }
  } while (1);

  if ((ffmpegdec->pctx || oclass->in_plugin->id == CODEC_ID_MP3) &&
      bsize > 0) {
    GST_DEBUG ("Keeping %d bytes of data", bsize);

    ffmpegdec->pcache = gst_buffer_create_sub (inbuf,
        GST_BUFFER_SIZE (inbuf) - bsize, bsize);
  }
  gst_buffer_unref (inbuf);
}

static GstElementStateReturn
gst_ffmpegdec_change_state (GstElement * element)
{
  GstFFMpegDec *ffmpegdec = (GstFFMpegDec *) element;
  gint transition = GST_STATE_TRANSITION (element);

  switch (transition) {
    case GST_STATE_PAUSED_TO_READY:
      gst_ffmpegdec_close (ffmpegdec);
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean
gst_ffmpegdec_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstFFMpegDecClass),
    (GBaseInitFunc) gst_ffmpegdec_base_init,
    NULL,
    (GClassInitFunc) gst_ffmpegdec_class_init,
    NULL,
    NULL,
    sizeof (GstFFMpegDec),
    0,
    (GInstanceInitFunc) gst_ffmpegdec_init,
  };
  GType type;
  AVCodec *in_plugin;
  gint rank;

  in_plugin = first_avcodec;

  global_plugins = g_hash_table_new (NULL, NULL);

  while (in_plugin) {
    GstFFMpegDecClassParams *params;
    GstCaps *srccaps, *sinkcaps;
    gchar *type_name;

    /* no quasi-codecs, please */
    if (in_plugin->id == CODEC_ID_RAWVIDEO ||
        (in_plugin->id >= CODEC_ID_PCM_S16LE &&
            in_plugin->id <= CODEC_ID_PCM_ALAW)) {
      goto next;
    }

    /* only decoders */
    if (!in_plugin->decode) {
      goto next;
    }

    /* name */
    if (!gst_ffmpeg_get_codecid_longname (in_plugin->id))
      goto next;

    /* first make sure we've got a supported type */
    sinkcaps = gst_ffmpeg_codecid_to_caps (in_plugin->id, NULL, FALSE);
    srccaps = gst_ffmpeg_codectype_to_caps (in_plugin->type, NULL);
    if (!sinkcaps || !srccaps) {
      if (sinkcaps) gst_caps_free (sinkcaps);
      if (srccaps) gst_caps_free (srccaps);
      goto next;
    }

    /* construct the type */
    type_name = g_strdup_printf ("ffdec_%s", in_plugin->name);

    /* if it's already registered, drop it */
    if (g_type_from_name (type_name)) {
      g_free (type_name);
      goto next;
    }

    params = g_new0 (GstFFMpegDecClassParams, 1);
    params->in_plugin = in_plugin;
    params->srccaps = srccaps;
    params->sinkcaps = sinkcaps;
    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (0), (gpointer) params);

    /* create the gtype now */
    type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);

    /* (Ronald) MPEG-4 gets a higher priority because it has been well-
     * tested and by far outperforms divxdec/xviddec - so we prefer it.
     * msmpeg4v3 same, as it outperforms divxdec for divx3 playback.
     * H263 has the same mimetype as H263I and since H263 works for the
     * few streams that I've tried (see, e.g., #155163), I'll use that
     * and use rank=none for H263I for now, until I know what the diff
     * is. */
    switch (in_plugin->id) {
      case CODEC_ID_MPEG4:
      case CODEC_ID_MSMPEG4V3:
        rank = GST_RANK_PRIMARY;
        break;
      case CODEC_ID_H263I:
        rank = GST_RANK_NONE;
        break;
      default:
        rank = GST_RANK_MARGINAL;
        break;
    }
    if (!gst_element_register (plugin, type_name, rank, type)) {
      g_free (type_name);
      return FALSE;
    }

    g_free (type_name);

    g_hash_table_insert (global_plugins,
        GINT_TO_POINTER (type), (gpointer) params);

  next:
    in_plugin = in_plugin->next;
  }
  g_hash_table_remove (global_plugins, GINT_TO_POINTER (0));

  return TRUE;
}
