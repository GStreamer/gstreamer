/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2006 Tim-Philipp Müller <tim centricular net>
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

/* TODO: "Unkown track header" & "Unknown entry": implement if useful
 * TODO: dynamic number of tracks without upper bound
 * FIXME: uint64 -> int64 overflows!
 * TODO: check CRC32 if present
 * FIXME: go out of loops, don't add Track or whatever if something goes wrong
 *        or required elements are not there
 * TODO: there can be a segment after the first segment. Handle like
 *       chained oggs. Fixes #334082
 * TODO: handle gaps better, especially gaps at the start of a track.
 *       Needs sending of filler segments, closing of segments and
 *       other magic... Fixes #429322
 * TODO: Test samples: http://www.matroska.org/samples/matrix/index.html
 *                     http://samples.mplayerhq.hu/Matroska/
 * TODO: check if demuxing is done correct for all codecs according to spec
 * TODO: seeking with incomplete or without CUE
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <string.h>

/* For AVI compatibility mode
   and for fourcc stuff */
#include <gst/riff/riff-read.h>
#include <gst/riff/riff-ids.h>
#include <gst/riff/riff-media.h>

#ifdef HAVE_ZLIB
#include <zlib.h>
#endif

#include "matroska-demux.h"
#include "matroska-ids.h"

GST_DEBUG_CATEGORY_STATIC (matroskademux_debug);
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

/* TODO: fill in caps! */

static GstStaticPadTemplate audio_src_templ =
GST_STATIC_PAD_TEMPLATE ("audio_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate video_src_templ =
GST_STATIC_PAD_TEMPLATE ("video_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate subtitle_src_templ =
    GST_STATIC_PAD_TEMPLATE ("subtitle_%02d",
    GST_PAD_SRC,
    GST_PAD_SOMETIMES,
    GST_STATIC_CAPS ("text/plain; application/x-ssa; application/x-ass; "
        "application/x-usf; video/x-dvd-subpicture; "
        "application/x-subtitle-unknown")
    );

static GstFlowReturn gst_matroska_demux_parse_contents (GstMatroskaDemux *
    demux, gboolean * p_run_loop);

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
static GstCaps
    * gst_matroska_demux_subtitle_caps (GstMatroskaTrackSubtitleContext *
    subtitlecontext, const gchar * codec_id, gpointer data, guint size);

/* stream methods */
static void gst_matroska_demux_reset (GstElement * element);

GST_BOILERPLATE (GstMatroskaDemux, gst_matroska_demux, GstEbmlRead,
    GST_TYPE_EBML_READ);

static void
gst_matroska_demux_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&subtitle_src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));

  gst_element_class_set_details_simple (element_class, "Matroska demuxer",
      "Codec/Demuxer",
      "Demuxes a Matroska Stream into video/audio/subtitles",
      "Ronald Bultje <rbultje@ronald.bitfreak.net>");
}

static void
gst_matroska_demux_class_init (GstMatroskaDemuxClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;;

  GST_DEBUG_CATEGORY_INIT (matroskademux_debug, "matroskademux", 0,
      "Matroska demuxer");

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_change_state);
  gstelement_class->send_event =
      GST_DEBUG_FUNCPTR (gst_matroska_demux_element_send_event);
}

