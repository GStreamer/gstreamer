/* GStreamer
 * Copyright (C) 2005 Wim Taymans <wim@fluendo.com>
 *
 * gstghostpad.c: Unit test for GstGhostPad
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

/* test if removing a bin also cleans up the ghostpads
 */
GST_START_TEST (test_remove1)
{
  GstElement *b1, *b2, *src, *sink;
  GstPad *srcpad, *sinkpad;
  GstPadLinkReturn ret;

  b1 = gst_element_factory_make ("pipeline", NULL);
  b2 = gst_element_factory_make ("bin", NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  ASSERT_OBJECT_REFCOUNT (b1, "pipeline", 1);
  ASSERT_OBJECT_REFCOUNT (b2, "bin", 1);

  fail_unless (gst_bin_add (GST_BIN (b2), sink));
  fail_unless (gst_bin_add (GST_BIN (b1), src));
  ASSERT_OBJECT_REFCOUNT (b1, "pipeline", 1);
  ASSERT_OBJECT_REFCOUNT (b2, "bin", 1);
  fail_unless (gst_bin_add (GST_BIN (b1), b2));
  ASSERT_OBJECT_REFCOUNT (b1, "pipeline", 1);
  ASSERT_OBJECT_REFCOUNT (b2, "bin", 1);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_element_add_pad (b2, gst_ghost_pad_new ("sink", sinkpad));
  gst_object_unref (sinkpad);

  srcpad = gst_element_get_static_pad (src, "src");
  /* get the ghostpad */
  sinkpad = gst_element_get_static_pad (b2, "sink");

  ret = gst_pad_link (srcpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* now remove the bin with the ghostpad, b2 is disposed now. */
  ASSERT_OBJECT_REFCOUNT (b1, "pipeline", 1);
  ASSERT_OBJECT_REFCOUNT (b2, "bin", 1);
  gst_bin_remove (GST_BIN (b1), b2);

  srcpad = gst_element_get_static_pad (src, "src");
  /* pad cannot be linked now */
  fail_if (gst_pad_is_linked (srcpad));
  gst_object_unref (srcpad);

  ASSERT_OBJECT_REFCOUNT (b1, "pipeline", 1);
  gst_object_unref (b1);
}

GST_END_TEST;

/* test if removing a bin also cleans up the ghostpads
 */
GST_START_TEST (test_remove2)
{
  GstElement *b1, *b2, *src, *sink;
  GstPad *srcpad, *sinkpad;
  GstPadLinkReturn ret;

  b1 = gst_element_factory_make ("pipeline", NULL);
  b2 = gst_element_factory_make ("bin", NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  fail_unless (gst_bin_add (GST_BIN (b2), sink));
  fail_unless (gst_bin_add (GST_BIN (b1), src));
  fail_unless (gst_bin_add (GST_BIN (b1), b2));
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_element_add_pad (b2, gst_ghost_pad_new ("sink", sinkpad));
  gst_object_unref (sinkpad);

  srcpad = gst_element_get_static_pad (src, "src");
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 2); /* since we got one */
  /* get the ghostpad */
  sinkpad = gst_element_get_static_pad (b2, "sink");
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);       /* since we got one */

  GST_DEBUG ("linking srcpad and sinkpad");
  ret = gst_pad_link (srcpad, sinkpad);
  GST_DEBUG ("linked srcpad and sinkpad");
  fail_unless (ret == GST_PAD_LINK_OK);
  /* the linking causes a proxypad to be created for srcpad,
   * to which sinkpad gets linked.  This proxypad has a ref to srcpad */
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 3);
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 2);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* now remove the sink from the bin */
  gst_bin_remove (GST_BIN (b2), sink);

  srcpad = gst_element_get_static_pad (src, "src");
  /* pad is still linked to ghostpad */
  fail_if (!gst_pad_is_linked (srcpad));
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 3);
  gst_object_unref (srcpad);
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 1);

  /* cleanup */
  /* now unlink the pads */
  gst_pad_unlink (srcpad, sinkpad);
  ASSERT_OBJECT_REFCOUNT (srcpad, "srcpad", 1); /* proxy has dropped ref */
  ASSERT_OBJECT_REFCOUNT (sinkpad, "sinkpad", 1);

  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  ASSERT_OBJECT_REFCOUNT (b2, "bin", 1);
  /* remove b2 from b1 */
  gst_bin_remove (GST_BIN (b1), b2);

  /* flush the message, dropping the b1 refcount to 1 */
  gst_element_set_state (b1, GST_STATE_READY);
  gst_element_set_state (b1, GST_STATE_NULL);
  ASSERT_OBJECT_REFCOUNT (b1, "pipeline", 1);
  gst_object_unref (b1);
}

GST_END_TEST;



