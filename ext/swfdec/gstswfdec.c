/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002,2003,2006 David A. Schleef <ds@schleef.org>
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
#include "gstswfdec.h"
#include <string.h>
#include <gst/video/video.h>
#include <swfdec_buffer.h>
#include <swfdec_decoder.h>

GST_DEBUG_CATEGORY_STATIC (swfdec_debug);
#define GST_CAT_DEFAULT swfdec_debug

/* Swfdec signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
      /* FILL ME */
};

#define __GST_VIDEO_CAPS_MAKE_32_new(R, G, B)				\
    "video/x-raw-rgb, "							\
    "bpp = (int) 32, "							\
    "depth = (int) 24, "						\
    "endianness = (int) BIG_ENDIAN, "					\
    "red_mask = (int) " GST_VIDEO_BYTE ## R ## _MASK_32 ", "		\
    "green_mask = (int) " GST_VIDEO_BYTE ## G ## _MASK_32 ", "		\
    "blue_mask = (int) " GST_VIDEO_BYTE ## B ## _MASK_32 ", "		\
    "width = " GST_VIDEO_SIZE_RANGE ", "				\
    "height = " GST_VIDEO_SIZE_RANGE ", "				\
    "framerate = (fraction) [ 0, MAX ]"

#define GST_VIDEO_CAPS_BGRx_new \
    __GST_VIDEO_CAPS_MAKE_32 (3, 2, 1)

static GstStaticPadTemplate video_template_factory =
GST_STATIC_PAD_TEMPLATE ("video_00",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx_new)
    );

static GstStaticPadTemplate audio_template_factory =
GST_STATIC_PAD_TEMPLATE ("audio_00",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "rate = (int) 44100, "
        "channels = (int) 2, "
        "endianness = (int) BYTE_ORDER, "
        "width = (int) 16, " "depth = (int) 16, " "signed = (boolean) true")
    );

static GstStaticPadTemplate sink_template_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-shockwave-flash")
    );

static void gst_swfdec_base_init (gpointer g_class);
static void gst_swfdec_class_init (GstSwfdecClass * klass);
static void gst_swfdec_init (GstSwfdec * swfdec);

static void gst_swfdec_dispose (GObject * object);

static void gst_swfdec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_swfdec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_swfdec_sink_event (GstPad * pad, GstEvent * event);

static gboolean gst_swfdec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_swfdec_src_query (GstPad * pad, GstQuery * query);
static const GstQueryType *gst_swfdec_get_query_types (GstPad * pad);

static GstStateChangeReturn gst_swfdec_change_state (GstElement * element,
    GstStateChange transition);

static GstFlowReturn gst_swfdec_chain (GstPad * pad, GstBuffer * buffer);
static void gst_swfdec_render (GstSwfdec * swfdec, int ret);

static GstElementClass *parent_class = NULL;

//static guint gst_swfdec_signals[LAST_SIGNAL];


/* GstSwfdecBuffer */

typedef struct _GstSwfdecBuffer GstSwfdecBuffer;
struct _GstSwfdecBuffer
{
  GstBuffer buffer;

  SwfdecBuffer *swfdec_buffer;
};

static GstBufferClass *buffer_parent_class = NULL;

static void gst_swfdecbuffer_class_init (gpointer g_class, gpointer class_data);
static void gst_swfdecbuffer_finalize (GstSwfdecBuffer * swfdecbuffer);

GType
gst_swfdecbuffer_get_type (void)
{
  static GType _gst_swfdecbuffer_type;

  if (G_UNLIKELY (_gst_swfdecbuffer_type == 0)) {
    static const GTypeInfo swfdecbuffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_swfdecbuffer_class_init,
      NULL,
      NULL,
      sizeof (GstSwfdecBuffer),
      0,
      NULL,
      NULL
    };

    _gst_swfdecbuffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstSwfdecBuffer", &swfdecbuffer_info, 0);
  }
  return _gst_swfdecbuffer_type;
}

