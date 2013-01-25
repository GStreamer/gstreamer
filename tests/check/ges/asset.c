/* GStreamer Editing Services
 *
 * Copyright (C) 2012 Volodymyr Rudyi <vladimir.rudoy@gmail.com>
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

#include "../../../ges/ges-internal.h"
#undef GST_CAT_DEFAULT
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

static GMainLoop *mainloop;

static void
source_asset_created (GObject * source, GAsyncResult * res, gpointer udata)
{
  GError *error = NULL;

  GESAsset *a = ges_asset_request_finish (res, &error);

  fail_unless (a == NULL);
  assert_equals_int (error->domain, GST_RESOURCE_ERROR);

  g_main_loop_quit (mainloop);
}

GST_START_TEST (test_basic)
{
  gst_init (NULL, NULL);
  ges_init ();

  mainloop = g_main_loop_new (NULL, FALSE);
  ges_asset_request_async (GES_TYPE_URI_CLIP,
      "file:///this/is/not/for/real", NULL, source_asset_created, NULL);

  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);
}

GST_END_TEST;

GST_START_TEST (test_change_asset)
{
  gchar *id;
  GESAsset *a;
  GESExtractable *extractable;

  gst_init (NULL, NULL);
  ges_init ();

  a = ges_asset_request (GES_TYPE_TRANSITION_CLIP, "box-wipe-lc", NULL);

  fail_unless (GES_IS_ASSET (a));
  fail_unless_equals_string (ges_asset_get_id (a), "box-wipe-lc");

  extractable = ges_asset_extract (a, NULL);
  fail_unless (ges_extractable_get_asset (extractable) == a);

  id = ges_extractable_get_id (extractable);
  fail_unless_equals_string (id, "box-wipe-lc");
  g_free (id);

  g_object_set (extractable, "vtype", 2, NULL);

  id = ges_extractable_get_id (extractable);
  fail_unless_equals_string (id, "bar-wipe-tb");
  g_free (id);

  fail_if (ges_extractable_get_asset (extractable) == a);
  gst_object_unref (a);

  a = ges_extractable_get_asset (extractable);
  fail_unless_equals_string (ges_asset_get_id (a), "bar-wipe-tb");

  /* Now try to set the a and see if the vtype is properly updated */
  a = ges_asset_request (GES_TYPE_TRANSITION_CLIP, "box-wipe-lc", NULL);
  ges_extractable_set_asset (extractable, a);
  fail_unless_equals_int (GES_TRANSITION_CLIP (extractable)->vtype, 26);

  gst_object_unref (extractable);
}

GST_END_TEST;

GST_START_TEST (test_list_asset)
{
  GList *assets;
  GEnumClass *enum_class;
  enum_class = g_type_class_peek (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);

  fail_unless (ges_init ());
  fail_if (ges_list_assets (GES_TYPE_OVERLAY_CLIP));

  assets = ges_list_assets (GES_TYPE_TRANSITION_CLIP);
  /* note: we do not have a a for value=0 "Transition not set" */
  assert_equals_int (g_list_length (assets), enum_class->n_values - 1);
  g_list_free (assets);
}

GST_END_TEST;

GST_START_TEST (test_proxy_asset)
{
  GESAsset *identity, *nothing, *nothing_at_all;

  fail_unless (ges_init ());

  identity = ges_asset_request (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT, "identity",
      NULL);
  fail_unless (identity != NULL);

  nothing = ges_asset_request (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT, "nothing",
      NULL);
  fail_if (nothing);

  nothing = ges_asset_cache_lookup (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT,
      "nothing");
  fail_unless (nothing != NULL);

  fail_unless (ges_asset_set_proxy (nothing, "identity"));

  nothing_at_all = ges_asset_request (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT,
      "nothing_at_all", NULL);
  fail_if (nothing_at_all);

  nothing_at_all = ges_asset_cache_lookup (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT,
      "nothing_at_all");
  fail_unless (nothing_at_all != NULL);

  /* Now we proxy nothing_at_all to nothing which is itself proxied to identity */
  fail_unless (ges_asset_set_proxy (nothing_at_all, "nothing"));

  /* If we request nothing_at_all we should get the good proxied identity */
  nothing_at_all = ges_asset_request (GES_TYPE_TRACK_PARSE_LAUNCH_EFFECT,
      "nothing_at_all", NULL);
  fail_unless (nothing_at_all == identity);

  gst_object_unref (identity);
  gst_object_unref (nothing_at_all);
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("a");

  suite_add_tcase (s, tc_chain);

  /* Must be first until we implement deinit */
  tcase_add_test (tc_chain, test_list_asset);
  tcase_add_test (tc_chain, test_basic);
  tcase_add_test (tc_chain, test_change_asset);
  tcase_add_test (tc_chain, test_proxy_asset);

  return s;
}

int
main (int argc, char **argv)
{
  int nf;

  Suite *s = ges_suite ();
  SRunner *sr = srunner_create (s);

  gst_check_init (&argc, &argv);
  ges_init ();

  srunner_run_all (sr, CK_NORMAL);
  nf = srunner_ntests_failed (sr);
  srunner_free (sr);

  return nf;
}
