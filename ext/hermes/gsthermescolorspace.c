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
#include <gst/gst.h>
#include <gst/video/video.h>
#include <Hermes/Hermes.h>


#define GST_TYPE_COLORSPACE \
  (gst_hermes_colorspace_get_type())
#define GST_HERMES_COLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COLORSPACE,GstHermesColorspace))
#define GST_HERMES_COLORSPACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstHermesColorspace))
#define GST_IS_COLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COLORSPACE))
#define GST_IS_COLORSPACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COLORSPACE))

typedef struct _GstHermesColorspace GstHermesColorspace;
typedef struct _GstHermesColorspaceClass GstHermesColorspaceClass;

typedef enum {
  GST_HERMES_COLORSPACE_NONE,
  GST_HERMES_COLORSPACE_HERMES,
  GST_HERMES_COLORSPACE_YUV_RGB,
  GST_HERMES_COLORSPACE_YUY2_I420,
  GST_HERMES_COLORSPACE_RGB32_I420,
  GST_HERMES_COLORSPACE_RGB32_YV12,
  GST_HERMES_COLORSPACE_420_SWAP,
} GstColorSpaceConverterType;

struct _GstHermesColorspace {
  GstElement element;

  GstPad *sinkpad,*srcpad;

  HermesHandle h_handle;
  HermesFormat sink_format;
  HermesFormat src_format;

  int src_format_index;
  int sink_format_index;

  int src_size;
  int sink_size;
  
  int src_stride;
  int sink_stride;

  gint width, height;
  gdouble fps;
  gboolean passthru;
};

struct _GstHermesColorspaceClass {
  GstElementClass parent_class;
};

GType gst_hermes_colorspace_get_type(void);

typedef struct _GstHermesColorspaceFormat {
  GstStaticCaps caps;

} GstHermesColorspaceFormat;

static GstHermesColorspaceFormat gst_hermes_colorspace_formats[] = {
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_xRGB) },
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_RGBx) },
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_BGRx) },
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_xBGR) },
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_BGR) },
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB) },
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_15) },
  { GST_STATIC_CAPS (GST_VIDEO_CAPS_RGB_16) },
};

static GstElementDetails colorspace_details = {
  "Colorspace converter",
  "Filter/Converter/Video",
  "Converts video from one colorspace to another using libhermes",
  "Wim Taymans <wim.taymans@chello.be>",
};


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

static void             gst_hermes_colorspace_base_init                (gpointer g_class);
static void		gst_hermes_colorspace_class_init		(GstHermesColorspaceClass *klass);
static void		gst_hermes_colorspace_init			(GstHermesColorspace *space);

static void		gst_hermes_colorspace_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void		gst_hermes_colorspace_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstPadLinkReturn
			gst_hermes_colorspace_link     		(GstPad *pad, const GstCaps *caps);
static void		gst_hermes_colorspace_chain			(GstPad *pad, GstData *_data);
static GstElementStateReturn
			gst_hermes_colorspace_change_state 		(GstElement *element);


static GstElementClass *parent_class = NULL;
/*static guint gst_hermes_colorspace_signals[LAST_SIGNAL] = { 0 }; */

#if 0
static gboolean 
colorspace_setup_converter (GstHermesColorspace *space, GstCaps *from_caps, GstCaps *to_caps)
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
          space->type = GST_HERMES_COLORSPACE_HERMES;
	  return TRUE;
	}
#else
	  g_warning ("colorspace: compiled without hermes!");
	  return FALSE;
#endif
        case GST_MAKE_FOURCC ('Y','V','1','2'):
	  if (from_bpp == 32) {
            space->type = GST_HERMES_COLORSPACE_RGB32_YV12;
	    space->destbpp = 12;
	    return TRUE;
	  }
        case GST_MAKE_FOURCC ('I','4','2','0'):
	  if (from_bpp == 32) {
            space->type = GST_HERMES_COLORSPACE_RGB32_I420;
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
	  space->converter = gst_hermes_colorspace_yuv2rgb_get_converter (from_caps, to_caps);
          space->type = GST_HERMES_COLORSPACE_YUV_RGB;
	  return TRUE;
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_HERMES_COLORSPACE_NONE;
	  space->destbpp = 12;
	  return TRUE;
        case GST_MAKE_FOURCC ('Y','V','1','2'):
          space->type = GST_HERMES_COLORSPACE_420_SWAP;
	  space->destbpp = 12;
	  return TRUE;

      }
      break;
    case GST_MAKE_FOURCC ('Y','U','Y','2'):
      switch (to_space) {
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_HERMES_COLORSPACE_YUY2_I420;
	  space->destbpp = 12;
	  return TRUE;
        case GST_MAKE_FOURCC ('Y','U','Y','2'):
          space->type = GST_HERMES_COLORSPACE_NONE;
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
	  space->converter = gst_hermes_colorspace_yuv2rgb_get_converter (from_caps, to_caps);
          space->type = GST_HERMES_COLORSPACE_YUV_RGB;
	  return TRUE;
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_HERMES_COLORSPACE_420_SWAP;
	  space->destbpp = 12;
	  return TRUE;
        case GST_MAKE_FOURCC ('Y','V','1','2'):
          space->type = GST_HERMES_COLORSPACE_NONE;
	  space->destbpp = 12;
	  return TRUE;
      }
      break;
  }
  return FALSE;
}
#endif

