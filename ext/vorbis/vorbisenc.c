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


#include <stdlib.h>
#include <string.h>

#include <vorbis/vorbisenc.h>

#include "vorbisenc.h"



extern GstPadTemplate *enc_src_template, *enc_sink_template;

/* elementfactory information */
GstElementDetails vorbisenc_details = {
  "Ogg Vorbis encoder",
  "Filter/Audio/Encoder",
  "Encodes audio in OGG Vorbis format",
  VERSION,
  "Monty <monty@xiph.org>, " 
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2000",
};

/* VorbisEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BITRATE,
};

static void 	gst_vorbisenc_class_init 	(VorbisEncClass * klass);
static void 	gst_vorbisenc_init 		(VorbisEnc * vorbisenc);

static void 	gst_vorbisenc_chain 		(GstPad * pad, GstBuffer * buf);
static void 	gst_vorbisenc_setup 		(VorbisEnc * vorbisenc);

static void 	gst_vorbisenc_get_property 	(GObject * object, guint prop_id, GValue * value,
						 GParamSpec * pspec);
static void 	gst_vorbisenc_set_property 	(GObject * object, guint prop_id, const GValue * value,
						 GParamSpec * pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_vorbisenc_signals[LAST_SIGNAL] = { 0 }; */

GType
vorbisenc_get_type (void)
{
  static GType vorbisenc_type = 0;

  if (!vorbisenc_type) {
    static const GTypeInfo vorbisenc_info = {
      sizeof (VorbisEncClass), 
      NULL,
      NULL,
      (GClassInitFunc) gst_vorbisenc_class_init,
      NULL,
      NULL,
      sizeof (VorbisEnc),
      0,
      (GInstanceInitFunc) gst_vorbisenc_init,
    };

    vorbisenc_type = g_type_register_static (GST_TYPE_ELEMENT, "VorbisEnc", &vorbisenc_info, 0);
  }
  return vorbisenc_type;
}

static void
gst_vorbisenc_class_init (VorbisEncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE, 
    g_param_spec_int ("bitrate", "bitrate", "bitrate", 
	    G_MININT, G_MAXINT, 0, G_PARAM_READWRITE));

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_vorbisenc_set_property;
  gobject_class->get_property = gst_vorbisenc_get_property;
}

static GstPadConnectReturn
gst_vorbisenc_sinkconnect (GstPad * pad, GstCaps * caps)
{
  VorbisEnc *vorbisenc;

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_CONNECT_DELAYED;

  gst_caps_get_int (caps, "channels", &vorbisenc->channels);
  gst_caps_get_int (caps, "rate",     &vorbisenc->frequency);

  gst_vorbisenc_setup (vorbisenc);

  if (vorbisenc->setup)
    return GST_PAD_CONNECT_OK;

  return GST_PAD_CONNECT_REFUSED;
}

static void
gst_vorbisenc_init (VorbisEnc * vorbisenc)
{
  vorbisenc->sinkpad = gst_pad_new_from_template (enc_sink_template, "sink");
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->sinkpad);
  gst_pad_set_chain_function (vorbisenc->sinkpad, gst_vorbisenc_chain);
  gst_pad_set_connect_function (vorbisenc->sinkpad, gst_vorbisenc_sinkconnect);

  vorbisenc->srcpad = gst_pad_new_from_template (enc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (vorbisenc), vorbisenc->srcpad);

  vorbisenc->channels = 2;
  vorbisenc->frequency = 44100;
  vorbisenc->bitrate = 128000;
  vorbisenc->setup = FALSE;

  /* we're chained and we can deal with events */
  GST_FLAG_SET (vorbisenc, GST_ELEMENT_EVENT_AWARE);
}

static void
gst_vorbisenc_setup (VorbisEnc * vorbisenc)
{
  static const gchar *comment = "Track encoded with GStreamer";
  /********** Encode setup ************/

  /* choose an encoding mode */
  /* (mode 0: 44kHz stereo uncoupled, roughly 128kbps VBR) */
  vorbis_info_init (&vorbisenc->vi);
  vorbis_encode_init (&vorbisenc->vi, vorbisenc->channels, vorbisenc->frequency,
		      -1, vorbisenc->bitrate, -1);

  /* add a comment */
  vorbis_comment_init (&vorbisenc->vc);
  vorbis_comment_add (&vorbisenc->vc, (gchar *)comment);
  /*
  gst_element_send_event (GST_ELEMENT (vorbisenc),
             gst_event_new_info ("comment", GST_PROPS_STRING (comment), NULL));
	     */

  /* set up the analysis state and auxiliary encoding storage */
  vorbis_analysis_init (&vorbisenc->vd, &vorbisenc->vi);
  vorbis_block_init (&vorbisenc->vd, &vorbisenc->vb);

  /* set up our packet->stream encoder */
  /* pick a random serial number; that way we can more likely build
     chained streams just by concatenation */
  srand (time (NULL));
  ogg_stream_init (&vorbisenc->os, rand ());

  /* Vorbis streams begin with three headers; the initial header (with
     most of the codec setup parameters) which is mandated by the Ogg
     bitstream spec.  The second header holds any comment fields.  The
     third header holds the bitstream codebook.  We merely need to
     make the headers, then pass them to libvorbis one at a time;
     libvorbis handles the additional Ogg bitstream constraints */

  {
    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;

    vorbis_analysis_headerout (&vorbisenc->vd, &vorbisenc->vc, &header, &header_comm, &header_code);
    ogg_stream_packetin (&vorbisenc->os, &header);	/* automatically placed in its own
							   page */
    ogg_stream_packetin (&vorbisenc->os, &header_comm);
    ogg_stream_packetin (&vorbisenc->os, &header_code);

    /* no need to write out here.  We'll get to that in the main loop */
  }

  vorbisenc->setup = TRUE;
}

