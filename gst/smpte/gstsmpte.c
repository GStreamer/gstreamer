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
#include <gstsmpte.h>
#include <gst/video/video.h>
#include "paint.h"

/* elementfactory information */
static GstElementDetails smpte_details = {
  "SMPTE transitions",
  "Filter/Editor/Video",
  "Apply the standard SMPTE transitions on video images",
  "Wim Taymans <wim.taymans@chello.be>"
};

GST_PAD_TEMPLATE_FACTORY (smpte_src_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  gst_caps_new (
   "smpte_src",
   "video/x-raw-yuv",
     GST_VIDEO_YUV_PAD_TEMPLATE_PROPS(
	     GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")))
  )
)

GST_PAD_TEMPLATE_FACTORY (smpte_sink1_factory,
  "sink1",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  gst_caps_new (
   "smpte_sink1",
   "video/x-raw-yuv",
     GST_VIDEO_YUV_PAD_TEMPLATE_PROPS(
	     GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")))
  )
)

GST_PAD_TEMPLATE_FACTORY (smpte_sink2_factory,
  "sink2",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  gst_caps_new (
   "smpte_sink2",
   "video/x-raw-yuv",
     GST_VIDEO_YUV_PAD_TEMPLATE_PROPS(
	     GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")))
  )
)


/* SMPTE signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_TYPE,
  ARG_BORDER,
  ARG_DEPTH,
  ARG_FPS,
};

#define GST_TYPE_SMPTE_TRANSITION_TYPE (gst_smpte_transition_type_get_type())
static GType
gst_smpte_transition_type_get_type (void) 
{
  static GType smpte_transition_type = 0;
  GEnumValue *smpte_transitions;

  if (!smpte_transition_type) {
    const GList *definitions;
    gint i=0;

    definitions = gst_mask_get_definitions ();
    smpte_transitions = g_new0 (GEnumValue, g_list_length ((GList *)definitions)+1);

    while (definitions) {
      GstMaskDefinition *definition = (GstMaskDefinition *) definitions->data;
      definitions = g_list_next (definitions);

      smpte_transitions[i].value = definition->type;
      smpte_transitions[i].value_name = definition->short_name;
      smpte_transitions[i].value_nick = definition->long_name;
      
      i++;
    }

    smpte_transition_type = 
	    g_enum_register_static ("GstSMPTETransitionType", smpte_transitions);
  }
  return smpte_transition_type;
}   


static void	gst_smpte_class_init		(GstSMPTEClass *klass);
static void	gst_smpte_base_init		(GstSMPTEClass *klass);
static void	gst_smpte_init			(GstSMPTE *smpte);

static void	gst_smpte_loop			(GstElement *element);

static void	gst_smpte_set_property		(GObject *object, guint prop_id, 
						 const GValue *value, GParamSpec *pspec);
static void	gst_smpte_get_property		(GObject *object, guint prop_id, 
						 GValue *value, GParamSpec *pspec);

static GstElementClass *parent_class = NULL;
/*static guint gst_smpte_signals[LAST_SIGNAL] = { 0 }; */

static GType
gst_smpte_get_type (void)
{
  static GType smpte_type = 0;

  if (!smpte_type) {
    static const GTypeInfo smpte_info = {
      sizeof(GstSMPTEClass),      
      (GBaseInitFunc)gst_smpte_base_init,
      NULL,
      (GClassInitFunc)gst_smpte_class_init,
      NULL,
      NULL,
      sizeof(GstSMPTE),
      0,
      (GInstanceInitFunc)gst_smpte_init,
    };
    smpte_type = g_type_register_static(GST_TYPE_ELEMENT, "GstSMPTE", &smpte_info, 0);
  }
  return smpte_type;
}

static void
gst_smpte_base_init (GstSMPTEClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class, 
		  GST_PAD_TEMPLATE_GET (smpte_sink1_factory));
  gst_element_class_add_pad_template (element_class, 
		  GST_PAD_TEMPLATE_GET (smpte_sink2_factory));
  gst_element_class_add_pad_template (element_class, 
		  GST_PAD_TEMPLATE_GET (smpte_src_factory));
  gst_element_class_set_details (element_class, &smpte_details);
}

static void
gst_smpte_class_init (GstSMPTEClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_smpte_set_property;
  gobject_class->get_property = gst_smpte_get_property;

  _gst_mask_init ();

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_TYPE,
    g_param_spec_enum ("type", "Type", "The type of transition to use",
                       GST_TYPE_SMPTE_TRANSITION_TYPE, 1, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_FPS,
    g_param_spec_float ("fps", "FPS", "Frames per second if no input files are given",
                      0., G_MAXFLOAT, 25., G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BORDER,
    g_param_spec_int ("border", "Border", "The border width of the transition",
                      0, G_MAXINT, 0, G_PARAM_READWRITE));
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_DEPTH,
    g_param_spec_int ("depth", "Depth", "Depth of the mask in bits",
                      1, 24, 16, G_PARAM_READWRITE));
}

/*                        wht  yel  cya  grn  mag  red  blu  blk   -I    Q */
static int y_colors[] = { 255, 226, 179, 150, 105,  76,  29,  16,  16,   0 };
static int u_colors[] = { 128,   0, 170,  46, 212,  85, 255, 128,   0, 128 };
static int v_colors[] = { 128, 155,   0,  21, 235, 255, 107, 128, 128, 255 };

static void
fill_i420 (guint8 *data, gint width, gint height, gint color)
{
  gint size = width * height, size4 = size >> 2;
  guint8 *yp = data;
  guint8 *up = data + size;
  guint8 *vp = data + size + size4;
  
  memset (yp, y_colors[color], size);
  memset (up, u_colors[color], size4);
  memset (vp, v_colors[color], size4);
}

static gboolean
gst_smpte_update_mask (GstSMPTE *smpte, gint type, gint depth, gint width, gint height)
{
  GstMask *newmask;

  newmask = gst_mask_factory_new (type, depth, width, height);
  if (newmask) {
    if (smpte->mask) {
      gst_mask_destroy (smpte->mask);
    }
    smpte->mask = newmask;
    smpte->type = type;
    smpte->depth = depth;
    smpte->width = width;
    smpte->height = height;

    return TRUE;
  }
  return FALSE;
}

static gboolean
gst_smpte_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstSMPTE *smpte;

  smpte = GST_SMPTE (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps))
    return GST_PAD_LINK_DELAYED;

  gst_caps_get_int (caps, "width", &smpte->width);
  gst_caps_get_int (caps, "height", &smpte->height);
  gst_caps_get_float (caps, "framerate", &smpte->fps);

  gst_smpte_update_mask (smpte, smpte->type, smpte->depth, smpte->width, smpte->height);

  /* forward to the next plugin */
  return gst_pad_try_set_caps(smpte->srcpad, gst_caps_copy_1(caps));
}

