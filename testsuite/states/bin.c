/* GStreamer
 * Copyright (C) <2004> Benjamin Otte <otte@gnome.org>
 *
 * bin.c:
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

#define RETURN_NAME(x) ((x) == GST_STATE_CHANGE_SUCCESS ? "GST_STATE_CHANGE_SUCCESS" : \
    (x) == GST_STATE_CHANGE_ASYNC ? "GST_STATE_CHANGE_ASYNC" : "GST_STATE_CHANGE_FAILURE")
static void
assert_state (GstElement * element, GstState state)
{
  GstState current, pending;

  gst_element_get_state (element, &current, &pending, NULL);
  if (current != state) {
    g_printerr ("%s: state is %s instead of %s",
        GST_OBJECT_NAME (element),
        gst_element_state_get_name (GST_STATE (element)),
        gst_element_state_get_name (state));
    g_assert_not_reached ();
  }
}

static void
assert_state_change (GstElement * element, GstState new_state,
    GstStateChangeReturn result, GstState result_state)
{
  GstStateChangeReturn ret = gst_element_set_state (element, new_state);

  if (ret != result) {
    g_printerr ("%s: change state to %s returned %s instead of %s",
        GST_OBJECT_NAME (element), gst_element_state_get_name (new_state),
        RETURN_NAME (ret), RETURN_NAME (result));
    g_assert_not_reached ();
  }
  assert_state (element, result_state);
}

static void
empty_bin (gchar * bin_name)
{
  /* Test the behaviour of empty bins. Since a bin's state is always the state
   * of its highest child, nothing should change in here
   * Return values when no error occured but the state didn't change should be
   * GST_STATE_CHANGE_ASYNC */
  GstElement *bin = gst_element_factory_make (bin_name, NULL);

  g_assert (bin);
  /* obvious */
  assert_state (bin, GST_STATE_NULL);
  /* see above */
  assert_state_change (bin, GST_STATE_READY, GST_STATE_CHANGE_SUCCESS,
      GST_STATE_READY);
  assert_state_change (bin, GST_STATE_PAUSED, GST_STATE_CHANGE_SUCCESS,
      GST_STATE_PAUSED);
  assert_state_change (bin, GST_STATE_PLAYING, GST_STATE_CHANGE_SUCCESS,
      GST_STATE_PLAYING);
}

static void
test_adding_one_element (GstElement * bin)
{
  /* Tests behaviour of adding/removing elements to/from bins. It makes sure the
   * state of the bin is always the highest of all contained children. */
  GstState test_states[] = { GST_STATE_READY, GST_STATE_PAUSED,
    GST_STATE_PLAYING, GST_STATE_PAUSED, GST_STATE_READY, GST_STATE_NULL
  };
  GstElement *test = gst_element_factory_make ("identity", NULL);
  GstState bin_state;
  gint i;

  gst_element_get_state (bin, &bin_state, NULL, NULL);
  g_assert (test);
  gst_object_ref (test);
  assert_state (test, GST_STATE_NULL);
  gst_bin_add (GST_BIN (bin), test);
  assert_state (bin, MAX (bin_state, GST_STATE_NULL));
  for (i = 0; i < G_N_ELEMENTS (test_states); i++) {
    GstState test_state = test_states[i];

    assert_state_change (test, test_state, GST_STATE_CHANGE_SUCCESS,
        test_state);
    assert_state (test, test_state);
    assert_state (bin, MAX (bin_state, test_state));
    gst_bin_remove (GST_BIN (bin), test);
    assert_state (bin, bin_state);
    gst_bin_add (GST_BIN (bin), test);
    assert_state (test, test_state);
    assert_state (bin, MAX (bin_state, test_state));
  }
  gst_bin_remove (GST_BIN (bin), test);
  gst_object_unref (test);
  assert_state (bin, bin_state);
}

static void
test_element_in_bin (gchar * bin_name)
{
  gint i;
  GstState test_states[] = { GST_STATE_NULL, GST_STATE_READY,
    GST_STATE_PAUSED, GST_STATE_PLAYING
  };
  GstElement *id, *bin = gst_element_factory_make (bin_name, NULL);

  g_assert (bin);

  /* test correct behaviour in empty bins */
  test_adding_one_element (bin);

  id = gst_element_factory_make ("identity", NULL);
  g_assert (id);
  assert_state (id, GST_STATE_NULL);
  gst_bin_add (GST_BIN (bin), id);
  /* test correct behaviour in bins which contain elements in various states */
  for (i = 0; i < G_N_ELEMENTS (test_states); i++) {
    GstState test_state = test_states[i];

    assert_state_change (bin, test_state, GST_STATE_CHANGE_SUCCESS, test_state);
    assert_state (id, test_state);
    test_adding_one_element (bin);
  }

  gst_object_unref (bin);
}

gint
main (gint argc, gchar * argv[])
{
  gst_init (&argc, &argv);

  /* test behaviour of empty bins */
  empty_bin ("bin");
  empty_bin ("pipeline");

  g_print ("how far\n");
  /* test behaviour of adding/removing elements to/from all core bin types */
  test_element_in_bin ("bin");
  test_element_in_bin ("pipeline");

  return 0;
}
