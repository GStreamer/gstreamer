/* Quicktime muxer plugin for GStreamer
 * Copyright (C) 2008-2010 Thiago Santos <thiagoss@embedded.ufcg.edu.br>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
 * Copyright (C) 2010 Nokia Corporation. All rights reserved.
 * Copyright (C) 2014 Jan Schmidt <jan@centricular.com>
 * Contact: Stefan Kost <stefan.kost@nokia.com>

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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


/**
 * SECTION:element-qtmux
 * @short_description: Muxer for quicktime(.mov) files
 *
 * This element merges streams (audio and video) into QuickTime(.mov) files.
 *
 * The following background intends to explain why various similar muxers
 * are present in this plugin.
 *
 * The <ulink url="http://www.apple.com/quicktime/resources/qtfileformat.pdf">
 * QuickTime file format specification</ulink> served as basis for the MP4 file
 * format specification (mp4mux), and as such the QuickTime file structure is
 * nearly identical to the so-called ISO Base Media file format defined in
 * ISO 14496-12 (except for some media specific parts).
 * In turn, the latter ISO Base Media format was further specialized as a
 * Motion JPEG-2000 file format in ISO 15444-3 (mj2mux)
 * and in various 3GPP(2) specs (gppmux).
 * The fragmented file features defined (only) in ISO Base Media are used by
 * ISMV files making up (a.o.) Smooth Streaming (ismlmux).
 *
 * A few properties (#GstQTMux:movie-timescale, #GstQTMux:trak-timescale) allow
 * adjusting some technical parameters, which might be useful in (rare) cases to
 * resolve compatibility issues in some situations.
 *
 * Some other properties influence the result more fundamentally.
 * A typical mov/mp4 file's metadata (aka moov) is located at the end of the
 * file, somewhat contrary to this usually being called "the header".
 * However, a #GstQTMux:faststart file will (with some effort) arrange this to
 * be located near start of the file, which then allows it e.g. to be played
 * while downloading. Alternatively, rather than having one chunk of metadata at
 * start (or end), there can be some metadata at start and most of the other
 * data can be spread out into fragments of #GstQTMux:fragment-duration.
 * If such fragmented layout is intended for streaming purposes, then
 * #GstQTMux:streamable allows foregoing to add index metadata (at the end of
 * file).
 *
 * When the maximum duration to be recorded can be known in advance, #GstQTMux
 * also supports a 'Robust Muxing' mode. In robust muxing mode,  space for the
 * headers are reserved at the start of muxing, and rewritten at a configurable
 * interval, so that the output file is always playable, even if the recording
 * is interrupted uncleanly by a crash. Robust muxing mode requires a seekable
 * output, such as filesink, because it needs to rewrite the start of the file.
 *
 * To enable robust muxing mode, set the #GstQTMux::reserved-moov-update-period
 * and #GstQTMux::reserved-max-duration property. Also present is the
 * #GstQTMux::reserved-bytes-per-sec property, which can be increased if
 * for some reason the default is not large enough and the initial reserved
 * space for headers is too small. Applications can monitor the
 * #GstQTMux::reserved-duration-remaining property to see how close to full
 * the reserved space is becoming.
 *
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 v4l2src num-buffers=500 ! video/x-raw,width=320,height=240 ! videoconvert ! qtmux ! filesink location=video.mov
 * ]|
 * Records a video stream captured from a v4l2 device and muxes it into a qt file.
 * </refsect2>
 */

/*
 * Based on avimux
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib/gstdio.h>

#include <gst/gst.h>
#include <gst/base/gstcollectpads.h>
#include <gst/base/gstbytereader.h>
#include <gst/base/gstbitreader.h>
#include <gst/audio/audio.h>
#include <gst/video/video.h>
#include <gst/tag/tag.h>
#include <gst/pbutils/pbutils.h>

#include <sys/types.h>
#ifdef G_OS_WIN32
#include <io.h>                 /* lseek, open, close, read */
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#endif

#ifdef _MSC_VER
#define ftruncate g_win32_ftruncate
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include "gstqtmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_qt_mux_debug);
#define GST_CAT_DEFAULT gst_qt_mux_debug

/* Hacker notes.
 *
 * The basic building blocks of MP4 files are:
 *  - an 'ftyp' box at the very start
 *  - an 'mdat' box which contains the raw audio/video/subtitle data;
 *    this is just a bunch of bytes, completely unframed and possibly
 *    unordered with no additional meta-information
 *  - a 'moov' box that contains information about the different streams
 *    and what they contain, as well as sample tables for each stream
 *    that tell the demuxer where in the mdat box each buffer/sample is
 *    and what its duration/timestamp etc. is, and whether it's a
 *    keyframe etc.
 * Additionally, fragmented MP4 works by writing chunks of data in
 * pairs of 'moof' and 'mdat' boxes:
 *  - 'moof' boxes, header preceding each mdat fragment describing the
 *    contents, like a moov but only for that fragment.
 *  - a 'mfra' box for Fragmented MP4, which is written at the end and
 *    contains a summary of all fragments and seek tables.
 *
 * Currently mp4mux can work in 4 different modes / generate 4 types
 * of output files/streams:
 *
 * - Normal mp4: mp4mux will write a little ftyp identifier at the
 *   beginning, then start an mdat box into which it will write all the
 *   sample data. At EOS it will then write the moov header with track
 *   headers and sample tables at the end of the file, and rewrite the
 *   start of the file to fix up the mdat box size at the beginning.
 *   It has to wait for EOS to write the moov (which includes the
 *   sample tables) because it doesn't know how much space those
 *   tables will be. The output downstream must be seekable to rewrite
 *   the mdat box at EOS.
 *
 * - Fragmented mp4: moov header with track headers at start
 *   but no sample table, followed by N fragments, each containing
 *   track headers with sample tables followed by some data. Downstream
 *   does not need to be seekable if the 'streamable' flag is TRUE,
 *   as the final mfra and total duration will be omitted.
 *
 * - Fast-start mp4: the goal here is to create a file where the moov
 *   headers are at the beginning; what mp4mux will do is write all
 *   sample data into a temp file and build moov header plus sample
 *   tables in memory and then when EOS comes, it will push out the
 *   moov header plus sample tables at the beginning, followed by the
 *   mdat sample data at the end which is read in from the temp file
 *   Files created in this mode are better for streaming over the
 *   network, since the client doesn't have to seek to the end of the
 *   file to get the headers, but it requires copying all sample data
 *   out of the temp file at EOS, which can be expensive. Downstream does
 *   not need to be seekable, because of the use of the temp file.
 *
 * - Robust Muxing mode: In this mode, qtmux uses the reserved-max-duration
 *   and reserved-moov-update-period properties to reserve free space
 *   at the start of the file and periodically write the MOOV atom out
 *   to it. That means that killing the muxing at any point still
 *   results in a playable file, at the cost of wasting some amount of
 *   free space at the start of file. The approximate recording duration
 *   has to be known in advance to estimate how much free space to reserve
 *   for the moov, and the downstream must be seekable.
 *   If the moov header grows larger than the reserved space, an error
 *   is generated - so it's better to over-estimate the amount of space
 *   to reserve. To ensure the file is playable at any point, the moov
 *   is updated using a 'ping-pong' strategy, so the output is never in
 *   an invalid state.
 */

#ifndef GST_REMOVE_DEPRECATED
enum
{
  DTS_METHOD_DD,
  DTS_METHOD_REORDER,
  DTS_METHOD_ASC
};

static GType
gst_qt_mux_dts_method_get_type (void)
{
  static GType gst_qt_mux_dts_method = 0;

  if (!gst_qt_mux_dts_method) {
    static const GEnumValue dts_methods[] = {
      {DTS_METHOD_DD, "delta/duration", "dd"},
      {DTS_METHOD_REORDER, "reorder", "reorder"},
      {DTS_METHOD_ASC, "ascending", "asc"},
      {0, NULL, NULL},
    };

    gst_qt_mux_dts_method =
        g_enum_register_static ("GstQTMuxDtsMethods", dts_methods);
  }

  return gst_qt_mux_dts_method;
}

#define GST_TYPE_QT_MUX_DTS_METHOD \
  (gst_qt_mux_dts_method_get_type ())
#endif

/* QTMux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_MOVIE_TIMESCALE,
  PROP_TRAK_TIMESCALE,
  PROP_FAST_START,
  PROP_FAST_START_TEMP_FILE,
  PROP_MOOV_RECOV_FILE,
  PROP_FRAGMENT_DURATION,
  PROP_STREAMABLE,
  PROP_RESERVED_MAX_DURATION,
  PROP_RESERVED_DURATION_REMAINING,
  PROP_RESERVED_MOOV_UPDATE_PERIOD,
  PROP_RESERVED_BYTES_PER_SEC,
#ifndef GST_REMOVE_DEPRECATED
  PROP_DTS_METHOD,
#endif
  PROP_DO_CTTS,
};

/* some spare for header size as well */
#define MDAT_LARGE_FILE_LIMIT           ((guint64) 1024 * 1024 * 1024 * 2)

#define DEFAULT_MOVIE_TIMESCALE         1800
#define DEFAULT_TRAK_TIMESCALE          0
#define DEFAULT_DO_CTTS                 TRUE
#define DEFAULT_FAST_START              FALSE
#define DEFAULT_FAST_START_TEMP_FILE    NULL
#define DEFAULT_MOOV_RECOV_FILE         NULL
#define DEFAULT_FRAGMENT_DURATION       0
#define DEFAULT_STREAMABLE              TRUE
#ifndef GST_REMOVE_DEPRECATED
#define DEFAULT_DTS_METHOD              DTS_METHOD_REORDER
#endif
#define DEFAULT_RESERVED_MAX_DURATION   GST_CLOCK_TIME_NONE
#define DEFAULT_RESERVED_MOOV_UPDATE_PERIOD   GST_CLOCK_TIME_NONE
#define DEFAULT_RESERVED_BYTES_PER_SEC_PER_TRAK 550

static void gst_qt_mux_finalize (GObject * object);

static GstStateChangeReturn gst_qt_mux_change_state (GstElement * element,
    GstStateChange transition);

/* property functions */
static void gst_qt_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_qt_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

/* pad functions */
static GstPad *gst_qt_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name, const GstCaps * caps);
static void gst_qt_mux_release_pad (GstElement * element, GstPad * pad);

/* event */
static gboolean gst_qt_mux_sink_event (GstCollectPads * pads,
    GstCollectData * data, GstEvent * event, gpointer user_data);

static GstFlowReturn gst_qt_mux_handle_buffer (GstCollectPads * pads,
    GstCollectData * cdata, GstBuffer * buf, gpointer user_data);
static GstFlowReturn gst_qt_mux_add_buffer (GstQTMux * qtmux, GstQTPad * pad,
    GstBuffer * buf);

static GstFlowReturn
gst_qt_mux_robust_recording_rewrite_moov (GstQTMux * qtmux);

static GstElementClass *parent_class = NULL;

static void
gst_qt_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstQTMuxClass *klass = (GstQTMuxClass *) g_class;
  GstQTMuxClassParams *params;
  GstPadTemplate *videosinktempl, *audiosinktempl, *subtitlesinktempl;
  GstPadTemplate *srctempl;
  gchar *longname, *description;

  params =
      (GstQTMuxClassParams *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (g_class),
      GST_QT_MUX_PARAMS_QDATA);
  g_assert (params != NULL);

  /* construct the element details struct */
  longname = g_strdup_printf ("%s Muxer", params->prop->long_name);
  description = g_strdup_printf ("Multiplex audio and video into a %s file",
      params->prop->long_name);
  gst_element_class_set_static_metadata (element_class, longname,
      "Codec/Muxer", description,
      "Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>");
  g_free (longname);
  g_free (description);

  /* pad templates */
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, params->src_caps);
  gst_element_class_add_pad_template (element_class, srctempl);

  if (params->audio_sink_caps) {
    audiosinktempl = gst_pad_template_new ("audio_%u",
        GST_PAD_SINK, GST_PAD_REQUEST, params->audio_sink_caps);
    gst_element_class_add_pad_template (element_class, audiosinktempl);
  }

  if (params->video_sink_caps) {
    videosinktempl = gst_pad_template_new ("video_%u",
        GST_PAD_SINK, GST_PAD_REQUEST, params->video_sink_caps);
    gst_element_class_add_pad_template (element_class, videosinktempl);
  }

  if (params->subtitle_sink_caps) {
    subtitlesinktempl = gst_pad_template_new ("subtitle_%u",
        GST_PAD_SINK, GST_PAD_REQUEST, params->subtitle_sink_caps);
    gst_element_class_add_pad_template (element_class, subtitlesinktempl);
  }

  klass->format = params->prop->format;
}

