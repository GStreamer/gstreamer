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

/*#define GST_DEBUG_ENABLED */
#include <gstmpegaudioparse.h>


/* elementfactory information */
static GstElementDetails mp3parse_details = {
  "MP3 Parser",
  "Filter/Parser/Audio",
  "Parses and frames MP3 audio streams, provides seek",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>",
  "(C) 1999",
};

static GstPadTemplate*
mp3_src_factory (void)
{
  return
   gst_padtemplate_new (
  	"src",
  	GST_PAD_SRC,
  	GST_PAD_ALWAYS,
  	gst_caps_new (
  	  "mp3parse_src",
    	  "audio/mp3",
	  /*
	  gst_props_new (
    	    "layer",   GST_PROPS_INT_RANGE (1, 3),
    	    "bitrate", GST_PROPS_INT_RANGE (8, 320),
    	    "framed",  GST_PROPS_BOOLEAN (TRUE),
	    */
	    NULL),
	NULL);
}

static GstPadTemplate*
mp3_sink_factory (void) 
{
  return
   gst_padtemplate_new (
  	"sink",
  	GST_PAD_SINK,
  	GST_PAD_ALWAYS,
  	gst_caps_new (
  	  "mp3parse_sink",
    	  "audio/mp3",
	  NULL),
	NULL);
};

/* GstMPEGAudioParse signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SKIP,
  ARG_BIT_RATE,
  /* FILL ME */
};

static GstPadTemplate *sink_temp, *src_temp;

static void	gst_mp3parse_class_init		(GstMPEGAudioParseClass *klass);
static void	gst_mp3parse_init		(GstMPEGAudioParse *mp3parse);

static void	gst_mp3parse_loop		(GstElement *element);
static void	gst_mp3parse_chain		(GstPad *pad,GstBuffer *buf);
static long	bpf_from_header			(GstMPEGAudioParse *parse, unsigned long header);
static int	head_check			(unsigned long head);

static void	gst_mp3parse_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void	gst_mp3parse_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_mp3parse_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_mp3parse_get_type(void) {
  static GType mp3parse_type = 0;

  if (!mp3parse_type) {
    static const GTypeInfo mp3parse_info = {
      sizeof(GstMPEGAudioParseClass),      NULL,
      NULL,
      (GClassInitFunc)gst_mp3parse_class_init,
      NULL,
      NULL,
      sizeof(GstMPEGAudioParse),
      0,
      (GInstanceInitFunc)gst_mp3parse_init,
    };
    mp3parse_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMPEGAudioParse", &mp3parse_info, 0);
  }
  return mp3parse_type;
}

static void
gst_mp3parse_class_init (GstMPEGAudioParseClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_SKIP,
    g_param_spec_int("skip","skip","skip",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BIT_RATE,
    g_param_spec_int("bit_rate","bit_rate","bit_rate",
                     G_MININT,G_MAXINT,0,G_PARAM_READABLE)); /* CHECKME */

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_mp3parse_set_property;
  gobject_class->get_property = gst_mp3parse_get_property;
}

static void
gst_mp3parse_init (GstMPEGAudioParse *mp3parse)
{
  mp3parse->sinkpad = gst_pad_new_from_template(sink_temp, "sink");
  gst_element_add_pad(GST_ELEMENT(mp3parse),mp3parse->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT(mp3parse),gst_mp3parse_loop);
#if 1	/* set this to one to use the old chaining code */
  gst_pad_set_chain_function(mp3parse->sinkpad,gst_mp3parse_chain);
  gst_element_set_loop_function (GST_ELEMENT(mp3parse),NULL);
#endif

  mp3parse->srcpad = gst_pad_new_from_template(src_temp, "src");
  gst_element_add_pad(GST_ELEMENT(mp3parse),mp3parse->srcpad);
  /*gst_pad_set_type_id(mp3parse->srcpad, mp3frametype); */

  mp3parse->partialbuf = NULL;
  mp3parse->skip = 0;
  mp3parse->in_flush = FALSE;
}

