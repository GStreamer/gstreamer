/* GStreamer
 *
 * unit test for kate
 *
 * Copyright (C) <2007> Stefan Kost <ensonic@users.sf.net>
 * Copyright (C) <2008> ogg.k.ogg.k <ogg.k.ogg.k@googlemail.com>
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
#include <gst/base/gsttypefindhelper.h>


static const guint8 kate_header_0x80[64] = {
  0x80, 0x6b, 0x61, 0x74, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x09, 0x00, 0x00, 0x00, 0x20,       /* .kate...... ...  */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xe8, 0x03, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,       /* ................ */
  0x65, 0x6e, 0x5f, 0x47, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       /* en_GB........... */
  0x6e, 0x6f, 0x6e, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,       /* none............ */
};

static const guint8 kate_header_0x81[53] = {
  0x81, 0x6b, 0x61, 0x74, 0x65, 0x00, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x6c, 0x69, 0x62,       /* .kate........lib */
  0x6b, 0x61, 0x74, 0x65, 0x20, 0x30, 0x2e, 0x31, 0x2e, 0x30, 0x20, 0x28, 0x54, 0x69, 0x67, 0x65,       /* kate 0.1.0 (Tige */
  0x72, 0x29, 0x01, 0x00, 0x00, 0x00, 0x0b, 0x00, 0x00, 0x00, 0x54, 0x49, 0x54, 0x4c, 0x45, 0x3d,       /* r)........TITLE= */
  0x54, 0x69, 0x67, 0x65, 0x72, /* Tiger            */
};

static const guint8 kate_header_0x8x[10] = {
  0x80, 0x6b, 0x61, 0x74, 0x65, 0x00, 0x00, 0x00, 0x00
};

static const guint8 kate_header_0x88[11] = {
  0x88, 0x6b, 0x61, 0x74, 0x65, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const guint8 kate_header_0x00[45] = {
  0x00, 0xe8, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xd0, 0x07, 0x00, 0x00, 0x00, 0x00, 0x00,       /* ................ */
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x70, 0x6c, 0x61,       /* .............pla */
  0x69, 0x6e, 0x20, 0x6f, 0x6c, 0x64, 0x20, 0x74, 0x65, 0x78, 0x74, 0x08, 0x00  /* in old text..    */
};

static const guint8 kate_header_0x7f[1] = {
  0x7f
};

static const unsigned char kate_spu[] = {
  0x00, 0x1b,                   /* size */
  0x00, 0x06,                   /* commands at offset 6 */
  0x45,                         /* first line data - 2 pixels of colors 0 and 1 */
  0x76,                         /* first line data - 2 pixels of colors 3 and 2 */
  0x00, 0x00,                   /* timestamp */
  0x00, 0x06,                   /* link to next command sequence - points back to the current one to mark no more */
  0x06, 0x00, 0x04, 0x00, 0x05, /* pointers to data */
  0x05, 0x00, 0x30, 0x04, 0x00, 0x10, 0x02,     /* area: 3x1 -> 4x2 */
  0x04, 0x0f, 0xff,             /* alpha: color 0 transparent, all others opaque */
  0x01,                         /* show */
  0xff                          /* end */
};

/* A lot of these taken from the vorbisdec test */

/* For ease of programming we use globals to keep refs for our floating
 * src and sink pads we create; otherwise we always have to do get_pad,
 * get_peer, and then remove references in every test function */
static GstPad *mydecsrcpad, *mydecsinkpad;
static GstPad *myencsrcpad, *myencsinkpad;
static GstPad *myparsesrcpad, *myparsesinkpad;
static GstPad *mytagsrcpad, *mytagsinkpad;

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);
static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GList *events = NULL;

static gboolean
collect_events_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GST_LOG ("event: %" GST_PTR_FORMAT, event);
  events = g_list_append (events, gst_event_ref (event));
  return gst_pad_event_default (pad, parent, event);
}