static void
gst_qt_mux_class_init (GstQTMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GParamFlags streamable_flags;
  const gchar *streamable_desc;
  gboolean streamable;
#define STREAMABLE_DESC "If set to true, the output should be as if it is to "\
  "be streamed and hence no indexes written or duration written."

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_qt_mux_finalize;
  gobject_class->get_property = gst_qt_mux_get_property;
  gobject_class->set_property = gst_qt_mux_set_property;

  streamable_flags = G_PARAM_READWRITE | G_PARAM_CONSTRUCT;
  if (klass->format == GST_QT_MUX_FORMAT_ISML) {
    streamable_desc = STREAMABLE_DESC;
    streamable = DEFAULT_STREAMABLE;
  } else {
    streamable_desc =
        STREAMABLE_DESC " (DEPRECATED, only valid for fragmented MP4)";
    streamable_flags |= G_PARAM_DEPRECATED;
    streamable = FALSE;
  }

  g_object_class_install_property (gobject_class, PROP_MOVIE_TIMESCALE,
      g_param_spec_uint ("movie-timescale", "Movie timescale",
          "Timescale to use in the movie (units per second)",
          1, G_MAXUINT32, DEFAULT_MOVIE_TIMESCALE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TRAK_TIMESCALE,
      g_param_spec_uint ("trak-timescale", "Track timescale",
          "Timescale to use for the tracks (units per second, 0 is automatic)",
          0, G_MAXUINT32, DEFAULT_TRAK_TIMESCALE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DO_CTTS,
      g_param_spec_boolean ("presentation-time",
          "Include presentation-time info",
          "Calculate and include presentation/composition time "
          "(in addition to decoding time)", DEFAULT_DO_CTTS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
#ifndef GST_REMOVE_DEPRECATED
  g_object_class_install_property (gobject_class, PROP_DTS_METHOD,
      g_param_spec_enum ("dts-method", "dts-method",
          "Method to determine DTS time (DEPRECATED)",
          GST_TYPE_QT_MUX_DTS_METHOD, DEFAULT_DTS_METHOD,
          G_PARAM_DEPRECATED | G_PARAM_READWRITE | G_PARAM_CONSTRUCT |
          G_PARAM_STATIC_STRINGS));
#endif
  g_object_class_install_property (gobject_class, PROP_FAST_START,
      g_param_spec_boolean ("faststart", "Format file to faststart",
          "If the file should be formatted for faststart (headers first)",
          DEFAULT_FAST_START, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FAST_START_TEMP_FILE,
      g_param_spec_string ("faststart-file", "File to use for storing buffers",
          "File that will be used temporarily to store data from the stream "
          "when creating a faststart file. If null a filepath will be "
          "created automatically", DEFAULT_FAST_START_TEMP_FILE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_MOOV_RECOV_FILE,
      g_param_spec_string ("moov-recovery-file",
          "File to store data for posterior moov atom recovery",
          "File to be used to store "
          "data for moov atom making movie file recovery possible in case "
          "of a crash during muxing. Null for disabled. (Experimental)",
          DEFAULT_MOOV_RECOV_FILE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FRAGMENT_DURATION,
      g_param_spec_uint ("fragment-duration", "Fragment duration",
          "Fragment durations in ms (produce a fragmented file if > 0)",
          0, G_MAXUINT32, klass->format == GST_QT_MUX_FORMAT_ISML ?
          2000 : DEFAULT_FRAGMENT_DURATION,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_STREAMABLE,
      g_param_spec_boolean ("streamable", "Streamable", streamable_desc,
          streamable, streamable_flags | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RESERVED_MAX_DURATION,
      g_param_spec_uint64 ("reserved-max-duration",
          "Reserved maximum file duration (ns)",
          "When set to a value > 0, reserves space for index tables at the "
          "beginning of the file.",
          0, G_MAXUINT64, DEFAULT_RESERVED_MAX_DURATION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_RESERVED_DURATION_REMAINING,
      g_param_spec_uint64 ("reserved-duration-remaining",
          "Report the approximate amount of remaining recording space (ns)",
          "Reports the approximate amount of remaining moov header space "
          "reserved using reserved-max-duration", 0, G_MAXUINT64, 0,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_RESERVED_MOOV_UPDATE_PERIOD,
      g_param_spec_uint64 ("reserved-moov-update-period",
          "Interval at which to update index tables (ns)",
          "When used with reserved-max-duration, periodically updates the "
          "index tables with information muxed so far.", 0, G_MAXUINT64,
          DEFAULT_RESERVED_MOOV_UPDATE_PERIOD,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_RESERVED_BYTES_PER_SEC,
      g_param_spec_uint ("reserved-bytes-per-sec",
          "Reserved MOOV bytes per second, per track",
          "Multiplier for converting reserved-max-duration into bytes of header to reserve, per second, per track",
          0, 10000, DEFAULT_RESERVED_BYTES_PER_SEC_PER_TRAK,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_qt_mux_request_new_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (gst_qt_mux_change_state);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (gst_qt_mux_release_pad);
}

static void
gst_qt_mux_pad_reset (GstQTPad * qtpad)
{
  qtpad->fourcc = 0;
  qtpad->is_out_of_order = FALSE;
  qtpad->sample_size = 0;
  qtpad->sync = FALSE;
  qtpad->last_dts = 0;
  qtpad->dts_adjustment = GST_CLOCK_TIME_NONE;
  qtpad->first_ts = GST_CLOCK_TIME_NONE;
  qtpad->first_dts = GST_CLOCK_TIME_NONE;
  qtpad->prepare_buf_func = NULL;
  qtpad->create_empty_buffer = NULL;
  qtpad->avg_bitrate = 0;
  qtpad->max_bitrate = 0;
  qtpad->total_duration = 0;
  qtpad->total_bytes = 0;
  qtpad->sparse = FALSE;

  qtpad->buf_head = 0;
  qtpad->buf_tail = 0;

  gst_buffer_replace (&qtpad->last_buf, NULL);

  if (qtpad->tags) {
    gst_tag_list_unref (qtpad->tags);
    qtpad->tags = NULL;
  }

  /* reference owned elsewhere */
  qtpad->trak = NULL;

  if (qtpad->traf) {
    atom_traf_free (qtpad->traf);
    qtpad->traf = NULL;
  }
  atom_array_clear (&qtpad->fragment_buffers);

  /* reference owned elsewhere */
  qtpad->tfra = NULL;
}

/*
 * Takes GstQTMux back to its initial state
 */
static void
gst_qt_mux_reset (GstQTMux * qtmux, gboolean alloc)
{
  GSList *walk;

  qtmux->state = GST_QT_MUX_STATE_NONE;
  qtmux->header_size = 0;
  qtmux->mdat_size = 0;
  qtmux->moov_pos = 0;
  qtmux->mdat_pos = 0;
  qtmux->longest_chunk = GST_CLOCK_TIME_NONE;
  qtmux->video_pads = 0;
  qtmux->audio_pads = 0;
  qtmux->fragment_sequence = 0;

  if (qtmux->ftyp) {
    atom_ftyp_free (qtmux->ftyp);
    qtmux->ftyp = NULL;
  }
  if (qtmux->moov) {
    atom_moov_free (qtmux->moov);
    qtmux->moov = NULL;
  }
  if (qtmux->mfra) {
    atom_mfra_free (qtmux->mfra);
    qtmux->mfra = NULL;
  }
  if (qtmux->fast_start_file) {
    fclose (qtmux->fast_start_file);
    g_remove (qtmux->fast_start_file_path);
    qtmux->fast_start_file = NULL;
  }
  if (qtmux->moov_recov_file) {
    fclose (qtmux->moov_recov_file);
    qtmux->moov_recov_file = NULL;
  }
  for (walk = qtmux->extra_atoms; walk; walk = g_slist_next (walk)) {
    AtomInfo *ainfo = (AtomInfo *) walk->data;
    ainfo->free_func (ainfo->atom);
    g_free (ainfo);
  }
  g_slist_free (qtmux->extra_atoms);
  qtmux->extra_atoms = NULL;

  GST_OBJECT_LOCK (qtmux);
  gst_tag_setter_reset_tags (GST_TAG_SETTER (qtmux));
  GST_OBJECT_UNLOCK (qtmux);

  /* reset pad data */
  for (walk = qtmux->sinkpads; walk; walk = g_slist_next (walk)) {
    GstQTPad *qtpad = (GstQTPad *) walk->data;
    gst_qt_mux_pad_reset (qtpad);

    /* hm, moov_free above yanked the traks away from us,
     * so do not free, but do clear */
    qtpad->trak = NULL;
  }

  if (alloc) {
    qtmux->moov = atom_moov_new (qtmux->context);
    /* ensure all is as nice and fresh as request_new_pad would provide it */
    for (walk = qtmux->sinkpads; walk; walk = g_slist_next (walk)) {
      GstQTPad *qtpad = (GstQTPad *) walk->data;

      qtpad->trak = atom_trak_new (qtmux->context);
      atom_moov_add_trak (qtmux->moov, qtpad->trak);
    }
  }

  qtmux->reserved_moov_size = 0;
  qtmux->last_moov_update = GST_CLOCK_TIME_NONE;
  qtmux->muxed_since_last_update = 0;
  qtmux->reserved_duration_remaining = GST_CLOCK_TIME_NONE;
}

static void
gst_qt_mux_init (GstQTMux * qtmux, GstQTMuxClass * qtmux_klass)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (qtmux_klass);
  GstPadTemplate *templ;

  templ = gst_element_class_get_pad_template (klass, "src");
  qtmux->srcpad = gst_pad_new_from_template (templ, "src");
  gst_pad_use_fixed_caps (qtmux->srcpad);
  gst_element_add_pad (GST_ELEMENT (qtmux), qtmux->srcpad);

  qtmux->sinkpads = NULL;
  qtmux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_buffer_function (qtmux->collect,
      GST_DEBUG_FUNCPTR (gst_qt_mux_handle_buffer), qtmux);
  gst_collect_pads_set_event_function (qtmux->collect,
      GST_DEBUG_FUNCPTR (gst_qt_mux_sink_event), qtmux);
  gst_collect_pads_set_clip_function (qtmux->collect,
      GST_DEBUG_FUNCPTR (gst_collect_pads_clip_running_time), qtmux);

  /* properties set to default upon construction */

  qtmux->reserved_max_duration = DEFAULT_RESERVED_MAX_DURATION;
  qtmux->reserved_moov_update_period = DEFAULT_RESERVED_MOOV_UPDATE_PERIOD;
  qtmux->reserved_bytes_per_sec_per_trak =
      DEFAULT_RESERVED_BYTES_PER_SEC_PER_TRAK;

  /* always need this */
  qtmux->context =
      atoms_context_new (gst_qt_mux_map_format_to_flavor (qtmux_klass->format));

  /* internals to initial state */
  gst_qt_mux_reset (qtmux, TRUE);
}


static void
gst_qt_mux_finalize (GObject * object)
{
  GstQTMux *qtmux = GST_QT_MUX_CAST (object);

  gst_qt_mux_reset (qtmux, FALSE);

  g_free (qtmux->fast_start_file_path);
  g_free (qtmux->moov_recov_file_path);

  atoms_context_free (qtmux->context);
  gst_object_unref (qtmux->collect);

  g_slist_free (qtmux->sinkpads);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstBuffer *
gst_qt_mux_prepare_jpc_buffer (GstQTPad * qtpad, GstBuffer * buf,
    GstQTMux * qtmux)
{
  GstBuffer *newbuf;
  GstMapInfo map;
  gsize size;

  GST_LOG_OBJECT (qtmux, "Preparing jpc buffer");

  if (buf == NULL)
    return NULL;

  size = gst_buffer_get_size (buf);
  newbuf = gst_buffer_new_and_alloc (size + 8);
  gst_buffer_copy_into (newbuf, buf, GST_BUFFER_COPY_ALL, 8, size);

  gst_buffer_map (newbuf, &map, GST_MAP_WRITE);
  GST_WRITE_UINT32_BE (map.data, map.size);
  GST_WRITE_UINT32_LE (map.data + 4, FOURCC_jp2c);

  gst_buffer_unmap (buf, &map);
  gst_buffer_unref (buf);

  return newbuf;
}

static GstBuffer *
gst_qt_mux_prepare_tx3g_buffer (GstQTPad * qtpad, GstBuffer * buf,
    GstQTMux * qtmux)
{
  GstBuffer *newbuf;
  GstMapInfo frommap;
  GstMapInfo tomap;
  gsize size;
  const guint8 *dataend;

  GST_LOG_OBJECT (qtmux, "Preparing tx3g buffer %" GST_PTR_FORMAT, buf);

  if (buf == NULL)
    return NULL;

  gst_buffer_map (buf, &frommap, GST_MAP_READ);

  dataend = memchr (frommap.data, 0, frommap.size);
  size = dataend ? dataend - frommap.data : frommap.size;
  newbuf = gst_buffer_new_and_alloc (size + 2);

  gst_buffer_map (newbuf, &tomap, GST_MAP_WRITE);

  GST_WRITE_UINT16_BE (tomap.data, size);
  memcpy (tomap.data + 2, frommap.data, size);

  gst_buffer_unmap (newbuf, &tomap);
  gst_buffer_unmap (buf, &frommap);

  gst_buffer_copy_into (newbuf, buf, GST_BUFFER_COPY_METADATA, 0, size);

  /* gst_buffer_copy_into is trying to be too clever and
   * won't copy duration when size is different */
  GST_BUFFER_DURATION (newbuf) = GST_BUFFER_DURATION (buf);

  gst_buffer_unref (buf);

  return newbuf;
}

static void
gst_qt_mux_pad_add_ac3_extension (GstQTMux * qtmux, GstQTPad * qtpad,
    guint8 fscod, guint8 frmsizcod, guint8 bsid, guint8 bsmod, guint8 acmod,
    guint8 lfe_on)
{
  AtomInfo *ext;

  g_return_if_fail (qtpad->trak_ste);

  ext = build_ac3_extension (fscod, bsid, bsmod, acmod, lfe_on, frmsizcod >> 1);        /* bitrate_code is inside frmsizcod */

  sample_table_entry_add_ext_atom (qtpad->trak_ste, ext);
}

static GstBuffer *
gst_qt_mux_prepare_parse_ac3_frame (GstQTPad * qtpad, GstBuffer * buf,
    GstQTMux * qtmux)
{
  GstMapInfo map;
  GstByteReader reader;
  guint off;

  if (!gst_buffer_map (buf, &map, GST_MAP_READ)) {
    GST_WARNING_OBJECT (qtpad->collect.pad, "Failed to map buffer");
    return buf;
  }

  if (G_UNLIKELY (map.size < 8))
    goto done;

  gst_byte_reader_init (&reader, map.data, map.size);
  off = gst_byte_reader_masked_scan_uint32 (&reader, 0xffff0000, 0x0b770000,
      0, map.size);

  if (off != -1) {
    GstBitReader bits;
    guint8 fscod, frmsizcod, bsid, bsmod, acmod, lfe_on;

    GST_DEBUG_OBJECT (qtpad->collect.pad, "Found ac3 sync point at offset: %u",
        off);

    gst_bit_reader_init (&bits, map.data, map.size);

    /* off + sync + crc */
    gst_bit_reader_skip_unchecked (&bits, off * 8 + 16 + 16);

    fscod = gst_bit_reader_get_bits_uint8_unchecked (&bits, 2);
    frmsizcod = gst_bit_reader_get_bits_uint8_unchecked (&bits, 6);
    bsid = gst_bit_reader_get_bits_uint8_unchecked (&bits, 5);
    bsmod = gst_bit_reader_get_bits_uint8_unchecked (&bits, 3);
    acmod = gst_bit_reader_get_bits_uint8_unchecked (&bits, 3);

    if ((acmod & 0x1) && (acmod != 0x1))        /* 3 front channels */
      gst_bit_reader_skip_unchecked (&bits, 2);
    if ((acmod & 0x4))          /* if a surround channel exists */
      gst_bit_reader_skip_unchecked (&bits, 2);
    if (acmod == 0x2)           /* if in 2/0 mode */
      gst_bit_reader_skip_unchecked (&bits, 2);

    lfe_on = gst_bit_reader_get_bits_uint8_unchecked (&bits, 1);

    gst_qt_mux_pad_add_ac3_extension (qtmux, qtpad, fscod, frmsizcod, bsid,
        bsmod, acmod, lfe_on);

    /* AC-3 spec says that those values should be constant for the
     * whole stream when muxed in mp4. We trust the input follows it */
    GST_DEBUG_OBJECT (qtpad->collect.pad, "Data parsed, removing "
        "prepare buffer function");
    qtpad->prepare_buf_func = NULL;
  }

done:
  gst_buffer_unmap (buf, &map);
  return buf;
}

static GstBuffer *
gst_qt_mux_create_empty_tx3g_buffer (GstQTPad * qtpad, gint64 duration)
{
  guint8 *data;

  data = g_malloc (2);
  GST_WRITE_UINT16_BE (data, 0);

  return gst_buffer_new_wrapped (data, 2);
}

static void
gst_qt_mux_add_mp4_tag (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  switch (gst_tag_get_type (tag)) {
      /* strings */
    case G_TYPE_STRING:
    {
      gchar *str = NULL;

      if (!gst_tag_list_get_string (list, tag, &str) || !str)
        break;
      GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s",
          GST_FOURCC_ARGS (fourcc), str);
      atom_udta_add_str_tag (udta, fourcc, str);
      g_free (str);
      break;
    }
      /* double */
    case G_TYPE_DOUBLE:
    {
      gdouble value;

      if (!gst_tag_list_get_double (list, tag, &value))
        break;
      GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %u",
          GST_FOURCC_ARGS (fourcc), (gint) value);
      atom_udta_add_uint_tag (udta, fourcc, 21, (gint) value);
      break;
    }
    case G_TYPE_UINT:
    {
      guint value = 0;
      if (tag2) {
        /* paired unsigned integers */
        guint count = 0;
        gboolean got_tag;

        got_tag = gst_tag_list_get_uint (list, tag, &value);
        got_tag = gst_tag_list_get_uint (list, tag2, &count) || got_tag;
        if (!got_tag)
          break;
        GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %u/%u",
            GST_FOURCC_ARGS (fourcc), value, count);
        atom_udta_add_uint_tag (udta, fourcc, 0,
            value << 16 | (count & 0xFFFF));
      } else {
        /* unpaired unsigned integers */
        if (!gst_tag_list_get_uint (list, tag, &value))
          break;
        GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %u",
            GST_FOURCC_ARGS (fourcc), value);
        atom_udta_add_uint_tag (udta, fourcc, 1, value);
      }
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }
}

static void
gst_qt_mux_add_mp4_date (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  GDate *date = NULL;
  GDateYear year;
  GDateMonth month;
  GDateDay day;
  gchar *str;

  g_return_if_fail (gst_tag_get_type (tag) == G_TYPE_DATE);

  if (!gst_tag_list_get_date (list, tag, &date) || !date)
    return;

  year = g_date_get_year (date);
  month = g_date_get_month (date);
  day = g_date_get_day (date);

  g_date_free (date);

  if (year == G_DATE_BAD_YEAR && month == G_DATE_BAD_MONTH &&
      day == G_DATE_BAD_DAY) {
    GST_WARNING_OBJECT (qtmux, "invalid date in tag");
    return;
  }

  str = g_strdup_printf ("%u-%u-%u", year, month, day);
  GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s",
      GST_FOURCC_ARGS (fourcc), str);
  atom_udta_add_str_tag (udta, fourcc, str);
  g_free (str);
}

static void
gst_qt_mux_add_mp4_cover (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  GValue value = { 0, };
  GstBuffer *buf;
  GstSample *sample;
  GstCaps *caps;
  GstStructure *structure;
  gint flags = 0;
  GstMapInfo map;

  g_return_if_fail (gst_tag_get_type (tag) == GST_TYPE_SAMPLE);

  if (!gst_tag_list_copy_value (&value, list, tag))
    return;

  sample = gst_value_get_sample (&value);

  if (!sample)
    goto done;

  buf = gst_sample_get_buffer (sample);
  if (!buf)
    goto done;

  caps = gst_sample_get_caps (sample);
  if (!caps) {
    GST_WARNING_OBJECT (qtmux, "preview image without caps");
    goto done;
  }

  GST_DEBUG_OBJECT (qtmux, "preview image caps %" GST_PTR_FORMAT, caps);

  structure = gst_caps_get_structure (caps, 0);
  if (gst_structure_has_name (structure, "image/jpeg"))
    flags = 13;
  else if (gst_structure_has_name (structure, "image/png"))
    flags = 14;

  if (!flags) {
    GST_WARNING_OBJECT (qtmux, "preview image format not supported");
    goto done;
  }

  gst_buffer_map (buf, &map, GST_MAP_READ);
  GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT
      " -> image size %" G_GSIZE_FORMAT "", GST_FOURCC_ARGS (fourcc), map.size);
  atom_udta_add_tag (udta, fourcc, flags, map.data, map.size);
  gst_buffer_unmap (buf, &map);
done:
  g_value_unset (&value);
}

static void
gst_qt_mux_add_3gp_str (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  gchar *str = NULL;
  guint number;

  g_return_if_fail (gst_tag_get_type (tag) == G_TYPE_STRING);
  g_return_if_fail (!tag2 || gst_tag_get_type (tag2) == G_TYPE_UINT);

  if (!gst_tag_list_get_string (list, tag, &str) || !str)
    return;

  if (tag2)
    if (!gst_tag_list_get_uint (list, tag2, &number))
      tag2 = NULL;

  if (!tag2) {
    GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s",
        GST_FOURCC_ARGS (fourcc), str);
    atom_udta_add_3gp_str_tag (udta, fourcc, str);
  } else {
    GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s/%d",
        GST_FOURCC_ARGS (fourcc), str, number);
    atom_udta_add_3gp_str_int_tag (udta, fourcc, str, number);
  }

  g_free (str);
}

static void
gst_qt_mux_add_3gp_date (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  GDate *date = NULL;
  GDateYear year;

  g_return_if_fail (gst_tag_get_type (tag) == G_TYPE_DATE);

  if (!gst_tag_list_get_date (list, tag, &date) || !date)
    return;

  year = g_date_get_year (date);
  g_date_free (date);

  if (year == G_DATE_BAD_YEAR) {
    GST_WARNING_OBJECT (qtmux, "invalid date in tag");
    return;
  }

  GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %d",
      GST_FOURCC_ARGS (fourcc), year);
  atom_udta_add_3gp_uint_tag (udta, fourcc, year);
}

static void
gst_qt_mux_add_3gp_location (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  gdouble latitude = -360, longitude = -360, altitude = 0;
  gchar *location = NULL;
  guint8 *data, *ddata;
  gint size = 0, len = 0;
  gboolean ret = FALSE;

  g_return_if_fail (strcmp (tag, GST_TAG_GEO_LOCATION_NAME) == 0);

  ret = gst_tag_list_get_string (list, tag, &location);
  ret |= gst_tag_list_get_double (list, GST_TAG_GEO_LOCATION_LONGITUDE,
      &longitude);
  ret |= gst_tag_list_get_double (list, GST_TAG_GEO_LOCATION_LATITUDE,
      &latitude);
  ret |= gst_tag_list_get_double (list, GST_TAG_GEO_LOCATION_ELEVATION,
      &altitude);

  if (!ret)
    return;

  if (location)
    len = strlen (location);
  size += len + 1 + 2;

  /* role + (long, lat, alt) + body + notes */
  size += 1 + 3 * 4 + 1 + 1;

  data = ddata = g_malloc (size);

  /* language tag */
  GST_WRITE_UINT16_BE (data, language_code (GST_QT_MUX_DEFAULT_TAG_LANGUAGE));
  /* location */
  if (location)
    memcpy (data + 2, location, len);
  GST_WRITE_UINT8 (data + 2 + len, 0);
  data += len + 1 + 2;
  /* role */
  GST_WRITE_UINT8 (data, 0);
  /* long, lat, alt */
#define QT_WRITE_SFP32(data, fp) GST_WRITE_UINT32_BE(data, (guint32) ((gint) (fp * 65536.0)))
  QT_WRITE_SFP32 (data + 1, longitude);
  QT_WRITE_SFP32 (data + 5, latitude);
  QT_WRITE_SFP32 (data + 9, altitude);
  /* neither astronomical body nor notes */
  GST_WRITE_UINT16_BE (data + 13, 0);

  GST_DEBUG_OBJECT (qtmux, "Adding tag 'loci'");
  atom_udta_add_3gp_tag (udta, fourcc, ddata, size);
  g_free (ddata);
}

static void
gst_qt_mux_add_3gp_keywords (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  gchar *keywords = NULL;
  guint8 *data, *ddata;
  gint size = 0, i;
  gchar **kwds;

  g_return_if_fail (strcmp (tag, GST_TAG_KEYWORDS) == 0);

  if (!gst_tag_list_get_string (list, tag, &keywords) || !keywords)
    return;

  kwds = g_strsplit (keywords, ",", 0);
  g_free (keywords);

  size = 0;
  for (i = 0; kwds[i]; i++) {
    /* size byte + null-terminator */
    size += strlen (kwds[i]) + 1 + 1;
  }

  /* language tag + count + keywords */
  size += 2 + 1;

  data = ddata = g_malloc (size);

  /* language tag */
  GST_WRITE_UINT16_BE (data, language_code (GST_QT_MUX_DEFAULT_TAG_LANGUAGE));
  /* count */
  GST_WRITE_UINT8 (data + 2, i);
  data += 3;
  /* keywords */
  for (i = 0; kwds[i]; ++i) {
    gint len = strlen (kwds[i]);

    GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s",
        GST_FOURCC_ARGS (fourcc), kwds[i]);
    /* size */
    GST_WRITE_UINT8 (data, len + 1);
    memcpy (data + 1, kwds[i], len + 1);
    data += len + 2;
  }

  g_strfreev (kwds);

  atom_udta_add_3gp_tag (udta, fourcc, ddata, size);
  g_free (ddata);
}

static gboolean
gst_qt_mux_parse_classification_string (GstQTMux * qtmux, const gchar * input,
    guint32 * p_fourcc, guint16 * p_table, gchar ** p_content)
{
  guint32 fourcc;
  gint table;
  gint size;
  const gchar *data;

  data = input;
  size = strlen (input);

  if (size < 4 + 3 + 1 + 1 + 1) {
    /* at least the minimum xxxx://y/z */
    GST_WARNING_OBJECT (qtmux, "Classification tag input (%s) too short, "
        "ignoring", input);
    return FALSE;
  }

  /* read the fourcc */
  memcpy (&fourcc, data, 4);
  size -= 4;
  data += 4;

  if (strncmp (data, "://", 3) != 0) {
    goto mismatch;
  }
  data += 3;
  size -= 3;

  /* read the table number */
  if (sscanf (data, "%d", &table) != 1) {
    goto mismatch;
  }
  if (table < 0) {
    GST_WARNING_OBJECT (qtmux, "Invalid table number in classification tag (%d)"
        ", table numbers should be positive, ignoring tag", table);
    return FALSE;
  }

  /* find the next / */
  while (size > 0 && data[0] != '/') {
    data += 1;
    size -= 1;
  }
  if (size == 0) {
    goto mismatch;
  }
  g_assert (data[0] == '/');

  /* skip the '/' */
  data += 1;
  size -= 1;
  if (size == 0) {
    goto mismatch;
  }

  /* read up the rest of the string */
  *p_content = g_strdup (data);
  *p_table = (guint16) table;
  *p_fourcc = fourcc;
  return TRUE;

mismatch:
  {
    GST_WARNING_OBJECT (qtmux, "Ignoring classification tag as "
        "input (%s) didn't match the expected entitycode://table/content",
        input);
    return FALSE;
  }
}

static void
gst_qt_mux_add_3gp_classification (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta, const char *tag, const char *tag2, guint32 fourcc)
{
  gchar *clsf_data = NULL;
  gint size = 0;
  guint32 entity = 0;
  guint16 table = 0;
  gchar *content = NULL;
  guint8 *data;

  g_return_if_fail (strcmp (tag, GST_TAG_3GP_CLASSIFICATION) == 0);

  if (!gst_tag_list_get_string (list, tag, &clsf_data) || !clsf_data)
    return;

  GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s",
      GST_FOURCC_ARGS (fourcc), clsf_data);

  /* parse the string, format is:
   * entityfourcc://table/content
   */
  gst_qt_mux_parse_classification_string (qtmux, clsf_data, &entity, &table,
      &content);
  g_free (clsf_data);
  /* +1 for the \0 */
  size = strlen (content) + 1;

  /* now we have everything, build the atom
   * atom description is at 3GPP TS 26.244 V8.2.0 (2009-09) */
  data = g_malloc (4 + 2 + 2 + size);
  GST_WRITE_UINT32_LE (data, entity);
  GST_WRITE_UINT16_BE (data + 4, (guint16) table);
  GST_WRITE_UINT16_BE (data + 6, 0);
  memcpy (data + 8, content, size);
  g_free (content);

  atom_udta_add_3gp_tag (udta, fourcc, data, 4 + 2 + 2 + size);
  g_free (data);
}

typedef void (*GstQTMuxAddUdtaTagFunc) (GstQTMux * mux,
    const GstTagList * list, AtomUDTA * udta, const char *tag,
    const char *tag2, guint32 fourcc);

/*
 * Struct to record mappings from gstreamer tags to fourcc codes
 */
typedef struct _GstTagToFourcc
{
  guint32 fourcc;
  const gchar *gsttag;
  const gchar *gsttag2;
  const GstQTMuxAddUdtaTagFunc func;
} GstTagToFourcc;

/* tag list tags to fourcc matching */
static const GstTagToFourcc tag_matches_mp4[] = {
  {FOURCC__alb, GST_TAG_ALBUM, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_soal, GST_TAG_ALBUM_SORTNAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__ART, GST_TAG_ARTIST, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_soar, GST_TAG_ARTIST_SORTNAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_aART, GST_TAG_ALBUM_ARTIST, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_soaa, GST_TAG_ALBUM_ARTIST_SORTNAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__swr, GST_TAG_APPLICATION_NAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__cmt, GST_TAG_COMMENT, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__wrt, GST_TAG_COMPOSER, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_soco, GST_TAG_COMPOSER_SORTNAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_tvsh, GST_TAG_SHOW_NAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_sosn, GST_TAG_SHOW_SORTNAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_tvsn, GST_TAG_SHOW_SEASON_NUMBER, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_tves, GST_TAG_SHOW_EPISODE_NUMBER, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__gen, GST_TAG_GENRE, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__nam, GST_TAG_TITLE, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_sonm, GST_TAG_TITLE_SORTNAME, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_perf, GST_TAG_PERFORMER, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__grp, GST_TAG_GROUPING, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__des, GST_TAG_DESCRIPTION, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__lyr, GST_TAG_LYRICS, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__too, GST_TAG_ENCODER, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_cprt, GST_TAG_COPYRIGHT, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_keyw, GST_TAG_KEYWORDS, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC__day, GST_TAG_DATE, NULL, gst_qt_mux_add_mp4_date},
  {FOURCC_tmpo, GST_TAG_BEATS_PER_MINUTE, NULL, gst_qt_mux_add_mp4_tag},
  {FOURCC_trkn, GST_TAG_TRACK_NUMBER, GST_TAG_TRACK_COUNT,
      gst_qt_mux_add_mp4_tag},
  {FOURCC_disk, GST_TAG_ALBUM_VOLUME_NUMBER, GST_TAG_ALBUM_VOLUME_COUNT,
      gst_qt_mux_add_mp4_tag},
  {FOURCC_covr, GST_TAG_PREVIEW_IMAGE, NULL, gst_qt_mux_add_mp4_cover},
  {FOURCC_covr, GST_TAG_IMAGE, NULL, gst_qt_mux_add_mp4_cover},
  {0, NULL,}
};

static const GstTagToFourcc tag_matches_3gp[] = {
  {FOURCC_titl, GST_TAG_TITLE, NULL, gst_qt_mux_add_3gp_str},
  {FOURCC_dscp, GST_TAG_DESCRIPTION, NULL, gst_qt_mux_add_3gp_str},
  {FOURCC_cprt, GST_TAG_COPYRIGHT, NULL, gst_qt_mux_add_3gp_str},
  {FOURCC_perf, GST_TAG_ARTIST, NULL, gst_qt_mux_add_3gp_str},
  {FOURCC_auth, GST_TAG_COMPOSER, NULL, gst_qt_mux_add_3gp_str},
  {FOURCC_gnre, GST_TAG_GENRE, NULL, gst_qt_mux_add_3gp_str},
  {FOURCC_kywd, GST_TAG_KEYWORDS, NULL, gst_qt_mux_add_3gp_keywords},
  {FOURCC_yrrc, GST_TAG_DATE, NULL, gst_qt_mux_add_3gp_date},
  {FOURCC_albm, GST_TAG_ALBUM, GST_TAG_TRACK_NUMBER, gst_qt_mux_add_3gp_str},
  {FOURCC_loci, GST_TAG_GEO_LOCATION_NAME, NULL, gst_qt_mux_add_3gp_location},
  {FOURCC_clsf, GST_TAG_3GP_CLASSIFICATION, NULL,
      gst_qt_mux_add_3gp_classification},
  {0, NULL,}
};

/* qtdemux produces these for atoms it cannot parse */
#define GST_QT_DEMUX_PRIVATE_TAG "private-qt-tag"

static void
gst_qt_mux_add_xmp_tags (GstQTMux * qtmux, const GstTagList * list)
{
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  GstBuffer *xmp = NULL;

  /* adobe specs only have 'quicktime' and 'mp4',
   * but I guess we can extrapolate to gpp.
   * Keep mj2 out for now as we don't add any tags for it yet.
   * If you have further info about xmp on these formats, please share */
  if (qtmux_klass->format == GST_QT_MUX_FORMAT_MJ2)
    return;

  GST_DEBUG_OBJECT (qtmux, "Adding xmp tags");

  if (qtmux_klass->format == GST_QT_MUX_FORMAT_QT) {
    xmp = gst_tag_xmp_writer_tag_list_to_xmp_buffer (GST_TAG_XMP_WRITER (qtmux),
        list, TRUE);
    if (xmp)
      atom_udta_add_xmp_tags (&qtmux->moov->udta, xmp);
  } else {
    AtomInfo *ainfo;
    /* for isom/mp4, it is a top level uuid atom */
    xmp = gst_tag_xmp_writer_tag_list_to_xmp_buffer (GST_TAG_XMP_WRITER (qtmux),
        list, TRUE);
    if (xmp) {
      ainfo = build_uuid_xmp_atom (xmp);
      if (ainfo) {
        qtmux->extra_atoms = g_slist_prepend (qtmux->extra_atoms, ainfo);
      }
    }
  }
  if (xmp)
    gst_buffer_unref (xmp);
}

static void
gst_qt_mux_add_metadata_tags (GstQTMux * qtmux, const GstTagList * list,
    AtomUDTA * udta)
{
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  guint32 fourcc;
  gint i;
  const gchar *tag, *tag2;
  const GstTagToFourcc *tag_matches;

  switch (qtmux_klass->format) {
    case GST_QT_MUX_FORMAT_3GP:
      tag_matches = tag_matches_3gp;
      break;
    case GST_QT_MUX_FORMAT_MJ2:
      tag_matches = NULL;
      break;
    default:
      /* sort of iTunes style for mp4 and QT (?) */
      tag_matches = tag_matches_mp4;
      break;
  }

  if (!tag_matches)
    return;

  /* Clear existing tags so we don't add them over and over */
  atom_udta_clear_tags (udta);

  for (i = 0; tag_matches[i].fourcc; i++) {
    fourcc = tag_matches[i].fourcc;
    tag = tag_matches[i].gsttag;
    tag2 = tag_matches[i].gsttag2;

    g_assert (tag_matches[i].func);
    tag_matches[i].func (qtmux, list, udta, tag, tag2, fourcc);
  }

  /* add unparsed blobs if present */
  if (gst_tag_exists (GST_QT_DEMUX_PRIVATE_TAG)) {
    guint num_tags;

    num_tags = gst_tag_list_get_tag_size (list, GST_QT_DEMUX_PRIVATE_TAG);
    for (i = 0; i < num_tags; ++i) {
      GstSample *sample = NULL;
      GstBuffer *buf;
      const GstStructure *s;

      if (!gst_tag_list_get_sample_index (list, GST_QT_DEMUX_PRIVATE_TAG, i,
              &sample))
        continue;
      buf = gst_sample_get_buffer (sample);

      if (buf && (s = gst_sample_get_info (sample))) {
        const gchar *style = NULL;
        GstMapInfo map;

        gst_buffer_map (buf, &map, GST_MAP_READ);
        GST_DEBUG_OBJECT (qtmux,
            "Found private tag %d/%d; size %" G_GSIZE_FORMAT ", info %"
            GST_PTR_FORMAT, i, num_tags, map.size, s);
        if (s && (style = gst_structure_get_string (s, "style"))) {
          /* try to prevent some style tag ending up into another variant
           * (todo: make into a list if more cases) */
          if ((strcmp (style, "itunes") == 0 &&
                  qtmux_klass->format == GST_QT_MUX_FORMAT_MP4) ||
              (strcmp (style, "iso") == 0 &&
                  qtmux_klass->format == GST_QT_MUX_FORMAT_3GP)) {
            GST_DEBUG_OBJECT (qtmux, "Adding private tag");
            atom_udta_add_blob_tag (udta, map.data, map.size);
          }
        }
        gst_buffer_unmap (buf, &map);
      }
      gst_sample_unref (sample);
    }
  }

  return;
}

/*
 * Gets the tagsetter iface taglist and puts the known tags
 * into the output stream
 */
static void
gst_qt_mux_setup_metadata (GstQTMux * qtmux)
{
  const GstTagList *tags = NULL;
  GSList *walk;

  GST_OBJECT_LOCK (qtmux);
  if (qtmux->tags_changed) {
    tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (qtmux));
    qtmux->tags_changed = FALSE;
  }
  GST_OBJECT_UNLOCK (qtmux);

  GST_LOG_OBJECT (qtmux, "tags: %" GST_PTR_FORMAT, tags);

  if (tags && !gst_tag_list_is_empty (tags)) {
    GstTagList *copy = gst_tag_list_copy (tags);

    GST_DEBUG_OBJECT (qtmux, "Removing bogus tags");
    gst_tag_list_remove_tag (copy, GST_TAG_VIDEO_CODEC);
    gst_tag_list_remove_tag (copy, GST_TAG_AUDIO_CODEC);
    gst_tag_list_remove_tag (copy, GST_TAG_CONTAINER_FORMAT);

    GST_DEBUG_OBJECT (qtmux, "Formatting tags");
    gst_qt_mux_add_metadata_tags (qtmux, copy, &qtmux->moov->udta);
    gst_qt_mux_add_xmp_tags (qtmux, copy);
    gst_tag_list_unref (copy);
  } else {
    GST_DEBUG_OBJECT (qtmux, "No new tags received");
  }

  for (walk = qtmux->sinkpads; walk; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstQTPad *qpad = (GstQTPad *) cdata;
    GstPad *pad = qpad->collect.pad;

    if (qpad->tags_changed && qpad->tags) {
      GST_DEBUG_OBJECT (pad, "Adding tags");
      gst_tag_list_remove_tag (qpad->tags, GST_TAG_CONTAINER_FORMAT);
      gst_qt_mux_add_metadata_tags (qtmux, qpad->tags, &qpad->trak->udta);
      qpad->tags_changed = FALSE;
      GST_DEBUG_OBJECT (pad, "Tags added");
    } else {
      GST_DEBUG_OBJECT (pad, "No new tags received");
    }
  }
}

static inline GstBuffer *
_gst_buffer_new_take_data (guint8 * data, guint size)
{
  GstBuffer *buf;

  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf,
      gst_memory_new_wrapped (0, data, size, 0, size, data, g_free));

  return buf;
}

static GstFlowReturn
gst_qt_mux_send_buffer (GstQTMux * qtmux, GstBuffer * buf, guint64 * offset,
    gboolean mind_fast)
{
  GstFlowReturn res;
  gsize size;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  size = gst_buffer_get_size (buf);
  GST_LOG_OBJECT (qtmux, "sending buffer size %" G_GSIZE_FORMAT, size);

  if (mind_fast && qtmux->fast_start_file) {
    GstMapInfo map;
    gint ret;

    GST_LOG_OBJECT (qtmux, "to temporary file");
    gst_buffer_map (buf, &map, GST_MAP_READ);
    ret = fwrite (map.data, sizeof (guint8), map.size, qtmux->fast_start_file);
    gst_buffer_unmap (buf, &map);
    gst_buffer_unref (buf);
    if (ret != size)
      goto write_error;
    else
      res = GST_FLOW_OK;
  } else {
    GST_LOG_OBJECT (qtmux, "downstream");
    res = gst_pad_push (qtmux->srcpad, buf);
  }

  if (G_LIKELY (offset))
    *offset += size;

  return res;

  /* ERRORS */
write_error:
  {
    GST_ELEMENT_ERROR (qtmux, RESOURCE, WRITE,
        ("Failed to write to temporary file"), GST_ERROR_SYSTEM);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_qt_mux_seek_to_beginning (FILE * f)
{
#ifdef HAVE_FSEEKO
  if (fseeko (f, (off_t) 0, SEEK_SET) != 0)
    return FALSE;
#elif defined (G_OS_UNIX) || defined (G_OS_WIN32)
  if (lseek (fileno (f), (off_t) 0, SEEK_SET) == (off_t) - 1)
    return FALSE;
#else
  if (fseek (f, (long) 0, SEEK_SET) != 0)
    return FALSE;
#endif
  return TRUE;
}

static GstFlowReturn
gst_qt_mux_send_buffered_data (GstQTMux * qtmux, guint64 * offset)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf = NULL;

  if (fflush (qtmux->fast_start_file))
    goto flush_failed;

  if (!gst_qt_mux_seek_to_beginning (qtmux->fast_start_file))
    goto seek_failed;

  /* hm, this could all take a really really long time,
   * but there may not be another way to get moov atom first
   * (somehow optimize copy?) */
  GST_DEBUG_OBJECT (qtmux, "Sending buffered data");
  while (ret == GST_FLOW_OK) {
    const int bufsize = 4096;
    GstMapInfo map;
    gsize size;

    buf = gst_buffer_new_and_alloc (bufsize);
    gst_buffer_map (buf, &map, GST_MAP_WRITE);
    size = fread (map.data, sizeof (guint8), bufsize, qtmux->fast_start_file);
    if (size == 0) {
      gst_buffer_unmap (buf, &map);
      break;
    }
    GST_LOG_OBJECT (qtmux, "Pushing buffered buffer of size %d", (gint) size);
    gst_buffer_unmap (buf, &map);
    if (size != bufsize)
      gst_buffer_set_size (buf, size);
    ret = gst_qt_mux_send_buffer (qtmux, buf, offset, FALSE);
    buf = NULL;
  }
  if (buf)
    gst_buffer_unref (buf);

  if (ftruncate (fileno (qtmux->fast_start_file), 0))
    goto seek_failed;
  if (!gst_qt_mux_seek_to_beginning (qtmux->fast_start_file))
    goto seek_failed;

  return ret;

  /* ERRORS */
flush_failed:
  {
    GST_ELEMENT_ERROR (qtmux, RESOURCE, WRITE,
        ("Failed to flush temporary file"), GST_ERROR_SYSTEM);
    ret = GST_FLOW_ERROR;
    goto fail;
  }
seek_failed:
  {
    GST_ELEMENT_ERROR (qtmux, RESOURCE, SEEK,
        ("Failed to seek temporary file"), GST_ERROR_SYSTEM);
    ret = GST_FLOW_ERROR;
    goto fail;
  }
fail:
  {
    /* clear descriptor so we don't remove temp file later on,
     * might be possible to recover */
    fclose (qtmux->fast_start_file);
    qtmux->fast_start_file = NULL;
    return ret;
  }
}

/*
 * Sends the initial mdat atom fields (size fields and fourcc type),
 * the subsequent buffers are considered part of it's data.
 * As we can't predict the amount of data that we are going to place in mdat
 * we need to record the position of the size field in the stream so we can
 * seek back to it later and update when the streams have finished.
 */
static GstFlowReturn
gst_qt_mux_send_mdat_header (GstQTMux * qtmux, guint64 * off, guint64 size,
    gboolean extended, gboolean fsync_after)
{
  GstBuffer *buf;
  GstMapInfo map;

  GST_DEBUG_OBJECT (qtmux, "Sending mdat's atom header, "
      "size %" G_GUINT64_FORMAT, size);

  /* if the qtmux state is EOS, really write the mdat, otherwise
   * allow size == 0 for a placeholder atom */
  if (qtmux->state == GST_QT_MUX_STATE_EOS || size > 0)
    size += 8;

  if (extended) {
    gboolean large_file = (size > MDAT_LARGE_FILE_LIMIT);
    /* Always write 16-bytes, but put a free atom first
     * if the size is < 4GB. */
    buf = gst_buffer_new_and_alloc (16);
    gst_buffer_map (buf, &map, GST_MAP_WRITE);

    if (large_file) {
      /* Write extended mdat header and large_size field */
      GST_WRITE_UINT32_BE (map.data, 1);
      GST_WRITE_UINT32_LE (map.data + 4, FOURCC_mdat);
      GST_WRITE_UINT64_BE (map.data + 8, size + 8);
    } else {
      /* Write an empty free atom, then standard 32-bit mdat */
      GST_WRITE_UINT32_BE (map.data, 8);
      GST_WRITE_UINT32_LE (map.data + 4, FOURCC_free);
      GST_WRITE_UINT32_BE (map.data + 8, size);
      GST_WRITE_UINT32_LE (map.data + 12, FOURCC_mdat);
    }
    gst_buffer_unmap (buf, &map);
  } else {
    buf = gst_buffer_new_and_alloc (8);
    gst_buffer_map (buf, &map, GST_MAP_WRITE);

    /* Vanilla 32-bit mdat */
    GST_WRITE_UINT32_BE (map.data, size);
    GST_WRITE_UINT32_LE (map.data + 4, FOURCC_mdat);
    gst_buffer_unmap (buf, &map);
  }

  GST_LOG_OBJECT (qtmux, "Pushing mdat header");
  if (fsync_after)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_SYNC_AFTER);

  return gst_qt_mux_send_buffer (qtmux, buf, off, FALSE);

}

/*
 * We get the position of the mdat size field, seek back to it
 * and overwrite with the real value
 */
static GstFlowReturn
gst_qt_mux_update_mdat_size (GstQTMux * qtmux, guint64 mdat_pos,
    guint64 mdat_size, guint64 * offset, gboolean fsync_after)
{
  GstSegment segment;

  /* We must have recorded the mdat position for this to work */
  g_assert (mdat_pos != 0);

  /* seek and rewrite the header */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  segment.start = mdat_pos;
  gst_pad_push_event (qtmux->srcpad, gst_event_new_segment (&segment));

  return gst_qt_mux_send_mdat_header (qtmux, offset, mdat_size, TRUE,
      fsync_after);
}

static GstFlowReturn
gst_qt_mux_send_ftyp (GstQTMux * qtmux, guint64 * off)
{
  GstBuffer *buf;
  guint64 size = 0, offset = 0;
  guint8 *data = NULL;

  GST_DEBUG_OBJECT (qtmux, "Sending ftyp atom");

  if (!atom_ftyp_copy_data (qtmux->ftyp, &data, &size, &offset))
    goto serialize_error;

  buf = _gst_buffer_new_take_data (data, offset);

  GST_LOG_OBJECT (qtmux, "Pushing ftyp");
  return gst_qt_mux_send_buffer (qtmux, buf, off, FALSE);

  /* ERRORS */
serialize_error:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Failed to serialize ftyp"));
    return GST_FLOW_ERROR;
  }
}

static void
gst_qt_mux_prepare_ftyp (GstQTMux * qtmux, AtomFTYP ** p_ftyp,
    GstBuffer ** p_prefix)
{
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  guint32 major, version;
  GList *comp;
  GstBuffer *prefix = NULL;
  AtomFTYP *ftyp = NULL;

  GST_DEBUG_OBJECT (qtmux, "Preparing ftyp and possible prefix atom");

  /* init and send context and ftyp based on current property state */
  gst_qt_mux_map_format_to_header (qtmux_klass->format, &prefix, &major,
      &version, &comp, qtmux->moov, qtmux->longest_chunk,
      qtmux->fast_start_file != NULL);
  ftyp = atom_ftyp_new (qtmux->context, major, version, comp);
  if (comp)
    g_list_free (comp);
  if (prefix) {
    if (p_prefix)
      *p_prefix = prefix;
    else
      gst_buffer_unref (prefix);
  }
  *p_ftyp = ftyp;
}

static GstFlowReturn
gst_qt_mux_prepare_and_send_ftyp (GstQTMux * qtmux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *prefix = NULL;

  GST_DEBUG_OBJECT (qtmux, "Preparing to send ftyp atom");

  /* init and send context and ftyp based on current property state */
  if (qtmux->ftyp) {
    atom_ftyp_free (qtmux->ftyp);
    qtmux->ftyp = NULL;
  }
  gst_qt_mux_prepare_ftyp (qtmux, &qtmux->ftyp, &prefix);
  if (prefix) {
    ret = gst_qt_mux_send_buffer (qtmux, prefix, &qtmux->header_size, FALSE);
    if (ret != GST_FLOW_OK)
      return ret;
  }
  return gst_qt_mux_send_ftyp (qtmux, &qtmux->header_size);
}

static void
gst_qt_mux_set_header_on_caps (GstQTMux * mux, GstBuffer * buf)
{
  GstStructure *structure;
  GValue array = { 0 };
  GValue value = { 0 };
  GstCaps *caps, *tcaps;

  tcaps = gst_pad_get_current_caps (mux->srcpad);
  caps = gst_caps_copy (tcaps);
  gst_caps_unref (tcaps);

  structure = gst_caps_get_structure (caps, 0);

  g_value_init (&array, GST_TYPE_ARRAY);

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_HEADER);
  g_value_init (&value, GST_TYPE_BUFFER);
  gst_value_take_buffer (&value, gst_buffer_ref (buf));
  gst_value_array_append_value (&array, &value);
  g_value_unset (&value);

  gst_structure_set_value (structure, "streamheader", &array);
  g_value_unset (&array);
  gst_pad_set_caps (mux->srcpad, caps);
  gst_caps_unref (caps);
}

