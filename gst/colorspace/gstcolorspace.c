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
#include <gstcolorspace.h>
#include <gst/gst.h>
#include <gst/video/video.h>

#include "yuv2rgb.h"


static GstColorspaceFormat gst_colorspace_formats[] = {
  { GST_STATIC_CAPS (GST_VIDEO_YUV_PAD_TEMPLATE_CAPS("I420")) },
  { GST_STATIC_CAPS (GST_VIDEO_YUV_PAD_TEMPLATE_CAPS("YV12")) },
  { GST_STATIC_CAPS (GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_32) },
  { GST_STATIC_CAPS (GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24) },
  { GST_STATIC_CAPS (GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_16) },
};

static GstColorspaceConverter gst_colorspace_converters[] = {
  { GST_COLORSPACE_I420, GST_COLORSPACE_RGB32, gst_colorspace_I420_to_rgb32 },
  { GST_COLORSPACE_YV12, GST_COLORSPACE_RGB32, gst_colorspace_YV12_to_rgb32 },
  { GST_COLORSPACE_I420, GST_COLORSPACE_RGB24, gst_colorspace_I420_to_rgb24 },
  { GST_COLORSPACE_YV12, GST_COLORSPACE_RGB24, gst_colorspace_YV12_to_rgb24 },
  { GST_COLORSPACE_I420, GST_COLORSPACE_RGB16, gst_colorspace_I420_to_rgb16 },
  { GST_COLORSPACE_YV12, GST_COLORSPACE_RGB16, gst_colorspace_YV12_to_rgb16 },
};

static GstElementDetails colorspace_details = GST_ELEMENT_DETAILS (
  "Colorspace converter",
  "Filter/Converter/Video",
  "Converts video from one colorspace to another",
  "Wim Taymans <wim.taymans@chello.be>"
);

static GstStaticPadTemplate gst_colorspace_sink_template =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_YUV_PAD_TEMPLATE_CAPS ("{ I420, YV12 }"))
);

static GstStaticPadTemplate gst_colorspace_src_template =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_32 "; "
      GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24 "; "
      GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_16
    )
);

/* Stereo signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_SOURCE,
  ARG_DEST,
};

static void             gst_colorspace_base_init                (gpointer g_class);
static void		gst_colorspace_class_init		(GstColorspaceClass *klass);
static void		gst_colorspace_init			(GstColorspace *space);

static void		gst_colorspace_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void		gst_colorspace_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstPadLinkReturn
			gst_colorspace_link     		(GstPad *pad, const GstCaps *caps);
static void		gst_colorspace_chain			(GstPad *pad, GstData *_data);
static GstElementStateReturn
			gst_colorspace_change_state 		(GstElement *element);


static GstElementClass *parent_class = NULL;
/*static guint gst_colorspace_signals[LAST_SIGNAL] = { 0 }; */

