/* GStreamer
 *
 * parent.c: test to check that setting state on a parent sets same state
 * recursively on children
 *
 * Copyright (C) <2004> Thomas Vander Stichele <thomas at apestaart dot org>
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

#include <gst/gst.h>

gint
main (gint argc, gchar * argv[])
{
  GstElement *pipeline;
  GstElement *bin1, *bin2;
  GstElement *fakesrc, *identity, *fakesink;

  gst_init (&argc, &argv);

  /*
   * +-pipeline----------------------------------------+
   * | +-bin2----------------------------------------+ |
   * | | +-bin1-----------------------+              | |
   * | | | +---------+   +----------+ | +----------+ | |
   * | | | | fakesrc |---| identity |---| fakesink | | |
   * | | | +---------+   +----------- | +----------+ | |
   * | | +----------------------------+              | |
   * | +---------------------------------------------+ |
   * +-------------------------------------------------+
   */

  pipeline = gst_pipeline_new ("pipeline");
  g_assert (pipeline);
  bin1 = gst_bin_new ("bin1");
  g_assert (bin1);
  bin2 = gst_bin_new ("bin2");
  g_assert (bin2);

  fakesrc = gst_element_factory_make ("fakesrc", "fakesrc");
  g_assert (fakesrc);
  g_object_set (G_OBJECT (fakesrc), "num_buffers", 5, NULL);
  identity = gst_element_factory_make ("identity", "identity");
  g_assert (identity);
  fakesink = gst_element_factory_make ("fakesink", "fakesink");
  g_assert (fakesink);

  gst_bin_add_many (GST_BIN (bin1), fakesrc, identity, NULL);
  g_assert (gst_element_link (fakesrc, identity));

  gst_bin_add_many (GST_BIN (bin2), bin1, fakesink, NULL);
  g_assert (gst_element_link (identity, fakesink));

  gst_bin_add (GST_BIN (pipeline), bin2);
  g_signal_connect (G_OBJECT (pipeline), "deep_notify",
      G_CALLBACK (gst_element_default_deep_notify), NULL);

  /* setting pipeline to READY should bring in all children to READY */
  gst_element_set_state (pipeline, GST_STATE_READY);
  g_assert (GST_STATE (bin1) == GST_STATE_READY);
  g_assert (GST_STATE (bin2) == GST_STATE_READY);
  g_assert (GST_STATE (fakesrc) == GST_STATE_READY);
  g_assert (GST_STATE (identity) == GST_STATE_READY);
  g_assert (GST_STATE (fakesink) == GST_STATE_READY);

  /* setting fakesink to PAUSED should set pipeline and bin2 to PAUSED */
  gst_element_set_state (fakesink, GST_STATE_PAUSED);
  g_assert (GST_STATE (bin1) == GST_STATE_READY);
  g_assert (GST_STATE (bin2) == GST_STATE_PAUSED);
  g_assert (GST_STATE (fakesrc) == GST_STATE_READY);
  g_assert (GST_STATE (identity) == GST_STATE_READY);
  g_assert (GST_STATE (fakesink) == GST_STATE_PAUSED);

  /* setting fakesrc to PAUSED should set bin1 and fakesrc to PAUSED */
  gst_element_set_state (fakesrc, GST_STATE_PAUSED);
  g_assert (GST_STATE (bin1) == GST_STATE_PAUSED);
  g_assert (GST_STATE (bin2) == GST_STATE_PAUSED);
  g_assert (GST_STATE (fakesrc) == GST_STATE_PAUSED);
  g_assert (GST_STATE (identity) == GST_STATE_READY);
  g_assert (GST_STATE (fakesink) == GST_STATE_PAUSED);

  /* setting bin1 to PAUSED, even though it is already, should set
   * identity to PAUSED as well */
  gst_element_set_state (bin1, GST_STATE_PAUSED);
  g_assert (GST_STATE (bin1) == GST_STATE_PAUSED);
  g_assert (GST_STATE (bin2) == GST_STATE_PAUSED);
  g_assert (GST_STATE (fakesrc) == GST_STATE_PAUSED);
  //FIXME: fix core so that this assert works
  //g_assert (GST_STATE (identity) == GST_STATE_PAUSED);
  g_assert (GST_STATE (fakesink) == GST_STATE_PAUSED);

  return 0;
}