/*
 * Write out a free space atom. The offset is adjusted by the full
 * size, but a smaller buffer is sent
 */
static GstFlowReturn
gst_qt_mux_send_free_atom (GstQTMux * qtmux, guint64 * off, guint32 size,
    gboolean fsync_after)
{
  Atom *node_header;
  GstBuffer *buf;
  guint8 *data = NULL;
  guint64 offset = 0, bsize = 0;
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (qtmux, "Sending free atom header of size %u", size);

  /* We can't make a free space atom smaller than the header */
  if (size < 8)
    goto too_small;

  node_header = g_malloc0 (sizeof (Atom));
  node_header->type = FOURCC_free;
  node_header->size = size;

  bsize = offset = 0;
  if (atom_copy_data (node_header, &data, &bsize, &offset) == 0)
    goto serialize_error;

  buf = _gst_buffer_new_take_data (data, offset);
  g_free (node_header);

  if (fsync_after)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_SYNC_AFTER);

  GST_LOG_OBJECT (qtmux, "Pushing free atom");
  ret = gst_qt_mux_send_buffer (qtmux, buf, off, FALSE);

  if (off) {
    GstSegment segment;

    *off += size - 8;

    /* Make sure downstream position ends up at the end of this free box */
    gst_segment_init (&segment, GST_FORMAT_BYTES);
    segment.start = *off;
    gst_pad_push_event (qtmux->srcpad, gst_event_new_segment (&segment));
  }

  return ret;

  /* ERRORS */
