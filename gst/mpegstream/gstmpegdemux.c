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


/*#define GST_DEBUG_ENABLED*/
#include <gstmpegdemux.h>

/* elementfactory information */
static GstElementDetails mpeg_demux_details = {
  "MPEG Demuxer",
  "Codec/Demuxer",
  "LGPL",
  "Demultiplexes MPEG1 and MPEG2 System Streams",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n"
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 1999",
};

/* MPEG2Demux signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BIT_RATE,
  ARG_MPEG2,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (sink_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg_demux_sink",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT_RANGE (1, 2),
      "systemstream", GST_PROPS_BOOLEAN (TRUE)
  )
);

GST_PAD_TEMPLATE_FACTORY (audio_factory,
  "audio_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "mpeg_demux_audio",
    "audio/x-mp3",
    NULL
  )
);

GST_PAD_TEMPLATE_FACTORY (video_mpeg1_factory,
  "video_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "mpeg_demux_video_mpeg1",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT (1),
      "systemstream",  GST_PROPS_BOOLEAN (FALSE)
  )
);

GST_PAD_TEMPLATE_FACTORY (video_mpeg2_factory,
  "video_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "mpeg_demux_video_mpeg2",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT (2),
      "systemstream",  GST_PROPS_BOOLEAN (FALSE)
  )
);


GST_PAD_TEMPLATE_FACTORY (private1_factory,
  "private_stream_1_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "mpeg_demux_private1",
    "audio/a52",
    NULL
  )
);

GST_PAD_TEMPLATE_FACTORY (private2_factory,
  "private_stream_2",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "mpeg_demux_private2",
    "unknown/unknown",
    NULL
  )
);

GST_PAD_TEMPLATE_FACTORY (pcm_factory,
  "pcm_stream_%02d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "mpeg_demux_pcm",
    "audio/raw",
      "format",            GST_PROPS_STRING ("int"),
       "law",              GST_PROPS_INT (0),
       "endianness",       GST_PROPS_INT (G_BIG_ENDIAN),
       "signed",           GST_PROPS_BOOLEAN (TRUE),
       "width",            GST_PROPS_LIST (
	                     GST_PROPS_INT (16),
	                     GST_PROPS_INT (20),
	                     GST_PROPS_INT (24)
                           ),
       "depth",            GST_PROPS_LIST (
	                     GST_PROPS_INT (16),
	                     GST_PROPS_INT (20),
	                     GST_PROPS_INT (24)
                           ),
       "rate",             GST_PROPS_LIST (
	                     GST_PROPS_INT (48000),
	                     GST_PROPS_INT (96000)
                           ),
       "channels",         GST_PROPS_INT_RANGE (1, 8)
  )
);

GST_PAD_TEMPLATE_FACTORY (subtitle_factory,
  "subtitle_stream_%d",
  GST_PAD_SRC,
  GST_PAD_SOMETIMES,
  GST_CAPS_NEW (
    "mpeg_demux_subtitle",
    "video/mpeg",
    NULL
  )
);

static void 		gst_mpeg_demux_class_init	(GstMPEGDemuxClass *klass);
static void 		gst_mpeg_demux_init		(GstMPEGDemux *mpeg_demux);

static gboolean 	gst_mpeg_demux_parse_packhead 	(GstMPEGParse *mpeg_parse, GstBuffer *buffer);
static gboolean 	gst_mpeg_demux_parse_syshead 	(GstMPEGParse *mpeg_parse, GstBuffer *buffer);
static gboolean 	gst_mpeg_demux_parse_packet 	(GstMPEGParse *mpeg_parse, GstBuffer *buffer);
static gboolean 	gst_mpeg_demux_parse_pes 	(GstMPEGParse *mpeg_parse, GstBuffer *buffer);
static void		gst_mpeg_demux_send_data 	(GstMPEGParse *mpeg_parse, 
							 GstData *data, GstClockTime time);

static void		gst_mpeg_demux_lpcm_set_caps	(GstPad *pad, guint8 sample_info);
static void		gst_mpeg_demux_dvd_audio_clear	(GstMPEGDemux *mpeg_demux, int channel);

static void		gst_mpeg_demux_handle_discont 	(GstMPEGParse *mpeg_parse);
static gboolean 	gst_mpeg_demux_handle_src_event (GstPad *pad, GstEvent *event);

const GstFormat* 	gst_mpeg_demux_get_src_formats 	(GstPad *pad);
static void 		gst_mpeg_demux_set_index 	(GstElement *element, GstIndex *index);
static GstIndex* 	gst_mpeg_demux_get_index 	(GstElement *element);

static GstElementStateReturn
			gst_mpeg_demux_change_state 	(GstElement *element);

static GstMPEGParseClass *parent_class = NULL;
/*static guint gst_mpeg_demux_signals[LAST_SIGNAL] = { 0 };*/

