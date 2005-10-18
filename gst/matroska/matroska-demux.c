/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *
 * matroska-demux.c: matroska file/stream demuxer
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

#include <math.h>
#include <string.h>

/* For AVI compatibility mode... Who did that? */
/* and for fourcc stuff */
#include <gst/riff/riff-ids.h>
#include <gst/riff/riff-media.h>

#include "matroska-demux.h"
#include "matroska-ids.h"

GST_DEBUG_CATEGORY (matroskademux_debug);
#define GST_CAT_DEFAULT matroskademux_debug

enum
{
  ARG_0,
  ARG_METADATA,
  ARG_STREAMINFO
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska")
    );

static void gst_matroska_demux_base_init (GstMatroskaDemuxClass * klass);
static void gst_matroska_demux_class_init (GstMatroskaDemuxClass * klass);
static void gst_matroska_demux_init (GstMatroskaDemux * demux);

/* element functions */
static void gst_matroska_demux_loop (GstPad * pad);

static gboolean gst_matroska_demux_element_send_event (GstElement * element,
    GstEvent * event);

/* pad functions */
static gboolean gst_matroska_demux_sink_activate_pull (GstPad * sinkpad,
    gboolean active);
static gboolean gst_matroska_demux_sink_activate (GstPad * sinkpad);
static gboolean gst_matroska_demux_handle_seek_event (GstMatroskaDemux * demux,
    GstEvent * event);
static gboolean gst_matroska_demux_handle_src_event (GstPad * pad,
    GstEvent * event);
static const GstQueryType *gst_matroska_demux_get_src_query_types (GstPad *
    pad);
static gboolean gst_matroska_demux_handle_src_query (GstPad * pad,
    GstQuery * query);

static GstStateChangeReturn
gst_matroska_demux_change_state (GstElement * element,
    GstStateChange transition);

/* caps functions */
static GstCaps *gst_matroska_demux_video_caps (GstMatroskaTrackVideoContext
    * videocontext,
    const gchar * codec_id, gpointer data, guint size, gchar ** codec_name);
static GstCaps *gst_matroska_demux_audio_caps (GstMatroskaTrackAudioContext
    * audiocontext,
    const gchar * codec_id, gpointer data, guint size, gchar ** codec_name);
static GstCaps *gst_matroska_demux_complex_caps (GstMatroskaTrackComplexContext
    * complexcontext, const gchar * codec_id, gpointer data, guint size);
static GstCaps
    * gst_matroska_demux_subtitle_caps (GstMatroskaTrackSubtitleContext *
    subtitlecontext, const gchar * codec_id, gpointer data, guint size);

/* stream methods */
static void gst_matroska_demux_reset (GstElement * element);

static GstPadTemplate *subtitlesrctempl;        /* NULL */
static GstPadTemplate *videosrctempl;   /* NULL */
static GstPadTemplate *audiosrctempl;   /* NULL */

static GstEbmlReadClass *parent_class;  /* NULL; */

static GType
gst_matroska_demux_get_type (void)
{
  static GType gst_matroska_demux_type; /* 0 */

  if (!gst_matroska_demux_type) {
    static const GTypeInfo gst_matroska_demux_info = {
      sizeof (GstMatroskaDemuxClass),
      (GBaseInitFunc) gst_matroska_demux_base_init,
      NULL,
      (GClassInitFunc) gst_matroska_demux_class_init,
      NULL,
      NULL,
      sizeof (GstMatroskaDemux),
      0,
      (GInstanceInitFunc) gst_matroska_demux_init,
    };

    gst_matroska_demux_type =
        g_type_register_static (GST_TYPE_EBML_READ,
        "GstMatroskaDemux", &gst_matroska_demux_info, 0);
  }

  return gst_matroska_demux_type;
}

static void
gst_matroska_demux_base_init (GstMatroskaDemuxClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  static GstElementDetails gst_matroska_demux_details = {
    "Matroska demuxer",
    "Codec/Demuxer",
    "Demuxes a Matroska Stream into video/audio/subtitles",
    "Ronald Bultje <rbultje@ronald.bitfreak.net>"
  };

  gst_element_class_add_pad_template (element_class, videosrctempl);
  gst_element_class_add_pad_template (element_class, audiosrctempl);
  gst_element_class_add_pad_template (element_class, subtitlesrctempl);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));
  gst_element_class_set_details (element_class, &gst_matroska_demux_details);

  GST_DEBUG_CATEGORY_INIT (matroskademux_debug, "matroskademux", 0,
      "Matroska demuxer");
}

static void
gst_matroska_demux_class_init (GstMatroskaDemuxClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_change_state);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_element_send_event);
}

static void
gst_matroska_demux_init (GstMatroskaDemux * demux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);
  gint i;

  demux->sinkpad =
      gst_pad_new_from_template (gst_element_class_get_pad_template (klass,
          "sink"), "sink");
  gst_pad_set_activate_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_sink_activate));
  gst_pad_set_activatepull_function (demux->sinkpad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_sink_activate_pull));
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
  GST_EBML_READ (demux)->sinkpad = demux->sinkpad;

  /* initial stream no. */
  for (i = 0; i < GST_MATROSKA_DEMUX_MAX_STREAMS; i++) {
    demux->src[i] = NULL;
  }
  demux->writing_app = NULL;
  demux->muxing_app = NULL;
  demux->index = NULL;

  /* finish off */
  gst_matroska_demux_reset (GST_ELEMENT (demux));
}

static void
gst_matroska_demux_reset (GstElement * element)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  guint i;

  /* reset input */
  demux->state = GST_MATROSKA_DEMUX_STATE_START;

  /* clean up existing streams */
  for (i = 0; i < GST_MATROSKA_DEMUX_MAX_STREAMS; i++) {
    if (demux->src[i] != NULL) {
      if (demux->src[i]->pad != NULL) {
        gst_element_remove_pad (GST_ELEMENT (demux), demux->src[i]->pad);
      }
      g_free (demux->src[i]->codec_id);
      g_free (demux->src[i]->codec_name);
      g_free (demux->src[i]->name);
      g_free (demux->src[i]->language);
      g_free (demux->src[i]->codec_priv);
      g_free (demux->src[i]);
      demux->src[i] = NULL;
    }
  }
  demux->num_streams = 0;
  demux->num_a_streams = 0;
  demux->num_t_streams = 0;
  demux->num_v_streams = 0;

  /* reset media info */
  g_free (demux->writing_app);
  demux->writing_app = NULL;
  g_free (demux->muxing_app);
  demux->muxing_app = NULL;

  /* reset indexes */
  demux->num_indexes = 0;
  g_free (demux->index);
  demux->index = NULL;

  /* reset timers */
  demux->clock = NULL;
  demux->time_scale = 1000000;
  demux->duration = 0;
  demux->pos = 0;
  demux->created = G_MININT64;

  demux->metadata_parsed = FALSE;
  demux->index_parsed = FALSE;

  demux->segment_rate = 1.0;
  demux->segment_start = GST_CLOCK_TIME_NONE;
  demux->segment_stop = GST_CLOCK_TIME_NONE;
  demux->segment_play = FALSE;
  demux->seek_pending = FALSE;
}

static gint
gst_matroska_demux_stream_from_num (GstMatroskaDemux * demux, guint track_num)
{
  guint n;

  for (n = 0; n < demux->num_streams; n++) {
    if (demux->src[n] != NULL && demux->src[n]->num == track_num) {
      return n;
    }
  }

  if (n == demux->num_streams) {
    GST_WARNING ("Failed to find corresponding pad for tracknum %d", track_num);
  }

  return -1;
}

static GstCaps *
gst_matroska_demux_getcaps (GstPad * pad)
{
  GstMatroskaDemux *demux;
  GstCaps *caps = NULL;
  guint i;

  demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));

  for (i = 0; caps == NULL && i < demux->num_streams; ++i) {
    if (demux->src[i]->pad == pad)
      caps = gst_caps_copy (demux->src[i]->caps);
  }

  gst_object_unref (demux);

  g_return_val_if_fail (caps != NULL, NULL);

  return caps;
}