static GstElement *
setup_katedec (void)
{
  GstElement *katedec;

  GST_DEBUG ("setup_katedec");
  katedec = gst_check_setup_element ("katedec");
  mydecsrcpad = gst_check_setup_src_pad (katedec, &srctemplate);
  mydecsinkpad = gst_check_setup_sink_pad (katedec, &sinktemplate);
  gst_pad_set_event_function (mydecsinkpad, collect_events_func);
  gst_pad_set_active (mydecsrcpad, TRUE);
  gst_pad_set_active (mydecsinkpad, TRUE);

  return katedec;
}

static void
cleanup_katedec (GstElement * katedec)
{
  GList *l;

  GST_DEBUG ("cleanup_katedec");
  gst_element_set_state (katedec, GST_STATE_NULL);

  for (l = events; l != NULL; l = l->next)
    gst_event_unref (GST_EVENT (l->data));
  g_list_free (events);
  events = NULL;

  gst_pad_set_active (mydecsrcpad, FALSE);
  gst_pad_set_active (mydecsinkpad, FALSE);
  gst_check_teardown_src_pad (katedec);
  gst_check_teardown_sink_pad (katedec);
  gst_check_teardown_element (katedec);
}

static GstElement *
setup_kateenc (void)
{
  GstElement *kateenc;

  GST_DEBUG ("setup_kateenc");
  kateenc = gst_check_setup_element ("kateenc");
  myencsrcpad = gst_check_setup_src_pad (kateenc, &srctemplate);
  myencsinkpad = gst_check_setup_sink_pad (kateenc, &sinktemplate);
  gst_pad_set_active (myencsrcpad, TRUE);
  gst_pad_set_active (myencsinkpad, TRUE);

  return kateenc;
}

static void
cleanup_kateenc (GstElement * kateenc)
{
  GST_DEBUG ("cleanup_kateenc");
  gst_element_set_state (kateenc, GST_STATE_NULL);

  gst_pad_set_active (myencsrcpad, FALSE);
  gst_pad_set_active (myencsinkpad, FALSE);
  gst_check_teardown_src_pad (kateenc);
  gst_check_teardown_sink_pad (kateenc);
  gst_check_teardown_element (kateenc);
}

static GstElement *
setup_kateparse (void)
{
  GstElement *kateparse;

  GST_DEBUG ("setup_kateparse");
  kateparse = gst_check_setup_element ("kateparse");
  myparsesrcpad = gst_check_setup_src_pad (kateparse, &srctemplate);
  myparsesinkpad = gst_check_setup_sink_pad (kateparse, &sinktemplate);
  gst_pad_set_active (myparsesrcpad, TRUE);
  gst_pad_set_active (myparsesinkpad, TRUE);

  return kateparse;
}

static void
cleanup_kateparse (GstElement * kateparse)
{
  GST_DEBUG ("cleanup_kateparse");
  gst_element_set_state (kateparse, GST_STATE_NULL);

  gst_pad_set_active (myparsesrcpad, FALSE);
  gst_pad_set_active (myparsesinkpad, FALSE);
  gst_check_teardown_src_pad (kateparse);
  gst_check_teardown_sink_pad (kateparse);
  gst_check_teardown_element (kateparse);
}

static GstElement *
setup_katetag (void)
{
  GstElement *katetag;

  GST_DEBUG ("setup_katetag");
  katetag = gst_check_setup_element ("katetag");
  mytagsrcpad = gst_check_setup_src_pad (katetag, &srctemplate);
  mytagsinkpad = gst_check_setup_sink_pad (katetag, &sinktemplate);
  gst_pad_set_active (mytagsrcpad, TRUE);
  gst_pad_set_active (mytagsinkpad, TRUE);

  return katetag;
}

static void
cleanup_katetag (GstElement * katetag)
{
  GST_DEBUG ("cleanup_katetag");
  gst_element_set_state (katetag, GST_STATE_NULL);

  gst_pad_set_active (mytagsrcpad, FALSE);
  gst_pad_set_active (mytagsinkpad, FALSE);
  gst_check_teardown_src_pad (katetag);
  gst_check_teardown_sink_pad (katetag);
  gst_check_teardown_element (katetag);
}

