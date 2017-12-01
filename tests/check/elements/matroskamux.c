/* GStreamer
 *
 * unit test for matroskamux
 *
 * Copyright (C) <2005> Michal Benes <michal.benes@xeris.cz>
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

#include <unistd.h>

#include <gst/check/gstcheck.h>
#include <gst/base/gstadapter.h>

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
GstPad *mysrcpad, *mysinkpad;

#define AC3_CAPS_STRING "audio/x-ac3, " \
                        "channels = (int) 1, " \
                        "rate = (int) 8000"
#define VORBIS_TMPL_CAPS_STRING "audio/x-vorbis, " \
                                "channels = (int) 1, " \
                                "rate = (int) 8000"
/* streamheader shouldn't be in the template caps, only in the actual caps */
#define VORBIS_CAPS_STRING VORBIS_TMPL_CAPS_STRING \
                           ", streamheader=(buffer)<10, 2020, 303030>"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-matroska; audio/x-matroska"));
static GstStaticPadTemplate srcvorbistemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VORBIS_TMPL_CAPS_STRING));

static GstStaticPadTemplate srcac3template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (AC3_CAPS_STRING));


static GstPad *
setup_src_pad (GstElement * element, GstStaticPadTemplate * template)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up sending pad");
  /* sending pad */
  srcpad = gst_pad_new_from_static_template (template, "src");
  fail_if (srcpad == NULL, "Could not create a srcpad");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1);
  gst_pad_set_active (srcpad, TRUE);

  if (!(sinkpad = gst_element_get_static_pad (element, "audio_%u")))
    sinkpad = gst_element_get_request_pad (element, "audio_%u");
  fail_if (sinkpad == NULL, "Could not get sink pad from %s",
      GST_ELEMENT_NAME (element));
  /* references are owned by: 1) us, 2) matroskamux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link source and %s sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (sinkpad);   /* because we got it higher up */

  /* references are owned by: 1) matroskamux, 2) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);

  return srcpad;
}

static void
teardown_src_pad (GstElement * element)
{
  GstPad *srcpad, *sinkpad;

  /* clean up floating src pad */
  if (!(sinkpad = gst_element_get_static_pad (element, "audio_0")))
    sinkpad = gst_element_get_request_pad (element, "audio_0");
  /* references are owned by: 1) us, 2) matroskamux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  srcpad = gst_pad_get_peer (sinkpad);

  gst_pad_unlink (srcpad, sinkpad);

  /* references are owned by: 1) us, 2) matroskamux, 3) collect pads */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 3);
  gst_object_unref (sinkpad);
  /* one more ref is held by element itself */

  /* pad refs held by both creator and this function (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);
  gst_object_unref (srcpad);
  gst_object_unref (srcpad);
}

static GstPad *
setup_sink_pad (GstElement * element, GstStaticPadTemplate * template)
{
  GstPad *srcpad, *sinkpad;

  GST_DEBUG_OBJECT (element, "setting up receiving pad");
  /* receiving pad */
  sinkpad = gst_pad_new_from_static_template (template, "sink");

  fail_if (sinkpad == NULL, "Could not create a sinkpad");
  gst_pad_set_active (sinkpad, TRUE);

  srcpad = gst_element_get_static_pad (element, "src");
  fail_if (srcpad == NULL, "Could not get source pad from %s",
      GST_ELEMENT_NAME (element));
  gst_pad_set_chain_function (sinkpad, gst_check_chain_func);

  fail_unless (gst_pad_link (srcpad, sinkpad) == GST_PAD_LINK_OK,
      "Could not link %s source and sink pads", GST_ELEMENT_NAME (element));
  gst_object_unref (srcpad);    /* because we got it higher up */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2);

  return sinkpad;
}

static void
teardown_sink_pad (GstElement * element)
{
  GstPad *srcpad, *sinkpad;

  /* clean up floating sink pad */
  srcpad = gst_element_get_static_pad (element, "src");
  sinkpad = gst_pad_get_peer (srcpad);
  gst_pad_unlink (srcpad, sinkpad);

  /* pad refs held by both creator and this function (through _get_pad) */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 3);
  gst_object_unref (srcpad);
  /* one more ref is held by element itself */

  /* pad refs held by both creator and this function (through _get_peer) */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (sinkpad);
  gst_object_unref (sinkpad);
}


