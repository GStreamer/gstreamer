/* GStreamer unit tests for libgstpbutils
 *
 * Copyright (C) 2006 Tim-Philipp Müller <tim centricular net>
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
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/pbutils/pbutils.h>

#include <stdio.h>
#include <glib/gstdio.h>
#include <glib/gprintf.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>          /* for chmod() and getpid () */
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>           /* for chmod() */
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>             /* for unlink() */
#endif

static void
missing_msg_check_getters (GstMessage * msg)
{
  gchar *str;

  str = gst_missing_plugin_message_get_installer_detail (msg);
  fail_unless (str != NULL);
  fail_unless (*str != '\0');
  fail_unless (g_str_has_prefix (str, "gstreamer|"));
  g_free (str);

  str = gst_missing_plugin_message_get_description (msg);
  fail_unless (str != NULL);
  fail_unless (*str != '\0');
  g_free (str);
}

GST_START_TEST (test_pb_utils_post_missing_messages)
{
  const GstStructure *s;
  GstElement *pipeline;
  GstMessage *msg;
  GstCaps *caps;
  GstBus *bus;

  gst_pb_utils_init ();

  pipeline = gst_pipeline_new ("pipeline");
  bus = gst_element_get_bus (pipeline);

  /* first, test common assertion failure cases */
  ASSERT_CRITICAL (msg = gst_missing_uri_source_message_new (NULL, "http"));
  ASSERT_CRITICAL (gst_missing_uri_source_message_new (pipeline, NULL));

  ASSERT_CRITICAL (gst_missing_uri_sink_message_new (NULL, "http"));
  ASSERT_CRITICAL (gst_missing_uri_sink_message_new (pipeline, NULL));

  ASSERT_CRITICAL (gst_missing_element_message_new (NULL, "rgbfyltr"));
  ASSERT_CRITICAL (gst_missing_element_message_new (pipeline, NULL));

  caps = gst_caps_new_empty_simple ("audio/x-dontexist");

  ASSERT_CRITICAL (gst_missing_decoder_message_new (NULL, caps));
  ASSERT_CRITICAL (gst_missing_decoder_message_new (pipeline, NULL));

  ASSERT_CRITICAL (gst_missing_encoder_message_new (NULL, caps));
  ASSERT_CRITICAL (gst_missing_encoder_message_new (pipeline, NULL));

  gst_caps_unref (caps);

  /* URI source (with existing protocol) */
  msg = gst_missing_uri_source_message_new (pipeline, "http");
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "urisource");
  fail_unless (gst_structure_has_field_typed (s, "detail", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "detail"), "http");
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  /* URI sink (with existing protocol) */
  msg = gst_missing_uri_sink_message_new (pipeline, "smb");
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "urisink");
  fail_unless (gst_structure_has_field_typed (s, "detail", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "detail"), "smb");
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  /* URI source (with bogus protocol) */
  msg = gst_missing_uri_source_message_new (pipeline, "chchck");
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "urisource");
  fail_unless (gst_structure_has_field_typed (s, "detail", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "detail"), "chchck");
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  /* URI sink (with bogus protocol) */
  msg = gst_missing_uri_sink_message_new (pipeline, "chchck");
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "urisink");
  fail_unless (gst_structure_has_field_typed (s, "detail", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "detail"), "chchck");
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  /* element */
  msg = gst_missing_element_message_new (pipeline, "foobar");
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "element");
  fail_unless (gst_structure_has_field_typed (s, "detail", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "detail"), "foobar");
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  /* create bogus caps that don't exist */
  caps = gst_caps_new_simple ("do/x-not", "exist", G_TYPE_BOOLEAN, FALSE, NULL);

  /* decoder (with unknown caps) */
  msg = gst_missing_decoder_message_new (pipeline, caps);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "decoder");
  fail_unless (gst_structure_has_field_typed (s, "detail", GST_TYPE_CAPS));
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  /* encoder (with unknown caps) */
  msg = gst_missing_encoder_message_new (pipeline, caps);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "encoder");
  fail_unless (gst_structure_has_field_typed (s, "detail", GST_TYPE_CAPS));
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  gst_caps_unref (caps);

  /* create caps that exist */
  caps = gst_caps_new_empty_simple ("video/x-matroska");
  /* decoder (with known caps) */
  msg = gst_missing_decoder_message_new (pipeline, caps);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "decoder");
  fail_unless (gst_structure_has_field_typed (s, "detail", GST_TYPE_CAPS));
  fail_unless (gst_structure_has_field_typed (s, "name", G_TYPE_STRING));
  fail_unless (gst_structure_get_string (s, "name") != NULL);
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  /* encoder (with known caps) */
  msg = gst_missing_encoder_message_new (pipeline, caps);
  fail_unless (msg != NULL);
  fail_unless_equals_int (GST_MESSAGE_TYPE (msg), GST_MESSAGE_ELEMENT);
  fail_unless (gst_message_get_structure (msg) != NULL);
  s = gst_message_get_structure (msg);
  fail_unless (gst_structure_has_name (s, "missing-plugin"));
  fail_unless (gst_structure_has_field_typed (s, "type", G_TYPE_STRING));
  fail_unless_equals_string (gst_structure_get_string (s, "type"), "encoder");
  fail_unless (gst_structure_has_field_typed (s, "detail", GST_TYPE_CAPS));
  fail_unless (gst_structure_has_field_typed (s, "name", G_TYPE_STRING));
  fail_unless (gst_structure_get_string (s, "name") != NULL);
  missing_msg_check_getters (msg);
  gst_message_unref (msg);

  gst_caps_unref (caps);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  gst_object_unref (bus);
}

