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
/* #include "../../../ges/ges-internal.h" */
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
    g_object_unref (a);
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

  gst_object_unref (asset1);
  gst_object_unref (asset);

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

/*
 * NOTE: this test is commented out because it requires the internal
 * ges_asset_finish_proxy method. This method replaces the behaviour of
 * ges_asset_set_proxy (NULL, proxy), which is no longer supported.
 *
 * ges_asset_cache_lookup and ges_asset_try_proxy are similarly internal,
 * but they are marked with GES_API in ges-internal.h
 * The newer ges_asset_finish_proxy is not marked as GES_API, because it
 * would add it to the symbols list, and could therefore not be easily
 * removed.
 *
 * Once we have a nice way to call internal methods for tests, we should
 * uncomment this.
 *
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
  fail_unless (ges_asset_finish_proxy (identity));

  nothing_at_all = ges_asset_request (GES_TYPE_EFFECT, "nothing_at_all", NULL);
  fail_if (nothing_at_all);

  nothing_at_all = ges_asset_cache_lookup (GES_TYPE_EFFECT, "nothing_at_all");
  fail_unless (nothing_at_all != NULL);

  // Now we proxy nothing_at_all to nothing which is itself proxied to identity
  fail_unless (ges_asset_try_proxy (nothing_at_all, "nothing"));
  fail_unless (ges_asset_finish_proxy (nothing));
  fail_unless_equals_int (g_list_length (ges_asset_list_proxies
          (nothing_at_all)), 1);

  fail_unless_equals_pointer (ges_asset_get_proxy_target (nothing),
      nothing_at_all);

  // If we request nothing_at_all we should get the good proxied identity
  nothing_at_all = ges_asset_request (GES_TYPE_EFFECT, "nothing_at_all", NULL);
  fail_unless (nothing_at_all == identity);

  gst_object_unref (identity);
  gst_object_unref (nothing_at_all);

  ges_deinit ();
}

GST_END_TEST;
*/

static void
_count_cb (GObject * obj, GParamSpec * pspec, gpointer key)
{
  guint count = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (obj), key));
  g_object_set_data (G_OBJECT (obj), key, GUINT_TO_POINTER (count + 1));
}

#define _CONNECT_PROXY_SIGNALS(asset) \
  g_signal_connect (asset, "notify::proxy", G_CALLBACK (_count_cb), \
      (gchar *)"test-data-proxy-count"); \
  g_signal_connect (asset, "notify::proxy-target", G_CALLBACK (_count_cb), \
      (gchar *)"test-data-target-count");

/* test that @asset has the properties proxy = @proxy and
 * proxy-target = @proxy_target
 * Also check that the callback for "notify::proxy" (set up in
 * _CONNECT_PROXY_SIGNALS) has been called @p_count times, and the
 * callback for "notify::target-proxy" has been called @t_count times.
 */
#define _assert_proxy_state(asset, proxy, proxy_target, p_count, t_count) \
{ \
  const gchar *id = ges_asset_get_id (asset); \
  guint found_p_count = GPOINTER_TO_UINT (g_object_get_data ( \
        G_OBJECT (asset), "test-data-proxy-count")); \
  guint found_t_count = GPOINTER_TO_UINT (g_object_get_data ( \
        G_OBJECT (asset), "test-data-target-count")); \
  GESAsset *found_proxy = ges_asset_get_proxy (asset); \
  GESAsset *found_target = ges_asset_get_proxy_target (asset); \
  fail_unless (found_proxy == proxy, "Asset '%s' has the proxy '%s' " \
      "rather than the expected '%s'", id, \
      found_proxy ? ges_asset_get_id (found_proxy) : NULL, \
      proxy ? ges_asset_get_id (proxy) : NULL); \
  fail_unless (found_target == proxy_target, "Asset '%s' has the proxy " \
      "target '%s' rather than the expected '%s'", id, \
      found_target ? ges_asset_get_id (found_target) : NULL, \
      proxy_target ? ges_asset_get_id (proxy_target) : NULL); \
  fail_unless (p_count == found_p_count, "notify::proxy for asset '%s' " \
      "was called %u times, rather than the expected %u times", \
      id, found_p_count, p_count); \
  fail_unless (t_count == found_t_count, "notify::target-proxy for " \
      "asset '%s' was called %u times, rather than the expected %u times", \
      id, found_t_count, t_count); \
}

