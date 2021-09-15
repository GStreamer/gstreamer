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
#include <gst/rtp/rtp.h>
#include <gst/sdp/gstsdpmessage.h>
#include <gst/audio/audio.h>
#include <gst/check/gstharness.h>

#define URN "urn:ietf:params:rtp-hdrext:ssrc-audio-level"

#define SDP "v=0\r\n" \
    "o=- 123456 2 IN IP4 127.0.0.1 \r\n" \
    "s=-\r\n" \
    "t=0 0\r\n" \
    "a=maxptime:60\r\n" \
    "a=sendrecv\r\n" \
    "m=audio 55815 RTP/SAVPF 100\r\n" \
    "c=IN IP4 1.1.1.1\r\n" \
    "a=rtpmap:100 opus/48000/2\r\n"

#define SDP_NO_VAD SDP \
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level\r\n"
#define SDP_VAD_ON SDP \
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level vad=on\r\n"
#define SDP_VAD_OFF SDP \
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level vad=off\r\n"
#define SDP_VAD_WRONG SDP \
    "a=extmap:1 urn:ietf:params:rtp-hdrext:ssrc-audio-level vad=badger\r\n"

static GstCaps *
create_caps (const gchar * sdp)
{
  GstSDPMessage *message;
  glong length = -1;
  const GstSDPMedia *media;
  GstCaps *caps;

  gst_sdp_message_new (&message);
  gst_sdp_message_parse_buffer ((guint8 *) sdp, length, message);
  media = gst_sdp_message_get_media (message, 0);
  fail_unless (media != NULL);

  caps = gst_sdp_media_get_caps_from_media (media, 100);
  gst_sdp_media_attributes_to_caps (media, caps);
  gst_sdp_message_free (message);
  return caps;
}

static void
check_caps (GstRTPHeaderExtension * ext, gboolean vad)
{
  GstCaps *caps;
  GstStructure *s;
  const GValue *arr, *val;

  caps = gst_caps_new_empty_simple ("application/x-rtp");
  fail_unless (gst_rtp_header_extension_set_caps_from_attributes (ext, caps));
  s = gst_caps_get_structure (caps, 0);

  arr = gst_structure_get_value (s, "extmap-1");
  fail_unless (arr != NULL);
  fail_unless (GST_VALUE_HOLDS_ARRAY (arr));
  fail_unless (gst_value_array_get_size (arr) == 3);

  val = gst_value_array_get_value (arr, 0);
  fail_unless_equals_string (g_value_get_string (val), "");

  val = gst_value_array_get_value (arr, 1);
  fail_unless_equals_string (g_value_get_string (val), URN);

  val = gst_value_array_get_value (arr, 2);
  if (vad) {
    fail_unless_equals_string (g_value_get_string (val), "vad=on");
  } else {
    fail_unless_equals_string (g_value_get_string (val), "vad=off");
  }

  gst_caps_unref (caps);
}

GST_START_TEST (rtphdrext_client_audio_level_sdp)
{
  GstRTPHeaderExtension *ext;
  GstCaps *caps;
  gboolean vad = FALSE;

  ext = gst_rtp_header_extension_create_from_uri (URN);
  fail_unless (ext != NULL);
  gst_rtp_header_extension_set_id (ext, 1);

  /* vad default to on */
  caps = create_caps (SDP_NO_VAD);
  fail_unless (gst_rtp_header_extension_set_attributes_from_caps (ext, caps));
  gst_caps_unref (caps);
  g_object_get (ext, "vad", &vad, NULL);
  fail_unless (vad);
  check_caps (ext, TRUE);

  /* vad is disabled */
  caps = create_caps (SDP_VAD_OFF);
  fail_unless (gst_rtp_header_extension_set_attributes_from_caps (ext, caps));
  gst_caps_unref (caps);
  g_object_get (ext, "vad", &vad, NULL);
  fail_if (vad);

  /* vad is enabled */
  caps = create_caps (SDP_VAD_ON);
  fail_unless (gst_rtp_header_extension_set_attributes_from_caps (ext, caps));
  gst_caps_unref (caps);
  g_object_get (ext, "vad", &vad, NULL);
  fail_unless (vad);

  /* invalid vad */
  caps = create_caps (SDP_VAD_WRONG);
  fail_if (gst_rtp_header_extension_set_attributes_from_caps (ext, caps));
  gst_caps_unref (caps);

  gst_object_unref (ext);
}