static gboolean
gst_matroska_demux_add_stream (GstMatroskaDemux * demux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstMatroskaTrackContext *context;
  GstPadTemplate *templ = NULL;
  GstCaps *caps = NULL;
  gchar *padname = NULL;
  gboolean res = TRUE;
  guint32 id;
  GstTagList *list = NULL;
  gchar *codec = NULL;

  if (demux->num_streams >= GST_MATROSKA_DEMUX_MAX_STREAMS) {
    GST_WARNING ("Maximum number of streams (%d) exceeded, skipping",
        GST_MATROSKA_DEMUX_MAX_STREAMS);
    return gst_ebml_read_skip (ebml);   /* skip-and-continue */
  }

  /* allocate generic... if we know the type, we'll g_renew()
   * with the precise type */
  context = g_new0 (GstMatroskaTrackContext, 1);
  demux->src[demux->num_streams] = context;
  context->index = demux->num_streams;
  context->type = 0;            /* no type yet */
  context->default_duration = 0;
  context->pos = 0;
  demux->num_streams++;

  /* start with the master */
  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  /* try reading the trackentry headers */
  while (res) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id)) {
      res = FALSE;
      break;
    } else if (demux->level_up > 0) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* track number (unique stream ID) */
      case GST_MATROSKA_ID_TRACKNUMBER:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        context->num = num;
        break;
      }

        /* track UID (unique identifier) */
      case GST_MATROSKA_ID_TRACKUID:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        context->uid = num;
        break;
      }

        /* track type (video, audio, combined, subtitle, etc.) */
      case GST_MATROSKA_ID_TRACKTYPE:{
        guint64 num;

        if (context->type != 0) {
          GST_WARNING
              ("More than one tracktype defined in a trackentry - skipping");
          break;
        }
        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        context->type = num;

        /* ok, so we're actually going to reallocate this thing */
        switch (context->type) {
          case GST_MATROSKA_TRACK_TYPE_VIDEO:
            context = (GstMatroskaTrackContext *)
                g_renew (GstMatroskaTrackVideoContext, context, 1);
            ((GstMatroskaTrackVideoContext *) context)->display_width = 0;
            ((GstMatroskaTrackVideoContext *) context)->display_height = 0;
            ((GstMatroskaTrackVideoContext *) context)->pixel_width = 0;
            ((GstMatroskaTrackVideoContext *) context)->pixel_height = 0;
            ((GstMatroskaTrackVideoContext *) context)->eye_mode = 0;
            ((GstMatroskaTrackVideoContext *) context)->asr_mode = 0;
            ((GstMatroskaTrackVideoContext *) context)->fourcc = 0;
            break;
          case GST_MATROSKA_TRACK_TYPE_AUDIO:
            context = (GstMatroskaTrackContext *)
                g_renew (GstMatroskaTrackAudioContext, context, 1);
            /* defaults */
            ((GstMatroskaTrackAudioContext *) context)->channels = 1;
            ((GstMatroskaTrackAudioContext *) context)->samplerate = 8000;
            break;
          case GST_MATROSKA_TRACK_TYPE_COMPLEX:
            context = (GstMatroskaTrackContext *)
                g_renew (GstMatroskaTrackComplexContext, context, 1);
            break;
          case GST_MATROSKA_TRACK_TYPE_SUBTITLE:
            context = (GstMatroskaTrackContext *)
                g_renew (GstMatroskaTrackSubtitleContext, context, 1);
            break;
          case GST_MATROSKA_TRACK_TYPE_LOGO:
          case GST_MATROSKA_TRACK_TYPE_CONTROL:
          default:
            GST_WARNING ("Unknown or unsupported track type 0x%x",
                context->type);
            context->type = 0;
            break;
        }
        demux->src[demux->num_streams - 1] = context;
        break;
      }

        /* tracktype specific stuff for video */
      case GST_MATROSKA_ID_TRACKVIDEO:{
        GstMatroskaTrackVideoContext *videocontext;

        if (context->type != GST_MATROSKA_TRACK_TYPE_VIDEO) {
          GST_WARNING
              ("trackvideo EBML entry in non-video track - ignoring track");
          res = FALSE;
          break;
        } else if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        videocontext = (GstMatroskaTrackVideoContext *) context;

        while (res) {
          if (!gst_ebml_peek_id (ebml, &demux->level_up, &id)) {
            res = FALSE;
            break;
          } else if (demux->level_up > 0) {
            demux->level_up--;
            break;
          }

          switch (id) {
              /* fixme, this should be one-up, but I get it here (?) */
            case GST_MATROSKA_ID_TRACKDEFAULTDURATION:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              context->default_duration = num;
              break;
            }

              /* video framerate */
            case GST_MATROSKA_ID_VIDEOFRAMERATE:{
              gdouble num;

              if (!gst_ebml_read_float (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              context->default_duration = GST_SECOND * (1. / num);
              break;
            }

              /* width of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYWIDTH:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->display_width = num;
              GST_DEBUG ("display_width %" G_GUINT64_FORMAT, num);
              break;
            }

              /* height of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYHEIGHT:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->display_height = num;
              GST_DEBUG ("display_height %" G_GUINT64_FORMAT, num);
              break;
            }

              /* width of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELWIDTH:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->pixel_width = num;
              GST_DEBUG ("pixel_width %" G_GUINT64_FORMAT, num);
              break;
            }

              /* height of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELHEIGHT:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->pixel_height = num;
              GST_DEBUG ("pixel_height %" G_GUINT64_FORMAT, num);
              break;
            }

              /* whether the video is interlaced */
            case GST_MATROSKA_ID_VIDEOFLAGINTERLACED:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              if (num)
                context->flags |= GST_MATROSKA_VIDEOTRACK_INTERLACED;
              else
                context->flags &= ~GST_MATROSKA_VIDEOTRACK_INTERLACED;
              break;
            }

              /* stereo mode (whether the video has two streams, where
               * one is for the left eye and the other for the right eye,
               * which creates a 3D-like effect) */
            case GST_MATROSKA_ID_VIDEOSTEREOMODE:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              if (num != GST_MATROSKA_EYE_MODE_MONO &&
                  num != GST_MATROSKA_EYE_MODE_LEFT &&
                  num != GST_MATROSKA_EYE_MODE_RIGHT &&
                  num != GST_MATROSKA_EYE_MODE_BOTH) {
                GST_WARNING ("Unknown eye mode 0x%x - ignoring", (guint) num);
                break;
              }
              videocontext->eye_mode = num;
              break;
            }

              /* aspect ratio behaviour */
            case GST_MATROSKA_ID_VIDEOASPECTRATIO:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              if (num != GST_MATROSKA_ASPECT_RATIO_MODE_FREE &&
                  num != GST_MATROSKA_ASPECT_RATIO_MODE_KEEP &&
                  num != GST_MATROSKA_ASPECT_RATIO_MODE_FIXED) {
                GST_WARNING ("Unknown aspect ratio mode 0x%x - ignoring",
                    (guint) num);
                break;
              }
              videocontext->asr_mode = num;
              break;
            }

              /* colourspace (only matters for raw video) fourcc */
            case GST_MATROSKA_ID_VIDEOCOLOURSPACE:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->fourcc = num;
              break;
            }

            default:
              GST_WARNING ("Unknown video track header entry 0x%x - ignoring",
                  id);
              /* pass-through */

            case GST_EBML_ID_VOID:
              if (!gst_ebml_read_skip (ebml))
                res = FALSE;
              break;
          }

          if (demux->level_up) {
            demux->level_up--;
            break;
          }
        }
        break;
      }

        /* tracktype specific stuff for audio */
      case GST_MATROSKA_ID_TRACKAUDIO:{
        GstMatroskaTrackAudioContext *audiocontext;

        if (context->type != GST_MATROSKA_TRACK_TYPE_AUDIO) {
          GST_WARNING
              ("trackaudio EBML entry in non-audio track - ignoring track");
          res = FALSE;
          break;
        } else if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        audiocontext = (GstMatroskaTrackAudioContext *) context;

        while (res) {
          if (!gst_ebml_peek_id (ebml, &demux->level_up, &id)) {
            res = FALSE;
            break;
          } else if (demux->level_up > 0) {
            demux->level_up--;
            break;
          }

          switch (id) {
              /* samplerate */
            case GST_MATROSKA_ID_AUDIOSAMPLINGFREQ:{
              gdouble num;

              if (!gst_ebml_read_float (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              audiocontext->samplerate = num;
              break;
            }

              /* bitdepth */
            case GST_MATROSKA_ID_AUDIOBITDEPTH:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              audiocontext->bitdepth = num;
              break;
            }

              /* channels */
            case GST_MATROSKA_ID_AUDIOCHANNELS:{
              guint64 num;

              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              audiocontext->channels = num;
              break;
            }

            default:
              GST_WARNING ("Unknown audio track header entry 0x%x - ignoring",
                  id);
              /* pass-through */

            case GST_EBML_ID_VOID:
              if (!gst_ebml_read_skip (ebml))
                res = FALSE;
              break;
          }

          if (demux->level_up) {
            demux->level_up--;
            break;
          }
        }
        break;
      }

        /* codec identifier */
      case GST_MATROSKA_ID_CODECID:{
        gchar *text;

        if (!gst_ebml_read_ascii (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->codec_id = text;
        break;
      }

        /* codec private data */
      case GST_MATROSKA_ID_CODECPRIVATE:{
        guint8 *data;
        guint64 size;

        if (!gst_ebml_read_binary (ebml, &id, &data, &size)) {
          res = FALSE;
          break;
        }
        context->codec_priv = data;
        context->codec_priv_size = size;
        break;
      }

        /* name of the codec */
      case GST_MATROSKA_ID_CODECNAME:{
        gchar *text;

        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->codec_name = text;
        break;
      }

        /* name of this track */
      case GST_MATROSKA_ID_TRACKNAME:{
        gchar *text;

        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->name = text;
        break;
      }

        /* language (matters for audio/subtitles, mostly) */
      case GST_MATROSKA_ID_TRACKLANGUAGE:{
        gchar *text;

        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->language = text;
        break;
      }

        /* whether this is actually used */
      case GST_MATROSKA_ID_TRACKFLAGENABLED:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        if (num)
          context->flags |= GST_MATROSKA_TRACK_ENABLED;
        else
          context->flags &= ~GST_MATROSKA_TRACK_ENABLED;
        break;
      }

        /* whether it's the default for this track type */
      case GST_MATROSKA_ID_TRACKFLAGDEFAULT:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        if (num)
          context->flags |= GST_MATROSKA_TRACK_DEFAULT;
        else
          context->flags &= ~GST_MATROSKA_TRACK_DEFAULT;
        break;
      }

        /* lacing (like MPEG, where blocks don't end/start on frame
         * boundaries) */
      case GST_MATROSKA_ID_TRACKFLAGLACING:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        if (num)
          context->flags |= GST_MATROSKA_TRACK_LACING;
        else
          context->flags &= ~GST_MATROSKA_TRACK_LACING;
        break;
      }

        /* default length (in time) of one data block in this track */
      case GST_MATROSKA_ID_TRACKDEFAULTDURATION:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        context->default_duration = num;
        break;
      }

      default:
        GST_WARNING ("Unknown track header entry 0x%x - ignoring", id);
        /* pass-through */

        /* we ignore these because they're nothing useful (i.e. crap). */
      case GST_MATROSKA_ID_CODECINFOURL:
      case GST_MATROSKA_ID_CODECDOWNLOADURL:
      case GST_MATROSKA_ID_TRACKMINCACHE:
      case GST_MATROSKA_ID_TRACKMAXCACHE:
      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          res = FALSE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (context->type == 0 || context->codec_id == NULL || !res) {
    if (res)
      GST_WARNING ("Unknown stream/codec in track entry header");

    demux->num_streams--;
    demux->src[demux->num_streams] = NULL;
    if (context) {
      g_free (context->codec_id);
      g_free (context->codec_name);
      g_free (context->name);
      g_free (context->language);
      g_free (context->codec_priv);
      g_free (context);
    }

    return res;
  }

  /* now create the GStreamer connectivity */
  switch (context->type) {
    case GST_MATROSKA_TRACK_TYPE_VIDEO:{
      GstMatroskaTrackVideoContext *videocontext =
          (GstMatroskaTrackVideoContext *) context;
      padname = g_strdup_printf ("video_%02d", demux->num_v_streams++);
      templ = gst_element_class_get_pad_template (klass, "video_%02d");
      caps = gst_matroska_demux_video_caps (videocontext,
          context->codec_id,
          context->codec_priv, context->codec_priv_size, &codec);
      if (codec) {
        list = gst_tag_list_new ();
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_VIDEO_CODEC, codec, NULL);
        g_free (codec);
      }
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_AUDIO:{
      GstMatroskaTrackAudioContext *audiocontext =
          (GstMatroskaTrackAudioContext *) context;
      padname = g_strdup_printf ("audio_%02d", demux->num_a_streams++);
      templ = gst_element_class_get_pad_template (klass, "audio_%02d");
      caps = gst_matroska_demux_audio_caps (audiocontext,
          context->codec_id,
          context->codec_priv, context->codec_priv_size, &codec);
      audiocontext->first_frame = TRUE;
      if (codec) {
        list = gst_tag_list_new ();
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_AUDIO_CODEC, codec, NULL);
        g_free (codec);
      }
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_COMPLEX:{
      GstMatroskaTrackComplexContext *complexcontext =
          (GstMatroskaTrackComplexContext *) context;
      padname = g_strdup_printf ("video_%02d", demux->num_v_streams++);
      templ = gst_element_class_get_pad_template (klass, "video_%02d");
      caps = gst_matroska_demux_complex_caps (complexcontext,
          context->codec_id, context->codec_priv, context->codec_priv_size);
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_SUBTITLE:{
      GstMatroskaTrackSubtitleContext *subtitlecontext =
          (GstMatroskaTrackSubtitleContext *) context;
      padname = g_strdup_printf ("subtitle_%02d", demux->num_t_streams++);
      templ = gst_element_class_get_pad_template (klass, "subtitle_%02d");
      caps = gst_matroska_demux_subtitle_caps (subtitlecontext,
          context->codec_id, context->codec_priv, context->codec_priv_size);
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_LOGO:
    case GST_MATROSKA_TRACK_TYPE_CONTROL:
    default:
      /* we should already have quit by now */
      g_assert_not_reached ();
  }

  if (context->language) {
    if (!list)
      list = gst_tag_list_new ();
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_LANGUAGE_CODE, context->language, NULL);
  }

  /* the pad in here */
  context->pad = gst_pad_new_from_template (templ, padname);
  context->caps = caps ? caps : gst_caps_new_empty ();

  gst_pad_set_event_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_handle_src_event));
  gst_pad_set_query_type_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_get_src_query_types));
  gst_pad_set_query_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_handle_src_query));

  if (caps) {
    GST_LOG ("Adding pad '%s' with caps %" GST_PTR_FORMAT, padname, caps);
    if (gst_caps_is_fixed (caps)) {
      GST_LOG ("fixed caps");
      gst_pad_use_fixed_caps (context->pad);
      gst_pad_set_caps (context->pad, context->caps);
    } else {
      GST_LOG ("non-fixed caps");
      gst_pad_set_getcaps_function (context->pad,
          GST_DEBUG_FUNCPTR (gst_matroska_demux_getcaps));
    }
    gst_pad_set_active (context->pad, TRUE);
    gst_element_add_pad (GST_ELEMENT (demux), context->pad);
  } else {
    /* FIXME: are we leaking the pad here? can this even happen? */
    GST_LOG ("Not adding pad '%s' with empty caps", padname);
  }

  /* tags */
  if (list) {
    gst_element_found_tags_for_pad (GST_ELEMENT (demux), context->pad, list);
  }

  g_free (padname);

  /* tadaah! */
  return TRUE;
}

