/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wim.taymans@chello.be>
 *
 * gstosssink.c: 
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


#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <unistd.h>
#include <errno.h>

#include <gstosssink.h>

static GstElementDetails gst_osssink_details = {  
  "Audio Sink (OSS)",
  "Sink/Audio",
  "Output to a sound card via OSS",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>, "
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 1999",
};

static void 			gst_osssink_class_init		(GstOssSinkClass *klass);
static void 			gst_osssink_init		(GstOssSink *osssink);
static void 			gst_osssink_finalize		(GObject *object);

static gboolean 		gst_osssink_open_audio		(GstOssSink *sink);
static void 			gst_osssink_close_audio		(GstOssSink *sink);
static void 			gst_osssink_sync_parms 		(GstOssSink *osssink);
static GstElementStateReturn 	gst_osssink_change_state	(GstElement *element);
static GstPadNegotiateReturn	gst_osssink_negotiate 		(GstPad *pad, GstCaps **caps, gpointer *user_data);

static void 			gst_osssink_set_property	(GObject *object, guint prop_id, const GValue *value, 
		  						 GParamSpec *pspec);
static void 			gst_osssink_get_property	(GObject *object, guint prop_id, GValue *value, 
								 GParamSpec *pspec);

static void 			gst_osssink_chain		(GstPad *pad,GstBuffer *buf);

