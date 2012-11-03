/* GStreamer
 * Copyright (C) 2008 Pioneers of the Inevitable <songbird@songbirdnest.com>
 *
 * Authors: Michael Smith <msmith@songbirdnest.com>
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

#include <windows.h>
#include <mmreg.h>
#include <msacm.h>

#include <gst/gst.h>
#include <gst/riff/riff-media.h>

/* This has to be bigger than some unspecified minimum size or things
 * break; I don't understand why (4kB isn't enough). Make it nice and
 * big.
 */
#define ACM_BUFFER_SIZE (64 * 1024)
enum
{
  ARG_0,
  ARG_BITRATE
};

#define DEFAULT_BITRATE 128000

#define ACMENC_PARAMS_QDATA g_quark_from_static_string("acmenc-params")

#define GST_CAT_DEFAULT acmenc_debug
GST_DEBUG_CATEGORY_STATIC (acmenc_debug);

static GstStaticPadTemplate acmenc_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK, GST_PAD_ALWAYS, GST_STATIC_CAPS ("audio/x-raw-int, "
        "depth = (int)16, "
        "width = (int)16, "
        "endianness = (int)" G_STRINGIFY (G_BYTE_ORDER) ", "
        "signed = (boolean)TRUE, "
        "channels = (int) [1,2], " "rate = (int)[1, MAX]"));

static GstStaticPadTemplate acmenc_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstElementClass *parent_class = NULL;
typedef struct _ACMEncClass
{
  GstElementClass parent_class;
  HACMDRIVERID driverId;
} ACMEncClass;

typedef struct _ACMEnc
{
  GstElement parent;
  GstPad *sinkpad;
  GstPad *srcpad;
  gboolean is_setup;
  WAVEFORMATEX infmt;
  WAVEFORMATEX *outfmt;
  HACMDRIVER driver;
  HACMSTREAM stream;
  ACMSTREAMHEADER header;

  /* Offset into input buffer to write next data */
  int offset;

  /* Number of bytes written */
  int bytes_output;

  /* From received caps */
  int rate;
  int channels;

  /* Set in properties */
  int selected_bitrate;
  GstCaps *output_caps;
} ACMEnc;

typedef struct _ACMEncParams
{
  HACMDRIVERID driverId;
  HMODULE dll;
  gchar *name;
} ACMEncParams;

static GstCaps *
acmenc_caps_from_format (WAVEFORMATEX * fmt)
{
  return gst_riff_create_audio_caps (fmt->wFormatTag, NULL,
      (gst_riff_strf_auds *) fmt, NULL, NULL, NULL);
}

static gboolean
acmenc_set_input_format (ACMEnc * enc, WAVEFORMATEX * infmt)
{
  infmt->wFormatTag = WAVE_FORMAT_PCM;
  infmt->nChannels = enc->channels;
  infmt->nSamplesPerSec = enc->rate;
  infmt->nAvgBytesPerSec = 2 * enc->channels * enc->rate;
  infmt->nBlockAlign = 2 * enc->channels;
  infmt->wBitsPerSample = 16;
  infmt->cbSize = 0;
  return TRUE;
}

static BOOL CALLBACK
acmenc_format_enum (HACMDRIVERID driverId, LPACMFORMATDETAILS fd,
    DWORD_PTR dwInstance, DWORD fdwSupport)
{
  ACMEnc *enc = (ACMEnc *) dwInstance;
  int oldbrdiff, newbrdiff;
  gboolean oldmatch, newmatch;
  if (!enc->outfmt) {

    /* First one is always the best so far */
    enc->outfmt =
        (WAVEFORMATEX *) g_malloc (fd->pwfx->cbSize + sizeof (WAVEFORMATEX));
    memcpy (enc->outfmt, fd->pwfx, fd->pwfx->cbSize + sizeof (WAVEFORMATEX));
    return TRUE;
  }

  /* We prefer a format with exact rate/channels match,
   * and the closest bitrate match. Otherwise, we accept
   * the closest bitrate match.
   */
  newmatch = (enc->channels == fd->pwfx->nChannels) &&
      (enc->rate == fd->pwfx->nSamplesPerSec);
  oldmatch = (enc->outfmt->nChannels == enc->channels) &&
      (enc->outfmt->nSamplesPerSec == enc->rate);
  newbrdiff = abs (enc->selected_bitrate - fd->pwfx->nAvgBytesPerSec * 8);
  oldbrdiff = abs (enc->selected_bitrate - enc->outfmt->nAvgBytesPerSec * 8);
  if ((newmatch && (!oldmatch || (newbrdiff < oldbrdiff))) ||
      (!newmatch && !oldmatch && (newbrdiff < oldbrdiff))) {
    g_free (enc->outfmt);
    enc->outfmt =
        (WAVEFORMATEX *) g_malloc (fd->pwfx->cbSize + sizeof (WAVEFORMATEX));
    memcpy (enc->outfmt, fd->pwfx, fd->pwfx->cbSize + sizeof (WAVEFORMATEX));
  }

  /* Always return TRUE to continue enumeration */
  return TRUE;
}