#define _assert_proxy_list(asset, cmp_list) \
{ \
  const gchar * id = ges_asset_get_id (asset); \
  int i; \
  GList *tmp; \
  for (i = 0, tmp = ges_asset_list_proxies (asset); cmp_list[i] && tmp; \
      i++, tmp = tmp->next) { \
    GESAsset *proxy = tmp->data; \
    fail_unless (proxy == cmp_list[i], "The asset '%s' has '%s' as its " \
        "%ith proxy, rather than the expected '%s'", id, \
        ges_asset_get_id (proxy), i, ges_asset_get_id (cmp_list[i])); \
  } \
  fail_unless (tmp == NULL, "Found more proxies for '%s' than expected", \
      id); \
  fail_unless (cmp_list[i] == NULL, "Found less proxies (%i) for '%s' " \
      "than expected", i, id); \
}

#define _assert_effect_asset_request(req_id, expect) \
{ \
  GESAsset *requested = ges_asset_request (GES_TYPE_EFFECT, req_id, NULL); \
  fail_unless (requested == expect, "Requested asset for id '%s' is " \
      "'%s' rather than the expected '%s'", req_id, \
      requested ? ges_asset_get_id (requested) : NULL, \
      ges_asset_get_id (expect)); \
  gst_object_unref (requested); \
}

