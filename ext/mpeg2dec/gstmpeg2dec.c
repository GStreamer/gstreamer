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

#include "gstmpeg2dec.h"

/* elementfactory information */
static GstElementDetails gst_mpeg2dec_details = {
  "mpeg1 and mpeg2 video decoder",
  "Codec/Video/Decoder",
  "GPL",
  "Uses libmpeg2 to decode MPEG video streams",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2002"
};

/* Mpeg2dec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_STREAMINFO,
  /* FILL ME */
};

GST_PAD_TEMPLATE_FACTORY (src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg2dec_src",
    "video/raw",
      "format",       GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y','V','1','2')),
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096),
      "pixel_width",  GST_PROPS_INT_RANGE (1, 255),
      "pixel_height", GST_PROPS_INT_RANGE (1, 255)
  ),
  GST_CAPS_NEW (
    "mpeg2dec_src",
    "video/raw",
      "format",       GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096),
      "pixel_width",  GST_PROPS_INT_RANGE (1, 255),
      "pixel_height", GST_PROPS_INT_RANGE (1, 255)
  )
);

GST_PAD_TEMPLATE_FACTORY (user_data_template_factory,
  "user_data",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "mpeg2dec_user_data",
    "application/octet-stream",
    NULL
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

static void		gst_mpeg2dec_class_init		(GstMpeg2decClass *klass);
static void		gst_mpeg2dec_init		(GstMpeg2dec *mpeg2dec);

static void		gst_mpeg2dec_dispose		(GObject *object);

static void		gst_mpeg2dec_set_property	(GObject *object, guint prop_id, 
							 const GValue *value, GParamSpec *pspec);
static void		gst_mpeg2dec_get_property	(GObject *object, guint prop_id, 
							 GValue *value, GParamSpec *pspec);
static void 		gst_mpeg2dec_set_index 		(GstElement *element, GstIndex *index);
static GstIndex*	gst_mpeg2dec_get_index 		(GstElement *element);

static const GstFormat*
			gst_mpeg2dec_get_src_formats 	(GstPad *pad);
static const GstEventMask*
			gst_mpeg2dec_get_src_event_masks (GstPad *pad);
static gboolean 	gst_mpeg2dec_src_event       	(GstPad *pad, GstEvent *event);
static const GstQueryType*
			gst_mpeg2dec_get_src_query_types (GstPad *pad);
static gboolean 	gst_mpeg2dec_src_query 		(GstPad *pad, GstQueryType type,
			       				 GstFormat *format, gint64 *value);

static const GstFormat*
			gst_mpeg2dec_get_sink_formats 	(GstPad *pad);
static gboolean 	gst_mpeg2dec_convert_sink 	(GstPad *pad, GstFormat src_format, gint64 src_value,
			         			 GstFormat *dest_format, gint64 *dest_value);
static gboolean 	gst_mpeg2dec_convert_src 	(GstPad *pad, GstFormat src_format, gint64 src_value,
			        	 		 GstFormat *dest_format, gint64 *dest_value);

static GstElementStateReturn
			gst_mpeg2dec_change_state	(GstElement *element);

static void		gst_mpeg2dec_chain		(GstPad *pad, GstBuffer *buffer);

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

  g_object_class_install_property (gobject_class, ARG_STREAMINFO,
    g_param_spec_boxed ("streaminfo", "Streaminfo", "Streaminfo",
                        GST_TYPE_CAPS, G_PARAM_READABLE));
  
  gobject_class->set_property 	= gst_mpeg2dec_set_property;
  gobject_class->get_property 	= gst_mpeg2dec_get_property;
  gobject_class->dispose 	= gst_mpeg2dec_dispose;

  gstelement_class->change_state = gst_mpeg2dec_change_state;
  gstelement_class->set_index 	 = gst_mpeg2dec_set_index;
  gstelement_class->get_index 	 = gst_mpeg2dec_get_index;
}

