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

typedef enum {
  GST_COLORSPACE_NONE,
  GST_COLORSPACE_LCS,
} GstColorSpaceConverterType;

struct _GstColorspace {
  GstElement 	 element;

  GstPad 	*sinkpad, *srcpad;

  LCSConverter 	*converter;
  
  GstColorSpaceConverterType type;
  gint 		width, height;
  gfloat	fps;
  gboolean 	disabled;

  GstCaps	*sinkcaps;
  
  GstBufferPool *pool;
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
			gst_colorspace_sinkconnect		(GstPad *pad, GstCaps *caps);
static GstPadLinkReturn
			gst_colorspace_srcconnect 		(GstPad *pad, GstCaps *caps);
static GstPadLinkReturn
			gst_colorspace_srcconnect_func 		(GstPad *pad, GstCaps *caps, gboolean newcaps);
static void		gst_colorspace_chain			(GstPad *pad, GstData *_data);
static GstElementStateReturn
			gst_colorspace_change_state 		(GstElement *element);

static GstPadTemplate *srctempl, *sinktempl;
static GstElementClass *parent_class = NULL;
/*static guint gst_colorspace_signals[LAST_SIGNAL] = { 0 }; */

static GstBufferPool* 
colorspace_get_bufferpool (GstPad *pad)
{
  GstColorspace *space;

  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  if (space->type == GST_COLORSPACE_NONE && !space->disabled)
    return gst_pad_get_bufferpool (space->srcpad);
  else
    return NULL;
}

static const LCSFormat*
colorspace_find_lcs_format (GstCaps *caps)
{
  const LCSFormat *format = NULL;
  guint32 space;

  gst_caps_get_fourcc_int (caps, "format", &space);

  if (space == GST_MAKE_FOURCC ('R','G','B',' ')) {
    gint mask, depth;

    gulong red_bits, red_shift;
    gulong green_bits, green_shift;
    gulong blue_bits, blue_shift;
     
    gst_caps_get_int (caps, "depth", &depth);
    gst_caps_get_int (caps, "red_mask", &mask);
    lcs_utils_mask_to_shift (mask, &red_bits, &red_shift);
    gst_caps_get_int (caps, "green_mask", &mask);
    lcs_utils_mask_to_shift (mask, &green_bits, &green_shift);
    gst_caps_get_int (caps, "blue_mask", &mask);
    lcs_utils_mask_to_shift (mask, &blue_bits, &blue_shift);

    format = lcs_format_new_simple_rgb_packed (red_bits, green_bits, blue_bits, 0,
		    red_shift, green_shift, blue_shift, 0, depth, G_BYTE_ORDER);

    GST_DEBUG ("%lu %lu %lu %lu %lu %lu %u %s\n", red_bits, green_bits, blue_bits, 
		    red_shift, green_shift, blue_shift, depth, lcs_format_get_layout (format));
  }
  else {
    gchar fourcc[5];

    lcs_utils_fourcc_to_string (space, fourcc);
    format = lcs_alias_find_format (fourcc);
  }

  return format;
}

static gboolean 
colorspace_setup_converter (GstColorspace *space, GstCaps *from_caps, GstCaps *to_caps)
{
  const LCSFormat *from_format = NULL;
  const LCSFormat *to_format = NULL;
  guint32 from_space, to_space;

  g_return_val_if_fail (to_caps != NULL, FALSE);
  g_return_val_if_fail (from_caps != NULL, FALSE);

  if (gst_caps_has_property (from_caps, "format"))
    gst_caps_get_fourcc_int (from_caps, "format", &from_space);
  else
    from_space = GST_MAKE_FOURCC ('R','G','B',' ');

  if (gst_caps_has_property (to_caps, "format"))
    gst_caps_get_fourcc_int (to_caps, "format", &to_space);
  else
    to_space = GST_MAKE_FOURCC ('R','G','B',' ');

  from_format 	= colorspace_find_lcs_format (from_caps);
  to_format 	= colorspace_find_lcs_format (to_caps);
	  
  GST_DEBUG ("trying from %4.4s to %4.4s\n", (gchar*)&from_space, (gchar*)&to_space);
  space->converter = lcs_get_converter (from_format, to_format, LCS_FLAG_FAST);

  if (space->converter) {
    GST_DEBUG ("converting from %4.4s to %4.4s\n", (gchar*)&from_space, (gchar*)&to_space);
    space->type = GST_COLORSPACE_LCS;
    return TRUE;
  }

  return FALSE;
}

static GstCaps*
gst_colorspace_getcaps (GstPad *pad, GstCaps *caps)
{
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
}

static GstPadLinkReturn
gst_colorspace_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstColorspace *space;
  GstPad *peer;

  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_LINK_DELAYED;
  }

  gst_caps_get_int (caps, "width", &space->width);
  gst_caps_get_int (caps, "height", &space->height);
  gst_caps_get_float (caps, "framerate", &space->fps);

  GST_INFO ( "size: %dx%d", space->width, space->height);

  space->sinkcaps = caps;

  peer = gst_pad_get_peer (pad);
  if (peer) {
    if (gst_colorspace_srcconnect_func (pad, gst_pad_get_allowed_caps (space->srcpad), FALSE) < 1) {
      space->sinkcaps = NULL;
      return GST_PAD_LINK_REFUSED;
    }
  }

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_colorspace_srcconnect (GstPad *pad, GstCaps *caps)
{
  return gst_colorspace_srcconnect_func (pad, caps, TRUE);
}

