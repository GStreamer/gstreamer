/*
 * GStreamer
 * Copyright (C) 2009 Sebastian Pölsterl <sebp@k-d-w.org>
 * Copyright (C) 2010 Andoni Morales Alastruey <ylatuya@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

/**
 * SECTION:element-teletextdec
 *
 * Decode PES stream containing teletext information to RGBA stream
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m filesrc location=recording.mpeg ! mpegtsdemux ! private/teletext ! teletextdec ! videoconvert ! ximagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstteletextdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_teletextdec_debug);
#define GST_CAT_DEFAULT gst_teletextdec_debug

#define SUBTITLES_PAGE 888
#define MAX_SLICES 32
#define DEFAULT_FONT_DESCRIPTION "verdana 12"
#define PANGO_TEMPLATE "<span font_desc=\"%s\" foreground=\"%s\"> %s \n</span>"

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_PAGENO,
  PROP_SUBNO,
  PROP_SUBTITLES_MODE,
  PROP_SUBS_TEMPLATE,
  PROP_FONT_DESCRIPTION
};

enum
{
  VBI_ERROR = -1,
  VBI_SUCCESS = 0,
  VBI_NEW_FRAME = 1
};

typedef enum
{
  DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE = 0x02,
  DATA_UNIT_EBU_TELETEXT_SUBTITLE = 0x03,
  DATA_UNIT_EBU_TELETEXT_INVERTED = 0x0C,

  DATA_UNIT_ZVBI_WSS_CPR1204 = 0xB4,
  DATA_UNIT_ZVBI_CLOSED_CAPTION_525 = 0xB5,
  DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525 = 0xB6,

  DATA_UNIT_VPS = 0xC3,
  DATA_UNIT_WSS = 0xC4,
  DATA_UNIT_CLOSED_CAPTION = 0xC5,
  DATA_UNIT_MONOCHROME_SAMPLES = 0xC6,

  DATA_UNIT_STUFFING = 0xFF,
} data_unit_id;

typedef struct
{
  int pgno;
  int subno;
} page_info;

typedef enum
{
  SYSTEM_525 = 0,
  SYSTEM_625
} systems;

/*
 *  ETS 300 706 Table 30: Colour Map
 */
static const gchar *default_color_map[40] = {
  "#000000", "#FF0000", "#00FF00", "#FFFF00", "#0000FF",
  "#FF00FF", "#00FFFF", "#FFFFFF", "#000000", "#770000",
  "#007700", "#777700", "#000077", "#770077", "#007777",
  "#777777", "#FF0055", "#FF7700", "#00FF77", "#FFFFBB",
  "#00CCAA", "#550000", "#665522", "#CC7777", "#333333",
  "#FF7777", "#77FF77", "#FFFF77", "#7777FF", "#FF77FF",
  "#77FFFF", "#DDD0DD",

  /* Private colors */
  "#000000", "#FFAA99", "#44EE00", "#FFDD00", "#FFAA99",
  "#FF00FF", "#00FFFF", "#EEEEEE"
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    ("video/mpeg,mpegversion=2,systemstream=TRUE ; private/teletext")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS
    (GST_VIDEO_CAPS_RGBA "; text/plain ; text/html ; text/x-pango-markup")
    );

/* debug category for filtering log messages */
#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_teletextdec_debug, "teletext", 0, "Teletext decoder");

GST_BOILERPLATE_FULL (GstTeletextDec, gst_teletextdec, GstElement,
    GST_TYPE_ELEMENT, DEBUG_INIT);

static void gst_teletextdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_teletextdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_teletextdec_finalize (GObject * object);

static GstStateChangeReturn gst_teletextdec_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_teletextdec_chain (GstPad * pad, GstBuffer * buf);
static gboolean gst_teletextdec_sink_setcaps (GstPad * pad, GstCaps * caps);
static gboolean gst_teletextdec_sink_event (GstPad * pad, GstEvent * event);
static GstPadLinkReturn gst_teletextdec_src_set_caps (GstPad * pad,
    GstCaps * caps);

static vbi_bool gst_teletextdec_convert (vbi_dvb_demux * dx, gpointer user_data,
    const vbi_sliced * sliced, guint n_lines, gint64 pts);
static void gst_teletextdec_event_handler (vbi_event * ev, void *user_data);

static GstFlowReturn gst_teletextdec_push_page (GstTeletextDec * teletext);
static GstFlowReturn gst_teletextdec_export_text_page (GstTeletextDec *
    teletext, vbi_page * page, GstBuffer ** buf);
static GstFlowReturn gst_teletextdec_export_html_page (GstTeletextDec *
    teletext, vbi_page * page, GstBuffer ** buf);
static GstFlowReturn gst_teletextdec_export_rgba_page (GstTeletextDec *
    teletext, vbi_page * page, GstBuffer ** buf);
static GstFlowReturn gst_teletextdec_export_pango_page (GstTeletextDec *
    teletext, vbi_page * page, GstBuffer ** buf);