GType
mpeg_demux_get_type (void)
{
  static GType mpeg_demux_type = 0;

  if (!mpeg_demux_type) {
    static const GTypeInfo mpeg_demux_info = {
      sizeof(GstMPEGDemuxClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_mpeg_demux_class_init,
      NULL,
      NULL,
      sizeof(GstMPEGDemux),
      0,
      (GInstanceInitFunc)gst_mpeg_demux_init,
    };
    mpeg_demux_type = g_type_register_static(GST_TYPE_MPEG_PARSE, "GstMPEGDemux", &mpeg_demux_info, 0);
  }
  return mpeg_demux_type;
}

static void
gst_mpeg_demux_class_init (GstMPEGDemuxClass *klass) 
{
  GstMPEGParseClass *mpeg_parse_class;
  GstElementClass *gstelement_class;

  parent_class = g_type_class_ref (GST_TYPE_MPEG_PARSE);

  mpeg_parse_class = (GstMPEGParseClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gstelement_class->change_state = gst_mpeg_demux_change_state;
  gstelement_class->set_index 	 = gst_mpeg_demux_set_index;
  gstelement_class->get_index 	 = gst_mpeg_demux_get_index;

  mpeg_parse_class->parse_packhead	= gst_mpeg_demux_parse_packhead;
  mpeg_parse_class->parse_syshead	= gst_mpeg_demux_parse_syshead;
  mpeg_parse_class->parse_packet	= gst_mpeg_demux_parse_packet;
  mpeg_parse_class->parse_pes		= gst_mpeg_demux_parse_pes;
  mpeg_parse_class->send_data		= gst_mpeg_demux_send_data;
  mpeg_parse_class->handle_discont	= gst_mpeg_demux_handle_discont;

}

static void
gst_mpeg_demux_init (GstMPEGDemux *mpeg_demux)
{
  gint i;
  GstMPEGParse *mpeg_parse = GST_MPEG_PARSE (mpeg_demux);

  gst_element_remove_pad (GST_ELEMENT (mpeg_parse), mpeg_parse->sinkpad);
  mpeg_parse->sinkpad = gst_pad_new_from_template(
		  GST_PAD_TEMPLATE_GET (sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (mpeg_parse), mpeg_parse->sinkpad);
  gst_element_remove_pad (GST_ELEMENT (mpeg_parse), mpeg_parse->srcpad);

  /* i think everything is already zero'd, but oh well*/
  for (i=0;i<NUM_PRIVATE_1_STREAMS;i++) {
    mpeg_demux->private_1_stream[i] = NULL;
  }
  for (i=0;i<NUM_PCM_STREAMS;i++) {
    mpeg_demux->pcm_stream[i] = NULL;
  }
  for (i=0;i<NUM_SUBTITLE_STREAMS;i++) {
    mpeg_demux->subtitle_stream[i] = NULL;
  }
  mpeg_demux->private_2_stream = NULL;
  for (i=0;i<NUM_VIDEO_STREAMS;i++) {
    mpeg_demux->video_stream[i] = NULL;
  }
  for (i=0;i<NUM_AUDIO_STREAMS;i++) {
    mpeg_demux->audio_stream[i] = NULL;
  }

  GST_FLAG_SET (mpeg_demux, GST_ELEMENT_EVENT_AWARE);
}

static GstMPEGStream* 
gst_mpeg_demux_new_stream (void)
{
  GstMPEGStream *stream;

  stream = g_new0 (GstMPEGStream, 1);

  return stream;
}

static void
gst_mpeg_demux_send_data (GstMPEGParse *mpeg_parse, GstData *data, GstClockTime time)
{
  /* GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse); */

  if (GST_IS_BUFFER (data)) {
    gst_buffer_unref (GST_BUFFER (data));
  }
  else {
    GstEvent *event = GST_EVENT (data);

    switch (GST_EVENT_TYPE (event)) {
      default:
        gst_pad_event_default (mpeg_parse->sinkpad, event);
	break;
    }
  }
}
static void
gst_mpeg_demux_handle_discont (GstMPEGParse *mpeg_parse)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  gint64 current_time = MPEGTIME_TO_GSTTIME (mpeg_parse->current_scr);
  gint i;

  GST_DEBUG (GST_CAT_EVENT, "discont %" G_GUINT64_FORMAT "\n", current_time);

  for (i=0;i<NUM_VIDEO_STREAMS;i++) {
    if (mpeg_demux->video_stream[i] && 
        GST_PAD_IS_USABLE (mpeg_demux->video_stream[i]->pad))
    {
      GstEvent *discont;

      discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
			current_time, NULL);

      gst_pad_push (mpeg_demux->video_stream[i]->pad, GST_BUFFER (discont));
    }
    if (mpeg_demux->audio_stream[i] && 
        GST_PAD_IS_USABLE (mpeg_demux->audio_stream[i]->pad))
    {
      GstEvent *discont;

      discont = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
			current_time, NULL);

      gst_pad_push (mpeg_demux->audio_stream[i]->pad, GST_BUFFER (discont));
    }
  }
}

