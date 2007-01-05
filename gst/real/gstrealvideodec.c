/* RealVideo wrapper plugin
 *
 * Copyright (C) 2005 Lutz Mueller <lutz@topfrose.de>
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

#include "gstrealvideodec.h"

#include <dlfcn.h>
#include <string.h>

GST_DEBUG_CATEGORY_STATIC (realvideode_debug);
#define GST_CAT_DEFAULT realvideode_debug

static GstElementDetails realvideode_details =
GST_ELEMENT_DETAILS ("RealVideo decoder",
    "Codec/Decoder", "Decoder for RealVideo streams",
    "Lutz Mueller <lutz@topfrose.de>");

static GstStaticPadTemplate snk_t =
GST_STATIC_PAD_TEMPLATE ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-pn-realvideo, " "rmversion = (int) [ 2, 4 ]"));
static GstStaticPadTemplate src_t =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-yuv, "
        "format = (fourcc) I420, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 16, 4096 ], " "height = (int) [ 16, 4096 ] "));

#ifdef HAVE_CPU_I386
#define DEFAULT_PATH_RV20 "/usr/lib/win32/drv2.so.6.0"
#define DEFAULT_PATH_RV30 "/usr/lib/win32/drv3.so.6.0"
#define DEFAULT_PATH_RV40 "/usr/lib/win32/drv4.so.6.0"
#endif
#ifdef HAVE_CPU_X86_64
#define DEFAULT_PATH_RV20 "/usr/lib/drvc.so"
#define DEFAULT_PATH_RV30 "/usr/lib/drvc.so"
#define DEFAULT_PATH_RV40 "/usr/lib/drvc.so"
#endif

enum
{
  PROP_0,
  PROP_PATH_RV20,
  PROP_PATH_RV30,
  PROP_PATH_RV40
};


GST_BOILERPLATE (GstRealVideoDec, gst_real_video_dec, GstElement,
    GST_TYPE_ELEMENT);

static gboolean open_library (GstRealVideoDec * dec);
static void close_library (GstRealVideoDec * dec);

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
gst_real_video_dec_alloc_buffer (GstRealVideoDec * dec, GstClockTime timestamp,
    GstBuffer ** buf)
{
  GstFlowReturn ret;
  const guint8 *b;
  guint8 frame_type;
  guint16 seq;
  GstClockTime ts = timestamp;

  GST_LOG_OBJECT (dec, "timestamp %" GST_TIME_FORMAT, GST_TIME_ARGS (ts));

  /* Fix timestamp. */
  b = gst_adapter_peek (dec->adapter, 4);
  switch (dec->version) {
    case GST_REAL_VIDEO_DEC_VERSION_2:
    {

      /*
       * Bit  1- 2: frame type
       * Bit  3- 9: ?
       * Bit 10-22: sequence number
       * Bit 23-32: ?
       */
      frame_type = (b[0] >> 6) & 0x03;
      seq = ((b[1] & 0x7f) << 6) + ((b[2] & 0xfc) >> 2);
      break;
    }
    case GST_REAL_VIDEO_DEC_VERSION_3:
    {
      /*
       * Bit  1- 2: ?
       * Bit     3: skip packet if 1
       * Bit  4- 5: frame type
       * Bit  6-12: ?
       * Bit 13-25: sequence number
       * Bit 26-32: ?
       */
      frame_type = (b[0] >> 3) & 0x03;
      seq = ((b[1] & 0x0f) << 9) + (b[2] << 1) + ((b[3] & 0x80) >> 7);
      break;
    }
    case GST_REAL_VIDEO_DEC_VERSION_4:
    {
      /*
       * Bit     1: skip packet if 1
       * Bit  2- 3: frame type
       * Bit  4-13: ?
       * Bit 14-26: sequence number
       * Bit 27-32: ?
       */
      frame_type = (b[0] >> 5) & 0x03;
      seq = ((b[1] & 0x07) << 10) + (b[2] << 2) + ((b[3] & 0xc0) >> 6);
      break;
    }
    default:
      goto unknown_version;
  }

  GST_LOG_OBJECT (dec, "frame_type:%d", frame_type);

  switch (frame_type) {
    case 0:
    case 1:
    {
      /* I frame */
      timestamp = dec->next_ts;
      dec->last_ts = dec->next_ts;
      dec->next_ts = ts;
      dec->last_seq = dec->next_seq;
      dec->next_seq = seq;

      break;
    }
    case 2:
    {
      /* P frame */
      timestamp = dec->last_ts = dec->next_ts;
      if (seq < dec->next_seq)
        dec->next_ts += (seq + 0x2000 - dec->next_seq) * GST_MSECOND;
      else
        dec->next_ts += (seq - dec->next_seq) * GST_MSECOND;
      dec->last_seq = dec->next_seq;
      dec->next_seq = seq;
      break;
    }
    case 3:
    {
      /* B frame */
      if (seq < dec->last_seq) {
        timestamp = (seq + 0x2000 - dec->last_seq) * GST_MSECOND + dec->last_ts;
      } else {
        timestamp = (seq - dec->last_seq) * GST_MSECOND + dec->last_ts;
      }

      break;
    }
    default:
      goto unknown_frame_type;
  }

  ret = gst_pad_alloc_buffer (dec->src, GST_BUFFER_OFFSET_NONE,
      dec->width * dec->height * 3 / 2, GST_PAD_CAPS (dec->src), buf);

  if (ret == GST_FLOW_OK)
    GST_BUFFER_TIMESTAMP (*buf) = timestamp;

  return ret;

  /* Errors */
