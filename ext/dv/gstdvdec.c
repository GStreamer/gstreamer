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

/* First, include the header file for the plugin, to bring in the
 * object definition and other useful things.
 */
#include "gstdvdec.h"

#define NTSC_HEIGHT 480
#define NTSC_BUFFER 120000
#define PAL_HEIGHT 576
#define PAL_BUFFER 144000

/* The ElementDetails structure gives a human-readable description
 * of the plugin, as well as author and version data.
 */
static GstElementDetails dvdec_details = {
  "DV (smpte314) decoder plugin",
  "Decoder/DV",
  "Uses libdv to decode DV video (libdv.sourceforge.net)",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 2001-2002",
};


/* These are the signals that this element can fire.  They are zero-
 * based because the numbers themselves are private to the object.
 * LAST_SIGNAL is used for initialization of the signal array.
 */
enum {
  ASDF,
  /* FILL ME */
  LAST_SIGNAL
};

/* Arguments are identified the same way, but cannot be zero, so you
 * must leave the ARG_0 entry in as a placeholder.
 */
enum {
  ARG_0,
  /* FILL ME */
};

/* The PadFactory structures describe what pads the element has or
 * can have.  They can be quite complex, but for this dvdec plugin
 * they are rather simple.
 */
GST_PAD_TEMPLATE_FACTORY (sink_temp,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "dv_dec_sink",
    "video/dv",
    "format",   GST_PROPS_LIST (
	          GST_PROPS_STRING ("PAL"),
	          GST_PROPS_STRING ("NTSC")
	       )
  )
)


GST_PAD_TEMPLATE_FACTORY (video_src_temp,
  "video",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "dv_dec_src",
    "video/raw",
    "format",   	GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','U','Y','2')),
    "width",    	GST_PROPS_INT (720),
    "height",  		GST_PROPS_INT_RANGE (NTSC_HEIGHT, PAL_HEIGHT)
  ),
  GST_CAPS_NEW (
    "dv_dec_src",
    "video/raw",
    "format", 		GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
    "bpp", 		GST_PROPS_INT(24),
    "depth", 		GST_PROPS_INT(24),
    "endianness", 	GST_PROPS_INT (G_LITTLE_ENDIAN),
    "red_mask", 	GST_PROPS_INT(0x0000ff),
    "green_mask", 	GST_PROPS_INT(0x00ff00),
    "blue_mask", 	GST_PROPS_INT(0xff0000),
    "width",     	GST_PROPS_INT (720),
    "height",    	GST_PROPS_INT_RANGE (NTSC_HEIGHT, PAL_HEIGHT)
  ),
  GST_CAPS_NEW (
    "dv_dec_src",
    "video/raw",
    "format", 		GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
    "bpp", 		GST_PROPS_INT(32),
    "depth", 		GST_PROPS_INT(32),
    "endianness", 	GST_PROPS_INT (G_LITTLE_ENDIAN),
    "red_mask", 	GST_PROPS_INT(0x00ff0000),
    "green_mask", 	GST_PROPS_INT(0x0000ff00),
    "blue_mask", 	GST_PROPS_INT(0x000000ff),
    "width",     	GST_PROPS_INT (720),
    "height",    	GST_PROPS_INT_RANGE (NTSC_HEIGHT, PAL_HEIGHT)
  )
)

GST_PAD_TEMPLATE_FACTORY ( audio_src_temp,
  "audio",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "arts_sample",
    "audio/raw",
    "format",   	GST_PROPS_STRING ("int"),
    "law",      	GST_PROPS_INT (0),
    "depth",   		GST_PROPS_INT (16),
    "width",    	GST_PROPS_INT (16),
    "signed",   	GST_PROPS_BOOLEAN (TRUE),
    "channels", 	GST_PROPS_INT (2),
    "endianness", 	GST_PROPS_INT (G_LITTLE_ENDIAN)
  )
)

