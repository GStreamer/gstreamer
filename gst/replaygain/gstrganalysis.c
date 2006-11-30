/* GStreamer ReplayGain analysis
 *
 * Copyright (C) 2006 Rene Stadler <mail@renestadler.de>
 * 
 * gstrganalysis.c: Element that performs the ReplayGain analysis
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1 of
 * the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */

/**
 * SECTION:element-rganalysis
 *
 * <refsect2>
 * <para>
 * GstRgAnalysis analyzes raw audio sample data in accordance with the
 * proposed <ulink url="http://replaygain.org">ReplayGain
 * standard</ulink> for calculating the ideal replay gain for music
 * tracks and albums.  The element is designed as a pass-through
 * filter that never modifies any data.  As it receives an EOS event,
 * it finalizes the ongoing analysis and generates a tag list
 * containing the results.  It is sent downstream with a TAG event and
 * posted on the message bus with a TAG message.  The EOS event is
 * forwarded as normal afterwards.  Result tag lists at least contain
 * the tags #GST_TAG_TRACK_GAIN and #GST_TAG_TRACK_PEAK.
 * </para>
 * <title>Album processing</title>
 * <para>
 * Analyzing several streams sequentially and assigning them a common
 * result gain is known as "album processing".  If this gain is used
 * during playback (by switching to "album mode"), all tracks receive
 * the same amplification.  This keeps the relative volume levels
 * between the tracks intact.  To enable this, set the <link
 * linkend="GstRgAnalysis--num-tracks">num-tracks</link> property to
 * the number of streams that will be processed as album tracks.
 * Every time an EOS event is received, the value of this property
 * will be decremented by one.  As it reaches zero, it is assumed that
 * the last track of the album finished.  The tag list for the final
 * stream will contain the additional tags #GST_TAG_ALBUM_GAIN and
 * #GST_TAG_ALBUM_PEAK.  All other streams just get the two track tags
 * posted because the values for the album tags are not known before
 * all tracks are analyzed.  Applications need to make sure that the
 * album gain and peak values are also associated with the other
 * tracks when storing the results.  It is thus a bit more complex to
 * implement, but should not be avoided since the album gain is
 * generally more valuable for use during playback than the track
 * gain.
 * </para>
 * <title>Skipping processing</title> 
 * <para>
 * For assisting transcoder/converter applications, the element can
 * silently skip the processing of streams that already contain the
 * necessary meta data tags.  Data will flow as usual but the element
 * will not consume CPU time and will not generate result tags.  To
 * enable possible skipping, set the <link
 * linkend="GstRgAnalysis--forced">forced</link> property to #FALSE.
 * If used in conjunction with album processing, the element will skip
 * the number of remaining album tracks if a full set of tags is found
 * for the first track.  If a subsequent track of the album is missing
 * tags, processing cannot start again.  If this is undesired, your
 * application has to scan all files beforehand and enable forcing of
 * processing if needed.
 * </para>
 * <title>Tips</title>
 * <itemizedlist>
 * <listitem><para>
 * Because the generated metadata tags become available at the end of
 * streams, downstream muxer and encoder elements are normally unable
 * to save them in their output since they generally save metadata in
 * the file header.  Therefore, it is often necessary that
 * applications read the results in a bus event handler for the tag
 * message.  Obtaining the values this way is always needed for album
 * processing since the album gain and peak values need to be
 * associated with all tracks of an album, not just the last one.
 * </para></listitem>
 * <listitem><para>
 * To perform album processing, the element has to preserve data
 * between streams.  This cannot survive a state change to the NULL or
 * READY state.  If you change your pipeline's state to NULL or READY
 * between tracks, lock the rganalysis element's state using
 * gst_element_set_locked_state() when it is in PAUSED or PLAYING.  As
 * with any other element, don't forget to unlock it again and set it
 * to the NULL state before dropping the last reference.
 * </para></listitem>
 * <listitem><para>
 * If the total number of album tracks is unknown beforehand, set the
 * num-tracks property to some large value like #G_MAXINT (or set it
 * to >= 2 before each track starts).  Before the last track ends, set
 * the property value to 1.
 * </para></listitem>
 * </itemizedlist>
 * <title>Compliance</title>
 * <para>
 * Analyzing the ReplayGain pink noise reference waveform will compute
 * a result of +6.00dB instead of the expected 0.00dB because the
 * default reference level is 89dB.  To obtain values as lined out in
 * the original proposal of ReplayGain, set the <link
 * linkend="GstRgAnalysis--reference-level">reference-level</link>
 * property to 83.  Almost all software uses 89dB as a reference
 * however, which works against the tendency of the algorithm to
 * advise to drastically lower the volume of music with a highly
 * compressed dynamic range and high average output levels.  This
 * tendency is normally to be fought during playback (if wanted), by
 * using a default pre-amp value of at least +6.00dB.  At one point,
 * the majority of analyzer implementations switched to 89dB which
 * moved this adjustment to the analyzing/metadata writing process.
 * This change has been acknowledged by the author of the ReplayGain
 * proposal, however at the time of this writing, the webpage is still
 * not updated.
 * </para>
 * <title>Example launch lines</title>
 * <para>Analyze a simple test waveform:</para>
 * <programlisting>
 * gst-launch -t audiotestsrc wave=sine num-buffers=512 ! rganalysis ! fakesink
 * </programlisting>
 * <para>Analyze a given file:</para>
 * <programlisting>
 * gst-launch -t filesrc location="Some file.ogg" ! decodebin ! audioconvert ! audioresample ! rganalysis ! fakesink
 * </programlisting>
 * <para>Analyze the pink noise reference file:</para>
 * <programlisting>
 * gst-launch -t gnomevfssrc location=http://replaygain.hydrogenaudio.org/ref_pink.wav ! wavparse ! rganalysis ! fakesink
 * </programlisting>
 * <title>Acknowledgements</title>
 * <para>
 * This element is based on code used in the <ulink
 * url="http://sjeng.org/vorbisgain.html">vorbisgain</ulink> program
 * and many others.  The relevant parts are copyrighted by David
 * Robinson, Glen Sawyer and Frank Klemm.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <gst/gst.h>
#include <gst/base/gstbasetransform.h>

#include "gstrganalysis.h"

GST_DEBUG_CATEGORY_STATIC (gst_rg_analysis_debug);
#define GST_CAT_DEFAULT gst_rg_analysis_debug

static const GstElementDetails rganalysis_details = {
  "ReplayGain analysis",
  "Filter/Analyzer/Audio",
  "Perform the ReplayGain analysis",
  "Ren\xc3\xa9 Stadler <mail@renestadler.de>"
};

/* Default property value. */
#define FORCED_DEFAULT TRUE

