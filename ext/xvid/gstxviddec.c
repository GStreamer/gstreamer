/* GStreamer xvid decoder plugin
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#include <xvid.h>

#include <gst/video/video.h>
#include "gstxviddec.h"

/* elementfactory information */
GstElementDetails gst_xviddec_details = {
  "Xvid decoder",
  "Codec/Video/Decoder",
  "Xvid decoder based on xvidcore",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
};

static GstStaticPadTemplate sink_template =
GST_STATIC_PAD_TEMPLATE (
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    "video/x-xvid, "
    "width = (int) [ 0, MAX ], "
    "height = (int) [ 0, MAX ], "
    "framerate = (double) [ 0, MAX ]"
  )
);

static GstStaticPadTemplate src_template =
GST_STATIC_PAD_TEMPLATE (
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_STATIC_CAPS (
    GST_VIDEO_CAPS_YUV ("{ I420, YUY2, YV12, YVYU, UYVY }") "; "
    RGB_24_32_STATIC_CAPS (32, 0x00ff0000, 0x0000ff00, 0x000000ff) "; "
    RGB_24_32_STATIC_CAPS (32, 0xff000000, 0x00ff0000, 0x0000ff00) "; "
    RGB_24_32_STATIC_CAPS (32, 0x0000ff00, 0x00ff0000, 0xff000000) "; "
    RGB_24_32_STATIC_CAPS (32, 0x000000ff, 0x0000ff00, 0x00ff0000) "; "
    RGB_24_32_STATIC_CAPS (24, 0x0000ff, 0x00ff00, 0xff0000) "; "
    GST_VIDEO_CAPS_RGB_15 "; "
    GST_VIDEO_CAPS_RGB_16
  )
);


/* XvidDec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
  /* FILL ME */
};

static void gst_xviddec_base_init    (gpointer g_class);
static void gst_xviddec_class_init   (GstXvidDecClass *klass);
static void gst_xviddec_init         (GstXvidDec      *xviddec);
static void gst_xviddec_chain        (GstPad          *pad,
                                      GstData         *data);
static GstPadLinkReturn
	    gst_xviddec_sink_link    (GstPad          *pad,
                                      const GstCaps   *vscapslist);
static GstPadLinkReturn
	    gst_xviddec_src_link     (GstPad          *pad,
                                      const GstCaps   *vscapslist);
static GstCaps *
            gst_xviddec_src_getcaps  (GstPad          *pad);
static GstElementStateReturn
	    gst_xviddec_change_state (GstElement      *element);


static GstElementClass *parent_class = NULL;
/* static guint gst_xviddec_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_xviddec_get_type(void)
{
  static GType xviddec_type = 0;

  if (!xviddec_type)
  {
    static const GTypeInfo xviddec_info = {
      sizeof(GstXvidDecClass),
      gst_xviddec_base_init,
      NULL,
      (GClassInitFunc) gst_xviddec_class_init,
      NULL,
      NULL,
      sizeof(GstXvidDec),
      0,
      (GInstanceInitFunc) gst_xviddec_init,
    };
    xviddec_type = g_type_register_static(GST_TYPE_ELEMENT,
                                          "GstXvidDec",
                                          &xviddec_info, 0);
  }
  return xviddec_type;
}

static void
gst_xviddec_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (element_class, 
		  gst_static_pad_template_get (&sink_template));
  gst_element_class_add_pad_template (element_class, 
		  gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_xviddec_details);
}

static void
gst_xviddec_class_init (GstXvidDecClass *klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gstelement_class->change_state = gst_xviddec_change_state;
}


static void
gst_xviddec_init (GstXvidDec *xviddec)
{
  gst_xvid_init();

  /* create the sink pad */
  xviddec->sinkpad = gst_pad_new_from_template(
                       gst_static_pad_template_get (&sink_template),
                       "sink");
  gst_element_add_pad(GST_ELEMENT(xviddec), xviddec->sinkpad);

  gst_pad_set_chain_function(xviddec->sinkpad, gst_xviddec_chain);
  gst_pad_set_link_function(xviddec->sinkpad, gst_xviddec_sink_link);

  /* create the src pad */
  xviddec->srcpad = gst_pad_new_from_template(
                      gst_static_pad_template_get (&src_template),
                      "src");
  gst_element_add_pad(GST_ELEMENT(xviddec), xviddec->srcpad);

  gst_pad_set_getcaps_function (xviddec->srcpad, gst_xviddec_src_getcaps);
  gst_pad_set_link_function(xviddec->srcpad, gst_xviddec_src_link);

  /* size, etc. */
  xviddec->width = xviddec->height = xviddec->csp = -1;

  /* set xvid handle to NULL */
  xviddec->handle = NULL;
}


