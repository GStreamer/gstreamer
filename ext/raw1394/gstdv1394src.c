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

#include <gst/gst.h>
#include "gstdv1394src.h"


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
};

static GstPadTemplate*
gst_dv1394src_factory (void)
{
  static GstPadTemplate *template = NULL;

  if (!template) {
    template = gst_pad_template_new (
      "src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      gst_caps_new (
        "dv1394src",
        "video/dv",
	gst_props_new (
          "format", GST_PROPS_STRING ("NTSC"),
	  NULL)),
      NULL);
  }
  return template;
}

static void		gst_dv1394src_class_init		(GstDV1394SrcClass *klass);
static void		gst_dv1394src_init		(GstDV1394Src *filter);

static void		gst_dv1394src_set_property		(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_dv1394src_get_property		(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static GstElementStateReturn	gst_dv1394src_change_state	(GstElement *element);

static GstBuffer *	gst_dv1394src_get			(GstPad *pad);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_dv1394src_get_type(void) {
  static GType gst_dv1394src_type = 0;

  if (!gst_dv1394src_type) {
    static const GTypeInfo gst_dv1394src_info = {
      sizeof(GstDV1394Src),      NULL,
      NULL,
      (GClassInitFunc)gst_dv1394src_class_init,
      NULL,
      NULL,
      sizeof(GstDV1394Src),
      0,
      (GInstanceInitFunc)gst_dv1394src_init,
    };
    gst_dv1394src_type = g_type_register_static(GST_TYPE_ELEMENT, "DV1394Src", &gst_dv1394src_info, 0);
  }
  return gst_dv1394src_type;
}

static void
gst_dv1394src_class_init (GstDV1394SrcClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_dv1394src_set_property;
  gobject_class->get_property = gst_dv1394src_get_property;

  gstelement_class->change_state = gst_dv1394src_change_state;
}

static void
gst_dv1394src_init (GstDV1394Src *dv1394src)
{
  dv1394src->srcpad = gst_pad_new ("src", GST_PAD_SRC);
  gst_pad_set_get_function (dv1394src->srcpad, gst_dv1394src_get);
  gst_element_add_pad (GST_ELEMENT (dv1394src), dv1394src->srcpad);

  dv1394src->card = 0;
  dv1394src->port = 0;
  dv1394src->channel = 63;
}

static void
gst_dv1394src_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDV1394Src *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DV1394SRC(object));
  filter = GST_DV1394SRC(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_dv1394src_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDV1394Src *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DV1394SRC(object));
  filter = GST_DV1394SRC(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static
int gst_dv1394src_iso_receive(raw1394handle_t handle,int channel,size_t len,quadlet_t *data) {
  GstDV1394Src *dv1394src = GST_DV1394SRC (raw1394_get_userdata(handle));
  unsigned char *ptr = (unsigned char *)&data[3];
  GstBuffer *buf;

  if (len > 16) {
/*fprintf(stderr,"section_type %d, dif_sequence %d, dif_block %d\n",ptr[0] >> 5,ptr[1] >> 4,ptr[2]); */
fprintf(stderr,".");
    if (((ptr[0] >> 5) == 0) &&
        ((ptr[1] >> 4) == 0) && (ptr[2] == 0)) dv1394src->started = TRUE;
    if (dv1394src->started) {
      buf = gst_buffer_new();
      GST_BUFFER_DATA(buf) = ptr;
      GST_BUFFER_SIZE(buf) = 480;
      GST_BUFFER_OFFSET(buf) = 0;
      GST_BUFFER_FLAG_SET(buf,GST_BUFFER_DONTFREE);

      dv1394src->buf = buf;
    }
  }

  return 0;
}

static
int gst_dv1394src_bus_reset(raw1394handle_t handle) {
  GST_INFO_ELEMENT(0,GST_DV1394SRC(raw1394_get_userdata(handle)),"have bus reset");
  return 0;
}

static GstBuffer *
gst_dv1394src_get (GstPad *pad)
{
  GstDV1394Src *dv1394src = GST_DV1394SRC (GST_PAD_PARENT(pad));

  dv1394src->buf = NULL;
  while (dv1394src->buf == NULL)
    raw1394_loop_iterate(dv1394src->handle);

  return dv1394src->buf;
}  

static GstElementStateReturn
gst_dv1394src_change_state (GstElement *element)
{
  GstDV1394Src *dv1394src;

  g_return_val_if_fail (GST_IS_DV1394SRC (element), GST_STATE_FAILURE);
  dv1394src = GST_DV1394SRC (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_NULL_TO_READY:
      if ((dv1394src->handle = raw1394_get_handle()) == NULL) {
        GST_INFO_ELEMENT(0,dv1394src,"can't get raw1394 handle");
        return GST_STATE_FAILURE;
      }
      raw1394_set_userdata(dv1394src->handle,dv1394src);
      dv1394src->numcards = raw1394_get_port_info(dv1394src->handle,dv1394src->pinfo,16);
      if (dv1394src->numcards == 0) {
        GST_INFO_ELEMENT(0,dv1394src,"no cards available for raw1394");
        return GST_STATE_FAILURE;
      }
      if (dv1394src->pinfo[dv1394src->card].nodes <= 1) {
        GST_INFO_ELEMENT(0,dv1394src,"there are no nodes on the 1394 bus");
        return GST_STATE_FAILURE;
      }
      if (raw1394_set_port(dv1394src->handle,dv1394src->port) < 0) {
        GST_INFO_ELEMENT(0,dv1394src,"can't set 1394 port %d",dv1394src->port);
        return GST_STATE_FAILURE;
      }
      raw1394_set_iso_handler(dv1394src->handle,dv1394src->channel,gst_dv1394src_iso_receive);
      raw1394_set_bus_reset_handler(dv1394src->handle,gst_dv1394src_bus_reset);
      dv1394src->started = FALSE;
      GST_DEBUG(0,"successfully opened up 1394 connection");
      break;
    case GST_STATE_PAUSED_TO_PLAYING:
      if (raw1394_start_iso_rcv(dv1394src->handle,dv1394src->channel) < 0) {
        GST_INFO_ELEMENT(0,dv1394src,"can't start 1394 iso receive");
        return GST_STATE_FAILURE;
      }
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      raw1394_stop_iso_rcv(dv1394src->handle, dv1394src->channel);
      break;
    case GST_STATE_READY_TO_NULL:
      raw1394_destroy_handle(dv1394src->handle);
      break;
    default:
      break;
  }

  /* if we haven't failed already, give the parent class a chance to ;-) */  
  if (GST_ELEMENT_CLASS (parent_class)->change_state)
    return GST_ELEMENT_CLASS (parent_class)->change_state (element);

  return GST_STATE_SUCCESS;
}