static void
gst_mpeg2dec_init (GstMpeg2dec *mpeg2dec)
{
  /* create the sink and src pads */
  mpeg2dec->sinkpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->sinkpad);
  gst_pad_set_chain_function (mpeg2dec->sinkpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_chain));
  gst_pad_set_formats_function (mpeg2dec->sinkpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_get_sink_formats));
  gst_pad_set_convert_function (mpeg2dec->sinkpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_convert_sink));

  mpeg2dec->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (src_template_factory), "src");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->srcpad);
  gst_pad_set_formats_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_get_src_formats));
  gst_pad_set_event_mask_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_get_src_event_masks));
  gst_pad_set_event_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_event));
  gst_pad_set_query_type_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_get_src_query_types));
  gst_pad_set_query_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_src_query));
  gst_pad_set_convert_function (mpeg2dec->srcpad, GST_DEBUG_FUNCPTR (gst_mpeg2dec_convert_src));

  mpeg2dec->userdatapad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (user_data_template_factory), "user_data");
  gst_element_add_pad (GST_ELEMENT (mpeg2dec), mpeg2dec->userdatapad);

  /* initialize the mpeg2dec acceleration */
  mpeg2_accel (MPEG2_ACCEL_DETECT);
  mpeg2dec->closed = TRUE;
  mpeg2dec->have_fbuf = FALSE;

  GST_FLAG_SET (GST_ELEMENT (mpeg2dec), GST_ELEMENT_EVENT_AWARE);
}

static void
gst_mpeg2dec_close_decoder (GstMpeg2dec *mpeg2dec)
{
  if (!mpeg2dec->closed) {
    mpeg2_close (mpeg2dec->decoder);
    mpeg2dec->closed = TRUE;
    mpeg2dec->decoder = NULL;
  }
}

static void
gst_mpeg2dec_open_decoder (GstMpeg2dec *mpeg2dec)
{
  gst_mpeg2dec_close_decoder (mpeg2dec);
  mpeg2dec->decoder = mpeg2_init ();
  mpeg2dec->closed = FALSE;
  mpeg2dec->have_fbuf = FALSE;
}

static void
gst_mpeg2dec_dispose (GObject *object)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (object);

  gst_mpeg2dec_close_decoder (mpeg2dec);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_mpeg2dec_set_index (GstElement *element, GstIndex *index)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);
  
  mpeg2dec->index = index;

  gst_index_get_writer_id (index, GST_OBJECT (element), &mpeg2dec->index_id);
}

static GstIndex*
gst_mpeg2dec_get_index (GstElement *element)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (element);

  return mpeg2dec->index;
}

static GstBuffer*
gst_mpeg2dec_alloc_buffer (GstMpeg2dec *mpeg2dec, const mpeg2_info_t *info, gint64 offset)
{
  GstBuffer *outbuf = NULL;
  gint size = mpeg2dec->width * mpeg2dec->height;
  guint8 *buf[3], *out;
  const picture_t *picture;

  if (mpeg2dec->peerpool) {
    outbuf = gst_buffer_new_from_pool (mpeg2dec->peerpool, 0, 0);
  }
  if (!outbuf) {
    outbuf = gst_buffer_new_and_alloc ((size * 3) / 2);
  }

  out = GST_BUFFER_DATA (outbuf);

  buf[0] = out;
  if (mpeg2dec->format == MPEG2DEC_FORMAT_I420) {
    buf[1] = buf[0] + size;
    buf[2] = buf[1] + size/4;
  }
  else {
    buf[2] = buf[0] + size;
    buf[1] = buf[2] + size/4;
  }

  gst_buffer_ref (outbuf);
  mpeg2_set_buf (mpeg2dec->decoder, buf, outbuf);

  picture = info->current_picture;

  if (picture && (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I) {
    GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);
  }
  else {
    GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_KEY_UNIT);
  }
  /* we store the original byteoffset of this picture in the stream here
   * because we need it for indexing */
  GST_BUFFER_OFFSET (outbuf) = offset;

  return outbuf;
}

