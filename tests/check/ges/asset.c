/* GStreamer Editing Services
 *
 * Copyright (C) 2012 Volodymyr Rudyi <vladimir.rudoy@gmail.com>
 * Copyright (C) 2015 Thibault Saunier <tsaunier@gnome.org>
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

#include "test-utils.h"
#include "../../../ges/ges-internal.h"
#include <ges/ges.h>
#include <gst/check/gstcheck.h>

static GMainLoop *mainloop;

static void
source_asset_created (GObject * source, GAsyncResult * res,
    gpointer expected_ok)
{
  GError *error = NULL;

  GESAsset *a = ges_asset_request_finish (res, &error);

  if (GPOINTER_TO_INT (expected_ok)) {
    fail_unless (a != NULL);
    fail_unless (error == NULL);
  } else {
    fail_unless (a == NULL);
    assert_equals_int (error->domain, GST_RESOURCE_ERROR);
  }

  g_clear_error (&error);
  g_main_loop_quit (mainloop);
}

GST_START_TEST (test_basic)
{
  ges_init ();

  mainloop = g_main_loop_new (NULL, FALSE);
  ges_asset_request_async (GES_TYPE_URI_CLIP,
      "file:///this/is/not/for/real", NULL, source_asset_created,
      GINT_TO_POINTER (FALSE));

  g_main_loop_run (mainloop);
  g_main_loop_unref (mainloop);
  ges_deinit ();
}

GST_END_TEST;

typedef struct _CustomContextData
{
  GMutex lock;
  GCond cond;
  gboolean finish;
  gboolean expected_ok;
  gchar *uri;
} CustomContextData;

static gpointer
custom_context_thread_func (CustomContextData * data)
{
  GMainContext *context;

  context = g_main_context_new ();
  mainloop = g_main_loop_new (context, FALSE);

  g_main_context_push_thread_default (context);

  /* To use custom context, we need to call ges_init() in the thread */
  fail_unless (ges_init ());

  ges_asset_request_async (GES_TYPE_URI_CLIP,
      data->uri, NULL, source_asset_created,
      GINT_TO_POINTER (data->expected_ok));
  g_main_loop_run (mainloop);

  g_main_context_pop_thread_default (context);
  ges_deinit ();
  g_main_context_unref (context);
  g_main_loop_unref (mainloop);

  data->finish = TRUE;
  g_cond_signal (&data->cond);

  return NULL;
}

GST_START_TEST (test_custom_context)
{
  GThread *thread;
  CustomContextData data;

  mainloop = NULL;

  g_mutex_init (&data.lock);
  g_cond_init (&data.cond);
  /* ensure default context here, but we will not use the default context */
  g_main_context_default ();

  /* first run with invalid uri */
  data.finish = FALSE;
  data.expected_ok = FALSE;
  data.uri = g_strdup ("file:///this/is/not/for/real");

  thread = g_thread_new ("test-custom-context-thread",
      (GThreadFunc) custom_context_thread_func, &data);

  g_mutex_lock (&data.lock);
  while (data.finish)
    g_cond_wait (&data.cond, &data.lock);
  g_mutex_unlock (&data.lock);

  g_thread_join (thread);
  g_free (data.uri);

  /* second run with valid uri */
  data.finish = FALSE;
  data.expected_ok = TRUE;
  data.uri = ges_test_file_uri ("audio_video.ogg");

  thread = g_thread_new ("test-custom-context-thread",
      (GThreadFunc) custom_context_thread_func, &data);

  g_mutex_lock (&data.lock);
  while (data.finish)
    g_cond_wait (&data.cond, &data.lock);
  g_mutex_unlock (&data.lock);

  g_thread_join (thread);
  g_free (data.uri);

  g_mutex_clear (&data.lock);
  g_cond_clear (&data.cond);
}

GST_END_TEST;