gboolean downstream_is_seekable;
static gboolean
matroskamux_sinkpad_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  gboolean ret = FALSE;

  if (GST_QUERY_TYPE (query) == GST_QUERY_SEEKING) {
    gst_query_set_seeking (query, GST_FORMAT_BYTES, downstream_is_seekable, 0,
        -1);
    ret = TRUE;
  }

  return ret;
}

static GstElement *
setup_matroskamux (GstStaticPadTemplate * srctemplate)
{
  GstElement *matroskamux;

  GST_DEBUG ("setup_matroskamux");
  matroskamux = gst_check_setup_element ("matroskamux");
  g_object_set (matroskamux, "version", 1, NULL);
  mysrcpad = setup_src_pad (matroskamux, srctemplate);
  mysinkpad = setup_sink_pad (matroskamux, &sinktemplate);

  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  return matroskamux;
}

static void
cleanup_matroskamux (GstElement * matroskamux)
{
  GST_DEBUG ("cleanup_matroskamux");
  gst_element_set_state (matroskamux, GST_STATE_NULL);

  teardown_src_pad (matroskamux);
  teardown_sink_pad (matroskamux);
  gst_check_teardown_element (matroskamux);
}

static void
check_buffer_data (GstBuffer * buffer, void *data, size_t data_size)
{
  fail_unless (gst_buffer_get_size (buffer) == data_size);
  fail_unless (gst_buffer_memcmp (buffer, 0, data, data_size) == 0);
}

GST_START_TEST (test_ebml_header)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer;
  GstAdapter *adapter;
  int num_buffers;
  int i;
  gint available;
  GstCaps *caps;
  guint8 data[] =
      { 0x1a, 0x45, 0xdf, 0xa3, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x14,
    0x42, 0x82, 0x89, 0x6d, 0x61, 0x74, 0x72, 0x6f, 0x73, 0x6b, 0x61, 0x00,
    0x42, 0x87, 0x81, 0x01,
    0x42, 0x85, 0x81, 0x01
  };

  matroskamux = setup_matroskamux (&srcac3template);

  caps = gst_caps_from_string (srcac3template.static_caps.string);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 1,
      "expected at least 5 buffers, but got only %d", num_buffers);

  adapter = gst_adapter_new ();
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);

    gst_adapter_push (adapter, outbuffer);
  }

  available = gst_adapter_available (adapter);
  fail_unless (available >= sizeof (data));
  outbuffer = gst_adapter_take_buffer (adapter, sizeof (data));
  g_object_unref (adapter);

  check_buffer_data (outbuffer, data, sizeof (data));
  gst_buffer_unref (outbuffer);

  cleanup_matroskamux (matroskamux);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;


GST_START_TEST (test_vorbis_header)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer;
  GstCaps *caps;
  int num_buffers;
  int i;
  gboolean vorbis_header_found = FALSE;
  guint8 data[12] =
      { 0x63, 0xa2, 0x89, 0x02, 0x01, 0x02, 0x10, 0x20, 0x20, 0x30, 0x30,
    0x30
  };

  matroskamux = setup_matroskamux (&srcvorbistemplate);

  caps = gst_caps_from_string (VORBIS_CAPS_STRING);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);

  for (i = 0; i < num_buffers; ++i) {
    gint j;
    gsize buffer_size;

    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffer_size = gst_buffer_get_size (outbuffer);
    buffers = g_list_remove (buffers, outbuffer);

    if (!vorbis_header_found && buffer_size >= sizeof (data)) {
      for (j = 0; j <= buffer_size - sizeof (data); j++) {
        if (gst_buffer_memcmp (outbuffer, j, data, sizeof (data)) == 0) {
          vorbis_header_found = TRUE;
          break;
        }
      }
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  fail_unless (vorbis_header_found);

  cleanup_matroskamux (matroskamux);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;


GST_START_TEST (test_block_group)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer;
  guint8 *indata;
  GstCaps *caps;
  int num_buffers;
  int i;
  guint8 data0[] = { 0xa0, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07,
    0xa1, 0x85,
    0x81, 0x00, 0x01, 0x00
  };
  guint8 data1[] = { 0x42 };

  matroskamux = setup_matroskamux (&srcac3template);

  caps = gst_caps_from_string (AC3_CAPS_STRING);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* Generate the header */
  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless_equals_int (gst_pad_push (mysrcpad, inbuffer), GST_FLOW_OK);
  num_buffers = g_list_length (buffers);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  /* Now push a buffer */
  indata = g_malloc (1);
  inbuffer = gst_buffer_new_wrapped (indata, 1);
  indata[0] = 0x42;
  GST_BUFFER_TIMESTAMP (inbuffer) = 1000000;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 2);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    switch (i) {
      case 0:
        check_buffer_data (outbuffer, data0, sizeof (data0));
        break;
      case 1:
        check_buffer_data (outbuffer, data1, sizeof (data1));
        break;
      default:
        break;
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  g_list_free (buffers);
  buffers = NULL;

  cleanup_matroskamux (matroskamux);
}

GST_END_TEST;

GST_START_TEST (test_reset)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer;
  GstBuffer *outbuffer;
  int num_buffers;
  int i;
  GstCaps *caps;

  matroskamux = setup_matroskamux (&srcac3template);

  caps = gst_caps_from_string (srcac3template.static_caps.string);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 1,
      "expected at least 1 buffer, but got only %d", num_buffers);

  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS, "could not set to null");

  fail_unless (gst_element_set_state (matroskamux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  inbuffer = gst_buffer_new_allocate (NULL, 1, 0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= 2,
      "expected at least 2 buffers, but got only %d", num_buffers);

  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
  }

  cleanup_matroskamux (matroskamux);
  g_list_free (buffers);
  buffers = NULL;
}

