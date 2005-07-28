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

#include "../gstcheck.h"

static void
assert_gstrefcount (gpointer p, gint i)
{
  if (GST_OBJECT_REFCOUNT_VALUE (p) != i)
    g_critical ("Expected refcount %d for %s, got %d", i, GST_OBJECT_NAME (p),
        GST_OBJECT_REFCOUNT_VALUE (p));
}

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

  fail_unless (gst_bin_add (GST_BIN (b2), sink));
  fail_unless (gst_bin_add (GST_BIN (b1), src));
  fail_unless (gst_bin_add (GST_BIN (b1), b2));

  sinkpad = gst_element_get_pad (sink, "sink");
  gst_element_add_pad (b2, gst_ghost_pad_new ("sink", sinkpad));
  gst_object_unref (sinkpad);

  srcpad = gst_element_get_pad (src, "src");
  /* get the ghostpad */
  sinkpad = gst_element_get_pad (b2, "sink");

  ret = gst_pad_link (srcpad, sinkpad);
  fail_unless (ret == GST_PAD_LINK_OK);
  gst_object_unref (srcpad);
  gst_object_unref (sinkpad);

  /* now remove the bin with the ghostpad, b2 is disposed
   * now. */
  gst_bin_remove (GST_BIN (b1), b2);

  srcpad = gst_element_get_pad (src, "src");
  /* pad cannot be linked now */
  fail_if (gst_pad_is_linked (srcpad));
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

  srcpad = gst_element_get_pad (src, "src");
  fail_unless (srcpad != NULL);
  sinkpad = gst_element_get_pad (sink, "sink");
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
  GST_LOCK (b2);
  fail_unless (b2->numsinkpads == 1);
  fail_unless (GST_IS_GHOST_PAD (b2->sinkpads->data));
  fail_unless (b2->numsrcpads == 1);
  fail_unless (GST_IS_GHOST_PAD (b2->srcpads->data));
  GST_UNLOCK (b2);

  fsrc = gst_element_get_pad (src, "src");
  fail_unless (fsrc != NULL);
  gsink = GST_PAD (gst_object_ref (b2->sinkpads->data));
  fail_unless (gsink != NULL);
  gsrc = GST_PAD (gst_object_ref (b2->srcpads->data));
  fail_unless (gsrc != NULL);
  fsink = gst_element_get_pad (sink, "sink");
  fail_unless (fsink != NULL);

  isink = gst_element_get_pad (i1, "sink");
  fail_unless (isink != NULL);
  isrc = gst_element_get_pad (i1, "src");
  fail_unless (isrc != NULL);
  gisrc = gst_pad_get_peer (isink);
  fail_unless (gisrc != NULL);
  gisink = gst_pad_get_peer (isrc);
  fail_unless (gisink != NULL);

  /* all objects above have one refcount owned by us as well */

  assert_gstrefcount (fsrc, 3); /* parent and gisrc */
  assert_gstrefcount (gsink, 2);        /* parent */
  assert_gstrefcount (gsrc, 2); /* parent */
  assert_gstrefcount (fsink, 3);        /* parent and gisink */

  assert_gstrefcount (gisrc, 2);        /* parent */
  assert_gstrefcount (isink, 3);        /* parent and gsink */
  assert_gstrefcount (gisink, 2);       /* parent */
  assert_gstrefcount (isrc, 3); /* parent and gsrc */

  fail_unless (gst_element_set_state (b1,
          GST_STATE_PLAYING) == GST_STATE_SUCCESS);

  fail_unless (gst_element_set_state (b1, GST_STATE_NULL) == GST_STATE_SUCCESS);

  gst_object_unref (b1);
  /* unreffing the bin will unref all elements, which will unlink and unparent
   * all pads */

  /* FIXME: ghost pads need to drop their internal pad in the unlink function,
   * but can't right now. So internal pads have a ref from their parent, and the
   * internal pads' targets have refs from the internals. When we do the last
   * unref on the ghost pads, these refs should go away.
   */

  assert_gstrefcount (fsrc, 2); /* gisrc */
  assert_gstrefcount (gsink, 1);
  assert_gstrefcount (gsrc, 1);
  assert_gstrefcount (fsink, 2);        /* gisink */

  assert_gstrefcount (gisrc, 2);        /* gsink -- fixme drop ref in unlink */
  assert_gstrefcount (isink, 2);        /* gsink */
  assert_gstrefcount (gisink, 2);       /* gsrc -- fixme drop ref in unlink */
  assert_gstrefcount (isrc, 2); /* gsrc */

  /* while the fixme isn't fixed, check cleanup */
  gst_object_unref (gsink);
  assert_gstrefcount (isink, 1);
  assert_gstrefcount (gisrc, 1);
  assert_gstrefcount (fsrc, 2); /* gisrc */
  gst_object_unref (gisrc);
  assert_gstrefcount (fsrc, 1);

  gst_object_unref (gsrc);
  assert_gstrefcount (isrc, 1);
  assert_gstrefcount (gisink, 1);
  assert_gstrefcount (fsink, 2);        /* gisrc */
  gst_object_unref (gisink);
  assert_gstrefcount (fsink, 1);
}

GST_END_TEST;

Suite *
gst_ghost_pad_suite (void)
{
  Suite *s = suite_create ("GstGhostPad");
  TCase *tc_chain = tcase_create ("ghost pad tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_remove1);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_ghost_pads);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = gst_ghost_pad_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