/* typefind stuff */
static GstCaps*
dv_type_find (GstBuffer *buf, gpointer private)
{
  gulong head = GULONG_FROM_BE(*((gulong *)GST_BUFFER_DATA(buf)));
  GstCaps *new = NULL;

  /* check for DIF  and DV flag */
  if ((head & 0xffffff00) == 0x1f070000 && !(GST_BUFFER_DATA(buf)[4] & 0x01)) {
    gchar *format;

    if ((head & 0x000000ff) & 0x80)
      format = "PAL";
    else
      format = "NTSC";
    
    new = GST_CAPS_NEW ("dv_type_find",
                        "video/dv",
                          "format",   GST_PROPS_STRING (format)
		       );
  }
  return new;
}

static GstTypeDefinition dv_definition = {
  "dv_video/dv", "video/dv", ".dv", dv_type_find 
};

/* A number of functon prototypes are given so we can refer to them later. */
static void		gst_dvdec_class_init		(GstDVDecClass *klass);
static void		gst_dvdec_init			(GstDVDec *dvdec);

static gboolean 	gst_dvdec_src_query 		(GstPad *pad, GstPadQueryType type,
                    					 GstFormat *format, gint64 *value);
static gboolean 	gst_dvdec_sink_convert 		(GstPad *pad, GstFormat src_format, gint64 src_value,
                        				 GstFormat *dest_format, gint64 *dest_value);
static gboolean 	gst_dvdec_src_convert 		(GstPad *pad, GstFormat src_format, gint64 src_value,
                      					 GstFormat *dest_format, gint64 *dest_value);

static gboolean 	gst_dvdec_handle_src_event 	(GstPad *pad, GstEvent *event);

static void		gst_dvdec_loop			(GstElement *element);

static GstElementStateReturn 	
			gst_dvdec_change_state 		(GstElement *element);

static void		gst_dvdec_set_property		(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_dvdec_get_property		(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);

/* The parent class pointer needs to be kept around for some object
 * operations.
 */
static GstElementClass *parent_class = NULL;

/* This array holds the ids of the signals registered for this object.
 * The array indexes are based on the enum up above.
 */
/*static guint gst_dvdec_signals[LAST_SIGNAL] = { 0 }; */

/* This function is used to register and subsequently return the type
 * identifier for this object class.  On first invocation, it will
 * register the type, providing the name of the class, struct sizes,
 * and pointers to the various functions that define the class.
 */
GType
gst_dvdec_get_type (void)
{
  static GType dvdec_type = 0;

  if (!dvdec_type) {
    static const GTypeInfo dvdec_info = {
      sizeof (GstDVDecClass),      
      NULL,      
      NULL,
      (GClassInitFunc) gst_dvdec_class_init,
      NULL,
      NULL,
      sizeof (GstDVDec),
      0,
      (GInstanceInitFunc) gst_dvdec_init,
    };
    dvdec_type = g_type_register_static (GST_TYPE_ELEMENT, "GstDVDec", &dvdec_info, 0);
  }
  return dvdec_type;
}

/* In order to create an instance of an object, the class must be
 * initialized by this function.  GObject will take care of running
 * it, based on the pointer to the function provided above.
 */
static void
gst_dvdec_class_init (GstDVDecClass *klass)
{
  /* Class pointers are needed to supply pointers to the private
   * implementations of parent class methods.
   */
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  /* Since the dvdec class contains the parent classes, you can simply
   * cast the pointer to get access to the parent classes.
   */
  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;

  /* The parent class is needed for class method overrides. */
  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_dvdec_set_property;
  gobject_class->get_property = gst_dvdec_get_property;

  gstelement_class->change_state = gst_dvdec_change_state;

  /* table initialization, only do once */
  dv_init(0, 0);
}

/* This function is responsible for initializing a specific instance of
 * the plugin.
 */
static void
gst_dvdec_init(GstDVDec *dvdec)
{
  gint i;

  dvdec->sinkpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (sink_temp), "sink");
  gst_element_add_pad (GST_ELEMENT (dvdec), dvdec->sinkpad);
  gst_pad_set_query_function (dvdec->sinkpad, NULL);
  gst_pad_set_convert_function (dvdec->sinkpad, gst_dvdec_sink_convert);

  dvdec->videosrcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET (video_src_temp), "video");
  gst_element_add_pad (GST_ELEMENT (dvdec), dvdec->videosrcpad);
  gst_pad_set_query_function (dvdec->videosrcpad, gst_dvdec_src_query);
  gst_pad_set_event_function (dvdec->videosrcpad, gst_dvdec_handle_src_event);
  gst_pad_set_convert_function (dvdec->videosrcpad, gst_dvdec_src_convert);

  dvdec->audiosrcpad = gst_pad_new_from_template (GST_PAD_TEMPLATE_GET(audio_src_temp), "audio");
  gst_element_add_pad(GST_ELEMENT(dvdec),dvdec->audiosrcpad);
  gst_pad_set_query_function (dvdec->audiosrcpad, gst_dvdec_src_query);
  gst_pad_set_event_function (dvdec->audiosrcpad, gst_dvdec_handle_src_event);
  gst_pad_set_convert_function (dvdec->audiosrcpad, gst_dvdec_src_convert);

  gst_element_set_loop_function (GST_ELEMENT (dvdec), gst_dvdec_loop);

  dvdec->decoder = dv_decoder_new (0, 0, 0);
  dvdec->decoder->quality = DV_QUALITY_BEST;
  dvdec->pool = NULL;
  dvdec->length = 0;
  dvdec->next_ts = 0LL;
  dvdec->need_discont = FALSE;
  dvdec->framerate = 0;
  dvdec->height = 0;

  for (i = 0; i <4; i++) {
    dvdec->audio_buffers[i] = (gint16 *)g_malloc (DV_AUDIO_MAX_SAMPLES * sizeof (gint16));
  }
}

