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
#include <gst/riff/riff-ids.h>
#include <gst/riff/riff-media.h>

#include "matroska-demux.h"
#include "matroska-ids.h"

enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_METADATA,
  ARG_STREAMINFO,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "matroskademux_sink",
     "video/x-matroska",
       NULL
  )
)

/* gobject magic foo */
static void     gst_matroska_demux_base_init	      (GstMatroskaDemuxClass *klass);
static void 	gst_matroska_demux_class_init	      (GstMatroskaDemuxClass *klass);
static void 	gst_matroska_demux_init		      (GstMatroskaDemux *demux);

/* element functions */
static void 	gst_matroska_demux_loop 	      (GstElement  *element);
static gboolean	gst_matroska_demux_send_event         (GstElement  *element,
						       GstEvent    *event);

/* pad functions */
static const GstEventMask *
		gst_matroska_demux_get_event_mask     (GstPad      *pad);
static gboolean gst_matroska_demux_handle_src_event   (GstPad      *pad,
						       GstEvent    *event);
static const GstFormat *
		gst_matroska_demux_get_src_formats    (GstPad      *pad); 
static const GstQueryType*
		gst_matroska_demux_get_src_query_types(GstPad      *pad);
static gboolean gst_matroska_demux_handle_src_query   (GstPad      *pad,
						       GstQueryType type, 
						       GstFormat   *format,
						       gint64      *value);

/* gst internal change state handler */
static GstElementStateReturn
		gst_matroska_demux_change_state       (GstElement  *element);
static void	gst_matroska_demux_set_clock	      (GstElement  *element,
						       GstClock    *clock);

/* gobject bla bla */
static void     gst_matroska_demux_get_property       (GObject     *object,
						       guint        prop_id, 	
						       GValue      *value,
						       GParamSpec  *pspec);

/* caps functions */
static GstCaps *gst_matroska_demux_video_caps         (GstMatroskaTrackVideoContext
								   *videocontext,
						       const gchar *codec_id,
						       gpointer     data,
						       guint        size);
static GstCaps *gst_matroska_demux_audio_caps         (GstMatroskaTrackAudioContext
								   *audiocontext,
						       const gchar *codec_id,
						       gpointer     data,
						       guint        size);
static GstCaps *gst_matroska_demux_complex_caps       (GstMatroskaTrackComplexContext
								   *complexcontext,
						       const gchar *codec_id,
						       gpointer     data,
						       guint        size);
static GstCaps *gst_matroska_demux_subtitle_caps      (GstMatroskaTrackSubtitleContext
								   *subtitlecontext,
						       const gchar *codec_id,
						       gpointer     data,
						       guint        size);

/* stream methods */
static void     gst_matroska_demux_reset	      (GstElement  *element);

static GstEbmlReadClass *parent_class = NULL;
static GstPadTemplate *videosrctempl, *audiosrctempl, *subtitlesrctempl;
/*static guint gst_matroska_demux_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_matroska_demux_get_type (void) 
{
  static GType gst_matroska_demux_type = 0;

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
				"GstMatroskaDemux",
				&gst_matroska_demux_info, 0);
  }

  return gst_matroska_demux_type;
}

static void
gst_matroska_demux_base_init (GstMatroskaDemuxClass *klass)
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
		GST_PAD_TEMPLATE_GET (sink_templ));
  gst_element_class_set_details (element_class,
		&gst_matroska_demux_details);
}

static void
gst_matroska_demux_class_init (GstMatroskaDemuxClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata",
                        GST_TYPE_CAPS, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_STREAMINFO,
    g_param_spec_boxed ("streaminfo", "Streaminfo", "Streaminfo",
                        GST_TYPE_CAPS, G_PARAM_READABLE));

  parent_class = g_type_class_ref (GST_TYPE_EBML_READ);

  gobject_class->get_property = gst_matroska_demux_get_property;

  gstelement_class->change_state = gst_matroska_demux_change_state;
  gstelement_class->send_event = gst_matroska_demux_send_event;
  gstelement_class->set_clock = gst_matroska_demux_set_clock;
}

static void 
gst_matroska_demux_init (GstMatroskaDemux *demux) 
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);
  gint i;

  GST_FLAG_SET (GST_OBJECT (demux), GST_ELEMENT_EVENT_AWARE);
				
  demux->sinkpad = gst_pad_new_from_template (
	gst_element_class_get_pad_template (klass, "sink"), "sink");
  gst_element_add_pad (GST_ELEMENT (demux), demux->sinkpad);
  GST_EBML_READ (demux)->sinkpad = demux->sinkpad;

  gst_element_set_loop_function (GST_ELEMENT (demux),
				 gst_matroska_demux_loop);

  /* initial stream no. */
  for (i = 0; i < GST_MATROSKA_DEMUX_MAX_STREAMS; i++) {
    demux->src[i] = NULL;
  }
  demux->streaminfo = demux->metadata = NULL;
  demux->writing_app = demux->muxing_app = NULL;
  demux->index = NULL;

  /* finish off */
  gst_matroska_demux_reset (GST_ELEMENT (demux));
}

static void
gst_matroska_demux_reset (GstElement *element)
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
  gst_caps_replace (&demux->metadata, NULL);
  gst_caps_replace (&demux->streaminfo, NULL);

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
  demux->seek_pending = GST_CLOCK_TIME_NONE;

  demux->metadata_parsed = FALSE;
  demux->index_parsed = FALSE;
}

static void
gst_matroska_demux_set_clock (GstElement  *element,
			      GstClock    *clock)
{
  GST_MATROSKA_DEMUX (element)->clock = clock;
}

static gint
gst_matroska_demux_stream_from_num (GstMatroskaDemux *demux,
				    guint             track_num)
{
  guint n;

  for (n = 0; n < demux->num_streams; n++) {
    if (demux->src[n] != NULL &&
        demux->src[n]->num == track_num) {
      return n;
    }
  }

  if (n == demux->num_streams) {
    GST_WARNING ("Failed to find corresponding pad for tracknum %d",
		 track_num); 
  }

  return -1;
}