static gboolean
gst_mpeg2dec_negotiate_format (GstMpeg2dec *mpeg2dec)
{
  GstCaps *allowed;
  GstCaps *intersect, *trylist, *head, *to_intersect;

  /* we what we are allowed to do */
  allowed = gst_pad_get_allowed_caps (mpeg2dec->srcpad);
  /* we could not get allowed caps */
  if (!allowed) {
    allowed = GST_CAPS_NEW (
                "mpeg2dec_negotiate",
                "video/raw",
               	  "format",        GST_PROPS_FOURCC (GST_STR_FOURCC ("I420"))
	      );
  }

  to_intersect = GST_CAPS_NEW (
                   "mpeg2dec_negotiate",
                      "video/raw",
                      "width",        GST_PROPS_INT (mpeg2dec->width),
 	              "height",       GST_PROPS_INT (mpeg2dec->height),
                      "pixel_width",  GST_PROPS_INT (mpeg2dec->pixel_width),
 	              "pixel_height", GST_PROPS_INT (mpeg2dec->pixel_height)
                 );

  /* try to fix our height */
  intersect = gst_caps_intersect (allowed, to_intersect);
  gst_caps_unref (allowed);
  gst_caps_unref (to_intersect);

  /* prepare for looping */
  head = trylist = gst_caps_normalize (intersect);
  gst_caps_unref (intersect);

  while (trylist) {
    GstCaps *to_try = gst_caps_copy_1 (trylist);

    /* try each format */
    if (gst_pad_try_set_caps (mpeg2dec->srcpad, to_try) > 0) {
      guint32 fourcc;

      /* it worked, try to find what it was again */
      gst_caps_get_fourcc_int (to_try, "format", &fourcc);
								      
      if (fourcc == GST_STR_FOURCC ("I420")) {
	mpeg2dec->format = MPEG2DEC_FORMAT_I420;
      }
      else {
	mpeg2dec->format = MPEG2DEC_FORMAT_YV12;
      }
      break;
    }

    trylist = trylist->next;
  }
  gst_caps_unref (head);

  /* oops list exhausted and nothing was found... */
  if (!trylist) {
    return FALSE;
  }
  return TRUE;
}

static void
update_streaminfo (GstMpeg2dec *mpeg2dec)
{
  GstCaps *caps;
  GstProps *props;
  GstPropsEntry *entry;
  const mpeg2_info_t *info;

  info = mpeg2_info (mpeg2dec->decoder);

  props = gst_props_empty_new ();

  entry = gst_props_entry_new ("framerate", GST_PROPS_FLOAT (GST_SECOND/(float)mpeg2dec->frame_period));
  gst_props_add_entry (props, entry);
  entry = gst_props_entry_new ("bitrate", GST_PROPS_INT (info->sequence->byte_rate * 8));
  gst_props_add_entry (props, entry);

  caps = gst_caps_new ("mpeg2dec_streaminfo",
                       "application/x-gst-streaminfo",
                        props);

  gst_caps_replace_sink (&mpeg2dec->streaminfo, caps);
  g_object_notify (G_OBJECT (mpeg2dec), "streaminfo");
}

static void
gst_mpeg2dec_flush_decoder (GstMpeg2dec *mpeg2dec)
{
  gint state;

  if (mpeg2dec->decoder) {
    const mpeg2_info_t *info = mpeg2_info (mpeg2dec->decoder);

    do {
      state = mpeg2_parse (mpeg2dec->decoder);
      if (state == STATE_END) {
	if (info->discard_fbuf && info->discard_fbuf->id) {
	  gst_buffer_unref ((GstBuffer *)info->discard_fbuf->id);
	}
      }
    } 
    while (state != -1);
  }
}

