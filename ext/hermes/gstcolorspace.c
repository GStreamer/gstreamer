/* Gnome-Streamer
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

#include "gstcolorspace.h"
#include "yuv2rgb.h"


static GstElementDetails colorspace_details = {
  "Colorspace converter",
  "Filter/Effect",
  "Converts video from one colorspace to another",
  VERSION,
  "Wim Taymans <wim.taymans@chello.be>",
  "(C) 2001",
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

GST_PADTEMPLATE_FACTORY (colorspace_src_template_factory,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "colorspace_src",
    "video/raw",
      "format",		GST_PROPS_LIST (
	                  GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
	                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2")),
	                  GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB "))
	                ),
      "width", 		GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height",		GST_PROPS_INT_RANGE (0, G_MAXINT) 
  )
)

GST_PADTEMPLATE_FACTORY (colorspace_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "colorspace_sink",
    "video/raw",
      "format",		GST_PROPS_LIST (
	                  GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
	                  GST_PROPS_FOURCC (GST_STR_FOURCC ("YUY2")),
	                  GST_PROPS_FOURCC (GST_STR_FOURCC ("RGB "))
	                ),
      "width", 		GST_PROPS_INT_RANGE (0, G_MAXINT),
      "height",		GST_PROPS_INT_RANGE (0, G_MAXINT) 
  )
)

static void		gst_colorspace_class_init		(GstColorspaceClass *klass);
static void		gst_colorspace_init			(GstColorspace *space);

static void		gst_colorspace_set_property		(GObject *object, guint prop_id, 
								 const GValue *value, GParamSpec *pspec);
static void		gst_colorspace_get_property		(GObject *object, guint prop_id, 
								 GValue *value, GParamSpec *pspec);

static GstPadConnectReturn
			gst_colorspace_sinkconnect		(GstPad *pad, GstCaps *caps);
static GstPadConnectReturn
			gst_colorspace_srcconnect 		(GstPad *pad, GstCaps *caps);
static GstPadConnectReturn
			gst_colorspace_srcconnect_func 		(GstPad *pad, GstCaps *caps, gboolean newcaps);
static void		gst_colorspace_chain			(GstPad *pad, GstBuffer *buf);
static GstElementStateReturn
			gst_colorspace_change_state 		(GstElement *element);

// FIXME
extern void 	gst_colorspace_yuy2_to_i420	(unsigned char *src, unsigned char *dest, 
						 guint width, guint height);
extern void 	gst_colorspace_rgb32_to_i420	(unsigned char *src, unsigned char *dest, 
						 guint width, guint height);
extern void 	gst_colorspace_rgb32_to_yv12	(unsigned char *src, unsigned char *dest, 
						 guint width, guint height);

static GstElementClass *parent_class = NULL;
//static guint gst_colorspace_signals[LAST_SIGNAL] = { 0 };

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

static gboolean 
colorspace_setup_converter (GstColorspace *space, GstCaps *from_caps, GstCaps *to_caps)
{
  gulong from_space, to_space;

  g_return_val_if_fail (to_caps != NULL, FALSE);
  g_return_val_if_fail (from_caps != NULL, FALSE);

  from_space = gst_caps_get_fourcc_int (from_caps, "format");
  to_space = gst_caps_get_fourcc_int (to_caps, "format");

  GST_INFO (GST_CAT_NEGOTIATION, "set up converter for %08lx to %08lx", from_space, to_space);

  switch (from_space) {
    case GST_MAKE_FOURCC ('R','G','B',' '):
    {
      gint from_bpp = gst_caps_get_int (from_caps, "bpp");

      switch (to_space) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
#ifdef HAVE_HERMES
        {
	  space->source.r = gst_caps_get_int (from_caps, "red_mask");
	  space->source.g = gst_caps_get_int (from_caps, "green_mask");
	  space->source.b = gst_caps_get_int (from_caps, "blue_mask");
	  space->source.a = 0;
	  space->srcbpp = space->source.bits = from_bpp;
	  space->source.indexed = 0;
	  space->source.has_colorkey = 0;

	  GST_INFO (GST_CAT_PLUGIN_INFO, "source red mask   %08x", space->source.r);
	  GST_INFO (GST_CAT_PLUGIN_INFO, "source green mask %08x", space->source.g);
	  GST_INFO (GST_CAT_PLUGIN_INFO, "source blue mask  %08x", space->source.b);
	  GST_INFO (GST_CAT_PLUGIN_INFO, "source bpp        %08x", space->srcbpp);

	  space->dest.r = gst_caps_get_int (to_caps, "red_mask");
	  space->dest.g = gst_caps_get_int (to_caps, "green_mask");
	  space->dest.b = gst_caps_get_int (to_caps, "blue_mask");
	  space->dest.a = 0;
	  space->destbpp = space->dest.bits = gst_caps_get_int (to_caps, "bpp");
	  space->dest.indexed = 0;
	  space->dest.has_colorkey = 0;

	  GST_INFO (GST_CAT_PLUGIN_INFO, "dest red mask   %08x", space->dest.r);
	  GST_INFO (GST_CAT_PLUGIN_INFO, "dest green mask %08x", space->dest.g);
	  GST_INFO (GST_CAT_PLUGIN_INFO, "dest blue mask  %08x", space->dest.b);
	  GST_INFO (GST_CAT_PLUGIN_INFO, "dest bpp        %08x", space->destbpp);

	  if (!Hermes_ConverterRequest (space->h_handle, &space->source, &space->dest)) {
	    g_warning ("Hermes: could not get converter\n");
	    return FALSE;
	  }
	  GST_INFO (GST_CAT_PLUGIN_INFO, "converter set up");
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
          GST_INFO (GST_CAT_NEGOTIATION, "colorspace: RGB to YUV with bpp %d not implemented!!", from_bpp);
	  return FALSE;
      }
      break;
    }
    case GST_MAKE_FOURCC ('I','4','2','0'):
      switch (to_space) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
          GST_INFO (GST_CAT_NEGOTIATION, "colorspace: YUV to RGB");

	  space->destbpp = gst_caps_get_int (to_caps, "bpp");
	  space->converter = gst_colorspace_yuv2rgb_get_converter (from_caps, to_caps);
          space->type = GST_COLORSPACE_YUV_RGB;
	  return TRUE;
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_COLORSPACE_NONE;
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
      }
      break;
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
  ourcaps = gst_caps_copy (gst_pad_get_padtemplate_caps (pad));

  /* merge them together, we prefer the peercaps first */
  result = gst_caps_prepend (ourcaps, peercaps);

  return result;
}