static void
check_buffers (guint expected, gboolean headers_in_caps)
{
  GstBuffer *outbuffer;
  GstMapInfo info;
  guint i, num_buffers;
  const int num_headers = 9;
  unsigned char packet_type;

  /* check buffers are the type we expect */
  num_buffers = g_list_length (buffers);
  fail_unless (num_buffers >= num_headers + expected);  /* at least 9 headers, plus a variable number of data packets */
  for (i = 0; i < num_buffers; ++i) {
    outbuffer = GST_BUFFER (buffers->data);
    fail_if (outbuffer == NULL);
    fail_if (gst_buffer_get_size (outbuffer) == 0);

    assert_equals_int (gst_buffer_map (outbuffer, &info, GST_MAP_READ), TRUE);
    if (i < num_headers) {
      /* different headers packets */
      packet_type = (0x80 | i);
      fail_unless (info.data[0] == packet_type);
      /* headers could be in caps, so would have an extra ref */
    } else if (i == num_buffers - 1) {
      /* eos data packet */
      packet_type = 0x7f;
      fail_unless (info.data[0] == packet_type);
    } else {
      /* data packet */
      packet_type = 0;
      fail_unless (info.data[0] >= 0 && info.data[0] < 0x7f);
    }
    gst_buffer_unmap (outbuffer, &info);

    buffers = g_list_remove (buffers, outbuffer);

    ASSERT_BUFFER_REFCOUNT (outbuffer, "outbuffer", 1);
    gst_buffer_unref (outbuffer);
    outbuffer = NULL;
  }
}

GST_START_TEST (test_kate_typefind)
{
  GstTypeFindProbability prob;
  const gchar *type;
  GstBuffer *buf;
  GstCaps *caps = NULL;

  buf = gst_buffer_new_wrapped (g_memdup (kate_header_0x80,
          sizeof (kate_header_0x80)), sizeof (kate_header_0x80));
  GST_BUFFER_OFFSET (buf) = 0;

  caps = gst_type_find_helper_for_buffer (NULL, buf, &prob);
  fail_unless (caps != NULL);
  GST_LOG ("Found type: %" GST_PTR_FORMAT, caps);

  type = gst_structure_get_name (gst_caps_get_structure (caps, 0));
  fail_unless_equals_string (type, "application/x-kate");
  fail_unless (prob > GST_TYPE_FIND_MINIMUM && prob <= GST_TYPE_FIND_MAXIMUM);

  gst_buffer_unref (buf);
  gst_caps_unref (caps);
}

GST_END_TEST;