static gboolean
gst_dvdec_src_convert (GstPad *pad, GstFormat src_format, gint64 src_value,
                       GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstDVDec *dvdec;
  gint scale = 1;
        
  dvdec = GST_DVDEC (gst_pad_get_parent (pad));

  if (dvdec->length == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
	  if (pad == dvdec->videosrcpad)
	    scale = 720 * dvdec->height * dvdec->bpp;
	  else if (pad == dvdec->audiosrcpad)
	    scale = dvdec->decoder->audio->num_channels * 2;
	  /* fallthrough */
        case GST_FORMAT_UNITS:
	  if (pad == dvdec->videosrcpad)
	    *dest_value = src_value * dvdec->framerate * scale / GST_SECOND;
	  else if (pad == dvdec->audiosrcpad)
	    *dest_value = src_value * dvdec->decoder->audio->frequency * scale / GST_SECOND;
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
gst_dvdec_sink_convert (GstPad *pad, GstFormat src_format, gint64 src_value,
                        GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstDVDec *dvdec;
        
  dvdec = GST_DVDEC (gst_pad_get_parent (pad));

  if (dvdec->length == 0)
    return FALSE;

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
          *dest_value = src_value * GST_SECOND / (dvdec->length * dvdec->framerate); 
          break;
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
	{
          guint64 frame;
          /* calculate the frame */
          frame = src_value * dvdec->framerate / GST_SECOND;
          /* calculate the offset */
          *dest_value = frame * dvdec->length;
          break;
	}
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
gst_dvdec_src_query (GstPad *pad, GstPadQueryType type,
                     GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstDVDec *dvdec;

  dvdec = GST_DVDEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
        default:
	{
          guint64 len;
	  GstFormat tmp_format;

	  len = gst_bytestream_length (dvdec->bs);
	  tmp_format = GST_FORMAT_TIME;
	  if (len == -1 || !gst_pad_convert (dvdec->sinkpad, GST_FORMAT_BYTES, len, &tmp_format, value)) {
	    return FALSE;
	  }
	  if (!gst_pad_convert (pad, GST_FORMAT_TIME, *value, format, value)) {
	    return FALSE;
	  }
          break;
	}
      }
      break;
    case GST_PAD_QUERY_POSITION:
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
        default:
	  res = gst_pad_convert (pad, GST_FORMAT_TIME, dvdec->next_ts, format, value);
          break;
      }
      break;
    default:
      res = FALSE;
      break;
  }
  return res;
} 