static gboolean
acmenc_set_format (ACMEnc * enc)
{
  WAVEFORMATEX *in = NULL;
  ACMFORMATDETAILS details;
  MMRESULT res;
  int maxSize;

  /* Input is fixed */
  acmenc_set_input_format (enc, &enc->infmt);

  /* Select the closest output that matches */
  res =
      acmMetrics ((HACMOBJ) enc->driver, ACM_METRIC_MAX_SIZE_FORMAT, &maxSize);

  /* Set the format to match what we'll be providing */
  in = (WAVEFORMATEX *) g_malloc (maxSize);
  acmenc_set_input_format (enc, in);
  in->cbSize = (WORD) (maxSize - sizeof (*in));
  details.cbStruct = sizeof (details);
  details.dwFormatIndex = 0;
  details.dwFormatTag = WAVE_FORMAT_UNKNOWN;
  details.fdwSupport = 0;
  details.pwfx = in;
  details.cbwfx = maxSize;

  /* We set enc->outfmt to the closest match in the callback */
  res =
      acmFormatEnum (enc->driver, &details, acmenc_format_enum, (DWORD_PTR) enc,
      ACM_FORMATENUMF_CONVERT);
  g_free (in);
  if (res) {
    GST_WARNING_OBJECT (enc, "Failed to enumerate formats");
    return FALSE;
  }
  if (!enc->outfmt) {
    GST_WARNING_OBJECT (enc, "No compatible output for input format");
    return FALSE;
  }
  return TRUE;
}

static gboolean
acmenc_setup (ACMEnc * enc)
{
  MMRESULT res;
  ACMEncClass *encclass = (ACMEncClass *) G_OBJECT_GET_CLASS (enc);
  int destBufferSize;
  res = acmDriverOpen (&enc->driver, encclass->driverId, 0);
  if (res) {
    GST_WARNING ("Failed to open ACM driver: %d", res);
    return FALSE;
  }
  if (!acmenc_set_format (enc))
    return FALSE;
  res =
      acmStreamOpen (&enc->stream, enc->driver, &enc->infmt, enc->outfmt, 0, 0,
      0, ACM_STREAMOPENF_NONREALTIME);
  if (res) {
    GST_WARNING_OBJECT (enc, "Failed to open ACM stream");
    return FALSE;
  }
  enc->header.cbStruct = sizeof (ACMSTREAMHEADER);
  enc->header.fdwStatus = 0;
  enc->header.dwUser = 0;
  enc->header.pbSrc = (BYTE *) g_malloc (ACM_BUFFER_SIZE);
  enc->header.cbSrcLength = ACM_BUFFER_SIZE;
  enc->header.cbSrcLengthUsed = 0;
  enc->header.dwSrcUser = 0;

  /* Ask what buffer size we need to use for our output */
  acmStreamSize (enc->stream, ACM_BUFFER_SIZE, (LPDWORD) & destBufferSize,
      ACM_STREAMSIZEF_SOURCE);
  enc->header.pbDst = (BYTE *) g_malloc (destBufferSize);
  enc->header.cbDstLength = destBufferSize;
  enc->header.cbDstLengthUsed = 0;
  enc->header.dwDstUser = 0;
  res = acmStreamPrepareHeader (enc->stream, &enc->header, 0);
  if (res) {
    GST_WARNING_OBJECT (enc, "Failed to prepare ACM stream");
    return FALSE;
  }
  enc->output_caps = acmenc_caps_from_format (enc->outfmt);
  if (enc->output_caps) {
    gst_pad_set_caps (enc->srcpad, enc->output_caps);
  }
  enc->is_setup = TRUE;
  return TRUE;
}

