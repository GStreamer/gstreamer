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

#include <inttypes.h>

#include <mpeg2dec/mm_accel.h>
#include <mpeg2dec/video_out.h>
#include "gstmpeg2dec.h"

/* elementfactory information */
static GstElementDetails gst_mpeg2dec_details = {
  "mpeg1 and mpeg2 video decoder",
  "Codec/Video/Decoder",
  "Uses libmpeg2 to decode MPEG video streams",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>, "
  "(C) 2002",
};

/* Mpeg2dec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_FRAME_RATE,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg2dec_src",
    "video/raw",
      "format",    GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
      "width",     GST_PROPS_INT_RANGE (16, 4096),
      "height",    GST_PROPS_INT_RANGE (16, 4096)
  )
);

GST_PAD_TEMPLATE_FACTORY (sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg2dec_sink",
    "video/mpeg",
      "mpegversion",  GST_PROPS_INT_RANGE (1, 2),
      "systemstream", GST_PROPS_BOOLEAN (FALSE)
  )
);

static void	gst_mpeg2dec_class_init		(GstMpeg2decClass *klass);
static void	gst_mpeg2dec_init		(GstMpeg2dec *mpeg2dec);

static void	gst_mpeg2dec_dispose		(GObject *object);

static void	gst_mpeg2dec_set_property	(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_mpeg2dec_get_property	(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static gboolean gst_mpeg2dec_src_event       	(GstPad *pad, GstEvent *event);
static gboolean gst_mpeg2dec_src_query 		(GstPad *pad, GstPadQueryType type,
		       				 GstFormat *format, gint64 *value);

static gboolean gst_mpeg2dec_convert_sink 	(GstPad *pad, GstFormat src_format, gint64 src_value,
		         			 GstFormat *dest_format, gint64 *dest_value);
static gboolean gst_mpeg2dec_convert_src 	(GstPad *pad, GstFormat src_format, gint64 src_value,
		        	 		 GstFormat *dest_format, gint64 *dest_value);

static GstElementStateReturn
		gst_mpeg2dec_change_state	(GstElement *element);

static void	gst_mpeg2dec_chain		(GstPad *pad, GstBuffer *buffer);

static GstElementClass *parent_class = NULL;
/*static guint gst_mpeg2dec_signals[LAST_SIGNAL] = { 0 };*/

GType
gst_mpeg2dec_get_type (void)
{
  static GType mpeg2dec_type = 0;

  if (!mpeg2dec_type) {
    static const GTypeInfo mpeg2dec_info = {
      sizeof(GstMpeg2decClass),      
      NULL,
      NULL,
      (GClassInitFunc)gst_mpeg2dec_class_init,
      NULL,
      NULL,
      sizeof(GstMpeg2dec),
      0,
      (GInstanceInitFunc)gst_mpeg2dec_init,
    };
    mpeg2dec_type = g_type_register_static(GST_TYPE_ELEMENT, "GstMpeg2dec", &mpeg2dec_info, 0);
  }
  return mpeg2dec_type;
}

static void
gst_mpeg2dec_class_init(GstMpeg2decClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FRAME_RATE,
    g_param_spec_float ("frame_rate","frame_rate","frame_rate",
                        0.0, 1000.0, 0.0, G_PARAM_READABLE)); 
  
  gobject_class->set_property 	= gst_mpeg2dec_set_property;
  gobject_class->get_property 	= gst_mpeg2dec_get_property;
  gobject_class->dispose 	= gst_mpeg2dec_dispose;

  gstelement_class->change_state = gst_mpeg2dec_change_state;
}

static void
gst_mpeg2dec_init (GstMpeg2dec *mpeg2dec)
{
  /* create the sink and src pads */
  mpeg2dec->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->sinkpad);
  gst_pad_set_chain_function (mpeg2dec->sinkpad, gst_mpeg2dec_chain);
  gst_pad_set_convert_function (mpeg2dec->sinkpad, gst_mpeg2dec_convert_sink);

  mpeg2dec->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->srcpad);
  gst_pad_set_event_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_event));
  gst_pad_set_query_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_query));
  gst_pad_set_convert_function (mpeg2dec->srcpad, gst_mpeg2dec_convert_src);

  /* initialize the mpeg2dec decoder state */
  mpeg2dec->decoder = mpeg2_init (mm_accel());

  GST_FLAG_SET (GST_ELEMENT (mpeg2dec), GST_ELEMENT_EVENT_AWARE);
}

