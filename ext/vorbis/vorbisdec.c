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
#include <sys/soundcard.h>

#include <vorbisdec.h>


extern GstPadTemplate *dec_src_template, *dec_sink_template;

/* elementfactory information */
GstElementDetails vorbisdec_details = 
{
  "Ogg Vorbis decoder",
  "Filter/Audio/Decoder",
  "Decodes OGG Vorbis audio",
  VERSION,
  "Monty <monty@xiph.org>, " 
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* VorbisDec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_COMMENT,
  ARG_VENDOR,
  ARG_VERSION,
  ARG_CHANNELS,
  ARG_RATE,
  ARG_BITRATE_UPPER,
  ARG_BITRATE_NOMINAL,
  ARG_BITRATE_LOWER,
  ARG_BITRATE_WINDOW,
};

static void 	gst_vorbisdec_class_init 	(VorbisDecClass *klass);
static void 	gst_vorbisdec_init 		(VorbisDec *vorbisdec);

static void 	gst_vorbisdec_get_property 	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static void 	gst_vorbisdec_loop 		(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_vorbisdec_signals[LAST_SIGNAL] = { 0 }; */

GType
vorbisdec_get_type (void)
{
  static GType vorbisdec_type = 0;

  if (!vorbisdec_type) {
    static const GTypeInfo vorbisdec_info = {
      sizeof (VorbisDecClass), 
      NULL,
      NULL,
      (GClassInitFunc) gst_vorbisdec_class_init,
      NULL,
      NULL,
      sizeof (VorbisDec),
      0,
      (GInstanceInitFunc) gst_vorbisdec_init,
    };

    vorbisdec_type = g_type_register_static (GST_TYPE_ELEMENT, "VorbisDec", &vorbisdec_info, 0);
  }
  return vorbisdec_type;
}

static void
gst_vorbisdec_class_init (VorbisDecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  g_object_class_install_property (gobject_class, ARG_COMMENT,
    g_param_spec_string ("comment", "Comment", "The comment tags for this vorbis stream",
                         "", G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_VENDOR,
    g_param_spec_string ("vendor", "Vendor", "The vendor for this vorbis stream",
                         "", G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_VERSION,
    g_param_spec_int ("version", "Version", "The version",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_CHANNELS,
    g_param_spec_int ("channels", "Channels", "The number of channels",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_RATE,
    g_param_spec_int ("rate", "Rate", "The samplerate",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE_UPPER,
    g_param_spec_int ("bitrate_upper", "bitrate_upper", "bitrate_upper",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE_NOMINAL,
    g_param_spec_int ("bitrate_nominal", "bitrate_nominal", "bitrate_nominal",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE_LOWER,
    g_param_spec_int ("bitrate_lower", "bitrate_lower", "bitrate_lower",
                       0, G_MAXINT, 0, G_PARAM_READABLE));
  g_object_class_install_property (gobject_class, ARG_BITRATE_WINDOW,
    g_param_spec_int ("bitrate_window", "bitrate_window", "bitrate_window",
                       0, G_MAXINT, 0, G_PARAM_READABLE));

  gobject_class->get_property = gst_vorbisdec_get_property;
}

static void
gst_vorbisdec_init (VorbisDec * vorbisdec)
{
  vorbisdec->sinkpad = gst_pad_new_from_template (dec_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (vorbisdec), vorbisdec->sinkpad);

  gst_element_set_loop_function (GST_ELEMENT (vorbisdec), gst_vorbisdec_loop);
  vorbisdec->srcpad = gst_pad_new_from_template (dec_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (vorbisdec), vorbisdec->srcpad);

  ogg_sync_init (&vorbisdec->oy);	/* Now we can read pages */
  vorbisdec->convsize = 4096;
  vorbisdec->total_out = 0;
}

static GstBuffer *
gst_vorbisdec_pull (VorbisDec * vorbisdec, ogg_sync_state * oy)
{
  GstBuffer *buf;

  do {
    GST_DEBUG (0, "vorbisdec: pull ");

    buf = gst_pad_pull (vorbisdec->sinkpad);

    if (GST_IS_EVENT (buf)) {
      switch (GST_EVENT_TYPE (buf)) {
	case GST_EVENT_FLUSH:
	  ogg_sync_reset (oy);
	case GST_EVENT_EOS:
	default:
	  gst_pad_event_default (vorbisdec->sinkpad, GST_EVENT (buf));
	  break;
      }
      buf = NULL;
    }
  } while (buf == NULL);

  GST_DEBUG (0, "vorbisdec: pull done");

  return buf;
}

static void
gst_vorbisdec_loop (GstElement * element)
{
  VorbisDec *vorbisdec;
  GstBuffer *buf;
  GstBuffer *outbuf;

  ogg_sync_state oy;		/* sync and verify incoming physical bitstream */
  ogg_stream_state os;		/* take physical pages, weld into a logical
				   stream of packets */
  ogg_page og;			/* one Ogg bitstream page.  Vorbis packets are inside */
  ogg_packet op;		/* one raw packet of data for decode */

  vorbis_dsp_state vd;		/* central working state for the packet->PCM decoder */
  vorbis_block vb;		/* local working space for packet->PCM decode */

  char *buffer;
  int bytes;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_VORBISDEC (element));

  vorbisdec = GST_VORBISDEC (element);

  /********** Decode setup ************/
  ogg_sync_init (&oy);		/* Now we can read pages */

  while (1) {			/* we repeat if the bitstream is chained */
    int eos = 0;
    int i;

    /* grab some data at the head of the stream.  We want the first page
       (which is guaranteed to be small and only contain the Vorbis
       stream initial header) We need the first page to get the stream
       serialno. */

    /* submit a 4k block to libvorbis' Ogg layer */
    buf = gst_vorbisdec_pull (vorbisdec, &oy);

    bytes = GST_BUFFER_SIZE (buf);
    buffer = ogg_sync_buffer (&oy, bytes);
    memcpy (buffer, GST_BUFFER_DATA (buf), bytes);
    gst_buffer_unref (buf);

    ogg_sync_wrote (&oy, bytes);

    /* Get the first page. */
    if (ogg_sync_pageout (&oy, &og) != 1) {
      /* error case.  Must not be Vorbis data */
      gst_element_error (element, "input does not appear to be an Ogg bitstream.");
      break;
    }

    /* Get the serial number and set up the rest of decode. */
    /* serialno first; use it to set up a logical stream */
    ogg_stream_init (&os, ogg_page_serialno (&og));

    /* extract the initial header from the first page and verify that the
       Ogg bitstream is in fact Vorbis data */

    /* I handle the initial header first instead of just having the code
       read all three Vorbis headers at once because reading the initial
       header is an easy way to identify a Vorbis bitstream and it's
       useful to see that functionality seperated out. */

    vorbis_info_init (&vorbisdec->vi);
    vorbis_comment_init (&vorbisdec->vc);
    if (ogg_stream_pagein (&os, &og) < 0) {
      /* error; stream version mismatch perhaps */
      g_warning ("Error reading first page of Ogg bitstream data.\n");
      return;
    }

    if (ogg_stream_packetout (&os, &op) != 1) {
      /* no page? must not be vorbis */
      g_warning ("Error reading initial header packet.\n");
      return;
    }

    if (vorbis_synthesis_headerin (&vorbisdec->vi, &vorbisdec->vc, &op) < 0) {
      /* error case; not a vorbis header */
      g_warning ("This Ogg bitstream does not contain Vorbis audio data.\n");
      return;
    }

    /* At this point, we're sure we're Vorbis.  We've set up the logical
       (Ogg) bitstream decoder.  Get the comment and codebook headers and
       set up the Vorbis decoder */

    /* The next two packets in order are the comment and codebook headers.
       They're likely large and may span multiple pages.  Thus we reead
       and submit data until we get our two pacakets, watching that no
       pages are missing.  If a page is missing, error out; losing a
       header page is the only place where missing data is fatal. */

    i = 0;
    while (i < 2) {
      while (i < 2) {
	int result = ogg_sync_pageout (&oy, &og);

	if (result == 0)
	  break;		/* Need more data */
	/* Don't complain about missing or corrupt data yet.  We'll
	   catch it at the packet output phase */
	if (result == 1) {
	  ogg_stream_pagein (&os, &og);	/* we can ignore any errors here
					   as they'll also become apparent
					   at packetout */
	  while (i < 2) {
	    result = ogg_stream_packetout (&os, &op);
	    if (result == 0)
	      break;
	    if (result == -1) {
	      /* Uh oh; data at some point was corrupted or missing!
	         We can't tolerate that in a header.  Die. */
	      g_warning ("Corrupt secondary header. expect trouble\n");
	    }
	    vorbis_synthesis_headerin (&vorbisdec->vi, &vorbisdec->vc, &op);
	    i++;
	  }
	}
      }
      gst_element_yield (GST_ELEMENT (vorbisdec));

      buf = gst_vorbisdec_pull (vorbisdec, &oy);
      bytes = GST_BUFFER_SIZE (buf);
      buffer = ogg_sync_buffer (&oy, bytes);
      memcpy (buffer, GST_BUFFER_DATA (buf), bytes);
      gst_buffer_unref (buf);

      if (bytes == 0 && i < 2) {
	g_warning ("End of file before finding all Vorbis headers! expect trouble..\n");
      }
      ogg_sync_wrote (&oy, bytes);
    }

    /* Throw the comments plus a few lines about the bitstream we're
       decoding */
    {
      char **ptr = vorbisdec->vc.user_comments;

      while (*ptr) {
	/* FIXME parse comments */
	++ptr;
      }
      g_object_freeze_notify (G_OBJECT (vorbisdec));
      g_object_notify (G_OBJECT (vorbisdec), "comment");
      g_object_notify (G_OBJECT (vorbisdec), "vendor");
      g_object_notify (G_OBJECT (vorbisdec), "version");
      g_object_notify (G_OBJECT (vorbisdec), "channels");
      g_object_notify (G_OBJECT (vorbisdec), "rate");
      g_object_notify (G_OBJECT (vorbisdec), "bitrate_upper");
      g_object_notify (G_OBJECT (vorbisdec), "bitrate_nominal");
      g_object_notify (G_OBJECT (vorbisdec), "bitrate_lower");
      g_object_notify (G_OBJECT (vorbisdec), "bitrate_window");
      g_object_thaw_notify (G_OBJECT (vorbisdec));
    }

    gst_pad_try_set_caps (vorbisdec->srcpad,
		      GST_CAPS_NEW ("vorbisdec_src",
				    "audio/raw",
				      "format",     GST_PROPS_STRING ("int"),
				      "law",        GST_PROPS_INT (0),
				      "endianness", GST_PROPS_INT (G_BYTE_ORDER),
				      "signed",     GST_PROPS_BOOLEAN (TRUE),
				      "width",      GST_PROPS_INT (16),
				      "depth",      GST_PROPS_INT (16),
				      "rate",       GST_PROPS_INT (vorbisdec->vi.rate),
				      "channels",   GST_PROPS_INT (vorbisdec->vi.channels)
				   ));

    vorbisdec->convsize = 4096 / vorbisdec->vi.channels;

    /* OK, got and parsed all three headers. Initialize the Vorbis
       packet->PCM decoder. */
    vorbis_synthesis_init (&vd, &vorbisdec->vi);	/* central decode state */
    vorbis_block_init (&vd, &vb);	/* local state for most of the decode
					   so multiple block decodes can
					   proceed in parallel.  We could init
					   multiple vorbis_block structures
					   for vd here */

    /* The rest is just a straight decode loop until end of stream */
    while (!eos) {
      while (!eos) {
	int result = ogg_sync_pageout (&oy, &og);

	if (result == 0)
	  break;		/* need more data */
	if (result == -1) {	/* missing or corrupt data at this page position */
	}
	else {
	  ogg_stream_pagein (&os, &og);	/* can safely ignore errors at
					   this point */
	  while (1) {
	    result = ogg_stream_packetout (&os, &op);

	    if (result == 0)
	      break;		/* need more data */
	    if (result == -1) {	/* missing or corrupt data at this page position */
	      /* no reason to complain; already complained above */
	    }
	    else {
	      /* we have a packet.  Decode it */
	      float **pcm;
	      int samples;

	      if (vorbis_synthesis (&vb, &op) == 0)	/* test for success! */
		vorbis_synthesis_blockin (&vd, &vb);

	      /* 
	         **pcm is a multichannel double vector.  In stereo, for
	         example, pcm[0] is left, and pcm[1] is right.  samples is
	         the size of each channel.  Convert the float values
	         (-1.<=range<=1.) to whatever PCM format and write it out */

	      while ((samples = vorbis_synthesis_pcmout (&vd, &pcm)) > 0) {
		int j;
		int clipflag = 0;
		int bout = (samples < vorbisdec->convsize ? samples : vorbisdec->convsize);


		outbuf = gst_buffer_new ();
		GST_BUFFER_DATA (outbuf) = g_malloc (2 * vorbisdec->vi.channels * bout);
		GST_BUFFER_SIZE (outbuf) = 2 * vorbisdec->vi.channels * bout;
		GST_BUFFER_TIMESTAMP (outbuf) = vorbisdec->total_out * 1000000LL / vorbisdec->vi.rate;

		vorbisdec->total_out += bout;

		/* convert doubles to 16 bit signed ints (host order) and
		   interleave */
		for (i = 0; i < vorbisdec->vi.channels; i++) {
		  int16_t *ptr = ((int16_t *) GST_BUFFER_DATA (outbuf)) + i;
		  float *mono = pcm[i];

		  for (j = 0; j < bout; j++) {
		    int val = mono[j] * 32767.;

		    /* might as well guard against clipping */
		    if (val > 32767) {
		      val = 32767;
		      clipflag = 1;
		    }
		    if (val < -32768) {
		      val = -32768;
		      clipflag = 1;
		    }
		    *ptr = val;
		    ptr += vorbisdec->vi.channels;
		  }
		}

		GST_DEBUG (0, "vorbisdec: push");
		gst_pad_push (vorbisdec->srcpad, outbuf);
		GST_DEBUG (0, "vorbisdec: push done");

		vorbis_synthesis_read (&vd, bout);	/* tell libvorbis how
							   many samples we
							   actually consumed */
	      }
	    }
	  }
	  if (ogg_page_eos (&og))
	    eos = 1;
	}
      }
      if (!eos) {
        gst_element_yield (GST_ELEMENT (vorbisdec));

	buf = gst_vorbisdec_pull (vorbisdec, &oy);
	bytes = GST_BUFFER_SIZE (buf);
	buffer = ogg_sync_buffer (&oy, bytes);
	memcpy (buffer, GST_BUFFER_DATA (buf), bytes);
	gst_buffer_unref (buf);

	ogg_sync_wrote (&oy, bytes);
	if (bytes == 0) {
	  eos = 1;
	}
      }
    }

    /* clean up this logical bitstream; before exit we see if we're
       followed by another [chained] */

    ogg_stream_clear (&os);

    /* ogg_page and ogg_packet structs always point to storage in
       libvorbis.  They're never freed or manipulated directly */

    vorbis_block_clear (&vb);
    vorbis_dsp_clear (&vd);
    vorbis_info_clear (&vorbisdec->vi);	/* must be called last */
  }

  /* OK, clean up the framer */
  ogg_sync_clear (&oy);
}

static void 
gst_vorbisdec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  VorbisDec *vorbisdec;
	      
  g_return_if_fail (GST_IS_VORBISDEC (object));

  vorbisdec = GST_VORBISDEC (object);

  switch (prop_id) {
    case ARG_COMMENT:
      g_value_set_string (value, "comment");
      break;
    case ARG_VENDOR:
      g_value_set_string (value, vorbisdec->vc.vendor);
      break;
    case ARG_VERSION:
      g_value_set_int (value, vorbisdec->vi.version);
      break;
    case ARG_CHANNELS:
      g_value_set_int (value, vorbisdec->vi.channels);
      break;
    case ARG_RATE:
      g_value_set_int (value, vorbisdec->vi.rate);
      break;
    case ARG_BITRATE_UPPER:
      g_value_set_int (value, vorbisdec->vi.bitrate_upper);
      break;
    case ARG_BITRATE_NOMINAL:
      g_value_set_int (value, vorbisdec->vi.bitrate_nominal);
      break;
    case ARG_BITRATE_LOWER:
      g_value_set_int (value, vorbisdec->vi.bitrate_lower);
      break;
    case ARG_BITRATE_WINDOW:
      g_value_set_int (value, vorbisdec->vi.bitrate_window);
      break;
  }
}
