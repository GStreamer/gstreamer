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
#include <math.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gsty4mencode.h"

static GstElementDetails y4mencode_details = GST_ELEMENT_DETAILS ("Y4mEncode",
    "Codec/Encoder/Video",
    "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
    "Wim Taymans <wim.taymans@chello.be>");


/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0
};

static GstStaticPadTemplate y4mencode_src_factory =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-yuv4mpeg, " "y4mversion = (int) 1")
    );

static GstStaticPadTemplate y4mencode_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("I420"))
    );

static void gst_y4mencode_base_init (gpointer g_class);
static void gst_y4mencode_class_init (GstY4mEncodeClass * klass);
static void gst_y4mencode_init (GstY4mEncode * filter);

static void gst_y4mencode_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_y4mencode_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_y4mencode_chain (GstPad * pad, GstData * _data);
static GstElementStateReturn gst_y4mencode_change_state (GstElement * element);


static GstElementClass *parent_class = NULL;

/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_y4mencode_get_type (void)
{
  static GType y4mencode_type = 0;

  if (!y4mencode_type) {
    static const GTypeInfo y4mencode_info = {
      sizeof (GstY4mEncodeClass),
      gst_y4mencode_base_init,
      NULL,
      (GClassInitFunc) gst_y4mencode_class_init,
      NULL,
      NULL,
      sizeof (GstY4mEncode),
      0,
      (GInstanceInitFunc) gst_y4mencode_init,
    };

    y4mencode_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstY4mEncode", &y4mencode_info, 0);
  }
  return y4mencode_type;
}


static void
gst_y4mencode_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&y4mencode_src_factory));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&y4mencode_sink_factory));
  gst_element_class_set_details (element_class, &y4mencode_details);
}
static void
gst_y4mencode_class_init (GstY4mEncodeClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref (GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_y4mencode_change_state;

  gobject_class->set_property = gst_y4mencode_set_property;
  gobject_class->get_property = gst_y4mencode_get_property;
}

static GstPadLinkReturn
gst_y4mencode_sinkconnect (GstPad * pad, const GstCaps * caps)
{
  GstY4mEncode *filter;
  gint idx = -1, i;
  gdouble fps;
  gdouble framerates[] = {
    00.000,
    23.976, 24.000,             /* 24fps movie */
    25.000,                     /* PAL */
    29.970, 30.000,             /* NTSC */
    50.000,
    59.940, 60.000
  };
  GstStructure *structure;

  filter = GST_Y4MENCODE (gst_pad_get_parent (pad));

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &filter->width);
  gst_structure_get_int (structure, "height", &filter->height);
  gst_structure_get_double (structure, "framerate", &fps);

  /* find fps idx */
  for (i = 1; i < 9; i++) {
    if (idx == -1) {
      idx = i;
    } else {
      gdouble old_diff = fabs (framerates[idx] - fps),
          new_diff = fabs (framerates[i] - fps);

      if (new_diff < old_diff) {
        idx = i;
      }
    }
  }
  filter->fps_idx = idx;

  return GST_PAD_LINK_OK;
}

static void
gst_y4mencode_init (GstY4mEncode * filter)
{
  filter->sinkpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&y4mencode_sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);
  gst_pad_set_chain_function (filter->sinkpad, gst_y4mencode_chain);
  gst_pad_set_link_function (filter->sinkpad, gst_y4mencode_sinkconnect);

  filter->srcpad =
      gst_pad_new_from_template (gst_static_pad_template_get
      (&y4mencode_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->init = TRUE;
}

static void
gst_y4mencode_chain (GstPad * pad, GstData * _data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstY4mEncode *filter;
  GstBuffer *outbuf;
  gchar *header;
  gint len;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_Y4MENCODE (gst_pad_get_parent (pad));
  g_return_if_fail (filter != NULL);
  g_return_if_fail (GST_IS_Y4MENCODE (filter));

  outbuf = gst_buffer_new ();
  GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (buf) + 256);

  if (filter->init) {
    header = "YUV4MPEG %d %d %d\nFRAME\n";
    filter->init = FALSE;
  } else {
    header = "FRAME\n";
  }

  snprintf (GST_BUFFER_DATA (outbuf), 255, header,
      filter->width, filter->height, filter->fps_idx);
  len = strlen (GST_BUFFER_DATA (outbuf));

  memcpy (GST_BUFFER_DATA (outbuf) + len, GST_BUFFER_DATA (buf),
      GST_BUFFER_SIZE (buf));
  GST_BUFFER_SIZE (outbuf) = GST_BUFFER_SIZE (buf) + len;

  gst_buffer_unref (buf);

  gst_pad_push (filter->srcpad, GST_DATA (outbuf));
}

static void
gst_y4mencode_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstY4mEncode *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_Y4MENCODE (object));
  filter = GST_Y4MENCODE (object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_y4mencode_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstY4mEncode *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_Y4MENCODE (object));
  filter = GST_Y4MENCODE (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstElementStateReturn
gst_y4mencode_change_state (GstElement * element)
{
  GstY4mEncode *filter;

  g_return_val_if_fail (GST_IS_Y4MENCODE (element), GST_STATE_FAILURE);

  filter = GST_Y4MENCODE (element);

  if (GST_STATE_TRANSITION (element) == GST_STATE_NULL_TO_READY) {
    filter->init = TRUE;
  }

  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "y4menc", GST_RANK_NONE,
      GST_TYPE_Y4MENCODE);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "y4menc",
    "Encodes a YUV frame into the yuv4mpeg format (mjpegtools)",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE, GST_ORIGIN)