/* test if a ghost pad without a target can be linked and
 * unlinked. An untargeted ghostpad has a default ANY caps unless there 
 * is a padtemplate that says something else.
 */
GST_START_TEST (test_ghost_pads_notarget)
{
  GstElement *b1, *b2, *sink;
  GstPad *srcpad, *sinkpad, *peer;
  GstPadLinkReturn ret;
  gboolean bret;
  GstBus *bus;
  GstCaps *caps;

  b1 = gst_element_factory_make ("pipeline", NULL);

  /* make sure all messages are discarded */
  bus = gst_pipeline_get_bus (GST_PIPELINE (b1));
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  b2 = gst_element_factory_make ("bin", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  fail_unless (gst_bin_add (GST_BIN (b1), sink));
  fail_unless (gst_bin_add (GST_BIN (b1), b2));

  srcpad = gst_ghost_pad_new_no_target ("src", GST_PAD_SRC);
  fail_unless (srcpad != NULL);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_unless (sinkpad != NULL);

  ret = gst_pad_link (srcpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_OK);

  /* check if the peers are ok */
  peer = gst_pad_get_peer (srcpad);
  fail_unless (peer == sinkpad);
  gst_object_unref (peer);

  peer = gst_pad_get_peer (sinkpad);
  fail_unless (peer == srcpad);
  gst_object_unref (peer);

  /* check caps, untargetted pad should return ANY or the padtemplate caps 
   * when it was created from a template */
  caps = gst_pad_get_caps (srcpad);
  fail_unless (gst_caps_is_any (caps));
  gst_caps_unref (caps);

  /* unlink */
  bret = gst_pad_unlink (srcpad, sinkpad);
  fail_unless (bret == TRUE);

  /* cleanup */
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
  gst_object_unref (b1);
}

GST_END_TEST;

/* Test that removing the target of a ghostpad properly sets the target of the
 * ghostpad to NULL */
GST_START_TEST (test_remove_target)
{
  GstElement *b1, *b2, *src, *sink;
  GstPad *sinkpad, *ghost, *target;

  b1 = gst_element_factory_make ("pipeline", NULL);
  b2 = gst_element_factory_make ("bin", NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  fail_unless (gst_bin_add (GST_BIN (b2), sink));
  fail_unless (gst_bin_add (GST_BIN (b1), src));
  fail_unless (gst_bin_add (GST_BIN (b1), b2));
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  sinkpad = gst_element_get_static_pad (sink, "sink");
  gst_element_add_pad (b2, gst_ghost_pad_new ("sink", sinkpad));

  ghost = gst_element_get_static_pad (b2, "sink");

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (ghost));
  fail_unless (target == sinkpad);
  gst_object_unref (target);
  gst_object_unref (sinkpad);

  gst_bin_remove (GST_BIN (b2), sink);

  target = gst_ghost_pad_get_target (GST_GHOST_PAD (ghost));
  fail_unless (target == NULL);

  gst_object_unref (b1);
}

GST_END_TEST;


/* test if linking fails over different bins using a pipeline
 * like this:
 *
 * fakesrc num_buffers=10 ! ( fakesink )
 *
 */
GST_START_TEST (test_link)
{
  GstElement *b1, *b2, *src, *sink;
  GstPad *srcpad, *sinkpad, *gpad;
  GstPadLinkReturn ret;

  b1 = gst_element_factory_make ("pipeline", NULL);
  b2 = gst_element_factory_make ("bin", NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  fail_unless (gst_bin_add (GST_BIN (b2), sink));
  fail_unless (gst_bin_add (GST_BIN (b1), src));
  fail_unless (gst_bin_add (GST_BIN (b1), b2));

  srcpad = gst_element_get_static_pad (src, "src");
  fail_unless (srcpad != NULL);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  fail_unless (sinkpad != NULL);

  /* linking in different hierarchies should fail */
  ret = gst_pad_link (srcpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_WRONG_HIERARCHY);

  /* now setup a ghostpad */
  gpad = gst_ghost_pad_new ("sink", sinkpad);
  gst_object_unref (sinkpad);
  /* need to ref as _add_pad takes ownership */
  gst_object_ref (gpad);
  gst_element_add_pad (b2, gpad);

  /* our new sinkpad */
  sinkpad = gpad;

  /* and linking should work now */
  ret = gst_pad_link (srcpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_OK);

  /* flush the message, dropping the b1 refcount to 1 */
  gst_element_set_state (b1, GST_STATE_READY);
  gst_element_set_state (b1, GST_STATE_NULL);
  ASSERT_OBJECT_REFCOUNT (b1, "pipeline", 1);

  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);
  gst_object_unref (b1);
}

GST_END_TEST;

/* test if ghostpads are created automagically when using
 * gst_element_link_pads.
 *
 * fakesrc num_buffers=10 ! ( identity ) ! fakesink
 */
GST_START_TEST (test_ghost_pads)
{
  GstElement *b1, *b2, *src, *i1, *sink;
  GstPad *gsink, *gsrc, *gisrc, *gisink, *isink, *isrc, *fsrc, *fsink;
  GstStateChangeReturn ret;

  b1 = gst_element_factory_make ("pipeline", NULL);
  b2 = gst_element_factory_make ("bin", NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src, "num-buffers", (int) 10, NULL);
  i1 = gst_element_factory_make ("identity", NULL);
  sink = gst_element_factory_make ("fakesink", NULL);

  fail_unless (gst_bin_add (GST_BIN (b2), i1));
  fail_unless (gst_bin_add (GST_BIN (b1), src));
  fail_unless (gst_bin_add (GST_BIN (b1), b2));
  fail_unless (gst_bin_add (GST_BIN (b1), sink));
  fail_unless (gst_element_link_pads (src, NULL, i1, NULL));
  fail_unless (gst_element_link_pads (i1, NULL, sink, NULL));
  GST_OBJECT_LOCK (b2);
  fail_unless (b2->numsinkpads == 1);
  fail_unless (GST_IS_GHOST_PAD (b2->sinkpads->data));
  fail_unless (b2->numsrcpads == 1);
  fail_unless (GST_IS_GHOST_PAD (b2->srcpads->data));
  GST_OBJECT_UNLOCK (b2);

  fsrc = gst_element_get_static_pad (src, "src");
  fail_unless (fsrc != NULL);
  gsink = GST_PAD (gst_object_ref (b2->sinkpads->data));
  fail_unless (gsink != NULL);
  gsrc = GST_PAD (gst_object_ref (b2->srcpads->data));
  fail_unless (gsrc != NULL);
  fsink = gst_element_get_static_pad (sink, "sink");
  fail_unless (fsink != NULL);

  isink = gst_element_get_static_pad (i1, "sink");
  fail_unless (isink != NULL);
  isrc = gst_element_get_static_pad (i1, "src");
  fail_unless (isrc != NULL);
  gisrc = gst_pad_get_peer (isink);
  fail_unless (gisrc != NULL);
  gisink = gst_pad_get_peer (isrc);
  fail_unless (gisink != NULL);

  /* all objects above have one refcount owned by us as well */

  ASSERT_OBJECT_REFCOUNT (fsrc, "fsrc", 3);     /* parent and gisrc */
  ASSERT_OBJECT_REFCOUNT (gsink, "gsink", 2);   /* parent */
  ASSERT_OBJECT_REFCOUNT (gsrc, "gsrc", 2);     /* parent */
  ASSERT_OBJECT_REFCOUNT (fsink, "fsink", 3);   /* parent and gisink */

  ASSERT_OBJECT_REFCOUNT (gisrc, "gisrc", 2);   /* parent */
  ASSERT_OBJECT_REFCOUNT (isink, "isink", 3);   /* parent and gsink */
  ASSERT_OBJECT_REFCOUNT (gisink, "gisink", 2); /* parent */
  ASSERT_OBJECT_REFCOUNT (isrc, "isrc", 3);     /* parent and gsrc */

  ret = gst_element_set_state (b1, GST_STATE_PLAYING);
  ret = gst_element_get_state (b1, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  ret = gst_element_set_state (b1, GST_STATE_NULL);
  ret = gst_element_get_state (b1, NULL, NULL, GST_CLOCK_TIME_NONE);
  fail_unless (ret == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (b1);
  /* unreffing the bin will unref all elements, which will unlink and unparent
   * all pads */

  /* wait for thread to settle down */
  while (GST_OBJECT_REFCOUNT_VALUE (fsrc) > 1)
    THREAD_SWITCH ();

  ASSERT_OBJECT_REFCOUNT (fsrc, "fsrc", 1);
  ASSERT_OBJECT_REFCOUNT (gsink, "gsink", 1);
  ASSERT_OBJECT_REFCOUNT (gsrc, "gsink", 1);
  ASSERT_OBJECT_REFCOUNT (fsink, "fsink", 1);

  ASSERT_OBJECT_REFCOUNT (gisrc, "gisrc", 2);   /* gsink */
  ASSERT_OBJECT_REFCOUNT (isink, "isink", 1);   /* gsink */
  ASSERT_OBJECT_REFCOUNT (gisink, "gisink", 2); /* gsrc */
  ASSERT_OBJECT_REFCOUNT (isrc, "isrc", 1);     /* gsrc */

  gst_object_unref (gsink);
  ASSERT_OBJECT_REFCOUNT (isink, "isink", 1);
  ASSERT_OBJECT_REFCOUNT (gisrc, "gisrc", 1);
  ASSERT_OBJECT_REFCOUNT (fsrc, "fsrc", 1);
  gst_object_unref (gisrc);
  ASSERT_OBJECT_REFCOUNT (fsrc, "fsrc", 1);

  gst_object_unref (gsrc);
  ASSERT_OBJECT_REFCOUNT (isrc, "isrc", 1);
  ASSERT_OBJECT_REFCOUNT (gisink, "gisink", 1);
  ASSERT_OBJECT_REFCOUNT (fsink, "fsink", 1);
  gst_object_unref (gisink);
  ASSERT_OBJECT_REFCOUNT (fsink, "fsink", 1);

  gst_object_unref (fsrc);
  gst_object_unref (isrc);
  gst_object_unref (isink);
  gst_object_unref (fsink);
}

GST_END_TEST;

GST_START_TEST (test_ghost_pads_bin)
{
  GstBin *pipeline;
  GstBin *srcbin;
  GstBin *sinkbin;
  GstElement *src;
  GstElement *sink;
  GstPad *srcpad, *srcghost, *target;
  GstPad *sinkpad, *sinkghost;

  pipeline = GST_BIN (gst_pipeline_new ("pipe"));
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  srcbin = GST_BIN (gst_bin_new ("srcbin"));
  gst_bin_add (pipeline, GST_ELEMENT (srcbin));
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  sinkbin = GST_BIN (gst_bin_new ("sinkbin"));
  gst_bin_add (pipeline, GST_ELEMENT (sinkbin));
  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  src = gst_element_factory_make ("fakesrc", "src");
  gst_bin_add (srcbin, src);
  srcpad = gst_element_get_static_pad (src, "src");
  srcghost = gst_ghost_pad_new ("src", srcpad);
  gst_object_unref (srcpad);
  gst_element_add_pad (GST_ELEMENT (srcbin), srcghost);

  sink = gst_element_factory_make ("fakesink", "sink");
  gst_bin_add (sinkbin, sink);
  sinkpad = gst_element_get_static_pad (sink, "sink");
  sinkghost = gst_ghost_pad_new ("sink", sinkpad);
  gst_object_unref (sinkpad);
  gst_element_add_pad (GST_ELEMENT (sinkbin), sinkghost);

  gst_element_link (GST_ELEMENT (srcbin), GST_ELEMENT (sinkbin));

  fail_unless (GST_PAD_PEER (srcghost) != NULL);
  fail_unless (GST_PAD_PEER (sinkghost) != NULL);
  target = gst_ghost_pad_get_target (GST_GHOST_PAD (srcghost));
  fail_unless (GST_PAD_PEER (target) != NULL);
  gst_object_unref (target);
  target = gst_ghost_pad_get_target (GST_GHOST_PAD (sinkghost));
  fail_unless (GST_PAD_PEER (target) != NULL);
  gst_object_unref (target);

  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);

  gst_object_unref (pipeline);
}

GST_END_TEST;

typedef struct
{
  GMutex *mutex;
  GCond *cond;
} BlockData;

static void
block_callback (GstPad * pad, gboolean blocked, gpointer user_data)
{
  BlockData *block_data = (BlockData *) user_data;

  g_mutex_lock (block_data->mutex);
  GST_DEBUG ("blocked\n");
  g_cond_signal (block_data->cond);
  g_mutex_unlock (block_data->mutex);
}

GST_START_TEST (test_ghost_pads_block)
{
  GstBin *pipeline;
  GstBin *srcbin;
  GstElement *src;
  GstPad *srcpad;
  GstPad *srcghost;
  BlockData block_data;

  pipeline = GST_BIN (gst_pipeline_new ("pipeline"));

  srcbin = GST_BIN (gst_bin_new ("srcbin"));
  gst_bin_add (pipeline, GST_ELEMENT (srcbin));

  src = gst_element_factory_make ("fakesrc", "src");
  gst_bin_add (srcbin, src);
  srcpad = gst_element_get_static_pad (src, "src");
  srcghost = gst_ghost_pad_new ("src", srcpad);
  gst_element_add_pad (GST_ELEMENT (srcbin), srcghost);
  gst_object_unref (srcpad);

  block_data.mutex = g_mutex_new ();
  block_data.cond = g_cond_new ();

  g_mutex_lock (block_data.mutex);
  gst_pad_set_blocked_async (srcghost, TRUE, block_callback, &block_data);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  /* and wait now */
  g_cond_wait (block_data.cond, block_data.mutex);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  g_mutex_unlock (block_data.mutex);

  g_mutex_free (block_data.mutex);
  g_cond_free (block_data.cond);

  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_ghost_pads_probes)
{
  GstBin *pipeline;
  GstBin *srcbin;
  GstElement *src;
  GstPad *srcpad;
  GstPad *srcghost;
  BlockData block_data;

  pipeline = GST_BIN (gst_pipeline_new ("pipeline"));

  srcbin = GST_BIN (gst_bin_new ("srcbin"));
  gst_bin_add (pipeline, GST_ELEMENT (srcbin));

  src = gst_element_factory_make ("fakesrc", "src");
  gst_bin_add (srcbin, src);
  srcpad = gst_element_get_static_pad (src, "src");
  srcghost = gst_ghost_pad_new ("src", srcpad);
  gst_element_add_pad (GST_ELEMENT (srcbin), srcghost);
  gst_object_unref (srcpad);

  block_data.mutex = g_mutex_new ();
  block_data.cond = g_cond_new ();

  g_mutex_lock (block_data.mutex);
  gst_pad_set_blocked_async (srcghost, TRUE, block_callback, &block_data);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  /* and wait now */
  g_cond_wait (block_data.cond, block_data.mutex);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_NULL);
  g_mutex_unlock (block_data.mutex);

  g_mutex_free (block_data.mutex);
  g_cond_free (block_data.cond);

  ASSERT_OBJECT_REFCOUNT (pipeline, "pipeline", 1);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_ghost_pads_new_from_template)
{
  GstPad *sinkpad, *ghostpad;
  GstPadTemplate *padtempl, *ghosttempl;
  GstCaps *padcaps, *ghostcaps, *newcaps;
  GstCaps *copypadcaps;

  padcaps = gst_caps_from_string ("some/caps");
  fail_unless (padcaps != NULL);
  ghostcaps = gst_caps_from_string ("some/caps;some/other-caps");
  fail_unless (ghostcaps != NULL);

  copypadcaps = gst_caps_copy (padcaps);
  padtempl = gst_pad_template_new ("padtempl", GST_PAD_SINK,
      GST_PAD_ALWAYS, copypadcaps);
  fail_unless (padtempl != NULL);
  ghosttempl = gst_pad_template_new ("ghosttempl", GST_PAD_SINK,
      GST_PAD_ALWAYS, ghostcaps);

  sinkpad = gst_pad_new_from_template (padtempl, "sinkpad");
  fail_unless (sinkpad != NULL);

  ghostpad = gst_ghost_pad_new_from_template ("ghostpad", sinkpad, ghosttempl);
  fail_unless (ghostpad != NULL);

  /* check template is properly set */
  fail_unless (GST_PAD_PAD_TEMPLATE (ghostpad) == ghosttempl);

  /* check ghostpad caps are from the sinkpad */
  newcaps = gst_pad_get_caps (ghostpad);
  fail_unless (newcaps != NULL);
  fail_unless (gst_caps_is_equal (newcaps, padcaps));
  gst_caps_unref (newcaps);
  gst_caps_unref (padcaps);

  gst_object_unref (sinkpad);
  gst_object_unref (ghostpad);

  gst_object_unref (padtempl);
  gst_object_unref (ghosttempl);
}

GST_END_TEST;

GST_START_TEST (test_ghost_pads_new_no_target_from_template)
{
  GstPad *sinkpad, *ghostpad;
  GstPadTemplate *padtempl, *ghosttempl;
  GstCaps *padcaps, *ghostcaps, *newcaps;
  GstCaps *copypadcaps, *copyghostcaps;

  padcaps = gst_caps_from_string ("some/caps");
  fail_unless (padcaps != NULL);
  ghostcaps = gst_caps_from_string ("some/caps;some/other-caps");
  fail_unless (ghostcaps != NULL);

  copypadcaps = gst_caps_copy (padcaps);
  copyghostcaps = gst_caps_copy (ghostcaps);

  padtempl = gst_pad_template_new ("padtempl", GST_PAD_SINK,
      GST_PAD_ALWAYS, copypadcaps);
  fail_unless (padtempl != NULL);
  ghosttempl = gst_pad_template_new ("ghosttempl", GST_PAD_SINK,
      GST_PAD_ALWAYS, copyghostcaps);

  sinkpad = gst_pad_new_from_template (padtempl, "sinkpad");
  fail_unless (sinkpad != NULL);

  ghostpad = gst_ghost_pad_new_no_target_from_template ("ghostpad", ghosttempl);
  fail_unless (ghostpad != NULL);

  /* check template is properly set */
  fail_unless (GST_PAD_PAD_TEMPLATE (ghostpad) == ghosttempl);

  /* check ghostpad caps are from the ghostpad template */
  newcaps = gst_pad_get_caps (ghostpad);
  fail_unless (newcaps != NULL);
  fail_unless (gst_caps_is_equal (newcaps, ghostcaps));
  gst_caps_unref (newcaps);

  fail_unless (gst_ghost_pad_set_target ((GstGhostPad *) ghostpad, sinkpad));

  /* check ghostpad caps are now from the target pad */
  newcaps = gst_pad_get_caps (ghostpad);
  fail_unless (newcaps != NULL);
  fail_unless (gst_caps_is_equal (newcaps, padcaps));
  gst_caps_unref (newcaps);

  gst_object_unref (sinkpad);
  gst_object_unref (ghostpad);

  gst_object_unref (padtempl);
  gst_object_unref (ghosttempl);

  gst_caps_unref (padcaps);
  gst_caps_unref (ghostcaps);
}

GST_END_TEST;

static void
ghost_notify_caps (GObject * object, GParamSpec * pspec, gpointer * user_data)
{
  GST_DEBUG ("caps notify called");
  (*(gint *) user_data)++;
}

GST_START_TEST (test_ghost_pads_forward_setcaps)
{
  GstCaps *templ_caps, *caps1, *caps2;
  GstPadTemplate *src_template, *sink_template;
  GstPad *src, *ghost, *sink;
  gint notify_counter = 0;

  templ_caps = gst_caps_from_string ("meh; muh");
  src_template = gst_pad_template_new ("src", GST_PAD_SRC,
      GST_PAD_ALWAYS, templ_caps);

  templ_caps = gst_caps_from_string ("muh; meh");
  sink_template = gst_pad_template_new ("sink", GST_PAD_SINK,
      GST_PAD_ALWAYS, templ_caps);

  src = gst_pad_new_from_template (src_template, "src");
  sink = gst_pad_new_from_template (sink_template, "sink");

  /* ghost source pad, setting caps on the source influences the caps of the
   * ghostpad. */
  ghost = gst_ghost_pad_new ("ghostsrc", src);
  g_signal_connect (ghost, "notify::caps",
      G_CALLBACK (ghost_notify_caps), &notify_counter);
  fail_unless (gst_pad_link (ghost, sink) == GST_PAD_LINK_OK);

  caps1 = gst_caps_from_string ("meh");
  fail_unless (gst_pad_set_caps (src, caps1));
  caps2 = GST_PAD_CAPS (ghost);
  fail_unless (gst_caps_is_equal (caps1, caps2));
  fail_unless_equals_int (notify_counter, 1);

  gst_object_unref (ghost);
  gst_caps_unref (caps1);

  fail_unless (gst_pad_set_caps (src, NULL));

  /* source 2, setting the caps on the ghostpad does not influence the caps of
   * the target */
  notify_counter = 0;
  ghost = gst_ghost_pad_new ("ghostsrc", src);
  g_signal_connect (ghost, "notify::caps",
      G_CALLBACK (ghost_notify_caps), &notify_counter);
  fail_unless (gst_pad_link (ghost, sink) == GST_PAD_LINK_OK);

  caps1 = gst_caps_from_string ("meh");
  fail_unless (gst_pad_set_caps (ghost, caps1));
  caps2 = GST_PAD_CAPS (src);
  fail_unless (caps2 == NULL);
  fail_unless_equals_int (notify_counter, 1);

  gst_object_unref (ghost);
  gst_caps_unref (caps1);


  /* ghost sink pad. Setting caps on the ghostpad will also set those caps on
   * the target pad. */
  notify_counter = 0;
  ghost = gst_ghost_pad_new ("ghostsink", sink);
  g_signal_connect (ghost, "notify::caps",
      G_CALLBACK (ghost_notify_caps), &notify_counter);
  fail_unless (gst_pad_link (src, ghost) == GST_PAD_LINK_OK);

  caps1 = gst_caps_from_string ("muh");
  fail_unless (gst_pad_set_caps (ghost, caps1));
  caps2 = GST_PAD_CAPS (sink);
  fail_unless (gst_caps_is_equal (caps1, caps2));
  fail_unless_equals_int (notify_counter, 1);

  gst_object_unref (ghost);
  gst_caps_unref (caps1);

  /* sink pad 2, setting caps just on the target pad should not influence the caps
   * on the ghostpad. */
  notify_counter = 0;
  ghost = gst_ghost_pad_new ("ghostsink", sink);
  g_signal_connect (ghost, "notify::caps",
      G_CALLBACK (ghost_notify_caps), &notify_counter);
  fail_unless (gst_pad_link (src, ghost) == GST_PAD_LINK_OK);

  caps1 = gst_caps_from_string ("muh");
  fail_unless (gst_pad_set_caps (sink, caps1));
  caps2 = GST_PAD_CAPS (ghost);
  fail_unless (caps2 == NULL);
  fail_unless_equals_int (notify_counter, 0);

  gst_object_unref (ghost);
  gst_caps_unref (caps1);

  gst_object_unref (src);
  gst_object_unref (sink);
  gst_object_unref (src_template);
  gst_object_unref (sink_template);
}

GST_END_TEST;

static gint linked_count1;
static gint unlinked_count1;
static gint linked_count2;
static gint unlinked_count2;

static GstPadLinkReturn
pad_linked1 (GstPad * pad, GstPad * peer)
{
  linked_count1++;

  return GST_PAD_LINK_OK;
}

static void
pad_unlinked1 (GstPad * pad)
{
  unlinked_count1++;
}

static GstPadLinkReturn
pad_linked2 (GstPad * pad, GstPad * peer)
{
  linked_count2++;

  return GST_PAD_LINK_OK;
}

static void
pad_unlinked2 (GstPad * pad)
{
  unlinked_count2++;
}

GST_START_TEST (test_ghost_pads_sink_link_unlink)
{
  GstCaps *padcaps;
  GstPad *srcpad, *sinkpad, *ghostpad;
  GstPadTemplate *srctempl, *sinktempl;
  GstPadLinkReturn ret;
  gboolean res;

  padcaps = gst_caps_from_string ("some/caps");
  fail_unless (padcaps != NULL);
  srctempl = gst_pad_template_new ("srctempl", GST_PAD_SRC,
      GST_PAD_ALWAYS, padcaps);

  padcaps = gst_caps_from_string ("some/caps");
  fail_unless (padcaps != NULL);
  sinktempl = gst_pad_template_new ("sinktempl", GST_PAD_SINK,
      GST_PAD_ALWAYS, padcaps);

  srcpad = gst_pad_new_from_template (srctempl, "src");
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new_from_template (sinktempl, "sink");
  fail_unless (sinkpad != NULL);

  /* set up link/unlink functions for the pad */
  linked_count1 = unlinked_count1 = 0;
  gst_pad_set_link_function (sinkpad, pad_linked1);
  gst_pad_set_unlink_function (sinkpad, pad_unlinked1);
  linked_count2 = unlinked_count2 = 0;
  gst_pad_set_link_function (srcpad, pad_linked2);
  gst_pad_set_unlink_function (srcpad, pad_unlinked2);

  /* this should trigger a link from the internal pad to the sinkpad */
  ghostpad = gst_ghost_pad_new ("ghostpad", sinkpad);
  fail_unless (ghostpad != NULL);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 0);
  fail_unless (unlinked_count2 == 0);

  /* this should not trigger anything because we are not directly
   * linking/unlinking the sink pad. */
  ret = gst_pad_link (srcpad, ghostpad);
  fail_unless (ret == GST_PAD_LINK_OK);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 0);

  res = gst_pad_unlink (srcpad, ghostpad);
  fail_unless (res == TRUE);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 1);

  /* this should trigger the unlink */
  res = gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ghostpad), NULL);
  fail_unless (res == TRUE);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 1);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 1);

  gst_object_unref (ghostpad);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (srctempl);
  gst_object_unref (sinktempl);
}