GST_END_TEST;

GST_START_TEST (test_link_webmmux_webm_sink)
{
  static GstStaticPadTemplate webm_sinktemplate =
      GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS ("video/webm; audio/webm"));
  GstElement *mux;

  mux = gst_check_setup_element ("webmmux");
  mysinkpad = setup_sink_pad (mux, &webm_sinktemplate);
  fail_unless (mysinkpad != NULL);

  fail_unless (gst_element_set_state (mux,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  gst_element_set_state (mux, GST_STATE_NULL);

  teardown_sink_pad (mux);
  gst_check_teardown_element (mux);
}

GST_END_TEST;

/* Create a new chapter */
static GstTocEntry *
new_chapter (const guint chapter_nb, const gint64 start, const gint64 stop)
{
  GstTocEntry *toc_entry, *toc_sub_entry;
  GstTagList *tags;
  gchar title[32];
  gchar artist[32];
  gchar str_uid[32];

  g_snprintf (str_uid, sizeof (str_uid), "uid.%d", chapter_nb);
  toc_entry = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, str_uid);
  gst_toc_entry_set_start_stop_times (toc_entry, start, stop);

  g_snprintf (title, sizeof (title), "chap.%d", chapter_nb);
  g_snprintf (artist, sizeof (artist), "art.%d", chapter_nb);
  tags = gst_tag_list_new (GST_TAG_TITLE, title, GST_TAG_ARTIST, artist, NULL);
  gst_toc_entry_set_tags (toc_entry, tags);

  g_snprintf (str_uid, sizeof (str_uid), "uid.%d.1", chapter_nb);
  toc_sub_entry = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, str_uid);
  gst_toc_entry_set_start_stop_times (toc_sub_entry, start, (start + stop) / 2);

  g_snprintf (title, sizeof (title), "nested.%d.1", chapter_nb);
  g_snprintf (artist, sizeof (artist), "art.%d.1", chapter_nb);
  tags = gst_tag_list_new (GST_TAG_TITLE, title, GST_TAG_ARTIST, artist, NULL);
  gst_toc_entry_set_tags (toc_sub_entry, tags);

  gst_toc_entry_append_sub_entry (toc_entry, toc_sub_entry);

  g_snprintf (str_uid, sizeof (str_uid), "uid.%d.2", chapter_nb);
  toc_sub_entry = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, str_uid);
  gst_toc_entry_set_start_stop_times (toc_sub_entry, (start + stop) / 2, stop);

  g_snprintf (title, sizeof (title), "nested/%d.2", chapter_nb);
  g_snprintf (artist, sizeof (artist), "art.%d.2", chapter_nb);
  tags = gst_tag_list_new (GST_TAG_TITLE, title, GST_TAG_ARTIST, artist, NULL);
  gst_toc_entry_set_tags (toc_sub_entry, tags);

  gst_toc_entry_append_sub_entry (toc_entry, toc_sub_entry);

  return toc_entry;
}

