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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>

#include "gstmplex.h"

#include "videostrm.hh"
#include "audiostrm.hh"


/* elementfactory information */
static GstElementDetails gst_mplex_details = {
  "MPlex multiplexer",
  "Codec/Muxer",
  "multiplex mpeg audio and video into a system stream",
  "Wim Taymans <wim.taymans@chello.be> ",
};

/* Sidec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_MUX_FORMAT,
  ARG_MUX_BITRATE,
  ARG_VIDEO_BUFFER,
  ARG_SYNC_OFFSET,
  ARG_SECTOR_SIZE,
  ARG_VBR,
  ARG_PACKETS_PER_PACK,
  ARG_SYSTEM_HEADERS,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "src_video",
    "video/mpeg",
      "mpegversion",    GST_PROPS_INT_RANGE (1, 2),
      "systemstream",   GST_PROPS_BOOLEAN (TRUE)
  )
)

GST_PAD_TEMPLATE_FACTORY (video_sink_factory,
  "video_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sink_video",
    "video/mpeg",
      "mpegversion",    GST_PROPS_INT_RANGE (1, 2),
      "systemstream",   GST_PROPS_BOOLEAN (FALSE)
  )
)
  
GST_PAD_TEMPLATE_FACTORY (audio_sink_factory,
  "audio_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sink_audio",
    "audio/mpeg",
      "layer", GST_PROPS_INT_RANGE (1, 3)
  )
)

GST_PAD_TEMPLATE_FACTORY (private_1_sink_factory,
  "private_stream_1_%d",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sink_private1",
    "audio/x-ac3",
       NULL
  )
)

GST_PAD_TEMPLATE_FACTORY (private_2_sink_factory,
  "private_stream_2",
  GST_PAD_SINK,
  GST_PAD_REQUEST,
  GST_CAPS_NEW (
    "sink_private2",
    "unknown/unknown",
       NULL
  )
)

#define GST_TYPE_MPLEX_MUX_FORMAT (gst_mplex_mux_format_get_type())
static GType
gst_mplex_mux_format_get_type (void) 
{
  static GType mplex_mux_format_type = 0;
  static GEnumValue mplex_mux_format[] = {
    { MPEG_FORMAT_MPEG1,      "0", "Generic MPEG1" },
    { MPEG_FORMAT_VCD,        "1", "VCD" },
    { MPEG_FORMAT_VCD_NSR,    "2", "user-rate VCD" },
    { MPEG_FORMAT_MPEG2,      "3", "Generic MPEG2" },
    { MPEG_FORMAT_SVCD,       "4", "SVCD" },
    { MPEG_FORMAT_SVCD_NSR,   "5", "user-rate SVCD" },
    { MPEG_FORMAT_VCD_STILL,  "6", "VCD Stills" },
    { MPEG_FORMAT_SVCD_STILL, "7", "SVCD Stills" },
    { MPEG_FORMAT_DVD,        "8", "DVD" },
    {0, NULL, NULL},
  };
  if (!mplex_mux_format_type) {
    mplex_mux_format_type = g_enum_register_static("GstMPlexMuxFormat", mplex_mux_format);
  }
  return mplex_mux_format_type;
}

static void     gst_mplex_base_init             (gpointer g_class);
static void 	gst_mplex_class_init		(GstMPlex *klass);
static void 	gst_mplex_init			(GstMPlex *mplex);

static GstPad* 	gst_mplex_request_new_pad 	(GstElement     *element,
                          			 GstPadTemplate *templ,
			  			 const gchar    *req_name);
static void 	gst_mplex_loop 			(GstElement *element);
static size_t 	gst_mplex_read_callback  	(BitStream *bitstream, 
						 uint8_t *dest, size_t size, 
						 void *user_data);
static size_t 	gst_mplex_write_callback 	(PS_Stream *stream, 
						 uint8_t *data, size_t size, 
						 void *user_data);

static void     gst_mplex_get_property         	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);
static void     gst_mplex_set_property         	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
//static guint gst_mplex_signals[LAST_SIGNAL] = { 0 };

GType
gst_mplex_get_type (void) 
{
  static GType mplex_type = 0;

  if (!mplex_type) {
    static const GTypeInfo mplex_info = {
      sizeof(GstMPlexClass),      
      gst_mplex_base_init,
      NULL,
      (GClassInitFunc) gst_mplex_class_init,
      NULL,
      NULL,
      sizeof(GstMPlex),
      0,
      (GInstanceInitFunc) gst_mplex_init,
      NULL
    };
    mplex_type = g_type_register_static (GST_TYPE_ELEMENT, "GstMPlex", &mplex_info, (GTypeFlags)0);
  }

  return mplex_type;
}

static void
gst_mplex_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (src_factory));
  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (audio_sink_factory));
  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (video_sink_factory));
  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (private_1_sink_factory));
  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (private_2_sink_factory));

  gst_element_class_set_details (element_class, &gst_mplex_details);
}

static void
gst_mplex_class_init (GstMPlex *klass) 
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = GST_ELEMENT_CLASS (g_type_class_ref (GST_TYPE_ELEMENT));

  gobject_class->set_property = gst_mplex_set_property;
  gobject_class->get_property = gst_mplex_get_property;

  g_object_class_install_property (gobject_class, ARG_MUX_FORMAT,
    g_param_spec_enum ("mux_format", "Mux format", "Set defaults for particular MPEG profiles",
                       GST_TYPE_MPLEX_MUX_FORMAT, MPEG_FORMAT_MPEG1, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_MUX_BITRATE,
    g_param_spec_int ("mux_bitrate", "Mux bitrate", "Specify data rate of output stream in kbit/sec"
    		      "(0 = Compute from source streams)",
                      0, G_MAXINT, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_VIDEO_BUFFER,
    g_param_spec_int ("video_buffer", "Video buffer", "Specifies decoder buffers size in kB",
                      20, 2000, 20, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SYNC_OFFSET,
    g_param_spec_int ("sync_offset", "Sync offset", "Specify offset of timestamps (video-audio) in mSec",
                      G_MININT, G_MAXINT, 0, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SECTOR_SIZE,
    g_param_spec_int ("sector_size", "Sector size", "Specify sector size in bytes for generic formats",
                      256, 16384, 2028, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_VBR,
    g_param_spec_boolean ("vbr", "VBR", "Multiplex variable bit-rate video",
                          TRUE, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_PACKETS_PER_PACK,
      g_param_spec_int ("packets_per_pack", "Packets per pack",
                        "Number of packets per pack generic formats",
			1, 100, 1, (GParamFlags) G_PARAM_READWRITE));
  g_object_class_install_property (gobject_class, ARG_SYSTEM_HEADERS,
    g_param_spec_boolean ("system_headers", "System headers", 
                          " Create System header in every pack in generic formats",
                          TRUE, (GParamFlags) G_PARAM_READWRITE));

  gstelement_class->request_new_pad = gst_mplex_request_new_pad;
}

static void 
gst_mplex_init (GstMPlex *mplex) 
{
  mplex->srcpad = gst_pad_new_from_template (
  		GST_PAD_TEMPLATE_GET (src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (mplex), mplex->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (mplex), gst_mplex_loop);

  mplex->ostrm = new OutputStream ();
  mplex->strms = new vector<ElementaryStream *>();

  mplex->state = GST_MPLEX_OPEN_STREAMS;

  mplex->ostrm->opt_mux_format = 0;

  (void)mjpeg_default_handler_verbosity(mplex->ostrm->opt_verbosity);
}

static GstPadLinkReturn
gst_mplex_video_link (GstPad *pad, GstCaps *caps)
{   
  GstMPlex *mplex;
  gint version;
  GstMPlexStream *stream;
  
  mplex = GST_MPLEX (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  stream = (GstMPlexStream *) gst_pad_get_element_private (pad);
  
  if (!gst_caps_get_int (caps, "mpegversion", &version)){
    return GST_PAD_LINK_REFUSED;
  }

  if (version == 2) {
    stream->type = GST_MPLEX_STREAM_DVD_VIDEO;
  }
  else {
    stream->type = GST_MPLEX_STREAM_VIDEO;
  }

  return GST_PAD_LINK_OK;
}


static GstPad*
gst_mplex_request_new_pad (GstElement     *element,
                           GstPadTemplate *templ,
			   const gchar    *req_name) 
{
  GstMPlexStream *stream;
  GstMPlex *mplex;
  GstPad *pad = NULL;

  mplex = GST_MPLEX (element);
  
  stream = g_new0 (GstMPlexStream, 1);

  if (!strncmp (templ->name_template, "audio", 5)) {
    gchar *name = g_strdup_printf (templ->name_template, mplex->num_audio);

    pad = gst_pad_new (name, GST_PAD_SINK);
    g_free (name);

    stream->type = GST_MPLEX_STREAM_MPA;
  }
  else if (!strncmp (templ->name_template, "video", 5)) {
    gchar *name = g_strdup_printf (templ->name_template, mplex->num_video);

    pad = gst_pad_new (name, GST_PAD_SINK);
    /* we still need to figure out the mpeg version */
    gst_pad_set_link_function (pad, gst_mplex_video_link);
    g_free (name);

    stream->type = GST_MPLEX_STREAM_UNKOWN;
  }
  else if (!strncmp (templ->name_template, "private_stream_1", 16)) {
    gchar *name = g_strdup_printf (templ->name_template, mplex->num_private1);

    pad = gst_pad_new (name, GST_PAD_SINK);
    g_free (name);

    stream->type = GST_MPLEX_STREAM_AC3;
  }
  else if (!strncmp (templ->name_template, "private_stream_2", 16)) {
    pad = gst_pad_new ("private_stream_2", GST_PAD_SINK);

    stream->type = GST_MPLEX_STREAM_UNKOWN;
  }
  
  if (pad) {
    stream->pad = pad;
    stream->bitstream = new IBitStream();
    stream->bytestream = gst_bytestream_new (pad);
    stream->eos = FALSE;

    mplex->streams = g_list_prepend (mplex->streams, stream);

    gst_element_add_pad (element, pad);
    gst_pad_set_element_private (pad, stream);
  }
  else {
    /* no pad, free our stream again */
    g_free (stream);
  }

  return pad;
}