static gboolean
gst_mpeg_demux_parse_packhead (GstMPEGParse *mpeg_parse, GstBuffer *buffer)
{
  guint8 *buf;

  parent_class->parse_packhead (mpeg_parse, buffer);

  GST_DEBUG (0, "in parse_packhead");

  buf = GST_BUFFER_DATA (buffer);
  /* do something usefull here */

  return TRUE;
}

static gboolean
gst_mpeg_demux_parse_syshead (GstMPEGParse *mpeg_parse, GstBuffer *buffer)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  guint16 header_length;
  guchar *buf;

  GST_DEBUG (0, "in parse_syshead");

  buf = GST_BUFFER_DATA (buffer);
  buf += 4;

  header_length = GUINT16_FROM_BE (*(guint16 *) buf);
  GST_DEBUG (0, "header_length %d", header_length);
  buf += 2;

  /* marker:1==1 ! rate_bound:22 | marker:1==1*/
  buf += 3;

  /* audio_bound:6==1 ! fixed:1 | constrained:1*/
  buf += 1;

  /* audio_lock:1 | video_lock:1 | marker:1==1 | video_bound:5 */
  buf += 1;

  /* apacket_rate_restriction:1 | reserved:7==0x7F */
  buf += 1;

  if (!GST_MPEG_PARSE_IS_MPEG2 (mpeg_demux)) {
    gint stream_count = (header_length - 6) / 3;
    gint i, j=0;

    GST_DEBUG (0, "number of streams=%d ",
	       stream_count);

    for (i = 0; i < stream_count; i++) {
      guint8 stream_id;
      gboolean STD_buffer_bound_scale;
      guint16 STD_buffer_size_bound;
      guint32 buf_byte_size_bound;
      gchar *name = NULL;
      GstMPEGStream **outstream = NULL;
      GstPadTemplate *newtemp = NULL;

      stream_id = *buf++;
      if (!(stream_id & 0x80)) {
	GST_DEBUG (0, "error in system header length");
	return FALSE;
      }

      /* check marker bits */
      if ((*buf & 0xC0) != 0xC0) {
	GST_DEBUG (0, "expecting placeholder bit values '11' after stream id\n");
	return FALSE;
      }

      STD_buffer_bound_scale = *buf & 0x20;
      STD_buffer_size_bound = (*buf++ & 0x1F) << 8;
      STD_buffer_size_bound |= *buf++;

      if (STD_buffer_bound_scale == 0) {
	buf_byte_size_bound = STD_buffer_size_bound * 128;
      }
      else {
	buf_byte_size_bound = STD_buffer_size_bound * 1024;
      }

      if (stream_id == 0xBD) {
        /* private_stream_1 */
	name = NULL;
	outstream = NULL;
      } else if (stream_id == 0xBF) {
        /* private_stream_2 */
	name = g_strdup_printf ("private_stream_2");
	outstream = &mpeg_demux->private_2_stream;
	newtemp = GST_PAD_TEMPLATE_GET (private2_factory);
      } else if (stream_id >= 0xC0 && stream_id < 0xE0) {
        /* Audio */
	name = g_strdup_printf ("audio_%02d", stream_id & 0x1F);
	outstream = &mpeg_demux->audio_stream[stream_id & 0x1F];
	newtemp = GST_PAD_TEMPLATE_GET (audio_factory);
      } else if (stream_id >= 0xE0 && stream_id < 0xF0) {
        /* Video */
	name = g_strdup_printf ("video_%02d", stream_id & 0x0F);
	outstream = &mpeg_demux->video_stream[stream_id & 0x0F];
        if (!GST_MPEG_PARSE_IS_MPEG2 (mpeg_demux)) {
          newtemp = GST_PAD_TEMPLATE_GET (video_mpeg1_factory);
	} else {
	  newtemp = GST_PAD_TEMPLATE_GET (video_mpeg2_factory);
	}
      } else {
	GST_DEBUG (0, "unkown stream id %d", stream_id);
      }

      GST_DEBUG (0, "stream ID 0x%02X (%s)", stream_id, name);
      GST_DEBUG (0, "STD_buffer_bound_scale %d", STD_buffer_bound_scale);
      GST_DEBUG (0, "STD_buffer_size_bound %d or %d bytes",
		 STD_buffer_size_bound, buf_byte_size_bound);

      /* create the pad and add it to self if it does not yet exist
       * this should trigger the NEW_PAD signal, which should be caught by
       * the app and used to attach to desired streams.
       */
      if (outstream && *outstream == NULL) {
	GstPad **outpad;
	GstCaps *caps;

	*outstream = gst_mpeg_demux_new_stream ();
        outpad = &((*outstream)->pad);
			
	*outpad = gst_pad_new_from_template (newtemp, name);
	caps = gst_pad_template_get_caps (newtemp);
	gst_pad_try_set_caps (*outpad, caps);
	gst_caps_unref (caps);

	gst_pad_set_formats_function (*outpad, gst_mpeg_demux_get_src_formats);
	gst_pad_set_convert_function (*outpad, gst_mpeg_parse_convert_src);
	gst_pad_set_event_mask_function (*outpad, gst_mpeg_parse_get_src_event_masks);
	gst_pad_set_event_function (*outpad, gst_mpeg_demux_handle_src_event);
	gst_pad_set_query_type_function (*outpad, gst_mpeg_parse_get_src_query_types);
	gst_pad_set_query_function (*outpad, gst_mpeg_parse_handle_src_query);

	gst_element_add_pad (GST_ELEMENT (mpeg_demux), (*outpad));
	gst_pad_set_element_private (*outpad, *outstream);

	(*outstream)->size_bound = buf_byte_size_bound;
	mpeg_demux->total_size_bound += buf_byte_size_bound;

	if (mpeg_demux->index) {
          gst_index_get_writer_id (mpeg_demux->index, GST_OBJECT (*outpad),
                           &(*outstream)->index_id);
	}

	if (GST_PAD_IS_USABLE (*outpad)) {
          GstEvent *event;
	  gint64 time;

	  time = mpeg_parse->current_scr;
	  
          event  = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME, 
			MPEGTIME_TO_GSTTIME (time), NULL);

	  gst_pad_push (*outpad, GST_BUFFER (event));
	}
      }
      else {
	/* we won't be needing this. */
	if (name)
	  g_free (name);
      }

      j++;
    }
  }

  return TRUE;
}