GST_START_TEST (test_transition_change_asset)
{
  gchar *id;
  GESAsset *a;
  GESExtractable *extractable;

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
  gst_object_unref (a);

  fail_unless_equals_int (GES_TRANSITION_CLIP (extractable)->vtype, 26);

  gst_object_unref (extractable);
  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_uri_clip_change_asset)
{
  GESAsset *asset, *asset1;
  GESExtractable *extractable;
  GESLayer *layer;
  gchar *uri;
  gchar *uri1;
  GESTimeline *timeline;

  ges_init ();

  layer = ges_layer_new ();
  uri = ges_test_file_uri ("audio_video.ogg");
  uri1 = ges_test_file_uri ("audio_only.ogg");
  timeline = ges_timeline_new_audio_video ();

  ges_timeline_add_layer (timeline, layer);

  asset = GES_ASSET (ges_uri_clip_asset_request_sync (uri, NULL));

  fail_unless (GES_IS_ASSET (asset));
  fail_unless_equals_string (ges_asset_get_id (asset), uri);

  extractable = GES_EXTRACTABLE (ges_layer_add_asset (layer,
          asset, 0, 0, GST_CLOCK_TIME_NONE, GES_TRACK_TYPE_UNKNOWN));
  fail_unless (ges_extractable_get_asset (extractable) == asset);
  gst_object_unref (asset);

  /* Now try to set the a and see if the vtype is properly updated */
  asset1 = GES_ASSET (ges_uri_clip_asset_request_sync (uri1, NULL));
  fail_unless_equals_int (g_list_length (GES_CONTAINER_CHILDREN (extractable)),
      2);
  fail_unless (ges_extractable_set_asset (extractable, asset1));
  fail_unless_equals_int (g_list_length (GES_CONTAINER_CHILDREN (extractable)),
      1);

  gst_object_unref (extractable);

  g_free (uri);
  g_free (uri1);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_list_asset)
{
  GList *assets;
  GEnumClass *enum_class;

  ges_init ();

  enum_class = g_type_class_peek (GES_VIDEO_STANDARD_TRANSITION_TYPE_TYPE);

  fail_unless (ges_init ());
  fail_if (ges_list_assets (GES_TYPE_OVERLAY_CLIP));

  assets = ges_list_assets (GES_TYPE_TRANSITION_CLIP);
  /* note: we do not have a a for value=0 "Transition not set" */
  assert_equals_int (g_list_length (assets), enum_class->n_values - 1);
  g_list_free (assets);

  ges_deinit ();
}

GST_END_TEST;

GST_START_TEST (test_proxy_asset)
{
  GESAsset *identity, *nothing, *nothing_at_all;

  fail_unless (ges_init ());

  identity = ges_asset_request (GES_TYPE_EFFECT, "video identity", NULL);
  fail_unless (identity != NULL);

  nothing = ges_asset_request (GES_TYPE_EFFECT, "nothing", NULL);
  fail_if (nothing);

  nothing = ges_asset_cache_lookup (GES_TYPE_EFFECT, "nothing");
  fail_unless (nothing != NULL);

  fail_unless (ges_asset_try_proxy (nothing, "video identity"));
  fail_unless (ges_asset_set_proxy (NULL, identity));

  nothing_at_all = ges_asset_request (GES_TYPE_EFFECT, "nothing_at_all", NULL);
  fail_if (nothing_at_all);

  nothing_at_all = ges_asset_cache_lookup (GES_TYPE_EFFECT, "nothing_at_all");
  fail_unless (nothing_at_all != NULL);

  /* Now we proxy nothing_at_all to nothing which is itself proxied to identity */
  fail_unless (ges_asset_try_proxy (nothing_at_all, "nothing"));
  fail_unless (ges_asset_set_proxy (NULL, nothing));
  fail_unless_equals_int (g_list_length (ges_asset_list_proxies
          (nothing_at_all)), 1);

  fail_unless_equals_pointer (ges_asset_get_proxy_target (nothing),
      nothing_at_all);

  /* If we request nothing_at_all we should get the good proxied identity */
  nothing_at_all = ges_asset_request (GES_TYPE_EFFECT, "nothing_at_all", NULL);
  fail_unless (nothing_at_all == identity);

  gst_object_unref (identity);
  gst_object_unref (nothing_at_all);

  ges_deinit ();
}

GST_END_TEST;

static Suite *
ges_suite (void)
{
  Suite *s = suite_create ("ges");
  TCase *tc_chain = tcase_create ("a");

  suite_add_tcase (s, tc_chain);

  tcase_add_test (tc_chain, test_basic);
  tcase_add_test (tc_chain, test_custom_context);
  tcase_add_test (tc_chain, test_transition_change_asset);
  tcase_add_test (tc_chain, test_uri_clip_change_asset);
  tcase_add_test (tc_chain, test_list_asset);
  tcase_add_test (tc_chain, test_proxy_asset);

  return s;
}

GST_CHECK_MAIN (ges);