static const GstQueryType *
gst_matroska_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0
  };

  return query_types;
}

static gboolean
gst_matroska_demux_handle_src_query (GstPad * pad, GstQuery * query)
{
  GstMatroskaDemux *demux;
  gboolean res = FALSE;

  demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG ("only query position on TIME is supported");
        break;
      }

      GST_LOCK (demux);

      /* mabe we should only fill in the total time and let
       * decoders fill in the current position? (like oggdemux) */
      gst_query_set_position (query, GST_FORMAT_TIME, demux->pos,
          demux->duration);

      GST_UNLOCK (demux);

      res = TRUE;
      break;
    }

    default:
      break;
  }

  gst_object_unref (demux);
  return res;
}


static GstMatroskaIndex *
gst_matroskademux_do_index_seek (GstMatroskaDemux * demux, guint64 seek_pos)
{
  guint entry = (guint) - 1;
  guint n;

  for (n = 0; n < demux->num_indexes; n++) {
    if (entry == (guint) - 1) {
      entry = n;
    } else {
      gfloat diff_old = fabs (1. * (demux->index[entry].time - seek_pos)),
          diff_new = fabs (1. * (demux->index[n].time - seek_pos));

      if (diff_new < diff_old) {
        entry = n;
      }
    }
  }

  if (entry != (guint) - 1) {
    return &demux->index[entry];
  }

  return NULL;
}

/* takes ownership of the passed event! */
static gboolean
gst_matroska_demux_send_event (GstMatroskaDemux * demux, GstEvent * event)
{
  gboolean ret = TRUE;
  gint i;

  GST_DEBUG_OBJECT (demux, "Sending event of type %s to all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; i++) {
    GstMatroskaTrackContext *stream;

    stream = demux->src[i];
    gst_event_ref (event);
    gst_pad_push_event (stream->pad, event);
  }
  gst_event_unref (event);
  return ret;
}

static gboolean
gst_matroska_demux_element_send_event (GstElement * element, GstEvent * event)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  gboolean res;

  if (GST_EVENT_TYPE (event) == GST_EVENT_SEEK) {
    res = gst_matroska_demux_handle_seek_event (demux, event);
  } else {
    GST_WARNING ("Unhandled event of type %s", GST_EVENT_TYPE_NAME (event));
    res = FALSE;
  }
  gst_event_unref (event);
  return res;
}

static gboolean
gst_matroska_demux_handle_seek_event (GstMatroskaDemux * demux,
    GstEvent * event)
{
  GstMatroskaIndex *entry;
  GstSeekFlags flags;
  GstSeekType cur_type, stop_type;
  GstFormat format;
  GstEvent *newsegment_event;
  gboolean flush;
  gdouble rate;
  gint64 cur, stop;
  gint64 segment_start, segment_stop;
  gint i;

  gst_event_parse_seek (event, &rate, &format, &flags, &cur_type, &cur,
      &stop_type, &stop);

  /* we can only seek on time */
  if (format != GST_FORMAT_TIME) {
    GST_DEBUG ("Can only seek on TIME");
    return FALSE;
  }

  /* cannot yet do backwards playback */
  if (rate <= 0.0) {
    GST_DEBUG ("Can only seek with positive rate");
    return FALSE;
  }

  /* check sanity before we start flushing and all that */
  if (cur_type == GST_SEEK_TYPE_SET) {
    GST_LOCK (demux);
    if (!gst_matroskademux_do_index_seek (demux, cur)) {
      GST_DEBUG ("No matching seek entry in index");
      GST_UNLOCK (demux);
      return FALSE;
    }
    GST_DEBUG ("Seek position looks sane");
    GST_UNLOCK (demux);
  }

  flush = !!(flags & GST_SEEK_FLAG_FLUSH);

  if (flush) {
    GST_DEBUG ("Starting flush");
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_start ());
    gst_matroska_demux_send_event (demux, gst_event_new_flush_start ());
  } else {
    gst_pad_pause_task (demux->sinkpad);
  }

  /* now grab the stream lock so that streaming cannot continue, for
   * non flushing seeks when the element is in PAUSED this could block
   * forever. */
  GST_STREAM_LOCK (demux->sinkpad);

  GST_LOCK (demux);

  /* if nothing configured, play complete file */
  if (cur == GST_CLOCK_TIME_NONE)
    cur = 0;
  if (stop == GST_CLOCK_TIME_NONE)
    stop = demux->duration;

  if (cur_type == GST_SEEK_TYPE_SET)
    segment_start = cur;
  else if (cur_type == GST_SEEK_TYPE_CUR)
    segment_start = demux->segment_start + cur;
  else
    segment_start = demux->segment_start;

  if (stop_type == GST_SEEK_TYPE_SET)
    segment_stop = stop;
  else if (stop_type == GST_SEEK_TYPE_CUR)
    segment_stop = demux->segment_stop + stop;
  else
    segment_stop = demux->segment_stop;

  segment_start = CLAMP (segment_start, 0, demux->duration);
  segment_stop = CLAMP (segment_stop, 0, demux->duration);

  GST_DEBUG ("New segment positions: %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
      GST_TIME_ARGS (segment_start), GST_TIME_ARGS (segment_stop));

  entry = gst_matroskademux_do_index_seek (demux, segment_start);
  if (!entry) {
    GST_DEBUG ("No matching seek entry in index");
    goto seek_error;
  }

  /* seek (relative to matroska segment) */
  if (!gst_ebml_read_seek (GST_EBML_READ (demux),
          entry->pos + demux->ebml_segment_start)) {
    GST_DEBUG ("Failed to seek to offset %" G_GUINT64_FORMAT,
        entry->pos + demux->ebml_segment_start);
    goto seek_error;
  }

  GST_DEBUG ("Seeked to offset %" G_GUINT64_FORMAT, entry->pos +
      demux->ebml_segment_start);

  GST_DEBUG ("Committing new seek segment");

  demux->segment_rate = rate;
  demux->segment_play = !!(flags & GST_SEEK_FLAG_SEGMENT);

  demux->segment_start = segment_start;
  demux->segment_stop = segment_stop;

  /* notify start of new segment */
  if (demux->segment_play) {
    GstMessage *msg;

    msg = gst_message_new_segment_start (GST_OBJECT (demux), GST_FORMAT_TIME, demux->segment_start);    /* or entry->time? */
    gst_element_post_message (GST_ELEMENT (demux), msg);
  }

  newsegment_event = gst_event_new_newsegment (FALSE, demux->segment_rate,
      GST_FORMAT_TIME, entry->time, demux->segment_stop, 0);

  GST_UNLOCK (demux);

  GST_DEBUG ("Stopping flush");
  if (flush) {
    gst_matroska_demux_send_event (demux, gst_event_new_flush_stop ());
  }
  gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop ());

  /* send newsegment event to all source pads and update the time */
  gst_matroska_demux_send_event (demux, newsegment_event);
  for (i = 0; i < demux->num_streams; i++)
    demux->src[i]->pos = entry->time;
  demux->pos = entry->time;

  /* restart our task since it might have been stopped when we did the
   * flush. */
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_matroska_demux_loop,
      demux->sinkpad);

  /* streaming can continue now */
  GST_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;

seek_error:

  /* FIXME: shouldn't we either make it a real error or start the task
   * function again so that things can continue from where they left off? */
  GST_DEBUG ("Got a seek error");
  GST_UNLOCK (demux);
  GST_STREAM_UNLOCK (demux->sinkpad);

  return FALSE;
}

static gboolean
gst_matroska_demux_handle_src_event (GstPad * pad, GstEvent * event)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      res = gst_matroska_demux_handle_seek_event (demux, event);
      break;

      /* events we don't need to handle */
    case GST_EVENT_NAVIGATION:
      break;

    default:
      GST_WARNING ("Unhandled event of type %d", GST_EVENT_TYPE (event));
      res = FALSE;
      break;
  }

  gst_object_unref (demux);
  gst_event_unref (event);

  return res;
}

static gboolean
gst_matroska_demux_init_stream (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  gchar *doctype;
  guint version;

  if (!gst_ebml_read_header (ebml, &doctype, &version))
    return FALSE;

  if (!doctype || strcmp (doctype, "matroska") != 0) {
    GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
        ("Input is not a matroska stream (doctype=%s)",
            doctype ? doctype : "none"));
    g_free (doctype);
    return FALSE;
  }
  g_free (doctype);
  if (version > 1) {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("Demuxer version (1) is too old to read stream version %d", version));
    return FALSE;
  }

  /* find segment, must be the next element */
  while (1) {
    guint last_level;

    if (!gst_ebml_peek_id (ebml, &last_level, &id)) {
      GST_DEBUG_OBJECT (demux, "gst_ebml_peek_id() failed!");
      return FALSE;
    }

    if (id == GST_MATROSKA_ID_SEGMENT)
      break;

    /* oi! */
    GST_WARNING ("Expected a Segment ID (0x%x), but received 0x%x!",
        GST_MATROSKA_ID_SEGMENT, id);

    if (!gst_ebml_read_skip (ebml))
      return FALSE;
  }

  /* we now have a EBML segment */
  if (!gst_ebml_read_master (ebml, &id)) {
    GST_DEBUG_OBJECT (demux, "gst_ebml_read_master() failed!");
    return FALSE;
  }

  /* seeks are from the beginning of the segment,
   * after the segment ID/length */
  demux->ebml_segment_start = ebml->offset;

  return TRUE;
}

