/* RealAudio wrapper plugin
 *
 * Copyright (C) 2006 Lutz Mueller <lutz@topfrose.de>
 * Copyright (C) 2006 Edward Hervey <bilboed@bilboed.com>
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

#include "gstreal.h"
#include "gstrealaudiodec.h"

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

#define DEFAULT_RACOOK_NAMES "cook.so:cook.so.6.0"
#define DEFAULT_RAATRK_NAMES "atrc.so:atrc.so.6.0"
#define DEFAULT_RA14_4_NAMES "14_4.so.6.0"
#define DEFAULT_RA28_8_NAMES "28_8.so.6.0"
#define DEFAULT_RASIPR_NAMES "sipr.so:sipr.so.6.0"
#define DEFAULT_PWD "Ardubancel Quazanga"

enum
{
  PROP_0,
  PROP_REAL_CODECS_PATH,
  PROP_RACOOK_NAMES,
  PROP_RAATRK_NAMES,
  PROP_RA14_4_NAMES,
  PROP_RA28_8_NAMES,
  PROP_RASIPR_NAMES,
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
  GModule *module;
  RealFunctions funcs;

  /* Used by the REAL library. */
  gpointer context;

  /* Properties */
  gchar *real_codecs_path, *racook_names, *raatrk_names, *ra14_4_names,
      *ra28_8_names, *rasipr_names;
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
  GstFlowReturn flow;
  GstClockTime timestamp;
  GstBuffer *out = NULL;
  guint16 res = 0;
  guint len;

  if (G_UNLIKELY (dec->funcs.RADecode == NULL || dec->module == NULL))
    goto not_negotiated;

  timestamp = GST_BUFFER_TIMESTAMP (in);

  flow = gst_pad_alloc_buffer (dec->src, GST_BUFFER_OFFSET_NONE,
      dec->width * dec->leaf_size * dec->height * 16,
      GST_PAD_CAPS (dec->src), &out);

  if (flow != GST_FLOW_OK)
    goto done;

  res = dec->funcs.RADecode (dec->context, GST_BUFFER_DATA (in),
      GST_BUFFER_SIZE (in), GST_BUFFER_DATA (out), &len, -1);

  if (res != 0)
    goto could_not_decode;

  GST_BUFFER_SIZE (out) = len;
  GST_BUFFER_TIMESTAMP (out) = timestamp;

  flow = gst_pad_push (dec->src, out);

done:
  gst_buffer_unref (in);
  return flow;

  /* Errors */
could_not_decode:
  {
    gst_buffer_unref (out);
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, (NULL),
        ("Could not decode buffer (%i).", res));
    flow = GST_FLOW_ERROR;
    goto done;
  }
not_negotiated:
  {
    GST_WARNING_OBJECT (dec, "decoder not open, probably no input caps set "
        "yet, caps on input buffer: %" GST_PTR_FORMAT, GST_BUFFER_CAPS (in));
    flow = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }
}

