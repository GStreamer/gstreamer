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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/soundcard.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include <gstosssrc.h>
#include <gstosselement.h>
#include <gst/audio/audioclock.h>

/* elementfactory information */
static GstElementDetails gst_osssrc_details = GST_ELEMENT_DETAILS (
  "Audio Source (OSS)",
  "Source/Audio",
  "Read from the sound card",
  "Erik Walthinsen <omega@cse.ogi.edu>"
);


/* OssSrc signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BUFFERSIZE,
  ARG_FRAGMENT,
};

static GstStaticPadTemplate osssrc_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("audio/x-raw-int, "
      "endianness = (int) BYTE_ORDER, "
      "signed = (boolean) { TRUE, FALSE }, "
      "width = (int) { 8, 16 }, "
      "depth = (int) { 8, 16 }, "
      "rate = (int) [ 1000, 48000 ], "
      "channels = (int) [ 1, 2 ]"
  )
);

static void			gst_osssrc_base_init	(gpointer g_class);
static void 			gst_osssrc_class_init	(GstOssSrcClass *klass);
static void 			gst_osssrc_init		(GstOssSrc *osssrc);
static void 			gst_osssrc_dispose	(GObject *object);

static GstPadLinkReturn 	gst_osssrc_srcconnect 	(GstPad *pad, const GstCaps *caps);
static const GstFormat* 	gst_osssrc_get_formats 	(GstPad *pad);
static gboolean 		gst_osssrc_convert 	(GstPad *pad, 
							 GstFormat src_format, gint64 src_value,
		                    			 GstFormat *dest_format, gint64 *dest_value);

static void 			gst_osssrc_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void 			gst_osssrc_get_property	(GObject *object, guint prop_id,
							 GValue *value, GParamSpec *pspec);
static GstElementStateReturn 	gst_osssrc_change_state	(GstElement *element);

static void		 	gst_osssrc_set_clock	(GstElement *element, GstClock *clock);
static GstClock* 		gst_osssrc_get_clock	(GstElement *element);
static GstClockTime 		gst_osssrc_get_time 	(GstClock *clock, gpointer data);

static const GstEventMask* 	gst_osssrc_get_event_masks (GstPad *pad);
static gboolean 		gst_osssrc_src_event 	(GstPad *pad, GstEvent *event);
static gboolean                 gst_osssrc_send_event 	(GstElement *element, GstEvent *event);
static const GstQueryType* 	gst_osssrc_get_query_types (GstPad *pad);
static gboolean 		gst_osssrc_src_query 	(GstPad *pad, GstQueryType type, 
							 GstFormat *format, gint64 *value);

static GstData *		gst_osssrc_get		(GstPad *pad);

static GstElementClass *parent_class = NULL;
/*static guint gst_osssrc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_osssrc_get_type (void) 
{
  static GType osssrc_type = 0;

  if (!osssrc_type) {
    static const GTypeInfo osssrc_info = {
      sizeof(GstOssSrcClass),
      gst_osssrc_base_init,
      NULL,
      (GClassInitFunc)gst_osssrc_class_init,
      NULL,
      NULL,
      sizeof(GstOssSrc),
      0,
      (GInstanceInitFunc)gst_osssrc_init,
    };
    osssrc_type = g_type_register_static (GST_TYPE_OSSELEMENT, "GstOssSrc", &osssrc_info, 0);
  }
  return osssrc_type;
}

static void
gst_osssrc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_set_details (element_class, &gst_osssrc_details);
  gst_element_class_add_pad_template (element_class, gst_static_pad_template_get (&osssrc_src_factory));
}
static void
gst_osssrc_class_init (GstOssSrcClass *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref (GST_TYPE_OSSELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFFERSIZE,
    g_param_spec_ulong ("buffersize","Buffer Size","The size of the buffers with samples",
                        0, G_MAXULONG, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAGMENT,
    g_param_spec_int ("fragment", "Fragment",
                      "The fragment as 0xMMMMSSSS (MMMM = total fragments, 2^SSSS = fragment size)",
                      0, G_MAXINT, 6, G_PARAM_READWRITE));
  
  gobject_class->set_property = gst_osssrc_set_property;
  gobject_class->get_property = gst_osssrc_get_property;
  gobject_class->dispose      = gst_osssrc_dispose;

  gstelement_class->change_state = gst_osssrc_change_state;
  gstelement_class->send_event = gst_osssrc_send_event;

  gstelement_class->set_clock = gst_osssrc_set_clock;
  gstelement_class->get_clock = gst_osssrc_get_clock;
}

static void 
gst_osssrc_init (GstOssSrc *osssrc) 
{
  osssrc->srcpad = gst_pad_new_from_template (
		  gst_static_pad_template_get (&osssrc_src_factory), "src");
  gst_pad_set_get_function (osssrc->srcpad, gst_osssrc_get);
  gst_pad_set_link_function (osssrc->srcpad, gst_osssrc_srcconnect);
  gst_pad_set_convert_function (osssrc->srcpad, gst_osssrc_convert);
  gst_pad_set_formats_function (osssrc->srcpad, gst_osssrc_get_formats);
  gst_pad_set_event_function (osssrc->srcpad, gst_osssrc_src_event);
  gst_pad_set_event_mask_function (osssrc->srcpad, gst_osssrc_get_event_masks);
  gst_pad_set_query_function (osssrc->srcpad, gst_osssrc_src_query);
  gst_pad_set_query_type_function (osssrc->srcpad, gst_osssrc_get_query_types);


  gst_element_add_pad (GST_ELEMENT (osssrc), osssrc->srcpad);

  osssrc->buffersize = 4096;
  osssrc->curoffset = 0;

  osssrc->provided_clock = gst_audio_clock_new ("ossclock", gst_osssrc_get_time, osssrc);
  gst_object_set_parent (GST_OBJECT (osssrc->provided_clock), GST_OBJECT (osssrc));
  
  osssrc->clock = NULL;
}

static void
gst_osssrc_dispose (GObject *object)
{
  GstOssSrc *osssrc = (GstOssSrc *) object;

  gst_object_unparent (GST_OBJECT (osssrc->provided_clock));

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static GstPadLinkReturn 
gst_osssrc_srcconnect (GstPad *pad, const GstCaps *caps)
{
  GstOssSrc *src;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  if (!gst_osselement_parse_caps (GST_OSSELEMENT (src), caps))
    return GST_PAD_LINK_REFUSED;

  if (!gst_osselement_sync_parms (GST_OSSELEMENT (src)))
    return GST_PAD_LINK_REFUSED;

  return GST_PAD_LINK_OK;
}

static gboolean
gst_osssrc_negotiate (GstPad *pad)
{
  GstOssSrc *src;
  GstCaps *allowed;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  allowed = gst_pad_get_allowed_caps (pad);

  if (!gst_osselement_merge_fixed_caps (GST_OSSELEMENT (src), allowed))
    return FALSE;

  if (!gst_osselement_sync_parms (GST_OSSELEMENT (src)))
    return FALSE;
    
  /* set caps on src pad */
  if (gst_pad_try_set_caps (src->srcpad, 
	gst_caps_new_simple("audio/x-raw-int",
	    "endianness", G_TYPE_INT, GST_OSSELEMENT (src)->endianness,
	    "signed",     G_TYPE_BOOLEAN, GST_OSSELEMENT (src)->sign,
	    "width",      G_TYPE_INT, GST_OSSELEMENT (src)->width,
	    "depth",      G_TYPE_INT, GST_OSSELEMENT (src)->depth,
	    "rate",       G_TYPE_INT, GST_OSSELEMENT (src)->rate,
	    "channels",   G_TYPE_INT, GST_OSSELEMENT (src)->channels,
	    NULL)) <= 0) {
    return FALSE;
  }
  return TRUE;
}