static gboolean gst_teletextdec_push_preroll_buffer (GstTeletextDec * teletext);
static void gst_teletextdec_process_telx_buffer (GstTeletextDec * teletext,
    GstBuffer * buf);
static void gst_teletextdec_process_pes_buffer (GstTeletextDec * teletext,
    GstBuffer * buf);
static gboolean gst_teletextdec_extract_data_units (GstTeletextDec * teletext,
    GstTeletextFrame * f, guint8 * packet, guint * offset, gint size);

static void gst_teletextdec_zvbi_init (GstTeletextDec * teletext);
static void gst_teletextdec_zvbi_clear (GstTeletextDec * teletext);

/* GObject vmethod implementations */

static void
gst_teletextdec_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_static_metadata (element_class,
      "Teletext decoder",
      "Decoder",
      "Decode PES or raw VBI stream containing teletext information to RGBA, HTML and text",
      "Sebastian Pölsterl <sebp@k-d-w.org>, "
      "Andoni Morales Alastruey <ylatuya@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template));
}

/* initialize the gstteletext's class */
static void
gst_teletextdec_class_init (GstTeletextDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->set_property = gst_teletextdec_set_property;
  gobject_class->get_property = gst_teletextdec_get_property;
  gobject_class->finalize = gst_teletextdec_finalize;

  gstelement_class = GST_ELEMENT_CLASS (klass);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_teletextdec_change_state);

  g_object_class_install_property (gobject_class, PROP_PAGENO,
      g_param_spec_int ("page", "Page number",
          "Number of page that should displayed",
          100, 999, 100, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBNO,
      g_param_spec_int ("subpage", "Sub-page number",
          "Number of sub-page that should displayed (-1 for all)",
          -1, 0x99, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBTITLES_MODE,
      g_param_spec_boolean ("subtitles-mode", "Enable subtitles mode",
          "Enables subtitles mode for text output stripping the blank lines and "
          "the teletext state lines", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_SUBS_TEMPLATE,
      g_param_spec_string ("subtitles-template", "Subtitles output template",
          "Output template used to print each one of the subtitles lines",
          g_strescape ("%s\n", NULL),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FONT_DESCRIPTION,
      g_param_spec_string ("font-description", "Pango font description",
          "Font description used for the pango output.",
          DEFAULT_FONT_DESCRIPTION,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}

/* initialize the new element
 * initialize instance structure
 */
static void
gst_teletextdec_init (GstTeletextDec * teletext, GstTeletextDecClass * klass)
{
  /* Create sink pad */
  teletext->sinkpad = gst_pad_new_from_static_template (&sink_template, "sink");
  gst_pad_set_setcaps_function (teletext->sinkpad,
      GST_DEBUG_FUNCPTR (gst_teletextdec_sink_setcaps));
  gst_pad_set_chain_function (teletext->sinkpad,
      GST_DEBUG_FUNCPTR (gst_teletextdec_chain));
  gst_pad_set_event_function (teletext->sinkpad,
      GST_DEBUG_FUNCPTR (gst_teletextdec_sink_event));
  gst_element_add_pad (GST_ELEMENT (teletext), teletext->sinkpad);

  /* Create src pad */
  teletext->srcpad = gst_pad_new_from_static_template (&src_template, "src");
  gst_pad_set_setcaps_function (teletext->srcpad,
      GST_DEBUG_FUNCPTR (gst_teletextdec_src_set_caps));
  gst_element_add_pad (GST_ELEMENT (teletext), teletext->srcpad);

  teletext->demux = NULL;
  teletext->decoder = NULL;
  teletext->pageno = 0x100;
  teletext->subno = -1;
  teletext->subtitles_mode = FALSE;
  teletext->subtitles_template = g_strescape ("%s\n", NULL);
  teletext->font_description = g_strdup (DEFAULT_FONT_DESCRIPTION);

  teletext->in_timestamp = GST_CLOCK_TIME_NONE;
  teletext->in_duration = GST_CLOCK_TIME_NONE;

  teletext->rate_numerator = 0;
  teletext->rate_denominator = 1;

  teletext->queue = NULL;
  teletext->queue_lock = g_mutex_new ();

  teletext->frame = g_new0 (GstTeletextFrame, 1);
  teletext->frame->sliced_begin = g_new (vbi_sliced, MAX_SLICES);
  teletext->frame->current_slice = teletext->frame->sliced_begin;
  teletext->frame->sliced_end = teletext->frame->sliced_begin + MAX_SLICES;

  teletext->last_ts = 0;

  teletext->process_buf_func = NULL;
}

static void
gst_teletextdec_finalize (GObject * object)
{
  GstTeletextDec *teletext = GST_TELETEXTDEC (object);

  g_mutex_free (teletext->queue_lock);

  g_free (teletext->frame);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_teletextdec_zvbi_init (GstTeletextDec * teletext)
{
  g_return_if_fail (teletext != NULL);

  GST_LOG_OBJECT (teletext, "Initializing structures");

  teletext->decoder = vbi_decoder_new ();

  vbi_event_handler_register (teletext->decoder,
      VBI_EVENT_TTX_PAGE | VBI_EVENT_CAPTION,
      gst_teletextdec_event_handler, teletext);

  g_mutex_lock (teletext->queue_lock);
  teletext->queue = g_queue_new ();
  g_mutex_unlock (teletext->queue_lock);
}

static void
gst_teletextdec_zvbi_clear (GstTeletextDec * teletext)
{
  g_return_if_fail (teletext != NULL);

  GST_LOG_OBJECT (teletext, "Clearing structures");

  if (teletext->demux != NULL) {
    vbi_dvb_demux_delete (teletext->demux);
    teletext->demux = NULL;
  }
  if (teletext->decoder != NULL) {
    vbi_decoder_delete (teletext->decoder);
    teletext->decoder = NULL;
  }
  if (teletext->frame != NULL) {
    g_free (teletext->frame);
    teletext->frame = NULL;
  }

  g_mutex_lock (teletext->queue_lock);
  if (teletext->queue != NULL) {
    g_queue_free (teletext->queue);
    teletext->queue = NULL;
  }
  g_mutex_unlock (teletext->queue_lock);

  teletext->in_timestamp = GST_CLOCK_TIME_NONE;
  teletext->in_duration = GST_CLOCK_TIME_NONE;
  teletext->pageno = 0x100;
  teletext->subno = -1;
  teletext->last_ts = 0;
}

static void
gst_teletextdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTeletextDec *teletext = GST_TELETEXTDEC (object);

  switch (prop_id) {
    case PROP_PAGENO:
      teletext->pageno = (gint) vbi_bin2bcd (g_value_get_int (value));
      break;
    case PROP_SUBNO:
      teletext->subno = g_value_get_int (value);
      break;
    case PROP_SUBTITLES_MODE:
      teletext->subtitles_mode = g_value_get_boolean (value);
      break;
    case PROP_SUBS_TEMPLATE:
      teletext->subtitles_template = g_value_dup_string (value);
      break;
    case PROP_FONT_DESCRIPTION:
      teletext->font_description = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_teletextdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTeletextDec *teletext = GST_TELETEXTDEC (object);

  switch (prop_id) {
    case PROP_PAGENO:
      g_value_set_int (value, (gint) vbi_bcd2dec (teletext->pageno));
      break;
    case PROP_SUBNO:
      g_value_set_int (value, teletext->subno);
      break;
    case PROP_SUBTITLES_MODE:
      g_value_set_boolean (value, teletext->subtitles_mode);
      break;
    case PROP_SUBS_TEMPLATE:
      g_value_set_string (value, teletext->subtitles_template);
      break;
    case PROP_FONT_DESCRIPTION:
      g_value_set_string (value, teletext->font_description);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_teletextdec_sink_event (GstPad * pad, GstEvent * event)
{
  gboolean ret;
  GstTeletextDec *teletext = GST_TELETEXTDEC (gst_pad_get_parent (pad));

  GST_DEBUG_OBJECT (teletext, "got event %s",
      gst_event_type_get_name (GST_EVENT_TYPE (event)));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_NEWSEGMENT:
      /* maybe save and/or update the current segment (e.g. for output
       * clipping) or convert the event into one in a different format
       * (e.g. BYTES to TIME) or drop it and set a flag to send a newsegment
       * event in a different format later */
      ret = gst_pad_push_event (teletext->srcpad, event);
      break;
    case GST_EVENT_EOS:
      /* end-of-stream, we should close down all stream leftovers here */
      gst_teletextdec_zvbi_clear (teletext);
      ret = gst_pad_push_event (teletext->srcpad, event);
      break;
    case GST_EVENT_FLUSH_STOP:
      gst_teletextdec_zvbi_clear (teletext);
      gst_teletextdec_zvbi_init (teletext);
      ret = gst_pad_push_event (teletext->srcpad, event);
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  gst_object_unref (teletext);

  return ret;
}

static GstStateChangeReturn
gst_teletextdec_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstTeletextDec *teletext;

  teletext = GST_TELETEXTDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_teletextdec_zvbi_init (teletext);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret != GST_STATE_CHANGE_SUCCESS)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      gst_teletextdec_zvbi_clear (teletext);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_teletextdec_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  GstTeletextDec *teletext = GST_TELETEXTDEC (gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (caps, 0);
  const gchar *mimetype = gst_structure_get_name (structure);

  GST_DEBUG_OBJECT (teletext, "%s:%s, caps=%" GST_PTR_FORMAT,
      GST_DEBUG_PAD_NAME (pad), caps);

  if (g_strcmp0 (mimetype, "private/teletext") == 0) {
    teletext->process_buf_func = gst_teletextdec_process_telx_buffer;
    goto accept_caps;
  } else if (g_strcmp0 (mimetype, "video/mpeg") == 0) {
    gint version;
    gboolean is_systemstream;

    if (!gst_structure_get_int (structure, "mpegversion", &version) ||
        !gst_structure_get_boolean (structure, "systemstream",
            &is_systemstream))
      goto refuse_caps;

    if (version != 2 || !is_systemstream)
      goto refuse_caps;

    teletext->process_buf_func = gst_teletextdec_process_pes_buffer;
    teletext->demux = vbi_dvb_pes_demux_new (gst_teletextdec_convert, teletext);
    goto accept_caps;
  } else
    goto refuse_caps;

accept_caps:
  {
    gst_object_unref (teletext);
    return gst_teletextdec_push_preroll_buffer (teletext);
  }

refuse_caps:
  {
    GST_ERROR_OBJECT (teletext,
        "pad %s refused renegotiation to %" GST_PTR_FORMAT,
        GST_PAD_NAME (pad), caps);
    gst_object_unref (teletext);
    return FALSE;
  }
}

static gboolean
gst_teletextdec_src_set_caps (GstPad * pad, GstCaps * caps)
{
  GstTeletextDec *teletext;
  GstStructure *structure = NULL;
  const gchar *mimetype;
  GstPad *peer;

  teletext = GST_TELETEXTDEC (gst_pad_get_parent (pad));
  GST_DEBUG_OBJECT (teletext, "Linking teletext source pad");

  if (gst_caps_is_empty (caps)) {
    GST_ERROR_OBJECT (teletext,
        "pad %s refused renegotiation to %" GST_PTR_FORMAT,
        GST_PAD_NAME (pad), caps);
    goto refuse_caps;
  }

  peer = gst_pad_get_peer (pad);
  if (peer) {
    gst_pad_set_caps (peer, caps);
    gst_object_unref (peer);
  }

  structure = gst_caps_get_structure (caps, 0);
  mimetype = gst_structure_get_name (structure);

  if (g_strcmp0 (mimetype, "video/x-raw-rgb") == 0) {
    teletext->output_format = GST_TELETEXTDEC_OUTPUT_FORMAT_RGBA;
    GST_DEBUG_OBJECT (teletext, "Selected RGBA output format");
  } else if (g_strcmp0 (mimetype, "text/html") == 0) {
    teletext->output_format = GST_TELETEXTDEC_OUTPUT_FORMAT_HTML;
    GST_DEBUG_OBJECT (teletext, "Selected HTML output format");
  } else if (g_strcmp0 (mimetype, "text/plain") == 0) {
    teletext->output_format = GST_TELETEXTDEC_OUTPUT_FORMAT_TEXT;
    GST_DEBUG_OBJECT (teletext, "Selected text output format");
  } else if (g_strcmp0 (mimetype, "text/x-pango-markup") == 0) {
    teletext->output_format = GST_TELETEXTDEC_OUTPUT_FORMAT_PANGO;
    GST_DEBUG_OBJECT (teletext, "Selected pango markup output format");
  } else
    goto refuse_caps;

  gst_object_unref (teletext);
  return TRUE;


refuse_caps:
  {
    gst_object_unref (teletext);
    return FALSE;
  }
}

static void
gst_teletextdec_reset_frame (GstTeletextDec * teletext)
{
  teletext->frame->current_slice = teletext->frame->sliced_begin;
  teletext->frame->sliced_end = teletext->frame->sliced_begin + MAX_SLICES;
  teletext->frame->last_field = 0;
  teletext->frame->last_field_line = 0;
  teletext->frame->last_frame_line = 0;
}

static void
gst_teletextdec_process_pes_buffer (GstTeletextDec * teletext, GstBuffer * buf)
{
  vbi_dvb_demux_feed (teletext->demux, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  return;
}

static void
gst_teletextdec_process_telx_buffer (GstTeletextDec * teletext, GstBuffer * buf)
{
  guint8 *data = GST_BUFFER_DATA (buf);
  const gint size = GST_BUFFER_SIZE (buf);
  guint offset = 0;
  gint res;

  teletext->in_timestamp = GST_BUFFER_TIMESTAMP (buf);
  teletext->in_duration = GST_BUFFER_DURATION (buf);

  if (teletext->frame == NULL) {
    gst_teletextdec_reset_frame (teletext);
  }

  while (offset < size) {
    res =
        gst_teletextdec_extract_data_units (teletext, teletext->frame, data,
        &offset, size);

    if (res == VBI_NEW_FRAME) {
      /* We have a new frame, it's time to feed the decoder */
      vbi_sliced *s;
      gint n_lines;

      n_lines = teletext->frame->current_slice - teletext->frame->sliced_begin;
      GST_LOG_OBJECT (teletext, "Completed frame, decoding new %d lines",
          n_lines);
      s = g_memdup (teletext->frame->sliced_begin,
          n_lines * sizeof (vbi_sliced));
      vbi_decode (teletext->decoder, s, n_lines, teletext->last_ts);
      /* From vbi_decode():
       * timestamp shall advance by 1/30 to 1/25 seconds whenever calling this
       * function. Failure to do so will be interpreted as frame dropping, which
       * starts a resynchronization cycle, eventually a channel switch may be assumed
       * which resets even more decoder state. So even if a frame did not contain
       * any useful data this function must be called, with lines set to zero.
       */
      teletext->last_ts += 0.04;

      g_free (s);
      gst_teletextdec_reset_frame (teletext);
    } else if (res == VBI_ERROR) {
      gst_teletextdec_reset_frame (teletext);
      return;
    }
  }
  return;
}

static vbi_bool
gst_teletextdec_convert (vbi_dvb_demux * dx,
    gpointer user_data, const vbi_sliced * sliced, guint n_lines, gint64 pts)
{
  gdouble sample_time;
  vbi_sliced *s;

  GstTeletextDec *teletext = GST_TELETEXTDEC (user_data);

  GST_DEBUG_OBJECT (teletext, "Converting %u lines to decode", n_lines);

  sample_time = pts * (1 / 90000.0);

  s = g_memdup (sliced, n_lines * sizeof (vbi_sliced));
  vbi_decode (teletext->decoder, s, n_lines, sample_time);
  g_free (s);

  return GST_FLOW_OK;
}

static void
gst_teletextdec_event_handler (vbi_event * ev, void *user_data)
{
  page_info *pi;
  vbi_pgno pgno;
  vbi_subno subno;

  GstTeletextDec *teletext = GST_TELETEXTDEC (user_data);

  switch (ev->type) {
    case VBI_EVENT_TTX_PAGE:
      pgno = ev->ev.ttx_page.pgno;
      subno = ev->ev.ttx_page.subno;

      if (pgno != teletext->pageno
          || (teletext->subno != -1 && subno != teletext->subno))
        return;

      GST_DEBUG_OBJECT (teletext, "Received teletext page %03d.%02d",
          (gint) vbi_bcd2dec (pgno), (gint) vbi_bcd2dec (subno));

      pi = g_new (page_info, 1);
      pi->pgno = pgno;
      pi->subno = subno;

      g_mutex_lock (teletext->queue_lock);
      g_queue_push_tail (teletext->queue, pi);
      g_mutex_unlock (teletext->queue_lock);
      break;
    case VBI_EVENT_CAPTION:
      /* TODO: Handle subtitles in caption teletext pages */
      GST_DEBUG_OBJECT (teletext, "Received caption page. Not implemented");
      break;
    default:
      break;
  }
  return;
}

/* this function does the actual processing
 */
static GstFlowReturn
gst_teletextdec_chain (GstPad * pad, GstBuffer * buf)
{
  GstTeletextDec *teletext = GST_TELETEXTDEC (GST_PAD_PARENT (pad));
  GstFlowReturn ret = GST_FLOW_OK;

  teletext->in_timestamp = GST_BUFFER_TIMESTAMP (buf);
  teletext->in_duration = GST_BUFFER_DURATION (buf);

  teletext->process_buf_func (teletext, buf);
  gst_buffer_unref (buf);

  g_mutex_lock (teletext->queue_lock);
  if (!g_queue_is_empty (teletext->queue)) {
    ret = gst_teletextdec_push_page (teletext);
    if (ret != GST_FLOW_OK) {
      g_mutex_unlock (teletext->queue_lock);
      goto error;
    }
  }
  g_mutex_unlock (teletext->queue_lock);

  return ret;

/* ERRORS */
error:
  {
    if (ret != GST_FLOW_OK && ret != GST_FLOW_NOT_LINKED
        && ret != GST_FLOW_FLUSHING) {
      GST_ELEMENT_ERROR (teletext, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (ret)));
      return GST_FLOW_ERROR;
    }
    return ret;
  }
}

static GstFlowReturn
gst_teletextdec_push_page (GstTeletextDec * teletext)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBuffer *buf;
  vbi_page page;
  page_info *pi;
  gint pgno, subno;
  gboolean success;

  pi = (page_info *) g_queue_pop_head (teletext->queue);

  pgno = vbi_bcd2dec (pi->pgno);
  subno = vbi_bcd2dec (pi->subno);

  GST_INFO_OBJECT (teletext, "Fetching teletext page %03d.%02d", pgno, subno);

  success = vbi_fetch_vt_page (teletext->decoder, &page, pi->pgno, pi->subno,
      VBI_WST_LEVEL_3p5, 25, FALSE);
  if (G_UNLIKELY (!success))
    goto fetch_page_failed;

  switch (teletext->output_format) {
    case GST_TELETEXTDEC_OUTPUT_FORMAT_TEXT:
      ret = gst_teletextdec_export_text_page (teletext, &page, &buf);
      break;
    case GST_TELETEXTDEC_OUTPUT_FORMAT_HTML:
      ret = gst_teletextdec_export_html_page (teletext, &page, &buf);
      break;
    case GST_TELETEXTDEC_OUTPUT_FORMAT_RGBA:
      ret = gst_teletextdec_export_rgba_page (teletext, &page, &buf);
      break;
    case GST_TELETEXTDEC_OUTPUT_FORMAT_PANGO:
      ret = gst_teletextdec_export_pango_page (teletext, &page, &buf);
      break;
    default:
      g_assert_not_reached ();
      break;
  }
  vbi_unref_page (&page);
  g_free (pi);

  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  GST_BUFFER_TIMESTAMP (buf) = teletext->in_timestamp;
  GST_BUFFER_DURATION (buf) = teletext->in_duration;

  GST_INFO_OBJECT (teletext, "Pushing buffer of size %d",
      GST_BUFFER_SIZE (buf));

  ret = gst_pad_push (teletext->srcpad, buf);
  if (ret != GST_FLOW_OK)
    goto push_failed;

  return GST_FLOW_OK;

fetch_page_failed:
  {
    GST_ELEMENT_ERROR (teletext, RESOURCE, READ, (NULL), (NULL));
    return GST_FLOW_ERROR;
  }

alloc_failed:
  {
    GST_ERROR_OBJECT (teletext, "Error allocating output buffer, reason %s",
        gst_flow_get_name (ret));
    return ret;
  }

push_failed:
  {
    GST_ERROR_OBJECT (teletext, "Pushing buffer failed, reason %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static gchar **
gst_teletextdec_vbi_page_to_text_lines (GstTeletextDec * teletext,
    guint start, guint stop, vbi_page * page)
{
  const guint lines_count = stop - start + 1;
  const guint line_length = page->columns;
  gchar **lines;
  gint i;

  /* allocate a new NULL-terminated array of strings */
  lines = (gchar **) g_malloc (sizeof (gchar *) * (lines_count + 1));
  lines[lines_count] = g_strdup ('\0');

  /* export each line in the range of the teletext page in text format */
  for (i = start; i <= stop; i++) {
    lines[i - start] = (gchar *) g_malloc (sizeof (gchar) * (line_length + 1));
    vbi_print_page_region (page, lines[i - start], line_length + 1, "UTF-8",
        TRUE, 0, 0, i, line_length, 1);
    /* Add the null character */
    lines[i - start][line_length] = '\0';
  }

  return lines;
}

static GstFlowReturn
gst_teletextdec_export_text_page (GstTeletextDec * teletext, vbi_page * page,
    GstBuffer ** buf)
{
  GstCaps *caps;
  GstFlowReturn ret;
  gchar *text;
  guint size;

  if (teletext->subtitles_mode) {
    gchar **lines;
    GString *subs;
    guint i;

    lines = gst_teletextdec_vbi_page_to_text_lines (teletext, 1, 23, page);
    subs = g_string_new ("");
    /* Strip white spaces and squash blank lines */
    for (i = 0; i < 23; i++) {
      g_strstrip (lines[i]);
      if (g_strcmp0 (lines[i], ""))
        g_string_append_printf (subs, teletext->subtitles_template, lines[i]);
    }
    /* if the page is blank and doesn't contain any line of text, just add a
     * line break */
    if (!g_strcmp0 (subs->str, ""))
      g_string_append (subs, "\n");

    text = subs->str;
    size = subs->len + 1;
    g_string_free (subs, FALSE);
    g_strfreev (lines);
  } else {
    size = page->columns * page->rows;
    text = g_malloc (size);
    vbi_print_page (page, text, size, "UTF-8", FALSE, TRUE);
  }

  /* Allocate new buffer */
  caps = gst_caps_new_simple ("text/plain", NULL);
  ret = gst_pad_alloc_buffer (teletext->srcpad, GST_BUFFER_OFFSET_NONE,
      size, caps, &(*buf));
  if (G_LIKELY (ret == GST_FLOW_OK))
    GST_BUFFER_DATA (*buf) = GST_BUFFER_MALLOCDATA (*buf) = (guint8 *) text;
  else
    gst_buffer_unref (*buf);

  gst_caps_unref (caps);
  return ret;
}

static GstFlowReturn
gst_teletextdec_export_html_page (GstTeletextDec * teletext, vbi_page * page,
    GstBuffer ** buf)
{
  GstCaps *caps;
  GstFlowReturn ret;
  gchar *html;
  gssize size;
  vbi_export *ex;
  gchar *err;

  if (!(ex = vbi_export_new ("html", &err))) {
    GST_ELEMENT_ERROR (teletext, LIBRARY, SETTINGS,
        ("Can't open the HTML export module: %s", err), (NULL));
    g_free (err);
    return GST_FLOW_ERROR;
  }

  /* export to NULL to get size of the memory needed to allocate the page */
  size = vbi_export_mem (ex, NULL, 0, page);
  if (size < 0)
    return GST_FLOW_ERROR;
  html = g_malloc (size);
  vbi_export_mem (ex, html, size, page);

  /* Allocate new buffer */
  caps = gst_caps_new_simple ("text/html", NULL);
  ret = gst_pad_alloc_buffer (teletext->srcpad, GST_BUFFER_OFFSET_NONE,
      size, caps, &(*buf));
  if (G_LIKELY (ret == GST_FLOW_OK))
    GST_BUFFER_DATA (*buf) = GST_BUFFER_MALLOCDATA (*buf) = (guint8 *) html;

  gst_caps_unref (caps);
  return ret;
}

static GstFlowReturn
gst_teletextdec_export_rgba_page (GstTeletextDec * teletext, vbi_page * page,
    GstBuffer ** buf)
{
  guint size;
  GstCaps *caps, *out_caps;
  GstFlowReturn ret;
  gint width, height;
  GstPadTemplate *templ;

  /* one character occupies 12 x 10 pixels */
  width = page->columns * 12;
  height = page->rows * 10;

  caps = gst_caps_new_simple ("video/x-raw-rgb",
      "width", G_TYPE_INT, width,
      "height", G_TYPE_INT, height,
      "framerate", GST_TYPE_FRACTION, teletext->rate_numerator,
      teletext->rate_denominator, NULL);

  templ = gst_static_pad_template_get (&src_template);
  out_caps = gst_caps_intersect (caps, gst_pad_template_get_caps (templ));
  gst_caps_unref (caps);
  gst_object_unref (templ);

  size = (guint) width *(guint) height *sizeof (vbi_rgba);

  ret = gst_pad_alloc_buffer_and_set_caps (teletext->srcpad,
      GST_BUFFER_OFFSET_NONE, size, out_caps, &(*buf));

  if (ret == GST_FLOW_OK) {
    GST_DEBUG_OBJECT (teletext, "Creating image with %d rows and %d cols",
        page->rows, page->columns);
    vbi_draw_vt_page (page, VBI_PIXFMT_RGBA32_LE,
        (vbi_rgba *) GST_BUFFER_DATA (*buf), FALSE, TRUE);
  }

  gst_caps_unref (out_caps);
  return ret;
}

static GstFlowReturn
gst_teletextdec_export_pango_page (GstTeletextDec * teletext, vbi_page * page,
    GstBuffer ** buf)
{
  vbi_char *acp;
  const guint rows = page->rows;
  gchar **colors;
  gchar **lines;
  GString *subs;
  GstCaps *caps;
  GstFlowReturn ret;
  guint start, stop;
  guint i, j;

  colors = (gchar **) g_malloc (sizeof (gchar *) * (rows + 1));
  colors[rows] = g_strdup ('\0');

  /* parse all the lines and approximate it's foreground color using the first
   * non null character */
  for (acp = page->text, i = 0; i < page->rows; acp += page->columns, i++) {
    for (j = 0; j < page->columns; j++) {
      colors[i] = g_strdup (default_color_map[7]);
      if (acp[j].unicode != 0x20) {
        colors[i] = g_strdup (default_color_map[acp[j].foreground]);
        break;
      }
    }
  }

  /* get an array of strings with each line of the telext page */
  start = teletext->subtitles_mode ? 1 : 0;
  stop = teletext->subtitles_mode ? rows - 2 : rows - 1;
  lines = gst_teletextdec_vbi_page_to_text_lines (teletext, start, stop, page);

  /* format each line in pango markup */
  subs = g_string_new ("");
  for (i = start; i <= stop; i++) {
    g_string_append_printf (subs, PANGO_TEMPLATE,
        teletext->font_description, colors[i], lines[i - start]);
  }

  /* Allocate new buffer */
  caps = gst_caps_new_simple ("text/x-pango-markup", NULL);
  ret = gst_pad_alloc_buffer (teletext->srcpad, GST_BUFFER_OFFSET_NONE,
      subs->len + 1, caps, &(*buf));
  if (G_LIKELY (ret == GST_FLOW_OK))
    GST_BUFFER_DATA (*buf) = GST_BUFFER_MALLOCDATA (*buf) =
        (guint8 *) subs->str;
  else
    gst_buffer_unref (*buf);

  g_strfreev (lines);
  g_strfreev (colors);
  g_string_free (subs, FALSE);
  gst_caps_unref (caps);
  return ret;
}

static gboolean
gst_teletextdec_push_preroll_buffer (GstTeletextDec * teletext)
{
  GstFlowReturn ret;
  GstBuffer *buf;
  gboolean res = TRUE;
  GstStructure *structure;
  const gchar *mimetype;
  GstCaps *out_caps, *peer_caps, *pad_caps;

  /* the stream is sparse, we send a dummy buffer for preroll */
  peer_caps = gst_pad_peer_get_caps (teletext->srcpad);
  pad_caps = gst_pad_get_caps (teletext->srcpad);
  out_caps = gst_caps_intersect (pad_caps, peer_caps);

  if (gst_caps_is_empty (out_caps)) {
    res = FALSE;
    goto beach;
  }

  gst_caps_truncate (out_caps);
  structure = gst_caps_get_structure (out_caps, 0);
  mimetype = gst_structure_get_name (structure);
  if (g_strcmp0 (mimetype, "video/x-raw-rgb") == 0) {
    /* omit preroll buffer for this format */
    goto beach;
  }

  buf = gst_buffer_new_and_alloc (1);
  GST_BUFFER_DATA (buf)[0] = 0;
  gst_buffer_set_caps (buf, out_caps);
  ret = gst_pad_push (teletext->srcpad, buf);
  if (ret != GST_FLOW_OK)
    res = FALSE;

beach:
  {
    gst_caps_unref (out_caps);
    gst_caps_unref (pad_caps);
    gst_caps_unref (peer_caps);
    return res;
  }
}


/* Converts the line_offset / field_parity byte of a VBI data unit. */
static void
gst_teletextdec_lofp_to_line (guint * field, guint * field_line,
    guint * frame_line, guint lofp, systems system)
{
  guint line_offset;

  /* field_parity */
  *field = !(lofp & (1 << 5));

  line_offset = lofp & 31;

  if (line_offset > 0) {
    static const guint field_start[2][2] = {
      {0, 263},
      {0, 313},
    };

    *field_line = line_offset;
    *frame_line = field_start[system][*field] + line_offset;
  } else {
    *field_line = 0;
    *frame_line = 0;
  }
}

static int
gst_teletextdec_line_address (GstTeletextDec * teletext,
    GstTeletextFrame * frame, vbi_sliced ** spp, guint lofp, systems system)
{
  guint field;
  guint field_line;
  guint frame_line;

  if (G_UNLIKELY (frame->current_slice >= frame->sliced_end)) {
    GST_LOG_OBJECT (teletext, "Out of sliced VBI buffer space (%d lines).",
        (int) (frame->sliced_end - frame->sliced_begin));
    return VBI_ERROR;
  }

  gst_teletextdec_lofp_to_line (&field, &field_line, &frame_line, lofp, system);

  GST_LOG_OBJECT (teletext, "Line %u/%u=%u.", field, field_line, frame_line);

  if (frame_line != 0) {
    GST_LOG_OBJECT (teletext, "Last frame Line %u.", frame->last_frame_line);
    if (frame_line <= frame->last_frame_line) {
      GST_LOG_OBJECT (teletext, "New frame");
      return VBI_NEW_FRAME;
    }

    /* FIXME : This never happens, since lofp is a guint8 */
#if 0
    /* new segment flag */
    if (lofp < 0) {
      GST_LOG_OBJECT (teletext, "New frame");
      return VBI_NEW_FRAME;
    }
#endif

    frame->last_field = field;
    frame->last_field_line = field_line;
    frame->last_frame_line = frame_line;

    *spp = frame->current_slice++;
    (*spp)->line = frame_line;
  } else {
    /* Undefined line. */
    return VBI_ERROR;
  }

  return VBI_SUCCESS;
}

static gboolean
gst_teletextdec_extract_data_units (GstTeletextDec * teletext,
    GstTeletextFrame * f, guint8 * packet, guint * offset, gint size)
{
  guint8 *data_unit;
  guint i;

  while (*offset < size) {
    vbi_sliced *s = NULL;
    gint data_unit_id, data_unit_length;

    data_unit = packet + *offset;
    data_unit_id = data_unit[0];
    data_unit_length = data_unit[1];
    GST_LOG_OBJECT (teletext, "vbi header %02x %02x %02x\n", data_unit[0],
        data_unit[1], data_unit[2]);

    switch (data_unit_id) {
      case DATA_UNIT_STUFFING:
      {
        *offset += 2 + data_unit_length;
        break;
      }

      case DATA_UNIT_EBU_TELETEXT_NON_SUBTITLE:
      case DATA_UNIT_EBU_TELETEXT_SUBTITLE:
      {
        gint res;

        if (G_UNLIKELY (data_unit_length != 1 + 1 + 42)) {
          /* Skip this data unit */
          GST_WARNING_OBJECT (teletext, "The data unit length is not 44 bytes");
          *offset += 2 + data_unit_length;
          break;
        }

        res =
            gst_teletextdec_line_address (teletext, f, &s, data_unit[2],
            SYSTEM_625);
        if (G_UNLIKELY (res == VBI_ERROR)) {
          /* Can't retrieve line address, skip this data unit */
          GST_WARNING_OBJECT (teletext,
              "Could not retrieve line address for this data unit");
          return VBI_ERROR;
        }
        if (G_UNLIKELY (f->last_field_line > 0
                && (f->last_field_line - 7 >= 23 - 7))) {
          GST_WARNING_OBJECT (teletext, "Bad line: %d", f->last_field_line - 7);
          return VBI_ERROR;
        }
        if (res == VBI_NEW_FRAME) {
          /* New frame */
          return VBI_NEW_FRAME;
        }
        s->id = VBI_SLICED_TELETEXT_B;
        for (i = 0; i < 42; i++)
          s->data[i] = vbi_rev8 (data_unit[4 + i]);
        *offset += 46;
        break;
      }

      case DATA_UNIT_ZVBI_WSS_CPR1204:
      case DATA_UNIT_ZVBI_CLOSED_CAPTION_525:
      case DATA_UNIT_ZVBI_MONOCHROME_SAMPLES_525:
      case DATA_UNIT_VPS:
      case DATA_UNIT_WSS:
      case DATA_UNIT_CLOSED_CAPTION:
      case DATA_UNIT_MONOCHROME_SAMPLES:
      {
        /*Not supported yet */
        *offset += 2 + data_unit_length;
        break;
      }

      default:
      {
        /* corrupted stream, increase the offset by one until we sync */
        GST_LOG_OBJECT (teletext, "Corrupted, increasing offset by one");
        *offset += 1;
        break;
      }
    }
  }
  return VBI_SUCCESS;
}
