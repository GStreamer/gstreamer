/* GStreamer
 *
 * Copyright (C) 2006 Lutz Mueller <lutz@topfrose.de>
 *		 2006 Edward Hervey <bilboed@bilboed.com>
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

#include "gstrealaudiodec.h"

#include <dlfcn.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (real_audio_dec_debug);
#define GST_CAT_DEFAULT real_audio_dec_debug

static GstElementDetails real_audio_dec_details =
GST_ELEMENT_DETAILS ("RealAudio decoder",
    "Codec/Decoder/Audio", "Decoder for RealAudio streams",
    "Lutz Mueller <lutz@topfrose.de>");

static GstStaticPadTemplate snk_t =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-pn-realaudio; " "audio/x-sipro "));
static GstStaticPadTemplate src_t =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "width = (int) [ 1, MAX ], "
        "depth = (int) [ 1, MAX ], "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]"));

#ifdef HAVE_CPU_I386
#define DEFAULT_PATH "/usr/lib/win32/"
#endif
#ifdef HAVE_CPU_X86_64
#define DEFAULT_PATH "/usr/lib/"
#endif

#define DEFAULT_PATH_RACOOK DEFAULT_PATH "cook.so.6.0"
#define DEFAULT_PATH_RAATRK DEFAULT_PATH "atrk.so.6.0"
#define DEFAULT_PATH_RA14_4 DEFAULT_PATH "14_4.so.6.0"
#define DEFAULT_PATH_RA28_8 DEFAULT_PATH "28_8.so.6.0"
#define DEFAULT_PATH_RASIPR DEFAULT_PATH "sipr.so.6.0"
#define DEFAULT_PWD "Ardubancel Quazanga"

enum
{
  PROP_0,
  PROP_PATH_RACOOK,
  PROP_PATH_RAATRK,
  PROP_PATH_RA14_4,
  PROP_PATH_RA28_8,
  PROP_PATH_RASIPR,
  PROP_PASSWORD
};

typedef enum
{
  GST_REAL_AUDIO_DEC_VERSION_COOK = 8,
  GST_REAL_AUDIO_DEC_VERSION_ATRK = 3,
  GST_REAL_AUDIO_DEC_VERSION_14_4 = 4,
  GST_REAL_AUDIO_DEC_VERSION_28_8 = 5,
  GST_REAL_AUDIO_DEC_VERSION_SIPR = 6
} GstRealAudioDecVersion;

typedef struct
{
  guint16 (*RADecode) (gpointer, guint8 *, guint32, guint8 *, guint32 *,
      guint32);
  guint16 (*RACloseCodec) (gpointer);
  guint16 (*RAFreeDecoder) (gpointer);
  guint16 (*RAInitDecoder) (gpointer, gpointer);
  guint16 (*RAOpenCodec2) (gpointer, const gchar *);
  guint16 (*RASetFlavor) (gpointer, guint16);
  void (*SetDLLAccessPath) (gchar *);
  void (*RASetPwd) (gpointer, gchar *);
} RealFunctions;

typedef struct
{
  guint32 samplerate;
  guint16 width;
  guint16 channels;
  guint16 quality;
  guint32 leaf_size;
  guint32 packet_size;
  guint32 datalen;
  gpointer data;
} RAInit;

struct _GstRealAudioDec
{
  GstElement parent;

  GstPad *src, *snk;

  /* Caps */
  guint width, height, leaf_size;

  /* Hooks */
  gpointer handle;
  RealFunctions funcs;

  /* Used by the REAL library. */
  gpointer context;

  /* Properties */
  gchar *path_racook, *path_raatrk, *path_ra14_4, *path_ra28_8, *path_rasipr;
  gchar *pwd;
};

struct _GstRealAudioDecClass
{
  GstElementClass parent_class;
};

GST_BOILERPLATE (GstRealAudioDec, gst_real_audio_dec, GstElement,
    GST_TYPE_ELEMENT);