/* OssSink signals and args */
enum {
  SIGNAL_HANDOFF,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_DEVICE,
  ARG_MUTE,
  ARG_FORMAT,
  ARG_CHANNELS,
  ARG_FREQUENCY,
  ARG_FRAGMENT,
  ARG_BUFFER_SIZE
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (osssink_sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "osssink_sink",
    "audio/raw",
      "format",     GST_PROPS_STRING ("int"),   // hack
      "law",        GST_PROPS_INT (0),
      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
      "signed",     GST_PROPS_LIST (
 	      	      GST_PROPS_BOOLEAN (FALSE),
       		      GST_PROPS_BOOLEAN (TRUE)
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
);

#define GST_TYPE_OSSSINK_CHANNELS (gst_osssink_channels_get_type())
static GType 
gst_osssink_channels_get_type(void) {
  static GType osssink_channels_type = 0;
  static GEnumValue osssink_channels[] = {
    {0, "0", "Silence"},
    {1, "1", "Mono"},
    {2, "2", "Stereo"},
    {0, NULL, NULL},
  };
  if (!osssink_channels_type) {
    osssink_channels_type = g_enum_register_static("GstAudiosinkChannels", osssink_channels);
  }
  return osssink_channels_type;
}


static GstElementClass *parent_class = NULL;
static guint gst_osssink_signals[LAST_SIGNAL] = { 0 };

GType
gst_osssink_get_type (void) 
{
  static GType osssink_type = 0;

  if (!osssink_type) {
    static const GTypeInfo osssink_info = {
      sizeof(GstOssSinkClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_osssink_class_init,
      NULL,
      NULL,
      sizeof(GstOssSink),
      0,
      (GInstanceInitFunc)gst_osssink_init,
    };
    osssink_type = g_type_register_static (GST_TYPE_ELEMENT, "GstOssSink", &osssink_info, 0);
  }

  return osssink_type;
}

static GstBufferPool*
gst_osssink_get_bufferpool (GstPad *pad)
{
  GstOssSink *oss;
  
  oss = GST_OSSSINK (gst_pad_get_parent(pad));

  return oss->sinkpool;
}

static void
gst_osssink_finalize (GObject *object)
{
	GstOssSink *osssink = (GstOssSink *) object;

	g_free (osssink->device);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_osssink_class_init (GstOssSinkClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_string("device","device","device",
                        "/dev/dsp",G_PARAM_READWRITE)); // CHECKME!
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MUTE,
    g_param_spec_boolean("mute","mute","mute",
                         TRUE,G_PARAM_READWRITE)); 

  // it would be nice to show format in symbolic form, oh well
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FORMAT,
    g_param_spec_int ("format","format","format",
                      0, G_MAXINT, AFMT_S16_LE, G_PARAM_READWRITE)); 

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNELS,
    g_param_spec_enum("channels","channels","channels",
                      GST_TYPE_OSSSINK_CHANNELS,2,G_PARAM_READWRITE)); 
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FREQUENCY,
    g_param_spec_int("frequency","frequency","frequency",
                     0,G_MAXINT,44100,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FRAGMENT,
    g_param_spec_int("fragment","fragment","fragment",
                     0,G_MAXINT,6,G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFER_SIZE,
    g_param_spec_int("buffer_size","buffer_size","buffer_size",
                     0,G_MAXINT,4096,G_PARAM_READWRITE));

  gst_osssink_signals[SIGNAL_HANDOFF] =
    g_signal_new("handoff",G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                   G_STRUCT_OFFSET(GstOssSinkClass,handoff), NULL, NULL,
                   g_cclosure_marshal_VOID__VOID,G_TYPE_NONE,0);
  
  gobject_class->set_property = gst_osssink_set_property;
  gobject_class->get_property = gst_osssink_get_property;
  gobject_class->finalize     = gst_osssink_finalize;
  
  gstelement_class->change_state = gst_osssink_change_state;
}

static void 
gst_osssink_init (GstOssSink *osssink) 
{
  osssink->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (osssink_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (osssink), osssink->sinkpad);
  gst_pad_set_negotiate_function (osssink->sinkpad, gst_osssink_negotiate);
  gst_pad_set_bufferpool_function (osssink->sinkpad, gst_osssink_get_bufferpool);

  gst_pad_set_chain_function (osssink->sinkpad, gst_osssink_chain);

  osssink->device = g_strdup ("/dev/dsp");
  osssink->fd = -1;
  osssink->clock = gst_clock_get_system();
  osssink->channels = 1;
  osssink->frequency = 11025;
  osssink->fragment = 6;
/* AFMT_*_BE not available on all OSS includes (e.g. FBSD) */
#ifdef WORDS_BIGENDIAN
  osssink->format = AFMT_S16_BE;
#else
  osssink->format = AFMT_S16_LE;
#endif /* WORDS_BIGENDIAN */  
  gst_clock_register (osssink->clock, GST_OBJECT (osssink));
  osssink->bufsize = 4096;
  /* 6 buffers per chunk by default */
  osssink->sinkpool = gst_buffer_pool_get_default (osssink->bufsize, 6);
  
  GST_FLAG_SET (osssink, GST_ELEMENT_THREAD_SUGGESTED);
}

static gboolean
gst_osssink_parse_caps (GstOssSink *osssink, GstCaps *caps)
{
  gint law, endianness, width, depth;
  gboolean sign;
  gint format = -1;

  // deal with the case where there are no props...
  if (gst_caps_get_props(caps) == NULL) return FALSE;
  
  width = gst_caps_get_int (caps, "width");
  depth = gst_caps_get_int (caps, "depth");

  if (width != depth) return FALSE;

  law = gst_caps_get_int (caps, "law");
  endianness = gst_caps_get_int (caps, "endianness");
  sign = gst_caps_get_boolean (caps, "signed");

  if (law == 0) {
    if (width == 16) {
      if (sign == TRUE) {
        if (endianness == G_LITTLE_ENDIAN)
	  format = AFMT_S16_LE;
        else if (endianness == G_BIG_ENDIAN)
	  format = AFMT_S16_BE;
      }
      else {
        if (endianness == G_LITTLE_ENDIAN)
	  format = AFMT_U16_LE;
        else if (endianness == G_BIG_ENDIAN)
	  format = AFMT_U16_BE;
      }
    }
    else if (width == 8) {
      if (sign == TRUE) {
	format = AFMT_S8;
      }
      else {
        format = AFMT_U8;
      }
    }
  }

  if (format == -1) 
   return FALSE;

  osssink->format = format;
  osssink->channels = gst_caps_get_int (caps, "channels");
  osssink->frequency = gst_caps_get_int (caps, "rate");

  return TRUE;
}

static GstPadNegotiateReturn 
gst_osssink_negotiate (GstPad *pad, GstCaps **caps, gpointer *user_data) 
{
  GstOssSink *osssink;

  g_return_val_if_fail (pad != NULL, GST_PAD_NEGOTIATE_FAIL);
  g_return_val_if_fail (GST_IS_PAD (pad), GST_PAD_NEGOTIATE_FAIL);

  osssink = GST_OSSSINK (gst_pad_get_parent (pad));

  GST_INFO (GST_CAT_NEGOTIATION, "osssink: negotiate");
  // we decide
  if (user_data == NULL) {
    *caps = NULL;
    return GST_PAD_NEGOTIATE_TRY;
  }
  // have we got caps?
  else if (*caps) {

    if (gst_osssink_parse_caps (osssink, *caps)) {
      gst_osssink_sync_parms (osssink);

      return GST_PAD_NEGOTIATE_AGREE;
    }

    // FIXME check if the sound card was really set to these caps,
    // else send out another caps..

    return GST_PAD_NEGOTIATE_FAIL;
  }
  
  return GST_PAD_NEGOTIATE_FAIL;
}

static void 
gst_osssink_sync_parms (GstOssSink *osssink) 
{
  audio_buf_info ospace;
  int frag;

  g_return_if_fail (osssink != NULL);
  g_return_if_fail (GST_IS_OSSSINK (osssink));

  if (osssink->fd == -1) return;
  
  if (osssink->fragment >> 16)
      frag = osssink->fragment;
  else
      frag = 0x7FFF0000 | osssink->fragment;
  
  ioctl (osssink->fd, SNDCTL_DSP_SETFRAGMENT, &frag);

  ioctl (osssink->fd, SNDCTL_DSP_RESET, 0);

  ioctl (osssink->fd, SNDCTL_DSP_SETFMT, &osssink->format);
  ioctl (osssink->fd, SNDCTL_DSP_CHANNELS, &osssink->channels);
  ioctl (osssink->fd, SNDCTL_DSP_SPEED, &osssink->frequency);

  ioctl (osssink->fd, SNDCTL_DSP_GETBLKSIZE, &frag);
  ioctl (osssink->fd, SNDCTL_DSP_GETOSPACE, &ospace);

  /*
  g_warning ("osssink: setting sound card to %dHz %d bit %s (%d bytes buffer, %d fragment)\n",
           osssink->frequency, osssink->format,
           (osssink->channels == 2) ? "stereo" : "mono", ospace.bytes, frag);
	   */
  GST_INFO (GST_CAT_PLUGIN_INFO, "osssink: setting sound card to %dHz %d bit %s (%d bytes buffer, %d fragment)",
           osssink->frequency, osssink->format,
           (osssink->channels == 2) ? "stereo" : "mono", ospace.bytes, frag);

}

static void 
gst_osssink_chain (GstPad *pad, GstBuffer *buf) 
{
  GstOssSink *osssink;
  gboolean in_flush;
  audio_buf_info ospace;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);
  
  
  /* this has to be an audio buffer */
  osssink = GST_OSSSINK (gst_pad_get_parent (pad));
//  g_return_if_fail(GST_FLAG_IS_SET(osssink,GST_STATE_RUNNING));