static size_t
gst_mplex_read_callback (BitStream *bitstream, uint8_t *dest, size_t size, void *user_data)
{
  GstMPlexStream *stream;
  guint8 *data;
  guint32 len;

  stream = (GstMPlexStream *) user_data;

  if (stream->eos)
    return 0;

  do {
    len = gst_bytestream_peek_bytes (stream->bytestream, &data, size);
    if (len < size) {
      guint32 avail= 0;
      GstEvent *event = NULL;
    
      gst_bytestream_get_status (stream->bytestream, &avail, &event);
      if (event != NULL) {
        switch (GST_EVENT_TYPE (event)) {
          case GST_EVENT_EOS:
	    stream->eos = TRUE;
	    break;
	  default:
	    break;
        }
        gst_event_unref (event);
      }
    }
  }
  while (len == 0);

  memcpy (dest, data, len);
  
  gst_bytestream_flush_fast (stream->bytestream, len);

  return len;
}

static size_t
gst_mplex_write_callback (PS_Stream *stream, uint8_t *data, size_t size, void *user_data)
{
  GstMPlex *mplex;
  GstBuffer *outbuf;

  mplex = GST_MPLEX (user_data);

  if (GST_PAD_IS_USABLE (mplex->srcpad)) {
    outbuf = gst_buffer_new_and_alloc (size);
    memcpy (GST_BUFFER_DATA (outbuf), data, size);

    gst_pad_push (mplex->srcpad, GST_DATA (outbuf));
  }

  return size;
}