static GstCaps *
gst_hermes_colorspace_caps_remove_format_info (GstCaps *caps)
{
  int i;
  GstStructure *structure;
  GstCaps *rgbcaps;

  for (i=0; i<gst_caps_get_size (caps); i++) {
    structure = gst_caps_get_structure (caps, i);

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

static void
gst_hermes_colorspace_structure_to_hermes_format (HermesFormat *format,
    GstStructure *structure)
{
  gst_structure_get_int (structure, "red_mask",   &format->r);
  gst_structure_get_int (structure, "green_mask", &format->g);
  gst_structure_get_int (structure, "blue_mask",  &format->b);
  format->a = 0;
  gst_structure_get_int (structure, "bpp",  &format->bits);
  format->indexed = 0;
  format->has_colorkey = 0;
}

static GstCaps*
gst_hermes_colorspace_getcaps (GstPad *pad)
{
  GstHermesColorspace *space;
  GstPad *otherpad;
  GstCaps *othercaps;
  GstCaps *caps;
  
  space = GST_HERMES_COLORSPACE (gst_pad_get_parent (pad));

  otherpad = (pad == space->srcpad) ? space->sinkpad : space->srcpad;

  othercaps = gst_pad_get_allowed_caps (otherpad);

  othercaps = gst_hermes_colorspace_caps_remove_format_info (othercaps);

  caps = gst_caps_intersect (othercaps, gst_pad_get_pad_template_caps (pad));
  gst_caps_free (othercaps);

  return caps;
}

static GstPadLinkReturn
gst_hermes_colorspace_link (GstPad *pad, const GstCaps *caps)
{
  GstHermesColorspace *space;
  GstPad *otherpad;
  GstStructure *structure;
  GstPadLinkReturn link_ret;
  int width, height;
  double fps;
  int i;

  space = GST_HERMES_COLORSPACE (gst_pad_get_parent (pad));
  otherpad = (pad == space->sinkpad) ? space->srcpad : space->sinkpad;

  link_ret = gst_pad_try_set_caps (otherpad, caps);
  if (link_ret == GST_PAD_LINK_OK) {
    space->passthru = TRUE;
    return link_ret;
  }

  structure = gst_caps_get_structure (caps, 0);

  for(i=0; i<G_N_ELEMENTS (gst_hermes_colorspace_formats); i++) {
    GstCaps *icaps;
    GstCaps *fcaps;
    
    fcaps = gst_caps_copy (gst_static_caps_get (
          &gst_hermes_colorspace_formats[i].caps));

    icaps = gst_caps_intersect (caps, fcaps);
    if (!gst_caps_is_empty (icaps)) {
      break;
    }
    gst_caps_free (icaps);
  }
  if (i==G_N_ELEMENTS (gst_hermes_colorspace_formats)) {
    g_assert_not_reached ();
    return GST_PAD_LINK_REFUSED;
  }

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
    space->src_format_index = i;
    gst_hermes_colorspace_structure_to_hermes_format (&space->src_format, structure);
  } else {
    space->sink_format_index = i;
    gst_hermes_colorspace_structure_to_hermes_format (&space->sink_format, structure);
  }

  space->sink_stride = width*(space->sink_format.bits/8);
  space->src_stride = width*(space->src_format.bits/8);
  space->sink_size = space->sink_stride * height;
  space->src_size = space->src_stride * height;
  space->width = width;
  space->height = height;
  space->fps = fps;

  if (gst_pad_is_negotiated (otherpad)) {
    if (!Hermes_ConverterRequest (space->h_handle, &space->sink_format,
          &space->src_format)) {
      g_warning ("Hermes: could not get converter\n");
      return GST_PAD_LINK_REFUSED;
    }
g_print("inited\n");
  }

  return GST_PAD_LINK_OK;
}

GType
gst_hermes_colorspace_get_type (void)
{
  static GType colorspace_type = 0;

  if (!colorspace_type) {
    static const GTypeInfo colorspace_info = {
      sizeof(GstHermesColorspaceClass),      
      gst_hermes_colorspace_base_init,
      NULL,
      (GClassInitFunc)gst_hermes_colorspace_class_init,
      NULL,
      NULL,
      sizeof(GstHermesColorspace),
      0,
      (GInstanceInitFunc)gst_hermes_colorspace_init,
    };
    colorspace_type = g_type_register_static(GST_TYPE_ELEMENT, "GstHermesColorspace", &colorspace_info, 0);
  }
  return colorspace_type;
}

static GstStaticPadTemplate gst_hermes_colorspace_src_pad_template = 
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_xRGB "; "
      GST_VIDEO_CAPS_RGBx "; "
      GST_VIDEO_CAPS_BGRx "; "
      GST_VIDEO_CAPS_xBGR "; "
      GST_VIDEO_CAPS_BGR "; "
      GST_VIDEO_CAPS_RGB "; "
      GST_VIDEO_CAPS_RGB_16 "; "
      GST_VIDEO_CAPS_RGB_15)
);