#if 0
static gboolean 
colorspace_setup_converter (GstColorspace *space, GstCaps *from_caps, GstCaps *to_caps)
{
  guint32 from_space, to_space;
  GstStructure *from_struct;
  GstStructure *to_struct;

  g_return_val_if_fail (to_caps != NULL, FALSE);
  g_return_val_if_fail (from_caps != NULL, FALSE);

  from_struct = gst_caps_get_structure (from_caps, 0);
  to_struct = gst_caps_get_structure (to_caps, 0);

  from_space = GST_MAKE_FOURCC ('R','G','B',' ');
  gst_structure_get_fourcc (from_struct, "format", &from_space);

  to_space = GST_MAKE_FOURCC ('R','G','B',' ');
  gst_structure_get_fourcc (to_struct, "format", &to_space);

  GST_INFO ("set up converter for "  GST_FOURCC_FORMAT
	    " (%08x) to " GST_FOURCC_FORMAT " (%08x)",
	    GST_FOURCC_ARGS (from_space), from_space,
	    GST_FOURCC_ARGS (to_space), to_space);

  switch (from_space) {
    case GST_MAKE_FOURCC ('R','G','B',' '):
    {
      gint from_bpp;
      
      gst_structure_get_int (from_struct, "bpp", &from_bpp);

      switch (to_space) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
#ifdef HAVE_HERMES
        {
          gint to_bpp;
      
          gst_structure_get_int (to_struct, "bpp", &to_bpp);

	  gst_structure_get_int (from_struct, "red_mask",   &space->source.r);
	  gst_structure_get_int (from_struct, "green_mask", &space->source.g);
	  gst_structure_get_int (from_struct, "blue_mask",  &space->source.b);
	  space->source.a = 0;
	  space->srcbpp = space->source.bits = from_bpp;
	  space->source.indexed = 0;
	  space->source.has_colorkey = 0;

	  GST_INFO ( "source red mask   %08x", space->source.r);
	  GST_INFO ( "source green mask %08x", space->source.g);
	  GST_INFO ( "source blue mask  %08x", space->source.b);
	  GST_INFO ( "source bpp        %08x", space->srcbpp);

	  gst_structure_get_int (to_struct, "red_mask",   &space->dest.r);
	  gst_structure_get_int (to_struct, "green_mask", &space->dest.g);
	  gst_structure_get_int (to_struct, "blue_mask",  &space->dest.b);
	  space->dest.a = 0;
	  space->destbpp = space->dest.bits = to_bpp;
	  space->dest.indexed = 0;
	  space->dest.has_colorkey = 0;

	  GST_INFO ( "dest red mask   %08x", space->dest.r);
	  GST_INFO ( "dest green mask %08x", space->dest.g);
	  GST_INFO ( "dest blue mask  %08x", space->dest.b);
	  GST_INFO ( "dest bpp        %08x", space->destbpp);

	  if (!Hermes_ConverterRequest (space->h_handle, &space->source, &space->dest)) {
	    g_warning ("Hermes: could not get converter\n");
	    return FALSE;
	  }
	  GST_INFO ( "converter set up");
          space->type = GST_COLORSPACE_HERMES;
	  return TRUE;
	}
#else
	  g_warning ("colorspace: compiled without hermes!");
	  return FALSE;
#endif
        case GST_MAKE_FOURCC ('Y','V','1','2'):
	  if (from_bpp == 32) {
            space->type = GST_COLORSPACE_RGB32_YV12;
	    space->destbpp = 12;
	    return TRUE;
	  }
        case GST_MAKE_FOURCC ('I','4','2','0'):
	  if (from_bpp == 32) {
            space->type = GST_COLORSPACE_RGB32_I420;
	    space->destbpp = 12;
	    return TRUE;
	  }
        case GST_MAKE_FOURCC ('Y','U','Y','2'):
          GST_INFO ( "colorspace: RGB to YUV with bpp %d not implemented!!", from_bpp);
	  return FALSE;
      }
      break;
    }
    case GST_MAKE_FOURCC ('I','4','2','0'):
      switch (to_space) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
          GST_INFO ( "colorspace: YUV to RGB");

	  gst_structure_get_int (to_struct, "bpp", &space->destbpp);
	  space->converter = gst_colorspace_yuv2rgb_get_converter (from_caps, to_caps);
          space->type = GST_COLORSPACE_YUV_RGB;
	  return TRUE;
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_COLORSPACE_NONE;
	  space->destbpp = 12;
	  return TRUE;
        case GST_MAKE_FOURCC ('Y','V','1','2'):
          space->type = GST_COLORSPACE_420_SWAP;
	  space->destbpp = 12;
	  return TRUE;

      }
      break;
    case GST_MAKE_FOURCC ('Y','U','Y','2'):
      switch (to_space) {
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_COLORSPACE_YUY2_I420;
	  space->destbpp = 12;
	  return TRUE;
        case GST_MAKE_FOURCC ('Y','U','Y','2'):
          space->type = GST_COLORSPACE_NONE;
	  space->destbpp = 16;
	  return TRUE;
        case GST_MAKE_FOURCC ('R','G','B',' '):
          GST_INFO ( "colorspace: YUY2 to RGB not implemented!!");
	  return FALSE;
      }
      break;
    case GST_MAKE_FOURCC ('Y','V','1','2'):
      switch (to_space) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
          GST_INFO ( "colorspace: YV12 to RGB");

	  gst_structure_get_int (to_struct, "bpp", &space->destbpp);
	  space->converter = gst_colorspace_yuv2rgb_get_converter (from_caps, to_caps);
          space->type = GST_COLORSPACE_YUV_RGB;
	  return TRUE;
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_COLORSPACE_420_SWAP;
	  space->destbpp = 12;
	  return TRUE;
        case GST_MAKE_FOURCC ('Y','V','1','2'):
          space->type = GST_COLORSPACE_NONE;
	  space->destbpp = 12;
	  return TRUE;
      }
      break;
  }
  return FALSE;
}
#endif