/* Create a reference toc which includes a master edition entry */
static GstToc *
new_reference_toc (void)
{
  GstToc *ref_toc;
  GstTocEntry *toc_edition_entry, *toc_entry;
  GstTagList *tags;

  ref_toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);

  toc_edition_entry = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, "00");
  tags = gst_tag_list_new (GST_TAG_COMMENT, "Ed", NULL);
  gst_toc_entry_set_tags (toc_edition_entry, tags);

  toc_entry = new_chapter (1, 0 * GST_MSECOND, 2 * GST_MSECOND);
  gst_toc_entry_append_sub_entry (toc_edition_entry, toc_entry);

  toc_entry = new_chapter (2, 2 * GST_MSECOND, 4 * GST_MSECOND);
  gst_toc_entry_append_sub_entry (toc_edition_entry, toc_entry);

  gst_toc_append_entry (ref_toc, toc_edition_entry);

  return ref_toc;
}

/* Create a toc which includes chapters without edition entry */
static GstToc *
new_no_edition_toc (void)
{
  GstToc *ref_toc;
  GstTocEntry *toc_entry;

  ref_toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);

  toc_entry = new_chapter (1, 0 * GST_MSECOND, 2 * GST_MSECOND);
  gst_toc_append_entry (ref_toc, toc_entry);

  toc_entry = new_chapter (2, 2 * GST_MSECOND, 4 * GST_MSECOND);
  gst_toc_append_entry (ref_toc, toc_entry);

  return ref_toc;
}

static guint64
read_integer (GstMapInfo * info, gsize * index, guint64 len)
{
  guint64 total = 0;

  for (; len > 0; --len) {
    total = (total << 8) | GST_READ_UINT8 (info->data + *index);
    ++(*index);
  }

  return total;
}

static guint64
read_length (GstMapInfo * info, gsize * index)
{
  gint len_mask = 0x80, read = 1;
  guint64 total;
  guint8 b;

  b = GST_READ_UINT8 (info->data + *index);
  ++(*index);
  total = (guint64) b;
  while (read <= 8 && !(total & len_mask)) {
    read++;
    len_mask >>= 1;
  }
  total &= (len_mask - 1);

  for (; read > 1; --read) {
    total = (total << 8) | GST_READ_UINT8 (info->data + *index);
    ++(*index);
  }

  return total;
}

static gboolean
check_id (GstMapInfo * info, gsize * index,
    guint8 * tag, gint tag_len, guint64 * len)
{
  if (memcmp (info->data + *index, tag, tag_len) == 0) {
    *index += tag_len;
    *len = read_length (info, index);
    return TRUE;
  } else {
    return FALSE;
  }
}

static gboolean
check_id_read_int (GstMapInfo * info, gsize * index,
    guint8 * tag, gint tag_len, guint64 * value)
{
  guint64 len;

  if (check_id (info, index, tag, tag_len, &len)) {
    *value = read_integer (info, index, len);
    return TRUE;
  } else {
    return FALSE;
  }
}

/* Check the toc entry against the muxed buffer
 * Returns the internal UID */