static gboolean
gst_matroska_demux_parse_tracks (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;

  while (res) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id)) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one track within the "all-tracks" header */
      case GST_MATROSKA_ID_TRACKENTRY:
        if (!gst_matroska_demux_add_stream (demux))
          res = FALSE;
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in track header", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          res = FALSE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return res;
}

static gboolean
gst_matroska_demux_parse_index_cuetrack (GstMatroskaDemux * demux,
    gboolean prevent_eos, GstMatroskaIndex * idx, guint64 length)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  guint32 id;

  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  while (!got_error) {
    if (prevent_eos && length == ebml->offset)
      break;

    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* track number */
      case GST_MATROSKA_ID_CUETRACK:
      {
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          goto error;

        idx->track = num;
        break;
      }

        /* position in file */
      case GST_MATROSKA_ID_CUECLUSTERPOSITION:
      {
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num))
          goto error;

        idx->pos = num;
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in CuesTrackPositions", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          goto error;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return TRUE;

error:
  if (demux->level_up)
    demux->level_up--;

  return FALSE;
}

static gboolean
gst_matroska_demux_parse_index_pointentry (GstMatroskaDemux * demux,
    gboolean prevent_eos, guint64 length)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstMatroskaIndex idx;
  gboolean got_error = FALSE;
  guint32 id;

  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  /* in the end, we hope to fill one entry with a
   * timestamp, a file position and a tracknum */
  idx.pos = (guint64) - 1;
  idx.time = (guint64) - 1;
  idx.track = (guint16) - 1;

  while (!got_error) {
    if (prevent_eos && length == ebml->offset)
      break;

    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_CUETIME:
      {
        guint64 time;

        if (!gst_ebml_read_uint (ebml, &id, &time)) {
          got_error = TRUE;
        } else {
          idx.time = time * demux->time_scale;
        }
        break;
      }

        /* position in the file + track to which it belongs */
      case GST_MATROSKA_ID_CUETRACKPOSITION:
      {
        if (!gst_matroska_demux_parse_index_cuetrack (demux, prevent_eos, &idx,
                length)) {
          got_error = TRUE;
        }
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in cuespoint index", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  /* so let's see if we got what we wanted */
  if (idx.pos != (guint64) - 1 &&
      idx.time != (guint64) - 1 && idx.track != (guint16) - 1) {
    if (demux->num_indexes % 32 == 0) {
      /* re-allocate bigger index */
      demux->index = g_renew (GstMatroskaIndex, demux->index,
          demux->num_indexes + 32);
    }
    GST_DEBUG_OBJECT (demux, "Index entry: pos=%" G_GUINT64_FORMAT
        ", time=%" GST_TIME_FORMAT ", track=%u", idx.pos,
        GST_TIME_ARGS (idx.time), (guint) idx.track);
    demux->index[demux->num_indexes].pos = idx.pos;
    demux->index[demux->num_indexes].time = idx.time;
    demux->index[demux->num_indexes].track = idx.track;
    demux->num_indexes++;
  }

  return (!got_error);
}

static gboolean
gst_matroska_demux_parse_index (GstMatroskaDemux * demux, gboolean prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  guint32 id;
  guint64 length = 0;

  if (prevent_eos) {
    length = gst_ebml_read_get_length (ebml);
  }

  while (!got_error) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == ebml->offset)
      break;

    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_POINTENTRY:
        if (!gst_matroska_demux_parse_index_pointentry (demux, prevent_eos,
                length))
          got_error = TRUE;
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in cues header", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return (!got_error);
}

static gboolean
gst_matroska_demux_parse_info (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;

  while (res) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id)) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_TIMECODESCALE:{
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        demux->time_scale = num;
        break;
      }

      case GST_MATROSKA_ID_DURATION:{
        gdouble num;

        if (!gst_ebml_read_float (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        demux->duration = num * demux->time_scale;
        break;
      }

      case GST_MATROSKA_ID_WRITINGAPP:{
        gchar *text;

        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        demux->writing_app = text;
        break;
      }

      case GST_MATROSKA_ID_MUXINGAPP:{
        gchar *text;

        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        demux->muxing_app = text;
        break;
      }

      case GST_MATROSKA_ID_DATEUTC:{
        gint64 time;

        if (!gst_ebml_read_date (ebml, &id, &time)) {
          res = FALSE;
          break;
        }
        demux->created = time;
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in info header", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          res = FALSE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return res;
}

static gboolean
gst_matroska_demux_parse_metadata_id_simple_tag (GstMatroskaDemux * demux,
    gboolean prevent_eos, guint64 length, GstTagList ** p_taglist)
{
  struct
  {
    gchar *matroska_tagname;
    gchar *gstreamer_tagname;
  }
  tag_conv[] = {
    {
    GST_MATROSKA_TAG_ID_TITLE, GST_TAG_TITLE}, {
    GST_MATROSKA_TAG_ID_AUTHOR, GST_TAG_ARTIST}, {
    GST_MATROSKA_TAG_ID_ALBUM, GST_TAG_ALBUM}, {
    GST_MATROSKA_TAG_ID_COMMENTS, GST_TAG_COMMENT}, {
    GST_MATROSKA_TAG_ID_BITSPS, GST_TAG_BITRATE}, {
    GST_MATROSKA_TAG_ID_ENCODER, GST_TAG_ENCODER}, {
    GST_MATROSKA_TAG_ID_DATE, GST_TAG_DATE}, {
    GST_MATROSKA_TAG_ID_ISRC, GST_TAG_ISRC}, {
    GST_MATROSKA_TAG_ID_COPYRIGHT, GST_TAG_COPYRIGHT}
  };
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  guint32 id;
  gchar *value = NULL;
  gchar *tag = NULL;

  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  while (!got_error) {
    /* read all sub-entries */
    if (prevent_eos && length == ebml->offset)
      break;

    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_TAGNAME:
        g_free (tag);
        if (!gst_ebml_read_ascii (ebml, &id, &tag))
          got_error = TRUE;
        break;

      case GST_MATROSKA_ID_TAGSTRING:
        g_free (value);
        if (!gst_ebml_read_utf8 (ebml, &id, &value))
          got_error = TRUE;
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in metadata collection", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (tag && value) {
    guint i;

    for (i = 0; i < G_N_ELEMENTS (tag_conv); i++) {
      const gchar *tagname_gst = tag_conv[i].gstreamer_tagname;
      const gchar *tagname_mkv = tag_conv[i].matroska_tagname;

      if (strcmp (tagname_mkv, tag) == 0) {
        GValue src = { 0, };
        GValue dest = { 0, };
        GType dest_type = gst_tag_get_type (tagname_gst);

        g_value_init (&src, G_TYPE_STRING);
        g_value_set_string (&src, value);
        g_value_init (&dest, dest_type);
        g_value_transform (&src, &dest);
        g_value_unset (&src);
        gst_tag_list_add_values (*p_taglist, GST_TAG_MERGE_APPEND,
            tagname_gst, &dest, NULL);
        g_value_unset (&dest);
        break;
      }
    }
  }

  g_free (tag);
  g_free (value);

  return (!got_error);
}

static gboolean
gst_matroska_demux_parse_metadata_id_tag (GstMatroskaDemux * demux,
    gboolean prevent_eos, guint64 length, GstTagList ** p_taglist)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  guint32 id;

  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  while (!got_error) {
    /* read all sub-entries */
    if (prevent_eos && length == ebml->offset)
      break;

    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SIMPLETAG:
        if (!gst_matroska_demux_parse_metadata_id_simple_tag (demux,
                prevent_eos, length, p_taglist)) {
          got_error = TRUE;
        }
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in metadata collection", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return (!got_error);
}

static gboolean
gst_matroska_demux_parse_metadata (GstMatroskaDemux * demux,
    gboolean prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstTagList *taglist = gst_tag_list_new ();
  gboolean got_error = FALSE;
  guint64 length = 0;
  guint32 id;

  if (prevent_eos) {
    length = gst_ebml_read_get_length (ebml);
  }

  while (!got_error) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == ebml->offset)
      break;

    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_TAG:
        if (!gst_matroska_demux_parse_metadata_id_tag (demux, prevent_eos,
                length, &taglist)) {
          got_error = TRUE;
        }
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in metadata header", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (gst_structure_n_fields (GST_STRUCTURE (taglist)) > 0) {
    gst_element_found_tags (GST_ELEMENT (ebml), taglist);
  } else {
    gst_tag_list_free (taglist);
  }

  return (!got_error);
}

/*
 * Read signed/unsigned "EBML" numbers.
 * Return: number of bytes processed.
 */

static gint
gst_matroska_ebmlnum_uint (guint8 * data, guint size, guint64 * num)
{
  gint len_mask = 0x80, read = 1, n = 1, num_ffs = 0;
  guint64 total;

  if (size <= 0) {
    return -1;
  }

  total = data[0];
  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  if (read > 8)
    return -1;

  if ((total &= (len_mask - 1)) == len_mask - 1)
    num_ffs++;
  if (size < read)
    return -1;
  while (n < read) {
    if (data[n] == 0xff)
      num_ffs++;
    total = (total << 8) | data[n];
    n++;
  }

  if (read == num_ffs && total != 0)
    *num = G_MAXUINT64;
  else
    *num = total;

  return read;
}

static gint
gst_matroska_ebmlnum_sint (guint8 * data, guint size, gint64 * num)
{
  guint64 unum;
  gint res;

  /* read as unsigned number first */
  if ((res = gst_matroska_ebmlnum_uint (data, size, &unum)) < 0)
    return -1;

  /* make signed */
  if (unum == G_MAXUINT64)
    *num = G_MAXINT64;
  else
    *num = unum - ((1 << ((7 * res) - 1)) - 1);

  return res;
}

/*
 * Mostly used for subtitles. We add void filler data for each
 * lagging stream to make sure we don't deadlock.
 */