static void
gst_matroska_demux_init (GstMatroskaDemux * demux,
    GstMatroskaDemuxClass * klass)
{
  gint i;

  demux->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
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
gst_matroska_track_free (GstMatroskaTrackContext * track)
{
  g_free (track->codec_id);
  g_free (track->codec_name);
  g_free (track->name);
  g_free (track->language);
  g_free (track->codec_priv);

  if (track->encodings != NULL) {
    int i;

    for (i = 0; i < track->encodings->len; ++i) {
      GstMatroskaTrackEncoding *enc = &g_array_index (track->encodings,
          GstMatroskaTrackEncoding,
          i);

      g_free (enc->comp_settings);
    }
    g_array_free (track->encodings, TRUE);
  }

  if (track->pending_tags)
    gst_tag_list_free (track->pending_tags);

  g_free (track);
}

/*
 * Returns the aggregated GstFlowReturn.
 */
static GstFlowReturn
gst_matroska_demux_combine_flows (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * track, GstFlowReturn ret)
{
  guint i;

  /* store the value */
  track->last_flow = ret;

  /* any other error that is not-linked can be returned right away */
  if (ret != GST_FLOW_NOT_LINKED)
    goto done;

  /* only return NOT_LINKED if all other pads returned NOT_LINKED */
  for (i = 0; i < GST_MATROSKA_DEMUX_MAX_STREAMS; i++) {
    GstMatroskaTrackContext *ostream = demux->src[i];

    if (ostream == NULL)
      continue;

    ret = ostream->last_flow;
    /* some other return value (must be SUCCESS but we can return
     * other values as well) */
    if (ret != GST_FLOW_NOT_LINKED)
      goto done;
  }
  /* if we get here, all other pads were unlinked and we return
   * NOT_LINKED then */
done:
  GST_LOG_OBJECT (demux, "combined return %s", gst_flow_get_name (ret));
  return ret;
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
      gst_caps_replace (&demux->src[i]->caps, NULL);
      gst_matroska_track_free (demux->src[i]);
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
  demux->created = G_MININT64;

  demux->index_parsed = FALSE;
  demux->tracks_parsed = FALSE;
  demux->segmentinfo_parsed = FALSE;

  g_list_foreach (demux->tags_parsed, (GFunc) gst_ebml_level_free, NULL);
  g_list_free (demux->tags_parsed);
  demux->tags_parsed = NULL;

  gst_segment_init (&demux->segment, GST_FORMAT_TIME);
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

static gint
gst_matroska_demux_encoding_cmp (gconstpointer a, gconstpointer b)
{
  const GstMatroskaTrackEncoding *enc_a;

  const GstMatroskaTrackEncoding *enc_b;

  enc_a = (const GstMatroskaTrackEncoding *) a;
  enc_b = (const GstMatroskaTrackEncoding *) b;

  /* FIXME: give warning if diff == 0, should be unique! */

  return (gint) enc_b->order - (gint) enc_a->order;
}

static GstFlowReturn
gst_matroska_demux_read_track_encodings (GstEbmlRead * ebml,
    GstMatroskaDemux * demux, GstMatroskaTrackContext * context)
{
  GstFlowReturn ret;

  guint32 id;

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
    return ret;

  context->encodings =
      g_array_sized_new (FALSE, FALSE, sizeof (GstMatroskaTrackEncoding), 1);

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK) {
      break;
    } else if (demux->level_up > 0) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_CONTENTENCODING:{
        GstMatroskaTrackEncoding enc = { 0, };

        /* Set default values */
        enc.scope = 1;

        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
          break;
        }

        while (ret == GST_FLOW_OK) {
          if ((ret =
                  gst_ebml_peek_id (ebml, &demux->level_up,
                      &id)) != GST_FLOW_OK) {
            break;
          } else if (demux->level_up > 0) {
            demux->level_up--;
            break;
          }

          switch (id) {
            case GST_MATROSKA_ID_CONTENTENCODINGORDER:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }
              /* FIXME: must be unique, check this! */
              enc.order = num;
              break;
            }
            case GST_MATROSKA_ID_CONTENTENCODINGSCOPE:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }
              if (num > 7 && num == 0)
                GST_WARNING ("Unknown scope value in contents encoding.");
              else
                enc.scope = num;
              break;
            }
            case GST_MATROSKA_ID_CONTENTENCODINGTYPE:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }
              if (num > 1)
                GST_WARNING ("Unknown type value in contents encoding.");
              else
                enc.type = num;
              break;
            }
            case GST_MATROSKA_ID_CONTENTCOMPRESSION:{

              if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
                break;
              }

              /* FIXME: Might be compressed or encrypted, depending on ContentEncodingScope & 0x4
               * and the previous ContentEncodingOrder */
              while (ret == GST_FLOW_OK) {

                if ((ret =
                        gst_ebml_peek_id (ebml, &demux->level_up,
                            &id)) != GST_FLOW_OK) {
                  break;
                } else if (demux->level_up > 0) {
                  demux->level_up--;
                  break;
                }

                switch (id) {
                  case GST_MATROSKA_ID_CONTENTCOMPALGO:{
                    guint64 num;

                    if ((ret =
                            gst_ebml_read_uint (ebml, &id,
                                &num)) != GST_FLOW_OK) {
                      break;
                    }
                    /* FIXME: maybe don't add tracks at all for which we don't
                     * support the compression algorithm */
                    if (num > 3)
                      GST_WARNING ("Unknown value in encoding compalgo.");
                    else
                      enc.comp_algo = num;
                    break;
                  }
                  case GST_MATROSKA_ID_CONTENTCOMPSETTINGS:{
                    guint8 *data;

                    guint64 size;


                    if ((ret =
                            gst_ebml_read_binary (ebml, &id, &data,
                                &size)) != GST_FLOW_OK) {
                      break;
                    }
                    enc.comp_settings = data;
                    enc.comp_settings_length = size;
                    break;
                  }
                  default:
                    GST_WARNING ("Unknown track compression header entry 0x%x"
                        " - ignoring", id);
                    ret = gst_ebml_read_skip (ebml);
                    break;
                }

                if (demux->level_up) {
                  demux->level_up--;
                  break;
                }
              }
              break;
            }

            case GST_MATROSKA_ID_CONTENTENCRYPTION:
              GST_WARNING ("Encrypted tracks not yet supported");
              /* FIXME: Might be compressed, depending on ContentEncodingScope & 0x4 
               * and the previous ContentEncodingOrder */
              /* FIXME: don't add encrypted tracks at all */
              ret = gst_ebml_read_skip (ebml);
              break;
            default:
              GST_WARNING
                  ("Unknown track encoding header entry 0x%x - ignoring", id);
              ret = gst_ebml_read_skip (ebml);
              break;
          }

          if (demux->level_up) {
            demux->level_up--;
            break;
          }
        }

        g_array_append_val (context->encodings, enc);
        break;
      }

      default:
        GST_WARNING ("Unknown track encodings header entry 0x%x - ignoring",
            id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  /* Sort encodings according to their order */
  g_array_sort (context->encodings, gst_matroska_demux_encoding_cmp);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_add_stream (GstMatroskaDemux * demux)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (demux);

  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstMatroskaTrackContext *context;

  GstPadTemplate *templ = NULL;

  GstCaps *caps = NULL;

  gchar *padname = NULL;

  GstFlowReturn ret;

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
  context->set_discont = TRUE;
  context->timecodescale = 1.0;
  context->flags =
      GST_MATROSKA_TRACK_ENABLED | GST_MATROSKA_TRACK_DEFAULT |
      GST_MATROSKA_TRACK_LACING;
  context->last_flow = GST_FLOW_OK;
  demux->num_streams++;

  /* start with the master */
  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
    return ret;

  /* try reading the trackentry headers */
  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK) {
      break;
    } else if (demux->level_up > 0) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* FIXME: check unique */
        /* track number (unique stream ID) */
      case GST_MATROSKA_ID_TRACKNUMBER:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
          break;
        }
        if (num == 0) {
          GST_WARNING ("Invalid track number (0) - skipping");
          ret = GST_FLOW_ERROR;
          break;
        }

        context->num = num;
        break;
      }
        /* track UID (unique identifier) */
      case GST_MATROSKA_ID_TRACKUID:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
          break;
        }
        if (num == 0) {
          GST_WARNING ("Invalid track UID (0) - skipping");
          ret = GST_FLOW_ERROR;
          break;
        }

        context->uid = num;
        break;
      }

        /* track type (video, audio, combined, subtitle, etc.) */
      case GST_MATROSKA_ID_TRACKTYPE:{
        guint64 track_type;

        if ((ret = gst_ebml_read_uint (ebml, &id, &track_type)) != GST_FLOW_OK) {
          break;
        }

        if (context->type != 0 && context->type != track_type) {
          GST_WARNING
              ("More than one tracktype defined in a trackentry - skipping");
          break;
        } else if (track_type < 1 || track_type > 254) {
          GST_WARNING ("Invalid track type (%u) - skipping",
              (guint) track_type);
          break;
        }

        /* ok, so we're actually going to reallocate this thing */
        switch (track_type) {
          case GST_MATROSKA_TRACK_TYPE_VIDEO:
            gst_matroska_track_init_video_context (&context);
            break;
          case GST_MATROSKA_TRACK_TYPE_AUDIO:
            gst_matroska_track_init_audio_context (&context);
            break;
          case GST_MATROSKA_TRACK_TYPE_SUBTITLE:
            gst_matroska_track_init_subtitle_context (&context);
            break;
          case GST_MATROSKA_TRACK_TYPE_COMPLEX:
          case GST_MATROSKA_TRACK_TYPE_LOGO:
          case GST_MATROSKA_TRACK_TYPE_BUTTONS:
          case GST_MATROSKA_TRACK_TYPE_CONTROL:
          default:
            GST_WARNING ("Unknown or unsupported track type %"
                G_GUINT64_FORMAT, track_type);
            context->type = 0;
            break;
        }
        demux->src[demux->num_streams - 1] = context;
        break;
      }

        /* tracktype specific stuff for video */
      case GST_MATROSKA_ID_TRACKVIDEO:{
        GstMatroskaTrackVideoContext *videocontext;

        if (!gst_matroska_track_init_video_context (&context)) {
          GST_WARNING
              ("trackvideo EBML entry in non-video track - ignoring track");
          ret = GST_FLOW_ERROR;
          break;
        } else if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
          break;
        }
        videocontext = (GstMatroskaTrackVideoContext *) context;
        demux->src[demux->num_streams - 1] = context;

        while (ret == GST_FLOW_OK) {
          if ((ret =
                  gst_ebml_peek_id (ebml, &demux->level_up,
                      &id)) != GST_FLOW_OK) {
            break;
          } else if (demux->level_up > 0) {
            demux->level_up--;
            break;
          }

          switch (id) {
              /* Should be one level up but some broken muxers write it here. */
            case GST_MATROSKA_ID_TRACKDEFAULTDURATION:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num == 0) {
                GST_WARNING ("Invalid track default duration (0) - ignoring");
                break;
              }

              context->default_duration = num;
              break;
            }

              /* video framerate */
              /* NOTE: This one is here only for backward compatibility.
               * Use _TRACKDEFAULDURATION one level up. */
            case GST_MATROSKA_ID_VIDEOFRAMERATE:{
              gdouble num;

              if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num <= 0.0) {
                GST_WARNING ("Invalid video framerate (%lf fps) - ignoring",
                    num);
                break;
              }

              if (context->default_duration == 0)
                context->default_duration =
                    gst_gdouble_to_guint64 ((gdouble) GST_SECOND * (1.0 / num));
              videocontext->default_fps = num;
              break;
            }

              /* width of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYWIDTH:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num == 0) {
                GST_WARNING ("Invalid display width (0) - ignoring");
                break;
              }

              videocontext->display_width = num;
              GST_DEBUG ("display_width %" G_GUINT64_FORMAT, num);
              break;
            }

              /* height of the size to display the video at */
            case GST_MATROSKA_ID_VIDEODISPLAYHEIGHT:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num == 0) {
                GST_WARNING ("Invalid display height (0) - ignoring");
                break;
              }

              videocontext->display_height = num;
              GST_DEBUG ("display_height %" G_GUINT64_FORMAT, num);
              break;
            }

              /* width of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELWIDTH:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num == 0) {
                GST_WARNING ("Invalid pixel width (0) - ignoring");
                break;
              }

              videocontext->pixel_width = num;
              GST_DEBUG ("pixel_width %" G_GUINT64_FORMAT, num);
              break;
            }

              /* height of the video in the file */
            case GST_MATROSKA_ID_VIDEOPIXELHEIGHT:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num == 0) {
                GST_WARNING ("Invalid pixel height (0) - ignoring");
                break;
              }

              videocontext->pixel_height = num;
              GST_DEBUG ("pixel_height %" G_GUINT64_FORMAT, num);
              break;
            }

              /* whether the video is interlaced */
            case GST_MATROSKA_ID_VIDEOFLAGINTERLACED:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
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

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
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
            case GST_MATROSKA_ID_VIDEOASPECTRATIOTYPE:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
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

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num > G_MAXUINT32) {
                GST_WARNING ("Invalid video colourspace (%" G_GUINT64_FORMAT
                    ") - ignoring", num);
                break;
              }
              videocontext->fourcc = num;
              break;
            }

            case GST_MATROSKA_ID_VIDEODISPLAYUNIT:
            case GST_MATROSKA_ID_VIDEOPIXELCROPBOTTOM:
            case GST_MATROSKA_ID_VIDEOPIXELCROPTOP:
            case GST_MATROSKA_ID_VIDEOPIXELCROPLEFT:
            case GST_MATROSKA_ID_VIDEOPIXELCROPRIGHT:
            case GST_MATROSKA_ID_VIDEOGAMMAVALUE:
            default:
              GST_WARNING ("Unknown video track header entry 0x%x - ignoring",
                  id);
              ret = gst_ebml_read_skip (ebml);
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

        if (!gst_matroska_track_init_audio_context (&context)) {
          GST_WARNING
              ("trackaudio EBML entry in non-audio track - ignoring track");
          ret = GST_FLOW_ERROR;
          break;
        } else if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
          break;
        }
        audiocontext = (GstMatroskaTrackAudioContext *) context;
        demux->src[demux->num_streams - 1] = context;

        while (ret == GST_FLOW_OK) {
          if ((ret =
                  gst_ebml_peek_id (ebml, &demux->level_up,
                      &id)) != GST_FLOW_OK) {
            break;
          } else if (demux->level_up > 0) {
            demux->level_up--;
            break;
          }

          switch (id) {
              /* samplerate */
            case GST_MATROSKA_ID_AUDIOSAMPLINGFREQ:{
              gdouble num;

              if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num <= 0.0) {
                GST_WARNING ("Invalid audio sample rate (%lf) - ignoring)",
                    num);
                break;
              }

              if (context->default_duration == 0)
                context->default_duration =
                    gst_gdouble_to_guint64 ((gdouble) GST_SECOND * (1.0 / num));

              audiocontext->samplerate = num;
              break;
            }

              /* bitdepth */
            case GST_MATROSKA_ID_AUDIOBITDEPTH:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num == 0) {
                GST_WARNING ("Invalid audio bit depth (0) - ignoring)");
                break;
              }

              audiocontext->bitdepth = num;
              break;
            }

              /* channels */
            case GST_MATROSKA_ID_AUDIOCHANNELS:{
              guint64 num;

              if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
                break;
              }

              if (num == 0) {
                GST_WARNING
                    ("Invalid number of audio channels (0) - ignoring)");
                break;
              }

              audiocontext->channels = num;
              break;
            }

            case GST_MATROSKA_ID_AUDIOCHANNELPOSITIONS:
            case GST_MATROSKA_ID_AUDIOOUTPUTSAMPLINGFREQ:
            default:
              GST_WARNING ("Unknown audio track header entry 0x%x - ignoring",
                  id);
              ret = gst_ebml_read_skip (ebml);
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

        if ((ret = gst_ebml_read_ascii (ebml, &id, &text)) != GST_FLOW_OK) {
          break;
        }
        context->codec_id = text;
        break;
      }

        /* codec private data */
      case GST_MATROSKA_ID_CODECPRIVATE:{
        guint8 *data;

        guint64 size;

        if ((ret =
                gst_ebml_read_binary (ebml, &id, &data,
                    &size)) != GST_FLOW_OK) {
          break;
        }
        /* TODO: might be compressed or encrypted */
        context->codec_priv = data;
        context->codec_priv_size = size;
        GST_LOG_OBJECT (demux, "%u bytes of codec private data", (guint) size);
        break;
      }

        /* name of the codec */
      case GST_MATROSKA_ID_CODECNAME:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK) {
          break;
        }
        context->codec_name = text;
        break;
      }

        /* name of this track */
      case GST_MATROSKA_ID_TRACKNAME:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK) {
          break;
        }
        context->name = text;
        GST_LOG ("stream %d: trackname=%s", context->index, text);
        break;
      }

        /* language (matters for audio/subtitles, mostly) */
      case GST_MATROSKA_ID_TRACKLANGUAGE:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK) {
          break;
        }

        context->language = text;
        GST_LOG ("stream %d: language=%s", context->index, text);

        /* fre-ca => fre */
        if (strlen (context->language) >= 4 && context->language[3] == '-')
          context->language[3] = '\0';
        break;
      }

        /* whether this is actually used */
      case GST_MATROSKA_ID_TRACKFLAGENABLED:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
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

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
          break;
        }
        if (num)
          context->flags |= GST_MATROSKA_TRACK_DEFAULT;
        else
          context->flags &= ~GST_MATROSKA_TRACK_DEFAULT;
        break;
      }

        /* whether the track must be used during playback */
      case GST_MATROSKA_ID_TRACKFLAGFORCED:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num)
          context->flags |= GST_MATROSKA_TRACK_FORCED;
        else
          context->flags &= ~GST_MATROSKA_TRACK_FORCED;
        break;
      }

        /* lacing (like MPEG, where blocks don't end/start on frame
         * boundaries) */
      case GST_MATROSKA_ID_TRACKFLAGLACING:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
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

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
          break;
        }

        if (num == 0) {
          GST_WARNING ("Invalid track default duration (0) - ignoring");
          break;
        }

        context->default_duration = num;
        break;
      }

      case GST_MATROSKA_ID_CONTENTENCODINGS:{
        ret = gst_matroska_demux_read_track_encodings (ebml, demux, context);
        break;
      }

      case GST_MATROSKA_ID_TRACKTIMECODESCALE:{
        gdouble num;

        if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK)
          break;

        if (num <= 0.0) {
          GST_WARNING ("Invalid track time code scale (%lf) - ignoring", num);
          break;
        }

        context->timecodescale = num;
        break;
      }

      default:
        GST_WARNING ("Unknown track header entry 0x%x - ignoring", id);
        /* pass-through */

        /* we ignore these because they're nothing useful (i.e. crap)
         * or simply not implemented yet. */
      case GST_MATROSKA_ID_TRACKMINCACHE:
      case GST_MATROSKA_ID_TRACKMAXCACHE:
      case GST_MATROSKA_ID_MAXBLOCKADDITIONID:
      case GST_MATROSKA_ID_TRACKATTACHMENTLINK:
      case GST_MATROSKA_ID_TRACKOVERLAY:
      case GST_MATROSKA_ID_TRACKTRANSLATE:
      case GST_MATROSKA_ID_TRACKOFFSET:
      case GST_MATROSKA_ID_CODECSETTINGS:
      case GST_MATROSKA_ID_CODECINFOURL:
      case GST_MATROSKA_ID_CODECDOWNLOADURL:
      case GST_MATROSKA_ID_CODECDECODEALL:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (context->type == 0 || context->codec_id == NULL || ret != GST_FLOW_OK) {
    if (ret == GST_FLOW_OK)
      GST_WARNING ("Unknown stream/codec in track entry header");

    demux->num_streams--;
    demux->src[demux->num_streams] = NULL;
    if (context) {
      gst_matroska_track_free (context);
    }

    return ret;
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
      if (codec) {
        list = gst_tag_list_new ();
        gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
            GST_TAG_AUDIO_CODEC, codec, NULL);
        g_free (codec);
      }
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

    case GST_MATROSKA_TRACK_TYPE_COMPLEX:
    case GST_MATROSKA_TRACK_TYPE_LOGO:
    case GST_MATROSKA_TRACK_TYPE_BUTTONS:
    case GST_MATROSKA_TRACK_TYPE_CONTROL:
    default:
      /* we should already have quit by now */
      g_assert_not_reached ();
  }

  if ((context->language == NULL || *context->language == '\0') &&
      (context->type == GST_MATROSKA_TRACK_TYPE_AUDIO ||
          context->type == GST_MATROSKA_TRACK_TYPE_SUBTITLE)) {
    GST_LOG ("stream %d: language=eng (assuming default)", context->index);
    context->language = g_strdup ("eng");
  }

  if (context->language) {
    if (!list)
      list = gst_tag_list_new ();
    gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
        GST_TAG_LANGUAGE_CODE, context->language, NULL);
  }

  if (caps == NULL) {
    GST_WARNING_OBJECT (demux, "could not determine caps for stream with "
        "codec_id='%s'", context->codec_id);
    switch (context->type) {
      case GST_MATROSKA_TRACK_TYPE_VIDEO:
        caps = gst_caps_new_simple ("video/x-unknown", NULL);
        break;
      case GST_MATROSKA_TRACK_TYPE_AUDIO:
        caps = gst_caps_new_simple ("audio/x-unknown", NULL);
        break;
      case GST_MATROSKA_TRACK_TYPE_SUBTITLE:
        caps = gst_caps_new_simple ("application/x-subtitle-unknown", NULL);
        break;
      case GST_MATROSKA_TRACK_TYPE_COMPLEX:
      default:
        caps = gst_caps_new_simple ("application/x-matroska-unknown", NULL);
        break;
    }
    gst_caps_set_simple (caps, "codec-id", G_TYPE_STRING, context->codec_id,
        NULL);
  }

  /* the pad in here */
  context->pad = gst_pad_new_from_template (templ, padname);
  context->caps = caps;

  gst_pad_set_event_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_handle_src_event));
  gst_pad_set_query_type_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_get_src_query_types));
  gst_pad_set_query_function (context->pad,
      GST_DEBUG_FUNCPTR (gst_matroska_demux_handle_src_query));

  GST_INFO_OBJECT (demux, "Adding pad '%s' with caps %" GST_PTR_FORMAT,
      padname, caps);

  context->pending_tags = list;

  gst_pad_use_fixed_caps (context->pad);
  gst_pad_set_caps (context->pad, context->caps);
  gst_pad_set_active (context->pad, TRUE);
  gst_element_add_pad (GST_ELEMENT (demux), context->pad);

  g_free (padname);

  /* tadaah! */
  return ret;
}