unknown_version:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE,
        ("Unknown version: %i.", dec->version), (NULL));
    return GST_FLOW_ERROR;
  }

unknown_frame_type:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Unknown frame type."), (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_real_video_dec_decode (GstRealVideoDec * dec, GstBuffer * in, guint offset)
{
  guint32 result;
  GstBuffer *out = NULL;
  GstFlowReturn ret;
  guint8 *data, hdr_subseq, hdr_seqnum;
  guint32 hdr_offset, hdr_length;
  guint16 n;
  gboolean bres;
  guint8 *buf = GST_BUFFER_DATA (in) + offset;
  guint len = GST_BUFFER_SIZE (in) - offset;
  GstClockTime timestamp = GST_BUFFER_TIMESTAMP (in);

  GST_LOG_OBJECT (dec,
      "Got buffer %p with timestamp %" GST_TIME_FORMAT " offset %d", in,
      GST_TIME_ARGS (timestamp), offset);

  /* Subsequence */
  if (len < 1)
    goto not_enough_data;
  if (*buf != 0x40 && *buf != 0x41 && *buf != 0x42 &&
      *buf != 0x43 && *buf != 0x44 && *buf != 0x45) {
    hdr_subseq = *buf & 0x7f;
    buf++;
    len--;
    if (hdr_subseq == 64)
      hdr_subseq = 1;
  } else {
    hdr_subseq = 1;
  }

  /* Length */
  if (len < 2)
    goto not_enough_data;
  hdr_length = GST_READ_UINT16_BE (buf);
  if (!(hdr_length & 0xc000)) {
    if (len < 4)
      goto not_enough_data;
    hdr_length = GST_READ_UINT32_BE (buf);
    buf += 4;
    len -= 4;
  } else {
    hdr_length &= 0x3fff;
    buf += 2;
    len -= 2;
  }

  /* Offset */
  if (len < 2)
    goto not_enough_data;
  hdr_offset = GST_READ_UINT16_BE (buf);
  if (!(hdr_offset & 0xc000)) {
    if (len < 4)
      goto not_enough_data;
    hdr_offset = GST_READ_UINT32_BE (buf);
    buf += 4;
    len -= 4;
  } else {
    hdr_offset &= 0x3fff;
    buf += 2;
    len -= 2;
  }

  /* Sequence number */
  if (len < 1)
    goto not_enough_data;
  hdr_seqnum = *buf;
  buf++;
  len--;
  if (len < 1)
    goto not_enough_data;

  /* Verify the sequence numbers. */
  if (hdr_subseq == 1) {
    n = gst_adapter_available_fast (dec->adapter);
    if (n > 0) {
      GST_DEBUG_OBJECT (dec, "Dropping data for sequence %i "
          "because we are already receiving data for sequence %i.",
          dec->seqnum, hdr_seqnum);
      gst_adapter_clear (dec->adapter);
    }
    dec->seqnum = hdr_seqnum;
    dec->length = hdr_length;
    dec->fragment_count = 1;
  } else {
    if (dec->seqnum != hdr_seqnum) {
      GST_DEBUG_OBJECT (dec, "Expected sequence %i, got sequence %i "
          "(subseq=%i). Dropping packet.", dec->seqnum, hdr_seqnum, hdr_subseq);
      return GST_FLOW_OK;
    } else if (dec->subseq + 1 != hdr_subseq) {
      GST_DEBUG_OBJECT (dec, "Expected subsequence %i, got subseqence %i. "
          "Dropping packet.", dec->subseq + 1, hdr_subseq);
      return GST_FLOW_OK;
    }
    dec->fragment_count++;
  }
  dec->subseq = hdr_subseq;

  /* Remember the offset */
  if (sizeof (dec->fragments) < 2 * (dec->fragment_count - 1) + 1)
    goto too_many_fragments;
  dec->fragments[2 * (dec->fragment_count - 1)] = 1;
  dec->fragments[2 * (dec->fragment_count - 1) + 1] =
      gst_adapter_available (dec->adapter);

  /* Some buffers need to be skipped. */
  if (((dec->version == GST_REAL_VIDEO_DEC_VERSION_3) && (*buf & 0x20)) ||
      ((dec->version == GST_REAL_VIDEO_DEC_VERSION_4) && (*buf & 0x80))) {
    dec->fragment_count--;
    dec->length -= len;
  } else {
    gst_adapter_push (dec->adapter,
        gst_buffer_create_sub (in, GST_BUFFER_SIZE (in) - len, len));
  }

  /* All bytes received? */
  n = gst_adapter_available (dec->adapter);
  GST_LOG_OBJECT (dec, "We know have %d bytes, and we need %d", n, dec->length);
  if (dec->length <= n) {
    RVInData tin;
    RVOutData tout;

    if ((ret =
            gst_real_video_dec_alloc_buffer (dec, timestamp,
                &out)) != GST_FLOW_OK)
      return ret;

    /* Decode */
    tin.datalen = dec->length;
    tin.interpolate = 0;
    tin.nfragments = dec->fragment_count - 1;
    tin.fragments = dec->fragments;
    tin.flags = 0;
    tin.timestamp = GST_BUFFER_TIMESTAMP (out);
    data = gst_adapter_take (dec->adapter, dec->length);

    result = dec->transform (
        (gchar *) data,
        (gchar *) GST_BUFFER_DATA (out), &tin, &tout, dec->context);

    g_free (data);
    if (result)
      goto could_not_transform;

    /* Check for new dimensions */
    if (tout.frames && ((dec->width != tout.width)
            || (dec->height != tout.height))) {
      GstCaps *caps = gst_caps_copy (GST_PAD_CAPS (dec->src));
      GstStructure *s = gst_caps_get_structure (caps, 0);

      GST_DEBUG_OBJECT (dec, "New dimensions: %"
          G_GUINT32_FORMAT " x %" G_GUINT32_FORMAT, tout.width, tout.height);
      gst_structure_set (s, "width", G_TYPE_LONG, tout.width,
          "height", G_TYPE_LONG, tout.height, NULL);
      bres = gst_pad_set_caps (dec->src, caps);
      gst_caps_unref (caps);
      if (!bres)
        goto new_dimensions_failed;
      dec->width = tout.width;
      dec->height = tout.height;
    }

    GST_DEBUG_OBJECT (dec,
        "Pushing out buffer with timestamp %" GST_TIME_FORMAT,
        GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (out)));

    if ((ret = gst_pad_push (dec->src, out)) != GST_FLOW_OK)
      goto could_not_push;

    n = gst_adapter_available (dec->adapter);
    if (n > 0) {
      GST_LOG_OBJECT (dec, "Data left in the adapter: %d", n);
      in = gst_adapter_take_buffer (dec->adapter, n);
      ret = gst_real_video_dec_decode (dec, in, 0);
      gst_buffer_unref (in);
      if (ret != GST_FLOW_OK)
        return ret;
    }
  }

  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Not enough data."), (NULL));
    return GST_FLOW_ERROR;
  }