static void
gst_xviddec_unset (GstXvidDec *xviddec)
{
  /* unref this instance */
  xvid_decore(xviddec->handle, XVID_DEC_DESTROY, NULL, NULL);
  xviddec->handle = NULL;
}


static gboolean
gst_xviddec_setup (GstXvidDec *xviddec)
{
  xvid_dec_create_t xdec;
  int ret;

  /* initialise parameters, see xvid documentation */
  gst_xvid_init_struct (xdec);
  xdec.width = xviddec->width;
  xdec.height = xviddec->height;
  xdec.handle = NULL;

  if ((ret = xvid_decore(NULL, XVID_DEC_CREATE,
                         &xdec, NULL)) < 0) {
    GST_ELEMENT_ERROR (xviddec, LIBRARY, SETTINGS, (NULL),
		      ("Setting parameters %dx%d@%d failed: %s (%d)",
	              xviddec->width, xviddec->height, xviddec->csp,
		      gst_xvid_error(ret), ret));
    return FALSE;
  }

  xviddec->handle = xdec.handle;

  return TRUE;
}


static void
gst_xviddec_chain (GstPad    *pad,
                   GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstXvidDec *xviddec = GST_XVIDDEC(GST_OBJECT_PARENT(pad));
  GstBuffer *outbuf;
  xvid_dec_frame_t xframe;
  int ret;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));

  if (!xviddec->handle) {
    GST_ELEMENT_ERROR (xviddec, CORE, NEGOTIATION, (NULL),
         ("format wasn't negotiated before chain function"));
    gst_buffer_unref(buf);
    return;
  }

  outbuf = gst_buffer_new_and_alloc(xviddec->width *
                                    xviddec->height *
                                    xviddec->bpp / 8);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);
  GST_BUFFER_DURATION(outbuf)  = GST_BUFFER_DURATION(buf);
  GST_BUFFER_SIZE(outbuf) = xviddec->width *
                            xviddec->height *
                            xviddec->bpp / 8;

  /* decode and so ... */
  gst_xvid_init_struct (xframe);
  xframe.general = 0;
  xframe.bitstream = (void *) GST_BUFFER_DATA(buf);
  xframe.length = GST_BUFFER_SIZE(buf);
  xframe.output.csp = xviddec->csp;
  if (xviddec->width == xviddec->stride) {
    xframe.output.plane[0] = GST_BUFFER_DATA(outbuf);
    xframe.output.plane[1] = xframe.output.plane[0] + (xviddec->width * xviddec->height);
    xframe.output.plane[2] = xframe.output.plane[1] + (xviddec->width * xviddec->height / 4);
    xframe.output.stride[0] = xviddec->width;
    xframe.output.stride[1] = xviddec->width / 2;
    xframe.output.stride[2] = xviddec->width / 2;
  } else {
    xframe.output.plane[0] = GST_BUFFER_DATA(outbuf);
    xframe.output.stride[0] = xviddec->stride;
  }

  if ((ret = xvid_decore(xviddec->handle, XVID_DEC_DECODE,
                         &xframe, NULL)) < 0) {
    GST_ELEMENT_ERROR (xviddec, STREAM, DECODE, (NULL),
                      ("Error decoding xvid frame: %s (%d)\n",
		      gst_xvid_error(ret), ret));
    gst_buffer_unref(buf);
    gst_buffer_unref(outbuf);
    return;
  }

  gst_pad_push(xviddec->srcpad, GST_DATA (outbuf));
  gst_buffer_unref(buf);
}