GST_END_TEST;

GST_START_TEST (rtphdrext_client_audio_level_one_byte)
{
  GstRTPHeaderExtension *ext;
  GstRTPHeaderExtensionFlags flags;
  GstBuffer *buffer;
  guint8 *data;
  gsize size, written;
  GstAudioLevelMeta *meta;
  guint8 level = 12;
  gboolean voice = TRUE;

  ext = gst_rtp_header_extension_create_from_uri (URN);
  fail_unless (ext != NULL);
  gst_rtp_header_extension_set_id (ext, 1);

  flags = gst_rtp_header_extension_get_supported_flags (ext);
  fail_unless (flags & GST_RTP_HEADER_EXTENSION_ONE_BYTE);

  buffer = gst_buffer_new ();
  meta = gst_buffer_add_audio_level_meta (buffer, level, voice);

  size = gst_rtp_header_extension_get_max_size (ext, buffer);
  fail_unless (size > 0);
  data = g_malloc0 (size);
  fail_unless (data != NULL);

  /* Write extension */
  written =
      gst_rtp_header_extension_write (ext, buffer,
      GST_RTP_HEADER_EXTENSION_ONE_BYTE, buffer, data, size);
  fail_unless (written == 1);

  /* Read it back */
  fail_unless (gst_buffer_remove_meta (buffer, (GstMeta *) meta));
  fail_unless (gst_rtp_header_extension_read (ext,
          GST_RTP_HEADER_EXTENSION_ONE_BYTE, data, size, buffer));
  meta = gst_buffer_get_audio_level_meta (buffer);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->level, level);
  fail_unless (meta->voice_activity == voice);

  g_free (data);
  gst_buffer_unref (buffer);
  gst_object_unref (ext);
}

GST_END_TEST;

GST_START_TEST (rtphdrext_client_audio_level_two_bytes)
{
  GstRTPHeaderExtension *ext;
  GstRTPHeaderExtensionFlags flags;
  GstBuffer *buffer;
  guint8 *data;
  gsize size, written;
  GstAudioLevelMeta *meta;
  guint8 level = 12;
  gboolean voice = TRUE;

  ext = gst_rtp_header_extension_create_from_uri (URN);
  fail_unless (ext != NULL);
  gst_rtp_header_extension_set_id (ext, 1);

  flags = gst_rtp_header_extension_get_supported_flags (ext);
  fail_unless (flags & GST_RTP_HEADER_EXTENSION_TWO_BYTE);

  buffer = gst_buffer_new ();
  meta = gst_buffer_add_audio_level_meta (buffer, level, voice);

  size = gst_rtp_header_extension_get_max_size (ext, buffer);
  fail_unless (size > 0);
  data = g_malloc0 (size);
  fail_unless (data != NULL);

  /* Write extension */
  written =
      gst_rtp_header_extension_write (ext, buffer,
      GST_RTP_HEADER_EXTENSION_TWO_BYTE, buffer, data, size);
  fail_unless (written == 2);

  /* Read it back */
  fail_unless (gst_buffer_remove_meta (buffer, (GstMeta *) meta));
  fail_unless (gst_rtp_header_extension_read (ext,
          GST_RTP_HEADER_EXTENSION_TWO_BYTE, data, size, buffer));
  meta = gst_buffer_get_audio_level_meta (buffer);
  fail_unless (meta != NULL);
  fail_unless_equals_int (meta->level, level);
  fail_unless (meta->voice_activity == voice);

  g_free (data);
  gst_buffer_unref (buffer);
  gst_object_unref (ext);
}

GST_END_TEST;