static GstClockTime 
gst_osssrc_get_time (GstClock *clock, gpointer data) 
{
  GstOssSrc *osssrc = GST_OSSSRC (data);
  audio_buf_info info;

  if (!GST_OSSELEMENT (osssrc)->bps)
    return 0;

  if (ioctl(GST_OSSELEMENT (osssrc)->fd, SNDCTL_DSP_GETISPACE, &info) < 0)
    return 0;

  return (osssrc->curoffset + info.bytes) * GST_SECOND / GST_OSSELEMENT (osssrc)->bps;
}

static GstClock*
gst_osssrc_get_clock (GstElement *element)
{
  GstOssSrc *osssrc;
	    
  osssrc = GST_OSSSRC (element);

  return GST_CLOCK (osssrc->provided_clock);
}

static void
gst_osssrc_set_clock (GstElement *element, GstClock *clock)
{
  GstOssSrc *osssrc;
  
  osssrc = GST_OSSSRC (element);

  osssrc->clock = clock;
}
	
static GstData *
gst_osssrc_get (GstPad *pad)
{
  GstOssSrc *src;
  GstBuffer *buf;
  glong readbytes;

  src = GST_OSSSRC(gst_pad_get_parent (pad));

  GST_DEBUG ("attempting to read something from the soundcard");

  if (src->need_eos) {
    src->need_eos = FALSE;
    return GST_DATA (gst_event_new (GST_EVENT_EOS));
  }
  
  buf = gst_buffer_new_and_alloc (src->buffersize);
  
  if (!GST_PAD_CAPS (pad)) {
    /* nothing was negotiated, we can decide on a format */
    if (!gst_osssrc_negotiate (pad)) {
      gst_buffer_unref (buf);
      gst_element_error (src, CORE, NEGOTIATION, NULL, NULL);
      return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
    }
  }
  if (GST_OSSELEMENT (src)->bps == 0) {
    gst_buffer_unref (buf);
    gst_element_error (src, CORE, NEGOTIATION, NULL,
                       ("format wasn't negotiated before chain function"));
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }

  readbytes = read (GST_OSSELEMENT (src)->fd,GST_BUFFER_DATA (buf),
                    src->buffersize);
  if (readbytes < 0) {
    gst_buffer_unref (buf);
    gst_element_error (src, RESOURCE, READ, NULL, GST_ERROR_SYSTEM);
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }

  if (readbytes == 0) {
    gst_buffer_unref (buf);
    gst_element_set_eos (GST_ELEMENT (src));
    return GST_DATA (gst_event_new (GST_EVENT_INTERRUPT));
  }

  GST_BUFFER_SIZE (buf) = readbytes;
  GST_BUFFER_OFFSET (buf) = src->curoffset;

  /* FIXME: we are falsely assuming that we are the master clock here */
  GST_BUFFER_TIMESTAMP (buf) = src->curoffset * GST_SECOND / GST_OSSELEMENT (src)->bps;
  GST_BUFFER_DURATION (buf) = (GST_SECOND * GST_BUFFER_SIZE (buf)) /
                              (GST_OSSELEMENT (src)->bps * GST_OSSELEMENT (src)->rate);

  src->curoffset += readbytes;

  GST_DEBUG ("pushed buffer from soundcard of %ld bytes, timestamp %" G_GINT64_FORMAT, 
		  readbytes, GST_BUFFER_TIMESTAMP (buf));

  return GST_DATA (buf);
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
    case ARG_FRAGMENT:
      GST_OSSELEMENT (src)->fragment = g_value_get_int (value);
      gst_osselement_sync_parms (GST_OSSELEMENT (src));
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
    case ARG_FRAGMENT:
      g_value_set_int (value, GST_OSSELEMENT (src)->fragment);
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
  
  GST_DEBUG ("osssrc: state change");

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      osssrc->curoffset = 0;
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssrc->provided_clock), TRUE);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      gst_audio_clock_set_active (GST_AUDIO_CLOCK (osssrc->provided_clock), FALSE);
      break;
    case GST_STATE_PAUSED_TO_READY:
      if (GST_FLAG_IS_SET (element, GST_OSSSRC_OPEN))
        ioctl (GST_OSSELEMENT (osssrc)->fd, SNDCTL_DSP_RESET, 0);
      break;
    default:
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
    GST_FORMAT_DEFAULT,
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

  return gst_osselement_convert (GST_OSSELEMENT (osssrc), src_format, src_value,
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
      if (gst_osselement_convert (GST_OSSELEMENT (osssrc), 
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

static const GstQueryType*
gst_osssrc_get_query_types (GstPad *pad)
{
  static const GstQueryType query_types[] = {
    GST_QUERY_POSITION,
    0,
  };
  return query_types;
} 

static gboolean
gst_osssrc_src_query (GstPad *pad, GstQueryType type, GstFormat *format, gint64 *value)
{
  gboolean res = FALSE;
  GstOssSrc *osssrc;
	      
  osssrc = GST_OSSSRC (gst_pad_get_parent (pad));
	        
  switch (type) {
    case GST_QUERY_POSITION:
      res = gst_osselement_convert (GST_OSSELEMENT (osssrc), 
		      		    GST_FORMAT_BYTES, osssrc->curoffset,
                                    format, value); 
      break;
    default:
      break;
  }
  return res;
} 
