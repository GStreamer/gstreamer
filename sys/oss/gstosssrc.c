/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstosssrc.c: 
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
#include <fcntl.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <gstosssrc.h>
#include <gstosscommon.h>

/* elementfactory information */
static GstElementDetails gst_osssrc_details = {
  "Audio Source (OSS)",
  "Source/Audio",
  "LGPL",
  "Read from the sound card",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};


/* OssSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_DEVICE,
  ARG_BYTESPERREAD,
};

GST_PAD_TEMPLATE_FACTORY (osssrc_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "osssrc_src",
    "audio/raw",
      "format",		GST_PROPS_STRING ("int"),
      "law",     	GST_PROPS_INT (0),
      "endianness",     GST_PROPS_INT (G_BYTE_ORDER),
      "signed",   	GST_PROPS_LIST (
			  GST_PROPS_BOOLEAN (TRUE),
			  GST_PROPS_BOOLEAN (FALSE)
			),
      "width",   	GST_PROPS_LIST (
			  GST_PROPS_INT (8),
			  GST_PROPS_INT (16)
			),
      "depth",   	GST_PROPS_LIST (
			  GST_PROPS_INT (8),
			  GST_PROPS_INT (16)
			),
      "rate",     	GST_PROPS_INT_RANGE (1000, 48000),
      "channels", 	GST_PROPS_INT_RANGE (1, 2)
  )
)

static void 			gst_osssrc_class_init	(GstOssSrcClass *klass);
static void 			gst_osssrc_init		(GstOssSrc *osssrc);

static GstPadConnectReturn 	gst_osssrc_srcconnect 	(GstPad *pad, GstCaps *caps);
static void 			gst_osssrc_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 			gst_osssrc_get_property	(GObject *object, guint prop_id,
							 GValue *value, GParamSpec *pspec);
static GstElementStateReturn 	gst_osssrc_change_state	(GstElement *element);

static gboolean                 gst_osssrc_send_event (GstElement *element, GstEvent *event);
static void 			gst_osssrc_close_audio	(GstOssSrc *src);
static gboolean 		gst_osssrc_open_audio	(GstOssSrc *src);
static gboolean			gst_osssrc_sync_parms	(GstOssSrc *osssrc);

static GstBuffer *		gst_osssrc_get		(GstPad *pad);

static GstElementClass *parent_class = NULL;
/*static guint gst_osssrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_osssrc_get_type (void) 
{
  static GType osssrc_type = 0;

  if (!osssrc_type) {
    static const GTypeInfo osssrc_info = {
      sizeof(GstOssSrcClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_osssrc_class_init,
      NULL,
      NULL,
      sizeof(GstOssSrc),
      0,
      (GInstanceInitFunc)gst_osssrc_init,
    };
    osssrc_type = g_type_register_static (GST_TYPE_ELEMENT, "GstOssSrc", &osssrc_info, 0);
  }
  return osssrc_type;
}

static void
gst_osssrc_class_init (GstOssSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BYTESPERREAD,
    g_param_spec_ulong("bytes_per_read","bytes_per_read","bytes_per_read",
                       0,G_MAXULONG,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_string("device","device","oss device (/dev/dspN usually)",
                        "default",G_PARAM_READWRITE));
  
  gobject_class->set_property = gst_osssrc_set_property;
  gobject_class->get_property = gst_osssrc_get_property;

  gstelement_class->change_state = gst_osssrc_change_state;
  gstelement_class->send_event = gst_osssrc_send_event;
}

static void 
gst_osssrc_init (GstOssSrc *osssrc) 
{
  osssrc->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (osssrc_src_factory), "src");
  gst_pad_set_get_function (osssrc->srcpad, gst_osssrc_get);
  gst_pad_set_connect_function (osssrc->srcpad, gst_osssrc_srcconnect);
  gst_element_add_pad (GST_ELEMENT (osssrc), osssrc->srcpad);

  osssrc->device = g_strdup ("/dev/dsp");
  osssrc->fd = -1;

  /* adding some default values */
  osssrc->law = 0;
  osssrc->endianness = G_BYTE_ORDER;
  osssrc->sign = TRUE;
  osssrc->depth = 16;
  osssrc->width = 16;
  osssrc->channels = 2;
  osssrc->rate = 44100;
  osssrc->need_eos = FALSE;
  
  osssrc->bytes_per_read = 4096;
  osssrc->curoffset = 0;
  osssrc->basetime = 0;
  osssrc->samples_since_basetime = 0;
}