static void
gst_swfdecbuffer_class_init (gpointer klass, gpointer class_data)
{
  GstBufferClass *swfdecbuffer_class = GST_BUFFER_CLASS (klass);

  swfdecbuffer_class->mini_object_class.finalize =
      (GstMiniObjectFinalizeFunction) gst_swfdecbuffer_finalize;

  buffer_parent_class = g_type_class_peek_parent (klass);
}

static void
gst_swfdecbuffer_finalize (GstSwfdecBuffer * swfdecbuffer)
{
  g_return_if_fail (swfdecbuffer != NULL);

  GST_LOG ("finalize %p", swfdecbuffer);

  swfdec_buffer_unref (swfdecbuffer->swfdec_buffer);

  ((GstMiniObjectClass *) buffer_parent_class)->finalize ((GstMiniObject *)
      swfdecbuffer);
}



/* GstSwfdec */

GType
gst_swfdec_get_type (void)
{
  static GType swfdec_type = 0;

  if (!swfdec_type) {
    static const GTypeInfo swfdec_info = {
      sizeof (GstSwfdecClass),
      gst_swfdec_base_init,
      NULL,
      (GClassInitFunc) gst_swfdec_class_init,
      NULL,
      NULL,
      sizeof (GstSwfdec),
      0,
      (GInstanceInitFunc) gst_swfdec_init,
    };

    swfdec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSwfdec", &swfdec_info, 0);
  }
  return swfdec_type;
}

static void
gst_swfdec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "SWF video decoder",
      "Codec/Decoder/Video",
      "Uses libswfdec to decode Flash video streams",
      "David Schleef <ds@schleef.org>");

  gst_element_class_add_static_pad_template (element_class,
      &video_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &audio_template_factory);
  gst_element_class_add_static_pad_template (element_class,
      &sink_template_factory);
}

static void
gst_swfdec_class_init (GstSwfdecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);

  gobject_class->set_property = gst_swfdec_set_property;
  gobject_class->get_property = gst_swfdec_get_property;
  gobject_class->dispose = gst_swfdec_dispose;

  gstelement_class->change_state = gst_swfdec_change_state;

  GST_DEBUG_CATEGORY_INIT (swfdec_debug, "swfdec", 0, "Flash decoder plugin");

}

static GstCaps *
gst_swfdec_video_getcaps (GstPad * pad)
{
  GstSwfdec *swfdec;
  GstCaps *caps;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  if (swfdec->have_format) {
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_FRACTION, swfdec->frame_rate_n,
        swfdec->frame_rate_d, NULL);
  }

  return caps;
}

static gboolean
gst_swfdec_video_link (GstPad * pad, GstCaps * caps)
{
  GstSwfdec *swfdec;
  GstStructure *structure;
  int width, height;
  int ret;
  gboolean res = FALSE;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  ret = swfdec_decoder_set_image_size (swfdec->decoder, width, height);
  if (ret == SWF_OK) {
    swfdec->width = width;
    swfdec->height = height;

    res = TRUE;
  }

  gst_object_unref (swfdec);
  return res;

}

static void
gst_swfdec_buffer_free (SwfdecBuffer * buf, void *priv)
{
  gst_buffer_unref (GST_BUFFER (priv));
}