static void
gst_matroska_demux_sync_streams (GstMatroskaDemux * demux)
{
  gint stream_nr;
  GstMatroskaTrackContext *context;

  GST_DEBUG ("Sync to %" GST_TIME_FORMAT, GST_TIME_ARGS (demux->pos));

  for (stream_nr = 0; stream_nr < demux->num_streams; stream_nr++) {
    context = demux->src[stream_nr];
    if (context->type != GST_MATROSKA_TRACK_TYPE_SUBTITLE)
      continue;
    GST_DEBUG ("Checking for resync on stream %d (%" GST_TIME_FORMAT ")",
        stream_nr, GST_TIME_ARGS (context->pos));

    /* does it lag? 1 second is a random treshold... */
    if (context->pos + (GST_SECOND / 2) < demux->pos) {
      GstEvent *event;
      static gboolean showed_msg = FALSE;       /* FIXME */

      event = gst_event_new_filler ();

      /* FIXME: fillers in 0.9 aren't specified properly yet 
         event = gst_event_new_filler_stamped (context->pos,
         demux->pos - context->pos); */
      if (!showed_msg) {
        g_message ("%s: fix filler stuff when spec'ed out in core", G_STRLOC);
        showed_msg = TRUE;
      }

      context->pos = demux->pos;

      /* sync */
      GST_DEBUG ("Synchronizing stream %d with others by sending filler "
          "at time %" GST_TIME_FORMAT " and duration %" GST_TIME_FORMAT
          " to time %" GST_TIME_FORMAT, stream_nr,
          GST_TIME_ARGS (context->pos),
          GST_TIME_ARGS (demux->pos - context->pos),
          GST_TIME_ARGS (demux->pos));

      gst_pad_push_event (context->pad, event);
    }
  }
}

static gboolean
gst_matroska_demux_stream_is_first_vorbis_frame (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  if (stream->type == GST_MATROSKA_TRACK_TYPE_AUDIO
      && ((GstMatroskaTrackAudioContext *) stream)->first_frame == TRUE) {
    return (strcmp (stream->codec_id, GST_MATROSKA_CODEC_ID_AUDIO_VORBIS) == 0);
  }
  return FALSE;
}

static gboolean
gst_matroska_demux_push_vorbis_codec_priv_data (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  GstFlowReturn ret;
  GstBuffer *priv;
  guint32 offset, length;
  guchar *p;
  gint i;

  /* start of the stream and vorbis audio, need to send the codec_priv
   * data as first three packets */
  ((GstMatroskaTrackAudioContext *) stream)->first_frame = FALSE;
  p = (guchar *) stream->codec_priv;
  offset = 3;

  for (i = 0; i < 2; i++) {
    length = p[i + 1];
    if (gst_pad_alloc_buffer (stream->pad, GST_BUFFER_OFFSET_NONE,
            length, stream->caps, &priv) != GST_FLOW_OK) {
      return FALSE;
    }

    memcpy (GST_BUFFER_DATA (priv), &p[offset], length);

    ret = gst_pad_push (stream->pad, priv);
    if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)
      return FALSE;

    offset += length;
  }
  length = stream->codec_priv_size - offset;
  if (gst_pad_alloc_buffer (stream->pad, GST_BUFFER_OFFSET_NONE, length,
          stream->caps, &priv) != GST_FLOW_OK) {
    return FALSE;
  }
  memcpy (GST_BUFFER_DATA (priv), &p[offset], length);
  ret = gst_pad_push (stream->pad, priv);
  if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)
    return FALSE;

  return TRUE;
}

static gboolean
gst_matroska_demux_stream_is_wavpack (GstMatroskaTrackContext * stream)
{
  if (stream->type == GST_MATROSKA_TRACK_TYPE_AUDIO) {
    return (strcmp (stream->codec_id,
            GST_MATROSKA_CODEC_ID_AUDIO_WAVPACK4) == 0);
  }
  return FALSE;
}

static gboolean
gst_matroska_demux_add_wvpk_header (GstMatroskaTrackContext * stream,
    gint block_length, GstBuffer ** buf)
{
  GstBuffer *newbuf;
  guint8 *data;
  guint newlen;

  /* we need to reconstruct the header of the wavpack block */
  Wavpack4Header wvh;

  wvh.ck_id[0] = 'w';
  wvh.ck_id[1] = 'v';
  wvh.ck_id[2] = 'p';
  wvh.ck_id[3] = 'k';
  /* -20 because ck_size is the size of the wavpack block -8
   * and lace_size is the size of the wavpack block + 12
   * (the three guint32 of the header that already are in the buffer) */
  wvh.ck_size = block_length + sizeof (Wavpack4Header) - 20;
  wvh.version = GST_READ_UINT16_LE (stream->codec_priv);
  wvh.track_no = 0;
  wvh.index_no = 0;
  wvh.total_samples = -1;
  wvh.block_index = 0;

  /* block_samples, flags and crc are already in the buffer */
  newlen = block_length + sizeof (Wavpack4Header) - 12;
  if (gst_pad_alloc_buffer (stream->pad, GST_BUFFER_OFFSET_NONE, newlen,
          stream->caps, &newbuf) != GST_FLOW_OK) {
    return FALSE;
  }

  data = GST_BUFFER_DATA (newbuf);
  data[0] = 'w';
  data[1] = 'v';
  data[2] = 'p';
  data[3] = 'k';
  GST_WRITE_UINT32_LE (data + 4, wvh.ck_size);
  GST_WRITE_UINT16_LE (data + 8, wvh.version);
  GST_WRITE_UINT8 (data + 10, wvh.track_no);
  GST_WRITE_UINT8 (data + 11, wvh.index_no);
  GST_WRITE_UINT32_LE (data + 12, wvh.total_samples);
  GST_WRITE_UINT32_LE (data + 16, wvh.block_index);
  g_memmove (data + 20, GST_BUFFER_DATA (*buf), block_length);
  gst_buffer_stamp (newbuf, *buf);
  gst_buffer_unref (*buf);
  *buf = newbuf;
  return TRUE;
}

static gboolean
gst_matroska_demux_parse_blockgroup (GstMatroskaDemux * demux,
    guint64 cluster_time)
{
  GstMatroskaTrackContext *stream = NULL;
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  gboolean readblock = FALSE;
  guint32 id;
  guint64 block_duration = 0;
  GstBuffer *buf = NULL;
  gint stream_num = 0, n, laces = 0;
  guint size = 0;
  gint *lace_size = NULL;
  gint64 time = 0;

  while (!got_error) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      goto error;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one block inside the group. Note, block parsing is one
         * of the harder things, so this code is a bit complicated.
         * See http://www.matroska.org/ for documentation. */
      case GST_MATROSKA_ID_BLOCK:
      {
        guint64 num;
        guint8 *data;
        gint flags = 0;

        if (!gst_ebml_read_buffer (ebml, &id, &buf)) {
          got_error = TRUE;
          break;
        }

        data = GST_BUFFER_DATA (buf);
        size = GST_BUFFER_SIZE (buf);

        /* first byte(s): blocknum */
        if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0) {
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL), ("Data error"));
          gst_buffer_unref (buf);
          got_error = TRUE;
          break;
        }
        data += n;
        size -= n;

        /* fetch stream from num */
        stream_num = gst_matroska_demux_stream_from_num (demux, num);
        if (size <= 3 || stream_num < 0 || stream_num >= demux->num_streams) {
          gst_buffer_unref (buf);
          GST_WARNING ("Invalid stream %d or size %u", stream_num, size);
          break;
        }

        stream = demux->src[stream_num];

        /* time (relative to cluster time) */
        time = ((gint16) GST_READ_UINT16_BE (data)) * demux->time_scale;
        data += 2;
        size -= 2;
        flags = GST_READ_UINT8 (data);
        data += 1;
        size -= 1;

        switch ((flags & 0x06) >> 1) {
          case 0x0:            /* no lacing */
            laces = 1;
            lace_size = g_new (gint, 1);
            lace_size[0] = size;
            break;

          case 0x1:            /* xiph lacing */
          case 0x2:            /* fixed-size lacing */
          case 0x3:            /* EBML lacing */
            if (size == 0) {
              got_error = TRUE;
              break;
            }
            laces = GST_READ_UINT8 (data) + 1;
            data += 1;
            size -= 1;
            lace_size = g_new0 (gint, laces);

            switch ((flags & 0x06) >> 1) {
              case 0x1:        /* xiph lacing */  {
                guint temp, total = 0;

                for (n = 0; !got_error && n < laces - 1; n++) {
                  while (1) {
                    if (size == 0) {
                      got_error = TRUE;
                      break;
                    }
                    temp = GST_READ_UINT8 (data);
                    lace_size[n] += temp;
                    data += 1;
                    size -= 1;
                    if (temp != 0xff)
                      break;
                  }
                  total += lace_size[n];
                }
                lace_size[n] = size - total;
                break;
              }

              case 0x2:        /* fixed-size lacing */
                for (n = 0; n < laces; n++)
                  lace_size[n] = size / laces;
                break;

              case 0x3:        /* EBML lacing */  {
                guint total;

                if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0) {
                  GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                      ("Data error"));
                  got_error = TRUE;
                  break;
                }
                data += n;
                size -= n;
                total = lace_size[0] = num;
                for (n = 1; !got_error && n < laces - 1; n++) {
                  gint64 snum;
                  gint r;

                  if ((r = gst_matroska_ebmlnum_sint (data, size, &snum)) < 0) {
                    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                        ("Data error"));
                    got_error = TRUE;
                    break;
                  }
                  data += r;
                  size -= r;
                  lace_size[n] = lace_size[n - 1] + snum;
                  total += lace_size[n];
                }
                if (n < laces)
                  lace_size[n] = size - total;
                break;
              }
            }
            break;
        }

        if (gst_matroska_demux_stream_is_first_vorbis_frame (demux, stream)) {
          if (!gst_matroska_demux_push_vorbis_codec_priv_data (demux, stream))
            got_error = TRUE;
        }

        if (got_error)
          break;

        readblock = TRUE;
        break;
      }

      case GST_MATROSKA_ID_BLOCKDURATION:{
        if (!gst_ebml_read_uint (ebml, &id, &block_duration))
          got_error = TRUE;
        break;
      }

      case GST_MATROSKA_ID_REFERENCEBLOCK:{
        /* FIXME: this segfaults
           gint64 num;
           if (!gst_ebml_read_sint (ebml, &id, &num)) {
           res = FALSE;
           break;
           }
           GST_WARNING ("FIXME: implement support for ReferenceBlock");
         */
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in blockgroup data", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (!got_error && readblock) {
    guint64 duration = 0;

    stream = demux->src[stream_num];

    if (block_duration) {
      duration = block_duration * demux->time_scale;
    } else if (stream->default_duration) {
      duration = stream->default_duration;
    }

    for (n = 0; n < laces; n++) {
      GstFlowReturn ret;
      GstBuffer *sub;

      if (lace_size[n] == 0)
        continue;

      sub = gst_buffer_create_sub (buf,
          GST_BUFFER_SIZE (buf) - size, lace_size[n]);

      if (cluster_time != GST_CLOCK_TIME_NONE) {
        if (time < 0 && (-time) > cluster_time)
          GST_BUFFER_TIMESTAMP (sub) = cluster_time;
        else
          GST_BUFFER_TIMESTAMP (sub) = cluster_time + time;

        demux->pos = GST_BUFFER_TIMESTAMP (sub);
      }
      stream->pos = demux->pos;
      gst_matroska_demux_sync_streams (demux);

      if (gst_matroska_demux_stream_is_wavpack (stream)) {
        if (!gst_matroska_demux_add_wvpk_header (stream, lace_size[n], &sub)) {
          got_error = TRUE;
        }
      }

      /* FIXME: do all laces have the same lenght? */
      if (duration) {
        GST_BUFFER_DURATION (sub) = duration / laces;
        stream->pos += GST_BUFFER_DURATION (sub);
      }

      GST_DEBUG ("Pushing data of size %d for stream %d, time=%"
          GST_TIME_FORMAT " and duration=%" GST_TIME_FORMAT,
          GST_BUFFER_SIZE (sub), stream_num,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (sub)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (sub)));

      gst_buffer_set_caps (sub, GST_PAD_CAPS (stream->pad));
      ret = gst_pad_push (stream->pad, sub);
      if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED)
        got_error = TRUE;

      size -= lace_size[n];
    }
  }

  if (0) {
  error:
    got_error = TRUE;
  }

  if (readblock)
    gst_buffer_unref (buf);
  g_free (lace_size);

  return (!got_error);
}

