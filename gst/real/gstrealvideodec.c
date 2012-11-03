/* RealVideo wrapper plugin
 *
 * Copyright (C) 2005 Lutz Mueller <lutz@topfrose.de>
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
#include "gstrealvideodec.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (realvideode_debug);
#define GST_CAT_DEFAULT realvideode_debug

static GstStaticPadTemplate snk_t =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-pn-realvideo, " "rmversion = (int) [ 2, 4 ]"));
static GstStaticPadTemplate src_t =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ] "));

#define DEFAULT_RV20_NAMES "drv2.so:drv2.so.6.0"
#define DEFAULT_RV30_NAMES "drvc.so:drv3.so.6.0"
#define DEFAULT_RV40_NAMES "drvc.so:drv4.so.6.0"
#define DEFAULT_MAX_ERRORS 25

enum
{
  PROP_0,
  PROP_REAL_CODECS_PATH,
  PROP_RV20_NAMES,
  PROP_RV30_NAMES,
  PROP_RV40_NAMES,
  PROP_MAX_ERRORS
};

GST_BOILERPLATE (GstRealVideoDec, gst_real_video_dec, GstElement,
    GST_TYPE_ELEMENT);

static gboolean open_library (GstRealVideoDec * dec,
    GstRealVideoDecVersion version, GstRVDecLibrary * lib);
static void close_library (GstRealVideoDec * dec, GstRVDecLibrary * lib);

typedef struct
{
  guint32 datalen;
  gint32 interpolate;
  gint32 nfragments;
  gpointer fragments;
  guint32 flags;
  guint32 timestamp;
} RVInData;

typedef struct
{
  guint32 frames;
  guint32 notes;
  guint32 timestamp;
  guint32 width;
  guint32 height;
} RVOutData;

static GstFlowReturn
gst_real_video_dec_chain (GstPad * pad, GstBuffer * in)
{
  GstRealVideoDec *dec;
  guint8 *data;
  guint size;
  GstFlowReturn ret;
  RVInData tin;
  RVOutData tout;
  GstClockTime timestamp, duration;
  GstBuffer *out;
  guint32 result;
  guint frag_count, frag_size;

  dec = GST_REAL_VIDEO_DEC (GST_PAD_PARENT (pad));

  if (G_UNLIKELY (dec->lib.Transform == NULL || dec->lib.module == NULL))
    goto not_negotiated;

  data = GST_BUFFER_DATA (in);
  size = GST_BUFFER_SIZE (in);
  timestamp = GST_BUFFER_TIMESTAMP (in);
  duration = GST_BUFFER_DURATION (in);

  GST_DEBUG_OBJECT (dec, "got buffer of size %u, timestamp %" GST_TIME_FORMAT,
      size, GST_TIME_ARGS (timestamp));

  /* alloc output buffer */
  ret = gst_pad_alloc_buffer (dec->src, GST_BUFFER_OFFSET_NONE,
      dec->width * dec->height * 3 / 2, GST_PAD_CAPS (dec->src), &out);
  if (ret != GST_FLOW_OK)
    goto alloc_failed;

  GST_BUFFER_TIMESTAMP (out) = timestamp;
  GST_BUFFER_DURATION (out) = duration;

  frag_count = *data++;
  frag_size = (frag_count + 1) * 8;
  size -= (frag_size + 1);

  GST_DEBUG_OBJECT (dec, "frag_count %u, frag_size %u, data size %u",
      frag_count, frag_size, size);

  /* Decode.
   *
   * The Buffers contain
   *
   *  0                   1                   2                   3
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |  nfragments   |   fragment1 ...                               |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |  ....                                                         |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |  ...          |   fragment2 ...                               |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *    ....                                                          
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |  ...          |   fragment data                               |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   *
   * nfragments: number of fragments 
   * fragmentN: 8 bytes of fragment data (nfragements + 1) of them
   * fragment data: the data of the fragments.
   */
  tin.datalen = size;
  tin.interpolate = 0;
  tin.nfragments = frag_count;
  tin.fragments = data;
  tin.flags = 0;
  tin.timestamp = timestamp;

  /* jump over the frag table to the fragments */
  data += frag_size;

  result = dec->lib.Transform (
      (gchar *) data,
      (gchar *) GST_BUFFER_DATA (out), &tin, &tout, dec->lib.context);
  if (result)
    goto could_not_transform;

  /* When we decoded a frame, reset the error counter. We only fail after N
   * consecutive decoding errors. */
  dec->error_count = 0;

  gst_buffer_unref (in);

  /* Check for new dimensions */
  if (tout.frames && ((dec->width != tout.width)
          || (dec->height != tout.height))) {
    GstCaps *caps = gst_caps_copy (GST_PAD_CAPS (dec->src));
    GstStructure *s = gst_caps_get_structure (caps, 0);

    GST_DEBUG_OBJECT (dec, "New dimensions: %"
        G_GUINT32_FORMAT " x %" G_GUINT32_FORMAT, tout.width, tout.height);

    gst_structure_set (s, "width", G_TYPE_INT, (gint) tout.width,
        "height", G_TYPE_INT, (gint) tout.height, NULL);

    gst_pad_set_caps (dec->src, caps);
    gst_buffer_set_caps (out, caps);
    gst_caps_unref (caps);

    dec->width = tout.width;
    dec->height = tout.height;
    GST_BUFFER_SIZE (out) = dec->width * dec->height * 3 / 2;
  }

  GST_DEBUG_OBJECT (dec,
      "Pushing out buffer with timestamp %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out)));

  if ((ret = gst_pad_push (dec->src, out)) != GST_FLOW_OK)
    goto could_not_push;

  return ret;

  /* Errors */
not_negotiated:
  {
    GST_WARNING_OBJECT (dec, "decoder not open, probably no input caps set "
        "yet, caps on input buffer: %" GST_PTR_FORMAT, GST_BUFFER_CAPS (in));
    gst_buffer_unref (in);
    return GST_FLOW_NOT_NEGOTIATED;
  }
alloc_failed:
  {
    GST_DEBUG_OBJECT (dec, "buffer alloc failed: %s", gst_flow_get_name (ret));
    gst_buffer_unref (in);
    return ret;
  }
could_not_transform:
  {
    gst_buffer_unref (out);
    gst_buffer_unref (in);

    dec->error_count++;

    if (dec->max_errors && dec->error_count >= dec->max_errors) {
      GST_ELEMENT_ERROR (dec, STREAM, DECODE,
          ("Could not decode buffer: %" G_GUINT32_FORMAT, result), (NULL));
      return GST_FLOW_ERROR;
    } else {
      GST_ELEMENT_WARNING (dec, STREAM, DECODE,
          ("Could not decode buffer: %" G_GUINT32_FORMAT, result), (NULL));
      return GST_FLOW_OK;
    }
  }
could_not_push:
  {
    GST_DEBUG_OBJECT (dec, "Could not push buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static GstCaps *
gst_real_video_dec_getcaps (GstPad * pad)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (GST_PAD_PARENT (pad));
  GstCaps *res;

  if (dec->checked_modules) {
    GValue versions = { 0 };
    GValue version = { 0 };

    GST_LOG_OBJECT (dec, "constructing caps");

    g_value_init (&versions, GST_TYPE_LIST);
    g_value_init (&version, G_TYPE_INT);

    if (dec->valid_rv20) {
      g_value_set_int (&version, GST_REAL_VIDEO_DEC_VERSION_2);
      gst_value_list_append_value (&versions, &version);
    }
    if (dec->valid_rv30) {
      g_value_set_int (&version, GST_REAL_VIDEO_DEC_VERSION_3);
      gst_value_list_append_value (&versions, &version);
    }
    if (dec->valid_rv40) {
      g_value_set_int (&version, GST_REAL_VIDEO_DEC_VERSION_4);
      gst_value_list_append_value (&versions, &version);
    }

    if (gst_value_list_get_size (&versions) > 0) {
      res = gst_caps_new_simple ("video/x-pn-realvideo", NULL);
      gst_structure_set_value (gst_caps_get_structure (res, 0),
          "rmversion", &versions);
    } else {
      res = gst_caps_new_empty ();
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
gst_real_video_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (GST_PAD_PARENT (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint version, res, width, height, format, subformat;
  gint framerate_num, framerate_denom;
  gchar data[36];
  gboolean bres;
  const GValue *v;

  if (!gst_structure_get_int (s, "rmversion", &version) ||
      !gst_structure_get_int (s, "width", (gint *) & width) ||
      !gst_structure_get_int (s, "height", (gint *) & height) ||
      !gst_structure_get_int (s, "format", &format) ||
      !gst_structure_get_int (s, "subformat", &subformat) ||
      !gst_structure_get_fraction (s, "framerate", &framerate_num,
          &framerate_denom))
    goto missing_keys;

  GST_LOG_OBJECT (dec, "Setting version to %d", version);

  close_library (dec, &dec->lib);

  if (!open_library (dec, version, &dec->lib))
    goto open_failed;

  /* Initialize REAL driver. */
  GST_WRITE_UINT16_LE (data + 0, 11);
  GST_WRITE_UINT16_LE (data + 2, width);
  GST_WRITE_UINT16_LE (data + 4, height);
  GST_WRITE_UINT16_LE (data + 6, 0);
  GST_WRITE_UINT32_LE (data + 8, 0);
  GST_WRITE_UINT32_LE (data + 12, subformat);
  GST_WRITE_UINT32_LE (data + 16, 1);
  GST_WRITE_UINT32_LE (data + 20, format);

  if ((res = dec->lib.Init (&data, &dec->lib.context)))
    goto could_not_initialize;

  if ((v = gst_structure_get_value (s, "codec_data"))) {
    GstBuffer *buf;
    guint32 *msgdata;
    guint i;
    guint8 *bufdata;
    guint bufsize;
    struct
    {
      guint32 type;
      guint32 msg;
      gpointer data;
      guint32 extra[6];
    } msg;

    buf = g_value_peek_pointer (v);

    bufdata = GST_BUFFER_DATA (buf);
    bufsize = GST_BUFFER_SIZE (buf);

    /* skip format and subformat */
    bufdata += 8;
    bufsize -= 8;

    GST_LOG_OBJECT (dec, "Creating custom message of length %d", bufsize);

    msgdata = g_new0 (guint32, bufsize + 2);
    if (!msgdata)
      goto could_not_allocate;

    msg.type = 0x24;
    msg.msg = 1 + ((subformat >> 16) & 7);
    msg.data = msgdata;
    for (i = 0; i < 6; i++)
      msg.extra[i] = 0;
    msgdata[0] = width;
    msgdata[1] = height;
    for (i = 0; i < bufsize; i++)
      msgdata[i + 2] = 4 * (guint32) bufdata[i];

    res = dec->lib.Message (&msg, dec->lib.context);

    g_free (msgdata);
    if (res)
      goto could_not_send_message;
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
      "framerate", GST_TYPE_FRACTION, framerate_num, framerate_denom,
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);

  /* set PAR if one was specified in the sink caps */
  if ((v = gst_structure_get_value (s, "pixel-aspect-ratio"))) {
    gst_structure_set_value (gst_caps_get_structure (caps, 0),
        "pixel-aspect-ratio", v);
  }

  bres = gst_pad_set_caps (GST_PAD (dec->src), caps);
  gst_caps_unref (caps);
  if (!bres)
    goto could_not_set_caps;

  dec->version = version;
  dec->width = width;
  dec->height = height;
  dec->format = format;
  dec->subformat = subformat;
  dec->framerate_num = framerate_num;
  dec->framerate_denom = framerate_denom;

  return TRUE;

missing_keys:
  {
    GST_ERROR_OBJECT (dec, "Could not find all necessary keys in structure.");
    return FALSE;
  }
open_failed:
  {
    GST_ERROR_OBJECT (dec, "failed to open library");
    return FALSE;
  }
could_not_initialize:
  {
    GST_ERROR_OBJECT (dec, "Initialization of REAL driver failed (%i).", res);
    close_library (dec, &dec->lib);
    return FALSE;
  }
could_not_allocate:
  {
    GST_ERROR_OBJECT (dec, "Could not allocate memory.");
    close_library (dec, &dec->lib);
    return FALSE;
  }
could_not_send_message:
  {
    GST_ERROR_OBJECT (dec, "Failed to send custom message needed for "
        "initialization (%i).", res);
    close_library (dec, &dec->lib);
    return FALSE;
  }
could_not_set_caps:
  {
    GST_ERROR_OBJECT (dec, "Could not convince peer to accept dimensions "
        "%i x %i.", dec->width, dec->height);
    close_library (dec, &dec->lib);
    return FALSE;
  }
}

/* Attempts to open the correct library for the configured version */

static gboolean
open_library (GstRealVideoDec * dec, GstRealVideoDecVersion version,
    GstRVDecLibrary * lib)
{
  gpointer rv_custom_msg, rv_free, rv_init, rv_transform;
  GModule *module = NULL;
  const gchar *path, *names;
  gchar **split_names, **split_path;
  int i, j;

  GST_DEBUG_OBJECT (dec,
      "Attempting to open shared library for real video version %d", version);

  path = dec->real_codecs_path ? dec->real_codecs_path :
      DEFAULT_REAL_CODECS_PATH;

  switch (version) {
    case GST_REAL_VIDEO_DEC_VERSION_2:
      names = dec->rv20_names ? dec->rv20_names : DEFAULT_RV20_NAMES;
      break;
    case GST_REAL_VIDEO_DEC_VERSION_3:
      names = dec->rv30_names ? dec->rv30_names : DEFAULT_RV30_NAMES;
      break;
    case GST_REAL_VIDEO_DEC_VERSION_4:
      names = dec->rv40_names ? dec->rv40_names : DEFAULT_RV40_NAMES;
      break;
    default:
      goto unknown_version;
  }

  split_path = g_strsplit (path, ":", 0);
  split_names = g_strsplit (names, ":", 0);

  for (i = 0; split_path[i]; i++) {
    for (j = 0; split_names[j]; j++) {
      gchar *codec = g_strconcat (split_path[i], "/", split_names[j], NULL);

      GST_DEBUG_OBJECT (dec, "trying %s", codec);
      /* This is racy, but it doesn't matter here; would be nice if GModule
       * gave us a GError instead of an error string, but it doesn't, so.. */
      if (g_file_test (codec, G_FILE_TEST_EXISTS)) {
        module = g_module_open (codec, G_MODULE_BIND_LAZY);
        if (module == NULL) {
          GST_ERROR_OBJECT (dec, "Could not open codec library '%s': %s",
              codec, g_module_error ());
        }
      } else {
        GST_LOG_OBJECT (dec, "%s does not exist", codec);
      }
      g_free (codec);
      if (module)
        goto codec_search_done;
    }
  }

codec_search_done:
  g_strfreev (split_path);
  g_strfreev (split_names);

  if (module == NULL)
    return FALSE;

  GST_DEBUG_OBJECT (dec, "module opened, finding symbols");

  /* First try opening legacy symbols, if that fails try loading new symbols */
  if (g_module_symbol (module, "RV20toYUV420Init", &rv_init) &&
      g_module_symbol (module, "RV20toYUV420Free", &rv_free) &&
      g_module_symbol (module, "RV20toYUV420Transform", &rv_transform) &&
      g_module_symbol (module, "RV20toYUV420CustomMessage", &rv_custom_msg)) {
    GST_LOG_OBJECT (dec, "Loaded legacy symbols");
  } else if (g_module_symbol (module, "RV40toYUV420Init", &rv_init) &&
      g_module_symbol (module, "RV40toYUV420Free", &rv_free) &&
      g_module_symbol (module, "RV40toYUV420Transform", &rv_transform) &&
      g_module_symbol (module, "RV40toYUV420CustomMessage", &rv_custom_msg)) {
    GST_LOG_OBJECT (dec, "Loaded new symbols");
  } else {
    goto could_not_load;
  }

  lib->Init = (guint32 (*)(gpointer, gpointer)) rv_init;
  lib->Free = (guint32 (*)(gpointer)) rv_free;
  lib->Transform = (guint32 (*)(gchar *, gchar *, gpointer, gpointer, gpointer))
      rv_transform;
  lib->Message = (guint32 (*)(gpointer, gpointer)) rv_custom_msg;
  lib->module = module;

  dec->error_count = 0;

  return TRUE;

unknown_version:
  {
    GST_ERROR_OBJECT (dec, "Cannot handle version %i.", version);
    return FALSE;
  }
could_not_load:
  {
    close_library (dec, lib);
    GST_ERROR_OBJECT (dec, "Could not load all symbols: %s", g_module_error ());
    return FALSE;
  }
}

static void
close_library (GstRealVideoDec * dec, GstRVDecLibrary * lib)
{
  if (lib->context) {
    GST_LOG_OBJECT (dec, "closing library");
    if (lib->Free)
      lib->Free (lib->context);
  }
  if (lib->module) {
    GST_LOG_OBJECT (dec, "closing library module");
    g_module_close (lib->module);
    lib->module = NULL;
  }
  memset (lib, 0, sizeof (*lib));
}

static void
gst_real_video_dec_probe_modules (GstRealVideoDec * dec)
{
  GstRVDecLibrary dummy = { NULL };

  if ((dec->valid_rv20 =
          open_library (dec, GST_REAL_VIDEO_DEC_VERSION_2, &dummy)))
    close_library (dec, &dummy);
  if ((dec->valid_rv30 =
          open_library (dec, GST_REAL_VIDEO_DEC_VERSION_3, &dummy)))
    close_library (dec, &dummy);
  if ((dec->valid_rv40 =
          open_library (dec, GST_REAL_VIDEO_DEC_VERSION_4, &dummy)))
    close_library (dec, &dummy);
}

static GstStateChangeReturn
gst_real_video_dec_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      gst_real_video_dec_probe_modules (dec);
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
gst_real_video_dec_init (GstRealVideoDec * dec, GstRealVideoDecClass * klass)
{
  dec->snk = gst_pad_new_from_static_template (&snk_t, "sink");
  gst_pad_set_getcaps_function (dec->snk,
      GST_DEBUG_FUNCPTR (gst_real_video_dec_getcaps));
  gst_pad_set_setcaps_function (dec->snk,
      GST_DEBUG_FUNCPTR (gst_real_video_dec_setcaps));
  gst_pad_set_chain_function (dec->snk,
      GST_DEBUG_FUNCPTR (gst_real_video_dec_chain));
  gst_element_add_pad (GST_ELEMENT (dec), dec->snk);

  dec->src = gst_pad_new_from_static_template (&src_t, "src");
  gst_pad_use_fixed_caps (dec->src);
  gst_element_add_pad (GST_ELEMENT (dec), dec->src);

  dec->max_errors = DEFAULT_MAX_ERRORS;
  dec->error_count = 0;
}

static void
gst_real_video_dec_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&snk_t));
  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&src_t));
  gst_element_class_set_static_metadata (ec, "RealVideo decoder",
      "Codec/Decoder/Video", "Decoder for RealVideo streams",
      "Lutz Mueller <lutz@topfrose.de>");
}

static void
gst_real_video_dec_finalize (GObject * object)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (object);

  close_library (dec, &dec->lib);

  if (dec->real_codecs_path) {
    g_free (dec->real_codecs_path);
    dec->real_codecs_path = NULL;
  }
  if (dec->rv20_names) {
    g_free (dec->rv20_names);
    dec->rv20_names = NULL;
  }
  if (dec->rv30_names) {
    g_free (dec->rv30_names);
    dec->rv30_names = NULL;
  }
  if (dec->rv40_names) {
    g_free (dec->rv40_names);
    dec->rv40_names = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_real_video_dec_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (object);

  /* Changing the location of the .so supposes it's not being done
   * in a state greater than READY !
   */

  switch (prop_id) {
    case PROP_REAL_CODECS_PATH:
      if (dec->real_codecs_path)
        g_free (dec->real_codecs_path);
      dec->real_codecs_path = g_value_dup_string (value);
      break;
    case PROP_RV20_NAMES:
      if (dec->rv20_names)
        g_free (dec->rv20_names);
      dec->rv20_names = g_value_dup_string (value);
      break;
    case PROP_RV30_NAMES:
      if (dec->rv30_names)
        g_free (dec->rv30_names);
      dec->rv30_names = g_value_dup_string (value);
      break;
    case PROP_RV40_NAMES:
      if (dec->rv40_names)
        g_free (dec->rv40_names);
      dec->rv40_names = g_value_dup_string (value);
      break;
    case PROP_MAX_ERRORS:
      dec->max_errors = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_real_video_dec_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (object);

  switch (prop_id) {
    case PROP_REAL_CODECS_PATH:
      g_value_set_string (value, dec->real_codecs_path ? dec->real_codecs_path
          : DEFAULT_REAL_CODECS_PATH);
      break;
    case PROP_RV20_NAMES:
      g_value_set_string (value, dec->rv20_names ? dec->rv20_names :
          DEFAULT_RV20_NAMES);
      break;
    case PROP_RV30_NAMES:
      g_value_set_string (value, dec->rv30_names ? dec->rv30_names :
          DEFAULT_RV30_NAMES);
      break;
    case PROP_RV40_NAMES:
      g_value_set_string (value, dec->rv40_names ? dec->rv40_names :
          DEFAULT_RV40_NAMES);
      break;
    case PROP_MAX_ERRORS:
      g_value_set_int (value, dec->max_errors);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_real_video_dec_class_init (GstRealVideoDecClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  object_class->set_property = gst_real_video_dec_set_property;
  object_class->get_property = gst_real_video_dec_get_property;
  object_class->finalize = gst_real_video_dec_finalize;

  element_class->change_state = gst_real_video_dec_change_state;

  g_object_class_install_property (object_class, PROP_REAL_CODECS_PATH,
      g_param_spec_string ("real-codecs-path",
          "Path where to search for RealPlayer codecs",
          "Path where to search for RealPlayer codecs",
          DEFAULT_REAL_CODECS_PATH,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RV20_NAMES,
      g_param_spec_string ("rv20-names", "Names of rv20 driver",
          "Names of rv20 driver", DEFAULT_RV20_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RV30_NAMES,
      g_param_spec_string ("rv30-names", "Names of rv30 driver",
          "Names of rv30 driver", DEFAULT_RV30_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_RV40_NAMES,
      g_param_spec_string ("rv40-names", "Names of rv40 driver",
          "Names of rv40 driver", DEFAULT_RV40_NAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (object_class, PROP_MAX_ERRORS,
      g_param_spec_int ("max-errors", "Max errors",
          "Maximum number of consecutive errors (0 = unlimited)",
          0, G_MAXINT, DEFAULT_MAX_ERRORS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (realvideode_debug, "realvideodec", 0,
      "RealVideo decoder");
}
