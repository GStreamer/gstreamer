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
  ARG_BUFFERSIZE,
  ARG_FRAGMENT,
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
static const GstFormat* 	gst_osssrc_get_formats 	(GstPad *pad);
static gboolean 		gst_osssrc_convert 	(GstPad *pad, 
							 GstFormat src_format, gint64 src_value,
		                    			 GstFormat *dest_format, gint64 *dest_value);

static void 			gst_osssrc_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 			gst_osssrc_get_property	(GObject *object, guint prop_id,
							 GValue *value, GParamSpec *pspec);
static GstElementStateReturn 	gst_osssrc_change_state	(GstElement *element);

static const GstEventMask* 	gst_osssrc_get_event_masks (GstPad *pad);
static gboolean 		gst_osssrc_src_event 	(GstPad *pad, GstEvent *event);
static gboolean                 gst_osssrc_send_event 	(GstElement *element, GstEvent *event);
static const GstPadQueryType* 	gst_osssrc_get_query_types (GstPad *pad);
static gboolean 		gst_osssrc_src_query 	(GstPad *pad, GstPadQueryType type, 
							 GstFormat *format, gint64 *value);

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

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFERSIZE,
    g_param_spec_ulong ("buffersize","Buffer Size","The size of the buffers with samples",
                        0, G_MAXULONG, 0, G_PARAM_READWRITE));
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DEVICE,
    g_param_spec_string ("device", "device", "oss device (/dev/dspN usually)",
                         "default", G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAGMENT,
    g_param_spec_int ("fragment", "Fragment",
                      "The fragment as 0xMMMMSSSS (MMMM = total fragments, 2^SSSS = fragment size)",
                      0, G_MAXINT, 6, G_PARAM_READWRITE));
  
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
  gst_pad_set_convert_function (osssrc->srcpad, gst_osssrc_convert);
  gst_pad_set_formats_function (osssrc->srcpad, gst_osssrc_get_formats);
  gst_pad_set_event_function (osssrc->srcpad, gst_osssrc_src_event);
  gst_pad_set_event_mask_function (osssrc->srcpad, gst_osssrc_get_event_masks);
  gst_pad_set_query_function (osssrc->srcpad, gst_osssrc_src_query);
  gst_pad_set_query_type_function (osssrc->srcpad, gst_osssrc_get_query_types);


  gst_element_add_pad (GST_ELEMENT (osssrc), osssrc->srcpad);

  gst_osscommon_init (&osssrc->common);

  osssrc->buffersize = 4096;
  osssrc->curoffset = 0;
}

static GstPadConnectReturn 
gst_osssrc_srcconnect (GstPad *pad, GstCaps *caps)
{
  GstOssSrc *src;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  if (!gst_osscommon_parse_caps (&src->common, caps))
    return GST_PAD_CONNECT_REFUSED;

  if (!gst_osscommon_sync_parms (&src->common))
    return GST_PAD_CONNECT_REFUSED;

  return GST_PAD_CONNECT_OK;
}

