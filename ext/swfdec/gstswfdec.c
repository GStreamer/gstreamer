/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2002,2003> David A. Schleef <ds@schleef.org>
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

/* elementfactory information */
static GstElementDetails gst_swfdec_details =
GST_ELEMENT_DETAILS ("SWF video decoder",
    "Codec/Decoder/Video",
    "Uses libswfdec to decode Flash video streams",
    "David Schleef <ds@schleef.org>");

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

static GstStaticPadTemplate video_template_factory =
GST_STATIC_PAD_TEMPLATE ("video_00",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx)
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

static gboolean gst_swfdec_src_event (GstPad * pad, GstEvent * event);
static gboolean gst_swfdec_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value);
static const GstQueryType *gst_swfdec_get_query_types (GstPad * pad);
static const GstEventMask *gst_swfdec_get_event_masks (GstPad * pad);

static GstStateChangeReturn gst_swfdec_change_state (GstElement * element,
    GstStateChange transition);


static GstElementClass *parent_class = NULL;

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

  gst_element_class_set_details (element_class, &gst_swfdec_details);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&video_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&audio_template_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&sink_template_factory));
}
static void
gst_swfdec_class_init (GstSwfdecClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_swfdec_set_property;
  gobject_class->get_property = gst_swfdec_get_property;
  gobject_class->dispose = gst_swfdec_dispose;

  gstelement_class->change_state = gst_swfdec_change_state;
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
        "framerate", G_TYPE_DOUBLE, swfdec->frame_rate, NULL);
  }

  return caps;
}

static GstPadLinkReturn
gst_swfdec_video_link (GstPad * pad, const GstCaps * caps)
{
  GstSwfdec *swfdec;
  GstStructure *structure;
  int width, height;
  int ret;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  if (!swfdec->have_format) {
    return GST_PAD_LINK_DELAYED;
  }

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);

  g_print ("setting size to %d x %d\n", width, height);
  ret = swfdec_decoder_set_image_size (swfdec->decoder, width, height);
  if (ret == SWF_OK) {
    swfdec->width = width;
    swfdec->height = height;

    return GST_PAD_LINK_OK;
  }

  return GST_PAD_LINK_REFUSED;
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

