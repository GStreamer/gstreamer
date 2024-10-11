/* GStreamer Matroska muxer/demuxer
 * (c) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
 * (c) 2005 Michal Benes <michal.benes@xeris.cz>
 * (c) 2008 Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * (c) 2011 Mark Nauwelaerts <mark.nauwelaerts@collabora.co.uk>
 * (c) 2024 Tim-Philipp Müller <tim centricular com>
 * (c) 2024 Sebastian Dröge <sebastian@centricular.com>
 *
 * matroska-mux.c: matroska file/stream muxer
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

/* TODO: - check everywhere that we don't write invalid values
 *       - make sure timestamps are correctly scaled everywhere
 */

/**
 * SECTION:element-matroskamux
 * @title: matroskamux
 *
 * matroskamux muxes different input streams into a Matroska file.
 *
 * ## Example launch line
 * |[
 * gst-launch-1.0 -v filesrc location=/path/to/mp3 ! mpegaudioparse ! matroskamux name=mux ! filesink location=test.mkv  filesrc location=/path/to/theora.ogg ! oggdemux ! theoraparse ! mux.
 * ]| This pipeline muxes an MP3 file and a Ogg Theora video into a Matroska file.
 * |[
 * gst-launch-1.0 -v audiotestsrc num-buffers=100 ! audioconvert ! vorbisenc ! matroskamux ! filesink location=test.mka
 * ]| This pipeline muxes a 440Hz sine wave encoded with the Vorbis codec into a Matroska file.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

#include <gst/audio/audio.h>
#include <gst/riff/riff-media.h>
#include <gst/tag/tag.h>
#include <gst/pbutils/codec-utils.h>

#include "gstmatroskaelements.h"
#include "matroska-mux.h"
#include "matroska-ids.h"

#define GST_MATROSKA_MUX_CHAPLANG "und"

GST_DEBUG_CATEGORY_STATIC (matroskamux_debug);
#define GST_CAT_DEFAULT matroskamux_debug

enum
{
  PROP_0,
  PROP_WRITING_APP,
  PROP_DOCTYPE_VERSION,
  PROP_MIN_INDEX_INTERVAL,
  PROP_STREAMABLE,
  PROP_TIMECODESCALE,
  PROP_MIN_CLUSTER_DURATION,
  PROP_MAX_CLUSTER_DURATION,
  PROP_OFFSET_TO_ZERO,
  PROP_CREATION_TIME,
  PROP_CLUSTER_TIMESTAMP_OFFSET,
};

#define  DEFAULT_DOCTYPE_VERSION         2
#define  DEFAULT_WRITING_APP             "GStreamer Matroska muxer"
#define  DEFAULT_MIN_INDEX_INTERVAL      0
#define  DEFAULT_STREAMABLE              FALSE
#define  DEFAULT_TIMECODESCALE           GST_MSECOND
#define  DEFAULT_MIN_CLUSTER_DURATION    500 * GST_MSECOND
#define  DEFAULT_MAX_CLUSTER_DURATION    65535 * GST_MSECOND
#define  DEFAULT_OFFSET_TO_ZERO          FALSE
#define  DEFAULT_CLUSTER_TIMESTAMP_OFFSET 0

/* WAVEFORMATEX is gst_riff_strf_auds + an extra guint16 extension size */
#define WAVEFORMATEX_SIZE  (2 + sizeof (gst_riff_strf_auds))

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska; video/x-matroska-3d; audio/x-matroska")
    );

#define COMMON_VIDEO_CAPS \
  "width = (int) [ 1, MAX ], " \
  "height = (int) [ 1, MAX ] "

/* FIXME:
 * * require codec data, etc as needed
 */

static GstStaticPadTemplate videosink_templ =
    GST_STATIC_PAD_TEMPLATE ("video_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("video/mpeg, "
        "mpegversion = (int) { 1, 2, 4 }, "
        "systemstream = (boolean) false, "
        COMMON_VIDEO_CAPS "; "
        "video/x-h264, stream-format = (string) { avc, avc3 }, alignment=au, "
        COMMON_VIDEO_CAPS "; "
        "video/x-h265, stream-format = (string) { hvc1, hev1 }, alignment=au, "
        COMMON_VIDEO_CAPS "; "
        "video/x-divx, "
        COMMON_VIDEO_CAPS "; "
        "video/x-huffyuv, "
        COMMON_VIDEO_CAPS "; "
        "video/x-dv, "
        COMMON_VIDEO_CAPS "; "
        "video/x-h263, "
        COMMON_VIDEO_CAPS "; "
        "video/x-msmpeg, "
        COMMON_VIDEO_CAPS "; "
        "image/jpeg, "
        COMMON_VIDEO_CAPS "; "
        "video/x-theora; "
        "video/x-dirac, "
        COMMON_VIDEO_CAPS "; "
        "video/x-pn-realvideo, "
        "rmversion = (int) [1, 4], "
        COMMON_VIDEO_CAPS "; "
        "video/x-vp8, "
        COMMON_VIDEO_CAPS "; "
        "video/x-vp9, "
        COMMON_VIDEO_CAPS "; "
        "video/x-raw, "
        "format = (string) { YUY2, I420, YV12, UYVY, AYUV, GRAY8, GRAY10_LE32,"
        " GRAY16_LE, BGR, RGB, RGBA64_LE, BGRA64_LE }, "
        COMMON_VIDEO_CAPS "; "
        "video/x-prores, "
        COMMON_VIDEO_CAPS "; "
        "video/x-wmv, " "wmvversion = (int) [ 1, 3 ], " COMMON_VIDEO_CAPS "; "
        "video/x-av1, " "stream-format = (string) \"obu-stream\", "
        "alignment = (string) \"tu\", " COMMON_VIDEO_CAPS ";"
        "video/x-ffv, ffversion = (int) 1, " COMMON_VIDEO_CAPS)
    );

#define COMMON_AUDIO_CAPS \
  "channels = (int) [ 1, MAX ], " \
  "rate = (int) [ 1, MAX ]"

/* FIXME:
 * * require codec data, etc as needed
 */
static GstStaticPadTemplate audiosink_templ =
    GST_STATIC_PAD_TEMPLATE ("audio_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 1, "
        "layer = (int) [ 1, 3 ], "
        COMMON_AUDIO_CAPS "; "
        "audio/mpeg, "
        "mpegversion = (int) { 2, 4 }, "
        "stream-format = (string) raw, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-ac3, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-eac3, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-dts, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-vorbis, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-flac, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-opus, "
        "channels = (int) [ 1, 8 ], "
        "rate = (int) { 8000, 16000, 24000, 32000, 48000 }; "
        "audio/x-speex, "
        COMMON_AUDIO_CAPS "; "
        "audio/x-raw, "
        "format = (string) { U8, S16BE, S16LE, S24BE, S24LE, S32BE, S32LE, F32LE, F64LE }, "
        "layout = (string) interleaved, "
        COMMON_AUDIO_CAPS ";"
        "audio/x-tta, "
        "width = (int) { 8, 16, 24 }, "
        "channels = (int) { 1, 2 }, " "rate = (int) [ 8000, 96000 ]; "
        "audio/x-pn-realaudio, "
        "raversion = (int) { 1, 2, 8 }, " COMMON_AUDIO_CAPS "; "
        "audio/x-wma, " "wmaversion = (int) [ 1, 3 ], "
        "block_align = (int) [ 0, 65535 ], bitrate = (int) [ 0, 524288 ], "
        COMMON_AUDIO_CAPS ";"
        "audio/x-alaw, "
        "channels = (int) {1, 2}, " "rate = (int) [ 8000, 192000 ]; "
        "audio/x-mulaw, "
        "channels = (int) {1, 2}, " "rate = (int) [ 8000, 192000 ]; "
        "audio/x-adpcm, "
        "layout = (string)dvi, "
        "block_align = (int)[64, 8192], "
        "channels = (int) { 1, 2 }, " "rate = (int) [ 8000, 96000 ]; "
        "audio/G722, "
        "channels = (int)1," "rate = (int)16000; "
        "audio/x-adpcm, "
        "layout = (string)g726, " "channels = (int)1," "rate = (int)8000; ")
    );

static GstStaticPadTemplate subtitlesink_templ =
    GST_STATIC_PAD_TEMPLATE ("subtitle_%u",
    GST_PAD_SINK,
    GST_PAD_REQUEST,
    GST_STATIC_CAPS ("subtitle/x-kate; "
        "text/x-raw, format=utf8; application/x-ssa; application/x-ass; "
        "application/x-usf; subpicture/x-dvd; "
        "application/x-subtitle-unknown")
    );

static gpointer parent_class;   /* NULL */

static void gst_matroska_mux_class_init (GstMatroskaMuxClass * klass);
static void gst_matroska_mux_init (GstMatroskaMux * mux, gpointer g_class);
static void gst_matroska_mux_finalize (GObject * object);

static GstFlowReturn gst_matroska_mux_aggregate (GstAggregator * agg,
    gboolean timeout);
static gboolean gst_matroska_mux_sink_event (GstAggregator * agg,
    GstAggregatorPad * agg_pad, GstEvent * event);
static gboolean gst_matroska_mux_src_event (GstAggregator * agg,
    GstEvent * event);
static gboolean gst_matroska_mux_stop (GstAggregator * agg);
static GstBuffer *gst_matroska_mux_clip (GstAggregator * agg,
    GstAggregatorPad * agg_pad, GstBuffer * buffer);
static GstClockTime gst_matroska_mux_get_next_time (GstAggregator * agg);

static GstPad *gst_matroska_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_matroska_mux_release_pad (GstElement * element, GstPad * pad);

static void gst_matroska_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_matroska_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_matroska_pad_reset (GstMatroskaMuxPad * pad, gboolean full);

/* uid generation */
static guint64 gst_matroska_mux_create_uid (void);

static gboolean theora_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context);
static gboolean vorbis_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context);
static gboolean speex_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context);
static gboolean kate_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context);
static gboolean flac_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context);
static void
gst_matroska_mux_write_simple_tag (const GstTagList * list, const gchar * tag,
    gpointer data);
static gboolean gst_matroska_mux_tag_list_is_empty (const GstTagList * list);
static void gst_matroska_mux_write_streams_tags (GstMatroskaMux * mux);
static gboolean gst_matroska_mux_streams_have_tags (GstMatroskaMux * mux);

/* Cannot use boilerplate macros here because we need the full init function
 * signature with the additional class argument, so we use the right template
 * for the sink caps */
GType
gst_matroska_mux_get_type (void)
{
  static GType object_type;     /* 0 */

  if (object_type == 0) {
    static const GTypeInfo object_info = {
      sizeof (GstMatroskaMuxClass),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) gst_matroska_mux_class_init,
      NULL,                     /* class_finalize */
      NULL,                     /* class_data */
      sizeof (GstMatroskaMux),
      0,                        /* n_preallocs */
      (GInstanceInitFunc) gst_matroska_mux_init
    };
    const GInterfaceInfo iface_info = { NULL };

    object_type = g_type_register_static (GST_TYPE_AGGREGATOR,
        "GstMatroskaMux", &object_info, (GTypeFlags) 0);

    g_type_add_interface_static (object_type, GST_TYPE_TAG_SETTER, &iface_info);
    g_type_add_interface_static (object_type, GST_TYPE_TOC_SETTER, &iface_info);
  }

  return object_type;
}

GST_ELEMENT_REGISTER_DEFINE_WITH_CODE (matroskamux, "matroskamux",
    GST_RANK_PRIMARY, GST_TYPE_MATROSKA_MUX, matroska_element_init (plugin));