static guint32
gst_mp3parse_next_header (guchar *buf,guint32 len,guint32 start)
{
  guint32 offset = start;
  int f = 0;

  while (offset < (len - 4)) {
    fprintf(stderr,"%02x ",buf[offset]);
    if (buf[offset] == 0xff)
      f = 1;
    else if (f && ((buf[offset] >> 4) == 0x0f))
      return offset - 1;
    else
      f = 0;
    offset++;
  }
  return -1;
}

static void
gst_mp3parse_loop (GstElement *element)
{
  GstMPEGAudioParse *parse = GST_MP3PARSE(element);
  GstBuffer *inbuf, *outbuf;
  guint32 size, offset;
  guchar *data;
  guint32 start;
  guint32 header;
  gint bpf;

  while (1) {
    /* get a new buffer */
    inbuf = gst_pad_pull (parse->sinkpad);
    size = GST_BUFFER_SIZE (inbuf);
    data = GST_BUFFER_DATA (inbuf);
    offset = 0;
fprintf(stderr, "have buffer of %d bytes\n",size);

    /* loop through it and find all the frames */
    while (offset < (size - 4)) {
      start = gst_mp3parse_next_header (data,size,offset);
fprintf(stderr, "skipped %d bytes searching for the next header\n",start-offset);
      header = GULONG_FROM_BE(*((guint32 *)(data+start)));
fprintf(stderr, "header is 0x%08x\n",header);

      /* figure out how big the frame is supposed to be */
      bpf = bpf_from_header (parse, header);

      /* see if there are enough bytes in this buffer for the whole frame */
      if ((start + bpf) <= size) {
        outbuf = gst_buffer_create_sub (inbuf,start,bpf);
fprintf(stderr, "sending buffer of %d bytes\n",bpf);
        gst_pad_push (parse->srcpad, outbuf);
        offset = start + bpf;

      /* if not, we have to deal with it somehow */
      } else {
fprintf(stderr,"don't have enough data for this frame\n");
        
        break;
      }
    }
  }
}

static void
gst_mp3parse_chain (GstPad *pad, GstBuffer *buf)
{
  GstMPEGAudioParse *mp3parse;
  guchar *data;
  glong size,offset = 0;
  unsigned long header;
  int bpf;
  GstBuffer *outbuf;
  guint64 last_ts;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);
