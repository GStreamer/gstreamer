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


/*#define GST_DEBUG_ENABLED */
#include <string.h>

#include "gstavidecoder.h"



/* elementfactory information */
static GstElementDetails gst_avi_decoder_details = {
  ".avi decoder",
  "Decoder/Video",
  "Decodes a .avi file into audio and video",
  VERSION,
  "Erik Walthinsen <omega@cse.ogi.edu>\n" "Wim Taymans <wim.taymans@tvd.be>",
  "(C) 1999",
};

static GstCaps *avi_typefind (GstBuffer * buf, gpointer private);

/* typefactory for 'avi' */
static GstTypeDefinition avidefinition = {
  "avidecoder_video/avi",
  "video/avi",
  ".avi",
  avi_typefind,
};

/* AviDecoder signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  ARG_0,
  ARG_BITRATE,
  ARG_MEDIA_TIME,
  ARG_CURRENT_TIME,
  /* FILL ME */
};

GST_PADTEMPLATE_FACTORY (sink_templ,
    "sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_CAPS_NEW ("avidecoder_sink",
        "video/avi", "RIFF", GST_PROPS_STRING ("AVI")
    )
    )

    GST_PADTEMPLATE_FACTORY (src_video_templ,
    "video_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_CAPS_NEW ("wincodec_src",
        "video/raw",
        "format", GST_PROPS_LIST (GST_PROPS_FOURCC (GST_MAKE_FOURCC ('Y', 'U', 'Y',
                    '2')), GST_PROPS_FOURCC (GST_MAKE_FOURCC ('I', '4', '2', '0')),
            GST_PROPS_FOURCC (GST_MAKE_FOURCC ('R', 'G', 'B', ' '))
        ), "width", GST_PROPS_INT_RANGE (16, 4096), "height",
        GST_PROPS_INT_RANGE (16, 4096)
    )
    )

    GST_PADTEMPLATE_FACTORY (src_audio_templ,
    "audio_src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_CAPS_NEW ("src_audio",
        "audio/raw",
        "format", GST_PROPS_STRING ("int"),
        "law", GST_PROPS_INT (0),
        "endianness", GST_PROPS_INT (G_BYTE_ORDER),
        "signed", GST_PROPS_LIST (GST_PROPS_BOOLEAN (TRUE),
            GST_PROPS_BOOLEAN (FALSE)
        ), "width", GST_PROPS_LIST (GST_PROPS_INT (8), GST_PROPS_INT (16)
        ), "depth", GST_PROPS_LIST (GST_PROPS_INT (8), GST_PROPS_INT (16)
        ),
        "rate", GST_PROPS_INT_RANGE (11025, 48000),
        "channels", GST_PROPS_INT_RANGE (1, 2)
    )
    )

     static void gst_avi_decoder_class_init (GstAviDecoderClass * klass);
     static void gst_avi_decoder_init (GstAviDecoder * avi_decoder);

     static void gst_avi_decoder_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);



     static GstElementClass *parent_class = NULL;

/*static guint gst_avi_decoder_signals[LAST_SIGNAL] = { 0 }; */

GType
gst_avi_decoder_get_type (void)
{
  static GType avi_decoder_type = 0;

  if (!avi_decoder_type) {
    static const GTypeInfo avi_decoder_info = {
      sizeof (GstAviDecoderClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_avi_decoder_class_init,
      NULL,
      NULL,
      sizeof (GstAviDecoder),
      0,
      (GInstanceInitFunc) gst_avi_decoder_init,
    };

    avi_decoder_type =
        g_type_register_static (GST_TYPE_BIN, "GstAviDecoder",
        &avi_decoder_info, 0);
  }
  return avi_decoder_type;
}

static void
gst_avi_decoder_class_init (GstAviDecoderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_BITRATE, g_param_spec_long ("bitrate", "bitrate", "bitrate", G_MINLONG, G_MAXLONG, 0, G_PARAM_READABLE));        /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_MEDIA_TIME, g_param_spec_long ("media_time", "media_time", "media_time", G_MINLONG, G_MAXLONG, 0, G_PARAM_READABLE));    /* CHECKME */
  g_object_class_install_property (G_OBJECT_CLASS (klass), ARG_CURRENT_TIME, g_param_spec_long ("current_time", "current_time", "current_time", G_MINLONG, G_MAXLONG, 0, G_PARAM_READABLE));    /* CHECKME */

  parent_class = g_type_class_ref (GST_TYPE_BIN);

  gobject_class->get_property = gst_avi_decoder_get_property;
}