static void
gst_matroska_mux_class_init (GstMatroskaMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstAggregatorClass *gstaggregator_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstaggregator_class = (GstAggregatorClass *) klass;

  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &videosink_templ, GST_TYPE_MATROSKA_MUX_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &audiosink_templ, GST_TYPE_MATROSKA_MUX_PAD);
  gst_element_class_add_static_pad_template_with_gtype (gstelement_class,
      &subtitlesink_templ, GST_TYPE_MATROSKA_MUX_PAD);

  gst_element_class_add_static_pad_template (gstelement_class, &src_templ);

  gst_element_class_set_static_metadata (gstelement_class, "Matroska muxer",
      "Codec/Muxer",
      "Muxes video/audio/subtitle streams into a matroska stream",
      "GStreamer maintainers <gstreamer-devel@lists.freedesktop.org>");

  GST_DEBUG_CATEGORY_INIT (matroskamux_debug, "matroskamux", 0,
      "Matroska muxer");

  gobject_class->finalize = gst_matroska_mux_finalize;

  gobject_class->get_property = gst_matroska_mux_get_property;
  gobject_class->set_property = gst_matroska_mux_set_property;

  g_object_class_install_property (gobject_class, PROP_WRITING_APP,
      g_param_spec_string ("writing-app", "Writing application.",
          "The name the application that creates the matroska file.",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DOCTYPE_VERSION,
      g_param_spec_int ("version", "DocType version",
          "This parameter determines what Matroska features can be used.",
          1, 2, DEFAULT_DOCTYPE_VERSION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_INDEX_INTERVAL,
      g_param_spec_int64 ("min-index-interval", "Minimum time between index "
          "entries", "An index entry is created every so many nanoseconds.",
          0, G_MAXINT64, DEFAULT_MIN_INDEX_INTERVAL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_STREAMABLE,
      g_param_spec_boolean ("streamable", "Determines whether output should "
          "be streamable", "If set to true, the output should be as if it is "
          "to be streamed and hence no indexes written or duration written.",
          DEFAULT_STREAMABLE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TIMECODESCALE,
      g_param_spec_int64 ("timecodescale", "Timecode Scale",
          "TimecodeScale used to calculate the Raw Timecode of a Block", 1,
          GST_SECOND, DEFAULT_TIMECODESCALE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MIN_CLUSTER_DURATION,
      g_param_spec_int64 ("min-cluster-duration", "Minimum cluster duration",
          "Desired cluster duration as nanoseconds. A new cluster will be "
          "created irrespective of this property if a force key unit event "
          "is received. 0 means create a new cluster for each video keyframe "
          "or for each audio buffer in audio only streams.", 0,
          G_MAXINT64, DEFAULT_MIN_CLUSTER_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MAX_CLUSTER_DURATION,
      g_param_spec_int64 ("max-cluster-duration", "Maximum cluster duration",
          "A new cluster will be created if its duration exceeds this value. "
          "0 means no maximum duration.", 0,
          G_MAXINT64, DEFAULT_MAX_CLUSTER_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_OFFSET_TO_ZERO,
      g_param_spec_boolean ("offset-to-zero", "Offset To Zero",
          "Offsets all streams so that the " "earliest stream starts at 0.",
          DEFAULT_OFFSET_TO_ZERO, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CREATION_TIME,
      g_param_spec_boxed ("creation-time", "Creation Time",
          "Date and time of creation. This will be used for the DateUTC field."
          " NULL means that the current time will be used.",
          G_TYPE_DATE_TIME, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstMatroskaMux:cluster-timestamp-offset:
   *
   * An offset to add to all clusters/blocks (in nanoseconds)
   *
   * Since: 1.20
   */
  g_object_class_install_property (gobject_class, PROP_CLUSTER_TIMESTAMP_OFFSET,
      g_param_spec_uint64 ("cluster-timestamp-offset",
          "Cluster timestamp offset",
          "An offset to add to all clusters/blocks (in nanoseconds)", 0,
          G_MAXUINT64, DEFAULT_CLUSTER_TIMESTAMP_OFFSET,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_release_pad);

  gstaggregator_class->aggregate =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_aggregate);
  gstaggregator_class->clip = GST_DEBUG_FUNCPTR (gst_matroska_mux_clip);
  gstaggregator_class->stop = GST_DEBUG_FUNCPTR (gst_matroska_mux_stop);
  gstaggregator_class->negotiate = NULL;
  gstaggregator_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_sink_event);
  gstaggregator_class->src_event =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_src_event);
  gstaggregator_class->get_next_time =
      GST_DEBUG_FUNCPTR (gst_matroska_mux_get_next_time);

  parent_class = g_type_class_peek_parent (klass);

  gst_type_mark_as_plugin_api (GST_TYPE_MATROSKA_MUX_PAD, 0);
}

/*
 * Start of pad option handler code
 */
#define DEFAULT_PAD_FRAME_DURATION TRUE

enum
{
  PROP_PAD_0,
  PROP_PAD_FRAME_DURATION
};

G_DEFINE_TYPE (GstMatroskaMuxPad, gst_matroska_mux_pad,
    GST_TYPE_AGGREGATOR_PAD);


static void
gst_matroska_mux_pad_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMatroskaMuxPad *pad = GST_MATROSKA_MUX_PAD (object);

  switch (prop_id) {
    case PROP_PAD_FRAME_DURATION:
      g_value_set_boolean (value, pad->frame_duration);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_matroska_mux_pad_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMatroskaMuxPad *pad = GST_MATROSKA_MUX_PAD (object);

  switch (prop_id) {
    case PROP_PAD_FRAME_DURATION:
      pad->frame_duration = g_value_get_boolean (value);
      pad->frame_duration_user = TRUE;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_matroska_mux_pad_finalize (GObject * object)
{
  GstMatroskaMuxPad *pad = GST_MATROSKA_MUX_PAD (object);

  gst_matroska_pad_reset (pad, TRUE);
  gst_clear_tag_list (&pad->tags);

  G_OBJECT_CLASS (gst_matroska_mux_pad_parent_class)->finalize (object);
}

static void
gst_matroska_mux_pad_class_init (GstMatroskaMuxPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gobject_class->set_property = gst_matroska_mux_pad_set_property;
  gobject_class->get_property = gst_matroska_mux_pad_get_property;
  gobject_class->finalize = gst_matroska_mux_pad_finalize;

  g_object_class_install_property (gobject_class, PROP_PAD_FRAME_DURATION,
      g_param_spec_boolean ("frame-duration", "Frame duration",
          "Default frame duration", DEFAULT_PAD_FRAME_DURATION,
          G_PARAM_READWRITE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_matroska_mux_pad_init (GstMatroskaMuxPad * pad)
{
  pad->frame_duration = DEFAULT_PAD_FRAME_DURATION;
  pad->frame_duration_user = FALSE;
}

/*
 * End of pad option handler code
 **/

static void
gst_matroska_mux_init (GstMatroskaMux * mux, gpointer g_class)
{
  mux->ebml_write = gst_ebml_write_new (GST_AGGREGATOR (mux));
  mux->doctype = GST_MATROSKA_DOCTYPE_MATROSKA;

  /* property defaults */
  mux->doctype_version = DEFAULT_DOCTYPE_VERSION;
  mux->writing_app = g_strdup (DEFAULT_WRITING_APP);
  mux->min_index_interval = DEFAULT_MIN_INDEX_INTERVAL;
  mux->ebml_write->streamable = DEFAULT_STREAMABLE;
  mux->time_scale = DEFAULT_TIMECODESCALE;
  mux->min_cluster_duration = DEFAULT_MIN_CLUSTER_DURATION;
  mux->max_cluster_duration = DEFAULT_MAX_CLUSTER_DURATION;
  mux->cluster_timestamp_offset = DEFAULT_CLUSTER_TIMESTAMP_OFFSET;

  /* initialize internal variables */
  mux->index = NULL;
  mux->num_streams = 0;
  mux->num_a_streams = 0;
  mux->num_t_streams = 0;
  mux->num_v_streams = 0;
  mux->internal_toc = NULL;

  /* initialize remaining variables */
  gst_matroska_mux_stop (GST_AGGREGATOR (mux));
}


static void
gst_matroska_mux_finalize (GObject * object)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (object);

  gst_event_replace (&mux->force_key_unit_event, NULL);

  gst_object_unref (mux->ebml_write);
  g_free (mux->writing_app);
  g_clear_pointer (&mux->creation_time, g_date_time_unref);

  if (mux->internal_toc) {
    gst_toc_unref (mux->internal_toc);
    mux->internal_toc = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


static guint64
gst_matroska_mux_create_uid (void)
{
  return (((guint64) g_random_int ()) << 32) | g_random_int ();
}


static void
gst_matroska_pad_reset (GstMatroskaMuxPad * pad, gboolean full)
{
  gchar *name = NULL;
  GstMatroskaTrackType type = 0;

  /* free track information */
  if (pad->track != NULL) {
    /* retrieve for optional later use */
    name = pad->track->name;
    type = pad->track->type;
    /* extra for video */
    if (type == GST_MATROSKA_TRACK_TYPE_VIDEO) {
      GstMatroskaTrackVideoContext *ctx =
          (GstMatroskaTrackVideoContext *) pad->track;

      if (ctx->dirac_unit) {
        gst_buffer_unref (ctx->dirac_unit);
        ctx->dirac_unit = NULL;
      }
    }
    g_free (pad->track->codec_id);
    g_free (pad->track->codec_name);
    if (full)
      g_free (pad->track->name);
    g_free (pad->track->language);
    g_free (pad->track->codec_priv);
    g_free (pad->track);
    pad->track = NULL;
    if (pad->tags) {
      gst_tag_list_unref (pad->tags);
      pad->tags = NULL;
    }
  }

  if (!full && type != 0) {
    GstMatroskaTrackContext *context;

    /* create a fresh context */
    switch (type) {
      case GST_MATROSKA_TRACK_TYPE_VIDEO:
        context = (GstMatroskaTrackContext *)
            g_new0 (GstMatroskaTrackVideoContext, 1);
        break;
      case GST_MATROSKA_TRACK_TYPE_AUDIO:
        context = (GstMatroskaTrackContext *)
            g_new0 (GstMatroskaTrackAudioContext, 1);
        break;
      case GST_MATROSKA_TRACK_TYPE_SUBTITLE:
        context = (GstMatroskaTrackContext *)
            g_new0 (GstMatroskaTrackSubtitleContext, 1);
        break;
      default:
        g_assert_not_reached ();
        return;
    }

    context->type = type;
    context->name = name;
    context->uid = gst_matroska_mux_create_uid ();
    /* TODO: check default values for the context */
    context->flags = GST_MATROSKA_TRACK_ENABLED | GST_MATROSKA_TRACK_DEFAULT;
    pad->track = context;
    pad->start_ts = GST_CLOCK_TIME_NONE;
    pad->end_ts = GST_CLOCK_TIME_NONE;
    pad->tags = gst_tag_list_new_empty ();
    gst_tag_list_set_scope (pad->tags, GST_TAG_SCOPE_STREAM);
  }
}

static gboolean
gst_matroska_mux_stop (GstAggregator * agg)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (agg);
  GList *walk;

  /* reset EBML write */
  gst_ebml_write_reset (mux->ebml_write);

  /* reset input */
  mux->state = GST_MATROSKA_MUX_STATE_START;

  /* clean up existing streams */

  GST_OBJECT_LOCK (mux);
  for (walk = GST_ELEMENT (mux)->sinkpads; walk; walk = g_list_next (walk)) {
    GstMatroskaMuxPad *pad = (GstMatroskaMuxPad *) walk->data;

    /* reset pad to pristine state */
    gst_matroska_pad_reset (pad, FALSE);
  }
  GST_OBJECT_UNLOCK (mux);

  /* reset indexes */
  mux->num_indexes = 0;
  g_free (mux->index);
  mux->index = NULL;

  /* reset timers */
  mux->duration = 0;
  mux->last_pos = 0;

  /* reset cluster */
  mux->cluster = 0;
  mux->cluster_time = 0;
  mux->cluster_pos = 0;
  mux->prev_cluster_size = 0;

  /* reset tags */
  gst_tag_setter_reset_tags (GST_TAG_SETTER (mux));

  mux->tags_pos = 0;

  /* reset chapters */
  gst_toc_setter_reset (GST_TOC_SETTER (mux));
  if (mux->internal_toc) {
    gst_toc_unref (mux->internal_toc);
    mux->internal_toc = NULL;
  }

  mux->chapters_pos = 0;

  return TRUE;
}

static gboolean
gst_matroska_mux_src_event (GstAggregator * agg, GstEvent * event)
{
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
      /* disable seeking for now */
      gst_event_unref (event);
      return FALSE;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_event (agg, event);
}

static GstClockTime
gst_matroska_mux_get_next_time (GstAggregator * agg)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (agg);
  GstSegment segment;
  GstClockTime next_time;

  gst_segment_init (&segment, GST_FORMAT_TIME);
  next_time =
      gst_segment_to_running_time (&segment, GST_FORMAT_TIME, mux->last_pos);

  return next_time;
}

static void
gst_matroska_mux_free_codec_priv (GstMatroskaTrackContext * context)
{
  if (context->codec_priv != NULL) {
    g_free (context->codec_priv);
    context->codec_priv = NULL;
    context->codec_priv_size = 0;
  }
}

static void
gst_matroska_mux_build_vobsub_private (GstMatroskaTrackContext * context,
    const guint * clut)
{
  gchar *clutv[17];
  gchar *sclut;
  gint i;
  guint32 col;
  gdouble y, u, v;
  guint8 r, g, b;

  /* produce comma-separated list in hex format */
  for (i = 0; i < 16; ++i) {
    col = clut[i];
    /* replicate vobsub's slightly off RGB conversion calculation */
    y = (((col >> 16) & 0xff) - 16) * 255 / 219;
    u = ((col >> 8) & 0xff) - 128;
    v = (col & 0xff) - 128;
    r = CLAMP (1.0 * y + 1.4022 * u, 0, 255);
    g = CLAMP (1.0 * y - 0.3456 * u - 0.7145 * v, 0, 255);
    b = CLAMP (1.0 * y + 1.7710 * v, 0, 255);
    clutv[i] = g_strdup_printf ("%02x%02x%02x", r, g, b);
  }
  clutv[i] = NULL;
  sclut = g_strjoinv (",", clutv);

  /* build codec private; only palette for now */
  gst_matroska_mux_free_codec_priv (context);
  context->codec_priv = (guint8 *) g_strdup_printf ("palette: %s", sclut);
  /* include terminating 0 */
  context->codec_priv_size = strlen ((gchar *) context->codec_priv) + 1;
  g_free (sclut);
  for (i = 0; i < 16; ++i) {
    g_free (clutv[i]);
  }
}


static gboolean
gst_matroska_mux_sink_event (GstAggregator * agg, GstAggregatorPad * agg_pad,
    GstEvent * event)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (agg);
  GstMatroskaMuxPad *mux_pad = GST_MATROSKA_MUX_PAD (agg_pad);
  GstMatroskaTrackContext *context;
  gboolean ret = TRUE;

  context = mux_pad->track;
  g_assert (context);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:{
      GstCaps *caps;

      gst_event_parse_caps (event, &caps);

      ret = mux_pad->capsfunc (mux, mux_pad, caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_TAG:{
      GstTagList *list;
      gchar *lang = NULL;

      GST_DEBUG_OBJECT (mux, "received tag event");
      gst_event_parse_tag (event, &list);

      /* Matroska wants ISO 639-2B code, taglist most likely contains 639-1 */
      if (gst_tag_list_get_string (list, GST_TAG_LANGUAGE_CODE, &lang)) {
        const gchar *lang_code;

        lang_code = gst_tag_get_language_code_iso_639_2B (lang);
        if (lang_code) {
          GST_INFO_OBJECT (mux_pad, "Setting language to '%s'", lang_code);
          g_free (context->language);
          context->language = g_strdup (lang_code);
        } else {
          GST_WARNING_OBJECT (mux_pad, "Did not get language code for '%s'",
              lang);
        }
        g_free (lang);
      }

      if (gst_tag_list_get_scope (list) == GST_TAG_SCOPE_GLOBAL) {
        gst_tag_setter_merge_tags (GST_TAG_SETTER (mux), list,
            gst_tag_setter_get_tag_merge_mode (GST_TAG_SETTER (mux)));
      } else {
        gchar *title = NULL;

        /* Stream specific tags */
        gst_tag_list_insert (mux_pad->tags, list, GST_TAG_MERGE_REPLACE);

        /* If the tags contain a title, update the context name to write it there */
        if (gst_tag_list_get_string (list, GST_TAG_TITLE, &title)) {
          GST_INFO_OBJECT (mux_pad, "Setting track name to '%s'", title);
          g_free (context->name);
          context->name = g_strdup (title);
        }
        g_free (title);
      }

      gst_event_unref (event);
      /* handled this, don't want to forward it downstream */
      event = NULL;
      ret = TRUE;
      break;
    }
    case GST_EVENT_TOC:{
      GstToc *toc, *old_toc;

      if (mux->chapters_pos > 0)
        break;

      GST_DEBUG_OBJECT (mux, "received toc event");
      gst_event_parse_toc (event, &toc, NULL);

      if (toc != NULL) {
        old_toc = gst_toc_setter_get_toc (GST_TOC_SETTER (mux));
        if (old_toc != NULL) {
          if (old_toc != toc)
            GST_INFO_OBJECT (mux_pad, "Replacing TOC with a new one");
          gst_toc_unref (old_toc);
        }

        gst_toc_setter_set_toc (GST_TOC_SETTER (mux), toc);
        gst_toc_unref (toc);
      }

      gst_event_unref (event);
      /* handled this, don't want to forward it downstream */
      event = NULL;
      break;
    }
    case GST_EVENT_CUSTOM_DOWNSTREAM:
    case GST_EVENT_CUSTOM_DOWNSTREAM_STICKY:{
      const GstStructure *structure;

      structure = gst_event_get_structure (event);
      if (gst_structure_has_name (structure, "GstForceKeyUnit")) {
        gst_event_replace (&mux->force_key_unit_event, NULL);
        mux->force_key_unit_event = event;
        event = NULL;
      } else if (gst_structure_has_name (structure, "application/x-gst-dvd") &&
          !strcmp ("dvd-spu-clut-change",
              gst_structure_get_string (structure, "event"))) {
        gchar name[16];
        gint i, value;
        guint clut[16];

        GST_DEBUG_OBJECT (mux_pad, "New DVD colour table received");
        if (context->type != GST_MATROSKA_TRACK_TYPE_SUBTITLE) {
          GST_DEBUG_OBJECT (mux_pad, "... discarding");
          break;
        }
        /* first transform event data into table form */
        for (i = 0; i < 16; i++) {
          g_snprintf (name, sizeof (name), "clut%02d", i);
          if (!gst_structure_get_int (structure, name, &value)) {
            GST_ERROR_OBJECT (mux, "dvd-spu-clut-change event did not "
                "contain %s field", name);
            goto break_hard;
          }
          clut[i] = value;
        }

        /* transform into private data for stream; text form */
        gst_matroska_mux_build_vobsub_private (context, clut);
      }
    }
      /* fall through */
    default:
      break;
  }

break_hard:
  if (event != NULL)
    return GST_AGGREGATOR_CLASS (parent_class)->sink_event (agg, agg_pad,
        event);

  return ret;
}

static void
gst_matroska_mux_set_codec_id (GstMatroskaTrackContext * context,
    const char *id)
{
  g_assert (context && id);
  g_free (context->codec_id);
  context->codec_id = g_strdup (id);
}

static gboolean
check_field (const GstIdStr * fieldname, const GValue * value,
    gpointer user_data)
{
  GstStructure *structure = (GstStructure *) user_data;
  const gchar *name = gst_structure_get_name (structure);

  if ((g_strcmp0 (name, "video/x-h264") == 0 &&
          !g_strcmp0 (gst_structure_get_string (structure, "stream-format"),
              "avc3")) || (g_strcmp0 (name, "video/x-h265") == 0
          && !g_strcmp0 (gst_structure_get_string (structure, "stream-format"),
              "hev1"))
      ) {
    /* While in theory, matroska only supports avc1 / hvc1, and doesn't support codec_data
     * changes, in practice most decoders will use in-band SPS / PPS (avc3 / hev1), if the
     * input stream is avc3 / hev1 we let the new codec_data slide to support "smart" encoding.
     *
     * We don't warn here as we already warned elsewhere.
     */
    if (gst_id_str_is_equal_to_str (fieldname, "codec_data")) {
      return FALSE;
    } else if (gst_id_str_is_equal_to_str (fieldname, "tier")) {
      return FALSE;
    } else if (gst_id_str_is_equal_to_str (fieldname, "profile")) {
      return FALSE;
    } else if (gst_id_str_is_equal_to_str (fieldname, "level")) {
      return FALSE;
    } else if (gst_id_str_is_equal_to_str (fieldname, "width")) {
      return FALSE;
    } else if (gst_id_str_is_equal_to_str (fieldname, "height")) {
      return FALSE;
    }
  } else if (gst_structure_has_name (structure, "video/x-vp8")
      || gst_structure_has_name (structure, "video/x-vp9")) {
    /* We do not use profile and streamheader for VPX so let it change
     * mid stream */
    if (gst_id_str_is_equal_to_str (fieldname, "streamheader"))
      return FALSE;
    else if (gst_id_str_is_equal_to_str (fieldname, "profile"))
      return FALSE;
    else if (gst_id_str_is_equal_to_str (fieldname, "width"))
      return FALSE;
    else if (gst_id_str_is_equal_to_str (fieldname, "height"))
      return FALSE;
  }

  /* This fields aren't used and are not retained into the bitstream so we can
   * discard them. */
  if (g_str_has_prefix (gst_structure_get_name (structure), "video/")) {
    if (gst_id_str_is_equal_to_str (fieldname, "chroma-site"))
      return FALSE;
    else if (gst_id_str_is_equal_to_str (fieldname, "chroma-format"))
      return FALSE;
    else if (gst_id_str_is_equal_to_str (fieldname, "bit-depth-luma"))
      return FALSE;

    /* Remove pixel-aspect-ratio field if it contains 1/1 as that's considered
     * equivalent to not having the field but are not considered equivalent
     * by the generic caps functions
     */
    if (gst_id_str_is_equal_to_str (fieldname, "pixel-aspect-ratio")) {
      gint par_n = gst_value_get_fraction_numerator (value);
      gint par_d = gst_value_get_fraction_denominator (value);

      if (par_n == 1 && par_d == 1)
        return FALSE;
    }

    /* Remove multiview-mode=mono and multiview-flags=0 fields as those are
     * equivalent with not having the fields but are not considered equivalent
     * by the generic caps functions.
     */
    if (gst_id_str_is_equal_to_str (fieldname, "multiview-mode")) {
      const gchar *s = g_value_get_string (value);

      if (g_strcmp0 (s, "mono") == 0)
        return FALSE;
    }

    if (gst_id_str_is_equal_to_str (fieldname, "multiview-flags")) {
      guint multiview_flags = gst_value_get_flagset_flags (value);

      if (multiview_flags == 0)
        return FALSE;
    }
  }

  return TRUE;
}

static gboolean
check_new_caps (GstMatroskaTrackVideoContext * videocontext, GstCaps * old_caps,
    GstCaps * new_caps)
{
  GstStructure *old_s, *new_s;
  gboolean ret;

  old_caps = gst_caps_copy (old_caps);
  new_caps = gst_caps_copy (new_caps);

  new_s = gst_caps_get_structure (new_caps, 0);
  old_s = gst_caps_get_structure (old_caps, 0);

  gst_structure_filter_and_map_in_place_id_str (new_s,
      (GstStructureFilterMapIdStrFunc) check_field, new_s);
  gst_structure_filter_and_map_in_place_id_str (old_s,
      (GstStructureFilterMapIdStrFunc) check_field, old_s);

  ret = gst_caps_is_subset (new_caps, old_caps);

  gst_caps_unref (new_caps);
  gst_caps_unref (old_caps);

  return ret;
}

static gboolean
gst_matroska_mux_video_pad_setcaps (GstMatroskaMux * mux,
    GstMatroskaMuxPad * mux_pad, GstCaps * caps)
{
  GstMatroskaTrackContext *context = NULL;
  GstMatroskaTrackVideoContext *videocontext;
  GstStructure *structure;
  const gchar *mimetype;
  const gchar *interlace_mode, *s;
  const GValue *value = NULL;
  GstBuffer *codec_buf = NULL;
  gint width, height, pixel_width, pixel_height;
  gint fps_d, fps_n;
  guint multiview_flags;
  GstCaps *old_caps;

  /* find context */
  context = mux_pad->track;
  g_assert (context);
  g_assert (context->type == GST_MATROSKA_TRACK_TYPE_VIDEO);
  videocontext = (GstMatroskaTrackVideoContext *) context;

  if ((old_caps = gst_pad_get_current_caps (GST_PAD (mux_pad)))) {
    if (mux->state >= GST_MATROSKA_MUX_STATE_HEADER
        && !check_new_caps (videocontext, old_caps, caps)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("Caps changes are not supported by Matroska\nCurrent: `%"
              GST_PTR_FORMAT "`\nNew: `%" GST_PTR_FORMAT "`", old_caps, caps));
      gst_caps_unref (old_caps);
      goto refuse_caps;
    }
    gst_caps_unref (old_caps);
  } else if (mux->state >= GST_MATROSKA_MUX_STATE_HEADER) {
    GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
        ("Caps on pad %" GST_PTR_FORMAT
            " arrived late. Headers were already written", mux_pad));
    goto refuse_caps;
  }

  /* gst -> matroska ID'ing */
  structure = gst_caps_get_structure (caps, 0);

  mimetype = gst_structure_get_name (structure);

  interlace_mode = gst_structure_get_string (structure, "interlace-mode");
  if (interlace_mode != NULL) {
    if (strcmp (interlace_mode, "progressive") == 0)
      videocontext->interlace_mode = GST_MATROSKA_INTERLACE_MODE_PROGRESSIVE;
    else
      videocontext->interlace_mode = GST_MATROSKA_INTERLACE_MODE_INTERLACED;
  } else {
    videocontext->interlace_mode = GST_MATROSKA_INTERLACE_MODE_UNKNOWN;
  }

  if (!strcmp (mimetype, "video/x-theora")) {
    /* we'll extract the details later from the theora identification header */
    goto skip_details;
  }

  /* get general properties */
  /* spec says it is mandatory */
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height))
    goto refuse_caps;

  videocontext->pixel_width = width;
  videocontext->pixel_height = height;

  if (mux_pad->frame_duration
      && gst_structure_get_fraction (structure, "framerate", &fps_n, &fps_d)
      && fps_n > 0) {
    context->default_duration =
        gst_util_uint64_scale_int (GST_SECOND, fps_d, fps_n);
    GST_LOG_OBJECT (mux_pad, "default duration = %" GST_TIME_FORMAT,
        GST_TIME_ARGS (context->default_duration));
  } else {
    context->default_duration = 0;
  }
  if (gst_structure_get_fraction (structure, "pixel-aspect-ratio",
          &pixel_width, &pixel_height)) {
    if (pixel_width > pixel_height) {
      videocontext->display_width = width * pixel_width / pixel_height;
      videocontext->display_height = height;
    } else if (pixel_width < pixel_height) {
      videocontext->display_width = width;
      videocontext->display_height = height * pixel_height / pixel_width;
    } else {
      videocontext->display_width = 0;
      videocontext->display_height = 0;
    }
  } else {
    videocontext->display_width = 0;
    videocontext->display_height = 0;
  }

  if ((s = gst_structure_get_string (structure, "colorimetry"))) {
    if (!gst_video_colorimetry_from_string (&videocontext->colorimetry, s)) {
      GST_WARNING_OBJECT (mux_pad, "Could not parse colorimetry %s", s);
    }
  }

  if ((s = gst_structure_get_string (structure, "mastering-display-info"))) {
    if (!gst_video_mastering_display_info_from_string
        (&videocontext->mastering_display_info, s)) {
      GST_WARNING_OBJECT (mux_pad,
          "Could not parse mastering-display-metadata %s", s);
    } else {
      videocontext->mastering_display_info_present = TRUE;
    }
  }

  if ((s = gst_structure_get_string (structure, "content-light-level"))) {
    if (!gst_video_content_light_level_from_string
        (&videocontext->content_light_level, s))
      GST_WARNING_OBJECT (mux_pad, "Could not parse content-light-level %s", s);
  }

  /* Collect stereoscopic info, if any */
  if ((s = gst_structure_get_string (structure, "multiview-mode")))
    videocontext->multiview_mode =
        gst_video_multiview_mode_from_caps_string (s);
  gst_structure_get_flagset (structure, "multiview-flags", &multiview_flags,
      NULL);
  videocontext->multiview_flags = multiview_flags;


skip_details:

  videocontext->asr_mode = GST_MATROSKA_ASPECT_RATIO_MODE_FREE;
  videocontext->fourcc = 0;

  /* TODO: - check if we handle all codecs by the spec, i.e. codec private
   *         data and other settings
   *       - add new formats
   */

  /* extract codec_data, may turn out needed */
  value = gst_structure_get_value (structure, "codec_data");
  if (value)
    codec_buf = (GstBuffer *) gst_value_get_buffer (value);

  /* find type */
  if (!strcmp (mimetype, "video/x-raw")) {
    const gchar *fstr;
    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_VIDEO_UNCOMPRESSED);
    fstr = gst_structure_get_string (structure, "format");
    if (fstr) {
      if (strlen (fstr) == 4)
        videocontext->fourcc = GST_STR_FOURCC (fstr);
      else if (!strcmp (fstr, "GRAY8"))
        videocontext->fourcc = GST_MAKE_FOURCC ('Y', '8', '0', '0');
      else if (!strcmp (fstr, "GRAY10_LE32"))
        videocontext->fourcc = GST_MAKE_FOURCC ('Y', '1', 0, 10);
      else if (!strcmp (fstr, "GRAY16_LE"))
        videocontext->fourcc = GST_MAKE_FOURCC ('Y', '1', 0, 16);
      else if (!strcmp (fstr, "BGR"))
        videocontext->fourcc = GST_MAKE_FOURCC ('B', 'G', 'R', 24);
      else if (!strcmp (fstr, "RGB"))
        videocontext->fourcc = GST_MAKE_FOURCC ('R', 'G', 'B', 24);
      else if (!strcmp (fstr, "RGBA64_LE"))
        videocontext->fourcc = GST_MAKE_FOURCC ('R', 'B', 'A', 64);
      else if (!strcmp (fstr, "BGRA64_LE"))
        videocontext->fourcc = GST_MAKE_FOURCC ('B', 'R', 'A', 64);
    }
  } else if (!strcmp (mimetype, "video/x-huffyuv")      /* MS/VfW compatibility cases */
      ||!strcmp (mimetype, "video/x-divx")
      || !strcmp (mimetype, "video/x-dv")
      || !strcmp (mimetype, "video/x-h263")
      || !strcmp (mimetype, "video/x-msmpeg")
      || !strcmp (mimetype, "video/x-wmv")
      || !strcmp (mimetype, "image/jpeg")) {
    gst_riff_strf_vids *bih;
    gint size = sizeof (gst_riff_strf_vids);
    guint32 fourcc = 0;

    if (!strcmp (mimetype, "video/x-huffyuv"))
      fourcc = GST_MAKE_FOURCC ('H', 'F', 'Y', 'U');
    else if (!strcmp (mimetype, "video/x-dv"))
      fourcc = GST_MAKE_FOURCC ('D', 'V', 'S', 'D');
    else if (!strcmp (mimetype, "video/x-h263"))
      fourcc = GST_MAKE_FOURCC ('H', '2', '6', '3');
    else if (!strcmp (mimetype, "video/x-divx")) {
      gint divxversion;

      gst_structure_get_int (structure, "divxversion", &divxversion);
      switch (divxversion) {
        case 3:
          fourcc = GST_MAKE_FOURCC ('D', 'I', 'V', '3');
          break;
        case 4:
          fourcc = GST_MAKE_FOURCC ('D', 'I', 'V', 'X');
          break;
        case 5:
          fourcc = GST_MAKE_FOURCC ('D', 'X', '5', '0');
          break;
      }
    } else if (!strcmp (mimetype, "video/x-msmpeg")) {
      gint msmpegversion;

      gst_structure_get_int (structure, "msmpegversion", &msmpegversion);
      switch (msmpegversion) {
        case 41:
          fourcc = GST_MAKE_FOURCC ('M', 'P', 'G', '4');
          break;
        case 42:
          fourcc = GST_MAKE_FOURCC ('M', 'P', '4', '2');
          break;
        case 43:
          goto msmpeg43;
          break;
      }
    } else if (!strcmp (mimetype, "video/x-wmv")) {
      gint wmvversion;
      const gchar *fstr;

      fstr = gst_structure_get_string (structure, "format");
      if (fstr && strlen (fstr) == 4) {
        fourcc = GST_STR_FOURCC (fstr);
      } else if (gst_structure_get_int (structure, "wmvversion", &wmvversion)) {
        if (wmvversion == 2) {
          fourcc = GST_MAKE_FOURCC ('W', 'M', 'V', '2');
        } else if (wmvversion == 1) {
          fourcc = GST_MAKE_FOURCC ('W', 'M', 'V', '1');
        } else if (wmvversion == 3) {
          fourcc = GST_MAKE_FOURCC ('W', 'M', 'V', '3');
        }
      }
    } else if (!strcmp (mimetype, "image/jpeg")) {
      fourcc = GST_MAKE_FOURCC ('M', 'J', 'P', 'G');
    }

    if (!fourcc)
      goto refuse_caps;

    bih = g_new0 (gst_riff_strf_vids, 1);
    GST_WRITE_UINT32_LE (&bih->size, size);
    GST_WRITE_UINT32_LE (&bih->width, videocontext->pixel_width);
    GST_WRITE_UINT32_LE (&bih->height, videocontext->pixel_height);
    GST_WRITE_UINT32_LE (&bih->compression, fourcc);
    GST_WRITE_UINT16_LE (&bih->planes, (guint16) 1);
    GST_WRITE_UINT16_LE (&bih->bit_cnt, (guint16) 24);
    GST_WRITE_UINT32_LE (&bih->image_size, videocontext->pixel_width *
        videocontext->pixel_height * 3);

    /* process codec private/initialization data, if any */
    if (codec_buf) {
      size += gst_buffer_get_size (codec_buf);
      bih = g_realloc (bih, size);
      GST_WRITE_UINT32_LE (&bih->size, size);
      gst_buffer_extract (codec_buf, 0,
          (guint8 *) bih + sizeof (gst_riff_strf_vids), -1);
    }

    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_VIDEO_VFW_FOURCC);
    gst_matroska_mux_free_codec_priv (context);
    context->codec_priv = (gpointer) bih;
    context->codec_priv_size = size;
    context->dts_only = TRUE;
  } else if (!strcmp (mimetype, "video/x-h264")) {
    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_AVC);
    gst_matroska_mux_free_codec_priv (context);

    if (!g_strcmp0 (gst_structure_get_string (structure, "stream-format"),
            "avc3")) {
      GST_WARNING_OBJECT (mux,
          "avc3 is not officially supported, only use this format for smart encoding");
    }

    /* Create avcC header */
    if (codec_buf != NULL) {
      context->codec_priv_size = gst_buffer_get_size (codec_buf);
      context->codec_priv = g_malloc0 (context->codec_priv_size);
      gst_buffer_extract (codec_buf, 0, context->codec_priv, -1);
    }
  } else if (!strcmp (mimetype, "video/x-h265")) {
    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_VIDEO_MPEGH_HEVC);
    gst_matroska_mux_free_codec_priv (context);

    if (!g_strcmp0 (gst_structure_get_string (structure, "stream-format"),
            "hev1")) {
      GST_WARNING_OBJECT (mux,
          "hev1 is not officially supported, only use this format for smart encoding");
    }

    /* Create hvcC header */
    if (codec_buf != NULL) {
      context->codec_priv_size = gst_buffer_get_size (codec_buf);
      context->codec_priv = g_malloc0 (context->codec_priv_size);
      gst_buffer_extract (codec_buf, 0, context->codec_priv, -1);
    }
  } else if (!strcmp (mimetype, "video/x-theora")) {
    const GValue *streamheader;

    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_VIDEO_THEORA);

    gst_matroska_mux_free_codec_priv (context);

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (!theora_streamheader_to_codecdata (streamheader, context)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("theora stream headers missing or malformed"));
      goto refuse_caps;
    }
  } else if (!strcmp (mimetype, "video/x-dirac")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_VIDEO_DIRAC);
  } else if (!strcmp (mimetype, "video/x-vp8")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_VIDEO_VP8);
  } else if (!strcmp (mimetype, "video/x-vp9")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_VIDEO_VP9);
  } else if (!strcmp (mimetype, "video/x-av1")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_VIDEO_AV1);
    gst_matroska_mux_free_codec_priv (context);
    /* Create av1C header */
    if (codec_buf != NULL)
      gst_buffer_extract_dup (codec_buf, 0, gst_buffer_get_size (codec_buf),
          &context->codec_priv, &context->codec_priv_size);
  } else if (!strcmp (mimetype, "video/x-ffv")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_VIDEO_FFV1);
    gst_matroska_mux_free_codec_priv (context);
    if (codec_buf != NULL)
      gst_buffer_extract_dup (codec_buf, 0, gst_buffer_get_size (codec_buf),
          &context->codec_priv, &context->codec_priv_size);
  } else if (!strcmp (mimetype, "video/mpeg")) {
    gint mpegversion;

    gst_structure_get_int (structure, "mpegversion", &mpegversion);
    switch (mpegversion) {
      case 1:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_VIDEO_MPEG1);
        break;
      case 2:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_VIDEO_MPEG2);
        break;
      case 4:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_VIDEO_MPEG4_ASP);
        break;
      default:
        goto refuse_caps;
    }

    /* global headers may be in codec data */
    if (codec_buf != NULL) {
      gst_matroska_mux_free_codec_priv (context);
      context->codec_priv_size = gst_buffer_get_size (codec_buf);
      context->codec_priv = g_malloc0 (context->codec_priv_size);
      gst_buffer_extract (codec_buf, 0, context->codec_priv, -1);
    }
  } else if (!strcmp (mimetype, "video/x-msmpeg")) {
  msmpeg43:
    /* can only make it here if preceding case verified it was version 3 */
    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_VIDEO_MSMPEG4V3);
  } else if (!strcmp (mimetype, "video/x-pn-realvideo")) {
    gint rmversion;
    const GValue *mdpr_data;

    gst_structure_get_int (structure, "rmversion", &rmversion);
    switch (rmversion) {
      case 1:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO1);
        break;
      case 2:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO2);
        break;
      case 3:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO3);
        break;
      case 4:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_VIDEO_REALVIDEO4);
        break;
      default:
        goto refuse_caps;
    }

    mdpr_data = gst_structure_get_value (structure, "mdpr_data");
    if (mdpr_data != NULL) {
      guint8 *priv_data = NULL;
      guint priv_data_size = 0;

      GstBuffer *codec_data_buf = g_value_peek_pointer (mdpr_data);

      priv_data_size = gst_buffer_get_size (codec_data_buf);
      priv_data = g_malloc0 (priv_data_size);

      gst_buffer_extract (codec_data_buf, 0, priv_data, -1);

      gst_matroska_mux_free_codec_priv (context);
      context->codec_priv = priv_data;
      context->codec_priv_size = priv_data_size;
    }
  } else if (strcmp (mimetype, "video/x-prores") == 0) {
    const gchar *variant;

    gst_matroska_mux_free_codec_priv (context);

    variant = gst_structure_get_string (structure, "format");
    if (!variant || !g_strcmp0 (variant, "standard"))
      context->codec_priv = g_strdup ("apcn");
    else if (!g_strcmp0 (variant, "hq"))
      context->codec_priv = g_strdup ("apch");
    else if (!g_strcmp0 (variant, "lt"))
      context->codec_priv = g_strdup ("apcs");
    else if (!g_strcmp0 (variant, "proxy"))
      context->codec_priv = g_strdup ("apco");
    else if (!g_strcmp0 (variant, "4444"))
      context->codec_priv = g_strdup ("ap4h");
    else {
      GST_WARNING_OBJECT (mux, "Unhandled prores format: %s", variant);

      goto refuse_caps;
    }

    context->codec_priv_size = sizeof (guint32);
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_VIDEO_PRORES);
  }

  return TRUE;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (mux, "pad %s refused caps %" GST_PTR_FORMAT,
        GST_PAD_NAME (mux_pad), caps);
    return FALSE;
  }
}