static gboolean
gst_osssrc_negotiate (GstPad *pad)
{
  GstOssSrc *src;
  GstCaps *allowed;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  allowed = gst_pad_get_allowed_caps (pad);

  if (!gst_osscommon_merge_fixed_caps (&src->common, allowed))
    return FALSE;

  if (!gst_osscommon_sync_parms (&src->common))
    return FALSE;
    
  /* set caps on src pad */
  if (gst_pad_try_set_caps (src->srcpad, 
	GST_CAPS_NEW (
    	  "oss_src",
	  "audio/raw",
            "format",       GST_PROPS_STRING ("int"),
	      "law",        GST_PROPS_INT (src->common.law),
	      "endianness", GST_PROPS_INT (src->common.endianness),
	      "signed",     GST_PROPS_BOOLEAN (src->common.sign),
	      "width",      GST_PROPS_INT (src->common.width),
	      "depth",      GST_PROPS_INT (src->common.depth),
	      "rate",       GST_PROPS_INT (src->common.rate),
	      "channels",   GST_PROPS_INT (src->common.channels)
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

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "attempting to read something from the soundcard");

  if (src->need_eos) {
    src->need_eos = FALSE;
    return GST_BUFFER (gst_event_new (GST_EVENT_EOS));
  }
  
  buf = gst_buffer_new_and_alloc (src->buffersize);
  
  readbytes = read (src->common.fd,GST_BUFFER_DATA (buf),
                    src->buffersize);

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
  if (src->common.bps == 0) {
    gst_element_error (GST_ELEMENT (src), "no format negotiated");
    return NULL;
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = src->curoffset;
  GST_BUFFER_TIMESTAMP (buf) = src->curoffset * GST_SECOND / src->common.bps;

  src->curoffset += readbytes;

  GST_DEBUG (GST_CAT_PLUGIN_INFO, "pushed buffer from soundcard of %ld bytes, timestamp %lld", 
		  readbytes, GST_BUFFER_TIMESTAMP (buf));

  return buf;
}

static void 
gst_osssrc_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec) 
{
  GstOssSrc *src;

  src = GST_OSSSRC (object);

  switch (prop_id) {
    case ARG_BUFFERSIZE:
      src->buffersize = g_value_get_ulong (value);
      break;
    case ARG_DEVICE:
      g_free(src->common.device);
      src->common.device = g_strdup (g_value_get_string (value));
      break;
    case ARG_FRAGMENT:
      src->common.fragment = g_value_get_int (value);
      gst_osscommon_sync_parms (&src->common);
      break; 
    default:
      break;
  }
}

static void 
gst_osssrc_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec) 
{
  GstOssSrc *src;

  src = GST_OSSSRC (object);

  switch (prop_id) {
    case ARG_BUFFERSIZE:
      g_value_set_ulong (value, src->buffersize);
      break;
    case ARG_DEVICE:
      g_value_set_string (value, src->common.device);
      break;
    case ARG_FRAGMENT:
      g_value_set_int (value, src->common.fragment);
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
	gchar *error;
        if (!gst_osscommon_open_audio (&osssrc->common, GST_OSSCOMMON_READ, &error)) {
	  gst_element_error (GST_ELEMENT (osssrc), error);
	  g_free (error);
          return GST_STATE_FAILURE;
	}
        GST_FLAG_SET (osssrc, GST_OSSSRC_OPEN);
      }
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_OSSSRC_OPEN)) {
        gst_osscommon_close_audio (&osssrc->common);
        GST_FLAG_UNSET (osssrc, GST_OSSSRC_OPEN);
      }
      gst_osscommon_init (&osssrc->common);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);
  
  return GST_STATE_SUCCESS;
}

static const GstFormat*
gst_osssrc_get_formats (GstPad *pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,
    GST_FORMAT_UNITS,
    GST_FORMAT_BYTES,
    0
  };
  return formats;
}

static gboolean
gst_osssrc_convert (GstPad *pad, GstFormat src_format, gint64 src_value,
		                     GstFormat *dest_format, gint64 *dest_value)
{
  GstOssSrc *osssrc;

  osssrc = GST_OSSSRC (gst_pad_get_parent (pad));

  return gst_osscommon_convert (&osssrc->common, src_format, src_value,
                                dest_format, dest_value);
}

static const GstEventMask*
gst_osssrc_get_event_masks (GstPad *pad)
{
  static const GstEventMask gst_osssrc_src_event_masks[] = {
    { GST_EVENT_EOS, 0 },
    { GST_EVENT_SIZE, 0 },
    { 0, } 
  };
  return gst_osssrc_src_event_masks;
} 

static gboolean
gst_osssrc_src_event (GstPad *pad, GstEvent *event)
{
  GstOssSrc *osssrc;
  gboolean retval = FALSE;

  osssrc = GST_OSSSRC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      osssrc->need_eos = TRUE;
      retval = TRUE;
      break;
    case GST_EVENT_SIZE:
    {
      GstFormat format;
      gint64 value;

      format = GST_FORMAT_BYTES;
      
      /* convert to bytes */
      if (gst_osscommon_convert (&osssrc->common, 
			         GST_EVENT_SIZE_FORMAT (event), 
				 GST_EVENT_SIZE_VALUE (event),
                                 &format, &value)) 
      {
        osssrc->buffersize = GST_EVENT_SIZE_VALUE (event);
        g_object_notify (G_OBJECT (osssrc), "buffersize");
        retval = TRUE;
      }
    }
    default:
      break;
  }
  gst_event_unref (event);
  return retval;
}

static gboolean
gst_osssrc_send_event (GstElement *element,
		       GstEvent *event)
{
  GstOssSrc *osssrc = GST_OSSSRC (element);

  return gst_osssrc_src_event (osssrc->srcpad, event);
}

static const GstPadQueryType*
gst_osssrc_get_query_types (GstPad *pad)
{
  static const GstPadQueryType query_types[] = {
    GST_PAD_QUERY_POSITION,
    0,
  };
  return query_types;
} 

static gboolean
gst_osssrc_src_query (GstPad *pad, GstPadQueryType type, GstFormat *format, gint64 *value)
{
  gboolean res = FALSE;
  GstOssSrc *osssrc;
	      
  osssrc = GST_OSSSRC (gst_pad_get_parent (pad));
	        
  switch (type) {
    case GST_PAD_QUERY_POSITION:
      res = gst_osscommon_convert (&osssrc->common, 
		      		   GST_FORMAT_BYTES, osssrc->curoffset,
                                   format, value); 
      break;
    default:
      break;
  }
  return res;
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