GST_END_TEST;

GST_START_TEST (test_pb_utils_init)
{
  /* should be fine to call multiple times */
  gst_pb_utils_init ();
  gst_pb_utils_init ();
  gst_pb_utils_init ();
  gst_pb_utils_init ();
}

GST_END_TEST;

static const gchar *caps_strings[] = {
  /* formats with static descriptions */
  "application/ogg", "application/vnd.rn-realmedia", "video/x-fli",
  "video/x-flv", "video/x-matroska", "video/x-ms-asf", "video/x-msvideo",
  "video/x-quicktime", "video/quicktime", "audio/x-ac3", "audio/ac3",
  "audio/x-private-ac3", "audio/x-private1-ac3", "audio/x-adpcm",
  "audio/aiff", "audio/x-alaw", "audio/amr", "audio/AMR", "audio/AMR-WB",
  "audio/iLBC-sh", "audio/ms-gsm", "audio/qcelp", "audio/x-adpcm",
  "audio/x-aiff", "audio/x-alac", "audio/x-amr-nb-sh", "audio/x-amr-wb-sh",
  "audio/x-au", "audio/x-cinepak", "audio/x-dpcm", "audio/x-dts",
  "audio/x-dv", "audio/x-flac", "audio/x-gsm", "audio/x-iec958",
  "audio/x-iLBC", "audio/x-ircam", "audio/x-lpcm", "audio/x-private1-lpcm",
  "audio/x-m4a", "audio/x-mod", "audio/x-mulaw", "audio/x-musepack",
  "audio/x-nist", "audio/x-nsf", "audio/x-paris", "audio/x-qdm2",
  "audio/x-ralf-mpeg4-generic", "audio/x-sds", "audio/x-shorten",
  "audio/x-sid", "audio/x-sipro", "audio/x-spc", "audio/x-speex",
  "audio/x-svx", "audio/x-tta", "audio/x-ttafile",
  "audio/x-vnd.sony.atrac3", "audio/x-vorbis", "audio/x-voc", "audio/x-w64",
  "audio/x-wav", "audio/x-wavpack", "audio/x-wavpack-correction",
  "audio/x-wms", "audio/x-voxware", "audio/x-xi", "video/sp5x", "video/vivo",
  "video/x-4xm", "video/x-apple-video", "video/x-camtasia",
  "video/x-cdxa", "video/x-cinepak", "video/x-cirrus-logic-accupak",
  "video/x-compressed-yuv", "subpicture/x-dvd",
  "video/x-ffv", "video/x-flash-screen", "video/x-flash-video",
  "video/x-h261", "video/x-huffyuv", "video/x-intel-h263", "video/x-jpeg",
  "video/x-mjpeg", "video/x-mjpeg-b", "video/mpegts", "video/x-mng",
  "video/x-mszh", "video/x-msvideocodec", "video/x-mve", "video/x-nut",
  "video/x-nuv", "video/x-qdrw", "video/x-raw", "video/x-smc",
  "video/x-smoke", "video/x-tarkin", "video/x-theora", "video/x-rle",
  "video/x-ultimotion", "video/x-vcd", "video/x-vmnc", "video/x-vp3",
  "video/x-vp5", "video/x-vp6", "video/x-vp6-flash", "video/x-vp7",
  "video/x-zlib", "image/bmp", "image/x-bmp",
  "image/x-MS-bmp", "image/gif", "image/jpeg", "image/jng", "image/png",
  "image/pbm", "image/ppm", "image/svg+xml", "image/tiff",
  "image/x-cmu-raster", "image/x-icon", "image/x-xcf", "image/x-pixmap",
  "image/x-xpixmap", "image/x-quicktime", "image/x-sun-raster",
  "image/x-tga", "video/x-dv", "video/x-dv",
  /* some RTP formats */
  "application/x-rtp, media=(string)video, encoding-name=(string)TimVCodec",
  "application/x-rtp, media=(string)audio, encoding-name=(string)TimACodec",
  "application/x-rtp, media=(string)application, encoding-name=(string)TimMux",
  "application/x-rtp, media=(string)woohoo, encoding-name=(string)TPM",
  /* incomplete RTP formats */
  "application/x-rtp, media=(string)woohoo",
  "application/x-rtp, encoding-name=(string)TPM",
  "application/x-rtp, media=(string)woohoo",
  /* formats with dynamic descriptions */
  "audio/x-adpcm",
  "audio/x-adpcm, layout=(string)dvi",
  "audio/x-adpcm, layout=(string)swf",
  "audio/x-adpcm, layout=(string)microsoft",
  "audio/x-adpcm, layout=(string)quicktime",
  "audio/mpeg, mpegversion=(int)4",
  "audio/mpeg, mpegversion=(int)1, layer=(int)1",
  "audio/mpeg, mpegversion=(int)1, layer=(int)2",
  "audio/mpeg, mpegversion=(int)1, layer=(int)3",
  "audio/mpeg, mpegversion=(int)1, layer=(int)99",
  "audio/mpeg, mpegversion=(int)99",
  "video/mpeg, mpegversion=(int)2, systemstream=(boolean)TRUE",
  "video/mpeg, systemstream=(boolean)FALSE",
  "video/mpeg, mpegversion=(int)2",
  "video/mpeg, mpegversion=(int)1, systemstream=(boolean)FALSE",
  "video/mpeg, mpegversion=(int)2, systemstream=(boolean)FALSE",
  "video/mpeg, mpegversion=(int)4, systemstream=(boolean)FALSE",
  "video/mpeg, mpegversion=(int)99, systemstream=(boolean)TRUE",
  "video/mpeg, mpegversion=(int)99, systemstream=(boolean)FALSE",
  "video/mpeg, mpegversion=(int)4, systemstream=(boolean)FALSE, profile=main",
  "video/mpeg, mpegversion=(int)4, systemstream=(boolean)FALSE, profile=adsfad",
  "video/mpeg",
  "video/x-indeo, indeoversion=(int)3",
  "video/x-indeo, indeoversion=(int)5",
  "video/x-indeo",
  "video/x-wmv, wmvversion=(int)1",
  "video/x-wmv, wmvversion=(int)2",
  "video/x-wmv, wmvversion=(int)3",
  "video/x-wmv, wmvversion=(int)99",
  "video/x-wmv",
  "audio/x-wma, wmaversion=(int)1",
  "audio/x-wma, wmaversion=(int)2",
  "audio/x-wma, wmaversion=(int)3",
  "audio/x-wma, wmaversion=(int)99",
  "audio/x-wma",
  "video/x-dirac",
  "video/x-dirac, profile=(string)vc2-low-delay",
  "video/x-dirac, profile=(string)vc2-simple",
  "video/x-dirac, profile=(string)vc2-main",
  "video/x-dirac, profile=(string)main",
  "video/x-dirac, profile=(string)czvja",
  "video/x-divx, divxversion=(int)3",
  "video/x-divx, divxversion=(int)4",
  "video/x-divx, divxversion=(int)5",
  "video/x-divx, divxversion=(int)99",
  "video/x-divx",
  "video/x-svq, svqversion=(int)1",
  "video/x-svq, svqversion=(int)3",
  "video/x-svq, svqversion=(int)99",
  "video/x-svq",
  "video/x-h265, profile=(string)main",
  "video/x-h265, profile=(string)xafasdf",
  "video/x-h265",
  "video/x-h264, variant=(string)itu",
  "video/x-h264, variant=(string)videosoft",
  "video/x-h264, variant=(string)foobar",
  "video/x-h264",
  "video/x-h264, profile=(string)foobar",
  "video/x-h264, profile=(string)high-4:4:4-intra",
  "video/x-h264, profile=(string)high",
  "video/x-h263, variant=(string)itu",
  "video/x-h263, variant=(string)lead",
  "video/x-h263, variant=(string)microsoft",
  "video/x-h263, variant=(string)vdolive",
  "video/x-h263, variant=(string)vivo",
  "video/x-h263, variant=(string)xirlink",
  "video/x-h263, variant=(string)foobar",
  "video/x-h263",
  "video/x-msmpeg, msmpegversion=(int)41",
  "video/x-msmpeg, msmpegversion=(int)42",
  "video/x-msmpeg, msmpegversion=(int)43",
  "video/x-msmpeg, msmpegversion=(int)99",
  "video/x-msmpeg",
  "video/x-pn-realvideo, rmversion=(int)1",
  "video/x-pn-realvideo, rmversion=(int)2",
  "video/x-pn-realvideo, rmversion=(int)3",
  "video/x-pn-realvideo, rmversion=(int)4",
  "video/x-pn-realvideo, rmversion=(int)99",
  "video/x-pn-realvideo",
  "audio/x-pn-realaudio, raversion=(int)1",
  "audio/x-pn-realaudio, raversion=(int)2",
  "audio/x-pn-realaudio, raversion=(int)99",
  "audio/x-pn-realaudio",
  "audio/x-mace, maceversion=(int)3",
  "audio/x-mace, maceversion=(int)6",
  "audio/x-mace, maceversion=(int)99",
  "audio/x-mace",
  "video/x-truemotion, trueversion=(int)1",
  "video/x-truemotion, trueversion=(int)2",
  "video/x-truemotion, trueversion=(int)99",
  "video/x-truemotion",
  "video/x-asus, asusversion=(int)1",
  "video/x-asus, asusversion=(int)2",
  "video/x-asus, asusversion=(int)99",
  "video/x-asus",
  "video/x-xan, wcversion=(int)1",
  "video/x-xan, wcversion=(int)99",
  "video/x-xan",
  "video/x-ati-vcr, vcrversion=(int)1",
  "video/x-ati-vcr, vcrversion=(int)2",
  "video/x-ati-vcr, vcrversion=(int)99",
  "video/x-ati-vcr",
  /* raw audio */
  "audio/x-raw, format=(string)S16LE, rate=(int)44100, channels=(int)2",
  "audio/x-raw, format=(string)F32,rate=(int)22050, channels=(int)2",
  /* raw video */
  "video/x-raw, format=(string)RGB16, width=(int)320, height=(int)240, framerate=(fraction)30/1, pixel-aspect-ratio=(fraction)1/1",
  "video/x-raw, format=(string)YUY2, width=(int)320, height=(int)240, framerate=(fraction)30/1",
  /* and a made-up format */
  "video/x-tpm"
};