static gboolean
gst_matroska_demux_add_stream (GstMatroskaDemux *demux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstMatroskaTrackContext *context;
  GstPadTemplate *templ = NULL;
  GstCaps *caps = NULL;
  gchar *padname = NULL;
  gboolean res = TRUE;
  guint32 id;

  if (demux->num_streams >= GST_MATROSKA_DEMUX_MAX_STREAMS) {
    GST_WARNING ("Maximum number of streams (%d) exceeded, skipping",
		 GST_MATROSKA_DEMUX_MAX_STREAMS);
    return gst_ebml_read_skip (ebml); /* skip-and-continue */
  }

  /* allocate generic... if we know the type, we'll g_renew()
   * with the precise type */
  context = g_new0 (GstMatroskaTrackContext, 1);
  demux->src[demux->num_streams] = context;
  context->index = demux->num_streams;
  context->type = 0; /* no type yet */
  demux->num_streams++;

  /* start with the master */
  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;

  /* try reading the trackentry headers */
  while (res) {
    if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up > 0) {
      demux->level_up--;
      break;
    }

    switch (id) {
      /* track number (unique stream ID) */
      case GST_MATROSKA_ID_TRACKNUMBER: {
        guint64 num;
        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        context->num = num;
        break;
      }

      /* track UID (unique identifier) */
      case GST_MATROSKA_ID_TRACKUID: {
        guint64 num;
        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        context->uid = num;
        break;
      }

      /* track type (video, audio, combined, subtitle, etc.) */
      case GST_MATROSKA_ID_TRACKTYPE: {
        guint64 num;
        if (context->type != 0) {
          GST_WARNING ("More than one tracktype defined in a trackentry - skipping");
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
        demux->src[demux->num_streams-1] = context;
        break;
      }

      /* tracktype specific stuff for video */
      case GST_MATROSKA_ID_TRACKVIDEO: {
        GstMatroskaTrackVideoContext *videocontext;
        if (context->type != GST_MATROSKA_TRACK_TYPE_VIDEO) {
          GST_WARNING ("trackvideo EBML entry in non-video track - ignoring track");
          res = FALSE;
          break;
        } else if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        videocontext = (GstMatroskaTrackVideoContext *) context;

        while (res) {
          if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
            res = FALSE;
            break;
          } else if (demux->level_up > 0) {
            demux->level_up--;
            break;
          }

          switch (id) {
            /* fixme, this should be one-up, but I get it here (?) */
            case GST_MATROSKA_ID_TRACKDEFAULTDURATION: {
              guint64 num;
              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              context->default_duration = num;
              break;
            }

            /* video framerate */
            case GST_MATROSKA_ID_VIDEOFRAMERATE: {
              gdouble num;
              if (!gst_ebml_read_float (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              context->default_duration = GST_SECOND * (1. / num);
              break;
            }

            /* width of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYWIDTH: {
              guint64 num;
              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->display_width = num;
              break;
            }

            /* height of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYHEIGHT: {
              guint64 num;
              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->display_height = num;
              break;
            }

            /* width of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELWIDTH: {
              guint64 num;
              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->pixel_width = num;
              break;
            }

            /* height of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELHEIGHT: {
              guint64 num;
              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              videocontext->pixel_height = num;
              break;
            }

            /* whether the video is interlaced */
            case GST_MATROSKA_ID_VIDEOFLAGINTERLACED: {
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
            case GST_MATROSKA_ID_VIDEOSTEREOMODE: {
              guint64 num;
              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              if (num != GST_MATROSKA_EYE_MODE_MONO &&
                  num != GST_MATROSKA_EYE_MODE_LEFT &&
                  num != GST_MATROSKA_EYE_MODE_RIGHT &&
                  num != GST_MATROSKA_EYE_MODE_BOTH) {
                GST_WARNING ("Unknown eye mode 0x%x - ignoring",
			     (guint) num);
                break;
              }
              videocontext->eye_mode = num;
              break;
            }

            /* aspect ratio behaviour */
            case GST_MATROSKA_ID_VIDEOASPECTRATIO: {
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
            case GST_MATROSKA_ID_VIDEOCOLOURSPACE: {
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
      case GST_MATROSKA_ID_TRACKAUDIO: {
        GstMatroskaTrackAudioContext *audiocontext;
        if (context->type != GST_MATROSKA_TRACK_TYPE_AUDIO) {
          GST_WARNING ("trackaudio EBML entry in non-audio track - ignoring track");
          res = FALSE;
          break;
        } else if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        audiocontext = (GstMatroskaTrackAudioContext *) context;

        while (res) {
          if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
            res = FALSE;
            break;
          } else if (demux->level_up > 0) {
            demux->level_up--;
            break;
          }

          switch (id) {
            /* samplerate */
            case GST_MATROSKA_ID_AUDIOSAMPLINGFREQ: {
              gdouble num;
              if (!gst_ebml_read_float (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              audiocontext->samplerate = num;
              break;
            }

            /* bitdepth */
            case GST_MATROSKA_ID_AUDIOBITDEPTH: {
              guint64 num;
              if (!gst_ebml_read_uint (ebml, &id, &num)) {
                res = FALSE;
                break;
              }
              audiocontext->bitdepth = num;
              break;
            }

            /* channels */
            case GST_MATROSKA_ID_AUDIOCHANNELS: {
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
      case GST_MATROSKA_ID_CODECID: {
        gchar *text;
        if (!gst_ebml_read_ascii (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->codec_id = text;
        break;
      }

      /* codec private data */
      case GST_MATROSKA_ID_CODECPRIVATE: {
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
      case GST_MATROSKA_ID_CODECNAME: {
        gchar *text;
        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->codec_name = text;
        break;
      }

      /* name of this track */
      case GST_MATROSKA_ID_TRACKNAME: {
        gchar *text;
        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->name = text;
        break;
      }

      /* language (matters for audio/subtitles, mostly) */
      case GST_MATROSKA_ID_TRACKLANGUAGE: {
        gchar *text;
        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        context->language = text;
        break;
      }

      /* whether this is actually used */
      case GST_MATROSKA_ID_TRACKFLAGENABLED: {
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
      case GST_MATROSKA_ID_TRACKFLAGDEFAULT: {
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
      case GST_MATROSKA_ID_TRACKFLAGLACING: {
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
      case GST_MATROSKA_ID_TRACKDEFAULTDURATION: {
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
    case GST_MATROSKA_TRACK_TYPE_VIDEO: {
      GstMatroskaTrackVideoContext *videocontext =
	(GstMatroskaTrackVideoContext *) context;
      padname = g_strdup_printf ("video_%02d", demux->num_v_streams);
      templ = gst_element_class_get_pad_template (klass, "video_%02d");
      caps = gst_matroska_demux_video_caps (videocontext,
					    context->codec_id,
					    context->codec_priv,
					    context->codec_priv_size);
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_AUDIO: {
      GstMatroskaTrackAudioContext *audiocontext =
	(GstMatroskaTrackAudioContext *) context;
      padname = g_strdup_printf ("audio_%02d", demux->num_a_streams);
      templ = gst_element_class_get_pad_template (klass, "audio_%02d");
      caps = gst_matroska_demux_audio_caps (audiocontext,
					    context->codec_id,
					    context->codec_priv,
					    context->codec_priv_size);
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_COMPLEX: {
      GstMatroskaTrackComplexContext *complexcontext =
	(GstMatroskaTrackComplexContext *) context;
      padname = g_strdup_printf ("video_%02d", demux->num_v_streams);
      templ = gst_element_class_get_pad_template (klass, "video_%02d");
      caps = gst_matroska_demux_complex_caps (complexcontext,
					      context->codec_id,
					      context->codec_priv,
					      context->codec_priv_size);
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_SUBTITLE: {
      GstMatroskaTrackSubtitleContext *subtitlecontext =
	(GstMatroskaTrackSubtitleContext *) context;
      padname = g_strdup_printf ("subtitle_%02d", demux->num_t_streams);
      templ = gst_element_class_get_pad_template (klass, "subtitle_%02d");
      caps = gst_matroska_demux_subtitle_caps (subtitlecontext,
					       context->codec_id,
					       context->codec_priv,
					       context->codec_priv_size);
      break;
    }

    case GST_MATROSKA_TRACK_TYPE_LOGO:
    case GST_MATROSKA_TRACK_TYPE_CONTROL:
    default:
      /* we should already have quit by now */
      g_assert (0);
  }

  /* the pad in here */
  context->pad =  gst_pad_new_from_template (templ, padname);

  if (caps != NULL) {
    if (gst_pad_try_set_caps (context->pad, caps) <= 0) {
      GST_WARNING ("Failed to set caps on next element for %s",
		   padname); 
    }
  }
  g_free (padname);

  /* set some functions */
  gst_pad_set_formats_function (context->pad,
				gst_matroska_demux_get_src_formats);
  gst_pad_set_event_mask_function (context->pad,
				   gst_matroska_demux_get_event_mask);
  gst_pad_set_event_function (context->pad,
			      gst_matroska_demux_handle_src_event);
  gst_pad_set_query_type_function (context->pad,
				   gst_matroska_demux_get_src_query_types);
  gst_pad_set_query_function (context->pad,
			      gst_matroska_demux_handle_src_query);

  gst_element_add_pad (GST_ELEMENT (demux), context->pad);

  /* tadaah! */
  return TRUE;
}

static const GstFormat *
gst_matroska_demux_get_src_formats (GstPad *pad) 
{
  /*GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));*/

  /* we could try to look for units (i.e. samples) in audio streams
   * or video streams, but both samplerate and framerate are not
   * always constant, and since we only have a time indication, we
   * cannot guarantee anything here based purely on index. So, we
   * only support time for now. */
  static const GstFormat src_formats[] = {
    GST_FORMAT_TIME,
    (GstFormat) 0
  };

  return src_formats;
}

static const GstQueryType *
gst_matroska_demux_get_src_query_types (GstPad *pad) 
{
  static const GstQueryType src_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    (GstQueryType) 0
  };

  return src_types;
}

static gboolean
gst_matroska_demux_handle_src_query (GstPad       *pad,
				     GstQueryType  type, 
				     GstFormat    *format,
				     gint64       *value)
{
  gboolean res = TRUE;
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          *value = demux->duration;
	  break;
	default:
          res = FALSE;
	  break;
      }
      break;

    case GST_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fall through */
        case GST_FORMAT_TIME:
          *value = demux->pos;
	  break;
	default:
          res = FALSE;
	  break;
      }
      break;

    default:
      res = FALSE;
      break;
  }

  return res;
}

static GstMatroskaIndex *
gst_matroskademux_seek (GstMatroskaDemux *demux)
{
  guint entry = (guint) -1;
  guint64 offset = demux->seek_pending;
  guint n;

  /* make sure we don't seek twice */
  demux->seek_pending = GST_CLOCK_TIME_NONE;

  for (n = 0; n < demux->num_indexes; n++) {
    if (entry == (guint) -1) {
      entry = n;
    } else {
      gfloat diff_old = fabs (1. * (demux->index[entry].time - offset)),
             diff_new = fabs (1. * (demux->index[n].time - offset));

      if (diff_new < diff_old) {
        entry = n;
      }
    }
  }

  if (entry != (guint) -1) {
    return &demux->index[entry];
  }

  return NULL;
}

static gboolean
gst_matroska_demux_send_event (GstElement *element,
			       GstEvent   *event)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      switch (GST_EVENT_SEEK_FORMAT (event)) {
        case GST_FORMAT_TIME:
          demux->seek_pending = GST_EVENT_SEEK_OFFSET (event);
          break;

        default:
          GST_WARNING ("Only time seek is supported");
          res = FALSE;
          break;
      }
      break;

    default:
      GST_WARNING ("Unhandled event of type %d",
		   GST_EVENT_TYPE (event));
      res = FALSE;
      break;
  }

  gst_event_unref (event);

  return res;
}

static const GstEventMask *
gst_matroska_demux_get_event_mask (GstPad *pad)
{
  static const GstEventMask masks[] = {
    { GST_EVENT_SEEK, 	      (GstEventFlag) ((gint) GST_SEEK_METHOD_SET |
					      (gint) GST_SEEK_FLAG_KEY_UNIT) },
    { GST_EVENT_SEEK_SEGMENT, (GstEventFlag) ((gint) GST_SEEK_METHOD_SET |
					      (gint) GST_SEEK_FLAG_KEY_UNIT) },
    { (GstEventType) 0,	      (GstEventFlag) 0 }
  };

  return masks;
}
	
static gboolean
gst_matroska_demux_handle_src_event (GstPad   *pad,
				     GstEvent *event)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (gst_pad_get_parent (pad));
  gboolean res = TRUE;
  
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK_SEGMENT:
    case GST_EVENT_SEEK:
      return gst_matroska_demux_send_event (GST_ELEMENT (demux), event);

    default:
      GST_WARNING ("Unhandled event of type %d",
		   GST_EVENT_TYPE (event));
      res = FALSE;
      break;
  }

  gst_event_unref (event);

  return res;
}

static gboolean
gst_matroska_demux_handle_seek_event (GstMatroskaDemux *demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  GstMatroskaIndex *entry = gst_matroskademux_seek (demux);
  GstEvent *event;
  guint i;

  if (!entry)
    return FALSE;

  /* seek (relative to segment) */
  if (!(event = gst_ebml_read_seek (ebml,
			entry->pos + demux->segment_start)))
    return FALSE;
  gst_event_unref (event); /* byte - we want time */
  event = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
                                       entry->time);

  /* forward to all src pads */
  for (i = 0; i < demux->num_streams; i++) {
    if (GST_PAD_IS_USABLE (demux->src[i]->pad)) {
      gst_event_ref (event);
      gst_pad_push (demux->src[i]->pad, GST_DATA (event));
    }
  }

  gst_event_unref (event);

  return TRUE;
}

static gboolean
gst_matroska_demux_init_stream (GstMatroskaDemux *demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  guint32 id;
  gchar *doctype;
  guint version;

  if (!gst_ebml_read_header (ebml, &doctype, &version))
    return FALSE;

  if (!doctype || strcmp (doctype, "matroska") != 0) {
    gst_element_error (GST_ELEMENT (demux),
		       "Input is not a matroska stream (doctype=%s)",
		       doctype ? doctype : "none");
    g_free (doctype);
    return FALSE;
  }
  g_free (doctype);
  if (version > 1) {
    gst_element_error (GST_ELEMENT (demux),
		       "Demuxer version (1) is too old to read stream version %d",
		       version);
    return FALSE;
  }

  /* find segment, must be the next element */
  while (1) {
    guint last_level;

    if (!(id = gst_ebml_peek_id (ebml, &last_level)))
      return FALSE;

    if (id == GST_MATROSKA_ID_SEGMENT)
      break;

    /* oi! */
    GST_WARNING ("Expected a Segment ID (0x%x), but received 0x%x!",
		 GST_MATROSKA_ID_SEGMENT, id);
    if (!gst_ebml_read_skip (ebml))
      return FALSE;
  }

  /* we now have a EBML segment */
  if (!gst_ebml_read_master (ebml, &id))
    return FALSE;
  /* seeks are from the beginning of the segment,
   * after the segment ID/length */
  demux->segment_start = gst_bytestream_tell (ebml->bs);

  return TRUE;
}

static gboolean
gst_matroska_demux_parse_tracks (GstMatroskaDemux *demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;

  while (res) {
    if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
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
gst_matroska_demux_parse_index (GstMatroskaDemux *demux,
				gboolean          prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;
  GstMatroskaIndex idx;
  guint64 length = 0;

  if (prevent_eos) {
    length = gst_bytestream_length (ebml->bs);
    gst_clock_set_active (demux->clock, FALSE);
  }

  while (res) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == gst_bytestream_tell (ebml->bs)) {
      res = FALSE;
      break;
    } else if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      /* one single index entry ('point') */
      case GST_MATROSKA_ID_POINTENTRY:
        if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }

        /* in the end, we hope to fill one entry with a
	 * timestamp, a file position and a tracknum */
        idx.pos   = (guint64) -1;
        idx.time  = (guint64) -1;
        idx.track = (guint16) -1;

        while (res) {
          if (prevent_eos && length == gst_bytestream_tell (ebml->bs)) {
            res = FALSE;
            break;
          } else if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
            res = FALSE;
            break;
          } else if (demux->level_up) {
            demux->level_up--;
            break;
          }

          switch (id) {
            /* one single index entry ('point') */
            case GST_MATROSKA_ID_CUETIME: {
              gint64 time;
              if (!gst_ebml_read_uint (ebml, &id, &time)) {
                res = FALSE;
                break;
              }
              idx.time = time * demux->time_scale;
              break;
            }

            /* position in the file + track to which it belongs */
            case GST_MATROSKA_ID_CUETRACKPOSITION:
              if (!gst_ebml_read_master (ebml, &id)) {
                res = FALSE;
                break;
              }

              while (res) {
                if (prevent_eos && length == gst_bytestream_tell (ebml->bs)) {
                  res = FALSE;
                  break;
                } else if (!(id = gst_ebml_peek_id (ebml,
					&demux->level_up))) {
                  res = FALSE;
                  break;
                } else if (demux->level_up) {
                  demux->level_up--;
                  break;
                }

                switch (id) {
                  /* track number */
                  case GST_MATROSKA_ID_CUETRACK: {
                    guint64 num;
                    if (!gst_ebml_read_uint (ebml, &id, &num)) {
                      res = FALSE;
                      break;
                    }
                    idx.track = num;
                    break;
                  }

                  /* position in file */
                  case GST_MATROSKA_ID_CUECLUSTERPOSITION: {
                    guint64 num;
                    if (!gst_ebml_read_uint (ebml, &id, &num)) {
                      res = FALSE;
                      break;
                    }
                    idx.pos = num;
                    break;
                  }

                  default:
                    GST_WARNING ("Unknown entry 0x%x in CuesTrackPositions", id);
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

              break;

            default:
              GST_WARNING ("Unknown entry 0x%x in cuespoint index", id);
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

        /* so let's see if we got what we wanted */
        if (idx.pos   != (guint64) -1 &&
	    idx.time  != (guint64) -1 &&
	    idx.track != (guint16) -1) {
          if (demux->num_indexes % 32 == 0) {
            /* re-allocate bigger index */
            demux->index = g_renew (GstMatroskaIndex, demux->index,
				    demux->num_indexes + 32);
          }
          demux->index[demux->num_indexes].pos   = idx.pos;
          demux->index[demux->num_indexes].time  = idx.time;
          demux->index[demux->num_indexes].track = idx.track;
          demux->num_indexes++;
        }

        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in cues header", id);
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

  if (prevent_eos) {
    gst_clock_set_active (demux->clock, TRUE);
  }

  return res;
}

static gboolean
gst_matroska_demux_parse_info (GstMatroskaDemux *demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;

  while (res) {
    if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      /* cluster timecode */
      case GST_MATROSKA_ID_TIMECODESCALE: {
        guint64 num;
        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        demux->time_scale = num;
        break;
      }

      case GST_MATROSKA_ID_DURATION: {
        gdouble num;
        if (!gst_ebml_read_float (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        demux->duration = num * demux->time_scale;
        break;
      }

      case GST_MATROSKA_ID_WRITINGAPP: {
        gchar *text;
        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        demux->writing_app = text;
        break;
      }

      case GST_MATROSKA_ID_MUXINGAPP: {
        gchar *text;
        if (!gst_ebml_read_utf8 (ebml, &id, &text)) {
          res = FALSE;
          break;
        }
        demux->muxing_app = text;
        break;
      }

      case GST_MATROSKA_ID_DATEUTC: {
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
gst_matroska_demux_parse_metadata (GstMatroskaDemux *demux,
				   gboolean          prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;
  guint64 length = 0;

  if (prevent_eos) {
    length = gst_bytestream_length (ebml->bs);
    gst_clock_set_active (demux->clock, FALSE);
  }

  while (res) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == gst_bytestream_tell (ebml->bs)) {
      res = FALSE;
      break;
    } else if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      default:
        GST_WARNING ("metadata unimplemented");
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

  if (prevent_eos) {
    gst_clock_set_active (demux->clock, TRUE);
  }

  return res;
}

/*
 * Read signed/unsigned "EBML" numbers.
 * Return: number of bytes processed.
 */

static gint
gst_matroska_ebmlnum_uint (guint8  *data,
			   guint    size,
			   guint64 *num)
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

  if (!total)
    return -1;

  if (read == num_ffs)
    *num = G_MAXUINT64;
  else
    *num = total;

  return read;
}

static gint
gst_matroska_ebmlnum_sint (guint8 *data,
			   guint   size,
			   gint64 *num)
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

static gboolean
gst_matroska_demux_parse_blockgroup (GstMatroskaDemux *demux,
				     guint64           cluster_time)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;

  while (res) {
    if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      /* one block inside the group. Note, block parsing is one
       * of the harder things, so this code is a bit complicated.
       * See http://www.matroska.org/ for documentation. */
      case GST_MATROSKA_ID_BLOCK: {
        GstBuffer *buf;
        guint8 *data;
        gint16 time;
        guint size, *lace_size = NULL;
        gint n, stream, flags, laces = 0;
        guint64 num;

        if (!gst_ebml_read_buffer (ebml, &id, &buf)) {
          res = FALSE;
          break;
        }
        data = GST_BUFFER_DATA (buf);
        size = GST_BUFFER_SIZE (buf);

        /* first byte(s): blocknum */
        if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0) {
          gst_element_error (GST_ELEMENT (demux), "Data error");
          gst_buffer_unref (buf);
          res = FALSE;
          break;
        }
        data += n; size -= n;

        /* fetch stream from num */
        stream = gst_matroska_demux_stream_from_num (demux, num);
        if (size <= 3 || stream < 0 || stream >= demux->num_streams) {
          gst_buffer_unref (buf);
          GST_WARNING ("Invalid stream %d or size %u", stream, size);
          break;
        }
        if (!GST_PAD_IS_USABLE (demux->src[stream]->pad)) {
          gst_buffer_unref (buf);
          break;
        }

        /* time (relative to cluster time) */
        time = (* (gint16 *) data) * demux->time_scale;
        time = GINT16_FROM_BE (time);
        data += 2; size -= 2;
        flags = * (guint8 *) data;
        data += 1; size -= 1;
        switch ((flags & 0x06) >> 1) {
          case 0x0: /* no lacing */
            laces = 1;
            lace_size = g_new (gint, 1);
            lace_size[0] = size;
            break;

          case 0x1: /* xiph lacing */
          case 0x2: /* fixed-size lacing */
          case 0x3: /* EBML lacing */
            if (size == 0) {
              res = FALSE;
              break;
            }
            laces = (* (guint8 *) data) + 1;
            data += 1; size -= 1;
            lace_size = g_new0 (gint, laces);

            switch ((flags & 0x06) >> 1) {
              case 0x1: /* xiph lacing */ {
                guint temp, total = 0;
                for (n = 0; res && n < laces - 1; n++) {
                  while (1) {
                     if (size == 0) {
                       res = FALSE;
                       break;
                     }
                     temp = * (guint8 *) data;
                     lace_size[n] += temp;
                     data += 1; size -= 1;
                     if (temp != 0xff)
                       break;
                  }
                  total += lace_size[n];
                }
                lace_size[n] = size - total;
                break;
              }

              case 0x2: /* fixed-size lacing */
                for (n = 0; n < laces; n++)
                  lace_size[n] = size / laces;
                break;

              case 0x3: /* EBML lacing */ {
                guint total;
                if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0) {
                  gst_element_error (GST_ELEMENT (demux), "Data error");
                  res = FALSE;
                  break;
                }
                data += n; size -= n;
                total = lace_size[0] = num;
                for (n = 1; res && n < laces - 1; n++) {
                  gint64 snum;
                  gint r;
                  if ((r = gst_matroska_ebmlnum_sint (data, size, &snum)) < 0) {
                    gst_element_error (GST_ELEMENT (demux), "Data error");
                    res = FALSE;
                    break;
                  }
                  data += r; size -= r;
                  lace_size[n] = lace_size[0] + snum;
                  total += lace_size[n];
                }
                lace_size[n] = size - total;
                break;
              }
            }
            break;
        }

        if (res) {
          for (n = 0; n < laces; n++) {
            GstBuffer *sub = gst_buffer_create_sub (buf,
						    GST_BUFFER_SIZE (buf) - size,
						    lace_size[n]);

            if (cluster_time != GST_CLOCK_TIME_NONE)
              GST_BUFFER_TIMESTAMP (sub) = cluster_time + time;

            /* FIXME: duration */

            gst_pad_push (demux->src[stream]->pad, GST_DATA (sub));

            size -= lace_size[n];
          }
        }

        g_free (lace_size);
        gst_buffer_unref (buf);
        break;
      }

      case GST_MATROSKA_ID_BLOCKDURATION: {
        guint64 num;
        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        GST_WARNING ("FIXME: implement support for BlockDuration");
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in blockgroup data", id);
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
gst_matroska_demux_parse_cluster (GstMatroskaDemux *demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;
  guint64 cluster_time = GST_CLOCK_TIME_NONE;

  /* We seek after index/header parsing before doing a new
   * buffer. So here. */
  if (demux->seek_pending != GST_CLOCK_TIME_NONE) {
    if (!gst_matroska_demux_handle_seek_event (demux))
      return FALSE;
    demux->seek_pending = GST_CLOCK_TIME_NONE;
  }

  while (res) {
    if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      /* cluster timecode */
      case GST_MATROSKA_ID_CLUSTERTIMECODE: {
        guint64 num;
        if (!gst_ebml_read_uint (ebml, &id, &num)) {
          res = FALSE;
          break;
        }
        cluster_time = num * demux->time_scale;
        break;
      }

      /* a group of blocks inside a cluster */
      case GST_MATROSKA_ID_BLOCKGROUP:
        if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        res = gst_matroska_demux_parse_blockgroup (demux, cluster_time);
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in cluster data", id);
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
gst_matroska_demux_parse_contents (GstMatroskaDemux *demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;

  while (res) {
    if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SEEKENTRY: {
        guint32 seek_id = 0;
        guint64 seek_pos = (guint64) -1, t;

        if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }

        while (res) {
          if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
            res = FALSE;
            break;
          } else if (demux->level_up) {
            demux->level_up--;
            break;
          }

          switch (id) {
            case GST_MATROSKA_ID_SEEKID:
              if (!gst_ebml_read_uint (ebml, &id, &t))
                res = FALSE;
              seek_id = t;
              break;

            case GST_MATROSKA_ID_SEEKPOSITION:
              if (!gst_ebml_read_uint (ebml, &id, &seek_pos))
                res = FALSE;
              break;

            default:
              GST_WARNING ("Unknown seekhead ID 0x%x", id);
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

        if (!seek_id || seek_pos == (guint64) -1) {
          GST_WARNING ("Incomplete seekhead entry (0x%x/%"
		       G_GUINT64_FORMAT ")", seek_id, seek_pos);
          break;
        }

        switch (seek_id) {
          case GST_MATROSKA_ID_CUES:
          case GST_MATROSKA_ID_TAGS: {
            guint level_up = demux->level_up;
            guint64 before_pos, length;
            GstEbmlLevel *level;
            GstEvent *event;

            /* remember */
            length = gst_bytestream_length (ebml->bs);
            before_pos = gst_bytestream_tell (ebml->bs);

            /* check for validity */
            if (seek_pos + demux->segment_start + 12 >= length) {
              g_warning ("Seekhead reference lies outside file!");
              break;
            }

            /* seek */
            if (!(event = gst_ebml_read_seek (ebml,
				seek_pos + demux->segment_start)))
              return FALSE;
            gst_event_unref (event);

            /* we don't want to lose our seekhead level, so we add
             * a dummy. This is a crude hack. */
            level = g_new (GstEbmlLevel, 1);
            level->start = 0;
            level->length = G_MAXUINT64;
            ebml->level = g_list_append (ebml->level, level);

            /* check ID */
            if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
              res = FALSE;
              break;
            }
            if (id != seek_id) {
              g_warning ("We looked for ID=0x%x but got ID=0x%x (pos=%llu)",
			 seek_id, id, seek_pos + demux->segment_start);
              goto finish;
            }

            /* read master + parse */
            switch (id) {
              case GST_MATROSKA_ID_CUES:
                if (!gst_ebml_read_master (ebml, &id))
                  res = FALSE;
                else if (!gst_matroska_demux_parse_index (demux, TRUE) &&
			 gst_bytestream_length (ebml->bs) !=
				gst_bytestream_tell (ebml->bs))
                  res = FALSE;
                else
                  demux->index_parsed = TRUE;
                break;
              case GST_MATROSKA_ID_TAGS:
                if (!gst_ebml_read_master (ebml, &id))
                  res = FALSE;
                else if (!gst_matroska_demux_parse_metadata (demux, TRUE) &&
			 gst_bytestream_length (ebml->bs) !=
				gst_bytestream_tell (ebml->bs))
                  res = FALSE;
                else
                  demux->metadata_parsed = TRUE;
                break;
            }
            if (!res)
              break;

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
            if (!(event = gst_ebml_read_seek (ebml, before_pos)))
              return FALSE;
            gst_event_unref (event);
            demux->level_up = level_up;
            break;
          }

          default:
            GST_INFO ("Ignoring seekhead entry for ID=0x%x", seek_id);
            break;
        }

        break;
      }

      default:
        GST_WARNING ("Unknown seekhead ID 0x%x", id);
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
gst_matroska_demux_loop_stream (GstMatroskaDemux *demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);
  gboolean res = TRUE;
  guint32 id;

  /* we've found our segment, start reading the different contents in here */
  while (res) {
    if (!(id = gst_ebml_peek_id (ebml, &demux->level_up))) {
      res = FALSE;
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      /* stream info */
      case GST_MATROSKA_ID_INFO: {
        if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        res = gst_matroska_demux_parse_info (demux);
        break;
      }

      /* track info headers */
      case GST_MATROSKA_ID_TRACKS: {
        if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        res = gst_matroska_demux_parse_tracks (demux);
        break;
      }

      /* stream index */
      case GST_MATROSKA_ID_CUES: {
        if (!demux->index_parsed) {
          if (!gst_ebml_read_master (ebml, &id)) {
            res = FALSE;
            break;
          }
          res = gst_matroska_demux_parse_index (demux, FALSE);
        } else
          res = gst_ebml_read_skip (ebml);
        break;
      }

      /* metadata */
      case GST_MATROSKA_ID_TAGS: {
        if (!demux->index_parsed) {
          if (!gst_ebml_read_master (ebml, &id)) {
            res = FALSE;
            break;
          }
          res = gst_matroska_demux_parse_metadata (demux, FALSE);
        } else
          res = gst_ebml_read_skip (ebml);
        break;
      }

      /* file index (if seekable, seek to Cues/Tags to parse it) */
      case GST_MATROSKA_ID_SEEKHEAD: {
        if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        res = gst_matroska_demux_parse_contents (demux);
        break;
      }

      case GST_MATROSKA_ID_CLUSTER: {
        if (!gst_ebml_read_master (ebml, &id)) {
          res = FALSE;
          break;
        }
        /* The idea is that we parse one cluster per loop and
         * then break out of the loop here. In the next call
         * of the loopfunc, we will get back here with the
         * next cluster. If an error occurs, we didn't
         * actually push a buffer, but we still want to break
         * out of the loop to handle a possible error. We'll
         * get back here if it's recoverable. */
        gst_matroska_demux_parse_cluster (demux);
        demux->state = GST_MATROSKA_DEMUX_STATE_DATA;
        res = FALSE;
        break;
      }

      default:
        GST_WARNING ("Unknown matroska file header ID 0x%x", id);
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

static void
gst_matroska_demux_loop (GstElement *element)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);

  /* first, if we're to start, let's actually get starting */
  if (demux->state == GST_MATROSKA_DEMUX_STATE_START) {
    if (!gst_matroska_demux_init_stream (demux)) {
      return;
    }
    demux->state = GST_MATROSKA_DEMUX_STATE_HEADER;
  }

  gst_matroska_demux_loop_stream (demux);
}

static GstCaps *
gst_matroska_demux_video_caps (GstMatroskaTrackVideoContext *videocontext,
			       const gchar                  *codec_id,
			       gpointer                      data,
			       guint                         size)
{
  GstMatroskaTrackContext *context =
	(GstMatroskaTrackContext *) videocontext;
  GstCaps *caps = NULL;

  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC)) {
    gst_riff_strf_vids *vids = NULL;
    GstCaps *t;

    if (data) {
      vids = (gst_riff_strf_vids *) data;

      /* assure size is big enough */
      if (size < 24) {
        GST_WARNING ("Too small BITMAPINFOHEADER (%d bytes)", size);
        return NULL;
      }
      if (size < sizeof (gst_riff_strf_vids)) {
        vids = (gst_riff_strf_vids *) g_realloc (vids, sizeof (gst_riff_strf_vids));
      }

      /* little-endian -> byte-order */
      vids->size        = GUINT32_FROM_LE (vids->size);
      vids->width       = GUINT32_FROM_LE (vids->width);
      vids->height      = GUINT32_FROM_LE (vids->height);
      vids->planes      = GUINT16_FROM_LE (vids->planes);
      vids->bit_cnt     = GUINT16_FROM_LE (vids->bit_cnt);
      vids->compression = GUINT32_FROM_LE (vids->compression);
      vids->image_size  = GUINT32_FROM_LE (vids->image_size);
      vids->xpels_meter = GUINT32_FROM_LE (vids->xpels_meter);
      vids->ypels_meter = GUINT32_FROM_LE (vids->ypels_meter);
      vids->num_colors  = GUINT32_FROM_LE (vids->num_colors);
      vids->imp_colors  = GUINT32_FROM_LE (vids->imp_colors);

      caps = gst_riff_create_video_caps (vids->compression, NULL, vids);
    } else {
      caps = gst_riff_create_video_template_caps ();
    }

    for (t = caps; t != NULL; t = t->next) {
      gst_props_remove_entry_by_name (t->properties, "width");
      gst_props_remove_entry_by_name (t->properties, "height");
      gst_props_remove_entry_by_name (t->properties, "framerate");
    }
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED)) {
    /* how nice, this is undocumented... */
    if (videocontext != NULL) {
      guint32 fourcc = 0;

      switch (videocontext->fourcc) {
        case GST_MAKE_FOURCC ('I','4','2','0'):
        case GST_MAKE_FOURCC ('Y','U','Y','2'):
          fourcc = videocontext->fourcc;
          break;

        default:
          GST_DEBUG ("Unknown fourcc " GST_FOURCC_FORMAT,
		     GST_FOURCC_ARGS (videocontext->fourcc));
          return NULL;
      }

      caps = GST_CAPS_NEW ("matroskademux_src_uncompressed",
			   "video/x-raw-yuv",
			     "format", GST_PROPS_FOURCC (fourcc));
    } else {
      caps = GST_CAPS_NEW ("matroskademux_src_uncompressed",
			   "video/x-raw-yuv",
			     "format", GST_PROPS_LIST (
				GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
				GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2')),
				GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2'))
			     )
			  );
    }
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP)) {
    caps = GST_CAPS_NEW ("matroskademux_src_divx4",
			 "video/x-divx",
			   "divxversion", GST_PROPS_INT (4));
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP) ||
	     !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AP)) {
    caps = GST_CAPS_NEW ("matroskademux_src_divx5",
			 "video/x-divx",
			   "divxversion", GST_PROPS_INT (5));
    caps = gst_caps_append (caps,
	   GST_CAPS_NEW ("matroskademux_src_xvid",
			 "video/x-xvid",
			   NULL));
    caps = gst_caps_append (caps,
	   GST_CAPS_NEW ("matroskademux_src_mpeg4asp/ap",
			 "video/mpeg",
			   "mpegversion",  GST_PROPS_INT (4),
			   "systemstream", GST_PROPS_BOOLEAN (FALSE)));
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3)) {
    caps = GST_CAPS_NEW ("matroskademux_src_msmpeg4v3",
			 "video/x-divx",
			   "divxversion", GST_PROPS_INT (3));
    caps = gst_caps_append (caps,
	   GST_CAPS_NEW ("matroskademux_src_divx3",
			 "video/x-msmpeg",
			   "msmpegversion", GST_PROPS_INT (43)));
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1) ||
	     !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2)) {
    gint mpegversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1))
      mpegversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2))
      mpegversion = 2;
    else
      g_assert (0);

    caps = GST_CAPS_NEW ("matroska_demux_mpeg1",
			 "video/mpeg",
			   "systemstream", GST_PROPS_BOOLEAN (FALSE),
			   "mpegversion",  GST_PROPS_INT (mpegversion));
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MJPEG)) {
    caps = GST_CAPS_NEW ("matroska_demux_mjpeg",
			 "video/x-jpeg",
			   NULL);
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps",
		 codec_id);
  }

  if (caps != NULL) {
    GstCaps *one;
    GstPropsEntry *fps = NULL;
    GstPropsEntry *width = NULL, *height = NULL;
    GstPropsEntry *pixel_width = NULL, *pixel_height = NULL;

    for (one = caps; one != NULL; one = one->next) { 
      if (videocontext != NULL) {
        if (videocontext->pixel_width > 0 &&
            videocontext->pixel_height > 0) {
          gint w = videocontext->pixel_width;
          gint h = videocontext->pixel_height;

          width = gst_props_entry_new ("width",
				       GST_PROPS_INT (w));
          height = gst_props_entry_new ("height",
				        GST_PROPS_INT (h));
        }
#if 0
        if (videocontext->display_width > 0 &&
	    videocontext->display_height > 0) {
          gint w = 100 * videocontext->display_width / videocontext->pixel_width;
          gint h = 100 * videocontext->display_height / videocontext->pixel_height;

          pixel_width = gst_props_entry_new ("pixel_width",
					     GST_PROPS_INT (w));
          pixel_height = gst_props_entry_new ("pixel_height",
					      GST_PROPS_INT (h));
        }
#endif
        if (context->default_duration > 0) {
          gfloat framerate = 1. * GST_SECOND / context->default_duration;

          fps = gst_props_entry_new ("framerate",
				     GST_PROPS_FLOAT (framerate));
        } else {
          /* sort of a hack to get most codecs to support,
	   * even if the default_duration is missing */
          fps = gst_props_entry_new ("framerate", GST_PROPS_FLOAT (25.));
        }
      } else {
        width = gst_props_entry_new ("width",
				     GST_PROPS_INT_RANGE (16, 4096));
        height = gst_props_entry_new ("height",
				      GST_PROPS_INT_RANGE (16, 4096));
#if 0
        pixel_width = gst_props_entry_new ("pixel_width",
				           GST_PROPS_INT_RANGE (0, 255));
        pixel_height = gst_props_entry_new ("pixel_height",
					    GST_PROPS_INT_RANGE (0, 255));
#endif
        fps = gst_props_entry_new ("framerate",
			           GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT));
      }

      if (one->properties == NULL) {
        one->properties = gst_props_empty_new ();
      }

      if (width != NULL && height != NULL) {
        gst_props_add_entry (one->properties, width);
        gst_props_add_entry (one->properties, height);
      }

      if (pixel_width != NULL && pixel_height != NULL) {
        gst_props_add_entry (one->properties, pixel_width);
        gst_props_add_entry (one->properties, pixel_height);
      }

      if (fps != NULL) {
        gst_props_add_entry (one->properties, fps);
      }
    }
  }

  return caps;
}

static GstCaps *
gst_matroska_demux_audio_caps (GstMatroskaTrackAudioContext *audiocontext,
			       const gchar                  *codec_id,
			       gpointer                      data,
			       guint                         size)
{
  GstMatroskaTrackContext *context =
	(GstMatroskaTrackContext *) audiocontext;
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

    caps = GST_CAPS_NEW ("matroskademux_mpeg1-l1",
			 "audio/mpeg",
			   "mpegversion",  GST_PROPS_INT (1),
			   "systemstream", GST_PROPS_BOOLEAN (FALSE),
			   "layer",        GST_PROPS_INT (layer));
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE) ||
	     !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE)) {
    gint endianness = -1;
    GstPropsEntry *depth, *width, *sign;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE))
      endianness = G_BIG_ENDIAN;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE))
      endianness = G_LITTLE_ENDIAN;
    else
      g_assert (0);

    if (context != NULL) {
      width = gst_props_entry_new ("width",
			GST_PROPS_INT (audiocontext->bitdepth));
      depth = gst_props_entry_new ("depth",
			GST_PROPS_INT (audiocontext->bitdepth));
      sign = gst_props_entry_new ("signed",
			GST_PROPS_BOOLEAN (audiocontext->bitdepth == 8));
    } else {
      width = gst_props_entry_new ("width", GST_PROPS_LIST (
			GST_PROPS_INT (8),
			GST_PROPS_INT (16)));
      depth = gst_props_entry_new ("depth", GST_PROPS_LIST (
			GST_PROPS_INT (8),
			GST_PROPS_INT (16)));
      sign = gst_props_entry_new ("signed", GST_PROPS_LIST (
			GST_PROPS_BOOLEAN (TRUE),
			GST_PROPS_BOOLEAN (FALSE)));
    }

    caps = GST_CAPS_NEW ("matroskademux_audio_raw",
			 "audio/x-raw-int",
			   "endianness", GST_PROPS_INT (endianness));
    gst_props_add_entry (caps->properties, width);
    gst_props_add_entry (caps->properties, depth);
    gst_props_add_entry (caps->properties, sign);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT)) {
    GstPropsEntry *width;

    if (audiocontext != NULL) {
      width = gst_props_entry_new ("width",
			GST_PROPS_INT (audiocontext->bitdepth));
    } else {
      width = gst_props_entry_new ("width", GST_PROPS_LIST (
			GST_PROPS_INT (32),
			GST_PROPS_INT (64)));
    }

    caps = GST_CAPS_NEW ("matroskademux_audio_float",
			 "audio/x-raw-float",
			   "endianness", GST_PROPS_INT (G_BYTE_ORDER),
                           "buffer-frames", GST_PROPS_INT_RANGE (1, G_MAXINT));

    gst_props_add_entry (caps->properties, width);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AC3) ||
	     !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_DTS)) {
    caps = GST_CAPS_NEW ("matroskademux_audio_ac3/dts",
			 "audio/x-ac3",
			   NULL);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_VORBIS)) {
    caps = GST_CAPS_NEW ("matroskademux_audio_vorbis",
			 "audio/x-vorbis",
			   NULL);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_ACM)) {
    gst_riff_strf_auds *auds = NULL;
    GstCaps *t;

    if (data) {
      auds = (gst_riff_strf_auds *) data;

      /* little-endian -> byte-order */
      auds->format     = GUINT16_FROM_LE (auds->format);
      auds->channels   = GUINT16_FROM_LE (auds->channels);
      auds->rate       = GUINT32_FROM_LE (auds->rate);
      auds->av_bps     = GUINT32_FROM_LE (auds->av_bps);
      auds->blockalign = GUINT16_FROM_LE (auds->blockalign);
      auds->size       = GUINT16_FROM_LE (auds->size);

      caps = gst_riff_create_audio_caps (auds->format, NULL, auds);
    } else {
      caps = gst_riff_create_audio_template_caps ();
    }

    for (t = caps; t != NULL; t = t->next) {
      gst_props_remove_entry_by_name (t->properties, "rate");
      gst_props_remove_entry_by_name (t->properties, "channels");
    }
  } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG2,
		       strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG2)) ||
             !strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG4,
		       strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG4))) {
    gint mpegversion = -1;

    if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG2,
		       strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG2)))
      mpegversion = 2;
    else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_MPEG4,
		       strlen (GST_MATROSKA_CODEC_ID_AUDIO_MPEG4)))
      mpegversion = 4;
    else
      g_assert (0);

    caps = GST_CAPS_NEW ("matroska_demux_aac_mpeg2",
			 "audio/mpeg",
			   "mpegversion",  GST_PROPS_INT (mpegversion),
			   "systemstream", GST_PROPS_BOOLEAN (FALSE));
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps",
		 codec_id);
  }

  if (caps != NULL) {
    GstCaps *one;
    GstPropsEntry *chans = NULL, *rate = NULL;

    for (one = caps; one != NULL; one = one->next) { 
      if (audiocontext != NULL) {
        if (audiocontext->samplerate > 0 &&
            audiocontext->channels > 0) {
          chans = gst_props_entry_new ("channels",
				GST_PROPS_INT (audiocontext->channels));
          rate = gst_props_entry_new ("rate",
				GST_PROPS_INT (audiocontext->samplerate));
        }
      } else {
        chans = gst_props_entry_new ("channels",
				GST_PROPS_INT_RANGE (1, 6));
        rate = gst_props_entry_new ("rate",
				GST_PROPS_INT_RANGE (4000, 96000));
      }

      if (caps->properties == NULL) {
        caps->properties = gst_props_empty_new ();
      }

      if (chans != NULL && rate != NULL) {
        gst_props_add_entry (caps->properties, chans);
        gst_props_add_entry (caps->properties, rate);
      }
    }
  }

  return caps;
}