  if (GST_IS_EVENT (buf)) {
    g_print ("eos on osssink\n");
    gst_element_set_state (GST_ELEMENT (osssink), GST_STATE_PAUSED);
    gst_event_free (GST_EVENT (buf));
    return;
  }

  g_signal_emit (G_OBJECT (osssink), gst_osssink_signals[SIGNAL_HANDOFF], 0,
                  osssink);

  if (GST_BUFFER_DATA (buf) != NULL) {
#ifndef GST_DISABLE_TRACE
    gst_trace_add_entry(NULL, 0, buf, "osssink: writing to soundcard");
#endif // GST_DISABLE_TRACE
    //g_print("osssink: writing to soundcard\n");
    if (osssink->fd >= 0) {
      if (!osssink->mute) {
        gst_clock_wait (osssink->clock, GST_BUFFER_TIMESTAMP (buf), GST_OBJECT (osssink));
        ioctl (osssink->fd, SNDCTL_DSP_GETOSPACE, &ospace);
        GST_DEBUG (GST_CAT_PLUGIN_INFO,"osssink: (%d bytes buffer) %d %p %d\n", ospace.bytes, 
			osssink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
        write (osssink->fd, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
        //write(STDOUT_FILENO,GST_BUFFER_DATA(buf),GST_BUFFER_SIZE(buf));
      }
    }
  }
  gst_buffer_unref (buf);
}

static void 
gst_osssink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstOssSink *osssink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OSSSINK (object));
  
  osssink = GST_OSSSINK (object);

  switch (prop_id) {
    case ARG_DEVICE:
      osssink->device = g_strdup (g_value_get_string (value));
      break;
    case ARG_MUTE:
      osssink->mute = g_value_get_boolean (value);
      break;
    case ARG_FORMAT:
      osssink->format = g_value_get_int (value);
      gst_osssink_sync_parms (osssink);
      break;
    case ARG_CHANNELS:
      osssink->channels = g_value_get_enum (value);
      gst_osssink_sync_parms (osssink);
      break;
    case ARG_FREQUENCY:
      osssink->frequency = g_value_get_int (value);
      gst_osssink_sync_parms (osssink);
      break;
    case ARG_FRAGMENT:
      osssink->fragment = g_value_get_int (value);
      gst_osssink_sync_parms (osssink);
      break;
    case ARG_BUFFER_SIZE:
      osssink->bufsize = g_value_get_int (value);
      osssink->sinkpool = gst_buffer_pool_get_default (osssink->bufsize, 6);
      break;
    default:
      break;
  }
}

static void 
gst_osssink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstOssSink *osssink;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OSSSINK (object));
  
  osssink = GST_OSSSINK (object);

  switch (prop_id) {
    case ARG_DEVICE:
      g_value_set_string (value, osssink->device);
      break;
    case ARG_MUTE:
      g_value_set_boolean (value, osssink->mute);
      break;
    case ARG_FORMAT:
      g_value_set_int (value, osssink->format);
      break;
    case ARG_CHANNELS:
      g_value_set_enum (value, osssink->channels);
      break;
    case ARG_FREQUENCY:
      g_value_set_int (value, osssink->frequency);
      break;
    case ARG_FRAGMENT:
      g_value_set_int (value, osssink->fragment);
      break;
    case ARG_BUFFER_SIZE:
      g_value_set_int (value, osssink->bufsize);
      break;
    default:
      break;
  }
}

