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
     NULL
  )
)

GST_PADTEMPLATE_FACTORY (colorspace_sink_template_factory,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "colorspace_sink",
    "video/raw",
    NULL
  )
)

static void		gst_colorspace_class_init		(GstColorspaceClass *klass);
static void		gst_colorspace_init			(GstColorspace *space);

static void		gst_colorspace_set_property			(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void		gst_colorspace_get_property			(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

static void		gst_colorspace_chain			(GstPad *pad, GstBuffer *buf);

// FIXME
extern void gst_colorspace_yuy2_to_i420(unsigned char *src, unsigned char *dest, guint width, guint height);

static GstElementClass *parent_class = NULL;
//static guint gst_colorspace_signals[LAST_SIGNAL] = { 0 };

static GstBufferPool* 
colorspace_get_bufferpool (GstPad *pad)
{
  GstColorspace *space;

  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  if (space->type == GST_COLORSPACE_NONE)
    return gst_pad_get_bufferpool (space->srcpad);
  else
    return NULL;
}

static gboolean 
colorspace_setup_converter (GstColorspace *space)
{
  gulong from_space, to_space;
  GstCaps *from_caps, *to_caps;

  from_caps = space->sinkcaps;
  to_caps = space->srccaps;

  g_return_val_if_fail (to_caps != NULL, FALSE);
  g_return_val_if_fail (from_caps != NULL, FALSE);

  from_space = gst_caps_get_fourcc_int (from_caps, "format");
  to_space = gst_caps_get_fourcc_int (to_caps, "format");

  g_warning ("set up converter for %08lx to %08lx\n", from_space, to_space);

  switch (from_space) {
    case GST_MAKE_FOURCC ('R','G','B',' '):
      switch (to_space) {
#ifdef HAVE_LIBHERMES
        case GST_MAKE_FOURCC ('R','G','B',' '):
        {
	  space->source.r = gst_caps_get_int (from_caps, "red_mask");
	  space->source.g = gst_caps_get_int (from_caps, "green_mask");
	  space->source.b = gst_caps_get_int (from_caps, "blue_mask");
	  space->source.a = 0;
	  space->srcbpp = space->source.bits = gst_caps_get_int (from_caps, "bpp");
	  space->source.indexed = 0;
	  space->source.has_colorkey = 0;

	  GST_INFO (0,"source red mask   %08x\n", space->source.r);
	  GST_INFO (0, "source green mask %08x\n", space->source.g);
	  GST_INFO (0, "source blue mask  %08x\n", space->source.b);
	  GST_INFO (0, "source bpp        %08x\n", space->srcbpp);

	  space->dest.r = gst_caps_get_int (to_caps, "red_mask");
	  space->dest.g = gst_caps_get_int (to_caps, "green_mask");
	  space->dest.b = gst_caps_get_int (to_caps, "blue_mask");
	  space->dest.a = 0;
	  space->destbpp = space->dest.bits = gst_caps_get_int (to_caps, "bpp");
	  space->dest.indexed = 0;
	  space->dest.has_colorkey = 0;

	  GST_INFO (0, "dest red mask   %08x\n", space->dest.r);
	  GST_INFO (0, "dest green mask %08x\n", space->dest.g);
	  GST_INFO (0, "dest blue mask  %08x\n", space->dest.b);
	  GST_INFO (0, "dest bpp        %08x\n", space->destbpp);

	  if (!Hermes_ConverterRequest (space->h_handle, &space->source, &space->dest)) {
	    g_warning ("could not get converter\n");
	    return FALSE;
	  }
	  GST_INFO (0, "converter set up\n");
          space->type = GST_COLORSPACE_HERMES;
          break;
	}
#endif
        case GST_MAKE_FOURCC ('Y','U','Y','2'):
        case GST_MAKE_FOURCC ('I','4','2','0'):
          g_error ("colorspace: RGB to YUV implement me");
          break;
      }
      break;
    case GST_MAKE_FOURCC ('Y','U','Y','2'):
    case GST_MAKE_FOURCC ('I','4','2','0'):
      switch (to_space) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
          g_warning ("colorspace: YUV to RGB");

	  space->destbpp = gst_caps_get_int (to_caps, "bpp");
	  space->converter = gst_colorspace_yuv2rgb_get_converter (from_caps, to_caps);
          space->type = GST_COLORSPACE_YUV_RGB;
          break;
        case GST_MAKE_FOURCC ('I','4','2','0'):
          space->type = GST_COLORSPACE_YUY2_I420;
	  space->destbpp = 12;
          break;
      }
      break;
  }
  return TRUE;
}

static GstPadNegotiateReturn
colorspace_negotiate_src (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstColorspace* space = GST_COLORSPACE (gst_object_get_parent (GST_OBJECT (pad)));
  GstCaps *original;
  gint src_width, src_height;

  GST_DEBUG (GST_CAT_NEGOTIATION, "colorspace: src negotiate\n");

  g_return_val_if_fail (space->sinkcaps != NULL, GST_PAD_NEGOTIATE_FAIL);

  src_width = gst_caps_get_int (space->sinkcaps, "width");
  src_height = gst_caps_get_int (space->sinkcaps, "height");

  space->width = src_width;
  space->height = src_height;

  if (*caps==NULL)  {
    *caps = gst_caps_new ("colorspace_caps",
		    "video/raw",
		    gst_props_new (
			    "format", GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
			    "width",  GST_PROPS_INT (src_width),
			    "height",  GST_PROPS_INT (src_height),
			    NULL));
    space->srccaps = gst_caps_ref (*caps);
    return GST_PAD_NEGOTIATE_TRY;
    //return gst_pad_negotiate_proxy (pad, space->sinkpad, caps);
  }


  original = gst_caps_copy (*caps);
  //g_print ("%d %d\n", src_width, src_height);

  // peers couldn't agree, we need to help
  switch (gst_caps_get_fourcc_int (original, "format")) {
    case GST_MAKE_FOURCC ('R','G','B',' '):
      gst_caps_ref (*caps);
      if (gst_caps_get_int (*caps, "width") == src_width &&
          gst_caps_get_int (*caps, "height") == src_height) 
      {
        space->srccaps = *caps;
        if (colorspace_setup_converter (space)) {
          return GST_PAD_NEGOTIATE_AGREE;
	}
      }
      else {
        gst_caps_set (*caps, "width", GST_PROPS_INT (src_width));
        gst_caps_set (*caps, "height", GST_PROPS_INT (src_height));

        space->srccaps = *caps;
	// FIXME
	GST_PAD_CAPS (space->srcpad) = gst_caps_ref (*caps);

        return GST_PAD_NEGOTIATE_TRY;
      }
      break;
    case GST_MAKE_FOURCC ('Y','U','Y','2'):
    case GST_MAKE_FOURCC ('I','4','2','0'):
      //space->srccaps = original;
      //fprintf (stderr, "found something suitable\n");
      return GST_PAD_NEGOTIATE_AGREE;
    default:
      *caps = NULL;
      return GST_PAD_NEGOTIATE_TRY;
      break;
  }
  return GST_PAD_NEGOTIATE_FAIL;
}

static GstPadNegotiateReturn
colorspace_negotiate_sink (GstPad *pad, GstCaps **caps, gpointer *data)
{
  GstColorspace* space = GST_COLORSPACE (gst_object_get_parent (GST_OBJECT (pad)));
  GstCaps *original;

  GST_DEBUG (GST_CAT_NEGOTIATION, "colorspace: sink negotiate\n");
  
  if (*caps==NULL) 
    return gst_pad_negotiate_proxy (pad, space->srcpad, caps);
    //return GST_PAD_NEGOTIATE_FAIL;


  space->type = GST_COLORSPACE_NONE;

  original = gst_caps_copy (*caps);

  // see if a common format exists between both peers...
  switch (gst_pad_negotiate_proxy (pad, space->srcpad, caps)) {
    case GST_PAD_NEGOTIATE_AGREE:
      //g_print ("colorspace: common format found\n");
      return GST_PAD_NEGOTIATE_AGREE;
    default:
      break;
  }
  g_warning ("colorspace: no common format found\n");
  g_warning ("colorspace: src: %08lx\n", gst_caps_get_fourcc_int (original, "format"));

  // peers couldn't agree, we need to help
  space->sinkcaps = original;

  /*
  space->width = gst_caps_get_int (original, "width");
  space->height = gst_caps_get_int (original, "height");

  space->srccaps = gst_caps_new (
		  "testcaps",
		  "video/raw",
		  gst_props_new (
		    "format",   GST_PROPS_FOURCC (GST_STR_FOURCC ("I420")),
		     "width",   GST_PROPS_INT (gst_caps_get_int (original, "width")),
		     "height",  GST_PROPS_INT (gst_caps_get_int (original, "height")),
		     NULL
		     ));

  GST_PAD_CAPS (space->srcpad) = space->srccaps;
  */

  if (gst_pad_renegotiate (space->srcpad)) {
    g_warning ("found something suitable\n");
    if (colorspace_setup_converter (space)) {
      return GST_PAD_NEGOTIATE_AGREE;
    }
  }

  return GST_PAD_NEGOTIATE_FAIL;
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
}

static void
gst_colorspace_init (GstColorspace *space)
{
  space->sinkpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (colorspace_sink_template_factory), "sink");
  gst_pad_set_negotiate_function (space->sinkpad, colorspace_negotiate_sink);
  gst_pad_set_bufferpool_function (space->sinkpad, colorspace_get_bufferpool);
  gst_pad_set_chain_function(space->sinkpad,gst_colorspace_chain);
  gst_element_add_pad(GST_ELEMENT(space),space->sinkpad);

  space->srcpad = gst_pad_new_from_template (
		  GST_PADTEMPLATE_GET (colorspace_src_template_factory), "src");
  gst_pad_set_negotiate_function (space->srcpad, colorspace_negotiate_src);
  gst_element_add_pad(GST_ELEMENT(space),space->srcpad);

#ifdef HAVE_LIBHERMES
  space->h_handle = Hermes_ConverterInstance (0);
#endif
  space->pool = NULL;
  space->converter = NULL;
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

  if (space->type == GST_COLORSPACE_NONE) {
    outbuf = buf;
  }
  else {
    gint dest_bytes, src_bytes;

    if (!space->pool) {
      space->pool = gst_pad_get_bufferpool (space->srcpad);
    }

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
#ifdef HAVE_LIBHERMES
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

    GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (buf);

    gst_buffer_unref (buf);
  }
  gst_pad_push (space->srcpad, outbuf);
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
#ifdef HAVE_LIBHERMES
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