static SwfdecBuffer *
gst_swfdec_buffer_to_swf (GstBuffer * buffer)
{
  SwfdecBuffer *sbuf;

  sbuf = swfdec_buffer_new_with_data (GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
  sbuf->free = gst_swfdec_buffer_free;
  sbuf->priv = buffer;

  return sbuf;
}

static GstBuffer *
gst_swfdec_buffer_from_swf (SwfdecBuffer * buffer)
{
  GstSwfdecBuffer *buf;

  buf = (GstSwfdecBuffer *) gst_mini_object_new (gst_swfdecbuffer_get_type ());
  GST_BUFFER_DATA (buf) = buffer->data;
  GST_BUFFER_SIZE (buf) = buffer->length;
  buf->swfdec_buffer = buffer;

  return GST_BUFFER (buf);
}

GstFlowReturn
gst_swfdec_chain (GstPad * pad, GstBuffer * buffer)
{
  GstFlowReturn res = GST_FLOW_OK;
  int ret;
  GstSwfdec *swfdec = GST_SWFDEC (GST_PAD_PARENT (pad));

  g_static_rec_mutex_lock (&swfdec->mutex);
  GST_DEBUG_OBJECT (swfdec, "about to call swfdec_decoder_parse");
  ret = swfdec_decoder_parse (swfdec->decoder);
  if (ret == SWF_NEEDBITS) {
    guint buf_size;
    GstBuffer *prev_buffer;

    GST_DEBUG_OBJECT (swfdec, "SWF_NEEDBITS, feeding data to swfdec-decoder");
    buf_size = gst_adapter_available (swfdec->adapter);
    if (buf_size) {
      prev_buffer = gst_buffer_new_and_alloc (buf_size);
      memcpy (GST_BUFFER_DATA (prev_buffer), gst_adapter_peek (swfdec->adapter,
              buf_size), buf_size);
      gst_adapter_flush (swfdec->adapter, buf_size);

      swfdec_decoder_add_buffer (swfdec->decoder,
          gst_swfdec_buffer_to_swf (prev_buffer));
    }

    swfdec_decoder_add_buffer (swfdec->decoder,
        gst_swfdec_buffer_to_swf (buffer));

  } else if (ret == SWF_CHANGE) {

    GstCaps *caps;
    double rate;

    GstTagList *taglist;

    GST_DEBUG_OBJECT (swfdec, "SWF_CHANGE");
    gst_adapter_push (swfdec->adapter, buffer);

    swfdec_decoder_get_image_size (swfdec->decoder,
        &swfdec->width, &swfdec->height);
    swfdec_decoder_get_rate (swfdec->decoder, &rate);
    swfdec->interval = GST_SECOND / rate;

    swfdec->frame_rate_n = (int) (rate * 256.0);
    swfdec->frame_rate_d = 256;

    caps = gst_caps_copy (gst_pad_get_pad_template_caps (swfdec->videopad));
    gst_caps_set_simple (caps,
        "framerate", GST_TYPE_FRACTION, swfdec->frame_rate_n,
        swfdec->frame_rate_d, "height", G_TYPE_INT, swfdec->height, "width",
        G_TYPE_INT, swfdec->width, NULL);
    if (gst_pad_set_caps (swfdec->videopad, caps)) {
      /* good */
    } else {
      gst_caps_unref (caps);
      GST_ELEMENT_ERROR (swfdec, CORE, NEGOTIATION, (NULL), (NULL));
      res = GST_FLOW_ERROR;
      goto done;
    }
    gst_caps_unref (caps);

    caps = gst_caps_copy (gst_pad_get_pad_template_caps (swfdec->audiopad));
    if (gst_pad_set_caps (swfdec->audiopad, caps)) {
      swfdec->have_format = TRUE;
    } else {
      gst_caps_unref (caps);
      GST_ELEMENT_ERROR (swfdec, CORE, NEGOTIATION, (NULL), (NULL));
      res = GST_FLOW_ERROR;
      goto done;
    }

    gst_caps_unref (caps);


    taglist = gst_tag_list_new ();
    gst_tag_list_add (taglist, GST_TAG_MERGE_REPLACE,
        GST_TAG_ENCODER_VERSION, swfdec_decoder_get_version (swfdec->decoder),
        NULL);
    gst_element_found_tags (GST_ELEMENT (swfdec), taglist);
  } else if (ret == SWF_EOF) {
    GST_DEBUG_OBJECT (swfdec, "SWF_EOF");
    gst_swfdec_render (swfdec, ret);
    gst_task_start (swfdec->task);
  }

done:
  g_static_rec_mutex_unlock (&swfdec->mutex);
  return res;

}

static void
gst_swfdec_loop (GstSwfdec * swfdec)
{

  gst_swfdec_render (swfdec, swfdec_decoder_parse (swfdec->decoder));

}

static void
gst_swfdec_render (GstSwfdec * swfdec, int ret)
{

  if (ret == SWF_EOF) {
    SwfdecBuffer *audio_buffer;
    SwfdecBuffer *video_buffer;
    GstBuffer *videobuf;
    GstBuffer *audiobuf;
    gboolean ret;
    GstFlowReturn res;
    const char *url;

    GST_DEBUG_OBJECT (swfdec, "render:SWF_EOF");
    swfdec_decoder_set_mouse (swfdec->decoder, swfdec->x, swfdec->y,
        swfdec->button);

    ret = swfdec_render_iterate (swfdec->decoder);

    if (swfdec->decoder->using_experimental) {
      GST_ELEMENT_ERROR (swfdec, LIBRARY, FAILED,
          ("SWF file contains features known to trigger bugs."),
          ("SWF file contains features known to trigger bugs."));
      gst_task_stop (swfdec->task);
    }

    if (!ret) {
      gst_task_stop (swfdec->task);
      res = gst_pad_push_event (swfdec->videopad, gst_event_new_eos ());
      res = gst_pad_push_event (swfdec->audiopad, gst_event_new_eos ());

      return;
    }

    if (swfdec->send_discont) {
      GstEvent *event;

      swfdec->timestamp = swfdec_render_get_frame_index (swfdec->decoder) *
          swfdec->interval;

      GST_DEBUG ("sending discont %" G_GINT64_FORMAT, swfdec->timestamp);

      event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
          swfdec->timestamp, GST_CLOCK_TIME_NONE, 0);
      gst_pad_push_event (swfdec->videopad, event);

      event = gst_event_new_new_segment (FALSE, 1.0, GST_FORMAT_TIME,
          swfdec->timestamp, GST_CLOCK_TIME_NONE, 0);
      gst_pad_push_event (swfdec->audiopad, event);

      swfdec->send_discont = FALSE;
    }

    GST_DEBUG ("pushing image/sound %" G_GINT64_FORMAT, swfdec->timestamp);

    if (swfdec->skip_index) {
      video_buffer = NULL;
      swfdec->skip_index--;
    } else {


      video_buffer = swfdec_render_get_image (swfdec->decoder);

      if (!video_buffer) {
        gst_task_stop (swfdec->task);
        gst_pad_push_event (swfdec->videopad, gst_event_new_eos ());
        gst_pad_push_event (swfdec->audiopad, gst_event_new_eos ());
        return;
      }

      swfdec->skip_index = swfdec->skip_frames - 1;

      videobuf = gst_swfdec_buffer_from_swf (video_buffer);
      GST_BUFFER_TIMESTAMP (videobuf) = swfdec->timestamp;
      gst_buffer_set_caps (videobuf, GST_PAD_CAPS (swfdec->videopad));

      gst_pad_push (swfdec->videopad, videobuf);
    }

    audio_buffer = swfdec_render_get_audio (swfdec->decoder);

    if (audio_buffer) {

      audiobuf = gst_swfdec_buffer_from_swf (audio_buffer);
      GST_BUFFER_TIMESTAMP (audiobuf) = swfdec->timestamp;
      gst_buffer_set_caps (audiobuf, GST_PAD_CAPS (swfdec->audiopad));

      gst_pad_push (swfdec->audiopad, audiobuf);

    }

    swfdec->timestamp += swfdec->interval;

    url = swfdec_decoder_get_url (swfdec->decoder);
    if (url) {
      GstStructure *s;
      GstMessage *msg;

      s = gst_structure_new ("embedded-url", "url", G_TYPE_STRING, url,
          "target", G_TYPE_STRING, "_self", NULL);
      msg = gst_message_new_element (GST_OBJECT (swfdec), s);
      gst_element_post_message (GST_ELEMENT (swfdec), msg);
    }
  }
}

