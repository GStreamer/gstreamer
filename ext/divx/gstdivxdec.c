/* GStreamer divx decoder plugin
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
#include "gstdivxdec.h"
#include <gst/video/video.h>

/* elementfactory information */
GstElementDetails gst_divxdec_details = {
  "Divx decoder",
  "Codec/Video/Decoder",
  "Divx decoder based on divxdecore",
  "Ronald Bultje <rbultje@ronald.bitfreak.net>"
};

GST_PAD_TEMPLATE_FACTORY(sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW(
    "divxdec_sink",
    "video/x-divx",
      "divxversion", GST_PROPS_INT_RANGE(3, 5),
      "width",       GST_PROPS_INT_RANGE(0, G_MAXINT),
      "height",      GST_PROPS_INT_RANGE(0, G_MAXINT),
      "framerate",   GST_PROPS_FLOAT_RANGE(0, G_MAXFLOAT)
  )
)

GST_PAD_TEMPLATE_FACTORY(src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  gst_caps_new(
    "divxdec_src",
    "video/x-raw-yuv",
      GST_VIDEO_YUV_PAD_TEMPLATE_PROPS(
        GST_PROPS_LIST(
          GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','4','2','0')),
          GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','U','Y','2')),
          GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','V','1','2')),
          GST_PROPS_FOURCC(GST_MAKE_FOURCC('U','Y','V','Y'))
        )
      )
  ),
  gst_caps_new(
    "divxdec_src_rgb1",
    "video/x-raw-rgb",
      GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_24_32
  ),
  gst_caps_new(
    "divxdec_src_rgb2",
    "video/x-raw-rgb",
      GST_VIDEO_RGB_PAD_TEMPLATE_PROPS_15_16
  )
)


/* DivxDec signals and args */
enum {
  /* FILL ME */
  LAST_SIGNAL
};

enum {
  ARG_0
  /* FILL ME */
};


static void             gst_divxdec_base_init    (GstDivxDecClass *klass);
static void             gst_divxdec_class_init   (GstDivxDecClass *klass);
static void             gst_divxdec_init         (GstDivxDec      *divxdec);
static void             gst_divxdec_dispose      (GObject         *object);
static void             gst_divxdec_chain        (GstPad          *pad,
                                                  GstData         *data);
static GstPadLinkReturn gst_divxdec_connect      (GstPad          *pad,
                                                  GstCaps         *vscapslist);
static GstPadLinkReturn	gst_divxdec_negotiate	(GstDivxDec *divxdec);

static GstElementClass *parent_class = NULL;
/* static guint gst_divxdec_signals[LAST_SIGNAL] = { 0 }; */


static gchar *
gst_divxdec_error (int errorcode)
{
  gchar *error;

  switch (errorcode) {
    case DEC_OK:
      error = "No error";
      break;
    case DEC_MEMORY:
      error = "Invalid memory";
      break;
    case DEC_BAD_FORMAT:
      error = "Invalid format";
      break;
    case DEC_INVALID_ARGUMENT:
      error = "Invalid argument";
      break;
    case DEC_NOT_IMPLEMENTED:
      error = "Not implemented";
      break;
    default:
      error = "Unknown error";
      break;
  }

  return error;
}

GType
gst_divxdec_get_type(void)
{
  static GType divxdec_type = 0;

  if (!divxdec_type)
  {
    static const GTypeInfo divxdec_info = {
      sizeof(GstDivxDecClass),
      (GBaseInitFunc) gst_divxdec_base_init,
      NULL,
      (GClassInitFunc) gst_divxdec_class_init,
      NULL,
      NULL,
      sizeof(GstDivxDec),
      0,
      (GInstanceInitFunc) gst_divxdec_init,
    };
    divxdec_type = g_type_register_static(GST_TYPE_ELEMENT,
                                          "GstDivxDec",
                                          &divxdec_info, 0);
  }
  return divxdec_type;
}


static void
gst_divxdec_base_init (GstDivxDecClass *klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (sink_template));
  gst_element_class_add_pad_template (element_class,
		GST_PAD_TEMPLATE_GET (src_template));

  gst_element_class_set_details (element_class, &gst_divxdec_details);
}


static void
gst_divxdec_class_init (GstDivxDecClass *klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  gobject_class->dispose = gst_divxdec_dispose;
}