static GstPadConnectReturn 
gst_osssrc_srcconnect (GstPad *pad, GstCaps *caps)
{
  GstOssSrc *src;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int 	(caps, "law", 		&src->law);
  gst_caps_get_int 	(caps, "endianness", 	&src->endianness);
  gst_caps_get_boolean 	(caps, "signed", 	&src->sign);
  gst_caps_get_int 	(caps, "width", 	&src->width);
  gst_caps_get_int 	(caps, "depth", 	&src->depth);
  gst_caps_get_int 	(caps, "rate", 		&src->rate);
  gst_caps_get_int 	(caps, "channels", 	&src->channels);

  if (!gst_osssrc_sync_parms (src))
    return GST_PAD_CONNECT_REFUSED;

  return GST_PAD_CONNECT_OK;
}
#define GET_FIXED_INT(caps, name, dest) 	\
G_STMT_START {					\
  if (gst_caps_has_fixed_property (caps, name))	\
    gst_caps_get_int (caps, name, dest);	\
} G_STMT_END
#define GET_FIXED_BOOLEAN(caps, name, dest) 	\
G_STMT_START {					\
  if (gst_caps_has_fixed_property (caps, name))	\
    gst_caps_get_boolean (caps, name, dest);	\
} G_STMT_END

static gboolean
gst_osssrc_negotiate (GstPad *pad)
{
  GstOssSrc *src;
  GstCaps *allowed;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  allowed = gst_pad_get_allowed_caps (pad);

  /* peel off fixed stuff from the allowed caps */
  GET_FIXED_INT 	(allowed, "law", 	&src->law);
  GET_FIXED_INT 	(allowed, "endianness", &src->endianness);
  GET_FIXED_BOOLEAN 	(allowed, "signed", 	&src->sign);
  GET_FIXED_INT 	(allowed, "width", 	&src->width);
  GET_FIXED_INT 	(allowed, "depth", 	&src->depth);
  GET_FIXED_INT 	(allowed, "rate", 	&src->rate);
  GET_FIXED_INT 	(allowed, "channels", 	&src->channels);

  if (!gst_osssrc_sync_parms (src))
    return FALSE;
    
  /* set caps on src pad */
  if (gst_pad_try_set_caps (src->srcpad, 
	GST_CAPS_NEW (
    	  "oss_src",
	  "audio/raw",
            "format",       GST_PROPS_STRING ("int"),
	      "law",        GST_PROPS_INT (src->law),
	      "endianness", GST_PROPS_INT (src->endianness),
	      "signed",     GST_PROPS_BOOLEAN (src->sign),
	      "width",      GST_PROPS_INT (src->width),
	      "depth",      GST_PROPS_INT (src->depth),
	      "rate",       GST_PROPS_INT (src->rate),
	      "channels",   GST_PROPS_INT (src->channels)
        )) <= 0) 
  {
    return FALSE;
  }
  return TRUE;
}
	
static GstBuffer *
gst_osssrc_get (GstPad *pad)
{
  GstOssSrc *src;
  GstBuffer *buf;
  glong readbytes;
  glong readsamples;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "attempting to read something from the soundcard");

  if (src->need_eos) {
    src->need_eos = FALSE;
    return GST_BUFFER (gst_event_new (GST_EVENT_EOS));
  }
  
  buf = gst_buffer_new_and_alloc (src->bytes_per_read);
  
  readbytes = read (src->fd,GST_BUFFER_DATA (buf),
                    src->bytes_per_read);

  if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (src));
    return NULL;
  }

  if (!GST_PAD_CAPS (pad)) {
    /* nothing was negotiated, we can decide on a format */
    if (!gst_osssrc_negotiate (pad)) {
      gst_element_error (GST_ELEMENT (src), "could not negotiate format");
      return NULL;
    }
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_TIMESTAMP (buf) = src->basetime +
	  src->samples_since_basetime * GST_SECOND / src->rate;

  src->curoffset += readbytes;
  readsamples = readbytes / src->channels;
  if (src->width == 16) readsamples /= 2;
  src->samples_since_basetime += readsamples;

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "pushed buffer from soundcard of %ld bytes, timestamp %lld", 
		  readbytes, GST_BUFFER_TIMESTAMP (buf));

  return buf;
}

static void 
gst_osssrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstOssSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OSSSRC (object));
  
  src = GST_OSSSRC (object);

  switch (prop_id) {
    case ARG_BYTESPERREAD:
      src->bytes_per_read = g_value_get_ulong (value);
      break;
    case ARG_DEVICE:
      g_free(src->device);
      src->device = g_strdup (g_value_get_string (value));
      break;
    default:
      break;
  }
}

