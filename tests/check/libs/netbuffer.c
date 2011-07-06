/* GStreamer unit tests for libgstnetbuffer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gst/check/gstcheck.h>
#include <gst/netbuffer/gstnetbuffer.h>

#define DATA_STRING "Yoho this is a string"

GST_START_TEST (test_netbuffer_copy)
{
  GstBuffer *netbuf, *copy;
  guint8 ipv6_addr[16] = { 0xff, 0x11, 0xee, 0x22, 0xdd, 0x33, 0xcc,
    0x44, 0xbb, 0x55, 0xaa, 0x66, 0x00, 0x77, 0x99, 0x88
  };
  guint8 ipv6_copy[16];
  guint32 ipv4_copy, ipv4_addr = 0xfe12dc34;
  guint16 ipv6_port = 3490;
  guint16 ipv4_port = 5678;
  guint16 port;
  GstMetaNetAddress *meta, *cmeta;
  gsize len;
  guint8 *data1, *data2;
  gsize size1, size2;

  netbuf = gst_buffer_new ();
  fail_unless (netbuf != NULL, "failed to create net buffer");
  meta = gst_buffer_add_meta_net_address (netbuf);

  gst_netaddress_set_ip4_address (&meta->naddr, ipv4_addr, ipv4_port);

  len = strlen (DATA_STRING);
  gst_buffer_take_memory (netbuf, -1,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          (gpointer) DATA_STRING, NULL, len, 0, len));

  GST_BUFFER_FLAG_SET (netbuf, GST_BUFFER_FLAG_DISCONT);

  copy = gst_buffer_copy (netbuf);
  fail_unless (copy != NULL, "failed to copy net buffer");

  cmeta = gst_buffer_get_meta_net_address (copy);
  fail_unless (cmeta != NULL, "copied buffer is not a GstNetBuffer!");

  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT_VALUE (copy), 1);

  data1 = gst_buffer_map (netbuf, &size1, NULL, GST_MAP_READ);
  data2 = gst_buffer_map (copy, &size2, NULL, GST_MAP_READ);
  fail_unless_equals_int (size1, size2);
  fail_unless (memcmp (data1, data2, size1) == 0);
  gst_buffer_unmap (copy, data2, size2);
  gst_buffer_unmap (netbuf, data1, size1);

  fail_unless (GST_BUFFER_FLAG_IS_SET (copy, GST_BUFFER_FLAG_DISCONT));

  fail_unless (gst_netaddress_get_ip4_address (&cmeta->naddr, &ipv4_copy,
          &port));
  fail_unless (ipv4_copy == ipv4_addr,
      "Copied buffer has wrong IPV4 from address");
  fail_unless (port == ipv4_port, "Copied buffer has wrong IPV4 from port");
  gst_buffer_unref (netbuf);
  gst_buffer_unref (copy);

  netbuf = gst_buffer_new ();
  fail_unless (netbuf != NULL, "failed to create net buffer");
  meta = gst_buffer_add_meta_net_address (netbuf);

  gst_netaddress_set_ip6_address (&meta->naddr, ipv6_addr, ipv6_port);

  len = strlen (DATA_STRING);
  gst_buffer_take_memory (netbuf, -1,
      gst_memory_new_wrapped (GST_MEMORY_FLAG_READONLY,
          (gpointer) DATA_STRING, NULL, len, 0, len));

  GST_BUFFER_FLAG_SET (netbuf, GST_BUFFER_FLAG_DISCONT);

  copy = gst_buffer_copy (netbuf);
  fail_unless (copy != NULL, "failed to copy net buffer");

  cmeta = gst_buffer_get_meta_net_address (copy);
  fail_unless (cmeta != NULL, "copied buffer is not a GstNetBuffer!");

  fail_unless_equals_int (GST_MINI_OBJECT_REFCOUNT_VALUE (copy), 1);

  data1 = gst_buffer_map (netbuf, &size1, NULL, GST_MAP_READ);
  data2 = gst_buffer_map (copy, &size2, NULL, GST_MAP_READ);
  fail_unless_equals_int (size1, size2);
  fail_unless (memcmp (data1, data2, size1) == 0);
  gst_buffer_unmap (copy, data2, size2);
  gst_buffer_unmap (netbuf, data1, size1);

  fail_unless (GST_BUFFER_FLAG_IS_SET (copy, GST_BUFFER_FLAG_DISCONT));

  fail_unless (gst_netaddress_get_ip6_address (&cmeta->naddr, ipv6_copy,
          &port));
  fail_unless (memcmp (ipv6_copy, ipv6_addr, 16) == 0,
      "Copied buffer has wrong IPv6 destination address");
  fail_unless (port == ipv6_port,
      "Copied buffer has wrong IPv6 destination port");
  gst_buffer_unref (netbuf);
  gst_buffer_unref (copy);

}

GST_END_TEST;

static Suite *
netbuffer_suite (void)
{
  Suite *s = suite_create ("netbuffer");
  TCase *tc_chain = tcase_create ("netbuffer");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_netbuffer_copy);

  return s;
}

GST_CHECK_MAIN (netbuffer);