/*  g_return_if_fail(GST_IS_BUFFER(buf)); */

  mp3parse = GST_MP3PARSE (gst_pad_get_parent (pad));

  GST_DEBUG (0,"mp3parse: received buffer of %d bytes",GST_BUFFER_SIZE(buf));

  last_ts = GST_BUFFER_TIMESTAMP(buf);

  /* FIXME, do flush */
  /*
    if (mp3parse->partialbuf) {
      gst_buffer_unref(mp3parse->partialbuf);
      mp3parse->partialbuf = NULL;
    }
    mp3parse->in_flush = TRUE;
    */

  /* if we have something left from the previous frame */
  if (mp3parse->partialbuf) {

    mp3parse->partialbuf = gst_buffer_append(mp3parse->partialbuf, buf);
    /* and the one we received.. */
    gst_buffer_unref(buf);
  }
  else {
    mp3parse->partialbuf = buf;
  }

  size = GST_BUFFER_SIZE(mp3parse->partialbuf);
  data = GST_BUFFER_DATA(mp3parse->partialbuf);

  /* while we still have bytes left -4 for the header */
  while (offset < size-4) {
    int skipped = 0;

    GST_DEBUG (0,"mp3parse: offset %ld, size %ld ",offset, size);

    /* search for a possible start byte */
    for (;((data[offset] != 0xff) && (offset < size));offset++) skipped++;
    if (skipped && !mp3parse->in_flush) {
      GST_DEBUG (0,"mp3parse: **** now at %ld skipped %d bytes",offset,skipped);
    }
    /* construct the header word */
    header = GULONG_FROM_BE(*((gulong *)(data+offset)));
    /* if it's a valid header, go ahead and send off the frame */
    if (head_check(header)) {
      /* calculate the bpf of the frame */
      bpf = bpf_from_header(mp3parse, header);

      /********************************************************************************
      * robust seek support
      * - This performs additional frame validation if the in_flush flag is set
      *   (indicating a discontinuous stream).
      * - The current frame header is not accepted as valid unless the NEXT frame
      *   header has the same values for most fields.  This significantly increases
      *   the probability that we aren't processing random data.
      * - It is not clear if this is sufficient for robust seeking of Layer III
      *   streams which utilize the concept of a "bit reservoir" by borrow bitrate
      *   from previous frames.  In this case, seeking may be more complicated because
      *   the frames are not independently coded.
      ********************************************************************************/
      if ( mp3parse->in_flush ) {
        unsigned long header2;

        if ((size-offset)<(bpf+4)) { if (mp3parse->in_flush) break; } /* wait until we have the the entire current frame as well as the next frame header */

        header2 = GULONG_FROM_BE(*((gulong *)(data+offset+bpf)));
        GST_DEBUG(0,"mp3parse: header=%08lX, header2=%08lX, bpf=%d", header, header2, bpf );

        #define HDRMASK ~( (0xF<<12)/*bitrate*/ | (1<<9)/*padding*/ | (3<<4)/*mode extension*/ ) /* mask the bits which are allowed to differ between frames */

        if ( (header2&HDRMASK) != (header&HDRMASK) ) { /* require 2 matching headers in a row */
           GST_DEBUG(0,"mp3parse: next header doesn't match (header=%08lX, header2=%08lX, bpf=%d)", header, header2, bpf );
           offset++; /* This frame is invalid.  Start looking for a valid frame at the next position in the stream */
           continue;
        }

      }

      /* if we don't have the whole frame... */
      if ((size - offset) < bpf) {
        GST_DEBUG (0,"mp3parse: partial buffer needed %ld < %d ",(size-offset), bpf);
	break;
      } else {

        outbuf = gst_buffer_create_sub(mp3parse->partialbuf,offset,bpf);

        offset += bpf;
	if (mp3parse->skip == 0) {
          GST_DEBUG (0,"mp3parse: pushing buffer of %d bytes",GST_BUFFER_SIZE(outbuf));
	  if (mp3parse->in_flush) {
	    /* FIXME do some sort of flush event */
	    mp3parse->in_flush = FALSE;
	  }
	  GST_BUFFER_TIMESTAMP(outbuf) = last_ts;
          gst_pad_push(mp3parse->srcpad,outbuf);
	}
	else {
          GST_DEBUG (0,"mp3parse: skipping buffer of %d bytes",GST_BUFFER_SIZE(outbuf));
          gst_buffer_unref(outbuf);
	  mp3parse->skip--;
	}
      }
    } else {
      offset++;
      if (!mp3parse->in_flush) GST_DEBUG (0,"mp3parse: *** wrong header, skipping byte (FIXME?)");
    }
  }
  /* if we have processed this block and there are still */
  /* bytes left not in a partial block, copy them over. */
  if (size-offset > 0) {
    glong remainder = (size - offset);
    GST_DEBUG (0,"mp3parse: partial buffer needed %ld for trailing bytes",remainder);

    outbuf = gst_buffer_create_sub(mp3parse->partialbuf,offset,remainder);
    gst_buffer_unref(mp3parse->partialbuf);
    mp3parse->partialbuf = outbuf;
  }
  else {
    gst_buffer_unref(mp3parse->partialbuf);
    mp3parse->partialbuf = NULL;
  }
}