static void
gst_avi_decoder_new_pad (GstElement * element, GstPad * pad,
    GstAviDecoder * avi_decoder)
{
  GstCaps *caps;
  GstCaps *targetcaps = NULL;
  const gchar *format;
  gboolean type_found;
  GstElement *type;
  GstElement *new_element = NULL;
  gchar *padname = NULL;
  gchar *gpadname = NULL;

#define AVI_TYPE_VIDEO  1
#define AVI_TYPE_AUDIO  2
  gint media_type = 0;

  GST_DEBUG (0, "avidecoder: new pad for element \"%s\"",
      gst_element_get_name (element));

  caps = gst_pad_get_caps (pad);
  format = gst_caps_get_string (caps, "format");

  if (!strcmp (format, "strf_vids")) {
    targetcaps =
        gst_padtemplate_get_caps (GST_PADTEMPLATE_GET (src_video_templ));
    media_type = AVI_TYPE_VIDEO;
    gpadname = g_strdup_printf ("video_%02d", avi_decoder->video_count++);
  } else if (!strcmp (format, "strf_auds")) {
    targetcaps =
        gst_padtemplate_get_caps (GST_PADTEMPLATE_GET (src_audio_templ));
    media_type = AVI_TYPE_AUDIO;
    gpadname = g_strdup_printf ("audio_%02d", avi_decoder->audio_count++);
  } else if (!strcmp (format, "strf_iavs")) {
    targetcaps =
        gst_padtemplate_get_caps (GST_PADTEMPLATE_GET (src_video_templ));
    media_type = AVI_TYPE_VIDEO;
    gpadname = g_strdup_printf ("video_%02d", avi_decoder->video_count++);
  } else {
    g_assert_not_reached ();
  }

  gst_element_set_state (GST_ELEMENT (avi_decoder), GST_STATE_PAUSED);

  type = gst_elementfactory_make ("avitypes",
      g_strdup_printf ("typeconvert%d", avi_decoder->count));

  /* brin the element to the READY state so it can do our caps negotiation */
  gst_element_set_state (type, GST_STATE_READY);

  gst_pad_connect (pad, gst_element_get_pad (type, "sink"));
  type_found = gst_util_get_bool_arg (G_OBJECT (type), "type_found");

  if (type_found) {

    gst_bin_add (GST_BIN (avi_decoder), type);

    pad = gst_element_get_pad (type, "src");
    caps = gst_pad_get_caps (pad);

    if (gst_caps_is_always_compatible (caps, targetcaps)) {
      gst_element_add_ghost_pad (GST_ELEMENT (avi_decoder),
          gst_element_get_pad (type, "src"), gpadname);

      avi_decoder->count++;
      goto done;
    }
#ifndef GST_DISABLE_AUTOPLUG
    else {
      GstAutoplug *autoplug;

      autoplug = gst_autoplugfactory_make ("static");

      new_element = gst_autoplug_to_caps (autoplug, caps, targetcaps, NULL);

      padname = "src_00";
    }
#endif /* GST_DISABLE_AUTOPLUG */
  }

  if (!new_element && (media_type == AVI_TYPE_VIDEO)) {
    padname = "src";
  } else if (!new_element && (media_type == AVI_TYPE_AUDIO)) {
    /*FIXME */
    padname = "src";
  }

  if (new_element) {
    gst_pad_connect (pad, gst_element_get_pad (new_element, "sink"));
    gst_element_set_name (new_element, g_strdup_printf ("element%d",
            avi_decoder->count));
    gst_bin_add (GST_BIN (avi_decoder), new_element);

    gst_element_add_ghost_pad (GST_ELEMENT (avi_decoder),
        gst_element_get_pad (new_element, padname), gpadname);

    avi_decoder->count++;
  } else {
    g_warning ("avidecoder: could not autoplug\n");
  }

done:
  gst_element_set_state (GST_ELEMENT (avi_decoder), GST_STATE_PLAYING);
}

static void
gst_avi_decoder_init (GstAviDecoder * avi_decoder)
{
  avi_decoder->demuxer = gst_elementfactory_make ("avidemux", "demux");

  if (avi_decoder->demuxer) {
    gst_bin_add (GST_BIN (avi_decoder), avi_decoder->demuxer);

    gst_element_add_ghost_pad (GST_ELEMENT (avi_decoder),
        gst_element_get_pad (avi_decoder->demuxer, "sink"), "sink");

    g_signal_connect (G_OBJECT (avi_decoder->demuxer), "new_pad",
        G_CALLBACK (gst_avi_decoder_new_pad), avi_decoder);
  } else {
    g_warning ("wow!, no avi demuxer found. help me\n");
  }

  avi_decoder->count = 0;
  avi_decoder->audio_count = 0;
  avi_decoder->video_count = 0;
}

static GstCaps *
avi_typefind (GstBuffer * buf, gpointer private)
{
  gchar *data = GST_BUFFER_DATA (buf);
  GstCaps *new;

  GST_DEBUG (0, "avi_decoder: typefind");
  if (strncmp (&data[0], "RIFF", 4))
    return NULL;
  if (strncmp (&data[8], "AVI ", 4))
    return NULL;

  new = GST_CAPS_NEW ("avi_typefind",
      "video/avi", "RIFF", GST_PROPS_STRING ("AVI"));

  return new;
}

static void
gst_avi_decoder_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAviDecoder *src;

  g_return_if_fail (GST_IS_AVI_DECODER (object));

  src = GST_AVI_DECODER (object);

  switch (prop_id) {
    case ARG_BITRATE:
      break;
    case ARG_MEDIA_TIME:
      g_value_set_long (value, gst_util_get_long_arg (G_OBJECT (src->demuxer),
              "media_time"));
      break;
    case ARG_CURRENT_TIME:
      g_value_set_long (value, gst_util_get_long_arg (G_OBJECT (src->demuxer),
              "current_time"));
      break;
    default:
      break;
  }
}


static gboolean
plugin_init (GModule * module, GstPlugin * plugin)
{
  GstElementFactory *factory;
  GstTypeFactory *type;

  /* create an elementfactory for the avi_decoder element */
  factory = gst_elementfactory_new ("avidecoder", GST_TYPE_AVI_DECODER,
      &gst_avi_decoder_details);
  g_return_val_if_fail (factory != NULL, FALSE);

  gst_elementfactory_add_padtemplate (factory,
      GST_PADTEMPLATE_GET (src_audio_templ));
  gst_elementfactory_add_padtemplate (factory,
      GST_PADTEMPLATE_GET (src_video_templ));
  gst_elementfactory_add_padtemplate (factory,
      GST_PADTEMPLATE_GET (sink_templ));

  type = gst_typefactory_new (&avidefinition);
  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (type));

  gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));

  return TRUE;
}

GstPluginDesc plugin_desc = {
  GST_VERSION_MAJOR,
  GST_VERSION_MINOR,
  "avidecoder",
  plugin_init
};
