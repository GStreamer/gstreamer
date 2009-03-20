/* Quicktime muxer plugin for GStreamer
 * Copyright (C) 2008 Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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
 * SECTION:gstqtmux
 * @short_description: Muxer for quicktime(.mov) files
 *
 * <refsect2>
 * <para>
 * This element merges streams (audio and video) into qt(.mov) files.
 * </para>
 * <title>Example pipelines</title>
 * <para>
 * <programlisting>
 * gst-launch v4l2src num-buffers=500 ! video/x-raw-yuv,width=320,height=240 ! ffmpegcolorspace ! qtmux ! filesink location=video.mov
 * </programlisting>
 * Records a video stream captured from a v4l2 device and muxes it into a qt file.
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2008-08-27
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

#ifdef G_OS_WIN32
#include <io.h>                 /* lseek, open, close, read */
#undef lseek
#define lseek _lseeki64
#undef off_t
#define off_t guint64
#endif

#include "gstqtmux.h"

GST_DEBUG_CATEGORY_STATIC (gst_qt_mux_debug);
#define GST_CAT_DEFAULT gst_qt_mux_debug

/* QTMux signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_LARGE_FILE,
  PROP_MOVIE_TIMESCALE,
  PROP_DO_CTTS,
  PROP_FLAVOR,
  PROP_FAST_START,
  PROP_FAST_START_TEMP_FILE
};

#define MDAT_ATOM_HEADER_SIZE           16
#define DEFAULT_LARGE_FILE              FALSE
#define DEFAULT_MOVIE_TIMESCALE         600
#define DEFAULT_DO_CTTS                 FALSE
#define DEFAULT_FAST_START              FALSE
#define DEFAULT_FAST_START_TEMP_FILE    NULL

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
    GstPadTemplate * templ, const gchar * name);
static void gst_qt_mux_release_pad (GstElement * element, GstPad * pad);

/* event */
static gboolean gst_qt_mux_sink_event (GstPad * pad, GstEvent * event);

static GstFlowReturn gst_qt_mux_collected (GstCollectPads * pads,
    gpointer user_data);
static GstFlowReturn gst_qt_mux_add_buffer (GstQTMux * qtmux, GstQTPad * pad,
    GstBuffer * buf);

static GstElementClass *parent_class = NULL;

static void
gst_qt_mux_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstQTMuxClass *klass = (GstQTMuxClass *) g_class;
  GstQTMuxClassParams *params;
  GstElementDetails details;
  GstPadTemplate *videosinktempl, *audiosinktempl, *srctempl;

  params =
      (GstQTMuxClassParams *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (g_class),
      GST_QT_MUX_PARAMS_QDATA);
  g_assert (params != NULL);

  /* construct the element details struct */
  details.longname = g_strdup_printf ("%s Muxer", params->prop->long_name);
  details.klass = g_strdup ("Codec/Muxer");
  details.description =
      g_strdup_printf ("Multiplex audio and video into a %s file",
      params->prop->long_name);
  details.author = "Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>";
  gst_element_class_set_details (element_class, &details);
  g_free (details.longname);
  g_free (details.klass);
  g_free (details.description);

  /* pad templates */
  srctempl = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, params->src_caps);
  gst_element_class_add_pad_template (element_class, srctempl);

  if (params->audio_sink_caps) {
    audiosinktempl = gst_pad_template_new ("audio_%d",
        GST_PAD_SINK, GST_PAD_REQUEST, params->audio_sink_caps);
    gst_element_class_add_pad_template (element_class, audiosinktempl);
  }

  if (params->video_sink_caps) {
    videosinktempl = gst_pad_template_new ("video_%d",
        GST_PAD_SINK, GST_PAD_REQUEST, params->video_sink_caps);
    gst_element_class_add_pad_template (element_class, videosinktempl);
  }

  klass->format = params->prop->format;
}