GST_END_TEST;

GST_START_TEST (test_ghost_pads_src_link_unlink)
{
  GstCaps *padcaps;
  GstPad *srcpad, *sinkpad, *ghostpad, *dummy;
  GstPadTemplate *srctempl, *sinktempl;
  GstPadLinkReturn ret;
  gboolean res;

  padcaps = gst_caps_from_string ("some/caps");
  fail_unless (padcaps != NULL);
  srctempl = gst_pad_template_new ("srctempl", GST_PAD_SRC,
      GST_PAD_ALWAYS, padcaps);

  padcaps = gst_caps_from_string ("some/caps");
  fail_unless (padcaps != NULL);
  sinktempl = gst_pad_template_new ("sinktempl", GST_PAD_SINK,
      GST_PAD_ALWAYS, padcaps);

  srcpad = gst_pad_new_from_template (srctempl, "src");
  fail_unless (srcpad != NULL);
  sinkpad = gst_pad_new_from_template (sinktempl, "sink");
  fail_unless (sinkpad != NULL);

  /* set up link/unlink functions for the pad */
  linked_count1 = unlinked_count1 = 0;
  gst_pad_set_link_function (srcpad, pad_linked1);
  gst_pad_set_unlink_function (srcpad, pad_unlinked1);
  linked_count2 = unlinked_count2 = 0;
  gst_pad_set_link_function (sinkpad, pad_linked2);
  gst_pad_set_unlink_function (sinkpad, pad_unlinked2);

  /* this should trigger a link from the internal pad to the srcpad */
  ghostpad = gst_ghost_pad_new ("ghostpad", srcpad);
  fail_unless (ghostpad != NULL);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 0);
  fail_unless (unlinked_count2 == 0);

  /* this should fail with a critial */
  ASSERT_CRITICAL (dummy = gst_ghost_pad_new ("ghostpad", srcpad));
  fail_unless (dummy == NULL);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 0);
  fail_unless (unlinked_count2 == 0);

  /* this should not trigger anything because we are not directly
   * linking/unlinking the src pad. */
  ret = gst_pad_link (ghostpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_OK);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 0);

  /* this link should fail because we are already linked. Let's make sure the
   * link functions are not called */
  ret = gst_pad_link (ghostpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_WAS_LINKED);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 0);

  res = gst_pad_unlink (ghostpad, sinkpad);
  fail_unless (res == TRUE);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 1);

  res = gst_pad_unlink (ghostpad, sinkpad);
  fail_unless (res == FALSE);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 0);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 1);

  /* this should trigger the unlink function */
  res = gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ghostpad), NULL);
  fail_unless (res == TRUE);
  fail_unless (linked_count1 == 1);
  fail_unless (unlinked_count1 == 1);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 1);

  /* and this the link function again */
  res = gst_ghost_pad_set_target (GST_GHOST_PAD_CAST (ghostpad), srcpad);
  fail_unless (res == TRUE);
  fail_unless (linked_count1 == 2);
  fail_unless (unlinked_count1 == 1);
  fail_unless (linked_count2 == 1);
  fail_unless (unlinked_count2 == 1);

  gst_object_unref (ghostpad);
  gst_object_unref (sinkpad);
  gst_object_unref (srcpad);
  gst_object_unref (srctempl);
  gst_object_unref (sinktempl);
}