GST_START_TEST (test_pb_utils_get_codec_description)
{
  gint i;

  gst_pb_utils_init ();

  for (i = 0; i < G_N_ELEMENTS (caps_strings); ++i) {
    GstCaps *caps;
    gchar *desc;

    caps = gst_caps_from_string (caps_strings[i]);
    fail_unless (caps != NULL, "could not create caps from string '%s'",
        caps_strings[i]);
    GST_LOG ("Caps %s:", caps_strings[i]);
    desc = gst_pb_utils_get_codec_description (caps);
    fail_unless (desc != NULL);
    GST_LOG (" - codec   : %s", desc);
    fail_unless (g_utf8_validate (desc, -1, NULL));
    g_free (desc);
    desc = gst_pb_utils_get_decoder_description (caps);
    fail_unless (desc != NULL);
    GST_LOG (" - decoder : %s", desc);
    fail_unless (g_utf8_validate (desc, -1, NULL));
    g_free (desc);
    desc = gst_pb_utils_get_encoder_description (caps);
    fail_unless (desc != NULL);
    GST_LOG (" - encoder : %s", desc);
    fail_unless (g_utf8_validate (desc, -1, NULL));
    g_free (desc);
    gst_caps_unref (caps);
  }
}

GST_END_TEST;


GST_START_TEST (test_pb_utils_taglist_add_codec_info)
{
  GstTagList *list;
  GstCaps *caps, *bogus_caps;
  gchar *res;

  gst_pb_utils_init ();
  list = gst_tag_list_new_empty ();
  caps = gst_caps_new_empty_simple ("video/x-theora");
  ASSERT_CRITICAL (fail_if
      (gst_pb_utils_add_codec_description_to_tag_list (NULL,
              GST_TAG_VIDEO_CODEC, caps)));
  ASSERT_CRITICAL (fail_if
      (gst_pb_utils_add_codec_description_to_tag_list (list, "asdfa", caps)));
  ASSERT_CRITICAL (fail_if
      (gst_pb_utils_add_codec_description_to_tag_list (list,
              GST_TAG_IMAGE, caps)));
  ASSERT_CRITICAL (fail_if
      (gst_pb_utils_add_codec_description_to_tag_list (list,
              GST_TAG_VIDEO_CODEC, NULL)));

  /* Try adding bogus caps (should fail) */
  bogus_caps = gst_caps_new_empty_simple ("bogus/format");
  fail_if (gst_pb_utils_add_codec_description_to_tag_list (list,
          GST_TAG_VIDEO_CODEC, bogus_caps));
  gst_caps_unref (bogus_caps);

  /* Try adding valid caps with known tag */
  fail_unless (gst_pb_utils_add_codec_description_to_tag_list (list,
          GST_TAG_VIDEO_CODEC, caps));
  fail_if (gst_tag_list_is_empty (list));
  fail_unless (gst_tag_list_get_string (list, GST_TAG_VIDEO_CODEC, &res));
  g_free (res);
  gst_tag_list_unref (list);

  /* Try adding valid caps with auto-tag (for video, audio, subtitle, generic) */
  list = gst_tag_list_new_empty ();
  fail_unless (gst_pb_utils_add_codec_description_to_tag_list (list, NULL,
          caps));
  fail_if (gst_tag_list_is_empty (list));
  fail_unless (gst_tag_list_get_string (list, GST_TAG_VIDEO_CODEC, &res));
  g_free (res);
  gst_tag_list_unref (list);
  gst_caps_unref (caps);

  list = gst_tag_list_new_empty ();
  caps = gst_caps_new_empty_simple ("audio/x-vorbis");
  fail_unless (gst_pb_utils_add_codec_description_to_tag_list (list, NULL,
          caps));
  fail_if (gst_tag_list_is_empty (list));
  fail_unless (gst_tag_list_get_string (list, GST_TAG_AUDIO_CODEC, &res));
  g_free (res);
  gst_tag_list_unref (list);
  gst_caps_unref (caps);

  list = gst_tag_list_new_empty ();
  caps = gst_caps_new_empty_simple ("subtitle/x-kate");
  fail_unless (gst_pb_utils_add_codec_description_to_tag_list (list, NULL,
          caps));
  fail_if (gst_tag_list_is_empty (list));
  fail_unless (gst_tag_list_get_string (list, GST_TAG_SUBTITLE_CODEC, &res));
  g_free (res);
  gst_tag_list_unref (list);
  gst_caps_unref (caps);

  list = gst_tag_list_new_empty ();
  caps = gst_caps_new_empty_simple ("application/ogg");
  fail_unless (gst_pb_utils_add_codec_description_to_tag_list (list, NULL,
          caps));
  fail_if (gst_tag_list_is_empty (list));
  fail_unless (gst_tag_list_get_string (list, GST_TAG_CONTAINER_FORMAT, &res));
  g_free (res);
  gst_tag_list_unref (list);
  gst_caps_unref (caps);

  list = gst_tag_list_new_empty ();
  caps = gst_caps_new_empty_simple ("image/bmp");
  fail_unless (gst_pb_utils_add_codec_description_to_tag_list (list, NULL,
          caps));
  fail_if (gst_tag_list_is_empty (list));
  fail_unless (gst_tag_list_get_string (list, GST_TAG_CODEC, &res));
  g_free (res);
  gst_tag_list_unref (list);
  gst_caps_unref (caps);
}