static gboolean
gst_mpeg_demux_parse_packet (GstMPEGParse *mpeg_parse, GstBuffer *buffer)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  guint8 id;
  guint16 headerlen;

  guint16 packet_length;
  gboolean STD_buffer_bound_scale;
  guint16 STD_buffer_size_bound;
  guint64 dts;
  guint8 ps_id_code;
  gint64 pts = -1;

  guint16 datalen;

  GstMPEGStream **outstream = NULL;
  GstPad *outpad = NULL;
  GstBuffer *outbuf;
  guint8 *buf, *basebuf;
  gint64 timestamp;

  GST_DEBUG (0, "in parse_packet");

  basebuf = buf = GST_BUFFER_DATA (buffer);
  id = *(buf+3);
  buf += 4;

  /* start parsing */
  packet_length = GUINT16_FROM_BE (*((guint16 *)buf));

  GST_DEBUG (0, "got packet_length %d", packet_length);
  headerlen = 2;
  buf += 2;

  /* loop through looping for stuffing bits, STD, PTS, DTS, etc */
  do {
    guint8 bits = *buf++;

    /* stuffing bytes */
    switch (bits & 0xC0) {
      case 0xC0:
        if (bits == 0xff) {
          GST_DEBUG (0, "have stuffing byte");
        } else {
          GST_DEBUG (0, "expected stuffing byte");
        }
        headerlen++;
	break;
      case 0x40:
        GST_DEBUG (0, "have STD");

        STD_buffer_bound_scale =  bits & 0x20;
        STD_buffer_size_bound  = (bits & 0x1F) << 8;
        STD_buffer_size_bound |=  *buf++;

        headerlen += 2;
	break;
      case 0x00:
        switch (bits & 0x30) {
	  case 0x20:
            /* pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
            pts  = (bits & 0x0E)   << 29;
            pts |=  *buf++         << 22;
            pts |= (*buf++ & 0xFE) << 14;
            pts |=  *buf++         <<  7;
            pts |= (*buf++ & 0xFE) >>  1;

            GST_DEBUG (0, "PTS = %" G_GUINT64_FORMAT, pts);
            headerlen += 5;
	    goto done;
	  case 0x30:
            /* pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
            pts  = (bits & 0x0E)   << 29;
            pts |=  *buf++         << 22;
            pts |= (*buf++ & 0xFE) << 14;
            pts |=  *buf++         <<  7;
            pts |= (*buf++ & 0xFE) >>  1;

            /* sync:4 ! pts:3 ! 1 ! pts:15 ! 1 | pts:15 ! 1 */
            dts  = (*buf++ & 0x0E) << 29;
            dts |=  *buf++         << 22;
            dts |= (*buf++ & 0xFE) << 14;
            dts |=  *buf++         <<  7;
            dts |= (*buf++ & 0xFE) >>  1;

            GST_DEBUG (0, "PTS = %" G_GUINT64_FORMAT ", DTS = %" G_GUINT64_FORMAT, pts, dts);
            headerlen += 10;
	    goto done;
	  case 0x00:
            GST_DEBUG (0, "have no pts/dts");
            GST_DEBUG (0, "got trailer bits %x", (bits & 0x0f));
            if ((bits & 0x0f) != 0xf) {
              GST_DEBUG (0, "not a valid packet time sequence");
    	      return FALSE;
            }
            headerlen++;
          default:
	    goto done;
	}
      default:
	goto done;
    } 
  } while (1);
  GST_DEBUG (0, "done with header loop");

