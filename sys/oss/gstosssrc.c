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

//#define WEIRD_THING	/* show the weird thing with 44100 KHz rec */

static GstElementDetails gst_osssrc_details = {
  "Audio Source (OSS)",
  "Source/Audio",
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
  ARG_CUROFFSET,
  ARG_FORMAT,
  ARG_CHANNELS,
  ARG_FREQUENCY
};

GST_PADTEMPLATE_FACTORY (osssrc_src_factory,
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

static void 			gst_osssrc_set_property	(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void 			gst_osssrc_get_property	(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static GstElementStateReturn 	gst_osssrc_change_state	(GstElement *element);

static void 			gst_osssrc_close_audio	(GstOssSrc *src);
static gboolean 		gst_osssrc_open_audio	(GstOssSrc *src);
static void 			gst_osssrc_sync_parms	(GstOssSrc *osssrc);

static GstBuffer *		gst_osssrc_get		(GstPad *pad);

static GstElementClass *parent_class = NULL;
//static guint gst_osssrc_signals[LAST_SIGNAL] = { 0 };

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
                       0,G_MAXULONG,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CUROFFSET,
    g_param_spec_ulong("curoffset","curoffset","curoffset",
                       0,G_MAXULONG,0,G_PARAM_READABLE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FORMAT,
    g_param_spec_int("format","format","format",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_CHANNELS,
    g_param_spec_int("channels","channels","channels",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_FREQUENCY,
    g_param_spec_int("frequency","frequency","frequency",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); // CHECKME
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_string("device","device","oss device (/dev/dspN usually)",
                        "default",G_PARAM_READWRITE));
  
  gobject_class->set_property = gst_osssrc_set_property;
  gobject_class->get_property = gst_osssrc_get_property;

  gstelement_class->change_state = gst_osssrc_change_state;
}

static void 
gst_osssrc_init (GstOssSrc *osssrc) 
{
  osssrc->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (osssrc_src_factory), "src");
  gst_pad_set_get_function(osssrc->srcpad,gst_osssrc_get);
  gst_element_add_pad (GST_ELEMENT (osssrc), osssrc->srcpad);

  osssrc->device = g_strdup ("/dev/dsp");
  osssrc->fd = -1;

  /* adding some default values */
  osssrc->format = AFMT_S16_LE;
  osssrc->channels = 2;
  osssrc->frequency = 44100;
  
  osssrc->bytes_per_read = 4096;
  osssrc->curoffset = 0;
  osssrc->seq = 0;
}

static GstBuffer *
gst_osssrc_get (GstPad *pad)
{
  GstOssSrc *src;
  GstBuffer *buf;
  glong readbytes;

  g_return_val_if_fail (pad != NULL, NULL);
  src = GST_OSSSRC(gst_pad_get_parent (pad));

  GST_DEBUG (0, "attempting to read something from soundcard\n");

  buf = gst_buffer_new ();
  g_return_val_if_fail (buf, NULL);
  
  GST_BUFFER_DATA (buf) = (gpointer)g_malloc (src->bytes_per_read);

  readbytes = read (src->fd,GST_BUFFER_DATA (buf),
                    src->bytes_per_read);

  if (readbytes == 0) {
    gst_element_set_eos (GST_ELEMENT (src));
    return NULL;
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  
  src->curoffset += readbytes;

  GST_DEBUG (0, "pushed buffer from soundcard of %ld bytes\n", readbytes);
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
    case ARG_FORMAT:
      src->format = g_value_get_int (value);
      break;
    case ARG_CHANNELS:
      src->channels = g_value_get_int (value);
      break;
    case ARG_FREQUENCY:
      src->frequency = g_value_get_int (value);
      break;
    case ARG_CUROFFSET:
      src->curoffset = g_value_get_int (value);
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
    case ARG_FORMAT:
      g_value_set_int (value, src->format);
      break;
    case ARG_CHANNELS:
      g_value_set_int (value, src->channels);
      break;
    case ARG_FREQUENCY:
      g_value_set_int (value, src->frequency);
      break;
    case ARG_CUROFFSET:
      g_value_set_ulong (value, src->curoffset);
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
  g_return_val_if_fail (GST_IS_OSSSRC (element), FALSE);
  GST_DEBUG (0, "osssrc: state change\n");
  /* if going down into NULL state, close the file if it's open */
  if (GST_STATE_PENDING (element) == GST_STATE_NULL) {
    if (GST_FLAG_IS_SET (element, GST_OSSSRC_OPEN))
      gst_osssrc_close_audio (GST_OSSSRC (element));
  /* otherwise (READY or higher) we need to open the sound card */
  } else {
    GST_DEBUG (0, "DEBUG: osssrc: ready or higher\n");

    if (!GST_FLAG_IS_SET (element, GST_OSSSRC_OPEN)) { 
      if (!gst_osssrc_open_audio (GST_OSSSRC (element)))
        return GST_STATE_FAILURE;
    }
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return GST_STATE_SUCCESS;
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
    gst_osssrc_sync_parms (src);
    GST_DEBUG (0,"opened audio: %s\n",src->device);
    
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

static void 
gst_osssrc_sync_parms (GstOssSrc *osssrc) 
{
  audio_buf_info ispace;
  gint frag;
  /* hack : ioctl on frequency seems to *change* the frequency
	maybe this is right, but at least for 44100 Khz, it
	gives unexpected results ! */

  guint frequency;

  g_return_if_fail (osssrc != NULL);
  g_return_if_fail (GST_IS_OSSSRC (osssrc));
  g_return_if_fail (osssrc->fd > 0);
 
  frequency = osssrc->frequency;

  frag = 0x7fff0006;

  ioctl(osssrc->fd, SNDCTL_DSP_SETFRAGMENT, &frag);
  ioctl(osssrc->fd, SNDCTL_DSP_RESET, 0);
 
  ioctl(osssrc->fd, SNDCTL_DSP_SETFMT, &osssrc->format);
  ioctl(osssrc->fd, SNDCTL_DSP_CHANNELS, &osssrc->channels);
#ifdef WEIRD_THING
  g_print ("DEBUG: do you want to see A Weird Thing (TM) ?\n");
  g_print ("DEBUG: then set osssrc to a 44100 frequency and check these numbers.\n");
  g_print ("DEBUG: osssrc frequency before ioctl: %d\n", osssrc->frequency);
#endif
  ioctl(osssrc->fd, SNDCTL_DSP_SPEED, &osssrc->frequency);
#ifdef WEIRD_THING
  g_print ("DEBUG: osssrc frequency after ioctl : %d\n", osssrc->frequency);
  g_print ("DEBUG: redoing ioctl using a temp variable, and resetting osssrc->frequency\n");
  osssrc->frequency = frequency;
  ioctl(osssrc->fd, SNDCTL_DSP_SPEED, &frequency);
#endif  
  ioctl(osssrc->fd, SNDCTL_DSP_GETISPACE, &ispace);
  ioctl(osssrc->fd, SNDCTL_DSP_GETBLKSIZE, &frag);
 
  g_print("setting sound card to %dKHz %d bit %s (%d bytes buffer, %d fragment)\n",
          osssrc->frequency, osssrc->format,
          (osssrc->channels == 2) ? "stereo" : "mono", ispace.bytes, frag);

  /* set caps on src pad */
  gst_pad_try_set_caps (osssrc->srcpad, gst_caps_new (
    "oss_src",
    "audio/raw",
    gst_props_new (
        "format",       GST_PROPS_STRING ("int"),
          "law",        GST_PROPS_INT (0),              //FIXME
          "endianness", GST_PROPS_INT (G_BYTE_ORDER),   //FIXME
          "signed",     GST_PROPS_BOOLEAN (TRUE),	//FIXME
          "width",      GST_PROPS_INT (osssrc->format),
          "depth",      GST_PROPS_INT (osssrc->format),
          "rate",       GST_PROPS_INT (osssrc->frequency),
          "channels",   GST_PROPS_INT (osssrc->channels),
          NULL
        )
      ));   
}

gboolean
gst_osssrc_factory_init (GstPlugin *plugin)
{
  GstElementFactory *factory;

  factory = gst_elementfactory_new ("osssrc", GST_TYPE_OSSSRC, &gst_osssrc_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory, GST_PADTEMPLATE_GET (osssrc_src_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