GST_END_TEST;

GST_START_TEST (test_ghost_pads_change_when_linked)
{
  GstElement *b1, *b2, *src, *fmt, *sink1, *sink2;
  GstPad *sinkpad, *ghostpad;
  GstCaps *caps;

  b1 = gst_element_factory_make ("pipeline", NULL);
  b2 = gst_element_factory_make ("bin", NULL);
  src = gst_element_factory_make ("fakesrc", NULL);
  fmt = gst_element_factory_make ("capsfilter", NULL);
  sink1 = gst_element_factory_make ("fakesink", NULL);
  sink2 = gst_element_factory_make ("fakesink", NULL);

  gst_bin_add (GST_BIN (b2), sink1);
  gst_bin_add (GST_BIN (b2), sink2);
  gst_bin_add (GST_BIN (b1), src);
  gst_bin_add (GST_BIN (b1), fmt);
  gst_bin_add (GST_BIN (b1), b2);

  caps = gst_caps_from_string ("audio/x-raw-int, width=16, channels=1");
  g_object_set (fmt, "caps", caps, NULL);
  gst_caps_unref (caps);

  /* create the ghostpad as a sink-pad for bin 2 */
  ghostpad = gst_ghost_pad_new_no_target ("sink", GST_PAD_SINK);
  gst_element_add_pad (b2, ghostpad);

  sinkpad = gst_element_get_static_pad (sink1, "sink");
  fail_unless (gst_ghost_pad_set_target ((GstGhostPad *) ghostpad, sinkpad));
  gst_object_unref (sinkpad);

  fail_unless (gst_element_link_many (src, fmt, b2, NULL));

  /* set different target after ghostpad is linked */
  sinkpad = gst_element_get_static_pad (sink2, "sink");
  fail_unless (gst_ghost_pad_set_target ((GstGhostPad *) ghostpad, sinkpad));
  gst_object_unref (sinkpad);

  /* clean up */
  gst_object_unref (b1);
}

GST_END_TEST;


static Suite *
gst_ghost_pad_suite (void)
{
  Suite *s = suite_create ("GstGhostPad");

  TCase *tc_chain = tcase_create ("ghost pad tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_remove1);
  tcase_add_test (tc_chain, test_remove2);
  tcase_add_test (tc_chain, test_remove_target);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_ghost_pads);
  tcase_add_test (tc_chain, test_ghost_pads_bin);
  tcase_add_test (tc_chain, test_ghost_pads_notarget);
  tcase_add_test (tc_chain, test_ghost_pads_block);
  tcase_add_test (tc_chain, test_ghost_pads_probes);
  tcase_add_test (tc_chain, test_ghost_pads_new_from_template);
  tcase_add_test (tc_chain, test_ghost_pads_new_no_target_from_template);
  tcase_add_test (tc_chain, test_ghost_pads_forward_setcaps);
  tcase_add_test (tc_chain, test_ghost_pads_sink_link_unlink);
  tcase_add_test (tc_chain, test_ghost_pads_src_link_unlink);
  tcase_add_test (tc_chain, test_ghost_pads_change_when_linked);

  return s;
}

GST_CHECK_MAIN (gst_ghost_pad);