static const GstQueryType *
gst_matroska_demux_get_src_query_types (GstPad * pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
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

  /* FIXME: do queries on the Tracks, not on the Segment.
   * Convert between time and frames if we know the duration
   * of one frame for the track */
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;

      gst_query_parse_position (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG ("only query position on TIME is supported");
        break;
      }

      GST_OBJECT_LOCK (demux);
      gst_query_set_position (query, GST_FORMAT_TIME, demux->segment.last_stop);
      GST_OBJECT_UNLOCK (demux);

      res = TRUE;
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;

      gst_query_parse_duration (query, &format, NULL);

      if (format != GST_FORMAT_TIME) {
        GST_DEBUG ("only query duration on TIME is supported");
        break;
      }

      GST_OBJECT_LOCK (demux);
      gst_query_set_duration (query, GST_FORMAT_TIME, demux->segment.duration);
      GST_OBJECT_UNLOCK (demux);

      res = TRUE;
      break;
    }

    default:
      res = gst_pad_query_default (pad, query);
      break;
  }

  gst_object_unref (demux);
  return res;
}


static GstMatroskaIndex *
gst_matroskademux_do_index_seek (GstMatroskaDemux * demux, gint64 seek_pos,
    gint64 segment_stop, gboolean keyunit)
{
  guint entry;

  guint n = 0;

  if (!demux->num_indexes)
    return NULL;

  if (keyunit) {
    /* find index entry closest to the requested position */
    entry = 0;
    for (n = 0; n < demux->num_indexes; ++n) {
      gdouble d_entry, d_this;

      d_entry = fabs (gst_guint64_to_gdouble (demux->index[entry].time) -
          gst_guint64_to_gdouble (seek_pos));
      d_this = fabs (gst_guint64_to_gdouble (demux->index[n].time) -
          gst_guint64_to_gdouble (seek_pos));

      if (d_this < d_entry &&
          (demux->index[n].time < segment_stop || segment_stop == -1)) {
        entry = n;
      }
    }
  } else {
    /* find index entry at or before the requested position */
    entry = demux->num_indexes - 1;

    while (n < demux->num_indexes - 1) {
      if ((demux->index[n].time <= seek_pos) &&
          (demux->index[n + 1].time > seek_pos)) {
        entry = n;
        break;
      }
      n++;
    }
  }

  return &demux->index[entry];
}

/* takes ownership of the passed event! */
static gboolean
gst_matroska_demux_send_event (GstMatroskaDemux * demux, GstEvent * event)
{
  gboolean ret = TRUE;

  gint i;

  g_return_val_if_fail (event != NULL, FALSE);

  GST_DEBUG_OBJECT (demux, "Sending event of type %s to all source pads",
      GST_EVENT_TYPE_NAME (event));

  for (i = 0; i < demux->num_streams; i++) {
    GstMatroskaTrackContext *stream;

    stream = demux->src[i];
    gst_event_ref (event);
    gst_pad_push_event (stream->pad, event);

    if (stream->pending_tags) {
      gst_element_found_tags_for_pad (GST_ELEMENT (demux), stream->pad,
          stream->pending_tags);
      stream->pending_tags = NULL;
    }
  }
  gst_event_unref (event);
  return ret;
}

static gboolean
gst_matroska_demux_element_send_event (GstElement * element, GstEvent * event)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (element);

  gboolean res;

  g_return_val_if_fail (event != NULL, FALSE);

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

  gboolean flush, keyunit;

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
    GST_OBJECT_LOCK (demux);
    if (!gst_matroskademux_do_index_seek (demux, cur, -1, FALSE)) {
      GST_DEBUG ("No matching seek entry in index");
      GST_OBJECT_UNLOCK (demux);
      return FALSE;
    }
    GST_DEBUG ("Seek position looks sane");
    GST_OBJECT_UNLOCK (demux);
  }

  flush = !!(flags & GST_SEEK_FLAG_FLUSH);
  keyunit = !!(flags & GST_SEEK_FLAG_KEY_UNIT);

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
  GST_PAD_STREAM_LOCK (demux->sinkpad);

  GST_OBJECT_LOCK (demux);

  /* if nothing configured, play complete file */
  if (!GST_CLOCK_TIME_IS_VALID (cur))
    cur = 0;
  if (!GST_CLOCK_TIME_IS_VALID (stop))
    stop = demux->segment.duration;
  /* prevent some calculations and comparisons involving INVALID */
  segment_start = demux->segment.start;
  segment_stop = demux->segment.stop;
  if (!GST_CLOCK_TIME_IS_VALID (segment_start))
    segment_start = 0;
  if (!GST_CLOCK_TIME_IS_VALID (segment_stop))
    segment_stop = demux->segment.duration;

  if (cur_type == GST_SEEK_TYPE_SET)
    segment_start = cur;
  else if (cur_type == GST_SEEK_TYPE_CUR)
    segment_start += cur;

  if (stop_type == GST_SEEK_TYPE_SET)
    segment_stop = stop;
  else if (stop_type == GST_SEEK_TYPE_CUR)
    segment_stop += stop;

  segment_start = CLAMP (segment_start, 0, demux->segment.duration);
  segment_stop = CLAMP (segment_stop, 0, demux->segment.duration);

  GST_DEBUG ("New segment positions: %" GST_TIME_FORMAT "-%" GST_TIME_FORMAT,
      GST_TIME_ARGS (segment_start), GST_TIME_ARGS (segment_stop));

  entry = gst_matroskademux_do_index_seek (demux, segment_start,
      segment_stop, keyunit);

  if (!entry) {
    GST_DEBUG ("No matching seek entry in index");
    goto seek_error;
  }

  /* seek (relative to matroska segment) */
  if (gst_ebml_read_seek (GST_EBML_READ (demux),
          entry->pos + demux->ebml_segment_start) != GST_FLOW_OK) {
    GST_DEBUG ("Failed to seek to offset %" G_GUINT64_FORMAT,
        entry->pos + demux->ebml_segment_start);
    goto seek_error;
  }

  GST_DEBUG ("Seeked to offset %" G_GUINT64_FORMAT, entry->pos +
      demux->ebml_segment_start);

  if (keyunit) {
    GST_DEBUG ("seek to key unit, adjusting segment start to %"
        GST_TIME_FORMAT, GST_TIME_ARGS (entry->time));
    segment_start = entry->time;
  }

  GST_DEBUG ("Committing new seek segment");

  demux->segment.rate = rate;
  demux->segment.flags = flags;

  demux->segment.start = segment_start;
  demux->segment.stop = segment_stop;

  GST_OBJECT_UNLOCK (demux);

  /* notify start of new segment */
  if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
    GstMessage *msg;

    msg = gst_message_new_segment_start (GST_OBJECT (demux),
        GST_FORMAT_TIME, demux->segment.start);
    gst_element_post_message (GST_ELEMENT (demux), msg);
  }

  newsegment_event = gst_event_new_new_segment (FALSE, rate,
      GST_FORMAT_TIME, segment_start, segment_stop, segment_start);

  if (flush) {
    GST_DEBUG ("Stopping flush");
    gst_pad_push_event (demux->sinkpad, gst_event_new_flush_stop ());
    gst_matroska_demux_send_event (demux, gst_event_new_flush_stop ());
  } else if (demux->segment_running) {
    /* FIXME, the current segment was still running when we performed the
     * seek, we need to close the current segment */
    GST_DEBUG ("Closing currently running segment");
  }

  /* send newsegment event to all source pads and update the time */
  gst_matroska_demux_send_event (demux, newsegment_event);
  for (i = 0; i < demux->num_streams; i++) {
    demux->src[i]->pos = entry->time;
    demux->src[i]->set_discont = TRUE;
    demux->src[i]->last_flow = GST_FLOW_OK;
  }
  demux->segment.last_stop = entry->time;

  /* restart our task since it might have been stopped when we did the
   * flush. */
  demux->segment_running = TRUE;
  gst_pad_start_task (demux->sinkpad, (GstTaskFunction) gst_matroska_demux_loop,
      demux->sinkpad);

  /* streaming can continue now */
  GST_PAD_STREAM_UNLOCK (demux->sinkpad);

  return TRUE;

seek_error:
  {
    /* FIXME: shouldn't we either make it a real error or start the task
     * function again so that things can continue from where they left off? */
    GST_DEBUG ("Got a seek error");
    GST_OBJECT_UNLOCK (demux);
    GST_PAD_STREAM_UNLOCK (demux->sinkpad);
    return FALSE;
  }
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
    case GST_EVENT_QOS:
      res = FALSE;
      break;

    default:
      GST_WARNING ("Unhandled %s event, dropped", GST_EVENT_TYPE_NAME (event));
      res = FALSE;
      break;
  }

  gst_object_unref (demux);
  gst_event_unref (event);

  return res;
}

static GstFlowReturn
gst_matroska_demux_init_stream (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  guint32 id;

  gchar *doctype;

  guint version;

  GstFlowReturn ret;

  if ((ret = gst_ebml_read_header (ebml, &doctype, &version)) != GST_FLOW_OK)
    return ret;

  if (!doctype || strcmp (doctype, "matroska") != 0) {
    GST_ELEMENT_ERROR (demux, STREAM, WRONG_TYPE, (NULL),
        ("Input is not a matroska stream (doctype=%s)",
            doctype ? doctype : "none"));
    g_free (doctype);
    return GST_FLOW_ERROR;
  }
  g_free (doctype);
  if (version > 2) {
    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
        ("Demuxer version (2) is too old to read stream version %d", version));
    return GST_FLOW_ERROR;
  }

  /* find segment, must be the next element but search as long as
   * we find it anyway */
  while (TRUE) {
    guint last_level;

    if ((ret = gst_ebml_peek_id (ebml, &last_level, &id)) != GST_FLOW_OK) {
      GST_DEBUG_OBJECT (demux, "gst_ebml_peek_id() failed!");
      return ret;
    }

    if (id == GST_MATROSKA_ID_SEGMENT)
      break;

    /* oi! */
    GST_WARNING ("Expected a Segment ID (0x%x), but received 0x%x!",
        GST_MATROSKA_ID_SEGMENT, id);

    if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
      return ret;
  }

  /* we now have a EBML segment */
  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "gst_ebml_read_master() failed!");
    return ret;
  }

  /* seeks are from the beginning of the segment,
   * after the segment ID/length */
  demux->ebml_segment_start = ebml->offset;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_tracks (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret = GST_FLOW_OK;

  guint32 id;

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK) {
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one track within the "all-tracks" header */
      case GST_MATROSKA_ID_TRACKENTRY:
        ret = gst_matroska_demux_add_stream (demux);
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in track header", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  demux->tracks_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_index_cuetrack (GstMatroskaDemux * demux,
    gboolean prevent_eos, GstMatroskaIndex * idx, guint64 length)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  guint32 id;

  GstFlowReturn ret;

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
    return ret;

  while (TRUE) {
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* track number */
      case GST_MATROSKA_ID_CUETRACK:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          goto error;
        if (num == 0) {
          idx->track = -1;
          GST_WARNING ("Invalid cue track number (0)");
          goto error;
          break;
        }

        idx->track = num;
        break;
      }

        /* position in file */
      case GST_MATROSKA_ID_CUECLUSTERPOSITION:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK)
          goto error;

        /* FIXME: may overflow, our seeks, etc are int64 based */

        idx->pos = num;
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in CuesTrackPositions", id);
        /* fall-through */

      case GST_MATROSKA_ID_CUEBLOCKNUMBER:
      case GST_MATROSKA_ID_CUECODECSTATE:
      case GST_MATROSKA_ID_CUEREFERENCE:
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          goto error;
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return ret;