static void
gst_mpeg2dec_dispose (GObject *object)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (object);

  mpeg2_close (mpeg2dec->decoder);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_mpeg2dec_alloc_buffer (GstMpeg2dec *mpeg2dec, mpeg2_info_t *info)
{
  GstBuffer *outbuf = NULL;
  gint size = mpeg2dec->width * mpeg2dec->height;
  guint8 *buf[3], *out;

  if (mpeg2dec->peerpool) {
    outbuf = gst_buffer_new_from_pool (mpeg2dec->peerpool, 0, 0);
  }
  if (!outbuf) {
    outbuf = gst_buffer_new ();

    GST_BUFFER_SIZE (outbuf) = (size * 3) / 2;
    GST_BUFFER_DATA (outbuf) = g_malloc0 ((size * 3)/2);
  }

  out = GST_BUFFER_DATA (outbuf);

  buf[0] = out;
  buf[1] = buf[0] + size;
  buf[2] = buf[1] + size/4;

  gst_buffer_ref (outbuf);
  mpeg2_set_buf (mpeg2dec->decoder, buf, outbuf);

  if (info->current_picture && (info->current_picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I)
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);
  else
    GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_KEY_UNIT);

  return TRUE;
}

static void
gst_mpeg2dec_chain (GstPad *pad, GstBuffer *buf)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));
  guint32 size;
  guint8 *data, *end;
  gint64 pts;
  mpeg2_info_t *info;
  gint state;
  gboolean done = FALSE;

  GST_DEBUG (0, "MPEG2DEC: chain called");

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
	//gint64 value = GST_EVENT_DISCONT_OFFSET (event, 0).value;
	//mpeg2dec->decoder->is_sequence_needed = 1;
	GST_DEBUG (GST_CAT_EVENT, "mpeg2dec: discont\n"); 
        mpeg2dec->first = TRUE;
        mpeg2dec->frames_per_PTS = 0;
        mpeg2dec->last_PTS = -1;
        mpeg2dec->adjust = 0;
        mpeg2dec->next_time = 0;
        mpeg2dec->discont_pending = TRUE;
        gst_pad_event_default (pad, event);
	return;
      }
      case GST_EVENT_EOS:
        if (!mpeg2dec->closed) {
          mpeg2_close (mpeg2dec->decoder); 
	  mpeg2dec->closed = TRUE;
        }
      default:
        gst_pad_event_default (pad, event);
	return;
    }
  }

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);
  pts = GST_BUFFER_TIMESTAMP (buf);

  GST_DEBUG (GST_CAT_CLOCK, "mpeg2dec: pts %llu\n", pts);

  info = mpeg2_info (mpeg2dec->decoder);
  end = data + size;

  while (!done) {
    state = mpeg2_buffer (mpeg2dec->decoder, &data, end);
    switch (state) {
      case STATE_SEQUENCE:
      {
	mpeg2dec->width = info->sequence->width;
	mpeg2dec->height = info->sequence->height;
	mpeg2dec->total_frames = 0;

        gst_pad_try_set_caps (mpeg2dec->srcpad,
                    GST_CAPS_NEW (
                      "mpeg2dec_caps",
                      "video/raw",
                        "format",   GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
                          "width",  GST_PROPS_INT (mpeg2dec->width),
                          "height", GST_PROPS_INT (mpeg2dec->height)
                    ));

	gst_mpeg2dec_alloc_buffer (mpeg2dec, info);
        break;
      }
      case STATE_GOP:
        break;
      case STATE_PICTURE:
	gst_mpeg2dec_alloc_buffer (mpeg2dec, info);
        if (info->current_picture && (info->current_picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I && 
			mpeg2dec->discont_pending) {
	  mpeg2dec->discont_pending = FALSE;
	  mpeg2dec->first = TRUE;
          if (pts != -1 && mpeg2dec->last_PTS == -1) {
            mpeg2dec->last_PTS = pts;
            mpeg2dec->next_time = pts;
          }
	}
        break;
      case STATE_SLICE_1ST:
        break;
      case STATE_PICTURE_2ND:
        break;
      case STATE_SLICE:
      case STATE_END:
      {
	GstBuffer *outbuf = NULL;

	if (info->display_fbuf) {
	  outbuf = (GstBuffer *) info->display_fbuf->id;

          GST_BUFFER_TIMESTAMP (outbuf) = mpeg2dec->next_time;
          mpeg2dec->next_time += info->sequence->frame_period * GST_USECOND / 27;

	  if (mpeg2dec->discont_pending ||
	      (mpeg2dec->first && !GST_BUFFER_FLAG_IS_SET (outbuf, GST_BUFFER_KEY_UNIT))) {
	    gst_buffer_unref (outbuf);
	  }
	  else {
            mpeg2dec->first = FALSE;
	    gst_pad_push (mpeg2dec->srcpad, outbuf);
	  }
	}
	if (info->discard_fbuf) {
	  gst_buffer_unref ((GstBuffer *)info->discard_fbuf->id);
	}

        break;
      }
      /* need more data */
      case -1:
	done = TRUE;
	break;
      /* error */
      default:
      case STATE_INVALID:
	gst_element_error (GST_ELEMENT (mpeg2dec), "fatal error");
	done = TRUE;
        break;
    }
  }
  gst_buffer_unref(buf);
}