GST_END_TEST;

static gint marker;

static void
result_cb (GstInstallPluginsReturn result, gpointer user_data)
{
  GST_LOG ("result = %u, user_data = %p", result, user_data);

  fail_unless (user_data == (gpointer) & marker);

  marker = result;
}

#define SCRIPT_NO_XID \
    "#!/bin/sh\n"                                  \
    "if test x$1 != xdetail1; then exit 21; fi;\n" \
    "if test x$2 != xdetail2; then exit 22; fi;\n" \
    "exit 1\n"

#define SCRIPT_WITH_XID \
    "#!/bin/sh\n"                                  \
    "if test x$1 != 'x--transient-for=42'; then exit 21; fi;\n"      \
    "if test x$2 != xdetail1; then exit 22; fi;\n" \
    "if test x$3 != xdetail2; then exit 23; fi;\n" \
    "exit 0\n"

/* make sure our script gets called with the right parameters */
static void
test_pb_utils_install_plugins_do_callout (const gchar * const *details,
    GstInstallPluginsContext * ctx, const gchar * script,
    GstInstallPluginsReturn expected_result)
{
#ifdef G_OS_UNIX
  GstInstallPluginsReturn ret;
  GError *err = NULL;
  gchar *path;

  path = g_strdup_printf ("%s/gst-plugins-base-unit-test-helper.%s.%lu",
      g_get_tmp_dir (), (g_get_user_name ())? g_get_user_name () : "nobody",
      (gulong) getpid ());

  if (!g_file_set_contents (path, script, -1, &err)) {
    GST_DEBUG ("Failed to write test script to %s: %s", path, err->message);
    g_error_free (err);
    goto done;
  }

  if (chmod (path, S_IRUSR | S_IWUSR | S_IXUSR) != 0) {
    GST_DEBUG ("Could not set mode u+rwx on '%s'", path);
    goto done;
  }

  /* test gst_install_plugins_supported() I */
  g_setenv ("GST_INSTALL_PLUGINS_HELPER", "/i/do/not/ex.ist!", 1);
  fail_if (gst_install_plugins_supported ());

  GST_LOG ("setting GST_INSTALL_PLUGINS_HELPER to '%s'", path);
  g_setenv ("GST_INSTALL_PLUGINS_HELPER", path, 1);

  /* test gst_install_plugins_supported() II */
  fail_unless (gst_install_plugins_supported ());

  /* test sync callout */
  ret = gst_install_plugins_sync (details, ctx);
  fail_unless (ret == GST_INSTALL_PLUGINS_HELPER_MISSING ||
      ret == expected_result,
      "gst_install_plugins_sync() failed with unexpected ret %d, which is "
      "neither HELPER_MISSING nor %d", ret, expected_result);

  /* test async callout */
  marker = -333;
  ret = gst_install_plugins_async (details, ctx, result_cb,
      (gpointer) & marker);
  fail_unless (ret == GST_INSTALL_PLUGINS_HELPER_MISSING ||
      ret == GST_INSTALL_PLUGINS_STARTED_OK,
      "gst_install_plugins_async() failed with unexpected ret %d", ret);
  if (ret == GST_INSTALL_PLUGINS_STARTED_OK) {
    while (marker == -333) {
      g_usleep (500);
      g_main_context_iteration (NULL, FALSE);
    }
    /* and check that the callback was called with the expected code */
    fail_unless_equals_int (marker, expected_result);
  }

done:

  unlink (path);
  g_free (path);
#endif /* G_OS_UNIX */
}