enum
{
  PROP_0,
  PROP_NUM_TRACKS,
  PROP_FORCED,
  PROP_REFERENCE_LEVEL
};

/* The ReplayGain algorithm is intended for use with mono and stereo
 * audio.  The used implementation has filter coefficients for the
 * "usual" sample rates in the 8000 to 48000 Hz range. */
#define REPLAY_GAIN_CAPS                                                \
  "channels = (int) { 1, 2 }, "                                         \
  "rate = (int) { 8000, 11025, 12000, 16000, 22050, 24000, 32000, "     \
  "44100, 48000 }"

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS ("audio/x-raw-float, "
        "width = (int) 32, " "endianness = (int) BYTE_ORDER, "
        REPLAY_GAIN_CAPS "; "
        "audio/x-raw-int, "
        "width = (int) 16, " "depth = (int) [ 1, 16 ], "
        "signed = (boolean) true, " "endianness = (int) BYTE_ORDER, "
        REPLAY_GAIN_CAPS));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC, GST_PAD_ALWAYS, GST_STATIC_CAPS ("audio/x-raw-float, "
        "width = (int) 32, " "endianness = (int) BYTE_ORDER, "
        REPLAY_GAIN_CAPS "; "
        "audio/x-raw-int, "
        "width = (int) 16, " "depth = (int) [ 1, 16 ], "
        "signed = (boolean) true, " "endianness = (int) BYTE_ORDER, "
        REPLAY_GAIN_CAPS));