static gboolean
gst_dvdec_handle_sink_event (GstDVDec *dvdec)
{
  guint32 remaining;
  GstEvent *event;
  GstEventType type;
	        
  gst_bytestream_get_status (dvdec->bs, &remaining, &event);
		  
  type = event? GST_EVENT_TYPE (event) : GST_EVENT_UNKNOWN;
		      
  switch (type) {
    case GST_EVENT_EOS:
      gst_pad_event_default (dvdec->sinkpad, event);
      break;
    case GST_EVENT_FLUSH:
      break;
    case GST_EVENT_DISCONTINUOUS:
    {
      gint i;
      gboolean found = FALSE;
      GstFormat format;

      format = GST_FORMAT_TIME;
      /* try to get a timestamp from the discont formats */
      for (i = 0; i < GST_EVENT_DISCONT_OFFSET_LEN(event); i++) {
	if (gst_pad_convert (dvdec->sinkpad, GST_EVENT_DISCONT_OFFSET(event,i).format, GST_EVENT_DISCONT_OFFSET(event,i).value,
				&format, &dvdec->next_ts)) 
	{
          found = TRUE;
	  break;
	}
      }
      /* assume 0 then */
      if (!found) {
	dvdec->next_ts = 0LL;
      }
      dvdec->need_discont = TRUE;
      break;
    }
    default:
      g_warning ("unhandled event %d\n", type);
      break;
  }
  gst_event_free (event);
  return TRUE;
}