static void
gst_swfdec_loop (GstElement * element)
{
  GstSwfdec *swfdec;
  GstBuffer *buf = NULL;
  int ret;

  g_return_if_fail (element != NULL);
  g_return_if_fail (GST_IS_SWFDEC (element));

  swfdec = GST_SWFDEC (element);

  if (!swfdec->videopad) {
  }

  ret = swfdec_decoder_parse (swfdec->decoder);
  if (ret == SWF_NEEDBITS) {
    buf = GST_BUFFER (gst_pad_pull (swfdec->sinkpad));
    if (GST_IS_EVENT (buf)) {
      switch (GST_EVENT_TYPE (buf)) {
        case GST_EVENT_EOS:
          swfdec_decoder_eof (swfdec->decoder);
          GST_DEBUG ("got eos");
          break;
        default:
          GST_DEBUG ("got other event");
          break;
      }

    } else {
      if (!GST_BUFFER_DATA (buf)) {
        GST_DEBUG ("expected non-null buffer");
      }
      swfdec_decoder_add_buffer (swfdec->decoder,
          gst_swfdec_buffer_to_swf (buf));
    }
  } else if (ret == SWF_CHANGE) {
    GstCaps *caps;
    GstPadLinkReturn link_ret;

    swfdec_decoder_get_image_size (swfdec->decoder,
        &swfdec->width, &swfdec->height);
    swfdec_decoder_get_rate (swfdec->decoder, &swfdec->rate);
    swfdec->interval = GST_SECOND / swfdec->rate;

    caps = gst_caps_copy (gst_pad_get_pad_template_caps (swfdec->videopad));
    swfdec_decoder_get_rate (swfdec->decoder, &swfdec->frame_rate);
    gst_caps_set_simple (caps,
        "framerate", G_TYPE_DOUBLE, swfdec->frame_rate,
        "height", G_TYPE_INT, swfdec->height,
        "width", G_TYPE_INT, swfdec->width, NULL);
    link_ret = gst_pad_try_set_caps (swfdec->videopad, caps);
    if (GST_PAD_LINK_SUCCESSFUL (link_ret)) {
      /* good */
    } else {
      GST_ELEMENT_ERROR (swfdec, CORE, NEGOTIATION, (NULL), (NULL));
      return;
    }
    swfdec->have_format = TRUE;
  } else if (ret == SWF_EOF) {
    SwfdecBuffer *audio_buffer;
    SwfdecBuffer *video_buffer;
    GstBuffer *videobuf;
    GstBuffer *audiobuf;
    gboolean ret;

    ret = swfdec_render_iterate (swfdec->decoder);
    if (!ret) {
      gst_pad_push (swfdec->videopad, GST_DATA (gst_event_new (GST_EVENT_EOS)));
      gst_pad_push (swfdec->audiopad, GST_DATA (gst_event_new (GST_EVENT_EOS)));

      gst_element_set_eos (GST_ELEMENT (swfdec));

      return;
    }

    if (swfdec->send_discont) {
      GstEvent *event;

      swfdec->timestamp = swfdec_render_get_frame_index (swfdec->decoder) *
          swfdec->interval;

      GST_DEBUG ("sending discont %" G_GINT64_FORMAT, swfdec->timestamp);

      event = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
          swfdec->timestamp, GST_FORMAT_UNDEFINED);
      gst_pad_push (swfdec->videopad, GST_DATA (event));

      event = gst_event_new_discontinuous (FALSE, GST_FORMAT_TIME,
          swfdec->timestamp, GST_FORMAT_UNDEFINED);
      gst_pad_push (swfdec->audiopad, GST_DATA (event));

      swfdec->send_discont = FALSE;
    }

    GST_DEBUG ("pushing image/sound %" G_GINT64_FORMAT, swfdec->timestamp);

    video_buffer = swfdec_render_get_image (swfdec->decoder);
    videobuf = gst_buffer_new ();
    GST_BUFFER_DATA (videobuf) = video_buffer->data;
    GST_BUFFER_SIZE (videobuf) = video_buffer->length;
    GST_BUFFER_TIMESTAMP (videobuf) = swfdec->timestamp;

    gst_pad_push (swfdec->videopad, GST_DATA (videobuf));

    audio_buffer = swfdec_render_get_audio (swfdec->decoder);
    audiobuf = gst_buffer_new ();
    GST_BUFFER_DATA (audiobuf) = audio_buffer->data;
    GST_BUFFER_SIZE (audiobuf) = audio_buffer->length;
    GST_BUFFER_TIMESTAMP (audiobuf) = swfdec->timestamp;

    gst_pad_push (swfdec->audiopad, GST_DATA (audiobuf));

    swfdec->timestamp += swfdec->interval;
  }
}

static void
gst_swfdec_init (GstSwfdec * swfdec)
{
  /* create the sink and src pads */
  swfdec->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&sink_template_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->sinkpad);

  swfdec->videopad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&video_template_factory), "video_00");
  gst_pad_set_query_function (swfdec->videopad, gst_swfdec_src_query);
  gst_pad_set_getcaps_function (swfdec->videopad, gst_swfdec_video_getcaps);
  gst_pad_set_link_function (swfdec->videopad, gst_swfdec_video_link);
  gst_pad_set_event_function (swfdec->videopad, gst_swfdec_src_event);
  gst_pad_set_event_mask_function (swfdec->videopad,
      gst_swfdec_get_event_masks);
  gst_pad_set_query_type_function (swfdec->videopad,
      gst_swfdec_get_query_types);
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->videopad);

  swfdec->audiopad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&audio_template_factory), "audio_00");
  gst_pad_set_query_function (swfdec->audiopad, gst_swfdec_src_query);
  gst_pad_set_event_function (swfdec->audiopad, gst_swfdec_src_event);
  gst_pad_set_event_mask_function (swfdec->audiopad,
      gst_swfdec_get_event_masks);
  gst_pad_set_query_type_function (swfdec->audiopad,
      gst_swfdec_get_query_types);
  gst_element_add_pad (GST_ELEMENT (swfdec), swfdec->audiopad);

  gst_element_set_loop_function (GST_ELEMENT (swfdec), gst_swfdec_loop);

  /* initialize the swfdec decoder state */
  swfdec->decoder = swfdec_decoder_new ();
  g_return_if_fail (swfdec->decoder != NULL);

  swfdec_decoder_set_colorspace (swfdec->decoder, SWF_COLORSPACE_RGB888);

  GST_OBJECT_FLAG_SET (GST_ELEMENT (swfdec), GST_ELEMENT_EVENT_AWARE);

  swfdec->frame_rate = 0.;
}