static GstFlowReturn
gst_real_audio_dec_chain (GstPad * pad, GstBuffer * in)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (GST_PAD_PARENT (pad));
  guint len;
  GstBuffer *out = NULL;
  guint16 res = 0;
  GstFlowReturn ret = GST_FLOW_OK;
  GstClockTime timestamp = GST_BUFFER_TIMESTAMP (in);

  if ((ret = gst_pad_alloc_buffer (dec->src, GST_BUFFER_OFFSET_NONE,
              dec->width * dec->leaf_size * dec->height * 16,
              GST_PAD_CAPS (dec->src), &out)) != GST_FLOW_OK)
    return ret;
  res = dec->funcs.RADecode (dec->context, GST_BUFFER_DATA (in),
      GST_BUFFER_SIZE (in), GST_BUFFER_DATA (out), &len, -1);
  if (res)
    goto could_not_decode;
  GST_BUFFER_SIZE (out) = len;
  GST_BUFFER_TIMESTAMP (out) = timestamp;
  return gst_pad_push (dec->src, out);

  /* Errors */
could_not_decode:
  gst_buffer_unref (out);
  GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Could not decode buffer (%i).",
          res), (NULL));
  return GST_FLOW_ERROR;
}

static gboolean
gst_real_audio_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (GST_PAD_PARENT (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gchar *path;
  gint version, flavor, channels, rate, leaf_size, packet_size, width, height;
  guint16 res;
  RAInit data;
  gboolean bres;
  const GValue *v;
  GstBuffer *buf = NULL;
  const gchar *name = gst_structure_get_name (s);
  gpointer context = NULL, handle;
  RealFunctions funcs;

  if (!strcmp (name, "audio/x-sipro"))
    version = GST_REAL_AUDIO_DEC_VERSION_SIPR;
  else {
    if (!gst_structure_get_int (s, "raversion", &version))
      goto missing_keys;
  }

  if (!gst_structure_get_int (s, "flavor", &flavor) ||
      !gst_structure_get_int (s, "channels", &channels) ||
      !gst_structure_get_int (s, "width", &width) ||
      !gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "height", &height) ||
      !gst_structure_get_int (s, "leaf_size", &leaf_size) ||
      !gst_structure_get_int (s, "packet_size", &packet_size))
    goto missing_keys;
  if ((v = gst_structure_get_value (s, "codec_data")))
    buf = g_value_peek_pointer (v);

  switch (version) {
    case GST_REAL_AUDIO_DEC_VERSION_COOK:
      path = dec->path_racook ? dec->path_racook : DEFAULT_PATH_RACOOK;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_ATRK:
      path = dec->path_raatrk ? dec->path_raatrk : DEFAULT_PATH_RAATRK;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_14_4:
      path = dec->path_ra14_4 ? dec->path_ra14_4 : DEFAULT_PATH_RA14_4;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_28_8:
      path = dec->path_ra28_8 ? dec->path_ra28_8 : DEFAULT_PATH_RA28_8;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_SIPR:
      path = dec->path_rasipr ? dec->path_rasipr : DEFAULT_PATH_RASIPR;
      break;
    default:
      goto unknown_version;
  }

  handle = dlopen (path, RTLD_LAZY);
  if (!handle)
    goto could_not_open;
  funcs.RACloseCodec = dlsym (handle, "RACloseCodec");
  funcs.RADecode = dlsym (handle, "RADecode");
  funcs.RAFreeDecoder = dlsym (handle, "RAFreeDecoder");
  funcs.RAOpenCodec2 = dlsym (handle, "RAOpenCodec2");
  funcs.RAInitDecoder = dlsym (handle, "RAInitDecoder");
  funcs.RASetFlavor = dlsym (handle, "RASetFlavor");
  funcs.SetDLLAccessPath = dlsym (handle, "SetDLLAccessPath");
  funcs.RASetPwd = dlsym (handle, "RASetPwd");
  if (!(funcs.RACloseCodec && funcs.RADecode &&
          funcs.RAFreeDecoder && funcs.RAOpenCodec2 &&
          funcs.RAInitDecoder && funcs.RASetFlavor))
    goto could_not_load;

  if (funcs.SetDLLAccessPath)
    funcs.SetDLLAccessPath (DEFAULT_PATH);

  if ((res = funcs.RAOpenCodec2 (&context, NULL))) {
    GST_DEBUG_OBJECT (dec, "RAOpenCodec2() failed");
    goto could_not_initialize;
  }

  data.samplerate = rate;
  data.width = width;
  data.channels = channels;
  data.quality = 100;
  data.leaf_size = leaf_size;
  data.packet_size = packet_size;
  data.datalen = buf ? GST_BUFFER_SIZE (buf) : 0;
  data.data = buf ? GST_BUFFER_DATA (buf) : NULL;

  if ((res = funcs.RAInitDecoder (context, &data))) {
    GST_DEBUG_OBJECT (dec, "RAInitDecoder() failed");
    goto could_not_initialize;
  }

  if (funcs.RASetPwd) {
    funcs.RASetPwd (dec->context, dec->pwd ? dec->pwd : DEFAULT_PWD);
  }

  if ((res = funcs.RASetFlavor (context, flavor))) {
    GST_DEBUG_OBJECT (dec, "RASetFlavor(%d) failed", flavor);
    goto could_not_initialize;
  }

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, width,
      "depth", G_TYPE_INT, height,
      "rate", G_TYPE_INT, rate,
      "channels", G_TYPE_INT, channels, "signed", G_TYPE_BOOLEAN, TRUE, NULL);
  bres = gst_pad_set_caps (GST_PAD (dec->src), caps);
  gst_caps_unref (caps);
  if (!bres)
    goto could_not_set_caps;

  dec->width = width;
  dec->height = height;
  dec->leaf_size = leaf_size;
  if (dec->context) {
    dec->funcs.RACloseCodec (dec->context);
    dec->funcs.RAFreeDecoder (dec->context);
  }
  dec->context = context;
  if (dec->handle)
    dlclose (dec->handle);
  dec->handle = handle;
  dec->funcs = funcs;

  return TRUE;

