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
#include <gstrtjpegenc.h>

/* elementfactory information */
GstElementDetails gst_rtjpegenc_details = {
  "RTjpeg encoder",
  "Codec/Encoder/Video",
  "Encodes video in RTjpeg format",
  "Erik Walthinsen <omega@cse.ogi.edu>"
};

/* GstRTJpegEnc signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_QUALITY,
};


static void gst_rtjpegenc_class_init (GstRTJpegEncClass * klass);
static void gst_rtjpegenc_base_init (GstRTJpegEncClass * klass);
static void gst_rtjpegenc_init (GstRTJpegEnc * rtjpegenc);

static void gst_rtjpegenc_chain (GstPad * pad, GstData * _data);

static GstElementClass *parent_class = NULL;

/*static guint gst_rtjpegenc_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_rtjpegenc_get_type (void)
{
  static GType rtjpegenc_type = 0;

  if (!rtjpegenc_type) {
    static const GTypeInfo rtjpegenc_info = {
      sizeof (GstRTJpegEncClass),
      (GBaseInitFunc) gst_rtjpegenc_base_init,
      NULL,
      (GClassInitFunc) gst_rtjpegenc_class_init,
      NULL,
      NULL,
      sizeof (GstRTJpegEnc),
      0,
      (GInstanceInitFunc) gst_rtjpegenc_init,
    };

    rtjpegenc_type =
        g_type_register_static (GST_TYPE_ELEMENT, "GstRTJpegEnc",
        &rtjpegenc_info, 0);
  }
  return rtjpegenc_type;
}

static void
gst_rtjpegenc_base_init (GstRTJpegEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &gst_rtjpegenc_details);
}

static void
gst_rtjpegenc_class_init (GstRTJpegEncClass * klass)
{
  GstElementClass *gstelement_class;

  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);
}

static void
gst_rtjpegenc_init (GstRTJpegEnc * rtjpegenc)
{
  rtjpegenc->sinkpad = gst_pad_new ("sink", GST_PAD_SINK);
  gst_element_add_pad (GST_ELEMENT (rtjpegenc), rtjpegenc->sinkpad);
  gst_pad_set_chain_function (rtjpegenc->sinkpad, gst_rtjpegenc_chain);
  rtjpegenc->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_element_add_pad (GST_ELEMENT (rtjpegenc), rtjpegenc->srcpad);
}

static void
gst_rtjpegenc_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstRTJpegEnc *rtjpegenc;
  guchar *data;
  gulong size;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  rtjpegenc = GST_RTJPEGENC (GST_OBJECT_PARENT (pad));
  data = GST_BUFFER_DATA (buf);
  size = GST_BUFFER_SIZE (buf);

  gst_info ("would be encoding frame here\n");

  gst_pad_push (rtjpegenc->srcpad, GST_DATA (buf));
}