too_many_fragments:
  {
    gst_buffer_unref (in);
    GST_ELEMENT_ERROR (dec, STREAM, DECODE,
        ("Got more fragments (%i) than can be handled (%i).",
            dec->fragment_count, sizeof (dec->fragments)), (NULL));
    return GST_FLOW_ERROR;
  }

could_not_transform:
  {
    gst_buffer_unref (out);
    GST_ELEMENT_ERROR (dec, STREAM, DECODE,
        ("Could not decode buffer: %" G_GUINT32_FORMAT, result), (NULL));
    return GST_FLOW_ERROR;
  }

new_dimensions_failed:
  {
    gst_buffer_unref (out);
    GST_ELEMENT_ERROR (dec, STREAM, DECODE,
        ("Could not set new dimensions."), (NULL));
    return GST_FLOW_ERROR;
  }

could_not_push:
  {
    GST_DEBUG_OBJECT (dec, "Could not push buffer: %s",
        gst_flow_get_name (ret));
    return ret;
  }
}

static GstFlowReturn
gst_real_video_dec_chain (GstPad * pad, GstBuffer * in)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (GST_PAD_PARENT (pad));
  guint8 flags, *buf = GST_BUFFER_DATA (in);
  guint len = GST_BUFFER_SIZE (in);
  GstFlowReturn ret;

  /* Flags */
  if (len < 1)
    goto not_enough_data;
  flags = *buf;
  buf++;
  len--;

  if (flags == 0x40) {
    GST_DEBUG_OBJECT (dec, "Don't know how to handle buffer of type 0x40 "
        "(size=%i).", len);
    return GST_FLOW_OK;
  }

  ret = gst_real_video_dec_decode (dec, in, 1);
  gst_buffer_unref (in);
  if (ret != GST_FLOW_OK)
    return ret;
  return GST_FLOW_OK;

  /* Errors */