too_small:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Not enough free reserved space"));
    return GST_FLOW_ERROR;
  }
serialize_error:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Failed to serialize mdat"));
    g_free (node_header);
    return GST_FLOW_ERROR;
  }
}

static void
gst_qt_mux_configure_moov (GstQTMux * qtmux)
{
  gboolean fragmented = FALSE;
  guint32 timescale;

  GST_OBJECT_LOCK (qtmux);
  timescale = qtmux->timescale;
  if (qtmux->mux_mode == GST_QT_MUX_MODE_FRAGMENTED ||
      qtmux->mux_mode == GST_QT_MUX_MODE_FRAGMENTED_STREAMABLE)
    fragmented = TRUE;
  GST_OBJECT_UNLOCK (qtmux);

  /* inform lower layers of our property wishes, and determine duration.
   * Let moov take care of this using its list of traks;
   * so that released pads are also included */
  GST_DEBUG_OBJECT (qtmux, "Updating timescale to %" G_GUINT32_FORMAT,
      timescale);
  atom_moov_update_timescale (qtmux->moov, timescale);
  atom_moov_set_fragmented (qtmux->moov, fragmented);

  atom_moov_update_duration (qtmux->moov);
}

static GstFlowReturn
gst_qt_mux_send_moov (GstQTMux * qtmux, guint64 * _offset,
    guint64 padded_moov_size, gboolean mind_fast, gboolean fsync_after)
{
  guint64 offset = 0, size = 0;
  guint8 *data;
  GstBuffer *buf;
  GstFlowReturn ret = GST_FLOW_OK;

  /* serialize moov */
  offset = size = 0;
  data = NULL;
  GST_LOG_OBJECT (qtmux, "Copying movie header into buffer");
  if (!atom_moov_copy_data (qtmux->moov, &data, &size, &offset))
    goto serialize_error;
  qtmux->last_moov_size = offset;

  /* Check we have enough reserved space for this and a Free atom */
  if (padded_moov_size > 0 && offset + 8 > padded_moov_size)
    goto too_small_reserved;
  buf = _gst_buffer_new_take_data (data, offset);
  GST_DEBUG_OBJECT (qtmux, "Pushing moov atoms");

  /* If at EOS, this is the final moov, put in the streamheader
   * (apparently used by a flumotion util) */
  if (qtmux->state == GST_QT_MUX_STATE_EOS)
    gst_qt_mux_set_header_on_caps (qtmux, buf);

  if (fsync_after)
    GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_SYNC_AFTER);
  ret = gst_qt_mux_send_buffer (qtmux, buf, _offset, mind_fast);

  /* Write out a free atom if needed */
  if (ret == GST_FLOW_OK && offset < padded_moov_size) {
    GST_LOG_OBJECT (qtmux, "Writing out free atom of size %u",
        (guint32) (padded_moov_size - offset));
    ret =
        gst_qt_mux_send_free_atom (qtmux, _offset, padded_moov_size - offset,
        fsync_after);
  }

  return ret;
too_small_reserved:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX,
        ("Not enough free reserved header space"),
        ("Needed %" G_GUINT64_FORMAT " bytes, reserved %" G_GUINT64_FORMAT,
            offset, padded_moov_size));
    return GST_FLOW_ERROR;
  }
serialize_error:
  {
    g_free (data);
    return GST_FLOW_ERROR;
  }
}

