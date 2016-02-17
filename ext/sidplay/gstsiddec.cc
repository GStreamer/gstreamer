/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-siddec
 *
 * This element decodes .sid files to raw audio. .sid files are in fact 
 * small Commodore 64 programs that are executed on an emulated 6502 CPU and a 
 * MOS 6581 sound chip.
 * 
 * This plugin will first load the complete program into memory before starting
 * the emulator and producing output.
 * 
 * Seeking is not (and cannot be) implemented.
 * 
 * <refsect2>
 * <title>Example pipelines</title>
 * |[
 * gst-launch-1.0 -v filesrc location=Hawkeye.sid ! siddec ! audioconvert ! audioresample ! autoaudiosink
 * ]| Decode a sid file and play it back.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <gst/audio/audio.h>
#include "gstsiddec.h"

#define DEFAULT_TUNE		0
#define DEFAULT_CLOCK		SIDTUNE_CLOCK_PAL
#define DEFAULT_MEMORY		MPU_BANK_SWITCHING
#define DEFAULT_FILTER		TRUE
#define DEFAULT_MEASURED_VOLUME	TRUE
#define DEFAULT_MOS8580		FALSE
#define DEFAULT_FORCE_SPEED	FALSE
#define DEFAULT_BLOCKSIZE	4096

enum
{
  PROP_0,
  PROP_TUNE,
  PROP_CLOCK,
  PROP_MEMORY,
  PROP_FILTER,
  PROP_MEASURED_VOLUME,
  PROP_MOS8580,
  PROP_FORCE_SPEED,
  PROP_BLOCKSIZE,
  PROP_METADATA
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sid")
    );

#define FORMATS "{ " GST_AUDIO_NE(S16) "," GST_AUDIO_NE(U16) ", S8, U8 }"

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " FORMATS ", "
        "layout = (string) interleaved, "
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );

GST_DEBUG_CATEGORY_STATIC (gst_siddec_debug);
#define GST_CAT_DEFAULT gst_siddec_debug

#define GST_TYPE_SID_CLOCK (gst_sid_clock_get_type())
static GType
gst_sid_clock_get_type (void)
{
  static GType sid_clock_type = 0;
  static const GEnumValue sid_clock[] = {
    {SIDTUNE_CLOCK_PAL, "PAL", "pal"},
    {SIDTUNE_CLOCK_NTSC, "NTSC", "ntsc"},
    {0, NULL, NULL},
  };

  if (!sid_clock_type) {
    sid_clock_type = g_enum_register_static ("GstSidClock", sid_clock);
  }
  return sid_clock_type;
}

#define GST_TYPE_SID_MEMORY (gst_sid_memory_get_type())
static GType
gst_sid_memory_get_type (void)
{
  static GType sid_memory_type = 0;
  static const GEnumValue sid_memory[] = {
    {MPU_BANK_SWITCHING, "Bank Switching", "bank-switching"},
    {MPU_TRANSPARENT_ROM, "Transparent ROM", "transparent-rom"},
    {MPU_PLAYSID_ENVIRONMENT, "Playsid Environment", "playsid-environment"},
    {0, NULL, NULL},
  };

  if (!sid_memory_type) {
    sid_memory_type = g_enum_register_static ("GstSidMemory", sid_memory);
  }
  return sid_memory_type;
}

static void gst_siddec_finalize (GObject * object);

static GstFlowReturn gst_siddec_chain (GstPad * pad, GstObject * parent,
    GstBuffer * buffer);
static gboolean gst_siddec_sink_event (GstPad * pad, GstObject * parent,
    GstEvent * event);

static gboolean gst_siddec_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_siddec_src_event (GstPad * pad, GstObject * parent,
    GstEvent * event);
static gboolean gst_siddec_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static void gst_siddec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_siddec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#define gst_siddec_parent_class parent_class
G_DEFINE_TYPE (GstSidDec, gst_siddec, GST_TYPE_ELEMENT);