/* N > 0 to expect a particular number of headers, negative if the
   number of headers is variable */
static gboolean
xiphN_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context, GstBuffer ** p_buf0, int N)
{
  GstBuffer **buf = NULL;
  GArray *bufarr;
  guint8 *priv_data;
  guint bufi, i, offset, priv_data_size;

  if (streamheader == NULL)
    goto no_stream_headers;

  if (G_VALUE_TYPE (streamheader) != GST_TYPE_ARRAY)
    goto wrong_type;

  bufarr = g_value_peek_pointer (streamheader);
  if (bufarr->len <= 0 || bufarr->len > 255)    /* at least one header, and count stored in a byte */
    goto wrong_count;
  if (N > 0 && bufarr->len != N)
    goto wrong_count;

  context->xiph_headers_to_skip = bufarr->len;

  buf = (GstBuffer **) g_malloc0 (sizeof (GstBuffer *) * bufarr->len);
  for (i = 0; i < bufarr->len; i++) {
    GValue *bufval = &g_array_index (bufarr, GValue, i);

    if (G_VALUE_TYPE (bufval) != GST_TYPE_BUFFER) {
      g_free (buf);
      goto wrong_content_type;
    }

    buf[i] = g_value_peek_pointer (bufval);
  }

  priv_data_size = 1;
  if (bufarr->len > 0) {
    for (i = 0; i < bufarr->len - 1; i++) {
      priv_data_size += gst_buffer_get_size (buf[i]) / 0xff + 1;
    }
  }

  for (i = 0; i < bufarr->len; ++i) {
    priv_data_size += gst_buffer_get_size (buf[i]);
  }

  priv_data = g_malloc0 (priv_data_size);

  priv_data[0] = bufarr->len - 1;
  offset = 1;

  if (bufarr->len > 0) {
    for (bufi = 0; bufi < bufarr->len - 1; bufi++) {
      for (i = 0; i < gst_buffer_get_size (buf[bufi]) / 0xff; ++i) {
        priv_data[offset++] = 0xff;
      }
      priv_data[offset++] = gst_buffer_get_size (buf[bufi]) % 0xff;
    }
  }

  for (i = 0; i < bufarr->len; ++i) {
    gst_buffer_extract (buf[i], 0, priv_data + offset, -1);
    offset += gst_buffer_get_size (buf[i]);
  }

  gst_matroska_mux_free_codec_priv (context);
  context->codec_priv = priv_data;
  context->codec_priv_size = priv_data_size;

  if (p_buf0)
    *p_buf0 = gst_buffer_ref (buf[0]);

  g_free (buf);

  return TRUE;

/* ERRORS */
no_stream_headers:
  {
    GST_WARNING ("required streamheaders missing in sink caps!");
    return FALSE;
  }
wrong_type:
  {
    GST_WARNING ("streamheaders are not a GST_TYPE_ARRAY, but a %s",
        G_VALUE_TYPE_NAME (streamheader));
    return FALSE;
  }
wrong_count:
  {
    GST_WARNING ("got %u streamheaders, not %d as expected", bufarr->len, N);
    return FALSE;
  }
wrong_content_type:
  {
    GST_WARNING ("streamheaders array does not contain GstBuffers");
    return FALSE;
  }
}

static gboolean
vorbis_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GstBuffer *buf0 = NULL;

  if (!xiphN_streamheader_to_codecdata (streamheader, context, &buf0, 3))
    return FALSE;

  if (buf0 == NULL || gst_buffer_get_size (buf0) < 1 + 6 + 4) {
    GST_WARNING ("First vorbis header too small, ignoring");
  } else {
    if (gst_buffer_memcmp (buf0, 1, "vorbis", 6) == 0) {
      GstMatroskaTrackAudioContext *audiocontext;
      GstMapInfo map;
      guint8 *hdr;

      gst_buffer_map (buf0, &map, GST_MAP_READ);
      hdr = map.data + 1 + 6 + 4;
      audiocontext = (GstMatroskaTrackAudioContext *) context;
      audiocontext->channels = GST_READ_UINT8 (hdr);
      audiocontext->samplerate = GST_READ_UINT32_LE (hdr + 1);
      gst_buffer_unmap (buf0, &map);
    }
  }

  if (buf0)
    gst_buffer_unref (buf0);

  return TRUE;
}

static gboolean
theora_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GstBuffer *buf0 = NULL;

  if (!xiphN_streamheader_to_codecdata (streamheader, context, &buf0, 3))
    return FALSE;

  if (buf0 == NULL || gst_buffer_get_size (buf0) < 1 + 6 + 26) {
    GST_WARNING ("First theora header too small, ignoring");
  } else if (gst_buffer_memcmp (buf0, 0, "\200theora\003\002", 9) != 0) {
    GST_WARNING ("First header not a theora identification header, ignoring");
  } else {
    GstMatroskaTrackVideoContext *videocontext;
    guint fps_num, fps_denom, par_num, par_denom;
    GstMapInfo map;
    guint8 *hdr;

    gst_buffer_map (buf0, &map, GST_MAP_READ);
    hdr = map.data + 1 + 6 + 3 + 2 + 2;

    videocontext = (GstMatroskaTrackVideoContext *) context;
    videocontext->pixel_width = GST_READ_UINT32_BE (hdr) >> 8;
    videocontext->pixel_height = GST_READ_UINT32_BE (hdr + 3) >> 8;
    hdr += 3 + 3 + 1 + 1;
    fps_num = GST_READ_UINT32_BE (hdr);
    fps_denom = GST_READ_UINT32_BE (hdr + 4);
    context->default_duration = gst_util_uint64_scale_int (GST_SECOND,
        fps_denom, fps_num);
    hdr += 4 + 4;
    par_num = GST_READ_UINT32_BE (hdr) >> 8;
    par_denom = GST_READ_UINT32_BE (hdr + 3) >> 8;
    if (par_num > 0 && par_denom > 0) {
      if (par_num > par_denom) {
        videocontext->display_width =
            videocontext->pixel_width * par_num / par_denom;
        videocontext->display_height = videocontext->pixel_height;
      } else if (par_num < par_denom) {
        videocontext->display_width = videocontext->pixel_width;
        videocontext->display_height =
            videocontext->pixel_height * par_denom / par_num;
      } else {
        videocontext->display_width = 0;
        videocontext->display_height = 0;
      }
    } else {
      videocontext->display_width = 0;
      videocontext->display_height = 0;
    }

    gst_buffer_unmap (buf0, &map);
  }

  if (buf0)
    gst_buffer_unref (buf0);

  return TRUE;
}

static gboolean
kate_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GstBuffer *buf0 = NULL;

  if (!xiphN_streamheader_to_codecdata (streamheader, context, &buf0, -1))
    return FALSE;

  if (buf0 == NULL || gst_buffer_get_size (buf0) < 64) {        /* Kate ID header is 64 bytes */
    GST_WARNING ("First kate header too small, ignoring");
  } else if (gst_buffer_memcmp (buf0, 0, "\200kate\0\0\0", 8) != 0) {
    GST_WARNING ("First header not a kate identification header, ignoring");
  }

  if (buf0)
    gst_buffer_unref (buf0);

  return TRUE;
}