static void
gst_qt_mux_class_init (GstQTMuxClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->finalize = gst_qt_mux_finalize;
  gobject_class->get_property = gst_qt_mux_get_property;
  gobject_class->set_property = gst_qt_mux_set_property;

  g_object_class_install_property (gobject_class, PROP_LARGE_FILE,
      g_param_spec_boolean ("large-file", "Support for large files",
          "Uses 64bits to some fields instead of 32bits, "
          "providing support for large files",
          DEFAULT_LARGE_FILE, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_MOVIE_TIMESCALE,
      g_param_spec_uint ("movie-timescale", "Movie timescale",
          "Timescale to use in the movie (units per second)",
          1, G_MAXUINT32, DEFAULT_MOVIE_TIMESCALE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_DO_CTTS,
      g_param_spec_boolean ("presentation-time",
          "Include presentation-time info",
          "Calculate and include presentation/composition time (in addition to decoding time)"
          " (use with caution)", DEFAULT_DO_CTTS,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
  g_object_class_install_property (gobject_class, PROP_FAST_START,
      g_param_spec_boolean ("faststart", "Format file to faststart",
          "If the file should be formated for faststart (headers first). ",
          DEFAULT_FAST_START, G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, PROP_FAST_START_TEMP_FILE,
      g_param_spec_string ("faststart-file", "File to use for storing buffers",
          "File that will be used temporarily to store data from the stream when "
          "creating a faststart file. If null a filepath will be created automatically",
          DEFAULT_FAST_START_TEMP_FILE, G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

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
  qtpad->have_dts = FALSE;
  qtpad->sample_size = 0;
  qtpad->sync = FALSE;
  qtpad->last_dts = 0;

  if (qtpad->last_buf)
    gst_buffer_replace (&qtpad->last_buf, NULL);

  /* reference owned elsewhere */
  qtpad->trak = NULL;
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
  qtmux->mdat_pos = 0;

  if (qtmux->ftyp) {
    atom_ftyp_free (qtmux->ftyp);
    qtmux->ftyp = NULL;
  }
  if (qtmux->moov) {
    atom_moov_free (qtmux->moov);
    qtmux->moov = NULL;
  }
  if (qtmux->fast_start_file) {
    fclose (qtmux->fast_start_file);
    qtmux->fast_start_file = NULL;
  }
  gst_tag_setter_reset_tags (GST_TAG_SETTER (qtmux));

  /* reset pad data */
  for (walk = qtmux->collect->data; walk; walk = g_slist_next (walk)) {
    GstQTPad *qtpad = (GstQTPad *) walk->data;
    gst_qt_mux_pad_reset (qtpad);

    /* hm, moov_free above yanked the traks away from us,
     * so do not free, but do clear */
    qtpad->trak = NULL;
  }

  if (alloc) {
    qtmux->moov = atom_moov_new (qtmux->context);
  }
}

static void
gst_qt_mux_init (GstQTMux * qtmux, GstQTMuxClass * qtmux_klass)
{
  GstElementClass *klass = GST_ELEMENT_CLASS (qtmux_klass);
  GstPadTemplate *templ;
  GstCaps *caps;

  templ = gst_element_class_get_pad_template (klass, "src");
  qtmux->srcpad = gst_pad_new_from_template (templ, "src");
  caps = gst_caps_copy (gst_pad_get_pad_template_caps (qtmux->srcpad));
  gst_pad_set_caps (qtmux->srcpad, caps);
  gst_caps_unref (caps);
  gst_pad_use_fixed_caps (qtmux->srcpad);
  gst_element_add_pad (GST_ELEMENT (qtmux), qtmux->srcpad);

  qtmux->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (qtmux->collect,
      (GstCollectPadsFunction) GST_DEBUG_FUNCPTR (gst_qt_mux_collected), qtmux);

  /* properties set to default upon construction */

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

  if (qtmux->fast_start_file_path)
    g_free (qtmux->fast_start_file_path);

  atoms_context_free (qtmux->context);
  gst_object_unref (qtmux->collect);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* FIXME approach below is pretty Apple/MOV/MP4/iTunes specific,
 * and as such does not comply with e.g. 3GPP specs */

/*
 * Struct to record mappings from gstreamer tags to fourcc codes
 */
typedef struct _GstTagToFourcc
{
  guint32 fourcc;
  const gchar *gsttag;
  const gchar *gsttag2;
} GstTagToFourcc;

/* tag list tags to fourcc matching */
static const GstTagToFourcc tag_matches[] = {
  {FOURCC__alb, GST_TAG_ALBUM,},
  {FOURCC__ART, GST_TAG_ARTIST,},
  {FOURCC__cmt, GST_TAG_COMMENT,},
  {FOURCC__wrt, GST_TAG_COMPOSER,},
  {FOURCC__gen, GST_TAG_GENRE,},
  {FOURCC__nam, GST_TAG_TITLE,},
  {FOURCC__des, GST_TAG_DESCRIPTION,},
  {FOURCC__too, GST_TAG_ENCODER,},
  {FOURCC_cprt, GST_TAG_COPYRIGHT,},
  {FOURCC_keyw, GST_TAG_KEYWORDS,},
  {FOURCC__day, GST_TAG_DATE,},
  {FOURCC_tmpo, GST_TAG_BEATS_PER_MINUTE,},
  {FOURCC_trkn, GST_TAG_TRACK_NUMBER, GST_TAG_TRACK_COUNT},
  {FOURCC_disk, GST_TAG_ALBUM_VOLUME_NUMBER, GST_TAG_ALBUM_VOLUME_COUNT},
  {FOURCC_covr, GST_TAG_PREVIEW_IMAGE,},
  {0, NULL,}
};

/* qtdemux produces these for atoms it cannot parse */
#define GST_QT_DEMUX_PRIVATE_TAG "private-qt-tag"

static void
gst_qt_mux_add_metadata_tags (GstQTMux * qtmux, const GstTagList * list)
{
  guint32 fourcc;
  gint i;
  const gchar *tag, *tag2;

  for (i = 0; tag_matches[i].fourcc; i++) {
    fourcc = tag_matches[i].fourcc;
    tag = tag_matches[i].gsttag;
    tag2 = tag_matches[i].gsttag2;

    switch (gst_tag_get_type (tag)) {
        /* strings */
      case G_TYPE_STRING:
      {
        gchar *str = NULL;

        if (!gst_tag_list_get_string (list, tag, &str) || !str)
          break;
        GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s",
            GST_FOURCC_ARGS (fourcc), str);
        atom_moov_add_str_tag (qtmux->moov, fourcc, str);
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
        atom_moov_add_uint_tag (qtmux->moov, fourcc, 21, (gint) value);
        break;
      }
        /* paired unsigned integers */
      case G_TYPE_UINT:
      {
        guint value;
        guint count;

        if (!gst_tag_list_get_uint (list, tag, &value) ||
            !gst_tag_list_get_uint (list, tag2, &count))
          break;
        GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %u/%u",
            GST_FOURCC_ARGS (fourcc), value, count);
        atom_moov_add_uint_tag (qtmux->moov, fourcc, 0,
            value << 16 | (count & 0xFFFF));
        break;
      }
      default:
      {
        if (gst_tag_get_type (tag) == GST_TYPE_DATE) {
          GDate *date = NULL;
          GDateYear year;
          GDateMonth month;
          GDateDay day;
          gchar *str;

          if (!gst_tag_list_get_date (list, tag, &date) || !date)
            break;
          year = g_date_get_year (date);
          month = g_date_get_month (date);
          day = g_date_get_day (date);

          if (year == G_DATE_BAD_YEAR && month == G_DATE_BAD_MONTH &&
              day == G_DATE_BAD_DAY) {
            GST_WARNING_OBJECT (qtmux, "invalid date in tag");
            break;
          }

          str = g_strdup_printf ("%u-%u-%u", year, month, day);
          GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT " -> %s",
              GST_FOURCC_ARGS (fourcc), str);
          atom_moov_add_str_tag (qtmux->moov, fourcc, str);
        } else if (gst_tag_get_type (tag) == GST_TYPE_BUFFER) {
          GValue value = { 0, };
          GstBuffer *buf;
          GstCaps *caps;
          GstStructure *structure;
          gint flags = 0;

          if (!gst_tag_list_copy_value (&value, list, tag))
            break;

          buf = gst_value_get_buffer (&value);
          if (!buf)
            goto done;

          caps = gst_buffer_get_caps (buf);
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
          gst_caps_unref (caps);

          if (!flags) {
            GST_WARNING_OBJECT (qtmux, "preview image format not supported");
            goto done;
          }

          GST_DEBUG_OBJECT (qtmux, "Adding tag %" GST_FOURCC_FORMAT
              " -> image size %d", GST_FOURCC_ARGS (fourcc),
              GST_BUFFER_SIZE (buf));
          atom_moov_add_tag (qtmux->moov, fourcc, flags, GST_BUFFER_DATA (buf),
              GST_BUFFER_SIZE (buf));
        done:
          g_value_unset (&value);
        } else
          g_assert_not_reached ();
        break;
      }
    }
  }

  /* add unparsed blobs if present */
  if (gst_tag_exists (GST_QT_DEMUX_PRIVATE_TAG)) {
    guint num_tags;

    num_tags = gst_tag_list_get_tag_size (list, GST_QT_DEMUX_PRIVATE_TAG);
    for (i = 0; i < num_tags; ++i) {
      const GValue *val;
      GstBuffer *buf;
      GstCaps *caps = NULL;

      val = gst_tag_list_get_value_index (list, GST_QT_DEMUX_PRIVATE_TAG, i);
      buf = (GstBuffer *) gst_value_get_mini_object (val);

      if (buf && (caps = gst_buffer_get_caps (buf))) {
        GstStructure *s;
        const gchar *style = NULL;

        GST_DEBUG_OBJECT (qtmux, "Found private tag %d/%d; size %d, caps %"
            GST_PTR_FORMAT, i, num_tags, GST_BUFFER_SIZE (buf), caps);
        s = gst_caps_get_structure (caps, 0);
        if (s && (style = gst_structure_get_string (s, "style"))) {
          /* FIXME make into a parameter */
          if (strcmp (style, "itunes") == 0) {
            GST_DEBUG_OBJECT (qtmux, "Adding private tag");
            atom_moov_add_blob_tag (qtmux->moov, GST_BUFFER_DATA (buf),
                GST_BUFFER_SIZE (buf));
          }
        }
        gst_caps_unref (caps);
      }
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
  const GstTagList *tags;

  tags = gst_tag_setter_get_tag_list (GST_TAG_SETTER (qtmux));

  GST_LOG_OBJECT (qtmux, "tags: %" GST_PTR_FORMAT, tags);

  if (tags && !gst_tag_list_is_empty (tags)) {
    GST_DEBUG_OBJECT (qtmux, "Formatting tags");
    gst_qt_mux_add_metadata_tags (qtmux, tags);
  } else {
    GST_DEBUG_OBJECT (qtmux, "No tags received");
  }
}

static GstFlowReturn
gst_qt_mux_send_buffer (GstQTMux * qtmux, GstBuffer * buf, guint64 * offset,
    gboolean mind_fast)
{
  GstFlowReturn res;
  guint8 *data;
  guint size;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  GST_LOG_OBJECT (qtmux, "sending buffer size %d", size);

  if (mind_fast && qtmux->fast_start_file) {
    gint ret;

    GST_LOG_OBJECT (qtmux, "to temporary file");
    ret = fwrite (data, sizeof (guint8), size, qtmux->fast_start_file);
    gst_buffer_unref (buf);
    if (ret != size)
      goto write_error;
    else
      res = GST_FLOW_OK;
  } else {
    GST_LOG_OBJECT (qtmux, "downstream");

    gst_buffer_set_caps (buf, GST_PAD_CAPS (qtmux->srcpad));
    res = gst_pad_push (qtmux->srcpad, buf);
  }

  if (offset)
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

static GstFlowReturn
gst_qt_mux_send_buffered_data (GstQTMux * qtmux, guint64 * offset)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf = NULL;

  if (fflush (qtmux->fast_start_file))
    goto flush_failed;

#ifdef HAVE_FSEEKO
  if (fseeko (qtmux->fast_start_file, (off_t) 0, SEEK_SET) != 0)
    goto seek_failed;
#elif defined (G_OS_UNIX) || defined (G_OS_WIN32)
  if (lseek (fileno (qtmux->fast_start_file), (off_t) 0,
          SEEK_SET) == (off_t) - 1)
    goto seek_failed;
#else
  if (fseek (qtmux->fast_start_file, (long) 0, SEEK_SET) != 0)
    goto seek_failed;
#endif

  /* hm, this could all take a really really long time,
   * but there may not be another way to get moov atom first
   * (somehow optimize copy?) */
  GST_DEBUG_OBJECT (qtmux, "Sending buffered data");
  while (ret == GST_FLOW_OK) {
    gint r;
    const int bufsize = 4096;

    buf = gst_buffer_new_and_alloc (bufsize);
    r = fread (GST_BUFFER_DATA (buf), sizeof (guint8), bufsize,
        qtmux->fast_start_file);
    if (r == 0)
      break;
    GST_BUFFER_SIZE (buf) = r;
    GST_LOG_OBJECT (qtmux, "Pushing buffered buffer of size %d", r);
    ret = gst_qt_mux_send_buffer (qtmux, buf, offset, FALSE);
    buf = NULL;
  }
  if (buf)
    gst_buffer_unref (buf);

exit:
  /* best cleaning up effort, eat possible error */
  fclose (qtmux->fast_start_file);
  qtmux->fast_start_file = NULL;

  /* FIXME maybe delete temporary file, or let the system handle that ? */

  return ret;

  /* ERRORS */
flush_failed:
  {
    GST_ELEMENT_ERROR (qtmux, RESOURCE, WRITE,
        ("Failed to flush temporary file"), GST_ERROR_SYSTEM);
    ret = GST_FLOW_ERROR;
    goto exit;
  }
seek_failed:
  {
    GST_ELEMENT_ERROR (qtmux, RESOURCE, SEEK,
        ("Failed to seek temporary file"), GST_ERROR_SYSTEM);
    ret = GST_FLOW_ERROR;
    goto exit;
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
gst_qt_mux_send_mdat_header (GstQTMux * qtmux, guint64 * off, guint64 size)
{
  Atom *node_header;
  GstBuffer *buf;
  guint8 *data = NULL;
  guint64 offset = 0;

  GST_DEBUG_OBJECT (qtmux, "Sending mdat's atom header, "
      "size %" G_GUINT64_FORMAT, size);

  node_header = g_malloc0 (sizeof (Atom));
  node_header->type = FOURCC_mdat;
  /* use extended size */
  node_header->size = 1;
  node_header->extended_size = 0;
  if (size)
    node_header->extended_size = size;

  size = offset = 0;
  if (atom_copy_data (node_header, &data, &size, &offset) == 0)
    goto serialize_error;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;
  GST_BUFFER_SIZE (buf) = offset;

  g_free (node_header);

  GST_LOG_OBJECT (qtmux, "Pushing mdat start");
  return gst_qt_mux_send_buffer (qtmux, buf, off, FALSE);

  /* ERRORS */
serialize_error:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Failed to serialize ftyp"));
    return GST_FLOW_ERROR;
  }
}

/*
 * We get the position of the mdat size field, seek back to it
 * and overwrite with the real value
 */
static GstFlowReturn
gst_qt_mux_update_mdat_size (GstQTMux * qtmux, guint64 mdat_pos,
    guint64 mdat_size, guint64 * offset)
{
  GstEvent *event;
  GstBuffer *buf;

  /* seek and rewrite the header */
  event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES,
      mdat_pos, GST_CLOCK_TIME_NONE, 0);
  gst_pad_push_event (qtmux->srcpad, event);

  buf = gst_buffer_new_and_alloc (sizeof (guint64));
  GST_WRITE_UINT64_BE (GST_BUFFER_DATA (buf), mdat_size);

  return gst_qt_mux_send_buffer (qtmux, buf, offset, FALSE);
}

static GstFlowReturn
gst_qt_mux_stop_file (GstQTMux * qtmux)
{
  gboolean ret = GST_FLOW_OK;
  GstBuffer *buffer = NULL;
  guint64 offset = 0, size = 0;
  guint8 *data;
  GSList *walk;
  gboolean large_file;
  guint32 timescale;

  GST_DEBUG_OBJECT (qtmux, "Updating remaining values and sending last data");

  /* pushing last buffers for each pad */
  for (walk = qtmux->collect->data; walk; walk = g_slist_next (walk)) {
    GstCollectData *cdata = (GstCollectData *) walk->data;
    GstQTPad *qtpad = (GstQTPad *) cdata;

    /* send last buffer */
    GST_DEBUG_OBJECT (qtmux, "Sending the last buffer for pad %s",
        GST_PAD_NAME (qtpad->collect.pad));
    ret = gst_qt_mux_add_buffer (qtmux, qtpad, NULL);
    if (ret != GST_FLOW_OK)
      GST_DEBUG_OBJECT (qtmux, "Failed to send last buffer for %s, "
          "flow return: %s", GST_PAD_NAME (qtpad->collect.pad),
          gst_flow_get_name (ret));
  }

  GST_OBJECT_LOCK (qtmux);
  timescale = qtmux->timescale;
  large_file = qtmux->large_file;
  GST_OBJECT_UNLOCK (qtmux);

  /* inform lower layers of our property wishes, and determine duration.
   * Let moov take care of this using its list of traks;
   * so that released pads are also included */
  GST_DEBUG_OBJECT (qtmux, "Large file support: %d", large_file);
  GST_DEBUG_OBJECT (qtmux, "Updating timescale to %" G_GUINT32_FORMAT,
      timescale);
  atom_moov_update_timescale (qtmux->moov, timescale);
  atom_moov_set_64bits (qtmux->moov, large_file);
  atom_moov_update_duration (qtmux->moov);

  /* tags into file metadata */
  gst_qt_mux_setup_metadata (qtmux);

  /* if faststart, update the offset of the atoms in the movie with the offset
   * that the movie headers before mdat will cause */
  if (qtmux->fast_start_file) {
    /* copy into NULL to obtain size */
    offset = size = 0;
    if (!atom_moov_copy_data (qtmux->moov, NULL, &size, &offset))
      goto serialize_error;
    GST_DEBUG_OBJECT (qtmux, "calculated moov atom size %" G_GUINT64_FORMAT,
        size);
    offset += qtmux->header_size + MDAT_ATOM_HEADER_SIZE;
  } else
    offset = qtmux->header_size;
  atom_moov_chunks_add_offset (qtmux->moov, offset);

  /* serialize moov */
  offset = size = 0;
  data = NULL;
  GST_LOG_OBJECT (qtmux, "Copying movie header into buffer");
  ret = atom_moov_copy_data (qtmux->moov, &data, &size, &offset);
  if (!ret)
    goto serialize_error;

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = GST_BUFFER_MALLOCDATA (buffer) = data;
  GST_BUFFER_SIZE (buffer) = offset;
  /* note: as of this point, we no longer care about tracking written data size,
   * since there is no more use for it anyway */
  GST_DEBUG_OBJECT (qtmux, "Pushing movie atoms");
  gst_qt_mux_send_buffer (qtmux, buffer, NULL, FALSE);

  /* total mdat size as of now also includes the atom header */
  qtmux->mdat_size += MDAT_ATOM_HEADER_SIZE;
  /* if needed, send mdat atom and move buffered data into it */
  if (qtmux->fast_start_file) {
    /* mdat size = accumulated (buffered data) + mdat atom header */
    ret = gst_qt_mux_send_mdat_header (qtmux, NULL, qtmux->mdat_size);
    if (ret != GST_FLOW_OK)
      return ret;
    ret = gst_qt_mux_send_buffered_data (qtmux, NULL);
    if (ret != GST_FLOW_OK)
      return ret;
  } else {
    /* mdata needs update iff not using faststart */
    GST_DEBUG_OBJECT (qtmux, "updating mdata size");
    ret = gst_qt_mux_update_mdat_size (qtmux, qtmux->mdat_pos,
        qtmux->mdat_size, NULL);
    /* note; no seeking back to the end of file is done,
     * since we longer write anything anyway */
  }

  return ret;

  /* ERRORS */
serialize_error:
  {
    gst_buffer_unref (buffer);
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Failed to serialize moov"));
    return GST_FLOW_ERROR;
  }
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

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = GST_BUFFER_MALLOCDATA (buf) = data;
  GST_BUFFER_SIZE (buf) = offset;

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

static GstFlowReturn
gst_qt_mux_start_file (GstQTMux * qtmux)
{
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  GstFlowReturn ret = GST_FLOW_OK;
  guint32 major, version;
  GList *comp;
  GstBuffer *prefix;

  GST_DEBUG_OBJECT (qtmux, "starting file");

  /* let downstream know we think in BYTES and expect to do seeking later on */
  gst_pad_push_event (qtmux->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_BYTES, 0, -1, 0));

  /* init and send context and ftyp based on current property state */
  if (qtmux->ftyp)
    atom_ftyp_free (qtmux->ftyp);
  gst_qt_mux_map_format_to_header (qtmux_klass->format, &prefix, &major,
      &version, &comp, qtmux->moov);
  qtmux->ftyp = atom_ftyp_new (qtmux->context, major, version, comp);
  if (comp)
    g_list_free (comp);
  if (prefix) {
    ret = gst_qt_mux_send_buffer (qtmux, prefix, &qtmux->header_size, FALSE);
    if (ret != GST_FLOW_OK)
      goto exit;
  }
  ret = gst_qt_mux_send_ftyp (qtmux, &qtmux->header_size);
  if (ret != GST_FLOW_OK)
    goto exit;

  /* send mdat header if already needed, and mark position for later update */
  GST_OBJECT_LOCK (qtmux);
  if (qtmux->fast_start) {
    qtmux->fast_start_file = g_fopen (qtmux->fast_start_file_path, "wb+");
    if (!qtmux->fast_start_file)
      goto open_failed;
  } else {
    ret = gst_qt_mux_send_mdat_header (qtmux, &qtmux->header_size, 0);
    /* mdat size position = current header pos - extended header size */
    qtmux->mdat_pos = qtmux->header_size - sizeof (guint64);
  }
  GST_OBJECT_UNLOCK (qtmux);

exit:
  return ret;

  /* ERRORS */
open_failed:
  {
    GST_ELEMENT_ERROR (qtmux, RESOURCE, OPEN_READ_WRITE,
        (("Could not open temporary file \"%s\""), qtmux->fast_start_file_path),
        GST_ERROR_SYSTEM);
    GST_OBJECT_UNLOCK (qtmux);
    return GST_FLOW_ERROR;
  }
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
  guint64 scaled_duration, chunk_offset;
  gint64 last_dts;
  gint64 pts_offset = 0;
  gboolean sync = FALSE, do_pts = FALSE;

  if (!pad->fourcc)
    goto not_negotiated;

  last_buf = pad->last_buf;
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
    return GST_FLOW_OK;
  } else
    gst_buffer_ref (last_buf);

  /* fall back to duration if:
   * - last bufer
   * - this format has out of order buffers (e.g. MPEG-4),
   * - lack of valid time forces fall back */
  if (buf == NULL || pad->is_out_of_order ||
      !GST_BUFFER_TIMESTAMP_IS_VALID (last_buf) ||
      !GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    if (!GST_BUFFER_DURATION_IS_VALID (last_buf)) {
      /* be forgiving for some possibly last upstream flushed buffer */
      if (buf)
        goto no_time;
      GST_WARNING_OBJECT (qtmux, "no duration for last buffer");
      /* iso spec recommends some small value, try 0 */
      duration = 0;
    } else {
      duration = GST_BUFFER_DURATION (last_buf);
    }
  } else {
    duration = GST_BUFFER_TIMESTAMP (buf) - GST_BUFFER_TIMESTAMP (last_buf);
  }

  gst_buffer_replace (&pad->last_buf, buf);

  last_dts = gst_util_uint64_scale (pad->last_dts,
      atom_trak_get_timescale (pad->trak), GST_SECOND);

  /* raw audio has many samples per buffer (= chunk) */
  if (pad->sample_size) {
    sample_size = pad->sample_size;
    if (GST_BUFFER_SIZE (last_buf) % sample_size != 0)
      goto fragmented_sample;
    /* note: qt raw audio storage warps it implicitly into a timewise
     * perfect stream, discarding buffer times */
    nsamples = GST_BUFFER_SIZE (last_buf) / sample_size;
    duration = GST_BUFFER_DURATION (last_buf) / nsamples;
    /* timescale = samplerate */
    scaled_duration = 1;
    pad->last_dts += duration * nsamples;
  } else {
    nsamples = 1;
    sample_size = GST_BUFFER_SIZE (last_buf);
    if (pad->have_dts) {
      gint64 scaled_dts;
      pad->last_dts = GST_BUFFER_OFFSET_END (last_buf);
      if ((gint64) (pad->last_dts) < 0) {
        scaled_dts = -gst_util_uint64_scale (-pad->last_dts,
            atom_trak_get_timescale (pad->trak), GST_SECOND);
      } else {
        scaled_dts = gst_util_uint64_scale (pad->last_dts,
            atom_trak_get_timescale (pad->trak), GST_SECOND);
      }
      scaled_duration = scaled_dts - last_dts;
      last_dts = scaled_dts;
    } else {
      /* first convert intended timestamp (in GstClockTime resolution) to
       * trak timescale, then derive delta;
       * this ensures sums of (scale)delta add up to converted timestamp,
       * which only deviates at most 1/scale from timestamp itself */
      scaled_duration = gst_util_uint64_scale (pad->last_dts + duration,
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

  /* optionally calculate ctts entry values
   * (if composition-time expected different from decoding-time) */
  /* really not recommended:
   * - decoder typically takes care of dts/pts issues
   * - in case of out-of-order, dts may only be determined as above
   *   (e.g. sum of duration), which may be totally different from
   *   buffer timestamps in case of multiple segment, non-perfect streams
   *  (and just perhaps maybe with some luck segment_to_running_time
   *   or segment_to_media_time might get near to it) */
  if ((pad->have_dts || qtmux->guess_pts) && pad->is_out_of_order) {
    guint64 pts;

    pts = gst_util_uint64_scale (GST_BUFFER_TIMESTAMP (last_buf),
        atom_trak_get_timescale (pad->trak), GST_SECOND);
    pts_offset = (gint64) (pts - last_dts);
    do_pts = TRUE;
    GST_LOG_OBJECT (qtmux, "Adding ctts entry for pad %s: %" G_GINT64_FORMAT,
        GST_PAD_NAME (pad->collect.pad), pts_offset);
  }

  /* now we go and register this buffer/sample all over */
  /* note that a new chunk is started each time (not fancy but works) */
  atom_trak_add_samples (pad->trak, nsamples, scaled_duration, sample_size,
      chunk_offset, sync, do_pts, pts_offset);

  if (buf)
    gst_buffer_unref (buf);

  return gst_qt_mux_send_buffer (qtmux, last_buf, &qtmux->mdat_size, TRUE);

  /* ERRORS */
bail:
  {
    if (buf)
      gst_buffer_unref (buf);
    gst_buffer_unref (last_buf);
    return GST_FLOW_ERROR;
  }
no_time:
  {
    GST_ELEMENT_ERROR (qtmux, STREAM, MUX, (NULL),
        ("Failed to determine time to mux."));
    goto bail;
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
    gst_buffer_unref (buf);
    return GST_FLOW_NOT_NEGOTIATED;
  }
}

static GstFlowReturn
gst_qt_mux_collected (GstCollectPads * pads, gpointer user_data)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstQTMux *qtmux = GST_QT_MUX_CAST (user_data);
  GSList *walk;
  GstQTPad *best_pad = NULL;
  GstClockTime time, best_time = GST_CLOCK_TIME_NONE;
  GstBuffer *buf;

  if (G_UNLIKELY (qtmux->state == GST_QT_MUX_STATE_STARTED)) {
    if ((ret = gst_qt_mux_start_file (qtmux)) != GST_FLOW_OK)
      return ret;
    else
      qtmux->state = GST_QT_MUX_STATE_DATA;
  }

  if (G_UNLIKELY (qtmux->state == GST_QT_MUX_STATE_EOS))
    return GST_FLOW_UNEXPECTED;

  /* select the best buffer */
  walk = qtmux->collect->data;
  while (walk) {
    GstQTPad *pad;
    GstCollectData *data;

    data = (GstCollectData *) walk->data;
    pad = (GstQTPad *) data;

    walk = g_slist_next (walk);

    buf = gst_collect_pads_peek (pads, data);
    if (buf == NULL) {
      GST_LOG_OBJECT (qtmux, "Pad %s has no buffers",
          GST_PAD_NAME (pad->collect.pad));
      continue;
    }
    time = GST_BUFFER_TIMESTAMP (buf);
    gst_buffer_unref (buf);

    if (best_pad == NULL || !GST_CLOCK_TIME_IS_VALID (time) ||
        (GST_CLOCK_TIME_IS_VALID (best_time) && time < best_time)) {
      best_pad = pad;
      best_time = time;
    }
  }

  if (best_pad != NULL) {
    GST_LOG_OBJECT (qtmux, "selected pad %s with time %" GST_TIME_FORMAT,
        GST_PAD_NAME (best_pad->collect.pad), GST_TIME_ARGS (best_time));
    buf = gst_collect_pads_pop (pads, &best_pad->collect);
    ret = gst_qt_mux_add_buffer (qtmux, best_pad, buf);
  } else {
    ret = gst_qt_mux_stop_file (qtmux);
    if (ret == GST_FLOW_OK) {
      gst_pad_push_event (qtmux->srcpad, gst_event_new_eos ());
      ret = GST_FLOW_UNEXPECTED;
    }
    qtmux->state = GST_QT_MUX_STATE_EOS;
  }

  return ret;
}

static gboolean
gst_qt_mux_audio_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstQTMux *qtmux = GST_QT_MUX_CAST (gst_pad_get_parent (pad));
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  GstQTPad *qtpad = NULL;
  GstStructure *structure;
  const gchar *mimetype;
  gint rate, channels;
  const GValue *value = NULL;
  const GstBuffer *codec_data = NULL;
  GstQTMuxFormat format;
  AudioSampleEntry entry = { 0, };
  AtomInfo *ext_atom = NULL;
  gint constant_size = 0;

  /* find stream data */
  qtpad = (GstQTPad *) gst_pad_get_element_private (pad);
  g_assert (qtpad);

  /* does not go well to renegotiate stream mid-way */
  if (qtpad->fourcc)
    goto refuse_renegotiation;

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
  qtpad->have_dts = FALSE;

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
    gint mpegversion = 0;
    gint layer = -1;

    gst_structure_get_int (structure, "mpegversion", &mpegversion);
    switch (mpegversion) {
      case 1:
        gst_structure_get_int (structure, "layer", &layer);
        switch (layer) {
          case 3:
            /* mp3 */
            /* note: QuickTime player does not like mp3 either way in iso/mp4 */
            if (format == GST_QT_MUX_FORMAT_QT)
              entry.fourcc = FOURCC__mp3;
            else {
              entry.fourcc = FOURCC_mp4a;
              ext_atom =
                  build_esds_extension (qtpad->trak, ESDS_OBJECT_TYPE_MPEG1_P3,
                  ESDS_STREAM_TYPE_AUDIO, codec_data);
            }
            entry.samples_per_packet = 1152;
            entry.bytes_per_sample = 2;
            break;
        }
        break;
      case 4:
        /* AAC */
        entry.fourcc = FOURCC_mp4a;
        if (!codec_data || GST_BUFFER_SIZE (codec_data) < 2)
          GST_WARNING_OBJECT (qtmux, "no (valid) codec_data for AAC audio");
        else {
          guint8 profile = GST_READ_UINT8 (GST_BUFFER_DATA (codec_data));

          /* warn if not Low Complexity profile */
          profile >>= 3;
          if (profile != 2)
            GST_WARNING_OBJECT (qtmux,
                "non-LC AAC may not run well on (Apple) QuickTime/iTunes");
        }
        if (format == GST_QT_MUX_FORMAT_QT)
          ext_atom = build_mov_aac_extension (qtpad->trak, codec_data);
        else
          ext_atom =
              build_esds_extension (qtpad->trak, ESDS_OBJECT_TYPE_MPEG4_P3,
              ESDS_STREAM_TYPE_AUDIO, codec_data);
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
  } else if (strcmp (mimetype, "audio/x-raw-int") == 0) {
    gint width;
    gint depth;
    gint endianness;
    gboolean sign;

    if (!gst_structure_get_int (structure, "width", &width) ||
        !gst_structure_get_int (structure, "depth", &depth) ||
        !gst_structure_get_boolean (structure, "signed", &sign) ||
        !gst_structure_get_int (structure, "endianness", &endianness)) {
      GST_DEBUG_OBJECT (qtmux,
          "broken caps, width/depth/signed/endianness field missing");
      goto refuse_caps;
    }

    /* spec has no place for a distinction in these */
    if (width != depth) {
      GST_DEBUG_OBJECT (qtmux, "width must be same as depth!");
      goto refuse_caps;
    }

    if (sign) {
      if (endianness == G_LITTLE_ENDIAN)
        entry.fourcc = FOURCC_sowt;
      else if (endianness == G_BIG_ENDIAN)
        entry.fourcc = FOURCC_twos;
      /* maximum backward compatibility; only new version for > 16 bit */
      if (depth <= 16)
        entry.version = 0;
      /* not compressed in any case */
      entry.compression_id = 0;
      /* QT spec says: max at 16 bit even if sample size were actually larger,
       * however, most players (e.g. QuickTime!) seem to disagree, so ... */
      entry.sample_size = depth;
      entry.bytes_per_sample = depth / 8;
      entry.samples_per_packet = 1;
      entry.bytes_per_packet = depth / 8;
      entry.bytes_per_frame = entry.bytes_per_packet * channels;
    } else {
      if (width == 8 && depth == 8) {
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
    constant_size = (depth / 8) * channels;
  } else if (strcmp (mimetype, "audio/x-alaw") == 0) {
    entry.fourcc = FOURCC_alaw;
    entry.samples_per_packet = 1023;
    entry.bytes_per_sample = 2;
  } else if (strcmp (mimetype, "audio/x-mulaw") == 0) {
    entry.fourcc = FOURCC_ulaw;
    entry.samples_per_packet = 1023;
    entry.bytes_per_sample = 2;
  }

  if (!entry.fourcc)
    goto refuse_caps;

  /* ok, set the pad info accordingly */
  qtpad->fourcc = entry.fourcc;
  qtpad->sample_size = constant_size;
  atom_trak_set_audio_type (qtpad->trak, qtmux->context, &entry,
      entry.sample_rate, ext_atom, constant_size);

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
  while (rate >= 10000)
    rate /= 10;

  while (rate < 1000)
    rate *= 10;

  return (guint32) rate;
}

static gboolean
gst_qt_mux_video_sink_set_caps (GstPad * pad, GstCaps * caps)
{
  GstQTMux *qtmux = GST_QT_MUX_CAST (gst_pad_get_parent (pad));
  GstQTMuxClass *qtmux_klass = (GstQTMuxClass *) (G_OBJECT_GET_CLASS (qtmux));
  GstQTPad *qtpad = NULL;
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
  gboolean sync = FALSE;
  int par_num, par_den;

  /* find stream data */
  qtpad = (GstQTPad *) gst_pad_get_element_private (pad);
  g_assert (qtpad);

  /* does not go well to renegotiate stream mid-way */
  if (qtpad->fourcc)
    goto refuse_renegotiation;

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
  /* FIXME: pixel-aspect-ratio */

  qtpad->is_out_of_order = FALSE;

  /* bring frame numerator into a range that ensures both reasonable resolution
   * as well as a fair duration */
  rate = adjust_rate (framerate_num);
  GST_DEBUG_OBJECT (qtmux, "Rate of video track selected: %" G_GUINT32_FORMAT,
      rate);

  /* set common properties */
  entry.width = width;
  entry.height = height;
  /* should be OK according to qt and iso spec, override if really needed */
  entry.color_table_id = -1;
  entry.frame_count = 1;
  entry.depth = 24;

  /* sync entries by default */
  sync = TRUE;

  /* now map onto a fourcc, and some extra properties */
  if (strcmp (mimetype, "video/x-raw-rgb") == 0) {
    gint bpp;

    entry.fourcc = FOURCC_raw_;
    gst_structure_get_int (structure, "bpp", &bpp);
    entry.depth = bpp;
    sync = FALSE;
  } else if (strcmp (mimetype, "video/x-raw-yuv") == 0) {
    guint32 format = 0;

    sync = FALSE;
    gst_structure_get_fourcc (structure, "format", &format);
    switch (format) {
      case GST_MAKE_FOURCC ('U', 'Y', 'V', 'Y'):
        if (depth == -1)
          depth = 24;
        entry.fourcc = FOURCC_2vuy;
        entry.depth = depth;
        break;
    }
  } else if (strcmp (mimetype, "video/x-h263") == 0) {
    entry.fourcc = FOURCC_h263;
    ext_atom = build_h263_extension ();
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
          ESDS_STREAM_TYPE_VISUAL, codec_data);
      if (!codec_data)
        GST_WARNING_OBJECT (qtmux, "no codec_data for MPEG4 video; "
            "output might not play in Apple QuickTime (try global-headers?)");
    }
  } else if (strcmp (mimetype, "video/x-h264") == 0) {
    entry.fourcc = FOURCC_avc1;
    qtpad->is_out_of_order = TRUE;
    if (!codec_data)
      GST_WARNING_OBJECT (qtmux, "no codec_data in h264 caps");
    ext_atom = build_codec_data_extension (FOURCC_avcC, codec_data);
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
          entry.fourcc = GST_MAKE_FOURCC ('d', 'v', 'c', 'p');
        else
          entry.fourcc = GST_MAKE_FOURCC ('d', 'v', 'c', ' ');
        break;
      case 50:
        if (pal)
          entry.fourcc = GST_MAKE_FOURCC ('d', 'v', '5', 'p');
        else
          entry.fourcc = GST_MAKE_FOURCC ('d', 'v', '5', 'n');
        break;
      default:
        GST_WARNING_OBJECT (qtmux, "unrecognized dv version");
        break;
    }
  } else if (strcmp (mimetype, "image/jpeg") == 0) {
    entry.fourcc = FOURCC_jpeg;
    sync = FALSE;
  } else if (strcmp (mimetype, "image/x-j2c") == 0) {
    guint32 fourcc;

    entry.fourcc = FOURCC_mjp2;
    sync = FALSE;
    if (!gst_structure_get_fourcc (structure, "fourcc", &fourcc) ||
        !(ext_atom =
            build_jp2h_extension (qtpad->trak, width, height, fourcc))) {
      GST_DEBUG_OBJECT (qtmux, "missing or invalid fourcc in jp2 caps");
      goto refuse_caps;
    }
  } else if (strcmp (mimetype, "video/x-qt-part") == 0) {
    guint32 fourcc;

    gst_structure_get_fourcc (structure, "format", &fourcc);
    entry.fourcc = fourcc;
    qtpad->is_out_of_order = TRUE;
    qtpad->have_dts = TRUE;
  } else if (strcmp (mimetype, "video/x-mp4-part") == 0) {
    guint32 fourcc;

    gst_structure_get_fourcc (structure, "format", &fourcc);
    entry.fourcc = fourcc;
    qtpad->is_out_of_order = TRUE;
    qtpad->have_dts = TRUE;
  }

  if (!entry.fourcc)
    goto refuse_caps;

  /* ok, set the pad info accordingly */
  qtpad->fourcc = entry.fourcc;
  qtpad->sync = sync;
  atom_trak_set_video_type (qtpad->trak, qtmux->context, &entry, rate,
      ext_atom);

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
        "pad %s refused renegotiation to %" GST_PTR_FORMAT " from %"
        GST_PTR_FORMAT, GST_PAD_NAME (pad), caps, GST_PAD_CAPS (pad));
    gst_object_unref (qtmux);
    return FALSE;
  }
}

static gboolean
gst_qt_mux_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstQTMux *qtmux;

  qtmux = GST_QT_MUX_CAST (gst_pad_get_parent (pad));
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:{
      GstTagList *list;
      GstTagSetter *setter = GST_TAG_SETTER (qtmux);
      const GstTagMergeMode mode = gst_tag_setter_get_tag_merge_mode (setter);

      GST_DEBUG_OBJECT (qtmux, "received tag event");
      gst_event_parse_tag (event, &list);
      gst_tag_setter_merge_tags (setter, list, mode);
      break;
    }
    default:
      break;
  }

  ret = qtmux->collect_event (pad, event);
  gst_object_unref (qtmux);

  return ret;
}

static void
gst_qt_mux_release_pad (GstElement * element, GstPad * pad)
{
  GstQTMux *mux = GST_QT_MUX_CAST (element);

  /* let GstCollectPads complain if it is some unknown pad */
  if (gst_collect_pads_remove_pad (mux->collect, pad))
    gst_element_remove_pad (element, pad);
}

static GstPad *
gst_qt_mux_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * name)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstQTMux *qtmux = GST_QT_MUX_CAST (element);
  GstQTPad *collect_pad;
  GstPad *newpad;
  gboolean audio;

  GST_DEBUG_OBJECT (qtmux, "Requested pad: %s", GST_STR_NULL (name));

  if (qtmux->state != GST_QT_MUX_STATE_NONE) {
    GST_WARNING_OBJECT (qtmux, "Not providing request pad after stream start.");
    return NULL;
  }

  if (templ == gst_element_class_get_pad_template (klass, "audio_%d")) {
    audio = TRUE;
  } else if (templ == gst_element_class_get_pad_template (klass, "video_%d")) {
    audio = FALSE;
  } else {
    GST_WARNING_OBJECT (qtmux, "This is not our template!");
    return NULL;
  }

  /* add pad to collections */
  newpad = gst_pad_new_from_template (templ, name);
  collect_pad = (GstQTPad *)
      gst_collect_pads_add_pad_full (qtmux->collect, newpad, sizeof (GstQTPad),
      (GstCollectDataDestroyNotify) (gst_qt_mux_pad_reset));
  /* set up pad */
  gst_qt_mux_pad_reset (collect_pad);
  collect_pad->trak = atom_trak_new (qtmux->context);
  atom_moov_add_trak (qtmux->moov, collect_pad->trak);

  /* set up pad functions */
  if (audio)
    gst_pad_set_setcaps_function (newpad,
        GST_DEBUG_FUNCPTR (gst_qt_mux_audio_sink_set_caps));
  else
    gst_pad_set_setcaps_function (newpad,
        GST_DEBUG_FUNCPTR (gst_qt_mux_video_sink_set_caps));

  /* FIXME: hacked way to override/extend the event function of
   * GstCollectPads; because it sets its own event function giving the
   * element no access to events.
   */
  qtmux->collect_event = (GstPadEventFunction) GST_PAD_EVENTFUNC (newpad);
  gst_pad_set_event_function (newpad,
      GST_DEBUG_FUNCPTR (gst_qt_mux_sink_event));

  gst_pad_set_active (newpad, TRUE);
  gst_element_add_pad (element, newpad);

  return newpad;
}

