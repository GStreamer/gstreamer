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
#include "gstxviddec.h"
#include <gst/video/video.h>

/* elementfactory information */
GstElementDetails gst_xviddec_details = {
  "Xvid decoder",
  "Codec/Video/Decoder",
  "Xvid decoder based on xviddecore",
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
    GST_VIDEO_YUV_PAD_TEMPLATE_CAPS ("{ I420, YUY2, YV12, YVYU, UYVY }") "; "
    GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_24_32 "; "
    GST_VIDEO_RGB_PAD_TEMPLATE_CAPS_15_16
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

static void             gst_xviddec_base_init    (gpointer g_class);
static void             gst_xviddec_class_init   (GstXvidDecClass *klass);
static void             gst_xviddec_init         (GstXvidDec      *xviddec);
static void             gst_xviddec_dispose      (GObject         *object);
static void             gst_xviddec_chain        (GstPad          *pad,
                                                  GstData         *data);
static GstPadLinkReturn gst_xviddec_link	 (GstPad          *pad,
                                                  const GstCaps  *vscapslist);
static GstPadLinkReturn	gst_xviddec_negotiate	 (GstXvidDec *xviddec);

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
  GObjectClass *gobject_class = (GObjectClass *) klass;

  gst_xvid_init();

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_xviddec_dispose;
}


static void
gst_xviddec_init (GstXvidDec *xviddec)
{
  /* create the sink pad */
  xviddec->sinkpad = gst_pad_new_from_template(
                       gst_static_pad_template_get (&sink_template),
                       "sink");
  gst_element_add_pad(GST_ELEMENT(xviddec), xviddec->sinkpad);

  gst_pad_set_chain_function(xviddec->sinkpad, gst_xviddec_chain);
  gst_pad_set_link_function(xviddec->sinkpad, gst_xviddec_link);

  /* create the src pad */
  xviddec->srcpad = gst_pad_new_from_template(
                      gst_static_pad_template_get (&src_template),
                      "src");
  gst_element_add_pad(GST_ELEMENT(xviddec), xviddec->srcpad);

  /* bitrate, etc. */
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
  XVID_DEC_PARAM xdec;
  int ret;

  /* initialise parameters, see xvid documentation */
  memset(&xdec, 0, sizeof(XVID_DEC_PARAM));
  xdec.width = xviddec->width;
  xdec.height = xviddec->height;

  if ((ret = xvid_decore(NULL, XVID_DEC_CREATE,
                         &xdec, NULL)) != XVID_ERR_OK) {
    gst_element_error(GST_ELEMENT(xviddec),
		      "Setting parameters %dx%d@%d failed: %s (%d)",
	              xviddec->width, xviddec->height, xviddec->csp,
		      gst_xvid_error(ret), ret);
    return FALSE;
  }

  xviddec->handle = xdec.handle;

  return TRUE;
}


static void
gst_xviddec_dispose (GObject *object)
{
  GstXvidDec *xviddec = GST_XVIDDEC(object);

  gst_xviddec_unset(xviddec);
}


static void
gst_xviddec_chain (GstPad    *pad,
                   GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstXvidDec *xviddec;
  GstBuffer *outbuf;
  XVID_DEC_FRAME xframe;
  int ret;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  xviddec = GST_XVIDDEC(GST_OBJECT_PARENT(pad));

  if (!xviddec->handle) {
    if (!gst_xviddec_negotiate(xviddec)) {
      gst_element_error(GST_ELEMENT(xviddec),
                        "No format set - aborting");
      gst_buffer_unref(buf);
      return;
    }
  }

  outbuf = gst_buffer_new_and_alloc(xviddec->width *
                                    xviddec->height *
                                    xviddec->bpp / 8);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);
  GST_BUFFER_SIZE(outbuf) = xviddec->width *
                            xviddec->height *
                            xviddec->bpp / 8;

  /* encode and so ... */
  xframe.bitstream = (void *) GST_BUFFER_DATA(buf);
  xframe.image = (void *) GST_BUFFER_DATA(outbuf);
  xframe.length = GST_BUFFER_SIZE(buf);
  xframe.stride = 0; /*xviddec->width * xviddec->bpp / 8;*/
  xframe.colorspace = xviddec->csp;

  if ((ret = xvid_decore(xviddec->handle, XVID_DEC_DECODE,
                         &xframe, NULL))) {
    gst_element_error(GST_ELEMENT(xviddec),
                      "Error decoding xvid frame: %s (%d)\n",
		      gst_xvid_error(ret), ret);
    gst_buffer_unref(buf);
    return;
  }

  gst_pad_push(xviddec->srcpad, GST_DATA (outbuf));
  gst_buffer_unref(buf);
}