static gboolean
flac_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GArray *bufarr;
  gint i;
  GValue *bufval;
  GstBuffer *buffer;

  if (streamheader == NULL || G_VALUE_TYPE (streamheader) != GST_TYPE_ARRAY) {
    GST_WARNING ("No or invalid streamheader field in the caps");
    return FALSE;
  }

  bufarr = g_value_peek_pointer (streamheader);
  if (bufarr->len < 2) {
    GST_WARNING ("Too few headers in streamheader field");
    return FALSE;
  }

  context->xiph_headers_to_skip = bufarr->len + 1;

  bufval = &g_array_index (bufarr, GValue, 0);
  if (G_VALUE_TYPE (bufval) != GST_TYPE_BUFFER) {
    GST_WARNING ("streamheaders array does not contain GstBuffers");
    return FALSE;
  }

  buffer = g_value_peek_pointer (bufval);

  /* Need at least OggFLAC mapping header, fLaC marker and STREAMINFO block */
  if (gst_buffer_get_size (buffer) < 9 + 4 + 4 + 34
      || gst_buffer_memcmp (buffer, 1, "FLAC", 4) != 0
      || gst_buffer_memcmp (buffer, 9, "fLaC", 4) != 0) {
    GST_WARNING ("Invalid streamheader for FLAC");
    return FALSE;
  }

  gst_matroska_mux_free_codec_priv (context);
  context->codec_priv_size = gst_buffer_get_size (buffer) - 9;
  context->codec_priv = g_malloc (context->codec_priv_size);
  gst_buffer_extract (buffer, 9, context->codec_priv, -1);

  for (i = 1; i < bufarr->len; i++) {
    guint old_size;
    bufval = &g_array_index (bufarr, GValue, i);

    if (G_VALUE_TYPE (bufval) != GST_TYPE_BUFFER) {
      gst_matroska_mux_free_codec_priv (context);
      GST_WARNING ("streamheaders array does not contain GstBuffers");
      return FALSE;
    }

    buffer = g_value_peek_pointer (bufval);

    old_size = context->codec_priv_size;
    context->codec_priv_size += gst_buffer_get_size (buffer);

    context->codec_priv = g_realloc (context->codec_priv,
        context->codec_priv_size);
    gst_buffer_extract (buffer, 0,
        (guint8 *) context->codec_priv + old_size, -1);
  }

  return TRUE;
}

static gboolean
speex_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GArray *bufarr;
  GValue *bufval;
  GstBuffer *buffer;
  guint old_size;

  if (streamheader == NULL || G_VALUE_TYPE (streamheader) != GST_TYPE_ARRAY) {
    GST_WARNING ("No or invalid streamheader field in the caps");
    return FALSE;
  }

  bufarr = g_value_peek_pointer (streamheader);
  if (bufarr->len != 2) {
    GST_WARNING ("Too few headers in streamheader field");
    return FALSE;
  }

  context->xiph_headers_to_skip = bufarr->len + 1;

  bufval = &g_array_index (bufarr, GValue, 0);
  if (G_VALUE_TYPE (bufval) != GST_TYPE_BUFFER) {
    GST_WARNING ("streamheaders array does not contain GstBuffers");
    return FALSE;
  }

  buffer = g_value_peek_pointer (bufval);

  if (gst_buffer_get_size (buffer) < 80
      || gst_buffer_memcmp (buffer, 0, "Speex   ", 8) != 0) {
    GST_WARNING ("Invalid streamheader for Speex");
    return FALSE;
  }

  gst_matroska_mux_free_codec_priv (context);
  context->codec_priv_size = gst_buffer_get_size (buffer);
  context->codec_priv = g_malloc (context->codec_priv_size);
  gst_buffer_extract (buffer, 0, context->codec_priv, -1);

  bufval = &g_array_index (bufarr, GValue, 1);

  if (G_VALUE_TYPE (bufval) != GST_TYPE_BUFFER) {
    gst_matroska_mux_free_codec_priv (context);
    GST_WARNING ("streamheaders array does not contain GstBuffers");
    return FALSE;
  }

  buffer = g_value_peek_pointer (bufval);

  old_size = context->codec_priv_size;
  context->codec_priv_size += gst_buffer_get_size (buffer);
  context->codec_priv = g_realloc (context->codec_priv,
      context->codec_priv_size);
  gst_buffer_extract (buffer, 0, (guint8 *) context->codec_priv + old_size, -1);

  return TRUE;
}

static gboolean
opus_streamheader_to_codecdata (const GValue * streamheader,
    GstMatroskaTrackContext * context)
{
  GArray *bufarr;
  GValue *bufval;
  GstBuffer *buf;

  if (G_VALUE_TYPE (streamheader) != GST_TYPE_ARRAY)
    goto wrong_type;

  bufarr = g_value_peek_pointer (streamheader);
  if (bufarr->len != 1 && bufarr->len != 2)     /* one header, and count stored in a byte */
    goto wrong_count;

  /* Opus headers are not in-band */
  context->xiph_headers_to_skip = 0;

  bufval = &g_array_index (bufarr, GValue, 0);
  if (G_VALUE_TYPE (bufval) != GST_TYPE_BUFFER) {
    goto wrong_content_type;
  }
  buf = g_value_peek_pointer (bufval);

  gst_matroska_mux_free_codec_priv (context);

  context->codec_priv_size = gst_buffer_get_size (buf);
  context->codec_priv = g_malloc0 (context->codec_priv_size);
  gst_buffer_extract (buf, 0, context->codec_priv, -1);

  context->codec_delay =
      GST_READ_UINT16_LE ((guint8 *) context->codec_priv + 10);
  context->codec_delay =
      gst_util_uint64_scale_round (context->codec_delay, GST_SECOND, 48000);
  context->seek_preroll = 80 * GST_MSECOND;

  return TRUE;

/* ERRORS */
wrong_type:
  {
    GST_WARNING ("streamheaders are not a GST_TYPE_ARRAY, but a %s",
        G_VALUE_TYPE_NAME (streamheader));
    return FALSE;
  }
wrong_count:
  {
    GST_WARNING ("got %u streamheaders, not 1 or 2 as expected", bufarr->len);
    return FALSE;
  }
wrong_content_type:
  {
    GST_WARNING ("streamheaders array does not contain GstBuffers");
    return FALSE;
  }
}

static gboolean
opus_make_codecdata (GstMatroskaTrackContext * context, GstCaps * caps)
{
  guint32 rate;
  guint8 channels;
  guint8 channel_mapping_family;
  guint8 stream_count, coupled_count, channel_mapping[256];
  GstBuffer *buffer;
  GstMapInfo map;

  /* Opus headers are not in-band */
  context->xiph_headers_to_skip = 0;

  context->codec_delay = 0;
  context->seek_preroll = 80 * GST_MSECOND;

  if (!gst_codec_utils_opus_parse_caps (caps, &rate, &channels,
          &channel_mapping_family, &stream_count, &coupled_count,
          channel_mapping)) {
    GST_WARNING ("Failed to parse caps for Opus");
    return FALSE;
  }

  buffer =
      gst_codec_utils_opus_create_header (rate, channels,
      channel_mapping_family, stream_count, coupled_count, channel_mapping, 0,
      0);
  if (!buffer) {
    GST_WARNING ("Failed to create Opus header from caps");
    return FALSE;
  }

  gst_buffer_map (buffer, &map, GST_MAP_READ);
  context->codec_priv_size = map.size;
  context->codec_priv = g_malloc (context->codec_priv_size);
  memcpy (context->codec_priv, map.data, map.size);
  gst_buffer_unmap (buffer, &map);
  gst_buffer_unref (buffer);

  return TRUE;
}

static gboolean
gst_matroska_mux_audio_pad_setcaps (GstMatroskaMux * mux,
    GstMatroskaMuxPad * mux_pad, GstCaps * caps)
{
  GstMatroskaTrackContext *context = NULL;
  GstMatroskaTrackAudioContext *audiocontext;
  const gchar *mimetype;
  gint samplerate = 0, channels = 0;
  GstStructure *structure;
  const GValue *codec_data = NULL;
  GstBuffer *buf = NULL;
  const gchar *stream_format = NULL;
  GstCaps *old_caps;

  if ((old_caps = gst_pad_get_current_caps (GST_PAD (mux_pad)))) {
    if (mux->state >= GST_MATROSKA_MUX_STATE_HEADER
        && !gst_caps_is_equal (caps, old_caps)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("Caps changes are not supported by Matroska"));
      gst_caps_unref (old_caps);
      goto refuse_caps;
    }
    gst_caps_unref (old_caps);
  } else if (mux->state >= GST_MATROSKA_MUX_STATE_HEADER) {
    GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
        ("Caps on pad %" GST_PTR_FORMAT
            " arrived late. Headers were already written", mux_pad));
    goto refuse_caps;
  }

  /* find context */
  context = mux_pad->track;
  g_assert (context);
  g_assert (context->type == GST_MATROSKA_TRACK_TYPE_AUDIO);
  audiocontext = (GstMatroskaTrackAudioContext *) context;

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* general setup */
  gst_structure_get_int (structure, "rate", &samplerate);
  gst_structure_get_int (structure, "channels", &channels);

  audiocontext->samplerate = samplerate;
  audiocontext->channels = channels;
  audiocontext->bitdepth = 0;
  context->default_duration = 0;

  codec_data = gst_structure_get_value (structure, "codec_data");
  if (codec_data)
    buf = gst_value_get_buffer (codec_data);

  /* TODO: - check if we handle all codecs by the spec, i.e. codec private
   *         data and other settings
   *       - add new formats
   */

  if (!strcmp (mimetype, "audio/mpeg")) {
    gint mpegversion = 0;

    gst_structure_get_int (structure, "mpegversion", &mpegversion);
    switch (mpegversion) {
      case 1:{
        gint layer;
        gint version = 1;
        gint spf;

        gst_structure_get_int (structure, "layer", &layer);

        if (!gst_structure_get_int (structure, "mpegaudioversion", &version)) {
          GST_WARNING_OBJECT (mux,
              "Unable to determine MPEG audio version, assuming 1");
          version = 1;
        }

        if (layer == 1)
          spf = 384;
        else if (layer == 2)
          spf = 1152;
        else if (version == 2)
          spf = 576;
        else
          spf = 1152;

        context->default_duration =
            gst_util_uint64_scale (GST_SECOND, spf, audiocontext->samplerate);

        switch (layer) {
          case 1:
            gst_matroska_mux_set_codec_id (context,
                GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L1);
            break;
          case 2:
            gst_matroska_mux_set_codec_id (context,
                GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L2);
            break;
          case 3:
            gst_matroska_mux_set_codec_id (context,
                GST_MATROSKA_CODEC_ID_AUDIO_MPEG1_L3);
            break;
          default:
            goto refuse_caps;
        }
        break;
      }
      case 2:
      case 4:
        stream_format = gst_structure_get_string (structure, "stream-format");
        /* check this is raw aac */
        if (stream_format) {
          if (strcmp (stream_format, "raw") != 0) {
            GST_WARNING_OBJECT (mux, "AAC stream-format must be 'raw', not %s",
                stream_format);
          }
        } else {
          GST_WARNING_OBJECT (mux, "AAC stream-format not specified, "
              "assuming 'raw'");
        }

        if (buf) {
          gst_matroska_mux_set_codec_id (context,
              GST_MATROSKA_CODEC_ID_AUDIO_AAC);
          context->codec_priv_size = gst_buffer_get_size (buf);
          context->codec_priv = g_malloc (context->codec_priv_size);
          gst_buffer_extract (buf, 0, context->codec_priv,
              context->codec_priv_size);
        } else {
          GST_DEBUG_OBJECT (mux, "no AAC codec_data; not packetized");
          goto refuse_caps;
        }
        break;
      default:
        goto refuse_caps;
    }
  } else if (!strcmp (mimetype, "audio/x-raw")) {
    GstAudioInfo info;

    gst_audio_info_init (&info);
    if (!gst_audio_info_from_caps (&info, caps)) {
      GST_DEBUG_OBJECT (mux,
          "broken caps, rejected by gst_audio_info_from_caps");
      goto refuse_caps;
    }

    switch (GST_AUDIO_INFO_FORMAT (&info)) {
      case GST_AUDIO_FORMAT_U8:
      case GST_AUDIO_FORMAT_S16BE:
      case GST_AUDIO_FORMAT_S16LE:
      case GST_AUDIO_FORMAT_S24BE:
      case GST_AUDIO_FORMAT_S24LE:
      case GST_AUDIO_FORMAT_S32BE:
      case GST_AUDIO_FORMAT_S32LE:
        if (GST_AUDIO_INFO_WIDTH (&info) != GST_AUDIO_INFO_DEPTH (&info)) {
          GST_DEBUG_OBJECT (mux, "width must be same as depth!");
          goto refuse_caps;
        }
        if (GST_AUDIO_INFO_IS_BIG_ENDIAN (&info))
          gst_matroska_mux_set_codec_id (context,
              GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_BE);
        else
          gst_matroska_mux_set_codec_id (context,
              GST_MATROSKA_CODEC_ID_AUDIO_PCM_INT_LE);
        break;
      case GST_AUDIO_FORMAT_F32LE:
      case GST_AUDIO_FORMAT_F64LE:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_AUDIO_PCM_FLOAT);
        break;

      default:
        GST_DEBUG_OBJECT (mux, "wrong format in raw audio caps");
        goto refuse_caps;
    }

    audiocontext->bitdepth = GST_AUDIO_INFO_WIDTH (&info);
  } else if (!strcmp (mimetype, "audio/x-vorbis")) {
    const GValue *streamheader;

    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_VORBIS);

    gst_matroska_mux_free_codec_priv (context);

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (!vorbis_streamheader_to_codecdata (streamheader, context)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("vorbis stream headers missing or malformed"));
      goto refuse_caps;
    }
  } else if (!strcmp (mimetype, "audio/x-flac")) {
    const GValue *streamheader;

    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_FLAC);

    gst_matroska_mux_free_codec_priv (context);

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (!flac_streamheader_to_codecdata (streamheader, context)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("flac stream headers missing or malformed"));
      goto refuse_caps;
    }
  } else if (!strcmp (mimetype, "audio/x-speex")) {
    const GValue *streamheader;

    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_SPEEX);
    gst_matroska_mux_free_codec_priv (context);

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (!speex_streamheader_to_codecdata (streamheader, context)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("speex stream headers missing or malformed"));
      goto refuse_caps;
    }
  } else if (!strcmp (mimetype, "audio/x-opus")) {
    const GValue *streamheader;

    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_OPUS);

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (streamheader) {
      gst_matroska_mux_free_codec_priv (context);
      if (!opus_streamheader_to_codecdata (streamheader, context)) {
        GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
            ("opus stream headers missing or malformed"));
        goto refuse_caps;
      }
    } else {
      /* no streamheader, but we need to have one, so we make one up
         based on caps */
      gst_matroska_mux_free_codec_priv (context);
      if (!opus_make_codecdata (context, caps)) {
        GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
            ("opus stream headers missing or malformed"));
        goto refuse_caps;
      }
    }
  } else if (!strcmp (mimetype, "audio/x-ac3")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_AC3);
  } else if (!strcmp (mimetype, "audio/x-eac3")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_EAC3);
  } else if (!strcmp (mimetype, "audio/x-dts")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_DTS);
  } else if (!strcmp (mimetype, "audio/x-tta")) {
    gint width;

    /* TTA frame duration */
    context->default_duration = 1.04489795918367346939 * GST_SECOND;

    gst_structure_get_int (structure, "width", &width);
    audiocontext->bitdepth = width;
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_TTA);

  } else if (!strcmp (mimetype, "audio/x-pn-realaudio")) {
    gint raversion;
    const GValue *mdpr_data;

    gst_structure_get_int (structure, "raversion", &raversion);
    switch (raversion) {
      case 1:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_AUDIO_REAL_14_4);
        break;
      case 2:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_AUDIO_REAL_28_8);
        break;
      case 8:
        gst_matroska_mux_set_codec_id (context,
            GST_MATROSKA_CODEC_ID_AUDIO_REAL_COOK);
        break;
      default:
        goto refuse_caps;
    }

    mdpr_data = gst_structure_get_value (structure, "mdpr_data");
    if (mdpr_data != NULL) {
      guint8 *priv_data = NULL;
      guint priv_data_size = 0;

      GstBuffer *codec_data_buf = g_value_peek_pointer (mdpr_data);

      priv_data_size = gst_buffer_get_size (codec_data_buf);
      priv_data = g_malloc0 (priv_data_size);

      gst_buffer_extract (codec_data_buf, 0, priv_data, -1);

      gst_matroska_mux_free_codec_priv (context);

      context->codec_priv = priv_data;
      context->codec_priv_size = priv_data_size;
    }

  } else if (!strcmp (mimetype, "audio/x-wma")
      || !strcmp (mimetype, "audio/x-alaw")
      || !strcmp (mimetype, "audio/x-mulaw")
      || !strcmp (mimetype, "audio/x-adpcm")
      || !strcmp (mimetype, "audio/G722")) {
    guint8 *codec_priv;
    guint codec_priv_size;
    guint16 format = 0;
    gint block_align = 0;
    gint bitrate = 0;

    if (samplerate == 0 || channels == 0) {
      GST_WARNING_OBJECT (mux, "Missing channels/samplerate on caps");
      goto refuse_caps;
    }

    if (!strcmp (mimetype, "audio/x-wma")) {
      gint wmaversion;
      gint depth;

      if (!gst_structure_get_int (structure, "wmaversion", &wmaversion)
          || !gst_structure_get_int (structure, "block_align", &block_align)
          || !gst_structure_get_int (structure, "bitrate", &bitrate)) {
        GST_WARNING_OBJECT (mux, "Missing wmaversion/block_align/bitrate"
            " on WMA caps");
        goto refuse_caps;
      }

      switch (wmaversion) {
        case 1:
          format = GST_RIFF_WAVE_FORMAT_WMAV1;
          break;
        case 2:
          format = GST_RIFF_WAVE_FORMAT_WMAV2;
          break;
        case 3:
          format = GST_RIFF_WAVE_FORMAT_WMAV3;
          break;
        default:
          GST_WARNING_OBJECT (mux, "Unexpected WMA version: %d", wmaversion);
          goto refuse_caps;
      }

      if (gst_structure_get_int (structure, "depth", &depth))
        audiocontext->bitdepth = depth;
    } else if (!strcmp (mimetype, "audio/x-alaw")
        || !strcmp (mimetype, "audio/x-mulaw")) {
      audiocontext->bitdepth = 8;
      if (!strcmp (mimetype, "audio/x-alaw"))
        format = GST_RIFF_WAVE_FORMAT_ALAW;
      else
        format = GST_RIFF_WAVE_FORMAT_MULAW;

      block_align = channels;
      bitrate = block_align * samplerate;
    } else if (!strcmp (mimetype, "audio/x-adpcm")) {
      const char *layout;

      layout = gst_structure_get_string (structure, "layout");
      if (!layout) {
        GST_WARNING_OBJECT (mux, "Missing layout on adpcm caps");
        goto refuse_caps;
      }

      if (!gst_structure_get_int (structure, "block_align", &block_align)) {
        GST_WARNING_OBJECT (mux, "Missing block_align on adpcm caps");
        goto refuse_caps;
      }

      if (!strcmp (layout, "dvi")) {
        format = GST_RIFF_WAVE_FORMAT_DVI_ADPCM;
      } else if (!strcmp (layout, "g726")) {
        format = GST_RIFF_WAVE_FORMAT_ITU_G726_ADPCM;
        if (!gst_structure_get_int (structure, "bitrate", &bitrate)) {
          GST_WARNING_OBJECT (mux, "Missing bitrate on adpcm g726 caps");
          goto refuse_caps;
        }
      } else {
        GST_WARNING_OBJECT (mux, "Unknown layout on adpcm caps");
        goto refuse_caps;
      }

    } else if (!strcmp (mimetype, "audio/G722")) {
      format = GST_RIFF_WAVE_FORMAT_ADPCM_G722;
    }
    g_assert (format != 0);

    codec_priv_size = WAVEFORMATEX_SIZE;
    if (buf)
      codec_priv_size += gst_buffer_get_size (buf);

    /* serialize waveformatex structure */
    codec_priv = g_malloc0 (codec_priv_size);
    GST_WRITE_UINT16_LE (codec_priv, format);
    GST_WRITE_UINT16_LE (codec_priv + 2, channels);
    GST_WRITE_UINT32_LE (codec_priv + 4, samplerate);
    GST_WRITE_UINT32_LE (codec_priv + 8, bitrate / 8);
    GST_WRITE_UINT16_LE (codec_priv + 12, block_align);
    GST_WRITE_UINT16_LE (codec_priv + 14, 0);
    if (buf)
      GST_WRITE_UINT16_LE (codec_priv + 16, gst_buffer_get_size (buf));
    else
      GST_WRITE_UINT16_LE (codec_priv + 16, 0);

    /* process codec private/initialization data, if any */
    if (buf) {
      gst_buffer_extract (buf, 0,
          (guint8 *) codec_priv + WAVEFORMATEX_SIZE, -1);
    }

    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_AUDIO_ACM);
    gst_matroska_mux_free_codec_priv (context);
    context->codec_priv = (gpointer) codec_priv;
    context->codec_priv_size = codec_priv_size;
  }

  return TRUE;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (mux, "pad %s refused caps %" GST_PTR_FORMAT,
        GST_PAD_NAME (mux_pad), caps);
    return FALSE;
  }
}

