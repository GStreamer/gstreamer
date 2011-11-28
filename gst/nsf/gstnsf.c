/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *           (C) <2004> Johan Dahlin <johan@gnome.org>
 *           (C) <2006> Wim Taymans <wim@fluendo.com>
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

#include <string.h>
#include "gstnsf.h"

#define GST_CAT_DEFAULT nsfdec_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

/* Nsfdec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_TUNE		0
#define DEFAULT_FILTER		NSF_FILTER_NONE

enum
{
  PROP_0,
  PROP_TUNE,
  PROP_FILTER,
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-nsf")
    );

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) TRUE, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

#define GST_TYPE_NSF_FILTER (gst_nsf_filter_get_type())
static GType
gst_nsf_filter_get_type (void)
{
  static GType nsf_filter_type = 0;
  static GEnumValue nsf_filter[] = {
    {NSF_FILTER_NONE, "NSF_FILTER_NONE", "None"},
    {NSF_FILTER_LOWPASS, "NSF_FILTER_LOWPASS", "Lowpass"},
    {NSF_FILTER_WEIGHTED, "NSF_FILTER_WEIGHTED", "Weighted"},
    {0, NULL, NULL},
  };

  if (!nsf_filter_type) {
    nsf_filter_type = g_enum_register_static ("GstNsfFilter", nsf_filter);
  }
  return nsf_filter_type;
}


static void gst_nsfdec_base_init (gpointer g_class);
static void gst_nsfdec_class_init (GstNsfDec * klass);
static void gst_nsfdec_init (GstNsfDec * nsfdec);
static void gst_nsfdec_finalize (GObject * object);

static GstFlowReturn gst_nsfdec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_nsfdec_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_nsfdec_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_nsfdec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_nsfdec_src_query (GstPad * pad, GstQuery * query);

static void gst_nsfdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_nsfdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

/* static guint gst_nsfdec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_nsfdec_get_type (void)
{
  static GType nsfdec_type = 0;

  if (G_UNLIKELY (nsfdec_type == 0)) {
    static const GTypeInfo nsfdec_info = {
      sizeof (GstNsfDecClass),
      gst_nsfdec_base_init,
      NULL,
      (GClassInitFunc) gst_nsfdec_class_init,
      NULL,
      NULL,
      sizeof (GstNsfDec),
      0,
      (GInstanceInitFunc) gst_nsfdec_init,
      NULL
    };

    nsfdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstNsfDec", &nsfdec_info,
        (GTypeFlags) 0);
  }

  return nsfdec_type;
}

static void
gst_nsfdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Nsf decoder",
      "Codec/Decoder/Audio",
      "Using nosefart to decode NSF audio tunes",
      "Johan Dahlin <johan@gnome.org>");

  gst_element_class_add_static_pad_template (element_class, &src_templ);
  gst_element_class_add_static_pad_template (element_class, &sink_templ);
}

static void
gst_nsfdec_class_init (GstNsfDec * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;

  parent_class = GST_ELEMENT_CLASS (g_type_class_peek_parent (klass));

  gobject_class->finalize = gst_nsfdec_finalize;
  gobject_class->set_property = gst_nsfdec_set_property;
  gobject_class->get_property = gst_nsfdec_get_property;

  g_object_class_install_property (gobject_class, PROP_TUNE,
      g_param_spec_int ("tune", "tune", "tune", 1, 100, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FILTER,
      g_param_spec_enum ("filter", "filter", "filter", GST_TYPE_NSF_FILTER,
          NSF_FILTER_NONE, G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (nsfdec_debug, "nsfdec", 0,
      "NES sound file (nsf) decoder");

  nsf_init ();
}

static void
gst_nsfdec_init (GstNsfDec * nsfdec)
{
  nsfdec->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_query_function (nsfdec->sinkpad, NULL);
  gst_pad_set_event_function (nsfdec->sinkpad, gst_nsfdec_sink_event);
  gst_pad_set_chain_function (nsfdec->sinkpad, gst_nsfdec_chain);
  gst_element_add_pad (GST_ELEMENT (nsfdec), nsfdec->sinkpad);

  nsfdec->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (nsfdec->srcpad, gst_nsfdec_src_event);
  gst_pad_set_query_function (nsfdec->srcpad, gst_nsfdec_src_query);
  gst_pad_use_fixed_caps (nsfdec->srcpad);
  gst_element_add_pad (GST_ELEMENT (nsfdec), nsfdec->srcpad);

  nsfdec->nsf = NULL;
  nsfdec->state = NSF_STATE_NEED_TUNE;
  nsfdec->tune_buffer = NULL;

  nsfdec->blocksize = 0;
  nsfdec->frequency = 44100;
  nsfdec->bits = 8;
  nsfdec->stereo = FALSE;
  nsfdec->channels = 1;

  nsfdec->tune_number = DEFAULT_TUNE;
  nsfdec->filter = DEFAULT_FILTER;
}

static void
gst_nsfdec_finalize (GObject * object)
{
  GstNsfDec *nsfdec = GST_NSFDEC (object);

  if (nsfdec->tune_buffer)
    gst_buffer_unref (nsfdec->tune_buffer);

  if (nsfdec->taglist)
    gst_tag_list_free (nsfdec->taglist);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static gboolean
nsfdec_negotiate (GstNsfDec * nsfdec)
{
  GstCaps *allowed;
  gboolean sign = TRUE;
  gint width = 16, depth = 16;
  GstStructure *structure;
  int rate = 44100;
  int channels = 1;
  GstCaps *caps;

  allowed = gst_pad_get_allowed_caps (nsfdec->srcpad);
  if (!allowed)
    goto nothing_allowed;

  GST_DEBUG_OBJECT (nsfdec, "allowed caps: %" GST_PTR_FORMAT, allowed);

  structure = gst_caps_get_structure (allowed, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "depth", &depth);

  if (width && depth && width != depth)
    goto wrong_width;

  width = width | depth;
  if (width) {
    nsfdec->bits = width;
  }

  gst_structure_get_boolean (structure, "signed", &sign);
  gst_structure_get_int (structure, "rate", &rate);
  nsfdec->frequency = rate;
  gst_structure_get_int (structure, "channels", &channels);
  nsfdec->channels = channels;
  nsfdec->stereo = (channels == 2);

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "signed", G_TYPE_BOOLEAN, TRUE,
      "width", G_TYPE_INT, nsfdec->bits,
      "depth", G_TYPE_INT, nsfdec->bits,
      "rate", G_TYPE_INT, nsfdec->frequency,
      "channels", G_TYPE_INT, nsfdec->channels, NULL);
  gst_pad_set_caps (nsfdec->srcpad, caps);
  gst_caps_unref (caps);

  gst_caps_unref (allowed);

  return TRUE;

nothing_allowed:
  {
    GST_DEBUG_OBJECT (nsfdec, "could not get allowed caps");
    return FALSE;
  }
wrong_width:
  {
    GST_DEBUG_OBJECT (nsfdec, "width %d and depth %d are different",
        width, depth);
    gst_caps_unref (allowed);
    return FALSE;
  }
}

static void
play_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstNsfDec *nsfdec;
  GstBuffer *out;
  gint64 value, offset, time;
  GstFormat format;

  nsfdec = GST_NSFDEC (gst_pad_get_parent (pad));

  out = gst_buffer_new_and_alloc (nsfdec->blocksize);
  gst_buffer_set_caps (out, GST_PAD_CAPS (pad));

  nsf_frame (nsfdec->nsf);
  apu_process (GST_BUFFER_DATA (out), nsfdec->blocksize / nsfdec->bps);

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  gst_nsfdec_src_convert (nsfdec->srcpad,
      GST_FORMAT_BYTES, nsfdec->total_bytes, &format, &offset);
  GST_BUFFER_OFFSET (out) = offset;

  /* get current timestamp */
  format = GST_FORMAT_TIME;
  gst_nsfdec_src_convert (nsfdec->srcpad,
      GST_FORMAT_BYTES, nsfdec->total_bytes, &format, &time);
  GST_BUFFER_TIMESTAMP (out) = time;

  /* update position and get new timestamp to calculate duration */
  nsfdec->total_bytes += nsfdec->blocksize;

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  gst_nsfdec_src_convert (nsfdec->srcpad,
      GST_FORMAT_BYTES, nsfdec->total_bytes, &format, &value);
  GST_BUFFER_OFFSET_END (out) = value;

  format = GST_FORMAT_TIME;
  gst_nsfdec_src_convert (nsfdec->srcpad,
      GST_FORMAT_BYTES, nsfdec->total_bytes, &format, &value);
  GST_BUFFER_DURATION (out) = value - time;

  if ((ret = gst_pad_push (nsfdec->srcpad, out)) != GST_FLOW_OK)
    goto pause;