static gboolean
gst_mpeg2dec_convert_sink (GstPad *pad, GstFormat src_format, gint64 src_value,
		           GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  mpeg2_info_t *info;
	      
  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  info = mpeg2_info (mpeg2dec->decoder);

  switch (src_format) {
    case GST_FORMAT_BYTES:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
	  if (info->sequence && info->sequence->byte_rate) {
            *dest_value = GST_SECOND * src_value / info->sequence->byte_rate;
            break;
	  }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_TIME:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_BYTES;
        case GST_FORMAT_BYTES:
	  if (info->sequence && info->sequence->byte_rate) {
            *dest_value = src_value * info->sequence->byte_rate / GST_SECOND;
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
gst_mpeg2dec_convert_src (GstPad *pad, GstFormat src_format, gint64 src_value,
		          GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  mpeg2_info_t *info;
  guint64 scale = 1;
	      
  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  info = mpeg2_info (mpeg2dec->decoder);

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
	  scale = 6 * (mpeg2dec->width * mpeg2dec->height >> 2);
        case GST_FORMAT_UNITS:
	  if (info->sequence && info->sequence->frame_period) {
            *dest_value = src_value * scale * 27 / (info->sequence->frame_period * GST_USECOND);
            break;
	  }
        default:
          res = FALSE;
      }
      break;
    case GST_FORMAT_UNITS:
      switch (*dest_format) {
        case GST_FORMAT_DEFAULT:
          *dest_format = GST_FORMAT_TIME;
        case GST_FORMAT_TIME:
          *dest_value = src_value * info->sequence->frame_period * GST_USECOND / 27;
	  break;
        case GST_FORMAT_BYTES:
	  *dest_value = src_value * 6 * ((mpeg2dec->width * mpeg2dec->height) >> 2);
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
gst_mpeg2dec_src_query (GstPad *pad, GstPadQueryType type,
		        GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  static const GstFormat formats[] = { GST_FORMAT_TIME, GST_FORMAT_BYTES };
#define MAX_SEEK_FORMATS 1 /* we can only do time seeking for now */
  gint i;

  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_PAD_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        case GST_FORMAT_TIME:
        case GST_FORMAT_BYTES:
        case GST_FORMAT_UNITS:
	{
          res = FALSE;

          for (i = 0; i < MAX_SEEK_FORMATS && !res; i++) {
	    GstFormat peer_format;
	    gint64 peer_value;
		  
	    peer_format = formats[i];
	  
            /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (mpeg2dec->sinkpad), GST_PAD_QUERY_TOTAL,
			       &peer_format, &peer_value)) 
	    {
              GstFormat conv_format;

              /* convert to TIME */
              conv_format = GST_FORMAT_TIME;
              res = gst_pad_convert (mpeg2dec->sinkpad,
                              peer_format, peer_value,
                              &conv_format, value);
              /* and to final format */
              res &= gst_pad_convert (pad,
                         GST_FORMAT_TIME, *value,
                         format, value);
            }
	  }
          break;
	}
        default:
	  res = FALSE;
          break;
      }
      break;
    }
    case GST_PAD_QUERY_POSITION:
    {
      switch (*format) {
        case GST_FORMAT_DEFAULT:
          *format = GST_FORMAT_TIME;
          /* fallthrough */
        default:
          res = gst_pad_convert (pad,
                          GST_FORMAT_TIME, mpeg2dec->next_time,
                          format, value);
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  return res;
}

static gboolean 
gst_mpeg2dec_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  static const GstFormat formats[] = { GST_FORMAT_TIME, GST_FORMAT_BYTES };
#define MAX_SEEK_FORMATS 1 /* we can only do time seeking for now */
  gint i;

  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
    /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      gint64 src_offset;
      gboolean flush;
      GstFormat format;
			                
      format = GST_FORMAT_TIME;

      /* first bring the src_format to TIME */
      if (!gst_pad_convert (pad,
                GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event),
                &format, &src_offset))
      {
        /* didn't work, probably unsupported seek format then */
        res = FALSE;
        break;
      }

      /* shave off the flush flag, we'll need it later */
      flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

      /* assume the worst */
      res = FALSE;

      /* while we did not exhaust our seek formats without result */
      for (i = 0; i < MAX_SEEK_FORMATS && !res; i++) {
        gint64 desired_offset;

        format = formats[i];

        /* try to convert requested format to one we can seek with on the sinkpad */
        if (gst_pad_convert (mpeg2dec->sinkpad, GST_FORMAT_TIME, src_offset, &format, &desired_offset))
        {
          GstEvent *seek_event;

          /* conversion succeeded, create the seek */
          seek_event = gst_event_new_seek (formats[i] | GST_SEEK_METHOD_SET | flush, desired_offset);
          /* do the seekk */
          if (gst_pad_send_event (GST_PAD_PEER (mpeg2dec->sinkpad), seek_event)) {
            /* seek worked, we're done, loop will exit */
            res = TRUE;
          }
          gst_event_free (seek_event);
        }
        /* at this point, either the seek worked or res == FALSE */
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }
  return res;
}

