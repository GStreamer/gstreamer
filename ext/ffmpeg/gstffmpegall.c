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

#include "config.h"
#include <assert.h>
#include <string.h>
#ifdef HAVE_FFMPEG_UNINSTALLED
#include <avcodec.h>
#else
#include <ffmpeg/avcodec.h>
#endif

#include <gst/gst.h>

#include "gstffmpegallcodecmap.h"

typedef struct _GstFFMpegDecAll {
  GstElement element;

  GstPad *srcpad, *sinkpad;

  AVCodecContext context;
  AVFrame picture;
} GstFFMpegDecAll;

typedef struct _GstFFMpegDecAllClass {
  GstElementClass parent_class;
} GstFFMpegDecAllClass;

#define GST_TYPE_FFMPEGDECALL \
  (gst_ffmpegdecall_get_type())
#define GST_FFMPEGDECALL(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_FFMPEGDECALL,GstFFMpegDecAll))
#define GST_FFMPEGDECALL_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_FFMPEGDECALL,GstFFMpegDecClassAll))
#define GST_IS_FFMPEGDECALL(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_FFMPEGDECALL))
#define GST_IS_FFMPEGDECALL_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_FFMPEGDECALL))

GST_PAD_TEMPLATE_FACTORY(src_templ,
  "src",
  GST_PAD_SRC,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "gstffmpeg_src_videoyuv",
    "video/raw",
      "format",       GST_PROPS_LIST (
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('Y','U','Y','2')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('I','4','2','0')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('Y','4','1','P'))
                      ),
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "gstffmpeg_src_videorgb",
    "video/raw",
      "format",       GST_PROPS_FOURCC (GST_MAKE_FOURCC('R','G','B',' ')),
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096),
      "bpp",          GST_PROPS_INT_RANGE (16, 32),
      "depth",        GST_PROPS_INT_RANGE (15, 32),
      "endianness",   GST_PROPS_INT (G_BYTE_ORDER)
  ) /*,
  GST_CAPS_NEW (
    "avidemux_src_audio",
    "audio/raw",
      "format",       GST_PROPS_STRING ("int"),
      "law",          GST_PROPS_INT (0),
      "endianness",   GST_PROPS_INT (G_BYTE_ORDER),
      "signed",       GST_PROPS_LIST (
                        GST_PROPS_BOOLEAN (TRUE),
                        GST_PROPS_BOOLEAN (FALSE)
                      ),
      "width",        GST_PROPS_LIST (
                        GST_PROPS_INT (8),
                        GST_PROPS_INT (16)
                      ),
      "depth",        GST_PROPS_LIST (
                        GST_PROPS_INT (8),
                        GST_PROPS_INT (16)
                      ),
      "rate",         GST_PROPS_INT_RANGE (11025, 96000),
      "channels",     GST_PROPS_INT_RANGE (1, 2)
  ) */
)

GST_PAD_TEMPLATE_FACTORY(sink_templ,
  "sink",
  GST_PAD_SINK,
  GST_PAD_ALWAYS,
  GST_CAPS_NEW (
    "gstffmpeg_sink_avivideo",
    "video/avi",
      "format",       GST_PROPS_STRING("strf_vids"),
      "compression",  GST_PROPS_LIST (
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('M','J','P','G')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('J','P','E','G')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('V','I','X','L')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('P','I','X','L')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('H','F','Y','U')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('V','I','X','L')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('D','V','S','D')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('d','v','s','d')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('M','P','E','G')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('M','P','G','I')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('H','2','6','3')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('i','2','6','3')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('L','2','6','3')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('M','2','6','3')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('V','D','O','W')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('V','I','V','O')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('x','2','6','3')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('D','I','V','X')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('d','i','v','x')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('D','I','V','3')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('D','I','V','4')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('D','I','V','5')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('D','X','5','o')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('M','P','G','4')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('M','P','4','2')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('M','P','4','3')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('W','M','V','1')),
                        GST_PROPS_FOURCC (GST_MAKE_FOURCC('W','M','V','2'))
                      ),
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "gstffmpeg_sink_dv",
    "video/dv",
      "format",       GST_PROPS_LIST (
                        GST_PROPS_STRING ("NTSC"),
                        GST_PROPS_STRING ("PAL")
                      ),
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "gstffmpeg_sink_h263",
    "video/H263",
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "gstffmpeg_sink_mpeg",
    "video/mpeg",
      "systemstream", GST_PROPS_BOOLEAN(FALSE),
      "mpegversion",  GST_PROPS_INT(1) /*,
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096)*/
  ),
  GST_CAPS_NEW (
    "gstffmpeg_sink_jpeg",
    "video/jpeg",
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096)
  ),
  GST_CAPS_NEW (
    "gstffmpeg_sink_wmv",
    "video/wmv",
      "width",        GST_PROPS_INT_RANGE (16, 4096),
      "height",       GST_PROPS_INT_RANGE (16, 4096)
  )
)