static void
gst_swfdec_init (GstSwfdec * swfdec)
{
  /* create the sink and src pads */
  swfdec->sinkpad =
      gst_pad_new_from_static_template (&sink_template_factory, "sink");
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->sinkpad);

  gst_pad_set_chain_function (swfdec->sinkpad, gst_swfdec_chain);
  gst_pad_set_event_function (swfdec->sinkpad, gst_swfdec_sink_event);

  swfdec->videopad =
      gst_pad_new_from_static_template (&video_template_factory, "video_00");
  gst_pad_set_query_function (swfdec->videopad, gst_swfdec_src_query);
  gst_pad_set_getcaps_function (swfdec->videopad, gst_swfdec_video_getcaps);
  gst_pad_set_setcaps_function (swfdec->videopad, gst_swfdec_video_link);
  gst_pad_set_event_function (swfdec->videopad, gst_swfdec_src_event);

  gst_pad_set_query_type_function (swfdec->videopad,
      gst_swfdec_get_query_types);

  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->videopad);

  swfdec->audiopad =
      gst_pad_new_from_static_template (&audio_template_factory, "audio_00");
  gst_pad_set_query_function (swfdec->audiopad, gst_swfdec_src_query);
  gst_pad_set_event_function (swfdec->audiopad, gst_swfdec_src_event);

  gst_pad_set_query_type_function (swfdec->audiopad,
      gst_swfdec_get_query_types);

  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->audiopad);


  /* initialize the swfdec decoder state */
  swfdec->decoder = swfdec_decoder_new ();
  g_return_if_fail (swfdec->decoder != NULL);

  swfdec_decoder_set_colorspace (swfdec->decoder, SWF_COLORSPACE_RGB888);

  swfdec->frame_rate_n = 0;
  swfdec->frame_rate_d = 1;
  swfdec->x = -1;
  swfdec->y = -1;

  swfdec->skip_frames = 2;
  swfdec->skip_index = 0;

  swfdec->adapter = gst_adapter_new ();
  swfdec->task = gst_task_create ((GstTaskFunction) gst_swfdec_loop, swfdec);
  g_static_rec_mutex_init (&swfdec->mutex);
  gst_task_set_lock (swfdec->task, &swfdec->mutex);

}