GST_START_TEST (test_pb_utils_install_plugins)
{
  GstInstallPluginsContext *ctx;
  GstInstallPluginsReturn ret;
  const gchar *details[] = { "detail1", "detail2", NULL };
  const gchar *details_multi[] = { "detail1", "detail1", "detail2", NULL };

  ctx = gst_install_plugins_context_new ();

  ASSERT_CRITICAL (ret = gst_install_plugins_sync (NULL, ctx));
  ASSERT_CRITICAL (ret =
      gst_install_plugins_async (NULL, ctx, result_cb, (gpointer) & marker));
  ASSERT_CRITICAL (ret =
      gst_install_plugins_async (details, ctx, NULL, (gpointer) & marker));

  /* make sure the functions return the right error code if the helper does
   * not exist */
  g_setenv ("GST_INSTALL_PLUGINS_HELPER", "/does/not/ex/is.t", 1);
  ret = gst_install_plugins_sync (details, NULL);
  fail_unless_equals_int (ret, GST_INSTALL_PLUGINS_HELPER_MISSING);

  marker = -333;
  ret =
      gst_install_plugins_async (details, NULL, result_cb, (gpointer) & marker);
  fail_unless_equals_int (ret, GST_INSTALL_PLUGINS_HELPER_MISSING);
  /* and check that the callback wasn't called */
  fail_unless_equals_int (marker, -333);

  /* now make sure our scripts are actually called as expected (if possible) */
  test_pb_utils_install_plugins_do_callout (details, NULL, SCRIPT_NO_XID,
      GST_INSTALL_PLUGINS_NOT_FOUND);

  /* and again with context */
  gst_install_plugins_context_set_xid (ctx, 42);
  test_pb_utils_install_plugins_do_callout (details, ctx, SCRIPT_WITH_XID,
      GST_INSTALL_PLUGINS_SUCCESS);

  /* and make sure that duplicate detail strings get dropped */
  test_pb_utils_install_plugins_do_callout (details_multi, NULL, SCRIPT_NO_XID,
      GST_INSTALL_PLUGINS_NOT_FOUND);

  /* and the same again with context */
  gst_install_plugins_context_set_xid (ctx, 42);
  test_pb_utils_install_plugins_do_callout (details_multi, ctx, SCRIPT_WITH_XID,
      GST_INSTALL_PLUGINS_SUCCESS);

  /* and free the context now that we don't need it any longer */
  gst_install_plugins_context_free (ctx);

  /* completely silly test to check gst_install_plugins_return_get_name()
   * is somewhat well-behaved */
  {
    gint i;

    for (i = -99; i < 16738; ++i) {
      const gchar *s;

      s = gst_install_plugins_return_get_name ((GstInstallPluginsReturn) i);
      fail_unless (s != NULL);
      /* GST_LOG ("%5d = %s", i, s); */
    }
  }
}