static void
acmenc_teardown (ACMEnc * enc)
{
  if (enc->outfmt) {
    g_free (enc->outfmt);
    enc->outfmt = NULL;
  }
  if (enc->output_caps) {
    gst_caps_unref (enc->output_caps);
    enc->output_caps = NULL;
  }
  if (enc->header.pbSrc)
    g_free (enc->header.pbSrc);
  if (enc->header.pbDst)
    g_free (enc->header.pbDst);
  memset (&enc->header, 0, sizeof (enc->header));
  if (enc->stream) {
    acmStreamClose (enc->stream, 0);
    enc->stream = 0;
  }
  if (enc->driver) {
    acmDriverClose (enc->driver, 0);
    enc->driver = 0;
  }
  enc->bytes_output = 0;
  enc->offset = 0;
  enc->is_setup = FALSE;
}

static gboolean
acmenc_sink_setcaps (GstPad * pad, GstCaps * caps)
{
  ACMEnc *enc = (ACMEnc *) GST_PAD_PARENT (pad);
  GstStructure *structure;
  gboolean ret;
  structure = gst_caps_get_structure (caps, 0);
  gst_structure_get_int (structure, "channels", &enc->channels);
  gst_structure_get_int (structure, "rate", &enc->rate);
  if (enc->is_setup)
    acmenc_teardown (enc);
  ret = acmenc_setup (enc);
  return ret;
}

static GstFlowReturn
acmenc_push_output (ACMEnc * enc)
{
  GstFlowReturn ret = GST_FLOW_OK;
  if (enc->header.cbDstLengthUsed > 0) {
    GstBuffer *outbuf = gst_buffer_new_and_alloc (enc->header.cbDstLengthUsed);
    memcpy (GST_BUFFER_DATA (outbuf), enc->header.pbDst,
        enc->header.cbDstLengthUsed);
    if (enc->outfmt->nAvgBytesPerSec > 0) {

      /* We have a bitrate, so we can create a timestamp, hopefully */
      GST_BUFFER_TIMESTAMP (outbuf) =
          gst_util_uint64_scale_int (enc->bytes_output, GST_SECOND,
          enc->outfmt->nAvgBytesPerSec);
    }
    enc->bytes_output += enc->header.cbDstLengthUsed;
    GST_DEBUG_OBJECT (enc, "Pushing %lu byte encoded buffer",
        enc->header.cbDstLengthUsed);
    ret = gst_pad_push (enc->srcpad, outbuf);
  }
  return ret;
}

static GstFlowReturn
acmenc_chain (GstPad * pad, GstBuffer * buf)
{
  MMRESULT res;
  ACMEnc *enc = (ACMEnc *) GST_PAD_PARENT (pad);
  guchar *data = GST_BUFFER_DATA (buf);
  gint len = GST_BUFFER_SIZE (buf);
  int chunklen;
  GstFlowReturn ret = GST_FLOW_OK;
  while (len) {
    chunklen = MIN (len, ACM_BUFFER_SIZE - enc->offset);
    memcpy (enc->header.pbSrc + enc->offset, data, chunklen);
    enc->header.cbSrcLength = enc->offset + chunklen;
    data += chunklen;
    len -= chunklen;

    /* Now we have a buffer ready to go */
    res =
        acmStreamConvert (enc->stream, &enc->header,
        ACM_STREAMCONVERTF_BLOCKALIGN);
    if (res) {
      GST_WARNING_OBJECT (enc, "Failed to encode data");
      break;
    }
    if (enc->header.cbSrcLengthUsed > 0) {
      if (enc->header.cbSrcLengthUsed != enc->header.cbSrcLength) {

        /* Only part of input consumed */
        memmove (enc->header.pbSrc,
            enc->header.pbSrc + enc->header.cbSrcLengthUsed,
            enc->header.cbSrcLengthUsed);
        enc->offset -= enc->header.cbSrcLengthUsed;
      }

      else
        enc->offset = 0;
    }

    /* Write out any data produced */
    acmenc_push_output (enc);
  }
  return ret;
}

