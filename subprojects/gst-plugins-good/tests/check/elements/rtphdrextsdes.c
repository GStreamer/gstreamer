/* GStreamer
 *
 * unit test for RTP RFC 6464 Header Extensions
 *
 * Copyright (C) <2020-2021> Guillaume Desmottes <guillaume.desmottes@collabora.com>
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

#include <gst/check/gstcheck.h>
#include <gst/sdp/sdp.h>
#include <gst/rtp/rtp.h>

#define URN_MID "urn:ietf:params:rtp-hdrext:sdes:mid"
#define URN_STREAM_ID "urn:ietf:params:rtp-hdrext:sdes:rtp-stream-id"
#define URN_REPAIRED_STREAM_ID "urn:ietf:params:rtp-hdrext:sdes:repaired-rtp-stream-id"

#define ALL_VALID_PROPERTY_ALPHANUMERIC "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVYZ"

typedef struct
{
  const gchar *property_name;
  gchar *out_val;
} NotifyValCtx;

static void
on_notify_val (GObject * ext, G_GNUC_UNUSED GParamSpec * pspec,
    NotifyValCtx * ctx)
{
  g_object_get (ext, ctx->property_name, &ctx->out_val, NULL);
}

static void
read_write_extension (GstRTPHeaderExtension * read_ext,
    GstRTPHeaderExtension * write_ext, GstRTPHeaderExtensionFlags flags,
    const char *property_name, const char *val)
{
  GstBuffer *buffer;
  gsize size, written;
  guint8 *data;
  GstRTPHeaderExtensionFlags supported_flags = 0;
  char *notify_signal_name;
  NotifyValCtx ctx = {.property_name = property_name,.out_val = NULL };

  buffer = gst_buffer_new ();

  supported_flags = gst_rtp_header_extension_get_supported_flags (write_ext);
  fail_unless (supported_flags & flags);

  size = gst_rtp_header_extension_get_max_size (write_ext, buffer);
  fail_unless (size > 0);
  data = g_malloc0 (size);
  fail_unless (data != NULL);

  /* Write extension */
  g_object_set (write_ext, property_name, val, NULL);
  written =
      gst_rtp_header_extension_write (write_ext, buffer,
      flags, buffer, data, size);
  fail_unless (written == strlen (val));

  /* moving from no rid to a detected rid, fires the property notify signal */
  notify_signal_name = g_strdup_printf ("notify::%s", property_name);
  g_signal_connect (read_ext,
      notify_signal_name, G_CALLBACK (on_notify_val), &ctx);
  g_clear_pointer (&notify_signal_name, g_free);

  fail_unless (gst_rtp_header_extension_read (read_ext,
          flags, data, written, buffer));
  fail_unless_equals_string (ctx.out_val, val);
  g_clear_pointer (&ctx.out_val, g_free);

  /* sequential val's don't notify */
  fail_unless (gst_rtp_header_extension_read (read_ext,
          flags, data, written, buffer));
  fail_if (ctx.out_val);

  /* attempting to write a NULL val, doesn't write anything */
  g_object_set (write_ext, property_name, NULL, NULL);
  written =
      gst_rtp_header_extension_write (write_ext, buffer,
      flags, buffer, data, size);
  fail_unless (written == 0);

  /* reading an empty extension data does nothing */
  fail_unless (gst_rtp_header_extension_read (read_ext,
          flags, data, written, buffer));
  fail_if (ctx.out_val);

  g_clear_pointer (&data, g_free);
  gst_clear_buffer (&buffer);
  g_signal_handlers_disconnect_by_func (read_ext, on_notify_val, &ctx);
}

static void
test_invalid_sdes_value (GstRTPHeaderExtension * ext, const char *property_name)
{
  char *val;

  g_object_set (ext, property_name, NULL, NULL);

  /* only alpahnumeric is supported */
  /* test all the invalid boundary conditions in ascii */
  g_object_set (ext, property_name, "/", NULL);
  g_object_get (ext, property_name, &val, NULL);
  fail_unless_equals_pointer (val, NULL);

  g_object_set (ext, property_name, ":", NULL);
  g_object_get (ext, property_name, &val, NULL);
  fail_unless_equals_pointer (val, NULL);

  g_object_set (ext, property_name, "@", NULL);
  g_object_get (ext, property_name, &val, NULL);
  fail_unless_equals_pointer (val, NULL);

  g_object_set (ext, property_name, "[", NULL);
  g_object_get (ext, property_name, &val, NULL);
  fail_unless_equals_pointer (val, NULL);

  g_object_set (ext, property_name, "`", NULL);
  g_object_get (ext, property_name, &val, NULL);
  fail_unless_equals_pointer (val, NULL);

  g_object_set (ext, property_name, "{", NULL);
  g_object_get (ext, property_name, &val, NULL);
  fail_unless_equals_pointer (val, NULL);
}