/* either calculates size of extra atoms or pushes them */
static GstFlowReturn
gst_qt_mux_send_extra_atoms (GstQTMux * qtmux, gboolean send, guint64 * offset,
    gboolean mind_fast)
{
  GSList *walk;
  guint64 loffset = 0, size = 0;
  guint8 *data;
  GstFlowReturn ret = GST_FLOW_OK;

  for (walk = qtmux->extra_atoms; walk; walk = g_slist_next (walk)) {
    AtomInfo *ainfo = (AtomInfo *) walk->data;

    loffset = size = 0;
    data = NULL;
    if (!ainfo->copy_data_func (ainfo->atom,
            send ? &data : NULL, &size, &loffset))
      goto serialize_error;

    if (send) {
      GstBuffer *buf;

      GST_DEBUG_OBJECT (qtmux,
          "Pushing extra top-level atom %" GST_FOURCC_FORMAT,
          GST_FOURCC_ARGS (ainfo->atom->type));
      buf = _gst_buffer_new_take_data (data, loffset);
      ret = gst_qt_mux_send_buffer (qtmux, buf, offset, FALSE);
      if (ret != GST_FLOW_OK)
        break;
    } else {
      if (offset)
        *offset += loffset;
    }
  }

  return ret;

serialize_error:
  {
    g_free (data);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_qt_mux_downstream_is_seekable (GstQTMux * qtmux)
{
  gboolean seekable = FALSE;
  GstQuery *query = gst_query_new_seeking (GST_FORMAT_BYTES);

  if (gst_pad_peer_query (qtmux->srcpad, query)) {
    gst_query_parse_seeking (query, NULL, &seekable, NULL, NULL);
    GST_INFO_OBJECT (qtmux, "downstream is %sseekable", seekable ? "" : "not ");
  } else {
    /* have to assume seeking is not supported if query not handled downstream */
    GST_WARNING_OBJECT (qtmux, "downstream did not handle seeking query");
    seekable = FALSE;
  }
  gst_query_unref (query);

  return seekable;
}

static void
gst_qt_mux_prepare_moov_recovery (GstQTMux * qtmux)
{
  GSList *walk;
  gboolean fail = FALSE;
  AtomFTYP *ftyp = NULL;
  GstBuffer *prefix = NULL;

  GST_DEBUG_OBJECT (qtmux, "Openning moov recovery file: %s",
      qtmux->moov_recov_file_path);

  qtmux->moov_recov_file = g_fopen (qtmux->moov_recov_file_path, "wb+");
  if (qtmux->moov_recov_file == NULL) {
    GST_WARNING_OBJECT (qtmux, "Failed to open moov recovery file in %s",
        qtmux->moov_recov_file_path);
    return;
  }

  gst_qt_mux_prepare_ftyp (qtmux, &ftyp, &prefix);

  if (!atoms_recov_write_headers (qtmux->moov_recov_file, ftyp, prefix,
          qtmux->moov, qtmux->timescale, g_slist_length (qtmux->sinkpads))) {
    GST_WARNING_OBJECT (qtmux, "Failed to write moov recovery file " "headers");
    goto fail;
  }

  atom_ftyp_free (ftyp);
  if (prefix)
    gst_buffer_unref (prefix);

  for (walk = qtmux->sinkpads; walk && !fail; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstQTPad *qpad = (GstQTPad *) cdata;
    /* write info for each stream */
    fail = atoms_recov_write_trak_info (qtmux->moov_recov_file, qpad->trak);
    if (fail) {
      GST_WARNING_OBJECT (qtmux, "Failed to write trak info to recovery "
          "file");
      break;
    }
  }

fail:
  /* cleanup */
  fclose (qtmux->moov_recov_file);
  qtmux->moov_recov_file = NULL;
  GST_WARNING_OBJECT (qtmux, "An error was detected while writing to "
      "recover file, moov recovery won't work");
}

static GstFlowReturn
gst_qt_mux_start_file (GstQTMux * qtmux)
{
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  GstFlowReturn ret = GST_FLOW_OK;
  GstCaps *caps;
  GstSegment segment;
  gchar s_id[32];
  GstClockTime reserved_max_duration;
  guint reserved_bytes_per_sec_per_trak;

  GST_DEBUG_OBJECT (qtmux, "starting file");

  GST_OBJECT_LOCK (qtmux);
  reserved_max_duration = qtmux->reserved_max_duration;
  reserved_bytes_per_sec_per_trak = qtmux->reserved_bytes_per_sec_per_trak;
  GST_OBJECT_UNLOCK (qtmux);

  /* stream-start (FIXME: create id based on input ids) */
  g_snprintf (s_id, sizeof (s_id), "qtmux-%08x", g_random_int ());
  gst_pad_push_event (qtmux->srcpad, gst_event_new_stream_start (s_id));

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (qtmux->srcpad));
  /* qtmux has structure with and without variant, remove all but the first */
  while (gst_caps_get_size (caps) > 1)
    gst_caps_remove_structure (caps, 1);
  gst_pad_set_caps (qtmux->srcpad, caps);
  gst_caps_unref (caps);

  /* Default is 'normal' mode */
  qtmux->mux_mode = GST_QT_MUX_MODE_MOOV_AT_END;

  /* Require a sensible fragment duration when muxing
   * using the ISML muxer */
  if (qtmux_klass->format == GST_QT_MUX_FORMAT_ISML &&
      qtmux->fragment_duration == 0)
    goto invalid_isml;

  if (qtmux->fragment_duration > 0) {
    if (qtmux->streamable)
      qtmux->mux_mode = GST_QT_MUX_MODE_FRAGMENTED_STREAMABLE;
    else
      qtmux->mux_mode = GST_QT_MUX_MODE_FRAGMENTED;
  } else if (qtmux->fast_start) {
    qtmux->mux_mode = GST_QT_MUX_MODE_FAST_START;
  } else if (reserved_max_duration != GST_CLOCK_TIME_NONE) {
    qtmux->mux_mode = GST_QT_MUX_MODE_ROBUST_RECORDING;
  }

  switch (qtmux->mux_mode) {
    case GST_QT_MUX_MODE_MOOV_AT_END:
    case GST_QT_MUX_MODE_ROBUST_RECORDING:
      /* We have to be able to seek to rewrite the mdat header, or any
       * moov atom we write will not be visible in the file, because an
       * MDAT with 0 as the size covers the rest of the file. A file
       * with no moov is not playable, so error out now. */
      if (!gst_qt_mux_downstream_is_seekable (qtmux)) {
        GST_ELEMENT_ERROR (qtmux, STREAM, MUX,
            ("Downstream is not seekable - will not be able to create a playable file"),
            (NULL));
        return GST_FLOW_ERROR;
      }
      break;
    case GST_QT_MUX_MODE_FAST_START:
    case GST_QT_MUX_MODE_FRAGMENTED_STREAMABLE:
      break;                    /* Don't need seekability, ignore */
    case GST_QT_MUX_MODE_FRAGMENTED:
      if (!gst_qt_mux_downstream_is_seekable (qtmux)) {
        GST_WARNING_OBJECT (qtmux, "downstream is not seekable, but "
            "streamable=false. Will ignore that and create streamable output "
            "instead");
        qtmux->streamable = TRUE;
        g_object_notify (G_OBJECT (qtmux), "streamable");
      }
      break;
  }

  /* let downstream know we think in BYTES and expect to do seeking later on */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  gst_pad_push_event (qtmux->srcpad, gst_event_new_segment (&segment));

  /* initialize our moov recovery file */
  GST_OBJECT_LOCK (qtmux);
  if (qtmux->moov_recov_file_path) {
    gst_qt_mux_prepare_moov_recovery (qtmux);
  }

  /* Make sure the first time we update the moov, we'll
   * include any tagsetter tags */
  qtmux->tags_changed = TRUE;

  GST_OBJECT_UNLOCK (qtmux);

  /*
   * send mdat header if already needed, and mark position for later update.
   * We don't send ftyp now if we are on fast start mode, because we can
   * better fine tune using the information we gather to create the whole moov
   * atom.
   */
  switch (qtmux->mux_mode) {
    case GST_QT_MUX_MODE_MOOV_AT_END:
      ret = gst_qt_mux_prepare_and_send_ftyp (qtmux);
      if (ret != GST_FLOW_OK)
        break;

      /* Store this as the mdat offset for later updating
       * when we write the moov */
      qtmux->mdat_pos = qtmux->header_size;
      /* extended atom in case we go over 4GB while writing and need
       * the full 64-bit atom */
      ret =
          gst_qt_mux_send_mdat_header (qtmux, &qtmux->header_size, 0, TRUE,
          FALSE);
      break;
    case GST_QT_MUX_MODE_ROBUST_RECORDING:

      ret = gst_qt_mux_prepare_and_send_ftyp (qtmux);
      if (ret != GST_FLOW_OK)
        break;

      /* Pad ftyp out to an 8-byte boundary before starting the moov
       * ping pong region. It should be well less than 1 disk sector,
       * unless there's a bajillion compatible types listed,
       * but let's be sure the free atom doesn't cross a sector
       * boundary anyway */
      if (qtmux->header_size % 8) {
        /* Extra 8 bytes for the padding free atom header */
        guint padding = (guint) (16 - (qtmux->header_size % 8));
        GST_LOG_OBJECT (qtmux, "Rounding ftyp by %u bytes", padding);
        ret =
            gst_qt_mux_send_free_atom (qtmux, &qtmux->header_size, padding,
            FALSE);
        if (ret != GST_FLOW_OK)
          return ret;
      }

      /* Store this as the moov offset for later updating.
       * We record mdat position below */
      qtmux->moov_pos = qtmux->header_size;

      /* Set up the initial 'ping' state of the ping-pong buffers */
      qtmux->reserved_moov_first_active = TRUE;

      gst_qt_mux_configure_moov (qtmux);
      gst_qt_mux_setup_metadata (qtmux);
      /* Empty free atom to begin, starting on an 8-byte boundary */
      ret = gst_qt_mux_send_free_atom (qtmux, &qtmux->header_size, 8, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;
      /* Moov header, not padded yet */
      ret = gst_qt_mux_send_moov (qtmux, &qtmux->header_size, 0, FALSE, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;
      /* The moov we just sent contains the 'base' size of the moov, before
       * we put in any time-dependent per-trak data. Use that to make
       * a good estimate of how much extra to reserve */
      /* Calculate how much space to reserve for our MOOV atom.
       * We actually reserve twice that, for ping-pong buffers */
      qtmux->base_moov_size = qtmux->last_moov_size;
      GST_LOG_OBJECT (qtmux, "Base moov size is %u before any indexes",
          qtmux->base_moov_size);
      qtmux->reserved_moov_size = qtmux->base_moov_size +
          gst_util_uint64_scale (reserved_max_duration,
          reserved_bytes_per_sec_per_trak *
          atom_moov_get_trak_count (qtmux->moov), GST_SECOND);

      /* Need space for at least 4 atom headers. More really, but
       * this as an absolute minimum */
      if (qtmux->reserved_moov_size < 4 * 8)
        goto reserved_moov_too_small;

      GST_DEBUG_OBJECT (qtmux, "reserving header area of size %u",
          2 * qtmux->reserved_moov_size + 16);

      GST_OBJECT_LOCK (qtmux);
      qtmux->reserved_duration_remaining =
          gst_util_uint64_scale (qtmux->reserved_moov_size -
          qtmux->base_moov_size, GST_SECOND,
          reserved_bytes_per_sec_per_trak *
          atom_moov_get_trak_count (qtmux->moov));
      GST_OBJECT_UNLOCK (qtmux);

      /* Now that we know how much reserved space is targetted,
       * output a free atom to fill the extra reserved */
      ret = gst_qt_mux_send_free_atom (qtmux, &qtmux->header_size,
          qtmux->reserved_moov_size - qtmux->base_moov_size, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;

      /* Then a free atom containing 'pong' buffer, with an
       * extra 8 bytes to account for the free atom header itself */
      ret = gst_qt_mux_send_free_atom (qtmux, &qtmux->header_size,
          qtmux->reserved_moov_size + 8, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;

      /* extra atoms go after the free/moov(s), before the mdat */
      ret =
          gst_qt_mux_send_extra_atoms (qtmux, TRUE, &qtmux->header_size, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;

      qtmux->mdat_pos = qtmux->header_size;
      /* extended atom in case we go over 4GB while writing and need
       * the full 64-bit atom */
      ret =
          gst_qt_mux_send_mdat_header (qtmux, &qtmux->header_size, 0, TRUE,
          FALSE);
      break;
    case GST_QT_MUX_MODE_FAST_START:
      GST_OBJECT_LOCK (qtmux);
      qtmux->fast_start_file = g_fopen (qtmux->fast_start_file_path, "wb+");
      if (!qtmux->fast_start_file)
        goto open_failed;
      GST_OBJECT_UNLOCK (qtmux);
      /* send a dummy buffer for preroll */
      ret = gst_qt_mux_send_buffer (qtmux, gst_buffer_new (), NULL, FALSE);
      break;
    case GST_QT_MUX_MODE_FRAGMENTED:
    case GST_QT_MUX_MODE_FRAGMENTED_STREAMABLE:
      ret = gst_qt_mux_prepare_and_send_ftyp (qtmux);
      if (ret != GST_FLOW_OK)
        break;
      /* store the moov pos so we can update the duration later
       * in non-streamable mode */
      qtmux->moov_pos = qtmux->header_size;

      GST_DEBUG_OBJECT (qtmux, "fragment duration %d ms, writing headers",
          qtmux->fragment_duration);
      /* also used as snapshot marker to indicate fragmented file */
      qtmux->fragment_sequence = 1;
      /* prepare moov and/or tags */
      gst_qt_mux_configure_moov (qtmux);
      gst_qt_mux_setup_metadata (qtmux);
      ret = gst_qt_mux_send_moov (qtmux, &qtmux->header_size, 0, FALSE, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;
      /* extra atoms */
      ret =
          gst_qt_mux_send_extra_atoms (qtmux, TRUE, &qtmux->header_size, FALSE);
      if (ret != GST_FLOW_OK)
        break;
      /* prepare index if not streamable */
      if (qtmux->mux_mode == GST_QT_MUX_MODE_FRAGMENTED)
        qtmux->mfra = atom_mfra_new (qtmux->context);
      break;
  }

  return ret;
  /* ERRORS */
invalid_isml:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX,
        ("Cannot create an ISML file with 0 fragment duration"), (NULL));
    return GST_FLOW_ERROR;
  }
reserved_moov_too_small:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX,
        ("Not enough reserved space for creating headers"), (NULL));
    return GST_FLOW_ERROR;
  }
open_failed:
  {
    GST_ELEMENT_ERROR (qtmux, RESOURCE, OPEN_READ_WRITE,
        (("Could not open temporary file \"%s\""),
            qtmux->fast_start_file_path), GST_ERROR_SYSTEM);
    GST_OBJECT_UNLOCK (qtmux);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_qt_mux_send_last_buffers (GstQTMux * qtmux)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GSList *walk;

  for (walk = qtmux->collect->data; walk; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstQTPad *qtpad = (GstQTPad *) cdata;

    /* avoid add_buffer complaining if not negotiated
     * in which case no buffers either, so skipping */
    if (!qtpad->fourcc) {
      GST_DEBUG_OBJECT (qtmux, "Pad %s has never had buffers",
          GST_PAD_NAME (qtpad->collect.pad));
      continue;
    }

    /* send last buffer; also flushes possibly queued buffers/ts */
    GST_DEBUG_OBJECT (qtmux, "Sending the last buffer for pad %s",
        GST_PAD_NAME (qtpad->collect.pad));
    ret = gst_qt_mux_add_buffer (qtmux, qtpad, NULL);
    if (ret != GST_FLOW_OK) {
      GST_WARNING_OBJECT (qtmux, "Failed to send last buffer for %s, "
          "flow return: %s", GST_PAD_NAME (qtpad->collect.pad),
          gst_flow_get_name (ret));
    }
  }

  return ret;
}

static void
gst_qt_mux_update_global_statistics (GstQTMux * qtmux)
{
  GSList *walk;

  /* for setting some subtitles fields */
  guint max_width = 0;
  guint max_height = 0;

  qtmux->first_ts = qtmux->last_dts = GST_CLOCK_TIME_NONE;

  for (walk = qtmux->collect->data; walk; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstQTPad *qtpad = (GstQTPad *) cdata;

    if (!qtpad->fourcc) {
      GST_DEBUG_OBJECT (qtmux, "Pad %s has never had buffers",
          GST_PAD_NAME (qtpad->collect.pad));
      continue;
    }

    /* having flushed above, can check for buffers now */
    if (GST_CLOCK_TIME_IS_VALID (qtpad->first_ts)) {
      /* determine max stream duration */
      if (!GST_CLOCK_TIME_IS_VALID (qtmux->last_dts)
          || qtpad->last_dts > qtmux->last_dts) {
        qtmux->last_dts = qtpad->last_dts;
      }
      if (!GST_CLOCK_TIME_IS_VALID (qtmux->first_ts)
          || qtpad->first_ts < qtmux->first_ts) {
        qtmux->first_ts = qtpad->first_ts;
      }
    }

    /* subtitles need to know the video width/height,
     * it is stored shifted 16 bits to the left according to the
     * spec */
    max_width = MAX (max_width, (qtpad->trak->tkhd.width >> 16));
    max_height = MAX (max_height, (qtpad->trak->tkhd.height >> 16));

    /* update average bitrate of streams if needed */
    {
      guint32 avgbitrate = 0;
      guint32 maxbitrate = qtpad->max_bitrate;

      if (qtpad->avg_bitrate)
        avgbitrate = qtpad->avg_bitrate;
      else if (qtpad->total_duration > 0)
        avgbitrate = (guint32) gst_util_uint64_scale_round (qtpad->total_bytes,
            8 * GST_SECOND, qtpad->total_duration);

      atom_trak_update_bitrates (qtpad->trak, avgbitrate, maxbitrate);
    }
  }

  /* need to update values on subtitle traks now that we know the
   * max width and height */
  for (walk = qtmux->collect->data; walk; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstQTPad *qtpad = (GstQTPad *) cdata;

    if (!qtpad->fourcc) {
      GST_DEBUG_OBJECT (qtmux, "Pad %s has never had buffers",
          GST_PAD_NAME (qtpad->collect.pad));
      continue;
    }

    if (qtpad->fourcc == FOURCC_tx3g) {
      atom_trak_tx3g_update_dimension (qtpad->trak, max_width, max_height);
    }
  }
}

/* Called after gst_qt_mux_update_global_statistics() updates the
 * first_ts tracking, to create/set edit lists for delayed streams */
static void
gst_qt_mux_update_edit_lists (GstQTMux * qtmux)
{
  GSList *walk;

  GST_DEBUG_OBJECT (qtmux, "Media first ts selected: %" GST_TIME_FORMAT,
      GST_TIME_ARGS (qtmux->first_ts));
  /* add/update EDTSs for late streams. configure_moov will have
   * set the trak durations above by summing the sample tables,
   * here we extend that if needing to insert an empty segment */
  for (walk = qtmux->collect->data; walk; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstQTPad *qtpad = (GstQTPad *) cdata;

    if (GST_CLOCK_TIME_IS_VALID (qtpad->first_ts)) {
      guint32 lateness = 0;
      guint32 duration = qtpad->trak->tkhd.duration;
      gboolean has_gap;

      has_gap = (qtpad->first_ts > (qtmux->first_ts + qtpad->dts_adjustment));

      if (has_gap) {
        GstClockTime diff;

        diff = qtpad->first_ts - (qtmux->first_ts + qtpad->dts_adjustment);
        lateness = gst_util_uint64_scale_round (diff,
            qtmux->timescale, GST_SECOND);

        GST_DEBUG_OBJECT (qtmux, "Pad %s is a late stream by %" GST_TIME_FORMAT,
            GST_PAD_NAME (qtpad->collect.pad), GST_TIME_ARGS (lateness));

        atom_trak_set_elst_entry (qtpad->trak, 0, lateness, (guint32) - 1,
            (guint32) (1 * 65536.0));
      }

      /* has shift */
      if (has_gap || (qtpad->dts_adjustment > 0)) {
        GstClockTime ctts = 0;
        guint32 media_start;

        if (qtpad->first_ts > qtpad->first_dts)
          ctts = qtpad->first_ts - qtpad->first_dts;

        media_start = gst_util_uint64_scale_round (ctts,
            atom_trak_get_timescale (qtpad->trak), GST_SECOND);

        atom_trak_set_elst_entry (qtpad->trak, 1, duration, media_start,
            (guint32) (1 * 65536.0));
      }

      /* need to add the empty time to the trak duration */
      duration += lateness;

      qtpad->trak->tkhd.duration = duration;

      /* And possibly grow the moov duration */
      if (duration > qtmux->moov->mvhd.time_info.duration) {
        qtmux->moov->mvhd.time_info.duration = duration;
        qtmux->moov->mvex.mehd.fragment_duration = duration;
      }
    }
  }
}

static GstFlowReturn
gst_qt_mux_stop_file (GstQTMux * qtmux)
{
  gboolean ret = GST_FLOW_OK;
  guint64 offset = 0, size = 0;
  gboolean large_file;

  GST_DEBUG_OBJECT (qtmux, "Updating remaining values and sending last data");

  /* pushing last buffers for each pad */
  if ((ret = gst_qt_mux_send_last_buffers (qtmux)) != GST_FLOW_OK)
    return ret;

  if (qtmux->mux_mode == GST_QT_MUX_MODE_FRAGMENTED_STREAMABLE) {
    /* Streamable mode; no need to write duration or MFRA */
    GST_DEBUG_OBJECT (qtmux, "streamable file; nothing to stop");
    return GST_FLOW_OK;
  }

  gst_qt_mux_update_global_statistics (qtmux);

  switch (qtmux->mux_mode) {
    case GST_QT_MUX_MODE_FRAGMENTED:{
      GstSegment segment;
      guint8 *data = NULL;
      GstBuffer *buf;

      size = offset = 0;
      GST_DEBUG_OBJECT (qtmux, "adding mfra");
      if (!atom_mfra_copy_data (qtmux->mfra, &data, &size, &offset))
        goto serialize_error;
      buf = _gst_buffer_new_take_data (data, offset);
      ret = gst_qt_mux_send_buffer (qtmux, buf, NULL, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;

      /* only mvex duration is updated,
       * mvhd should be consistent with empty moov
       * (but TODO maybe some clients do not handle that well ?) */
      qtmux->moov->mvex.mehd.fragment_duration =
          gst_util_uint64_scale (qtmux->last_dts, qtmux->timescale, GST_SECOND);
      GST_DEBUG_OBJECT (qtmux, "rewriting moov with mvex duration %"
          GST_TIME_FORMAT, GST_TIME_ARGS (qtmux->last_dts));
      /* seek and rewrite the header */
      gst_segment_init (&segment, GST_FORMAT_BYTES);
      segment.start = qtmux->moov_pos;
      gst_pad_push_event (qtmux->srcpad, gst_event_new_segment (&segment));
      /* no need to seek back */
      return gst_qt_mux_send_moov (qtmux, NULL, 0, FALSE, FALSE);
    }
    case GST_QT_MUX_MODE_ROBUST_RECORDING:{
      ret = gst_qt_mux_robust_recording_rewrite_moov (qtmux);
      if (G_UNLIKELY (ret != GST_FLOW_OK))
        return ret;
      /* Finalise by writing the final size into the mdat. Up until now
       * it's been 0, which means 'rest of the file'
       * No need to seek back after this, we won't write any more */
      return gst_qt_mux_update_mdat_size (qtmux, qtmux->mdat_pos,
          qtmux->mdat_size, NULL, TRUE);
    }
    default:
      break;
  }

  /* Moov-at-end or fast-start mode from here down */
  gst_qt_mux_configure_moov (qtmux);

  gst_qt_mux_update_edit_lists (qtmux);

  /* tags into file metadata */
  gst_qt_mux_setup_metadata (qtmux);

  large_file = (qtmux->mdat_size > MDAT_LARGE_FILE_LIMIT);

  switch (qtmux->mux_mode) {
    case GST_QT_MUX_MODE_FAST_START:{
      /* if faststart, update the offset of the atoms in the movie with the offset
       * that the movie headers before mdat will cause.
       * Also, send the ftyp */
      offset = size = 0;

      ret = gst_qt_mux_prepare_and_send_ftyp (qtmux);
      if (ret != GST_FLOW_OK) {
        goto ftyp_error;
      }
      /* copy into NULL to obtain size */
      if (!atom_moov_copy_data (qtmux->moov, NULL, &size, &offset))
        goto serialize_error;
      GST_DEBUG_OBJECT (qtmux, "calculated moov atom size %" G_GUINT64_FORMAT,
          offset);
      offset += qtmux->header_size + (large_file ? 16 : 8);

      /* sum up with the extra atoms size */
      ret = gst_qt_mux_send_extra_atoms (qtmux, FALSE, &offset, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;
      break;
    }
    default:
      offset = qtmux->header_size;
      break;
  }

  /* Now that we know the size of moov + extra atoms, we can adjust
   * the chunk offsets stored into the moov */
  atom_moov_chunks_set_offset (qtmux->moov, offset);

  /* write out moov and extra atoms */
  /* note: as of this point, we no longer care about tracking written data size,
   * since there is no more use for it anyway */
  ret = gst_qt_mux_send_moov (qtmux, NULL, 0, FALSE, FALSE);
  if (ret != GST_FLOW_OK)
    return ret;

  /* extra atoms */
  ret = gst_qt_mux_send_extra_atoms (qtmux, TRUE, NULL, FALSE);
  if (ret != GST_FLOW_OK)
    return ret;

  switch (qtmux->mux_mode) {
    case GST_QT_MUX_MODE_MOOV_AT_END:
    {
      /* mdat needs update iff not using faststart */
      GST_DEBUG_OBJECT (qtmux, "updating mdat size");
      ret = gst_qt_mux_update_mdat_size (qtmux, qtmux->mdat_pos,
          qtmux->mdat_size, NULL, FALSE);
      /* note; no seeking back to the end of file is done,
       * since we no longer write anything anyway */
      break;
    }
    case GST_QT_MUX_MODE_FAST_START:
    {
      /* send mdat atom and move buffered data into it */
      /* mdat_size = accumulated (buffered data) */
      ret = gst_qt_mux_send_mdat_header (qtmux, NULL, qtmux->mdat_size,
          large_file, FALSE);
      if (ret != GST_FLOW_OK)
        return ret;
      ret = gst_qt_mux_send_buffered_data (qtmux, NULL);
      if (ret != GST_FLOW_OK)
        return ret;
      break;
    }
    default:
      g_assert_not_reached ();
  }

  return ret;

  /* ERRORS */
serialize_error:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Failed to serialize moov"));
    return GST_FLOW_ERROR;
  }
ftyp_error:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL), ("Failed to send ftyp"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_qt_mux_pad_fragment_add_buffer (GstQTMux * qtmux, GstQTPad * pad,
    GstBuffer * buf, gboolean force, guint32 nsamples, gint64 dts,
    guint32 delta, guint32 size, gboolean sync, gint64 pts_offset)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* setup if needed */
  if (G_UNLIKELY (!pad->traf || force))
    goto init;

flush:
  /* flush pad fragment if threshold reached,
   * or at new keyframe if we should be minding those in the first place */
  if (G_UNLIKELY (force || (sync && pad->sync) ||
          pad->fragment_duration < (gint64) delta)) {
    AtomMOOF *moof;
    guint64 size = 0, offset = 0;
    guint8 *data = NULL;
    GstBuffer *buffer;
    guint i, total_size;

    /* now we know where moof ends up, update offset in tfra */
    if (pad->tfra)
      atom_tfra_update_offset (pad->tfra, qtmux->header_size);

    moof = atom_moof_new (qtmux->context, qtmux->fragment_sequence);
    /* takes ownership */
    atom_moof_add_traf (moof, pad->traf);
    pad->traf = NULL;
    atom_moof_copy_data (moof, &data, &size, &offset);
    buffer = _gst_buffer_new_take_data (data, offset);
    GST_LOG_OBJECT (qtmux, "writing moof size %" G_GSIZE_FORMAT,
        gst_buffer_get_size (buffer));
    ret = gst_qt_mux_send_buffer (qtmux, buffer, &qtmux->header_size, FALSE);

    /* and actual data */
    total_size = 0;
    for (i = 0; i < atom_array_get_len (&pad->fragment_buffers); i++) {
      total_size +=
          gst_buffer_get_size (atom_array_index (&pad->fragment_buffers, i));
    }

    GST_LOG_OBJECT (qtmux, "writing %d buffers, total_size %d",
        atom_array_get_len (&pad->fragment_buffers), total_size);
    if (ret == GST_FLOW_OK)
      ret = gst_qt_mux_send_mdat_header (qtmux, &qtmux->header_size, total_size,
          FALSE, FALSE);
    for (i = 0; i < atom_array_get_len (&pad->fragment_buffers); i++) {
      if (G_LIKELY (ret == GST_FLOW_OK))
        ret = gst_qt_mux_send_buffer (qtmux,
            atom_array_index (&pad->fragment_buffers, i), &qtmux->header_size,
            FALSE);
      else
        gst_buffer_unref (atom_array_index (&pad->fragment_buffers, i));
    }

    atom_array_clear (&pad->fragment_buffers);
    atom_moof_free (moof);
    qtmux->fragment_sequence++;
    force = FALSE;
  }

init:
  if (G_UNLIKELY (!pad->traf)) {
    GST_LOG_OBJECT (qtmux, "setting up new fragment");
    pad->traf = atom_traf_new (qtmux->context, atom_trak_get_id (pad->trak));
    atom_array_init (&pad->fragment_buffers, 512);
    pad->fragment_duration = gst_util_uint64_scale (qtmux->fragment_duration,
        atom_trak_get_timescale (pad->trak), 1000);

    if (G_UNLIKELY (qtmux->mfra && !pad->tfra)) {
      pad->tfra = atom_tfra_new (qtmux->context, atom_trak_get_id (pad->trak));
      atom_mfra_add_tfra (qtmux->mfra, pad->tfra);
    }
  }

  /* add buffer and metadata */
  atom_traf_add_samples (pad->traf, delta, size, sync, pts_offset,
      pad->sync && sync);
  atom_array_append (&pad->fragment_buffers, buf, 256);
  pad->fragment_duration -= delta;

  if (pad->tfra) {
    guint32 sn = atom_traf_get_sample_num (pad->traf);

    if ((sync && pad->sync) || (sn == 1 && !pad->sync))
      atom_tfra_add_entry (pad->tfra, dts, sn);
  }

  if (G_UNLIKELY (force))
    goto flush;

  return ret;
}

/* Here's the clever bit of robust recording: Updating the moov
 * header is done using a ping-pong scheme inside 2 blocks of size
 * 'reserved_moov_size' at the start of the file, in such a way that the
 * file on-disk is always valid if interrupted.
 * Inside the reserved space, we have 2 pairs of free + moov atoms
 * (in that order), free-A + moov-A @ offset 0 and free-B + moov-B at
 * at offset "reserved_moov_size".
 *
 * 1. Free-A has 0 size payload, moov-A immediately after is
 *    active/current, and is padded with an internal Free atom to
 *    end at reserved_space/2. Free-B is at reserved_space/2, sized
 *    to cover the remaining free space (including moov-B).
 * 2. We write moov-B (which is invisible inside free-B), and pad it to
 *    end at the end of free space. Then, we update free-A to size
 *    reserved_space/2 + sizeof(free-B), which hides moov-A and the
 *    free-B header, and makes moov-B active.
 * 3. Rewrite moov-A inside free-A, with padding out to free-B.
 *    Change the size of free-A to make moov-A active again.
 * 4. Rinse and repeat.
 *
 */
static GstFlowReturn
gst_qt_mux_robust_recording_rewrite_moov (GstQTMux * qtmux)
{
  GstSegment segment;
  GstFlowReturn ret;
  guint64 freeA_offset;
  guint32 new_freeA_size;
  guint64 new_moov_offset;

  /* Update moov info, then seek and rewrite the MOOV atom */
  gst_qt_mux_update_global_statistics (qtmux);
  gst_qt_mux_configure_moov (qtmux);

  gst_qt_mux_update_edit_lists (qtmux);

  /* tags into file metadata */
  gst_qt_mux_setup_metadata (qtmux);

  /* chunks position is set relative to the first byte of the
   * MDAT atom payload. Set the overall offset into the file */
  atom_moov_chunks_set_offset (qtmux->moov, qtmux->header_size);

  /* Calculate which moov to rewrite. qtmux->moov_pos points to
   * the start of the free-A header */
  freeA_offset = qtmux->moov_pos;
  if (qtmux->reserved_moov_first_active) {
    GST_DEBUG_OBJECT (qtmux, "Updating pong moov header");
    /* After this, freeA will include itself, moovA, plus the freeB
     * header */
    new_freeA_size = qtmux->reserved_moov_size + 16;
  } else {
    GST_DEBUG_OBJECT (qtmux, "Updating ping moov header");
    new_freeA_size = 8;
  }
  /* the moov we update is after free-A, calculate its offset */
  new_moov_offset = freeA_offset + new_freeA_size;

  /* Swap ping-pong cadence marker */
  qtmux->reserved_moov_first_active = !qtmux->reserved_moov_first_active;

  /* seek and rewrite the MOOV atom */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  segment.start = new_moov_offset;
  gst_pad_push_event (qtmux->srcpad, gst_event_new_segment (&segment));

  ret =
      gst_qt_mux_send_moov (qtmux, NULL, qtmux->reserved_moov_size, FALSE,
      TRUE);
  if (ret != GST_FLOW_OK)
    return ret;

  /* Update the estimated recording space remaining, based on amount used so
   * far and duration muxed so far */
  if (qtmux->last_moov_size > qtmux->base_moov_size && qtmux->last_dts > 0) {
    GstClockTime remain;
    GstClockTime time_muxed = qtmux->last_dts;

    remain =
        gst_util_uint64_scale (qtmux->reserved_moov_size -
        qtmux->last_moov_size, time_muxed,
        qtmux->last_moov_size - qtmux->base_moov_size);
    /* Always under-estimate slightly, so users
     * have time to stop muxing before we run out */
    if (remain < GST_SECOND / 2)
      remain = 0;
    else
      remain -= GST_SECOND / 2;

    GST_INFO_OBJECT (qtmux,
        "Reserved %u header bytes. Used %u in %" GST_TIME_FORMAT
        ". Remaining now %u or approx %" G_GUINT64_FORMAT " ns\n",
        qtmux->reserved_moov_size, qtmux->last_moov_size,
        GST_TIME_ARGS (qtmux->last_dts),
        qtmux->reserved_moov_size - qtmux->last_moov_size, remain);

    GST_OBJECT_LOCK (qtmux);
    qtmux->reserved_duration_remaining = remain;
    qtmux->muxed_since_last_update = 0;
    GST_DEBUG_OBJECT (qtmux, "reserved remaining duration now %"
        G_GUINT64_FORMAT, qtmux->reserved_duration_remaining);
    GST_OBJECT_UNLOCK (qtmux);
  }


  /* Now update the moov-A size. Don't pass offset, since we don't need
   * send_free_atom() to seek for us - all our callers seek back to
   * where they need after this, or they don't need it */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  segment.start = freeA_offset;
  gst_pad_push_event (qtmux->srcpad, gst_event_new_segment (&segment));

  ret = gst_qt_mux_send_free_atom (qtmux, NULL, new_freeA_size, TRUE);

  return ret;
}

static GstFlowReturn
gst_qt_mux_robust_recording_update (GstQTMux * qtmux, GstClockTime position)
{
  GstSegment segment;
  GstFlowReturn flow_ret;

  guint64 mdat_offset = qtmux->mdat_pos + 16 + qtmux->mdat_size;

  GST_OBJECT_LOCK (qtmux);
  if (qtmux->reserved_moov_update_period == GST_CLOCK_TIME_NONE) {
    GST_OBJECT_UNLOCK (qtmux);
    return GST_FLOW_OK;
  }

  /* Update if position is > the threshold or there's been no update yet */
  if (qtmux->last_moov_update != GST_CLOCK_TIME_NONE &&
      (position <= qtmux->last_moov_update ||
          (position - qtmux->last_moov_update) <
          qtmux->reserved_moov_update_period)) {
    /* Update the offset of how much we've muxed, so the
     * report of remaining space keeps counting down */
    if (position > qtmux->last_moov_update &&
        position - qtmux->last_moov_update > qtmux->muxed_since_last_update) {
      GST_LOG_OBJECT (qtmux,
          "Muxed time %" G_GUINT64_FORMAT " since last moov update",
          qtmux->muxed_since_last_update);
      qtmux->muxed_since_last_update = position - qtmux->last_moov_update;
    }
    GST_OBJECT_UNLOCK (qtmux);
    return GST_FLOW_OK;         /* No update needed yet */
  }

  qtmux->last_moov_update = position;
  GST_OBJECT_UNLOCK (qtmux);

  GST_DEBUG_OBJECT (qtmux, "Update moov atom, position %" GST_TIME_FORMAT
      " mdat starts @ %" G_GUINT64_FORMAT " we were a %" G_GUINT64_FORMAT,
      GST_TIME_ARGS (position), qtmux->mdat_pos, mdat_offset);

  flow_ret = gst_qt_mux_robust_recording_rewrite_moov (qtmux);
  if (G_UNLIKELY (flow_ret != GST_FLOW_OK))
    return flow_ret;

  /* Seek back to previous position */
  gst_segment_init (&segment, GST_FORMAT_BYTES);
  segment.start = mdat_offset;
  gst_pad_push_event (qtmux->srcpad, gst_event_new_segment (&segment));

  return flow_ret;
}

static GstFlowReturn
gst_qt_mux_register_and_push_sample (GstQTMux * qtmux, GstQTPad * pad,
    GstBuffer * buffer, gboolean is_last_buffer, guint nsamples,
    gint64 last_dts, gint64 scaled_duration, guint sample_size,
    guint64 chunk_offset, gboolean sync, gboolean do_pts, gint64 pts_offset)
{
  GstFlowReturn ret = GST_FLOW_OK;

  /* note that a new chunk is started each time (not fancy but works) */
  if (qtmux->moov_recov_file) {
    if (!atoms_recov_write_trak_samples (qtmux->moov_recov_file, pad->trak,
            nsamples, (gint32) scaled_duration, sample_size, chunk_offset, sync,
            do_pts, pts_offset)) {
      GST_WARNING_OBJECT (qtmux, "Failed to write sample information to "
          "recovery file, disabling recovery");
      fclose (qtmux->moov_recov_file);
      qtmux->moov_recov_file = NULL;
    }
  }

  switch (qtmux->mux_mode) {
    case GST_QT_MUX_MODE_MOOV_AT_END:
    case GST_QT_MUX_MODE_FAST_START:
    case GST_QT_MUX_MODE_ROBUST_RECORDING:
      atom_trak_add_samples (pad->trak, nsamples, (gint32) scaled_duration,
          sample_size, chunk_offset, sync, pts_offset);
      ret = gst_qt_mux_send_buffer (qtmux, buffer, &qtmux->mdat_size, TRUE);
      /* Check if it's time to re-write the headers in robust-recording mode */
      if (ret == GST_FLOW_OK
          && qtmux->mux_mode == GST_QT_MUX_MODE_ROBUST_RECORDING)
        ret = gst_qt_mux_robust_recording_update (qtmux, pad->total_duration);
      break;
    case GST_QT_MUX_MODE_FRAGMENTED:
    case GST_QT_MUX_MODE_FRAGMENTED_STREAMABLE:
      /* ensure that always sync samples are marked as such */
      ret = gst_qt_mux_pad_fragment_add_buffer (qtmux, pad, buffer,
          is_last_buffer, nsamples, last_dts, (gint32) scaled_duration,
          sample_size, !pad->sync || sync, pts_offset);
      break;
  }

  return ret;
}

/*
 * Here we push the buffer and update the tables in the track atoms
 */
static GstFlowReturn
gst_qt_mux_add_buffer (GstQTMux * qtmux, GstQTPad * pad, GstBuffer * buf)
{
  GstBuffer *last_buf = NULL;
  GstClockTime duration;
  guint nsamples, sample_size;
  guint64 chunk_offset;
  gint64 last_dts, scaled_duration;
  gint64 pts_offset = 0;
  gboolean sync = FALSE;
  GstFlowReturn ret = GST_FLOW_OK;

  if (!pad->fourcc)
    goto not_negotiated;

  /* if this pad has a prepare function, call it */
  if (pad->prepare_buf_func != NULL) {
    buf = pad->prepare_buf_func (pad, buf, qtmux);
  }

  last_buf = pad->last_buf;

  /* DTS delta is used to calculate sample duration.
   * If buffer has missing DTS, we take either segment start or
   *  previous buffer end time, whichever is later.
   * This must only be done for non sparse streams, sparse streams
   * can have gaps between buffers (which is handled later by adding
   * extra empty buffer with duration that fills the gap). */
  if (!pad->sparse && buf && !GST_BUFFER_DTS_IS_VALID (buf)) {
    GstClockTime last_buf_duration = last_buf
        && GST_BUFFER_DURATION_IS_VALID (last_buf) ?
        GST_BUFFER_DURATION (last_buf) : 0;

    buf = gst_buffer_make_writable (buf);
    GST_BUFFER_DTS (buf) = 0;   /* running-time 0 */

    if (last_buf
        && (GST_BUFFER_DTS (last_buf) + last_buf_duration) >
        GST_BUFFER_DTS (buf)) {
      GST_BUFFER_DTS (buf) = GST_BUFFER_DTS (last_buf) + last_buf_duration;
    }
  }

  if (last_buf && !buf && !GST_BUFFER_DURATION_IS_VALID (last_buf)) {
    /* this is last buffer; there is no next buffer so we need valid number as duration */
    last_buf = gst_buffer_make_writable (last_buf);
    GST_BUFFER_DURATION (last_buf) = 0;
  }

  if (last_buf == NULL) {
#ifndef GST_DISABLE_GST_DEBUG
    if (buf == NULL) {
      GST_DEBUG_OBJECT (qtmux, "Pad %s has no previous buffer stored and "
          "received NULL buffer, doing nothing",
          GST_PAD_NAME (pad->collect.pad));
    } else {
      GST_LOG_OBJECT (qtmux,
          "Pad %s has no previous buffer stored, storing now",
          GST_PAD_NAME (pad->collect.pad));
    }
#endif
    pad->last_buf = buf;
    goto exit;
  } else
    gst_buffer_ref (last_buf);

  /* if this is the first buffer, store the timestamp */
  if (G_UNLIKELY (pad->first_ts == GST_CLOCK_TIME_NONE) && last_buf) {
    if (GST_BUFFER_PTS_IS_VALID (last_buf)) {
      pad->first_ts = GST_BUFFER_PTS (last_buf);
    } else if (GST_BUFFER_DTS_IS_VALID (last_buf)) {
      pad->first_ts = GST_BUFFER_DTS (last_buf);
    }

    if (GST_BUFFER_DTS_IS_VALID (last_buf)) {
      pad->first_dts = pad->last_dts = GST_BUFFER_DTS (last_buf);
    } else if (GST_BUFFER_PTS_IS_VALID (last_buf)) {
      pad->first_dts = pad->last_dts = GST_BUFFER_PTS (last_buf);
    }

    if (GST_CLOCK_TIME_IS_VALID (pad->first_ts)) {
      GST_DEBUG ("setting first_ts to %" G_GUINT64_FORMAT, pad->first_ts);
    } else {
      GST_WARNING_OBJECT (qtmux, "First buffer for pad %s has no timestamp, "
          "using 0 as first timestamp", GST_PAD_NAME (pad->collect.pad));
      pad->first_ts = pad->first_dts = 0;
    }
    GST_DEBUG_OBJECT (qtmux, "Stored first timestamp for pad %s %"
        GST_TIME_FORMAT, GST_PAD_NAME (pad->collect.pad),
        GST_TIME_ARGS (pad->first_ts));
  }

  if (last_buf && buf && GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (buf)) &&
      GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (last_buf)) &&
      GST_BUFFER_DTS (buf) < GST_BUFFER_DTS (last_buf)) {
    GST_ERROR ("decreasing DTS value %" GST_TIME_FORMAT " < %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_DTS (buf)),
        GST_TIME_ARGS (GST_BUFFER_DTS (last_buf)));
    GST_BUFFER_DTS (buf) = GST_BUFFER_DTS (last_buf);
  }

  /* duration actually means time delta between samples, so we calculate
   * the duration based on the difference in DTS or PTS, falling back
   * to DURATION if the other two don't exist, such as with the last
   * sample before EOS. */
  duration = GST_BUFFER_DURATION (last_buf);
  if (!pad->sparse) {
    if (last_buf && buf && GST_BUFFER_DTS_IS_VALID (buf)
        && GST_BUFFER_DTS_IS_VALID (last_buf))
      duration = GST_BUFFER_DTS (buf) - GST_BUFFER_DTS (last_buf);
    else if (last_buf && buf && GST_BUFFER_PTS_IS_VALID (buf)
        && GST_BUFFER_PTS_IS_VALID (last_buf))
      duration = GST_BUFFER_PTS (buf) - GST_BUFFER_PTS (last_buf);
  }

  gst_buffer_replace (&pad->last_buf, buf);

  /* for computing the avg bitrate */
  if (G_LIKELY (last_buf)) {
    pad->total_bytes += gst_buffer_get_size (last_buf);
    pad->total_duration += duration;
  }

  last_dts = gst_util_uint64_scale_round (pad->last_dts,
      atom_trak_get_timescale (pad->trak), GST_SECOND);

  /* fragments only deal with 1 buffer == 1 chunk (== 1 sample) */
  if (pad->sample_size && !qtmux->fragment_sequence) {
    /* Constant size packets: usually raw audio (with many samples per
       buffer (= chunk)), but can also be fixed-packet-size codecs like ADPCM
     */
    sample_size = pad->sample_size;
    if (gst_buffer_get_size (last_buf) % sample_size != 0)
      goto fragmented_sample;
    /* note: qt raw audio storage warps it implicitly into a timewise
     * perfect stream, discarding buffer times */
    if (GST_BUFFER_DURATION (last_buf) != GST_CLOCK_TIME_NONE) {
      nsamples = gst_util_uint64_scale_round (GST_BUFFER_DURATION (last_buf),
          atom_trak_get_timescale (pad->trak), GST_SECOND);
    } else {
      nsamples = gst_buffer_get_size (last_buf) / sample_size;
    }
    if (nsamples > 0)
      duration = GST_BUFFER_DURATION (last_buf) / nsamples;
    else
      duration = 0;

    /* timescale = samplerate */
    scaled_duration = 1;
    pad->last_dts += duration * nsamples;
  } else {
    nsamples = 1;
    sample_size = gst_buffer_get_size (last_buf);
    if ((pad->last_buf && GST_BUFFER_DTS_IS_VALID (pad->last_buf))
        || GST_BUFFER_DTS_IS_VALID (last_buf)) {
      gint64 scaled_dts;
      if (pad->last_buf && GST_BUFFER_DTS_IS_VALID (pad->last_buf)) {
        pad->last_dts = GST_BUFFER_DTS (pad->last_buf);
      } else {
        pad->last_dts = GST_BUFFER_DTS (last_buf) +
            GST_BUFFER_DURATION (last_buf);
      }
      if ((gint64) (pad->last_dts) < 0) {
        scaled_dts = -gst_util_uint64_scale_round (-pad->last_dts,
            atom_trak_get_timescale (pad->trak), GST_SECOND);
      } else {
        scaled_dts = gst_util_uint64_scale_round (pad->last_dts,
            atom_trak_get_timescale (pad->trak), GST_SECOND);
      }
      scaled_duration = scaled_dts - last_dts;
      last_dts = scaled_dts;
    } else {
      /* first convert intended timestamp (in GstClockTime resolution) to
       * trak timescale, then derive delta;
       * this ensures sums of (scale)delta add up to converted timestamp,
       * which only deviates at most 1/scale from timestamp itself */
      scaled_duration = gst_util_uint64_scale_round (pad->last_dts + duration,
          atom_trak_get_timescale (pad->trak), GST_SECOND) - last_dts;
      pad->last_dts += duration;
    }
  }
  chunk_offset = qtmux->mdat_size;

  GST_LOG_OBJECT (qtmux,
      "Pad (%s) dts updated to %" GST_TIME_FORMAT,
      GST_PAD_NAME (pad->collect.pad), GST_TIME_ARGS (pad->last_dts));
  GST_LOG_OBJECT (qtmux,
      "Adding %d samples to track, duration: %" G_GUINT64_FORMAT
      " size: %" G_GUINT32_FORMAT " chunk offset: %" G_GUINT64_FORMAT,
      nsamples, scaled_duration, sample_size, chunk_offset);

  /* might be a sync sample */
  if (pad->sync &&
      !GST_BUFFER_FLAG_IS_SET (last_buf, GST_BUFFER_FLAG_DELTA_UNIT)) {
    GST_LOG_OBJECT (qtmux, "Adding new sync sample entry for track of pad %s",
        GST_PAD_NAME (pad->collect.pad));
    sync = TRUE;
  }

  if (GST_CLOCK_TIME_IS_VALID (GST_BUFFER_DTS (last_buf))) {
    last_dts = gst_util_uint64_scale_round (GST_BUFFER_DTS (last_buf),
        atom_trak_get_timescale (pad->trak), GST_SECOND);
    pts_offset =
        (gint64) (gst_util_uint64_scale_round (GST_BUFFER_PTS (last_buf),
            atom_trak_get_timescale (pad->trak), GST_SECOND) - last_dts);

  } else {
    pts_offset = 0;
    last_dts = gst_util_uint64_scale_round (GST_BUFFER_PTS (last_buf),
        atom_trak_get_timescale (pad->trak), GST_SECOND);
  }
  GST_DEBUG ("dts: %" GST_TIME_FORMAT " pts: %" GST_TIME_FORMAT
      " timebase_dts: %d pts_offset: %d",
      GST_TIME_ARGS (GST_BUFFER_DTS (last_buf)),
      GST_TIME_ARGS (GST_BUFFER_PTS (last_buf)),
      (int) (last_dts), (int) (pts_offset));

  /*
   * Each buffer starts a new chunk, so we can assume the buffer
   * duration is the chunk duration
   */
  if (GST_CLOCK_TIME_IS_VALID (duration) && (duration > qtmux->longest_chunk ||
          !GST_CLOCK_TIME_IS_VALID (qtmux->longest_chunk))) {
    GST_DEBUG_OBJECT (qtmux, "New longest chunk found: %" GST_TIME_FORMAT
        ", pad %s", GST_TIME_ARGS (duration), GST_PAD_NAME (pad->collect.pad));
    qtmux->longest_chunk = duration;
  }

  /* now we go and register this buffer/sample all over */
  ret = gst_qt_mux_register_and_push_sample (qtmux, pad, last_buf,
      buf == NULL, nsamples, last_dts, scaled_duration, sample_size,
      chunk_offset, sync, TRUE, pts_offset);

  /* if this is sparse and we have a next buffer, check if there is any gap
   * between them to insert an empty sample */
  if (pad->sparse && buf) {
    if (pad->create_empty_buffer) {
      GstBuffer *empty_buf;
      gint64 empty_duration =
          GST_BUFFER_TIMESTAMP (buf) - (GST_BUFFER_TIMESTAMP (last_buf) +
          duration);
      gint64 empty_duration_scaled;

      empty_buf = pad->create_empty_buffer (pad, empty_duration);

      empty_duration_scaled = gst_util_uint64_scale_round (empty_duration,
          atom_trak_get_timescale (pad->trak), GST_SECOND);

      pad->total_bytes += gst_buffer_get_size (empty_buf);
      pad->total_duration += duration;

      ret =
          gst_qt_mux_register_and_push_sample (qtmux, pad, empty_buf, FALSE, 1,
          last_dts + scaled_duration, empty_duration_scaled,
          gst_buffer_get_size (empty_buf), qtmux->mdat_size, sync, TRUE, 0);
    } else {
      /* our only case currently is tx3g subtitles, so there is no reason to fill this yet */
      g_assert_not_reached ();
      GST_WARNING_OBJECT (qtmux,
          "no empty buffer creation function found for pad %s",
          GST_PAD_NAME (pad->collect.pad));
    }
  }

  if (buf)
    gst_buffer_unref (buf);

exit:

  return ret;

  /* ERRORS */
bail:
  {
    if (buf)
      gst_buffer_unref (buf);
    gst_buffer_unref (last_buf);
    return GST_FLOW_ERROR;
  }
fragmented_sample:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Audio buffer contains fragmented sample."));
    goto bail;
  }
not_negotiated:
  {
    GST_ELEMENT_ERROR (qtmux, CORE, NEGOTIATION, (NULL),
        ("format wasn't negotiated before buffer flow on pad %s",
            GST_PAD_NAME (pad->collect.pad)));
    if (buf)
      gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

/*
 * DTS running time can be negative. There is no way to represent that in
 * MP4 however, thus we need to offset DTS so that it starts from 0.
 */
static void
gst_qt_pad_adjust_buffer_dts (GstQTMux * qtmux, GstQTPad * pad,
    GstCollectData * cdata, GstBuffer ** buf)
{
  GstClockTime pts;
  gint64 dts;

  pts = GST_BUFFER_PTS (*buf);
  dts = GST_COLLECT_PADS_DTS (cdata);

  GST_LOG_OBJECT (qtmux, "selected pad %s with PTS %" GST_TIME_FORMAT
      " and DTS %" GST_STIME_FORMAT, GST_PAD_NAME (cdata->pad),
      GST_TIME_ARGS (pts), GST_STIME_ARGS (dts));

  if (!GST_CLOCK_TIME_IS_VALID (pad->dts_adjustment)) {
    if (GST_CLOCK_STIME_IS_VALID (dts) && dts < 0)
      pad->dts_adjustment = -dts;
    else
      pad->dts_adjustment = 0;
  }

  if (pad->dts_adjustment > 0) {
    *buf = gst_buffer_make_writable (*buf);

    dts += pad->dts_adjustment;

    if (GST_CLOCK_TIME_IS_VALID (pts))
      pts += pad->dts_adjustment;

    if (GST_CLOCK_STIME_IS_VALID (dts) && dts < 0) {
      GST_WARNING_OBJECT (pad, "Decreasing DTS.");
      dts = 0;
    }

    if (pts < dts) {
      GST_WARNING_OBJECT (pad, "DTS is bigger then PTS");
      pts = dts;
    }

    GST_BUFFER_PTS (*buf) = pts;
    GST_BUFFER_DTS (*buf) = dts;

    GST_LOG_OBJECT (qtmux, "time adjusted to PTS %" GST_TIME_FORMAT
        " and DTS %" GST_TIME_FORMAT, GST_TIME_ARGS (pts), GST_TIME_ARGS (dts));
  }
}

static GstFlowReturn
gst_qt_mux_handle_buffer (GstCollectPads * pads, GstCollectData * cdata,
    GstBuffer * buf, gpointer user_data)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstQTMux *qtmux = GST_QT_MUX_CAST (user_data);
  GstQTPad *best_pad = NULL;

  if (G_UNLIKELY (qtmux->state == GST_QT_MUX_STATE_STARTED)) {
    if ((ret = gst_qt_mux_start_file (qtmux)) != GST_FLOW_OK)
      return ret;

    qtmux->state = GST_QT_MUX_STATE_DATA;
  }

  if (G_UNLIKELY (qtmux->state == GST_QT_MUX_STATE_EOS))
    return GST_FLOW_EOS;

  best_pad = (GstQTPad *) cdata;

  /* clipping already converted to running time */
  if (best_pad != NULL) {
    g_assert (buf);
    gst_qt_pad_adjust_buffer_dts (qtmux, best_pad, cdata, &buf);
    ret = gst_qt_mux_add_buffer (qtmux, best_pad, buf);
  } else {
    qtmux->state = GST_QT_MUX_STATE_EOS;
    ret = gst_qt_mux_stop_file (qtmux);
    if (ret == GST_FLOW_OK) {
      GST_DEBUG_OBJECT (qtmux, "Pushing eos");
      gst_pad_push_event (qtmux->srcpad, gst_event_new_eos ());
      ret = GST_FLOW_EOS;
    } else {
      GST_WARNING_OBJECT (qtmux, "Failed to stop file: %s",
          gst_flow_get_name (ret));
    }
  }

  return ret;
}

static gboolean
check_field (GQuark field_id, const GValue * value, gpointer user_data)
{
  GstStructure *structure = (GstStructure *) user_data;
  const GValue *other = gst_structure_id_get_value (structure, field_id);
  if (other == NULL)
    return FALSE;
  return gst_value_compare (value, other) == GST_VALUE_EQUAL;
}

static gboolean
gst_qtmux_caps_is_subset_full (GstQTMux * qtmux, GstCaps * subset,
    GstCaps * superset)
{
  GstStructure *sub_s = gst_caps_get_structure (subset, 0);
  GstStructure *sup_s = gst_caps_get_structure (superset, 0);

  return gst_structure_foreach (sub_s, check_field, sup_s);
}

static gboolean
gst_qt_mux_audio_sink_set_caps (GstQTPad * qtpad, GstCaps * caps)
{
  GstPad *pad = qtpad->collect.pad;
  GstQTMux *qtmux = GST_QT_MUX_CAST (gst_pad_get_parent (pad));
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  GstStructure *structure;
  const gchar *mimetype;
  gint rate, channels;
  const GValue *value = NULL;
  const GstBuffer *codec_data = NULL;
  GstQTMuxFormat format;
  AudioSampleEntry entry = { 0, };
  AtomInfo *ext_atom = NULL;
  gint constant_size = 0;
  const gchar *stream_format;

  qtpad->prepare_buf_func = NULL;

  /* does not go well to renegotiate stream mid-way, unless
   * the old caps are a subset of the new one (this means upstream
   * added more info to the caps, as both should be 'fixed' caps) */
  if (qtpad->fourcc) {
    GstCaps *current_caps;

    current_caps = gst_pad_get_current_caps (pad);
    g_assert (caps != NULL);

    if (!gst_qtmux_caps_is_subset_full (qtmux, current_caps, caps)) {
      gst_caps_unref (current_caps);
      goto refuse_renegotiation;
    }
    GST_DEBUG_OBJECT (qtmux,
        "pad %s accepted renegotiation to %" GST_PTR_FORMAT " from %"
        GST_PTR_FORMAT, GST_PAD_NAME (pad), caps, current_caps);
    gst_caps_unref (current_caps);
  }

  GST_DEBUG_OBJECT (qtmux, "%s:%s, caps=%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  format = qtmux_klass->format;
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* common info */
  if (!gst_structure_get_int (structure, "channels", &channels) ||
      !gst_structure_get_int (structure, "rate", &rate)) {
    goto refuse_caps;
  }

  /* optional */
  value = gst_structure_get_value (structure, "codec_data");
  if (value != NULL)
    codec_data = gst_value_get_buffer (value);

  qtpad->is_out_of_order = FALSE;

  /* set common properties */
  entry.sample_rate = rate;
  entry.channels = channels;
  /* default */
  entry.sample_size = 16;
  /* this is the typical compressed case */
  if (format == GST_QT_MUX_FORMAT_QT) {
    entry.version = 1;
    entry.compression_id = -2;
  }

  /* now map onto a fourcc, and some extra properties */
  if (strcmp (mimetype, "audio/mpeg") == 0) {
    gint mpegversion = 0, mpegaudioversion = 0;
    gint layer = -1;

    gst_structure_get_int (structure, "mpegversion", &mpegversion);
    switch (mpegversion) {
      case 1:
        gst_structure_get_int (structure, "layer", &layer);
        gst_structure_get_int (structure, "mpegaudioversion",
            &mpegaudioversion);

        /* mp1/2/3 */
        /* note: QuickTime player does not like mp3 either way in iso/mp4 */
        if (format == GST_QT_MUX_FORMAT_QT)
          entry.fourcc = FOURCC__mp3;
        else {
          entry.fourcc = FOURCC_mp4a;
          ext_atom =
              build_esds_extension (qtpad->trak, ESDS_OBJECT_TYPE_MPEG1_P3,
              ESDS_STREAM_TYPE_AUDIO, codec_data, qtpad->avg_bitrate,
              qtpad->max_bitrate);
        }
        if (layer == 1) {
          g_warn_if_fail (format == GST_QT_MUX_FORMAT_MP4);
          entry.samples_per_packet = 384;
        } else if (layer == 2) {
          g_warn_if_fail (format == GST_QT_MUX_FORMAT_MP4);
          entry.samples_per_packet = 1152;
        } else {
          g_warn_if_fail (layer == 3);
          entry.samples_per_packet = (mpegaudioversion <= 1) ? 1152 : 576;
        }
        entry.bytes_per_sample = 2;
        break;
      case 4:

        /* check stream-format */
        stream_format = gst_structure_get_string (structure, "stream-format");
        if (stream_format) {
          if (strcmp (stream_format, "raw") != 0) {
            GST_WARNING_OBJECT (qtmux, "Unsupported AAC stream-format %s, "
                "please use 'raw'", stream_format);
            goto refuse_caps;
          }
        } else {
          GST_WARNING_OBJECT (qtmux, "No stream-format present in caps, "
              "assuming 'raw'");
        }

        if (!codec_data || gst_buffer_get_size ((GstBuffer *) codec_data) < 2)
          GST_WARNING_OBJECT (qtmux, "no (valid) codec_data for AAC audio");
        else {
          guint8 profile;

          gst_buffer_extract ((GstBuffer *) codec_data, 0, &profile, 1);
          /* warn if not Low Complexity profile */
          profile >>= 3;
          if (profile != 2)
            GST_WARNING_OBJECT (qtmux,
                "non-LC AAC may not run well on (Apple) QuickTime/iTunes");
        }

        /* AAC */
        entry.fourcc = FOURCC_mp4a;

        if (format == GST_QT_MUX_FORMAT_QT)
          ext_atom = build_mov_aac_extension (qtpad->trak, codec_data,
              qtpad->avg_bitrate, qtpad->max_bitrate);
        else
          ext_atom =
              build_esds_extension (qtpad->trak, ESDS_OBJECT_TYPE_MPEG4_P3,
              ESDS_STREAM_TYPE_AUDIO, codec_data, qtpad->avg_bitrate,
              qtpad->max_bitrate);
        break;
      default:
        break;
    }
  } else if (strcmp (mimetype, "audio/AMR") == 0) {
    entry.fourcc = FOURCC_samr;
    entry.sample_size = 16;
    entry.samples_per_packet = 160;
    entry.bytes_per_sample = 2;
    ext_atom = build_amr_extension ();
  } else if (strcmp (mimetype, "audio/AMR-WB") == 0) {
    entry.fourcc = FOURCC_sawb;
    entry.sample_size = 16;
    entry.samples_per_packet = 320;
    entry.bytes_per_sample = 2;
    ext_atom = build_amr_extension ();
  } else if (strcmp (mimetype, "audio/x-raw") == 0) {
    GstAudioInfo info;

    gst_audio_info_init (&info);
    if (!gst_audio_info_from_caps (&info, caps))
      goto refuse_caps;

    /* spec has no place for a distinction in these */
    if (info.finfo->width != info.finfo->depth) {
      GST_DEBUG_OBJECT (qtmux, "width must be same as depth!");
      goto refuse_caps;
    }

    if ((info.finfo->flags & GST_AUDIO_FORMAT_FLAG_SIGNED)) {
      if (info.finfo->endianness == G_LITTLE_ENDIAN)
        entry.fourcc = FOURCC_sowt;
      else if (info.finfo->endianness == G_BIG_ENDIAN)
        entry.fourcc = FOURCC_twos;
      else
        entry.fourcc = FOURCC_sowt;
      /* maximum backward compatibility; only new version for > 16 bit */
      if (info.finfo->depth <= 16)
        entry.version = 0;
      /* not compressed in any case */
      entry.compression_id = 0;
      /* QT spec says: max at 16 bit even if sample size were actually larger,
       * however, most players (e.g. QuickTime!) seem to disagree, so ... */
      entry.sample_size = info.finfo->depth;
      entry.bytes_per_sample = info.finfo->depth / 8;
      entry.samples_per_packet = 1;
      entry.bytes_per_packet = info.finfo->depth / 8;
      entry.bytes_per_frame = entry.bytes_per_packet * info.channels;
    } else {
      if (info.finfo->width == 8 && info.finfo->depth == 8) {
        /* fall back to old 8-bit version */
        entry.fourcc = FOURCC_raw_;
        entry.version = 0;
        entry.compression_id = 0;
        entry.sample_size = 8;
      } else {
        GST_DEBUG_OBJECT (qtmux, "non 8-bit PCM must be signed");
        goto refuse_caps;
      }
    }
    constant_size = (info.finfo->depth / 8) * info.channels;
  } else if (strcmp (mimetype, "audio/x-alaw") == 0) {
    entry.fourcc = FOURCC_alaw;
    entry.samples_per_packet = 1023;
    entry.bytes_per_sample = 2;
  } else if (strcmp (mimetype, "audio/x-mulaw") == 0) {
    entry.fourcc = FOURCC_ulaw;
    entry.samples_per_packet = 1023;
    entry.bytes_per_sample = 2;
  } else if (strcmp (mimetype, "audio/x-adpcm") == 0) {
    gint blocksize;
    if (!gst_structure_get_int (structure, "block_align", &blocksize)) {
      GST_DEBUG_OBJECT (qtmux, "broken caps, block_align missing");
      goto refuse_caps;
    }
    /* Currently only supports WAV-style IMA ADPCM, for which the codec id is
       0x11 */
    entry.fourcc = MS_WAVE_FOURCC (0x11);
    /* 4 byte header per channel (including one sample). 2 samples per byte
       remaining. Simplifying gives the following (samples per block per
       channel) */
    entry.samples_per_packet = 2 * blocksize / channels - 7;
    entry.bytes_per_sample = 2;

    entry.bytes_per_frame = blocksize;
    entry.bytes_per_packet = blocksize / channels;
    /* ADPCM has constant size packets */
    constant_size = 1;
    /* TODO: I don't really understand why this helps, but it does! Constant
     * size and compression_id of -2 seem to be incompatible, and other files
     * in the wild use this too. */
    entry.compression_id = -1;

    ext_atom = build_ima_adpcm_extension (channels, rate, blocksize);
  } else if (strcmp (mimetype, "audio/x-alac") == 0) {
    GstBuffer *codec_config;
    gint len;
    GstMapInfo map;

    entry.fourcc = FOURCC_alac;
    gst_buffer_map ((GstBuffer *) codec_data, &map, GST_MAP_READ);
    /* let's check if codec data already comes with 'alac' atom prefix */
    if (!codec_data || (len = map.size) < 28) {
      GST_DEBUG_OBJECT (qtmux, "broken caps, codec data missing");
      gst_buffer_unmap ((GstBuffer *) codec_data, &map);
      goto refuse_caps;
    }
    if (GST_READ_UINT32_LE (map.data + 4) == FOURCC_alac) {
      len -= 8;
      codec_config =
          gst_buffer_copy_region ((GstBuffer *) codec_data,
          GST_BUFFER_COPY_MEMORY, 8, len);
    } else {
      codec_config = gst_buffer_ref ((GstBuffer *) codec_data);
    }
    gst_buffer_unmap ((GstBuffer *) codec_data, &map);
    if (len != 28) {
      /* does not look good, but perhaps some trailing unneeded stuff */
      GST_WARNING_OBJECT (qtmux, "unexpected codec-data size, possibly broken");
    }
    if (format == GST_QT_MUX_FORMAT_QT)
      ext_atom = build_mov_alac_extension (codec_config);
    else
      ext_atom = build_codec_data_extension (FOURCC_alac, codec_config);
    /* set some more info */
    gst_buffer_map (codec_config, &map, GST_MAP_READ);
    entry.bytes_per_sample = 2;
    entry.samples_per_packet = GST_READ_UINT32_BE (map.data + 4);
    gst_buffer_unmap (codec_config, &map);
    gst_buffer_unref (codec_config);
  } else if (strcmp (mimetype, "audio/x-ac3") == 0) {
    entry.fourcc = FOURCC_ac_3;

    /* Fixed values according to TS 102 366 but it also mentions that
     * they should be ignored */
    entry.channels = 2;
    entry.sample_size = 16;

    /* AC-3 needs an extension atom but its data can only be obtained from
     * the stream itself. Abuse the prepare_buf_func so we parse a frame
     * and get the needed data */
    qtpad->prepare_buf_func = gst_qt_mux_prepare_parse_ac3_frame;
  } else if (strcmp (mimetype, "audio/x-opus") == 0) {
    /* Based on the specification defined in:
     * https://www.opus-codec.org/docs/opus_in_isobmff.html */
    guint8 channels, mapping_family, stream_count, coupled_count;
    guint16 pre_skip;
    gint16 output_gain;
    guint32 rate;
    guint8 channel_mapping[256];
    const GValue *streamheader;
    const GValue *first_element;
    GstBuffer *header;

    entry.fourcc = FOURCC_opus;
    entry.sample_size = 16;

    streamheader = gst_structure_get_value (structure, "streamheader");
    if (streamheader && GST_VALUE_HOLDS_ARRAY (streamheader) &&
        gst_value_array_get_size (streamheader) != 0) {
      first_element = gst_value_array_get_value (streamheader, 0);
      header = gst_value_get_buffer (first_element);
      if (!gst_codec_utils_opus_parse_header (header, &rate, &channels,
              &mapping_family, &stream_count, &coupled_count, channel_mapping,
              &pre_skip, &output_gain)) {
        GST_ERROR_OBJECT (qtmux, "Incomplete OpusHead");
        goto refuse_caps;
      }
    } else {
      GST_WARNING_OBJECT (qtmux,
          "no streamheader field in caps %" GST_PTR_FORMAT, caps);

      if (!gst_codec_utils_opus_parse_caps (caps, &rate, &channels,
              &mapping_family, &stream_count, &coupled_count,
              channel_mapping)) {
        GST_ERROR_OBJECT (qtmux, "Incomplete Opus caps");
        goto refuse_caps;
      }
      pre_skip = 0;
      output_gain = 0;
    }

    entry.channels = channels;
    ext_atom = build_opus_extension (rate, channels, mapping_family,
        stream_count, coupled_count, channel_mapping, pre_skip, output_gain);
  }

  if (!entry.fourcc)
    goto refuse_caps;

  /* ok, set the pad info accordingly */
  qtpad->fourcc = entry.fourcc;
  qtpad->sample_size = constant_size;
  qtpad->trak_ste =
      (SampleTableEntry *) atom_trak_set_audio_type (qtpad->trak,
      qtmux->context, &entry,
      qtmux->trak_timescale ? qtmux->trak_timescale : entry.sample_rate,
      ext_atom, constant_size);

  gst_object_unref (qtmux);
  return TRUE;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (qtmux, "pad %s refused caps %" GST_PTR_FORMAT,
        GST_PAD_NAME (pad), caps);
    gst_object_unref (qtmux);
    return FALSE;
  }
refuse_renegotiation:
  {
    GST_WARNING_OBJECT (qtmux,
        "pad %s refused renegotiation to %" GST_PTR_FORMAT,
        GST_PAD_NAME (pad), caps);
    gst_object_unref (qtmux);
    return FALSE;
  }
}

/* scale rate up or down by factor of 10 to fit into [1000,10000] interval */
static guint32
adjust_rate (guint64 rate)
{
  if (rate == 0)
    return 10000;

  while (rate >= 10000)
    rate /= 10;

  while (rate < 1000)
    rate *= 10;

  return (guint32) rate;
}

static gboolean
gst_qt_mux_video_sink_set_caps (GstQTPad * qtpad, GstCaps * caps)
{
  GstPad *pad = qtpad->collect.pad;
  GstQTMux *qtmux = GST_QT_MUX_CAST (gst_pad_get_parent (pad));
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  GstStructure *structure;
  const gchar *mimetype;
  gint width, height, depth = -1;
  gint framerate_num, framerate_den;
  guint32 rate;
  const GValue *value = NULL;
  const GstBuffer *codec_data = NULL;
  VisualSampleEntry entry = { 0, };
  GstQTMuxFormat format;
  AtomInfo *ext_atom = NULL;
  GList *ext_atom_list = NULL;
  gboolean sync = FALSE;
  int par_num, par_den;

  qtpad->prepare_buf_func = NULL;

  /* does not go well to renegotiate stream mid-way, unless
   * the old caps are a subset of the new one (this means upstream
   * added more info to the caps, as both should be 'fixed' caps) */
  if (qtpad->fourcc) {
    GstCaps *current_caps;

    current_caps = gst_pad_get_current_caps (pad);
    g_assert (caps != NULL);

    if (!gst_qtmux_caps_is_subset_full (qtmux, current_caps, caps)) {
      gst_caps_unref (current_caps);
      goto refuse_renegotiation;
    }
    GST_DEBUG_OBJECT (qtmux,
        "pad %s accepted renegotiation to %" GST_PTR_FORMAT " from %"
        GST_PTR_FORMAT, GST_PAD_NAME (pad), caps, current_caps);
    gst_caps_unref (current_caps);
  }

  GST_DEBUG_OBJECT (qtmux, "%s:%s, caps=%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  format = qtmux_klass->format;
  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  /* required parts */
  if (!gst_structure_get_int (structure, "width", &width) ||
      !gst_structure_get_int (structure, "height", &height))
    goto refuse_caps;

  /* optional */
  depth = -1;
  /* works as a default timebase */
  framerate_num = 10000;
  framerate_den = 1;
  gst_structure_get_fraction (structure, "framerate", &framerate_num,
      &framerate_den);
  gst_structure_get_int (structure, "depth", &depth);
  value = gst_structure_get_value (structure, "codec_data");
  if (value != NULL)
    codec_data = gst_value_get_buffer (value);

  par_num = 1;
  par_den = 1;
  gst_structure_get_fraction (structure, "pixel-aspect-ratio", &par_num,
      &par_den);

  qtpad->is_out_of_order = FALSE;

  /* bring frame numerator into a range that ensures both reasonable resolution
   * as well as a fair duration */
  rate = qtmux->trak_timescale ?
      qtmux->trak_timescale : adjust_rate (framerate_num);
  GST_DEBUG_OBJECT (qtmux, "Rate of video track selected: %" G_GUINT32_FORMAT,
      rate);

  /* set common properties */
  entry.width = width;
  entry.height = height;
  entry.par_n = par_num;
  entry.par_d = par_den;
  /* should be OK according to qt and iso spec, override if really needed */
  entry.color_table_id = -1;
  entry.frame_count = 1;
  entry.depth = 24;

  /* sync entries by default */
  sync = TRUE;

  /* now map onto a fourcc, and some extra properties */
  if (strcmp (mimetype, "video/x-raw") == 0) {
    const gchar *format;
    GstVideoFormat fmt;
    const GstVideoFormatInfo *vinfo;

    format = gst_structure_get_string (structure, "format");
    fmt = gst_video_format_from_string (format);
    vinfo = gst_video_format_get_info (fmt);

    switch (fmt) {
      case GST_VIDEO_FORMAT_UYVY:
        if (depth == -1)
          depth = 24;
        entry.fourcc = FOURCC_2vuy;
        entry.depth = depth;
        sync = FALSE;
        break;
      case GST_VIDEO_FORMAT_v210:
        if (depth == -1)
          depth = 24;
        entry.fourcc = FOURCC_v210;
        entry.depth = depth;
        sync = FALSE;
        break;
      default:
        if (GST_VIDEO_FORMAT_INFO_FLAGS (vinfo) & GST_VIDEO_FORMAT_FLAG_RGB) {
          entry.fourcc = FOURCC_raw_;
          entry.depth = GST_VIDEO_FORMAT_INFO_PSTRIDE (vinfo, 0) * 8;
          sync = FALSE;
        }
        break;
    }
  } else if (strcmp (mimetype, "video/x-h263") == 0) {
    ext_atom = NULL;
    if (format == GST_QT_MUX_FORMAT_QT)
      entry.fourcc = FOURCC_h263;
    else
      entry.fourcc = FOURCC_s263;
    ext_atom = build_h263_extension ();
    if (ext_atom != NULL)
      ext_atom_list = g_list_prepend (ext_atom_list, ext_atom);
  } else if (strcmp (mimetype, "video/x-divx") == 0 ||
      strcmp (mimetype, "video/mpeg") == 0) {
    gint version = 0;

    if (strcmp (mimetype, "video/x-divx") == 0) {
      gst_structure_get_int (structure, "divxversion", &version);
      version = version == 5 ? 1 : 0;
    } else {
      gst_structure_get_int (structure, "mpegversion", &version);
      version = version == 4 ? 1 : 0;
    }
    if (version) {
      entry.fourcc = FOURCC_mp4v;
      ext_atom =
          build_esds_extension (qtpad->trak, ESDS_OBJECT_TYPE_MPEG4_P2,
          ESDS_STREAM_TYPE_VISUAL, codec_data, qtpad->avg_bitrate,
          qtpad->max_bitrate);
      if (ext_atom != NULL)
        ext_atom_list = g_list_prepend (ext_atom_list, ext_atom);
      if (!codec_data)
        GST_WARNING_OBJECT (qtmux, "no codec_data for MPEG4 video; "
            "output might not play in Apple QuickTime (try global-headers?)");
    }
  } else if (strcmp (mimetype, "video/x-h264") == 0) {
    /* check if we accept these caps */
    if (gst_structure_has_field (structure, "stream-format")) {
      const gchar *format;
      const gchar *alignment;

      format = gst_structure_get_string (structure, "stream-format");
      alignment = gst_structure_get_string (structure, "alignment");

      if (strcmp (format, "avc") != 0 || alignment == NULL ||
          strcmp (alignment, "au") != 0) {
        GST_WARNING_OBJECT (qtmux, "Rejecting h264 caps, qtmux only accepts "
            "avc format with AU aligned samples");
        goto refuse_caps;
      }
    } else {
      GST_WARNING_OBJECT (qtmux, "no stream-format field in h264 caps");
      goto refuse_caps;
    }

    if (!codec_data) {
      GST_WARNING_OBJECT (qtmux, "no codec_data in h264 caps");
      goto refuse_caps;
    }

    entry.fourcc = FOURCC_avc1;
    if (qtpad->avg_bitrate == 0) {
      gint avg_bitrate = 0;
      gst_structure_get_int (structure, "bitrate", &avg_bitrate);
      qtpad->avg_bitrate = avg_bitrate;
    }
    ext_atom = build_btrt_extension (0, qtpad->avg_bitrate, qtpad->max_bitrate);
    if (ext_atom != NULL)
      ext_atom_list = g_list_prepend (ext_atom_list, ext_atom);
    ext_atom = build_codec_data_extension (FOURCC_avcC, codec_data);
    if (ext_atom != NULL)
      ext_atom_list = g_list_prepend (ext_atom_list, ext_atom);
  } else if (strcmp (mimetype, "video/x-svq") == 0) {
    gint version = 0;
    const GstBuffer *seqh = NULL;
    const GValue *seqh_value;
    gdouble gamma = 0;

    gst_structure_get_int (structure, "svqversion", &version);
    if (version == 3) {
      entry.fourcc = FOURCC_SVQ3;
      entry.version = 3;
      entry.depth = 32;

      seqh_value = gst_structure_get_value (structure, "seqh");
      if (seqh_value) {
        seqh = gst_value_get_buffer (seqh_value);
        ext_atom = build_SMI_atom (seqh);
        if (ext_atom)
          ext_atom_list = g_list_prepend (ext_atom_list, ext_atom);
      }

      /* we need to add the gamma anyway because quicktime might crash
       * when it doesn't find it */
      if (!gst_structure_get_double (structure, "applied-gamma", &gamma)) {
        /* it seems that using 0 here makes it ignored */
        gamma = 0.0;
      }
      ext_atom = build_gama_atom (gamma);
      if (ext_atom)
        ext_atom_list = g_list_prepend (ext_atom_list, ext_atom);
    } else {
      GST_WARNING_OBJECT (qtmux, "SVQ version %d not supported. Please file "
          "a bug at http://bugzilla.gnome.org", version);
    }
  } else if (strcmp (mimetype, "video/x-dv") == 0) {
    gint version = 0;
    gboolean pal = TRUE;

    sync = FALSE;
    if (framerate_num != 25 || framerate_den != 1)
      pal = FALSE;
    gst_structure_get_int (structure, "dvversion", &version);
    /* fall back to typical one */
    if (!version)
      version = 25;
    switch (version) {
      case 25:
        if (pal)
          entry.fourcc = FOURCC_dvcp;
        else
          entry.fourcc = FOURCC_dvc_;
        break;
      case 50:
        if (pal)
          entry.fourcc = FOURCC_dv5p;
        else
          entry.fourcc = FOURCC_dv5n;
        break;
      default:
        GST_WARNING_OBJECT (qtmux, "unrecognized dv version");
        break;
    }
  } else if (strcmp (mimetype, "image/jpeg") == 0) {
    entry.fourcc = FOURCC_jpeg;
    sync = FALSE;
  } else if (strcmp (mimetype, "image/x-j2c") == 0 ||
      strcmp (mimetype, "image/x-jpc") == 0) {
    const gchar *colorspace;
    const GValue *cmap_array;
    const GValue *cdef_array;
    gint ncomp = 0;
    gint fields = 1;

    if (strcmp (mimetype, "image/x-jpc") == 0) {
      qtpad->prepare_buf_func = gst_qt_mux_prepare_jpc_buffer;
    }

    gst_structure_get_int (structure, "num-components", &ncomp);
    gst_structure_get_int (structure, "fields", &fields);
    cmap_array = gst_structure_get_value (structure, "component-map");
    cdef_array = gst_structure_get_value (structure, "channel-definitions");

    ext_atom = NULL;
    entry.fourcc = FOURCC_mjp2;
    sync = FALSE;

    colorspace = gst_structure_get_string (structure, "colorspace");
    if (colorspace &&
        (ext_atom =
            build_jp2h_extension (width, height, colorspace, ncomp, cmap_array,
                cdef_array)) != NULL) {
      ext_atom_list = g_list_append (ext_atom_list, ext_atom);

      ext_atom = build_fiel_extension (fields);
      if (ext_atom)
        ext_atom_list = g_list_append (ext_atom_list, ext_atom);

      ext_atom = build_jp2x_extension (codec_data);
      if (ext_atom)
        ext_atom_list = g_list_append (ext_atom_list, ext_atom);
    } else {
      GST_DEBUG_OBJECT (qtmux, "missing or invalid fourcc in jp2 caps");
      goto refuse_caps;
    }
  } else if (strcmp (mimetype, "video/x-vp8") == 0) {
    entry.fourcc = FOURCC_VP80;
    sync = FALSE;
  } else if (strcmp (mimetype, "video/x-dirac") == 0) {
    entry.fourcc = FOURCC_drac;
  } else if (strcmp (mimetype, "video/x-qt-part") == 0) {
    guint32 fourcc;

    gst_structure_get_uint (structure, "format", &fourcc);
    entry.fourcc = fourcc;
  } else if (strcmp (mimetype, "video/x-mp4-part") == 0) {
    guint32 fourcc;

    gst_structure_get_uint (structure, "format", &fourcc);
    entry.fourcc = fourcc;
  } else if (strcmp (mimetype, "video/x-prores") == 0) {
    const gchar *variant;

    variant = gst_structure_get_string (structure, "format");
    if (!variant || !g_strcmp0 (variant, "standard"))
      entry.fourcc = FOURCC_apcn;
    else if (!g_strcmp0 (variant, "lt"))
      entry.fourcc = FOURCC_apcs;
    else if (!g_strcmp0 (variant, "hq"))
      entry.fourcc = FOURCC_apch;
    else if (!g_strcmp0 (variant, "proxy"))
      entry.fourcc = FOURCC_ap4h;
  }

  if (!entry.fourcc)
    goto refuse_caps;

  /* ok, set the pad info accordingly */
  qtpad->fourcc = entry.fourcc;
  qtpad->sync = sync;
  qtpad->trak_ste =
      (SampleTableEntry *) atom_trak_set_video_type (qtpad->trak,
      qtmux->context, &entry, rate, ext_atom_list);

  gst_object_unref (qtmux);
  return TRUE;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (qtmux, "pad %s refused caps %" GST_PTR_FORMAT,
        GST_PAD_NAME (pad), caps);
    gst_object_unref (qtmux);
    return FALSE;
  }
refuse_renegotiation:
  {
    GST_WARNING_OBJECT (qtmux,
        "pad %s refused renegotiation to %" GST_PTR_FORMAT, GST_PAD_NAME (pad),
        caps);
    gst_object_unref (qtmux);
    return FALSE;
  }
}

static gboolean
gst_qt_mux_subtitle_sink_set_caps (GstQTPad * qtpad, GstCaps * caps)
{
  GstPad *pad = qtpad->collect.pad;
  GstQTMux *qtmux = GST_QT_MUX_CAST (gst_pad_get_parent (pad));
  GstStructure *structure;
  SubtitleSampleEntry entry = { 0, };

  /* does not go well to renegotiate stream mid-way, unless
   * the old caps are a subset of the new one (this means upstream
   * added more info to the caps, as both should be 'fixed' caps) */
  if (qtpad->fourcc) {
    GstCaps *current_caps;

    current_caps = gst_pad_get_current_caps (pad);
    g_assert (caps != NULL);

    if (!gst_qtmux_caps_is_subset_full (qtmux, current_caps, caps)) {
      gst_caps_unref (current_caps);
      goto refuse_renegotiation;
    }
    GST_DEBUG_OBJECT (qtmux,
        "pad %s accepted renegotiation to %" GST_PTR_FORMAT " from %"
        GST_PTR_FORMAT, GST_PAD_NAME (pad), caps, current_caps);
    gst_caps_unref (current_caps);
  }

  GST_DEBUG_OBJECT (qtmux, "%s:%s, caps=%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  /* subtitles default */
  subtitle_sample_entry_init (&entry);
  qtpad->is_out_of_order = FALSE;
  qtpad->sync = FALSE;
  qtpad->sparse = TRUE;
  qtpad->prepare_buf_func = NULL;

  structure = gst_caps_get_structure (caps, 0);

  if (gst_structure_has_name (structure, "text/x-raw")) {
    const gchar *format = gst_structure_get_string (structure, "format");
    if (format && strcmp (format, "utf8") == 0) {
      entry.fourcc = FOURCC_tx3g;
      qtpad->prepare_buf_func = gst_qt_mux_prepare_tx3g_buffer;
      qtpad->create_empty_buffer = gst_qt_mux_create_empty_tx3g_buffer;
    }
  }

  if (!entry.fourcc)
    goto refuse_caps;

  qtpad->fourcc = entry.fourcc;
  qtpad->trak_ste =
      (SampleTableEntry *) atom_trak_set_subtitle_type (qtpad->trak,
      qtmux->context, &entry);

  gst_object_unref (qtmux);
  return TRUE;

  /* ERRORS */
refuse_caps:
  {
    GST_WARNING_OBJECT (qtmux, "pad %s refused caps %" GST_PTR_FORMAT,
        GST_PAD_NAME (pad), caps);
    gst_object_unref (qtmux);
    return FALSE;
  }
refuse_renegotiation:
  {
    GST_WARNING_OBJECT (qtmux,
        "pad %s refused renegotiation to %" GST_PTR_FORMAT, GST_PAD_NAME (pad),
        caps);
    gst_object_unref (qtmux);
    return FALSE;
  }
}

static gboolean
gst_qt_mux_sink_event (GstCollectPads * pads, GstCollectData * data,
    GstEvent * event, gpointer user_data)
{
  GstQTMux *qtmux;
  guint32 avg_bitrate = 0, max_bitrate = 0;
  GstPad *pad = data->pad;
  gboolean ret = TRUE;

  qtmux = GST_QT_MUX_CAST (user_data);
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps *caps;
      GstQTPad *collect_pad;

      gst_event_parse_caps (event, &caps);

      /* find stream data */
      collect_pad = (GstQTPad *) gst_pad_get_element_private (pad);
      g_assert (collect_pad);
      g_assert (collect_pad->set_caps);

      ret = collect_pad->set_caps (collect_pad, caps);
      gst_event_unref (event);
      event = NULL;
      break;
    }
    case GST_EVENT_TAG:{
      GstTagList *list;
      GstTagSetter *setter = GST_TAG_SETTER (qtmux);
      GstTagMergeMode mode;
      gchar *code;
      GstQTPad *collect_pad;

      GST_OBJECT_LOCK (qtmux);
      mode = gst_tag_setter_get_tag_merge_mode (setter);
      collect_pad = (GstQTPad *) gst_pad_get_element_private (pad);

      gst_event_parse_tag (event, &list);
      GST_DEBUG_OBJECT (qtmux, "received tag event on pad %s:%s : %"
          GST_PTR_FORMAT, GST_DEBUG_PAD_NAME (pad), list);

      if (gst_tag_list_get_scope (list) == GST_TAG_SCOPE_GLOBAL) {
        gst_tag_setter_merge_tags (setter, list, mode);
        qtmux->tags_changed = TRUE;
      } else {
        if (!collect_pad->tags)
          collect_pad->tags = gst_tag_list_new_empty ();
        gst_tag_list_insert (collect_pad->tags, list, mode);
        collect_pad->tags_changed = TRUE;
      }
      GST_OBJECT_UNLOCK (qtmux);

      if (gst_tag_list_get_uint (list, GST_TAG_BITRATE, &avg_bitrate) |
          gst_tag_list_get_uint (list, GST_TAG_MAXIMUM_BITRATE, &max_bitrate)) {
        GstQTPad *qtpad = gst_pad_get_element_private (pad);
        g_assert (qtpad);

        if (avg_bitrate > 0 && avg_bitrate < G_MAXUINT32)
          qtpad->avg_bitrate = avg_bitrate;
        if (max_bitrate > 0 && max_bitrate < G_MAXUINT32)
          qtpad->max_bitrate = max_bitrate;
      }

      if (gst_tag_list_get_string (list, GST_TAG_LANGUAGE_CODE, &code)) {
        const char *iso_code = gst_tag_get_language_code_iso_639_2T (code);
        if (iso_code) {
          GstQTPad *qtpad = gst_pad_get_element_private (pad);
          g_assert (qtpad);
          if (qtpad->trak) {
            /* https://developer.apple.com/library/mac/#documentation/QuickTime/QTFF/QTFFChap4/qtff4.html */
            qtpad->trak->mdia.mdhd.language_code =
                (iso_code[0] - 0x60) * 0x400 + (iso_code[1] - 0x60) * 0x20 +
                (iso_code[2] - 0x60);
          }
        }
        g_free (code);
      }

      gst_event_unref (event);
      event = NULL;
      ret = TRUE;
      break;
    }
    default:
      break;
  }

  if (event != NULL)
    return gst_collect_pads_event_default (pads, data, event, FALSE);

  return ret;
}

static void
gst_qt_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstQTMux *mux = GST_QT_MUX_CAST (element);
  GSList *walk;

  GST_DEBUG_OBJECT (element, "Releasing %s:%s", GST_DEBUG_PAD_NAME (pad));

  for (walk = mux->sinkpads; walk; walk = g_slist_next (walk)) {
    GstQTPad *qtpad = (GstQTPad *) walk->data;
    GST_DEBUG ("Checking %s:%s", GST_DEBUG_PAD_NAME (qtpad->collect.pad));
    if (qtpad->collect.pad == pad) {
      /* this is it, remove */
      mux->sinkpads = g_slist_delete_link (mux->sinkpads, walk);
      gst_element_remove_pad (element, pad);
      break;
    }
  }

  gst_collect_pads_remove_pad (mux->collect, pad);
}

static GstPad *
gst_qt_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstQTMux *qtmux = GST_QT_MUX_CAST (element);
  GstQTPad *collect_pad;
  GstPad *newpad;
  GstQTPadSetCapsFunc setcaps_func;
  gchar *name;
  gint pad_id;
  gboolean lock = TRUE;

  if (templ->direction != GST_PAD_SINK)
    goto wrong_direction;

  if (qtmux->state > GST_QT_MUX_STATE_STARTED)
    goto too_late;

  if (templ == gst_element_class_get_pad_template (klass, "audio_%u")) {
    setcaps_func = gst_qt_mux_audio_sink_set_caps;
    if (req_name != NULL && sscanf (req_name, "audio_%u", &pad_id) == 1) {
      name = g_strdup (req_name);
    } else {
      name = g_strdup_printf ("audio_%u", qtmux->audio_pads++);
    }
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%u")) {
    setcaps_func = gst_qt_mux_video_sink_set_caps;
    if (req_name != NULL && sscanf (req_name, "video_%u", &pad_id) == 1) {
      name = g_strdup (req_name);
    } else {
      name = g_strdup_printf ("video_%u", qtmux->video_pads++);
    }
  } else if (templ == gst_element_class_get_pad_template (klass, "subtitle_%u")) {
    setcaps_func = gst_qt_mux_subtitle_sink_set_caps;
    if (req_name != NULL && sscanf (req_name, "subtitle_%u", &pad_id) == 1) {
      name = g_strdup (req_name);
    } else {
      name = g_strdup_printf ("subtitle_%u", qtmux->subtitle_pads++);
    }
    lock = FALSE;
  } else
    goto wrong_template;

  GST_DEBUG_OBJECT (qtmux, "Requested pad: %s", name);

  /* create pad and add to collections */
  newpad = gst_pad_new_from_template (templ, name);
  g_free (name);
  collect_pad = (GstQTPad *)
      gst_collect_pads_add_pad (qtmux->collect, newpad, sizeof (GstQTPad),
      (GstCollectDataDestroyNotify) (gst_qt_mux_pad_reset), lock);
  /* set up pad */
  gst_qt_mux_pad_reset (collect_pad);
  collect_pad->trak = atom_trak_new (qtmux->context);
  atom_moov_add_trak (qtmux->moov, collect_pad->trak);

  qtmux->sinkpads = g_slist_append (qtmux->sinkpads, collect_pad);

  /* set up pad functions */
  collect_pad->set_caps = setcaps_func;

  gst_pad_set_active (newpad, TRUE);
  gst_element_add_pad (element, newpad);

  return newpad;

  /* ERRORS */
wrong_direction:
  {
    GST_WARNING_OBJECT (qtmux, "Request pad that is not a SINK pad.");
    return NULL;
  }
too_late:
  {
    GST_WARNING_OBJECT (qtmux, "Not providing request pad after stream start.");
    return NULL;
  }
wrong_template:
  {
    GST_WARNING_OBJECT (qtmux, "This is not our template!");
    return NULL;
  }
}