static void 
gst_osssrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstOssSrc *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_OSSSRC (object));
  
  src = GST_OSSSRC (object);

  switch (prop_id) {
    case ARG_BYTESPERREAD:
      g_value_set_ulong (value, src->bytes_per_read);
      break;
    case ARG_DEVICE:
      g_value_set_string (value, src->device);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn 
gst_osssrc_change_state (GstElement *element) 
{
  GstOssSrc *osssrc = GST_OSSSRC (element);
  
  GST_DEBUG (GST_CAT_PLUGIN_INFO, "osssrc: state change");

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      if (!GST_FLAG_IS_SET (element, GST_OSSSRC_OPEN)) { 
        if (!gst_osssrc_open_audio (osssrc))
          return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_OSSSRC_OPEN))
        gst_osssrc_close_audio (osssrc);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return GST_STATE_SUCCESS;
}

static gboolean
gst_osssrc_send_event (GstElement *element,
		       GstEvent *event)
{
  gboolean retval = FALSE;
  GstOssSrc *osssrc;

  osssrc = GST_OSSSRC (element);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      osssrc->need_eos = TRUE;
      retval = TRUE;
      break;
    default:
      break;
  }

  gst_event_unref (event);
  return retval;
}

static gboolean 
gst_osssrc_open_audio (GstOssSrc *src) 
{
  g_return_val_if_fail (!GST_FLAG_IS_SET (src, GST_OSSSRC_OPEN), FALSE);

  /* first try to open the sound card */
  src->fd = open(src->device, O_RDONLY);

  /* if we have it, set the default parameters and go have fun */ 
  if (src->fd > 0) {

    /* set card state */
    GST_DEBUG (GST_CAT_PLUGIN_INFO,"opened audio: %s",src->device);
    
    GST_FLAG_SET (src, GST_OSSSRC_OPEN);
    return TRUE;
  }

  return FALSE;
}

static void 
gst_osssrc_close_audio (GstOssSrc *src) 
{
  g_return_if_fail (GST_FLAG_IS_SET (src, GST_OSSSRC_OPEN));

  close(src->fd);
  src->fd = -1;

  GST_FLAG_UNSET (src, GST_OSSSRC_OPEN);
}

static gboolean 
gst_osssrc_sync_parms (GstOssSrc *osssrc) 
{
  audio_buf_info ispace;
  gint frag;
  /* remember : ioctl on samplerate returns the sample rate the card
   * is actually set to ! Setting it to 44101 Hz could cause it to
   * be set to 44101, for example
   */
  guint rate;
  gint format;
  gint bps;

  g_return_val_if_fail (osssrc->fd > 0, FALSE);
 
  /* get rate, we don't modify the original rate as the audio device
   * might not exactly give us the requested value */
  rate = osssrc->rate;

  /* transform format parameters to oss format */
  if (!gst_ossformat_get (osssrc->law, osssrc->endianness, osssrc->sign, 
			  osssrc->width, osssrc->depth, &format, &bps)) 
  {
    return FALSE;
  }
  
  frag = 0x7fff0006;

  ioctl(osssrc->fd, SNDCTL_DSP_SETFRAGMENT, &frag);
  ioctl(osssrc->fd, SNDCTL_DSP_RESET, 0);
 
  ioctl(osssrc->fd, SNDCTL_DSP_SETFMT, &format);
  ioctl(osssrc->fd, SNDCTL_DSP_CHANNELS, &osssrc->channels);
  ioctl(osssrc->fd, SNDCTL_DSP_SPEED, &rate);
  ioctl(osssrc->fd, SNDCTL_DSP_GETISPACE, &ispace);
  ioctl(osssrc->fd, SNDCTL_DSP_GETBLKSIZE, &frag);
 
  g_print("setting sound card to %dHz %d bit %s (%d bytes buffer, %d fragment)\n",
          rate, osssrc->width,
          (osssrc->channels == 2) ? "stereo" : "mono", ispace.bytes, frag);

  return TRUE;
}

gboolean
gst_osssrc_factory_init (GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_element_factory_new ("osssrc", 
		  		     GST_TYPE_OSSSRC, 
				     &gst_osssrc_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_element_factory_add_pad_template (factory, 
		  			GST_PAD_TEMPLATE_GET (osssrc_src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