done:

  /* calculate the amount of real data in this packet */
  datalen = packet_length - headerlen+2;
  GST_DEBUG (0, "headerlen is %d, datalen is %d",
        headerlen,datalen);

  if (id == 0xBD) {
    /* private_stream_1 */
    /* first find the track code */
    ps_id_code = *(basebuf + headerlen);

    if (ps_id_code >= 0x80 && ps_id_code <= 0x87) {
      /* make sure it's valid */
      GST_DEBUG (0, "0x%02X: we have a private_stream_1 (AC3) packet, track %d",
                 id, ps_id_code - 0x80);
      outstream = &mpeg_demux->private_1_stream[ps_id_code - 0x80];
      /* scrap first 4 bytes (so-called "mystery AC3 tag") */
      headerlen += 4;
      datalen -= 4;
    }
  } else if (id == 0xBF) {
    /* private_stream_2 */
    GST_DEBUG (0, "0x%02X: we have a private_stream_2 packet", id);
    outstream = &mpeg_demux->private_2_stream;
  } else if (id >= 0xC0 && id <= 0xDF) {
    /* audio */
    GST_DEBUG (0, "0x%02X: we have an audio packet", id);
    outstream = &mpeg_demux->audio_stream[id & 0x1F];
  } else if (id >= 0xE0 && id <= 0xEF) {
    /* video */
    GST_DEBUG (0, "0x%02X: we have a video packet", id);
    outstream = &mpeg_demux->video_stream[id & 0x0F];
  }

  /* if we don't know what it is, bail */
  if (outstream == NULL) {
    GST_DEBUG (0, "unknown packet id 0x%02X !!", id);
    return FALSE;
  }

  outpad = (*outstream)->pad;

  /* the pad should have been created in parse_syshead */
  if (outpad == NULL) {
    GST_DEBUG (0, "unexpected packet id 0x%02X!!", id);
    return FALSE;
  }

  /* attach pts, if any */
  if (pts != -1) {
    pts += mpeg_parse->adjust;
    timestamp = MPEGTIME_TO_GSTTIME (pts);

    if (mpeg_demux->index) {
      gst_index_add_association (mpeg_demux->index, 
                                 (*outstream)->index_id, 0,
				 GST_FORMAT_BYTES, GST_BUFFER_OFFSET (buffer),
				 GST_FORMAT_TIME,  timestamp,
				 0);
    }
  }
  else {
    timestamp = GST_CLOCK_TIME_NONE;
  }

  /* create the buffer and send it off to the Other Side */
  if (GST_PAD_IS_LINKED (outpad) && datalen > 0) {
    GST_DEBUG (0, "creating subbuffer len %d", datalen);

    /* if this is part of the buffer, create a subbuffer */
    outbuf = gst_buffer_create_sub (buffer, headerlen + 4, datalen);

    GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
    GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buffer) + headerlen + 4;

    GST_DEBUG (0, "pushing buffer of len %d id %d, ts %" G_GINT64_FORMAT, 
		    datalen, id, GST_BUFFER_TIMESTAMP (outbuf));

    gst_pad_push (outpad, outbuf);
  }

  return TRUE;
}

