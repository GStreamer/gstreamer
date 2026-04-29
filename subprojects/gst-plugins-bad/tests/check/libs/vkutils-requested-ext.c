/* GStreamer
 *
 * Copyright (C) 2026 GStreamer developers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/vulkan/vulkan.h>

#define EXT_PEER "VK_EXT_PEER_TEST"
#define EXT_APP "VK_EXT_APP_TEST"
#define EXT_MIDDLE "VK_EXT_MIDDLE_TEST"
#define EXT_DOWN "VK_EXT_DOWN_TEST"

#define EXT_PEER_DEV "VK_EXT_PEER_DEV_TEST"
#define EXT_APP_DEV "VK_EXT_APP_DEV_TEST"

static gboolean
strv_has (gchar ** v, const gchar * name)
{
  guint i;

  if (!v)
    return FALSE;
  for (i = 0; v[i]; i++) {
    if (g_strcmp0 (v[i], name) == 0)
      return TRUE;
  }
  return FALSE;
}

static GstPadProbeReturn
probe_middle_sink (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);
  GstElement *elem = GST_ELEMENT (user_data);
  GstPadProbeType ptype = GST_PAD_PROBE_INFO_TYPE (info);

  if (!(ptype & (GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM |
              GST_PAD_PROBE_TYPE_QUERY_UPSTREAM)))
    return GST_PAD_PROBE_OK;

  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return GST_PAD_PROBE_OK;

  if (!gst_vulkan_requested_extensions_handle_context_query (elem,
          query, GST_PAD_SRC, NULL))
    return GST_PAD_PROBE_OK;

  return GST_PAD_PROBE_HANDLED;
}

static GstPadProbeReturn
probe_down_sink (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);
  const gchar *ctype;
  GstContext *ctx;
  GstPadProbeType ptype = GST_PAD_PROBE_INFO_TYPE (info);

  (void) pad;
  (void) user_data;

  if (!(ptype & (GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM |
              GST_PAD_PROBE_TYPE_QUERY_UPSTREAM)))
    return GST_PAD_PROBE_OK;

  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return GST_PAD_PROBE_OK;

  gst_query_parse_context_type (query, &ctype);
  if (g_strcmp0 (ctype,
          GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR) != 0)
    return GST_PAD_PROBE_OK;

  ctx = gst_vulkan_requested_instance_extensions_context_new ();
  gst_vulkan_requested_extensions_context_add (ctx, EXT_DOWN);
  gst_query_set_context (query, ctx);
  gst_context_unref (ctx);
  return GST_PAD_PROBE_HANDLED;
}

static GstPadProbeReturn
probe_peer_sink (GstPad * pad, GstPadProbeInfo * info, gpointer user_data)
{
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);
  const gchar *ctype;
  GstContext *ctx;
  GstPadProbeType ptype = GST_PAD_PROBE_INFO_TYPE (info);

  (void) pad;
  (void) user_data;

  if (!(ptype & (GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM |
              GST_PAD_PROBE_TYPE_QUERY_UPSTREAM)))
    return GST_PAD_PROBE_OK;

  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return GST_PAD_PROBE_OK;

  gst_query_parse_context_type (query, &ctype);
  if (g_strcmp0 (ctype,
          GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR) != 0)
    return GST_PAD_PROBE_OK;

  ctx = gst_vulkan_requested_instance_extensions_context_new ();
  gst_vulkan_requested_extensions_context_add (ctx, EXT_PEER);
  gst_query_set_context (query, ctx);
  gst_context_unref (ctx);
  return GST_PAD_PROBE_HANDLED;
}

static GstPadProbeReturn
probe_peer_sink_device (GstPad * pad, GstPadProbeInfo * info,
    gpointer user_data)
{
  GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);
  const gchar *ctype;
  GstContext *ctx;
  GstPadProbeType ptype = GST_PAD_PROBE_INFO_TYPE (info);

  (void) pad;
  (void) user_data;

  if (!(ptype & (GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM |
              GST_PAD_PROBE_TYPE_QUERY_UPSTREAM)))
    return GST_PAD_PROBE_OK;

  if (GST_QUERY_TYPE (query) != GST_QUERY_CONTEXT)
    return GST_PAD_PROBE_OK;

  gst_query_parse_context_type (query, &ctype);
  if (g_strcmp0 (ctype,
          GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR) != 0)
    return GST_PAD_PROBE_OK;

  ctx = gst_vulkan_requested_device_extensions_context_new ();
  gst_vulkan_requested_extensions_context_add (ctx, EXT_PEER_DEV);
  gst_query_set_context (query, ctx);
  gst_context_unref (ctx);
  return GST_PAD_PROBE_HANDLED;
}

GST_START_TEST (test_collect_merges_peer_and_app_contexts)
{
  GstElement *e1, *e2;
  GstPad *e2_sink;
  gchar **inst = NULL;
  GstContext *app;
  GstContext *merged_ctx;

  e1 = gst_element_factory_make ("identity", "e1");
  e2 = gst_element_factory_make ("identity", "e2");
  fail_unless (e1 != NULL && e2 != NULL);

  fail_unless (gst_element_link (e1, e2));

  e2_sink = gst_element_get_static_pad (e2, "sink");
  fail_unless (e2_sink != NULL);
  gst_pad_add_probe (e2_sink,
      (GstPadProbeType) GST_PAD_PROBE_TYPE_QUERY_BOTH,
      probe_peer_sink, NULL, NULL);
  gst_object_unref (e2_sink);

  app = gst_vulkan_requested_instance_extensions_context_new ();
  gst_vulkan_requested_extensions_context_add (app, EXT_APP);
  gst_element_set_context (e1, app);
  gst_context_unref (app);

  gst_vulkan_requested_extensions_global_context_query (e1,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);
  merged_ctx =
      gst_vulkan_element_get_merged_requested_instance_extensions_context (e1);
  fail_unless (merged_ctx != NULL);
  inst = gst_vulkan_requested_extensions_context_dup_extensions (merged_ctx);
  gst_context_unref (merged_ctx);
  fail_unless (inst != NULL);
  fail_unless (strv_has (inst, EXT_PEER));
  fail_unless (strv_has (inst, EXT_APP));
  g_strfreev (inst);

  gst_object_unref (e1);
  gst_object_unref (e2);
}

GST_END_TEST;

GST_START_TEST (test_middle_forwards_then_adds_local)
{
  GstElement *e1, *e2, *e3;
  GstPad *e2_sink, *e3_sink;
  gchar **inst = NULL;
  GstContext *merged_ctx;

  e1 = gst_element_factory_make ("identity", "e1a");
  e2 = gst_element_factory_make ("identity", "e2a");
  e3 = gst_element_factory_make ("identity", "e3a");
  fail_unless (e1 && e2 && e3);
  fail_unless (gst_element_link_many (e1, e2, e3, NULL));

  {
    GstContext *mid_ctx =
        gst_vulkan_requested_instance_extensions_context_new ();

    gst_vulkan_requested_extensions_context_add (mid_ctx, EXT_MIDDLE);
    gst_element_set_context (e2, mid_ctx);
    gst_context_unref (mid_ctx);
  }

  e2_sink = gst_element_get_static_pad (e2, "sink");
  fail_unless (e2_sink != NULL);
  gst_pad_add_probe (e2_sink,
      (GstPadProbeType) GST_PAD_PROBE_TYPE_QUERY_BOTH,
      probe_middle_sink, e2, NULL);
  gst_object_unref (e2_sink);

  e3_sink = gst_element_get_static_pad (e3, "sink");
  fail_unless (e3_sink != NULL);
  gst_pad_add_probe (e3_sink,
      (GstPadProbeType) GST_PAD_PROBE_TYPE_QUERY_BOTH,
      probe_down_sink, NULL, NULL);
  gst_object_unref (e3_sink);

  gst_vulkan_requested_extensions_global_context_query (e1,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);
  merged_ctx =
      gst_vulkan_element_get_merged_requested_instance_extensions_context (e1);
  fail_unless (merged_ctx != NULL);
  inst = gst_vulkan_requested_extensions_context_dup_extensions (merged_ctx);
  gst_context_unref (merged_ctx);
  fail_unless (inst != NULL);
  fail_unless (strv_has (inst, EXT_DOWN));
  fail_unless (strv_has (inst, EXT_MIDDLE));
  g_strfreev (inst);

  gst_object_unref (e1);
  gst_object_unref (e2);
  gst_object_unref (e3);
}

GST_END_TEST;

GST_START_TEST (test_device_collect_merges_peer_and_app)
{
  GstElement *e1, *e2;
  GstPad *e2_sink;
  gchar **dev = NULL;
  GstContext *app;
  GstContext *merged_ctx;
  GstVulkanInstance *vk_inst = gst_vulkan_instance_new ();

  fail_unless (vk_inst != NULL);

  e1 = gst_element_factory_make ("identity", "e1d");
  e2 = gst_element_factory_make ("identity", "e2d");
  fail_unless (e1 && e2);
  fail_unless (gst_element_link (e1, e2));

  e2_sink = gst_element_get_static_pad (e2, "sink");
  fail_unless (e2_sink != NULL);
  gst_pad_add_probe (e2_sink,
      (GstPadProbeType) GST_PAD_PROBE_TYPE_QUERY_BOTH,
      probe_peer_sink_device, NULL, NULL);
  gst_object_unref (e2_sink);

  app = gst_vulkan_requested_device_extensions_context_new ();
  gst_vulkan_requested_extensions_context_set_vulkan_instance (app, vk_inst);
  gst_vulkan_requested_extensions_context_add (app, EXT_APP_DEV);
  gst_element_set_context (e1, app);
  gst_context_unref (app);

  {
    GstQuery *query;
    GstContext *ctx;

    query =
        gst_vulkan_requested_extensions_local_context_query (e1,
        GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR, vk_inst);
    fail_unless (query != NULL);
    gst_query_parse_context (query, &ctx);
    gst_element_set_context (e1, ctx);
    gst_query_unref (query);
  }
  merged_ctx =
      gst_vulkan_element_get_merged_requested_device_extensions_context (e1);
  fail_unless (merged_ctx != NULL);
  dev = gst_vulkan_requested_extensions_context_dup_extensions (merged_ctx);
  gst_context_unref (merged_ctx);
  fail_unless (dev != NULL);
  fail_unless (strv_has (dev, EXT_PEER_DEV));
  fail_unless (strv_has (dev, EXT_APP_DEV));
  g_strfreev (dev);

  gst_object_unref (vk_inst);
  gst_object_unref (e1);
  gst_object_unref (e2);
}

GST_END_TEST;

GST_START_TEST (test_device_merged_requires_instance)
{
  GstElement *e1;
  GstContext *app;
  GstContext *merged_ctx;

  e1 = gst_element_factory_make ("identity", "e1solo");
  fail_unless (e1 != NULL);

  app = gst_vulkan_requested_device_extensions_context_new ();
  gst_vulkan_requested_extensions_context_add (app, EXT_APP_DEV);
  gst_element_set_context (e1, app);
  gst_context_unref (app);

  merged_ctx =
      gst_vulkan_element_get_merged_requested_device_extensions_context (e1);
  fail_unless (merged_ctx == NULL);

  gst_object_unref (e1);
}

GST_END_TEST;

GST_START_TEST (test_device_merge_drops_extensions_from_other_instance)
{
  GstElement *e1;
  GstVulkanInstance *a = gst_vulkan_instance_new ();
  GstVulkanInstance *b = gst_vulkan_instance_new ();
  GstContext *c1 = gst_vulkan_requested_device_extensions_context_new ();
  GstContext *c2 = gst_vulkan_requested_device_extensions_context_new ();
  gchar **dev;
  GstContext *merged;
  gboolean has_first;
  gboolean has_second;

  fail_unless (a && b);
  e1 = gst_element_factory_make ("identity", "e1conf");
  fail_unless (e1);

  gst_vulkan_requested_extensions_context_set_vulkan_instance (c1, a);
  gst_vulkan_requested_extensions_context_add (c1, "VK_EXT_FIRST");
  gst_element_set_context (e1, c1);
  gst_context_unref (c1);

  gst_vulkan_requested_extensions_context_set_vulkan_instance (c2, b);
  gst_vulkan_requested_extensions_context_add (c2, "VK_EXT_SECOND");
  gst_element_set_context (e1, c2);
  gst_context_unref (c2);

  merged =
      gst_vulkan_element_get_merged_requested_device_extensions_context (e1);
  fail_unless (merged != NULL);
  dev = gst_vulkan_requested_extensions_context_dup_extensions (merged);
  fail_unless (dev != NULL);
  has_first = strv_has (dev, "VK_EXT_FIRST");
  has_second = strv_has (dev, "VK_EXT_SECOND");
  fail_unless (has_first || has_second);
  fail_unless (!(has_first && has_second));
  g_strfreev (dev);
  gst_context_unref (merged);

  gst_object_unref (a);
  gst_object_unref (b);
  gst_object_unref (e1);
}

GST_END_TEST;

static GstBusSyncReply
need_context_fail_sync (GstBus * bus, GstMessage * message, gpointer user_data)
{
  (void) bus;
  (void) user_data;

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_NEED_CONTEXT)
    fail ("device path must not post NEED_CONTEXT");
  return GST_BUS_PASS;
}

GST_START_TEST (test_device_never_posts_need_context)
{
  GstElement *e1, *e2;
  GstPad *e2_sink;
  GstBus *bus;
  GstVulkanInstance *vk_inst = gst_vulkan_instance_new ();

  fail_unless (vk_inst != NULL);

  e1 = gst_element_factory_make ("identity", "e1nc");
  e2 = gst_element_factory_make ("identity", "e2nc");
  fail_unless (e1 && e2);
  fail_unless (gst_element_link (e1, e2));

  e2_sink = gst_element_get_static_pad (e2, "sink");
  fail_unless (e2_sink != NULL);
  gst_pad_add_probe (e2_sink,
      (GstPadProbeType) GST_PAD_PROBE_TYPE_QUERY_BOTH,
      probe_peer_sink_device, NULL, NULL);
  gst_object_unref (e2_sink);

  bus = gst_bus_new ();
  gst_bus_set_sync_handler (bus, need_context_fail_sync, NULL, NULL);
  gst_element_set_bus (e1, bus);

  {
    GstQuery *query;

    query =
        gst_vulkan_requested_extensions_local_context_query (e1,
        GST_VULKAN_REQUESTED_DEVICE_EXTENSIONS_CONTEXT_TYPE_STR, vk_inst);
    if (query)
      gst_query_unref (query);
  }

  gst_element_set_bus (e1, NULL);
  gst_object_unref (bus);
  gst_object_unref (vk_inst);
  gst_object_unref (e1);
  gst_object_unref (e2);
}

GST_END_TEST;

GST_START_TEST (test_instance_always_posts_need_context_with_peer)
{
  GstElement *e1, *e2;
  GstPad *e2_sink;
  GstBus *bus;
  GstMessage *msg;

  e1 = gst_element_factory_make ("identity", "e1in");
  e2 = gst_element_factory_make ("identity", "e2in");
  fail_unless (e1 && e2);
  fail_unless (gst_element_link (e1, e2));

  e2_sink = gst_element_get_static_pad (e2, "sink");
  fail_unless (e2_sink != NULL);
  gst_pad_add_probe (e2_sink,
      (GstPadProbeType) GST_PAD_PROBE_TYPE_QUERY_BOTH,
      probe_peer_sink, NULL, NULL);
  gst_object_unref (e2_sink);

  bus = gst_bus_new ();
  gst_element_set_bus (e1, bus);

  gst_vulkan_requested_extensions_global_context_query (e1,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);

  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_NEED_CONTEXT);
  fail_unless (msg != NULL);
  gst_message_unref (msg);

  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_HAVE_CONTEXT);
  fail_unless (msg != NULL);
  gst_message_unref (msg);

  fail_unless (gst_bus_pop (bus) == NULL);

  gst_element_set_bus (e1, NULL);
  gst_object_unref (bus);
  gst_object_unref (e1);
  gst_object_unref (e2);
}

GST_END_TEST;

GST_START_TEST (test_instance_posts_need_context_without_local_match)
{
  GstElement *e1;
  GstBus *bus;
  GstMessage *msg;

  e1 = gst_element_factory_make ("identity", "e1nc2");
  fail_unless (e1 != NULL);

  bus = gst_bus_new ();
  gst_element_set_bus (e1, bus);

  gst_vulkan_requested_extensions_global_context_query (e1,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);

  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_NEED_CONTEXT);
  fail_unless (msg != NULL);
  gst_message_unref (msg);

  fail_unless (gst_bus_pop_filtered (bus, GST_MESSAGE_HAVE_CONTEXT) == NULL);
  fail_unless (gst_bus_pop (bus) == NULL);

  gst_element_set_bus (e1, NULL);
  gst_object_unref (bus);
  gst_object_unref (e1);
}

GST_END_TEST;

GST_START_TEST (test_instance_global_sets_context_from_local_match)
{
  GstElement *e1;
  GstBus *bus;
  GstMessage *msg;
  GstContext *ctx;
  GstContext *app;
  GstContext *merged_ctx;
  gchar **ext;

  e1 = gst_element_factory_make ("identity", "e1hc");
  fail_unless (e1 != NULL);

  app = gst_vulkan_requested_instance_extensions_context_new ();
  gst_vulkan_requested_extensions_context_add (app, EXT_APP);
  gst_element_set_context (e1, app);
  gst_context_unref (app);

  bus = gst_bus_new ();
  gst_element_set_bus (e1, bus);

  gst_vulkan_requested_extensions_global_context_query (e1,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);

  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_NEED_CONTEXT);
  fail_unless (msg != NULL);
  gst_message_unref (msg);

  msg = gst_bus_pop_filtered (bus, GST_MESSAGE_HAVE_CONTEXT);
  fail_unless (msg != NULL);
  gst_message_parse_have_context (msg, &ctx);
  fail_unless (GST_IS_CONTEXT (ctx));
  fail_unless_equals_string (gst_context_get_context_type (ctx),
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);
  ext = gst_vulkan_requested_extensions_context_dup_extensions (ctx);
  fail_unless (strv_has (ext, EXT_APP));
  g_strfreev (ext);
  gst_context_unref (ctx);
  gst_message_unref (msg);

  fail_unless (gst_bus_pop (bus) == NULL);

  merged_ctx =
      gst_vulkan_element_get_merged_requested_instance_extensions_context (e1);
  fail_unless (merged_ctx != NULL);
  ext = gst_vulkan_requested_extensions_context_dup_extensions (merged_ctx);
  fail_unless (strv_has (ext, EXT_APP));
  g_strfreev (ext);
  gst_context_unref (merged_ctx);

  gst_element_set_bus (e1, NULL);
  gst_object_unref (bus);
  gst_object_unref (e1);
}

GST_END_TEST;

static GstBusSyncReply
instance_need_context_sync (GstBus * bus, GstMessage * message,
    gpointer user_data)
{
  const gchar *ext_name = user_data;

  (void) bus;

  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_NEED_CONTEXT) {
    const gchar *type;
    GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));
    GstContext *ctx;

    fail_unless (gst_message_parse_context_type (message, &type));
    fail_unless_equals_string (type,
        GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);
    ctx = gst_vulkan_requested_instance_extensions_context_new ();
    gst_vulkan_requested_extensions_context_add (ctx, ext_name);
    gst_element_set_context (element, ctx);
    gst_context_unref (ctx);
  }

  return GST_BUS_PASS;
}

GST_START_TEST (test_instance_remerge_after_need_context)
{
  GstElement *e1;
  GstBus *bus;
  gchar **inst = NULL;
  GstContext *merged_ctx;

  e1 = gst_element_factory_make ("identity", "e1rm");
  fail_unless (e1 != NULL);

  bus = gst_bus_new ();
  gst_bus_set_sync_handler (bus, instance_need_context_sync,
      (gpointer) EXT_APP, NULL);
  gst_element_set_bus (e1, bus);

  gst_vulkan_requested_extensions_global_context_query (e1,
      GST_VULKAN_REQUESTED_INSTANCE_EXTENSIONS_CONTEXT_TYPE_STR);

  merged_ctx =
      gst_vulkan_element_get_merged_requested_instance_extensions_context (e1);
  fail_unless (merged_ctx != NULL);
  inst = gst_vulkan_requested_extensions_context_dup_extensions (merged_ctx);
  gst_context_unref (merged_ctx);
  fail_unless (inst != NULL);
  fail_unless (strv_has (inst, EXT_APP));
  g_strfreev (inst);

  gst_element_set_bus (e1, NULL);
  gst_object_unref (bus);
  gst_object_unref (e1);
}

GST_END_TEST;

static Suite *
vkutils_requested_ext_suite (void)
{
  Suite *s = suite_create ("VkUtilsRequestedExt");
  TCase *tc = tcase_create ("general");

  suite_add_tcase (s, tc);
  tcase_add_test (tc, test_collect_merges_peer_and_app_contexts);
  tcase_add_test (tc, test_middle_forwards_then_adds_local);
  tcase_add_test (tc, test_device_collect_merges_peer_and_app);
  tcase_add_test (tc, test_device_merged_requires_instance);
  tcase_add_test (tc, test_device_merge_drops_extensions_from_other_instance);
  tcase_add_test (tc, test_device_never_posts_need_context);
  tcase_add_test (tc, test_instance_always_posts_need_context_with_peer);
  tcase_add_test (tc, test_instance_posts_need_context_without_local_match);
  tcase_add_test (tc, test_instance_global_sets_context_from_local_match);
  tcase_add_test (tc, test_instance_remerge_after_need_context);
  return s;
}

GST_CHECK_MAIN (vkutils_requested_ext);
