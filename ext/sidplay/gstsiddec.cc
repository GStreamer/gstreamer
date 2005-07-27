/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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
#include "gstsiddec.h"

/* Sidec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

#define DEFAULT_BLOCKSIZE	4096

enum
{
  ARG_0,
  ARG_TUNE,
  ARG_CLOCK,
  ARG_MEMORY,
  ARG_FILTER,
  ARG_MEASURED_VOLUME,
  ARG_MOS8580,
  ARG_FORCE_SPEED,
  ARG_BLOCKSIZE,
  ARG_METADATA
      /* FILL ME */
};

static GstStaticPadTemplate sink_templ = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-sid")
    );

static GstStaticPadTemplate src_templ = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) BYTE_ORDER, "
        "signed = (boolean) { true, false }, "
        "width = (int) { 8, 16 }, "
        "depth = (int) { 8, 16 }, "
        "rate = (int) [ 8000, 48000 ], " "channels = (int) [ 1, 2 ]")
    );


#define GST_TYPE_SID_CLOCK (gst_sid_clock_get_type())
static GType
gst_sid_clock_get_type (void)
{
  static GType sid_clock_type = 0;
  static GEnumValue sid_clock[] = {
    {SIDTUNE_CLOCK_PAL, "0", "PAL"},
    {SIDTUNE_CLOCK_NTSC, "1", "NTSC"},
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
  static GEnumValue sid_memory[] = {
    {MPU_BANK_SWITCHING, "32", "Bank Switching"},
    {MPU_TRANSPARENT_ROM, "33", "Transparent ROM"},
    {MPU_PLAYSID_ENVIRONMENT, "34", "Playsid Environment"},
    {0, NULL, NULL},
  };

  if (!sid_memory_type) {
    sid_memory_type = g_enum_register_static ("GstSidMemory", sid_memory);
  }
  return sid_memory_type;
}

static void gst_siddec_base_init (gpointer g_class);
static void gst_siddec_class_init (GstSidDec * klass);
static void gst_siddec_init (GstSidDec * siddec);

static GstFlowReturn gst_siddec_chain (GstPad * pad, GstBuffer * buffer);
static gboolean gst_siddec_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_siddec_src_convert (GstPad * pad, GstFormat src_format,
    gint64 src_value, GstFormat * dest_format, gint64 * dest_value);
static gboolean gst_siddec_src_query (GstPad * pad, GstQuery * query);

static void gst_siddec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_siddec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

static GstElementClass *parent_class = NULL;

//static guint gst_siddec_signals[LAST_SIGNAL] = { 0 };

GType
gst_siddec_get_type (void)
{
  static GType siddec_type = 0;

  if (!siddec_type) {
    static const GTypeInfo siddec_info = {
      sizeof (GstSidDecClass),
      gst_siddec_base_init,
      NULL,
      (GClassInitFunc) gst_siddec_class_init,
      NULL,
      NULL,
      sizeof (GstSidDec),
      0,
      (GInstanceInitFunc) gst_siddec_init,
      NULL
    };

    siddec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSidDec", &siddec_info,
        (GTypeFlags) 0);
  }

  return siddec_type;
}

static void
gst_siddec_base_init (gpointer g_class)
{
  static GstElementDetails gst_siddec_details =
      GST_ELEMENT_DETAILS ("Sid decoder",
      "Codec/Decoder/Audio",
      "Use sidplay to decode SID audio tunes",
      "Wim Taymans <wim.taymans@chello.be> ");
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_siddec_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_templ));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_templ));

}

