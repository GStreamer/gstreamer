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


#include <string.h>

#include "gstsiddec.h"

/* elementfactory information */
static GstElementDetails gst_siddec_details = {
  "Sid decoder",
  "Codec/Audio/Decoder",
  "GPL",
  "Use sidplay to decode SID audio tunes",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be> ",
  "(C) 2001",
};

static GstCaps* sid_typefind (GstBuffer *buf, gpointer priv);

/* typefactory for 'sid' */
static GstTypeDefinition siddefinition = {
  "siddec_audio/sid",
  "audio/sid",
  ".sid",
  sid_typefind,
};


/* Sidec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_TUNE,
  ARG_CLOCK,
  ARG_MEMORY,
  ARG_FILTER,
  ARG_MEASURED_VOLUME,
  ARG_MOS8580,
  ARG_FORCE_SPEED,
  ARG_METADATA,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "siddecoder_sink",
    "audio/sid",
    NULL
  )
)

GST_PAD_TEMPLATE_FACTORY (src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "src_audio",
    "audio/raw",
      "format",       GST_PROPS_STRING ("int"),
        "law",        GST_PROPS_INT (0),            
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
	"signed",     GST_PROPS_LIST (
	                GST_PROPS_BOOLEAN (TRUE),
		        GST_PROPS_BOOLEAN (FALSE)
		      ),
	"width",      GST_PROPS_LIST (
	                GST_PROPS_INT (8),
		        GST_PROPS_INT (16)
		      ),
	"depth",      GST_PROPS_LIST (
	                GST_PROPS_INT (8),
		        GST_PROPS_INT (16)
		      ),
        "rate",       GST_PROPS_INT_RANGE (8000, 48000),
        "channels",   GST_PROPS_INT_RANGE (1, 2)
  )
)

enum {
  SID_STATE_NEED_TUNE = 1,
  SID_STATE_LOAD_TUNE = 2,
  SID_STATE_PLAY_TUNE = 3,
};

#define GST_TYPE_SID_CLOCK (gst_sid_clock_get_type())
static GType
gst_sid_clock_get_type (void)
{
  static GType sid_clock_type = 0;
  static GEnumValue sid_clock[] = {
    { SIDTUNE_CLOCK_PAL,   "0", "PAL" },
    { SIDTUNE_CLOCK_NTSC,  "1", "NTSC" },
    { 0, NULL, NULL },
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
    { MPU_BANK_SWITCHING,      "32", "Bank Switching" },
    { MPU_TRANSPARENT_ROM,     "33", "Transparent ROM" },
    { MPU_PLAYSID_ENVIRONMENT, "34", "Playsid Environment" },
    { 0, NULL, NULL },
  };
  if (!sid_memory_type) {
    sid_memory_type = g_enum_register_static ("GstSidMemory", sid_memory);
  }
  return sid_memory_type;
}

static void 	gst_siddec_class_init		(GstSidDec *klass);
static void 	gst_siddec_init			(GstSidDec *siddec);

static void 	gst_siddec_loop 		(GstElement *element);

static gboolean gst_siddec_src_convert 		(GstPad *pad, GstFormat src_format, gint64 src_value,
                       				 GstFormat *dest_format, gint64 *dest_value);
static gboolean gst_siddec_src_query 		(GstPad *pad, GstQueryType type,
                     				 GstFormat *format, gint64 *value);

static void     gst_siddec_get_property         (GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);
static void     gst_siddec_set_property         (GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
//static guint gst_siddec_signals[LAST_SIGNAL] = { 0 };

GType
gst_siddec_get_type (void) 
{
  static GType siddec_type = 0;

  if (!siddec_type) {
    static const GTypeInfo siddec_info = {
      sizeof(GstSidDecClass),      
      NULL,
      NULL,
      (GClassInitFunc) gst_siddec_class_init,
      NULL,
      NULL,
      sizeof(GstSidDec),
      0,
      (GInstanceInitFunc) gst_siddec_init,
      NULL
    };
    siddec_type = g_type_register_static (GST_TYPE_ELEMENT, "GstSidDec", &siddec_info, (GTypeFlags)0);
  }

  return siddec_type;
}

static GstCaps*
sid_typefind (GstBuffer *buf, gpointer priv)
{
  guchar *data = GST_BUFFER_DATA (buf);
  GstCaps *newcaps;

  GST_DEBUG (0,"sid_demux: typefind");

  if (strncmp ((const char *)data, "PSID", 4))
    return NULL;

  newcaps = gst_caps_new ("sid_typefind","audio/sid", NULL);

  return newcaps;
}

static void
gst_siddec_class_init (GstSidDec *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_TUNE,
    g_param_spec_int ("tune", "tune", "tune",
                       1, 100, 1, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CLOCK,
    g_param_spec_enum ("clock", "clock", "clock",
                       GST_TYPE_SID_CLOCK, SIDTUNE_CLOCK_PAL, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEMORY,
    g_param_spec_enum ("memory", "memory", "memory",
                       GST_TYPE_SID_MEMORY, MPU_PLAYSID_ENVIRONMENT, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FILTER,
    g_param_spec_boolean ("filter", "filter", "filter",
                       TRUE, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MEASURED_VOLUME,
    g_param_spec_boolean ("measured_volume", "measured_volume", "measured_volume",
                       TRUE, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MOS8580,
    g_param_spec_boolean ("mos8580", "mos8580", "mos8580",
                       TRUE, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FORCE_SPEED,
    g_param_spec_boolean ("force_speed", "force_speed", "force_speed",
                       TRUE, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_METADATA,
    g_param_spec_boxed ("metadata", "Metadata", "Metadata",
                        GST_TYPE_CAPS, G_PARAM_READABLE));

  gobject_class->set_property = gst_siddec_set_property;
  gobject_class->get_property = gst_siddec_get_property;
}

static void 
gst_siddec_init (GstSidDec *siddec) 
{
  siddec->sinkpad = gst_pad_new_from_template (
  		GST_PAD_TEMPLATE_GET (sink_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (siddec), siddec->sinkpad);
  gst_pad_set_query_function (siddec->sinkpad, NULL);
  gst_pad_set_convert_function (siddec->sinkpad, NULL);

  siddec->srcpad = gst_pad_new_from_template (
  		GST_PAD_TEMPLATE_GET (src_templ), "src");
  gst_pad_set_event_function (siddec->srcpad, NULL);
  gst_pad_set_convert_function (siddec->srcpad, gst_siddec_src_convert);
  gst_pad_set_query_function (siddec->srcpad, gst_siddec_src_query);
  gst_element_add_pad (GST_ELEMENT (siddec), siddec->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (siddec), gst_siddec_loop);

  siddec->engine = new emuEngine();
  siddec->tune = new sidTune(0);
  siddec->config = (emuConfig *)g_malloc (sizeof (emuConfig));

  siddec->config->frequency = 44100;                       // frequency
  siddec->config->bitsPerSample = SIDEMU_16BIT;            // bits per sample
  siddec->config->sampleFormat = SIDEMU_SIGNED_PCM;        // sample fomat
  siddec->config->channels = SIDEMU_STEREO;                // channels
  
  siddec->config->sidChips = 0;                            // -
  siddec->config->volumeControl = SIDEMU_NONE;             // volume control
  siddec->config->mos8580 = TRUE;                          // mos8580
  siddec->config->measuredVolume = TRUE;                   // measure volume
  siddec->config->emulateFilter = TRUE;                    // emulate filter
  siddec->config->filterFs = SIDEMU_DEFAULTFILTERFS;       // filter Fs
  siddec->config->filterFm = SIDEMU_DEFAULTFILTERFM;       // filter Fm
  siddec->config->filterFt = SIDEMU_DEFAULTFILTERFT;       // filter Ft
  siddec->config->memoryMode = MPU_PLAYSID_ENVIRONMENT;    // memory mode
  siddec->config->clockSpeed = SIDTUNE_CLOCK_PAL;          // clock speed
  siddec->config->forceSongSpeed = TRUE;                   // force song speed
  siddec->config->digiPlayerScans = 0;                     // digi player scans
  siddec->config->autoPanning = SIDEMU_NONE;               // auto panning

  siddec->engine->setConfig (*siddec->config);
  siddec->engine->setDefaultFilterStrength ();

  siddec->state = SID_STATE_NEED_TUNE;
  siddec->tune_buffer = (guchar *) g_malloc (maxSidtuneFileLen);
  siddec->tune_len = 0;
  siddec->tune_number = 1;
  siddec->total_bytes = 0;
}

static void 
update_metadata (GstSidDec *siddec)
{
  sidTuneInfo info;
  GstProps *props;
  GstPropsEntry *entry;

  if (siddec->tune->getInfo (info)) {
    props = gst_props_empty_new ();

    if (info.nameString) {
      entry = gst_props_entry_new ("Title", GST_PROPS_STRING (info.nameString));
      gst_props_add_entry (props, entry);
    }
    if (info.authorString) {
      entry = gst_props_entry_new ("Composer", GST_PROPS_STRING (info.authorString));
      gst_props_add_entry (props, entry);
    }
    if (info.copyrightString) {
      entry = gst_props_entry_new ("Copyright", GST_PROPS_STRING (info.copyrightString));
      gst_props_add_entry (props, entry);
    }

    siddec->metadata = gst_caps_new ("sid_metadata",
                                     "application/x-gst-metadata",
			              props);

    g_object_notify (G_OBJECT (siddec), "metadata");
  }
}

#define GET_FIXED_INT(caps, name, dest)         \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_caps_get_int (caps, name, (gint*)dest);  \
} G_STMT_END
#define GET_FIXED_BOOLEAN(caps, name, dest)     \
G_STMT_START {                                  \
  if (gst_caps_has_fixed_property (caps, name)) \
    gst_caps_get_boolean (caps, name, dest);    \
} G_STMT_END

static gboolean
siddec_negotiate (GstSidDec *siddec)
{
  GstCaps *allowed;
  gboolean sign = TRUE;
  gint width = 0, depth = 0;

  allowed = gst_pad_get_allowed_caps (siddec->srcpad);
  if (!allowed)
    return FALSE;

  GET_FIXED_INT     (allowed, "width",      &width);
  GET_FIXED_INT     (allowed, "depth",      &depth);

  if (width && depth && width != depth) {
    return FALSE;
  }
  width = width | depth;
  
  if (width) {
    siddec->config->bitsPerSample = width;
  }

  GET_FIXED_BOOLEAN (allowed, "signed",     &sign);
  GET_FIXED_INT     (allowed, "rate",       &siddec->config->frequency);
  GET_FIXED_INT     (allowed, "channels",   &siddec->config->channels);

  siddec->config->sampleFormat = (sign ? SIDEMU_SIGNED_PCM : SIDEMU_UNSIGNED_PCM);
  
  if (!GST_PAD_CAPS (siddec->srcpad)) {
    if (!gst_pad_try_set_caps (siddec->srcpad, 
      GST_CAPS_NEW (
        "siddec_src",
        "audio/raw",
          "format",       GST_PROPS_STRING ("int"),
            "law",        GST_PROPS_INT (0),            
            "endianness", GST_PROPS_INT (G_BYTE_ORDER),
            "signed",     GST_PROPS_BOOLEAN (sign),
            "width",      GST_PROPS_INT (siddec->config->bitsPerSample),
            "depth",      GST_PROPS_INT (siddec->config->bitsPerSample),
            "rate",       GST_PROPS_INT (siddec->config->frequency),
            "channels",   GST_PROPS_INT (siddec->config->channels)
      )))
    {
      return FALSE;
    }
  }

  siddec->engine->setConfig (*siddec->config);

  return TRUE;
}

static void 
gst_siddec_loop (GstElement *element)
{
  GstSidDec *siddec;

  siddec = GST_SIDDEC (element);

  if (siddec->state == SID_STATE_NEED_TUNE) {
    GstBuffer *buf = gst_pad_pull (siddec->sinkpad);
    g_assert (buf != NULL);
      
    if (GST_IS_EVENT (buf)) {
      GstEvent *event = GST_EVENT (buf);

      switch (GST_EVENT_TYPE (buf)) {
	case GST_EVENT_EOS:
          siddec->state = SID_STATE_LOAD_TUNE;
	  break;
	case GST_EVENT_DISCONTINUOUS:
	  break;
	default:
	  // bail out, we're not going to do anything
          gst_event_unref (event);
	  gst_pad_send_event (siddec->srcpad, gst_event_new (GST_EVENT_EOS));
	  gst_element_set_eos (element);
	  return;
      }
      gst_event_unref (event);
    }
    else {
      memcpy (siddec->tune_buffer+siddec->tune_len, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      siddec->tune_len += GST_BUFFER_SIZE (buf);

      gst_buffer_unref (buf);
    }
  }
  if (siddec->state == SID_STATE_LOAD_TUNE) {
    if (!siddec->tune->load (siddec->tune_buffer, siddec->tune_len)) {
      gst_element_error (GST_ELEMENT (siddec), "could not load song");
      return;
    }
    
    update_metadata (siddec);

    if (!siddec_negotiate (siddec)) {
      gst_element_error (GST_ELEMENT (siddec), "could not negotiate format");
      return;
    }

    if (!sidEmuInitializeSong (*siddec->engine, *siddec->tune, siddec->tune_number)) {
      gst_element_error (GST_ELEMENT (siddec), "could not initialize song");
      return;
    }

    siddec->state = SID_STATE_PLAY_TUNE;
  }
  if (siddec->state == SID_STATE_PLAY_TUNE) {
    GstBuffer *out;
    GstFormat format;
    gint64 value;

    out = gst_buffer_new_and_alloc (4096);

    sidEmuFillBuffer (*siddec->engine, *siddec->tune,
 		      GST_BUFFER_DATA (out), GST_BUFFER_SIZE (out));

    format = GST_FORMAT_TIME;
    gst_siddec_src_query (siddec->srcpad, GST_QUERY_POSITION, &format, &value);
    GST_BUFFER_TIMESTAMP (out) = value;

    siddec->total_bytes += 4096;

    gst_pad_push (siddec->srcpad, out);
  }
}

static gboolean
gst_siddec_src_convert (GstPad *pad, GstFormat src_format, gint64 src_value,
                        GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  guint scale = 1;
  GstSidDec *siddec;
  gint bytes_per_sample;

  siddec = GST_SIDDEC (gst_pad_get_parent (pad));

  bytes_per_sample = (siddec->config->bitsPerSample>>3) * siddec->config->channels;

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
          *dest_value = src_value * scale * siddec->config->frequency / GST_SECOND;
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
gst_siddec_src_query (GstPad *pad, GstQueryType type,
                      GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstSidDec *siddec;

  siddec = GST_SIDDEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_POSITION:
      /* we only know about our bytes, convert to requested format */
      res &= gst_pad_convert (pad,
                        GST_FORMAT_BYTES, siddec->total_bytes,
                        format, value);
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
}

static void 
gst_siddec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSidDec *siddec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SIDDEC(object));
  siddec = GST_SIDDEC(object);

  switch(prop_id) {
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
gst_siddec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstSidDec *siddec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SIDDEC(object));
  siddec = GST_SIDDEC(object);

  switch(prop_id) {
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
    case ARG_METADATA:
      g_value_set_boxed (value, siddec->metadata);
      break;
    default:
      /* G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec); */
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* create an elementfactory for the avi_demux element */
  factory = gst_element_factory_new ("siddec",GST_TYPE_SIDDEC,
                                    &gst_siddec_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_templ));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_templ));

  type = gst_type_factory_new (&siddefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "siddec",
  plugin_init
};

