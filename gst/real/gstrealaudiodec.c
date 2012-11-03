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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstreal.h"
#include "gstrealaudiodec.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (real_audio_dec_debug);
#define GST_CAT_DEFAULT real_audio_dec_debug

static GstStaticPadTemplate snk_t =
    GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-pn-realaudio, "
        "raversion = { 3, 4, 5, 6, 8 }; " "audio/x-sipro "));

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
  GST_REAL_AUDIO_DEC_VERSION_ATRK = 3,
  GST_REAL_AUDIO_DEC_VERSION_14_4 = 4,
  GST_REAL_AUDIO_DEC_VERSION_28_8 = 5,
  GST_REAL_AUDIO_DEC_VERSION_SIPR = 6,
  GST_REAL_AUDIO_DEC_VERSION_COOK = 8
} GstRealAudioDecVersion;

typedef struct
{
  /* Hooks */
  GModule *module;

  /* Used by the REAL library. */
  gpointer context;

    guint16 (*RADecode) (gpointer, guint8 *, guint32, guint8 *, guint32 *,
      guint32);
    guint16 (*RACloseCodec) (gpointer);
    guint16 (*RAFreeDecoder) (gpointer);
    guint16 (*RAInitDecoder) (gpointer, gpointer);
    guint16 (*RAOpenCodec2) (gpointer, const gchar *);
    guint16 (*RASetFlavor) (gpointer, guint16);
  void (*SetDLLAccessPath) (gchar *);
  void (*RASetPwd) (gpointer, const gchar *);
} GstRADecLibrary;

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

  GstRADecLibrary lib;

  /* Properties */
  gboolean checked_modules;
  gchar *real_codecs_path;
  gchar *raatrk_names;
  gboolean valid_atrk;
  gchar *ra14_4_names;
  gboolean valid_ra14_4;
  gchar *ra28_8_names;
  gboolean valid_ra28_8;
  gchar *rasipr_names;
  gboolean valid_sipr;
  gchar *racook_names;
  gboolean valid_cook;
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

  if (G_UNLIKELY (dec->lib.RADecode == NULL || dec->lib.module == NULL))
    goto not_negotiated;

  timestamp = GST_BUFFER_TIMESTAMP (in);

  flow = gst_pad_alloc_buffer (dec->src, GST_BUFFER_OFFSET_NONE,
      dec->width * dec->leaf_size * dec->height * 16,
      GST_PAD_CAPS (dec->src), &out);

  if (flow != GST_FLOW_OK)
    goto done;

  res = dec->lib.RADecode (dec->lib.context, GST_BUFFER_DATA (in),
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

static void
close_library (GstRealAudioDec * dec, GstRADecLibrary * lib)
{
  if (lib->context) {
    GST_LOG_OBJECT (dec, "closing library");
    if (lib->RACloseCodec)
      lib->RACloseCodec (lib->context);
    /* lib->RAFreeDecoder (lib->context); */
  }
  if (lib->module) {
    GST_LOG_OBJECT (dec, "closing library module");
    g_module_close (lib->module);
  }
  memset (lib, 0, sizeof (GstRADecLibrary));
}

static gboolean
open_library (GstRealAudioDec * dec, gint version, GstRADecLibrary * lib)
{
  const gchar *path, *names;
  gchar **split_names, **split_path;
  gint i, j;
  gpointer ra_close_codec, ra_decode, ra_free_decoder;
  gpointer ra_open_codec2, ra_init_decoder, ra_set_flavor;
  gpointer set_dll_access_path = NULL, ra_set_pwd = NULL;
  gchar *tmppath = NULL;
  guint16 res = 0;

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

  GST_LOG_OBJECT (dec, "splitting paths %s, names %s", path, names);

  split_path = g_strsplit (path, ":", 0);
  split_names = g_strsplit (names, ":", 0);

  for (i = 0; split_path[i]; i++) {
    for (j = 0; split_names[j]; j++) {
      gchar *codec = g_strconcat (split_path[i], "/", split_names[j], NULL);

      GST_LOG_OBJECT (dec, "opening module %s", codec);

      /* This is racy, but it doesn't matter here; would be nice if GModule
       * gave us a GError instead of an error string, but it doesn't, so.. */
      if (g_file_test (codec, G_FILE_TEST_EXISTS)) {
        lib->module = g_module_open (codec, G_MODULE_BIND_LAZY);
        if (lib->module == NULL) {
          GST_ERROR_OBJECT (dec, "Could not open codec library '%s': %s",
              codec, g_module_error ());
        }
      } else {
        GST_DEBUG_OBJECT (dec, "%s does not exist", codec);
      }
      g_free (codec);
      if (lib->module)
        goto codec_search_done;
    }
  }

codec_search_done:
  /* we keep the path for a while to set the dll access path */
  g_strfreev (split_names);

  if (lib->module == NULL)
    goto could_not_open;

  GST_LOG_OBJECT (dec, "finding symbols");

  if (!g_module_symbol (lib->module, "RACloseCodec", &ra_close_codec) ||
      !g_module_symbol (lib->module, "RADecode", &ra_decode) ||
      !g_module_symbol (lib->module, "RAFreeDecoder", &ra_free_decoder) ||
      !g_module_symbol (lib->module, "RAOpenCodec2", &ra_open_codec2) ||
      !g_module_symbol (lib->module, "RAInitDecoder", &ra_init_decoder) ||
      !g_module_symbol (lib->module, "RASetFlavor", &ra_set_flavor)) {
    goto could_not_load;
  }

  g_module_symbol (lib->module, "RASetPwd", &ra_set_pwd);
  g_module_symbol (lib->module, "SetDLLAccessPath", &set_dll_access_path);

  lib->RACloseCodec = (guint16 (*)(gpointer)) ra_close_codec;
  lib->RADecode =
      (guint16 (*)(gpointer, guint8 *, guint32, guint8 *, guint32 *, guint32))
      ra_decode;
  lib->RAFreeDecoder = (guint16 (*)(gpointer)) ra_free_decoder;
  lib->RAOpenCodec2 = (guint16 (*)(gpointer, const gchar *)) ra_open_codec2;
  lib->RAInitDecoder = (guint16 (*)(gpointer, gpointer)) ra_init_decoder;
  lib->RASetFlavor = (guint16 (*)(gpointer, guint16)) ra_set_flavor;
  lib->RASetPwd = (void (*)(gpointer, const gchar *)) ra_set_pwd;
  lib->SetDLLAccessPath = (void (*)(gchar *)) set_dll_access_path;

  if (lib->SetDLLAccessPath)
    lib->SetDLLAccessPath (split_path[i]);

  tmppath = g_strdup_printf ("%s/", split_path[i]);
  if ((res = lib->RAOpenCodec2 (&lib->context, tmppath))) {
    g_free (tmppath);
    goto could_not_initialize;
  }
  g_free (tmppath);

  /* now we are done with the split paths, so free them */
  g_strfreev (split_path);

  return TRUE;

  /* ERRORS */
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
    g_strfreev (split_path);
    close_library (dec, lib);
    GST_DEBUG_OBJECT (dec, "Could not load all symbols: %s", g_module_error ());
    return FALSE;
  }
could_not_initialize:
  {
    close_library (dec, lib);
    GST_WARNING_OBJECT (dec, "Initialization of REAL driver failed (%i).", res);
    return FALSE;
  }
}