static GstPadLinkReturn
gst_colorspace_srcconnect_func (GstPad *pad, GstCaps *caps, gboolean newcaps)
{
  GstColorspace *space;
  GstCaps *peercaps;
  GstCaps *ourcaps;
  
  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  /* we cannot operate if we didn't get src caps */
  ourcaps = space->sinkcaps;
  if (!ourcaps) {
    if (newcaps)
      gst_pad_recalc_allowed_caps (space->sinkpad);

    return GST_PAD_LINK_DELAYED;
  }

  /* first see if we can do the format natively by filtering the peer caps 
   * with our incomming caps */
  peercaps = gst_caps_intersect (caps, ourcaps); 
  if (peercaps) {
    /* see if the peer likes it too, it should as the caps say so.. */
    if (gst_pad_try_set_caps (space->srcpad, peercaps) > 0) {
      space->type = GST_COLORSPACE_NONE;
      space->disabled = FALSE;
      return GST_PAD_LINK_DONE;
    }
  }
  /* then see what the peer has that matches the size */
  peercaps = gst_caps_intersect (caps,
		  gst_caps_append (
		  GST_CAPS_NEW (
		   "colorspace_filter",
		   "video/x-raw-yuv",
		     "width",     GST_PROPS_INT (space->width),
		     "height",    GST_PROPS_INT (space->height),
		     "framerate", GST_PROPS_FLOAT (space->fps)
		  ), GST_CAPS_NEW (
		   "colorspace_filter",
		   "video/x-raw-rgb",
		     "width",     GST_PROPS_INT (space->width),
		     "height",    GST_PROPS_INT (space->height),
		     "framerate", GST_PROPS_FLOAT (space->fps)
		  )));

  /* we are looping over the caps, so we have to get rid of the lists */
  peercaps = gst_caps_normalize (peercaps);

  /* loop over all possibilities and select the first one we can convert and
   * is accepted by the peer */
  while (peercaps) {
    if (colorspace_setup_converter (space, ourcaps, peercaps)) {
      if (gst_pad_try_set_caps (space->srcpad, peercaps) > 0) {
        space->disabled = FALSE;
        return GST_PAD_LINK_DONE;
      }
    }
    peercaps = peercaps->next;
  }
  
  GST_ELEMENT_ERROR (space, CORE, NEGOTIATION, (NULL), (NULL));
  /* we disable ourself here */
  space->disabled = TRUE;

  return GST_PAD_LINK_REFUSED;
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

  gst_element_class_add_pad_template (element_class, srctempl);
  gst_element_class_add_pad_template (element_class, sinktempl);
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
  space->sinkpad = gst_pad_new_from_template (sinktempl, "sink");
  gst_pad_set_link_function (space->sinkpad, gst_colorspace_sinkconnect);
  gst_pad_set_getcaps_function (space->sinkpad, gst_colorspace_getcaps);
  gst_pad_set_bufferpool_function (space->sinkpad, colorspace_get_bufferpool);
  gst_pad_set_chain_function(space->sinkpad,gst_colorspace_chain);
  gst_element_add_pad(GST_ELEMENT(space),space->sinkpad);

  space->srcpad = gst_pad_new_from_template (srctempl, "src");
  gst_element_add_pad(GST_ELEMENT(space),space->srcpad);
  gst_pad_set_link_function (space->srcpad, gst_colorspace_srcconnect);

  space->pool = NULL;
  space->disabled = TRUE;
}