static void
gst_swfdec_dispose (GObject * object)
{
  GstSwfdec *swfdec = GST_SWFDEC (object);

  gst_task_stop (swfdec->task);
  gst_task_join (swfdec->task);
  gst_object_unref (swfdec->task);
  g_static_rec_mutex_free (&swfdec->mutex);

  g_object_unref (swfdec->adapter);

  swfdec_decoder_free (swfdec->decoder);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static const GstQueryType *
gst_swfdec_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_swfdec_query_types[] = {
    GST_QUERY_POSITION,
    GST_QUERY_DURATION,
    0
  };

  return gst_swfdec_query_types;
}

static gboolean
gst_swfdec_src_query (GstPad * pad, GstQuery * query)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_POSITION:
    {
      GstFormat format;
      gint64 value;

      gst_query_parse_position (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
          value = swfdec_render_get_frame_index (swfdec->decoder) *
              swfdec->interval;
          gst_query_set_position (query, GST_FORMAT_TIME, value);
          res = TRUE;
        default:
          res = FALSE;
          break;
      }
      break;
    }
    case GST_QUERY_DURATION:
    {
      GstFormat format;
      gint64 value;

      gst_query_parse_duration (query, &format, NULL);

      switch (format) {
        case GST_FORMAT_TIME:
        {
          int n_frames;
          int ret;

          res = FALSE;
          ret = swfdec_decoder_get_n_frames (swfdec->decoder, &n_frames);
          if (ret == SWF_OK) {
            value = n_frames * swfdec->interval;
            gst_query_set_duration (query, GST_FORMAT_TIME, value);
            res = TRUE;
          }
          break;
        }
        default:
          res = FALSE;
          break;
      }
      break;
    }
    default:
      res = FALSE;
      break;
  }

  gst_object_unref (swfdec);
  return res;
}

#if 0
gst_swfdec_get_event_masks (GstPad * pad)
{
  static const GstEventMask gst_swfdec_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {GST_EVENT_NAVIGATION, 0},
    {0,}
  };

  return gst_swfdec_event_masks;
}
#endif

static gboolean
gst_swfdec_sink_event (GstPad * pad, GstEvent * event)
{
  GstSwfdec *swfdec;
  gboolean ret;

  swfdec = GST_SWFDEC (GST_PAD_PARENT (pad));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      swfdec_decoder_eof (swfdec->decoder);
      gst_task_start (swfdec->task);
      gst_event_unref (event);
      ret = TRUE;
      break;
    default:
      ret = gst_pad_event_default (pad, event);
      break;
  }

  return ret;
}

