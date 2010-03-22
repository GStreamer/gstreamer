/* GStreamer
 * Copyright (C) 2008 Nokia Corporation. (contact <stefan.kost@nokia.com>)
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

static GstTagList *received_tags = NULL;

static gboolean
bus_handler (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (message->type) {
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_WARNING:
    case GST_MESSAGE_ERROR:{
      GError *gerror;

      gchar *debug;

      gst_message_parse_error (message, &gerror, &debug);
      gst_object_default_error (GST_MESSAGE_SRC (message), gerror, debug);
      g_error_free (gerror);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_TAG:{
      if (received_tags == NULL) {
        gst_message_parse_tag (message, &received_tags);
      } else {
        GstTagList *tl = NULL, *ntl = NULL;

        gst_message_parse_tag (message, &tl);
        if (tl) {
          ntl = gst_tag_list_merge (received_tags, tl, GST_TAG_MERGE_PREPEND);
          if (ntl) {
            GST_LOG ("taglists merged: %" GST_PTR_FORMAT, ntl);
            gst_tag_list_free (received_tags);
            received_tags = ntl;
          }
          gst_tag_list_free (tl);
        }
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}


static void
test_tags (const gchar * tag_str)
{
  GstElement *pipeline;
  GstBus *bus;
  GMainLoop *loop;
  GstTagList *sent_tags;
  gint i, j, n_recv, n_sent;
  const gchar *name_sent, *name_recv;
  const GValue *value_sent, *value_recv;
  gboolean found, ok;
  gint comparison;
  GstElement *videotestsrc, *jpegenc, *metadatamux, *metadatademux, *fakesink;
  GstTagSetter *setter;

  GST_DEBUG ("testing tags : %s", tag_str);

  if (received_tags) {
    gst_tag_list_free (received_tags);
    received_tags = NULL;
  }

  pipeline = gst_pipeline_new ("pipeline");
  fail_unless (pipeline != NULL);

  videotestsrc = gst_element_factory_make ("videotestsrc", "src");
  fail_unless (videotestsrc != NULL);
  g_object_set (G_OBJECT (videotestsrc), "num-buffers", 1, NULL);

  jpegenc = gst_element_factory_make ("jpegenc", "enc");
  if (jpegenc == NULL) {
    g_print ("Cannot test - jpegenc not available\n");
    return;
  }

  metadatamux = gst_element_factory_make ("metadatamux", "mux");
  g_object_set (G_OBJECT (metadatamux), "exif", TRUE, NULL);
  fail_unless (metadatamux != NULL);

  metadatademux = gst_element_factory_make ("metadatademux", "demux");
  fail_unless (metadatademux != NULL);

  fakesink = gst_element_factory_make ("fakesink", "sink");
  fail_unless (fakesink != NULL);

  gst_bin_add_many (GST_BIN (pipeline), videotestsrc, jpegenc, metadatamux,
      metadatademux, fakesink, NULL);

  ok = gst_element_link_many (videotestsrc, jpegenc, metadatamux, metadatademux,
      fakesink, NULL);
  fail_unless (ok == TRUE);

  loop = g_main_loop_new (NULL, TRUE);
  fail_unless (loop != NULL);

  bus = gst_element_get_bus (pipeline);
  fail_unless (bus != NULL);
  gst_bus_add_watch (bus, bus_handler, loop);
  gst_object_unref (bus);

  gst_element_set_state (pipeline, GST_STATE_READY);

  setter = GST_TAG_SETTER (metadatamux);
  fail_unless (setter != NULL);
  sent_tags = gst_structure_from_string (tag_str, NULL);
  fail_unless (sent_tags != NULL);
  gst_tag_setter_merge_tags (setter, sent_tags, GST_TAG_MERGE_REPLACE);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  GST_DEBUG ("mainloop done : %p", received_tags);

  /* verify tags */
  fail_unless (received_tags != NULL);
  n_recv = gst_structure_n_fields (received_tags);
  n_sent = gst_structure_n_fields (sent_tags);
  /* we also get e.g. an exif binary block */
  fail_unless (n_recv >= n_sent);
  /* FIXME: compare taglits values */
  for (i = 0; i < n_sent; i++) {
    name_sent = gst_structure_nth_field_name (sent_tags, i);
    value_sent = gst_structure_get_value (sent_tags, name_sent);
    found = FALSE;
    for (j = 0; j < n_recv; j++) {
      name_recv = gst_structure_nth_field_name (received_tags, j);
      if (!strcmp (name_sent, name_recv)) {
        value_recv = gst_structure_get_value (received_tags, name_recv);
        comparison = gst_value_compare (value_sent, value_recv);
        if (comparison != GST_VALUE_EQUAL) {
          gchar *vs = g_strdup_value_contents (value_sent);
          gchar *vr = g_strdup_value_contents (value_recv);
          GST_DEBUG ("sent = %s:'%s', recv = %s:'%s'",
              G_VALUE_TYPE_NAME (value_sent), vs,
              G_VALUE_TYPE_NAME (value_recv), vr);
          g_free (vs);
          g_free (vr);
        }
        fail_unless (comparison == GST_VALUE_EQUAL,
            "tag item %s has been received with different type or value",
            name_sent);
        found = TRUE;
        break;
      }
    }
    fail_unless (found, "tag item %s is lost", name_sent);
  }

  gst_tag_list_free (received_tags);
  received_tags = NULL;
  gst_tag_list_free (sent_tags);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  g_main_loop_unref (loop);
  g_object_unref (pipeline);
}

GST_START_TEST (test_common_tags)
{
  /* The title tag will only work if the XMP backend is enabled.
   * And since we don't have any programmatic feedback on whether
   * a tag is properly handled or not... we need to do this kind
   * of hack. */
#ifdef HAVE_XMP
  test_tags ("taglist,title=\"test image\"");
#endif
}

GST_END_TEST;

GST_START_TEST (test_gps_tags)
{
  test_tags
      ("taglist,geo-location-latitude=66.1,geo-location-longitude=22.5,geo-location-elevation=10.3");
  test_tags
      ("taglist,geo-location-latitude=66.1,geo-location-longitude=22.5,geo-location-elevation=-10.3");
  test_tags
      ("taglist,geo-location-latitude=66.1,geo-location-longitude=-22.5,geo-location-elevation=10.3");
  test_tags
      ("taglist,geo-location-latitude=66.1,geo-location-longitude=-22.5,geo-location-elevation=-10.3");
  test_tags
      ("taglist,geo-location-latitude=-66.1,geo-location-longitude=22.5,geo-location-elevation=10.3");
  test_tags
      ("taglist,geo-location-latitude=-66.1,geo-location-longitude=22.5,geo-location-elevation=-10.3");
  test_tags
      ("taglist,geo-location-latitude=-66.1,geo-location-longitude=-22.5,geo-location-elevation=10.3");
  test_tags
      ("taglist,geo-location-latitude=-66.1,geo-location-longitude=-22.5,geo-location-elevation=-10.3");
}

GST_END_TEST;


static Suite *
metadata_suite (void)
{
  Suite *s = suite_create ("MetaData");

  TCase *tc_chain = tcase_create ("general");

  /* time out after 60s, not the default 3 */
  tcase_set_timeout (tc_chain, 60);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_common_tags);
  tcase_add_test (tc_chain, test_gps_tags);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = metadata_suite ();

  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