static gboolean
gst_matroska_demux_parse_cluster (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  guint64 cluster_time = GST_CLOCK_TIME_NONE;
  guint32 id;

  while (!got_error) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_CLUSTERTIMECODE:
      {
        guint64 num;

        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          got_error = TRUE;
        } else {
          cluster_time = num * demux->time_scale;
        }
        break;
      }

        /* a group of blocks inside a cluster */
      case GST_MATROSKA_ID_BLOCKGROUP:
        if (!gst_ebml_read_master (ebml, &id)) {
          got_error = TRUE;
        } else {
          if (!gst_matroska_demux_parse_blockgroup (demux, cluster_time))
            got_error = TRUE;
        }
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in cluster data", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return (!got_error);
}

static gboolean
gst_matroska_demux_parse_contents_seekentry (GstMatroskaDemux * demux,
    gboolean * p_run_loop)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  guint64 seek_pos = (guint64) - 1;
  guint32 seek_id = 0;
  guint32 id;

  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  while (!got_error) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SEEKID:
      {
        guint64 t;

        if (!gst_ebml_read_uint (ebml, &id, &t)) {
          got_error = TRUE;
        } else {
          seek_id = t;
        }
        break;
      }

      case GST_MATROSKA_ID_SEEKPOSITION:
      {
        guint64 t;

        if (!gst_ebml_read_uint (ebml, &id, &t)) {
          got_error = TRUE;
        } else {
          seek_pos = t;
        }
        break;
      }

      default:
        GST_WARNING ("Unknown seekhead ID 0x%x", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (got_error)
    return FALSE;

  if (!seek_id || seek_pos == (guint64) - 1) {
    GST_WARNING ("Incomplete seekhead entry (0x%x/%"
        G_GUINT64_FORMAT ")", seek_id, seek_pos);
    return TRUE;
  }

  switch (seek_id) {
    case GST_MATROSKA_ID_CUES:
    case GST_MATROSKA_ID_TAGS:
    {
      guint level_up = demux->level_up;
      guint64 before_pos, length;
      GstEbmlLevel *level;

      /* remember */
      length = gst_ebml_read_get_length (ebml);
      before_pos = ebml->offset;

      /* check for validity */
      if (seek_pos + demux->ebml_segment_start + 12 >= length) {
        GST_WARNING_OBJECT (demux,
            "Seekhead reference lies outside file!" " (%"
            G_GUINT64_FORMAT "+%" G_GUINT64_FORMAT "+12 >= %"
            G_GUINT64_FORMAT ")", seek_pos, demux->ebml_segment_start, length);
        break;
      }

      /* seek */
      if (!gst_ebml_read_seek (ebml, seek_pos + demux->ebml_segment_start))
        return FALSE;

      /* we don't want to lose our seekhead level, so we add
       * a dummy. This is a crude hack. */
      level = g_new (GstEbmlLevel, 1);
      level->start = 0;
      level->length = G_MAXUINT64;
      ebml->level = g_list_append (ebml->level, level);

      /* check ID */
      if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
        return FALSE;

      if (id != seek_id) {
        g_warning ("We looked for ID=0x%x but got ID=0x%x (pos=%"
            G_GUINT64_FORMAT ")", seek_id, id,
            seek_pos + demux->ebml_segment_start);
        goto finish;
      }

      /* read master + parse */
      switch (id) {
        case GST_MATROSKA_ID_CUES:
          if (!gst_ebml_read_master (ebml, &id))
            return FALSE;
          if (!gst_matroska_demux_parse_index (demux, TRUE))
            return FALSE;
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          else
            demux->index_parsed = TRUE;
          break;
        case GST_MATROSKA_ID_TAGS:
          if (!gst_ebml_read_master (ebml, &id))
            return FALSE;
          if (!gst_matroska_demux_parse_metadata (demux, TRUE))
            return FALSE;
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          else
            demux->metadata_parsed = TRUE;
          break;
      }

      /* used to be here in 0.8 version, but makes mewmew sample not work */
      /* if (*p_run_loop == FALSE) break; */

    finish:
      /* remove dummy level */
      while (ebml->level) {
        guint64 length;

        level = g_list_last (ebml->level)->data;
        ebml->level = g_list_remove (ebml->level, level);
        length = level->length;
        g_free (level);
        if (length == G_MAXUINT64)
          break;
      }

      /* seek back */
      (void) gst_ebml_read_seek (ebml, before_pos);
      demux->level_up = level_up;
      break;
    }

    default:
      GST_INFO ("Ignoring seekhead entry for ID=0x%x", seek_id);
      break;
  }

  return (!got_error);
}

static gboolean
gst_matroska_demux_parse_contents (GstMatroskaDemux * demux,
    gboolean * p_run_loop)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  guint32 id;

  while (!got_error) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SEEKENTRY:
      {
        if (!gst_matroska_demux_parse_contents_seekentry (demux, p_run_loop))
          got_error = TRUE;
        break;
      }

      default:
        GST_WARNING ("Unknown seekhead ID 0x%x", id);
        /* fall-through */

      case GST_EBML_ID_VOID:
        if (!gst_ebml_read_skip (ebml))
          got_error = TRUE;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return (!got_error);
}

/* returns FALSE on error, otherwise TRUE */
static gboolean
gst_matroska_demux_loop_stream_parse_id (GstMatroskaDemux * demux,
    guint32 id, gboolean * p_run_loop)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  switch (id) {
      /* stream info */
    case GST_MATROSKA_ID_INFO:
      if (!gst_ebml_read_master (ebml, &id))
        return FALSE;
      if (!gst_matroska_demux_parse_info (demux))
        return FALSE;
      break;

      /* track info headers */
    case GST_MATROSKA_ID_TRACKS:
    {
      if (!gst_ebml_read_master (ebml, &id))
        return FALSE;
      if (!gst_matroska_demux_parse_tracks (demux))
        return FALSE;
      break;
    }

      /* stream index */
    case GST_MATROSKA_ID_CUES:
    {
      if (!demux->index_parsed) {
        if (!gst_ebml_read_master (ebml, &id))
          return FALSE;
        if (!gst_matroska_demux_parse_index (demux, FALSE))
          return FALSE;
      } else {
        if (!gst_ebml_read_skip (ebml))
          return FALSE;
      }
      break;
    }

      /* metadata */
    case GST_MATROSKA_ID_TAGS:
    {
      if (!demux->index_parsed) {
        if (!gst_ebml_read_master (ebml, &id))
          return FALSE;
        if (!gst_matroska_demux_parse_metadata (demux, FALSE))
          return FALSE;
      } else {
        if (!gst_ebml_read_skip (ebml))
          return FALSE;
      }
      break;
    }

      /* file index (if seekable, seek to Cues/Tags to parse it) */
    case GST_MATROSKA_ID_SEEKHEAD:
    {
      if (!gst_ebml_read_master (ebml, &id))
        return FALSE;
      if (!gst_matroska_demux_parse_contents (demux, p_run_loop))
        return FALSE;
      break;
    }

    case GST_MATROSKA_ID_CLUSTER:
    {
      if (demux->state != GST_MATROSKA_DEMUX_STATE_DATA) {
        demux->state = GST_MATROSKA_DEMUX_STATE_DATA;
        /* FIXME: different streams might have different lengths! */
        /* send initial discont */
        gst_matroska_demux_send_event (demux,
            gst_event_new_newsegment (FALSE, 1.0,
                GST_FORMAT_TIME, 0, demux->duration, 0));

        GST_DEBUG_OBJECT (demux, "signaling no more pads");
        gst_element_no_more_pads (GST_ELEMENT (demux));
      } else {
        if (!gst_ebml_read_master (ebml, &id))
          return FALSE;

        /* The idea is that we parse one cluster per loop and
         * then break out of the loop here. In the next call
         * of the loopfunc, we will get back here with the
         * next cluster. If an error occurs, we didn't
         * actually push a buffer, but we still want to break
         * out of the loop to handle a possible error. We'll
         * get back here if it's recoverable. */
        if (!gst_matroska_demux_parse_cluster (demux))
          return FALSE;
        *p_run_loop = FALSE;
      }
      break;
    }

    default:
      GST_WARNING ("Unknown matroska file header ID 0x%x at %"
          G_GUINT64_FORMAT, id, GST_EBML_READ (demux)->offset);
      /* fall-through */

    case GST_EBML_ID_VOID:
    {
      if (!gst_ebml_read_skip (ebml))
        return FALSE;
      break;
    }
  }

  return TRUE;
}

