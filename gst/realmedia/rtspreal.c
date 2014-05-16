/* GStreamer
 * Copyright (C) <2005,2006> Wim Taymans <wim@fluendo.com>
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
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-rtspreal
 *
 * A RealMedia RTSP extension
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include "_stdint.h"
#include <string.h>

#include <gst/rtsp/gstrtspextension.h>

#include "realhash.h"
#include "rtspreal.h"
#include "asmrules.h"

GST_DEBUG_CATEGORY_STATIC (rtspreal_debug);
#define GST_CAT_DEFAULT (rtspreal_debug)

#define SERVER_PREFIX "RealServer"
#define DEFAULT_BANDWIDTH	"10485800"

static GstRTSPResult
rtsp_ext_real_get_transports (GstRTSPExtension * ext,
    GstRTSPLowerTrans protocols, gchar ** transport)
{
  GstRTSPReal *ctx = (GstRTSPReal *) ext;
  GString *str;

  if (!ctx->isreal)
    return GST_RTSP_OK;

  GST_DEBUG_OBJECT (ext, "generating transports for %d", protocols);

  str = g_string_new ("");

  /*
     if (protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST) {
     g_string_append (str, "x-real-rdt/mcast;client_port=%%u1;mode=play,");
     }
     if (protocols & GST_RTSP_LOWER_TRANS_UDP) {
     g_string_append (str, "x-real-rdt/udp;client_port=%%u1;mode=play,");
     g_string_append (str, "x-pn-tng/udp;client_port=%%u1;mode=play,");
     }
   */
  if (protocols & GST_RTSP_LOWER_TRANS_TCP) {
    g_string_append (str, "x-real-rdt/tcp;mode=play,");
    g_string_append (str, "x-pn-tng/tcp;mode=play,");
  }

  /* if we added something, remove trailing ',' */
  if (str->len > 0)
    g_string_truncate (str, str->len - 1);

  *transport = g_string_free (str, FALSE);

  return GST_RTSP_OK;
}

static GstRTSPResult
rtsp_ext_real_before_send (GstRTSPExtension * ext, GstRTSPMessage * request)
{
  GstRTSPReal *ctx = (GstRTSPReal *) ext;

  switch (request->type_data.request.method) {
    case GST_RTSP_OPTIONS:
    {
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_USER_AGENT,
          //"RealMedia Player (" GST_PACKAGE_NAME ")");
          "RealMedia Player Version 6.0.9.1235 (linux-2.0-libc6-i386-gcc2.95)");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_CLIENT_CHALLENGE,
          "9e26d33f2984236010ef6253fb1887f7");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_COMPANY_ID,
          "KnKV4M4I/B2FjJ1TToLycw==");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_GUID,
          "00000000-0000-0000-0000-000000000000");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_REGION_DATA, "0");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_PLAYER_START_TIME,
          "[28/03/2003:22:50:23 00:00]");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_CLIENT_ID,
          "Linux_2.4_6.0.9.1235_play32_RN01_EN_586");
      ctx->isreal = FALSE;
      break;
    }
    case GST_RTSP_DESCRIBE:
    {
      if (ctx->isreal) {
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_BANDWIDTH,
            DEFAULT_BANDWIDTH);
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_GUID,
            "00000000-0000-0000-0000-000000000000");
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_REGION_DATA, "0");
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_CLIENT_ID,
            "Linux_2.4_6.0.9.1235_play32_RN01_EN_586");
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_MAX_ASM_WIDTH, "1");
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_LANGUAGE, "en-US");
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_REQUIRE,
            "com.real.retain-entity-for-setup");
      }
      break;
    }
    case GST_RTSP_SETUP:
    {
      if (ctx->isreal) {
        gchar *value =
            g_strdup_printf ("%s, sd=%s", ctx->challenge2, ctx->checksum);
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_REAL_CHALLENGE2,
            value);
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_IF_MATCH, ctx->etag);
        g_free (value);
      }
      break;
    }
    default:
      break;
  }
  return GST_RTSP_OK;
}