static GstCaps *
gst_colorspace_caps_remove_format_info (GstCaps *caps, const char *media_type)
{
  int i;
  GstStructure *structure;
  GstCaps *rgbcaps;

  for (i=0; i<gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

    gst_structure_set_name (structure, media_type);
    gst_structure_remove_field (structure, "format");
    gst_structure_remove_field (structure, "endianness");
    gst_structure_remove_field (structure, "depth");
    gst_structure_remove_field (structure, "bpp");
    gst_structure_remove_field (structure, "red_mask");
    gst_structure_remove_field (structure, "green_mask");
    gst_structure_remove_field (structure, "blue_mask");
  }

  rgbcaps = gst_caps_simplify (caps);
  gst_caps_free (caps);

  return rgbcaps;
}

static GstCaps*
gst_colorspace_getcaps (GstPad *pad)
{
  GstColorspace *space;
  GstPad *otherpad;
  GstCaps *othercaps;
  GstCaps *caps;
  
  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  otherpad = (pad == space->srcpad) ? space->sinkpad : space->srcpad;

  othercaps = gst_pad_get_allowed_caps (otherpad);

  othercaps = gst_colorspace_caps_remove_format_info (othercaps,
      (pad == space->srcpad) ? "video/x-raw-rgb" : "video/x-raw-yuv");

  caps = gst_caps_intersect (othercaps, gst_pad_get_pad_template_caps (pad));
  gst_caps_free (othercaps);

  return caps;
}

static GstColorSpaceFormatType
gst_colorspace_get_format (const GstCaps *caps)
{
  int i;

  for(i=0; i<G_N_ELEMENTS (gst_colorspace_formats); i++) {
    GstCaps *icaps;
    GstCaps *fcaps;
    
    fcaps = gst_caps_copy (gst_static_caps_get (
          &gst_colorspace_formats[i].caps));

    icaps = gst_caps_intersect (caps, fcaps);
    if (!gst_caps_is_empty (icaps)) {
      gst_caps_free (icaps);
      return i;
    }
    gst_caps_free (icaps);
  }

  g_assert_not_reached();
  return -1;
}

#define ROUND_UP_2(x)  (((x)+1)&~1)
#define ROUND_UP_4(x)  (((x)+3)&~3)
#define ROUND_UP_8(x)  (((x)+7)&~7)

static int
gst_colorspace_format_get_size(GstColorSpaceFormatType index, int width,
    int height)
{
  int size;

  switch (index) {
    case GST_COLORSPACE_I420:
    case GST_COLORSPACE_YV12:
      size = ROUND_UP_4 (width) * ROUND_UP_2 (height);
      size += ROUND_UP_8 (width)/2 * ROUND_UP_2 (height)/2;
      size += ROUND_UP_8 (width)/2 * ROUND_UP_2 (height)/2;
      return size;
      break;
    case GST_COLORSPACE_RGB32:
      return width*height*4;
      break;
    case GST_COLORSPACE_RGB24:
      return ROUND_UP_4 (width*3) * height;
      break;
    case GST_COLORSPACE_RGB16:
      return ROUND_UP_4 (width*2) * height;
      break;
  }

  g_assert_not_reached();
  return 0;
}

static int
gst_colorspace_get_converter (GstColorSpaceFormatType from,
    GstColorSpaceFormatType to)
{
  int i;

  for (i=0; i<G_N_ELEMENTS (gst_colorspace_converters); i++) {
    GstColorspaceConverter *converter = gst_colorspace_converters + i;
    if (from == converter->from && to == converter->to) {
      return i;
    }
  }
  g_assert_not_reached();
  return -1;
}