static void
gst_swfdec_dispose (GObject * object)
{
  GstSwfdec *swfdec = GST_SWFDEC (object);

  swfdec_decoder_free (swfdec->decoder);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static const GstQueryType *
gst_swfdec_get_query_types (GstPad * pad)
{
  static const GstQueryType gst_swfdec_query_types[] = {
    GST_QUERY_TOTAL,
    GST_QUERY_POSITION,
    0
  };

  return gst_swfdec_query_types;
}

static gboolean
gst_swfdec_src_query (GstPad * pad, GstQueryType type,
    GstFormat * format, gint64 * value)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (type) {
    case GST_QUERY_TOTAL:
    {
      switch (*format) {
        case GST_FORMAT_TIME:
        {
          int n_frames;
          int ret;

          res = FALSE;
          ret = swfdec_decoder_get_n_frames (swfdec->decoder, &n_frames);
          if (ret == SWF_OK) {
            *value = n_frames * swfdec->interval;
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
    case GST_QUERY_POSITION:
    {
      switch (*format) {
        case GST_FORMAT_TIME:
          *value = swfdec_render_get_frame_index (swfdec->decoder) *
              swfdec->interval;
          res = TRUE;
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

  return res;
}

static const GstEventMask *
gst_swfdec_get_event_masks (GstPad * pad)
{
  static const GstEventMask gst_swfdec_event_masks[] = {
    {GST_EVENT_SEEK, GST_SEEK_METHOD_SET | GST_SEEK_FLAG_FLUSH},
    {0,}
  };

  return gst_swfdec_event_masks;
}

static gboolean
gst_swfdec_src_event (GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstSwfdec *swfdec;

  //static const GstFormat formats[] = { GST_FORMAT_TIME, GST_FORMAT_BYTES };

#define MAX_SEEK_FORMATS 1      /* we can only do time seeking for now */
  //gint i;

  swfdec = GST_SWFDEC (gst_pad_get_parent (pad));

  switch (GST_EVENT_TYPE (event)) {
      /* the all-formats seek logic */
    case GST_EVENT_SEEK:
    {
      int new_frame;
      int ret;
      int n_frames;

      new_frame = event->event_data.seek.offset / swfdec->interval;
      ret = swfdec_decoder_get_n_frames (swfdec->decoder, &n_frames);

      if (new_frame >= 0 && new_frame < n_frames) {
        GstEvent *event;

        GST_DEBUG ("seeking to frame %d\n", new_frame);
        swfdec_render_seek (swfdec->decoder, new_frame);

        GST_DEBUG ("sending flush event\n");
        event = gst_event_new (GST_EVENT_FLUSH);
        gst_pad_push (swfdec->videopad, GST_DATA (event));
        event = gst_event_new (GST_EVENT_FLUSH);
        gst_pad_push (swfdec->audiopad, GST_DATA (event));

        swfdec->send_discont = TRUE;
        swfdec->seek_frame = new_frame;
      }

      res = TRUE;
      break;
    }
    default:
      res = FALSE;
      break;
  }
  gst_event_unref (event);
  return res;
}

static GstStateChangeReturn
gst_swfdec_change_state (GstElement * element, GstStateChange transition)
{
  GstSwfdec *swfdec = GST_SWFDEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
    {
      //gst_swfdec_vo_open (swfdec);
      //swfdec_decoder_new (swfdec->decoder, swfdec->accel, swfdec->vo);

      //swfdec->decoder->is_sequence_needed = 1;
      //swfdec->decoder->frame_rate_code = 0;
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
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* if we are not closed by an EOS event do so now, this cen send a few frames but
       * we are prepared to not really send them (see above) */
      if (!swfdec->closed) {
        /*swf_close (swfdec->decoder); */
        swfdec->closed = TRUE;
      }
      //gst_swfdec_vo_destroy (swfdec);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_swfdec_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstSwfdec *src;

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

  g_return_if_fail (GST_IS_SWFDEC (object));
  swfdec = GST_SWFDEC (object);

  switch (prop_id) {
    default:
      break;
  }
}

void
art_warn (const char *fmt, ...)
{
  GST_ERROR ("caught art_warn");
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
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