GST_START_TEST (test_proxy_setters)
{
  GESAsset *proxies[] = { NULL, NULL, NULL, NULL };
  GESAsset *asset, *alt_asset;
  GESAsset *proxy0, *proxy1, *proxy2;
  gchar asset_id[] = "video agingtv ! videobalance";
  gchar alt_asset_id[] = "video gamma";
  gchar proxy0_id[] = "video videobalance contrast=0.0";
  gchar proxy1_id[] = "video videobalance contrast=1.0";
  gchar proxy2_id[] = "video videobalance contrast=2.0";

  ges_init ();

  asset = ges_asset_request (GES_TYPE_EFFECT, asset_id, NULL);
  alt_asset = ges_asset_request (GES_TYPE_EFFECT, alt_asset_id, NULL);

  proxy0 = ges_asset_request (GES_TYPE_EFFECT, proxy0_id, NULL);
  proxy1 = ges_asset_request (GES_TYPE_EFFECT, proxy1_id, NULL);
  proxy2 = ges_asset_request (GES_TYPE_EFFECT, proxy2_id, NULL);

  /* make sure our assets are unique */
  fail_unless (asset);
  fail_unless (alt_asset);
  fail_unless (proxy0);
  fail_unless (proxy1);
  fail_unless (proxy2);
  fail_unless (asset != alt_asset);
  fail_unless (asset != proxy0);
  fail_unless (asset != proxy1);
  fail_unless (asset != proxy2);
  fail_unless (alt_asset != proxy0);
  fail_unless (alt_asset != proxy1);
  fail_unless (alt_asset != proxy2);
  fail_unless (proxy0 != proxy1);
  fail_unless (proxy0 != proxy1);
  fail_unless (proxy0 != proxy2);
  fail_unless (proxy1 != proxy2);

  _CONNECT_PROXY_SIGNALS (asset);
  _CONNECT_PROXY_SIGNALS (alt_asset);
  _CONNECT_PROXY_SIGNALS (proxy0);
  _CONNECT_PROXY_SIGNALS (proxy1);
  _CONNECT_PROXY_SIGNALS (proxy2);

  /* no proxies to start with */
  _assert_proxy_state (asset, NULL, NULL, 0, 0);
  _assert_proxy_state (alt_asset, NULL, NULL, 0, 0);
  _assert_proxy_state (proxy0, NULL, NULL, 0, 0);
  _assert_proxy_state (proxy1, NULL, NULL, 0, 0);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 0);
  _assert_proxy_list (asset, proxies);
  _assert_proxy_list (alt_asset, proxies);
  _assert_proxy_list (proxy0, proxies);
  _assert_proxy_list (proxy1, proxies);
  _assert_proxy_list (proxy2, proxies);

  /* id for an asset with no proxy returns itself */
  _assert_effect_asset_request (asset_id, asset);
  _assert_effect_asset_request (alt_asset_id, alt_asset);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* set a proxy */
  fail_unless (ges_asset_set_proxy (asset, proxy0));
  _assert_proxy_state (asset, proxy0, NULL, 1, 0);
  _assert_proxy_state (proxy0, NULL, asset, 0, 1);
  _assert_proxy_state (proxy1, NULL, NULL, 0, 0);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 0);

  proxies[0] = proxy0;
  _assert_proxy_list (asset, proxies);

  /* requesting the same asset should return the proxy instead */
  _assert_effect_asset_request (asset_id, proxy0);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* can't proxy a different asset */
  /* Raises ERROR */
  fail_unless (ges_asset_set_proxy (alt_asset, proxy0) == FALSE);
  _assert_proxy_state (alt_asset, NULL, NULL, 0, 0);
  _assert_proxy_state (asset, proxy0, NULL, 1, 0);
  _assert_proxy_state (proxy0, NULL, asset, 0, 1);
  _assert_proxy_state (proxy1, NULL, NULL, 0, 0);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 0);

  _assert_proxy_list (asset, proxies);
  _assert_effect_asset_request (asset_id, proxy0);
  _assert_effect_asset_request (proxy0_id, proxy0);

  /* set the same proxy again is safe */
  fail_unless (ges_asset_set_proxy (asset, proxy0));
  /* notify::proxy callback count increases, even though we set the same
   * proxy. This is the default behaviour for setters. */
  _assert_proxy_state (asset, proxy0, NULL, 2, 0);
  /* but the notify::target-proxy has not increased for the proxy */
  _assert_proxy_state (proxy0, NULL, asset, 0, 1);
  _assert_proxy_state (proxy1, NULL, NULL, 0, 0);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 0);

  _assert_proxy_list (asset, proxies);
  _assert_effect_asset_request (asset_id, proxy0);
  _assert_effect_asset_request (proxy0_id, proxy0);

  /* replace the proxy with a new one */
  fail_unless (ges_asset_set_proxy (asset, proxy1));
  _assert_proxy_state (asset, proxy1, NULL, 3, 0);
  /* first proxy still keeps its target */
  _assert_proxy_state (proxy0, NULL, asset, 0, 1);
  _assert_proxy_state (proxy1, NULL, asset, 0, 1);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 0);

  proxies[0] = proxy1;
  proxies[1] = proxy0;
  _assert_proxy_list (asset, proxies);

  _assert_effect_asset_request (asset_id, proxy1);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* replace again */
  fail_unless (ges_asset_set_proxy (asset, proxy2));
  _assert_proxy_state (asset, proxy2, NULL, 4, 0);
  _assert_proxy_state (proxy0, NULL, asset, 0, 1);
  _assert_proxy_state (proxy1, NULL, asset, 0, 1);
  _assert_proxy_state (proxy2, NULL, asset, 0, 1);

  proxies[0] = proxy2;
  proxies[1] = proxy1;
  proxies[2] = proxy0;
  _assert_proxy_list (asset, proxies);

  _assert_effect_asset_request (asset_id, proxy2);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* move proxy0 back to being the default */
  fail_unless (ges_asset_set_proxy (asset, proxy0));
  _assert_proxy_state (asset, proxy0, NULL, 5, 0);
  _assert_proxy_state (proxy0, NULL, asset, 0, 1);
  _assert_proxy_state (proxy1, NULL, asset, 0, 1);
  _assert_proxy_state (proxy2, NULL, asset, 0, 1);

  proxies[0] = proxy0;
  proxies[1] = proxy2;
  proxies[2] = proxy1;
  _assert_proxy_list (asset, proxies);

  _assert_effect_asset_request (asset_id, proxy0);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* remove proxy2 */
  fail_unless (ges_asset_unproxy (asset, proxy2));
  /* notify::proxy not released since we have not switched defaults */
  _assert_proxy_state (asset, proxy0, NULL, 5, 0);
  _assert_proxy_state (proxy0, NULL, asset, 0, 1);
  _assert_proxy_state (proxy1, NULL, asset, 0, 1);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 2);

  proxies[0] = proxy0;
  proxies[1] = proxy1;
  proxies[2] = NULL;
  _assert_proxy_list (asset, proxies);

  _assert_effect_asset_request (asset_id, proxy0);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* make proxy2 a proxy for proxy0 */
  fail_unless (ges_asset_set_proxy (proxy0, proxy2));
  _assert_proxy_state (asset, proxy0, NULL, 5, 0);
  _assert_proxy_state (proxy0, proxy2, asset, 1, 1);
  _assert_proxy_state (proxy1, NULL, asset, 0, 1);
  _assert_proxy_state (proxy2, NULL, proxy0, 0, 3);

  proxies[0] = proxy0;
  proxies[1] = proxy1;
  proxies[2] = NULL;
  _assert_proxy_list (asset, proxies);

  proxies[0] = proxy2;
  proxies[1] = NULL;
  _assert_proxy_list (proxy0, proxies);

  /* original id will now follows two proxy links to get proxy2 */
  _assert_effect_asset_request (asset_id, proxy2);
  _assert_effect_asset_request (proxy0_id, proxy2);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* remove proxy0 from asset, should now default to proxy1 */
  fail_unless (ges_asset_unproxy (asset, proxy0));
  /* notify::proxy released since we have switched defaults */
  _assert_proxy_state (asset, proxy1, NULL, 6, 0);
  _assert_proxy_state (proxy0, proxy2, NULL, 1, 2);
  _assert_proxy_state (proxy1, NULL, asset, 0, 1);
  _assert_proxy_state (proxy2, NULL, proxy0, 0, 3);

  proxies[0] = proxy1;
  proxies[1] = NULL;
  _assert_proxy_list (asset, proxies);

  proxies[0] = proxy2;
  proxies[1] = NULL;
  _assert_proxy_list (proxy0, proxies);

  _assert_effect_asset_request (asset_id, proxy1);
  _assert_effect_asset_request (proxy0_id, proxy2);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* remove proxy2 from proxy0 */
  fail_unless (ges_asset_unproxy (proxy0, proxy2));
  _assert_proxy_state (asset, proxy1, NULL, 6, 0);
  _assert_proxy_state (proxy0, NULL, NULL, 2, 2);
  _assert_proxy_state (proxy1, NULL, asset, 0, 1);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 4);

  proxies[0] = proxy1;
  proxies[1] = NULL;
  _assert_proxy_list (asset, proxies);

  proxies[0] = NULL;
  _assert_proxy_list (proxy0, proxies);

  _assert_effect_asset_request (asset_id, proxy1);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* make both proxy0 and proxy2 proxies of proxy1 */
  fail_unless (ges_asset_set_proxy (proxy1, proxy0));
  _assert_proxy_state (asset, proxy1, NULL, 6, 0);
  _assert_proxy_state (proxy0, NULL, proxy1, 2, 3);
  _assert_proxy_state (proxy1, proxy0, asset, 1, 1);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 4);
  fail_unless (ges_asset_set_proxy (proxy1, proxy2));
  _assert_proxy_state (asset, proxy1, NULL, 6, 0);
  _assert_proxy_state (proxy0, NULL, proxy1, 2, 3);
  _assert_proxy_state (proxy1, proxy2, asset, 2, 1);
  _assert_proxy_state (proxy2, NULL, proxy1, 0, 5);

  proxies[0] = proxy1;
  proxies[1] = NULL;
  _assert_proxy_list (asset, proxies);

  proxies[0] = proxy2;
  proxies[1] = proxy0;
  proxies[2] = NULL;
  _assert_proxy_list (proxy1, proxies);

  _assert_effect_asset_request (asset_id, proxy2);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy2);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* should not be able to set up any circular proxies */
  /* Raises ERROR */
  fail_unless (ges_asset_set_proxy (proxy1, asset) == FALSE);
  _assert_proxy_state (asset, proxy1, NULL, 6, 0);
  _assert_proxy_state (proxy0, NULL, proxy1, 2, 3);
  _assert_proxy_state (proxy1, proxy2, asset, 2, 1);
  _assert_proxy_state (proxy2, NULL, proxy1, 0, 5);
  /* Raises ERROR */
  fail_unless (ges_asset_set_proxy (proxy0, asset) == FALSE);
  _assert_proxy_state (asset, proxy1, NULL, 6, 0);
  _assert_proxy_state (proxy0, NULL, proxy1, 2, 3);
  _assert_proxy_state (proxy1, proxy2, asset, 2, 1);
  _assert_proxy_state (proxy2, NULL, proxy1, 0, 5);
  /* Raises ERROR */
  fail_unless (ges_asset_set_proxy (proxy2, asset) == FALSE);
  _assert_proxy_state (asset, proxy1, NULL, 6, 0);
  _assert_proxy_state (proxy0, NULL, proxy1, 2, 3);
  _assert_proxy_state (proxy1, proxy2, asset, 2, 1);
  _assert_proxy_state (proxy2, NULL, proxy1, 0, 5);

  /* remove last proxy from asset, should set its proxy to NULL */
  fail_unless (ges_asset_unproxy (asset, proxy1));
  _assert_proxy_state (asset, NULL, NULL, 7, 0);
  _assert_proxy_state (proxy0, NULL, proxy1, 2, 3);
  _assert_proxy_state (proxy1, proxy2, NULL, 2, 2);
  _assert_proxy_state (proxy2, NULL, proxy1, 0, 5);

  proxies[0] = NULL;
  _assert_proxy_list (asset, proxies);

  proxies[0] = proxy2;
  proxies[1] = proxy0;
  proxies[2] = NULL;
  _assert_proxy_list (proxy1, proxies);

  /* get asset back */
  _assert_effect_asset_request (asset_id, asset);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy2);
  _assert_effect_asset_request (proxy2_id, proxy2);

  /* set the proxy property to NULL for proxy1, should remove all of
   * its proxies */
  fail_unless (ges_asset_set_proxy (proxy1, NULL));
  _assert_proxy_state (asset, NULL, NULL, 7, 0);
  /* only one notify for proxy1, but two separate ones for ex-proxies */
  _assert_proxy_state (proxy0, NULL, NULL, 2, 4);
  _assert_proxy_state (proxy1, NULL, NULL, 3, 2);
  _assert_proxy_state (proxy2, NULL, NULL, 0, 6);

  proxies[0] = NULL;
  _assert_proxy_list (asset, proxies);
  _assert_proxy_list (proxy0, proxies);
  _assert_proxy_list (proxy1, proxies);
  _assert_proxy_list (proxy2, proxies);

  _assert_effect_asset_request (asset_id, asset);
  _assert_effect_asset_request (proxy0_id, proxy0);
  _assert_effect_asset_request (proxy1_id, proxy1);
  _assert_effect_asset_request (proxy2_id, proxy2);

  gst_object_unref (asset);
  gst_object_unref (alt_asset);
  gst_object_unref (proxy0);
  gst_object_unref (proxy1);
  gst_object_unref (proxy2);

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
  tcase_add_test (tc_chain, test_proxy_setters);

  return s;
}

GST_CHECK_MAIN (ges);