static void 
gst_mplex_loop (GstElement *element)
{
  GstMPlex *mplex;

  mplex = GST_MPLEX (element);

  switch (mplex->state) {
    case GST_MPLEX_OPEN_STREAMS:
    {
      mplex->ostrm->InitSyntaxParameters();

      GList *walk = mplex->streams;
      while (walk) {
        GstMPlexStream *stream = (GstMPlexStream *) walk->data;

        stream->bitstream->open (gst_mplex_read_callback, stream);

	switch (stream->type) {
	  case GST_MPLEX_STREAM_MPA:
	  {
	    MPAStream *mpastream;

            mpastream = new MPAStream(*stream->bitstream, *mplex->ostrm);
            mpastream->Init(0);
            stream->elem_stream = mpastream;
	    break;
	  }
	  case GST_MPLEX_STREAM_AC3:
	  {
	    AC3Stream *ac3stream;

            ac3stream = new AC3Stream(*stream->bitstream, *mplex->ostrm);
            ac3stream->Init(0);
            stream->elem_stream = ac3stream;
	    break;
	  }
	  case GST_MPLEX_STREAM_DVD_VIDEO:
	  {
	    DVDVideoStream *dvdstream;

            dvdstream = new DVDVideoStream(*stream->bitstream, *mplex->ostrm);
            dvdstream->Init(0);
            stream->elem_stream = dvdstream;
	    break;
	  }
	  case GST_MPLEX_STREAM_VIDEO:
	  {
	    VideoStream *videostream;

            videostream = new VideoStream(*stream->bitstream, *mplex->ostrm);
            videostream->Init(0);
            stream->elem_stream = videostream;
	    break;
	  }
	  default:
	    break;
	}
        mplex->strms->push_back(stream->elem_stream);
	
        walk = g_list_next (walk);
      }

      mplex->ps_stream = new PS_Stream (gst_mplex_write_callback, mplex);
      mplex->ostrm->Init (mplex->strms, mplex->ps_stream);
      
      /* move to running state after this */
      mplex->state = GST_MPLEX_RUN;
      break;
    }
    case GST_MPLEX_RUN:
      if (!mplex->ostrm->OutputMultiplex()) {
        mplex->state = GST_MPLEX_END;
      }
      break;
    case GST_MPLEX_END:
    {
      mplex->ostrm->Close ();
      gst_pad_push (mplex->srcpad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
      gst_element_set_eos (element);
      break;
    }
    default:
      break;
  }

}

static void 
gst_mplex_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMPlex *mplex;

  mplex = GST_MPLEX(object);

  switch(prop_id) {
    case ARG_MUX_FORMAT:
      mplex->ostrm->opt_mux_format = g_value_get_enum (value);
      break;
    case ARG_MUX_BITRATE:
      mplex->data_rate = g_value_get_int (value);
      mplex->ostrm->opt_data_rate = ((mplex->data_rate * 1000 / 8 + 49) / 50) * 50;
      break;
    case ARG_VIDEO_BUFFER:
      mplex->ostrm->opt_buffer_size = g_value_get_int (value);
      break;
    case ARG_SYNC_OFFSET:
    {
      mplex->sync_offset = g_value_get_int (value);
      if (mplex->sync_offset < 0) {
        mplex->ostrm->opt_audio_offset = -mplex->sync_offset;
        mplex->ostrm->opt_video_offset = 0;
      }
      else {
        mplex->ostrm->opt_video_offset = mplex->sync_offset;
      }
      break;
    }
    case ARG_SECTOR_SIZE:
      mplex->ostrm->opt_sector_size = g_value_get_int (value);
      break;
    case ARG_VBR:
      mplex->ostrm->opt_VBR = g_value_get_boolean (value);
      break;
    case ARG_PACKETS_PER_PACK:
      mplex->ostrm->opt_packets_per_pack = g_value_get_int (value);
      break;
    case ARG_SYSTEM_HEADERS:
      mplex->ostrm->opt_always_system_headers = g_value_get_boolean (value);
      break;
    default:
      //G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      return;
  }
}