missing_keys:
  GST_DEBUG_OBJECT (dec, "Could not find all necessary keys in structure.");
  return FALSE;
unknown_version:
  GST_DEBUG_OBJECT (dec, "Cannot handle version %i.", version);
  return FALSE;
could_not_open:
  GST_DEBUG_OBJECT (dec, "Could not open library '%s'.", path);
  return FALSE;
could_not_load:
  dlclose (handle);
  GST_DEBUG_OBJECT (dec, "Could not load all symbols.");
  return FALSE;
could_not_initialize:
  if (context) {
    funcs.RACloseCodec (context);
    funcs.RAFreeDecoder (context);
  }
  dlclose (handle);
  GST_DEBUG_OBJECT (dec, "Initialization of REAL driver failed (%i).", res);
  return FALSE;
could_not_set_caps:
  if (context) {
    funcs.RACloseCodec (context);
    funcs.RAFreeDecoder (context);
  }
  dlclose (handle);
  GST_DEBUG_OBJECT (dec, "Could not convince peer to accept caps.");
  return FALSE;
}

static void
gst_real_audio_dec_init (GstRealAudioDec * dec, GstRealAudioDecClass * klass)
{
  dec->snk = gst_pad_new_from_static_template (&snk_t, "sink");
  gst_pad_set_setcaps_function (dec->snk,
      GST_DEBUG_FUNCPTR (gst_real_audio_dec_setcaps));
  gst_pad_set_chain_function (dec->snk,
      GST_DEBUG_FUNCPTR (gst_real_audio_dec_chain));
  gst_element_add_pad (GST_ELEMENT (dec), dec->snk);

  dec->src = gst_pad_new_from_static_template (&src_t, "src");
  gst_pad_use_fixed_caps (dec->src);
  gst_element_add_pad (GST_ELEMENT (dec), dec->src);
}

static void
gst_real_audio_dec_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&snk_t));
  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&src_t));
  gst_element_class_set_details (ec, &real_audio_dec_details);
}

static GstStateChangeReturn
gst_real_audio_dec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }
  return ret;
}