static void
gst_siddec_class_init (GstSidDec * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  gobject_class->set_property = gst_siddec_set_property;
  gobject_class->get_property = gst_siddec_get_property;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TUNE,
      g_param_spec_int ("tune", "tune", "tune",
          1, 100, 1, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CLOCK,
      g_param_spec_enum ("clock", "clock", "clock",
          GST_TYPE_SID_CLOCK, SIDTUNE_CLOCK_PAL,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEMORY,
      g_param_spec_enum ("memory", "memory", "memory", GST_TYPE_SID_MEMORY,
          MPU_PLAYSID_ENVIRONMENT, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FILTER,
      g_param_spec_boolean ("filter", "filter", "filter", TRUE,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEASURED_VOLUME,
      g_param_spec_boolean ("measured_volume", "measured_volume",
          "measured_volume", TRUE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MOS8580,
      g_param_spec_boolean ("mos8580", "mos8580", "mos8580", TRUE,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FORCE_SPEED,
      g_param_spec_boolean ("force_speed", "force_speed", "force_speed", TRUE,
          (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BLOCKSIZE,
      g_param_spec_ulong ("blocksize", "Block size",
          "Size in bytes to output per buffer", 1, G_MAXULONG,
          DEFAULT_BLOCKSIZE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_METADATA,
      g_param_spec_boxed ("metadata", "Metadata", "Metadata", GST_TYPE_CAPS,
          (GParamFlags) G_PARAM_READABLE));
}

static void
gst_siddec_init (GstSidDec * siddec)
{
  siddec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&sink_templ),
      "sink");
  gst_pad_set_query_function (siddec->sinkpad, NULL);
  gst_pad_set_event_function (siddec->sinkpad, gst_siddec_sink_event);
  gst_pad_set_chain_function (siddec->sinkpad, gst_siddec_chain);
  gst_element_add_pad (GST_ELEMENT (siddec), siddec->sinkpad);

  siddec->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_templ),
      "src");
  gst_pad_set_event_function (siddec->srcpad, NULL);
  gst_pad_set_query_function (siddec->srcpad, gst_siddec_src_query);
  gst_element_add_pad (GST_ELEMENT (siddec), siddec->srcpad);

  siddec->engine = new emuEngine ();
  siddec->tune = new sidTune (0);
  siddec->config = (emuConfig *) g_malloc (sizeof (emuConfig));

  siddec->config->frequency = 44100;    // frequency
  siddec->config->bitsPerSample = SIDEMU_16BIT; // bits per sample
  siddec->config->sampleFormat = SIDEMU_SIGNED_PCM;     // sample fomat
  siddec->config->channels = SIDEMU_STEREO;     // channels

  siddec->config->sidChips = 0; // -
  siddec->config->volumeControl = SIDEMU_NONE;  // volume control
  siddec->config->mos8580 = TRUE;       // mos8580
  siddec->config->measuredVolume = TRUE;        // measure volume
  siddec->config->emulateFilter = TRUE; // emulate filter
  siddec->config->filterFs = SIDEMU_DEFAULTFILTERFS;    // filter Fs
  siddec->config->filterFm = SIDEMU_DEFAULTFILTERFM;    // filter Fm
  siddec->config->filterFt = SIDEMU_DEFAULTFILTERFT;    // filter Ft
  siddec->config->memoryMode = MPU_PLAYSID_ENVIRONMENT; // memory mode
  siddec->config->clockSpeed = SIDTUNE_CLOCK_PAL;       // clock speed
  siddec->config->forceSongSpeed = TRUE;        // force song speed
  siddec->config->digiPlayerScans = 0;  // digi player scans
  siddec->config->autoPanning = SIDEMU_NONE;    // auto panning

  siddec->engine->setConfig (*siddec->config);
  siddec->engine->setDefaultFilterStrength ();

  siddec->tune_buffer = (guchar *) g_malloc (maxSidtuneFileLen);
  siddec->tune_len = 0;
  siddec->tune_number = 1;
  siddec->total_bytes = 0;
  siddec->blocksize = DEFAULT_BLOCKSIZE;
}

#if 0
static void
update_metadata (GstSidDec * siddec)
{
  sidTuneInfo info;
  GstProps *props;
  GstPropsEntry *entry;

  if (siddec->tune->getInfo (info)) {
    props = gst_props_empty_new ();

    if (info.nameString) {
      entry = gst_props_entry_new ("Title", G_TYPE_STRING (info.nameString));
      gst_props_add_entry (props, entry);
    }
    if (info.authorString) {
      entry =
          gst_props_entry_new ("Composer", G_TYPE_STRING (info.authorString));
      gst_props_add_entry (props, entry);
    }
    if (info.copyrightString) {
      entry =
          gst_props_entry_new ("Copyright",
          G_TYPE_STRING (info.copyrightString));
      gst_props_add_entry (props, entry);
    }

    siddec->metadata = gst_caps_new ("sid_metadata",
        "application/x-gst-metadata", props);

    g_object_notify (G_OBJECT (siddec), "metadata");
  }
}
#endif

#define GET_FIXED_INT(caps, name, dest)         \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_structure_get_int  (structure, name, (gint*)dest);  \
} G_STMT_END
#define GET_FIXED_BOOLEAN(caps, name, dest)     \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_structure_get_boolean  (structure, name, dest);    \
} G_STMT_END

static gboolean
siddec_negotiate (GstSidDec * siddec)
{
  GstCaps *allowed;
  gboolean sign = TRUE;
  gint width = 16, depth = 16;
  GstStructure *structure;
  int rate = 22050;
  int channels = 2;

  allowed = gst_pad_get_allowed_caps (siddec->srcpad);
  if (!allowed)
    return FALSE;

  structure = gst_caps_get_structure (allowed, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "depth", &depth);

  if (width && depth && width != depth) {
    return FALSE;
  }
  width = width | depth;

  if (width) {
    siddec->config->bitsPerSample = width;
  }

  gst_structure_get_boolean (structure, "signed", &sign);
  gst_structure_get_int (structure, "rate", &rate);
  siddec->config->frequency = rate;
  gst_structure_get_int (structure, "channels", &channels);
  siddec->config->channels = channels;

  siddec->config->sampleFormat =
      (sign ? SIDEMU_SIGNED_PCM : SIDEMU_UNSIGNED_PCM);

  gst_pad_set_caps (siddec->srcpad,
      gst_caps_new_simple ("audio/x-raw-int",
          "endianness", G_TYPE_INT, G_BYTE_ORDER,
          "signed", G_TYPE_BOOLEAN, sign,
          "width", G_TYPE_INT, siddec->config->bitsPerSample,
          "depth", G_TYPE_INT, siddec->config->bitsPerSample,
          "rate", G_TYPE_INT, siddec->config->frequency,
          "channels", G_TYPE_INT, siddec->config->channels, NULL));

  siddec->engine->setConfig (*siddec->config);

  return TRUE;
}

static void
play_loop (GstPad * pad)
{
  GstFlowReturn ret;
  GstSidDec *siddec;
  GstBuffer *out;
  gint64 value, offset, time;
  GstFormat format;

  siddec = GST_SIDDEC (GST_PAD_PARENT (pad));

  out = gst_buffer_new_and_alloc (siddec->blocksize);
  gst_buffer_set_caps (out, GST_PAD_CAPS (pad));

  sidEmuFillBuffer (*siddec->engine, *siddec->tune,
      GST_BUFFER_DATA (out), GST_BUFFER_SIZE (out));

  /* get offset in samples */
  format = GST_FORMAT_DEFAULT;
  gst_siddec_src_convert (siddec->srcpad,
      GST_FORMAT_BYTES, siddec->total_bytes, &format, &offset);
  GST_BUFFER_OFFSET (out) = offset;

  /* get current timestamp */
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

  return;

pause:
  {
    gst_pad_pause_task (pad);
  }
}

static gboolean
start_play_tune (GstSidDec * siddec)
{
  gboolean res;

  if (!siddec->tune->load (siddec->tune_buffer, siddec->tune_len))
    goto could_not_load;

  //update_metadata (siddec);

  if (!siddec_negotiate (siddec))
    goto could_not_negotiate;

  if (!sidEmuInitializeSong (*siddec->engine, *siddec->tune,
          siddec->tune_number))
    goto could_not_init;

  res = gst_pad_start_task (siddec->srcpad,
      (GstTaskFunction) play_loop, siddec->srcpad);
  return res;

  /* ERRORS */
could_not_load:
  {
    GST_ELEMENT_ERROR (siddec, LIBRARY, TOO_LAZY, (NULL), (NULL));
    return FALSE;
  }
could_not_negotiate:
  {
    GST_ELEMENT_ERROR (siddec, CORE, NEGOTIATION, (NULL), (NULL));
    return FALSE;
  }
could_not_init:
  {
    GST_ELEMENT_ERROR (siddec, LIBRARY, TOO_LAZY, (NULL), (NULL));
    return FALSE;
  }
}

static gboolean
gst_siddec_sink_event (GstPad * pad, GstEvent * event)
{
  GstSidDec *siddec;
  gboolean res;

  siddec = GST_SIDDEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      GST_STREAM_LOCK (pad);
      res = start_play_tune (siddec);
      GST_STREAM_UNLOCK (pad);
      break;
    case GST_EVENT_NEWSEGMENT:
      res = FALSE;
      break;
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);

  return res;
}

static GstFlowReturn
gst_siddec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstSidDec *siddec;
  guint64 size;

  siddec = GST_SIDDEC (GST_PAD_PARENT (pad));

  size = GST_BUFFER_SIZE (buffer);
  if (siddec->tune_len + size > maxSidtuneFileLen)
    goto overflow;

  memcpy (siddec->tune_buffer + siddec->tune_len, GST_BUFFER_DATA (buffer),
      size);
  siddec->tune_len += size;

  gst_buffer_unref (buffer);

  return GST_FLOW_OK;

overflow:
  {
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

  siddec = GST_SIDDEC (GST_PAD_PARENT (pad));

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
          *dest_value = src_value * GST_SECOND / byterate;
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
          *dest_value = src_value * GST_SECOND / siddec->config->frequency;
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
              src_value * scale * siddec->config->frequency / GST_SECOND;
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
gst_siddec_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstSidDec *siddec;

  siddec = GST_SIDDEC (GST_PAD_PARENT (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 current;

      gst_query_parse_position (query, &format, NULL, NULL);

      /* we only know about our bytes, convert to requested format */
      res &= gst_siddec_src_convert (pad,
          GST_FORMAT_BYTES, siddec->total_bytes, &format, &current);
      if (res) {
        gst_query_set_position (query, format, current, -1);
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;
}

static void
gst_siddec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSidDec *siddec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SIDDEC (object));
  siddec = GST_SIDDEC (object);

  switch (prop_id) {
    case ARG_TUNE:
      siddec->tune_number = g_value_get_int (value);
      break;
    case ARG_CLOCK:
      siddec->config->clockSpeed = g_value_get_enum (value);
      break;
    case ARG_MEMORY:
      siddec->config->memoryMode = g_value_get_enum (value);
      break;
    case ARG_FILTER:
      siddec->config->emulateFilter = g_value_get_boolean (value);
      break;
    case ARG_MEASURED_VOLUME:
      siddec->config->measuredVolume = g_value_get_boolean (value);
      break;
    case ARG_MOS8580:
      siddec->config->mos8580 = g_value_get_boolean (value);
      break;
    case ARG_BLOCKSIZE:
      siddec->blocksize = g_value_get_ulong (value);
      break;
    case ARG_FORCE_SPEED:
      siddec->config->forceSongSpeed = g_value_get_boolean (value);
      break;
    default:
      /* G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); */
      return;
  }
  siddec->engine->setConfig (*siddec->config);
}

static void
gst_siddec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSidDec *siddec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SIDDEC (object));
  siddec = GST_SIDDEC (object);

  switch (prop_id) {
    case ARG_TUNE:
      g_value_set_int (value, siddec->tune_number);
      break;
    case ARG_CLOCK:
      g_value_set_enum (value, siddec->config->clockSpeed);
      break;
    case ARG_MEMORY:
      g_value_set_enum (value, siddec->config->memoryMode);
      break;
    case ARG_FILTER:
      g_value_set_boolean (value, siddec->config->emulateFilter);
      break;
    case ARG_MEASURED_VOLUME:
      g_value_set_boolean (value, siddec->config->measuredVolume);
      break;
    case ARG_MOS8580:
      g_value_set_boolean (value, siddec->config->mos8580);
      break;
    case ARG_FORCE_SPEED:
      g_value_set_boolean (value, siddec->config->forceSongSpeed);
      break;
    case ARG_BLOCKSIZE:
      g_value_set_ulong (value, siddec->blocksize);
      break;
    case ARG_METADATA:
      g_value_set_boxed (value, siddec->metadata);
      break;
    default:
      /* G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); */
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
    "siddec",
    "Uses libsid to decode .sid files",
    plugin_init, VERSION, "GPL", GST_PACKAGE, GST_ORIGIN)