static void
gst_real_audio_dec_probe_modules (GstRealAudioDec * dec)
{
  GstRADecLibrary dummy = { NULL };

  if ((dec->valid_atrk =
          open_library (dec, GST_REAL_AUDIO_DEC_VERSION_ATRK, &dummy)))
    close_library (dec, &dummy);
  if ((dec->valid_ra14_4 =
          open_library (dec, GST_REAL_AUDIO_DEC_VERSION_14_4, &dummy)))
    close_library (dec, &dummy);
  if ((dec->valid_ra28_8 =
          open_library (dec, GST_REAL_AUDIO_DEC_VERSION_28_8, &dummy)))
    close_library (dec, &dummy);
#ifdef HAVE_CPU_X86_64
  /* disabled because it does not seem to work on 64 bits */
  dec->valid_sipr = FALSE;
#else
  if ((dec->valid_sipr =
          open_library (dec, GST_REAL_AUDIO_DEC_VERSION_SIPR, &dummy)))
    close_library (dec, &dummy);
#endif
  if ((dec->valid_cook =
          open_library (dec, GST_REAL_AUDIO_DEC_VERSION_COOK, &dummy)))
    close_library (dec, &dummy);
}

static GstCaps *
gst_real_audio_dec_getcaps (GstPad * pad)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (GST_PAD_PARENT (pad));
  GstCaps *res;

  if (dec->checked_modules) {
    GValue versions = { 0 };
    GValue version = { 0 };

    GST_LOG_OBJECT (dec, "constructing caps");

    g_value_init (&versions, GST_TYPE_LIST);
    g_value_init (&version, G_TYPE_INT);

    if (dec->valid_atrk) {
      g_value_set_int (&version, GST_REAL_AUDIO_DEC_VERSION_ATRK);
      gst_value_list_append_value (&versions, &version);
    }
    if (dec->valid_ra14_4) {
      g_value_set_int (&version, GST_REAL_AUDIO_DEC_VERSION_14_4);
      gst_value_list_append_value (&versions, &version);
    }
    if (dec->valid_ra28_8) {
      g_value_set_int (&version, GST_REAL_AUDIO_DEC_VERSION_28_8);
      gst_value_list_append_value (&versions, &version);
    }
    if (dec->valid_sipr) {
      g_value_set_int (&version, GST_REAL_AUDIO_DEC_VERSION_SIPR);
      gst_value_list_append_value (&versions, &version);
    }
    if (dec->valid_cook) {
      g_value_set_int (&version, GST_REAL_AUDIO_DEC_VERSION_COOK);
      gst_value_list_append_value (&versions, &version);
    }

    if (gst_value_list_get_size (&versions) > 0) {
      res = gst_caps_new_simple ("audio/x-pn-realaudio", NULL);
      gst_structure_set_value (gst_caps_get_structure (res, 0),
          "raversion", &versions);
    } else {
      res = gst_caps_new_empty ();
    }

    if (dec->valid_sipr) {
      gst_caps_append (res, gst_caps_new_simple ("audio/x-sipro", NULL));
    }
    g_value_unset (&versions);
    g_value_unset (&version);
  } else {
    GST_LOG_OBJECT (dec, "returning padtemplate caps");
    res = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  }
  GST_LOG_OBJECT (dec, "returning caps %" GST_PTR_FORMAT, res);

  return res;
}

