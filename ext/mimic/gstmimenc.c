   /* 
 * GStreamer
 * Copyright (c) 2005 INdT.
 * @author Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>
 * @author Philippe Khalaf <burger@speedy.org>
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

#include <gst/gst.h>

#include "gstmimenc.h"

GST_DEBUG_CATEGORY (mimenc_debug);
#define GST_CAT_DEFAULT (mimenc_debug)

#define MAX_INTERFRAMES 15

static GstStaticPadTemplate sink_factory =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      "video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) 4321, "
        "framerate = (fraction) [1/1, 30/1], "
        "red_mask = (int) 16711680, "
        "green_mask = (int) 65280, "
        "blue_mask = (int) 255, "
        "width = (int) 320, "
        "height = (int) 240"
      ";video/x-raw-rgb, "
        "bpp = (int) 24, "
        "depth = (int) 24, "
        "endianness = (int) 4321, "
        "framerate = (fraction) [1/1, 30/1], "
        "red_mask = (int) 16711680, "
        "green_mask = (int) 65280, "
        "blue_mask = (int) 255, "
        "width = (int) 160, "
        "height = (int) 120"
    )
);

static GstStaticPadTemplate src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ("video/x-msnmsgr-webcam")
);


static void          gst_mimenc_class_init	      (GstMimEncClass *klass);
static void          gst_mimenc_base_init	      (GstMimEncClass *klass);
static void          gst_mimenc_init              (GstMimEnc      *mimenc);

static gboolean      gst_mimenc_setcaps           (GstPad         *pad, 
                                                   GstCaps        *caps);
static GstFlowReturn gst_mimenc_chain             (GstPad         *pad, 
                                                   GstBuffer      *in);
static GstBuffer*    gst_mimenc_create_tcp_header (GstMimEnc      *mimenc, 
                                                   gint            payload_size);

static GstStateChangeReturn
                     gst_mimenc_change_state      (GstElement     *element,
                                                  GstStateChange   transition);

static GstElementClass *parent_class = NULL;

GType
gst_gst_mimenc_get_type (void)
{
  static GType plugin_type = 0;

  if (!plugin_type)
  {
    static const GTypeInfo plugin_info =
    {
      sizeof (GstMimEncClass),
      (GBaseInitFunc) gst_mimenc_base_init,
      NULL,
      (GClassInitFunc) gst_mimenc_class_init,
      NULL,
      NULL,
      sizeof (GstMimEnc),
      0,
      (GInstanceInitFunc) gst_mimenc_init,
    };
    plugin_type = g_type_register_static (GST_TYPE_ELEMENT,
   	                                      "GstMimEnc",
                                          &plugin_info, 0);
  }
  return plugin_type;
}

static void
gst_mimenc_base_init (GstMimEncClass *klass)
{
  static GstElementDetails plugin_details = {
    "MimEnc",
    "Codec/Encoder/Video",
    "Mimic encoder",
    "Andre Moreira Magalhaes <andre.magalhaes@indt.org.br>"
  };
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (element_class,
	gst_static_pad_template_get (&sink_factory));
  gst_element_class_set_details (element_class, &plugin_details);
}

static void
gst_mimenc_class_init (GstMimEncClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*) klass;
  gstelement_class = (GstElementClass*) klass;
  gstelement_class->change_state = gst_mimenc_change_state;

  parent_class = g_type_class_peek_parent (klass);

  GST_DEBUG_CATEGORY_INIT (mimenc_debug, "mimenc", 0, "Mimic encoder plugin");
}

static void
gst_mimenc_init (GstMimEnc *mimenc)
{
  mimenc->sinkpad = gst_pad_new_from_template (
	gst_static_pad_template_get (&sink_factory), "sink");
  gst_element_add_pad (GST_ELEMENT (mimenc), mimenc->sinkpad);
  gst_pad_set_setcaps_function (mimenc->sinkpad, gst_mimenc_setcaps);
  gst_pad_set_chain_function (mimenc->sinkpad, gst_mimenc_chain);

  mimenc->srcpad = gst_pad_new_from_template (
	gst_static_pad_template_get (&src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (mimenc), mimenc->srcpad);

  mimenc->enc = NULL;

  // TODO property to set resolution
  mimenc->res = MIMIC_RES_HIGH;
  mimenc->buffer_size = -1;
  mimenc->width = 0;
  mimenc->height = 0;
  mimenc->frames = 0;
}

static gboolean
gst_mimenc_setcaps (GstPad *pad, GstCaps *caps)
{
  GstMimEnc *filter;
  GstStructure *structure;
  int ret = TRUE, height, width;

  filter = GST_MIMENC (gst_pad_get_parent (pad));
  g_return_val_if_fail (filter != NULL, FALSE);
  g_return_val_if_fail (GST_IS_MIMENC (filter), FALSE);

  structure = gst_caps_get_structure( caps, 0 );
  ret = gst_structure_get_int( structure, "width", &width );
  if (!ret) {
    GST_DEBUG_OBJECT (filter, "No width set");
    goto out;
  }
  ret = gst_structure_get_int( structure, "height", &height );
  if (!ret) {
    GST_DEBUG_OBJECT (filter, "No height set");
    goto out;
  }

  if (width == 320 && height == 240)
    filter->res = MIMIC_RES_HIGH;
  else if (width == 160 && height == 120)
    filter->res = MIMIC_RES_LOW;
  else {
    GST_WARNING_OBJECT (filter, "Invalid resolution %dx%d", width, height);
    ret = FALSE;
    goto out;
  }

  filter->width = (guint16)width;
  filter->height = (guint16)height;

  GST_DEBUG_OBJECT (filter,"Got info from caps w : %d, h : %d",
      filter->width, filter->height);
 out:
  gst_object_unref(filter);
  return ret;
}

static GstFlowReturn
gst_mimenc_chain (GstPad *pad, GstBuffer *in)
{
  GstMimEnc *mimenc;
  GstBuffer *out_buf, *buf;
  guchar *data;
  gint buffer_size;
  GstBuffer * header = NULL;

  g_return_val_if_fail (GST_IS_PAD (pad), GST_FLOW_ERROR);
  mimenc = GST_MIMENC (GST_OBJECT_PARENT (pad));

  g_return_val_if_fail (GST_IS_MIMENC (mimenc), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_PAD_IS_LINKED (mimenc->srcpad), GST_FLOW_ERROR);

  if (mimenc->enc == NULL) {
    mimenc->enc = mimic_open ();
    if (mimenc->enc == NULL) {
      GST_WARNING ("mimic_open error\n");
      return GST_FLOW_ERROR;
    }
    
    if (!mimic_encoder_init (mimenc->enc, mimenc->res)) {
      GST_WARNING ("mimic_encoder_init error\n");
      mimic_close (mimenc->enc);
      mimenc->enc = NULL;
      return GST_FLOW_ERROR;
    }
    
    if (!mimic_get_property (mimenc->enc, "buffer_size", &mimenc->buffer_size)) {
      GST_WARNING ("mimic_get_property('buffer_size') error\n");
      mimic_close (mimenc->enc);
      mimenc->enc = NULL;
      return GST_FLOW_ERROR;
    }
  }

  buf = in;
  data = GST_BUFFER_DATA (buf);

  out_buf = gst_buffer_new_and_alloc (mimenc->buffer_size);
  GST_BUFFER_TIMESTAMP(out_buf) = GST_BUFFER_TIMESTAMP(buf);
  buffer_size = mimenc->buffer_size;
  if (!mimic_encode_frame (mimenc->enc, data, GST_BUFFER_DATA (out_buf), 
      &buffer_size, ((mimenc->frames % MAX_INTERFRAMES) == 0 ? TRUE : FALSE))) {
    GST_WARNING ("mimic_encode_frame error\n");
    gst_buffer_unref (out_buf);
    gst_buffer_unref (buf);
    return GST_FLOW_ERROR;
  }
  GST_BUFFER_SIZE (out_buf) = buffer_size;

  GST_DEBUG ("incoming buf size %d, encoded size %d", GST_BUFFER_SIZE(buf), GST_BUFFER_SIZE(out_buf));
  ++mimenc->frames;

  // now let's create that tcp header
  header = gst_mimenc_create_tcp_header (mimenc, buffer_size);

  if (header)
  {
      gst_pad_push (mimenc->srcpad, header);
      gst_pad_push (mimenc->srcpad, out_buf);
  }
  else
  {
      GST_DEBUG("header not created succesfully");
      return GST_FLOW_ERROR;
  }

  gst_buffer_unref (buf);

  return GST_FLOW_OK;
}

static GstBuffer* 
gst_mimenc_create_tcp_header (GstMimEnc *mimenc, gint payload_size)
{
    // 24 bytes
    GstBuffer *buf_header = gst_buffer_new_and_alloc (24);
    guchar *p = (guchar *) GST_BUFFER_DATA(buf_header);

    p[0] = 24;
    *((guchar *) (p + 1)) = 0;
    *((guint16 *) (p + 2)) = GUINT16_TO_LE(mimenc->width);
    *((guint16 *) (p + 4)) = GUINT16_TO_LE(mimenc->height);
    *((guint16 *) (p + 6)) = 0;
    *((guint32 *) (p + 8)) = GUINT32_TO_LE(payload_size); 
    *((guint32 *) (p + 12)) = GUINT32_TO_LE(GST_MAKE_FOURCC ('M', 'L', '2', '0')); 
    *((guint32 *) (p + 16)) = 0; 
    *((guint32 *) (p + 20)) = 0; /* FIXME: must be timestamp */

    return buf_header;
}

static GstStateChangeReturn
gst_mimenc_change_state (GstElement * element, GstStateChange transition)
{
  GstMimEnc *mimenc;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      mimenc = GST_MIMENC (element);
      if (mimenc->enc != NULL) {
        mimic_close (mimenc->enc);
        mimenc->enc = NULL;
        mimenc->buffer_size = -1;
        mimenc->frames = 0;
      }
      break;

    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}