static GstCaps *
gst_xviddec_src_getcaps (GstPad *pad)
{
  GstXvidDec *xviddec = GST_XVIDDEC (gst_pad_get_parent (pad));
  GstCaps *caps;
  gint csp[] = {
    XVID_CSP_I420,
    XVID_CSP_YV12,
    XVID_CSP_YUY2,
    XVID_CSP_UYVY,
    XVID_CSP_YVYU,
    XVID_CSP_BGRA,
    XVID_CSP_ABGR,
    XVID_CSP_RGBA,
#ifdef XVID_CSP_ARGB
    XVID_CSP_ARGB,
#endif
    XVID_CSP_BGR,
    XVID_CSP_RGB555,
    XVID_CSP_RGB565,
    0
  }, i;

  if (!GST_PAD_CAPS (xviddec->sinkpad)) {
    GstPadTemplate *templ = gst_static_pad_template_get (&src_template);
    return gst_caps_copy (gst_pad_template_get_caps (templ));
  }

  caps = gst_caps_new_empty ();
  for (i = 0; csp[i] != 0; i++) {
    GstCaps *one = gst_xvid_csp_to_caps (csp[i], xviddec->width,
					 xviddec->height, xviddec->fps);
    gst_caps_append (caps, one);
  }

  return caps;
}

static GstPadLinkReturn
gst_xviddec_src_link (GstPad        *pad,
                      const GstCaps *vscaps)
{
  GstXvidDec *xviddec = GST_XVIDDEC(gst_pad_get_parent (pad));
  GstStructure *structure = gst_caps_get_structure (vscaps, 0);

  if (!GST_PAD_CAPS (xviddec->sinkpad))
    return GST_PAD_LINK_DELAYED;

  /* if there's something old around, remove it */
  if (xviddec->handle) {
    gst_xviddec_unset(xviddec);
  }
g_print ("out: %s\n", gst_caps_to_string (vscaps));
  xviddec->csp = gst_xvid_structure_to_csp (structure, xviddec->width,
					    &xviddec->stride,
					    &xviddec->bpp);

  if (xviddec->csp < 0)
    return GST_PAD_LINK_REFUSED;

  if (!gst_xviddec_setup(xviddec))
    return GST_PAD_LINK_REFUSED;;

  return GST_PAD_LINK_OK;
}

static GstPadLinkReturn
gst_xviddec_sink_link (GstPad        *pad,
                       const GstCaps *vscaps)
{
  GstXvidDec *xviddec = GST_XVIDDEC(gst_pad_get_parent (pad));
  GstStructure *structure;

  /* if there's something old around, remove it */
  if (xviddec->handle) {
    gst_xviddec_unset(xviddec);
  }

  /* if we get here, we know the input is xvid. we
   * only need to bother with the output colorspace,
   * which the src_link function takes care of. */
  structure = gst_caps_get_structure (vscaps, 0);
  gst_structure_get_int(structure, "width", &xviddec->width);
  gst_structure_get_int(structure, "height", &xviddec->height);
  gst_structure_get_double(structure, "framerate", &xviddec->fps);
g_print ("in: %dx%d\n", xviddec->width, xviddec->height);
  /* re-nego? or just await src nego? */
  if (GST_PAD_CAPS(xviddec->srcpad)) {
    GstPadLinkReturn ret;
    GstCaps *vscaps = gst_pad_get_allowed_caps (xviddec->srcpad), *new;
    gint i, csp;

    for (i = 0; i < gst_caps_get_size (vscaps); i++) {
      csp = gst_xvid_structure_to_csp (gst_caps_get_structure (vscaps, i),
				       0, NULL, NULL);
      new = gst_xvid_csp_to_caps (csp, xviddec->width, xviddec->height, xviddec->fps);
      ret = gst_pad_try_set_caps(xviddec->srcpad, new);
      if (ret != GST_PAD_LINK_REFUSED)
        return ret;
    }

    return GST_PAD_LINK_REFUSED;
  }

  return GST_PAD_LINK_OK;
}

static GstElementStateReturn
gst_xviddec_change_state (GstElement *element)
{
  GstXvidDec *xviddec = GST_XVIDDEC (element);

  switch (GST_STATE_PENDING (element)) {
    case GST_STATE_PAUSED_TO_READY:
      if (xviddec->handle) {
        gst_xviddec_unset (xviddec);
      }
      break;
    default:
      break;
  }

  if (parent_class->change_state)
    return parent_class->change_state (element);

  return GST_STATE_SUCCESS;
}
