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
/* based on the Area Based Deinterlacer (for RGB frames)        */
/* (a VirtualDub filter) from Gunnar Thalin <guth@home.se>      */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstdeinterlace.h"

/* elementfactory information */
static GstElementDetails deinterlace_details = GST_ELEMENT_DETAILS (
  "Deinterlace",
  "Filter/Effect/Video",
  "Deinterlace video",
  "Wim Taymans <wim.taymans@chello.be>"
);


/* Filter signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_DI_ONLY,
  ARG_BLEND,
  ARG_THRESHOLD,
  ARG_EDGE_DETECT,
};

static GstStaticPadTemplate deinterlace_src_factory =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ( GST_VIDEO_YUV_PAD_TEMPLATE_CAPS ("I420"))
);

static GstStaticPadTemplate deinterlace_sink_factory =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS ( GST_VIDEO_YUV_PAD_TEMPLATE_CAPS ("I420"))
);

static GType 		gst_deinterlace_get_type		(void);

static void             gst_deinterlace_base_init               (gpointer g_class);
static void		gst_deinterlace_class_init		(GstDeInterlaceClass *klass);
static void		gst_deinterlace_init			(GstDeInterlace *filter);

static void		gst_deinterlace_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void		gst_deinterlace_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static void		gst_deinterlace_chain			(GstPad *pad, GstData *_data);

static GstElementClass *parent_class = NULL;
/*static guint gst_filter_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_deinterlace_get_type(void) {
  static GType deinterlace_type = 0;

  if (!deinterlace_type) {
    static const GTypeInfo deinterlace_info = {
      sizeof(GstDeInterlaceClass),      
      gst_deinterlace_base_init,
      NULL,
      (GClassInitFunc)gst_deinterlace_class_init,
      NULL,
      NULL,
      sizeof(GstDeInterlace),
      0,
      (GInstanceInitFunc)gst_deinterlace_init,
    };
    deinterlace_type = g_type_register_static(GST_TYPE_ELEMENT, "GstDeInterlace", &deinterlace_info, 0);
  }
  return deinterlace_type;
}

static void
gst_deinterlace_base_init (gpointer g_class)
{ 
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, 
		  gst_static_pad_template_get (&deinterlace_src_factory));
  gst_element_class_add_pad_template (element_class, 
		  gst_static_pad_template_get (&deinterlace_sink_factory));

  gst_element_class_set_details (element_class, &deinterlace_details);
}

static void
gst_deinterlace_class_init (GstDeInterlaceClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_DI_ONLY,
    g_param_spec_boolean("di_area_only","di_area_only","di_area_only",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BLEND,
    g_param_spec_boolean("blend","blend","blend",
                         TRUE,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_THRESHOLD,
    g_param_spec_int("threshold","threshold","threshold",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */
  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_EDGE_DETECT,
    g_param_spec_int("edge_detect","edge_detect","edge_detect",
                     G_MININT,G_MAXINT,0,G_PARAM_READWRITE)); /* CHECKME */

  gobject_class->set_property = gst_deinterlace_set_property;
  gobject_class->get_property = gst_deinterlace_get_property;
}

static GstPadLinkReturn
gst_deinterlace_link (GstPad *pad, const GstCaps *caps)
{
  GstDeInterlace *filter;
  GstStructure *structure;
  GstPadLinkReturn ret;

  filter = GST_DEINTERLACE(gst_pad_get_parent (pad));
  
  ret = gst_pad_try_set_caps (filter->srcpad, caps);
  if (GST_PAD_LINK_FAILED (ret)) {
    return ret;
  }

  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "width", &filter->width);
  gst_structure_get_int (structure, "height", &filter->height);

  if (filter->picsize != (filter->width*filter->height)) {
    if (filter->src) 
      g_free(filter->src);
    filter->picsize = filter->width*filter->height;
    filter->src = g_malloc(filter->picsize);
  }

  return GST_PAD_LINK_OK;
}

static void
gst_deinterlace_init (GstDeInterlace *filter)
{
  filter->sinkpad = gst_pad_new_from_template(
      gst_static_pad_template_get(&deinterlace_sink_factory),"sink");
  gst_pad_set_chain_function(filter->sinkpad,gst_deinterlace_chain);
  gst_pad_set_link_function(filter->sinkpad,gst_deinterlace_link);
  gst_element_add_pad(GST_ELEMENT(filter),filter->sinkpad);

  filter->srcpad = gst_pad_new_from_template(
      gst_static_pad_template_get(&deinterlace_src_factory),"src");
  gst_pad_set_link_function(filter->srcpad,gst_deinterlace_link);
  gst_element_add_pad(GST_ELEMENT(filter),filter->srcpad);

  filter->show_deinterlaced_area_only = FALSE;
  filter->blend = FALSE;
  /*filter->threshold_blend = 0;  */
  filter->threshold = 50;
  filter->edge_detect = 25;

  filter->src = NULL;
  filter->picsize = 0;
}