static void 
gst_smpte_init (GstSMPTE *smpte)
{
  smpte->sinkpad1 = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (smpte_sink1_factory), "sink1");
  gst_pad_set_link_function (smpte->sinkpad1, gst_smpte_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (smpte), smpte->sinkpad1);

  smpte->sinkpad2 = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (smpte_sink2_factory), "sink2");
  gst_pad_set_link_function (smpte->sinkpad2, gst_smpte_sinkconnect);
  gst_element_add_pad (GST_ELEMENT (smpte), smpte->sinkpad2);

  smpte->srcpad = gst_pad_new_from_template (
		  GST_PAD_TEMPLATE_GET (smpte_src_factory), "src");
  gst_element_add_pad (GST_ELEMENT (smpte), smpte->srcpad);

  gst_element_set_loop_function (GST_ELEMENT (smpte), gst_smpte_loop);

  smpte->width = 320;
  smpte->height = 200;
  smpte->fps = 25.;
  smpte->duration = 64;
  smpte->position = 0;
  smpte->type = 1;
  smpte->border = 0;
  smpte->depth = 16;
  gst_smpte_update_mask (smpte, smpte->type, smpte->depth, smpte->width, smpte->height);
}

static void
gst_smpte_blend_i420 (guint8 *in1, guint8 *in2, guint8 *out, GstMask *mask,
		      gint width, gint height, gint border, gint pos)
{
  guint32 *maskp;
  gint value;
  gint i, j;
  gint min, max;
  guint8 *in1u, *in1v, *in2u, *in2v, *outu, *outv; 
  gint lumsize = width * height;
  gint chromsize = lumsize >> 2;

  if (border == 0) border++;

  min = pos - border; 
  max = pos;

  in1u = in1 + lumsize; in1v = in1u + chromsize;
  in2u = in2 + lumsize; in2v = in2u + chromsize;
  outu = out + lumsize; outv = outu + chromsize;
  
  maskp = mask->data;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      value = *maskp++;
      value = ((CLAMP (value, min, max) - min) << 8) / border;
    
      *out++ = ((*in1++ * value) + (*in2++ * (256 - value))) >> 8;
      if (!(i & 1) && !(j & 1)) {
        *outu++ = ((*in1u++ * value) + (*in2u++ * (256 - value))) >> 8;
        *outv++ = ((*in1v++ * value) + (*in2v++ * (256 - value))) >> 8;
      }
    }
  }
}