/* A number of functon prototypes are given so we can refer to them later. */
static void	gst_ffmpegdecall_class_init	(GstFFMpegDecAllClass *klass);
static void	gst_ffmpegdecall_init		(GstFFMpegDecAll *ffmpegdec);
static void	gst_ffmpegdecall_chain		(GstPad *pad, GstBuffer *buffer);
static GstPadConnectReturn gst_ffmpegdecall_connect (GstPad *pad, GstCaps *caps);

static GstElementClass *parent_class = NULL;

/* elementfactory information */
GstElementDetails gst_ffmpegdecall_details = {
  "FFMPEG codec wrapper",
  "Codec/Audio-Video/FFMpeg",
  "LGPL",
  "FFMpeg-based video/audio decoder",
  VERSION,
  "Ronald Bultje <rbultje@ronald.bitfreak.net>",
  "(C) 2002",
};

GType
gst_ffmpegdecall_get_type(void)
{
  static GType ffmpegdecall_type = 0;

  if (!ffmpegdecall_type)
  {
    static const GTypeInfo ffmpegdecall_info = {
      sizeof(GstFFMpegDecAllClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_ffmpegdecall_class_init,
      NULL,
      NULL,
      sizeof(GstFFMpegDecAll),
      0,
      (GInstanceInitFunc)gst_ffmpegdecall_init,
    };
    ffmpegdecall_type = g_type_register_static(GST_TYPE_ELEMENT,
                                               "GstFFMpegDecAll",
                                               &ffmpegdecall_info, 0);
  }
  return ffmpegdecall_type;
}

static void
gst_ffmpegdecall_class_init (GstFFMpegDecAllClass *klass)
{
  parent_class = g_type_class_ref(GST_TYPE_ELEMENT);
}

static void
gst_ffmpegdecall_init(GstFFMpegDecAll *ffmpegdec)
{
  ffmpegdec->sinkpad = gst_pad_new_from_template(
                             GST_PAD_TEMPLATE_GET(sink_templ), "sink");
  gst_pad_set_connect_function(ffmpegdec->sinkpad,
                               gst_ffmpegdecall_connect);
  gst_pad_set_chain_function(ffmpegdec->sinkpad,
                             gst_ffmpegdecall_chain);

  ffmpegdec->srcpad = gst_pad_new_from_template(
                             GST_PAD_TEMPLATE_GET(src_templ), "src");

  gst_element_add_pad(GST_ELEMENT(ffmpegdec),
                      ffmpegdec->sinkpad);
  gst_element_add_pad(GST_ELEMENT(ffmpegdec),
                      ffmpegdec->srcpad);
}

static GstPadConnectReturn
gst_ffmpegdecall_connect (GstPad *pad, GstCaps *caps)
{
  GstFFMpegDecAll *ffmpegdec = GST_FFMPEGDECALL(gst_pad_get_parent(pad));
  enum CodecID id;
  AVCodec *plugin;
  GstCaps *newcaps;

  if (!GST_CAPS_IS_FIXED(caps))
    return GST_PAD_CONNECT_DELAYED;

  avcodec_get_context_defaults(&ffmpegdec->context);

  if ((id = gst_ffmpeg_caps_to_codecid(caps, &ffmpegdec->context)) == CODEC_ID_NONE) {
    GST_DEBUG(GST_CAT_PLUGIN_INFO,
              "Failed to find corresponding codecID");
    return GST_PAD_CONNECT_REFUSED;
  }

  if (ffmpegdec->context.codec_type == CODEC_TYPE_VIDEO)
    ffmpegdec->context.pix_fmt = PIX_FMT_YUV420P /*ANY*/;

  if ((plugin = avcodec_find_decoder(id)) == NULL) {
    GST_DEBUG(GST_CAT_PLUGIN_INFO,
              "Failed to find an avdecoder for id=%d", id);
    return GST_PAD_CONNECT_REFUSED;
  }

  /* we dont send complete frames */
  if (plugin->capabilities & CODEC_CAP_TRUNCATED)
    ffmpegdec->context.flags |= CODEC_FLAG_TRUNCATED;

  if (avcodec_open(&ffmpegdec->context, plugin)) {
    GST_DEBUG(GST_CAT_PLUGIN_INFO,
              "Failed to open FFMPEG codec for id=%d", id);
    return GST_PAD_CONNECT_REFUSED;
  }

  if (ffmpegdec->context.width > 0 && ffmpegdec->context.height > 0) {
    /* set caps on src pad based on context.pix_fmt && width/height */
    newcaps = gst_ffmpeg_codecid_to_caps(CODEC_ID_RAWVIDEO,
                                         &ffmpegdec->context);
    if (!newcaps) {
      GST_DEBUG(GST_CAT_PLUGIN_INFO,
                "Failed to create caps for other end (pix_fmt=%d)",
                ffmpegdec->context.pix_fmt);
      return GST_PAD_CONNECT_REFUSED;
    }

    return gst_pad_try_set_caps(ffmpegdec->srcpad, newcaps);
  }

  return GST_PAD_CONNECT_OK;
}

static void
gst_ffmpegdecall_chain (GstPad *pad, GstBuffer *inbuf)
{
  GstBuffer *outbuf;
  GstFFMpegDecAll *ffmpegdec = GST_FFMPEGDECALL(gst_pad_get_parent (pad));
  guchar *data;
  gint size, frame_size, len;
  gint have_picture;

  data = GST_BUFFER_DATA (inbuf);
  size = GST_BUFFER_SIZE (inbuf);

  do {
    ffmpegdec->context.frame_number++;

    len = avcodec_decode_video (&ffmpegdec->context, &ffmpegdec->picture,
		  &have_picture, data, size);

    if (len < 0) {
      gst_element_error(GST_ELEMENT(ffmpegdec),
                        "ffmpegdec: failed to decode frame");
      break;
    }

    if (have_picture) {
      guchar *picdata, *picdata2, *outdata, *outdata2;
      gint xsize, i, width, height;

      height = ffmpegdec->context.height;
      width = ffmpegdec->context.width;

      if (!GST_PAD_CAPS(ffmpegdec->srcpad)) {
        GstCaps *newcaps = gst_ffmpeg_codecid_to_caps(CODEC_ID_RAWVIDEO,
                                                      &ffmpegdec->context);

	if (!newcaps) {
          gst_element_error(GST_ELEMENT(ffmpegdec),
                            "Failed to create caps for ffmpeg (pix_fmt=%d)",
                            ffmpegdec->context.pix_fmt);
          break;
        }

        if (gst_pad_try_set_caps(ffmpegdec->srcpad, newcaps) <= 0) {
          gst_element_error(GST_ELEMENT(ffmpegdec),
                            "Failed to set caps on the other end");
          break;
        }
      }

      frame_size = width * height;

      outbuf = gst_buffer_new ();
      GST_BUFFER_SIZE (outbuf) = (frame_size*3)>>1;
      outdata = GST_BUFFER_DATA (outbuf) = g_malloc (GST_BUFFER_SIZE (outbuf));
      GST_BUFFER_TIMESTAMP (outbuf) = GST_BUFFER_TIMESTAMP (inbuf);
 
      picdata = ffmpegdec->picture.data[0];
      xsize = ffmpegdec->picture.linesize[0];
      for (i=height; i; i--) {
        memcpy (outdata, picdata, width);
        outdata += width;
        picdata += xsize;
      }

      frame_size >>= 2;
      width >>= 1;
      height >>= 1;
      outdata2 = outdata + frame_size;

      picdata = ffmpegdec->picture.data[1];
      picdata2 = ffmpegdec->picture.data[2];
      xsize = ffmpegdec->picture.linesize[1];
      for (i=height; i; i--) {
        memcpy (outdata, picdata, width);
        memcpy (outdata2, picdata2, width);
        outdata += width; outdata2 += width;
        picdata += xsize; picdata2 += xsize;
      }

      gst_pad_push (ffmpegdec->srcpad, outbuf);
    } 

    size -= len;
    data += len;
  } while (size > 0);

  gst_buffer_unref (inbuf);
}

static gboolean
plugin_init (GModule *module, GstPlugin *plugin)
{
  GstElementFactory *factory;

  avcodec_init ();
  avcodec_register_all ();

  /* create an elementfactory for the element */
  factory = gst_element_factory_new("ffmpegdecall",
                                    GST_TYPE_FFMPEGDECALL,
                                    &gst_ffmpegdecall_details);
  g_return_val_if_fail(factory != NULL, FALSE);

  gst_element_factory_add_pad_template(factory,
                                    GST_PAD_TEMPLATE_GET(src_templ));
  gst_element_factory_add_pad_template(factory,
                                    GST_PAD_TEMPLATE_GET(sink_templ));

  gst_plugin_add_feature(plugin, GST_PLUGIN_FEATURE(factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "ffmpegdecall",
  plugin_init
};