error:
  if (demux->level_up)
    demux->level_up--;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_index_pointentry (GstMatroskaDemux * demux,
    gboolean prevent_eos, guint64 length)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstMatroskaIndex idx;

  guint32 id;

  GstFlowReturn ret;

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
    return ret;

  /* in the end, we hope to fill one entry with a
   * timestamp, a file position and a tracknum */
  idx.pos = (guint64) - 1;
  idx.time = (guint64) - 1;
  idx.track = (guint16) - 1;

  while (ret == GST_FLOW_OK) {
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_CUETIME:
      {
        guint64 time;

        if ((ret = gst_ebml_read_uint (ebml, &id, &time)) == GST_FLOW_OK) {
          idx.time = time * demux->time_scale;
        }
        break;
      }

        /* position in the file + track to which it belongs */
      case GST_MATROSKA_ID_CUETRACKPOSITIONS:
      {
        ret = gst_matroska_demux_parse_index_cuetrack (demux, prevent_eos, &idx,
            length);
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in cuespoint index", id);
        ret = gst_ebml_read_skip (ebml);
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

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_index (GstMatroskaDemux * demux, gboolean prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  guint32 id;

  guint64 length = 0;

  GstFlowReturn ret = GST_FLOW_OK;

  if (prevent_eos) {
    length = gst_ebml_read_get_length (ebml);
  }

  while (ret == GST_FLOW_OK) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* one single index entry ('point') */
      case GST_MATROSKA_ID_POINTENTRY:
        ret = gst_matroska_demux_parse_index_pointentry (demux, prevent_eos,
            length);
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in cues header", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  demux->index_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_info (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret = GST_FLOW_OK;

  guint32 id;

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK) {
      break;
    } else if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_TIMECODESCALE:{
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) != GST_FLOW_OK) {
          break;
        }
        demux->time_scale = num;
        break;
      }

      case GST_MATROSKA_ID_DURATION:{
        gdouble num;

        GstClockTime dur;

        if ((ret = gst_ebml_read_float (ebml, &id, &num)) != GST_FLOW_OK) {
          break;
        }

        if (num <= 0.0) {
          GST_WARNING ("Invalid duration (%lf) - skipping", num);
          break;
        }

        dur = gst_gdouble_to_guint64 (num *
            gst_guint64_to_gdouble (demux->time_scale));
        if (GST_CLOCK_TIME_IS_VALID (dur) && dur <= G_MAXINT64)
          demux->segment.duration = dur;
        break;
      }

      case GST_MATROSKA_ID_WRITINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK) {
          break;
        }
        demux->writing_app = text;
        break;
      }

      case GST_MATROSKA_ID_MUXINGAPP:{
        gchar *text;

        if ((ret = gst_ebml_read_utf8 (ebml, &id, &text)) != GST_FLOW_OK) {
          break;
        }
        demux->muxing_app = text;
        break;
      }

      case GST_MATROSKA_ID_DATEUTC:{
        gint64 time;

        if ((ret = gst_ebml_read_date (ebml, &id, &time)) != GST_FLOW_OK) {
          break;
        }
        demux->created = time;
        break;
      }

      case GST_MATROSKA_ID_SEGMENTUID:
      case GST_MATROSKA_ID_SEGMENTFILENAME:
      case GST_MATROSKA_ID_PREVUID:
      case GST_MATROSKA_ID_PREVFILENAME:
      case GST_MATROSKA_ID_NEXTUID:
      case GST_MATROSKA_ID_NEXTFILENAME:
      case GST_MATROSKA_ID_TITLE:
      case GST_MATROSKA_ID_SEGMENTFAMILY:
      case GST_MATROSKA_ID_CHAPTERTRANSLATE:{
        /* TODO not yet implemented. */
        ret = gst_ebml_read_skip (ebml);
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in info header", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  demux->segmentinfo_parsed = TRUE;

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_metadata_id_simple_tag (GstMatroskaDemux * demux,
    gboolean prevent_eos, guint64 length, GstTagList ** p_taglist)
{
  /* FIXME: check if there are more useful mappings */
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
    GST_MATROSKA_TAG_ID_BPS, GST_TAG_BITRATE}, {
    GST_MATROSKA_TAG_ID_ENCODER, GST_TAG_ENCODER}, {
    GST_MATROSKA_TAG_ID_DATE, GST_TAG_DATE}, {
    GST_MATROSKA_TAG_ID_ISRC, GST_TAG_ISRC}, {
    GST_MATROSKA_TAG_ID_COPYRIGHT, GST_TAG_COPYRIGHT}, {
    GST_MATROSKA_TAG_ID_BPM, GST_TAG_BEATS_PER_MINUTE}, {
    GST_MATROSKA_TAG_ID_TERMS_OF_USE, GST_TAG_LICENSE}, {
    GST_MATROSKA_TAG_ID_COMPOSER, GST_TAG_COMPOSER}, {
    GST_MATROSKA_TAG_ID_LEAD_PERFORMER, GST_TAG_PERFORMER}, {
    GST_MATROSKA_TAG_ID_GENRE, GST_TAG_GENRE}
  };
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret;

  guint32 id;

  gchar *value = NULL;

  gchar *tag = NULL;

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
    return ret;

  while (ret == GST_FLOW_OK) {
    /* read all sub-entries */
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_TAGNAME:
        g_free (tag);
        tag = NULL;
        ret = gst_ebml_read_ascii (ebml, &id, &tag);
        break;

      case GST_MATROSKA_ID_TAGSTRING:
        g_free (value);
        value = NULL;
        ret = gst_ebml_read_utf8 (ebml, &id, &value);
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in metadata collection", id);
        /* fall-through */

      case GST_MATROSKA_ID_TAGLANGUAGE:
      case GST_MATROSKA_ID_TAGDEFAULT:
      case GST_MATROSKA_ID_TAGBINARY:
        ret = gst_ebml_read_skip (ebml);
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
        GValue dest = { 0, };
        GType dest_type = gst_tag_get_type (tagname_gst);

        g_value_init (&dest, dest_type);
        if (gst_value_deserialize (&dest, value)) {
          gst_tag_list_add_values (*p_taglist, GST_TAG_MERGE_APPEND,
              tagname_gst, &dest, NULL);
        } else {
          GST_WARNING_OBJECT (demux, "Can't transform tag '%s' with "
              "value '%s' to target type '%s'", tag, value,
              g_type_name (dest_type));
        }
        g_value_unset (&dest);
        break;
      }
    }
  }

  g_free (tag);
  g_free (value);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_metadata_id_tag (GstMatroskaDemux * demux,
    gboolean prevent_eos, guint64 length, GstTagList ** p_taglist)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  guint32 id;

  GstFlowReturn ret;

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
    return ret;

  while (ret == GST_FLOW_OK) {
    /* read all sub-entries */
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SIMPLETAG:
        ret = gst_matroska_demux_parse_metadata_id_simple_tag (demux,
            prevent_eos, length, p_taglist);
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in metadata collection", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_metadata (GstMatroskaDemux * demux,
    gboolean prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstTagList *taglist = gst_tag_list_new ();

  GstFlowReturn ret = GST_FLOW_OK;

  guint64 length = 0;

  guint32 id;

  GList *l;

  GstEbmlLevel *curlevel;

  /* Can't be NULL at this point */
  g_assert (ebml->level != NULL);
  curlevel = ebml->level->data;

  /* Make sure we don't parse a tags element twice and
   * post it's tags twice */
  for (l = demux->tags_parsed; l; l = l->next) {
    GstEbmlLevel *level = l->data;

    if (ebml->level)
      curlevel = ebml->level->data;
    else
      break;

    if (level->start == curlevel->start && level->length == curlevel->length) {
      GST_DEBUG_OBJECT (demux, "Skipping already parsed Tags at offset %"
          G_GUINT64_FORMAT, ebml->offset);
      ret = gst_ebml_read_skip (ebml);
      return ret;
    }
  }

  GST_DEBUG_OBJECT (demux, "Parsing Tags at offset %" G_GUINT64_FORMAT,
      ebml->offset);
  /* TODO: g_slice_dup() if we depend on GLib 2.14 */
  curlevel = g_slice_new (GstEbmlLevel);
  memcpy (curlevel, ebml->level->data, sizeof (GstEbmlLevel));
  demux->tags_parsed = g_list_prepend (demux->tags_parsed, curlevel);

  /* TODO: review length/eos logic */
  if (prevent_eos) {
    length = gst_ebml_read_get_length (ebml);
  }

  while (ret == GST_FLOW_OK) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_TAG:
        ret = gst_matroska_demux_parse_metadata_id_tag (demux, prevent_eos,
            length, &taglist);
        break;

      default:
        GST_WARNING ("Unknown entry 0x%x in metadata header", id);
        /* FIXME: Use to limit the tags to specific pads */
      case GST_MATROSKA_ID_TARGETS:
        ret = gst_ebml_read_skip (ebml);
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

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_attachments (GstMatroskaDemux * demux,
    gboolean prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  guint64 length = 0;

  guint32 id;

  GstFlowReturn ret = GST_FLOW_OK;

  GST_WARNING_OBJECT (demux, "Parsing of attachments not implemented yet");

  /* TODO: implement parsing of attachments */

  if (prevent_eos) {
    length = gst_ebml_read_get_length (ebml);
  }

  while (ret == GST_FLOW_OK) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      default:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_chapters (GstMatroskaDemux * demux,
    gboolean prevent_eos)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  guint64 length = 0;

  guint32 id;

  GstFlowReturn ret = GST_FLOW_OK;

  GST_WARNING_OBJECT (demux, "Parsing of chapters not implemented yet");

  /* TODO: implement parsing of chapters */
  if (prevent_eos) {
    length = gst_ebml_read_get_length (ebml);
  }

  while (ret == GST_FLOW_OK) {
    /* We're an element that can be seeked to. If we are, then
     * we want to prevent EOS, since that'll kill us. So we cache
     * file size and seek until there, and don't call EOS upon os. */
    if (prevent_eos && length == ebml->offset)
      break;

    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      default:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return ret;
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

  GST_LOG ("Sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (demux->segment.last_stop));

  for (stream_nr = 0; stream_nr < demux->num_streams; stream_nr++) {
    GstMatroskaTrackContext *context;

    context = demux->src[stream_nr];
    if (context->type != GST_MATROSKA_TRACK_TYPE_SUBTITLE)
      continue;

    GST_LOG ("Checking for resync on stream %d (%" GST_TIME_FORMAT ")",
        stream_nr, GST_TIME_ARGS (context->pos));

    /* does it lag? 0.5 seconds is a random treshold... */
    if (context->pos + (GST_SECOND / 2) < demux->segment.last_stop) {
      GST_DEBUG ("Synchronizing stream %d with others by advancing time "
          "from %" GST_TIME_FORMAT " to %" GST_TIME_FORMAT, stream_nr,
          GST_TIME_ARGS (context->pos),
          GST_TIME_ARGS (demux->segment.last_stop));

      context->pos = demux->segment.last_stop;

      /* advance stream time */
      gst_pad_push_event (context->pad,
          gst_event_new_new_segment (TRUE, demux->segment.rate,
              GST_FORMAT_TIME, demux->segment.last_stop, -1,
              demux->segment.last_stop));
    }
  }
}

static GstFlowReturn
gst_matroska_demux_push_hdr_buf (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream, guint8 * data, guint len)
{
  GstFlowReturn ret, cret;

  GstBuffer *header_buf = NULL;

  ret = gst_pad_alloc_buffer_and_set_caps (stream->pad,
      GST_BUFFER_OFFSET_NONE, len, stream->caps, &header_buf);

  /* we combine but don't use the combined value to check if we have a buffer
   * or not. The combined value is what we return. */
  cret = gst_matroska_demux_combine_flows (demux, stream, ret);
  if (ret != GST_FLOW_OK)
    goto no_buffer;

  memcpy (GST_BUFFER_DATA (header_buf), data, len);

  if (stream->set_discont) {
    GST_BUFFER_FLAG_SET (header_buf, GST_BUFFER_FLAG_DISCONT);
    stream->set_discont = FALSE;
  }

  ret = gst_pad_push (stream->pad, header_buf);

  /* combine flows */
  cret = gst_matroska_demux_combine_flows (demux, stream, ret);

  return cret;

  /* ERRORS */
no_buffer:
  {
    GST_DEBUG_OBJECT (demux, "could not alloc buffer: %s, combined %s",
        gst_flow_get_name (ret), gst_flow_get_name (cret));
    return cret;
  }
}

static GstFlowReturn
gst_matroska_demux_push_flac_codec_priv_data (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  GstFlowReturn ret;

  guint8 *pdata;

  guint off, len;

  GST_LOG_OBJECT (demux, "priv data size = %u", stream->codec_priv_size);

  pdata = (guint8 *) stream->codec_priv;

  /* need at least 'fLaC' marker + STREAMINFO metadata block */
  if (stream->codec_priv_size < ((4) + (4 + 34))) {
    GST_WARNING_OBJECT (demux, "not enough codec priv data for flac headers");
    return GST_FLOW_ERROR;
  }

  if (memcmp (pdata, "fLaC", 4) != 0) {
    GST_WARNING_OBJECT (demux, "no flac marker at start of stream headers");
    return GST_FLOW_ERROR;
  }

  ret = gst_matroska_demux_push_hdr_buf (demux, stream, pdata, 4);
  if (ret != GST_FLOW_OK)
    return ret;

  off = 4;                      /* skip fLaC marker */
  while (off < stream->codec_priv_size) {
    len = GST_READ_UINT8 (pdata + off + 1) << 16;
    len |= GST_READ_UINT8 (pdata + off + 2) << 8;
    len |= GST_READ_UINT8 (pdata + off + 3);

    GST_DEBUG_OBJECT (demux, "header packet: len=%u bytes, flags=0x%02x",
        len, (guint) pdata[off]);

    ret = gst_matroska_demux_push_hdr_buf (demux, stream, pdata + off, len);
    if (ret != GST_FLOW_OK)
      return ret;

    off += 4 + len;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_matroska_demux_push_xiph_codec_priv_data (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  GstFlowReturn ret;

  guint8 *p = (guint8 *) stream->codec_priv;

  gint i, offset, length, num_packets;

  /* start of the stream and vorbis audio or theora video, need to
   * send the codec_priv data as first three packets */
  num_packets = p[0] + 1;
  GST_DEBUG_OBJECT (demux, "%u stream headers, total length=%u bytes",
      (guint) num_packets, stream->codec_priv_size);

  offset = num_packets;         /* offset to data of first packet */

  for (i = 0; i < num_packets - 1; i++) {
    length = p[i + 1];

    GST_DEBUG_OBJECT (demux, "buffer %d: length=%u bytes", i, (guint) length);
    if (offset + length > stream->codec_priv_size)
      return GST_FLOW_ERROR;

    ret = gst_matroska_demux_push_hdr_buf (demux, stream, p + offset, length);
    if (ret != GST_FLOW_OK)
      return ret;

    offset += length;
  }

  length = stream->codec_priv_size - offset;
  GST_DEBUG_OBJECT (demux, "buffer %d: length=%u bytes", i, (guint) length);
  ret = gst_matroska_demux_push_hdr_buf (demux, stream, p + offset, length);
  if (ret != GST_FLOW_OK)
    return ret;

  return GST_FLOW_OK;
}

static void
gst_matroska_demux_push_dvd_clut_change_event (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream)
{
  gchar *buf, *start;

  g_assert (!strcmp (stream->codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_VOBSUB));

  if (!stream->codec_priv)
    return;

  /* ideally, VobSub private data should be parsed and stored more convenient
   * elsewhere, but for now, only interested in a small part */

  /* make sure we have terminating 0 */
  buf = g_strndup (stream->codec_priv, stream->codec_priv_size);

  /* just locate and parse palette part */
  start = strstr (stream->codec_priv, "palette:");
  if (start) {
    gint i;

    guint32 clut[16];

    guint32 col;

    guint8 r, g, b, y, u, v;

    start += 8;
    while (g_ascii_isspace (*start))
      start++;
    for (i = 0; i < 16; i++) {
      if (sscanf (start, "%06x", &col) != 1)
        break;
      start += 6;
      while ((*start == ',') || g_ascii_isspace (*start))
        start++;
      /* sigh, need to convert this from vobsub pseudo-RGB to YUV */
      r = (col >> 16) & 0xff;
      g = (col >> 8) & 0xff;
      b = col & 0xff;
      y = CLAMP ((0.1494 * r + 0.6061 * g + 0.2445 * b) * 219 / 255 + 16, 0,
          255);
      u = CLAMP (0.6066 * r - 0.4322 * g - 0.1744 * b + 128, 0, 255);
      v = CLAMP (-0.08435 * r - 0.3422 * g + 0.4266 * b + 128, 0, 255);
      clut[i] = (y << 16) | (u << 8) | v;
    }

    /* got them all without problems; build and send event */
    if (i == 16) {
      GstStructure *s;

      s = gst_structure_new ("application/x-gst-dvd", "event", G_TYPE_STRING,
          "dvd-spu-clut-change", "clut00", G_TYPE_INT, clut[0], "clut01",
          G_TYPE_INT, clut[1], "clut02", G_TYPE_INT, clut[2], "clut03",
          G_TYPE_INT, clut[3], "clut04", G_TYPE_INT, clut[4], "clut05",
          G_TYPE_INT, clut[5], "clut06", G_TYPE_INT, clut[6], "clut07",
          G_TYPE_INT, clut[7], "clut08", G_TYPE_INT, clut[8], "clut09",
          G_TYPE_INT, clut[9], "clut10", G_TYPE_INT, clut[10], "clut11",
          G_TYPE_INT, clut[11], "clut12", G_TYPE_INT, clut[12], "clut13",
          G_TYPE_INT, clut[13], "clut14", G_TYPE_INT, clut[14], "clut15",
          G_TYPE_INT, clut[15], NULL);

      gst_pad_push_event (stream->pad,
          gst_event_new_custom (GST_EVENT_CUSTOM_DOWNSTREAM, s));
    }
  }
  g_free (buf);
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

static GstFlowReturn
gst_matroska_demux_add_wvpk_header (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream, gint block_length, GstBuffer ** buf)
{
  GstBuffer *newbuf;

  guint8 *data;

  guint newlen;

  GstFlowReturn ret, cret;

  /* we need to reconstruct the header of the wavpack block */
  Wavpack4Header wvh;

  /* FIXME: broken for > 2 channels and hybrid files
     http://www.matroska.org/technical/specs/codecid/wavpack.html */

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
  ret = gst_pad_alloc_buffer_and_set_caps (stream->pad, GST_BUFFER_OFFSET_NONE,
      newlen, stream->caps, &newbuf);
  cret = gst_matroska_demux_combine_flows (demux, stream, ret);
  if (ret != GST_FLOW_OK) {
    GST_DEBUG_OBJECT (demux, "pad_alloc failed %s, combined %s",
        gst_flow_get_name (ret), gst_flow_get_name (cret));
    return cret;
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
  gst_buffer_copy_metadata (newbuf, *buf, GST_BUFFER_COPY_TIMESTAMPS);
  gst_buffer_unref (*buf);
  *buf = newbuf;

  return cret;
}

static GstBuffer *
gst_matroska_demux_check_subtitle_buffer (GstMatroskaDemux * demux,
    GstMatroskaTrackContext * stream, GstBuffer * buf)
{
  GstMatroskaTrackSubtitleContext *sub_stream;

  const gchar *encoding, *data;

  GError *err = NULL;

  GstBuffer *newbuf;

  gchar *utf8;

  guint size;

  sub_stream = (GstMatroskaTrackSubtitleContext *) stream;

  if (!sub_stream->check_utf8)
    return buf;

  data = (const gchar *) GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  if (!sub_stream->invalid_utf8) {
    if (g_utf8_validate (data, size, NULL)) {
      return buf;
    }
    GST_WARNING_OBJECT (demux, "subtitle stream %d is not valid UTF-8, this "
        "is broken according to the matroska specification", stream->num);
    sub_stream->invalid_utf8 = TRUE;
  }

  /* file with broken non-UTF8 subtitle, do the best we can do to fix it */
  encoding = g_getenv ("GST_SUBTITLE_ENCODING");
  if (encoding == NULL || *encoding == '\0') {
    /* if local encoding is UTF-8 and no encoding specified
     * via the environment variable, assume ISO-8859-15 */
    if (g_get_charset (&encoding)) {
      encoding = "ISO-8859-15";
    }
  }

  utf8 = g_convert_with_fallback (data, size, "UTF-8", encoding, "*",
      NULL, NULL, &err);

  if (err) {
    GST_LOG_OBJECT (demux, "could not convert string from '%s' to UTF-8: %s",
        encoding, err->message);
    g_error_free (err);
    g_free (utf8);

    /* invalid input encoding, fall back to ISO-8859-15 (always succeeds) */
    encoding = "ISO-8859-15";
    utf8 = g_convert_with_fallback (data, size, "UTF-8", encoding, "*",
        NULL, NULL, NULL);
  }

  GST_LOG_OBJECT (demux, "converted subtitle text from %s to UTF-8 %s",
      encoding, (err) ? "(using ISO-8859-15 as fallback)" : "");

  if (utf8 == NULL)
    utf8 = g_strdup ("invalid subtitle");

  newbuf = gst_buffer_new ();
  GST_BUFFER_MALLOCDATA (newbuf) = (guint8 *) utf8;
  GST_BUFFER_DATA (newbuf) = (guint8 *) utf8;
  GST_BUFFER_SIZE (newbuf) = strlen (utf8);
  gst_buffer_copy_metadata (newbuf, buf, GST_BUFFER_COPY_TIMESTAMPS);
  gst_buffer_unref (buf);

  return newbuf;
}

static GstBuffer *
gst_matroska_decode_buffer (GstMatroskaTrackContext * context, GstBuffer * buf)
{
  gint i;

  g_assert (context->encodings != NULL);

  for (i = 0; i < context->encodings->len; i++) {
    GstMatroskaTrackEncoding *enc;

    guint8 *new_data = NULL;

    guint new_size = 0;

    GstBuffer *new_buf;

    enc = &g_array_index (context->encodings, GstMatroskaTrackEncoding, i);

    /* Currently only compression is supported */
    if (enc->type != 0)
      break;

    /* FIXME: use enc->scope ! only necessary to decode buffer if scope & 0x1 */

    if (enc->comp_algo == 0) {
#ifdef HAVE_ZLIB
      /* zlib encoded track */
      z_stream zstream;

      guint orig_size;

      int result;

      orig_size = GST_BUFFER_SIZE (buf);
      zstream.zalloc = (alloc_func) 0;
      zstream.zfree = (free_func) 0;
      zstream.opaque = (voidpf) 0;
      if (inflateInit (&zstream) != Z_OK) {
        GST_WARNING ("zlib initialization failed.");
        break;
      }
      zstream.next_in = (Bytef *) GST_BUFFER_DATA (buf);
      zstream.avail_in = orig_size;
      new_size = orig_size;
      new_data = g_malloc (new_size);
      zstream.avail_out = new_size;
      /* FIXME: not exactly fast, right? */
      do {
        new_size += 4000;
        new_data = g_realloc (new_data, new_size);
        zstream.next_out = (Bytef *) (new_data + zstream.total_out);
        result = inflate (&zstream, Z_NO_FLUSH);
        if (result != Z_OK && result != Z_STREAM_END) {
          GST_WARNING ("zlib decompression failed.");
          g_free (new_data);
          inflateEnd (&zstream);
          break;
        }
        zstream.avail_out += 4000;
      } while (zstream.avail_out == 4000 &&
          zstream.avail_in != 0 && result != Z_STREAM_END);

      new_size = zstream.total_out;
      inflateEnd (&zstream);
#else
      GST_WARNING ("GZIP encoded tracks not supported.");
      break;
#endif
/* FIXME: add bzip/lzo support, what is header stripped?
 * it's insane and requires deeper knowledge of the used codec
 */
    } else if (enc->comp_algo == 1) {
      GST_WARNING ("BZIP encoded tracks not supported.");
      break;
    } else if (enc->comp_algo == 2) {
      GST_WARNING ("LZO encoded tracks not supported.");
      break;
    } else if (enc->comp_algo == 3) {
      GST_WARNING ("Header-stripped tracks not supported.");
      break;
    } else {
      g_assert_not_reached ();
    }

    g_assert (new_data != NULL);

    new_buf = gst_buffer_new ();
    GST_BUFFER_MALLOCDATA (new_buf) = (guint8 *) new_data;
    GST_BUFFER_DATA (new_buf) = (guint8 *) new_data;
    GST_BUFFER_SIZE (new_buf) = new_size;
    gst_buffer_copy_metadata (new_buf, buf, GST_BUFFER_COPY_TIMESTAMPS);

    gst_buffer_unref (buf);
    buf = new_buf;
  }

  return buf;
}

static GstFlowReturn
gst_matroska_demux_parse_blockgroup_or_simpleblock (GstMatroskaDemux * demux,
    guint64 cluster_time, gboolean is_simpleblock)
{
  GstMatroskaTrackContext *stream = NULL;

  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret = GST_FLOW_OK;

  gboolean readblock = FALSE;

  guint32 id;

  guint64 block_duration = 0;

  GstBuffer *buf = NULL;

  gint stream_num = -1, n, laces = 0;

  guint size = 0;

  gint *lace_size = NULL;

  gint64 time = 0;

  gint flags = 0;

  gint64 referenceblock = 0;

  while (ret == GST_FLOW_OK) {
    if (!is_simpleblock) {
      if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
        goto done;

      if (demux->level_up) {
        demux->level_up--;
        break;
      }
    } else {
      id = GST_MATROSKA_ID_SIMPLEBLOCK;
    }

    switch (id) {
        /* one block inside the group. Note, block parsing is one
         * of the harder things, so this code is a bit complicated.
         * See http://www.matroska.org/ for documentation. */
      case GST_MATROSKA_ID_SIMPLEBLOCK:
      case GST_MATROSKA_ID_BLOCK:
      {
        guint64 num;

        guint8 *data;

        if ((ret = gst_ebml_read_buffer (ebml, &id, &buf)) != GST_FLOW_OK)
          break;

        data = GST_BUFFER_DATA (buf);
        size = GST_BUFFER_SIZE (buf);

        /* first byte(s): blocknum */
        if ((n = gst_matroska_ebmlnum_uint (data, size, &num)) < 0) {
          GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL), ("Data error"));
          gst_buffer_unref (buf);
          buf = NULL;
          ret = GST_FLOW_ERROR;
          break;
        }
        data += n;
        size -= n;

        /* fetch stream from num */
        stream_num = gst_matroska_demux_stream_from_num (demux, num);
        if (size <= 3 || stream_num < 0 || stream_num >= demux->num_streams) {
          gst_buffer_unref (buf);
          buf = NULL;
          GST_WARNING ("Invalid stream %d or size %u", stream_num, size);
          ret = GST_FLOW_ERROR;
          break;
        }

        stream = demux->src[stream_num];

        /* time (relative to cluster time) */
        time = ((gint16) GST_READ_UINT16_BE (data));
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
              GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                  ("Invalid lacing size"));
              ret = GST_FLOW_ERROR;
              break;
            }
            laces = GST_READ_UINT8 (data) + 1;
            data += 1;
            size -= 1;
            lace_size = g_new0 (gint, laces);

            switch ((flags & 0x06) >> 1) {
              case 0x1:        /* xiph lacing */  {
                guint temp, total = 0;

                for (n = 0; ret == GST_FLOW_OK && n < laces - 1; n++) {
                  while (1) {
                    if (size == 0) {
                      GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                          ("Invalid lacing size"));
                      ret = GST_FLOW_ERROR;
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
                  ret = GST_FLOW_ERROR;
                  break;
                }
                data += n;
                size -= n;
                total = lace_size[0] = num;
                for (n = 1; ret == GST_FLOW_OK && n < laces - 1; n++) {
                  gint64 snum;

                  gint r;

                  if ((r = gst_matroska_ebmlnum_sint (data, size, &snum)) < 0) {
                    GST_ELEMENT_ERROR (demux, STREAM, DEMUX, (NULL),
                        ("Data error"));
                    ret = GST_FLOW_ERROR;
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

        if (stream->send_xiph_headers) {
          ret = gst_matroska_demux_push_xiph_codec_priv_data (demux, stream);
          stream->send_xiph_headers = FALSE;
        }

        if (stream->send_flac_headers) {
          ret = gst_matroska_demux_push_flac_codec_priv_data (demux, stream);
          stream->send_flac_headers = FALSE;
        }

        if (stream->send_dvd_event) {
          gst_matroska_demux_push_dvd_clut_change_event (demux, stream);
          /* FIXME: should we send this event again after (flushing) seek ? */
          stream->send_dvd_event = FALSE;
        }

        if (ret != GST_FLOW_OK)
          break;

        readblock = TRUE;
        break;
      }

      case GST_MATROSKA_ID_BLOCKDURATION:{
        ret = gst_ebml_read_uint (ebml, &id, &block_duration);
        break;
      }

      case GST_MATROSKA_ID_REFERENCEBLOCK:{
        ret = gst_ebml_read_sint (ebml, &id, &referenceblock);
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in blockgroup data", id);
        /* fall-through */

      case GST_MATROSKA_ID_BLOCKVIRTUAL:
      case GST_MATROSKA_ID_BLOCKADDITIONS:
      case GST_MATROSKA_ID_REFERENCEPRIORITY:
      case GST_MATROSKA_ID_REFERENCEVIRTUAL:
      case GST_MATROSKA_ID_CODECSTATE:
      case GST_MATROSKA_ID_SLICES:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (is_simpleblock)
      break;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (referenceblock && readblock && demux->src[stream_num]->set_discont) {
    /* When doing seeks or such, we need to restart on key frames or
       decoders might choke. */
    readblock = FALSE;
    gst_buffer_unref (buf);
    buf = NULL;
  }

  if (ret == GST_FLOW_OK && readblock) {
    guint64 duration = 0;

    gint64 lace_time = 0;

    stream = demux->src[stream_num];

    if (cluster_time != GST_CLOCK_TIME_NONE) {
      /* FIXME: What to do with negative timestamps? Give timestamp 0 or -1?
       * Drop unless the lace contains timestamp 0? */
      if (time < 0 && (-time) > cluster_time) {
        lace_time = 0;
      } else {
        if (stream->timecodescale == 1.0)
          lace_time = (cluster_time + time) * demux->time_scale;
        else
          lace_time =
              gst_util_guint64_to_gdouble ((cluster_time + time) *
              demux->time_scale) * stream->timecodescale;
      }
    } else {
      lace_time = GST_CLOCK_TIME_NONE;
    }

    if (block_duration) {
      if (stream->timecodescale == 1.0)
        duration = block_duration * demux->time_scale;
      else
        duration =
            gst_util_gdouble_to_guint64 (gst_util_guint64_to_gdouble
            (block_duration * demux->time_scale) * stream->timecodescale);
    } else if (stream->default_duration) {
      duration = stream->default_duration;
    }
    /* else duration is diff between timecode of this and next block */
    for (n = 0; n < laces; n++) {
      GstBuffer *sub;

      if (lace_size[n] == 0)
        continue;

      sub = gst_buffer_create_sub (buf,
          GST_BUFFER_SIZE (buf) - size, lace_size[n]);

      if (stream->encodings != NULL && stream->encodings->len > 0)
        sub = gst_matroska_decode_buffer (stream, sub);

      GST_BUFFER_TIMESTAMP (sub) = lace_time;
      if (lace_time != GST_CLOCK_TIME_NONE)
        demux->segment.last_stop = lace_time;

      stream->pos = demux->segment.last_stop;
      gst_matroska_demux_sync_streams (demux);

      if (gst_matroska_demux_stream_is_wavpack (stream)) {
        ret =
            gst_matroska_demux_add_wvpk_header (demux, stream, lace_size[n],
            &sub);
      }

      /* FIXME: do all laces have the same length? the lenght of a lace should
       * in theory be default_duration as one lace should contain on frame */
      if (duration) {
        GST_BUFFER_DURATION (sub) = duration / laces;
        stream->pos += GST_BUFFER_DURATION (sub);
      }

      if (is_simpleblock) {
        if (flags & 0x80)
          GST_BUFFER_FLAG_UNSET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
        else
          GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
      } else {
        if (referenceblock) {
          GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
        } else {
          GST_BUFFER_FLAG_UNSET (sub, GST_BUFFER_FLAG_DELTA_UNIT);
        }
      }

      if (stream->set_discont) {
        GST_BUFFER_FLAG_SET (sub, GST_BUFFER_FLAG_DISCONT);
        stream->set_discont = FALSE;
      }

      GST_DEBUG ("Pushing lace %d, data of size %d for stream %d, time=%"
          GST_TIME_FORMAT " and duration=%" GST_TIME_FORMAT, n,
          GST_BUFFER_SIZE (sub), stream_num,
          GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (sub)),
          GST_TIME_ARGS (GST_BUFFER_DURATION (sub)));

      gst_buffer_set_caps (sub, GST_PAD_CAPS (stream->pad));

      /* Fix up broken files with subtitles that are not UTF8 */
      if (stream->type == GST_MATROSKA_TRACK_TYPE_SUBTITLE) {
        sub = gst_matroska_demux_check_subtitle_buffer (demux, stream, sub);
      }

      ret = gst_pad_push (stream->pad, sub);
      /* combine flows */
      ret = gst_matroska_demux_combine_flows (demux, stream, ret);

      size -= lace_size[n];
      if (lace_time != GST_CLOCK_TIME_NONE)
        lace_time += duration;
    }
  }

done:
  if (readblock)
    gst_buffer_unref (buf);
  g_free (lace_size);

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_cluster (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret = GST_FLOW_OK;

  guint64 cluster_time = GST_CLOCK_TIME_NONE;

  guint32 id;

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
        /* cluster timecode */
      case GST_MATROSKA_ID_CLUSTERTIMECODE:
      {
        guint64 num;

        if ((ret = gst_ebml_read_uint (ebml, &id, &num)) == GST_FLOW_OK) {
          cluster_time = num;
        }
        break;
      }

        /* a group of blocks inside a cluster */
      case GST_MATROSKA_ID_BLOCKGROUP:
        if ((ret = gst_ebml_read_master (ebml, &id)) == GST_FLOW_OK) {
          ret = gst_matroska_demux_parse_blockgroup_or_simpleblock (demux,
              cluster_time, FALSE);
        }
        break;

      case GST_MATROSKA_ID_SIMPLEBLOCK:
      {
        ret = gst_matroska_demux_parse_blockgroup_or_simpleblock (demux,
            cluster_time, TRUE);
        break;
      }

      default:
        GST_WARNING ("Unknown entry 0x%x in cluster data", id);
        /* fall-through */

      case GST_MATROSKA_ID_POSITION:
      case GST_MATROSKA_ID_PREVSIZE:
      case GST_MATROSKA_ID_ENCRYPTEDBLOCK:
      case GST_MATROSKA_ID_SILENTTRACKS:
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_contents_seekentry (GstMatroskaDemux * demux,
    gboolean * p_run_loop)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret;

  guint64 seek_pos = (guint64) - 1;

  guint32 seek_id = 0;

  guint32 id;

  if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
    return ret;

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SEEKID:
      {
        guint64 t;

        if ((ret = gst_ebml_read_uint (ebml, &id, &t)) == GST_FLOW_OK) {
          seek_id = t;
        }
        break;
      }

      case GST_MATROSKA_ID_SEEKPOSITION:
      {
        guint64 t;

        if ((ret = gst_ebml_read_uint (ebml, &id, &t)) == GST_FLOW_OK) {
          seek_pos = t;
        }
        break;
      }

      default:
        GST_WARNING ("Unknown seekhead ID 0x%x", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  if (ret != GST_FLOW_OK)
    return ret;

  if (!seek_id || seek_pos == (guint64) - 1) {
    GST_WARNING ("Incomplete seekhead entry (0x%x/%"
        G_GUINT64_FORMAT ")", seek_id, seek_pos);
    return GST_FLOW_OK;
  }

  switch (seek_id) {
    case GST_MATROSKA_ID_CUES:
    case GST_MATROSKA_ID_TAGS:
    case GST_MATROSKA_ID_TRACKS:
    case GST_MATROSKA_ID_SEEKHEAD:
    case GST_MATROSKA_ID_SEGMENTINFO:
    case GST_MATROSKA_ID_ATTACHMENTS:
    case GST_MATROSKA_ID_CHAPTERS:
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
      if (gst_ebml_read_seek (ebml, seek_pos + demux->ebml_segment_start) !=
          GST_FLOW_OK)
        return GST_FLOW_ERROR;

      /* we don't want to lose our seekhead level, so we add
       * a dummy. This is a crude hack. */
      level = g_slice_new (GstEbmlLevel);
      level->start = 0;
      level->length = G_MAXUINT64;
      ebml->level = g_list_prepend (ebml->level, level);

      /* check ID */
      if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
        return ret;

      if (id != seek_id) {
        g_warning ("We looked for ID=0x%x but got ID=0x%x (pos=%"
            G_GUINT64_FORMAT ")", seek_id, id,
            seek_pos + demux->ebml_segment_start);
        goto finish;
      }

      /* read master + parse */
      switch (id) {
        case GST_MATROSKA_ID_CUES:
          if (!demux->index_parsed) {
            if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
              return ret;
            if ((ret =
                    gst_matroska_demux_parse_index (demux,
                        TRUE)) != GST_FLOW_OK)
              return ret;
          }
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          break;
        case GST_MATROSKA_ID_TAGS:
          if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
            return ret;
          if ((ret =
                  gst_matroska_demux_parse_metadata (demux,
                      TRUE)) != GST_FLOW_OK)
            return ret;
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          break;
        case GST_MATROSKA_ID_TRACKS:
          if (!demux->tracks_parsed) {
            if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
              return ret;
            if ((ret = gst_matroska_demux_parse_tracks (demux)) != GST_FLOW_OK)
              return ret;
          }
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          break;

        case GST_MATROSKA_ID_SEGMENTINFO:
          if (!demux->segmentinfo_parsed) {
            if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
              return ret;
            if ((ret = gst_matroska_demux_parse_info (demux)) != GST_FLOW_OK)
              return ret;
          }
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          break;
        case GST_MATROSKA_ID_SEEKHEAD:
        {
          GList *l;

          if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
            return ret;

          /* Prevent infinite recursion if there's a cycle from
           * one seekhead to the same again. Simply break if
           * we already had this seekhead, finish will clean up
           * everything. */
          for (l = ebml->level; l; l = l->next) {
            GstEbmlLevel *level = (GstEbmlLevel *) l->data;

            if (level->start == ebml->offset && l->prev)
              goto finish;
          }

          if ((ret =
                  gst_matroska_demux_parse_contents (demux,
                      p_run_loop)) != GST_FLOW_OK)
            return ret;
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          break;
        }
        case GST_MATROSKA_ID_ATTACHMENTS:
          if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
            return ret;
          if ((ret =
                  gst_matroska_demux_parse_attachments (demux,
                      TRUE)) != GST_FLOW_OK)
            return ret;
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          break;
        case GST_MATROSKA_ID_CHAPTERS:
          if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
            return ret;
          if ((ret =
                  gst_matroska_demux_parse_chapters (demux,
                      TRUE)) != GST_FLOW_OK)
            return ret;
          if (gst_ebml_read_get_length (ebml) == ebml->offset)
            *p_run_loop = FALSE;
          break;
      }

      /* FIXME:
       * used to be here in 0.8 version, but makes mewmew sample not work */
      /* if (*p_run_loop == FALSE) break; */

    finish:
      /* remove dummy level */
      while (ebml->level) {
        guint64 length;

        level = ebml->level->data;
        ebml->level = g_list_delete_link (ebml->level, ebml->level);
        length = level->length;
        g_slice_free (GstEbmlLevel, level);
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

  return ret;
}

static GstFlowReturn
gst_matroska_demux_parse_contents (GstMatroskaDemux * demux,
    gboolean * p_run_loop)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret = GST_FLOW_OK;

  guint32 id;

  while (ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    switch (id) {
      case GST_MATROSKA_ID_SEEKENTRY:
      {
        ret = gst_matroska_demux_parse_contents_seekentry (demux, p_run_loop);
        break;
      }

      default:
        GST_WARNING ("Unknown seekhead ID 0x%x", id);
        ret = gst_ebml_read_skip (ebml);
        break;
    }

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }

  return ret;
}

/* returns FALSE on error, otherwise TRUE */
static GstFlowReturn
gst_matroska_demux_loop_stream_parse_id (GstMatroskaDemux * demux,
    guint32 id, gboolean * p_run_loop)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret;

  switch (id) {
      /* stream info 
       * Can exist more than once but following occurences
       * must have the same content so ignore them */
    case GST_MATROSKA_ID_SEGMENTINFO:
      if (!demux->segmentinfo_parsed) {
        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          return ret;
        if ((ret = gst_matroska_demux_parse_info (demux)) != GST_FLOW_OK)
          return ret;
      } else {
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          return ret;
      }
      break;

      /* track info headers
       * Can exist more than once but following occurences
       * must have the same content so ignore them */
    case GST_MATROSKA_ID_TRACKS:
    {
      if (!demux->tracks_parsed) {
        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          return ret;
        if ((ret = gst_matroska_demux_parse_tracks (demux)) != GST_FLOW_OK)
          return ret;
      } else {
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          return ret;
      }
      break;
    }

      /* cues - seek table
       * Either exists exactly one time or never but ignore
       * following occurences for the sake of sanity */
    case GST_MATROSKA_ID_CUES:
    {
      if (!demux->index_parsed) {
        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          return ret;
        if ((ret =
                gst_matroska_demux_parse_index (demux, FALSE)) != GST_FLOW_OK)
          return ret;
      } else {
        if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
          return ret;
      }
      break;
    }

      /* metadata
       * can exist more than one time with different content */
    case GST_MATROSKA_ID_TAGS:
    {
      if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
        return ret;
      if ((ret =
              gst_matroska_demux_parse_metadata (demux, FALSE)) != GST_FLOW_OK)
        return ret;
      break;
    }

      /* file index (if seekable, seek to Cues/Tags/etc to parse it) */
    case GST_MATROSKA_ID_SEEKHEAD:
    {
      if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
        return ret;
      if ((ret =
              gst_matroska_demux_parse_contents (demux,
                  p_run_loop)) != GST_FLOW_OK)
        return ret;
      break;
    }

      /* cluster - contains the payload */
    case GST_MATROSKA_ID_CLUSTER:
    {
      if (demux->state != GST_MATROSKA_DEMUX_STATE_DATA) {
        /* FIXME: Skip first and try to read TRACKS and other things
         * first, then go back here. */
        demux->state = GST_MATROSKA_DEMUX_STATE_DATA;
        /* FIXME: different streams might have different lengths! */
        /* send initial discont */
        gst_matroska_demux_send_event (demux,
            gst_event_new_new_segment (FALSE, 1.0,
                GST_FORMAT_TIME, 0,
                (demux->segment.duration > 0) ? demux->segment.duration : -1,
                0));
        GST_DEBUG_OBJECT (demux, "signaling no more pads");
        gst_element_no_more_pads (GST_ELEMENT (demux));
      } else {
        if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
          return ret;

        /* The idea is that we parse one cluster per loop and
         * then break out of the loop here. In the next call
         * of the loopfunc, we will get back here with the
         * next cluster. If an error occurs, we didn't
         * actually push a buffer, but we still want to break
         * out of the loop to handle a possible error. We'll
         * get back here if it's recoverable. */
        if ((ret = gst_matroska_demux_parse_cluster (demux)) != GST_FLOW_OK)
          return ret;
        *p_run_loop = FALSE;
      }
      break;
    }

      /* attachments - contains files attached to the mkv container
       * like album art, etc */
    case GST_MATROSKA_ID_ATTACHMENTS:{
      if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
        return ret;
      if ((ret =
              gst_matroska_demux_parse_attachments (demux,
                  FALSE)) != GST_FLOW_OK)
        return ret;
      break;
    }

      /* chapters - contains meta information about how to group
       * the file into chapters, similar to DVD */
    case GST_MATROSKA_ID_CHAPTERS:{
      if ((ret = gst_ebml_read_master (ebml, &id)) != GST_FLOW_OK)
        return ret;
      if ((ret =
              gst_matroska_demux_parse_chapters (demux, FALSE)) != GST_FLOW_OK)
        return ret;
      break;
    }

    default:
      GST_WARNING ("Unknown matroska file header ID 0x%x at %"
          G_GUINT64_FORMAT, id, GST_EBML_READ (demux)->offset);
      if ((ret = gst_ebml_read_skip (ebml)) != GST_FLOW_OK)
        return ret;
      break;
  }
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_matroska_demux_loop_stream (GstMatroskaDemux * demux)
{
  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret = GST_FLOW_OK;

  gboolean run_loop = TRUE;

  guint32 id;

  /* we've found our segment, start reading the different contents in here */
  while (run_loop && ret == GST_FLOW_OK) {
    if ((ret = gst_ebml_peek_id (ebml, &demux->level_up, &id)) != GST_FLOW_OK)
      return ret;

    if (demux->level_up) {
      demux->level_up--;
      break;
    }

    ret = gst_matroska_demux_loop_stream_parse_id (demux, id, &run_loop);

    if (demux->level_up) {
      demux->level_up--;
      break;
    }
  }
  return ret;
}

static void
gst_matroska_demux_loop (GstPad * pad)
{
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (GST_PAD_PARENT (pad));

  GstEbmlRead *ebml = GST_EBML_READ (demux);

  GstFlowReturn ret;

  /* first, if we're to start, let's actually get starting */
  if (demux->state == GST_MATROSKA_DEMUX_STATE_START) {
    ret = gst_matroska_demux_init_stream (demux);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (demux, "init stream failed!");
      goto pause;
    }
    demux->state = GST_MATROSKA_DEMUX_STATE_HEADER;
  }

  ret = gst_matroska_demux_loop_stream (demux);
  if (ret != GST_FLOW_OK)
    goto pause;

  /* check if we're at the end of a configured segment */
  if (GST_CLOCK_TIME_IS_VALID (demux->segment.stop)) {
    guint i;

    for (i = 0; i < demux->num_streams; i++) {
      if (demux->src[i]->pos >= demux->segment.stop) {
        GST_INFO_OBJECT (demux, "Reached end of segment (%" G_GUINT64_FORMAT
            "-%" G_GUINT64_FORMAT ") on pad %s:%s", demux->segment.start,
            demux->segment.stop, GST_DEBUG_PAD_NAME (demux->src[i]->pad));
        ret = GST_FLOW_UNEXPECTED;
        goto pause;
      }
    }
  }

  if (ebml->offset == gst_ebml_read_get_length (ebml)) {
    GST_LOG ("Reached end of stream, sending EOS");
    ret = GST_FLOW_UNEXPECTED;
    goto pause;
  }

  return;

  /* ERRORS */
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_LOG_OBJECT (demux, "pausing task, reason %s", reason);
    demux->segment_running = FALSE;
    gst_pad_pause_task (demux->sinkpad);

    if (GST_FLOW_IS_FATAL (ret) || ret == GST_FLOW_NOT_LINKED) {
      if (ret == GST_FLOW_UNEXPECTED) {
        /* perform EOS logic */
        if (demux->segment.flags & GST_SEEK_FLAG_SEGMENT) {
          gint64 stop;

          /* for segment playback we need to post when (in stream time)
           * we stopped, this is either stop (when set) or the duration. */
          if ((stop = demux->segment.stop) == -1)
            stop = demux->segment.duration;

          GST_LOG_OBJECT (demux, "Sending segment done, at end of segment");
          gst_element_post_message (GST_ELEMENT (demux),
              gst_message_new_segment_done (GST_OBJECT (demux), GST_FORMAT_TIME,
                  stop));
        } else {
          /* normal playback, send EOS to all linked pads */
          GST_LOG_OBJECT (demux, "Sending EOS, at end of stream");
          gst_matroska_demux_send_event (demux, gst_event_new_eos ());
        }
      } else {
        GST_ELEMENT_ERROR (demux, STREAM, FAILED, (NULL),
            ("stream stopped, reason %s", reason));
        gst_matroska_demux_send_event (demux, gst_event_new_eos ());
      }
    }
    return;
  }
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
  GstMatroskaDemux *demux = GST_MATROSKA_DEMUX (GST_PAD_PARENT (sinkpad));

  if (active) {
    /* if we have a scheduler we can start the task */
    demux->segment_running = TRUE;
    gst_pad_start_task (sinkpad, (GstTaskFunction) gst_matroska_demux_loop,
        sinkpad);
  } else {
    demux->segment_running = FALSE;
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

  g_assert (videocontext != NULL);
  g_assert (codec_name != NULL);

  context->send_xiph_headers = FALSE;
  context->send_flac_headers = FALSE;

  /* TODO: check if we have all codec types from matroska-ids.h
   *       check if we have to do more special things with codec_private
   *
   * Add support for
   *  GST_MATROSKA_CODEC_ID_VIDEO_QUICKTIME
   *  GST_MATROSKA_CODEC_ID_VIDEO_SNOW
   */

  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC)) {
    gst_riff_strf_vids *vids = NULL;

    if (data) {
      GstBuffer *buf = NULL;

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

      if (size > sizeof (gst_riff_strf_vids)) { /* some extra_data */
        buf = gst_buffer_new_and_alloc (size - sizeof (gst_riff_strf_vids));
        memcpy (GST_BUFFER_DATA (buf),
            (guint8 *) vids + sizeof (gst_riff_strf_vids),
            GST_BUFFER_SIZE (buf));
      }

      caps = gst_riff_create_video_caps (vids->compression, NULL, vids,
          buf, NULL, codec_name);

      if (buf)
        gst_buffer_unref (buf);
    }
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED)) {
    guint32 fourcc = 0;

    switch (videocontext->fourcc) {
      case GST_MAKE_FOURCC ('I', '4', '2', '0'):
        *codec_name = g_strdup ("Raw planar YUV 4:2:0");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'):
        *codec_name = g_strdup ("Raw packed YUV 4:2:2");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('Y', 'V', '1', '2'):
        *codec_name = g_strdup ("Raw packed YUV 4:2:0");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        *codec_name = g_strdup ("Raw packed YUV 4:2:2");
        fourcc = videocontext->fourcc;
        break;
      case GST_MAKE_FOURCC ('A', 'Y', 'U', 'V'):
        *codec_name = g_strdup ("Raw packed YUV 4:4:4 with alpha channel");
        fourcc = videocontext->fourcc;
        break;

      default:
        GST_DEBUG ("Unknown fourcc %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (videocontext->fourcc));
        return NULL;
    }

    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, fourcc, NULL);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_SP)) {
    caps = gst_caps_new_simple ("video/x-divx",
        "divxversion", G_TYPE_INT, 4, NULL);
    *codec_name = g_strdup ("MPEG-4 simple profile");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AP)) {
#if 0
    caps = gst_caps_new_full (gst_structure_new ("video/x-divx",
            "divxversion", G_TYPE_INT, 5, NULL),
        gst_structure_new ("video/x-xvid", NULL),
        gst_structure_new ("video/mpeg",
            "mpegversion", G_TYPE_INT, 4,
            "systemstream", G_TYPE_BOOLEAN, FALSE, NULL), NULL);
#endif
    caps = gst_caps_new_simple ("video/mpeg",
        "mpegversion", G_TYPE_INT, 4,
        "systemstream", G_TYPE_BOOLEAN, FALSE, NULL);
    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP))
      *codec_name = g_strdup ("MPEG-4 advanced simple profile");
    else
      *codec_name = g_strdup ("MPEG-4 advanced profile");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3)) {
#if 0
    caps = gst_caps_new_full (gst_structure_new ("video/x-divx",
            "divxversion", G_TYPE_INT, 3, NULL),
        gst_structure_new ("video/x-msmpeg",
            "msmpegversion", G_TYPE_INT, 43, NULL), NULL);
#endif
    caps = gst_caps_new_simple ("video/x-msmpeg",
        "msmpegversion", G_TYPE_INT, 43, NULL);
    *codec_name = g_strdup ("Microsoft MPEG-4 v.3");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2)) {
    gint mpegversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG1))
      mpegversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG2))
      mpegversion = 2;
    else
      g_assert_not_reached ();

    caps = gst_caps_new_simple ("video/mpeg",
        "systemstream", G_TYPE_BOOLEAN, FALSE,
        "mpegversion", G_TYPE_INT, mpegversion, NULL);
    *codec_name = g_strdup_printf ("MPEG-%d video", mpegversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MJPEG)) {
    caps = gst_caps_new_simple ("image/jpeg", NULL);
    *codec_name = g_strdup ("Motion-JPEG");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AVC)) {
    caps = gst_caps_new_simple ("video/x-h264", NULL);
    if (data) {
      GstBuffer *priv = gst_buffer_new_and_alloc (size);

      memcpy (GST_BUFFER_DATA (priv), data, size);
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
      gst_buffer_unref (priv);

    }
    *codec_name = g_strdup ("H264");
  } else if ((!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO1)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO2)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO3)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO4))) {
    gint rmversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO1))
      rmversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO2))
      rmversion = 2;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO3))
      rmversion = 3;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO4))
      rmversion = 4;

    caps = gst_caps_new_simple ("video/x-pn-realvideo",
        "rmversion", G_TYPE_INT, rmversion, NULL);
    *codec_name = g_strdup_printf ("RealVideo %d.0", rmversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_THEORA)) {
    caps = gst_caps_new_simple ("video/x-theora", NULL);
    context->send_xiph_headers = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_VIDEO_DIRAC)) {
    caps = gst_caps_new_simple ("video/x-dirac", NULL);
    context->send_xiph_headers = FALSE;
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps", codec_id);
    return NULL;
  }

  if (caps != NULL) {
    int i;

    GstStructure *structure;

    for (i = 0; i < gst_caps_get_size (caps); i++) {
      structure = gst_caps_get_structure (caps, i);

      /* FIXME: use the real unit here! */
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

      if (videocontext->default_fps > 0.0) {
        GValue fps_double = { 0, };
        GValue fps_fraction = { 0, };

        g_value_init (&fps_double, G_TYPE_DOUBLE);
        g_value_init (&fps_fraction, GST_TYPE_FRACTION);
        g_value_set_double (&fps_double, videocontext->default_fps);
        g_value_transform (&fps_double, &fps_fraction);

        gst_structure_set_value (structure, "framerate", &fps_fraction);
        g_value_unset (&fps_double);
        g_value_unset (&fps_fraction);
      } else if (context->default_duration > 0) {
        GValue fps_double = { 0, };
        GValue fps_fraction = { 0, };

        g_value_init (&fps_double, G_TYPE_DOUBLE);
        g_value_init (&fps_fraction, GST_TYPE_FRACTION);
        g_value_set_double (&fps_double, (gdouble) GST_SECOND /
            gst_guint64_to_gdouble (context->default_duration));
        g_value_transform (&fps_double, &fps_fraction);

        gst_structure_set_value (structure, "framerate", &fps_fraction);
        g_value_unset (&fps_double);
        g_value_unset (&fps_fraction);
      } else {
        /* sort of a hack to get most codecs to support,
         * even if the default_duration is missing */
        gst_structure_set (structure, "framerate", GST_TYPE_FRACTION,
            25, 1, NULL);
      }
    }

    gst_caps_do_simplify (caps);
  }

  return caps;
}