not_enough_data:
  {
    GST_ELEMENT_ERROR (dec, STREAM, DECODE, ("Not enough data."), (NULL));
    gst_buffer_unref (in);
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_real_video_dec_activate_push (GstPad * pad, gboolean active)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (GST_PAD_PARENT (pad));

  gst_adapter_clear (dec->adapter);

  return TRUE;
}

static gboolean
gst_real_video_dec_setcaps (GstPad * pad, GstCaps * caps)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (GST_PAD_PARENT (pad));
  GstStructure *s = gst_caps_get_structure (caps, 0);
  gint version, res;
  gchar data[36];
  gboolean bres;
  const GValue *v;

  if (!gst_structure_get_int (s, "rmversion", &version) ||
      !gst_structure_get_int (s, "width", (gint *) & dec->width) ||
      !gst_structure_get_int (s, "height", (gint *) & dec->height) ||
      !gst_structure_get_int (s, "format", &dec->format) ||
      !gst_structure_get_int (s, "subformat", &dec->subformat) ||
      !gst_structure_get_fraction (s, "framerate", &dec->framerate_num,
          &dec->framerate_denom))
    goto missing_keys;
  dec->version = version;

  GST_LOG_OBJECT (dec, "Setting version to %d", version);

  if (!open_library (dec))
    return FALSE;

  /* Initialize REAL driver. */
  GST_WRITE_UINT16_LE (data + 0, 11);
  GST_WRITE_UINT16_LE (data + 2, dec->width);
  GST_WRITE_UINT16_LE (data + 4, dec->height);
  GST_WRITE_UINT16_LE (data + 6, 0);
  GST_WRITE_UINT32_LE (data + 8, 0);
  GST_WRITE_UINT32_LE (data + 12, dec->subformat);
  GST_WRITE_UINT32_LE (data + 16, 0);
  GST_WRITE_UINT32_LE (data + 20, dec->format);
  res = dec->init (&data, &dec->context);
  if (res)
    goto could_not_initialize;

  if ((v = gst_structure_get_value (s, "codec_data"))) {
    GstBuffer *buf;
    guint32 *msgdata;
    guint i;
    struct
    {
      guint32 type;
      guint32 msg;
      gpointer data;
      guint32 extra[6];
    } msg;

    buf = g_value_peek_pointer (v);

    GST_LOG_OBJECT (dec, "Creating custom message of length %d",
        GST_BUFFER_SIZE (buf));

    msgdata = g_new0 (guint32, GST_BUFFER_SIZE (buf) + 2);
    if (!msgdata)
      goto could_not_allocate;

    msg.type = 0x24;
    msg.msg = 1 + ((dec->subformat >> 16) & 7);
    msg.data = msgdata;
    for (i = 0; i < 6; i++)
      msg.extra[i] = 0;
    msgdata[0] = dec->width;
    msgdata[1] = dec->height;
    for (i = 0; i < GST_BUFFER_SIZE (buf); i++)
      msgdata[i + 2] = 4 * GST_BUFFER_DATA (buf)[i];

    res = dec->custom_message (&msg, dec->context);

    g_free (msgdata);
    if (res)
      goto could_not_send_message;
  }

  caps = gst_caps_new_simple ("video/x-raw-yuv",
      "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('I', '4', '2', '0'),
      "framerate", GST_TYPE_FRACTION, dec->framerate_num, dec->framerate_denom,
      "width", G_TYPE_INT, dec->width, "height", G_TYPE_INT, dec->height, NULL);
  bres = gst_pad_set_caps (GST_PAD (dec->src), caps);
  gst_caps_unref (caps);
  if (!bres)
    goto could_not_set_caps;

  return TRUE;