done:
  gst_object_unref (nsfdec);

  return;

  /* ERRORS */
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    GST_DEBUG_OBJECT (nsfdec, "pausing task, reason %s", reason);
    gst_pad_pause_task (pad);

    if (ret == GST_FLOW_UNEXPECTED) {
      /* perform EOS logic, FIXME, segment seek? */
      gst_pad_push_event (pad, gst_event_new_eos ());
    } else if (ret == GST_FLOW_NOT_LINKED || ret < GST_FLOW_UNEXPECTED) {
      /* for fatal errors we post an error message */
      GST_ELEMENT_ERROR (nsfdec, STREAM, FAILED,
          (NULL), ("streaming task paused, reason %s", reason));
      gst_pad_push_event (pad, gst_event_new_eos ());
    }
    goto done;
  }
}

static gboolean
start_play_tune (GstNsfDec * nsfdec)
{
  gboolean res;

  nsfdec->nsf = nsf_load (NULL, GST_BUFFER_DATA (nsfdec->tune_buffer),
      GST_BUFFER_SIZE (nsfdec->tune_buffer));

  if (!nsfdec->nsf)
    goto could_not_load;

  if (!nsfdec_negotiate (nsfdec))
    goto could_not_negotiate;

  nsfdec->taglist = gst_tag_list_new ();
  gst_tag_list_add (nsfdec->taglist, GST_TAG_MERGE_REPLACE,
      GST_TAG_AUDIO_CODEC, "NES Sound Format", NULL);

  if (nsfdec->nsf->artist_name)
    gst_tag_list_add (nsfdec->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_ARTIST, nsfdec->nsf->artist_name, NULL);

  if (nsfdec->nsf->song_name)
    gst_tag_list_add (nsfdec->taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_TITLE, nsfdec->nsf->song_name, NULL);

  gst_element_post_message (GST_ELEMENT_CAST (nsfdec),
      gst_message_new_tag (GST_OBJECT (nsfdec),
          gst_tag_list_copy (nsfdec->taglist)));

  nsf_playtrack (nsfdec->nsf,
      nsfdec->tune_number, nsfdec->frequency, nsfdec->bits, nsfdec->stereo);
  nsf_setfilter (nsfdec->nsf, nsfdec->filter);

  nsfdec->bps = (nsfdec->bits >> 3) * nsfdec->channels;
  /* calculate the number of bytes we need to output after each call to
   * nsf_frame(). */
  nsfdec->blocksize =
      nsfdec->bps * nsfdec->frequency / nsfdec->nsf->playback_rate;

  gst_pad_push_event (nsfdec->srcpad,
      gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME, 0, -1, 0));

  res = gst_pad_start_task (nsfdec->srcpad,
      (GstTaskFunction) play_loop, nsfdec->srcpad);

  return res;

  /* ERRORS */