static GstRTSPResult
rtsp_ext_real_after_send (GstRTSPExtension * ext, GstRTSPMessage * req,
    GstRTSPMessage * resp)
{
  GstRTSPReal *ctx = (GstRTSPReal *) ext;

  switch (req->type_data.request.method) {
    case GST_RTSP_OPTIONS:
    {
      gchar *challenge1 = NULL;
      gchar *server = NULL;

      gst_rtsp_message_get_header (resp, GST_RTSP_HDR_SERVER, &server, 0);

      gst_rtsp_message_get_header (resp, GST_RTSP_HDR_REAL_CHALLENGE1,
          &challenge1, 0);
      if (!challenge1)
        goto no_challenge1;

      gst_rtsp_ext_real_calc_response_and_checksum (ctx->challenge2,
          ctx->checksum, challenge1);

      GST_DEBUG_OBJECT (ctx, "Found Real challenge tag");
      ctx->isreal = TRUE;
      break;
    }
    case GST_RTSP_DESCRIBE:
    {
      gchar *etag = NULL;
      guint len;

      gst_rtsp_message_get_header (resp, GST_RTSP_HDR_ETAG, &etag, 0);
      if (etag) {
        len = sizeof (ctx->etag);
        strncpy (ctx->etag, etag, len);
        ctx->etag[len - 1] = '\0';
      }
      break;
    }
    default:
      break;
  }
  return GST_RTSP_OK;

  /* ERRORS */
no_challenge1:
  {
    GST_DEBUG_OBJECT (ctx, "Could not find challenge tag.");
    ctx->isreal = FALSE;
    return GST_RTSP_OK;
  }
}

#define ENSURE_SIZE(size)              \
G_STMT_START {			       \
  while (data_len < size) {            \
    data_len += 1024;                  \
    data = g_realloc (data, data_len); \
  }                                    \
} G_STMT_END

#define READ_BUFFER_GEN(src, func, name, dest, dest_len)    \
G_STMT_START {			                            \
  dest = (gchar *)func (src, name);                         \
  dest_len = 0;						    \
  if (!dest) {                                              \
    dest = (char *) "";                                     \
  }                                                         \
  else if (!strncmp (dest, "buffer;\"", 8)) {               \
    dest += 8;                                              \
    dest_len = strlen (dest) - 1;                           \
    dest[dest_len] = '\0';                                  \
    g_base64_decode_inplace (dest, &dest_len);            \
  }                                                         \
} G_STMT_END

#define READ_BUFFER(sdp, name, dest, dest_len)        \
 READ_BUFFER_GEN(sdp, gst_sdp_message_get_attribute_val, name, dest, dest_len)
#define READ_BUFFER_M(media, name, dest, dest_len)    \
 READ_BUFFER_GEN(media, gst_sdp_media_get_attribute_val, name, dest, dest_len)

#define READ_INT_GEN(src, func, name, dest)               \
G_STMT_START {			                          \
  const gchar *val = func (src, name);                          \
  if (val && !strncmp (val, "integer;", 8))               \
      dest = atoi (val + 8);                              \
    else                                                  \
      dest = 0;                                           \
} G_STMT_END

#define READ_INT(sdp, name, dest)                             \
 READ_INT_GEN(sdp, gst_sdp_message_get_attribute_val, name, dest)
#define READ_INT_M(media, name, dest)                         \
 READ_INT_GEN(media, gst_sdp_media_get_attribute_val, name, dest)

#define READ_STRING(media, name, dest, dest_len)              \
G_STMT_START {			                              \
  const gchar *val = gst_sdp_media_get_attribute_val (media, name); \
  if (val && !strncmp (val, "string;\"", 8)) {                \
    dest = (gchar *) val + 8;                                 \
    dest_len = strlen (dest) - 1;                             \
    dest[dest_len] = '\0';                                    \
  } else {                                                    \
    dest = (char *) "";                                       \
    dest_len = 0;                                             \
  }                                                           \
} G_STMT_END

#define WRITE_STRING1(datap, str, str_len)            \
G_STMT_START {			                      \
  *datap = str_len;                                   \
  memcpy ((datap) + 1, str, str_len);                 \
  datap += str_len + 1;                               \
} G_STMT_END

#define WRITE_STRING2(datap, str, str_len)            \
G_STMT_START {			                      \
  GST_WRITE_UINT16_BE (datap, str_len);               \
  memcpy (datap + 2, str, str_len);                   \
  datap += str_len + 2;                               \
} G_STMT_END