GST_END_TEST;

GST_START_TEST (test_pb_utils_installer_details)
{
  GstMessage *msg;
  GstElement *el;
  GstCaps *caps;
  gchar *detail1, *detail2;

  el = gst_pipeline_new ("dummy-element");

  /* uri source */
  detail1 = gst_missing_uri_source_installer_detail_new ("http");
  fail_unless (detail1 != NULL);
  fail_unless (g_str_has_prefix (detail1, "gstreamer|1.0|"));
  fail_unless (g_str_has_suffix (detail1, "|urisource-http"));
  msg = gst_missing_uri_source_message_new (el, "http");
  fail_unless (msg != NULL);
  detail2 = gst_missing_plugin_message_get_installer_detail (msg);
  fail_unless (detail2 != NULL);
  gst_message_unref (msg);
  fail_unless_equals_string (detail1, detail2);
  g_free (detail1);
  g_free (detail2);

  /* uri sink */
  detail1 = gst_missing_uri_sink_installer_detail_new ("http");
  fail_unless (detail1 != NULL);
  fail_unless (g_str_has_prefix (detail1, "gstreamer|1.0|"));
  fail_unless (g_str_has_suffix (detail1, "|urisink-http"));
  msg = gst_missing_uri_sink_message_new (el, "http");
  fail_unless (msg != NULL);
  detail2 = gst_missing_plugin_message_get_installer_detail (msg);
  fail_unless (detail2 != NULL);
  gst_message_unref (msg);
  fail_unless_equals_string (detail1, detail2);
  g_free (detail1);
  g_free (detail2);

  /* element */
  detail1 = gst_missing_element_installer_detail_new ("deinterlace");
  fail_unless (detail1 != NULL);
  fail_unless (g_str_has_prefix (detail1, "gstreamer|1.0|"));
  fail_unless (g_str_has_suffix (detail1, "|element-deinterlace"));
  msg = gst_missing_element_message_new (el, "deinterlace");
  fail_unless (msg != NULL);
  detail2 = gst_missing_plugin_message_get_installer_detail (msg);
  fail_unless (detail2 != NULL);
  gst_message_unref (msg);
  fail_unless_equals_string (detail1, detail2);
  g_free (detail1);
  g_free (detail2);

  /* decoder */
  caps = gst_caps_new_simple ("audio/x-spiffy", "spiffyversion", G_TYPE_INT,
      2, "channels", G_TYPE_INT, 6, NULL);
  detail1 = gst_missing_decoder_installer_detail_new (caps);
  fail_unless (detail1 != NULL);
  fail_unless (g_str_has_prefix (detail1, "gstreamer|1.0|"));
  fail_unless (g_str_has_suffix (detail1,
          "|decoder-audio/x-spiffy, spiffyversion=(int)2"));
  msg = gst_missing_decoder_message_new (el, caps);
  fail_unless (msg != NULL);
  detail2 = gst_missing_plugin_message_get_installer_detail (msg);
  fail_unless (detail2 != NULL);
  gst_message_unref (msg);
  gst_caps_unref (caps);
  fail_unless_equals_string (detail1, detail2);
  g_free (detail1);
  g_free (detail2);

  /* encoder */
  caps = gst_caps_new_simple ("audio/x-spiffy", "spiffyversion", G_TYPE_INT,
      2, "channels", G_TYPE_INT, 6, NULL);
  detail1 = gst_missing_encoder_installer_detail_new (caps);
  fail_unless (g_str_has_prefix (detail1, "gstreamer|1.0|"));
  fail_unless (g_str_has_suffix (detail1,
          "|encoder-audio/x-spiffy, spiffyversion=(int)2"));
  fail_unless (detail1 != NULL);
  msg = gst_missing_encoder_message_new (el, caps);
  fail_unless (msg != NULL);
  detail2 = gst_missing_plugin_message_get_installer_detail (msg);
  fail_unless (detail2 != NULL);
  gst_message_unref (msg);
  gst_caps_unref (caps);
  fail_unless_equals_string (detail1, detail2);
  g_free (detail1);
  g_free (detail2);

  gst_object_unref (el);
}