/* we probably don't have the data at start,
 * so have to reserve (a maximum) space to write this at the end.
 * bit spacy, but some formats can hold quite some */
#define SUBTITLE_MAX_CODEC_PRIVATE   2048       /* must be > 128 */

static gboolean
gst_matroska_mux_subtitle_pad_setcaps (GstMatroskaMux * mux,
    GstMatroskaMuxPad * mux_pad, GstCaps * caps)
{
  /* There is now (at least) one such alement (kateenc), and I'm going
     to handle it here and claim it works when it can be piped back
     through GStreamer and VLC */

  GstMatroskaTrackContext *context = NULL;
  GstMatroskaTrackSubtitleContext *scontext;
  const gchar *mimetype;
  GstStructure *structure;
  const GValue *value = NULL;
  GstBuffer *buf = NULL;
  gboolean ret = TRUE;
  GstCaps *old_caps;

  if ((old_caps = gst_pad_get_current_caps (GST_PAD (mux_pad)))) {
    if (mux->state >= GST_MATROSKA_MUX_STATE_HEADER
        && !gst_caps_is_equal (caps, old_caps)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("Caps changes are not supported by Matroska"));
      gst_caps_unref (old_caps);
      goto refuse_caps;
    }
    gst_caps_unref (old_caps);
  } else if (mux->state >= GST_MATROSKA_MUX_STATE_HEADER) {
    GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
        ("Caps on pad %" GST_PTR_FORMAT
            " arrived late. Headers were already written", mux_pad));
    goto refuse_caps;
  }

  /* find context */
  context = mux_pad->track;
  g_assert (context);
  g_assert (context->type == GST_MATROSKA_TRACK_TYPE_SUBTITLE);
  scontext = (GstMatroskaTrackSubtitleContext *) context;

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* general setup */
  scontext->check_utf8 = 1;
  scontext->invalid_utf8 = 0;
  context->default_duration = 0;

  if (!strcmp (mimetype, "subtitle/x-kate")) {
    const GValue *streamheader;

    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_SUBTITLE_KATE);

    gst_matroska_mux_free_codec_priv (context);

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (!kate_streamheader_to_codecdata (streamheader, context)) {
      GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
          ("kate stream headers missing or malformed"));
      ret = FALSE;
      goto exit;
    }
  } else if (!strcmp (mimetype, "text/x-raw")) {
    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_SUBTITLE_UTF8);
  } else if (!strcmp (mimetype, "application/x-ssa")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_SUBTITLE_SSA);
  } else if (!strcmp (mimetype, "application/x-ass")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_SUBTITLE_ASS);
  } else if (!strcmp (mimetype, "application/x-usf")) {
    gst_matroska_mux_set_codec_id (context, GST_MATROSKA_CODEC_ID_SUBTITLE_USF);
  } else if (!strcmp (mimetype, "subpicture/x-dvd")) {
    gst_matroska_mux_set_codec_id (context,
        GST_MATROSKA_CODEC_ID_SUBTITLE_VOBSUB);
  } else {
    ret = FALSE;
    goto exit;
  }

  /* maybe some private data, e.g. vobsub */
  value = gst_structure_get_value (structure, "codec_data");
  if (value)
    buf = gst_value_get_buffer (value);
  if (buf != NULL) {
    GstMapInfo map;
    guint8 *priv_data = NULL;

    gst_buffer_map (buf, &map, GST_MAP_READ);

    if (map.size > SUBTITLE_MAX_CODEC_PRIVATE) {
      GST_WARNING_OBJECT (mux, "pad %" GST_PTR_FORMAT " subtitle private data"
          " exceeded maximum (%d); discarding", mux_pad,
          SUBTITLE_MAX_CODEC_PRIVATE);
      gst_buffer_unmap (buf, &map);
      return TRUE;
    }

    gst_matroska_mux_free_codec_priv (context);

    priv_data = g_malloc0 (map.size);
    memcpy (priv_data, map.data, map.size);
    context->codec_priv = priv_data;
    context->codec_priv_size = map.size;
    gst_buffer_unmap (buf, &map);
  }

  GST_DEBUG_OBJECT (mux_pad, "codec_id %s, codec data size %" G_GSIZE_FORMAT,
      GST_STR_NULL (context->codec_id), context->codec_priv_size);

exit:
  return ret;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (mux, "pad %s refused caps %" GST_PTR_FORMAT,
        GST_PAD_NAME (mux_pad), caps);
    return FALSE;
  }
}


static GstPad *
gst_matroska_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);
  GstMatroskaMuxPad *pad;
  gchar *name = NULL;
  const gchar *pad_name = NULL;
  GstMatroskaCapsFunc capsfunc = NULL;
  GstMatroskaTrackContext *context = NULL;
  gint pad_id;
  const gchar *id = NULL;

  if (templ == gst_element_class_get_pad_template (klass, "audio_%u")) {
    /* don't mix named and unnamed pads, if the pad already exists we fail when
     * trying to add it */
    if (req_name != NULL && sscanf (req_name, "audio_%u", &pad_id) == 1) {
      pad_name = req_name;
    } else {
      name = g_strdup_printf ("audio_%u", mux->num_a_streams++);
      pad_name = name;
    }
    capsfunc = GST_DEBUG_FUNCPTR (gst_matroska_mux_audio_pad_setcaps);
    context = (GstMatroskaTrackContext *)
        g_new0 (GstMatroskaTrackAudioContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_AUDIO;
    context->name = g_strdup ("Audio");
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%u")) {
    /* don't mix named and unnamed pads, if the pad already exists we fail when
     * trying to add it */
    if (req_name != NULL && sscanf (req_name, "video_%u", &pad_id) == 1) {
      pad_name = req_name;
    } else {
      name = g_strdup_printf ("video_%u", mux->num_v_streams++);
      pad_name = name;
    }
    capsfunc = GST_DEBUG_FUNCPTR (gst_matroska_mux_video_pad_setcaps);
    context = (GstMatroskaTrackContext *)
        g_new0 (GstMatroskaTrackVideoContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_VIDEO;
    context->name = g_strdup ("Video");
  } else if (templ == gst_element_class_get_pad_template (klass, "subtitle_%u")) {
    /* don't mix named and unnamed pads, if the pad already exists we fail when
     * trying to add it */
    if (req_name != NULL && sscanf (req_name, "subtitle_%u", &pad_id) == 1) {
      pad_name = req_name;
    } else {
      name = g_strdup_printf ("subtitle_%u", mux->num_t_streams++);
      pad_name = name;
    }
    capsfunc = GST_DEBUG_FUNCPTR (gst_matroska_mux_subtitle_pad_setcaps);
    context = (GstMatroskaTrackContext *)
        g_new0 (GstMatroskaTrackSubtitleContext, 1);
    context->type = GST_MATROSKA_TRACK_TYPE_SUBTITLE;
    context->name = g_strdup ("Subtitle");
    /* setcaps may only provide proper one a lot later */
    id = "S_SUB_UNKNOWN";
  } else {
    GST_WARNING_OBJECT (mux, "This is not our template!");
    return NULL;
  }

  pad = (GstMatroskaMuxPad *)
      GST_ELEMENT_CLASS (parent_class)->request_new_pad (element,
      templ, pad_name, caps);

  pad->track = context;
  gst_matroska_pad_reset (pad, FALSE);
  if (id)
    gst_matroska_mux_set_codec_id (pad->track, id);
  pad->track->dts_only = FALSE;

  pad->capsfunc = capsfunc;

  g_free (name);

  mux->num_streams++;

  GST_DEBUG_OBJECT (pad, "Added new request pad");

  return GST_PAD (pad);
}

static void
gst_matroska_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (element);
  GList *walk;

  GST_OBJECT_LOCK (mux);
  for (walk = GST_ELEMENT (mux)->sinkpads; walk; walk = g_list_next (walk)) {
    GstMatroskaMuxPad *other_pad = (GstMatroskaMuxPad *) walk->data;

    if (GST_PAD_CAST (other_pad) == pad) {
      /*
       * observed duration, this will remain GST_CLOCK_TIME_NONE
       * only if the pad is reset
       */
      GstClockTime collected_duration = GST_CLOCK_TIME_NONE;

      if (GST_CLOCK_TIME_IS_VALID (other_pad->start_ts) &&
          GST_CLOCK_TIME_IS_VALID (other_pad->end_ts)) {
        collected_duration =
            GST_CLOCK_DIFF (other_pad->start_ts, other_pad->end_ts);
      }

      if (GST_CLOCK_TIME_IS_VALID (collected_duration)
          && mux->duration < collected_duration)
        mux->duration = collected_duration;

      break;
    }
  }
  GST_OBJECT_UNLOCK (mux);

  GST_ELEMENT_CLASS (parent_class)->release_pad (element, pad);

  mux->num_streams--;
}

static void
gst_matroska_mux_write_mastering_metadata (GstMatroskaMux * mux,
    GstMatroskaTrackVideoContext * videocontext)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  guint64 master;
  GstVideoMasteringDisplayInfo *minfo = &videocontext->mastering_display_info;
  gdouble value;
  const gdouble chroma_scale = 50000;
  const gdouble luma_scale = 50000;

  if (!videocontext->mastering_display_info_present)
    return;

  master =
      gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_MASTERINGMETADATA);

  value = (gdouble) minfo->display_primaries[0].x / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_PRIMARYRCHROMATICITYX, value);

  value = (gdouble) minfo->display_primaries[0].y / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_PRIMARYRCHROMATICITYY, value);

  value = (gdouble) minfo->display_primaries[1].x / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_PRIMARYGCHROMATICITYX, value);

  value = (gdouble) minfo->display_primaries[1].y / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_PRIMARYGCHROMATICITYY, value);

  value = (gdouble) minfo->display_primaries[2].x / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_PRIMARYBCHROMATICITYX, value);

  value = (gdouble) minfo->display_primaries[2].y / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_PRIMARYBCHROMATICITYY, value);

  value = (gdouble) minfo->white_point.x / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_WHITEPOINTCHROMATICITYX, value);

  value = (gdouble) minfo->white_point.y / chroma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_WHITEPOINTCHROMATICITYY, value);

  value = (gdouble) minfo->max_display_mastering_luminance / luma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_LUMINANCEMAX, value);

  value = (gdouble) minfo->min_display_mastering_luminance / luma_scale;
  gst_ebml_write_float (ebml, GST_MATROSKA_ID_LUMINANCEMIN, value);

  gst_ebml_write_master_finish (ebml, master);
  return;
}

static void
gst_matroska_mux_write_colour (GstMatroskaMux * mux,
    GstMatroskaTrackVideoContext * videocontext)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  guint64 master;
  guint matrix_id = 0;
  guint range_id = 0;
  guint transfer_id = 0;
  guint primaries_id = 0;

  master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_VIDEOCOLOUR);

  switch (videocontext->colorimetry.range) {
    case GST_VIDEO_COLOR_RANGE_UNKNOWN:
      range_id = 0;
      break;
    case GST_VIDEO_COLOR_RANGE_16_235:
      range_id = 1;
      break;
    case GST_VIDEO_COLOR_RANGE_0_255:
      range_id = 2;
  }

  matrix_id = gst_video_color_matrix_to_iso (videocontext->colorimetry.matrix);
  transfer_id =
      gst_video_transfer_function_to_iso (videocontext->colorimetry.transfer);
  primaries_id =
      gst_video_color_primaries_to_iso (videocontext->colorimetry.primaries);

  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEORANGE, range_id);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOMATRIXCOEFFICIENTS,
      matrix_id);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOTRANSFERCHARACTERISTICS,
      transfer_id);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOPRIMARIES, primaries_id);
  if (videocontext->content_light_level.max_content_light_level &&
      videocontext->content_light_level.max_frame_average_light_level) {
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_MAXCLL,
        videocontext->content_light_level.max_content_light_level);
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_MAXFALL,
        videocontext->content_light_level.max_frame_average_light_level);
  }

  gst_matroska_mux_write_mastering_metadata (mux, videocontext);
  gst_ebml_write_master_finish (ebml, master);
}

static void
gst_matroska_mux_track_header (GstMatroskaMux * mux,
    GstMatroskaTrackContext * context)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  guint64 master;

  /* TODO: check if everything necessary is written and check default values */

  /* track type goes before the type-specific stuff */
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKNUMBER, context->num);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKTYPE, context->type);

  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKUID, context->uid);
  if (context->default_duration) {
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TRACKDEFAULTDURATION,
        context->default_duration);
  }
  if (context->language) {
    gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_TRACKLANGUAGE,
        context->language);
  }

  /* FIXME: until we have a nice way of getting the codecname
   * out of the caps, I'm not going to enable this. Too much
   * (useless, double, boring) work... */
  /* TODO: Use value from tags if any */
  /*gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_CODECNAME,
     context->codec_name); */
  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_TRACKNAME, context->name);

  /* type-specific stuff */
  switch (context->type) {
    case GST_MATROSKA_TRACK_TYPE_VIDEO:{
      GstMatroskaTrackVideoContext *videocontext =
          (GstMatroskaTrackVideoContext *) context;

      master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKVIDEO);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOPIXELWIDTH,
          videocontext->pixel_width);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOPIXELHEIGHT,
          videocontext->pixel_height);
      if (videocontext->display_width && videocontext->display_height) {
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEODISPLAYWIDTH,
            videocontext->display_width);
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEODISPLAYHEIGHT,
            videocontext->display_height);
      }
      switch (videocontext->interlace_mode) {
        case GST_MATROSKA_INTERLACE_MODE_INTERLACED:
          gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOFLAGINTERLACED, 1);
          break;
        case GST_MATROSKA_INTERLACE_MODE_PROGRESSIVE:
          gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOFLAGINTERLACED, 2);
          break;
        default:
          break;
      }

      if (videocontext->fourcc) {
        guint32 fcc_le = GUINT32_TO_LE (videocontext->fourcc);

        gst_ebml_write_binary (ebml, GST_MATROSKA_ID_VIDEOCOLOURSPACE,
            (gpointer) & fcc_le, 4);
      }
      gst_matroska_mux_write_colour (mux, videocontext);
      if (videocontext->multiview_mode != GST_VIDEO_MULTIVIEW_MODE_NONE) {
        guint64 stereo_mode = 0;

        switch (videocontext->multiview_mode) {
          case GST_VIDEO_MULTIVIEW_MODE_MONO:
            break;
          case GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE:
            if (videocontext->multiview_flags &
                GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST)
              stereo_mode = GST_MATROSKA_STEREO_MODE_SBS_RL;
            else
              stereo_mode = GST_MATROSKA_STEREO_MODE_SBS_LR;
            break;
          case GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM:
            if (videocontext->multiview_flags &
                GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST)
              stereo_mode = GST_MATROSKA_STEREO_MODE_TB_RL;
            else
              stereo_mode = GST_MATROSKA_STEREO_MODE_TB_LR;
            break;
          case GST_VIDEO_MULTIVIEW_MODE_CHECKERBOARD:
            if (videocontext->multiview_flags &
                GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST)
              stereo_mode = GST_MATROSKA_STEREO_MODE_CHECKER_RL;
            else
              stereo_mode = GST_MATROSKA_STEREO_MODE_CHECKER_LR;
            break;
          case GST_VIDEO_MULTIVIEW_MODE_FRAME_BY_FRAME:
            if (videocontext->multiview_flags &
                GST_VIDEO_MULTIVIEW_FLAGS_RIGHT_VIEW_FIRST)
              stereo_mode = GST_MATROSKA_STEREO_MODE_FBF_RL;
            else
              stereo_mode = GST_MATROSKA_STEREO_MODE_FBF_LR;
            /* FIXME: In frame-by-frame mode, left/right frame buffers need to be
             * laced within one block. See http://www.matroska.org/technical/specs/index.html#StereoMode */
            GST_FIXME_OBJECT (mux,
                "Frame-by-frame stereoscopic mode not fully implemented");
            break;
          default:
            GST_WARNING_OBJECT (mux,
                "Multiview mode %d not supported in Matroska/WebM",
                videocontext->multiview_mode);
            break;
        }

        if (stereo_mode != 0)
          gst_ebml_write_uint (ebml, GST_MATROSKA_ID_VIDEOSTEREOMODE,
              stereo_mode);
      }
      gst_ebml_write_master_finish (ebml, master);

      break;
    }

    case GST_MATROSKA_TRACK_TYPE_AUDIO:{
      GstMatroskaTrackAudioContext *audiocontext =
          (GstMatroskaTrackAudioContext *) context;

      master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKAUDIO);
      if (audiocontext->samplerate != 8000)
        gst_ebml_write_float (ebml, GST_MATROSKA_ID_AUDIOSAMPLINGFREQ,
            audiocontext->samplerate);
      if (audiocontext->channels != 1)
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_AUDIOCHANNELS,
            audiocontext->channels);
      if (audiocontext->bitdepth) {
        gst_ebml_write_uint (ebml, GST_MATROSKA_ID_AUDIOBITDEPTH,
            audiocontext->bitdepth);
      }

      gst_ebml_write_master_finish (ebml, master);

      break;
    }

    case GST_MATROSKA_TRACK_TYPE_SUBTITLE:{
      break;
    }
    default:
      /* doesn't need type-specific data */
      break;
  }

  GST_DEBUG_OBJECT (mux, "Wrote track header. Codec %s", context->codec_id);

  gst_ebml_write_ascii (ebml, GST_MATROSKA_ID_CODECID, context->codec_id);
  if (context->codec_priv)
    gst_ebml_write_binary (ebml, GST_MATROSKA_ID_CODECPRIVATE,
        context->codec_priv, context->codec_priv_size);

  if (context->seek_preroll) {
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_SEEKPREROLL,
        context->seek_preroll);
  }

  if (context->codec_delay) {
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CODECDELAY,
        context->codec_delay);
  }
}

static void
gst_matroska_mux_write_chapter_title (const gchar * title, GstEbmlWrite * ebml)
{
  guint64 title_master;

  title_master =
      gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CHAPTERDISPLAY);

  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_CHAPSTRING, title);
  gst_ebml_write_ascii (ebml, GST_MATROSKA_ID_CHAPLANGUAGE,
      GST_MATROSKA_MUX_CHAPLANG);

  gst_ebml_write_master_finish (ebml, title_master);
}