static void
gst_siddec_class_init (GstSidDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_siddec_finalize;
  gobject_class->set_property = gst_siddec_set_property;
  gobject_class->get_property = gst_siddec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_TUNE,
      g_param_spec_int ("tune", "tune", "tune",
          0, 100, DEFAULT_TUNE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_CLOCK,
      g_param_spec_enum ("clock", "clock", "clock",
          GST_TYPE_SID_CLOCK, DEFAULT_CLOCK,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MEMORY,
      g_param_spec_enum ("memory", "memory", "memory", GST_TYPE_SID_MEMORY,
          DEFAULT_MEMORY,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FILTER,
      g_param_spec_boolean ("filter", "filter", "filter", DEFAULT_FILTER,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MEASURED_VOLUME,
      g_param_spec_boolean ("measured-volume", "measured_volume",
          "measured_volume", DEFAULT_MEASURED_VOLUME,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_MOS8580,
      g_param_spec_boolean ("mos8580", "mos8580", "mos8580", DEFAULT_MOS8580,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_FORCE_SPEED,
      g_param_spec_boolean ("force-speed", "force_speed", "force_speed",
          DEFAULT_FORCE_SPEED,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (G_OBJECT_CLASS (klass), PROP_BLOCKSIZE,
      g_param_spec_uint ("blocksize", "Block size",
          "Size in bytes to output per buffer", 1, G_MAXUINT,
          DEFAULT_BLOCKSIZE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_METADATA,
      g_param_spec_boxed ("metadata", "Metadata", "Metadata", GST_TYPE_CAPS,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class, "Sid decoder",
      "Codec/Decoder/Audio", "Use libsidplay to decode SID audio tunes",
      "Wim Taymans <wim.taymans@gmail.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_templ));

  GST_DEBUG_CATEGORY_INIT (gst_siddec_debug, "siddec", 0,
      "C64 sid song player");
}

static void
gst_siddec_init (GstSidDec * siddec)
{
  siddec->sinkpad = gst_pad_new_from_static_template (&sink_templ, "sink");
  gst_pad_set_event_function (siddec->sinkpad, gst_siddec_sink_event);
  gst_pad_set_chain_function (siddec->sinkpad, gst_siddec_chain);
  gst_element_add_pad (GST_ELEMENT (siddec), siddec->sinkpad);

  siddec->srcpad = gst_pad_new_from_static_template (&src_templ, "src");
  gst_pad_set_event_function (siddec->srcpad, gst_siddec_src_event);
  gst_pad_set_query_function (siddec->srcpad, gst_siddec_src_query);
  gst_pad_use_fixed_caps (siddec->srcpad);
  gst_element_add_pad (GST_ELEMENT (siddec), siddec->srcpad);

  siddec->engine = new emuEngine ();
  siddec->tune = new sidTune (0);
  siddec->config = (emuConfig *) g_malloc (sizeof (emuConfig));

  /* get default config parameters */
  siddec->engine->getConfig (*siddec->config);

  siddec->config->mos8580 = DEFAULT_MOS8580;    // mos8580
  siddec->config->memoryMode = DEFAULT_MEMORY;  // memory mode
  siddec->config->clockSpeed = DEFAULT_CLOCK;   // clock speed
  siddec->config->forceSongSpeed = DEFAULT_FORCE_SPEED; // force song speed

  siddec->engine->setConfig (*siddec->config);

  siddec->tune_buffer = (guchar *) g_malloc (maxSidtuneFileLen);
  siddec->tune_len = 0;
  siddec->tune_number = 0;
  siddec->total_bytes = 0;
  siddec->blocksize = DEFAULT_BLOCKSIZE;

  siddec->have_group_id = FALSE;
  siddec->group_id = G_MAXUINT;
}

static void
gst_siddec_finalize (GObject * object)
{
  GstSidDec *siddec = GST_SIDDEC (object);

  g_free (siddec->config);
  g_free (siddec->tune_buffer);

  delete (siddec->tune);
  delete (siddec->engine);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
update_tags (GstSidDec * siddec)
{
  sidTuneInfo info;
  GstTagList *list;

  if (siddec->tune->getInfo (info)) {
    list = gst_tag_list_new_empty ();

    if (info.nameString) {
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_TITLE, info.nameString, (void *) NULL);
    }
    if (info.authorString) {
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_ARTIST, info.authorString, (void *) NULL);
    }
    if (info.copyrightString) {
      gst_tag_list_add (list, GST_TAG_MERGE_REPLACE,
          GST_TAG_COPYRIGHT, info.copyrightString, (void *) NULL);
    }
    gst_pad_push_event (siddec->srcpad, gst_event_new_tag (list));
  }
}

static gboolean
siddec_negotiate (GstSidDec * siddec)
{
  GstCaps *allowed;
  GstStructure *structure;
  int rate = 44100;
  int channels = 1;
  GstCaps *caps;
  const gchar *str;
  GstAudioFormat format;
  GstEvent *event;
  gchar *stream_id;

  allowed = gst_pad_get_allowed_caps (siddec->srcpad);
  if (!allowed)
    goto nothing_allowed;

  GST_DEBUG_OBJECT (siddec, "allowed caps: %" GST_PTR_FORMAT, allowed);

  allowed = gst_caps_normalize (allowed);

  structure = gst_caps_get_structure (allowed, 0);

  str = gst_structure_get_string (structure, "format");
  if (str == NULL)
    goto invalid_format;

  format = gst_audio_format_from_string (str);
  switch (format) {
    case GST_AUDIO_FORMAT_S8:
      siddec->config->bitsPerSample = 8;
      siddec->config->sampleFormat = SIDEMU_SIGNED_PCM;
      break;
    case GST_AUDIO_FORMAT_U8:
      siddec->config->bitsPerSample = 8;
      siddec->config->sampleFormat = SIDEMU_UNSIGNED_PCM;
      break;
    case GST_AUDIO_FORMAT_S16:
      siddec->config->bitsPerSample = 16;
      siddec->config->sampleFormat = SIDEMU_SIGNED_PCM;
      break;
    case GST_AUDIO_FORMAT_U16:
      siddec->config->bitsPerSample = 16;
      siddec->config->sampleFormat = SIDEMU_UNSIGNED_PCM;
      break;
    default:
      goto invalid_format;
  }

  gst_structure_get_int (structure, "rate", &rate);
  siddec->config->frequency = rate;
  gst_structure_get_int (structure, "channels", &channels);
  siddec->config->channels = channels;

  stream_id =
      gst_pad_create_stream_id (siddec->srcpad, GST_ELEMENT_CAST (siddec),
      NULL);

  event = gst_pad_get_sticky_event (siddec->sinkpad, GST_EVENT_STREAM_START, 0);
  if (event) {
    if (gst_event_parse_group_id (event, &siddec->group_id))
      siddec->have_group_id = TRUE;
    else
      siddec->have_group_id = FALSE;
    gst_event_unref (event);
  } else if (!siddec->have_group_id) {
    siddec->have_group_id = TRUE;
    siddec->group_id = gst_util_group_id_next ();
  }

  event = gst_event_new_stream_start (stream_id);
  if (siddec->have_group_id)
    gst_event_set_group_id (event, siddec->group_id);

  gst_pad_push_event (siddec->srcpad, event);
  g_free (stream_id);

  caps = gst_caps_new_simple ("audio/x-raw",
      "format", G_TYPE_STRING, gst_audio_format_to_string (format),
      "layout", G_TYPE_STRING, "interleaved",
      "rate", G_TYPE_INT, siddec->config->frequency,
      "channels", G_TYPE_INT, siddec->config->channels, NULL);
  gst_pad_set_caps (siddec->srcpad, caps);
  gst_caps_unref (caps);

  gst_caps_unref (allowed);

  siddec->engine->setConfig (*siddec->config);

  return TRUE;

  /* ERRORS */
nothing_allowed:
  {
    GST_DEBUG_OBJECT (siddec, "could not get allowed caps");
    return FALSE;
  }
invalid_format:
  {
    GST_DEBUG_OBJECT (siddec, "invalid audio caps");
    gst_caps_unref (allowed);
    return FALSE;
  }
}

static void
play_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstSidDec *siddec;
  GstBuffer *out;
  GstMapInfo outmap;
  gint64 value, offset, time;
  GstFormat format;

  siddec = GST_SIDDEC (gst_pad_get_parent (pad));

  out = gst_buffer_new_and_alloc (siddec->blocksize);

  gst_buffer_map (out, &outmap, GST_MAP_WRITE);
  sidEmuFillBuffer (*siddec->engine, *siddec->tune,
      outmap.data, siddec->blocksize);
  gst_buffer_unmap (out, &outmap);

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  gst_siddec_src_convert (siddec->srcpad,
      GST_FORMAT_BYTES, siddec->total_bytes, &format, &offset);
  GST_BUFFER_OFFSET (out) = offset;

  /* get current timestamp */
  format = GST_FORMAT_TIME;
  gst_siddec_src_convert (siddec->srcpad,
      GST_FORMAT_BYTES, siddec->total_bytes, &format, &time);
  GST_BUFFER_TIMESTAMP (out) = time;

  /* update position and get new timestamp to calculate duration */
  siddec->total_bytes += siddec->blocksize;

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  gst_siddec_src_convert (siddec->srcpad,
      GST_FORMAT_BYTES, siddec->total_bytes, &format, &value);
  GST_BUFFER_OFFSET_END (out) = value;

  format = GST_FORMAT_TIME;
  gst_siddec_src_convert (siddec->srcpad,
      GST_FORMAT_BYTES, siddec->total_bytes, &format, &value);
  GST_BUFFER_DURATION (out) = value - time;

  if ((ret = gst_pad_push (siddec->srcpad, out)) != GST_FLOW_OK)
    goto pause;

done:
  gst_object_unref (siddec);

  return;

  /* ERRORS */
pause:
  {
    const gchar *reason = gst_flow_get_name (ret);

    if (ret == GST_FLOW_EOS) {
      /* perform EOS logic, FIXME, segment seek? */
      gst_pad_push_event (pad, gst_event_new_eos ());
    } else if (ret < GST_FLOW_EOS || ret == GST_FLOW_NOT_LINKED) {
      /* for fatal errors we post an error message */
      GST_ELEMENT_ERROR (siddec, STREAM, FAILED,
          (NULL), ("streaming task paused, reason %s", reason));
      gst_pad_push_event (pad, gst_event_new_eos ());
    }

    GST_INFO_OBJECT (siddec, "pausing task, reason: %s", reason);
    gst_pad_pause_task (pad);
    goto done;
  }
}

static gboolean
start_play_tune (GstSidDec * siddec)
{
  gboolean res;
  GstSegment segment;

  if (!siddec->tune->load (siddec->tune_buffer, siddec->tune_len))
    goto could_not_load;

  update_tags (siddec);

  if (!siddec_negotiate (siddec))
    goto could_not_negotiate;

  if (!sidEmuInitializeSong (*siddec->engine, *siddec->tune,
          siddec->tune_number))
    goto could_not_init;

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (siddec->srcpad, gst_event_new_segment (&segment));
  siddec->total_bytes = 0;
  siddec->have_group_id = FALSE;
  siddec->group_id = G_MAXUINT;

  res = gst_pad_start_task (siddec->srcpad,
      (GstTaskFunction) play_loop, siddec->srcpad, NULL);
  return res;

  /* ERRORS */
could_not_load:
  {
    GST_ELEMENT_ERROR (siddec, LIBRARY, INIT,
        ("Could not load tune"), ("Could not load tune"));
    return FALSE;
  }
could_not_negotiate:
  {
    GST_ELEMENT_ERROR (siddec, CORE, NEGOTIATION,
        ("Could not negotiate format"), ("Could not negotiate format"));
    return FALSE;
  }
could_not_init:
  {
    GST_ELEMENT_ERROR (siddec, LIBRARY, INIT,
        ("Could not initialize song"), ("Could not initialize song"));
    return FALSE;
  }
}

static gboolean
gst_siddec_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSidDec *siddec;
  gboolean res;

  siddec = GST_SIDDEC (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      res = start_play_tune (siddec);
      break;
    case GST_EVENT_SEGMENT:
      res = TRUE;
      break;
    default:
      res = TRUE;
      break;
  }
  gst_event_unref (event);

  return res;
}

static GstFlowReturn
gst_siddec_chain (GstPad * pad, GstObject * parent, GstBuffer * buffer)
{
  GstSidDec *siddec;
  guint64 size;

  siddec = GST_SIDDEC (parent);

  size = gst_buffer_get_size (buffer);
  if (siddec->tune_len + size > maxSidtuneFileLen)
    goto overflow;

  gst_buffer_extract (buffer, 0, siddec->tune_buffer + siddec->tune_len, size);

  siddec->tune_len += size;

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;

  /* ERRORS */
overflow:
  {
    GST_ELEMENT_ERROR (siddec, STREAM, DECODE,
        (NULL), ("Input data bigger than allowed buffer size"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_siddec_src_convert (GstPad * pad, GstFormat src_format, gint64 src_value,
    GstFormat * dest_format, gint64 * dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  GstSidDec *siddec;
  gint bytes_per_sample;

  siddec = GST_SIDDEC (gst_pad_get_parent (pad));

  if (src_format == *dest_format) {
    *dest_value = src_value;
    return TRUE;
  }

  bytes_per_sample =
      (siddec->config->bitsPerSample >> 3) * siddec->config->channels;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          if (bytes_per_sample == 0)
            return FALSE;
          *dest_value = src_value / bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
        {
          gint byterate = bytes_per_sample * siddec->config->frequency;

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
          *dest_value = src_value * bytes_per_sample;
          break;
        case GST_FORMAT_TIME:
          if (siddec->config->frequency == 0)
            return FALSE;
          *dest_value =
              gst_util_uint64_scale_int (src_value, GST_SECOND,
              siddec->config->frequency);
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_BYTES:
          scale = bytes_per_sample;
          /* fallthrough */
        case GST_FORMAT_DEFAULT:
          *dest_value =
              gst_util_uint64_scale_int (src_value,
              scale * siddec->config->frequency, GST_SECOND);
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
gst_siddec_src_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  gboolean res = FALSE;

  switch (GST_EVENT_TYPE (event)) {
    default:
      break;
  }
  gst_event_unref (event);

  return res;
}

static gboolean
gst_siddec_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean res = TRUE;
  GstSidDec *siddec;

  siddec = GST_SIDDEC (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL);

      /* we only know about our bytes, convert to requested format */
      res &= gst_siddec_src_convert (pad,
          GST_FORMAT_BYTES, siddec->total_bytes, &format, &current);
      if (res) {
        gst_query_set_position (query, format, current);
      }
      break;
    }
    default:
      res = gst_pad_query_default (pad, parent, query);
      break;
  }

  return res;
}

static void
gst_siddec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSidDec *siddec = GST_SIDDEC (object);

  switch (prop_id) {
    case PROP_TUNE:
      siddec->tune_number = g_value_get_int (value);
      break;
    case PROP_CLOCK:
      siddec->config->clockSpeed = g_value_get_enum (value);
      break;
    case PROP_MEMORY:
      siddec->config->memoryMode = g_value_get_enum (value);
      break;
    case PROP_FILTER:
      siddec->config->emulateFilter = g_value_get_boolean (value);
      break;
    case PROP_MEASURED_VOLUME:
      siddec->config->measuredVolume = g_value_get_boolean (value);
      break;
    case PROP_MOS8580:
      siddec->config->mos8580 = g_value_get_boolean (value);
      break;
    case PROP_BLOCKSIZE:
      siddec->blocksize = g_value_get_uint (value);
      break;
    case PROP_FORCE_SPEED:
      siddec->config->forceSongSpeed = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }
  siddec->engine->setConfig (*siddec->config);
}

static void
gst_siddec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSidDec *siddec = GST_SIDDEC (object);

  switch (prop_id) {
    case PROP_TUNE:
      g_value_set_int (value, siddec->tune_number);
      break;
    case PROP_CLOCK:
      g_value_set_enum (value, siddec->config->clockSpeed);
      break;
    case PROP_MEMORY:
      g_value_set_enum (value, siddec->config->memoryMode);
      break;
    case PROP_FILTER:
      g_value_set_boolean (value, siddec->config->emulateFilter);
      break;
    case PROP_MEASURED_VOLUME:
      g_value_set_boolean (value, siddec->config->measuredVolume);
      break;
    case PROP_MOS8580:
      g_value_set_boolean (value, siddec->config->mos8580);
      break;
    case PROP_FORCE_SPEED:
      g_value_set_boolean (value, siddec->config->forceSongSpeed);
      break;
    case PROP_BLOCKSIZE:
      g_value_set_uint (value, siddec->blocksize);
      break;
    case PROP_METADATA:
      g_value_set_boxed (value, NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "siddec", GST_RANK_PRIMARY,
      GST_TYPE_SIDDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    siddec,
    "Uses libsidplay to decode .sid files",
    plugin_init, VERSION, "GPL", GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