GST_START_TEST (rtprfc8843_one_byte)
{
  GstRTPHeaderExtension *read_ext, *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  read_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (read_ext != NULL);
  gst_rtp_header_extension_set_id (read_ext, 1);

  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE,
      "mid", "0");
  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE,
      "mid", "01");

  gst_object_unref (write_ext);
  gst_object_unref (read_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8843_two_bytes)
{
  GstRTPHeaderExtension *read_ext, *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  read_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (read_ext != NULL);
  gst_rtp_header_extension_set_id (read_ext, 1);

  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_TWO_BYTE,
      "mid", "0");
  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_TWO_BYTE,
      "mid", "01");

  gst_object_unref (write_ext);
  gst_object_unref (read_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8843_long_mid_uses_two_byte)
{
  GstRTPHeaderExtension *write_ext;
  GstRTPHeaderExtensionFlags flags;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  g_object_set (write_ext, "mid", "0123456789abcdefg", NULL);
  flags = gst_rtp_header_extension_get_supported_flags (write_ext);
  fail_unless ((flags & GST_RTP_HEADER_EXTENSION_ONE_BYTE) == 0);
  fail_unless ((flags & GST_RTP_HEADER_EXTENSION_TWO_BYTE) != 0);

  gst_object_unref (write_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8843_invalid_property_set)
{
  GstRTPHeaderExtension *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  test_invalid_sdes_value (write_ext, "mid");

  gst_object_unref (write_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8843_mid_in_caps)
{
  GstRTPHeaderExtension *write_ext;
  GstCaps *caps;
  GstStructure *s;

#define MID_VAL "0"

  write_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  g_object_set (write_ext, "mid", MID_VAL, NULL);

  caps = gst_caps_new_empty_simple ("application/x-rtp");
  gst_rtp_header_extension_set_caps_from_attributes (write_ext, caps);

  s = gst_caps_get_structure (caps, 0);
  fail_unless_equals_string (gst_structure_get_string (s, "a-mid"), MID_VAL);

  gst_clear_caps (&caps);
  gst_object_unref (write_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8843_all_valid_values)
{
  GstRTPHeaderExtension *write_ext;
  char *mid = NULL;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_MID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  g_object_set (write_ext, "mid", ALL_VALID_PROPERTY_ALPHANUMERIC, NULL);
  g_object_get (write_ext, "mid", &mid, NULL);
  fail_unless_equals_string (mid, ALL_VALID_PROPERTY_ALPHANUMERIC);

  gst_object_unref (write_ext);
  g_clear_pointer (&mid, g_free);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_stream_id_one_byte)
{
  GstRTPHeaderExtension *read_ext, *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  read_ext = gst_rtp_header_extension_create_from_uri (URN_STREAM_ID);
  fail_unless (read_ext != NULL);
  gst_rtp_header_extension_set_id (read_ext, 1);

  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE,
      "rid", "0");
  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE,
      "rid", "01");

  gst_object_unref (write_ext);
  gst_object_unref (read_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_stream_id_two_bytes)
{
  GstRTPHeaderExtension *read_ext, *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  read_ext = gst_rtp_header_extension_create_from_uri (URN_STREAM_ID);
  fail_unless (read_ext != NULL);
  gst_rtp_header_extension_set_id (read_ext, 1);

  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_TWO_BYTE,
      "rid", "0");
  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_TWO_BYTE,
      "rid", "01");

  gst_object_unref (write_ext);
  gst_object_unref (read_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_stream_id_long_rid_uses_two_byte)
{
  GstRTPHeaderExtension *write_ext;
  GstRTPHeaderExtensionFlags flags;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  g_object_set (write_ext, "rid", "0123456789abcdefg", NULL);
  flags = gst_rtp_header_extension_get_supported_flags (write_ext);
  fail_unless ((flags & GST_RTP_HEADER_EXTENSION_ONE_BYTE) == 0);
  fail_unless ((flags & GST_RTP_HEADER_EXTENSION_TWO_BYTE) != 0);

  gst_object_unref (write_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_stream_id_invalid_property_set)
{
  GstRTPHeaderExtension *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  test_invalid_sdes_value (write_ext, "rid");

  gst_object_unref (write_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_stream_id_all_valid_values)
{
  GstRTPHeaderExtension *write_ext;
  char *rid = NULL;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  g_object_set (write_ext, "rid", ALL_VALID_PROPERTY_ALPHANUMERIC, NULL);
  g_object_get (write_ext, "rid", &rid, NULL);
  fail_unless_equals_string (rid, ALL_VALID_PROPERTY_ALPHANUMERIC);

  gst_object_unref (write_ext);
  g_clear_pointer (&rid, g_free);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_repaired_stream_id_one_byte)
{
  GstRTPHeaderExtension *read_ext, *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_REPAIRED_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  read_ext = gst_rtp_header_extension_create_from_uri (URN_REPAIRED_STREAM_ID);
  fail_unless (read_ext != NULL);
  gst_rtp_header_extension_set_id (read_ext, 1);

  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE,
      "rid", "0");
  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_ONE_BYTE,
      "rid", "01");

  gst_object_unref (write_ext);
  gst_object_unref (read_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_repaired_stream_id_two_bytes)
{
  GstRTPHeaderExtension *read_ext, *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_REPAIRED_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  read_ext = gst_rtp_header_extension_create_from_uri (URN_REPAIRED_STREAM_ID);
  fail_unless (read_ext != NULL);
  gst_rtp_header_extension_set_id (read_ext, 1);

  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_TWO_BYTE,
      "rid", "0");
  read_write_extension (read_ext, write_ext, GST_RTP_HEADER_EXTENSION_TWO_BYTE,
      "rid", "01");

  gst_object_unref (write_ext);
  gst_object_unref (read_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_repaired_stream_id_long_rid_uses_two_byte)
{
  GstRTPHeaderExtension *write_ext;
  GstRTPHeaderExtensionFlags flags;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_REPAIRED_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  g_object_set (write_ext, "rid", "0123456789abcdefg", NULL);
  flags = gst_rtp_header_extension_get_supported_flags (write_ext);
  fail_unless ((flags & GST_RTP_HEADER_EXTENSION_ONE_BYTE) == 0);
  fail_unless ((flags & GST_RTP_HEADER_EXTENSION_TWO_BYTE) != 0);

  gst_object_unref (write_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_repaired_stream_id_invalid_property_set)
{
  GstRTPHeaderExtension *write_ext;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_REPAIRED_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  test_invalid_sdes_value (write_ext, "rid");

  gst_object_unref (write_ext);
}

GST_END_TEST;

GST_START_TEST (rtprfc8852_repaired_stream_id_all_valid_values)
{
  GstRTPHeaderExtension *write_ext;
  char *rid = NULL;

  write_ext = gst_rtp_header_extension_create_from_uri (URN_REPAIRED_STREAM_ID);
  fail_unless (write_ext != NULL);
  gst_rtp_header_extension_set_id (write_ext, 1);

  g_object_set (write_ext, "rid", ALL_VALID_PROPERTY_ALPHANUMERIC, NULL);
  g_object_get (write_ext, "rid", &rid, NULL);
  fail_unless_equals_string (rid, ALL_VALID_PROPERTY_ALPHANUMERIC);

  gst_object_unref (write_ext);
  g_clear_pointer (&rid, g_free);
}

GST_END_TEST;

static Suite *
rtprfc6464_suite (void)
{
  Suite *s = suite_create ("rtphdrextsdes");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, rtprfc8843_one_byte);
  tcase_add_test (tc_chain, rtprfc8843_two_bytes);
  tcase_add_test (tc_chain, rtprfc8843_long_mid_uses_two_byte);
  tcase_add_test (tc_chain, rtprfc8843_invalid_property_set);
  tcase_add_test (tc_chain, rtprfc8843_all_valid_values);
  tcase_add_test (tc_chain, rtprfc8843_mid_in_caps);

  tcase_add_test (tc_chain, rtprfc8852_stream_id_one_byte);
  tcase_add_test (tc_chain, rtprfc8852_stream_id_two_bytes);
  tcase_add_test (tc_chain, rtprfc8852_stream_id_long_rid_uses_two_byte);
  tcase_add_test (tc_chain, rtprfc8852_stream_id_invalid_property_set);
  tcase_add_test (tc_chain, rtprfc8852_stream_id_all_valid_values);

  tcase_add_test (tc_chain, rtprfc8852_repaired_stream_id_one_byte);
  tcase_add_test (tc_chain, rtprfc8852_repaired_stream_id_two_bytes);
  tcase_add_test (tc_chain,
      rtprfc8852_repaired_stream_id_long_rid_uses_two_byte);
  tcase_add_test (tc_chain, rtprfc8852_repaired_stream_id_invalid_property_set);
  tcase_add_test (tc_chain, rtprfc8852_repaired_stream_id_all_valid_values);

  return s;
}

GST_CHECK_MAIN (rtprfc6464)