static void
check_chapter (GstTocEntry * toc_entry, GstTocEntry * internal_toc_entry,
    GstMapInfo * info, gsize * index, gint last_offset)
{
  guint64 len, value, uid;
  gint64 start_ref, end_ref;
  gchar s_uid[32];
  const gchar *str_uid;
  GstTocEntry *internal_chapter;
  GList *cur_sub_chap;
  GstTagList *tags;
  gchar *title;

  guint8 chapter_atom[1] = { 0xb6 };
  guint8 chapter_uid[2] = { 0x73, 0xc4 };
  guint8 chapter_str_uid[2] = { 0x56, 0x54 };
  guint8 chapter_start[1] = { 0x91 };
  guint8 chapter_end[1] = { 0x92 };
  guint8 chapter_flag_hidden[1] = { 0x98 };
  guint8 chapter_flag_enabled[2] = { 0x45, 0x98 };
  guint8 chapter_segment_uid[2] = { 0x6e, 0x67 };
  guint8 chapter_segment_edition_uid[2] = { 0x6e, 0xbc };
  guint8 chapter_physical_equiv[2] = { 0x63, 0xc3 };
  guint8 chapter_track[1] = { 0x8f };
  guint8 chapter_track_nb[1] = { 0x89 };
  guint8 chapter_display[1] = { 0x80 };
  guint8 chapter_string[1] = { 0x85 };
  guint8 chapter_language[2] = { 0x43, 0x7c };

  fail_unless (check_id (info, index, chapter_atom,
          sizeof (chapter_atom), &len));

  fail_unless (check_id_read_int (info, index, chapter_uid,
          sizeof (chapter_uid), &uid));

  /* optional StringUID */
  if (check_id (info, index, chapter_str_uid, sizeof (chapter_str_uid), &len)) {
    str_uid = gst_toc_entry_get_uid (toc_entry);
    fail_unless (memcmp (info->data + *index, str_uid, strlen (str_uid)) == 0);
    *index += len;
  }

  gst_toc_entry_get_start_stop_times (toc_entry, &start_ref, &end_ref);

  fail_unless (check_id_read_int (info, index, chapter_start,
          sizeof (chapter_start), &value));
  fail_unless ((gint64) value == start_ref);

  /* optional chapter end */
  if (check_id_read_int (info, index, chapter_end,
          sizeof (chapter_end), &value)) {
    fail_unless ((gint64) value == end_ref);
  }

  fail_unless (check_id_read_int (info, index, chapter_flag_hidden,
          sizeof (chapter_flag_hidden), &value));

  fail_unless (check_id_read_int (info, index, chapter_flag_enabled,
          sizeof (chapter_flag_enabled), &value));

  /* optional segment UID */
  check_id_read_int (info, index, chapter_segment_uid,
      sizeof (chapter_segment_uid), &value);

  /* optional segment edition UID */
  check_id_read_int (info, index, chapter_segment_edition_uid,
      sizeof (chapter_segment_edition_uid), &value);

  /* optional physical equiv */
  check_id_read_int (info, index, chapter_physical_equiv,
      sizeof (chapter_physical_equiv), &value);

  /* optional chapter track */
  if (check_id (info, index, chapter_track, sizeof (chapter_track), &len)) {
    fail_unless (check_id_read_int (info, index, chapter_track_nb,
            sizeof (chapter_track_nb), &value));
  }

  /* FIXME: there can be several chapter displays */
  if (check_id (info, index, chapter_display, sizeof (chapter_display), &len)) {
    /* chapter display */
    fail_unless (check_id (info, index, chapter_string,
            sizeof (chapter_string), &len));

    tags = gst_toc_entry_get_tags (toc_entry);
    if (gst_tag_list_get_tag_size (tags, GST_TAG_TITLE) > 0) {
      gst_tag_list_get_string_index (tags, GST_TAG_TITLE, 0, &title);
      fail_unless (memcmp (info->data + *index, title, strlen (title)) == 0);
    }
    *index += len;

    fail_unless (check_id (info, index, chapter_language,
            sizeof (chapter_language), &len));
    /* TODO: define language - always "und" ATM */
    *index += len;
  }

  /* TODO: add remaining fields (not used in current matroska-mux) */

  g_snprintf (s_uid, sizeof (s_uid), "%" G_GINT64_FORMAT, uid);
  internal_chapter = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_CHAPTER, s_uid);
  gst_toc_entry_append_sub_entry (internal_toc_entry, internal_chapter);

  cur_sub_chap = gst_toc_entry_get_sub_entries (toc_entry);
  while (cur_sub_chap != NULL && *index < last_offset) {
    check_chapter (cur_sub_chap->data, internal_chapter, info,
        index, last_offset);
    cur_sub_chap = cur_sub_chap->next;
  }

  fail_unless (cur_sub_chap == NULL);
}

/* Check the reference toc against the muxed buffer */
static void
check_toc (GstToc * ref_toc, GstToc * internal_toc,
    GstMapInfo * info, gsize * index)
{
  guint64 len, value, uid;
  gchar s_uid[32];
  gint last_offset;
  GList *cur_entry, *cur_chapter;
  GstTocEntry *internal_edition;

  guint8 edition_entry[2] = { 0x45, 0xb9 };
  guint8 edition_uid[2] = { 0x45, 0xbc };
  guint8 edition_flag_hidden[2] = { 0x45, 0xbd };
  guint8 edition_flag_default[2] = { 0x45, 0xdb };
  guint8 edition_flag_ordered[2] = { 0x45, 0xdd };

  /* edition entry */
  fail_unless (check_id (info, index, edition_entry,
          sizeof (edition_entry), &len));
  last_offset = *index + (gint) len;

  cur_entry = gst_toc_get_entries (ref_toc);
  while (cur_entry != NULL && *index < last_offset) {
    uid = 0;
    check_id_read_int (info, index, edition_uid, sizeof (edition_uid), &uid);
    g_snprintf (s_uid, sizeof (s_uid), "%" G_GINT64_FORMAT, uid);
    internal_edition = gst_toc_entry_new (GST_TOC_ENTRY_TYPE_EDITION, s_uid);
    gst_toc_append_entry (internal_toc, internal_edition);

    fail_unless (check_id_read_int (info, index, edition_flag_hidden,
            sizeof (edition_flag_hidden), &value));

    fail_unless (check_id_read_int (info, index, edition_flag_default,
            sizeof (edition_flag_default), &value));

    /* optional */
    check_id_read_int (info, index, edition_flag_ordered,
        sizeof (edition_flag_ordered), &value);

    cur_chapter = gst_toc_entry_get_sub_entries (cur_entry->data);
    while (cur_chapter != NULL && *index < last_offset) {
      check_chapter (cur_chapter->data, internal_edition, info,
          index, last_offset);
      cur_chapter = cur_chapter->next;
    }
    fail_unless (cur_chapter == NULL);

    cur_entry = cur_entry->next;
  }

  fail_unless (cur_entry == NULL && *index == last_offset);
}