static void
gst_deinterlace_chain (GstPad *pad, GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstDeInterlace *filter;
  gint y0, y1, y2, y3;
  guchar *psrc1, *psrc2, *psrc3, *pdst1, *yuvptr, *src;
  gint iInterlaceValue0, iInterlaceValue1, iInterlaceValue2;
  gint x, y;
  gint y_line;
  guchar *y_dst, *y_src;
  gboolean bBlend;
  gint iThreshold;
  gint iEdgeDetect;
  gint width, height;
  gboolean bShowDeinterlacedAreaOnly;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  filter = GST_DEINTERLACE (gst_pad_get_parent (pad));

  bBlend = filter->blend;
  iThreshold = filter->threshold;
  iEdgeDetect = filter->edge_detect;
  width = filter->width;
  height = filter->height;
  src = filter->src;
  yuvptr = GST_BUFFER_DATA (buf);
  bShowDeinterlacedAreaOnly = filter->show_deinterlaced_area_only;

  memcpy(filter->src, yuvptr, filter->picsize); 

  y_dst = yuvptr;  /* dst y pointer */
                   /* we should not change u,v because one u, v value stands for */
                   /* 2 pixels per 2 lines = 4 pixel and we don't want to change */
                   /* the color of */

  y_line  = width;
  y_src = src;

  iThreshold = iThreshold * iThreshold * 4;
  /* We don't want an integer overflow in the  interlace calculation. */
  if (iEdgeDetect > 180)
    iEdgeDetect = 180;
  iEdgeDetect = iEdgeDetect * iEdgeDetect;

  y1 = 0;		/* Avoid compiler warning. The value is not used. */
  for (x = 0; x < width; x++) {
    psrc3 = y_src + x;
    y3    = *psrc3;
    psrc2 = psrc3 + y_line;
    y2 = *psrc2;
    pdst1 = y_dst + x;
    iInterlaceValue1 = iInterlaceValue2 = 0;
    for (y = 0; y <= height; y++) {
      psrc1 = psrc2;
      psrc2 = psrc3;
      psrc3 = psrc3 + y_line;
      y0 = y1;
      y1 = y2;
      y2 = y3;
      if (y < height - 1) {
        y3 = *psrc3;
      } else {
        y3 = y1;
      }

      iInterlaceValue0 = iInterlaceValue1;
      iInterlaceValue1 = iInterlaceValue2;

      if (y < height)
        iInterlaceValue2 = ((y1 - y2) * (y3 - y2) - 
                                  ((iEdgeDetect * (y1 - y3) * (y1 - y3)) >> 12))*10;
      else
        iInterlaceValue2 = 0;

      if (y > 0) {			
        if (iInterlaceValue0 + 2 * iInterlaceValue1 + iInterlaceValue2 > iThreshold) {
          if (bBlend) { 
            *pdst1 = (unsigned char)((y0 + 2*y1 + y2) >> 2);
          } else {
            /* this method seems to work better than blending if the */
            /* quality is pretty bad and the half pics don't fit together */
            if ((y % 2)==1) {  /* if odd simply copy the value */
              *pdst1 = *psrc1;
              /**pdst1 = 0; // FIXME this is for adjusting an initial iThreshold */
            } else {        /* even interpolate the even line (upper + lower)/2 */
              *pdst1 = (unsigned char)((y0 + y2) >> 1);
              /**pdst1 = 0; // FIXME this is for adjusting an initial iThreshold */
            }
          } 
        } else {
          /* so we went below the treshold and therefore we don't have to  */
          /* change anything */
          if (bShowDeinterlacedAreaOnly) {
                /* this is for testing to see how we should tune the treshhold */
                /* and shows as the things that haven't change because the  */
                /* threshhold was to low?? (or shows that everything is ok :-) */
            *pdst1 = 0; /* blank the point and so the interlac area */
          } else {
            *pdst1 = *psrc1;
          }
        }
        pdst1 = pdst1 + y_line;
      }
    }
  }

  gst_pad_push (filter->srcpad, GST_DATA (buf));
}

static void
gst_deinterlace_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstDeInterlace *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DEINTERLACE(object));

  filter = GST_DEINTERLACE(object);

  switch (prop_id) 
  {
    case ARG_DI_ONLY:
      filter->show_deinterlaced_area_only = g_value_get_boolean (value);
      break;
    case ARG_BLEND:
      filter->blend = g_value_get_boolean (value);
      break;
    case ARG_THRESHOLD:
      filter->threshold = g_value_get_int (value);
      break;
    case ARG_EDGE_DETECT:
      filter->edge_detect = g_value_get_int (value);
      break;
    default:
      break;
  }
}

static void
gst_deinterlace_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstDeInterlace *filter;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_DEINTERLACE(object));

  filter = GST_DEINTERLACE(object);

  switch (prop_id) {
    case ARG_DI_ONLY:
      g_value_set_boolean (value, filter->show_deinterlaced_area_only);
      break;
    case ARG_BLEND:
      g_value_set_boolean (value, filter->blend);
      break;
    case ARG_THRESHOLD:
      g_value_set_int (value, filter->threshold);
      break;
    case ARG_EDGE_DETECT:
      g_value_set_int (value, filter->edge_detect);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "deinterlace", GST_RANK_NONE, gst_deinterlace_get_type()))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "deinterlace",
  "Deinterlace video",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN  
);