static gboolean
gst_mpeg_demux_parse_pes (GstMPEGParse *mpeg_parse, GstBuffer *buffer)
{
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (mpeg_parse);
  guint8 id;
  gint64 pts = -1;

  guint16 packet_length;
  guint8 header_data_length = 0;

  guint16 datalen;
  guint16 headerlen;
  guint8 ps_id_code = 0x80;

  GstMPEGStream **outstream = NULL;
  GstPad **outpad = NULL;
  GstBuffer *outbuf;
  GstPadTemplate *newtemp = NULL;
  guint8 *buf, *basebuf;

  GST_DEBUG (0, "in parse_pes");

  basebuf = buf = GST_BUFFER_DATA (buffer);
  id = *(buf+3);
  buf += 4;

  /* start parsing */
  packet_length = GUINT16_FROM_BE (*((guint16 *)buf));

  GST_DEBUG (0, "got packet_length %d", packet_length);
  buf += 2;

  /* we don't operate on: program_stream_map, padding_stream, */
  /* private_stream_2, ECM, EMM, or program_stream_directory  */
  if ((id != 0xBC) && (id != 0xBE) && (id != 0xBF) && (id != 0xF0) &&
      (id != 0xF1) && (id != 0xFF)) 
  {
    guchar flags1 = *buf++;
    guchar flags2 = *buf++;

    if ((flags1 & 0xC0) != 0x80) {
      return FALSE;
    }

    header_data_length = *buf++;

    GST_DEBUG (0, "header_data_length is %d",header_data_length);

    /* check for PTS */
    if ((flags2 & 0x80)) {
    /*if ((flags2 & 0x80) && id == 0xe0) { */
      pts  = (*buf++ & 0x0E) << 29;
      pts |=  *buf++         << 22;
      pts |= (*buf++ & 0xFE) << 14;
      pts |=  *buf++         <<  7;
      pts |= (*buf++ & 0xFE) >>  1;

      GST_DEBUG (0, "%x PTS = %" G_GUINT64_FORMAT, 
		      id, MPEGTIME_TO_GSTTIME (pts));

    }
    if ((flags2 & 0x40)) {
      GST_DEBUG (0, "%x DTS found", id);
      buf += 5;
    }
    if ((flags2 & 0x20)) {
      GST_DEBUG (0, "%x ESCR found", id);
      buf += 6;
    }
    if ((flags2 & 0x10)) {
      guint32 es_rate;

      es_rate  = (*buf++ & 0x07) << 14;
      es_rate |= (*buf++       ) << 7;
      es_rate |= (*buf++ & 0xFE) >> 1;
      GST_DEBUG (0, "%x ES Rate found", id);
    }
    /* FIXME: lots of PES parsing missing here... */

  }

  /* calculate the amount of real data in this PES packet */
  /* constant is 2 bytes packet_length, 2 bytes of bits, 1 byte header len */
  headerlen = 5 + header_data_length;
  /* constant is 2 bytes of bits, 1 byte header len */
  datalen = packet_length - (3 + header_data_length);
  GST_DEBUG (0, "headerlen is %d, datalen is %d",
        headerlen, datalen);

  if (id == 0xBD) {
    /* private_stream_1 */
    /* first find the track code */
    ps_id_code = *(basebuf + headerlen + 4);

    if (ps_id_code >= 0x80 && ps_id_code <= 0x87) {
      GST_DEBUG (0, "we have a private_stream_1 (AC3) packet, track %d",
                 ps_id_code - 0x80);
      outstream = &mpeg_demux->private_1_stream[ps_id_code - 0x80];
      /* scrap first 4 bytes (so-called "mystery AC3 tag") */
      headerlen += 4;
      datalen -= 4;
    } else if (ps_id_code >= 0xA0 && ps_id_code <= 0xA7) {
      GST_DEBUG (0, "we have a pcm_stream packet, track %d",
                 ps_id_code - 0xA0);
      outstream = &mpeg_demux->pcm_stream[ps_id_code - 0xA0];

      /* Check for changes in the sample format. */
      if (*outstream != NULL &&
          basebuf[headerlen + 9] !=
            mpeg_demux->lpcm_sample_info[ps_id_code - 0xA0]) {
        /* Change the pad caps.*/
        gst_mpeg_demux_lpcm_set_caps((*outstream)->pad, basebuf[headerlen + 9]);
      }

      /* Store the sample info. */
      mpeg_demux->lpcm_sample_info[ps_id_code - 0xA0] =
        basebuf[headerlen + 9];

      /* Get rid of the LPCM header. */
      headerlen += 7;
      datalen -= 7;
    } else if (ps_id_code >= 0x20 && ps_id_code <= 0x2F) {
      GST_DEBUG (0, "we have a subtitle_stream packet, track %d",
                 ps_id_code - 0x20);
      outstream = &mpeg_demux->subtitle_stream[ps_id_code - 0x20];
      headerlen += 1;
      datalen -= 1;
    } else {
      GST_DEBUG (0, "0x%02X: unkonwn id %x",
                 id, ps_id_code);
    }
  } else if (id == 0xBF) {
    /* private_stream_2 */
    GST_DEBUG (0, "we have a private_stream_2 packet");
    outstream = &mpeg_demux->private_2_stream;
  } else if (id >= 0xC0 && id <= 0xDF) {
    /* audio */
    GST_DEBUG (0, "we have an audio packet");
    outstream = &mpeg_demux->audio_stream[id - 0xC0];
  } else if (id >= 0xE0 && id <= 0xEF) {
    /* video */
    GST_DEBUG (0, "we have a video packet");
    outstream = &mpeg_demux->video_stream[id - 0xE0];
  } else {
    GST_DEBUG (0, "we have a unkown packet");
  }

  /* if we don't know what it is, bail */
  if (outstream == NULL)
    return TRUE;

  /* create the pad and add it if we don't already have one. 		*/
  /* this should trigger the NEW_PAD signal, which should be caught by 	*/
  /* the app and used to attach to desired streams.			*/
  if ((*outstream) == NULL) {
    gchar *name = NULL;

    /* we have to name the stream approriately */
    if (id == 0xBD) {
      /* private_stream_1 */
      if (ps_id_code >= 0x80 && ps_id_code <= 0x87) {
        /* Erase any DVD audio pads. */
        gst_mpeg_demux_dvd_audio_clear (mpeg_demux, ps_id_code - 0x80);

        name = g_strdup_printf ("private_stream_1_%d",ps_id_code - 0x80);
	newtemp = GST_PAD_TEMPLATE_GET (private1_factory);
      } else if (ps_id_code >= 0xA0 && ps_id_code <= 0xA7) {
        /* Erase any DVD audio pads. */
        gst_mpeg_demux_dvd_audio_clear (mpeg_demux, ps_id_code - 0xA0);

        name = g_strdup_printf ("pcm_stream_%d", ps_id_code - 0xA0);
	newtemp = GST_PAD_TEMPLATE_GET (pcm_factory);
      } else if (ps_id_code >= 0x20 && ps_id_code <= 0x2F) {
        name = g_strdup_printf ("subtitle_stream_%d",ps_id_code - 0x20);
        newtemp = GST_PAD_TEMPLATE_GET (subtitle_factory);
      } else {
        name = g_strdup_printf ("unknown_stream_%d",ps_id_code);
      }
    } else if (id == 0xBF) {
      /* private_stream_2 */
      name = g_strdup ("private_stream_2");
      newtemp = GST_PAD_TEMPLATE_GET (private2_factory);
    } else if (id >= 0xC0 && id <= 0xDF) {
      /* audio */
      name = g_strdup_printf ("audio_%02d", id - 0xC0);
      newtemp = GST_PAD_TEMPLATE_GET (audio_factory);
    } else if (id >= 0xE0 && id <= 0xEF) {
      /* video */
      name = g_strdup_printf ("video_%02d", id - 0xE0);
      newtemp = GST_PAD_TEMPLATE_GET (video_mpeg2_factory);
    } else {
      /* unkown */
      name = g_strdup_printf ("unknown");
    }
    
    if (newtemp) {
      GstCaps *caps;

      *outstream = gst_mpeg_demux_new_stream ();
      outpad = &((*outstream)->pad);

      /* create the pad and add it to self */
      *outpad = gst_pad_new_from_template (newtemp, name);
      if (ps_id_code < 0xA0 || ps_id_code > 0xA7) {
        caps = gst_pad_template_get_caps (newtemp);
        gst_pad_try_set_caps (*outpad, caps);
        gst_caps_unref (caps);
      }
      else {
        gst_mpeg_demux_lpcm_set_caps(*outpad,
                                     mpeg_demux->lpcm_sample_info[ps_id_code
                                                                  - 0xA0]);
      }

      gst_pad_set_formats_function (*outpad, gst_mpeg_demux_get_src_formats);
      gst_pad_set_convert_function (*outpad, gst_mpeg_parse_convert_src);
      gst_pad_set_event_mask_function (*outpad, gst_mpeg_parse_get_src_event_masks);
      gst_pad_set_event_function (*outpad, gst_mpeg_demux_handle_src_event);
      gst_pad_set_query_type_function (*outpad, gst_mpeg_parse_get_src_query_types);
      gst_pad_set_query_function (*outpad, gst_mpeg_parse_handle_src_query);

      gst_element_add_pad(GST_ELEMENT(mpeg_demux), *outpad);
      gst_pad_set_element_private (*outpad, *outstream);

      if (mpeg_demux->index) {
        gst_index_get_writer_id (mpeg_demux->index, GST_OBJECT (*outpad),
                                 &(*outstream)->index_id);
      }
    }
    else {
      g_warning ("cannot create pad %s, no template for %02x", name, id);
    }
    if (name)
      g_free (name);
  }

  if (*outstream) {
    gint64 timestamp;

    outpad = &((*outstream)->pad);

    /* attach pts, if any */
    if (pts != -1) {
      pts += mpeg_parse->adjust;
      timestamp = MPEGTIME_TO_GSTTIME (pts);

      if (mpeg_demux->index) {
        gst_index_add_association (mpeg_demux->index, 
                                   (*outstream)->index_id, 0,
				   GST_FORMAT_BYTES, GST_BUFFER_OFFSET (buffer),
				   GST_FORMAT_TIME,  timestamp,
				   0);
      }
    }
    else {
      timestamp = GST_CLOCK_TIME_NONE;
    }

    /* create the buffer and send it off to the Other Side */
    if (*outpad && GST_PAD_IS_USABLE(*outpad)) {
      /* if this is part of the buffer, create a subbuffer */
      GST_DEBUG (0,"creating subbuffer len %d", datalen);

      outbuf = gst_buffer_create_sub (buffer, headerlen+4, datalen);

      GST_BUFFER_TIMESTAMP (outbuf) = timestamp;
      GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buffer) + headerlen + 4;

      gst_pad_push(*outpad,outbuf);
    }
  }

  return TRUE;
}