missing_keys:
  {
    GST_DEBUG_OBJECT (dec, "Could not find all necessary keys in structure.");
    return FALSE;
  }

could_not_initialize:
  {
    dlclose (dec->handle);
    dec->handle = NULL;
    GST_DEBUG_OBJECT (dec, "Initialization of REAL driver failed (%i).", res);
    return FALSE;
  }

could_not_allocate:
  {
    dlclose (dec->handle);
    dec->handle = NULL;
    GST_DEBUG_OBJECT (dec, "Could not allocate memory.");
    return FALSE;
  }

could_not_send_message:
  {
    dlclose (dec->handle);
    dec->handle = NULL;
    GST_DEBUG_OBJECT (dec, "Failed to send custom message needed for "
        "initialization (%i).", res);
    return FALSE;
  }

could_not_set_caps:
  {
    dlclose (dec->handle);
    dec->handle = NULL;
    GST_DEBUG_OBJECT (dec, "Could not convince peer to accept dimensions "
        "%i x %i.", dec->width, dec->height);
    return FALSE;
  }
}

/* Attempts to open the correct library for the configured version */

static gboolean
open_library (GstRealVideoDec * dec)
{
  gchar *path = NULL;

  GST_DEBUG_OBJECT (dec,
      "Attempting to open shared library for real video version %d",
      dec->version);

  /* FIXME : Search for the correct library in various places if dec->path_rv20
   *  isn't set explicitely !
   * Library names can also be different (ex : drv30.so vs drvc.so)
   */

  switch (dec->version) {
    case GST_REAL_VIDEO_DEC_VERSION_2:
    {
      if (dec->path_rv20)
        path = dec->path_rv20;
      else if (g_file_test (DEFAULT_PATH_RV20, G_FILE_TEST_EXISTS))
        path = DEFAULT_PATH_RV20;
      else if (g_file_test ("/usr/lib/drv2.so.6.0", G_FILE_TEST_EXISTS))
        path = "/usr/lib/drv2.so.6.0";
      else
        goto no_known_libraries;
      break;
    }
    case GST_REAL_VIDEO_DEC_VERSION_3:
    {
      if (dec->path_rv30)
        path = dec->path_rv30;
      else if (g_file_test (DEFAULT_PATH_RV30, G_FILE_TEST_EXISTS))
        path = DEFAULT_PATH_RV30;
      else if (g_file_test ("/usr/lib/drv3.so.6.0", G_FILE_TEST_EXISTS))
        path = "/usr/lib/drv3.so.6.0";
      else
        goto no_known_libraries;
      break;
    }
    case GST_REAL_VIDEO_DEC_VERSION_4:
    {
      if (dec->path_rv40)
        path = dec->path_rv40;
      else if (g_file_test (DEFAULT_PATH_RV40, G_FILE_TEST_EXISTS))
        path = DEFAULT_PATH_RV40;
      else if (g_file_test ("/usr/lib/drv4.so.6.0", G_FILE_TEST_EXISTS))
        path = "/usr/lib/drv4.so.6.0";
      else
        goto no_known_libraries;
      break;
    }
    default:
      goto unknown_version;
  }

  /* First close any open library */
  close_library (dec);

  dec->handle = dlopen (path, RTLD_LAZY);
  if (!dec->handle)
    goto could_not_open;

  /* First try opening legacy symbols */
  dec->custom_message = dlsym (dec->handle, "RV20toYUV420CustomMessage");
  dec->free = dlsym (dec->handle, "RV20toYUV420Free");
  dec->init = dlsym (dec->handle, "RV20toYUV420Init");
  dec->transform = dlsym (dec->handle, "RV20toYUV420Transform");

  if (!(dec->custom_message && dec->free && dec->init && dec->transform)) {
    /* Else try loading new symbols */
    dec->custom_message = dlsym (dec->handle, "RV40toYUV420CustomMessage");
    dec->free = dlsym (dec->handle, "RV40toYUV420Free");
    dec->init = dlsym (dec->handle, "RV40toYUV420Init");
    dec->transform = dlsym (dec->handle, "RV40toYUV420Transform");

    if (!(dec->custom_message && dec->free && dec->init && dec->transform))
      goto could_not_load;
  }

  return TRUE;

no_known_libraries:
  {
    GST_ELEMENT_ERROR (dec, LIBRARY, INIT,
        ("Couldn't find a realvideo shared library for version %d",
            dec->version), (NULL));
    return FALSE;
  }

unknown_version:
  {
    GST_ERROR_OBJECT (dec, "Cannot handle version %i.", dec->version);
    return FALSE;
  }

could_not_open:
  {
    GST_ERROR_OBJECT (dec, "Could not open library '%s'.", path);
    return FALSE;
  }

could_not_load:
  {
    close_library (dec);
    GST_ERROR_OBJECT (dec, "Could not load all symbols.");
    return FALSE;
  }
}