static GstTocEntry *
gst_matroska_mux_write_chapter (GstMatroskaMux * mux, GstTocEntry * edition,
    GstTocEntry * entry, GstEbmlWrite * ebml, guint64 * master_chapters,
    guint64 * master_edition)
{
  guint64 master_chapteratom;
  GList *cur;
  guint count, i;
  gchar *title;
  gint64 start, stop;
  guint64 uid;
  gchar s_uid[32];
  GstTocEntry *internal_chapter, *internal_nested;
  GstTagList *tags;

  if (G_UNLIKELY (master_chapters != NULL && *master_chapters == 0))
    *master_chapters =
        gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CHAPTERS);

  if (G_UNLIKELY (master_edition != NULL && *master_edition == 0)) {
    /* create uid for the parent */
    *master_edition =
        gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_EDITIONENTRY);

    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_EDITIONUID,
        g_ascii_strtoull (gst_toc_entry_get_uid (edition), NULL, 10));
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_EDITIONFLAGHIDDEN, 0);
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_EDITIONFLAGDEFAULT, 0);
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_EDITIONFLAGORDERED, 0);
  }

  gst_toc_entry_get_start_stop_times (entry, &start, &stop);
  tags = gst_toc_entry_get_tags (entry);
  if (tags != NULL) {
    tags = gst_tag_list_copy (tags);
  }

  /* build internal chapter */
  uid = gst_matroska_mux_create_uid ();
  g_snprintf (s_uid, sizeof (s_uid), "%" G_GINT64_FORMAT, uid);
  internal_chapter = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, s_uid);

  /* Write the chapter entry */
  master_chapteratom =
      gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CHAPTERATOM);

  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CHAPTERUID, uid);
  /* Store the user provided UID in the ChapterStringUID */
  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_CHAPTERSTRINGUID,
      gst_toc_entry_get_uid (entry));
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CHAPTERTIMESTART, start);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CHAPTERTIMESTOP, stop);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CHAPTERFLAGHIDDEN, 0);
  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CHAPTERFLAGENABLED, 1);

  /* write current ChapterDisplays before the nested chapters */
  if (G_LIKELY (tags != NULL)) {
    count = gst_tag_list_get_tag_size (tags, GST_TAG_TITLE);

    for (i = 0; i < count; ++i) {
      gst_tag_list_get_string_index (tags, GST_TAG_TITLE, i, &title);
      /* FIXME: handle ChapterLanguage entries */
      gst_matroska_mux_write_chapter_title (title, ebml);
      g_free (title);
    }

    /* remove title tag */
    if (G_LIKELY (count > 0))
      gst_tag_list_remove_tag (tags, GST_TAG_TITLE);

    gst_toc_entry_set_tags (internal_chapter, tags);
  }

  /* Write nested chapters */
  for (cur = gst_toc_entry_get_sub_entries (entry); cur != NULL;
      cur = cur->next) {
    internal_nested = gst_matroska_mux_write_chapter (mux, NULL, cur->data,
        ebml, NULL, NULL);

    gst_toc_entry_append_sub_entry (internal_chapter, internal_nested);
  }

  gst_ebml_write_master_finish (ebml, master_chapteratom);

  return internal_chapter;
}

static GstTocEntry *
gst_matroska_mux_write_chapter_edition (GstMatroskaMux * mux,
    GstTocEntry * edition, GList * chapters, GstEbmlWrite * ebml,
    guint64 * master_chapters)
{
  guint64 master_edition = 0;
  gchar s_uid[32];
  GList *cur;
  GstTocEntry *internal_edition, *internal_chapter;
  GstTagList *tags = NULL;

  g_snprintf (s_uid, sizeof (s_uid), "%" G_GINT64_FORMAT,
      gst_matroska_mux_create_uid ());

  if (edition != NULL) {
    /* Edition entry defined, get its tags */
    tags = gst_toc_entry_get_tags (edition);
    if (tags != NULL) {
      tags = gst_tag_list_copy (tags);
    }
  }

  internal_edition = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, s_uid);
  if (tags != NULL) {
    gst_toc_entry_set_tags (internal_edition, tags);
  }

  for (cur = g_list_first (chapters); cur != NULL; cur = cur->next) {
    internal_chapter = gst_matroska_mux_write_chapter (mux, internal_edition,
        cur->data, ebml, master_chapters, &master_edition);

    gst_toc_entry_append_sub_entry (internal_edition, internal_chapter);
  }

  if (G_LIKELY (master_edition != 0))
    gst_ebml_write_master_finish (ebml, master_edition);

  return internal_edition;
}

static gboolean
gst_matroska_mux_start_file (GstMatroskaMux * mux)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  const gchar *doctype;
  guint32 seekhead_id[] = { GST_MATROSKA_ID_SEGMENTINFO,
    GST_MATROSKA_ID_TRACKS,
    GST_MATROSKA_ID_CHAPTERS,
    GST_MATROSKA_ID_CUES,
    GST_MATROSKA_ID_TAGS,
    0
  };
  const gchar *media_type;
  gboolean audio_only;
  guint64 master, child;
  GList *l;
  int i;
  guint tracknum = 1;
  GstClockTime earliest_time = GST_CLOCK_TIME_NONE;
  GstClockTime duration = 0;
  guint32 segment_uid[4];
  gint64 time;
  GstToc *toc;
  GList *sinkpads;

  GST_OBJECT_LOCK (mux);
  sinkpads =
      g_list_copy_deep (GST_ELEMENT (mux)->sinkpads, (GCopyFunc) gst_object_ref,
      NULL);
  GST_OBJECT_UNLOCK (mux);

  if (!sinkpads) {
    GST_ELEMENT_ERROR (mux, STREAM, MUX, (NULL),
        ("No input streams configured"));
    return FALSE;
  }

  /* if not streaming, check if downstream is seekable */
  if (!mux->ebml_write->streamable) {
    gboolean seekable;
    GstQuery *query;

    query = gst_query_new_seeking (GST_FORMAT_BYTES);
    if (gst_pad_peer_query (GST_AGGREGATOR_SRC_PAD (mux), query)) {
      gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
      GST_INFO_OBJECT (mux, "downstream is %sseekable", seekable ? "" : "not ");
    } else {
      /* assume seeking is not supported if query not handled downstream */
      GST_WARNING_OBJECT (mux, "downstream did not handle seeking query");
      seekable = FALSE;
    }
    if (!seekable) {
      mux->ebml_write->streamable = TRUE;
      g_object_notify (G_OBJECT (mux), "streamable");
      GST_WARNING_OBJECT (mux, "downstream is not seekable, but "
          "streamable=false. Will ignore that and create streamable output "
          "instead");
    }
    gst_query_unref (query);
  }

  /* output caps */
  audio_only = mux->num_v_streams == 0 && mux->num_a_streams > 0;
  if (mux->is_webm) {
    media_type = (audio_only) ? "audio/webm" : "video/webm";
  } else {
    media_type = (audio_only) ? "audio/x-matroska" : "video/x-matroska";
  }
  ebml->caps = gst_caps_new_empty_simple (media_type);
  gst_aggregator_set_src_caps (GST_AGGREGATOR (mux), ebml->caps);

  /* we start with a EBML header */
  doctype = mux->doctype;
  GST_INFO_OBJECT (ebml, "DocType: %s, Version: %d",
      doctype, mux->doctype_version);
  gst_ebml_write_header (ebml, doctype, mux->doctype_version);

  /* the rest of the header is cached */
  gst_ebml_write_set_cache (ebml, 0x1000);

  /* start a segment */
  mux->segment_pos =
      gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEGMENT);
  mux->segment_master = ebml->pos;

  if (!mux->ebml_write->streamable) {
    /* seekhead (table of contents) - we set the positions later */
    mux->seekhead_pos = ebml->pos;
    master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEEKHEAD);
    for (i = 0; seekhead_id[i] != 0; i++) {
      child = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEEKENTRY);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_SEEKID, seekhead_id[i]);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_SEEKPOSITION, -1);
      gst_ebml_write_master_finish (ebml, child);
    }
    gst_ebml_write_master_finish (ebml, master);
  }

  if (mux->ebml_write->streamable) {
    const GstTagList *tags;
    gboolean has_main_tags;

    /* tags */
    tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (mux));
    has_main_tags = tags != NULL && !gst_matroska_mux_tag_list_is_empty (tags);

    if (has_main_tags || gst_matroska_mux_streams_have_tags (mux)) {
      guint64 master_tags, master_tag;

      GST_DEBUG_OBJECT (mux, "Writing tags");

      mux->tags_pos = ebml->pos;
      master_tags = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAGS);
      if (has_main_tags) {
        master_tag = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAG);
        gst_tag_list_foreach (tags, gst_matroska_mux_write_simple_tag, ebml);
        gst_ebml_write_master_finish (ebml, master_tag);
      }
      gst_matroska_mux_write_streams_tags (mux);
      gst_ebml_write_master_finish (ebml, master_tags);
    }
  }

  /* segment info */
  mux->info_pos = ebml->pos;
  master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_SEGMENTINFO);

  /* WebM does not support SegmentUID field on SegmentInfo */
  if (!mux->is_webm) {
    for (i = 0; i < 4; i++) {
      segment_uid[i] = g_random_int ();
    }
    gst_ebml_write_binary (ebml, GST_MATROSKA_ID_SEGMENTUID,
        (guint8 *) segment_uid, 16);
  }

  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TIMECODESCALE, mux->time_scale);
  mux->duration_pos = ebml->pos;
  /* get duration */
  if (!mux->ebml_write->streamable) {
    for (l = sinkpads; l; l = g_list_next (l)) {
      GstMatroskaMuxPad *pad = l->data;
      gint64 trackduration;

      /* Query the total length of the track. */
      GST_DEBUG_OBJECT (pad, "querying peer duration");
      if (gst_pad_peer_query_duration (GST_PAD_CAST (pad), GST_FORMAT_TIME,
              &trackduration)) {
        GST_DEBUG_OBJECT (GST_PAD_CAST (pad), "duration: %" GST_TIME_FORMAT,
            GST_TIME_ARGS (trackduration));
        if (trackduration != GST_CLOCK_TIME_NONE && trackduration > duration) {
          duration = (GstClockTime) trackduration;
        }
      }
    }

    gst_ebml_write_float (ebml, GST_MATROSKA_ID_DURATION,
        gst_guint64_to_gdouble (duration) /
        gst_guint64_to_gdouble (mux->time_scale));
  }
  gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_MUXINGAPP,
      "GStreamer matroskamux version " PACKAGE_VERSION);
  if (mux->writing_app && mux->writing_app[0]) {
    gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_WRITINGAPP, mux->writing_app);
  }
  if (mux->creation_time != NULL) {
    time = g_date_time_to_unix (mux->creation_time) * GST_SECOND;
    time += g_date_time_get_microsecond (mux->creation_time) * GST_USECOND;
  } else {
    time = g_get_real_time () * GST_USECOND;
  }
  gst_ebml_write_date (ebml, GST_MATROSKA_ID_DATEUTC, time);
  gst_ebml_write_master_finish (ebml, master);

  /* tracks */
  mux->tracks_pos = ebml->pos;
  master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKS);

  for (l = sinkpads; l; l = g_list_next (l)) {
    GstMatroskaMuxPad *pad = l->data;
    GstBuffer *buf;

    /* This will cause an error at a later time */
    if (pad->track->codec_id == NULL)
      continue;

    /* Find the smallest timestamp so we can offset all streams by this to
     * start at 0 */
    if (mux->offset_to_zero) {
      GstClockTime ts;

      buf = gst_aggregator_pad_peek_buffer (GST_AGGREGATOR_PAD (pad));

      if (buf) {
        ts = gst_matroska_track_get_buffer_timestamp (pad->track, buf);
        if (earliest_time == GST_CLOCK_TIME_NONE)
          earliest_time = ts;
        else if (ts != GST_CLOCK_TIME_NONE && ts < earliest_time)
          earliest_time = ts;
      }

      if (buf)
        gst_buffer_unref (buf);
    }

    /* For audio tracks, use the first buffers duration as the default
     * duration if we didn't get any better idea from the caps event already
     */
    if (pad->track->type == GST_MATROSKA_TRACK_TYPE_AUDIO &&
        pad->track->default_duration == 0) {
      buf = gst_aggregator_pad_peek_buffer (GST_AGGREGATOR_PAD (pad));

      if (buf && GST_BUFFER_DURATION_IS_VALID (buf))
        pad->track->default_duration =
            GST_BUFFER_DURATION (buf) + pad->track->codec_delay;
      if (buf)
        gst_buffer_unref (buf);
    }

    pad->track->num = tracknum++;
    child = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TRACKENTRY);
    gst_matroska_mux_track_header (mux, pad->track);
    gst_ebml_write_master_finish (ebml, child);
    /* some remaining pad/track setup */
    pad->default_duration_scaled =
        gst_util_uint64_scale (pad->track->default_duration,
        1, mux->time_scale);
  }
  gst_ebml_write_master_finish (ebml, master);

  mux->earliest_time = earliest_time == GST_CLOCK_TIME_NONE ? 0 : earliest_time;

  /* chapters */
  toc = gst_toc_setter_get_toc (GST_TOC_SETTER (mux));
  if (toc != NULL && !mux->ebml_write->streamable) {
    guint64 master_chapters = 0;
    GstTocEntry *internal_edition;
    GList *cur, *chapters;

    GST_DEBUG ("Writing chapters");

    /* There are two UIDs for Chapters:
     * - The ChapterUID is a mandatory unsigned integer which internally
     * refers to a given chapter. Except for the title & language which use
     * dedicated fields, this UID can also be used to add tags to the Chapter.
     * The tags come in a separate section of the container.
     * - The ChapterStringUID is an optional UTF-8 string which also uniquely
     * refers to a chapter but from an external perspective. It can act as a
     * "WebVTT cue identifier" which "can be used to reference a specific cue,
     * for example from script or CSS".
     *
     * The ChapterUID will be generated and checked for unicity, while the
     * ChapterStringUID will receive the user defined UID.
     *
     * In order to be able to refer to chapters from the tags section,
     * we must maintain an internal Toc tree with the generated ChapterUID
     * (see gst_matroska_mux_write_toc_entry_tags) */

    /* Check whether we have editions or chapters at the root level. */
    cur = gst_toc_get_entries (toc);
    if (cur != NULL) {
      mux->chapters_pos = ebml->pos;

      mux->internal_toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);

      if (gst_toc_entry_get_entry_type (cur->data) ==
          GST_TOC_ENTRY_TYPE_EDITION) {
        /* Editions at the root level */
        for (; cur != NULL; cur = cur->next) {
          chapters = gst_toc_entry_get_sub_entries (cur->data);
          internal_edition = gst_matroska_mux_write_chapter_edition (mux,
              cur->data, chapters, ebml, &master_chapters);
          gst_toc_append_entry (mux->internal_toc, internal_edition);
        }
      } else {
        /* Chapters at the root level */
        internal_edition = gst_matroska_mux_write_chapter_edition (mux,
            NULL, cur, ebml, &master_chapters);
        gst_toc_append_entry (mux->internal_toc, internal_edition);
      }

      /* close master element if any edition was written */
      if (G_LIKELY (master_chapters != 0))
        gst_ebml_write_master_finish (ebml, master_chapters);
    }
  }

  /* lastly, flush the cache */
  gst_ebml_write_flush_cache (ebml, FALSE, 0);

  if (toc != NULL)
    gst_toc_unref (toc);

  g_list_free_full (sinkpads, (GDestroyNotify) gst_object_unref);

  return TRUE;
}

/* TODO: more sensible tag mappings */
static const struct
{
  const gchar *matroska_tagname;
  const gchar *gstreamer_tagname;
}
gst_matroska_tag_conv[] = {
  {
      GST_MATROSKA_TAG_ID_TITLE, GST_TAG_TITLE}, {
      GST_MATROSKA_TAG_ID_ARTIST, GST_TAG_ARTIST}, {
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

/* Every stagefright implementation on android up to and including 6.0.1 is using
 libwebm with bug in matroska parsing, where it will choke on empty tag elements;
 so before outputting tags and tag elements we better make sure that there are
 actually tags we are going to write */
static gboolean
gst_matroska_mux_tag_list_is_empty (const GstTagList * list)
{
  int i;
  for (i = 0; i < gst_tag_list_n_tags (list); i++) {
    const gchar *tag = gst_tag_list_nth_tag_name (list, i);
    int i;
    for (i = 0; i < G_N_ELEMENTS (gst_matroska_tag_conv); i++) {
      const gchar *tagname_gst = gst_matroska_tag_conv[i].gstreamer_tagname;
      if (strcmp (tagname_gst, tag) == 0) {
        GValue src = { 0, };
        gchar *dest;

        if (!gst_tag_list_copy_value (&src, list, tag))
          break;
        dest = gst_value_serialize (&src);

        g_value_unset (&src);
        if (dest) {
          g_free (dest);
          return FALSE;
        }
      }
    }
  }
  return TRUE;
}

static void
gst_matroska_mux_write_simple_tag (const GstTagList * list, const gchar * tag,
    gpointer data)
{
  GstEbmlWrite *ebml = (GstEbmlWrite *) data;
  guint i;
  guint64 simpletag_master;

  for (i = 0; i < G_N_ELEMENTS (gst_matroska_tag_conv); i++) {
    const gchar *tagname_gst = gst_matroska_tag_conv[i].gstreamer_tagname;
    const gchar *tagname_mkv = gst_matroska_tag_conv[i].matroska_tagname;

    if (strcmp (tagname_gst, tag) == 0) {
      GValue src = { 0, };
      gchar *dest;

      if (!gst_tag_list_copy_value (&src, list, tag))
        break;
      if ((dest = gst_value_serialize (&src))) {

        simpletag_master = gst_ebml_write_master_start (ebml,
            GST_MATROSKA_ID_SIMPLETAG);
        gst_ebml_write_ascii (ebml, GST_MATROSKA_ID_TAGNAME, tagname_mkv);
        gst_ebml_write_utf8 (ebml, GST_MATROSKA_ID_TAGSTRING, dest);
        gst_ebml_write_master_finish (ebml, simpletag_master);
        g_free (dest);
      } else {
        GST_WARNING ("Can't transform tag '%s' to string", tagname_mkv);
      }
      g_value_unset (&src);
      break;
    }
  }
}

static void
gst_matroska_mux_write_stream_tags (GstMatroskaMux * mux,
    GstMatroskaMuxPad * mpad)
{
  guint64 master_tag, master_targets;
  GstEbmlWrite *ebml;

  ebml = mux->ebml_write;

  if (G_UNLIKELY (mpad->tags == NULL
          || gst_matroska_mux_tag_list_is_empty (mpad->tags)))
    return;

  master_tag = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAG);
  master_targets = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TARGETS);

  gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TARGETTRACKUID, mpad->track->uid);

  gst_ebml_write_master_finish (ebml, master_targets);
  gst_tag_list_foreach (mpad->tags, gst_matroska_mux_write_simple_tag, ebml);
  gst_ebml_write_master_finish (ebml, master_tag);
}

static void
gst_matroska_mux_write_streams_tags (GstMatroskaMux * mux)
{
  GList *sinkpads, *walk;

  GST_OBJECT_LOCK (mux);
  sinkpads =
      g_list_copy_deep (GST_ELEMENT (mux)->sinkpads, (GCopyFunc) gst_object_ref,
      NULL);
  GST_OBJECT_UNLOCK (mux);

  for (walk = sinkpads; walk; walk = g_list_next (walk)) {
    GstMatroskaMuxPad *pad = (GstMatroskaMuxPad *) walk->data;

    gst_matroska_mux_write_stream_tags (mux, pad);
  }

  g_list_free_full (sinkpads, (GDestroyNotify) gst_object_unref);
}

static gboolean
gst_matroska_mux_streams_have_tags (GstMatroskaMux * mux)
{
  GList *walk;

  GST_OBJECT_LOCK (mux);
  for (walk = GST_ELEMENT (mux)->sinkpads; walk; walk = g_list_next (walk)) {
    GstMatroskaMuxPad *pad = (GstMatroskaMuxPad *) walk->data;
    if (!gst_matroska_mux_tag_list_is_empty (pad->tags)) {
      GST_OBJECT_UNLOCK (mux);
      return TRUE;
    }
  }
  GST_OBJECT_UNLOCK (mux);
  return FALSE;
}

