/* GStreamer UDP source unit tests
 * Copyright (C) 2011 Tim-Philipp Müller <tim centricular net>
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
#include <gio/gio.h>
#include <stdlib.h>
#include <unistd.h>

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

GST_START_TEST (test_udpsrc_empty_packet)
{
  GstElement *udpsrc;
  GSocket *socket;
  GstPad *sinkpad;
  int port = 0;

  udpsrc = gst_check_setup_element ("udpsrc");
  fail_unless (udpsrc != NULL);
  g_object_set (udpsrc, "port", 0, NULL);

  sinkpad = gst_check_setup_sink_pad_by_name (udpsrc, &sinktemplate, "src");
  fail_unless (sinkpad != NULL);
  gst_pad_set_active (sinkpad, TRUE);

  gst_element_set_state (udpsrc, GST_STATE_PLAYING);
  g_object_get (udpsrc, "port", &port, NULL);
  GST_INFO ("udpsrc port = %d", port);

  socket = g_socket_new (G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_DATAGRAM,
      G_SOCKET_PROTOCOL_UDP, NULL);

  if (socket != NULL) {
    GSocketAddress *sa;
    GInetAddress *ia;
    gchar *s;

    ia = g_inet_address_new_loopback (G_SOCKET_FAMILY_IPV4);
    s = g_inet_address_to_string (ia);
    GST_LOG ("inet address %s", s);
    g_free (s);
    sa = g_inet_socket_address_new (ia, port);

    if (g_socket_send_to (socket, sa, "HeLL0", 0, NULL, NULL) == 0) {
      GST_INFO ("sent 0 bytes");
      if (g_socket_send_to (socket, sa, "HeLL0", 6, NULL, NULL) == 6) {
        GstMapInfo map;
        GstBuffer *buf;
        guint len;

        GST_INFO ("sent 6 bytes");

        g_usleep (G_USEC_PER_SEC / 2);

        len = g_list_length (buffers);
        GST_INFO ("%u buffers", len);
        fail_unless (len == 1 || len == 2);

        /* last buffer should be our HeLL0 string */
        buf = GST_BUFFER (g_list_nth_data (buffers, len - 1));
        gst_buffer_map (buf, &map, GST_MAP_READ);
        fail_unless_equals_int (map.size, 6);
        fail_unless_equals_string ((gchar *) map.data, "HeLL0");
        gst_buffer_unmap (buf, &map);

        /* if there's another buffer, it should be 0 bytes */
        if (len == 2) {
          buf = GST_BUFFER (g_list_nth_data (buffers, 0));
          fail_unless_equals_int (gst_buffer_get_size (buf), 0);
        }
      } else {
        GST_WARNING ("send_to(6 bytes) failed");
      }
    } else {
      GST_WARNING ("send_to(0 bytes) failed");
    }

    g_object_unref (sa);
    g_object_unref (ia);
  } else {
    GST_WARNING ("Could not create IPv4 UDP socket for unit test");
  }

  gst_element_set_state (udpsrc, GST_STATE_NULL);

  gst_check_teardown_pad_by_name (udpsrc, "src");
  gst_check_teardown_element (udpsrc);

  g_object_unref (socket);
}

GST_END_TEST;

static Suite *
udpsrc_suite (void)
{
  Suite *s = suite_create ("udpsrc");
  TCase *tc_chain = tcase_create ("udpsrc");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_udpsrc_empty_packet);
  return s;
}

GST_CHECK_MAIN (udpsrc)
