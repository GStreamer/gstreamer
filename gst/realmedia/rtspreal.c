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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */
/* Element-Checklist-Version: 5 */

/**
 * SECTION:element-rtspreal
 *
 * <refsect2>
 * <para>
 * A RealMedia RTSP extension
 * </para>
 * </refsect2>
 *
 * Last reviewed on 2007-07-25 (0.10.14)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <string.h>

#include <gst/interfaces/rtspextension.h>
#include <gst/rtsp/gstrtspbase64.h>

#include "rtspreal.h"

GST_DEBUG_CATEGORY_STATIC (rtspreal_debug);
#define GST_CAT_DEFAULT (rtspreal_debug)

/* elementfactory information */
static const GstElementDetails rtspreal_details =
GST_ELEMENT_DETAILS ("RealMedia RTSP Extension",
    "Network/Extension/Protocol",
    "Extends RTSP so that it can handle RealMedia setup",
    "Wim Taymans <wim.taymans@gmail.com>");

#define SERVER_PREFIX "RealServer"

static void rtsp_ext_real_calc_response_and_checksum (char *response,
    char *chksum, char *challenge);

static GstRTSPResult
rtsp_ext_real_get_transports (GstRTSPExtension * ext,
    GstRTSPLowerTrans protocols, gchar ** transport)
{
  GstRTSPReal *ctx = (GstRTSPReal *) ext;
  GString *str;

  if (!ctx->isreal)
    return GST_RTSP_OK;

  str = g_string_new ("");

  if (protocols & GST_RTSP_LOWER_TRANS_UDP_MCAST) {
    g_string_append (str, "x-real-rdt/mcast;client_port=%%u1;mode=play,");
  }
  if (protocols & GST_RTSP_LOWER_TRANS_UDP) {
    g_string_append (str, "x-real-rdt/udp;client_port=%%u1;mode=play,");
    g_string_append (str, "x-pn-tng/udp;client_port=%%u1;mode=play,");
  }
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
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_BANDWIDTH, "10485800");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_GUID,
          "00000000-0000-0000-0000-000000000000");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_REGION_DATA, "0");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_CLIENT_ID,
          "Linux_2.4_6.0.9.1235_play32_RN01_EN_586");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_MAX_ASM_WIDTH, "1");
      gst_rtsp_message_add_header (request, GST_RTSP_HDR_LANGUAGE, "en-US");
      break;
    }
    case GST_RTSP_SETUP:
    {
      if (ctx->isreal) {
        gchar *value =
            g_strdup_printf ("%s, sd=%s", ctx->challenge2, ctx->checksum);
        gst_rtsp_message_add_header (request, GST_RTSP_HDR_REAL_CHALLENGE2,
            value);
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

      rtsp_ext_real_calc_response_and_checksum (ctx->challenge2,
          ctx->checksum, challenge1);
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

/*
 * The following code has been copied from
 * xine-lib-1.1.1/src/input/libreal/real.c.
 */

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define be2me_32(x) GUINT32_SWAP_LE_BE(x)
#define le2me_32(x) (x)
#else
#define le2me_32(x) GUINT32_SWAP_LE_BE(x)
#define be2me_32(x) (x)
#endif

#define ALE_32(x) (le2me_32(*(uint32_t*)(x)))
#define LE_32(x) ALE_32(x)

static const unsigned char xor_table[] = {
  0x05, 0x18, 0x74, 0xd0, 0x0d, 0x09, 0x02, 0x53,
  0xc0, 0x01, 0x05, 0x05, 0x67, 0x03, 0x19, 0x70,
  0x08, 0x27, 0x66, 0x10, 0x10, 0x72, 0x08, 0x09,
  0x63, 0x11, 0x03, 0x71, 0x08, 0x08, 0x70, 0x02,
  0x10, 0x57, 0x05, 0x18, 0x54, 0x00, 0x00, 0x00
};

#define BE_32C(x,y) do { *(uint32_t *)(x) = be2me_32((y)); } while(0)
#define LE_32C(x,y) do { *(uint32_t *)(x) = le2me_32((y)); } while(0)

static void
hash (char *field, char *param)
{
  uint32_t a, b, c, d;

  /* fill variables */
  a = LE_32 (field);
  b = LE_32 (field + 4);
  c = LE_32 (field + 8);
  d = LE_32 (field + 12);

  a = ((b & c) | (~b & d)) + LE_32 ((param + 0x00)) + a - 0x28955B88;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + LE_32 ((param + 0x04)) + d - 0x173848AA;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + LE_32 ((param + 0x08)) + c + 0x242070DB;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + LE_32 ((param + 0x0c)) + b - 0x3E423112;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + LE_32 ((param + 0x10)) + a - 0x0A83F051;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + LE_32 ((param + 0x14)) + d + 0x4787C62A;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + LE_32 ((param + 0x18)) + c - 0x57CFB9ED;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + LE_32 ((param + 0x1c)) + b - 0x02B96AFF;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + LE_32 ((param + 0x20)) + a + 0x698098D8;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + LE_32 ((param + 0x24)) + d - 0x74BB0851;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + LE_32 ((param + 0x28)) + c - 0x0000A44F;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + LE_32 ((param + 0x2C)) + b - 0x76A32842;
  b = ((b << 0x16) | (b >> 0x0a)) + c;
  a = ((b & c) | (~b & d)) + LE_32 ((param + 0x30)) + a + 0x6B901122;
  a = ((a << 0x07) | (a >> 0x19)) + b;
  d = ((a & b) | (~a & c)) + LE_32 ((param + 0x34)) + d - 0x02678E6D;
  d = ((d << 0x0c) | (d >> 0x14)) + a;
  c = ((d & a) | (~d & b)) + LE_32 ((param + 0x38)) + c - 0x5986BC72;
  c = ((c << 0x11) | (c >> 0x0f)) + d;
  b = ((c & d) | (~c & a)) + LE_32 ((param + 0x3c)) + b + 0x49B40821;
  b = ((b << 0x16) | (b >> 0x0a)) + c;

  a = ((b & d) | (~d & c)) + LE_32 ((param + 0x04)) + a - 0x09E1DA9E;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + LE_32 ((param + 0x18)) + d - 0x3FBF4CC0;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + LE_32 ((param + 0x2c)) + c + 0x265E5A51;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + LE_32 ((param + 0x00)) + b - 0x16493856;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + LE_32 ((param + 0x14)) + a - 0x29D0EFA3;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + LE_32 ((param + 0x28)) + d + 0x02441453;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + LE_32 ((param + 0x3c)) + c - 0x275E197F;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + LE_32 ((param + 0x10)) + b - 0x182C0438;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + LE_32 ((param + 0x24)) + a + 0x21E1CDE6;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + LE_32 ((param + 0x38)) + d - 0x3CC8F82A;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + LE_32 ((param + 0x0c)) + c - 0x0B2AF279;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + LE_32 ((param + 0x20)) + b + 0x455A14ED;
  b = ((b << 0x14) | (b >> 0x0c)) + c;
  a = ((b & d) | (~d & c)) + LE_32 ((param + 0x34)) + a - 0x561C16FB;
  a = ((a << 0x05) | (a >> 0x1b)) + b;
  d = ((a & c) | (~c & b)) + LE_32 ((param + 0x08)) + d - 0x03105C08;
  d = ((d << 0x09) | (d >> 0x17)) + a;
  c = ((d & b) | (~b & a)) + LE_32 ((param + 0x1c)) + c + 0x676F02D9;
  c = ((c << 0x0e) | (c >> 0x12)) + d;
  b = ((c & a) | (~a & d)) + LE_32 ((param + 0x30)) + b - 0x72D5B376;
  b = ((b << 0x14) | (b >> 0x0c)) + c;

  a = (b ^ c ^ d) + LE_32 ((param + 0x14)) + a - 0x0005C6BE;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + LE_32 ((param + 0x20)) + d - 0x788E097F;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + LE_32 ((param + 0x2c)) + c + 0x6D9D6122;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + LE_32 ((param + 0x38)) + b - 0x021AC7F4;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + LE_32 ((param + 0x04)) + a - 0x5B4115BC;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + LE_32 ((param + 0x10)) + d + 0x4BDECFA9;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + LE_32 ((param + 0x1c)) + c - 0x0944B4A0;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + LE_32 ((param + 0x28)) + b - 0x41404390;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + LE_32 ((param + 0x34)) + a + 0x289B7EC6;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + LE_32 ((param + 0x00)) + d - 0x155ED806;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + LE_32 ((param + 0x0c)) + c - 0x2B10CF7B;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + LE_32 ((param + 0x18)) + b + 0x04881D05;
  b = ((b << 0x17) | (b >> 0x09)) + c;
  a = (b ^ c ^ d) + LE_32 ((param + 0x24)) + a - 0x262B2FC7;
  a = ((a << 0x04) | (a >> 0x1c)) + b;
  d = (a ^ b ^ c) + LE_32 ((param + 0x30)) + d - 0x1924661B;
  d = ((d << 0x0b) | (d >> 0x15)) + a;
  c = (d ^ a ^ b) + LE_32 ((param + 0x3c)) + c + 0x1fa27cf8;
  c = ((c << 0x10) | (c >> 0x10)) + d;
  b = (c ^ d ^ a) + LE_32 ((param + 0x08)) + b - 0x3B53A99B;
  b = ((b << 0x17) | (b >> 0x09)) + c;

  a = ((~d | b) ^ c) + LE_32 ((param + 0x00)) + a - 0x0BD6DDBC;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + LE_32 ((param + 0x1c)) + d + 0x432AFF97;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + LE_32 ((param + 0x38)) + c - 0x546BDC59;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + LE_32 ((param + 0x14)) + b - 0x036C5FC7;
  b = ((b << 0x15) | (b >> 0x0b)) + c;
  a = ((~d | b) ^ c) + LE_32 ((param + 0x30)) + a + 0x655B59C3;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + LE_32 ((param + 0x0C)) + d - 0x70F3336E;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + LE_32 ((param + 0x28)) + c - 0x00100B83;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + LE_32 ((param + 0x04)) + b - 0x7A7BA22F;
  b = ((b << 0x15) | (b >> 0x0b)) + c;
  a = ((~d | b) ^ c) + LE_32 ((param + 0x20)) + a + 0x6FA87E4F;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + LE_32 ((param + 0x3c)) + d - 0x01D31920;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + LE_32 ((param + 0x18)) + c - 0x5CFEBCEC;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + LE_32 ((param + 0x34)) + b + 0x4E0811A1;
  b = ((b << 0x15) | (b >> 0x0b)) + c;
  a = ((~d | b) ^ c) + LE_32 ((param + 0x10)) + a - 0x08AC817E;
  a = ((a << 0x06) | (a >> 0x1a)) + b;
  d = ((~c | a) ^ b) + LE_32 ((param + 0x2c)) + d - 0x42C50DCB;
  d = ((d << 0x0a) | (d >> 0x16)) + a;
  c = ((~b | d) ^ a) + LE_32 ((param + 0x08)) + c + 0x2AD7D2BB;
  c = ((c << 0x0f) | (c >> 0x11)) + d;
  b = ((~a | c) ^ d) + LE_32 ((param + 0x24)) + b - 0x14792C6F;
  b = ((b << 0x15) | (b >> 0x0b)) + c;

  a += LE_32 (field);
  b += LE_32 (field + 4);
  c += LE_32 (field + 8);
  d += LE_32 (field + 12);

  LE_32C (field, a);
  LE_32C (field + 4, b);
  LE_32C (field + 8, c);
  LE_32C (field + 12, d);
}

static void
call_hash (char *key, char *challenge, int len)
{
  uint8_t *ptr1, *ptr2;
  uint32_t a, b, c, d, tmp;

  ptr1 = (uint8_t *) (key + 16);
  ptr2 = (uint8_t *) (key + 20);

  a = LE_32 (ptr1);
  b = (a >> 3) & 0x3f;
  a += len * 8;
  LE_32C (ptr1, a);

  if (a < (len << 3))
    ptr2 += 4;

  tmp = LE_32 (ptr2) + (len >> 0x1d);
  LE_32C (ptr2, tmp);
  a = 64 - b;
  c = 0;
  if (a <= len) {

    memcpy (key + b + 24, challenge, a);
    hash (key, key + 24);
    c = a;
    d = c + 0x3f;

    while (d < len) {
      hash (key, challenge + d - 0x3f);
      d += 64;
      c += 64;
    }
    b = 0;
  }

  memcpy (key + b + 24, challenge + c, len - c);
}

static void
rtsp_ext_real_calc_response_and_checksum (char *response, char *chksum,
    char *challenge)
{
  int ch_len, table_len, resp_len;
  int i;
  char *ptr;
  char buf[128];
  char field[128];
  char zres[20];
  char buf1[128];
  char buf2[128];

  /* initialize return values */
  memset (response, 0, 64);
  memset (chksum, 0, 34);

  /* initialize buffer */
  memset (buf, 0, 128);
  ptr = buf;
  BE_32C (ptr, 0xa1e9149d);
  ptr += 4;
  BE_32C (ptr, 0x0e6b3b59);
  ptr += 4;

  if ((ch_len = MIN (strlen (challenge), 56)) == 40) {
    challenge[32] = 0;
    ch_len = 32;
  }
  memcpy (ptr, challenge, ch_len);

  /* xor challenge bytewise with xor_table */
  table_len = MIN (strlen ((char *) xor_table), 56);
  for (i = 0; i < table_len; i++)
    ptr[i] = ptr[i] ^ xor_table[i];

  /* initialize our field */
  BE_32C (field, 0x01234567);
  BE_32C (field + 4, 0x89ABCDEF);
  BE_32C (field + 8, 0xFEDCBA98);
  BE_32C (field + 12, 0x76543210);
  BE_32C (field + 16, 0x00000000);
  BE_32C (field + 20, 0x00000000);

  /* calculate response */
  call_hash (field, buf, 64);
  memset (buf1, 0, 64);
  *buf1 = 128;
  memcpy (buf2, field + 16, 8);
  i = (LE_32 ((buf2)) >> 3) & 0x3f;
  if (i < 56)
    i = 56 - i;
  else
    i = 120 - i;
  call_hash (field, buf1, i);
  call_hash (field, buf2, 8);
  memcpy (zres, field, 16);

  /* convert zres to ascii string */
  for (i = 0; i < 16; i++) {
    char a, b;

    a = (zres[i] >> 4) & 15;
    b = zres[i] & 15;

    response[i * 2] = ((a < 10) ? (a + 48) : (a + 87)) & 255;
    response[i * 2 + 1] = ((b < 10) ? (b + 48) : (b + 87)) & 255;
  }

  /* add tail */
  resp_len = strlen (response);
  strcpy (&response[resp_len], "01d0a8e3");

  /* calculate checksum */
  for (i = 0; i < resp_len / 4; i++)
    chksum[i] = response[i * 4];
}

/*
 * Stop xine code
 */
#define ENSURE_SIZE(size)              \
G_STMT_START {			       \
  while (data_len < size) {            \
    data_len += 1024;                  \
    data = g_realloc (data, data_len); \
  }                                    \
} G_STMT_END

#define READ_BUFFER_GEN(src, func, name, dest, dest_len)    \
G_STMT_START {			                            \
  dest = (gchar *)func (src, name);                                  \
  if (!dest) {                                              \
    dest = "";                                              \
    dest_len = 0;                                           \
  }                                                         \
  else if (!strncmp (dest, "buffer;\"", 8)) {               \
    dest += 8;                                              \
    dest_len = strlen (dest) - 1;                           \
    dest[dest_len] = '\0';                                  \
    gst_rtsp_base64_decode_ip (dest, &dest_len);            \
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
    dest = (gchar *) val + 8;                                           \
    dest_len = strlen (dest) - 1;                             \
    dest[dest_len] = '\0';                                    \
  } else {                                                    \
    dest = "";                                                \
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
  guint size, n_streams;
  gint i;
  guint32 max_bit_rate = 0, avg_bit_rate = 0;
  guint32 max_packet_size = 0, avg_packet_size = 0;
  guint32 start_time, preroll, duration = 0;
  gchar *title, *author, *copyright, *comment;
  gsize title_len, author_len, copyright_len, comment_len;
  guint8 *data = NULL, *datap;
  guint data_len = 0, offset;
  GstBuffer *buf;
  gchar *opaque_data;
  gsize opaque_data_len;

  /* don't bother for non-real formats */
  READ_INT (sdp, "IsRealDataType", ctx->isreal);
  if (!ctx->isreal)
    return TRUE;

  /* Force PAUSE | PLAY */
  //src->methods |= GST_RTSP_PLAY | GST_RTSP_PAUSE;

  n_streams = gst_sdp_message_medias_len (sdp);

  /* PROP */
  for (i = 0; i < n_streams; i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp, i);
    gint intval;

    READ_INT_M (media, "MaxBitRate", intval);
    max_bit_rate += intval;
    READ_INT_M (media, "AvgBitRate", intval);
    avg_bit_rate += intval;
    READ_INT_M (media, "MaxPacketSize", intval);
    max_packet_size = MAX (max_packet_size, intval);
    READ_INT_M (media, "AvgPacketSize", intval);
    avg_packet_size = (avg_packet_size * i + intval) / (i + 1);
    READ_INT_M (media, "Duration", intval);
    duration = MAX (duration, intval);
  }

  offset = 0;
  size = 50;
  ENSURE_SIZE (size);
  datap = data + offset;

  memcpy (datap + 0, "PROP", 4);
  GST_WRITE_UINT32_BE (datap + 4, size);
  GST_WRITE_UINT16_BE (datap + 8, 0);
  GST_WRITE_UINT32_BE (datap + 10, max_bit_rate);
  GST_WRITE_UINT32_BE (datap + 14, avg_bit_rate);
  GST_WRITE_UINT32_BE (datap + 18, max_packet_size);
  GST_WRITE_UINT32_BE (datap + 22, avg_packet_size);
  GST_WRITE_UINT32_BE (datap + 26, 0);
  GST_WRITE_UINT32_BE (datap + 30, duration);
  GST_WRITE_UINT32_BE (datap + 34, 0);
  GST_WRITE_UINT32_BE (datap + 38, 0);
  GST_WRITE_UINT32_BE (datap + 42, 0);
  GST_WRITE_UINT16_BE (datap + 46, n_streams);
  GST_WRITE_UINT16_BE (datap + 48, 0);
  offset += size;

  /* CONT */
  READ_BUFFER (sdp, "Title", title, title_len);
  READ_BUFFER (sdp, "Author", author, author_len);
  READ_BUFFER (sdp, "Comment", comment, comment_len);
  READ_BUFFER (sdp, "Copyright", copyright, copyright_len);

  size = 20 + title_len + author_len + comment_len + copyright_len;
  ENSURE_SIZE (offset + size);
  datap = data + offset;

  memcpy (datap, "CONT", 4);
  GST_WRITE_UINT32_BE (datap + 4, size);
  datap += 8;
  WRITE_STRING2 (datap, title, title_len);
  WRITE_STRING2 (datap, author, author_len);
  WRITE_STRING2 (datap, copyright, copyright_len);
  WRITE_STRING2 (datap, comment, comment_len);
  offset += size;

  /* MDPR */
  for (i = 0; i < n_streams; i++) {
    const GstSDPMedia *media = gst_sdp_message_get_media (sdp, i);
    gchar *asm_rule_book, *type_specific_data;
    guint asm_rule_book_len;
    gchar *stream_name, *mime_type;
    guint stream_name_len, mime_type_len;
    guint16 num_rules, j, sel, codec;
    guint32 type_specific_data_len, len;

    READ_INT_M (media, "MaxBitRate", max_bit_rate);
    READ_INT_M (media, "AvgBitRate", avg_bit_rate);
    READ_INT_M (media, "MaxPacketSize", max_packet_size);
    READ_INT_M (media, "AvgPacketSize", avg_packet_size);
    READ_INT_M (media, "StartTime", start_time);
    READ_INT_M (media, "Preroll", preroll);
    READ_INT_M (media, "Duration", duration);
    READ_STRING (media, "StreamName", stream_name, stream_name_len);
    READ_STRING (media, "mimetype", mime_type, mime_type_len);

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
    READ_STRING (media, "ASMRuleBook", asm_rule_book, asm_rule_book_len);
    sel = 0;

    READ_BUFFER_M (media, "OpaqueData", opaque_data, opaque_data_len);

    if (opaque_data_len < 4) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < 4", opaque_data_len);
      goto strange_opaque_data;
    }
    if (strncmp (opaque_data, "MLTI", 4)) {
      GST_DEBUG_OBJECT (ctx, "no MLTI found, appending all");
      type_specific_data_len = opaque_data_len;
      type_specific_data = opaque_data;
      goto no_type_specific;
    }
    opaque_data += 4;
    opaque_data_len -= 4;

    if (opaque_data_len < 2) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < 2", opaque_data_len);
      goto strange_opaque_data;
    }
    num_rules = GST_READ_UINT16_BE (opaque_data);
    opaque_data += 2;
    opaque_data_len -= 2;

    if (sel >= num_rules) {
      GST_DEBUG_OBJECT (ctx, "sel %d >= num_rules %d", sel, num_rules);
      goto strange_opaque_data;
    }

    if (opaque_data_len < 2 * sel) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < 2 * sel (%d)",
          opaque_data_len, 2 * sel);
      goto strange_opaque_data;
    }
    opaque_data += 2 * sel;
    opaque_data_len -= 2 * sel;

    if (opaque_data_len < 2) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < 2", opaque_data_len);
      goto strange_opaque_data;
    }
    codec = GST_READ_UINT16_BE (opaque_data);
    opaque_data += 2;
    opaque_data_len -= 2;

    if (opaque_data_len < 2 * (num_rules - sel - 1)) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < %d", opaque_data_len,
          2 * (num_rules - sel - 1));
      goto strange_opaque_data;
    }
    opaque_data += 2 * (num_rules - sel - 1);
    opaque_data_len -= 2 * (num_rules - sel - 1);

    if (opaque_data_len < 2) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < 2", opaque_data_len);
      goto strange_opaque_data;
    }
    num_rules = GST_READ_UINT16_BE (opaque_data);
    opaque_data += 2;
    opaque_data_len -= 2;

    if (codec > num_rules) {
      GST_DEBUG_OBJECT (ctx, "codec %d > num_rules %d", codec, num_rules);
      goto strange_opaque_data;
    }

    for (j = 0; j < codec; j++) {
      if (opaque_data_len < 4) {
        GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < 4", opaque_data_len);
        goto strange_opaque_data;
      }
      len = GST_READ_UINT32_BE (opaque_data);
      opaque_data += 4;
      opaque_data_len -= 4;

      if (opaque_data_len < len) {
        GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < len %d", opaque_data_len,
            len);
        goto strange_opaque_data;
      }
      opaque_data += len;
      opaque_data_len -= len;
    }

    if (opaque_data_len < 4) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < 4", opaque_data_len);
      goto strange_opaque_data;
    }
    type_specific_data_len = GST_READ_UINT32_BE (opaque_data);
    opaque_data += 4;
    opaque_data_len -= 4;

    if (opaque_data_len < type_specific_data_len) {
      GST_DEBUG_OBJECT (ctx, "opaque_data_len %d < %d", opaque_data_len,
          type_specific_data_len);
      goto strange_opaque_data;
    }
    type_specific_data = opaque_data;

  no_type_specific:
    size = 46 + stream_name_len + mime_type_len + type_specific_data_len;
    ENSURE_SIZE (offset + size);
    datap = data + offset;

    memcpy (datap, "MDPR", 4);
    GST_WRITE_UINT32_BE (datap + 4, size);
    GST_WRITE_UINT16_BE (datap + 8, 0);
    GST_WRITE_UINT16_BE (datap + 10, i);
    GST_WRITE_UINT32_BE (datap + 12, max_bit_rate);
    GST_WRITE_UINT32_BE (datap + 16, avg_bit_rate);
    GST_WRITE_UINT32_BE (datap + 20, max_packet_size);
    GST_WRITE_UINT32_BE (datap + 24, avg_packet_size);
    GST_WRITE_UINT32_BE (datap + 28, start_time);
    GST_WRITE_UINT32_BE (datap + 32, preroll);
    GST_WRITE_UINT32_BE (datap + 36, duration);
    datap += 40;
    WRITE_STRING1 (datap, stream_name, stream_name_len);
    WRITE_STRING1 (datap, mime_type, mime_type_len);
    GST_WRITE_UINT32_BE (datap, type_specific_data_len);
    if (type_specific_data_len)
      memcpy (datap + 4, type_specific_data, type_specific_data_len);
    offset += size;
  }

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

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = data;
  GST_BUFFER_MALLOCDATA (buf) = data;
  GST_BUFFER_SIZE (buf) = offset;

  /* Set on caps */
  gst_structure_set (props, "config", GST_TYPE_BUFFER, buf, NULL);

  /* Overwrite encoding and media fields */
  gst_structure_set (props, "encoding-name", G_TYPE_STRING, "x-real-rdt", NULL);
  gst_structure_set (props, "media", G_TYPE_STRING, "application", NULL);

  return TRUE;

  /* ERRORS */