GST_BOILERPLATE (GstRgAnalysis, gst_rg_analysis, GstBaseTransform,
    GST_TYPE_BASE_TRANSFORM);

static void gst_rg_analysis_class_init (GstRgAnalysisClass * klass);
static void gst_rg_analysis_init (GstRgAnalysis * filter,
    GstRgAnalysisClass * gclass);

static void gst_rg_analysis_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_rg_analysis_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_rg_analysis_start (GstBaseTransform * base);
static gboolean gst_rg_analysis_set_caps (GstBaseTransform * base,
    GstCaps * incaps, GstCaps * outcaps);
static GstFlowReturn gst_rg_analysis_transform_ip (GstBaseTransform * base,
    GstBuffer * buf);
static gboolean gst_rg_analysis_event (GstBaseTransform * base,
    GstEvent * event);
static gboolean gst_rg_analysis_stop (GstBaseTransform * base);

static void gst_rg_analysis_handle_tags (GstRgAnalysis * filter,
    const GstTagList * tag_list);
static void gst_rg_analysis_handle_eos (GstRgAnalysis * filter);
static gboolean gst_rg_analysis_track_result (GstRgAnalysis * filter,
    GstTagList ** tag_list);
static gboolean gst_rg_analysis_album_result (GstRgAnalysis * filter,
    GstTagList ** tag_list);

static void
gst_rg_analysis_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &rganalysis_details);

  GST_DEBUG_CATEGORY_INIT (gst_rg_analysis_debug, "rganalysis", 0,
      "ReplayGain analysis element");
}