GST_START_TEST (test_kate_empty_identification_header)
{
  GstElement *katedec;
  GstBuffer *inbuffer;
  GstBus *bus;
  GstCaps *caps;

  katedec = setup_katedec ();
  bus = gst_bus_new ();

  fail_unless (gst_element_set_state (katedec,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  caps = gst_caps_new_empty_simple ("subtitle/x-kate");
  gst_check_setup_events (mydecsrcpad, katedec, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_and_alloc (0);
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  /* set a bus here so we avoid getting state change messages */
  gst_element_set_bus (katedec, bus);

  fail_unless_equals_int (gst_pad_push (mydecsrcpad, inbuffer), GST_FLOW_ERROR);
  /* ... but it ends up being collected on the global buffer list */
  fail_unless_equals_int (g_list_length (buffers), 0);

  gst_element_set_bus (katedec, NULL);

  /* cleanup */
  gst_object_unref (GST_OBJECT (bus));
  cleanup_katedec (katedec);
}

GST_END_TEST;

/* FIXME: also tests comment header */
GST_START_TEST (test_kate_identification_header)
{
  GstElement *katedec;
  GstBuffer *inbuffer;
  GstBus *bus;
  GstCaps *caps;

  katedec = setup_katedec ();
  fail_unless (gst_element_set_state (katedec,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  caps = gst_caps_new_empty_simple ("subtitle/x-kate");
  gst_check_setup_events (mydecsrcpad, katedec, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x80,
          sizeof (kate_header_0x80)), sizeof (kate_header_0x80));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (katedec, bus);
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mydecsrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless (g_list_length (buffers) == 0);

  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x81,
          sizeof (kate_header_0x81)), sizeof (kate_header_0x81));
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_ref (inbuffer);

  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (mydecsrcpad, inbuffer) == GST_FLOW_OK);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless (g_list_length (buffers) == 0);

  /* there should've been a tag event */
  {
    gboolean found_tags = FALSE;
    GList *l;

    for (l = events; l != NULL; l = l->next) {
      GstEvent *event = GST_EVENT (l->data);

      if (GST_EVENT_TYPE (event) == GST_EVENT_TAG) {
        GstTagList *tags = NULL;
        gchar *language;
        gchar *title;

        found_tags = TRUE;
        gst_event_parse_tag (event, &tags);
        fail_unless (tags != NULL);
        fail_unless (gst_tag_list_get_scope (tags) == GST_TAG_SCOPE_STREAM);
        fail_unless_equals_int (gst_tag_list_get_tag_size (tags,
                GST_TAG_LANGUAGE_CODE), 1);
        fail_unless (gst_tag_list_get_string (tags, GST_TAG_LANGUAGE_CODE,
                &language));
        fail_unless_equals_string (language, "en");
        g_free (language);
        fail_unless_equals_int (gst_tag_list_get_tag_size (tags, "title"), 1);
        fail_unless (gst_tag_list_get_string (tags, GST_TAG_TITLE, &title));
        fail_unless_equals_string (title, "Tiger");
        g_free (title);
      }
    }
    fail_unless (found_tags);
  }

  /* cleanup */
  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (katedec, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_katedec (katedec);
}

GST_END_TEST;

GST_START_TEST (test_kate_encode_nothing)
{
  GstElement *kateenc;

  kateenc = setup_kateenc ();
  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");

  fail_unless (gst_pad_push_event (myencsrcpad, gst_event_new_eos ()) == TRUE);

  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* cleanup */
  cleanup_kateenc (kateenc);
}

GST_END_TEST;

GST_START_TEST (test_kate_encode_empty)
{
  GstElement *kateenc;
  GstBuffer *inbuffer;
  GstBus *bus;
  GstCaps *caps;

  kateenc = setup_kateenc ();
  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_and_alloc (0);
  GST_BUFFER_TIMESTAMP (inbuffer) = GST_BUFFER_OFFSET (inbuffer) =
      1 * GST_SECOND;
  GST_BUFFER_DURATION (inbuffer) = 5 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  caps = gst_caps_from_string ("text/x-raw, format=utf8");
  fail_unless (caps != NULL);
  gst_check_setup_events (myencsrcpad, kateenc, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  gst_element_set_bus (kateenc, bus);
  /* pushing gives away my reference ... */
  fail_unless (gst_pad_push (myencsrcpad, inbuffer) == GST_FLOW_ERROR);

  fail_unless (gst_pad_push_event (myencsrcpad, gst_event_new_eos ()) == TRUE);

  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  gst_element_set_bus (kateenc, NULL);

  /* cleanup */
  gst_object_unref (GST_OBJECT (bus));
  cleanup_kateenc (kateenc);
}

GST_END_TEST;

GST_START_TEST (test_kate_encode_simple)
{
  GstElement *kateenc;
  GstBuffer *inbuffer;
  GstBus *bus;
  const gchar *test_string = "";
  GstCaps *caps;

  kateenc = setup_kateenc ();
  g_object_set (kateenc, "category", "subtitles", NULL);

  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_wrapped (g_memdup (test_string,
          strlen (test_string) + 1), strlen (test_string) + 1);

  GST_BUFFER_TIMESTAMP (inbuffer) = GST_BUFFER_OFFSET (inbuffer) =
      1 * GST_SECOND;
  GST_BUFFER_DURATION (inbuffer) = 5 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  caps = gst_caps_from_string ("text/x-raw, format=utf8");
  fail_unless (caps != NULL);
  gst_check_setup_events (myencsrcpad, kateenc, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (kateenc, bus);
  /* pushing gives away my reference ... */
  fail_unless_equals_int (gst_pad_push (myencsrcpad, inbuffer), GST_FLOW_OK);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless (gst_pad_push_event (myencsrcpad, gst_event_new_eos ()) == TRUE);

  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* at least one data packet and one EOS packet should have been emitted */
  check_buffers (1 + 1, FALSE);

  /* cleanup */
  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (kateenc, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_kateenc (kateenc);
  gst_check_drop_buffers ();
}

GST_END_TEST;

GST_START_TEST (test_kate_encode_spu)
{
  GstElement *kateenc;
  GstBuffer *inbuffer;
  GstBus *bus;
  GstCaps *caps;

  kateenc = setup_kateenc ();
  g_object_set (kateenc, "category", "spu-subtitles", NULL);

  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_spu, sizeof (kate_spu)),
      sizeof (kate_spu));

  GST_BUFFER_TIMESTAMP (inbuffer) = GST_BUFFER_OFFSET (inbuffer) =
      1 * GST_SECOND;
  GST_BUFFER_DURATION (inbuffer) = 5 * GST_SECOND;
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);

  caps = gst_caps_from_string ("subpicture/x-dvd");
  fail_unless (caps != NULL);
  gst_check_setup_events (myencsrcpad, kateenc, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  gst_buffer_ref (inbuffer);

  gst_element_set_bus (kateenc, bus);
  /* pushing gives away my reference ... */
  fail_unless_equals_int (gst_pad_push (myencsrcpad, inbuffer), GST_FLOW_OK);
  /* ... and nothing ends up on the global buffer list */
  ASSERT_BUFFER_REFCOUNT (inbuffer, "inbuffer", 1);
  gst_buffer_unref (inbuffer);
  fail_unless (gst_pad_push_event (myencsrcpad, gst_event_new_eos ()) == TRUE);

  fail_unless (gst_element_set_state (kateenc,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* at least one data packet and one EOS packet should have been emitted */
  check_buffers (2, FALSE);

  /* cleanup */
  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (kateenc, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_kateenc (kateenc);
  gst_check_drop_buffers ();
}

GST_END_TEST;

GST_START_TEST (test_kate_encode_keepalives)
{
  GstElement *kateenc;
  GstBus *bus;
  guint i, round;
  GstSegment segment;
  enum
  { n_keepalives = 1000 };
  static const struct
  {
    gdouble keepalive_min_time;
    gint packets;
  } cfg[3] = {
    {
    0.5, n_keepalives}, {
    2.0, n_keepalives / 2}, {
  5.0, n_keepalives / 5},};

  for (round = 0; round < 3; ++round) {
    kateenc = setup_kateenc ();
    /* doesn't matter here, since we never send a packet */
    g_object_set (kateenc, "category", "subtitles", NULL);
    fail_unless (gst_element_set_state (kateenc,
            GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
        "could not set to playing");
    bus = gst_bus_new ();

    gst_element_set_bus (kateenc, bus);

    g_object_set (kateenc, "keepalive-min-time", cfg[round].keepalive_min_time,
        NULL);

    gst_pad_push_event (myencsrcpad, gst_event_new_stream_start ("test"));

    /* the second one here should not emit a keepalive since the time since last packet
       is less than the keepalive delay */
    for (i = 1; i <= n_keepalives; ++i) {
      gint64 t = i * GST_SECOND;
      gst_segment_init (&segment, GST_FORMAT_TIME);
      segment.start = t;
      segment.position = 0;
      fail_unless (gst_pad_push_event (myencsrcpad,
              gst_event_new_segment (&segment)) == TRUE);
    }

    fail_unless (gst_pad_push_event (myencsrcpad,
            gst_event_new_eos ()) == TRUE);

    fail_unless (gst_element_set_state (kateenc,
            GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
        "could not set to ready");

    /* at least a number data packet and an EOS packet should have been emitted */
    check_buffers (cfg[round].packets + 1, FALSE);

    /* cleanup */
    gst_bus_set_flushing (bus, TRUE);
    gst_element_set_bus (kateenc, NULL);
    gst_object_unref (GST_OBJECT (bus));
    cleanup_kateenc (kateenc);
    gst_check_drop_buffers ();
  }
}

GST_END_TEST;

static void
test_kate_send_headers (GstElement * element, GstPad * pad)
{
  GstBuffer *inbuffer;
  GstCaps *caps;
  GstMapInfo info;
  int i;

  caps = gst_caps_new_simple ("subtitle/x-kate", NULL, NULL);
  gst_check_setup_events (pad, element, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);

  /* push headers */
  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x80,
          sizeof (kate_header_0x80)), sizeof (kate_header_0x80));
  GST_BUFFER_OFFSET (inbuffer) = GST_BUFFER_OFFSET_END (inbuffer) = 0;
  fail_unless_equals_int (gst_pad_push (pad, inbuffer), GST_FLOW_OK);

  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x81,
          sizeof (kate_header_0x81)), sizeof (kate_header_0x81));
  GST_BUFFER_OFFSET (inbuffer) = GST_BUFFER_OFFSET_END (inbuffer) = 0;
  fail_unless_equals_int (gst_pad_push (pad, inbuffer), GST_FLOW_OK);

  for (i = 2; i < 8; ++i) {
    inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x8x,
            sizeof (kate_header_0x8x)), sizeof (kate_header_0x8x));
    fail_if (gst_buffer_map (inbuffer, &info, GST_MAP_WRITE) != TRUE);
    info.data[0] = 0x80 | i;
    gst_buffer_unmap (inbuffer, &info);
    GST_BUFFER_OFFSET (inbuffer) = GST_BUFFER_OFFSET_END (inbuffer) = 0;
    fail_unless_equals_int (gst_pad_push (pad, inbuffer), GST_FLOW_OK);
  }

  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x88,
          sizeof (kate_header_0x88)), sizeof (kate_header_0x88));
  GST_BUFFER_OFFSET (inbuffer) = GST_BUFFER_OFFSET_END (inbuffer) = 0;
  fail_unless_equals_int (gst_pad_push (pad, inbuffer), GST_FLOW_OK);
}