/*
 * Some AAC specific code... *sigh*
 * FIXME: maybe we should use '15' and code the sample rate explicitly
 * if the sample rate doesn't match the predefined rates exactly? (tpm)
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

  g_assert (audiocontext != NULL);
  g_assert (codec_name != NULL);

  context->send_xiph_headers = FALSE;
  context->send_flac_headers = FALSE;

  /* TODO: check if we have all codec types from matroska-ids.h
   *       check if we have to do more special things with codec_private
   *       check if we need bitdepth in different places too
   *       implement channel position magic
   * Add support for:
   *  GST_MATROSKA_CODEC_ID_AUDIO_AC3_BSID9
   *  GST_MATROSKA_CODEC_ID_AUDIO_AC3_BSID10
   *  GST_MATROSKA_CODEC_ID_AUDIO_QUICKTIME_QDMC
   *  GST_MATROSKA_CODEC_ID_AUDIO_QUICKTIME_QDM2
   */

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
      g_assert_not_reached ();

    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, 1, "layer", G_TYPE_INT, layer, NULL);
    *codec_name = g_strdup_printf ("MPEG-1 layer %d", layer);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE) ||
      !strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE)) {
    gint endianness = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE))
      endianness = G_BIG_ENDIAN;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE))
      endianness = G_LITTLE_ENDIAN;
    else
      g_assert_not_reached ();

    caps = gst_caps_new_simple ("audio/x-raw-int",
        "width", G_TYPE_INT, audiocontext->bitdepth,
        "depth", G_TYPE_INT, audiocontext->bitdepth,
        "signed", G_TYPE_BOOLEAN, audiocontext->bitdepth == 8,
        "endianness", G_TYPE_INT, endianness, NULL);

    *codec_name = g_strdup_printf ("Raw %d-bit PCM audio",
        audiocontext->bitdepth);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT)) {
    caps = gst_caps_new_simple ("audio/x-raw-float",
        "endianness", G_TYPE_INT, G_LITTLE_ENDIAN,
        "width", G_TYPE_INT, audiocontext->bitdepth, NULL);
    *codec_name = g_strdup_printf ("Raw %d-bit floating-point audio",
        audiocontext->bitdepth);
  } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AC3,
          strlen (GST_MATROSKA_CODEC_ID_AUDIO_AC3))) {
    caps = gst_caps_new_simple ("audio/x-ac3", NULL);
    *codec_name = g_strdup ("AC-3 audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_DTS)) {
    caps = gst_caps_new_simple ("audio/x-dts", NULL);
    *codec_name = g_strdup ("DTS audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_VORBIS)) {
    caps = gst_caps_new_simple ("audio/x-vorbis", NULL);
    context->send_xiph_headers = TRUE;
    /* vorbis decoder does tags */
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_FLAC)) {
    caps = gst_caps_new_simple ("audio/x-flac", NULL);
    context->send_flac_headers = TRUE;
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
    }
  } else if (g_str_has_prefix (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC)) {
    GstBuffer *priv = NULL;

    gint mpegversion = -1;

    gint rate_idx, profile;

    guint8 *data = NULL;

    /* unspecified AAC profile with opaque private codec data */
    if (strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC) == 0) {
      if (context->codec_priv_size >= 2) {
        guint obj_type, freq_index, explicit_freq_bytes = 0;

        codec_id = GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4;
        freq_index = (GST_READ_UINT16_BE (context->codec_priv) & 0x780) >> 7;
        obj_type = (GST_READ_UINT16_BE (context->codec_priv) & 0xF800) >> 11;
        if (freq_index == 15)
          explicit_freq_bytes = 3;
        GST_DEBUG ("obj_type = %u, freq_index = %u", obj_type, freq_index);
        priv = gst_buffer_new_and_alloc (context->codec_priv_size);
        memcpy (GST_BUFFER_DATA (priv), context->codec_priv,
            context->codec_priv_size);
        /* assume SBR if samplerate <= 24kHz */
        if (obj_type == 5 || (freq_index >= 6 && freq_index != 15) ||
            (context->codec_priv_size == (5 + explicit_freq_bytes))) {
          audiocontext->samplerate *= 2;
        }
      } else {
        GST_WARNING ("Opaque A_AAC codec ID, but no codec private data");
        /* just try this and see what happens ... */
        codec_id = GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4;
      }
    }

    /* make up decoder-specific data if it is not supplied */
    if (priv == NULL) {
      priv = gst_buffer_new_and_alloc (5);
      data = GST_BUFFER_DATA (priv);
      rate_idx = aac_rate_idx (audiocontext->samplerate);
      profile = aac_profile_idx (codec_id);

      data[0] = ((profile + 1) << 3) | ((rate_idx & 0xE) >> 1);
      data[1] = ((rate_idx & 0x1) << 7) | (audiocontext->channels << 3);
      GST_BUFFER_SIZE (priv) = 2;
    }

    if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG2,
            strlen (GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG2))) {
      mpegversion = 2;
    } else if (!strncmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4,
            strlen (GST_MATROSKA_CODEC_ID_AUDIO_AAC_MPEG4))) {
      mpegversion = 4;

      if (g_strrstr (codec_id, "SBR")) {
        /* HE-AAC (aka SBR AAC) */
        audiocontext->samplerate *= 2;
        rate_idx = aac_rate_idx (audiocontext->samplerate);
        data[2] = AAC_SYNC_EXTENSION_TYPE >> 3;
        data[3] = ((AAC_SYNC_EXTENSION_TYPE & 0x07) << 5) | 5;
        data[4] = (1 << 7) | (rate_idx << 3);
        GST_BUFFER_SIZE (priv) = 5;
      }
    } else {
      g_assert_not_reached ();
    }

    caps = gst_caps_new_simple ("audio/mpeg",
        "mpegversion", G_TYPE_INT, mpegversion,
        "framed", G_TYPE_BOOLEAN, TRUE, NULL);
    if (priv) {
      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, priv, NULL);
    }
    *codec_name = g_strdup_printf ("MPEG-%d AAC audio", mpegversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_TTA)) {
    caps = gst_caps_new_simple ("audio/x-tta",
        "width", G_TYPE_INT, audiocontext->bitdepth, NULL);
    *codec_name = g_strdup ("TTA audio");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_WAVPACK4)) {
    caps = gst_caps_new_simple ("audio/x-wavpack",
        "width", G_TYPE_INT, audiocontext->bitdepth,
        "framed", G_TYPE_BOOLEAN, TRUE, NULL);
    *codec_name = g_strdup ("Wavpack audio");
  } else if ((!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4)) ||
      (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_COOK))) {
    gint raversion = -1;

    if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4))
      raversion = 1;
    else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_COOK))
      raversion = 8;
    else
      raversion = 2;
    caps = gst_caps_new_simple ("audio/x-pn-realaudio",
        "raversion", G_TYPE_INT, raversion, NULL);
    *codec_name = g_strdup_printf ("RealAudio %d.0", raversion);
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_SIPR)) {
    caps = gst_caps_new_simple ("audio/x-sipro", NULL);
    *codec_name = g_strdup ("Sipro/ACELP.NET Voice Codec");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_RALF)) {
    caps = gst_caps_new_simple ("audio/x-ralf-mpeg4-generic", NULL);
    *codec_name = g_strdup ("Real Audio Lossless");
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_AUDIO_REAL_ATRC)) {
    caps = gst_caps_new_simple ("audio/x-vnd.sony.atrac3", NULL);
    *codec_name = g_strdup ("Sony ATRAC3");
  } else {
    GST_WARNING ("Unknown codec '%s', cannot build Caps", codec_id);
    return NULL;
  }

  if (caps != NULL) {
    if (audiocontext->samplerate > 0 && audiocontext->channels > 0) {
      gint i;

      for (i = 0; i < gst_caps_get_size (caps); i++) {
        gst_structure_set (gst_caps_get_structure (caps, i),
            "channels", G_TYPE_INT, audiocontext->channels,
            "rate", G_TYPE_INT, audiocontext->samplerate, NULL);
      }
    }

    gst_caps_do_simplify (caps);
  }

  return caps;
}

