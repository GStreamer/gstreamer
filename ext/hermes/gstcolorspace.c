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

#include "gstcolorspace.h"
#include "yuv2rgb.h"
#include "yuv2yuv.h"


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

static void             gst_colorspace_base_init                (gpointer g_class);
static void		gst_colorspace_class_init		(GstColorspaceClass *klass);
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

/* FIXME */
extern void 	gst_colorspace_rgb32_to_i420	(unsigned char *src, unsigned char *dest, 
						 guint width, guint height);
extern void 	gst_colorspace_rgb32_to_yv12	(unsigned char *src, unsigned char *dest, 
						 guint width, guint height);

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

static gboolean 
colorspace_setup_converter (GstColorspace *space, GstCaps *from_caps, GstCaps *to_caps)
{
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

  GST_INFO ("set up converter for "  GST_FOURCC_FORMAT
	    " (%08x) to " GST_FOURCC_FORMAT " (%08x)",
	    GST_FOURCC_ARGS (from_space), from_space,
	    GST_FOURCC_ARGS (to_space), to_space);

  switch (from_space) {
    case GST_MAKE_FOURCC ('R','G','B',' '):
    {
      gint from_bpp;
      
      gst_caps_get_int (from_caps, "bpp", &from_bpp);

      switch (to_space) {
        case GST_MAKE_FOURCC ('R','G','B',' '):
#ifdef HAVE_HERMES
        {
          gint to_bpp;
      
          gst_caps_get_int (to_caps, "bpp", &to_bpp);

	  gst_caps_get_int (from_caps, "red_mask",   &space->source.r);
	  gst_caps_get_int (from_caps, "green_mask", &space->source.g);
	  gst_caps_get_int (from_caps, "blue_mask",  &space->source.b);
	  space->source.a = 0;
	  space->srcbpp = space->source.bits = from_bpp;
	  space->source.indexed = 0;
	  space->source.has_colorkey = 0;

	  GST_INFO ( "source red mask   %08x", space->source.r);
	  GST_INFO ( "source green mask %08x", space->source.g);
	  GST_INFO ( "source blue mask  %08x", space->source.b);
	  GST_INFO ( "source bpp        %08x", space->srcbpp);

	  gst_caps_get_int (to_caps, "red_mask",   &space->dest.r);
	  gst_caps_get_int (to_caps, "green_mask", &space->dest.g);
	  gst_caps_get_int (to_caps, "blue_mask",  &space->dest.b);
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

	  gst_caps_get_int (to_caps, "bpp", &space->destbpp);
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

	  gst_caps_get_int (to_caps, "bpp", &space->destbpp);
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

static GstCaps*
gst_colorspace_getcaps (GstPad *pad, GstCaps *caps)
{
  GstColorspace *space;
  GstCaps *result;
  GstCaps *peercaps;
  GstCaps *ourcaps, *temp;
  
  space = GST_COLORSPACE (gst_pad_get_parent (pad));

  /* we can do everything our peer can... */
  temp = gst_pad_get_allowed_caps (space->srcpad);
  peercaps = gst_caps_copy (temp);
  gst_caps_unref (temp);

  /* and our own template of course */
  temp = gst_pad_get_pad_template_caps (pad);
  ourcaps = gst_caps_copy (temp);
  gst_caps_unref (temp);

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

  gst_caps_replace_sink (&space->sinkcaps, caps);

  peer = gst_pad_get_peer (pad);
  if (peer) {
    GstCaps *allowed = gst_pad_get_allowed_caps (space->srcpad);
    if (gst_colorspace_srcconnect_func (pad, allowed, FALSE) < 1) {
      space->sinkcaps = NULL;
      gst_caps_unref (allowed);
      return GST_PAD_LINK_REFUSED;
    }
    gst_caps_unref (allowed);
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
  GstCaps *ourcaps, *to_intersect, *try_peercaps;
  GstPadLinkReturn res = GST_PAD_LINK_REFUSED;
  
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
    GstCaps *trycaps;

    trycaps = peercaps;
    while(trycaps){
      GstCaps *caps1 = gst_caps_copy_1(trycaps);

      /* see if the peer likes it too, it should as the caps say so.. */
      if (gst_pad_try_set_caps (space->srcpad, caps1) > 0) {
	space->type = GST_COLORSPACE_NONE;
	space->disabled = FALSE;
	gst_caps_unref (peercaps);
	peercaps = caps1;
	res = GST_PAD_LINK_DONE;
	goto success;
      }
      trycaps = trycaps->next;
    }
    gst_caps_unref (peercaps);
  }

  to_intersect = gst_caps_append (
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
                 ));

  /* then see what the peer has that matches the size */
  peercaps = gst_caps_intersect (caps, to_intersect);
  gst_caps_unref (to_intersect);

  /* we are looping over the caps, so we have to get rid of the lists */
  try_peercaps = gst_caps_normalize (peercaps);
  gst_caps_unref (peercaps);
  peercaps = try_peercaps;

  /* loop over all possibilities and select the first one we can convert and
   * is accepted by the peer */
  while (peercaps) {
    GstCaps *peer1 = gst_caps_copy_1(peercaps);

    if (colorspace_setup_converter (space, ourcaps, peer1)) {
      if (gst_pad_try_set_caps (space->srcpad, peer1) > 0) {
        space->disabled = FALSE;
        gst_caps_unref (try_peercaps);
	res = GST_PAD_LINK_DONE;
	goto success;
      }
    }
    peercaps = peercaps->next;
  }
  gst_caps_unref (try_peercaps);

  /* we disable ourself here */
  space->disabled = TRUE;
  goto done;

success:
done:

  return res;
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
  GstCaps *caps;
  
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
					GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24_32_REVERSE));
  caps = gst_caps_append (caps,
			  gst_caps_new ("csp_templ_rgb24_32",
					"video/x-raw-rgb",
					GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24_32));
  caps = gst_caps_append (caps,
			  gst_caps_new ("csp_templ_rgb15",
					"video/x-raw-rgb",
					GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_15));
  caps = gst_caps_append (caps,
			  gst_caps_new ("csp_templ_rgb16",
					"video/x-raw-rgb",
					GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_16));
  
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

#ifdef HAVE_HERMES
  space->h_handle = Hermes_ConverterInstance (0);
#endif
  space->pool = NULL;
  space->converter = NULL;
  space->disabled = TRUE;
}

static void
gst_colorspace_chain (GstPad *pad,GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
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

    if (!space->pool)
      space->pool = gst_pad_get_bufferpool (space->srcpad);

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
    else if (space->type == GST_COLORSPACE_420_SWAP) {
      gst_colorspace_i420_to_yv12 (GST_BUFFER_DATA (buf),
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
      if (space->pool)
	gst_buffer_pool_unref (space->pool);
      space->pool = NULL;
      break;
    case GST_STATE_PAUSED_TO_READY:
      gst_colorspace_converter_destroy (space->converter);
      space->converter = NULL;
      space->disabled = TRUE;
      space->type = GST_COLORSPACE_NONE;
      gst_caps_replace (&space->sinkcaps, NULL);
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
#ifdef HAVE_HERMES
  gint hermes_res;

  hermes_res = Hermes_Init();
  g_return_val_if_fail (hermes_res != 0, FALSE);
#endif

  if (!gst_element_register (plugin, "colorspace", GST_RANK_PRIMARY, GST_TYPE_COLORSPACE))
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
  GST_COPYRIGHT,
  GST_PACKAGE,
  GST_ORIGIN)