static GstRTSPResult
rtsp_ext_real_parse_sdp (GstRTSPExtension * ext, GstSDPMessage * sdp,
    GstStructure * props)
{
  GstRTSPReal *ctx = (GstRTSPReal *) ext;
  guint size;
  gint i;
  gchar *title, *author, *copyright, *comment;
  gsize title_len, author_len, copyright_len, comment_len;
  guint8 *data = NULL, *datap;
  guint data_len = 0, offset;
  GstBuffer *buf;
  gchar *opaque_data;
  gsize opaque_data_len, asm_rule_book_len;
  GHashTable *vars;
  GString *rules;

  /* don't bother for non-real formats */
  READ_INT (sdp, "IsRealDataType", ctx->isreal);
  if (!ctx->isreal)
    return TRUE;

  /* Force PAUSE | PLAY */
  //src->methods |= GST_RTSP_PLAY | GST_RTSP_PAUSE;

  ctx->n_streams = gst_sdp_message_medias_len (sdp);

  ctx->max_bit_rate = 0;
  ctx->avg_bit_rate = 0;
  ctx->max_packet_size = 0;
  ctx->avg_packet_size = 0;
  ctx->duration = 0;

  for (i = 0; i < ctx->n_streams; i++) {
    const GstSDPMedia *media;
    gint intval;

    media = gst_sdp_message_get_media (sdp, i);

    READ_INT_M (media, "MaxBitRate", intval);
    ctx->max_bit_rate += intval;
    READ_INT_M (media, "AvgBitRate", intval);
    ctx->avg_bit_rate += intval;
    READ_INT_M (media, "MaxPacketSize", intval);
    ctx->max_packet_size = MAX (ctx->max_packet_size, intval);
    READ_INT_M (media, "AvgPacketSize", intval);
    ctx->avg_packet_size = (ctx->avg_packet_size * i + intval) / (i + 1);
    READ_INT_M (media, "Duration", intval);
    ctx->duration = MAX (ctx->duration, intval);
  }

  /* FIXME: use GstByteWriter to write the header */
  /* PROP */
  offset = 0;
  size = 50;
  ENSURE_SIZE (size);
  datap = data + offset;

  memcpy (datap + 0, "PROP", 4);
  GST_WRITE_UINT32_BE (datap + 4, size);
  GST_WRITE_UINT16_BE (datap + 8, 0);
  GST_WRITE_UINT32_BE (datap + 10, ctx->max_bit_rate);
  GST_WRITE_UINT32_BE (datap + 14, ctx->avg_bit_rate);
  GST_WRITE_UINT32_BE (datap + 18, ctx->max_packet_size);
  GST_WRITE_UINT32_BE (datap + 22, ctx->avg_packet_size);
  GST_WRITE_UINT32_BE (datap + 26, 0);
  GST_WRITE_UINT32_BE (datap + 30, ctx->duration);
  GST_WRITE_UINT32_BE (datap + 34, 0);
  GST_WRITE_UINT32_BE (datap + 38, 0);
  GST_WRITE_UINT32_BE (datap + 42, 0);
  GST_WRITE_UINT16_BE (datap + 46, ctx->n_streams);
  GST_WRITE_UINT16_BE (datap + 48, 0);
  offset += size;

  /* CONT */
  READ_BUFFER (sdp, "Title", title, title_len);
  READ_BUFFER (sdp, "Author", author, author_len);
  READ_BUFFER (sdp, "Comment", comment, comment_len);
  READ_BUFFER (sdp, "Copyright", copyright, copyright_len);

  size = 18 + title_len + author_len + comment_len + copyright_len;
  ENSURE_SIZE (offset + size);
  datap = data + offset;

  memcpy (datap, "CONT", 4);
  GST_WRITE_UINT32_BE (datap + 4, size);
  GST_WRITE_UINT16_BE (datap + 8, 0);   /* Version */
  datap += 10;
  WRITE_STRING2 (datap, title, title_len);
  WRITE_STRING2 (datap, author, author_len);
  WRITE_STRING2 (datap, copyright, copyright_len);
  WRITE_STRING2 (datap, comment, comment_len);
  offset += size;

  /* fix the hashtale for the rule parser */
  rules = g_string_new ("");
  vars = g_hash_table_new (g_str_hash, g_str_equal);
  g_hash_table_insert (vars, (gchar *) "Bandwidth",
      (gchar *) DEFAULT_BANDWIDTH);

  /* MDPR */
  for (i = 0; i < ctx->n_streams; i++) {
    const GstSDPMedia *media;
    guint32 len;
    GstRTSPRealStream *stream;
    gchar *str;
    gint rulematches[MAX_RULEMATCHES];
    gint sel, j, n;

    media = gst_sdp_message_get_media (sdp, i);

    if (media->media && !strcmp (media->media, "data"))
      continue;

    stream = g_new0 (GstRTSPRealStream, 1);
    ctx->streams = g_list_append (ctx->streams, stream);

    READ_INT_M (media, "MaxBitRate", stream->max_bit_rate);
    READ_INT_M (media, "AvgBitRate", stream->avg_bit_rate);
    READ_INT_M (media, "MaxPacketSize", stream->max_packet_size);
    READ_INT_M (media, "AvgPacketSize", stream->avg_packet_size);
    READ_INT_M (media, "StartTime", stream->start_time);
    READ_INT_M (media, "Preroll", stream->preroll);
    READ_INT_M (media, "Duration", stream->duration);
    READ_STRING (media, "StreamName", str, stream->stream_name_len);
    stream->stream_name = g_strndup (str, stream->stream_name_len);
    READ_STRING (media, "mimetype", str, stream->mime_type_len);
    stream->mime_type = g_strndup (str, stream->mime_type_len);

    /* FIXME: Depending on the current bandwidth, we need to select one
     * bandwith out of a list offered by the server. Someone needs to write
     * a parser for strings like
     *
     * #($Bandwidth < 67959),TimestampDelivery=T,DropByN=T,priority=9;
     * #($Bandwidth >= 67959) && ($Bandwidth < 167959),AverageBandwidth=67959,
     * Priority=9;#($Bandwidth >= 67959) && ($Bandwidth < 167959),
     * AverageBandwidth=0,Priority=5,OnDepend=\"1\";
     * #($Bandwidth >= 167959) && ($Bandwidth < 267959),
     * AverageBandwidth=167959,Priority=9;
     * #($Bandwidth >= 167959) && ($Bandwidth < 267959),AverageBandwidth=0,
     * Priority=5,OnDepend=\"3\";#($Bandwidth >= 267959),
     * AverageBandwidth=267959,Priority=9;#($Bandwidth >= 267959),
     * AverageBandwidth=0,Priority=5,OnDepend=\"5\";
     *
     * As I don't know how to do that, I just use the first entry (sel = 0).
     * But to give you a starting point, I offer you above string
     * in the variable 'asm_rule_book'.
     */
    READ_STRING (media, "ASMRuleBook", str, asm_rule_book_len);
    stream->rulebook = gst_asm_rule_book_new (str);

    n = gst_asm_rule_book_match (stream->rulebook, vars, rulematches);
    for (j = 0; j < n; j++) {
      g_string_append_printf (rules, "stream=%u;rule=%u,", i, rulematches[j]);
    }

    /* get the MLTI for the first matched rules */
    sel = rulematches[0];

    READ_BUFFER_M (media, "OpaqueData", opaque_data, opaque_data_len);

    if (opaque_data_len < 4) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < 4",
          opaque_data_len);
      goto strange_opaque_data;
    }
    if (strncmp (opaque_data, "MLTI", 4)) {
      GST_DEBUG_OBJECT (ctx, "no MLTI found, appending all");
      stream->type_specific_data_len = opaque_data_len;
      stream->type_specific_data = g_memdup (opaque_data, opaque_data_len);
      goto no_type_specific;
    }
    opaque_data += 4;
    opaque_data_len -= 4;

    if (opaque_data_len < 2) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < 2",
          opaque_data_len);
      goto strange_opaque_data;
    }
    stream->num_rules = GST_READ_UINT16_BE (opaque_data);
    opaque_data += 2;
    opaque_data_len -= 2;

    if (sel >= stream->num_rules) {
      GST_DEBUG_OBJECT (ctx, "sel %d >= num_rules %d", sel, stream->num_rules);
      goto strange_opaque_data;
    }

    if (opaque_data_len < 2 * sel) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT
          " < 2 * sel (%d)", opaque_data_len, 2 * sel);
      goto strange_opaque_data;
    }
    opaque_data += 2 * sel;
    opaque_data_len -= 2 * sel;

    if (opaque_data_len < 2) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < 2",
          opaque_data_len);
      goto strange_opaque_data;
    }
    stream->codec = GST_READ_UINT16_BE (opaque_data);
    opaque_data += 2;
    opaque_data_len -= 2;

    if (opaque_data_len < 2 * (stream->num_rules - sel - 1)) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT
          " < %d", opaque_data_len, 2 * (stream->num_rules - sel - 1));
      goto strange_opaque_data;
    }
    opaque_data += 2 * (stream->num_rules - sel - 1);
    opaque_data_len -= 2 * (stream->num_rules - sel - 1);

    if (opaque_data_len < 2) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < 2",
          opaque_data_len);
      goto strange_opaque_data;
    }
    stream->num_rules = GST_READ_UINT16_BE (opaque_data);
    opaque_data += 2;
    opaque_data_len -= 2;

    if (stream->codec > stream->num_rules) {
      GST_DEBUG_OBJECT (ctx, "codec %d > num_rules %d", stream->codec,
          stream->num_rules);
      goto strange_opaque_data;
    }

    for (j = 0; j < stream->codec; j++) {
      if (opaque_data_len < 4) {
        GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < 4",
            opaque_data_len);
        goto strange_opaque_data;
      }
      len = GST_READ_UINT32_BE (opaque_data);
      opaque_data += 4;
      opaque_data_len -= 4;

      if (opaque_data_len < len) {
        GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < len %d",
            opaque_data_len, len);
        goto strange_opaque_data;
      }
      opaque_data += len;
      opaque_data_len -= len;
    }

    if (opaque_data_len < 4) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < 4",
          opaque_data_len);
      goto strange_opaque_data;
    }
    stream->type_specific_data_len = GST_READ_UINT32_BE (opaque_data);
    opaque_data += 4;
    opaque_data_len -= 4;

    if (opaque_data_len < stream->type_specific_data_len) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %" G_GSIZE_FORMAT " < %d",
          opaque_data_len, stream->type_specific_data_len);
      goto strange_opaque_data;
    }
    stream->type_specific_data =
        g_memdup (opaque_data, stream->type_specific_data_len);

  no_type_specific:
    size =
        46 + stream->stream_name_len + stream->mime_type_len +
        stream->type_specific_data_len;
    ENSURE_SIZE (offset + size);
    datap = data + offset;

    memcpy (datap, "MDPR", 4);
    GST_WRITE_UINT32_BE (datap + 4, size);
    GST_WRITE_UINT16_BE (datap + 8, 0);
    GST_WRITE_UINT16_BE (datap + 10, i);
    GST_WRITE_UINT32_BE (datap + 12, stream->max_bit_rate);
    GST_WRITE_UINT32_BE (datap + 16, stream->avg_bit_rate);
    GST_WRITE_UINT32_BE (datap + 20, stream->max_packet_size);
    GST_WRITE_UINT32_BE (datap + 24, stream->avg_packet_size);
    GST_WRITE_UINT32_BE (datap + 28, stream->start_time);
    GST_WRITE_UINT32_BE (datap + 32, stream->preroll);
    GST_WRITE_UINT32_BE (datap + 36, stream->duration);
    datap += 40;
    WRITE_STRING1 (datap, stream->stream_name, stream->stream_name_len);
    WRITE_STRING1 (datap, stream->mime_type, stream->mime_type_len);
    GST_WRITE_UINT32_BE (datap, stream->type_specific_data_len);
    if (stream->type_specific_data_len)
      memcpy (datap + 4, stream->type_specific_data,
          stream->type_specific_data_len);
    offset += size;
  }

  /* destroy the rulebook hashtable now */
  g_hash_table_destroy (vars);

  /* strip final , if we added some stream rules */
  if (rules->len > 0) {
    rules = g_string_truncate (rules, rules->len - 1);
  }

  /* and store rules in the context */
  ctx->rules = g_string_free (rules, FALSE);

  /* DATA */
  size = 18;
  ENSURE_SIZE (offset + size);
  datap = data + offset;

  memcpy (datap, "DATA", 4);
  GST_WRITE_UINT32_BE (datap + 4, size);
  GST_WRITE_UINT16_BE (datap + 8, 0);
  GST_WRITE_UINT32_BE (datap + 10, 0);  /* number of packets */
  GST_WRITE_UINT32_BE (datap + 14, 0);  /* next data header */
  offset += size;

  buf = gst_buffer_new_wrapped (data, offset);

  /* Set on caps */
  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_HEADER);
  gst_structure_set (props, "config", GST_TYPE_BUFFER, buf, NULL);
  gst_buffer_unref (buf);

  /* Overwrite encoding and media fields */
  gst_structure_set (props, "encoding-name", G_TYPE_STRING, "X-REAL-RDT", NULL);
  gst_structure_set (props, "media", G_TYPE_STRING, "application", NULL);

  return TRUE;

  /* ERRORS */