static GstCaps *
gst_matroska_demux_subtitle_caps (GstMatroskaTrackSubtitleContext *
    subtitlecontext, const gchar * codec_id, gpointer data, guint size)
{
  GstCaps *caps = NULL;

  /* for backwards compatibility */
  if (!g_ascii_strcasecmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_ASCII))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8;
  else if (!g_ascii_strcasecmp (codec_id, "S_SSA"))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_SSA;
  else if (!g_ascii_strcasecmp (codec_id, "S_ASS"))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_ASS;
  else if (!g_ascii_strcasecmp (codec_id, "S_USF"))
    codec_id = GST_MATROSKA_CODEC_ID_SUBTITLE_USF;

  /* TODO: Add GST_MATROSKA_CODEC_ID_SUBTITLE_BMP support
   * Check if we have to do something with codec_private */
  if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8)) {
    caps = gst_caps_new_simple ("text/plain", NULL);
    subtitlecontext->check_utf8 = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_SSA)) {
    caps = gst_caps_new_simple ("application/x-ssa", NULL);
    subtitlecontext->check_utf8 = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_ASS)) {
    caps = gst_caps_new_simple ("application/x-ass", NULL);
    subtitlecontext->check_utf8 = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_USF)) {
    caps = gst_caps_new_simple ("application/x-usf", NULL);
    subtitlecontext->check_utf8 = TRUE;
  } else if (!strcmp (codec_id, GST_MATROSKA_CODEC_ID_SUBTITLE_VOBSUB)) {
    caps = gst_caps_new_simple ("video/x-dvd-subpicture", NULL);
    ((GstMatroskaTrackContext *) subtitlecontext)->send_dvd_event = TRUE;
    subtitlecontext->check_utf8 = FALSE;
  } else {
    GST_DEBUG ("Unknown subtitle stream: codec_id='%s'", codec_id);
    caps = gst_caps_new_simple ("application/x-subtitle-unknown", NULL);
    subtitlecontext->check_utf8 = FALSE;
  }

  if (data != NULL && size > 0) {
    GstBuffer *buf;

    buf = gst_buffer_new_and_alloc (size);
    memcpy (GST_BUFFER_DATA (buf), data, size);
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, buf, NULL);
    gst_buffer_unref (buf);
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
  gst_riff_init ();

  /* create an elementfactory for the matroska_demux element */
  if (!gst_element_register (plugin, "matroskademux",
          GST_RANK_PRIMARY, GST_TYPE_MATROSKA_DEMUX))
    return FALSE;

  return TRUE;
}