strange_opaque_data:
  {
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

  req_url = gst_rtsp_url_get_request_uri (url);

  /* create SET_PARAMETER */
  if ((res = gst_rtsp_message_init_request (&request, GST_RTSP_SET_PARAMETER,
              req_url)) < 0)
    goto create_request_failed;

  g_free (req_url);

  /* FIXME, do selection instead of hardcoded values */
  gst_rtsp_message_add_header (&request, GST_RTSP_HDR_SUBSCRIBE,
      //"stream=0;rule=5,stream=0;rule=6,stream=1;rule=0,stream=1;rule=1");
      //"stream=0;rule=0,stream=0;rule=1,stream=1;rule=0,stream=1;rule=1");
      "stream=0;rule=0,stream=0;rule=1");

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

static void gst_rtsp_real_finalize (GObject * object);

static GstStateChangeReturn gst_rtsp_real_change_state (GstElement * element,
    GstStateChange transition);

static void gst_rtsp_real_extension_init (gpointer g_iface,
    gpointer iface_data);

static void
_do_init (GType rtspreal_type)
{
  static const GInterfaceInfo rtspextension_info = {
    gst_rtsp_real_extension_init,
    NULL,
    NULL
  };

  g_type_add_interface_static (rtspreal_type, GST_TYPE_RTSP_EXTENSION,
      &rtspextension_info);
}

GST_BOILERPLATE_FULL (GstRTSPReal, gst_rtsp_real, GstElement, GST_TYPE_ELEMENT,
    _do_init);

static void
gst_rtsp_real_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &rtspreal_details);
}

static void
gst_rtsp_real_class_init (GstRTSPRealClass * g_class)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstRTSPRealClass *klass;

  klass = (GstRTSPRealClass *) g_class;
  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->finalize = gst_rtsp_real_finalize;

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_rtsp_real_change_state);

  GST_DEBUG_CATEGORY_INIT (rtspreal_debug, "rtspreal", 0,
      "RealMedia RTSP extension");
}

static void
gst_rtsp_real_init (GstRTSPReal * rtspreal, GstRTSPRealClass * klass)
{
  rtspreal->isreal = FALSE;
}

static void
gst_rtsp_real_finalize (GObject * object)
{
  GstRTSPReal *rtspreal;

  rtspreal = GST_RTSP_REAL (object);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_rtsp_real_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;
  GstRTSPReal *rtspreal;

  rtspreal = GST_RTSP_REAL (element);

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  return ret;
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
      GST_RANK_NONE, GST_TYPE_RTSP_REAL);
}