static void
gst_rg_analysis_class_init (GstRgAnalysisClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseTransformClass *trans_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_rg_analysis_set_property;
  gobject_class->get_property = gst_rg_analysis_get_property;

  g_object_class_install_property (gobject_class, PROP_NUM_TRACKS,
      g_param_spec_int ("num-tracks", "Number of album tracks",
          "Number of remaining tracks in the album",
          0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FORCED,
      g_param_spec_boolean ("forced", "Force processing",
          "Analyze streams even when ReplayGain tags exist",
          FORCED_DEFAULT, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_REFERENCE_LEVEL,
      g_param_spec_double ("reference-level", "Reference level",
          "Reference level in dB (83.0 for original proposal)",
          0.0, G_MAXDOUBLE, RG_REFERENCE_LEVEL, G_PARAM_READWRITE));

  trans_class = (GstBaseTransformClass *) klass;
  trans_class->start = GST_DEBUG_FUNCPTR (gst_rg_analysis_start);
  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_rg_analysis_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_rg_analysis_transform_ip);
  trans_class->event = GST_DEBUG_FUNCPTR (gst_rg_analysis_event);
  trans_class->stop = GST_DEBUG_FUNCPTR (gst_rg_analysis_stop);
  trans_class->passthrough_on_same_caps = TRUE;
}

static void
gst_rg_analysis_init (GstRgAnalysis * filter, GstRgAnalysisClass * gclass)
{
  filter->num_tracks = 0;
  filter->forced = FORCED_DEFAULT;
  filter->reference_level = RG_REFERENCE_LEVEL;

  filter->ctx = NULL;
  filter->analyze = NULL;
}

static void
gst_rg_analysis_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRgAnalysis *filter = GST_RG_ANALYSIS (object);

  switch (prop_id) {
    case PROP_NUM_TRACKS:
      filter->num_tracks = g_value_get_int (value);
      break;
    case PROP_FORCED:
      filter->forced = g_value_get_boolean (value);
      break;
    case PROP_REFERENCE_LEVEL:
      filter->reference_level = g_value_get_double (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_rg_analysis_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRgAnalysis *filter = GST_RG_ANALYSIS (object);

  switch (prop_id) {
    case PROP_NUM_TRACKS:
      g_value_set_int (value, filter->num_tracks);
      break;
    case PROP_FORCED:
      g_value_set_boolean (value, filter->forced);
      break;
    case PROP_REFERENCE_LEVEL:
      g_value_set_double (value, filter->reference_level);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_rg_analysis_start (GstBaseTransform * base)
{
  GstRgAnalysis *filter = GST_RG_ANALYSIS (base);

  filter->ignore_tags = FALSE;
  filter->skip = FALSE;
  filter->has_track_gain = FALSE;
  filter->has_track_peak = FALSE;
  filter->has_album_gain = FALSE;
  filter->has_album_peak = FALSE;

  filter->ctx = rg_analysis_new ();
  filter->analyze = NULL;

  GST_DEBUG_OBJECT (filter, "Started");

  return TRUE;
}

static gboolean
gst_rg_analysis_set_caps (GstBaseTransform * base, GstCaps * in_caps,
    GstCaps * out_caps)
{
  GstRgAnalysis *filter = GST_RG_ANALYSIS (base);
  GstStructure *structure;
  const gchar *mime_type;
  gint n_channels, sample_rate, sample_bit_size, sample_size;

  g_return_val_if_fail (filter->ctx != NULL, FALSE);

  GST_DEBUG_OBJECT (filter,
      "set_caps in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT,
      in_caps, out_caps);

  structure = gst_caps_get_structure (in_caps, 0);
  mime_type = gst_structure_get_name (structure);

  if (!gst_structure_get_int (structure, "width", &sample_bit_size)
      || !gst_structure_get_int (structure, "channels", &n_channels)
      || !gst_structure_get_int (structure, "rate", &sample_rate))
    goto invalid_format;

  if (!rg_analysis_set_sample_rate (filter->ctx, sample_rate))
    goto invalid_format;

  if (sample_bit_size % 8 != 0)
    goto invalid_format;
  sample_size = sample_bit_size / 8;

  if (strcmp (mime_type, "audio/x-raw-float") == 0) {

    if (sample_size != sizeof (gfloat))
      goto invalid_format;

    /* The depth is not variable for float formats of course.  It just
     * makes the transform function nice and simple if the
     * rg_analysis_analyze_* functions have a common signature. */
    filter->depth = sizeof (gfloat) * 8;

    if (n_channels == 1)
      filter->analyze = rg_analysis_analyze_mono_float;
    else if (n_channels == 2)
      filter->analyze = rg_analysis_analyze_stereo_float;
    else
      goto invalid_format;

  } else if (strcmp (mime_type, "audio/x-raw-int") == 0) {

    if (sample_size != sizeof (gint16))
      goto invalid_format;

    if (!gst_structure_get_int (structure, "depth", &filter->depth))
      goto invalid_format;
    if (filter->depth < 1 || filter->depth > 16)
      goto invalid_format;

    if (n_channels == 1)
      filter->analyze = rg_analysis_analyze_mono_int16;
    else if (n_channels == 2)
      filter->analyze = rg_analysis_analyze_stereo_int16;
    else
      goto invalid_format;

  } else {

    goto invalid_format;
  }

  return TRUE;

  /* Errors. */
invalid_format:
  {
    filter->analyze = NULL;
    GST_ELEMENT_ERROR (filter, CORE, NEGOTIATION,
        ("Invalid incoming caps: %" GST_PTR_FORMAT, in_caps), (NULL));
    return FALSE;
  }
}

static GstFlowReturn
gst_rg_analysis_transform_ip (GstBaseTransform * base, GstBuffer * buf)
{
  GstRgAnalysis *filter = GST_RG_ANALYSIS (base);

  g_return_val_if_fail (filter->ctx != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (filter->analyze != NULL, GST_FLOW_ERROR);

  if (filter->skip)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (filter, "Processing buffer of size %u",
      GST_BUFFER_SIZE (buf));

  filter->analyze (filter->ctx, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf),
      filter->depth);

  return GST_FLOW_OK;
}

static gboolean
gst_rg_analysis_event (GstBaseTransform * base, GstEvent * event)
{
  GstRgAnalysis *filter = GST_RG_ANALYSIS (base);

  g_return_val_if_fail (filter->ctx != NULL, TRUE);

  switch (GST_EVENT_TYPE (event)) {

    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (filter, "Received EOS event");

      gst_rg_analysis_handle_eos (filter);

      GST_DEBUG_OBJECT (filter, "Passing on EOS event");

      break;
    }
    case GST_EVENT_TAG:
    {
      GstTagList *tag_list;

      /* The reference to the tag list is borrowed. */
      gst_event_parse_tag (event, &tag_list);
      gst_rg_analysis_handle_tags (filter, tag_list);

      break;
    }
    default:
      break;
  }

  return GST_BASE_TRANSFORM_CLASS (parent_class)->event (base, event);
}

static gboolean
gst_rg_analysis_stop (GstBaseTransform * base)
{
  GstRgAnalysis *filter = GST_RG_ANALYSIS (base);

  g_return_val_if_fail (filter->ctx != NULL, FALSE);

  rg_analysis_destroy (filter->ctx);
  filter->ctx = NULL;

  GST_DEBUG_OBJECT (filter, "Stopped");

  return TRUE;
}

static void
gst_rg_analysis_handle_tags (GstRgAnalysis * filter,
    const GstTagList * tag_list)
{
  gboolean album_processing = (filter->num_tracks > 0);
  gdouble dummy;

  if (!album_processing)
    filter->ignore_tags = FALSE;

  if (filter->skip && album_processing) {
    GST_INFO_OBJECT (filter, "Ignoring TAG event: Skipping album");
    return;
  } else if (filter->skip) {
    GST_INFO_OBJECT (filter, "Ignoring TAG event: Skipping track");
    return;
  } else if (filter->ignore_tags) {
    GST_INFO_OBJECT (filter, "Ignoring TAG event: Cannot skip anyways");
    return;
  }

  filter->has_track_gain |= gst_tag_list_get_double (tag_list,
      GST_TAG_TRACK_GAIN, &dummy);
  filter->has_track_peak |= gst_tag_list_get_double (tag_list,
      GST_TAG_TRACK_PEAK, &dummy);
  filter->has_album_gain |= gst_tag_list_get_double (tag_list,
      GST_TAG_ALBUM_GAIN, &dummy);
  filter->has_album_peak |= gst_tag_list_get_double (tag_list,
      GST_TAG_ALBUM_PEAK, &dummy);

  if (!(filter->has_track_gain && filter->has_track_peak)) {
    GST_INFO_OBJECT (filter, "Track tags not complete yet");
    return;
  }

  if (album_processing && !(filter->has_album_gain && filter->has_album_peak)) {
    GST_INFO_OBJECT (filter, "Album tags not complete yet");
    return;
  }

  if (filter->forced) {
    GST_INFO_OBJECT (filter,
        "Existing tags are sufficient, but processing anyway (forced)");
    return;
  }

  filter->skip = TRUE;
  rg_analysis_reset (filter->ctx);

  if (!album_processing)
    GST_INFO_OBJECT (filter,
        "Existing tags are sufficient, will not process this track");
  else
    GST_INFO_OBJECT (filter,
        "Existing tags are sufficient, will not process this album");
}

static void
gst_rg_analysis_handle_eos (GstRgAnalysis * filter)
{
  gboolean album_processing = (filter->num_tracks > 0);
  gboolean album_finished = (filter->num_tracks == 1);
  gboolean album_skipping = album_processing && filter->skip;

  filter->has_track_gain = FALSE;
  filter->has_track_peak = FALSE;

  if (album_finished) {
    filter->ignore_tags = FALSE;
    filter->skip = FALSE;
    filter->has_album_gain = FALSE;
    filter->has_album_peak = FALSE;
  } else if (!album_skipping) {
    filter->skip = FALSE;
  }

  /* We might have just fully processed a track because it has
   * incomplete tags.  If we do album processing and allow skipping
   * (not forced), prevent switching to skipping if a later track with
   * full tags comes along: */
  if (!filter->forced && album_processing && !album_finished)
    filter->ignore_tags = TRUE;

  if (!filter->skip) {
    GstTagList *tag_list = NULL;
    gboolean track_success;
    gboolean album_success = FALSE;

    track_success = gst_rg_analysis_track_result (filter, &tag_list);

    if (album_finished)
      album_success = gst_rg_analysis_album_result (filter, &tag_list);
    else if (!album_processing)
      rg_analysis_reset_album (filter->ctx);

    if (track_success || album_success) {
      GST_DEBUG_OBJECT (filter, "Posting tag list with results");
      /* This steals our reference to the list: */
      gst_element_found_tags_for_pad (GST_ELEMENT (filter),
          GST_BASE_TRANSFORM_SRC_PAD (GST_BASE_TRANSFORM (filter)), tag_list);
    }
  }

  if (album_processing) {
    filter->num_tracks--;

    if (!album_finished)
      GST_INFO_OBJECT (filter, "Album not finished yet (num-tracks is now %u)",
          filter->num_tracks);
    else
      GST_INFO_OBJECT (filter, "Album finished (num-tracks is now 0)");
  }

  if (album_processing)
    g_object_notify (G_OBJECT (filter), "num-tracks");
}

static gboolean
gst_rg_analysis_track_result (GstRgAnalysis * filter, GstTagList ** tag_list)
{
  gboolean track_success;
  gdouble track_gain, track_peak;

  track_success = rg_analysis_track_result (filter->ctx, &track_gain,
      &track_peak);

  if (track_success) {
    track_gain += filter->reference_level - RG_REFERENCE_LEVEL;
    GST_INFO_OBJECT (filter, "Track gain is %+.2f dB, peak %.6f", track_gain,
        track_peak);
  } else {
    GST_INFO_OBJECT (filter, "Track was too short to analyze");
  }

  if (track_success) {
    if (*tag_list == NULL)
      *tag_list = gst_tag_list_new ();
    gst_tag_list_add (*tag_list, GST_TAG_MERGE_APPEND,
        GST_TAG_TRACK_PEAK, track_peak, GST_TAG_TRACK_GAIN, track_gain, NULL);
  }

  return track_success;
}

static gboolean
gst_rg_analysis_album_result (GstRgAnalysis * filter, GstTagList ** tag_list)
{
  gboolean album_success;
  gdouble album_gain, album_peak;

  album_success = rg_analysis_album_result (filter->ctx, &album_gain,
      &album_peak);

  if (album_success) {
    album_gain += filter->reference_level - RG_REFERENCE_LEVEL;
    GST_INFO_OBJECT (filter, "Album gain is %+.2f dB, peak %.6f", album_gain,
        album_peak);
  } else {
    GST_INFO_OBJECT (filter, "Album was too short to analyze");
  }

  if (album_success) {
    if (*tag_list == NULL)
      *tag_list = gst_tag_list_new ();
    gst_tag_list_add (*tag_list, GST_TAG_MERGE_APPEND,
        GST_TAG_ALBUM_PEAK, album_peak, GST_TAG_ALBUM_GAIN, album_gain, NULL);
  }

  return album_success;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rganalysis", GST_RANK_NONE,
      GST_TYPE_RG_ANALYSIS);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, "replaygain",
    "ReplayGain analysis", plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN);