static GstPadConnectReturn
gst_colorspace_sinkconnect (GstPad *pad, GstCaps *caps)
{
  GstColorspace *space;
  GstPad *peer;

  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  if (!GST_CAPS_IS_FIXED (caps)) {
    return GST_PAD_CONNECT_DELAYED;
  }

  space->width = gst_caps_get_int (caps, "width");
  space->height = gst_caps_get_int (caps, "height");

  GST_INFO (GST_CAT_PROPERTIES, "size: %dx%d", space->width, space->height);

  space->sinkcaps = caps;

  peer = gst_pad_get_peer (pad);
  if (peer) {
    if (!gst_colorspace_srcconnect_func (pad, gst_pad_get_allowed_caps (space->srcpad), FALSE)) {
      space->sinkcaps = NULL;
      return GST_PAD_CONNECT_REFUSED;
    }
  }

  return GST_PAD_CONNECT_OK;
}

static GstPadConnectReturn
gst_colorspace_srcconnect (GstPad *pad, GstCaps *caps)
{
  return gst_colorspace_srcconnect_func (pad, caps, TRUE);
}

static GstPadConnectReturn
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

    return GST_PAD_CONNECT_DELAYED;
  }

  /* first see if we can do the format natively by filtering the peer caps 
   * with our incomming caps */
  peercaps = gst_caps_intersect (caps, ourcaps); 
  if (peercaps) {
    /* see if the peer likes it too, it should as the caps say so.. */
    if (gst_pad_try_set_caps (space->srcpad, peercaps)) {
      space->type = GST_COLORSPACE_NONE;
      space->disabled = FALSE;
      return GST_PAD_CONNECT_OK;
    }
  }
  /* then see what the peer has that matches the size */
  peercaps = gst_caps_intersect (caps,
		  GST_CAPS_NEW (
		   "colorspace_filter",
		   "video/raw",
		     "width",   GST_PROPS_INT (space->width),
		     "height",  GST_PROPS_INT (space->height)
		  ));

  /* we are looping over the caps, so we have to get rid of the lists */
  peercaps = gst_caps_normalize (peercaps);

  /* loop over all possibilities and select the first one we can convert and
   * is accepted by the peer */
  while (peercaps) {
    if (colorspace_setup_converter (space, ourcaps, peercaps)) {
      if (gst_pad_try_set_caps (space->srcpad, peercaps)) {
        space->disabled = FALSE;
        return GST_PAD_CONNECT_OK;
      }
    }
    peercaps = peercaps->next;
  }
  
  gst_element_error (GST_ELEMENT (space), "could not agree on caps with peer pads");
  /* we disable ourself here */
  space->disabled = TRUE;

  return GST_PAD_CONNECT_REFUSED;
}