static GstTocEntry *
find_toc_entry (GstTocEntry * ref_toc_entry, GstTocEntry * internal_toc_entry,
    guint64 uid)
{
  GList *cur_ref_entry, *cur_internal_entry;
  guint64 internal_uid;
  GstTocEntry *result = NULL;

  internal_uid = g_ascii_strtoull (gst_toc_entry_get_uid (internal_toc_entry),
      NULL, 10);
  if (uid == internal_uid) {
    result = ref_toc_entry;
  } else {
    cur_ref_entry = gst_toc_entry_get_sub_entries (ref_toc_entry);
    cur_internal_entry = gst_toc_entry_get_sub_entries (internal_toc_entry);
    while (cur_ref_entry != NULL && cur_internal_entry != NULL) {
      result = find_toc_entry (cur_ref_entry->data, cur_internal_entry->data,
          uid);

      if (result != NULL) {
        break;
      }

      cur_ref_entry = cur_ref_entry->next;
      cur_internal_entry = cur_internal_entry->next;
    }
  }

  return result;
}

static void
find_and_check_tags (GstToc * ref_toc, GstToc * internal_toc, GstMapInfo * info,
    guint64 uid, gchar * tag_name, gchar * tag_string)
{
  GList *cur_ref_entry, *cur_internal_entry;
  GstTocEntry *ref_toc_entry = NULL;
  GstTagList *tags;
  const gchar *tag_type;
  gchar *cur_tag_string;

  /* find the reference toc entry matching the UID */
  cur_ref_entry = gst_toc_get_entries (ref_toc);
  cur_internal_entry = gst_toc_get_entries (internal_toc);
  while (cur_ref_entry != NULL && cur_internal_entry != NULL) {
    ref_toc_entry = find_toc_entry (cur_ref_entry->data,
        cur_internal_entry->data, uid);

    if (ref_toc_entry != NULL) {
      break;
    }

    cur_ref_entry = cur_ref_entry->next;
    cur_internal_entry = cur_internal_entry->next;
  }

  fail_unless (ref_toc_entry != NULL);

  if (g_strcmp0 (tag_name, "ARTIST") == 0) {
    tag_type = GST_TAG_ARTIST;
  } else if (g_strcmp0 (tag_name, "COMMENTS") == 0) {
    tag_type = GST_TAG_COMMENT;
  } else {
    tag_type = NULL;
  }

  fail_unless (tag_type != NULL);

  tags = gst_toc_entry_get_tags (ref_toc_entry);
  fail_unless (gst_tag_list_get_tag_size (tags, tag_type) > 0);
  gst_tag_list_get_string_index (tags, tag_type, 0, &cur_tag_string);
  fail_unless (g_strcmp0 (cur_tag_string, tag_string) == 0);
}