static GstPadLinkReturn
gst_xviddec_negotiate (GstXvidDec *xviddec)
{
  GstPadLinkReturn ret;
  GstCaps *caps;
  struct {
    guint32 fourcc;
    gint    depth, bpp;
    gint    csp;
  } fmt_list[] = {
    { GST_MAKE_FOURCC('Y','U','Y','V'), 16, 16, XVID_CSP_YUY2   },
    { GST_MAKE_FOURCC('U','Y','V','Y'), 16, 16, XVID_CSP_UYVY   },
    { GST_MAKE_FOURCC('Y','V','Y','U'), 16, 16, XVID_CSP_YVYU   },
    { GST_MAKE_FOURCC('Y','V','1','2'), 12, 12, XVID_CSP_YV12   },
    { GST_MAKE_FOURCC('I','4','2','0'), 12, 12, XVID_CSP_I420   },
    { GST_MAKE_FOURCC('R','G','B',' '), 32, 32, XVID_CSP_RGB32  },
    { GST_MAKE_FOURCC('R','G','B',' '), 24, 24, XVID_CSP_RGB24  },
    { GST_MAKE_FOURCC('R','G','B',' '), 16, 16, XVID_CSP_RGB555 },
    { GST_MAKE_FOURCC('R','G','B',' '), 15, 16, XVID_CSP_RGB565 },
    { 0, 0, 0 }
  };
  gint i;

  for (i = 0; fmt_list[i].fourcc != 0; i++) {
    xviddec->csp = fmt_list[i].csp;

    /* try making a caps to set on the other side */
    if (fmt_list[i].fourcc == GST_MAKE_FOURCC('R','G','B',' ')) {
      guint32 r_mask = 0, b_mask = 0, g_mask = 0;
      gint endianness = 0;
      switch (fmt_list[i].depth) {
        case 15:
          endianness = G_BYTE_ORDER;
          r_mask = 0xf800; g_mask = 0x07c0; b_mask = 0x003e;
          break;
        case 16:
          endianness = G_BYTE_ORDER;
          r_mask = R_MASK_16_INT; g_mask = G_MASK_16_INT; b_mask = B_MASK_16_INT;
          break;
        case 24:
          endianness = G_BIG_ENDIAN;
          r_mask = R_MASK_24_INT; g_mask = G_MASK_24_INT; b_mask = B_MASK_24_INT;
          break;
        case 32:
          endianness = G_BIG_ENDIAN;
          r_mask = R_MASK_32_INT; g_mask = G_MASK_32_INT; b_mask = B_MASK_32_INT;
          break;
      }
      caps = gst_caps_new_simple (
                          "video/x-raw-rgb",
                            "width",      G_TYPE_INT, xviddec->width,
                            "height",     G_TYPE_INT, xviddec->height,
                            "depth",      G_TYPE_INT, fmt_list[i].depth,
                            "bpp",        G_TYPE_INT, fmt_list[i].bpp,
                            "endianness", G_TYPE_INT, endianness,
                            "red_mask",   G_TYPE_INT, r_mask,
                            "green_mask", G_TYPE_INT, g_mask,
                            "blue_mask",  G_TYPE_INT, b_mask,
			    "framerate",  G_TYPE_DOUBLE, xviddec->fps,
                            NULL);
    } else {
      caps = gst_caps_new_simple (
                          "video/x-raw-yuv",
                            "width",      G_TYPE_INT, xviddec->width,
                            "height",     G_TYPE_INT, xviddec->height,
                            "format",     GST_TYPE_FOURCC, fmt_list[i].fourcc,
			    "framerate",  G_TYPE_DOUBLE, xviddec->fps,
                            NULL);
    }

    if ((ret = gst_pad_try_set_caps(xviddec->srcpad, caps)) > 0) {
      xviddec->csp = fmt_list[i].csp;
      xviddec->bpp = fmt_list[i].bpp;
      if (gst_xviddec_setup(xviddec))
        return GST_PAD_LINK_OK;
    } else if (ret == GST_PAD_LINK_DELAYED) {
      return ret; /* don't try further (yet) */
    }
  }

  /* if we got here - it's not good */
  return GST_PAD_LINK_REFUSED;
}


static GstPadLinkReturn
gst_xviddec_link (GstPad  *pad,
                  const GstCaps *vscaps)
{
  GstXvidDec *xviddec;
  GstStructure *structure;

  xviddec = GST_XVIDDEC(gst_pad_get_parent (pad));

  /* if there's something old around, remove it */
  if (xviddec->handle) {
    gst_xviddec_unset(xviddec);
  }

  /* if we get here, we know the input is xvid. we
   * only need to bother with the output colorspace */
  structure = gst_caps_get_structure (vscaps, 0);
  gst_structure_get_int(structure, "width", &xviddec->width);
  gst_structure_get_int(structure, "height", &xviddec->height);
  gst_structure_get_double(structure, "framerate", &xviddec->fps);

  return gst_xviddec_negotiate(xviddec);
}
