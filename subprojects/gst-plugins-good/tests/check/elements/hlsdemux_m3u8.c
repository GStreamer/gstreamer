/* GStreamer
 *
 * unit test for hlsdemux
 *
 * Copyright (C) <2012> Fluendo S.A <support@fluendo.com>
 *  Authors: Andoni Morales Alastruey <amorales@fluendo.com>
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
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

#include <gst/check/gstcheck.h>

#undef GST_CAT_DEFAULT
#include "m3u8.h"
#include "m3u8.c"

GST_DEBUG_CATEGORY (hls2_debug);

static const gchar *INVALID_PLAYLIST = "#EXTM3 UINVALID";

static const gchar *ON_DEMAND_PLAYLIST = "#EXTM3U \n\
#EXT-X-TARGETDURATION:10\n\
#EXTINF:10,Test\n\
http://media.example.com/001.ts\n\
#EXTINF:10,Test\n\
http://media.example.com/002.ts\n\
#EXTINF:10,Test\n\
http://media.example.com/003.ts\n\
#EXTINF:10,Test\n\
http://media.example.com/004.ts\n\
#EXT-X-ENDLIST";

static const gchar *DOUBLES_PLAYLIST = "#EXTM3U \n\
#EXT-X-TARGETDURATION:10\n\
#EXTINF:10.321,Test\n\
http://media.example.com/001.ts\n\
#EXTINF:9.6789,Test\n\
http://media.example.com/002.ts\n\
#EXTINF:10.2344,Test\n\
http://media.example.com/003.ts\n\
#EXTINF:9.92,Test\n\
http://media.example.com/004.ts\n\
#EXT-X-ENDLIST";

static const gchar *LIVE_PLAYLIST = "#EXTM3U\n\
#EXT-X-TARGETDURATION:8\n\
#EXT-X-MEDIA-SEQUENCE:2680\n\
\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence2680.ts\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence2681.ts\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence2682.ts\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence2683.ts";

static const gchar *LIVE_ROTATED_PLAYLIST = "#EXTM3U\n\
#EXT-X-TARGETDURATION:8\n\
#EXT-X-MEDIA-SEQUENCE:3001\n\
\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence3001.ts\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence3002.ts\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence3003.ts\n\
#EXTINF:8,\n\
https://priv.example.com/fileSequence3004.ts";

static const gchar *VARIANT_PLAYLIST = "#EXTM3U \n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=128000\n\
http://example.com/low.m3u8\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=256000\n\
http://example.com/mid.m3u8\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=768000\n\
http://example.com/hi.m3u8\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\n\
http://example.com/audio-only.m3u8";

static const gchar *VARIANT_PLAYLIST_WITH_URI_MISSING = "#EXTM3U \n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=128000\n\
http://example.com/low.m3u8\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=256000\n\
\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=768000\n\
http://example.com/hi.m3u8\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\n\
http://example.com/audio-only.m3u8";

static const gchar *EMPTY_LINES_VARIANT_PLAYLIST = "#EXTM3U \n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=128000\n\n\
http://example.com/low.m3u8\n\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=256000\n\n\
http://example.com/mid.m3u8\n\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=768000\n\n\
http://example.com/hi.m3u8\n\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\n\n\
http://example.com/audio-only.m3u8";

static const gchar *WINDOWS_EMPTY_LINES_VARIANT_PLAYLIST = "#EXTM3U \r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=128000\r\n\r\n\
http://example.com/low.m3u8\r\n\r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=256000\r\n\r\n\
http://example.com/mid.m3u8\r\n\r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=768000\r\n\r\n\
http://example.com/hi.m3u8\r\n\r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\r\n\r\n\
http://example.com/audio-only.m3u8";

static const gchar *EMPTY_LINES_PLAYLIST = "#EXTM3U \n\n\
#EXT-X-TARGETDURATION:10\n\
#EXTINF:10,Testr\n\n\
http://media.example.com/001.ts\n\n\
#EXTINF:10,Test\n\n\
http://media.example.com/002.ts\n\n\
#EXTINF:10,Test\n\n\
http://media.example.com/003.ts\n\n\
#EXTINF:10,Test\n\n\
http://media.example.com/004.ts\n\n\
#EXT-X-ENDLIST";

static const gchar *WINDOWS_EMPTY_LINES_PLAYLIST = "#EXTM3U \r\n\
#EXT-X-TARGETDURATION:10\r\n\r\n\
#EXTINF:10,Test\r\n\r\n\
http://media.example.com/001.ts\r\n\r\n\
#EXTINF:10,Test\r\n\r\n\
http://media.example.com/002.ts\r\n\r\n\
#EXTINF:10,Test\r\n\r\n\
http://media.example.com/003.ts\r\n\r\n\
#EXTINF:10,Test\r\n\r\n\
http://media.example.com/004.ts\r\n\r\n\
#EXT-X-ENDLIST";

static const gchar *BYTE_RANGES_PLAYLIST = "#EXTM3U \n\
#EXT-X-TARGETDURATION:40\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000@100\n\
http://media.example.com/all.ts\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000@1000\n\
http://media.example.com/all.ts\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000@2000\n\
http://media.example.com/all.ts\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000@3000\n\
http://media.example.com/all.ts\n\
#EXT-X-ENDLIST";

static const gchar *BYTE_RANGES_ACC_OFFSET_PLAYLIST = "#EXTM3U \n\
#EXT-X-TARGETDURATION:40\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000\n\
http://media.example.com/all.ts\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000\n\
http://media.example.com/all.ts\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000\n\
http://media.example.com/all.ts\n\
#EXTINF:10,Test\n\
#EXT-X-BYTERANGE:1000\n\
http://media.example.com/all.ts\n\
#EXT-X-ENDLIST";

static const gchar *AES_128_ENCRYPTED_PLAYLIST = "#EXTM3U \n\
#EXT-X-TARGETDURATION:10\n\
#EXTINF:10,Test\n\
http://media.example.com/mid/video-only-001.ts\n\
#EXT-X-KEY:METHOD=NONE\n\
#EXTINF:10,Test\n\
http://media.example.com/mid/video-only-002.ts\n\
#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.bin\"\n\
#EXTINF:10,Test\n\
http://media.example.com/mid/video-only-003.ts\n\
#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key2.bin\",IV=0x00000000000000000000000000000001\n\
#EXTINF:10,Test\n\
http://media.example.com/mid/video-only-004.ts\n\
#EXTINF:10,Test\n\
http://media.example.com/mid/video-only-005.ts\n\
#EXT-X-ENDLIST";

static const gchar *WINDOWS_LINE_ENDINGS_PLAYLIST = "#EXTM3U \r\n\
#EXT-X-TARGETDURATION:10\r\n\
#EXTINF:10,Test\r\n\
http://media.example.com/001.ts\r\n\
#EXTINF:10,Test\r\n\
http://media.example.com/002.ts\r\n\
#EXTINF:10,Test\r\n\
http://media.example.com/003.ts\r\n\
#EXTINF:10,Test\r\n\
http://media.example.com/004.ts\r\n\
#EXT-X-ENDLIST";

static const gchar *WINDOWS_LINE_ENDINGS_VARIANT_PLAYLIST = "#EXTM3U \r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=128000\r\n\
http://example.com/low.m3u8\r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=256000\r\n\
http://example.com/mid.m3u8\r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=768000\r\n\
http://example.com/hi.m3u8\r\n\
#EXT-X-STREAM-INF:PROGRAM-ID=1,BANDWIDTH=65000,CODECS=\"mp4a.40.5\"\r\n\
http://example.com/audio-only.m3u8";

static const gchar *MAP_TAG_PLAYLIST = "#EXTM3U \n\
#EXT-X-VERSION:7\n\
#EXT-X-MAP:URI=\"init1.mp4\",BYTERANGE=\"50@50\"\n\
#EXTINF:6.00000,\n\
#EXT-X-BYTERANGE:100@50\n\
main.mp4\n\
#EXTINF:6.00000,\n\
#EXT-X-BYTERANGE:100@150\n\
main.mp4\n\
#EXT-X-MAP:URI=\"init2.mp4\"\n\
#EXTINF:6.00000,\n\
#EXT-X-BYTERANGE:100@300\n\
main.mp4\n\
#EXT-X-ENDLIST";

static const gchar *LOW_LATENCY_PLAYLIST = "#EXTM3U\n\
#EXT-X-VERSION:7\n\
#EXT-X-TARGETDURATION:4\n\
#EXT-X-PART-INF:PART-TARGET=2\n\
#EXTINF:4.00008,\n\
fileSequence268.mp4\n\
#EXTINF:4.00008,\n\
fileSequence269.mp4\n\
#EXTINF:4.00008,\n\
fileSequence270.mp4\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"filePart271.0.mp4\"\n\
#EXT-X-PART:DURATION=2.00004,URI=\"filePart271.1.mp4\"\n\
#EXTINF:4.00008,\n\
fileSequence271.mp4\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"filePart272.0.mp4\"\n\
#EXT-X-PART:DURATION=0.50001,URI=\"filePart272.1.mp4\"\n\
#EXTINF:2.50005,\n\
fileSequence272.mp4\n\
#EXT-X-DISCONTINUITY\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"midRoll273.0.mp4\"\n\
#EXT-X-PART:DURATION=2.00004,URI=\"midRoll273.1.mp4\"\n\
#EXTINF:4.00008,\n\
midRoll273.mp4\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"midRoll274.0.mp4\"\n\
#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"midRoll274.1.mp4\"\n\
#EXT-X-RENDITION-REPORT:URI=\"/1M/LL-HLS.m3u8\",LAST-MSN=274,LAST-PART=1";

static const gchar *SKIP_PLAYLIST = "#EXTM3U\n\
#EXT-X-VERSION:7\n\
#EXT-X-TARGETDURATION:4\n\
#EXT-X-PART-INF:PART-TARGET=2\n\
#EXT-X-SKIP:SKIPPED-SEGMENTS=2,RECENTLY-REMOVED-DATERANGES=\"splice-6FFFFFF0\tsplice-6FFFFFF1\"\n\
#EXTINF:4.00008,\n\
fileSequence270.mp4\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"filePart271.0.mp4\"\n\
#EXT-X-PART:DURATION=2.00004,URI=\"filePart271.1.mp4\"\n\
#EXTINF:4.00008,\n\
fileSequence271.mp4\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"filePart272.0.mp4\"\n\
#EXT-X-PART:DURATION=0.50001,URI=\"filePart272.1.mp4\"\n\
#EXTINF:2.50005,\n\
fileSequence272.mp4\n\
#EXT-X-DISCONTINUITY\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"midRoll273.0.mp4\"\n\
#EXT-X-PART:DURATION=2.00004,URI=\"midRoll273.1.mp4\"\n\
#EXTINF:4.00008,\n\
midRoll273.mp4\n\
#EXT-X-PART:DURATION=2.00004,INDEPENDENT=YES,URI=\"midRoll274.0.mp4\"\n\
#EXT-X-PRELOAD-HINT:TYPE=PART,URI=\"midRoll274.1.mp4\"\n\
#EXT-X-RENDITION-REPORT:URI=\"/1M/LL-HLS.m3u8\",LAST-MSN=274,LAST-PART=1";

static GstHLSMediaPlaylist *
load_m3u8 (const gchar * data)
{
  GstHLSMediaPlaylist *playlist;

  playlist = gst_hls_media_playlist_parse (g_strdup (data),
      GST_CLOCK_TIME_NONE, "http://localhost/test.m3u8", NULL);
  fail_unless (playlist != NULL);

  return playlist;
}

static GstHLSMasterPlaylist *
load_master_playlist (const gchar * data)
{
  GstHLSMasterPlaylist *master;

  master = gst_hls_master_playlist_new_from_data (g_strdup (data),
      "http://localhost/test.m3u8");
  fail_unless (master != NULL);

  return master;
}

GST_START_TEST (test_load_main_playlist_invalid)
{
  GstHLSMasterPlaylist *master;

  master =
      gst_hls_master_playlist_new_from_data (g_strdup (INVALID_PLAYLIST), NULL);
  fail_unless (master == NULL);
}

GST_END_TEST;

GST_START_TEST (test_load_main_playlist_rendition)
{
  GstHLSMediaPlaylist *playlist;

  playlist = load_m3u8 (ON_DEMAND_PLAYLIST);

  assert_equals_int (playlist->segments->len, 4);
  assert_equals_int (playlist->version, 1);

  gst_hls_media_playlist_unref (playlist);
}

GST_END_TEST;

static void
do_test_load_main_playlist_variant (const gchar * playlist)
{
  GstHLSMasterPlaylist *master;
  GstHLSVariantStream *stream;
  GList *tmp;

  master = load_master_playlist (playlist);

  assert_equals_int (g_list_length (master->variants), 4);

  /* Audio-Only */
  tmp = g_list_first (master->variants);
  stream = tmp->data;
  assert_equals_int (stream->bandwidth, 65000);
  assert_equals_int (stream->program_id, 1);
  assert_equals_string (stream->uri, "http://example.com/audio-only.m3u8");
  assert_equals_string (stream->codecs, "mp4a.40.5");

  /* Low */
  tmp = g_list_next (tmp);
  stream = tmp->data;
  assert_equals_int (stream->bandwidth, 128000);
  assert_equals_int (stream->program_id, 1);
  assert_equals_string (stream->uri, "http://example.com/low.m3u8");

  /* Mid */
  tmp = g_list_next (tmp);
  stream = tmp->data;
  assert_equals_int (stream->bandwidth, 256000);
  assert_equals_int (stream->program_id, 1);
  assert_equals_string (stream->uri, "http://example.com/mid.m3u8");

  /* High */
  tmp = g_list_next (tmp);
  stream = tmp->data;
  assert_equals_int (stream->bandwidth, 768000);
  assert_equals_int (stream->program_id, 1);
  assert_equals_string (stream->uri, "http://example.com/hi.m3u8");

  /* Check the first playlist is selected */
  assert_equals_int (master->default_variant != NULL, TRUE);
  assert_equals_int (master->default_variant->bandwidth, 128000);

  gst_hls_master_playlist_unref (master);
}

