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
  "Commercial",
  "Divx decoder based on divxdecore",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2003",
};

GST_PAD_TEMPLATE_FACTORY(sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW("divxdec_sink",
               "video/divx",
                 NULL)
)

GST_PAD_TEMPLATE_FACTORY(src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW("divxdec_src",
               "video/raw",
                 "format", GST_PROPS_LIST(
                             GST_PROPS_FOURCC(GST_MAKE_FOURCC('R','G','B',' ')),
                             GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','4','2','0')),
                             GST_PROPS_FOURCC(GST_MAKE_FOURCC('I','Y','U','V')),
                             GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','U','Y','2')),
                             GST_PROPS_FOURCC(GST_MAKE_FOURCC('Y','V','1','2')),
                             GST_PROPS_FOURCC(GST_MAKE_FOURCC('U','Y','V','Y'))
                           ),
                 "width",  GST_PROPS_INT_RANGE(0, G_MAXINT),
                 "height", GST_PROPS_INT_RANGE(0, G_MAXINT),
                 NULL)
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


static void             gst_divxdec_class_init   (GstDivxDecClass *klass);
static void             gst_divxdec_init         (GstDivxDec      *divxdec);
static void             gst_divxdec_dispose      (GObject         *object);
static void             gst_divxdec_chain        (GstPad          *pad,
                                                  GstBuffer       *buf);
static GstPadLinkReturn gst_divxdec_connect      (GstPad          *pad,
                                                  GstCaps         *vscapslist);

static GstElementClass *parent_class = NULL;
/* static guint gst_divxdec_signals[LAST_SIGNAL] = { 0 }; */


GType
gst_divxdec_get_type(void)
{
  static GType divxdec_type = 0;

  if (!divxdec_type)
  {
    static const GTypeInfo divxdec_info = {
      sizeof(GstDivxDecClass),
      NULL,
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
  divxdec->width = divxdec->height = divxdec->csp = -1;

  /* set divx handle to NULL */
  divxdec->handle = NULL;
}


static void
gst_divxdec_unset (GstDivxDec *divxdec)
{
  /* free allocated memory */
  g_free(divxdec->bufinfo.mp4_edged_ref_buffers);
  g_free(divxdec->bufinfo.mp4_edged_for_buffers);
  g_free(divxdec->bufinfo.mp4_edged_back_buffers);
  g_free(divxdec->bufinfo.mp4_display_buffers);
  g_free(divxdec->bufinfo.mp4_state);
  g_free(divxdec->bufinfo.mp4_tables);
  g_free(divxdec->bufinfo.mp4_stream);
  g_free(divxdec->bufinfo.mp4_reference);

  if (divxdec->handle) {
    /* unref this instance */
    decore((gulong) divxdec->handle, DEC_OPT_RELEASE,
           NULL, NULL);
    divxdec->handle = NULL;
  }
}


static gboolean
gst_divxdec_setup (GstDivxDec *divxdec)
{
  DEC_PARAM xdec;
  DEC_MEM_REQS xreq;
  int ret;

  /* initialise parameters, see divx documentation */
  memset(&xdec, 0, sizeof(DEC_PARAM));
  xdec.x_dim = divxdec->width;
  xdec.y_dim = divxdec->height;
  xdec.time_incr = 15; /* default - what is this? */
  xdec.output_format = divxdec->csp;

  if ((ret = decore((gulong) divxdec, DEC_OPT_MEMORY_REQS,
                    &xdec, &xreq)) != 0) {
    char *error;
    switch (ret) {
      case DEC_MEMORY:
        error = "Memory allocation error";
        break;
      case DEC_BAD_FORMAT:
        error = "Format";
        break;
      default:
        error = "Internal failure";
        break;
    }
    GST_DEBUG(GST_CAT_PLUGIN_INFO,
              "Setting parameters %dx%d@%d failed: %s",
              divxdec->width, divxdec->height, divxdec->csp, error);
    return FALSE;
  }

  /* allocate memory */
  xdec.buffers.mp4_edged_ref_buffers = g_malloc(xreq.mp4_edged_ref_buffers_size);
  memset(xdec.buffers.mp4_edged_ref_buffers, 0, xreq.mp4_edged_ref_buffers_size);

  xdec.buffers.mp4_edged_for_buffers = g_malloc(xreq.mp4_edged_for_buffers_size);
  memset(xdec.buffers.mp4_edged_for_buffers, 0, xreq.mp4_edged_for_buffers_size);

  xdec.buffers.mp4_edged_back_buffers = g_malloc(xreq.mp4_edged_back_buffers_size);
  memset(xdec.buffers.mp4_edged_back_buffers, 0, xreq.mp4_edged_back_buffers_size);

  xdec.buffers.mp4_display_buffers = g_malloc(xreq.mp4_display_buffers_size);
  memset(xdec.buffers.mp4_display_buffers, 0, xreq.mp4_display_buffers_size);

  xdec.buffers.mp4_state = g_malloc(xreq.mp4_state_size);
  memset(xdec.buffers.mp4_state, 0, xreq.mp4_state_size);

  xdec.buffers.mp4_tables = g_malloc(xreq.mp4_tables_size);
  memset(xdec.buffers.mp4_tables, 0, xreq.mp4_tables_size);

  xdec.buffers.mp4_stream = g_malloc(xreq.mp4_stream_size);
  memset(xdec.buffers.mp4_stream, 0, xreq.mp4_stream_size);

  xdec.buffers.mp4_reference = g_malloc(xreq.mp4_reference_size);
  memset(xdec.buffers.mp4_reference, 0, xreq.mp4_reference_size);

  divxdec->bufinfo = xdec.buffers;

  if ((ret = decore((gulong) divxdec, DEC_OPT_INIT,
                    &xdec, &xreq)) != 0) {
    gst_element_error(GST_ELEMENT(divxdec),
                      "Expected error when confirming current settings: %d",
                      ret);
    gst_divxdec_unset(divxdec);
    return FALSE;
  }

  /* don't tell me this sucks - this is how divx4linux works... */
  divxdec->handle = divxdec;

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
                   GstBuffer *buf)
{
  GstDivxDec *divxdec;
  GstBuffer *outbuf;
  DEC_FRAME xframe;
  int ret;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  divxdec = GST_DIVXDEC(GST_OBJECT_PARENT(pad));

  if (!divxdec->handle) {
    gst_element_error(GST_ELEMENT(divxdec),
                      "No format set - aborting");
    gst_buffer_unref(buf);
    return;
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
  xframe.stride = divxdec->width * divxdec->bpp / 8;
  xframe.render_flag = 1;

  if ((ret = decore((gulong) divxdec->handle, DEC_OPT_FRAME,
                    &xframe, NULL))) {
    gst_element_error(GST_ELEMENT(divxdec),
                      "Error decoding divx frame: %d\n", ret);
    gst_buffer_unref(buf);
    return;
  }

  gst_pad_push(divxdec->srcpad, outbuf);
  gst_buffer_unref(buf);
}


static GstPadLinkReturn
gst_divxdec_connect (GstPad  *pad,
                     GstCaps *vscaps)
{
  GstDivxDec *divxdec;
  GstCaps *caps;
  struct {
    guint32 fourcc;
    gint    depth, bpp;
    gint    csp;
  } fmt_list[] = {
    { GST_MAKE_FOURCC('Y','U','Y','V'), 16, 16, DEC_YUY2   },
    { GST_MAKE_FOURCC('U','Y','V','Y'), 16, 16, DEC_UYVY   },
    { GST_MAKE_FOURCC('I','4','2','0'), 12, 12, DEC_420    },
    { GST_MAKE_FOURCC('I','Y','U','V'), 12, 12, DEC_420    },
    { GST_MAKE_FOURCC('Y','V','1','2'), 12, 12, DEC_YV12   },
    { GST_MAKE_FOURCC('R','G','B',' '), 32, 32, DEC_RGB32  },
    { GST_MAKE_FOURCC('R','G','B',' '), 24, 24, DEC_RGB24  },
    { GST_MAKE_FOURCC('R','G','B',' '), 16, 16, DEC_RGB555 },
    { GST_MAKE_FOURCC('R','G','B',' '), 15, 16, DEC_RGB565 },
    { 0, 0, 0 }
  };
  gint i;

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

  for (i = 0; fmt_list[i].fourcc != 0; i++) {
    divxdec->csp = fmt_list[i].csp;

    /* try making a caps to set on the other side */
    if (fmt_list[i].fourcc == GST_MAKE_FOURCC('R','G','B',' ')) {
      guint32 r_mask = 0, b_mask = 0, g_mask = 0;
      switch (fmt_list[i].depth) {
        case 15:
          r_mask = 0xf800; g_mask = 0x07c0; b_mask = 0x003e;
          break;
        case 16:
          r_mask = 0xf800; g_mask = 0x07e0; b_mask = 0x001f;
          break;
        case 24:
          r_mask = 0xff0000; g_mask = 0x00ff00; b_mask = 0x0000ff;
          break;
        case 32:
          r_mask = 0xff000000; g_mask = 0x00ff0000; b_mask = 0x0000ff00;
          break;
      }
      caps = GST_CAPS_NEW("divxdec_src_pad_rgb",
                          "video/raw",
                            "width",      GST_PROPS_INT(divxdec->width),
                            "height",     GST_PROPS_INT(divxdec->height),
                            "format",     GST_PROPS_FOURCC(fmt_list[i].fourcc),
                            "depth",      GST_PROPS_INT(fmt_list[i].depth),
                            "bpp",        GST_PROPS_INT(fmt_list[i].bpp),
                            "endianness", GST_PROPS_INT(G_BYTE_ORDER),
                            "red_mask",   GST_PROPS_INT(r_mask),
                            "green_mask", GST_PROPS_INT(g_mask),
                            "blue_mask",  GST_PROPS_INT(b_mask),
                            NULL);
    } else {
      caps = GST_CAPS_NEW("divxdec_src_pad_yuv",
                          "video/raw",
                            "width",      GST_PROPS_INT(divxdec->width),
                            "height",     GST_PROPS_INT(divxdec->height),
                            "format",     GST_PROPS_FOURCC(fmt_list[i].fourcc),
                            NULL);
    }

    if (gst_pad_try_set_caps(divxdec->srcpad, caps) > 0) {
      divxdec->csp = fmt_list[i].csp;
      divxdec->bpp = fmt_list[i].bpp;
      if (gst_divxdec_setup(divxdec))
        return GST_PAD_LINK_OK;
    }
  }

  /* if we got here - it's not good */
  return GST_PAD_LINK_REFUSED;
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
  GstElementFactory *factory;

  /* create an elementfactory for the v4lmjpegsrcparse element */
  factory = gst_element_factory_new("divxdec", GST_TYPE_DIVXDEC,
                                    &gst_divxdec_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  /* add pad templates */
  gst_element_factory_add_pad_template(factory,
    GST_PAD_TEMPLATE_GET(sink_template));
  gst_element_factory_add_pad_template(factory,
    GST_PAD_TEMPLATE_GET(src_template));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}


GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "divxdec",
  plugin_init
};