static void
gst_divxdec_init (GstDivxDec *divxdec)
{
  /* create the sink pad */
  divxdec->sinkpad = gst_pad_new_from_template(
                       GST_PAD_TEMPLATE_GET(sink_template),
                       "sink");
  gst_element_add_pad(GST_ELEMENT(divxdec), divxdec->sinkpad);

  gst_pad_set_chain_function(divxdec->sinkpad, gst_divxdec_chain);
  gst_pad_set_link_function(divxdec->sinkpad, gst_divxdec_connect);

  /* create the src pad */
  divxdec->srcpad = gst_pad_new_from_template(
                      GST_PAD_TEMPLATE_GET(src_template),
                      "src");
  gst_element_add_pad(GST_ELEMENT(divxdec), divxdec->srcpad);

  /* bitrate, etc. */
  divxdec->width = divxdec->height = divxdec->csp = divxdec->bitcnt = -1;

  /* set divx handle to NULL */
  divxdec->handle = NULL;
}


static void
gst_divxdec_unset (GstDivxDec *divxdec)
{
  if (divxdec->handle) {
    /* unref this instance */
    decore(divxdec->handle, DEC_OPT_RELEASE, NULL, NULL);
    divxdec->handle = NULL;
  }
}


static gboolean
gst_divxdec_setup (GstDivxDec *divxdec)
{
  void *handle;
  DEC_INIT xinit;
  DivXBitmapInfoHeader output;
  int ret;

  /* initialize the handle */
  memset(&xinit, 0, sizeof(DEC_INIT));
  xinit.smooth_playback = 0;
  xinit.codec_version = 500;
  if ((ret = decore(&handle, DEC_OPT_INIT, &xinit, NULL)) != 0) {
    gst_element_error(GST_ELEMENT(divxdec),
                      "Error initializing divx decoding library: %s (%d)",
                      gst_divxdec_error(ret), ret);
    return FALSE;
  }

  /* we've got a handle now */
  divxdec->handle = handle;

  /* initialise parameters, see divx documentation */
  memset(&output, 0, sizeof(DivXBitmapInfoHeader));
  output.biSize = sizeof(DivXBitmapInfoHeader);
  output.biWidth = divxdec->width;
  output.biHeight = divxdec->height;
  output.biBitCount = divxdec->bitcnt;
  output.biCompression = divxdec->csp;

  if ((ret = decore(divxdec->handle, DEC_OPT_SETOUT,
                    &output, NULL)) != 0) {
    gst_element_error(GST_ELEMENT(divxdec),
                      "Error setting output format: %s (%d)",
                      gst_divxdec_error(ret), ret);
    gst_divxdec_unset(divxdec);
    return FALSE;
  }

  return TRUE;
}


static void
gst_divxdec_dispose (GObject *object)
{
  GstDivxDec *divxdec = GST_DIVXDEC(object);

  gst_divxdec_unset(divxdec);
}


static void
gst_divxdec_chain (GstPad    *pad,
                   GstData *_data)
{
  GstBuffer *buf = GST_BUFFER (_data);
  GstDivxDec *divxdec;
  GstBuffer *outbuf;
  DEC_FRAME xframe;
  int ret;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  divxdec = GST_DIVXDEC(GST_OBJECT_PARENT(pad));

  if (!divxdec->handle) {
    if (gst_divxdec_negotiate(divxdec) <= 0) {
      gst_element_error(GST_ELEMENT(divxdec),
                        "No format set - aborting");
      gst_buffer_unref(buf);
      return;
    }
  }

  outbuf = gst_buffer_new_and_alloc(divxdec->width *
                                    divxdec->height *
                                    divxdec->bpp / 8);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);
  GST_BUFFER_SIZE(outbuf) = divxdec->width *
                            divxdec->height *
                            divxdec->bpp / 8;

  /* encode and so ... */
  xframe.bitstream = (void *) GST_BUFFER_DATA(buf);
  xframe.bmp = (void *) GST_BUFFER_DATA(outbuf);
  xframe.length = GST_BUFFER_SIZE(buf);
  xframe.stride = 0;
  xframe.render_flag = 1;

  if ((ret = decore(divxdec->handle, DEC_OPT_FRAME,
                    &xframe, NULL))) {
    gst_element_error(GST_ELEMENT(divxdec),
                      "Error decoding divx frame: %s (%d)",
                      gst_divxdec_error(ret), ret);
    gst_buffer_unref(buf);
    return;
  }

  gst_pad_push(divxdec->srcpad, GST_DATA (outbuf));
  gst_buffer_unref(buf);
}