static GstPadLinkReturn
gst_colorspace_link (GstPad *pad, const GstCaps *caps)
{
  GstColorspace *space;
  GstPad *otherpad;
  GstStructure *structure;
  GstPadLinkReturn link_ret;
  int width, height;
  double fps;
  int format_index;

  space = GST_COLORSPACE (gst_pad_get_parent (pad));
  otherpad = (pad == space->sinkpad) ? space->srcpad : space->sinkpad;

  link_ret = gst_pad_try_set_caps (otherpad, caps);
  if (link_ret == GST_PAD_LINK_OK) {
    return link_ret;
  }

  structure = gst_caps_get_structure (caps, 0);

  format_index = gst_colorspace_get_format (caps);
  g_print("format index is %d\n", format_index);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_double (structure, "framerate", &fps);

  GST_INFO ( "size: %dx%d", space->width, space->height);

  if (gst_pad_is_negotiated (otherpad)) {
    GstCaps *othercaps;
    
    othercaps = gst_caps_copy (gst_pad_get_negotiated_caps (otherpad));

    gst_caps_set_simple (othercaps,
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", G_TYPE_DOUBLE, fps, NULL);

    link_ret = gst_pad_try_set_caps (otherpad, othercaps);
    if (link_ret != GST_PAD_LINK_OK) {
      return link_ret;
    }
  }

  if (pad == space->srcpad) {
    space->src_format_index = format_index;
  } else {
    space->sink_format_index = format_index;
  }

  if (gst_pad_is_negotiated (otherpad)) {
    space->converter_index = gst_colorspace_get_converter (
        space->sink_format_index, space->src_format_index);

    g_print("using index %d\n", space->converter_index);

    space->sink_size = gst_colorspace_format_get_size(space->sink_format_index,
        width,height);
    space->src_size = gst_colorspace_format_get_size(space->src_format_index,
        width,height);
    space->width = width;
    space->height = height;
    space->fps = fps;
  }

#if 0
  if (gst_pad_is_negotiated (otherpad)) {
    g_warning ("could not get converter\n");
    return GST_PAD_LINK_REFUSED;
  }
#endif

  return GST_PAD_LINK_OK;
}

GType
gst_colorspace_get_type (void)
{
  static GType colorspace_type = 0;

  if (!colorspace_type) {
    static const GTypeInfo colorspace_info = {
      sizeof(GstColorspaceClass),      
      gst_colorspace_base_init,
      NULL,
      (GClassInitFunc)gst_colorspace_class_init,
      NULL,
      NULL,
      sizeof(GstColorspace),
      0,
      (GInstanceInitFunc)gst_colorspace_init,
    };
    colorspace_type = g_type_register_static(GST_TYPE_ELEMENT, "GstColorspace", &colorspace_info, 0);
  }
  return colorspace_type;
}

static void
gst_colorspace_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_colorspace_src_template));
  gst_element_class_add_pad_template (element_class,
     gst_static_pad_template_get (&gst_colorspace_sink_template));

  gst_element_class_set_details (element_class, &colorspace_details);
}
  
static void
gst_colorspace_class_init (GstColorspaceClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_colorspace_set_property;
  gobject_class->get_property = gst_colorspace_get_property;

  gstelement_class->change_state = gst_colorspace_change_state;

  gst_colorspace_table_init(NULL);
}

static void
gst_colorspace_init (GstColorspace *space)
{
  space->sinkpad = gst_pad_new_from_template (
     gst_static_pad_template_get (&gst_colorspace_sink_template),
     "sink");
  gst_pad_set_link_function (space->sinkpad, gst_colorspace_link);
  gst_pad_set_getcaps_function (space->sinkpad, gst_colorspace_getcaps);
  gst_pad_set_chain_function(space->sinkpad,gst_colorspace_chain);
  gst_element_add_pad(GST_ELEMENT(space),space->sinkpad);

  space->srcpad = gst_pad_new_from_template (
     gst_static_pad_template_get (&gst_colorspace_src_template),
     "src");
  gst_element_add_pad(GST_ELEMENT(space),space->srcpad);
  gst_pad_set_link_function (space->srcpad, gst_colorspace_link);
}

static void
gst_colorspace_chain (GstPad *pad,GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstColorspace *space;
  GstBuffer *outbuf = NULL;
  GstColorspaceConverter *converter;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  space = GST_COLORSPACE (gst_pad_get_parent (pad));
  
  g_return_if_fail (space != NULL);
  g_return_if_fail (GST_IS_COLORSPACE (space));

  if (GST_BUFFER_SIZE (buf) < space->sink_size) {
    g_critical ("input size is smaller than expected");
  }

  outbuf = gst_pad_alloc_buffer (space->srcpad, GST_BUFFER_OFFSET_NONE,
      space->src_size);
  
  converter = gst_colorspace_converters + space->converter_index;
  converter->convert (space, GST_BUFFER_DATA (outbuf),
      GST_BUFFER_DATA (buf));

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  gst_buffer_unref (buf);
  gst_pad_push (space->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_colorspace_change_state (GstElement *element)
{
  GstColorspace *space;

  space = GST_COLORSPACE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_PAUSED_TO_READY:
      break;
  }

  return parent_class->change_state (element);
}

static void
gst_colorspace_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstColorspace *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_COLORSPACE(object));
  space = GST_COLORSPACE(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_colorspace_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstColorspace *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_COLORSPACE(object));
  space = GST_COLORSPACE(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  if (!gst_element_register (plugin, "colorspace", GST_RANK_PRIMARY,
        GST_TYPE_COLORSPACE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "colorspace",
  "Hermes colorspace converter",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)
