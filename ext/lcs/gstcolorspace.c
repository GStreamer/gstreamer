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
#include <lcs/lcs.h>
#include <string.h>

#define GST_TYPE_COLORSPACE \
  (gst_colorspace_get_type())
#define GST_COLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_COLORSPACE,GstColorspace))
#define GST_COLORSPACE_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_ULAW,GstColorspace))
#define GST_IS_COLORSPACE(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_COLORSPACE))
#define GST_IS_COLORSPACE_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_COLORSPACE))

typedef struct _GstColorspace GstColorspace;
typedef struct _GstColorspaceClass GstColorspaceClass;

struct _GstColorspace {
  GstElement 	 element;

  GstPad 	*sinkpad, *srcpad;

  LCSConverter 	*converter;
  
  gboolean      passthrough;
  gint 		width, height;
  double        framerate;
  gboolean 	disabled;
};

struct _GstColorspaceClass {
  GstElementClass parent_class;
};

/* elementfactory information */
static GstElementDetails colorspace_details = {
  "Colorspace converter",
  "Filter/Effect",
  "Converts video from one colorspace to another",
  "Wim Taymans <wim.taymans@chello.be>"
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

static GType 		gst_colorspace_get_type 		(void);

static void		gst_colorspace_class_init		(GstColorspaceClass *klass);
static void		gst_colorspace_base_init		(GstColorspaceClass *klass);
static void		gst_colorspace_init			(GstColorspace *space);

static void		gst_colorspace_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void		gst_colorspace_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstPadLinkReturn
			gst_colorspace_link		        (GstPad *pad, const GstCaps *caps);
static void		gst_colorspace_chain			(GstPad *pad, GstData *_data);
static GstElementStateReturn
			gst_colorspace_change_state 		(GstElement *element);

static GstElementClass *parent_class = NULL;
/*static guint gst_colorspace_signals[LAST_SIGNAL] = { 0 }; */

static GstStaticPadTemplate gst_colorspace_src_template =
GST_STATIC_PAD_TEMPLATE (
    "src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_RGB "; "
      GST_VIDEO_CAPS_BGR "; "
      GST_VIDEO_CAPS_RGBx "; "
      GST_VIDEO_CAPS_xRGB "; "
      GST_VIDEO_CAPS_BGRx "; "
      GST_VIDEO_CAPS_xBGR "; "
      GST_VIDEO_CAPS_RGB_16 "; "
      GST_VIDEO_CAPS_RGB_15 "; "
      GST_VIDEO_CAPS_YUV("{ I420, YV12, YUY2, YVYU, UYVY, YUV9, YVU9, "
        "Y800, Y41P, Y41B, Y42B, IUY2 }")
    )
);

static GstStaticPadTemplate gst_colorspace_sink_template =
GST_STATIC_PAD_TEMPLATE (
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
      GST_VIDEO_CAPS_RGB "; "
      GST_VIDEO_CAPS_BGR "; "
      GST_VIDEO_CAPS_RGBx "; "
      GST_VIDEO_CAPS_xRGB "; "
      GST_VIDEO_CAPS_BGRx "; "
      GST_VIDEO_CAPS_xBGR "; "
      GST_VIDEO_CAPS_RGB_16 "; "
      GST_VIDEO_CAPS_RGB_15 "; "
      GST_VIDEO_CAPS_YUV("{ I420, YV12, YUY2, YVYU, UYVY, YUV9, YVU9, "
        "Y800, Y41P, Y41B, Y42B, IUY2 }")
    )
);