static void
close_library (GstRealVideoDec * dec)
{
  if (dec->context && dec->free) {
    dec->free (dec->context);
    dec->context = NULL;
  }

  dec->custom_message = NULL;
  dec->free = NULL;
  dec->init = NULL;
  dec->transform = NULL;

  if (dec->handle)
    dlclose (dec->handle);
  dec->handle = NULL;
}

static void
gst_real_video_dec_init (GstRealVideoDec * dec, GstRealVideoDecClass * klass)
{
  dec->snk =
      gst_pad_new_from_template (gst_static_pad_template_get (&snk_t), "sink");
  gst_pad_set_setcaps_function (dec->snk, gst_real_video_dec_setcaps);
  gst_pad_set_chain_function (dec->snk, gst_real_video_dec_chain);
  gst_pad_set_activatepush_function (dec->snk,
      gst_real_video_dec_activate_push);
  gst_element_add_pad (GST_ELEMENT (dec), dec->snk);

  dec->src =
      gst_pad_new_from_template (gst_static_pad_template_get (&src_t), "src");
  gst_element_add_pad (GST_ELEMENT (dec), dec->src);

  dec->adapter = g_object_new (GST_TYPE_ADAPTER, NULL);
}

static void
gst_real_video_dec_base_init (gpointer g_class)
{
  GstElementClass *ec = GST_ELEMENT_CLASS (g_class);

  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&snk_t));
  gst_element_class_add_pad_template (ec, gst_static_pad_template_get (&src_t));
  gst_element_class_set_details (ec, &realvideode_details);
}