strange_opaque_data:
  {
    g_string_free (rules, TRUE);
    g_hash_table_destroy (vars);
    g_free (data);

    GST_ELEMENT_ERROR (ctx, RESOURCE, WRITE, ("Strange opaque data."), (NULL));
    return FALSE;
  }
}

static GstRTSPResult
rtsp_ext_real_stream_select (GstRTSPExtension * ext, GstRTSPUrl * url)
{
  GstRTSPReal *ctx = (GstRTSPReal *) ext;
  GstRTSPResult res;
  GstRTSPMessage request = { 0 };
  GstRTSPMessage response = { 0 };
  gchar *req_url;

  if (!ctx->isreal)
    return GST_RTSP_OK;

  if (!ctx->rules)
    return GST_RTSP_OK;

  req_url = gst_rtsp_url_get_request_uri (url);

  /* create SET_PARAMETER */
  if ((res = gst_rtsp_message_init_request (&request, GST_RTSP_SET_PARAMETER,
              req_url)) < 0)
    goto create_request_failed;

  g_free (req_url);

  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SUBSCRIBE, ctx->rules);

  /* send SET_PARAMETER */
  if ((res = gst_rtsp_extension_send (ext, &request, &response)) < 0)
    goto send_error;

  gst_rtsp_message_unset (&request);
  gst_rtsp_message_unset (&response);

  return GST_RTSP_OK;

  /* ERRORS */