/**
 * Set the capabilities of the given pad based on the provided LPCM
 * sample information.
 */
static void
gst_mpeg_demux_lpcm_set_caps (GstPad *pad, guint8 sample_info)
{
  gint width, rate, channels;
  GstCaps *caps;

  /* Determine the sample width. */
  switch (sample_info & 0xC0) {
  case 0x80:
    width = 24;
    break;
  case 0x40:
    width = 20;
    break;
  default:
    width = 16;
    break;
  }

  /* Determine the rate. */
  if (sample_info & 0x10) {
    rate = 96000;
  }
  else {
    rate = 48000;
  }

  /* Determine the number of channels. */
  channels = (sample_info & 0x7) + 1;

  caps = GST_CAPS_NEW (
          "mpeg_demux_pcm",
          "audio/raw",
            "format",            GST_PROPS_STRING ("int"),
             "law",              GST_PROPS_INT (0),
             "endianness",       GST_PROPS_INT (G_BIG_ENDIAN),
             "signed",           GST_PROPS_BOOLEAN (TRUE),
             "width",            GST_PROPS_INT (width),
             "depth",            GST_PROPS_INT (width),
             "rate",             GST_PROPS_INT (rate),
             "channels",         GST_PROPS_INT (channels)
	  );
  gst_pad_try_set_caps (pad, caps);
}