static gboolean
gst_osssink_open_audio (GstOssSink *sink)
{
  gint caps;
  g_return_val_if_fail (sink->fd == -1, FALSE);

  GST_INFO (GST_CAT_PLUGIN_INFO, "osssink: attempting to open sound device");

  /* first try to open the sound card */
  sink->fd = open(sink->device, O_WRONLY | O_NONBLOCK);
  if (errno == EBUSY) {
    g_warning ("osssink: unable to open the sound device (in use ?)\n");
    return FALSE;
  }

  /* re-open the sound device in blocking mode */
  close(sink->fd);
  sink->fd = open(sink->device, O_WRONLY);

  /* if we have it, set the default parameters and go have fun */
  if (sink->fd >= 0) {
    /* set card state */
    ioctl(sink->fd, SNDCTL_DSP_GETCAPS, &caps);

    GST_INFO(GST_CAT_PLUGIN_INFO, "osssink: Capabilities %08x", caps);

    if (caps & DSP_CAP_DUPLEX)   	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Full duplex");
    if (caps & DSP_CAP_REALTIME) 	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Realtime");
    if (caps & DSP_CAP_BATCH)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Batch");
    if (caps & DSP_CAP_COPROC)   	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Has coprocessor");
    if (caps & DSP_CAP_TRIGGER)  	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Trigger");
    if (caps & DSP_CAP_MMAP)     	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Direct access");

#ifdef DSP_CAP_MULTI
    if (caps & DSP_CAP_MULTI)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Multiple open");
#endif /* DSP_CAP_MULTI */

#ifdef DSP_CAP_BIND
    if (caps & DSP_CAP_BIND)     	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   Channel binding");
#endif /* DSP_CAP_BIND */

    ioctl(sink->fd, SNDCTL_DSP_GETFMTS, &caps);

    GST_INFO (GST_CAT_PLUGIN_INFO, "osssink: Formats %08x", caps);
    if (caps & AFMT_MU_LAW)   		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   MU_LAW");
    if (caps & AFMT_A_LAW)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   A_LAW");
    if (caps & AFMT_IMA_ADPCM)    	GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   IMA_ADPCM");
    if (caps & AFMT_U8)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   U8");
    if (caps & AFMT_S16_LE)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   S16_LE");
    if (caps & AFMT_S16_BE)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   S16_BE");
    if (caps & AFMT_S8)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   S8");
    if (caps & AFMT_U16_LE)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   U16_LE");
    if (caps & AFMT_U16_BE)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   U16_BE");
    if (caps & AFMT_MPEG)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   MPEG");
#ifdef AFMT_AC3
    if (caps & AFMT_AC3)    		GST_INFO (GST_CAT_PLUGIN_INFO, "osssink:   AC3");
#endif

    GST_INFO (GST_CAT_PLUGIN_INFO, "osssink: opened audio (%s) with fd=%d", sink->device, sink->fd);
    GST_FLAG_SET (sink, GST_OSSSINK_OPEN);

    gst_osssink_sync_parms (sink);
    return TRUE;
  }

  return FALSE;
}

static void
gst_osssink_close_audio (GstOssSink *sink)
{
  if (sink->fd < 0) return;

  close(sink->fd);
  sink->fd = -1;

  GST_FLAG_UNSET (sink, GST_OSSSINK_OPEN);

  GST_INFO (GST_CAT_PLUGIN_INFO, "osssink: closed sound device");
}

static GstElementStateReturn 
gst_osssink_change_state (GstElement *element) 
{
  GstOssSink *osssink;

  g_return_val_if_fail (GST_IS_OSSSINK (element), FALSE);

  osssink = GST_OSSSINK (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if (!GST_FLAG_IS_SET (element, GST_OSSSINK_OPEN)) {
        if (!gst_osssink_open_audio (osssink)) {
          return GST_STATE_FAILURE;
        }
      }
      break;
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      if (GST_FLAG_IS_SET (element, GST_OSSSINK_OPEN))
        ioctl (osssink->fd, SNDCTL_DSP_RESET, 0);
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      if (GST_FLAG_IS_SET (element, GST_OSSSINK_OPEN))
        gst_osssink_close_audio (osssink);
      break;
  }
      
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

gboolean 
gst_osssink_factory_init (GstPlugin *plugin) 
{ 
  GstElementFactory *factory;

  factory = gst_elementfactory_new ("osssink", GST_TYPE_OSSSINK, &gst_osssink_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (osssink_sink_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