static gboolean
gst_dvdec_handle_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstDVDec *dvdec;

  dvdec = GST_DVDEC (gst_pad_get_parent (pad));
 
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
       gint64 position;
       GstFormat format;

       /* first bring the format to time */
       format = GST_FORMAT_TIME;
       if (!gst_pad_convert (pad, GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event),
		        &format, &position)) {
	 /* could not convert seek format to byte offset */
	 res = FALSE;
	 break;
       }
       /* then try to figure out the byteoffset for this time */
       format = GST_FORMAT_BYTES;
       if (!gst_pad_convert (dvdec->sinkpad, GST_FORMAT_TIME, position,
		        &format, &position)) {
	 /* could not convert seek format to byte offset */
	 res = FALSE;
	 break;
       }
       /* seek to offset */
       if (!gst_bytestream_seek (dvdec->bs, position, GST_SEEK_METHOD_SET)) {
	 res = FALSE;
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
gst_dvdec_loop (GstElement *element)
{   
  GstDVDec *dvdec;
  GstBuffer *buf, *outbuf;
  guint8 *inframe;
  gint height;
  guint32 length, got_bytes;
  GstFormat format;
  guint64 ts;

  dvdec = GST_DVDEC (element);

  /* first read enough bytes to parse the header */
  got_bytes = gst_bytestream_peek_bytes (dvdec->bs, &inframe, header_size);
  if (got_bytes < header_size) {
    gst_dvdec_handle_sink_event (dvdec);
    return;
  }
  dv_parse_header (dvdec->decoder, inframe);
  /* after parsing the header we know the size of the data */
  dvdec->PAL = dv_system_50_fields (dvdec->decoder);

  dvdec->framerate = (dvdec->PAL ? 25 : 30);
  dvdec->height = height = (dvdec->PAL ? PAL_HEIGHT : NTSC_HEIGHT);
  length = (dvdec->PAL ? PAL_BUFFER : NTSC_BUFFER);

  if (length != dvdec->length) {
    dvdec->length = length;
    gst_bytestream_size_hint (dvdec->bs, length);
  }

  /* then read the read data */
  got_bytes = gst_bytestream_read (dvdec->bs, &buf, length);
  if (got_bytes < length) {
    gst_dvdec_handle_sink_event (dvdec);
    return;
  }

  /* if we did not negotiate yet, do it now */
  if (!GST_PAD_CAPS (dvdec->videosrcpad)) {
    GstCaps *allowed;
    GstCaps *trylist;
    
    /* we what we are allowed to do */
    allowed = gst_pad_get_allowed_caps (dvdec->videosrcpad);

    /* try to fix our height */
    trylist = gst_caps_intersect (allowed,
				    GST_CAPS_NEW (
				      "dvdec_negotiate",
				      "video/raw",
				        "height",  	GST_PROPS_INT (height)
				    ));
    
    /* prepare for looping */
    trylist = gst_caps_normalize (trylist);

    while (trylist) {
      GstCaps *to_try = gst_caps_copy_1 (trylist);

      /* try each format */
      if (gst_pad_try_set_caps (dvdec->videosrcpad, to_try)) {
	guint32 fourcc;

	/* it worked, try to find what it was again */
	gst_caps_get_fourcc_int (to_try, "format", &fourcc);

	if (fourcc == GST_STR_FOURCC ("RGB ")) {
          gint bpp;

	  gst_caps_get_int (to_try, "bpp", &bpp);
	  if (bpp == 24) {
            dvdec->space = e_dv_color_rgb;
            dvdec->bpp = 3;
	  }
	  else {
            dvdec->space = e_dv_color_bgr0;
            dvdec->bpp = 4;
	  }
	}
	else {
          dvdec->space = e_dv_color_yuv;
          dvdec->bpp = 2;
	}
	break;
      }
      trylist = trylist->next;
    }
    /* oops list exhausted an nothing was found... */
    if (!trylist) {
      gst_element_error (element, "could not negotiate");
      return;
    }
  }

  format = GST_FORMAT_TIME;
  gst_pad_query (dvdec->videosrcpad, GST_PAD_QUERY_POSITION, &format, &ts);

  if (GST_PAD_IS_CONNECTED (dvdec->audiosrcpad)) {
    gint16 *a_ptr;
    gint i, j;

    dv_decode_full_audio (dvdec->decoder, GST_BUFFER_DATA (buf), dvdec->audio_buffers);

    /* if we did not negotiate yet, do it now */
    if (!GST_PAD_CAPS (dvdec->audiosrcpad)) {
      gst_pad_try_set_caps (dvdec->audiosrcpad,
		          GST_CAPS_NEW (
				  "dvdec_audio_caps",
				  "audio/raw",
    				    "format",   	GST_PROPS_STRING ("int"),
    				    "rate",   		GST_PROPS_INT (dvdec->decoder->audio->frequency),
				    "law",      	GST_PROPS_INT (0),
				    "depth",   		GST_PROPS_INT (16),
				    "width",    	GST_PROPS_INT (16),
				    "signed",   	GST_PROPS_BOOLEAN (TRUE),
				    "channels", 	GST_PROPS_INT (dvdec->decoder->audio->num_channels),
				    "endianness", 	GST_PROPS_INT (G_LITTLE_ENDIAN)
  			  ));
    }

    outbuf = gst_buffer_new ();
    GST_BUFFER_SIZE (outbuf) = dvdec->decoder->audio->samples_this_frame * sizeof (gint16) * dvdec->decoder->audio->num_channels;
    GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));

    a_ptr = (gint16 *) GST_BUFFER_DATA (outbuf);

    for (i = 0; i < dvdec->decoder->audio->samples_this_frame; i++) {
      for (j = 0; j < dvdec->decoder->audio->num_channels; j++) {
        *(a_ptr++) = dvdec->audio_buffers[j][i];
      }
    }
    GST_BUFFER_TIMESTAMP (outbuf) = ts;

    if (dvdec->need_discont) {
      GstEvent *discont;

      discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, ts, NULL);
      gst_pad_push (dvdec->audiosrcpad, GST_BUFFER (discont));
    }

    gst_pad_push (dvdec->audiosrcpad, outbuf);
  }

  if (GST_PAD_IS_CONNECTED (dvdec->videosrcpad)) {
    guint8 *outframe;
    guint8 *outframe_ptrs[3];
    gint outframe_pitches[3];

    /* try to grab a pool */
    if (!dvdec->pool) {
      dvdec->pool = gst_pad_get_bufferpool (dvdec->videosrcpad);
    }

    outbuf = NULL;
    /* try to get a buffer from the pool if we have one */
    if (dvdec->pool) {
      outbuf = gst_buffer_new_from_pool (dvdec->pool, 0, 0);
    }
    /* no buffer from pool, allocate one ourselves */
    if (!outbuf) {
      outbuf = gst_buffer_new ();
    
      GST_BUFFER_SIZE (outbuf) = (720 * height) * dvdec->bpp;
      GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
    }
    
    outframe = GST_BUFFER_DATA (outbuf);

    outframe_ptrs[0] = outframe;
    outframe_pitches[0] = 720 * dvdec->bpp;

    /* the rest only matters for YUY2 */
    if (dvdec->bpp < 3) {
      outframe_ptrs[1] = outframe_ptrs[0] + 720 * height;
      outframe_ptrs[2] = outframe_ptrs[1] + 360 * height;
  
      outframe_pitches[1] = height / 2;
      outframe_pitches[2] = outframe_pitches[1];
    }

    dv_decode_full_frame (dvdec->decoder, GST_BUFFER_DATA (buf), 
		          dvdec->space, outframe_ptrs, outframe_pitches);

    GST_BUFFER_TIMESTAMP (outbuf) = ts;

    if (dvdec->need_discont) {
      GstEvent *discont;

      discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, ts, NULL);
      gst_pad_push (dvdec->videosrcpad, GST_BUFFER (discont));
    }

    gst_pad_push (dvdec->videosrcpad, outbuf);
  }

  /* FIXME this is inaccurate for NTSC */
  dvdec->next_ts += GST_SECOND / dvdec->framerate;

  if (dvdec->need_discont) {
    dvdec->need_discont = FALSE;
  }

  gst_buffer_unref (buf);
}