static void
gst_qt_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstQTMux *qtmux = GST_QT_MUX_CAST (object);

  GST_OBJECT_LOCK (qtmux);
  switch (prop_id) {
    case PROP_MOVIE_TIMESCALE:
      g_value_set_uint (value, qtmux->timescale);
      break;
    case PROP_TRAK_TIMESCALE:
      g_value_set_uint (value, qtmux->trak_timescale);
      break;
    case PROP_DO_CTTS:
      g_value_set_boolean (value, qtmux->guess_pts);
      break;
#ifndef GST_REMOVE_DEPRECATED
    case PROP_DTS_METHOD:
      g_value_set_enum (value, qtmux->dts_method);
      break;
#endif
    case PROP_FAST_START:
      g_value_set_boolean (value, qtmux->fast_start);
      break;
    case PROP_FAST_START_TEMP_FILE:
      g_value_set_string (value, qtmux->fast_start_file_path);
      break;
    case PROP_MOOV_RECOV_FILE:
      g_value_set_string (value, qtmux->moov_recov_file_path);
      break;
    case PROP_FRAGMENT_DURATION:
      g_value_set_uint (value, qtmux->fragment_duration);
      break;
    case PROP_STREAMABLE:
      g_value_set_boolean (value, qtmux->streamable);
      break;
    case PROP_RESERVED_MAX_DURATION:
      g_value_set_uint64 (value, qtmux->reserved_max_duration);
      break;
    case PROP_RESERVED_DURATION_REMAINING:
      if (qtmux->reserved_duration_remaining == GST_CLOCK_TIME_NONE)
        g_value_set_uint64 (value, qtmux->reserved_max_duration);
      else {
        GstClockTime remaining = qtmux->reserved_duration_remaining;

        /* Report the remaining space as the calculated remaining, minus
         * however much we've muxed since the last update */
        if (remaining > qtmux->muxed_since_last_update)
          remaining -= qtmux->muxed_since_last_update;
        else
          remaining = 0;
        GST_LOG_OBJECT (qtmux, "reserved duration remaining - reporting %"
            G_GUINT64_FORMAT "(%" G_GUINT64_FORMAT " - %" G_GUINT64_FORMAT,
            remaining, qtmux->reserved_duration_remaining,
            qtmux->muxed_since_last_update);
        g_value_set_uint64 (value, remaining);
      }
      break;
    case PROP_RESERVED_MOOV_UPDATE_PERIOD:
      g_value_set_uint64 (value, qtmux->reserved_moov_update_period);
      break;
    case PROP_RESERVED_BYTES_PER_SEC:
      g_value_set_uint (value, qtmux->reserved_bytes_per_sec_per_trak);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (qtmux);
}