static void
gst_qt_mux_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstQTMux *qtmux = GST_QT_MUX_CAST (object);

  GST_OBJECT_LOCK (qtmux);
  switch (prop_id) {
    case PROP_LARGE_FILE:
      g_value_set_boolean (value, qtmux->large_file);
      break;
    case PROP_MOVIE_TIMESCALE:
      g_value_set_uint (value, qtmux->timescale);
      break;
    case PROP_DO_CTTS:
      g_value_set_boolean (value, qtmux->guess_pts);
      break;
    case PROP_FAST_START:
      g_value_set_boolean (value, qtmux->fast_start);
      break;
    case PROP_FAST_START_TEMP_FILE:
      g_value_set_string (value, qtmux->fast_start_file_path);
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

  if (qtmux->fast_start_file_path) {
    g_free (qtmux->fast_start_file_path);
    qtmux->fast_start_file_path = NULL;
  }

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
    case PROP_LARGE_FILE:
      qtmux->large_file = g_value_get_boolean (value);
      break;
    case PROP_MOVIE_TIMESCALE:
      qtmux->timescale = g_value_get_uint (value);
      break;
    case PROP_DO_CTTS:
      qtmux->guess_pts = g_value_get_boolean (value);
      break;
    case PROP_FAST_START:
      qtmux->fast_start = g_value_get_boolean (value);
      break;
    case PROP_FAST_START_TEMP_FILE:
      if (qtmux->fast_start_file_path) {
        g_free (qtmux->fast_start_file_path);
      }
      qtmux->fast_start_file_path = g_value_dup_string (value);
      /* NULL means to generate a random one */
      if (!qtmux->fast_start_file_path) {
        gst_qt_mux_generate_fast_start_file_path (qtmux);
      }
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
  GType type;
  GstQTMuxFormat format;
  GstQTMuxClassParams *params;
  guint i = 0;

  GST_LOG ("Registering muxers");

  while (TRUE) {
    GstQTMuxFormatProp *prop;

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

    /* create the type now */
    type = g_type_register_static (GST_TYPE_ELEMENT, prop->type_name, &typeinfo,
        0);
    g_type_set_qdata (type, GST_QT_MUX_PARAMS_QDATA, (gpointer) params);
    g_type_add_interface_static (type, GST_TYPE_TAG_SETTER, &tag_setter_info);

    if (!gst_element_register (plugin, prop->name, GST_RANK_NONE, type))
      return FALSE;

    i++;
  }

  GST_LOG ("Finished registering muxers");

  return TRUE;
}

gboolean
gst_qt_mux_plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gst_qt_mux_debug, "qtmux", 0, "QT Muxer");

  return gst_qt_mux_register (plugin);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "qtmux",
    "Quicktime Muxer plugin",
    gst_qt_mux_plugin_init, VERSION, "LGPL", "gsoc2008 package",
    "embedded.ufcg.edu.br")