static GstElementStateReturn
gst_mpeg2dec_change_state (GstElement *element)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  switch (GST_STATE_TRANSITION (element)) { 
    case GST_STATE_NULL_TO_READY:
      break;
    case GST_STATE_READY_TO_PAUSED:
    {
      mpeg2dec->next_time = 0;
      mpeg2dec->peerpool = NULL;
      mpeg2dec->closed = FALSE;

      /* reset the initial video state */
      mpeg2dec->format = -1;
      mpeg2dec->width = -1;
      mpeg2dec->height = -1;
      mpeg2dec->first = TRUE;
      mpeg2dec->frames_per_PTS = 0;
      mpeg2dec->last_PTS = -1;
      mpeg2dec->adjust = 0;
      mpeg2dec->discont_pending = TRUE;
      break;
    }
    case GST_STATE_PAUSED_TO_PLAYING:
      /* try to get a bufferpool */
      mpeg2dec->peerpool = gst_pad_get_bufferpool (mpeg2dec->srcpad);
      if (mpeg2dec->peerpool)
        GST_INFO (GST_CAT_PLUGIN_INFO, "got pool %p", mpeg2dec->peerpool);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      /* need to clear things we get from other plugins, since we could be reconnected */
      if (mpeg2dec->peerpool) {
	mpeg2dec->peerpool = NULL;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      /* if we are not closed by an EOS event do so now, this cen send a few frames but
       * we are prepared to not really send them (see above) */
      if (!mpeg2dec->closed) {
        /*mpeg2_close (mpeg2dec->decoder); */
	mpeg2dec->closed = TRUE;
      }
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static void
gst_mpeg2dec_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstMpeg2dec *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MPEG2DEC (object));
  src = GST_MPEG2DEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_mpeg2dec_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstMpeg2dec *mpeg2dec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_MPEG2DEC (object));
  mpeg2dec = GST_MPEG2DEC (object);

  switch (prop_id) {
    case ARG_FRAME_RATE:
      break;
    default:
      break;
  }
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the mpeg2dec element */
  factory = gst_element_factory_new("mpeg2dec",GST_TYPE_MPEG2DEC,
                                   &gst_mpeg2dec_details);
  g_return_val_if_fail(factory != NULL, FALSE);
  gst_element_factory_set_rank (factory, GST_ELEMENT_RANK_PRIMARY);

  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (src_template_factory));
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (sink_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mpeg2dec",
  plugin_init
};