static void
gst_real_video_dec_finalize (GObject * object)
{
  GstRealVideoDec *dec = GST_REAL_VIDEO_DEC (object);

  if (dec->adapter) {
    g_object_unref (G_OBJECT (dec->adapter));
    dec->adapter = NULL;
  }

  close_library (dec);

  if (dec->path_rv20) {
    g_free (dec->path_rv20);
    dec->path_rv20 = NULL;
  }
  if (dec->path_rv30) {
    g_free (dec->path_rv30);
    dec->path_rv30 = NULL;
  }
  if (dec->path_rv40) {
    g_free (dec->path_rv40);
    dec->path_rv40 = NULL;
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
    case PROP_PATH_RV20:
      if (dec->path_rv20)
        g_free (dec->path_rv20);
      dec->path_rv20 = g_value_dup_string (value);
      break;
    case PROP_PATH_RV30:
      if (dec->path_rv30)
        g_free (dec->path_rv30);
      dec->path_rv30 = g_value_dup_string (value);
      break;
    case PROP_PATH_RV40:
      if (dec->path_rv40)
        g_free (dec->path_rv40);
      dec->path_rv40 = g_value_dup_string (value);
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
    case PROP_PATH_RV20:
      g_value_set_string (value, dec->path_rv20 ? dec->path_rv20 :
          DEFAULT_PATH_RV20);
      break;
    case PROP_PATH_RV30:
      g_value_set_string (value, dec->path_rv30 ? dec->path_rv30 :
          DEFAULT_PATH_RV30);
      break;
    case PROP_PATH_RV40:
      g_value_set_string (value, dec->path_rv40 ? dec->path_rv40 :
          DEFAULT_PATH_RV40);
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

  object_class->set_property = gst_real_video_dec_set_property;
  object_class->get_property = gst_real_video_dec_get_property;
  object_class->finalize = gst_real_video_dec_finalize;

  g_object_class_install_property (object_class, PROP_PATH_RV20,
      g_param_spec_string ("path_rv20", "Path to rv20 driver",
          "Path to rv20 driver", DEFAULT_PATH_RV20, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PATH_RV30,
      g_param_spec_string ("path_rv30", "Path to rv30 driver",
          "Path to rv30 driver", DEFAULT_PATH_RV30, G_PARAM_READWRITE));
  g_object_class_install_property (object_class, PROP_PATH_RV40,
      g_param_spec_string ("path_rv40", "Path to rv40 driver",
          "Path to rv40 driver", DEFAULT_PATH_RV40, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (realvideode_debug, "realvideodec", 0,
      "RealVideo decoder");
}