static void
gst_vorbisenc_chain (GstPad * pad, GstBuffer * buf)
{
  VorbisEnc *vorbisenc;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  vorbisenc = GST_VORBISENC (gst_pad_get_parent (pad));

  if (!vorbisenc->setup) {
    gst_element_error (GST_ELEMENT (vorbisenc), "encoder not initialized (input is not audio?)");
    if (GST_IS_BUFFER (buf))
      gst_buffer_unref (buf);
    else
      gst_pad_event_default (pad, GST_EVENT (buf));
    return;
  }

  if (GST_IS_EVENT (buf)) {
    switch (GST_EVENT_TYPE (buf)) {
      case GST_EVENT_EOS:
        /* end of file.  this can be done implicitly in the mainline,
           but it's easier to see here in non-clever fashion.
           Tell the library we're at end of stream so that it can handle
           the last frame and mark end of stream in the output properly */
        vorbis_analysis_wrote (&vorbisenc->vd, 0);
      default:
	gst_pad_event_default (pad, GST_EVENT (buf));
	break;
    }
  }
  else {
    gint16 *data;
    gulong size;
    gulong i, j;
    float **buffer;
  
    /* data to encode */
    data = (gint16 *) GST_BUFFER_DATA (buf);
    size = GST_BUFFER_SIZE (buf) / 2;

    /* expose the buffer to submit data */
    buffer = vorbis_analysis_buffer (&vorbisenc->vd, size / vorbisenc->channels);

    /* uninterleave samples */
    for (i = 0; i < size / vorbisenc->channels; i++) {
      for (j = 0; j < vorbisenc->channels; j++)
	buffer[j][i] = data[i * vorbisenc->channels + j] / 32768.f;
    }

    /* tell the library how much we actually submitted */
    vorbis_analysis_wrote (&vorbisenc->vd, size / vorbisenc->channels);

    gst_buffer_unref (buf);
  }

  /* vorbis does some data preanalysis, then divvies up blocks for
     more involved (potentially parallel) processing.  Get a single
     block for encoding now */
  while (vorbis_analysis_blockout (&vorbisenc->vd, &vorbisenc->vb) == 1) {

    /* analysis */
    vorbis_analysis (&vorbisenc->vb, NULL);
    vorbis_bitrate_addblock(&vorbisenc->vb);
    
    while(vorbis_bitrate_flushpacket(&vorbisenc->vd, &vorbisenc->op)) {

      /* weld the packet into the bitstream */
      ogg_stream_packetin (&vorbisenc->os, &vorbisenc->op);

      /* write out pages (if any) */
      while (!vorbisenc->eos) {
        int result = ogg_stream_pageout (&vorbisenc->os, &vorbisenc->og);
        GstBuffer *outbuf;

        if (result == 0)
	  break;

        outbuf = gst_buffer_new ();
        GST_BUFFER_DATA (outbuf) = g_malloc (vorbisenc->og.header_len + vorbisenc->og.body_len);
        GST_BUFFER_SIZE (outbuf) = vorbisenc->og.header_len + vorbisenc->og.body_len;

        memcpy (GST_BUFFER_DATA (outbuf), vorbisenc->og.header, vorbisenc->og.header_len);
        memcpy (GST_BUFFER_DATA (outbuf) + vorbisenc->og.header_len, vorbisenc->og.body,
	        vorbisenc->og.body_len);

        GST_DEBUG (0, "vorbisenc: encoded buffer of %d bytes", GST_BUFFER_SIZE (outbuf));

        gst_pad_push (vorbisenc->srcpad, outbuf);

        /* this could be set above, but for illustrative purposes, I do
           it here (to show that vorbis does know where the stream ends) */
        if (ogg_page_eos (&vorbisenc->og)) {
	  vorbisenc->eos = 1;
        }
      }
    }
  }

  if (vorbisenc->eos) {
    /* clean up and exit.  vorbis_info_clear() must be called last */

    ogg_stream_clear (&vorbisenc->os);
    vorbis_block_clear (&vorbisenc->vb);
    vorbis_dsp_clear (&vorbisenc->vd);
    vorbis_info_clear (&vorbisenc->vi);
  }

}

static void
gst_vorbisenc_get_property (GObject * object, guint prop_id, GValue * value, GParamSpec * pspec)
{
  VorbisEnc *vorbisenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_int (value, vorbisenc->bitrate);
      break;
    default:
      break;
  }
}

static void
gst_vorbisenc_set_property (GObject * object, guint prop_id, const GValue * value,
			    GParamSpec * pspec)
{
  VorbisEnc *vorbisenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_VORBISENC (object));

  vorbisenc = GST_VORBISENC (object);

  switch (prop_id) {
    case ARG_BITRATE:
      vorbisenc->bitrate = g_value_get_int (value);
      break;
    default:
      break;
  }
}