could_not_load:
  {
    GST_ELEMENT_ERROR (nsfdec, LIBRARY, INIT,
        ("Could not load tune"), ("Could not load tune"));
    return FALSE;
  }
could_not_negotiate:
  {
    GST_ELEMENT_ERROR (nsfdec, CORE, NEGOTIATION,
        ("Could not negotiate format"), ("Could not negotiate format"));
    return FALSE;
  }
}

static gboolean
gst_nsfdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstNsfDec *nsfdec;
  gboolean res;

  nsfdec = GST_NSFDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      res = start_play_tune (nsfdec);
      break;
    case GST_EVENT_NEWSEGMENT:
      res = FALSE;
      break;
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
  gst_object_unref (nsfdec);

  return res;
}

static GstFlowReturn
gst_nsfdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstNsfDec *nsfdec;

  nsfdec = GST_NSFDEC (gst_pad_get_parent (pad));

  /* collect all data, we start doing something when we get an EOS
   * event */
  if (nsfdec->tune_buffer) {
    nsfdec->tune_buffer = gst_buffer_join (nsfdec->tune_buffer, buffer);
  } else {
    nsfdec->tune_buffer = buffer;
  }

  gst_object_unref (nsfdec);

  return GST_FLOW_OK;
}

static gboolean
gst_nsfdec_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  GstNsfDec *nsfdec;

  nsfdec = GST_NSFDEC (gst_pad_get_parent (pad));

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (nsfdec->bps == 0)
            return FALSE;
          *dest_value = src_value / nsfdec->bps;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = nsfdec->bps * nsfdec->frequency;

          if (byterate == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND, byterate);
          break;
        }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_DEFAULT:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          *dest_value = src_value * nsfdec->bps;
          break;
        case GST_FORMAT_TIME:
          if (nsfdec->frequency == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND,
              nsfdec->frequency);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = nsfdec->bps;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (src_value, scale * nsfdec->frequency,
              GST_SECOND);
          break;
        default:
          res = FALSE;
      }
      break;
    default:
      res = FALSE;
  }

  return res;
}

static gboolean
gst_nsfdec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = FALSE;
  GstNsfDec *nsfdec;

  nsfdec = GST_NSFDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }
  gst_event_unref (event);

  gst_object_unref (nsfdec);

  return res;
}

static gboolean
gst_nsfdec_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstNsfDec *nsfdec;

  nsfdec = GST_NSFDEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL);

      /* we only know about our bytes, convert to requested format */
      res &= gst_nsfdec_src_convert (pad,
          GST_FORMAT_BYTES, nsfdec->total_bytes, &format, &current);
      if (res) {
        gst_query_set_position (query, format, current);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, query);
      break;
  }
  gst_object_unref (nsfdec);

  return res;
}

static void
gst_nsfdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstNsfDec *nsfdec;

  nsfdec = GST_NSFDEC (object);

  switch (prop_id) {
    case PROP_TUNE:
      nsfdec->tune_number = g_value_get_int (value);
      break;
    case PROP_FILTER:
      nsfdec->filter = g_value_get_enum (value);
      if (nsfdec->nsf)
        nsf_setfilter (nsfdec->nsf, nsfdec->filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }
}

static void
gst_nsfdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstNsfDec *nsfdec;

  nsfdec = GST_NSFDEC (object);

  switch (prop_id) {
    case PROP_TUNE:
      g_value_set_int (value, nsfdec->tune_number);
      break;
    case PROP_FILTER:
      g_value_set_enum (value, nsfdec->filter);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "nsfdec", GST_RANK_PRIMARY,
      GST_TYPE_NSFDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR,
    "nsf",
    "Uses nosefart to decode .nsf files",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