static void
gst_matroska_mux_write_toc_entry_tags (GstMatroskaMux * mux,
    const GstTocEntry * entry, guint64 * master_tags, gboolean * has_tags)
{
  guint64 master_tag, master_targets;
  GstEbmlWrite *ebml;
  GList *cur;
  const GstTagList *tags;

  ebml = mux->ebml_write;

  tags = gst_toc_entry_get_tags (entry);
  if (G_UNLIKELY (tags != NULL && !gst_matroska_mux_tag_list_is_empty (tags))) {
    *has_tags = TRUE;

    if (*master_tags == 0) {
      mux->tags_pos = ebml->pos;
      *master_tags = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAGS);
    }

    master_tag = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAG);
    master_targets =
        gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TARGETS);

    if (gst_toc_entry_get_entry_type (entry) == GST_TOC_ENTRY_TYPE_EDITION)
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TARGETEDITIONUID,
          g_ascii_strtoull (gst_toc_entry_get_uid (entry), NULL, 10));
    else
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_TARGETCHAPTERUID,
          g_ascii_strtoull (gst_toc_entry_get_uid (entry), NULL, 10));

    gst_ebml_write_master_finish (ebml, master_targets);
    gst_tag_list_foreach (tags, gst_matroska_mux_write_simple_tag, ebml);
    gst_ebml_write_master_finish (ebml, master_tag);
  }

  for (cur = gst_toc_entry_get_sub_entries (entry); cur != NULL;
      cur = cur->next) {
    gst_matroska_mux_write_toc_entry_tags (mux, cur->data, master_tags,
        has_tags);
  }
}

static void
gst_matroska_mux_finish (GstMatroskaMux * mux)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  guint64 pos;
  guint64 duration = 0;
  GList *l;
  const GstTagList *tags, *toc_tags;
  const GstToc *toc;
  gboolean has_main_tags, toc_has_tags = FALSE;
  GList *cur;

  /* finish last cluster */
  if (mux->cluster) {
    gst_ebml_write_master_finish (ebml, mux->cluster);
  }

  /* cues */
  if (mux->index != NULL) {
    guint n;
    guint64 master, pointentry_master, trackpos_master;

    mux->cues_pos = ebml->pos;
    gst_ebml_write_set_cache (ebml, 12 + 41 * mux->num_indexes);
    master = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CUES);

    for (n = 0; n < mux->num_indexes; n++) {
      GstMatroskaIndex *idx = &mux->index[n];

      pointentry_master = gst_ebml_write_master_start (ebml,
          GST_MATROSKA_ID_POINTENTRY);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CUETIME,
          idx->time / mux->time_scale);
      trackpos_master = gst_ebml_write_master_start (ebml,
          GST_MATROSKA_ID_CUETRACKPOSITIONS);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CUETRACK, idx->track);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CUECLUSTERPOSITION,
          idx->pos - mux->segment_master);
      gst_ebml_write_master_finish (ebml, trackpos_master);
      gst_ebml_write_master_finish (ebml, pointentry_master);
    }

    gst_ebml_write_master_finish (ebml, master);
    gst_ebml_write_flush_cache (ebml, FALSE, GST_CLOCK_TIME_NONE);
  }

  /* tags */
  tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (mux));
  has_main_tags = tags != NULL && !gst_matroska_mux_tag_list_is_empty (tags);
  toc = gst_toc_setter_get_toc (GST_TOC_SETTER (mux));

  if (has_main_tags || gst_matroska_mux_streams_have_tags (mux) || toc != NULL) {
    guint64 master_tags = 0, master_tag;

    GST_DEBUG_OBJECT (mux, "Writing tags");

    if (has_main_tags) {
      /* TODO: maybe limit via the TARGETS id by looking at the source pad */
      mux->tags_pos = ebml->pos;
      master_tags = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAGS);
      master_tag = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAG);

      if (tags != NULL)
        gst_tag_list_foreach (tags, gst_matroska_mux_write_simple_tag, ebml);
      if (mux->internal_toc != NULL) {
        toc_tags = gst_toc_get_tags (mux->internal_toc);
        toc_has_tags = (toc_tags != NULL);
        gst_tag_list_foreach (toc_tags, gst_matroska_mux_write_simple_tag,
            ebml);
      }

      gst_ebml_write_master_finish (ebml, master_tag);
    }

    if (mux->internal_toc != NULL) {
      for (cur = gst_toc_get_entries (mux->internal_toc); cur != NULL;
          cur = cur->next) {
        gst_matroska_mux_write_toc_entry_tags (mux, cur->data, &master_tags,
            &toc_has_tags);
      }
    }

    if (master_tags == 0 && gst_matroska_mux_streams_have_tags (mux)) {
      mux->tags_pos = ebml->pos;
      master_tags = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_TAGS);
    }
    gst_matroska_mux_write_streams_tags (mux);

    if (master_tags != 0)
      gst_ebml_write_master_finish (ebml, master_tags);
  }

  /* update seekhead. We know that:
   * - a seekhead contains 5 entries.
   * - order of entries is as above.
   * - a seekhead has a 4-byte header + 8-byte length
   * - each entry is 2-byte master, 2-byte ID pointer,
   *     2-byte length pointer, all 8/1-byte length, 4-
   *     byte ID and 8-byte length pointer, where the
   *     length pointer starts at 20.
   * - all entries are local to the segment (so pos - segment_master).
   * - so each entry is at 12 + 20 + num * 28. */
  gst_ebml_replace_uint (ebml, mux->seekhead_pos + 32,
      mux->info_pos - mux->segment_master);
  gst_ebml_replace_uint (ebml, mux->seekhead_pos + 60,
      mux->tracks_pos - mux->segment_master);
  if (toc != NULL && mux->chapters_pos > 0) {
    gst_ebml_replace_uint (ebml, mux->seekhead_pos + 88,
        mux->chapters_pos - mux->segment_master);
  } else {
    /* void'ify */
    guint64 my_pos = ebml->pos;

    gst_ebml_write_seek (ebml, mux->seekhead_pos + 68);
    gst_ebml_write_buffer_header (ebml, GST_EBML_ID_VOID, 26);
    gst_ebml_write_seek (ebml, my_pos);
  }
  if (mux->index != NULL) {
    gst_ebml_replace_uint (ebml, mux->seekhead_pos + 116,
        mux->cues_pos - mux->segment_master);
  } else {
    /* void'ify */
    guint64 my_pos = ebml->pos;

    gst_ebml_write_seek (ebml, mux->seekhead_pos + 96);
    gst_ebml_write_buffer_header (ebml, GST_EBML_ID_VOID, 26);
    gst_ebml_write_seek (ebml, my_pos);
  }

  if (mux->tags_pos != 0 || toc_has_tags) {
    gst_ebml_replace_uint (ebml, mux->seekhead_pos + 144,
        mux->tags_pos - mux->segment_master);
  } else {
    /* void'ify */
    guint64 my_pos = ebml->pos;

    gst_ebml_write_seek (ebml, mux->seekhead_pos + 124);
    gst_ebml_write_buffer_header (ebml, GST_EBML_ID_VOID, 26);
    gst_ebml_write_seek (ebml, my_pos);
  }

  if (toc != NULL) {
    gst_toc_unref (toc);
  }

  /* loop tracks:
   * - first get the overall duration
   *   (a released track may have left a duration in here)
   * - write some track header data for subtitles
   */
  duration = mux->duration;
  pos = ebml->pos;
  GST_OBJECT_LOCK (mux);
  for (l = GST_ELEMENT (mux)->sinkpads; l; l = g_list_next (l)) {
    GstMatroskaMuxPad *pad = l->data;
    /*
     * observed duration, this will never remain GST_CLOCK_TIME_NONE
     * since this means buffer without timestamps that is not possible
     */
    GstClockTime collected_duration = GST_CLOCK_TIME_NONE;

    GST_DEBUG_OBJECT (mux,
        "Pad %" GST_PTR_FORMAT " start ts %" GST_TIME_FORMAT
        " end ts %" GST_TIME_FORMAT, pad,
        GST_TIME_ARGS (pad->start_ts), GST_TIME_ARGS (pad->end_ts));

    if (GST_CLOCK_TIME_IS_VALID (pad->start_ts) &&
        GST_CLOCK_TIME_IS_VALID (pad->end_ts)) {
      collected_duration = GST_CLOCK_DIFF (pad->start_ts, pad->end_ts);
      GST_DEBUG_OBJECT (pad,
          "final track duration: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (collected_duration));
    } else {
      GST_WARNING_OBJECT (pad, "unable to get final track duration");
    }
    if (GST_CLOCK_TIME_IS_VALID (collected_duration) &&
        duration < collected_duration)
      duration = collected_duration;
  }
  GST_OBJECT_UNLOCK (mux);

  /* seek back (optional, but do anyway) */
  gst_ebml_write_seek (ebml, pos);

  /* update duration */
  if (duration != 0) {
    GST_DEBUG_OBJECT (mux, "final total duration: %" GST_TIME_FORMAT,
        GST_TIME_ARGS (duration));
    pos = mux->ebml_write->pos;
    gst_ebml_write_seek (ebml, mux->duration_pos);
    gst_ebml_write_float (ebml, GST_MATROSKA_ID_DURATION,
        gst_guint64_to_gdouble (duration) /
        gst_guint64_to_gdouble (mux->time_scale));
    gst_ebml_write_seek (ebml, pos);
  } else {
    /* void'ify */
    guint64 my_pos = ebml->pos;

    gst_ebml_write_seek (ebml, mux->duration_pos);
    gst_ebml_write_buffer_header (ebml, GST_EBML_ID_VOID, 8);
    gst_ebml_write_seek (ebml, my_pos);
  }
  GST_DEBUG_OBJECT (mux, "finishing segment");
  /* finish segment - this also writes element length */
  gst_ebml_write_master_finish (ebml, mux->segment_pos);
}

static GstBuffer *
gst_matroska_mux_create_buffer_header (GstMatroskaTrackContext * track,
    gint16 relative_timestamp, int flags)
{
  GstBuffer *hdr;
  guint8 *data = g_malloc (4);

  hdr = gst_buffer_new_wrapped (data, 4);
  /* track num - FIXME: what if num >= 0x80 (unlikely)? */
  data[0] = track->num | 0x80;
  /* time relative to clustertime */
  GST_WRITE_UINT16_BE (data + 1, relative_timestamp);

  /* flags */
  data[3] = flags;

  return hdr;
}

#define DIRAC_PARSE_CODE_SEQUENCE_HEADER 0x00
#define DIRAC_PARSE_CODE_END_OF_SEQUENCE 0x10
#define DIRAC_PARSE_CODE_IS_PICTURE(x) ((x & 0x08) != 0)

static GstBuffer *
gst_matroska_mux_handle_dirac_packet (GstMatroskaMux * mux,
    GstMatroskaMuxPad * mux_pad, GstBuffer * buf)
{
  GstMatroskaTrackVideoContext *ctx =
      (GstMatroskaTrackVideoContext *) mux_pad->track;
  GstMapInfo map;
  guint8 *data;
  gsize size;
  guint8 parse_code;
  guint32 next_parse_offset;
  GstBuffer *ret = NULL;
  gboolean is_muxing_unit = FALSE;

  gst_buffer_map (buf, &map, GST_MAP_READ);
  data = map.data;
  size = map.size;

  if (size < 13) {
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);
    return ret;
  }

  /* Check if this buffer contains a picture or end-of-sequence packet */
  while (size >= 13) {
    if (GST_READ_UINT32_BE (data) != 0x42424344 /* 'BBCD' */ ) {
      gst_buffer_unmap (buf, &map);
      gst_buffer_unref (buf);
      return ret;
    }

    parse_code = GST_READ_UINT8 (data + 4);
    if (parse_code == DIRAC_PARSE_CODE_SEQUENCE_HEADER) {
      if (ctx->dirac_unit) {
        gst_buffer_unref (ctx->dirac_unit);
        ctx->dirac_unit = NULL;
      }
    } else if (DIRAC_PARSE_CODE_IS_PICTURE (parse_code) ||
        parse_code == DIRAC_PARSE_CODE_END_OF_SEQUENCE) {
      is_muxing_unit = TRUE;
      break;
    }

    next_parse_offset = GST_READ_UINT32_BE (data + 5);

    if (G_UNLIKELY (next_parse_offset == 0 || next_parse_offset > size))
      break;

    data += next_parse_offset;
    size -= next_parse_offset;
  }

  if (ctx->dirac_unit)
    ctx->dirac_unit = gst_buffer_append (ctx->dirac_unit, gst_buffer_ref (buf));
  else
    ctx->dirac_unit = gst_buffer_ref (buf);

  gst_buffer_unmap (buf, &map);

  if (is_muxing_unit) {
    ret = gst_buffer_make_writable (ctx->dirac_unit);
    ctx->dirac_unit = NULL;
    gst_buffer_copy_into (ret, buf,
        GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS, 0, -1);
    gst_buffer_unref (buf);
  } else {
    gst_buffer_unref (buf);
    ret = NULL;
  }

  return ret;
}

static void
gst_matroska_mux_stop_streamheader (GstMatroskaMux * mux)
{
  GstCaps *caps;
  GstStructure *s;
  GValue streamheader = { 0 };
  GValue bufval = { 0 };
  GstBuffer *streamheader_buffer;
  GstEbmlWrite *ebml = mux->ebml_write;

  streamheader_buffer = gst_ebml_stop_streamheader (ebml);
  caps = gst_caps_copy (mux->ebml_write->caps);
  s = gst_caps_get_structure (caps, 0);
  g_value_init (&streamheader, GST_TYPE_ARRAY);
  g_value_init (&bufval, GST_TYPE_BUFFER);
  GST_BUFFER_FLAG_SET (streamheader_buffer, GST_BUFFER_FLAG_HEADER);
  gst_value_set_buffer (&bufval, streamheader_buffer);
  gst_value_array_append_value (&streamheader, &bufval);
  g_value_unset (&bufval);
  gst_structure_set_value (s, "streamheader", &streamheader);
  g_value_unset (&streamheader);
  gst_caps_replace (&ebml->caps, caps);
  gst_buffer_unref (streamheader_buffer);
  gst_aggregator_set_src_caps (GST_AGGREGATOR (mux), caps);
  gst_caps_unref (caps);
}