static GstFlowReturn
acmenc_finish_stream (ACMEnc * enc)
{
  MMRESULT res;
  GstFlowReturn ret = GST_FLOW_OK;
  int len;

  /* Ensure any remaining input data is consumed */
  len = enc->offset;
  enc->header.cbSrcLength = len;
  res =
      acmStreamConvert (enc->stream, &enc->header,
      ACM_STREAMCONVERTF_BLOCKALIGN | ACM_STREAMCONVERTF_END);
  if (res) {
    GST_WARNING_OBJECT (enc, "Failed to encode data");
    return ret;
  }
  ret = acmenc_push_output (enc);
  return ret;
}

static gboolean
acmenc_sink_event (GstPad * pad, GstEvent * event)
{
  ACMEnc *enc = (ACMEnc *) GST_PAD_PARENT (pad);
  gboolean res;
  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_EOS:
      acmenc_finish_stream (enc);
      res = gst_pad_push_event (enc->srcpad, event);
      break;
    default:
      res = gst_pad_push_event (enc->srcpad, event);
      break;
  }
  return res;
}

static void
acmenc_dispose (GObject * obj)
{
  ACMEnc *enc = (ACMEnc *) obj;
  acmenc_teardown (enc);
  G_OBJECT_CLASS (parent_class)->dispose (obj);
} static void

acmenc_init (ACMEnc * enc)
{
  enc->sinkpad =
      gst_pad_new_from_static_template (&acmenc_sink_template, "sink");
  gst_pad_set_setcaps_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (acmenc_sink_setcaps));
  gst_pad_set_chain_function (enc->sinkpad, GST_DEBUG_FUNCPTR (acmenc_chain));
  gst_pad_set_event_function (enc->sinkpad,
      GST_DEBUG_FUNCPTR (acmenc_sink_event));
  gst_element_add_pad (GST_ELEMENT (enc), enc->sinkpad);
  enc->srcpad = gst_pad_new_from_static_template (&acmenc_src_template, "src");
  gst_element_add_pad (GST_ELEMENT (enc), enc->srcpad);
  enc->selected_bitrate = DEFAULT_BITRATE;
} static void

acmenc_set_property (GObject * obj, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  ACMEnc *enc = (ACMEnc *) obj;
  switch (prop_id) {
    case ARG_BITRATE:
      enc->selected_bitrate = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
  }
}

static void
acmenc_get_property (GObject * obj, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  ACMEnc *enc = (ACMEnc *) obj;
  switch (prop_id) {
    case ARG_BITRATE:
      g_value_set_int (value, enc->selected_bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (obj, prop_id, pspec);
  }
}

static void
acmenc_class_init (ACMEncClass * klass)
{
  GObjectClass *gobjectclass = (GObjectClass *) klass;
  parent_class = (GstElementClass *) g_type_class_peek_parent (klass);
  gobjectclass->dispose = acmenc_dispose;
  gobjectclass->set_property = acmenc_set_property;
  gobjectclass->get_property = acmenc_get_property;
  g_object_class_install_property (gobjectclass, ARG_BITRATE,
      g_param_spec_int ("bitrate", "Bitrate", "Bitrate to encode at (in bps)",
          0, 1000000, DEFAULT_BITRATE, G_PARAM_READWRITE));
} static void

acmenc_base_init (ACMEncClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  ACMEncParams *params;
  ACMDRIVERDETAILS driverdetails;
  gchar *shortname, *longname, *detail, *description;
  MMRESULT res;

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&acmenc_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&acmenc_src_template));
  params =
      (ACMEncParams *) g_type_get_qdata (G_OBJECT_CLASS_TYPE (klass),
      ACMENC_PARAMS_QDATA);
  g_assert (params);
  memset (&driverdetails, 0, sizeof (driverdetails));
  driverdetails.cbStruct = sizeof (driverdetails);
  res = acmDriverDetails (params->driverId, &driverdetails, 0);
  if (res) {
    GST_WARNING ("Could not get driver details: %d", res);
  }
  shortname =
      g_utf16_to_utf8 ((gunichar2 *) driverdetails.szShortName, -1, NULL, NULL,
      NULL);
  longname =
      g_utf16_to_utf8 ((gunichar2 *) driverdetails.szLongName, -1, NULL, NULL,
      NULL);
  detail = g_strdup_printf ("ACM Encoder: %s", (shortname
          && *shortname) ? shortname : params->name);
  description = g_strdup_printf ("ACM Encoder: %s", (longname
          && *longname) ? longname : params->name);
  gst_element_class_set_static_metadata (element_class, detail,
      "Codec/Encoder/Audio", description,
      "Pioneers of the Inevitable <songbird@songbirdnest.com>");
  g_free (shortname);
  g_free (longname);
  g_free (description);
  g_free (detail);
  klass->driverId = params->driverId;
}