create_request_failed:
  {
    GST_ELEMENT_ERROR (ctx, LIBRARY, INIT,
        ("Could not create request."), (NULL));
    goto reset;
  }
send_error:
  {
    GST_ELEMENT_ERROR (ctx, RESOURCE, WRITE,
        ("Could not send message."), (NULL));
    goto reset;
  }
reset:
  {
    gst_rtsp_message_unset (&request);
    gst_rtsp_message_unset (&response);
    return res;
  }
}

static void gst_rtsp_real_extension_init (gpointer g_iface,
    gpointer iface_data);
static void gst_rtsp_real_finalize (GObject * obj);

#define gst_rtsp_real_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstRTSPReal, gst_rtsp_real, GST_TYPE_ELEMENT,
    G_IMPLEMENT_INTERFACE (GST_TYPE_RTSP_EXTENSION,
        gst_rtsp_real_extension_init));

static void
gst_rtsp_real_class_init (GstRTSPRealClass * g_class)
{
  GObjectClass *gobject_class = (GObjectClass *) g_class;
  GstElementClass *gstelement_class = (GstElementClass *) g_class;

  gobject_class->finalize = gst_rtsp_real_finalize;

  gst_element_class_set_static_metadata (gstelement_class,
      "RealMedia RTSP Extension", "Network/Extension/Protocol",
      "Extends RTSP so that it can handle RealMedia setup",
      "Wim Taymans <wim.taymans@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (rtspreal_debug, "rtspreal", 0,
      "RealMedia RTSP extension");
}