static void
gst_qt_mux_generate_fast_start_file_path (GstQTMux * qtmux)
{
  gchar *tmp;

  g_free (qtmux->fast_start_file_path);
  qtmux->fast_start_file_path = NULL;

  tmp = g_strdup_printf ("%s%d", "qtmux", g_random_int ());
  qtmux->fast_start_file_path = g_build_filename (g_get_tmp_dir (), tmp, NULL);
  g_free (tmp);
}

static void
gst_qt_mux_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstQTMux *qtmux = GST_QT_MUX_CAST (object);

  GST_OBJECT_LOCK (qtmux);
  switch (prop_id) {
    case PROP_MOVIE_TIMESCALE:
      qtmux->timescale = g_value_get_uint (value);
      break;
    case PROP_TRAK_TIMESCALE:
      qtmux->trak_timescale = g_value_get_uint (value);
      break;
    case PROP_DO_CTTS:
      qtmux->guess_pts = g_value_get_boolean (value);
      break;
#ifndef GST_REMOVE_DEPRECATED
    case PROP_DTS_METHOD:
      qtmux->dts_method = g_value_get_enum (value);
      break;
#endif
    case PROP_FAST_START:
      qtmux->fast_start = g_value_get_boolean (value);
      break;
    case PROP_FAST_START_TEMP_FILE:
      g_free (qtmux->fast_start_file_path);
      qtmux->fast_start_file_path = g_value_dup_string (value);
      /* NULL means to generate a random one */
      if (!qtmux->fast_start_file_path) {
        gst_qt_mux_generate_fast_start_file_path (qtmux);
      }
      break;
    case PROP_MOOV_RECOV_FILE:
      g_free (qtmux->moov_recov_file_path);
      qtmux->moov_recov_file_path = g_value_dup_string (value);
      break;
    case PROP_FRAGMENT_DURATION:
      qtmux->fragment_duration = g_value_get_uint (value);
      break;
    case PROP_STREAMABLE:{
      GstQTMuxClass *qtmux_klass =
          (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
      if (qtmux_klass->format == GST_QT_MUX_FORMAT_ISML) {
        qtmux->streamable = g_value_get_boolean (value);
      }
      break;
    }
    case PROP_RESERVED_MAX_DURATION:
      qtmux->reserved_max_duration = g_value_get_uint64 (value);
      break;
    case PROP_RESERVED_MOOV_UPDATE_PERIOD:
      qtmux->reserved_moov_update_period = g_value_get_uint64 (value);
      break;
    case PROP_RESERVED_BYTES_PER_SEC:
      qtmux->reserved_bytes_per_sec_per_trak = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (qtmux);
}

static GstStateChangeReturn
gst_qt_mux_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstQTMux *qtmux = GST_QT_MUX_CAST (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (qtmux->collect);
      qtmux->state = GST_QT_MUX_STATE_STARTED;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_collect_pads_stop (qtmux->collect);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_qt_mux_reset (qtmux, TRUE);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

gboolean
gst_qt_mux_register (GstPlugin * plugin)
{
  GTypeInfo typeinfo = {
    sizeof (GstQTMuxClass),
    (GBaseInitFunc) gst_qt_mux_base_init,
    NULL,
    (GClassInitFunc) gst_qt_mux_class_init,
    NULL,
    NULL,
    sizeof (GstQTMux),
    0,
    (GInstanceInitFunc) gst_qt_mux_init,
  };
  static const GInterfaceInfo tag_setter_info = {
    NULL, NULL, NULL
  };
  static const GInterfaceInfo tag_xmp_writer_info = {
    NULL, NULL, NULL
  };
  GType type;
  GstQTMuxFormat format;
  GstQTMuxClassParams *params;
  guint i = 0;

  GST_DEBUG_CATEGORY_INIT (gst_qt_mux_debug, "qtmux", 0, "QT Muxer");

  GST_LOG ("Registering muxers");

  while (TRUE) {
    GstQTMuxFormatProp *prop;
    GstCaps *subtitle_caps;

    prop = &gst_qt_mux_format_list[i];
    format = prop->format;
    if (format == GST_QT_MUX_FORMAT_NONE)
      break;

    /* create a cache for these properties */
    params = g_new0 (GstQTMuxClassParams, 1);
    params->prop = prop;
    params->src_caps = gst_static_caps_get (&prop->src_caps);
    params->video_sink_caps = gst_static_caps_get (&prop->video_sink_caps);
    params->audio_sink_caps = gst_static_caps_get (&prop->audio_sink_caps);
    subtitle_caps = gst_static_caps_get (&prop->subtitle_sink_caps);
    if (!gst_caps_is_equal (subtitle_caps, GST_CAPS_NONE)) {
      params->subtitle_sink_caps = subtitle_caps;
    } else {
      gst_caps_unref (subtitle_caps);
    }

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, prop->type_name, &typeinfo,
        0);
    g_type_set_qdata (type, GST_QT_MUX_PARAMS_QDATA, (gpointer) params);
    g_type_add_interface_static (type, GST_TYPE_TAG_SETTER, &tag_setter_info);
    g_type_add_interface_static (type, GST_TYPE_TAG_XMP_WRITER,
        &tag_xmp_writer_info);

    if (!gst_element_register (plugin, prop->name, prop->rank, type))
      return FALSE;

    i++;
  }

  GST_LOG ("Finished registering muxers");

  /* FIXME: ideally classification tag should be added and
     registered in gstreamer core gsttaglist
   */

  GST_LOG ("Registering tags");

  gst_tag_register (GST_TAG_3GP_CLASSIFICATION, GST_TAG_FLAG_META,
      G_TYPE_STRING, GST_TAG_3GP_CLASSIFICATION, "content classification",
      gst_tag_merge_use_first);

  GST_LOG ("Finished registering tags");

  return TRUE;
}