static ACMEncParams *
acmenc_open_driver (wchar_t * filename)
{
  HACMDRIVERID driverid = NULL;
  HMODULE mod = NULL;
  FARPROC func;
  MMRESULT res;
  ACMEncParams *params;
  mod = LoadLibraryW (filename);
  if (!mod) {
    GST_WARNING ("Failed to load ACM");
    goto done;
  }
  func = GetProcAddress (mod, "DriverProc");
  if (!func) {
    GST_WARNING ("Failed to find 'DriverProc' in ACM");
    goto done;
  }
  res =
      acmDriverAdd (&driverid, mod, (LPARAM) func, 0, ACM_DRIVERADDF_FUNCTION);
  if (res) {
    GST_WARNING ("Failed to add ACM driver: %d", res);
    goto done;
  }
  params = g_new0 (ACMEncParams, 1);
  params->dll = mod;
  params->driverId = driverid;
  return params;
done:if (driverid)
    acmDriverRemove (driverid, 0);
  if (mod)
    FreeLibrary (mod);
  return NULL;
}

static gboolean
acmenc_register_file (GstPlugin * plugin, wchar_t * filename)
{
  ACMEncParams *params;
  gchar *type_name, *name;
  GType type;
  GTypeInfo typeinfo = {
    sizeof (ACMEncClass),
    (GBaseInitFunc) acmenc_base_init, NULL,
    (GClassInitFunc) acmenc_class_init, NULL, NULL, sizeof (ACMEnc),
    0, (GInstanceInitFunc) acmenc_init,
  };
  params = acmenc_open_driver (filename);
  if (!params)
    return FALSE;

  /* Strip .acm off the end, convert to utf-8 */
  name =
      g_utf16_to_utf8 (filename, (glong) wcslen (filename) - 4, NULL, NULL,
      NULL);
  params->name = name;
  type_name = g_strdup_printf ("acmenc_%s", name);
  type = g_type_register_static (GST_TYPE_ELEMENT, type_name, &typeinfo, 0);

  /* Store params in type qdata */
  g_type_set_qdata (type, ACMENC_PARAMS_QDATA, (gpointer) params);

  /* register type */
  if (!gst_element_register (plugin, type_name, GST_RANK_NONE, type)) {
    g_warning ("Failed to register %s", type_name);;
    g_type_set_qdata (type, ACMENC_PARAMS_QDATA, NULL);
    g_free (name);
    g_free (type_name);
    g_free (params);
    return FALSE;
  }
  g_free (type_name);
  return TRUE;
}

static gboolean
acmenc_register (GstPlugin * plugin)
{
  int res;
  wchar_t dirname[1024];
  WIN32_FIND_DATAW filedata;
  HANDLE find;
  res = GetSystemDirectoryW (dirname, sizeof (dirname) / sizeof (wchar_t));
  if (!res || res > 1000) {
    GST_WARNING ("Couldn't get system directory");
    return FALSE;
  }
  wcscat (dirname, L"\\*.acm");
  find = FindFirstFileW (dirname, &filedata);
  if (find == INVALID_HANDLE_VALUE) {
    GST_WARNING ("Failed to find ACM files: %lx", GetLastError ());
    return FALSE;
  }

  do {
    char *filename =
        g_utf16_to_utf8 ((gunichar2 *) filedata.cFileName, -1, NULL, NULL,
        NULL);
    GST_INFO ("Registering ACM filter from file %s", filename);
    if (acmenc_register_file (plugin, filedata.cFileName))
      GST_INFO ("Loading filter from ACM '%s' succeeded", filename);

    else
      GST_WARNING ("Loading filter from ACM '%s' failed", filename);
    g_free (filename);
  } while (FindNextFileW (find, &filedata));
  FindClose (find);
  return TRUE;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res;
  GST_DEBUG_CATEGORY_INIT (acmenc_debug, "acmenc", 0, "ACM Encoders");
  GST_INFO ("Registering ACM encoders");
  res = acmenc_register (plugin);
  return res;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR, GST_VERSION_MINOR, acmenc,
    "ACM Encoder wrapper", plugin_init, VERSION, "LGPL", "GStreamer",
    "http://gstreamer.net/")