GST_START_TEST (rtphdrext_client_audio_level_no_meta)
{
  GstRTPHeaderExtension *ext;
  GstBuffer *buffer;
  guint8 *data;
  gsize size, written;

  ext = gst_rtp_header_extension_create_from_uri (URN);
  fail_unless (ext != NULL);
  gst_rtp_header_extension_set_id (ext, 1);

  buffer = gst_buffer_new ();

  size = gst_rtp_header_extension_get_max_size (ext, buffer);
  fail_unless (size > 0);
  data = g_malloc0 (size);
  fail_unless (data != NULL);

  written =
      gst_rtp_header_extension_write (ext, buffer,
      GST_RTP_HEADER_EXTENSION_ONE_BYTE, buffer, data, size);
  fail_unless (written == 0);

  written =
      gst_rtp_header_extension_write (ext, buffer,
      GST_RTP_HEADER_EXTENSION_TWO_BYTE, buffer, data, size);
  fail_unless (written == 0);

  g_free (data);
  gst_buffer_unref (buffer);
  gst_object_unref (ext);
}

GST_END_TEST;

GST_START_TEST (rtphdrext_client_audio_level_payloader_depayloader)
{
  GstHarness *h;
  GstBuffer *b;
  GstFlowReturn fret;
  GstAudioLevelMeta *meta;

  h = gst_harness_new_parse ("rtpL16pay ! "
      "application/x-rtp, extmap-1=(string)< \"\", " URN " , \"vad=on\" >"
      " ! rtpL16depay");

  gst_harness_set_src_caps_str (h, "audio/x-raw, rate=44100, channels=1,"
      " layout=interleaved, format=S16BE");

  b = gst_buffer_new_allocate (NULL, 100, NULL);
  gst_buffer_add_audio_level_meta (b, 12, TRUE);
  fret = gst_harness_push (h, b);
  fail_unless (fret == GST_FLOW_OK);

  b = gst_harness_pull (h);
  meta = gst_buffer_get_audio_level_meta (b);

  fail_unless (meta != NULL);
  fail_unless (meta->level == 12);
  fail_unless (meta->voice_activity == TRUE);

  gst_buffer_unref (b);
  gst_harness_teardown (h);
}

GST_END_TEST;


GST_START_TEST (rtphdrext_client_audio_level_payloader_api)
{
  GstHarness *h;
  GstRTPHeaderExtension *ext;
  GstBuffer *b;
  GstFlowReturn fret;
  GstRTPBuffer rtp = GST_RTP_BUFFER_INIT;
  guint8 *data;
  guint size;
  guint8 level;
  gboolean voice_activity;

  h = gst_harness_new ("rtpL16pay");
  gst_harness_set_src_caps_str (h, "audio/x-raw, rate=44100, channels=1,"
      " layout=interleaved, format=S16BE");

  ext = gst_rtp_header_extension_create_from_uri (URN);
  gst_rtp_header_extension_set_id (ext, 2);
  fail_unless (ext);
  g_signal_emit_by_name (h->element, "add-extension", ext);

  b = gst_buffer_new_allocate (NULL, 100, NULL);
  gst_buffer_add_audio_level_meta (b, 12, TRUE);
  fret = gst_harness_push (h, b);
  fail_unless (fret == GST_FLOW_OK);

  b = gst_harness_pull (h);
  fail_unless (gst_rtp_buffer_map (b, GST_MAP_READ, &rtp));
  fail_unless (gst_rtp_buffer_get_extension_onebyte_header (&rtp, 2, 0,
          (gpointer *) & data, &size));
  fail_unless (size == 1);
  level = data[0] & 0x7F;
  voice_activity = (data[0] & 0x80) >> 7;
  fail_unless (level == 12);
  fail_unless (voice_activity == TRUE);
  gst_rtp_buffer_unmap (&rtp);
  gst_buffer_unref (b);

  gst_object_unref (ext);
  gst_harness_teardown (h);
}

GST_END_TEST;


static Suite *
rtphdrext_client_audio_level_suite (void)
{
  Suite *s = suite_create ("rtphdrext_client_audio_level");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, rtphdrext_client_audio_level_sdp);
  tcase_add_test (tc_chain, rtphdrext_client_audio_level_one_byte);
  tcase_add_test (tc_chain, rtphdrext_client_audio_level_two_bytes);
  tcase_add_test (tc_chain, rtphdrext_client_audio_level_no_meta);
  tcase_add_test (tc_chain, rtphdrext_client_audio_level_payloader_depayloader);
  tcase_add_test (tc_chain, rtphdrext_client_audio_level_payloader_api);

  return s;
}

GST_CHECK_MAIN (rtphdrext_client_audio_level)