GST_START_TEST (test_load_main_playlist_variant)
{
  do_test_load_main_playlist_variant (VARIANT_PLAYLIST);
}

GST_END_TEST;

GST_START_TEST (test_load_main_playlist_variant_with_missing_uri)
{
  GstHLSMasterPlaylist *master;
  master = load_master_playlist (VARIANT_PLAYLIST_WITH_URI_MISSING);

  assert_equals_int (g_list_length (master->variants), 3);
  gst_hls_master_playlist_unref (master);
}

GST_END_TEST;

GST_START_TEST (test_load_windows_line_endings_variant_playlist)
{
  do_test_load_main_playlist_variant (WINDOWS_LINE_ENDINGS_VARIANT_PLAYLIST);
}

GST_END_TEST;

GST_START_TEST (test_load_main_playlist_with_empty_lines)
{
  do_test_load_main_playlist_variant (EMPTY_LINES_VARIANT_PLAYLIST);
}

GST_END_TEST;

GST_START_TEST (test_load_windows_main_playlist_with_empty_lines)
{
  do_test_load_main_playlist_variant (WINDOWS_EMPTY_LINES_VARIANT_PLAYLIST);
}

GST_END_TEST;

static void
check_on_demand_playlist (const gchar * data)
{
  GstHLSMediaPlaylist *pl;
  GstM3U8MediaSegment *file;

  pl = load_m3u8 (data);

  /* Sequence should be 0 as it's an ondemand playlist */
  assert_equals_int (pl->media_sequence, 0);
  /* Check that we are not live */
  assert_equals_int (gst_hls_media_playlist_is_live (pl), FALSE);
  /* Check number of entries */
  assert_equals_int (pl->segments->len, 4);
  /* Check first media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  assert_equals_string (file->uri, "http://media.example.com/001.ts");
  assert_equals_int (file->sequence, 0);
  /* Check last media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 3));
  assert_equals_string (file->uri, "http://media.example.com/004.ts");
  assert_equals_int (file->sequence, 3);

  gst_hls_media_playlist_unref (pl);
}

GST_START_TEST (test_on_demand_playlist)
{
  check_on_demand_playlist (ON_DEMAND_PLAYLIST);
}

GST_END_TEST;

GST_START_TEST (test_windows_line_endings_playlist)
{
  check_on_demand_playlist (WINDOWS_LINE_ENDINGS_PLAYLIST);
}

GST_END_TEST;

GST_START_TEST (test_empty_lines_playlist)
{
  check_on_demand_playlist (EMPTY_LINES_PLAYLIST);
}

GST_END_TEST;

GST_START_TEST (test_windows_empty_lines_playlist)
{
  check_on_demand_playlist (WINDOWS_EMPTY_LINES_PLAYLIST);
}

GST_END_TEST;

/* This test is for live streams in which we pause the stream for more than the
 * DVR window and we resume playback. The playlist has rotated completely and
 * there is a jump in the media sequence that must be handled correctly. */
GST_START_TEST (test_live_playlist_rotated)
{
  GstHLSMediaPlaylist *pl;
  GstM3U8MediaSegment *file, *file2;

  pl = load_m3u8 (LIVE_PLAYLIST);

  /* Check first media segment */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  assert_equals_int (file->sequence, 2680);
  gst_m3u8_media_segment_ref (file);
  gst_hls_media_playlist_unref (pl);

  pl = load_m3u8 (LIVE_ROTATED_PLAYLIST);
  file2 = gst_hls_media_playlist_sync_to_segment (pl, file);
  /* We can't sync the previous media plyalist to the new one, they are
   * disconnected. A new synchronization point will be established later.
   */
  fail_unless (file2 == NULL);
  gst_m3u8_media_segment_unref (file);

  /* FIXME: Sequence should last - 3. Should it? */
  /* Check first media segment */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  assert_equals_int (file->sequence, 3001);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_playlist_with_doubles_duration)
{
  GstHLSMediaPlaylist *pl;
  GstM3U8MediaSegment *file;
  gint64 start = -1;
  gint64 stop = -1;

  pl = load_m3u8 (DOUBLES_PLAYLIST);

  /* Check first media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  assert_equals_float (file->duration / (double) GST_SECOND, 10.321);
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 1));
  assert_equals_float (file->duration / (double) GST_SECOND, 9.6789);
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 2));
  assert_equals_float (file->duration / (double) GST_SECOND, 10.2344);
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 3));
  assert_equals_float (file->duration / (double) GST_SECOND, 9.92);
  fail_unless (gst_hls_media_playlist_get_seek_range (pl, &start, &stop));
  assert_equals_int64 (start, 0);
  assert_equals_float (stop / (double) GST_SECOND,
      10.321 + 9.6789 + 10.2344 + 9.92);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_playlist_with_encryption)
{
  GstHLSMediaPlaylist *pl;
  GstM3U8MediaSegment *file;
  guint8 iv1[16] = { 0, };
  guint8 iv2[16] = { 0, };

  iv1[15] = 1;
  iv2[15] = 2;

  pl = load_m3u8 (AES_128_ENCRYPTED_PLAYLIST);

  assert_equals_int (pl->segments->len, 5);

  /* Check all media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  fail_unless (file->key == NULL);

  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 1));
  fail_unless (file->key == NULL);

  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 2));
  fail_unless (file->key != NULL);
  assert_equals_string (file->key, "https://priv.example.com/key.bin");
  fail_unless (memcmp (&file->iv, iv2, 16) == 0);

  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 3));
  fail_unless (file->key != NULL);
  assert_equals_string (file->key, "https://priv.example.com/key2.bin");
  fail_unless (memcmp (&file->iv, iv1, 16) == 0);

  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 4));
  fail_unless (file->key != NULL);
  assert_equals_string (file->key, "https://priv.example.com/key2.bin");
  fail_unless (memcmp (&file->iv, iv1, 16) == 0);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_parse_invalid_playlist)
{
  GstHLSMediaPlaylist *pl;

  pl = gst_hls_media_playlist_parse (g_strdup ("#INVALID"),
      GST_CLOCK_TIME_NONE, "http://localhost/test.m3u8", NULL);
  fail_if (pl != NULL);
}

GST_END_TEST;

GST_START_TEST (test_sync_playlist_to_segment)
{
  GstHLSMediaPlaylist *pl;
  gchar *live_pl;
  GstM3U8MediaSegment *file, *file2;

  /* Test updates in live playlists */
  pl = load_m3u8 (LIVE_PLAYLIST);
  assert_equals_int (pl->segments->len, 4);
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  gst_m3u8_media_segment_ref (file);
  gst_hls_media_playlist_unref (pl);

  /* Add a new entry to the playlist and check the update */
  live_pl = g_strdup_printf ("%s\n%s\n%s", LIVE_PLAYLIST, "#EXTINF:8",
      "https://priv.example.com/fileSequence2684.ts");
  pl = load_m3u8 (live_pl);
  fail_unless (pl != NULL);
  g_free (live_pl);
  file2 = gst_hls_media_playlist_sync_to_segment (pl, file);
  fail_unless (file2 != NULL);
  gst_m3u8_media_segment_unref (file);
  assert_equals_int (pl->segments->len, 5);
  gst_hls_media_playlist_unref (pl);

  /* Test sliding window */
  pl = load_m3u8 (LIVE_PLAYLIST);
  file = gst_hls_media_playlist_sync_to_segment (pl, file2);
  fail_unless (file != NULL);
  gst_m3u8_media_segment_unref (file);
  gst_m3u8_media_segment_unref (file2);
  assert_equals_int (pl->segments->len, 4);
  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_playlist_media_files)
{
  GstHLSMediaPlaylist *pl;
  GstM3U8MediaSegment *file;

  pl = load_m3u8 (ON_DEMAND_PLAYLIST);

  /* Check number of entries */
  assert_equals_int (pl->segments->len, 4);
  /* Check first media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  assert_equals_string (file->uri, "http://media.example.com/001.ts");
  assert_equals_int (file->sequence, 0);
  assert_equals_float (file->duration, 10 * (double) GST_SECOND);
  assert_equals_int (file->offset, 0);
  assert_equals_int (file->size, -1);
  assert_equals_string (file->title, "Test");

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_playlist_byte_range_media_files)
{
  GstHLSMediaPlaylist *pl;
  GstM3U8MediaSegment *file;

  pl = load_m3u8 (BYTE_RANGES_PLAYLIST);

  /* Check number of entries */
  assert_equals_int (pl->segments->len, 4);
  /* Check first media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  assert_equals_string (file->uri, "http://media.example.com/all.ts");
  assert_equals_int (file->sequence, 0);
  assert_equals_float (file->duration, 10 * (double) GST_SECOND);
  assert_equals_int (file->offset, 100);
  assert_equals_int (file->size, 1000);
  /* Check last media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 3));
  assert_equals_string (file->uri, "http://media.example.com/all.ts");
  assert_equals_int (file->sequence, 3);
  assert_equals_float (file->duration, 10 * (double) GST_SECOND);
  assert_equals_int (file->offset, 3000);
  assert_equals_int (file->size, 1000);

  gst_hls_media_playlist_unref (pl);
  pl = load_m3u8 (BYTE_RANGES_ACC_OFFSET_PLAYLIST);

  /* Check number of entries */
  assert_equals_int (pl->segments->len, 4);
  /* Check first media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 0));
  assert_equals_string (file->uri, "http://media.example.com/all.ts");
  assert_equals_int (file->sequence, 0);
  assert_equals_float (file->duration, 10 * (double) GST_SECOND);
  assert_equals_int (file->offset, 0);
  assert_equals_int (file->size, 1000);
  /* Check last media segments */
  file = GST_M3U8_MEDIA_SEGMENT (g_ptr_array_index (pl->segments, 3));
  assert_equals_string (file->uri, "http://media.example.com/all.ts");
  assert_equals_int (file->sequence, 3);
  assert_equals_float (file->duration, 10 * (double) GST_SECOND);
  assert_equals_int (file->offset, 3000);
  assert_equals_int (file->size, 1000);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_advance_fragment)
{
  GstHLSMediaPlaylist *pl;
  GstM3U8MediaSegment *mf;
  GstM3U8SeekResult seek_result;

  pl = load_m3u8 (BYTE_RANGES_PLAYLIST);

  /* Check the next fragment */
  fail_unless (gst_hls_media_playlist_get_starting_segment (pl,
          &seek_result) == TRUE);

  mf = seek_result.segment;
  fail_unless (mf != NULL);
  assert_equals_int (mf->discont, FALSE);
  assert_equals_string (mf->uri, "http://media.example.com/all.ts");
  assert_equals_uint64 (mf->stream_time, 0);
  assert_equals_uint64 (mf->duration, 10 * GST_SECOND);
  assert_equals_uint64 (mf->offset, 100);
  assert_equals_uint64 (mf->offset + mf->size, 1100);
  gst_m3u8_media_segment_unref (mf);

  /* Check next media segments */
  mf = gst_hls_media_playlist_advance_fragment (pl, mf, TRUE);
  fail_unless (mf != NULL);
  assert_equals_int (mf->discont, FALSE);
  assert_equals_string (mf->uri, "http://media.example.com/all.ts");
  assert_equals_uint64 (mf->stream_time, 10 * GST_SECOND);
  assert_equals_uint64 (mf->duration, 10 * GST_SECOND);
  assert_equals_uint64 (mf->offset, 1000);
  assert_equals_uint64 (mf->offset + mf->size, 2000);
  gst_m3u8_media_segment_unref (mf);

  /* Check next media segments */
  mf = gst_hls_media_playlist_advance_fragment (pl, mf, TRUE);
  assert_equals_int (mf->discont, FALSE);
  assert_equals_string (mf->uri, "http://media.example.com/all.ts");
  assert_equals_uint64 (mf->stream_time, 20 * GST_SECOND);
  assert_equals_uint64 (mf->duration, 10 * GST_SECOND);
  assert_equals_uint64 (mf->offset, 2000);
  assert_equals_uint64 (mf->offset + mf->size, 3000);
  gst_m3u8_media_segment_unref (mf);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_get_duration)
{
  GstHLSMediaPlaylist *pl;

  /* Test duration for on-demand playlists */
  pl = load_m3u8 (ON_DEMAND_PLAYLIST);
  assert_equals_uint64 (gst_hls_media_playlist_get_duration (pl),
      40 * GST_SECOND);
  gst_hls_media_playlist_unref (pl);

  /* Test duration for live playlists */
  pl = load_m3u8 (LIVE_PLAYLIST);
  assert_equals_uint64 (gst_hls_media_playlist_get_duration (pl),
      GST_CLOCK_TIME_NONE);
  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_get_target_duration)
{
  GstHLSMediaPlaylist *pl;

  pl = load_m3u8 (ON_DEMAND_PLAYLIST);
  assert_equals_uint64 (pl->targetduration, 10 * GST_SECOND);
  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;


GST_START_TEST (test_get_stream_for_bitrate)
{
  GstHLSMasterPlaylist *master;
  GstHLSVariantStream *stream;

  master = load_master_playlist (VARIANT_PLAYLIST);
  stream =
      gst_hls_master_playlist_get_variant_for_bitrate (master, FALSE, 0, 0,
      NULL);

  assert_equals_int (stream->bandwidth, 65000);

  stream =
      gst_hls_master_playlist_get_variant_for_bitrate (master, FALSE,
      G_MAXINT32, 0, NULL);
  assert_equals_int (stream->bandwidth, 768000);
  stream =
      gst_hls_master_playlist_get_variant_for_bitrate (master, FALSE, 300000, 0,
      NULL);
  assert_equals_int (stream->bandwidth, 256000);
  stream =
      gst_hls_master_playlist_get_variant_for_bitrate (master, FALSE, 500000, 0,
      NULL);
  assert_equals_int (stream->bandwidth, 256000);
  stream =
      gst_hls_master_playlist_get_variant_for_bitrate (master, FALSE, 255000, 0,
      NULL);
  assert_equals_int (stream->bandwidth, 128000);

  gst_hls_master_playlist_unref (master);
}

GST_END_TEST;

GST_START_TEST (test_url_with_slash_query_param)
{
  static const gchar *MASTER_PLAYLIST = "#EXTM3U \n"
      "#EXT-X-VERSION:4\n"
      "#EXT-X-STREAM-INF:PROGRAM-ID=1, BANDWIDTH=1251135, CODECS=\"avc1.42001f, mp4a.40.2\", RESOLUTION=640x352\n"
      "1251/media.m3u8?acl=/*1054559_h264_1500k.mp4\n";
  GstHLSMasterPlaylist *master;
  GstHLSVariantStream *stream;

  master = load_master_playlist (MASTER_PLAYLIST);

  assert_equals_int (g_list_length (master->variants), 1);
  stream = g_list_nth_data (master->variants, 0);
  assert_equals_string (stream->uri,
      "http://localhost/1251/media.m3u8?acl=/*1054559_h264_1500k.mp4");

  gst_hls_master_playlist_unref (master);
}

GST_END_TEST;

GST_START_TEST (test_stream_inf_tag)
{
  static const gchar *MASTER_PLAYLIST = "#EXTM3U \n"
      "#EXT-X-VERSION:4\n"
      "#EXT-X-STREAM-INF:PROGRAM-ID=1, BANDWIDTH=1251135, CODECS=\"avc1.42001f, mp4a.40.2\", RESOLUTION=640x352\n"
      "media.m3u8\n";
  GstHLSMasterPlaylist *master;
  GstHLSVariantStream *stream;

  master = load_master_playlist (MASTER_PLAYLIST);

  assert_equals_int (g_list_length (master->variants), 1);
  stream = g_list_nth_data (master->variants, 0);

  assert_equals_int64 (stream->program_id, 1);
  assert_equals_int64 (stream->width, 640);
  assert_equals_int64 (stream->height, 352);
  assert_equals_int64 (stream->bandwidth, 1251135);
  assert_equals_string (stream->codecs, "avc1.42001f, mp4a.40.2");
  gst_hls_master_playlist_unref (master);
}

GST_END_TEST;

GST_START_TEST (test_map_tag)
{
  GstHLSMediaPlaylist *pl;
  GPtrArray *segments;
  GstM3U8MediaSegment *seg1, *seg2, *seg3;
  GstM3U8InitFile *init1, *init2;

  /* Test EXT-X-MAP tag
   * This M3U8 has two EXT-X-MAP tag.
   * the first one is applied to the 1st and 2nd segments, and the other is
   * applied only to the 3rd segment
   */

  pl = load_m3u8 (MAP_TAG_PLAYLIST);
  segments = pl->segments;

  assert_equals_int (segments->len, 3);

  for (gsize i = 0; i < segments->len; i++) {
    GstM3U8MediaSegment *file = g_ptr_array_index (segments, i);

    GstM3U8InitFile *init_file = file->init_file;
    fail_unless (init_file != NULL);
    fail_unless (init_file->uri != NULL);
  }

  seg1 = g_ptr_array_index (segments, 0);
  seg2 = g_ptr_array_index (segments, 1);
  seg3 = g_ptr_array_index (segments, 2);

  /* Segment 1 and 2 share the identical init segment */
  fail_unless (seg1->init_file == seg2->init_file);
  assert_equals_int (seg1->init_file->ref_count, 2);

  fail_unless (seg2->init_file != seg3->init_file);
  assert_equals_int (seg3->init_file->ref_count, 1);

  init1 = seg1->init_file;
  init2 = seg3->init_file;

  fail_unless (g_strcmp0 (init1->uri, init2->uri));
  assert_equals_int (init1->offset, 50);
  assert_equals_int (init1->size, 50);

  assert_equals_int (init2->offset, 0);
  assert_equals_int (init2->size, -1);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_low_latency_playlist)
{
  GstHLSMediaPlaylist *pl;
  GPtrArray *segments;
  GstM3U8MediaSegment *seg;

  /* Test low latency playlist features. EXT-X-PART-INF and EXT-X-PART
   *
   * There are 6 complete segments, and 1 dummy trailing segment containing
   * only a partial segment
   */

  pl = load_m3u8 (LOW_LATENCY_PLAYLIST);
  fail_unless (pl != NULL);

  segments = pl->segments;

  assert_equals_int (segments->len, 7);

  for (gsize i = 0; i < segments->len; i++) {
    GstM3U8MediaSegment *file = g_ptr_array_index (segments, i);

    /* The first segments are full, with no partial entries */
    if (i < 3) {
      fail_unless (file->partial_segments == NULL);
    } else if (i < 6) {
      fail_unless (file->partial_segments != NULL);
      /* 2 partial segments expected */
      fail_unless (file->partial_segments->len == 2);
    } else {
      fail_unless (file->partial_segments != NULL);
      /* 1 partial segment expected in the last segment */
      fail_unless (file->partial_segments->len == 1);
    }
  }

  /* The final segment only has one partial segment */
  seg = g_ptr_array_index (segments, 6);
  fail_unless (seg->partial_only == TRUE);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

GST_START_TEST (test_playlist_skip)
{
  GstHLSMediaPlaylist *pl;

  pl = load_m3u8 (SKIP_PLAYLIST);
  fail_unless (pl != NULL);

  assert_equals_int (pl->skipped_segments, 2);
  assert_equals_int (pl->num_removed_date_ranges, 2);
  fail_unless (g_strcmp0 (pl->removed_date_ranges[0], "splice-6FFFFFF0") == 0);
  fail_unless (g_strcmp0 (pl->removed_date_ranges[1], "splice-6FFFFFF1") == 0);

  gst_hls_media_playlist_unref (pl);
}

GST_END_TEST;

static Suite *
hlsdemux_suite (void)
{
  Suite *s = suite_create ("hlsdemux_m3u8");
  TCase *tc_m3u8 = tcase_create ("m3u8client");

  GST_DEBUG_CATEGORY_INIT (hls2_debug, "hlsdemux_m3u", 0, "hlsdemux m3u test");

  suite_add_tcase (s, tc_m3u8);
  tcase_add_test (tc_m3u8, test_load_main_playlist_invalid);
  tcase_add_test (tc_m3u8, test_load_main_playlist_rendition);
  tcase_add_test (tc_m3u8, test_load_main_playlist_variant);
  tcase_add_test (tc_m3u8, test_load_main_playlist_variant_with_missing_uri);
  tcase_add_test (tc_m3u8, test_load_windows_line_endings_variant_playlist);
  tcase_add_test (tc_m3u8, test_load_main_playlist_with_empty_lines);
  tcase_add_test (tc_m3u8, test_load_windows_main_playlist_with_empty_lines);
  tcase_add_test (tc_m3u8, test_on_demand_playlist);
  tcase_add_test (tc_m3u8, test_windows_line_endings_playlist);
  tcase_add_test (tc_m3u8, test_windows_empty_lines_playlist);
  tcase_add_test (tc_m3u8, test_empty_lines_playlist);
  tcase_add_test (tc_m3u8, test_live_playlist_rotated);
  tcase_add_test (tc_m3u8, test_playlist_with_doubles_duration);
  tcase_add_test (tc_m3u8, test_playlist_with_encryption);
  tcase_add_test (tc_m3u8, test_parse_invalid_playlist);
  tcase_add_test (tc_m3u8, test_sync_playlist_to_segment);
  tcase_add_test (tc_m3u8, test_playlist_media_files);
  tcase_add_test (tc_m3u8, test_playlist_byte_range_media_files);
  tcase_add_test (tc_m3u8, test_advance_fragment);
  tcase_add_test (tc_m3u8, test_get_duration);
  tcase_add_test (tc_m3u8, test_get_target_duration);
  tcase_add_test (tc_m3u8, test_get_stream_for_bitrate);
  tcase_add_test (tc_m3u8, test_url_with_slash_query_param);
  tcase_add_test (tc_m3u8, test_stream_inf_tag);
  tcase_add_test (tc_m3u8, test_map_tag);
  tcase_add_test (tc_m3u8, test_low_latency_playlist);
  tcase_add_test (tc_m3u8, test_playlist_skip);
  return s;
}

GST_CHECK_MAIN (hlsdemux);