static void
gst_colorspace_chain (GstPad *pad,GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstColorspace *space;
  GstBuffer *outbuf = NULL;

  g_return_if_fail (pad != NULL);
  g_return_if_fail (GST_IS_PAD (pad));
  g_return_if_fail (buf != NULL);

  space = GST_COLORSPACE (gst_pad_get_parent (pad));
  
  g_return_if_fail (space != NULL);
  g_return_if_fail (GST_IS_COLORSPACE (space));

  if (space->disabled) {
    gst_buffer_unref (buf);
    return;
  }

  if (space->type == GST_COLORSPACE_NONE) {
    outbuf = buf;
  }
  else {
    if (space->pool) {
      outbuf = gst_buffer_new_from_pool (space->pool, 0, 0);
    }

    if (!outbuf) {
      guint size;
      outbuf = gst_buffer_new ();

      lcs_format_buffer_size (
		      lcs_convert_get_dest_format (space->converter),
		      space->width, space->height, &size);

      GST_BUFFER_SIZE (outbuf) = size;
      GST_BUFFER_DATA (outbuf) = g_malloc (size);
    }

    if (space->type == GST_COLORSPACE_LCS) {
       lcs_convert_auto (space->converter, 
		       	 GST_BUFFER_DATA (buf), 
		       	 GST_BUFFER_DATA (outbuf), 
			 space->width, space->height);
    }

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

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
    case GST_STATE_PAUSED_TO_PLAYING:
      space->pool = gst_pad_get_bufferpool (space->srcpad);
      break;
    case GST_STATE_PLAYING_TO_PAUSED:
      space->pool = NULL;
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
  GstCaps *caps;

  lcs_init (NULL, NULL);

  if (!gst_element_register (plugin, "colorspace", GST_RANK_NONE,
			     GST_TYPE_COLORSPACE))
    return FALSE;

  /* create caps for templates */
  caps = gst_caps_new ("csp_templ_yuv",
                       "video/x-raw-yuv",
	               GST_VIDEO_YUV_PAD_TEMPLATE_PROPS (
                         GST_PROPS_LIST (
                           GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
	                   GST_PROPS_FOURCC (GST_STR_FOURCC ("YV12")),
	                   GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2")))));
  caps = gst_caps_append (caps,
         gst_caps_new ("csp_templ_rgb24_32",
                       "video/x-raw-rgb",
                       GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24_32));
  caps = gst_caps_append (caps,
         gst_caps_new ("csp_templ_rgb15_16",
                       "video/x-raw-rgb",
                       GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_15_16));

  /* build templates */
  srctempl  = gst_pad_template_new ("src",
				    GST_PAD_SRC,
				    GST_PAD_ALWAYS,
				    caps, NULL);
  gst_caps_ref (caps);
  sinktempl = gst_pad_template_new ("sink",
				    GST_PAD_SINK,
				    GST_PAD_ALWAYS,
				    caps, NULL);

  return TRUE;
}

GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "colorspacelcs",
  "LCS colorspace convertor",
  plugin_init,
  VERSION,
  "LGPL",
  "(c) 2003 The LCS team",
  "LCS",
  "http://www.codecs.org/"
)