static const LCSFormat*
colorspace_find_lcs_format (const GstCaps *caps)
{
  const LCSFormat *format = NULL;
  const char *name;
  const GstStructure *structure = gst_caps_get_structure (caps, 0);

  name = gst_structure_get_name (structure);

  if (strcmp (name, "video/x-raw-rgb") == 0) {
    LCSRGBPackedFormat *rgb_fmt = g_new (LCSRGBPackedFormat, 1);
    gint mask;
    gchar *format_name;
    int endianness;

    format = (const LCSFormat *) rgb_fmt;

    ((LCSFormat *) rgb_fmt)->type = LCS_FORMAT_RGB_PACKED;
    gst_structure_get_int (structure, "bpp",
        &((LCSFormat *) rgb_fmt)->bits_per_pixel);
    gst_structure_get_int (structure, "red_mask", &mask);
    lcs_utils_mask_to_shift (mask, &rgb_fmt->bits_per_component[LCS_R],
			     &rgb_fmt->component_bit_offset[LCS_R]);
    gst_structure_get_int (structure, "green_mask", &mask);
    lcs_utils_mask_to_shift (mask, &rgb_fmt->bits_per_component[LCS_G],
			     &rgb_fmt->component_bit_offset[LCS_G]);
    gst_structure_get_int (structure, "blue_mask", &mask);
    lcs_utils_mask_to_shift (mask, &rgb_fmt->bits_per_component[LCS_B],
			     &rgb_fmt->component_bit_offset[LCS_B]);
    rgb_fmt->bits_per_component[LCS_A] = 0;
    rgb_fmt->component_bit_offset[LCS_A] = 0;
    gst_structure_get_int (structure, "endianness", &endianness);
    rgb_fmt->endianness = endianness;

    format_name = g_strdup_printf ("GST_RGB_%d", format->bits_per_pixel);
    lcs_register_format (format_name, (LCSFormat *) rgb_fmt, 1);
  }
  else if (strcmp (name, "video/x-raw-yuv") == 0) {
    guint32 space;
    gchar fourcc[5];

    gst_structure_get_fourcc (structure, "format", &space);
    lcs_utils_fourcc_to_string (space, fourcc);
    fourcc[4] = '\0';
    format = lcs_find_format (fourcc);
  } else {
    g_assert_not_reached();
  }

  return format;
}

static guint32
gst_colorspace_caps_get_fourcc (const GstCaps *caps)
{
  guint32 format;
  GstStructure *structure;

  structure = gst_caps_get_structure (caps, 0);
  if (strcmp (gst_structure_get_name (structure), "video/x-raw-rgb") == 0) {
    format = GST_MAKE_FOURCC ('R','G','B',' ');
  } else {
    gst_structure_get_fourcc (structure, "format", &format);
  }
  return format;
}

static gboolean 
colorspace_setup_converter (GstColorspace *space, const GstCaps *from_caps,
    const GstCaps *to_caps)
{
  const LCSFormat *from_format = NULL;
  const LCSFormat *to_format = NULL;
  guint32 from_space, to_space;

  g_return_val_if_fail (to_caps != NULL, FALSE);
  g_return_val_if_fail (from_caps != NULL, FALSE);

  from_space = gst_colorspace_caps_get_fourcc (from_caps);
  to_space = gst_colorspace_caps_get_fourcc (to_caps);

  from_format 	= colorspace_find_lcs_format (from_caps);
  to_format 	= colorspace_find_lcs_format (to_caps);
	  
  GST_DEBUG ("trying from " GST_FOURCC_FORMAT " to " GST_FOURCC_FORMAT,
	     GST_FOURCC_ARGS (from_space),
	     GST_FOURCC_ARGS (to_space));
  space->converter = lcs_find_converter (from_format, to_format, LCS_FLAG_FAST);

  if (space->converter) {
    GST_DEBUG ("trying from " GST_FOURCC_FORMAT " to " GST_FOURCC_FORMAT,
	       GST_FOURCC_ARGS (from_space),
	       GST_FOURCC_ARGS (to_space));
    space->disabled = FALSE;
    return TRUE;
  }

  return FALSE;
}

static GstCaps*
gst_colorspace_getcaps (GstPad *pad)
{
#if unused
  GstColorspace *space;
  GstCaps *result;
  GstCaps *peercaps;
  GstCaps *ourcaps;
  
  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  /* we can do everything our peer can... */
  peercaps = gst_caps_copy (gst_pad_get_allowed_caps (space->srcpad));
  /* and our own template of course */
  ourcaps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));

  /* merge them together, we prefer the peercaps first */
  result = gst_caps_prepend (ourcaps, peercaps);

  return result;
#endif
  return gst_caps_copy (gst_pad_get_pad_template_caps (pad));
}