static void
gst_mpeg2dec_chain (GstPad *pad, GstBuffer *buf)
{
  GstMpeg2dec *mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));
  guint32 size;
  guint8 *data, *end;
  gint64 pts;
  const mpeg2_info_t *info;
  gint state;
  gboolean done = FALSE;

  if (GST_IS_EVENT (buf)) {
    GstEvent *event = GST_EVENT (buf);

    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_DISCONTINUOUS:
      {
	GST_DEBUG (GST_CAT_EVENT, "discont"); 
        mpeg2dec->next_time = 0;
        mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
	gst_mpeg2dec_flush_decoder (mpeg2dec);
        gst_pad_event_default (pad, event);
	return;
      }
      case GST_EVENT_EOS:
	if (mpeg2dec->index && mpeg2dec->closed) {
	  gst_index_commit (mpeg2dec->index, mpeg2dec->index_id);
        }
      default:
        gst_pad_event_default (pad, event);
	return;
    }
  }

  size = GST_BUFFER_SIZE (buf);
  data = GST_BUFFER_DATA (buf);
  pts = GST_BUFFER_TIMESTAMP (buf);

  info = mpeg2_info (mpeg2dec->decoder);
  end = data + size;

  if (pts != -1) {
    gint64 mpeg_pts = GSTTIME_TO_MPEGTIME (pts);

    GST_DEBUG (0, "have pts: %" G_GINT64_FORMAT " (%" G_GINT64_FORMAT ")", 
		  mpeg_pts, MPEGTIME_TO_GSTTIME (mpeg_pts));

    mpeg2_pts (mpeg2dec->decoder, mpeg_pts);
  }
  else {
    GST_DEBUG (GST_CAT_CLOCK, "no pts");
  }

  mpeg2_buffer (mpeg2dec->decoder, data, end);

  while (!done) {
    gboolean slice = FALSE;

    state = mpeg2_parse (mpeg2dec->decoder);
    GST_DEBUG (0, "state %d", state);
    switch (state) {
      case STATE_SEQUENCE:
      {
	mpeg2dec->width = info->sequence->width;
	mpeg2dec->height = info->sequence->height;
	mpeg2dec->pixel_width = info->sequence->pixel_width;
	mpeg2dec->pixel_height = info->sequence->pixel_height;
	mpeg2dec->total_frames = 0;
        mpeg2dec->frame_period = info->sequence->frame_period * GST_USECOND / 27;

	GST_DEBUG (0, "sequence flags: %d, frame period: %d", 
		      info->sequence->flags, info->sequence->frame_period);

	if (!gst_mpeg2dec_negotiate_format (mpeg2dec)) {
          gst_element_error (GST_ELEMENT (mpeg2dec), "could not negotiate format");
	  goto exit;
	}

	update_streaminfo (mpeg2dec);

	if (!mpeg2dec->have_fbuf) {
	  /* alloc 3 buffers */
	  gst_mpeg2dec_alloc_buffer (mpeg2dec, info, GST_BUFFER_OFFSET (buf));
	  gst_mpeg2dec_alloc_buffer (mpeg2dec, info, GST_BUFFER_OFFSET (buf));
	  gst_mpeg2dec_alloc_buffer (mpeg2dec, info, GST_BUFFER_OFFSET (buf));
	  mpeg2dec->have_fbuf = TRUE;
	}

	mpeg2dec->need_sequence = FALSE;
	if (mpeg2dec->pending_event) {
	  done = GST_EVENT_SEEK_FLAGS (mpeg2dec->pending_event) & GST_SEEK_FLAG_FLUSH;
	  
          gst_mpeg2dec_src_event (mpeg2dec->srcpad, mpeg2dec->pending_event);
	  mpeg2dec->pending_event = NULL;
	}
        break;
      }
      case STATE_SEQUENCE_REPEATED:
	GST_DEBUG (0, "sequence repeated");
      case STATE_GOP:
        break;
      case STATE_PICTURE:
      {
	gboolean key_frame = FALSE;
	GstBuffer *outbuf;

        if (info->current_picture) {
	  key_frame = (info->current_picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I;
	}
	outbuf = gst_mpeg2dec_alloc_buffer (mpeg2dec, info, GST_BUFFER_OFFSET (buf));

	GST_DEBUG (0, "picture %d, %p, %" G_GINT64_FORMAT ", %" G_GINT64_FORMAT, 
			key_frame, outbuf, GST_BUFFER_OFFSET (outbuf), pts);

	if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_PICTURE && key_frame)
	  mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_KEYFRAME;

	if (!GST_PAD_IS_USABLE (mpeg2dec->srcpad))
	  mpeg2_skip (mpeg2dec->decoder, 1);
	else
	  mpeg2_skip (mpeg2dec->decoder, 0);

        break;
      }
      case STATE_SLICE_1ST:
	GST_DEBUG (0, "slice 1st");
        break;
      case STATE_PICTURE_2ND:
	GST_DEBUG (0, "picture second");
        break;
      case STATE_SLICE:
	slice = TRUE;
      case STATE_END:
      {
	GstBuffer *outbuf = NULL;
	gboolean skip = FALSE;

	if (!slice) {
	  mpeg2dec->need_sequence = TRUE;
	}
	GST_DEBUG (0, "picture end %p %p %p %p", info->display_fbuf, info->display_picture, info->current_picture,
			(info->display_fbuf ? info->display_fbuf->id : NULL));

	if (info->display_fbuf && info->display_fbuf->id) {
          const picture_t *picture;
	  gboolean key_frame = FALSE;
	  
	  outbuf = (GstBuffer *) info->display_fbuf->id;
	  picture = info->display_picture;

	  key_frame = (picture->flags & PIC_MASK_CODING_TYPE) == PIC_FLAG_CODING_TYPE_I;
	  GST_DEBUG (0, "picture keyfame %d", key_frame);

	  if (key_frame) {
            GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_KEY_UNIT);
	  }
	  if (mpeg2dec->discont_state == MPEG2DEC_DISC_NEW_KEYFRAME && key_frame)
	    mpeg2dec->discont_state = MPEG2DEC_DISC_NONE;

	  if (picture->flags & PIC_FLAG_PTS) {
            GstClockTime time = MPEGTIME_TO_GSTTIME (picture->pts);

	    GST_DEBUG (0, "picture had pts %" G_GINT64_FORMAT, time);
            GST_BUFFER_TIMESTAMP (outbuf) = time;

            mpeg2dec->next_time = time;
	  }
	  else {
	    GST_DEBUG (0, "picture didn't have pts using %" G_GINT64_FORMAT, mpeg2dec->next_time);
            GST_BUFFER_TIMESTAMP (outbuf) = mpeg2dec->next_time;
	  }
          mpeg2dec->next_time += (mpeg2dec->frame_period * picture->nb_fields) >> 1;


	  GST_DEBUG (0, "picture: %s %s fields:%d off:%" G_GINT64_FORMAT " ts:%" G_GINT64_FORMAT, 
			  (picture->flags & PIC_FLAG_TOP_FIELD_FIRST ? "tff " : "    "),
			  (picture->flags & PIC_FLAG_PROGRESSIVE_FRAME ? "prog" : "    "),
			  picture->nb_fields, 
	                  GST_BUFFER_OFFSET (outbuf),
	                  GST_BUFFER_TIMESTAMP (outbuf));

	  if (mpeg2dec->index) {
            gst_index_add_association (mpeg2dec->index, mpeg2dec->index_id,
	                               (key_frame ? GST_ASSOCIATION_FLAG_KEY_UNIT : 0),
	                               GST_FORMAT_BYTES, GST_BUFFER_OFFSET (outbuf),
	                               GST_FORMAT_TIME, GST_BUFFER_TIMESTAMP (outbuf), 0);
	  }

	  
	  if (picture->flags & PIC_FLAG_SKIP || 
	      !GST_PAD_IS_USABLE (mpeg2dec->srcpad) || 
	      mpeg2dec->discont_state != MPEG2DEC_DISC_NONE ||
	      mpeg2dec->next_time < mpeg2dec->segment_start ||
	      skip) 
	  {
	    gst_buffer_unref (outbuf);
	  }
	  else {
	    gst_pad_push (mpeg2dec->srcpad, outbuf);
	  }
	}
	if (info->discard_fbuf && info->discard_fbuf->id) {
	  gst_buffer_unref ((GstBuffer *)info->discard_fbuf->id);
	}
        break;
      }
      /* need more data */
      case -1:
	done = TRUE;
	break;
      /* error */
      case STATE_INVALID:
	gst_element_error (GST_ELEMENT (mpeg2dec), "decoding error");
	done = TRUE;
        break;
      default:
	g_warning ("%s: unhandled state %d, FIXME", 
			gst_element_get_name (GST_ELEMENT (mpeg2dec)),
			state);
        break;
    }

    /*
     * FIXME: should pass more information such as state the user data is from
     */
    if (info->user_data_len > 0) {
      if (GST_PAD_IS_USABLE (mpeg2dec->userdatapad)) {
        GstBuffer *udbuf = gst_buffer_new_and_alloc (info->user_data_len);

        memcpy (GST_BUFFER_DATA (udbuf), info->user_data, info->user_data_len);

        gst_pad_push (mpeg2dec->userdatapad, udbuf);
      }
    }
  }