static GstCaps *
gst_matroska_demux_complex_caps (GstMatroskaTrackComplexContext *complexcontext,
				 const gchar                    *codec_id,
				 gpointer                        data,
				 guint                           size)
{
  GstCaps *caps = NULL;

  //..

  return caps;
}

static GstCaps *
gst_matroska_demux_subtitle_caps (GstMatroskaTrackSubtitleContext *subtitlecontext,
			          const gchar                     *codec_id,
			          gpointer                         data,
			          guint                            size)
{
  GstCaps *caps = NULL;

  //..

  return caps;
}

static GstElementStateReturn
gst_matroska_demux_change_state (GstElement *element)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      gst_matroska_demux_reset (GST_ELEMENT (demux));
      break;
    default:
      break;
  }

  if (((GstElementClass *) parent_class)->change_state)
    return ((GstElementClass *) parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_matroska_demux_get_property (GObject    *object,
				 guint       prop_id,
				 GValue     *value,
				 GParamSpec *pspec)
{
  GstMatroskaDemux *demux;

  g_return_if_fail (GST_IS_MATROSKA_DEMUX (object));
  demux = GST_MATROSKA_DEMUX (object);

  switch (prop_id) {
    case ARG_STREAMINFO:
      g_value_set_boxed (value, demux->streaminfo);
      break;
    case ARG_METADATA:
      g_value_set_boxed (value, demux->metadata);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

gboolean
gst_matroska_demux_plugin_init (GstPlugin *plugin)
{
  gint i;
  GstCaps *videosrccaps = NULL, *audiosrccaps = NULL,
	  *subtitlesrccaps = NULL, *temp;
  const gchar *video_id[] = {
    GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC,
    GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP,
    GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG1,
    GST_MATROSKA_CODEC_ID_VIDEO_MPEG2,
    GST_MATROSKA_CODEC_ID_VIDEO_MJPEG,
    /* TODO: Real/Quicktime */
    /* FILLME */
    NULL,
  }, *audio_id[] = {
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1,
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2,
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3,
    GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE,
    GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE,
    GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT,
    GST_MATROSKA_CODEC_ID_AUDIO_AC3,
    GST_MATROSKA_CODEC_ID_AUDIO_ACM,
    GST_MATROSKA_CODEC_ID_AUDIO_VORBIS,
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG2,
    GST_MATROSKA_CODEC_ID_AUDIO_MPEG4,
    /* TODO: AC3-9/10, Real, Musepack, Quicktime */
    /* FILLME */
    NULL,
  }, *complex_id[] = {
    /* FILLME */
    NULL,
  }, *subtitle_id[] = {
    /* FILLME */
    NULL,
  };

  /* this filter needs the riff parser */
  if (!gst_library_load ("gstbytestream") ||
      !gst_library_load ("riff")) /* for fourcc stuff */
    return FALSE;

  /* video src template */
  for (i = 0; video_id[i] != NULL; i++) {
    temp = gst_matroska_demux_video_caps (NULL, video_id[i], NULL, 0);
    videosrccaps = gst_caps_append (videosrccaps, temp);
  }
  for (i = 0; complex_id[i] != NULL; i++) {
    temp = gst_matroska_demux_complex_caps (NULL, video_id[i], NULL, 0);
    videosrccaps = gst_caps_append (videosrccaps, temp);
  }
  videosrctempl = gst_pad_template_new ("video_%02d",
					GST_PAD_SRC,
					GST_PAD_SOMETIMES,
					videosrccaps, NULL);

  /* audio src template */
  for (i = 0; audio_id[i] != NULL; i++) {
    temp = gst_matroska_demux_audio_caps (NULL, audio_id[i], NULL, 0);
    audiosrccaps = gst_caps_append (audiosrccaps, temp);
  }
  audiosrctempl = gst_pad_template_new ("audio_%02d",
					GST_PAD_SRC,
					GST_PAD_SOMETIMES,
					audiosrccaps, NULL);

  /* subtitle src template */
  for (i = 0; subtitle_id[i] != NULL; i++) {
    temp = gst_matroska_demux_subtitle_caps (NULL, subtitle_id[i], NULL, 0);
    subtitlesrccaps = gst_caps_append (subtitlesrccaps, temp);
  }
  subtitlesrctempl = gst_pad_template_new ("subtitle_%02d",
					   GST_PAD_SRC,
					   GST_PAD_SOMETIMES,
					   subtitlesrccaps, NULL);

  /* create an elementfactory for the matroska_demux element */
  if (!gst_element_register (plugin, "matroskademux",
			     GST_RANK_PRIMARY, GST_TYPE_MATROSKA_DEMUX))
    return FALSE;

  return TRUE;
}