static void
gst_rtsp_real_init (GstRTSPReal * rtspreal)
{
  rtspreal->isreal = FALSE;
}

static void
gst_rtsp_stream_free (GstRTSPRealStream * stream)
{
  g_free (stream->stream_name);
  g_free (stream->mime_type);
  gst_asm_rule_book_free (stream->rulebook);
  g_free (stream->type_specific_data);

  g_free (stream);
}

static void
gst_rtsp_real_finalize (GObject * obj)
{
  GstRTSPReal *r = (GstRTSPReal *) obj;

  g_list_foreach (r->streams, (GFunc) gst_rtsp_stream_free, NULL);
  g_list_free (r->streams);
  g_free (r->rules);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_rtsp_real_extension_init (gpointer g_iface, gpointer iface_data)
{
  GstRTSPExtensionInterface *iface = (GstRTSPExtensionInterface *) g_iface;

  iface->before_send = rtsp_ext_real_before_send;
  iface->after_send = rtsp_ext_real_after_send;
  iface->parse_sdp = rtsp_ext_real_parse_sdp;
  iface->stream_select = rtsp_ext_real_stream_select;
  iface->get_transports = rtsp_ext_real_get_transports;
}

gboolean
gst_rtsp_real_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtspreal",
      GST_RANK_MARGINAL, GST_TYPE_RTSP_REAL);
}