static void
gst_smpte_loop (GstElement *element)
{
  GstSMPTE *smpte;
  GstBuffer *outbuf;
  GstClockTime ts;
  GstBuffer *in1 = NULL, *in2 = NULL;

  smpte = GST_SMPTE (element);

  ts = smpte->position * GST_SECOND / smpte->fps;

  while (GST_PAD_IS_USABLE (smpte->sinkpad1) && in1 == NULL) {
    in1 = GST_BUFFER (gst_pad_pull (smpte->sinkpad1));
    if (GST_IS_EVENT (in1)) {
      gst_pad_push (smpte->srcpad, GST_DATA (in1));
      in1 = NULL;
    }
    else 
      ts = GST_BUFFER_TIMESTAMP (in1);
  }
  if (GST_PAD_IS_USABLE (smpte->sinkpad2) && in2 == NULL) {
    in2 = GST_BUFFER (gst_pad_pull (smpte->sinkpad2));
    if (GST_IS_EVENT (in2)) {
      gst_pad_push (smpte->srcpad, GST_DATA (in2));
      in2 = NULL;
    }
    else 
      ts = GST_BUFFER_TIMESTAMP (in2);
  }

  if (in1 == NULL) {
    in1 = gst_buffer_new_and_alloc (smpte->width * smpte->height * 3);
    fill_i420 (GST_BUFFER_DATA (in1), smpte->width, smpte->height, 7);
  }
  if (in2 == NULL) {
    in2 = gst_buffer_new_and_alloc (smpte->width * smpte->height * 3);
    fill_i420 (GST_BUFFER_DATA (in2), smpte->width, smpte->height, 0);
  }

  if (smpte->position < smpte->duration) { 
    outbuf = gst_buffer_new_and_alloc (smpte->width * smpte->height * 3);

    if (!GST_PAD_CAPS (smpte->srcpad)) {
      if (!gst_pad_try_set_caps (smpte->srcpad,
	    GST_CAPS_NEW (
		    "smpte_srccaps",
		    "video/raw",
		      "format",   GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I','4','2','0')),
		      "width",    GST_PROPS_INT (smpte->width),
		      "height",   GST_PROPS_INT (smpte->height),
                      "framerate", GST_PROPS_FLOAT (smpte->fps)
		    )))
      {
        gst_element_error (element, "cannot set caps");
        return;
      }
    }

    gst_smpte_blend_i420 (GST_BUFFER_DATA (in1), 
		          GST_BUFFER_DATA (in2), 
			  GST_BUFFER_DATA (outbuf),
	                  smpte->mask, smpte->width, smpte->height, 
			  smpte->border,
			  ((1 << smpte->depth) + smpte->border) * 
			    smpte->position / smpte->duration);
  }
  else {
    outbuf = in2;
    gst_buffer_ref (in2);
  }

  smpte->position++;

  if (in1)
    gst_buffer_unref (in1);
  if (in2)
    gst_buffer_unref (in2);

  GST_BUFFER_TIMESTAMP (outbuf) = ts;
  gst_pad_push (smpte->srcpad, GST_DATA (outbuf));
}

static void
gst_smpte_set_property (GObject *object, guint prop_id, 
		        const GValue *value, GParamSpec *pspec)
{
  GstSMPTE *smpte;

  smpte = GST_SMPTE(object);

  switch (prop_id) {
    case ARG_TYPE:
    {
      gint type = g_value_get_enum (value);

      gst_smpte_update_mask (smpte, type, smpte->depth, 
		             smpte->width, smpte->height);
      break;
    }
    case ARG_BORDER:
      smpte->border = g_value_get_int (value);
      break;
    case ARG_FPS:
      smpte->fps = g_value_get_float (value);
      break;
    case ARG_DEPTH:
    {
      gint depth = g_value_get_int (value);

      gst_smpte_update_mask (smpte, smpte->type, depth, 
		             smpte->width, smpte->height);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_smpte_get_property (GObject *object, guint prop_id, 
		        GValue *value, GParamSpec *pspec)
{
  GstSMPTE *smpte;

  smpte = GST_SMPTE(object);

  switch (prop_id) {
    case ARG_TYPE:
      if (smpte->mask) {
	g_value_set_enum (value, smpte->mask->type);
      }
      break;
    case ARG_FPS:
      g_value_set_float (value, smpte->fps);
      break;
    case ARG_BORDER:
      g_value_set_int (value, smpte->border);
      break;
    case ARG_DEPTH:
      g_value_set_int (value, smpte->depth);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GstPlugin *plugin)
{
  return gst_element_register(plugin, "smpte",
			      GST_RANK_NONE, GST_TYPE_SMPTE);
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "smpte",
  "Apply the standard SMPTE transitions on video images",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