static void
gst_real_audio_dec_finalize (GObject * object)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (object);

  if (dec->context) {
    dec->funcs.RACloseCodec (dec->context);
    /* Calling RAFreeDecoder seems to randomly cause SEGFAULTs.
     * All other implementation (xine, mplayer) have also got this function call
     * commented. So until we know more, we comment it too. */

    /*     dec->funcs.RAFreeDecoder (dec->context); */
    dec->context = NULL;
  }
  if (dec->handle) {
    dlclose (dec->handle);
    dec->handle = NULL;
  }

  if (dec->path_racook) {
    g_free (dec->path_racook);
    dec->path_racook = NULL;
  }
  if (dec->path_raatrk) {
    g_free (dec->path_raatrk);
    dec->path_raatrk = NULL;
  }
  if (dec->path_ra14_4) {
    g_free (dec->path_ra14_4);
    dec->path_ra14_4 = NULL;
  }
  if (dec->path_ra28_8) {
    g_free (dec->path_ra28_8);
    dec->path_ra28_8 = NULL;
  }
  if (dec->path_rasipr) {
    g_free (dec->path_rasipr);
    dec->path_rasipr = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_real_audio_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (object);

  switch (prop_id) {
    case PROP_PATH_RACOOK:
      if (dec->path_racook)
        g_free (dec->path_racook);
      dec->path_racook = g_value_dup_string (value);
      break;
    case PROP_PATH_RAATRK:
      if (dec->path_raatrk)
        g_free (dec->path_raatrk);
      dec->path_raatrk = g_value_dup_string (value);
      break;
    case PROP_PATH_RA14_4:
      if (dec->path_ra14_4)
        g_free (dec->path_ra14_4);
      dec->path_ra14_4 = g_value_dup_string (value);
      break;
    case PROP_PATH_RA28_8:
      if (dec->path_ra28_8)
        g_free (dec->path_ra28_8);
      dec->path_ra28_8 = g_value_dup_string (value);
      break;
    case PROP_PATH_RASIPR:
      if (dec->path_rasipr)
        g_free (dec->path_rasipr);
      dec->path_rasipr = g_value_dup_string (value);
      break;
    case PROP_PASSWORD:
      if (dec->pwd)
        g_free (dec->pwd);
      dec->pwd = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_real_audio_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (object);

  switch (prop_id) {
    case PROP_PATH_RACOOK:
      g_value_set_string (value, dec->path_racook ? dec->path_racook :
          DEFAULT_PATH_RACOOK);
      break;
    case PROP_PATH_RAATRK:
      g_value_set_string (value, dec->path_raatrk ? dec->path_raatrk :
          DEFAULT_PATH_RAATRK);
      break;
    case PROP_PATH_RA14_4:
      g_value_set_string (value, dec->path_ra14_4 ? dec->path_ra14_4 :
          DEFAULT_PATH_RA14_4);
      break;
    case PROP_PATH_RA28_8:
      g_value_set_string (value, dec->path_ra28_8 ? dec->path_ra28_8 :
          DEFAULT_PATH_RA28_8);
      break;
    case PROP_PATH_RASIPR:
      g_value_set_string (value, dec->path_rasipr ? dec->path_rasipr :
          DEFAULT_PATH_RASIPR);
      break;
    case PROP_PASSWORD:
      g_value_set_string (value, dec->pwd ? dec->pwd : DEFAULT_PWD);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_real_audio_dec_class_init (GstRealAudioDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->set_property = gst_real_audio_dec_set_property;
  object_class->get_property = gst_real_audio_dec_get_property;
  object_class->finalize = gst_real_audio_dec_finalize;

  element_class->change_state = gst_real_audio_dec_change_state;

  g_object_class_install_property (object_class, PROP_PATH_RACOOK,
      g_param_spec_string ("path_racook", "Path to cook driver",
          "Path to cook driver", DEFAULT_PATH_RACOOK, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PATH_RAATRK,
      g_param_spec_string ("path_raatrk", "Path to atrk driver",
          "Path to atrk driver", DEFAULT_PATH_RAATRK, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PATH_RA14_4,
      g_param_spec_string ("path_ra14_4", "Path to 14_4 driver",
          "Path to 14_4 driver", DEFAULT_PATH_RA14_4, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PATH_RA28_8,
      g_param_spec_string ("path_ra28_8", "Path to 28_8 driver",
          "Path to 28_8 driver", DEFAULT_PATH_RA28_8, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PATH_RASIPR,
      g_param_spec_string ("path_rasipr", "Path to sipr driver",
          "Path to sipr driver", DEFAULT_PATH_RASIPR, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PASSWORD,
      g_param_spec_string ("password", "Password",
          "Password", DEFAULT_PWD, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (real_audio_dec_debug, "realaudiodec", 0,
      "RealAudio decoder");
}
