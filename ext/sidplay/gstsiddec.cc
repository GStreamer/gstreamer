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
  "Filter/Decoder/Audio",
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
  ARG_DEPTH,
  ARG_CHANNELS,
  ARG_FREQUENCY,
  ARG_TUNE,
  ARG_CLOCK,
  ARG_MEMORY,
  ARG_FILTER,
  ARG_MEASURED_VOLUME,
  ARG_MOS8580,
  ARG_FORCE_SPEED,
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

#define GST_TYPE_SID_DEPTH (gst_sid_depth_get_type())
static GType
gst_sid_depth_get_type (void)
{
  static GType sid_depth_type = 0;
  static GEnumValue sid_depth[] = {
    { SIDEMU_8BIT,   "8",  "8 bit" },
    { SIDEMU_16BIT,  "16", "16 bit" },
    { 0, NULL, NULL },
  };
  if (!sid_depth_type) {
    sid_depth_type = g_enum_register_static ("GstSidDepth", sid_depth);
  }
  return sid_depth_type;
}

#define GST_TYPE_SID_CHANNELS (gst_sid_channels_get_type())
static GType
gst_sid_channels_get_type (void)
{
  static GType sid_channels_type = 0;
  static GEnumValue sid_channels[] = {
    { SIDEMU_MONO,   "1", "Mono" },
    { SIDEMU_STEREO, "2", "Stereo" },
    { 0, NULL, NULL },
  };
  if (!sid_channels_type) {
    sid_channels_type = g_enum_register_static ("GstSidChannels", sid_channels);
  }
  return sid_channels_type;
}

#define GST_TYPE_SID_FREQUENCY (gst_sid_frequency_get_type())
static GType
gst_sid_frequency_get_type (void)
{
  static GType sid_frequency_type = 0;
  static GEnumValue sid_frequency[] = {
    { 8000,   "8000",  "8000 Hz" },
    { 11025,  "11025", "11025 Hz" },
    { 16500,  "16500", "16500 Hz" },
    { 22050,  "22050", "22050 Hz" },
    { 27500,  "27500", "27500 Hz" },
    { 32000,  "32000", "32000 Hz" },
    { 37500,  "37500", "37500 Hz" },
    { 44100,  "44100", "44100 Hz" },
    { 48000,  "48000", "48000 Hz" },
    { 0, NULL, NULL },
  };
  if (!sid_frequency_type) {
    sid_frequency_type = g_enum_register_static ("GstSidFrequency", sid_frequency);
  }
  return sid_frequency_type;
}

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

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEPTH,
    g_param_spec_enum ("depth", "depth", "depth",
                       GST_TYPE_SID_DEPTH, SIDEMU_16BIT, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNELS,
    g_param_spec_enum ("channels", "channels", "channels",
                       GST_TYPE_SID_CHANNELS, SIDEMU_STEREO, (GParamFlags)G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FREQUENCY,
    g_param_spec_enum ("frequency", "frequency", "frequency",
                       GST_TYPE_SID_FREQUENCY, 44100, (GParamFlags)G_PARAM_READWRITE));
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

  gobject_class->set_property = gst_siddec_set_property;
  gobject_class->get_property = gst_siddec_get_property;
}

static void 
gst_siddec_init (GstSidDec *siddec) 
{
  siddec->sinkpad = gst_pad_new_from_template (
  		GST_PAD_TEMPLATE_GET (sink_templ), "sink");
  gst_element_add_pad (GST_ELEMENT (siddec), siddec->sinkpad);

  siddec->srcpad = gst_pad_new_from_template (
  		GST_PAD_TEMPLATE_GET (src_templ), "src");
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
      switch (GST_EVENT_TYPE (buf)) {
	case GST_EVENT_EOS:
          siddec->state = SID_STATE_LOAD_TUNE;
	  break;
	default:
	  // bail out, we're not going to do anything
	  gst_element_set_eos (element);
	  gst_pad_send_event (siddec->srcpad, gst_event_new (GST_EVENT_EOS));
	  break;
      }
    }
    else {
      memcpy (siddec->tune_buffer+siddec->tune_len, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
      siddec->tune_len += GST_BUFFER_SIZE (buf);

      gst_buffer_unref (buf);
    }
  }
  if (siddec->state == SID_STATE_LOAD_TUNE) {

    if (siddec->tune->load (siddec->tune_buffer, siddec->tune_len)) {
      if (sidEmuInitializeSong (*siddec->engine, *siddec->tune, siddec->tune_number)) {

	gst_pad_try_set_caps (siddec->srcpad, 
			  GST_CAPS_NEW (
			    "siddec_src",
			    "audio/raw",
			      "format",       GST_PROPS_STRING ("int"),
			        "law",        GST_PROPS_INT (0),            
			        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
			        "signed",     GST_PROPS_BOOLEAN (siddec->config->bitsPerSample==8?FALSE:TRUE),
			        "width",      GST_PROPS_INT (siddec->config->bitsPerSample),
			        "depth",      GST_PROPS_INT (siddec->config->bitsPerSample),
			        "rate",       GST_PROPS_INT (siddec->config->frequency),
			        "channels",   GST_PROPS_INT (siddec->config->channels)
				));
	siddec->state = SID_STATE_PLAY_TUNE;
      }
      else {
        g_warning ("siddec: could not initialize song\n");
      }
    }
    else {
      g_warning ("siddec: could not load song\n");
    }
  }
  if (siddec->state == SID_STATE_PLAY_TUNE) {
    GstBuffer *out = gst_buffer_new ();

    GST_BUFFER_SIZE (out) = 4096;
    GST_BUFFER_DATA (out) = (guchar *)g_malloc (4096);

    sidEmuFillBuffer (*siddec->engine, *siddec->tune,
 		      GST_BUFFER_DATA (out), GST_BUFFER_SIZE (out));

    gst_pad_push (siddec->srcpad, out);
  }
}

static void 
gst_siddec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstSidDec *siddec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_SIDDEC(object));
  siddec = GST_SIDDEC(object);

  switch(prop_id) {
    case ARG_DEPTH:
      siddec->config->bitsPerSample =  g_value_get_enum (value);
      siddec->config->sampleFormat = (siddec->config->bitsPerSample==8?
      			SIDEMU_UNSIGNED_PCM:
      			SIDEMU_SIGNED_PCM);
      break;
    case ARG_CHANNELS:
      siddec->config->channels = g_value_get_enum (value);
      break;
    case ARG_FREQUENCY:
      siddec->config->frequency = g_value_get_enum (value);
      break;
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
    case ARG_DEPTH:
      g_value_set_enum (value, siddec->config->bitsPerSample);
      break;
    case ARG_CHANNELS:
      g_value_set_enum (value, siddec->config->channels);
      break;
    case ARG_FREQUENCY:
      g_value_set_enum (value, siddec->config->frequency);
      break;
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