static GstElementStateReturn
gst_dvdec_change_state (GstElement *element)
{
  GstDVDec *dvdec = GST_DVDEC (element);
	    
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
      dvdec->bs = gst_bytestream_new (dvdec->sinkpad);
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      dvdec->pool = NULL;
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_bytestream_destroy (dvdec->bs);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }
	      
  parent_class->change_state (element);
	        
  return GST_STATE_SUCCESS;
}

/* Arguments are part of the Gtk+ object system, and these functions
 * enable the element to respond to various arguments.
 */
static void
gst_dvdec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDVDec *dvdec;

  /* It's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DVDEC(object));

  /* Get a pointer of the right type. */
  dvdec = GST_DVDEC(object);

  /* Check the argument id to see which argument we're setting. */
  switch (prop_id) {
    default:
      break;
  }
}

/* The set function is simply the inverse of the get fuction. */
static void
gst_dvdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDVDec *dvdec;

  /* It's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DVDEC(object));
  dvdec = GST_DVDEC(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* This is the entry into the plugin itself.  When the plugin loads,
 * this function is called to register everything that the plugin provides.
 */
static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  if (!gst_library_load ("gstbytestream")) {
    gst_info("dvdec: could not load support library: 'gstbytestream'\n");
    return FALSE;
  }

  /* We need to create an ElementFactory for each element we provide.
   * This consists of the name of the element, the GType identifier,
   * and a pointer to the details structure at the top of the file.
   */
  factory = gst_element_factory_new("dvdec", GST_TYPE_DVDEC, &dvdec_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);
  /* The pad templates can be easily generated from the factories above,
   * and then added to the list of padtemplates for the elementfactory.
   * Note that the generated padtemplates are stored in static global
   * variables, for the gst_dvdec_init function to use later on.
   */
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET(sink_temp));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET(video_src_temp));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET(audio_src_temp));

  /* The very last thing is to register the elementfactory with the plugin. */
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  type = gst_type_factory_new (&dv_definition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "dvdec",
  plugin_init
};