static gboolean
gst_real_audio_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (GST_PAD_PARENT (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint version, flavor, channels, rate, leaf_size, packet_size, width, height;
  guint16 res = 0;
  RAInit data;
  gboolean bres;
  const GValue *v;
  GstBuffer *buf = NULL;
  const gchar *name = gst_structure_get_name (s);

  if (!strcmp (name, "audio/x-sipro")) {
    version = GST_REAL_AUDIO_DEC_VERSION_SIPR;
  } else {
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

  GST_LOG_OBJECT (dec, "opening code for version %d", version);

  /* first close existing decoder */
  close_library (dec, &dec->lib);

  if (!open_library (dec, version, &dec->lib))
    goto could_not_open;

  /* we have the module, no initialize with the caps data */
  data.samplerate = rate;
  data.width = width;
  data.channels = channels;
  data.quality = 100;
  data.leaf_size = leaf_size;
  data.packet_size = packet_size;
  data.datalen = buf ? GST_BUFFER_SIZE (buf) : 0;
  data.data = buf ? GST_BUFFER_DATA (buf) : NULL;

  if ((res = dec->lib.RAInitDecoder (dec->lib.context, &data))) {
    GST_WARNING_OBJECT (dec, "RAInitDecoder() failed");
    goto could_not_initialize;
  }

  if (dec->lib.RASetPwd) {
    dec->lib.RASetPwd (dec->lib.context, dec->pwd ? dec->pwd : DEFAULT_PWD);
  }

  if ((res = dec->lib.RASetFlavor (dec->lib.context, flavor))) {
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

  GST_LOG_OBJECT (dec, "opened module");

  return TRUE;

missing_keys:
  {
    GST_DEBUG_OBJECT (dec, "Could not find all necessary keys in structure.");
    return FALSE;
  }
could_not_open:
  {
    GST_DEBUG_OBJECT (dec, "Could not find decoder");
    return FALSE;
  }
could_not_initialize:
  {
    close_library (dec, &dec->lib);
    GST_WARNING_OBJECT (dec, "Initialization of REAL driver failed (%i).", res);
    return FALSE;
  }
could_not_set_caps:
  {
    /* should normally not fail */
    close_library (dec, &dec->lib);
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
  gst_pad_set_getcaps_function (dec->snk,
      GST_DEBUG_FUNCPTR (gst_real_audio_dec_getcaps));
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
  gst_element_class_set_static_metadata (ec, "RealAudio decoder",
      "Codec/Decoder/Audio", "Decoder for RealAudio streams",
      "Lutz Mueller <lutz@topfrose.de>");
}

static GstStateChangeReturn
gst_real_audio_dec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRealAudioDec *dec = GST_REAL_AUDIO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_real_audio_dec_probe_modules (dec);
      dec->checked_modules = TRUE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      close_library (dec, &dec->lib);
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      dec->checked_modules = FALSE;
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

  close_library (dec, &dec->lib);

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
      g_param_spec_string ("real-codecs-path",
          "Path where to search for RealPlayer codecs",
          "Path where to search for RealPlayer codecs",
          DEFAULT_REAL_CODECS_PATH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RACOOK_NAMES,
      g_param_spec_string ("racook-names", "Names of cook driver",
          "Names of cook driver", DEFAULT_RACOOK_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RAATRK_NAMES,
      g_param_spec_string ("raatrk-names", "Names of atrk driver",
          "Names of atrk driver", DEFAULT_RAATRK_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RA14_4_NAMES,
      g_param_spec_string ("ra14-4-names", "Names of 14_4 driver",
          "Names of 14_4 driver", DEFAULT_RA14_4_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RA28_8_NAMES,
      g_param_spec_string ("ra28-8-names", "Names of 28_8 driver",
          "Names of 28_8 driver", DEFAULT_RA28_8_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RASIPR_NAMES,
      g_param_spec_string ("rasipr-names", "Names of sipr driver",
          "Names of sipr driver", DEFAULT_RASIPR_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_PASSWORD,
      g_param_spec_string ("password", "Password", "Password",
          DEFAULT_PWD, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (real_audio_dec_debug, "realaudiodec", 0,
      "RealAudio decoder");
}