GST_START_TEST (test_kate_parse)
{
  GstElement *kateparse;
  GstBuffer *inbuffer;
  GstBus *bus;

  kateparse = setup_kateparse ();
  fail_unless (gst_element_set_state (kateparse,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  gst_element_set_bus (kateparse, bus);

  test_kate_send_headers (kateparse, myparsesrcpad);

  /* push a text packet */
  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x00,
          sizeof (kate_header_0x00)), sizeof (kate_header_0x00));
  GST_BUFFER_TIMESTAMP (inbuffer) = GST_BUFFER_OFFSET (inbuffer) =
      1 * GST_SECOND;
  GST_BUFFER_DURATION (inbuffer) = 5 * GST_SECOND;
  GST_BUFFER_OFFSET_END (inbuffer) = (GST_BUFFER_TIMESTAMP (inbuffer) << 32);   /* granpos */
  fail_unless_equals_int (gst_pad_push (myparsesrcpad, inbuffer), GST_FLOW_OK);

  /* push a eos packet */
  inbuffer = gst_buffer_new_wrapped (g_memdup (kate_header_0x7f,
          sizeof (kate_header_0x7f)), sizeof (kate_header_0x7f));
  GST_BUFFER_TIMESTAMP (inbuffer) = GST_BUFFER_OFFSET (inbuffer) =
      6 * GST_SECOND;
  GST_BUFFER_DURATION (inbuffer) = 0;
  GST_BUFFER_OFFSET_END (inbuffer) = (GST_BUFFER_TIMESTAMP (inbuffer) << 32);   /* granpos */
  fail_unless_equals_int (gst_pad_push (myparsesrcpad, inbuffer), GST_FLOW_OK);

  /* signal eos */
  fail_unless (gst_pad_push_event (myparsesrcpad,
          gst_event_new_eos ()) == TRUE);

  fail_unless (gst_element_set_state (kateparse,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* at least one data packet and one EOS packet should have been emitted */
  check_buffers (2, TRUE);

  /* cleanup */
  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (kateparse, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_kateparse (kateparse);
  g_list_foreach (buffers, (GFunc) gst_buffer_unref, NULL);
  gst_check_drop_buffers ();
}

GST_END_TEST;

GST_START_TEST (test_kate_tag_passthrough)
{
  GstElement *katetag;
  GstBus *bus;
  GstBuffer *outbuffer;
  GList *list;
  GstMapInfo info;

  katetag = setup_katetag ();
  fail_unless (gst_element_set_state (katetag,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  gst_element_set_bus (katetag, bus);

  test_kate_send_headers (katetag, mytagsrcpad);

  /* signal eos */
  fail_unless (gst_pad_push_event (mytagsrcpad, gst_event_new_eos ()) == TRUE);

  fail_unless (gst_element_set_state (katetag,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* get the first buffer and check language/category */
  fail_unless (g_list_length (buffers) >= 2);   /* ID header, Vorbis comments header */
  outbuffer = GST_BUFFER (buffers->data);
  fail_if (outbuffer == NULL);

  /* check identification header is unchanged */
  list = g_list_nth (buffers, 0);
  fail_unless (list != NULL);
  outbuffer = list->data;
  fail_if (gst_buffer_map (outbuffer, &info, GST_MAP_READ) != TRUE);
  fail_unless_equals_int (info.size, sizeof (kate_header_0x80));
  fail_unless_equals_int (memcmp (info.data, kate_header_0x80,
          sizeof (kate_header_0x80)), 0);
  gst_buffer_unmap (outbuffer, &info);

  /* check comment header is unchanged */
  list = g_list_nth (buffers, 1);
  fail_unless (list != NULL);
  outbuffer = list->data;
  fail_if (gst_buffer_map (outbuffer, &info, GST_MAP_READ) != TRUE);
  fail_unless_equals_int (info.size, sizeof (kate_header_0x81));
  fail_unless_equals_int (memcmp (info.data, kate_header_0x81,
          sizeof (kate_header_0x81)), 0);
  gst_buffer_unmap (outbuffer, &info);

  /* all headers should have been emitted, but no particular packets */
  check_buffers (0, TRUE);

  /* cleanup */
  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (katetag, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_katetag (katetag);
  gst_check_drop_buffers ();
}

GST_END_TEST;

GST_START_TEST (test_kate_tag)
{
  GstElement *katetag;
  GstBus *bus;
  GstBuffer *outbuffer;
  GstMapInfo info;

  katetag = setup_katetag ();
  fail_unless (gst_element_set_state (katetag,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS,
      "could not set to playing");
  bus = gst_bus_new ();

  gst_element_set_bus (katetag, bus);

  g_object_set (katetag, "language", "cy", NULL);
  g_object_set (katetag, "category", "subtitles", NULL);

  test_kate_send_headers (katetag, mytagsrcpad);

  /* signal eos */
  fail_unless (gst_pad_push_event (mytagsrcpad, gst_event_new_eos ()) == TRUE);

  fail_unless (gst_element_set_state (katetag,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS,
      "could not set to ready");

  /* get the first buffer and check language/category */
  fail_unless (g_list_length (buffers) >= 1);
  outbuffer = GST_BUFFER (buffers->data);
  fail_if (outbuffer == NULL);
  assert_equals_int (gst_buffer_map (outbuffer, &info, GST_MAP_READ), TRUE);
  fail_if (info.size != 64);
  fail_if (strcmp ((const char *) info.data + 32, "cy"));
  fail_if (strcmp ((const char *) info.data + 48, "subtitles"));
  gst_buffer_unmap (outbuffer, &info);

  /* all headers should have been emitted, but no particular packets */
  check_buffers (0, TRUE);

  /* cleanup */
  gst_bus_set_flushing (bus, TRUE);
  gst_element_set_bus (katetag, NULL);
  gst_object_unref (GST_OBJECT (bus));
  cleanup_katetag (katetag);
  gst_check_drop_buffers ();
}

GST_END_TEST;

static Suite *
kate_suite (void)
{
  Suite *s = suite_create ("kate");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_kate_typefind);
  tcase_add_test (tc_chain, test_kate_empty_identification_header);
  tcase_add_test (tc_chain, test_kate_identification_header);
  tcase_add_test (tc_chain, test_kate_encode_nothing);
  tcase_add_test (tc_chain, test_kate_encode_empty);
  tcase_add_test (tc_chain, test_kate_encode_simple);
  tcase_add_test (tc_chain, test_kate_encode_spu);
  tcase_add_test (tc_chain, test_kate_encode_keepalives);
  tcase_add_test (tc_chain, test_kate_parse);
  tcase_add_test (tc_chain, test_kate_tag_passthrough);
  tcase_add_test (tc_chain, test_kate_tag);

  return s;
}

GST_CHECK_MAIN (kate);