static gboolean
gst_matroska_demux_loop_stream (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean got_error = FALSE;
  gboolean run_loop = TRUE;
  guint32 id;

  /* we've found our segment, start reading the different contents in here */
  while (run_loop && !got_error) {
    if (!gst_ebml_peek_id (ebml, &demux->level_up, &id))
      return FALSE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    if (!gst_matroska_demux_loop_stream_parse_id (demux, id, &run_loop))
      got_error = TRUE;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return (!got_error);
}

static void
gst_matroska_demux_loop (GstPad * pad)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean ret;

  /* first, if we're to start, let's actually get starting */
  if (demux->state == GST_MATROSKA_DEMUX_STATE_START) {
    if (!gst_matroska_demux_init_stream (demux)) {
      GST_DEBUG_OBJECT (demux, "init stream failed!");
      goto eos_and_pause;
    }
    demux->state = GST_MATROSKA_DEMUX_STATE_HEADER;
  }

  ret = gst_matroska_demux_loop_stream (demux);

  /* check if we're at the end of a configured segment */
  if (demux->segment_play && GST_CLOCK_TIME_IS_VALID (demux->segment_stop)) {
    guint i;

    for (i = 0; i < demux->num_streams; i++) {
      if (demux->src[i]->pos >= demux->segment_stop) {
        GST_LOG ("Reached end of segment (%" G_GUINT64_FORMAT "-%"
            G_GUINT64_FORMAT ") on pad %s:%s", demux->segment_start,
            demux->segment_stop, GST_DEBUG_PAD_NAME (demux->src[i]->pad));
        gst_element_post_message (GST_ELEMENT (demux),
            gst_message_new_segment_done (GST_OBJECT (demux), GST_FORMAT_TIME,
                demux->segment_stop));
        goto pause;
      }
    }
  }

  if (ebml->offset == gst_ebml_read_get_length (ebml)) {
    if (demux->segment_play) {
      GST_LOG ("Reached end of stream and segment, posting message");
      gst_element_post_message (GST_ELEMENT (demux),
          gst_message_new_segment_done (GST_OBJECT (demux), GST_FORMAT_TIME,
              demux->duration));
      goto pause;
    }

    GST_LOG ("Reached end of stream, sending EOS");
    goto eos_and_pause;
  }

  if (ret == FALSE) {
    GST_LOG ("Error processing stream, sending EOS");
    goto eos_and_pause;
  }

  /* all is fine */
  gst_object_unref (demux);
  return;

eos_and_pause:
  gst_matroska_demux_send_event (demux, gst_event_new_eos ());
  /* fallthrough */
pause:
  GST_LOG_OBJECT (demux, "pausing task");
  gst_pad_pause_task (demux->sinkpad);
  gst_object_unref (demux);
}

static gboolean
gst_matroska_demux_sink_activate (GstPad * sinkpad)
{
  if (gst_pad_check_pull_range (sinkpad))
    return gst_pad_activate_pull (sinkpad, TRUE);

  return FALSE;
}

static gboolean
gst_matroska_demux_sink_activate_pull (GstPad * sinkpad, gboolean active)
{
  if (active) {
    /* if we have a scheduler we can start the task */
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_matroska_demux_loop,
        sinkpad);
  } else {
    gst_pad_stop_task (sinkpad);
  }

  return TRUE;
}

static GstCaps *
gst_matroska_demux_video_caps (GstMatroskaTrackVideoContext *
    videocontext, const gchar * codec_id, gpointer data, guint size,
    gchar ** codec_name)
{
  GstMatroskaTrackContext *context = (GstMatroskaTrackContext *) videocontext;
  GstCaps *caps = NULL;

  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC)) {
    gst_riff_strf_vids *vids = NULL;

    if (data) {
      vids = (gst_riff_strf_vids *) data;

      /* assure size is big enough */
      if (size < 24) {
        GST_WARNING ("Too small BITMAPINFOHEADER (%d bytes)", size);
        return NULL;
      }
      if (size < sizeof (gst_riff_strf_vids)) {
        vids =
            (gst_riff_strf_vids *) g_realloc (vids,
            sizeof (gst_riff_strf_vids));
      }

      /* little-endian -> byte-order */
      vids->size = GUINT32_FROM_LE (vids->size);
      vids->width = GUINT32_FROM_LE (vids->width);
      vids->height = GUINT32_FROM_LE (vids->height);
      vids->planes = GUINT16_FROM_LE (vids->planes);
      vids->bit_cnt = GUINT16_FROM_LE (vids->bit_cnt);
      vids->compression = GUINT32_FROM_LE (vids->compression);
      vids->image_size = GUINT32_FROM_LE (vids->image_size);
      vids->xpels_meter = GUINT32_FROM_LE (vids->xpels_meter);
      vids->ypels_meter = GUINT32_FROM_LE (vids->ypels_meter);
      vids->num_colors = GUINT32_FROM_LE (vids->num_colors);
      vids->imp_colors = GUINT32_FROM_LE (vids->imp_colors);

      caps = gst_riff_create_video_caps (vids->compression, NULL, vids,
          NULL, NULL, codec_name);
    } else {
      caps = gst_riff_create_video_template_caps ();
    }
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED)) {
    /* how nice, this is undocumented... */
    if (videocontext != NULL) {
      guint32 fourcc = 0;

      switch (videocontext->fourcc) {
        case GST_MAKE_FOURCC ('I', '4', '2', '0'):
          if (codec_name)
            *codec_name = g_strdup ("Raw planar YUV 4:2:0");
          fourcc = videocontext->fourcc;
          break;
        case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
          if (codec_name)
            *codec_name = g_strdup ("Raw packed YUV 4:2:2");
          fourcc = videocontext->fourcc;
          break;

        default:
          GST_DEBUG ("Unknown fourcc " GST_FOURCC_FORMAT,
              GST_FOURCC_ARGS (videocontext->fourcc));
          return NULL;
      }

      caps = gst_caps_new_simple ("video/x-raw-yuv",
          "format", GST_TYPE_FOURCC, fourcc, NULL);
    } else {
      caps = gst_caps_from_string ("video/x-raw-yuv, "
          "format = (fourcc) { I420, YUY2, YV12 }");
    }
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP)) {
    caps = gst_caps_new_simple ("video/x-divx",
        "divxversion", G_TYPE_INT, 4, NULL);
    if (codec_name)
      *codec_name = g_strdup ("MPEG-4 simple profile");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AP)) {
    caps = gst_caps_new_full (gst_structure_new ("video/x-divx",
            "divxversion", G_TYPE_INT, 5, NULL),
        gst_structure_new ("video/x-xvid", NULL),
        gst_structure_new ("video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL), NULL);
    if (codec_name) {
      if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP))
        *codec_name = g_strdup ("MPEG-4 advanced simple profile");
      else
        *codec_name = g_strdup ("MPEG-4 advanced profile");
    }
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3)) {
    caps = gst_caps_new_full (gst_structure_new ("video/x-divx",
            "divxversion", G_TYPE_INT, 3, NULL),
        gst_structure_new ("video/x-msmpeg",
            "msmpegversion", G_TYPE_INT, 43, NULL), NULL);
    if (codec_name)
      *codec_name = g_strdup ("Microsoft MPEG-4 v.3");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2)) {
    gint mpegversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1))
      mpegversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2))
      mpegversion = 2;
    else
      g_assert (0);

    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "mpegversion", G_TYPE_INT, mpegversion, NULL);
    if (codec_name)
      *codec_name = g_strdup_printf ("MPEG-%d video", mpegversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MJPEG)) {
    caps = gst_caps_new_simple ("image/jpeg", NULL);
    if (codec_name)
      *codec_name = g_strdup ("Motion-JPEG");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AVC)) {
    caps = gst_caps_new_simple ("video/x-h264", NULL);
    if (data) {
      GstBuffer *priv = gst_buffer_new_and_alloc (size);

      memcpy (GST_BUFFER_DATA (priv), data, size);
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
      gst_buffer_unref (priv);

    }
    if (codec_name)
      *codec_name = g_strdup ("H264");
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps", codec_id);
    return NULL;
  }

  if (caps != NULL) {
    int i;
    GstStructure *structure;

    for (i = 0; i < gst_caps_get_size (caps); i++) {
      structure = gst_caps_get_structure (caps, i);
      if (videocontext != NULL) {
        GST_DEBUG ("video size %dx%d, target display size %dx%d (any unit)",
            videocontext->pixel_width,
            videocontext->pixel_height,
            videocontext->display_width, videocontext->display_height);
        /* pixel width and height are the w and h of the video in pixels */
        if (videocontext->pixel_width > 0 && videocontext->pixel_height > 0) {
          gint w = videocontext->pixel_width;
          gint h = videocontext->pixel_height;

          gst_structure_set (structure,
              "width", G_TYPE_INT, w, "height", G_TYPE_INT, h, NULL);
        } else {
          gst_structure_set (structure,
              "width", GST_TYPE_INT_RANGE, 16, 4096,
              "height", GST_TYPE_INT_RANGE, 16, 4096, NULL);
        }

        if (videocontext->display_width > 0 && videocontext->display_height > 0) {
          int n, d;

          /* calculate the pixel aspect ratio using the display and pixel w/h */
          n = videocontext->display_width * videocontext->pixel_height;
          d = videocontext->display_height * videocontext->pixel_width;
          GST_DEBUG ("setting PAR to %d/%d", n, d);
          gst_structure_set (structure, "pixel-aspect-ratio",
              GST_TYPE_FRACTION,
              videocontext->display_width * videocontext->pixel_height,
              videocontext->display_height * videocontext->pixel_width, NULL);
        }

        if (context->default_duration > 0) {
          gdouble framerate = (gdouble) GST_SECOND / context->default_duration;

          gst_structure_set (structure,
              "framerate", G_TYPE_DOUBLE, framerate, NULL);
        } else {
          /* sort of a hack to get most codecs to support,
           * even if the default_duration is missing */
          gst_structure_set (structure, "framerate", G_TYPE_DOUBLE,
              (gdouble) 25.0, NULL);
        }
      } else {
        gst_structure_set (structure,
            "width", GST_TYPE_INT_RANGE, 16, 4096,
            "height", GST_TYPE_INT_RANGE, 16, 4096,
            "framerate", GST_TYPE_DOUBLE_RANGE, 0.0, G_MAXDOUBLE, NULL);
      }
    }
  }

  return caps;
}

/*
 * Some AAC specific code... *sigh*
 */

static gint
aac_rate_idx (gint rate)
{
  if (92017 <= rate)
    return 0;
  else if (75132 <= rate)
    return 1;
  else if (55426 <= rate)
    return 2;
  else if (46009 <= rate)
    return 3;
  else if (37566 <= rate)
    return 4;
  else if (27713 <= rate)
    return 5;
  else if (23004 <= rate)
    return 6;
  else if (18783 <= rate)
    return 7;
  else if (13856 <= rate)
    return 8;
  else if (11502 <= rate)
    return 9;
  else if (9391 <= rate)
    return 10;
  else
    return 11;
}

static gint
aac_profile_idx (const gchar * codec_id)
{
  gint profile;

  if (strlen (codec_id) <= 12)
    profile = 3;
  else if (!strncmp (&codec_id[12], "MAIN", 4))
    profile = 0;
  else if (!strncmp (&codec_id[12], "LC", 2))
    profile = 1;
  else if (!strncmp (&codec_id[12], "SSR", 3))
    profile = 2;
  else
    profile = 3;

  return profile;
}

#define AAC_SYNC_EXTENSION_TYPE 0x02b7