GST_END_TEST;

GST_START_TEST (test_pb_utils_versions)
{
  gchar *s;
  guint maj, min, mic, nano;

  gst_plugins_base_version (NULL, NULL, NULL, NULL);
  gst_plugins_base_version (&maj, &min, &mic, &nano);
  fail_unless_equals_int (maj, GST_PLUGINS_BASE_VERSION_MAJOR);
  fail_unless_equals_int (min, GST_PLUGINS_BASE_VERSION_MINOR);
  fail_unless_equals_int (mic, GST_PLUGINS_BASE_VERSION_MICRO);
  fail_unless_equals_int (nano, GST_PLUGINS_BASE_VERSION_NANO);

  s = gst_plugins_base_version_string ();
  if (GST_PLUGINS_BASE_VERSION_NANO == 0) {
    fail_if (strstr (s, "GIT") || strstr (s, "git") || strstr (s, "prerel"));
  }
  if (GST_PLUGINS_BASE_VERSION_NANO == 1) {
    fail_unless (strstr (s, "GIT") || strstr (s, "git"));
  }
  if (GST_PLUGINS_BASE_VERSION_NANO >= 2) {
    fail_unless (strstr (s, "Prerelease") || strstr (s, "prerelease"));
  }
  g_free (s);
}

GST_END_TEST;

static Suite *
libgstpbutils_suite (void)
{
  Suite *s = suite_create ("pbutils library");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_pb_utils_init);
  tcase_add_test (tc_chain, test_pb_utils_post_missing_messages);
  tcase_add_test (tc_chain, test_pb_utils_taglist_add_codec_info);
  tcase_add_test (tc_chain, test_pb_utils_get_codec_description);
  tcase_add_test (tc_chain, test_pb_utils_install_plugins);
  tcase_add_test (tc_chain, test_pb_utils_installer_details);
  tcase_add_test (tc_chain, test_pb_utils_versions);
  return s;
}

GST_CHECK_MAIN (libgstpbutils);