static int mp3parse_tabsel[2][3][16] =
{ { {0, 32, 64, 96, 128, 160, 192, 224, 256, 288, 320, 352, 384, 416, 448, },
    {0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, },
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, } },
  { {0, 32, 48, 56, 64, 80, 96, 112, 128, 144, 160, 176, 192, 224, 256, },
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, },
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, } },
};

static long mp3parse_freqs[9] =
{44100, 48000, 32000, 22050, 24000, 16000, 11025, 12000, 8000};


static long
bpf_from_header (GstMPEGAudioParse *parse, unsigned long header)
{
  int layer_index,layer,lsf,samplerate_index,padding;
  long bpf;

  /*mpegver = (header >> 19) & 0x3; // don't need this for bpf */
  layer_index = (header >> 17) & 0x3;
  layer = 4 - layer_index;
  lsf = (header & (1 << 20)) ? ((header & (1 << 19)) ? 0 : 1) : 1;
  parse->bit_rate = mp3parse_tabsel[lsf][layer - 1][((header >> 12) & 0xf)];
  samplerate_index = (header >> 10) & 0x3;
  padding = (header >> 9) & 0x1;

  if (layer == 1) {
    bpf = parse->bit_rate * 12000;
    bpf /= mp3parse_freqs[samplerate_index];
    bpf = ((bpf + padding) << 2);
  } else {
    bpf = parse->bit_rate * 144000;
    bpf /= mp3parse_freqs[samplerate_index];
    bpf += padding;
  }

  /*g_print("%08x: layer %d lsf %d bitrate %d samplerate_index %d padding %d - bpf %d\n", */
/*header,layer,lsf,bitrate,samplerate_index,padding,bpf); */

  return bpf;
}

static gboolean
head_check (unsigned long head)
{
  GST_DEBUG (0,"checking mp3 header 0x%08lx",head);
  /* if it's not a valid sync */
  if ((head & 0xffe00000) != 0xffe00000) {
    GST_DEBUG (0,"invalid sync");return FALSE; }
  /* if it's an invalid MPEG version */
  if (((head >> 19) & 3) == 0x1) {
    GST_DEBUG (0,"invalid MPEG version");return FALSE; }
  /* if it's an invalid layer */
  if (!((head >> 17) & 3)) {
    GST_DEBUG (0,"invalid layer");return FALSE; }
  /* if it's an invalid bitrate */
  if (((head >> 12) & 0xf) == 0x0) {
    GST_DEBUG (0,"invalid bitrate");return FALSE; }
  if (((head >> 12) & 0xf) == 0xf) {
    GST_DEBUG (0,"invalid bitrate");return FALSE; }
  /* if it's an invalid samplerate */
  if (((head >> 10) & 0x3) == 0x3) {
    GST_DEBUG (0,"invalid samplerate");return FALSE; }
  if ((head & 0xffff0000) == 0xfffe0000) { 
    GST_DEBUG (0,"invalid sync");return FALSE; }
  if (head & 0x00000002) {
        GST_DEBUG (0,"invalid emphasis");return FALSE; }

  return TRUE;
}

static void
gst_mp3parse_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMPEGAudioParse *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MP3PARSE(object));
  src = GST_MP3PARSE(object);

  switch (prop_id) {
    case ARG_SKIP:
      src->skip = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_mp3parse_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMPEGAudioParse *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_MP3PARSE(object));
  src = GST_MP3PARSE(object);

  switch (prop_id) {
    case ARG_SKIP:
      g_value_set_int (value, src->skip);
      break;
    case ARG_BIT_RATE:
      g_value_set_int (value, src->bit_rate * 1000);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the mp3parse element */
  factory = gst_elementfactory_new ("mp3parse",
		                    GST_TYPE_MP3PARSE,
                                    &mp3parse_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  sink_temp = mp3_sink_factory ();
  gst_elementfactory_add_padtemplate (factory, sink_temp);

  src_temp = mp3_src_factory ();
  gst_elementfactory_add_padtemplate (factory, src_temp);

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mp3parse",
  plugin_init
};