static gboolean
gst_swfdec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;

#define MAX_SEEK_FORMATS 1      /* we can only do time seeking for now */

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
#if 0
      /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      gdouble rate;
      GstFormat format;
      GstSeekFlags flags;
      GstSeekType start_type, stop_type;
      gint64 start, stop;

      int new_frame;
      int ret;
      int n_frames;

      gst_event_parse_seek (event, &rate, &format, &flags, &start_type, &start,
          &stop_type, &stop);

      new_frame = start / swfdec->interval;
      ret = swfdec_decoder_get_n_frames (swfdec->decoder, &n_frames);

      if (new_frame >= 0 && new_frame < n_frames) {
        GstEvent *event;

        GST_DEBUG ("seeking to frame %d\n", new_frame);
        swfdec_render_seek (swfdec->decoder, new_frame);

        GST_DEBUG ("sending flush event\n");

        event = gst_event_new_flush_start ();
        gst_pad_push_event (swfdec->videopad, event);
        event = gst_event_new_flush_start ();
        gst_pad_push_event (swfdec->audiopad, event);

        swfdec->send_discont = TRUE;
        swfdec->seek_frame = new_frame;
      }

      res = TRUE;
      break;
    }
#endif
    case GST_EVENT_NAVIGATION:
    {
      const GstStructure *structure = gst_event_get_structure (event);
      const char *type;

      type = gst_structure_get_string (structure, "event");
      GST_DEBUG ("got nav event %s", type);
      if (g_str_equal (type, "mouse-move")) {
        gst_structure_get_double (structure, "pointer_x", &swfdec->x);
        gst_structure_get_double (structure, "pointer_y", &swfdec->y);
      } else if (g_str_equal (type, "mouse-button-press")) {
        gst_structure_get_double (structure, "pointer_x", &swfdec->x);
        gst_structure_get_double (structure, "pointer_y", &swfdec->y);
        swfdec->button = 1;
      } else if (g_str_equal (type, "mouse-button-release")) {
        gst_structure_get_double (structure, "pointer_x", &swfdec->x);
        gst_structure_get_double (structure, "pointer_y", &swfdec->y);
        swfdec->button = 0;
      }
      break;
    }
    default:
      res = gst_pad_event_default (pad, event);
      event = NULL;
      break;
  }
  if (event) {
    gst_event_unref (event);
  }
  gst_object_unref (swfdec);
  return res;
}

static GstStateChangeReturn
gst_swfdec_change_state (GstElement * element, GstStateChange transition)
{
  GstSwfdec *swfdec = GST_SWFDEC (element);
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      gst_adapter_clear (swfdec->adapter);
      /*
         gst_swfdec_vo_open (swfdec);
         swfdec_decoder_new (swfdec->decoder, swfdec->accel, swfdec->vo);

         swfdec->decoder->is_sequence_needed = 1;
         swfdec->decoder->frame_rate_code = 0;
       */
      swfdec->timestamp = 0;
      swfdec->closed = FALSE;

      /* reset the initial video state */
      swfdec->have_format = FALSE;
      swfdec->format = -1;
      swfdec->width = -1;
      swfdec->height = -1;
      swfdec->first = TRUE;
      break;
    }
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      gst_task_start (swfdec->task);
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      gst_task_pause (swfdec->task);
      gst_task_join (swfdec->task);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* if we are not closed by an EOS event do so now, this cen send a few frames but
       * we are prepared to not really send them (see above) */
      if (!swfdec->closed) {
        /*swf_close (swfdec->decoder); */
        swfdec->closed = TRUE;
      }
      /* gst_swfdec_vo_destroy (swfdec); */
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_task_stop (swfdec->task);
      gst_task_join (swfdec->task);
      break;
    default:
      break;
  }

  return ret;

}

static void
gst_swfdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSwfdec *src;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SWFDEC (object));
  src = GST_SWFDEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_swfdec_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSwfdec *swfdec;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_SWFDEC (object));
  swfdec = GST_SWFDEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "swfdec", GST_RANK_PRIMARY,
      GST_TYPE_SWFDEC);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "swfdec",
    "Uses libswfdec to decode Flash video streams",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
