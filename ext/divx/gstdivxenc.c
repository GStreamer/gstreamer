/* GStreamer divx encoder plugin
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
#include "gstdivxenc.h"
#include <gst/video/video.h>
#include <encore2.h>

/* elementfactory information */
GstElementDetails gst_divxenc_details = {
  "Divx encoder",
  "Codec/Video/Encoder",
  "Commercial",
  "Divx encoder based on divxencore",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2003",
};

GST_PAD_TEMPLATE_FACTORY(sink_template,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW("divxenc_sink",
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

GST_PAD_TEMPLATE_FACTORY(src_template,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW("divxenc_src",
               "video/divx",
                 NULL)
)


/* DivxEnc signals and args */
enum {
  FRAME_ENCODED,
  LAST_SIGNAL
};

enum {
  ARG_0,
  ARG_BITRATE,
  ARG_MAXKEYINTERVAL,
  ARG_BUFSIZE
};


static void             gst_divxenc_class_init   (GstDivxEncClass *klass);
static void             gst_divxenc_init         (GstDivxEnc      *divxenc);
static void             gst_divxenc_dispose      (GObject         *object);
static void             gst_divxenc_chain        (GstPad          *pad,
                                                  GstBuffer       *buf);
static GstPadLinkReturn gst_divxenc_connect      (GstPad          *pad,
                                                  GstCaps         *vscapslist);

/* properties */
static void             gst_divxenc_set_property (GObject         *object,
                                                  guint            prop_id,
                                                  const GValue    *value,
                                                  GParamSpec      *pspec);
static void             gst_divxenc_get_property (GObject         *object,
                                                  guint            prop_id,
                                                  GValue          *value,
                                                  GParamSpec      *pspec);

static GstElementClass *parent_class = NULL;
static guint gst_divxenc_signals[LAST_SIGNAL] = { 0 };


GType
gst_divxenc_get_type(void)
{
  static GType divxenc_type = 0;

  if (!divxenc_type)
  {
    static const GTypeInfo divxenc_info = {
      sizeof(GstDivxEncClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_divxenc_class_init,
      NULL,
      NULL,
      sizeof(GstDivxEnc),
      0,
      (GInstanceInitFunc) gst_divxenc_init,
    };
    divxenc_type = g_type_register_static(GST_TYPE_ELEMENT,
                                          "GstDivxEnc",
                                          &divxenc_info, 0);
  }
  return divxenc_type;
}


static void
gst_divxenc_class_init (GstDivxEncClass *klass)
{
  GstElementClass *gstelement_class;
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BITRATE,
    g_param_spec_ulong("bitrate","Bitrate",
                       "Target video bitrate",
                       0,G_MAXULONG,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_MAXKEYINTERVAL,
    g_param_spec_int("max_key_interval","Max. Key Interval",
                     "Maximum number of frames between two keyframes",
                     0,G_MAXINT,0,G_PARAM_READWRITE));

  g_object_class_install_property(G_OBJECT_CLASS(klass), ARG_BUFSIZE,
    g_param_spec_ulong("buffer_size", "Buffer Size",
                       "Size of the video buffers",
                       0,G_MAXULONG,0,G_PARAM_READWRITE));

  gobject_class->set_property = gst_divxenc_set_property;
  gobject_class->get_property = gst_divxenc_get_property;

  gobject_class->dispose = gst_divxenc_dispose;

  gst_divxenc_signals[FRAME_ENCODED] =
    g_signal_new ("frame_encoded", G_TYPE_FROM_CLASS(klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (GstDivxEncClass, frame_encoded),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID, G_TYPE_NONE, 0);
}


static void
gst_divxenc_init (GstDivxEnc *divxenc)
{
  /* create the sink pad */
  divxenc->sinkpad = gst_pad_new_from_template(
                       GST_PAD_TEMPLATE_GET(sink_template),
                       "sink");
  gst_element_add_pad(GST_ELEMENT(divxenc), divxenc->sinkpad);

  gst_pad_set_chain_function(divxenc->sinkpad, gst_divxenc_chain);
  gst_pad_set_link_function(divxenc->sinkpad, gst_divxenc_connect);

  /* create the src pad */
  divxenc->srcpad = gst_pad_new_from_template(
                      GST_PAD_TEMPLATE_GET(src_template),
                      "src");
  gst_element_add_pad(GST_ELEMENT(divxenc), divxenc->srcpad);

  /* bitrate, etc. */
  divxenc->width = divxenc->height = divxenc->csp = -1;
  divxenc->bitrate = 512 * 1024;
  divxenc->max_key_interval = -1; /* default - 2*fps */
  divxenc->buffer_size = 512 * 1024;

  /* set divx handle to NULL */
  divxenc->handle = NULL;
}


static gboolean
gst_divxenc_setup (GstDivxEnc *divxenc)
{
  ENC_PARAM xenc;
  gdouble fps;
  int ret;

  fps = gst_video_frame_rate(GST_PAD_PEER(divxenc->sinkpad));

  /* set it up */
  memset(&xenc, 0, sizeof(ENC_PARAM));
  xenc.x_dim = divxenc->width;
  xenc.y_dim = divxenc->height;
  xenc.framerate = fps;

  xenc.rc_period = 2000;
  xenc.rc_reaction_period = 10;
  xenc.rc_reaction_ratio = 20;

  xenc.max_quantizer = 31;
  xenc.min_quantizer = 1;

  xenc.quality = 3;
  xenc.bitrate = divxenc->bitrate;

  xenc.deinterlace = 0;

  xenc.max_key_interval = (divxenc->max_key_interval == -1) ?
                            (2 * xenc.framerate) :
                            divxenc->max_key_interval;
  xenc.handle = NULL;

  if ((ret = encore(NULL, ENC_OPT_INIT, &xenc, NULL))) {
    gst_element_error(GST_ELEMENT(divxenc),
                      "Error setting up divx encoder: %d\n",
                      ret);
    return FALSE;
  }

  divxenc->handle = xenc.handle;

  return TRUE;
}


static void
gst_divxenc_unset (GstDivxEnc *divxenc)
{
  encore(divxenc->handle, ENC_OPT_RELEASE, NULL, NULL);
  divxenc->handle = NULL;
}


static void
gst_divxenc_dispose (GObject *object)
{
  GstDivxEnc *divxenc = GST_DIVXENC(object);

  gst_divxenc_unset(divxenc);
}


static void
gst_divxenc_chain (GstPad    *pad,
                   GstBuffer *buf)
{
  GstDivxEnc *divxenc;
  GstBuffer *outbuf;
  ENC_FRAME xframe;
  ENC_RESULT xres;
  int ret;

  g_return_if_fail(pad != NULL);
  g_return_if_fail(GST_IS_PAD(pad));
  g_return_if_fail(buf != NULL);

  divxenc = GST_DIVXENC(GST_OBJECT_PARENT(pad));

  if (!divxenc->handle) {
    if (!gst_divxenc_setup(divxenc)) {
      gst_buffer_unref(buf);
      return;
    }
  }

  outbuf = gst_buffer_new_and_alloc(divxenc->buffer_size);
  GST_BUFFER_TIMESTAMP(outbuf) = GST_BUFFER_TIMESTAMP(buf);

  /* encode and so ... */
  xframe.image = GST_BUFFER_DATA(buf);
  xframe.bitstream = (void *) GST_BUFFER_DATA(outbuf);
  xframe.length = GST_BUFFER_MAXSIZE(outbuf);
  xframe.mvs = NULL;
  xframe.colorspace = divxenc->csp;

  if ((ret = encore(divxenc->handle, ENC_OPT_ENCODE,
                    &xframe, &xres))) {
    gst_element_error(GST_ELEMENT(divxenc),
                      "Error encoding divx frame: %d\n", ret);
    gst_buffer_unref(buf);
    return;
  }

  GST_BUFFER_SIZE(outbuf) = xframe.length;
  if (xres.is_key_frame)
    GST_BUFFER_FLAG_SET(outbuf, GST_BUFFER_KEY_UNIT);

  /* go out, multiply! */
  gst_pad_push(divxenc->srcpad, outbuf);

  /* proclaim destiny */
  g_signal_emit(G_OBJECT(divxenc),gst_divxenc_signals[FRAME_ENCODED], 0);

  /* until the final judgement */
  gst_buffer_unref(buf);
}


static GstPadLinkReturn
gst_divxenc_connect (GstPad  *pad,
                     GstCaps *vscaps)
{
  GstDivxEnc *divxenc;
  GstCaps *caps;

  divxenc = GST_DIVXENC(gst_pad_get_parent (pad));

  /* if there's something old around, remove it */
  if (divxenc->handle) {
    gst_divxenc_unset(divxenc);
  }

  /* we are not going to act on variable caps */
  if (!GST_CAPS_IS_FIXED(vscaps))
    return GST_PAD_LINK_DELAYED;

  for (caps = vscaps; caps != NULL; caps = caps->next) {
    int w,h,d;
    guint32 fourcc;
    gint divx_cs;
    gst_caps_get_int(caps, "width", &w);
    gst_caps_get_int(caps, "height", &h);
    gst_caps_get_fourcc_int(caps, "format", &fourcc);

    switch (fourcc) {
      case GST_MAKE_FOURCC('I','4','2','0'):
      case GST_MAKE_FOURCC('I','Y','U','V'):
        divx_cs = ENC_CSP_I420;
        break;
      case GST_MAKE_FOURCC('Y','U','Y','2'):
        divx_cs = ENC_CSP_YUY2;
        break;
      case GST_MAKE_FOURCC('Y','V','1','2'):
        divx_cs = ENC_CSP_YV12;
        break;
      case GST_MAKE_FOURCC('U','Y','V','Y'):
        divx_cs = ENC_CSP_UYVY;
        break;
      case GST_MAKE_FOURCC('R','G','B',' '):
        gst_caps_get_int(caps, "depth", &d);
        switch (d) {
          case 24:
            divx_cs = ENC_CSP_RGB24;
            break;
          case 32:
            divx_cs = ENC_CSP_RGB32;
            break;
          default:
            goto trynext;
        }
        break;
      default:
        goto trynext;
    }

    /* grmbl, we only know the peer pad *after*
     * linking, so we accept here, get the fps on
     * the first cycle and set it all up then */
    divxenc->csp = divx_cs;
    divxenc->width = w;
    divxenc->height = h;
    return gst_pad_try_set_caps(divxenc->srcpad,
                                GST_CAPS_NEW("divxenc_src_caps",
                                             "video/divx",
                                               "width",  GST_PROPS_INT(w),
                                               "height", GST_PROPS_INT(h)));

trynext:
    continue;
  }

  /* if we got here - it's not good */
  return GST_PAD_LINK_REFUSED;
}


static void
gst_divxenc_set_property (GObject      *object,
                          guint         prop_id,
                          const GValue *value,
                          GParamSpec   *pspec)
{
  GstDivxEnc *divxenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DIVXENC (object));
  divxenc = GST_DIVXENC(object);

  switch (prop_id) {
    case ARG_BITRATE:
      divxenc->bitrate = g_value_get_ulong(value);
      break;
    case ARG_BUFSIZE:
      divxenc->buffer_size = g_value_get_ulong(value);
      break;
    case ARG_MAXKEYINTERVAL:
      divxenc->max_key_interval = g_value_get_int(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_divxenc_get_property (GObject    *object,
                          guint       prop_id,
                          GValue     *value,
                          GParamSpec *pspec)
{
  GstDivxEnc *divxenc;

  /* it's not null if we got it, but it might not be ours */
  g_return_if_fail (GST_IS_DIVXENC (object));
  divxenc = GST_DIVXENC(object);

  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_ulong(value, divxenc->bitrate);
      break;
    case ARG_BUFSIZE:
      g_value_set_ulong(value, divxenc->buffer_size);
      break;
    case ARG_MAXKEYINTERVAL:
      g_value_set_int(value, divxenc->max_key_interval);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
plugin_init (GModule   *module,
             GstPlugin *plugin)
{
  GstElementFactory *factory;

  if (!gst_library_load("gstvideo"))
    return FALSE;

  /* create an elementfactory for the element */
  factory = gst_element_factory_new("divxenc", GST_TYPE_DIVXENC,
                                    &gst_divxenc_details);
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
  "divxenc",
  plugin_init
};
