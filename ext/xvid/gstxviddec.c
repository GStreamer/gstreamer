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

GST_PAD_TEMPLATE_FACTORY(sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW("xviddec_sink",
               "video/x-xvid",
                 "width",     GST_PROPS_INT_RANGE (0, G_MAXINT),
		 "height",    GST_PROPS_INT_RANGE (0, G_MAXINT),
		 "framerate", GST_PROPS_FLOAT_RANGE (0, G_MAXFLOAT))
)

GST_PAD_TEMPLATE_FACTORY(src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  gst_caps_new(
    "xviddec_sink",
    "video/x-raw-yuv",
    GST_VIDEO_YUV_PAD_TEMPLATE_PROPS (
      GST_PROPS_LIST(
        GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','4','2','0')),
        GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','U','Y','2')),
        GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','V','1','2')),
        GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','V','Y','U')),
        GST_PROPS_FOURCC(GST_MAKE_FOURCC('U','Y','V','Y'))
      )
    )
  ),
  gst_caps_new(
    "xviddec_sink_rgb24_32",
    "video/x-raw-rgb",
    GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24_32
  ),
  gst_caps_new(
    "xviddec_sink_rgb15_16",
    "video/x-raw-rgb",
    GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_15_16
  )
)


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
static GstPadLinkReturn gst_xviddec_connect      (GstPad          *pad,
                                                  GstCaps         *vscapslist);
static GstPadLinkReturn	gst_xviddec_negotiate	(GstXvidDec *xviddec);

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

  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (sink_template));
  gst_element_class_add_pad_template (element_class, GST_PAD_TEMPLATE_GET (src_template));

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
                       GST_PAD_TEMPLATE_GET(sink_template),
                       "sink");
  gst_element_add_pad(GST_ELEMENT(xviddec), xviddec->sinkpad);

  gst_pad_set_chain_function(xviddec->sinkpad, gst_xviddec_chain);
  gst_pad_set_link_function(xviddec->sinkpad, gst_xviddec_connect);

  /* create the src pad */
  xviddec->srcpad = gst_pad_new_from_template(
                      GST_PAD_TEMPLATE_GET(src_template),
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
          r_mask = 0xf800; g_mask = 0x07e0; b_mask = 0x001f;
          break;
        case 24:
          endianness = G_BIG_ENDIAN;
          r_mask = R_MASK_24; g_mask = G_MASK_24; b_mask = B_MASK_24;
          break;
        case 32:
          endianness = G_BIG_ENDIAN;
          r_mask = R_MASK_32; g_mask = G_MASK_32; b_mask = B_MASK_32;
          break;
      }
      caps = GST_CAPS_NEW("xviddec_src_pad_rgb",
                          "video/x-raw-rgb",
                            "width",      GST_PROPS_INT(xviddec->width),
                            "height",     GST_PROPS_INT(xviddec->height),
                            "depth",      GST_PROPS_INT(fmt_list[i].depth),
                            "bpp",        GST_PROPS_INT(fmt_list[i].bpp),
                            "endianness", GST_PROPS_INT(endianness),
                            "red_mask",   GST_PROPS_INT(r_mask),
                            "green_mask", GST_PROPS_INT(g_mask),
                            "blue_mask",  GST_PROPS_INT(b_mask),
			    "framerate",  GST_PROPS_FLOAT(xviddec->fps),
                            NULL);
    } else {
      caps = GST_CAPS_NEW("xviddec_src_pad_yuv",
                          "video/x-raw-yuv",
                            "width",      GST_PROPS_INT(xviddec->width),
                            "height",     GST_PROPS_INT(xviddec->height),
                            "format",     GST_PROPS_FOURCC(fmt_list[i].fourcc),
			    "framerate",  GST_PROPS_FLOAT(xviddec->fps),
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
gst_xviddec_connect (GstPad  *pad,
                     GstCaps *vscaps)
{
  GstXvidDec *xviddec;

  xviddec = GST_XVIDDEC(gst_pad_get_parent (pad));

  /* if there's something old around, remove it */
  if (xviddec->handle) {
    gst_xviddec_unset(xviddec);
  }

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED(vscaps))
    return GST_PAD_LINK_DELAYED;

  /* if we get here, we know the input is xvid. we
   * only need to bother with the output colorspace */
  gst_caps_get_int(vscaps, "width", &xviddec->width);
  gst_caps_get_int(vscaps, "height", &xviddec->height);
  gst_caps_get_float(vscaps, "framerate", &xviddec->fps);

  return gst_xviddec_negotiate(xviddec);
}