static GstPadLinkReturn
gst_divxdec_negotiate (GstDivxDec *divxdec)
{
  GstPadLinkReturn ret;
  GstCaps *caps;
  struct {
    guint32 fourcc;
    gint    depth, bpp;
    guint32 csp;
    gint    bitcnt;
  } fmt_list[] = {
    { GST_MAKE_FOURCC('Y','U','Y','2'), 16, 16,
      GST_MAKE_FOURCC('Y','U','Y','2'), 0       },
    { GST_MAKE_FOURCC('U','Y','V','Y'), 16, 16,
      GST_MAKE_FOURCC('U','Y','V','Y'), 0       },
    { GST_MAKE_FOURCC('I','4','2','0'), 12, 12,
      GST_MAKE_FOURCC('I','4','2','0'), 0       },
    { GST_MAKE_FOURCC('Y','V','1','2'), 12, 12,
      GST_MAKE_FOURCC('Y','V','1','2'), 0       },
    { GST_MAKE_FOURCC('R','G','B',' '), 32, 32,
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      GST_MAKE_FOURCC('A','B','G','R'), 32      },
#else
      0,                                32      },
#endif
    { GST_MAKE_FOURCC('R','G','B',' '), 24, 24,
#if (G_BYTE_ORDER == G_BIG_ENDIAN)
      GST_MAKE_FOURCC('A','B','G','R'), 24      },
#else
      0,                                24      },
#endif
    { GST_MAKE_FOURCC('R','G','B',' '), 16, 16,
      3,                                16 },
    { GST_MAKE_FOURCC('R','G','B',' '), 15, 16,
      0,                                16 },
    { 0, 0, 0, 0, 0 }
  };
  gint i;

  for (i = 0; fmt_list[i].fourcc != 0; i++) {
    divxdec->csp = fmt_list[i].csp;

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
      caps = GST_CAPS_NEW("divxdec_src_pad_rgb",
                          "video/x-raw-rgb",
                            "width",      GST_PROPS_INT(divxdec->width),
                            "height",     GST_PROPS_INT(divxdec->height),
                            "framerate",  GST_PROPS_FLOAT(divxdec->fps),
                            "depth",      GST_PROPS_INT(fmt_list[i].depth),
                            "bpp",        GST_PROPS_INT(fmt_list[i].bpp),
                            "endianness", GST_PROPS_INT(endianness),
                            "red_mask",   GST_PROPS_INT(r_mask),
                            "green_mask", GST_PROPS_INT(g_mask),
                            "blue_mask",  GST_PROPS_INT(b_mask));
    } else {
      caps = GST_CAPS_NEW("divxdec_src_pad_yuv",
                          "video/x-raw-yuv",
                            "width",      GST_PROPS_INT(divxdec->width),
                            "height",     GST_PROPS_INT(divxdec->height),
                            "framerate",  GST_PROPS_FLOAT(divxdec->fps),
                            "format",     GST_PROPS_FOURCC(fmt_list[i].fourcc));
    }

    if ((ret = gst_pad_try_set_caps(divxdec->srcpad, caps)) > 0) {
      divxdec->csp    = fmt_list[i].csp;
      divxdec->bpp    = fmt_list[i].bpp;
      divxdec->bitcnt = fmt_list[i].bitcnt;
      if (gst_divxdec_setup(divxdec))
        return GST_PAD_LINK_OK;
    } else if (ret == GST_PAD_LINK_DELAYED) {
      return ret; /* trying more is useless */
    }
  }

  /* if we got here - it's not good */
  return GST_PAD_LINK_REFUSED;
}


static GstPadLinkReturn
gst_divxdec_connect (GstPad  *pad,
                     GstCaps *vscaps)
{
  GstDivxDec *divxdec;

  divxdec = GST_DIVXDEC(gst_pad_get_parent (pad));

  /* if there's something old around, remove it */
  if (divxdec->handle) {
    gst_divxdec_unset(divxdec);
  }

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED(vscaps))
    return GST_PAD_LINK_DELAYED;

  /* if we get here, we know the input is divx. we
   * only need to bother with the output colorspace */
  gst_caps_get_int(vscaps, "width", &divxdec->width);
  gst_caps_get_int(vscaps, "height", &divxdec->height);
  gst_caps_get_float(vscaps, "framerate", &divxdec->fps);

  return gst_divxdec_negotiate(divxdec);
}


static gboolean
plugin_init (GstPlugin *plugin)
{
  int lib_version;

  lib_version = decore(NULL, DEC_OPT_VERSION, 0, 0);
  if (lib_version != DECORE_VERSION) {
    g_warning("Version mismatch! This plugin was compiled for "
              "DivX version %d, while your library has version %d!",
              DECORE_VERSION, lib_version);
    return FALSE;
  }

  /* create an elementfactory for the v4lmjpegsrcparse element */
  return gst_element_register(plugin, "divxdec",
			      GST_RANK_PRIMARY, GST_TYPE_DIVXDEC);
}


GST_PLUGIN_DEFINE (
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "divxdec",
  "DivX decoder",
  plugin_init,
  "5.03",
  GST_LICENSE_UNKNOWN,
  "divx4linux",
  "http://www.divx.com/"
)