static void
check_tags (GstToc * ref_toc, GstToc * internal_toc,
    GstMapInfo * info, gsize * index)
{
  gboolean found_tags = FALSE, must_check_tag = FALSE;
  guint64 len, value, uid;
  gsize last_offset, next_tag;
  gchar *tag_name_str, *tag_string_str;
  guint8 tags[4] = { 0x12, 0x54, 0xc3, 0x67 };
  guint8 tag[2] = { 0x73, 0x73 };
  guint8 tag_targets[2] = { 0x63, 0xc0 };
  guint8 tag_target_type_value[2] = { 0x68, 0xca };
  guint8 tag_target_type[2] = { 0x63, 0xca };
  guint8 tag_edition_uid[2] = { 0x63, 0xc9 };
  guint8 tag_chapter_uid[2] = { 0x63, 0xc4 };
  guint8 simple_tag[2] = { 0x67, 0xc8 };
  guint8 tag_name[2] = { 0x45, 0xa3 };
  guint8 tag_string[2] = { 0x44, 0x87 };

  if (info->size > *index + sizeof (tags)) {
    for (; *index < info->size - sizeof (tags); ++(*index)) {
      if (memcmp (info->data + *index, tags, sizeof (tags)) == 0) {
        *index += sizeof (tags);

        len = read_length (info, index);
        last_offset = *index + len;

        found_tags = TRUE;
        break;
      }
    }
  }

  fail_unless (found_tags);

  while (*index < last_offset) {
    fail_unless (check_id (info, index, tag, sizeof (tag), &len));
    next_tag = *index + len;

    fail_unless (check_id (info, index, tag_targets,
            sizeof (tag_targets), &len));

    must_check_tag = FALSE;
    check_id_read_int (info, index, tag_target_type_value,
        sizeof (tag_target_type_value), &value);

    if (check_id (info, index, tag_target_type, sizeof (tag_target_type), &len)) {
      *index += len;
    }

    if (check_id_read_int (info, index, tag_chapter_uid,
            sizeof (tag_chapter_uid), &uid)) {
      must_check_tag = TRUE;
    } else if (check_id_read_int (info, index, tag_edition_uid,
            sizeof (tag_edition_uid), &uid)) {
      must_check_tag = TRUE;
    }

    if (must_check_tag) {
      fail_unless (check_id (info, index, simple_tag,
              sizeof (simple_tag), &len));

      fail_unless (check_id (info, index, tag_name, sizeof (tag_name), &len));
      tag_name_str = g_strndup ((gchar *) info->data + *index, len);
      *index += len;

      fail_unless (check_id (info, index, tag_string, sizeof (tag_string),
              &len));
      tag_string_str = g_strndup ((gchar *) info->data + *index, len);
      *index += len;

      find_and_check_tags (ref_toc, internal_toc, info, uid,
          tag_name_str, tag_string_str);

      g_free (tag_name_str);
      g_free (tag_string_str);
    }

    *index = next_tag;
  }
}

static void
check_segment (GstToc * ref_toc, GstToc * internal_toc,
    GstMapInfo * info, gsize * index)
{
  guint8 matroska_segment[4] = { 0x18, 0x53, 0x80, 0x67 };
  guint8 matroska_seek_id_chapters[7] = { 0x53, 0xab, 0x84,
    0x10, 0x43, 0xA7, 0x70
  };
  guint8 matroska_seek_id_tags[7] = { 0x53, 0xab, 0x84,
    0x12, 0x54, 0xc3, 0x67
  };
  guint8 matroska_seek_pos[2] = { 0x53, 0xac };
  guint8 matroska_chapters[4] = { 0x10, 0x43, 0xA7, 0x70 };

  guint64 len, value, segment_offset, chapters_offset, tags_offset;
  gboolean found_chapters_declaration = FALSE, found_tags_declaration = FALSE;

  /* Segment */
  fail_unless (info->size > sizeof (matroska_segment));
  fail_unless (check_id (info, index, matroska_segment,
          sizeof (matroska_segment), &len));

  segment_offset = *index;

  /* Search chapter declaration in seek head */
  for (; *index < len - sizeof (matroska_seek_id_chapters); ++(*index)) {
    if (memcmp (info->data + *index, matroska_seek_id_chapters,
            sizeof (matroska_seek_id_chapters)) == 0) {
      *index += sizeof (matroska_seek_id_chapters);

      if (check_id_read_int (info, index, matroska_seek_pos,
              sizeof (matroska_seek_pos), &value)) {
        /* found chapter declaration */
        found_chapters_declaration = TRUE;
        chapters_offset = segment_offset + value;
        break;
      }
    }
  }

  fail_unless (found_chapters_declaration);

  *index = chapters_offset;
  if (check_id (info, index, matroska_chapters,
          sizeof (matroska_chapters), &len)) {
    check_toc (ref_toc, internal_toc, info, index);
  }

  /* Search tags declaration in seek head */
  for (*index = segment_offset; *index < len - sizeof (matroska_seek_id_tags);
      ++(*index)) {
    if (memcmp (info->data + *index, matroska_seek_id_tags,
            sizeof (matroska_seek_id_tags)) == 0) {
      *index += sizeof (matroska_seek_id_tags);

      if (check_id_read_int (info, index, matroska_seek_pos,
              sizeof (matroska_seek_pos), &value)) {
        /* found tags declaration */
        found_tags_declaration = TRUE;
        tags_offset = segment_offset + value;
        break;
      }
    }
  }

  fail_unless (found_tags_declaration);

  *index = tags_offset;
  check_tags (ref_toc, internal_toc, info, index);
}

