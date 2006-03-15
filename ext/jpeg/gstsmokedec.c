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

/*#define DEBUG_ENABLED*/
#include "gstsmokedec.h"
#include <gst/video/video.h>

/* elementfactory information */
GstElementDetails gst_smokedec_details = {
  "Smoke video decoder",
  "Codec/Decoder/Image",
  "Decode video from Smoke format",
  "Wim Taymans <wim@fluendo.com>",
};

GST_DEBUG_CATEGORY (smokedec_debug);
#define GST_CAT_DEFAULT smokedec_debug

/* SmokeDec signals and args */
enum
{
  LAST_SIGNAL
};

enum
{
  PROP_0
};

static void gst_smokedec_base_init (gpointer g_class);
static void gst_smokedec_class_init (GstSmokeDec * klass);
static void gst_smokedec_init (GstSmokeDec * smokedec);

static GstFlowReturn gst_smokedec_chain (GstPad * pad, GstBuffer * buf);

static GstElementClass *parent_class = NULL;

/*static guint gst_smokedec_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_smokedec_get_type (void)
{
  static GType smokedec_type = 0;

  if (!smokedec_type) {
    static const GTypeInfo smokedec_info = {
      sizeof (GstSmokeDecClass),
      gst_smokedec_base_init,
      NULL,
      (GClassInitFunc) gst_smokedec_class_init,
      NULL,
      NULL,
      sizeof (GstSmokeDec),
      0,
      (GInstanceInitFunc) gst_smokedec_init,
    };

    smokedec_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstSmokeDec", &smokedec_info,
        0);
  }
  return smokedec_type;
}

static GstStaticPadTemplate gst_smokedec_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static GstStaticPadTemplate gst_smokedec_sink_pad_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-smoke, "
        "width = (int) [ 16, 4096 ], "
        "height = (int) [ 16, 4096 ], " "framerate = (fraction) [ 0/1, MAX ]")
    );

static void
gst_smokedec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokedec_src_pad_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_smokedec_sink_pad_template));
  gst_element_class_set_details (element_class, &gst_smokedec_details);
}

static void
gst_smokedec_class_init (GstSmokeDec * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  GST_DEBUG_CATEGORY_INIT (smokedec_debug, "smokedec", 0, "Smoke decoder");
}

static void
gst_smokedec_init (GstSmokeDec * smokedec)
{
  GST_DEBUG_OBJECT (smokedec, "gst_smokedec_init: initializing");
  /* create the sink and src pads */

  smokedec->sinkpad =
      gst_pad_new_from_static_template (&gst_smokedec_sink_pad_template,
      "sink");
  gst_pad_set_chain_function (smokedec->sinkpad, gst_smokedec_chain);
  gst_element_add_pad (GST_ELEMENT (smokedec), smokedec->sinkpad);

  smokedec->srcpad =
      gst_pad_new_from_static_template (&gst_smokedec_src_pad_template, "src");
  gst_pad_use_fixed_caps (smokedec->srcpad);
  gst_element_add_pad (GST_ELEMENT (smokedec), smokedec->srcpad);

  /* reset the initial video state */
  smokedec->format = -1;
  smokedec->width = -1;
  smokedec->height = -1;
  smokedec->fps_num = -1;
  smokedec->fps_denom = -1;
  smokedec->next_time = 0;

  smokecodec_decode_new (&smokedec->info);
}

static GstFlowReturn
gst_smokedec_chain (GstPad * pad, GstBuffer * buf)
{
  GstSmokeDec *smokedec;
  guint8 *data, *outdata;
  gulong size, outsize;
  GstBuffer *outbuf;
  SmokeCodecFlags flags;
  GstClockTime time;
  guint width, height;
  guint fps_num, fps_denom;
  gint smokeret;
  GstFlowReturn ret;

  smokedec = GST_SMOKEDEC (gst_pad_get_parent (pad));

  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);
  time = GST_BUFFER_TIMESTAMP (buf);

  GST_DEBUG_OBJECT (smokedec,
      "gst_smokedec_chain: got buffer of %ld bytes in '%s'", size,
      GST_OBJECT_NAME (smokedec));

  /* have the ID packet. */
  if (data[0] == SMOKECODEC_TYPE_ID) {
    smokeret = smokecodec_parse_id (smokedec->info, data, size);
    if (smokeret != SMOKECODEC_OK)
      goto header_error;

    ret = GST_FLOW_OK;
    goto done;
  }

  /* now handle data packets */
  GST_DEBUG_OBJECT (smokedec, "gst_smokedec_chain: reading header %08lx",
      *(gulong *) data);
  smokecodec_parse_header (smokedec->info, data, size, &flags, &width, &height,
      &fps_num, &fps_denom);

  if (smokedec->height != height || smokedec->width != width ||
      smokedec->fps_num != fps_num || smokedec->fps_denom != fps_denom) {
    GstCaps *caps;

    smokedec->height = height;
    smokedec->width = width;

    caps = gst_caps_new_simple ("video/x-raw-yuv",
        "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", GST_TYPE_FRACTION, fps_num, fps_denom, NULL);

    gst_pad_set_caps (smokedec->srcpad, caps);
    gst_caps_unref (caps);
  }


  if (smokedec->need_keyframe) {
    if (!(flags & SMOKECODEC_KEYFRAME))
      goto keyframe_skip;

    smokedec->need_keyframe = FALSE;
  }

  outbuf = gst_buffer_new ();
  outsize = GST_BUFFER_SIZE (outbuf) = width * height + width * height / 2;
  outdata = g_malloc (outsize);
  GST_BUFFER_DATA (outbuf) = outdata;
  GST_BUFFER_MALLOCDATA (outbuf) = outdata;

  GST_BUFFER_DURATION (outbuf) = GST_SECOND * fps_denom / fps_num;
  GST_BUFFER_OFFSET (outbuf) = GST_BUFFER_OFFSET (buf);
  gst_buffer_set_caps (outbuf, GST_PAD_CAPS (smokedec->srcpad));

  if (time == GST_CLOCK_TIME_NONE) {
    if (GST_BUFFER_OFFSET (buf) == -1) {
      time = smokedec->next_time;
    } else {
      time = GST_BUFFER_OFFSET (buf) * GST_BUFFER_DURATION (outbuf);
    }
  }
  GST_BUFFER_TIMESTAMP (outbuf) = time;
  smokedec->next_time = time + GST_BUFFER_DURATION (outbuf);

  smokeret = smokecodec_decode (smokedec->info, data, size, outdata);
  if (smokeret != SMOKECODEC_OK)
    goto decode_error;

  GST_DEBUG_OBJECT (smokedec, "gst_smokedec_chain: sending buffer");
  ret = gst_pad_push (smokedec->srcpad, outbuf);

done:
  gst_buffer_unref (buf);
  gst_object_unref (smokedec);

  return ret;

  /* ERRORS */
header_error:
  {
    GST_ELEMENT_ERROR (smokedec, STREAM, DECODE,
        (NULL), ("Could not parse smoke header, reason: %d", smokeret));
    ret = GST_FLOW_ERROR;
    goto done;
  }
keyframe_skip:
  {
    GST_DEBUG_OBJECT (smokedec, "dropping buffer while waiting for keyframe");
    ret = GST_FLOW_OK;
    goto done;
  }
decode_error:
  {
    GST_ELEMENT_ERROR (smokedec, STREAM, DECODE,
        (NULL), ("Could not decode smoke frame, reason: %d", smokeret));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}