static void 
gst_mplex_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMPlex *mplex;

  mplex = GST_MPLEX(object);

  switch(prop_id) {
    case ARG_MUX_FORMAT:
      g_value_set_enum (value, mplex->ostrm->opt_mux_format);
      break;
    case ARG_MUX_BITRATE:
      g_value_set_int (value, mplex->data_rate);
      break;
    case ARG_VIDEO_BUFFER:
      g_value_set_int (value, mplex->ostrm->opt_buffer_size);
      break;
    case ARG_SYNC_OFFSET:
      g_value_set_int (value, mplex->sync_offset);
      break;
    case ARG_SECTOR_SIZE:
      g_value_set_int (value, mplex->ostrm->opt_sector_size);
      break;
    case ARG_VBR:
      g_value_set_boolean (value, mplex->ostrm->opt_VBR);
      break;
    case ARG_PACKETS_PER_PACK:
      g_value_set_int (value, mplex->ostrm->opt_packets_per_pack);
      break;
    case ARG_SYSTEM_HEADERS:
      g_value_set_boolean (value, mplex->ostrm->opt_always_system_headers);
      break;
    default:
      //G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  if (!gst_element_register (plugin, "mplex", GST_RANK_NONE, GST_TYPE_MPLEX))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mplex",
  "MPlexs an audio and video stream into a system stream",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)