static void
test_toc (gboolean with_edition)
{
  GstElement *matroskamux;
  GstBuffer *inbuffer, *outbuffer, *merged_buffer;
  GstMapInfo info;
  GstCaps *caps;
  int num_buffers, i;
  guint64 len;
  gsize index;
  GstTocSetter *toc_setter;
  GstToc *test_toc, *ref_toc, *internal_toc;

  guint8 ebml_header[4] = { 0x1a, 0x45, 0xdf, 0xa3 };

  matroskamux = setup_matroskamux (&srcac3template);
  downstream_is_seekable = TRUE;
  gst_pad_set_query_function (mysinkpad, matroskamux_sinkpad_query);

  toc_setter = GST_TOC_SETTER (matroskamux);
  fail_unless (toc_setter != NULL);

  if (with_edition) {
    test_toc = new_reference_toc ();
  } else {
    test_toc = new_no_edition_toc ();
  }
  gst_toc_setter_set_toc (toc_setter, test_toc);
  gst_toc_unref (test_toc);

  caps = gst_caps_from_string (srcac3template.static_caps.string);
  gst_check_setup_events (mysrcpad, matroskamux, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_and_alloc (1);
  gst_buffer_memset (inbuffer, 0, 0, 1);
  GST_BUFFER_TIMESTAMP (inbuffer) = 0;
  GST_BUFFER_DURATION (inbuffer) = 1 * GST_MSECOND;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  fail_unless (gst_pad_push (mysrcpad, inbuffer) == GST_FLOW_OK);

  /* send eos to ensure everything is written */
  fail_unless (gst_pad_push_event (mysrcpad, gst_event_new_eos ()));

  /* Merge buffers */
  num_buffers = g_list_length (buffers);
  merged_buffer = gst_buffer_new ();
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    buffers = g_list_remove (buffers, outbuffer);

    if (outbuffer->offset == gst_buffer_get_size (merged_buffer)) {
      gst_buffer_append_memory (merged_buffer,
          gst_buffer_get_all_memory (outbuffer));
    } else {
      fail_unless (gst_buffer_map (outbuffer, &info, GST_MAP_READ));
      gst_buffer_fill (merged_buffer, outbuffer->offset, info.data, info.size);
      gst_buffer_unmap (outbuffer, &info);
    }

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }

  fail_unless (gst_buffer_map (merged_buffer, &info, GST_MAP_READ));
  index = 0;

  fail_unless (check_id (&info, &index, ebml_header,
          sizeof (ebml_header), &len));
  /* skip header */
  index += len;

  ref_toc = new_reference_toc ();
  internal_toc = gst_toc_new (GST_TOC_SCOPE_GLOBAL);
  check_segment (ref_toc, internal_toc, &info, &index);
  gst_toc_unref (internal_toc);
  gst_toc_unref (ref_toc);

  gst_buffer_unmap (merged_buffer, &info);

  cleanup_matroskamux (matroskamux);
  g_list_free (buffers);
  buffers = NULL;
}

GST_START_TEST (test_toc_with_edition)
{
  test_toc (TRUE);
}

GST_END_TEST;

GST_START_TEST (test_toc_without_edition)
{
  test_toc (FALSE);
}

GST_END_TEST;

static Suite *
matroskamux_suite (void)
{
  Suite *s = suite_create ("matroskamux");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_ebml_header);
  tcase_add_test (tc_chain, test_vorbis_header);
  tcase_add_test (tc_chain, test_block_group);
  tcase_add_test (tc_chain, test_reset);
  tcase_add_test (tc_chain, test_link_webmmux_webm_sink);
  tcase_add_test (tc_chain, test_toc_with_edition);
  tcase_add_test (tc_chain, test_toc_without_edition);
  return s;
}

GST_CHECK_MAIN (matroskamux);