static GstFlowReturn
gst_matroska_mux_write_data (GstMatroskaMux * mux, GstMatroskaMuxPad * mux_pad,
    GstBuffer * buf)
{
  GstEbmlWrite *ebml = mux->ebml_write;
  GstBuffer *hdr;
  guint64 blockgroup;
  gboolean write_duration;
  guint64 cluster_time_scaled;
  gint16 relative_timestamp;
  gint64 relative_timestamp64;
  guint64 block_duration, duration_diff = 0;
  gboolean is_video_keyframe = FALSE;
  gboolean is_video_invisible = FALSE;
  gboolean is_audio_only = FALSE;
  gboolean is_min_duration_reached = FALSE;
  gboolean is_max_duration_exceeded = FALSE;
  gint flags = 0;
  GstClockTime buffer_timestamp;
  GstAudioClippingMeta *cmeta = NULL;

  /* write data */

  /* vorbis/theora headers are retrieved from caps and put in CodecPrivate */
  if (mux_pad->track->xiph_headers_to_skip > 0) {
    --mux_pad->track->xiph_headers_to_skip;
    if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_HEADER)) {
      GST_LOG_OBJECT (mux_pad, "dropping streamheader buffer");
      gst_buffer_unref (buf);
      return GST_FLOW_OK;
    }
  }

  /* for dirac we have to queue up everything up to a picture unit */
  if (!strcmp (mux_pad->track->codec_id, GST_MATROSKA_CODEC_ID_VIDEO_DIRAC)) {
    buf = gst_matroska_mux_handle_dirac_packet (mux, mux_pad, buf);
    if (!buf)
      return GST_FLOW_OK;
  } else if (!strcmp (mux_pad->track->codec_id,
          GST_MATROSKA_CODEC_ID_VIDEO_PRORES)) {
    /* Remove the 'Frame container atom' header' */
    buf = gst_buffer_make_writable (buf);
    gst_buffer_resize (buf, 8, gst_buffer_get_size (buf) - 8);
  }

  buffer_timestamp =
      gst_matroska_track_get_buffer_timestamp (mux_pad->track, buf);
  if (buffer_timestamp >= mux->earliest_time) {
    buffer_timestamp -= mux->earliest_time;
  } else {
    buffer_timestamp = 0;
  }

  /* hm, invalid timestamp (due to --to be fixed--- element upstream);
   * this would wreak havoc with time stored in matroska file */
  /* TODO: maybe calculate a timestamp by using the previous timestamp
   * and default duration */
  if (!GST_CLOCK_TIME_IS_VALID (buffer_timestamp)) {
    GST_WARNING_OBJECT (mux_pad, "Invalid buffer timestamp; dropping buffer");
    gst_buffer_unref (buf);
    return GST_FLOW_OK;
  }

  if (!strcmp (mux_pad->track->codec_id, GST_MATROSKA_CODEC_ID_AUDIO_OPUS)
      && mux_pad->track->codec_delay) {
    /* All timestamps should include the codec delay */
    if (buffer_timestamp > mux_pad->track->codec_delay) {
      buffer_timestamp += mux_pad->track->codec_delay;
    } else {
      buffer_timestamp = 0;
      duration_diff = mux_pad->track->codec_delay - buffer_timestamp;
    }
  }

  /* set the timestamp for outgoing buffers */
  ebml->timestamp = buffer_timestamp;

  if (mux_pad->track->type == GST_MATROSKA_TRACK_TYPE_VIDEO) {
    if (!GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DELTA_UNIT)) {
      GST_LOG_OBJECT (mux, "have video keyframe, ts=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (buffer_timestamp));
      is_video_keyframe = TRUE;
    } else if (GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_DECODE_ONLY) &&
        (!strcmp (mux_pad->track->codec_id, GST_MATROSKA_CODEC_ID_VIDEO_VP8)
            || !strcmp (mux_pad->track->codec_id,
                GST_MATROSKA_CODEC_ID_VIDEO_VP9))) {
      GST_LOG_OBJECT (mux,
          "have VP8 video invisible frame, " "ts=%" GST_TIME_FORMAT,
          GST_TIME_ARGS (buffer_timestamp));
      is_video_invisible = TRUE;
    }
  }

  /* From this point on we use the buffer_timestamp to do cluster and other
   * related arithmetic, so apply the timestamp offset if we have one */
  buffer_timestamp += mux->cluster_timestamp_offset;

  is_audio_only = (mux_pad->track->type == GST_MATROSKA_TRACK_TYPE_AUDIO) &&
      (mux->num_streams == 1);
  is_min_duration_reached = (mux->min_cluster_duration == 0
      || (buffer_timestamp > mux->cluster_time
          && (buffer_timestamp - mux->cluster_time) >=
          mux->min_cluster_duration));
  is_max_duration_exceeded = (mux->max_cluster_duration > 0
      && buffer_timestamp > mux->cluster_time
      && (buffer_timestamp - mux->cluster_time) >=
      MIN (G_MAXINT16 * mux->time_scale, mux->max_cluster_duration));

  if (mux->cluster) {
    /* start a new cluster at every keyframe, at every GstForceKeyUnit event,
     * or when we may be reaching the limit of the relative timestamp */
    if (is_max_duration_exceeded || (is_video_keyframe
            && is_min_duration_reached) || mux->force_key_unit_event
        || (is_audio_only && is_min_duration_reached)) {
      if (!mux->ebml_write->streamable)
        gst_ebml_write_master_finish (ebml, mux->cluster);

      /* Forward the GstForceKeyUnit event after finishing the cluster */
      if (mux->force_key_unit_event) {
        gst_pad_push_event (GST_AGGREGATOR_SRC_PAD (mux),
            mux->force_key_unit_event);
        mux->force_key_unit_event = NULL;
      }
      cluster_time_scaled =
          gst_util_uint64_scale (buffer_timestamp, 1, mux->time_scale);

      mux->prev_cluster_size = ebml->pos - mux->cluster_pos;
      mux->cluster_pos = ebml->pos;
      gst_ebml_write_set_cache (ebml, 0x20);
      mux->cluster =
          gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CLUSTER);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CLUSTERTIMECODE,
          cluster_time_scaled);
      GST_LOG_OBJECT (mux, "cluster timestamp %" G_GUINT64_FORMAT,
          gst_util_uint64_scale (buffer_timestamp, 1, mux->time_scale));
      gst_ebml_write_flush_cache (ebml, is_video_keyframe
          || is_audio_only, buffer_timestamp);
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_PREVSIZE,
          mux->prev_cluster_size);
      /* cluster_time needs to be identical in value to what's stored in the
       * matroska so we need to have it with the same precision as what's
       * possible with the set timecodescale rather than just using the
       * buffer_timestamp.
       * If this is not done the rounding of relative_timestamp will be
       * incorrect and possibly making the timestamps get out of order if tw
       * buffers arrive at the same millisecond (assuming default timecodescale
       * of 1ms) */
      mux->cluster_time =
          gst_util_uint64_scale (cluster_time_scaled, mux->time_scale, 1);
    }
  } else {
    /* first cluster */
    cluster_time_scaled =
        gst_util_uint64_scale (buffer_timestamp, 1, mux->time_scale);
    mux->cluster_pos = ebml->pos;
    gst_ebml_write_set_cache (ebml, 0x20);
    mux->cluster = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_CLUSTER);
    gst_ebml_write_uint (ebml, GST_MATROSKA_ID_CLUSTERTIMECODE,
        cluster_time_scaled);
    gst_ebml_write_flush_cache (ebml, TRUE, buffer_timestamp);
    /* cluster_time needs to be identical in value to what's stored in the
     * matroska so we need to have it with the same precision as what's
     * possible with the set timecodescale rather than just using the
     * buffer_timestamp.
     * If this is not done the rounding of relative_timestamp will be
     * incorrect and possibly making the timestamps get out of order if tw
     * buffers arrive at the same millisecond (assuming default timecodescale
     * of 1ms) */
    mux->cluster_time =
        gst_util_uint64_scale (cluster_time_scaled, mux->time_scale, 1);
  }

  /* We currently write index entries for all video tracks or for the audio
   * track in a single-track audio file.  This could be improved by keeping the
   * index only for the *first* video track. */

  /* TODO: index is useful for every track, should contain the number of
   * the block in the cluster which contains the timestamp, should also work
   * for files with multiple audio tracks.
   */
  if (!mux->ebml_write->streamable && (is_video_keyframe || is_audio_only)) {
    gint last_idx = -1;

    if (mux->min_index_interval != 0) {
      for (last_idx = mux->num_indexes - 1; last_idx >= 0; last_idx--) {
        if (mux->index[last_idx].track == mux_pad->track->num)
          break;
      }
    }

    if (last_idx < 0 || mux->min_index_interval == 0 ||
        (GST_CLOCK_DIFF (mux->index[last_idx].time, buffer_timestamp)
            >= mux->min_index_interval)) {
      GstMatroskaIndex *idx;

      if (mux->num_indexes % 32 == 0) {
        mux->index = g_renew (GstMatroskaIndex, mux->index,
            mux->num_indexes + 32);
      }
      idx = &mux->index[mux->num_indexes++];

      idx->pos = mux->cluster_pos;
      idx->time = buffer_timestamp;
      idx->track = mux_pad->track->num;
    }
  }

  if (!strcmp (mux_pad->track->codec_id, GST_MATROSKA_CODEC_ID_AUDIO_OPUS)) {
    cmeta = gst_buffer_get_audio_clipping_meta (buf);
    g_assert (!cmeta || cmeta->format == GST_FORMAT_DEFAULT);

    /* Start clipping is done via header and CodecDelay */
    if (cmeta && !cmeta->end)
      cmeta = NULL;
  }

  /* Check if the duration differs from the default duration. */
  write_duration = FALSE;
  block_duration = 0;
  if (mux_pad->frame_duration && GST_BUFFER_DURATION_IS_VALID (buf)) {
    block_duration = GST_BUFFER_DURATION (buf) + duration_diff;
    block_duration = gst_util_uint64_scale (block_duration, 1, mux->time_scale);

    /* Padding should be considered in the block duration and is clipped off
     * again during playback. Specifically, firefox considers it a fatal error
     * if there is more padding than the block duration */
    if (cmeta) {
      guint64 end = gst_util_uint64_scale_round (cmeta->end, GST_SECOND, 48000);
      end = gst_util_uint64_scale (end, 1, mux->time_scale);
      block_duration += end;
    }

    /* small difference should be ok. */
    if (block_duration > mux_pad->default_duration_scaled + 1 ||
        block_duration < mux_pad->default_duration_scaled - 1) {
      write_duration = TRUE;
    }
  }

  /* write the block, for doctype v2 use SimpleBlock if possible
   * one slice (*breath*).
   * FIXME: Need to do correct lacing! */
  relative_timestamp64 = buffer_timestamp - mux->cluster_time;
  if (relative_timestamp64 >= 0) {
    /* round the timestamp */
    relative_timestamp64 += gst_util_uint64_scale (mux->time_scale, 1, 2);
    relative_timestamp = gst_util_uint64_scale (relative_timestamp64, 1,
        mux->time_scale);
  } else {
    /* round the timestamp */
    relative_timestamp64 -= gst_util_uint64_scale (mux->time_scale, 1, 2);
    relative_timestamp =
        -((gint16) gst_util_uint64_scale (-relative_timestamp64, 1,
            mux->time_scale));
  }

  if (is_video_invisible)
    flags |= 0x08;

  if (mux->doctype_version > 1 && !write_duration && !cmeta) {
    if (is_video_keyframe)
      flags |= 0x80;

    hdr =
        gst_matroska_mux_create_buffer_header (mux_pad->track,
        relative_timestamp, flags);
    gst_ebml_write_set_cache (ebml, 0x40);
    gst_ebml_write_buffer_header (ebml, GST_MATROSKA_ID_SIMPLEBLOCK,
        gst_buffer_get_size (buf) + gst_buffer_get_size (hdr));
    gst_ebml_write_buffer (ebml, hdr);
    gst_ebml_write_flush_cache (ebml, FALSE, buffer_timestamp);
    gst_ebml_write_buffer (ebml, buf);

    return gst_ebml_last_write_result (ebml);
  } else {
    gst_ebml_write_set_cache (ebml, gst_buffer_get_size (buf) * 2);
    /* write and call order slightly unnatural,
     * but avoids seek and minizes pushing */
    blockgroup = gst_ebml_write_master_start (ebml, GST_MATROSKA_ID_BLOCKGROUP);
    hdr =
        gst_matroska_mux_create_buffer_header (mux_pad->track,
        relative_timestamp, flags);
    if (write_duration)
      gst_ebml_write_uint (ebml, GST_MATROSKA_ID_BLOCKDURATION, block_duration);

    if (!strcmp (mux_pad->track->codec_id, GST_MATROSKA_CODEC_ID_AUDIO_OPUS)
        && cmeta) {
      /* Start clipping is done via header and CodecDelay */
      if (cmeta->end) {
        guint64 end =
            gst_util_uint64_scale_round (cmeta->end, GST_SECOND, 48000);
        gst_ebml_write_sint (ebml, GST_MATROSKA_ID_DISCARDPADDING, end);
      }
    }

    gst_ebml_write_buffer_header (ebml, GST_MATROSKA_ID_BLOCK,
        gst_buffer_get_size (buf) + gst_buffer_get_size (hdr));
    gst_ebml_write_buffer (ebml, hdr);
    gst_ebml_write_master_finish_full (ebml, blockgroup,
        gst_buffer_get_size (buf));
    gst_ebml_write_flush_cache (ebml, FALSE, buffer_timestamp);
    gst_ebml_write_buffer (ebml, buf);

    return gst_ebml_last_write_result (ebml);
  }
}

static GstBuffer *
gst_matroska_mux_clip (GstAggregator * agg, GstAggregatorPad * agg_pad,
    GstBuffer * buf)
{
  GstBuffer *outbuf = buf;
  gint64 signed_dts;

  /* invalid left alone and passed */
  if (G_LIKELY (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS_OR_PTS (buf)))) {
    GstClockTime time;
    GstClockTime buf_dts, abs_dts;
    gint dts_sign;

    time = GST_BUFFER_PTS (buf);

    if (GST_CLOCK_TIME_IS_VALID (time)) {
      time =
          gst_segment_to_running_time (&agg_pad->segment, GST_FORMAT_TIME,
          time);
      if (G_UNLIKELY (!GST_CLOCK_TIME_IS_VALID (time))) {
        GST_DEBUG_OBJECT (agg_pad, "clipping buffer on pad outside segment %"
            GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_PTS (buf)));
        gst_buffer_unref (buf);
        return NULL;
      }
    }

    GST_LOG_OBJECT (agg_pad, "buffer pts %" GST_TIME_FORMAT " -> %"
        GST_TIME_FORMAT " running time",
        GST_TIME_ARGS (GST_BUFFER_PTS (buf)), GST_TIME_ARGS (time));
    outbuf = gst_buffer_make_writable (buf);
    GST_BUFFER_PTS (outbuf) = time;

    dts_sign = gst_segment_to_running_time_full (&agg_pad->segment,
        GST_FORMAT_TIME, GST_BUFFER_DTS (outbuf), &abs_dts);
    buf_dts = GST_BUFFER_DTS (outbuf);
    if (dts_sign > 0) {
      GST_BUFFER_DTS (outbuf) = abs_dts;
      signed_dts = abs_dts;
    } else if (dts_sign < 0) {
      GST_BUFFER_DTS (outbuf) = GST_CLOCK_TIME_NONE;
      signed_dts = -((gint64) abs_dts);
    } else {
      GST_BUFFER_DTS (outbuf) = GST_CLOCK_TIME_NONE;
      signed_dts = GST_CLOCK_STIME_NONE;
    }

    GST_LOG_OBJECT (agg_pad, "buffer dts %" GST_TIME_FORMAT " -> %"
        GST_STIME_FORMAT " running time", GST_TIME_ARGS (buf_dts),
        GST_STIME_ARGS (signed_dts));
  }

  return outbuf;
}

static GstMatroskaMuxPad *
gst_matroska_mux_find_best_pad (GstMatroskaMux * mux, gboolean timeout)
{
  GstMatroskaMuxPad *best = NULL;
  GstClockTime best_time = GST_CLOCK_TIME_NONE;
  GList *l;

  GST_OBJECT_LOCK (mux);
  for (l = GST_ELEMENT (mux)->sinkpads; l; l = g_list_next (l)) {
    GstBuffer *buffer;
    GstClockTime timestamp;
    GstMatroskaMuxPad *mux_pad = l->data;

    buffer = gst_aggregator_pad_peek_buffer (GST_AGGREGATOR_PAD (mux_pad));
    if (!buffer) {
      if (!timeout && !GST_PAD_IS_EOS (mux_pad)) {
        best = NULL;
        best_time = GST_CLOCK_TIME_NONE;
        break;
      }
      continue;
    }

    timestamp =
        gst_matroska_track_get_buffer_timestamp (mux_pad->track, buffer);
    gst_buffer_unref (buffer);
    // GST_CLOCK_TIME_NONE < any other clock time
    if (best == NULL || !GST_CLOCK_TIME_IS_VALID (timestamp) || (best != NULL
            && GST_CLOCK_TIME_IS_VALID (best_time) && timestamp < best_time)) {
      best = mux_pad;
      best_time = timestamp;
    }
  }

  if (best)
    gst_object_ref (best);
  GST_OBJECT_UNLOCK (mux);

  GST_DEBUG_OBJECT (mux, "best pad %s, best time %" GST_TIME_FORMAT,
      best ? GST_PAD_NAME (best) : "(nil)", GST_TIME_ARGS (best_time));

  return best;
}

static gboolean
gst_matroska_mux_all_pads_eos (GstMatroskaMux * mux)
{
  GList *l;

  GST_OBJECT_LOCK (mux);
  for (l = GST_ELEMENT_CAST (mux)->sinkpads; l; l = l->next) {
    GstMatroskaMuxPad *pad = GST_MATROSKA_MUX_PAD (l->data);

    if (gst_aggregator_pad_has_buffer (GST_AGGREGATOR_PAD (pad))
        || !gst_aggregator_pad_is_eos (GST_AGGREGATOR_PAD (pad))) {
      GST_OBJECT_UNLOCK (mux);
      return FALSE;
    }
  }
  GST_OBJECT_UNLOCK (mux);

  return TRUE;
}

static GstFlowReturn
gst_matroska_mux_aggregate (GstAggregator * agg, gboolean timeout)
{
  GstMatroskaMux *mux = GST_MATROSKA_MUX (agg);
  GstClockTime buffer_timestamp, end_ts = GST_CLOCK_TIME_NONE;
  GstEbmlWrite *ebml = mux->ebml_write;
  GstMatroskaMuxPad *best = NULL;
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (mux, "Aggregating (timeout: %d)", timeout);

  /* start with a header */
  if (mux->state == GST_MATROSKA_MUX_STATE_START) {
    mux->state = GST_MATROSKA_MUX_STATE_HEADER;
    gst_ebml_start_streamheader (ebml);
    if (!gst_matroska_mux_start_file (mux)) {
      ret = GST_FLOW_ERROR;
      goto exit;
    }
    gst_matroska_mux_stop_streamheader (mux);
    mux->state = GST_MATROSKA_MUX_STATE_DATA;
  }

  best = gst_matroska_mux_find_best_pad (mux, timeout);

  /* if there is no best pad, we have reached EOS or timed out without any
   * buffers */
  if (best == NULL) {
    if (gst_matroska_mux_all_pads_eos (mux)) {
      GST_DEBUG_OBJECT (mux, "All pads EOS. Finishing...");
      if (!mux->ebml_write->streamable) {
        gst_matroska_mux_finish (mux);
      } else {
        GST_DEBUG_OBJECT (mux, "... but streamable, nothing to finish");
      }
      ret = GST_FLOW_EOS;
    } else {
      ret = GST_AGGREGATOR_FLOW_NEED_DATA;
    }
    goto exit;
  }

  if (best->track->codec_id == NULL) {
    GST_ERROR_OBJECT (best, "No codec-id for pad");
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto exit;
  }

  buf = gst_aggregator_pad_pop_buffer (GST_AGGREGATOR_PAD (best));
  if (!buf) {
    ret = GST_AGGREGATOR_FLOW_NEED_DATA;
    goto exit;
  }

  buffer_timestamp = gst_matroska_track_get_buffer_timestamp (best->track, buf);
  if (buffer_timestamp >= mux->earliest_time) {
    buffer_timestamp -= mux->earliest_time;
  } else {
    GST_ERROR_OBJECT (mux,
        "PTS before first PTS (%" GST_TIME_FORMAT " < %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (buffer_timestamp), GST_TIME_ARGS (mux->earliest_time));
    buffer_timestamp = 0;
  }

  GST_DEBUG_OBJECT (best, "best pad - buffer ts %"
      GST_TIME_FORMAT " dur %" GST_TIME_FORMAT,
      GST_TIME_ARGS (buffer_timestamp),
      GST_TIME_ARGS (GST_BUFFER_DURATION (buf)));

  /* make note of first and last encountered timestamps, so we can calculate
   * the actual duration later when we send an updated header on eos */
  if (GST_CLOCK_TIME_IS_VALID (buffer_timestamp)) {
    end_ts = buffer_timestamp;

    if (GST_BUFFER_DURATION_IS_VALID (buf))
      end_ts += GST_BUFFER_DURATION (buf);
    else if (best->track->default_duration)
      end_ts += best->track->default_duration;

    if (!GST_CLOCK_TIME_IS_VALID (best->end_ts) || end_ts > best->end_ts)
      best->end_ts = end_ts;

    if (G_UNLIKELY (best->start_ts == GST_CLOCK_TIME_NONE ||
            buffer_timestamp < best->start_ts))
      best->start_ts = buffer_timestamp;
  }

  if ((gst_buffer_get_size (buf) == 0 &&
          GST_BUFFER_FLAG_IS_SET (buf, GST_BUFFER_FLAG_GAP) &&
          gst_buffer_get_custom_meta (buf, "GstAggregatorMissingDataMeta"))) {
    GST_DEBUG_OBJECT (best, "Skipping gap buffer");
    gst_buffer_unref (buf);
  } else {
    /* write one buffer */
    ret = gst_matroska_mux_write_data (mux, best, buf);
  }

  if (GST_CLOCK_TIME_IS_VALID (buffer_timestamp)) {
    if (GST_CLOCK_TIME_IS_VALID (end_ts)
        && mux->last_pos < end_ts)
      mux->last_pos = end_ts;
    else if (mux->last_pos < buffer_timestamp)
      mux->last_pos = buffer_timestamp;
  }

exit:
  gst_clear_object (&best);

  return ret;
}


static void
gst_matroska_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstMatroskaMux *mux;

  g_return_if_fail (GST_IS_MATROSKA_MUX (object));
  mux = GST_MATROSKA_MUX (object);

  switch (prop_id) {
    case PROP_WRITING_APP:
      if (!g_value_get_string (value)) {
        GST_WARNING_OBJECT (mux, "writing-app property can not be NULL");
        break;
      }
      g_free (mux->writing_app);
      mux->writing_app = g_value_dup_string (value);
      break;
    case PROP_DOCTYPE_VERSION:
      mux->doctype_version = g_value_get_int (value);
      break;
    case PROP_MIN_INDEX_INTERVAL:
      mux->min_index_interval = g_value_get_int64 (value);
      break;
    case PROP_STREAMABLE:
      mux->ebml_write->streamable = g_value_get_boolean (value);
      break;
    case PROP_TIMECODESCALE:
      mux->time_scale = g_value_get_int64 (value);
      break;
    case PROP_MIN_CLUSTER_DURATION:
      mux->min_cluster_duration = g_value_get_int64 (value);
      break;
    case PROP_MAX_CLUSTER_DURATION:
      mux->max_cluster_duration = g_value_get_int64 (value);
      break;
    case PROP_OFFSET_TO_ZERO:
      mux->offset_to_zero = g_value_get_boolean (value);
      break;
    case PROP_CREATION_TIME:
      g_clear_pointer (&mux->creation_time, g_date_time_unref);
      mux->creation_time = g_value_dup_boxed (value);
      break;
    case PROP_CLUSTER_TIMESTAMP_OFFSET:
      mux->cluster_timestamp_offset = g_value_get_uint64 (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_matroska_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstMatroskaMux *mux;

  g_return_if_fail (GST_IS_MATROSKA_MUX (object));
  mux = GST_MATROSKA_MUX (object);

  switch (prop_id) {
    case PROP_WRITING_APP:
      g_value_set_string (value, mux->writing_app);
      break;
    case PROP_DOCTYPE_VERSION:
      g_value_set_int (value, mux->doctype_version);
      break;
    case PROP_MIN_INDEX_INTERVAL:
      g_value_set_int64 (value, mux->min_index_interval);
      break;
    case PROP_STREAMABLE:
      g_value_set_boolean (value, mux->ebml_write->streamable);
      break;
    case PROP_TIMECODESCALE:
      g_value_set_int64 (value, mux->time_scale);
      break;
    case PROP_MIN_CLUSTER_DURATION:
      g_value_set_int64 (value, mux->min_cluster_duration);
      break;
    case PROP_MAX_CLUSTER_DURATION:
      g_value_set_int64 (value, mux->max_cluster_duration);
      break;
    case PROP_OFFSET_TO_ZERO:
      g_value_set_boolean (value, mux->offset_to_zero);
      break;
    case PROP_CREATION_TIME:
      g_value_set_boxed (value, mux->creation_time);
      break;
    case PROP_CLUSTER_TIMESTAMP_OFFSET:
      g_value_set_uint64 (value, mux->cluster_timestamp_offset);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