static gboolean
gst_real_audio_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (GST_PAD_PARENT (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gpointer ra_close_codec, ra_decode, ra_free_decoder;
  gpointer ra_open_codec2, ra_init_decoder, ra_set_flavor;
  gpointer set_dll_access_path = NULL, ra_set_pwd = NULL;
  gchar *path, *names;
  gchar **split_names, **split_path;
  gint version, flavor, channels, rate, leaf_size, packet_size, width, height;
  guint16 res = 0;
  RAInit data;
  gboolean bres;
  const GValue *v;
  GstBuffer *buf = NULL;
  const gchar *name = gst_structure_get_name (s);
  GModule *module = NULL;
  gpointer context = NULL;
  RealFunctions funcs = { NULL, };
  int i, j;
  gchar *tmppath = NULL;

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

  path = dec->real_codecs_path ? dec->real_codecs_path :
      DEFAULT_REAL_CODECS_PATH;

  switch (version) {
    case GST_REAL_AUDIO_DEC_VERSION_COOK:
      names = dec->racook_names ? dec->racook_names : DEFAULT_RACOOK_NAMES;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_ATRK:
      names = dec->raatrk_names ? dec->raatrk_names : DEFAULT_RAATRK_NAMES;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_14_4:
      names = dec->ra14_4_names ? dec->ra14_4_names : DEFAULT_RA14_4_NAMES;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_28_8:
      names = dec->ra28_8_names ? dec->ra28_8_names : DEFAULT_RA28_8_NAMES;
      break;
    case GST_REAL_AUDIO_DEC_VERSION_SIPR:
      names = dec->rasipr_names ? dec->rasipr_names : DEFAULT_RASIPR_NAMES;
      break;
    default:
      goto unknown_version;
  }

  split_path = g_strsplit (path, ":", 0);
  split_names = g_strsplit (names, ":", 0);

  for (i = 0; split_path[i]; i++) {
    for (j = 0; split_names[j]; j++) {
      gchar *codec = g_strconcat (split_path[i], "/", split_names[j], NULL);

      module = g_module_open (codec, G_MODULE_BIND_LAZY);
      g_free (codec);
      if (module)
        goto codec_search_done;
    }
  }

codec_search_done:
  /* we keep the path for a while to set the dll access path */
  g_strfreev (split_names);

  if (module == NULL)
    goto could_not_open;

  if (!g_module_symbol (module, "RACloseCodec", &ra_close_codec) ||
      !g_module_symbol (module, "RADecode", &ra_decode) ||
      !g_module_symbol (module, "RAFreeDecoder", &ra_free_decoder) ||
      !g_module_symbol (module, "RAOpenCodec2", &ra_open_codec2) ||
      !g_module_symbol (module, "RAInitDecoder", &ra_init_decoder) ||
      !g_module_symbol (module, "RASetFlavor", &ra_set_flavor)) {
    goto could_not_load;
  }

  g_module_symbol (module, "RASetPwd", &ra_set_pwd);
  g_module_symbol (module, "SetDLLAccessPath", &set_dll_access_path);

  funcs.RACloseCodec = (guint16 (*)(gpointer)) ra_close_codec;
  funcs.RADecode =
      (guint16 (*)(gpointer, guint8 *, guint32, guint8 *, guint32 *, guint32))
      ra_decode;
  funcs.RAFreeDecoder = (guint16 (*)(gpointer)) ra_free_decoder;
  funcs.RAOpenCodec2 = (guint16 (*)(gpointer, const gchar *)) ra_open_codec2;
  funcs.RAInitDecoder = (guint16 (*)(gpointer, gpointer)) ra_init_decoder;
  funcs.RASetFlavor = (guint16 (*)(gpointer, guint16)) ra_set_flavor;
  funcs.RASetPwd = (void (*)(gpointer, gchar *)) ra_set_pwd;
  funcs.SetDLLAccessPath = (void (*)(gchar *)) set_dll_access_path;

  if (funcs.SetDLLAccessPath)
    funcs.SetDLLAccessPath (split_path[i]);

  tmppath = g_strdup_printf ("%s/", split_path[i]);
  if ((res = funcs.RAOpenCodec2 (&context, tmppath))) {
    g_free (tmppath);
    goto could_not_initialize;
  }
  g_free (tmppath);

  /* now we are done with the split paths, so free them */
  g_strfreev (split_path);

  data.samplerate = rate;
  data.width = width;
  data.channels = channels;
  data.quality = 100;
  data.leaf_size = leaf_size;
  data.packet_size = packet_size;
  data.datalen = buf ? GST_BUFFER_SIZE (buf) : 0;
  data.data = buf ? GST_BUFFER_DATA (buf) : NULL;

  if ((res = funcs.RAInitDecoder (context, &data))) {
    GST_WARNING_OBJECT (dec, "RAInitDecoder() failed");
    goto could_not_initialize;
  }

  if (funcs.RASetPwd) {
    funcs.RASetPwd (context, dec->pwd ? dec->pwd : DEFAULT_PWD);
  }

  if ((res = funcs.RASetFlavor (context, flavor))) {
    GST_WARNING_OBJECT (dec, "RASetFlavor(%d) failed", flavor);
    goto could_not_initialize;
  }

  caps = gst_caps_new_simple ("audio/x-raw-int",
      "endianness", G_TYPE_INT, G_BYTE_ORDER,
      "width", G_TYPE_INT, width,
      "depth", G_TYPE_INT, width,
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
  if (dec->module)
    g_module_close (dec->module);
  dec->module = module;
  dec->funcs = funcs;

  return TRUE;

missing_keys:
  {
    GST_DEBUG_OBJECT (dec, "Could not find all necessary keys in structure.");
    return FALSE;
  }
unknown_version:
  {
    GST_DEBUG_OBJECT (dec, "Cannot handle version %i.", version);
    return FALSE;
  }
could_not_open:
  {
    g_strfreev (split_path);
    GST_DEBUG_OBJECT (dec, "Could not find library '%s' in '%s'", names, path);
    return FALSE;
  }
could_not_load:
  {
    g_module_close (module);
    g_strfreev (split_path);
    GST_DEBUG_OBJECT (dec, "Could not load all symbols: %s", g_module_error ());
    return FALSE;
  }
could_not_initialize:
  {
    if (context) {
      funcs.RACloseCodec (context);
      funcs.RAFreeDecoder (context);
    }
    g_module_close (module);
    GST_WARNING_OBJECT (dec, "Initialization of REAL driver failed (%i).", res);
    return FALSE;
  }
could_not_set_caps:
  {
    if (context) {
      funcs.RACloseCodec (context);
      funcs.RAFreeDecoder (context);
    }
    g_module_close (module);
    GST_DEBUG_OBJECT (dec, "Could not convince peer to accept caps.");
    return FALSE;
  }
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
  if (dec->module) {
    g_module_close (dec->module);
    dec->module = NULL;
  }

  if (dec->real_codecs_path) {
    g_free (dec->real_codecs_path);
    dec->real_codecs_path = NULL;
  }
  if (dec->racook_names) {
    g_free (dec->racook_names);
    dec->racook_names = NULL;
  }
  if (dec->raatrk_names) {
    g_free (dec->raatrk_names);
    dec->raatrk_names = NULL;
  }
  if (dec->ra14_4_names) {
    g_free (dec->ra14_4_names);
    dec->ra14_4_names = NULL;
  }
  if (dec->ra28_8_names) {
    g_free (dec->ra28_8_names);
    dec->ra28_8_names = NULL;
  }
  if (dec->rasipr_names) {
    g_free (dec->rasipr_names);
    dec->rasipr_names = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_real_audio_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (object);

  switch (prop_id) {
    case PROP_REAL_CODECS_PATH:
      if (dec->real_codecs_path)
        g_free (dec->real_codecs_path);
      dec->real_codecs_path = g_value_dup_string (value);
      break;
    case PROP_RACOOK_NAMES:
      if (dec->racook_names)
        g_free (dec->racook_names);
      dec->racook_names = g_value_dup_string (value);
      break;
    case PROP_RAATRK_NAMES:
      if (dec->raatrk_names)
        g_free (dec->raatrk_names);
      dec->raatrk_names = g_value_dup_string (value);
      break;
    case PROP_RA14_4_NAMES:
      if (dec->ra14_4_names)
        g_free (dec->ra14_4_names);
      dec->ra14_4_names = g_value_dup_string (value);
      break;
    case PROP_RA28_8_NAMES:
      if (dec->ra28_8_names)
        g_free (dec->ra28_8_names);
      dec->ra28_8_names = g_value_dup_string (value);
      break;
    case PROP_RASIPR_NAMES:
      if (dec->rasipr_names)
        g_free (dec->rasipr_names);
      dec->rasipr_names = g_value_dup_string (value);
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
    case PROP_REAL_CODECS_PATH:
      g_value_set_string (value, dec->real_codecs_path ? dec->real_codecs_path
          : DEFAULT_REAL_CODECS_PATH);
      break;
    case PROP_RACOOK_NAMES:
      g_value_set_string (value, dec->racook_names ? dec->racook_names :
          DEFAULT_RACOOK_NAMES);
      break;
    case PROP_RAATRK_NAMES:
      g_value_set_string (value, dec->raatrk_names ? dec->raatrk_names :
          DEFAULT_RAATRK_NAMES);
      break;
    case PROP_RA14_4_NAMES:
      g_value_set_string (value, dec->ra14_4_names ? dec->ra14_4_names :
          DEFAULT_RA14_4_NAMES);
      break;
    case PROP_RA28_8_NAMES:
      g_value_set_string (value, dec->ra28_8_names ? dec->ra28_8_names :
          DEFAULT_RA28_8_NAMES);
      break;
    case PROP_RASIPR_NAMES:
      g_value_set_string (value, dec->rasipr_names ? dec->rasipr_names :
          DEFAULT_RASIPR_NAMES);
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

  g_object_class_install_property (object_class, PROP_REAL_CODECS_PATH,
      g_param_spec_string ("real_codecs_path",
          "Path where to search for RealPlayer codecs",
          "Path where to search for RealPlayer codecs",
          DEFAULT_REAL_CODECS_PATH, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_RACOOK_NAMES,
      g_param_spec_string ("racook_names", "Names of cook driver",
          "Names of cook driver", DEFAULT_RACOOK_NAMES, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_RAATRK_NAMES,
      g_param_spec_string ("raatrk_names", "Names of atrk driver",
          "Names of atrk driver", DEFAULT_RAATRK_NAMES, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_RA14_4_NAMES,
      g_param_spec_string ("ra14_4_names", "Names of 14_4 driver",
          "Names of 14_4 driver", DEFAULT_RA14_4_NAMES, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_RA28_8_NAMES,
      g_param_spec_string ("ra28_8_names", "Names of 28_8 driver",
          "Names of 28_8 driver", DEFAULT_RA28_8_NAMES, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_RASIPR_NAMES,
      g_param_spec_string ("rasipr_names", "Names of sipr driver",
          "Names of sipr driver", DEFAULT_RASIPR_NAMES, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PASSWORD,
      g_param_spec_string ("password", "Password",
          "Password", DEFAULT_PWD, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (real_audio_dec_debug, "realaudiodec", 0,
      "RealAudio decoder");
}