static GstPadLinkReturn
gst_colorspace_link (GstPad *pad, const GstCaps *caps)
{
  GstStructure *structure;
  GstPad *otherpad;
  GstCaps *othercaps;
  GstColorspace *space;
  int width;
  int height;
  double framerate;
  GstPadLinkReturn ret;

  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  otherpad = (pad == space->srcpad) ? space->sinkpad : space->srcpad;

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_get_int (structure, "width", &width);
  gst_structure_get_int (structure, "height", &height);
  gst_structure_get_double (structure, "framerate", &framerate);

  ret = gst_pad_try_set_caps (otherpad, caps);
  if (GST_PAD_LINK_SUCCESSFUL (ret)) {
    /* passthrough */
    space->passthrough = TRUE;
    return GST_PAD_LINK_OK;
  }
  space->passthrough = FALSE;

  if (gst_pad_is_negotiated (otherpad)) {
    othercaps = gst_caps_copy (gst_pad_get_negotiated_caps (otherpad));

    gst_caps_set_simple (othercaps,
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        "framerate", G_TYPE_DOUBLE, framerate,
        NULL);

    ret = gst_pad_try_set_caps (otherpad, othercaps);
    if (!GST_PAD_LINK_SUCCESSFUL (ret)) {
      return ret;
    }

    if (colorspace_setup_converter (space, caps, othercaps)) {

      space->width = width;
      space->height = height;
      space->framerate = framerate;
    } else {
      return GST_PAD_LINK_REFUSED;
    }
  }
  
  return GST_PAD_LINK_OK;
}

static GType
gst_colorspace_get_type (void)
{
  static GType colorspace_type = 0;

  if (!colorspace_type) {
    static const GTypeInfo colorspace_info = {
      sizeof(GstColorspaceClass),
      (GBaseInitFunc)gst_colorspace_base_init,
      NULL,
      (GClassInitFunc)gst_colorspace_class_init,
      NULL,
      NULL,
      sizeof(GstColorspace),
      0,
      (GInstanceInitFunc)gst_colorspace_init,
    };
    colorspace_type = g_type_register_static(GST_TYPE_ELEMENT, "GstColorspaceLCS", &colorspace_info, 0);
  }
  return colorspace_type;
}

static void
gst_colorspace_base_init (GstColorspaceClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

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
}

static void
gst_colorspace_init (GstColorspace *space)
{
  space->sinkpad = gst_pad_new_from_template (
      gst_static_pad_template_get(&gst_colorspace_sink_template), "sink");
  gst_pad_set_link_function (space->sinkpad, gst_colorspace_link);
  gst_pad_set_getcaps_function (space->sinkpad, gst_colorspace_getcaps);
  gst_pad_set_chain_function(space->sinkpad,gst_colorspace_chain);
  gst_element_add_pad(GST_ELEMENT(space),space->sinkpad);

  space->srcpad = gst_pad_new_from_template (
      gst_static_pad_template_get(&gst_colorspace_src_template), "src");
  gst_element_add_pad(GST_ELEMENT(space),space->srcpad);
  gst_pad_set_link_function (space->srcpad, gst_colorspace_link);

  space->disabled = TRUE;
}

static void
gst_colorspace_chain (GstPad *pad,GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstColorspace *space;
  GstBuffer *outbuf;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  space = GST_COLORSPACE (gst_pad_get_parent (pad));
  
  g_return_if_fail (space != NULL);
  g_return_if_fail (GST_IS_COLORSPACE (space));

  if (space->passthrough) {
    outbuf = buf;
  }
  else {
    unsigned long size;

    lcs_format_buffer_size (
                    lcs_converter_get_dest_format (space->converter),
                    space->width, space->height, &size);

    outbuf = gst_pad_alloc_buffer (space->srcpad, GST_BUFFER_OFFSET_NONE, size);

    lcs_convert_auto (space->converter, 
        GST_BUFFER_DATA (buf), 
        GST_BUFFER_DATA (outbuf), 
        space->width, space->height);

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);
    GST_BUFFER_DURATION (outbuf) = GST_BUFFER_DURATION (buf);

    gst_buffer_unref (buf);
  }
  gst_pad_push (space->srcpad, GST_DATA (outbuf));
}

static GstElementStateReturn
gst_colorspace_change_state (GstElement *element)
{
  GstColorspace *space;

  space = GST_COLORSPACE (element);

  switch (GST_STATE_TRANSITION (element)) {
    case GST_STATE_PAUSED_TO_READY:
      break;
  }

  parent_class->change_state (element);

  return GST_STATE_SUCCESS;
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
  lcs_init (NULL, NULL);

  return gst_element_register (plugin, "lcscolorspace", GST_RANK_NONE,
      GST_TYPE_COLORSPACE);
}


GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "colorspacelcs",
  "LCS colorspace convertor",
  plugin_init,
  VERSION,
  "LGPL",
  GST_PACKAGE,
  GST_ORIGIN
)