/**
 * Erase the DVD audio pad (if any) associated to the given channel.
 */
static void
gst_mpeg_demux_dvd_audio_clear (GstMPEGDemux *mpeg_demux, int channel)
{
  GstMPEGStream **stream = NULL;

  if (mpeg_demux->private_1_stream[channel] != NULL) {
    stream = &mpeg_demux->private_1_stream[channel];
  }
  else if (mpeg_demux->pcm_stream[channel] != NULL) {
    stream = &mpeg_demux->pcm_stream[channel];
  }

  if (stream == NULL) {
    return;
  }

  gst_pad_unlink ((*stream)->pad, gst_pad_get_peer((*stream)->pad));
  gst_element_remove_pad (GST_ELEMENT (mpeg_demux), (*stream)->pad);

  g_free (*stream);
  *stream = NULL;
}


const GstFormat*
gst_mpeg_demux_get_src_formats (GstPad *pad)
{ 
  static const GstFormat formats[] = {
    GST_FORMAT_TIME,		/* we prefer seeking on time */
    GST_FORMAT_BYTES,
    0 
  };
  return formats;
}   

static gboolean
index_seek (GstPad *pad, GstEvent *event, gint64 *offset)
{
  GstIndexEntry *entry;
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (gst_pad_get_parent (pad));
  GstMPEGStream *stream = gst_pad_get_element_private (pad);

  entry = gst_index_get_assoc_entry (mpeg_demux->index, stream->index_id,
                                     GST_INDEX_LOOKUP_BEFORE, 0,
                                     GST_EVENT_SEEK_FORMAT (event),
                                     GST_EVENT_SEEK_OFFSET (event));
  if (!entry)
    return FALSE;

  if (gst_index_entry_assoc_map (entry, GST_FORMAT_BYTES, offset)) {
    return TRUE;
  }
  return FALSE;
}

static gboolean
normal_seek (GstPad *pad, GstEvent *event, gint64 *offset)
{
  gboolean res = FALSE;
  gint64 adjust;
  GstFormat format;
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (gst_pad_get_parent (pad));

  format = GST_EVENT_SEEK_FORMAT (event);

  res = gst_pad_convert (pad, GST_FORMAT_BYTES, mpeg_demux->total_size_bound,
		         &format, &adjust);

  GST_DEBUG (0, "seek adjusted from %" G_GINT64_FORMAT " bytes to %" G_GINT64_FORMAT "\n", mpeg_demux->total_size_bound, adjust);

  if (res) 
    *offset = MAX (GST_EVENT_SEEK_OFFSET (event) - adjust, 0);

   return res;
}

static gboolean
gst_mpeg_demux_handle_src_event (GstPad *pad, GstEvent *event)
{   
  gboolean res = FALSE;
  GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      guint64 desired_offset;

      if (mpeg_demux->index) 
	res = index_seek (pad, event, &desired_offset);
      if (!res)
	res = normal_seek (pad, event, &desired_offset);

      if (res) {
        GstEvent *new_event;

        new_event = gst_event_new_seek (GST_EVENT_SEEK_TYPE (event), desired_offset);
        gst_event_unref (event);
        res = gst_mpeg_parse_handle_src_event (pad, new_event);
      }
      break;
    }
    default:
      break;
  } 
  return res;
}

static GstElementStateReturn
gst_mpeg_demux_change_state (GstElement *element)
{ 
  /* GstMPEGDemux *mpeg_demux = GST_MPEG_DEMUX (element); */
	    
  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_READY_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
}

static void
gst_mpeg_demux_set_index (GstElement *element, GstIndex *index)
{
  GstMPEGDemux *mpeg_demux;

  GST_ELEMENT_CLASS (parent_class)->set_index (element, index);

  mpeg_demux = GST_MPEG_DEMUX (element);

  mpeg_demux->index = index;
}

static GstIndex*
gst_mpeg_demux_get_index (GstElement *element)
{
  GstMPEGDemux *mpeg_demux;

  mpeg_demux = GST_MPEG_DEMUX (element);

  return mpeg_demux->index;
}


gboolean
gst_mpeg_demux_plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* this filter needs the bytestream package */
  if (!gst_library_load ("gstbytestream"))
    return FALSE;

  /* create an elementfactory for the mpeg_demux element */
  factory = gst_element_factory_new ("mpegdemux", GST_TYPE_MPEG_DEMUX,
                                     &mpeg_demux_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (video_mpeg1_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (video_mpeg2_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (private1_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (private2_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (pcm_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (subtitle_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (audio_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}