exit:
  gst_buffer_unref(buf);
}

static const GstFormat*
gst_mpeg2dec_get_sink_formats (GstPad *pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    0
  };
  return formats;
}

static gboolean
gst_mpeg2dec_convert_sink (GstPad *pad, GstFormat src_format, gint64 src_value,
		           GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  const mpeg2_info_t *info;
	      
  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  if (mpeg2dec->decoder == NULL)
    return FALSE;

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

static const GstFormat*
gst_mpeg2dec_get_src_formats (GstPad *pad)
{
  static const GstFormat formats[] = {
    GST_FORMAT_BYTES,
    GST_FORMAT_TIME,
    GST_FORMAT_UNITS,
    0
  };
  return formats;
}

static gboolean
gst_mpeg2dec_convert_src (GstPad *pad, GstFormat src_format, gint64 src_value,
		          GstFormat *dest_format, gint64 *dest_value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  const mpeg2_info_t *info;
  guint64 scale = 1;
	      
  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  if (mpeg2dec->decoder == NULL)
    return FALSE;

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
	  if (info->sequence && mpeg2dec->frame_period) {
            *dest_value = src_value * scale / mpeg2dec->frame_period;
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
          *dest_value = src_value * mpeg2dec->frame_period;
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

static const GstQueryType*
gst_mpeg2dec_get_src_query_types (GstPad *pad)
{
  static const GstQueryType types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };
  return types;
}

static gboolean 
gst_mpeg2dec_src_query (GstPad *pad, GstQueryType type,
		        GstFormat *format, gint64 *value)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;
  static const GstFormat *formats;

  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
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

          /* get our peer formats */
          formats = gst_pad_get_formats (GST_PAD_PEER (mpeg2dec->sinkpad));

          /* while we did not exhaust our seek formats without result */
          while (formats && *formats) {
	    GstFormat peer_format;
	    gint64 peer_value;
		  
	    peer_format = *formats;
	  
            /* do the probe */
            if (gst_pad_query (GST_PAD_PEER (mpeg2dec->sinkpad), GST_QUERY_TOTAL,
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
	    formats++;
	  }
          break;
	}
        default:
	  res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_POSITION:
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


static const GstEventMask*
gst_mpeg2dec_get_src_event_masks (GstPad *pad)
{
  static const GstEventMask masks[] = {
    { GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH },
    { 0, }
  };
  return masks;
}

static gboolean 
index_seek (GstPad *pad, GstEvent *event)
{
  GstIndexEntry *entry;
  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));
  
  entry = gst_index_get_assoc_entry (mpeg2dec->index, mpeg2dec->index_id,
		             GST_INDEX_LOOKUP_BEFORE, GST_ASSOCIATION_FLAG_KEY_UNIT,
			     GST_EVENT_SEEK_FORMAT (event),
			     GST_EVENT_SEEK_OFFSET (event));

  if (entry) {
    const GstFormat *peer_formats, *try_formats;
    /* since we know the exaxt byteoffset of the frame, make sure to seek on bytes first */
    const GstFormat try_all_formats[] = 
    { 
	GST_FORMAT_BYTES,
	GST_FORMAT_TIME,
	0 
    };

    try_formats = try_all_formats;
    peer_formats = gst_pad_get_formats (GST_PAD_PEER (mpeg2dec->sinkpad));

    while (gst_formats_contains (peer_formats, *try_formats)) {
      gint64 value;
    
      if (gst_index_entry_assoc_map (entry, *try_formats, &value)) {
        GstEvent *seek_event;

        /* lookup succeeded, create the seek */
        seek_event = gst_event_new_seek (*try_formats | GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH, value);
        /* do the seekk */
        if (gst_pad_send_event (GST_PAD_PEER (mpeg2dec->sinkpad), seek_event)) {
          /* seek worked, we're done, loop will exit */
	  gst_mpeg2dec_flush_decoder (mpeg2dec);
	  mpeg2dec->segment_start = GST_EVENT_SEEK_OFFSET (event);
          return TRUE;
        }
      }
      try_formats++;
    }
  }
  return FALSE;
}


static gboolean 
normal_seek (GstPad *pad, GstEvent *event)
{
  gint64 src_offset;
  gboolean flush;
  GstFormat format;
  const GstFormat *peer_formats;
  gboolean res = FALSE;
  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  format = GST_FORMAT_TIME;

  /* first bring the src_format to TIME */
  if (!gst_pad_convert (pad,
            GST_EVENT_SEEK_FORMAT (event), GST_EVENT_SEEK_OFFSET (event),
            &format, &src_offset))
  {
    /* didn't work, probably unsupported seek format then */
    return res;
  }

  /* shave off the flush flag, we'll need it later */
  flush = GST_EVENT_SEEK_FLAGS (event) & GST_SEEK_FLAG_FLUSH;

  /* get our peer formats */
  peer_formats = gst_pad_get_formats (GST_PAD_PEER (mpeg2dec->sinkpad));

  /* while we did not exhaust our seek formats without result */
  while (peer_formats && *peer_formats) {
    gint64 desired_offset;

    format = *peer_formats;

    /* try to convert requested format to one we can seek with on the sinkpad */
    if (gst_pad_convert (mpeg2dec->sinkpad, GST_FORMAT_TIME, src_offset, &format, &desired_offset))
    {
      GstEvent *seek_event;

      /* conversion succeeded, create the seek */
      seek_event = gst_event_new_seek (format | GST_SEEK_METHOD_SET | flush, desired_offset);
      /* do the seekk */
      if (gst_pad_send_event (GST_PAD_PEER (mpeg2dec->sinkpad), seek_event)) {
        /* seek worked, we're done, loop will exit */
	mpeg2dec->segment_start = GST_EVENT_SEEK_OFFSET (event);
        res = TRUE;
	break;
      }
    }
    peer_formats++;
  }
  /* at this point, either the seek worked and res = TRUE or res == FALSE and the seek
   * failed */
  if (res && flush) {
    /* if we need to flush, iterate until the buffer is empty */
    gst_mpeg2dec_flush_decoder (mpeg2dec);
  }

  return res;
}
static gboolean 
gst_mpeg2dec_src_event (GstPad *pad, GstEvent *event)
{
  gboolean res = TRUE;
  GstMpeg2dec *mpeg2dec;

  mpeg2dec = GST_MPEG2DEC (gst_pad_get_parent (pad));

  if (mpeg2dec->decoder == NULL)
    return FALSE;

  switch (GST_EVENT_TYPE (event)) {
    /* the all-formats seek logic */
    case GST_EVENT_SEEK:
      if (mpeg2dec->need_sequence) {
        mpeg2dec->pending_event = event;
	return TRUE;
      }
      else {
        if (mpeg2dec->index)
          res = index_seek (pad, event);
	else
          res = normal_seek (pad, event);
	
	if (res)
          mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
      }
      break;
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
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

      /* reset the initial video state */
      mpeg2dec->format = MPEG2DEC_FORMAT_NONE;
      mpeg2dec->width = -1;
      mpeg2dec->height = -1;
      mpeg2dec->segment_start = 0;
      mpeg2dec->segment_end = -1;
      mpeg2dec->discont_state = MPEG2DEC_DISC_NEW_PICTURE;
      mpeg2dec->frame_period = 0;
      mpeg2dec->streaminfo = NULL;
      gst_mpeg2dec_open_decoder (mpeg2dec);
      mpeg2dec->need_sequence = TRUE;
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
	gst_buffer_pool_unref (mpeg2dec->peerpool);
	mpeg2dec->peerpool = NULL;
      }
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_mpeg2dec_close_decoder (mpeg2dec);
      gst_caps_replace (&mpeg2dec->streaminfo, NULL);
      break;
    case GST_STATE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element);
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
    case ARG_STREAMINFO:
      g_value_set_boxed (value, mpeg2dec->streaminfo);
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
  gst_element_factory_add_pad_template (factory, GST_PAD_TEMPLATE_GET (user_data_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "mpeg2dec",
  plugin_init
};