static GstStaticPadTemplate gst_hermes_colorspace_sink_pad_template = 
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_xRGB "; "
      GST_VIDEO_CAPS_RGBx "; "
      GST_VIDEO_CAPS_BGRx "; "
      GST_VIDEO_CAPS_xBGR "; "
      GST_VIDEO_CAPS_BGR "; "
      GST_VIDEO_CAPS_RGB "; "
      GST_VIDEO_CAPS_RGB_16 "; "
      GST_VIDEO_CAPS_RGB_15)
);

static void
gst_hermes_colorspace_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_hermes_colorspace_src_pad_template));
  gst_element_class_add_pad_template (element_class,
     gst_static_pad_template_get (&gst_hermes_colorspace_sink_pad_template));

  gst_element_class_set_details (element_class, &colorspace_details);
}
  
static void
gst_hermes_colorspace_class_init (GstHermesColorspaceClass *klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass*)klass;
  gstelement_class = (GstElementClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->set_property = gst_hermes_colorspace_set_property;
  gobject_class->get_property = gst_hermes_colorspace_get_property;

  gstelement_class->change_state = gst_hermes_colorspace_change_state;
}

static void
gst_hermes_colorspace_init (GstHermesColorspace *space)
{
  space->sinkpad = gst_pad_new_from_template (
     gst_static_pad_template_get (&gst_hermes_colorspace_sink_pad_template),
     "sink");
  gst_pad_set_link_function (space->sinkpad, gst_hermes_colorspace_link);
  gst_pad_set_getcaps_function (space->sinkpad, gst_hermes_colorspace_getcaps);
  gst_pad_set_chain_function(space->sinkpad,gst_hermes_colorspace_chain);
  gst_element_add_pad(GST_ELEMENT(space),space->sinkpad);

  space->srcpad = gst_pad_new_from_template (
     gst_static_pad_template_get (&gst_hermes_colorspace_src_pad_template),
     "src");
  gst_element_add_pad(GST_ELEMENT(space),space->srcpad);
  gst_pad_set_link_function (space->srcpad, gst_hermes_colorspace_link);

  space->h_handle = Hermes_ConverterInstance (0);
  space->passthru = FALSE;
}

static void
gst_hermes_colorspace_chain (GstPad *pad,GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstHermesColorspace *space;
  GstBuffer *outbuf = NULL;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  space = GST_HERMES_COLORSPACE (gst_pad_get_parent (pad));
  
  g_return_if_fail (space != NULL);
  g_return_if_fail (GST_IS_COLORSPACE (space));

  if (space->passthru) {
    gst_pad_push (space->srcpad, _data);
    return;
  }

  if (GST_BUFFER_SIZE (buf) < space->sink_size) {
    g_critical ("input size is smaller than expected");
  }

  outbuf = gst_pad_alloc_buffer (space->srcpad, GST_BUFFER_OFFSET_NONE,
      space->src_size);
  
  Hermes_ConverterCopy (space->h_handle, 
      GST_BUFFER_DATA (buf), 0, 0, space->width, space->height,
      space->sink_stride, GST_BUFFER_DATA (outbuf), 0, 0,
      space->width, space->height, space->src_stride);

  GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
  GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

  gst_buffer_unref (buf);
  gst_pad_push (space->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_hermes_colorspace_change_state (GstElement *element)
{
  GstHermesColorspace *space;

  space = GST_HERMES_COLORSPACE (element);

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
gst_hermes_colorspace_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
  GstHermesColorspace *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_COLORSPACE(object));
  space = GST_HERMES_COLORSPACE(object);

  switch (prop_id) {
    default:
      break;
  }
}

static void
gst_hermes_colorspace_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
  GstHermesColorspace *space;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail(GST_IS_COLORSPACE(object));
  space = GST_HERMES_COLORSPACE(object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
plugin_init (GstPlugin *plugin)
{
  gint hermes_res;

  hermes_res = Hermes_Init();
  g_return_val_if_fail (hermes_res != 0, FALSE);

  if (!gst_element_register (plugin, "hermescolorspace", GST_RANK_PRIMARY,
        GST_TYPE_COLORSPACE))
    return FALSE;

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "hermescolorspace",
  "Hermes colorspace converter",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN)