static GstCaps *
gst_matroska_demux_audio_caps (GstMatroskaTrackAudioContext *
    audiocontext, const gchar * codec_id, gpointer data, guint size,
    gchar ** codec_name)
{
  GstMatroskaTrackContext *context = (GstMatroskaTrackContext *) audiocontext;
  GstCaps *caps = NULL;

  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3)) {
    gint layer = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1))
      layer = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2))
      layer = 2;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3))
      layer = 3;
    else
      g_assert (0);

    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, layer, NULL);
    if (codec_name)
      *codec_name = g_strdup_printf ("MPEG-1 layer %d", layer);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE)) {
    gint endianness = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE))
      endianness = G_BIG_ENDIAN;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE))
      endianness = G_LITTLE_ENDIAN;
    else
      g_assert (0);

    if (context != NULL) {
      caps = gst_caps_new_simple ("audio/x-raw-int",
          "width", G_TYPE_INT, audiocontext->bitdepth,
          "depth", G_TYPE_INT, audiocontext->bitdepth,
          "signed", G_TYPE_BOOLEAN, audiocontext->bitdepth == 8, NULL);
    } else {
      caps = gst_caps_from_string ("audio/x-raw-int, "
          "signed = (boolean) { TRUE, FALSE }, "
          "depth = (int) { 8, 16 }, " "width = (int) { 8, 16 }");
    }
    gst_caps_set_simple (caps, "endianness", G_TYPE_INT, endianness, NULL);
    if (codec_name && audiocontext)
      *codec_name = g_strdup_printf ("Raw %d-bits PCM audio",
          audiocontext->bitdepth);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT)) {
    caps = gst_caps_new_simple ("audio/x-raw-float",
        "endianness", G_TYPE_INT, G_BYTE_ORDER,
        "buffer-frames", GST_TYPE_INT_RANGE, 1, G_MAXINT, NULL);
    if (audiocontext != NULL) {
      gst_caps_set_simple (caps,
          "width", G_TYPE_INT, audiocontext->bitdepth, NULL);
    } else {
      gst_caps_set_simple (caps, "width", GST_TYPE_INT_RANGE, 32, 64, NULL);
    }
    if (codec_name && audiocontext)
      *codec_name = g_strdup_printf ("Raw %d-bits floating-point audio",
          audiocontext->bitdepth);
  } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AC3,
          strlen (GST_MATROSKA_CODEC_ID_AUDIO_AC3))) {
    caps = gst_caps_new_simple ("audio/x-ac3", NULL);
    if (codec_name)
      *codec_name = g_strdup ("AC-3 audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_DTS)) {
    caps = gst_caps_new_simple ("audio/x-dts", NULL);
    if (codec_name)
      *codec_name = g_strdup ("DTS audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_VORBIS)) {
    caps = gst_caps_new_simple ("audio/x-vorbis", NULL);
    /* vorbis decoder does tags */
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_ACM)) {
    gst_riff_strf_auds *auds = NULL;

    if (data) {
      auds = (gst_riff_strf_auds *) data;

      /* little-endian -> byte-order */
      auds->format = GUINT16_FROM_LE (auds->format);
      auds->channels = GUINT16_FROM_LE (auds->channels);
      auds->rate = GUINT32_FROM_LE (auds->rate);
      auds->av_bps = GUINT32_FROM_LE (auds->av_bps);
      auds->blockalign = GUINT16_FROM_LE (auds->blockalign);
      auds->size = GUINT16_FROM_LE (auds->size);

      caps = gst_riff_create_audio_caps (auds->format, NULL, auds, NULL,
          NULL, codec_name);
    } else {
      caps = gst_riff_create_audio_template_caps ();
    }
  } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG2,
          strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG2)) ||
      !strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG4,
          strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG4))) {
    gint mpegversion = -1;
    GstBuffer *priv = NULL;

    if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG2,
            strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG2)))
      mpegversion = 2;
    else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG4,
            strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG4))) {
      gint rate_idx, profile;
      guint8 *data;

      mpegversion = 4;

      if (audiocontext) {
        /* make up decoderspecificdata */
        priv = gst_buffer_new_and_alloc (5);
        data = GST_BUFFER_DATA (priv);
        rate_idx = aac_rate_idx (audiocontext->samplerate);
        profile = aac_profile_idx (codec_id);

        data[0] = ((profile + 1) << 3) | ((rate_idx & 0xE) >> 1);
        data[1] = ((rate_idx & 0x1) << 7) | (audiocontext->channels << 3);

        if (g_strrstr (codec_id, "SBR")) {
          /* HE-AAC (aka SBR AAC) */
          audiocontext->samplerate *= 2;
          rate_idx = aac_rate_idx (audiocontext->samplerate);
          data[2] = AAC_SYNC_EXTENSION_TYPE >> 3;
          data[3] = ((AAC_SYNC_EXTENSION_TYPE & 0x07) << 5) | 5;
          data[4] = (1 << 7) | (rate_idx << 3);
        } else {
          GST_BUFFER_SIZE (priv) = 2;
        }
      }
    } else
      g_assert (0);

    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, mpegversion,
        "framed", G_TYPE_BOOLEAN, TRUE, NULL);
    if (priv) {
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
    }
    if (codec_name)
      *codec_name = g_strdup_printf ("MPEG-%d AAC audio", mpegversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_TTA)) {
    if (audiocontext != NULL) {
      caps = gst_caps_new_simple ("audio/x-tta",
          "width", G_TYPE_INT, audiocontext->bitdepth, NULL);
    } else {
      caps = gst_caps_from_string ("audio/x-tta, "
          "width = (int) { 8, 16, 24 }");
    }
    if (codec_name)
      *codec_name = g_strdup ("TTA audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_WAVPACK4)) {
    if (audiocontext != NULL) {
      caps = gst_caps_new_simple ("audio/x-wavpack",
          "width", G_TYPE_INT, audiocontext->bitdepth,
          "framed", G_TYPE_BOOLEAN, TRUE, NULL);
    } else {
      caps = gst_caps_from_string ("audio/x-wavpack, "
          "width = (int) { 8, 16, 24 }, " "framed = (boolean) true");
    }
    if (codec_name)
      *codec_name = g_strdup ("Wavpack audio");
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps", codec_id);
    return NULL;
  }

  if (caps != NULL) {
    GstStructure *structure;
    int i;

    for (i = 0; i < gst_caps_get_size (caps); i++) {
      structure = gst_caps_get_structure (caps, i);
      if (audiocontext != NULL) {
        if (audiocontext->samplerate > 0 && audiocontext->channels > 0) {
          gst_structure_set (structure,
              "channels", G_TYPE_INT, audiocontext->channels,
              "rate", G_TYPE_INT, audiocontext->samplerate, NULL);
        }
      } else {
        gst_structure_set (structure,
            "channels", GST_TYPE_INT_RANGE, 1, 6,
            "rate", GST_TYPE_INT_RANGE, 4000, 96000, NULL);
      }
    }
  }

  return caps;
}

static GstCaps *
gst_matroska_demux_complex_caps (GstMatroskaTrackComplexContext *
    complexcontext, const gchar * codec_id, gpointer data, guint size)
{
  GstCaps *caps = NULL;

  GST_DEBUG ("Unknown complex stream: codec_id='%s'", codec_id);

  return caps;
}

static GstCaps *
gst_matroska_demux_subtitle_caps (GstMatroskaTrackSubtitleContext *
    subtitlecontext, const gchar * codec_id, gpointer data, guint size)
{
  /*GstMatroskaTrackContext *context =
     (GstMatroskaTrackContext *) subtitlecontext; */
  GstCaps *caps = NULL;

  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8)) {
    caps = gst_caps_new_simple ("text/plain", NULL);
  } else {
    GST_DEBUG ("Unknown subtitle stream: codec_id='%s'", codec_id);
    caps = gst_caps_new_simple ("application/x-subtitle-unknown", NULL);
  }

  return caps;
}

static GstStateChangeReturn
gst_matroska_demux_change_state (GstElement * element,
    GstStateChange transition)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  /* handle upwards state changes here */
  switch (transition) {
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  /* handle downwards state changes */
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_matroska_demux_reset (GST_ELEMENT (demux));
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_matroska_demux_plugin_init (GstPlugin * plugin)
{
  GstCaps *videosrccaps;
  GstCaps *audiosrccaps;
  GstCaps *subtitlesrccaps;
  GstCaps *temp;
  gint i;

  const gchar *video_id[] = {
    GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC,
    GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AVC,
    GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG1,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG2,
    GST_MATROSKA_CODEC_ID_VIDEO_MJPEG,
    /* TODO: Real/Quicktime */
    /* FILLME */
    NULL
  };
  const gchar *audio_id[] = {
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1,
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2,
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3,
    GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE,
    GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE,
    GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT,
    GST_MATROSKA_CODEC_ID_AUDIO_AC3,
    GST_MATROSKA_CODEC_ID_AUDIO_ACM,
    GST_MATROSKA_CODEC_ID_AUDIO_VORBIS,
    GST_MATROSKA_CODEC_ID_AUDIO_TTA,
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG2, GST_MATROSKA_CODEC_ID_AUDIO_MPEG4,
    GST_MATROSKA_CODEC_ID_AUDIO_WAVPACK4,
    /* TODO: AC3-9/10, Real, Musepack, Quicktime */
    /* FILLME */
    NULL
  };
  const gchar *complex_id[] = {
    /* FILLME */
    NULL
  };
  const gchar *subtitle_id[] = {
    GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8,
    /* FILLME */
    NULL
  };

  /* video src template */
  videosrccaps = gst_caps_new_empty ();
  for (i = 0; video_id[i] != NULL; i++) {
    temp = gst_matroska_demux_video_caps (NULL, video_id[i], NULL, 0, NULL);
    gst_caps_append (videosrccaps, temp);
  }
  for (i = 0; complex_id[i] != NULL; i++) {
    temp = gst_matroska_demux_complex_caps (NULL, video_id[i], NULL, 0);
    gst_caps_append (videosrccaps, temp);
  }
  videosrctempl = gst_pad_template_new ("video_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, videosrccaps);

  /* audio src template */
  audiosrccaps = gst_caps_new_empty ();
  for (i = 0; audio_id[i] != NULL; i++) {
    temp = gst_matroska_demux_audio_caps (NULL, audio_id[i], NULL, 0, NULL);
    gst_caps_append (audiosrccaps, temp);
  }
  audiosrctempl = gst_pad_template_new ("audio_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, audiosrccaps);

  /* subtitle src template */
  subtitlesrccaps = gst_caps_new_empty ();
  for (i = 0; subtitle_id[i] != NULL; i++) {
    temp = gst_matroska_demux_subtitle_caps (NULL, subtitle_id[i], NULL, 0);
    gst_caps_append (subtitlesrccaps, temp);
  }
  temp = gst_caps_new_simple ("application/x-subtitle-unknown", NULL);
  gst_caps_append (subtitlesrccaps, temp);
  subtitlesrctempl = gst_pad_template_new ("subtitle_%02d",
      GST_PAD_SRC, GST_PAD_SOMETIMES, subtitlesrccaps);

  /* create an elementfactory for the matroska_demux element */
  if (!gst_element_register (plugin, "matroskademux",
          GST_RANK_PRIMARY, GST_TYPE_MATROSKA_DEMUX))
    return FALSE;

  return TRUE;
}