GType
gst_colorspace_get_type (void)
{
  static GType colorspace_type = 0;

  if (!colorspace_type) {
    static const GTypeInfo colorspace_info = {
      sizeof(GstColorspaceClass),      NULL,
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
		  GST_PADTEMPLATE_GET (colorspace_sink_template_factory), "sink");
  gst_pad_set_connect_function (space->sinkpad, gst_colorspace_sinkconnect);
  gst_pad_set_getcaps_function (space->sinkpad, gst_colorspace_getcaps);
  gst_pad_set_bufferpool_function (space->sinkpad, colorspace_get_bufferpool);
  gst_pad_set_chain_function(space->sinkpad,gst_colorspace_chain);
  gst_element_add_pad(GST_ELEMENT(space),space->sinkpad);

  space->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (colorspace_src_template_factory), "src");
  gst_element_add_pad(GST_ELEMENT(space),space->srcpad);
  gst_pad_set_connect_function (space->srcpad, gst_colorspace_srcconnect);

#ifdef HAVE_HERMES
  space->h_handle = Hermes_ConverterInstance (0);
#endif
  space->pool = NULL;
  space->converter = NULL;
  space->disabled = TRUE;
}

static void
gst_colorspace_chain (GstPad *pad,GstBuffer *buf)
{
  GstColorspace *space;
  gint size;
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
    gint dest_bytes, src_bytes;

    size = space->width * space->height;
    dest_bytes = ((space->destbpp+7)/8);
    src_bytes = ((space->srcbpp+7)/8);

    if (space->pool) {
      outbuf = gst_buffer_new_from_pool (space->pool, 0, 0);
    }

    if (!outbuf) {
      outbuf = gst_buffer_new ();

      GST_BUFFER_SIZE (outbuf) = (size * space->destbpp)/8;
      GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
    }

    if (space->type == GST_COLORSPACE_YUV_RGB) {
      gst_colorspace_convert (space->converter, GST_BUFFER_DATA (buf), GST_BUFFER_DATA (outbuf));
    }
#ifdef HAVE_HERMES
    else if (space->type == GST_COLORSPACE_HERMES) {
      Hermes_ConverterCopy (space->h_handle, 
		    GST_BUFFER_DATA (buf), 0, 0, space->width, space->height, space->width * src_bytes, 
	  	    GST_BUFFER_DATA (outbuf), 0, 0, space->width, space->height, space->width * dest_bytes);
    }
#endif
    else if (space->type == GST_COLORSPACE_YUY2_I420) {
      gst_colorspace_yuy2_to_i420 (GST_BUFFER_DATA (buf),
		                   GST_BUFFER_DATA (outbuf),
				   space->width,
				   space->height);
    }
    else if (space->type == GST_COLORSPACE_RGB32_I420) {
      gst_colorspace_rgb32_to_i420 (GST_BUFFER_DATA (buf),
		                   GST_BUFFER_DATA (outbuf),
				   space->width,
				   space->height);
    }
    else if (space->type == GST_COLORSPACE_RGB32_YV12) {
      gst_colorspace_rgb32_to_yv12 (GST_BUFFER_DATA (buf),
		                   GST_BUFFER_DATA (outbuf),
				   space->width,
				   space->height);
    }

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

    gst_buffer_unref (buf);
  }
  gst_pad_push (space->srcpad, outbuf);
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
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;
#ifdef HAVE_HERMES
  gint hermes_res;

  hermes_res = Hermes_Init();
  g_return_val_if_fail (hermes_res != 0, FALSE);
#endif

  factory = gst_elementfactory_new ("colorspace", GST_TYPE_COLORSPACE,
                                    &colorspace_details);
  g_return_val_if_fail (factory != NULL, FALSE);
  
  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (colorspace_src_template_factory));
  gst_elementfactory_add_padtemplate (factory, 
		  GST_PADTEMPLATE_GET (colorspace_sink_template_factory));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "colorspace",
  plugin_init
};








