/* GStreamer
 * Copyright (C) 2005 Thomas Vander Stichele <thomas at apestaart dot org>
 *
 * gstelement.c: Unit test for GstElement
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
#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/check/gstcheck.h>
#include <gst/gstelement.h>

GST_START_TEST (test_add_remove_pad)
{
  GstElement *e;
  GstPad *p;

  /* getting an existing element class is cheating, but easier */
  e = gst_element_factory_make ("fakesrc", "source");

  /* create a new floating pad with refcount 1 */
  p = gst_pad_new ("source", GST_PAD_SRC);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);
  /* ref it for ourselves */
  gst_object_ref (p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);
  /* adding it sinks the pad -> not floating, same refcount */
  gst_element_add_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);

  /* removing it reduces the refcount */
  gst_element_remove_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);

  /* clean up our own reference */
  gst_object_unref (p);
  gst_object_unref (e);
}

GST_END_TEST;

GST_START_TEST (test_add_pad_unref_element)
{
  GstElement *e;
  GstPad *p;

  /* getting an existing element class is cheating, but easier */
  e = gst_element_factory_make ("fakesrc", "source");

  /* create a new floating pad with refcount 1 */
  p = gst_pad_new ("source", GST_PAD_SRC);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);
  /* ref it for ourselves */
  gst_object_ref (p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);
  /* adding it sinks the pad -> not floating, same refcount */
  gst_element_add_pad (e, p);
  ASSERT_OBJECT_REFCOUNT (p, "pad", 2);

  /* unreffing the element should clean it up */
  gst_object_unref (GST_OBJECT (e));

  ASSERT_OBJECT_REFCOUNT (p, "pad", 1);

  /* clean up our own reference */
  gst_object_unref (p);
}

GST_END_TEST;

static void
test_add_pad_while_paused_dummy_task (void *user_data)
{
  GstPad *pad = (GstPad *) user_data;
  gst_pad_pause_task (pad);
}

static gboolean
test_add_pad_while_paused_pad_activatemode (GstPad * pad, GstObject * parent,
    GstPadMode mode, gboolean active)
{
  *(gboolean *) pad->activatemodedata = active;
  fail_unless (mode == GST_PAD_MODE_PUSH);
  if (active)
    gst_pad_start_task (pad, test_add_pad_while_paused_dummy_task, pad, NULL);
  else
    gst_pad_stop_task (pad);
  return TRUE;
}

GST_START_TEST (test_add_pad_while_paused)
{
  GstElement *e;
  GstPad *p;
  gboolean active = FALSE;

  e = gst_element_factory_make ("fakesrc", "source");
  gst_element_set_state (e, GST_STATE_PAUSED);
  {
    GstPad *old_pad = gst_element_get_static_pad (e, "src");
    gst_pad_set_active (old_pad, FALSE);
    gst_element_remove_pad (e, old_pad);
    gst_object_unref (old_pad);
  }

  p = gst_pad_new ("dynamic", GST_PAD_SRC);
  gst_pad_set_activatemode_function_full (p,
      test_add_pad_while_paused_pad_activatemode, (void *) &active, NULL);

  fail_if (active);
  gst_element_add_pad (e, p);
  fail_if (!active);
  gst_element_set_state (e, GST_STATE_NULL);
  fail_if (active);

  gst_object_unref (e);
}

GST_END_TEST;

GST_START_TEST (test_error_no_bus)
{
  GstElement *e;
  GstBus *bus;

  e = gst_element_factory_make ("fakesrc", "source");

  /* get the bus, should be NULL */
  bus = gst_element_get_bus (e);
  fail_if (bus != NULL);

  /* I don't want errors shown */
  gst_debug_set_default_threshold (GST_LEVEL_NONE);

  GST_ELEMENT_ERROR (e, RESOURCE, OPEN_READ, ("I could not read"), ("debug"));

  gst_object_unref (e);
}

GST_END_TEST;

/* link and run two elements without putting them in a pipeline */
GST_START_TEST (test_link)
{
  GstElement *src, *sink;

  src = gst_element_factory_make ("fakesrc", "source");
  sink = gst_element_factory_make ("fakesink", "sink");

  fail_unless (gst_element_link_pads (src, "src", sink, "sink"));

  /* do sink to source state change */
  gst_element_set_state (sink, GST_STATE_PAUSED);
  gst_element_set_state (src, GST_STATE_PAUSED);

  /* wait for preroll */
  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* play some more */
  gst_element_set_state (sink, GST_STATE_PLAYING);
  gst_element_set_state (src, GST_STATE_PLAYING);

  g_usleep (G_USEC_PER_SEC);

  /* and stop */
  gst_element_set_state (sink, GST_STATE_PAUSED);
  gst_element_set_state (src, GST_STATE_PAUSED);

  /* wait for preroll */
  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);

  gst_element_set_state (sink, GST_STATE_NULL);
  gst_element_set_state (src, GST_STATE_NULL);

  gst_element_get_state (sink, NULL, NULL, GST_CLOCK_TIME_NONE);
  g_usleep (G_USEC_PER_SEC / 2);

  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);
  gst_element_unlink_pads (src, "src", sink, "sink");
  ASSERT_OBJECT_REFCOUNT (sink, "sink", 1);
  ASSERT_OBJECT_REFCOUNT (src, "src", 1);

  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

/* linking two elements without pads should fail */
GST_START_TEST (test_link_no_pads)
{
  GstElement *src, *sink;

  src = gst_bin_new ("src");
  sink = gst_bin_new ("sink");

  fail_if (gst_element_link (src, sink));

  gst_object_unref (src);
  gst_object_unref (sink);
}

GST_END_TEST;

typedef struct _GstTestElement
{
  GstElement parent;

} GstTestElement;

typedef struct _GstTestElementClass
{
  GstElementClass parent;

} GstTestElementClass;

static void
gst_test_element_class_init (GstTestElementClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *templ;

  gst_element_class_set_metadata (element_class, "Test element",
      "Element", "Does nothing", "Foo Bar <foo@bar.com>");

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 0);

  fail_unless (gst_element_class_get_pad_template (element_class,
          "test") == NULL);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test", GST_PAD_SRC, GST_PAD_ALWAYS, GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 1);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test2", GST_PAD_SRC, GST_PAD_ALWAYS,
          GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test2")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));

  /* Add "test" again, with NONE caps this time */
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test", GST_PAD_SRC, GST_PAD_ALWAYS,
          GST_CAPS_NONE));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_empty (templ->caps));
}

static GType
gst_test_element_get_type (void)
{
  static GType gst_test_element_type = G_TYPE_NONE;

  if (gst_test_element_type == G_TYPE_NONE) {
    static const GTypeInfo gst_test_element_info = {
      sizeof (GstTestElementClass),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) gst_test_element_class_init,
      NULL,
      NULL,
      sizeof (GstTestElement),
      0,
      NULL,                     /* instance_init */
      NULL
    };

    gst_test_element_type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstTestElement", &gst_test_element_info, 0);
  }
  return gst_test_element_type;
}

typedef struct _GstTestElement2
{
  GstTestElement parent;

} GstTestElement2;

typedef struct _GstTestElement2Class
{
  GstTestElementClass parent;

} GstTestElement2Class;

static void
gst_test_element2_class_init (GstTestElement2Class * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPadTemplate *templ;

  gst_element_class_set_metadata (element_class, "Test element 2",
      "Element", "Does nothing", "Foo Bar <foo@bar.com>");

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_empty (templ->caps));

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test2")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));

  /* Add "test" pad with ANY caps, should have "test" pad with EMPTY caps before */
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test", GST_PAD_SRC, GST_PAD_ALWAYS, GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 2);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));


  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("test4", GST_PAD_SRC, GST_PAD_ALWAYS,
          GST_CAPS_ANY));

  fail_unless_equals_int (g_list_length (gst_element_class_get_pad_template_list
          (element_class)), 3);

  fail_unless ((templ =
          gst_element_class_get_pad_template (element_class, "test4")) != NULL);
  fail_unless (gst_caps_is_any (templ->caps));
}

static GType
gst_test_element2_get_type (void)
{
  static GType gst_test_element2_type = G_TYPE_NONE;

  if (gst_test_element2_type == G_TYPE_NONE) {
    static const GTypeInfo gst_test_element2_info = {
      sizeof (GstTestElement2Class),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) gst_test_element2_class_init,
      NULL,
      NULL,
      sizeof (GstTestElement2),
      0,
      NULL,                     /* instance_init */
      NULL
    };

    gst_test_element2_type =
        g_type_register_static (gst_test_element_get_type (), "GstTestElement2",
        &gst_test_element2_info, 0);
  }
  return gst_test_element2_type;
}


GST_START_TEST (test_pad_templates)
{
  GstTestElement *test;
  GstTestElement2 *test2;

  test = g_object_new (gst_test_element_get_type (), NULL);
  test2 = g_object_new (gst_test_element2_get_type (), NULL);

  gst_object_unref (test);
  gst_object_unref (test2);
}

GST_END_TEST;

/* need to return the message here because object, property name and value
 * are only valid as long as we keep the message alive */
static GstMessage *
bus_wait_for_notify_message (GstBus * bus, GstElement ** obj,
    const gchar ** prop_name, const GValue ** val)
{
  GstMessage *msg;

  do {
    msg = gst_bus_timed_pop_filtered (bus, -1, GST_MESSAGE_ANY);
    if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_PROPERTY_NOTIFY)
      break;
    gst_message_unref (msg);
  } while (TRUE);

  gst_message_parse_property_notify (msg, (GstObject **) obj, prop_name, val);
  return msg;
}

GST_START_TEST (test_property_notify_message)
{
  GstElement *pipeline, *identity;
  gulong watch_id0, watch_id1, watch_id2, deep_watch_id1, deep_watch_id2;
  GstBus *bus;

  pipeline = gst_pipeline_new (NULL);
  identity = gst_element_factory_make ("identity", NULL);
  gst_bin_add (GST_BIN (pipeline), identity);

  bus = GST_ELEMENT_BUS (pipeline);

  /* need to set state to READY, otherwise bus will be flushing and discard
   * our messages */
  gst_element_set_state (pipeline, GST_STATE_READY);

  watch_id0 = gst_element_add_property_notify_watch (identity, NULL, FALSE);

  watch_id1 = gst_element_add_property_notify_watch (identity, "sync", FALSE);

  watch_id2 = gst_element_add_property_notify_watch (identity, "silent", TRUE);

  deep_watch_id1 =
      gst_element_add_property_deep_notify_watch (pipeline, NULL, TRUE);

  deep_watch_id2 =
      gst_element_add_property_deep_notify_watch (pipeline, "silent", FALSE);

  /* Now test property changes and if we get the messages we expect. We rely
   * on the signals being fired in the order that they were set up here. */
  {
    const GValue *val;
    const gchar *name;
    GstMessage *msg;
    GstElement *obj;

    /* A - This should be picked up by... */
    g_object_set (identity, "dump", TRUE, NULL);
    /* 1) the catch-all notify on the element (no value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless (obj == identity);
    fail_unless_equals_string (name, "dump");
    fail_unless (val == NULL);
    gst_message_unref (msg);
    /* 2) the catch-all deep-notify on the pipeline (with value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless_equals_string (name, "dump");
    fail_unless (obj == identity);
    fail_unless (G_VALUE_HOLDS_BOOLEAN (val));
    fail_unless_equals_int (g_value_get_boolean (val), TRUE);
    gst_message_unref (msg);

    /* B - This should be picked up by... */
    g_object_set (identity, "sync", TRUE, NULL);
    /* 1) the catch-all notify on the element (no value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless (obj == identity);
    fail_unless_equals_string (name, "sync");
    fail_unless (val == NULL);
    gst_message_unref (msg);
    /* 2) the "sync" notify on the element (no value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless (obj == identity);
    fail_unless_equals_string (name, "sync");
    fail_unless (val == NULL);
    gst_message_unref (msg);
    /* 3) the catch-all deep-notify on the pipeline (with value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless_equals_string (name, "sync");
    fail_unless (obj == identity);
    fail_unless (G_VALUE_HOLDS_BOOLEAN (val));
    fail_unless_equals_int (g_value_get_boolean (val), TRUE);
    gst_message_unref (msg);

    /* C - This should be picked up by... */
    g_object_set (identity, "silent", FALSE, NULL);
    /* 1) the catch-all notify on the element (no value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless (obj == identity);
    fail_unless_equals_string (name, "silent");
    fail_unless (val == NULL);
    gst_message_unref (msg);
    /* 2) the "silent" notify on the element (with value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless (obj == identity);
    fail_unless_equals_string (name, "silent");
    fail_unless (val != NULL);
    fail_unless (G_VALUE_HOLDS_BOOLEAN (val));
    fail_unless_equals_int (g_value_get_boolean (val), FALSE);
    gst_message_unref (msg);
    /* 3) the catch-all deep-notify on the pipeline (with value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless_equals_string (name, "silent");
    fail_unless (obj == identity);
    fail_unless (G_VALUE_HOLDS_BOOLEAN (val));
    fail_unless_equals_int (g_value_get_boolean (val), FALSE);
    gst_message_unref (msg);
    /* 4) the "silent" deep-notify on the pipeline (without value) */
    msg = bus_wait_for_notify_message (bus, &obj, &name, &val);
    fail_unless_equals_string (name, "silent");
    fail_unless (obj == identity);
    fail_unless (val == NULL);
    gst_message_unref (msg);
  }

  gst_element_remove_property_notify_watch (identity, watch_id0);
  gst_element_remove_property_notify_watch (identity, watch_id1);
  gst_element_remove_property_notify_watch (identity, watch_id2);
  gst_element_remove_property_notify_watch (pipeline, deep_watch_id1);
  gst_element_remove_property_notify_watch (pipeline, deep_watch_id2);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
}

GST_END_TEST;

typedef struct _GstTestElement3
{
  GstElement parent;

} GstTestElement3;

typedef struct _GstTestElement3Class
{
  GstElementClass parent;

} GstTestElement3Class;

static GstPad *
gst_test_element3_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * name, const GstCaps * caps)
{
  GstPad *pad;
  gchar *str;
  gchar *pad_name;
  gint n_conversion = 0;
  static gint i = 0;

  str = templ->name_template;
  while ((str = strchr (str, '%'))) {
    n_conversion++;
    str++;
  }

  if (strcmp (templ->name_template, "src_%ublah_blah%ublah") == 0)
    pad_name = g_strdup_printf ("src_%ublah_blah_%ublah", i, i + 1);
  else if (n_conversion == 1) {
    pad_name = g_strdup_printf ("src_%u", i);
  } else if (n_conversion == 2) {
    pad_name = g_strdup_printf ("src_%u_%u", i, i + 1);
  } else if (n_conversion == 3) {
    pad_name = g_strdup_printf ("src_%u_%u_%u", i, i + 1, i + 2);
  } else {
    pad_name = g_strdup (name);
  }

  pad = gst_pad_new_from_template (templ, pad_name);

  gst_element_add_pad (element, pad);

  i++;
  g_free (pad_name);

  return pad;
}

static void
gst_test_element3_release_pad (GstElement * element, GstPad * pad)
{
  gst_element_remove_pad (element, pad);
}

static void
gst_test_element3_init (GstTestElement3 * test)
{
  GstPadTemplate *pad_template;
  GstPad *sinkpad;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (test), "sink");
  g_return_if_fail (pad_template != NULL);
  sinkpad = gst_pad_new_from_template (pad_template, "sink");

  gst_element_add_pad (GST_ELEMENT (test), sinkpad);
}

static void
gst_test_element3_class_init (GstTestElement3Class * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_metadata (element_class, "Test element 3",
      "Element", "For testing request pad template", "Foo Bar <foo@bar.com>");

  element_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_test_element3_request_new_pad);
  element_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_test_element3_release_pad);

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%u", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%u_%u", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%u_%u_%u", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%ublah_blah%ublah", GST_PAD_SRC,
          GST_PAD_REQUEST, GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%d", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%d_%d", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%d_%d_%d", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%s", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src_%u_%s", GST_PAD_SRC, GST_PAD_REQUEST,
          GST_CAPS_ANY));

  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          GST_CAPS_ANY));
}

static GType
gst_test_element3_get_type (void)
{
  static GType gst_test_element3_type = G_TYPE_NONE;

  if (gst_test_element3_type == G_TYPE_NONE) {
    static const GTypeInfo gst_test_element3_info = {
      sizeof (GstTestElement3Class),
      NULL,                     /* base_init */
      NULL,                     /* base_finalize */
      (GClassInitFunc) gst_test_element3_class_init,
      NULL,
      NULL,
      sizeof (GstTestElement3),
      0,
      (GInstanceInitFunc) gst_test_element3_init,
      NULL
    };

    gst_test_element3_type =
        g_type_register_static (gst_element_get_type (), "GstTestElement3",
        &gst_test_element3_info, 0);
  }
  return gst_test_element3_type;
}

static gboolean
gst_test_element3_plugin_init (GstPlugin * plugin)
{
  gst_element_register (plugin, "test3", GST_RANK_NONE,
      gst_test_element3_get_type ());
  return TRUE;
}

GST_START_TEST (test_request_pad_templates)
{
  GstTestElement3 *test;
  GstElement *pipeline, *sink;
  GstPad *pad;
  GHashTable *padnames;
  GHashTableIter iter;
  gpointer key, value;
  const gchar *pad_name, *templ_name;
  GError *err = NULL;

  padnames = g_hash_table_new (g_str_hash, g_str_equal);

  g_hash_table_insert (padnames, (gpointer) "src_0", (gpointer) "src_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%u", (gpointer) "src_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%u_%u",
      (gpointer) "src_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_0_%u", (gpointer) "src_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%u_0", (gpointer) "src_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_0_1", (gpointer) "src_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%u_%u_%u",
      (gpointer) "src_%u_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_0_%u_%u",
      (gpointer) "src_%u_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_0_1_%u",
      (gpointer) "src_%u_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_0_1_2",
      (gpointer) "src_%u_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%u_0_%u",
      (gpointer) "src_%u_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%u_0_1",
      (gpointer) "src_%u_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%u_%u_0",
      (gpointer) "src_%u_%u_%u");
  g_hash_table_insert (padnames, (gpointer) "src_%ublah_blah%ublah",
      (gpointer) "src_%ublah_blah%ublah");
  g_hash_table_insert (padnames, (gpointer) "src_%d", (gpointer) "src_%d");
  g_hash_table_insert (padnames, (gpointer) "src_%d_%d",
      (gpointer) "src_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_1_%d", (gpointer) "src_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_%d_%d_%d",
      (gpointer) "src_%d_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_1_2_%d",
      (gpointer) "src_%d_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_1_%d_2",
      (gpointer) "src_%d_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_%d_2_1",
      (gpointer) "src_%d_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_%d_%d_1",
      (gpointer) "src_%d_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_%d_1_%d",
      (gpointer) "src_%d_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_1_%d_%d",
      (gpointer) "src_%d_%d_%d");
  g_hash_table_insert (padnames, (gpointer) "src_%s", (gpointer) "src_%s");
  g_hash_table_insert (padnames, (gpointer) "src_%u_%s",
      (gpointer) "src_%u_%s");
  g_hash_table_insert (padnames, (gpointer) "src_foo_bar", (gpointer) "src_%s");

  test = g_object_new (gst_test_element3_get_type (), NULL);

  /* check available request pad names */
  g_hash_table_iter_init (&iter, padnames);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad_name = (const gchar *) key;
    templ_name = (const gchar *) value;

    pad = gst_element_request_pad_simple (GST_ELEMENT (test), pad_name);
    fail_unless (pad != NULL);
    gst_element_release_request_pad (GST_ELEMENT (test), pad);
    gst_object_unref (pad);

    pad = gst_element_request_pad (GST_ELEMENT (test),
        gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (test),
            templ_name), pad_name, NULL);
    fail_unless (pad != NULL);
    gst_element_release_request_pad (GST_ELEMENT (test), pad);
    gst_object_unref (pad);
  }

  /* check it working with some APIs
   * gst_element_link/link_pads */
  sink = gst_element_factory_make ("fakesink", "sink");

  fail_unless (gst_element_link (GST_ELEMENT (test), sink));
  gst_element_unlink (GST_ELEMENT (test), sink);

  fail_unless (gst_element_link_pads (GST_ELEMENT (test), "src_%u_%u", sink,
          "sink"));
  gst_element_unlink (GST_ELEMENT (test), sink);

  gst_object_unref (test);
  gst_object_unref (sink);

  /* gst_parse_launch */
  gst_plugin_register_static (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "test3",
      "request pad template test",
      gst_test_element3_plugin_init,
      VERSION, GST_LICENSE, PACKAGE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);

  pipeline =
      gst_parse_launch ("fakesrc ! test3 name=t ! fakesink t. ! fakesink",
      &err);
  fail_unless (pipeline && err == NULL);

  if (err) {
    g_error_free (err);
  }
  g_hash_table_unref (padnames);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_forbidden_pad_template_names)
{
  const gchar *pad_name;
  GSList *padname_blacklists = NULL, *item;

  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%u%u");
  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%u_%s_%s");
  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%s_%u");
  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%s_%s");
  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%s_%s_%s");
  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%s_blah");

  item = padname_blacklists;

  /* check invalid request pad name */
  while (item) {
    pad_name = (const gchar *) (item->data);
    item = g_slist_next (item);
    ASSERT_WARNING (gst_pad_template_new (pad_name, GST_PAD_SRC,
            GST_PAD_REQUEST, GST_CAPS_ANY));
  }

  g_slist_free (padname_blacklists);
}

GST_END_TEST;

static gboolean run_foreach_thread;

/* thread function that just adds/removes pads while main thread iterates pads */
static gpointer
thread_add_remove_pads (GstElement * e)
{
  GPtrArray *pads;
  guint n, c = 0;

  pads = g_ptr_array_new ();

  THREAD_START ();

  while (g_atomic_int_get (&run_foreach_thread)) {
    GstPad *p;
    gchar name[16];

    /* add a new pad */
    g_snprintf (name, 16, "pad_%u", c++);
    p = gst_pad_new (name, g_random_boolean ()? GST_PAD_SRC : GST_PAD_SINK);
    g_ptr_array_add (pads, p);
    gst_element_add_pad (e, p);

    THREAD_SWITCH ();

    /* and remove a random pad */
    if (g_random_boolean () || pads->len > 100) {
      n = g_random_int_range (0, pads->len);
      p = g_ptr_array_remove_index (pads, n);
      gst_element_remove_pad (e, p);
    }

    THREAD_SWITCH ();
  }

  g_ptr_array_free (pads, TRUE);
  return NULL;
}

typedef struct
{
  GQuark q;
  GstPadDirection dir;          /* GST_PAD_UNKNOWN = both are allowed */
  gboolean func_called;
} PadChecks;

static gboolean
pad_foreach_func (GstElement * e, GstPad * pad, gpointer user_data)
{
  PadChecks *checks = user_data;

  /* check we haven't visited this pad already */
  fail_if (g_object_get_qdata (G_OBJECT (pad), checks->q) != NULL);

  g_object_set_qdata (G_OBJECT (pad), checks->q, GINT_TO_POINTER (1));

  if (checks->dir != GST_PAD_UNKNOWN) {
    fail_unless_equals_int (checks->dir, GST_PAD_DIRECTION (pad));
  }
  checks->func_called = TRUE;
  return TRUE;
}

GST_START_TEST (test_foreach_pad)
{
  PadChecks checks = { 0, GST_PAD_UNKNOWN, FALSE };
  GstElement *e;
  gint i;

  e = gst_bin_new ("testbin");

  /* function should not be called if there are no pads! */
  gst_element_foreach_pad (e, pad_foreach_func, &checks);
  fail_if (checks.func_called);

  g_atomic_int_set (&run_foreach_thread, TRUE);

  MAIN_INIT ();
  MAIN_START_THREAD_FUNCTION (0, thread_add_remove_pads, e);
  MAIN_SYNCHRONIZE ();

  for (i = 0; i < 1000; ++i) {
    gchar num[32];

    g_snprintf (num, 32, "foreach-test-%u", i);

    checks.q = g_quark_from_string (num);
    checks.func_called = FALSE;
    if (g_random_boolean ()) {
      checks.dir = GST_PAD_UNKNOWN;
      gst_element_foreach_pad (e, pad_foreach_func, &checks);
    } else if (g_random_boolean ()) {
      checks.dir = GST_PAD_SRC;
      gst_element_foreach_src_pad (e, pad_foreach_func, &checks);
    } else {
      checks.dir = GST_PAD_SINK;
      gst_element_foreach_sink_pad (e, pad_foreach_func, &checks);
    }

    THREAD_SWITCH ();
  }

  g_atomic_int_set (&run_foreach_thread, FALSE);

  MAIN_STOP_THREADS ();

  /* function should be called if there are pads */
  checks.q = g_quark_from_string ("fini");
  checks.dir = GST_PAD_UNKNOWN;
  checks.func_called = FALSE;
  gst_element_foreach_pad (e, pad_foreach_func, &checks);
  fail_if (e->numpads > 0 && !checks.func_called);

  gst_object_unref (e);
}

GST_END_TEST;

typedef struct
{
  GMutex lock;
  GCond cond;
  GThread *caller_thread;

  gint called;
  gint notified;
} CallAsyncData;

static void
call_async_func (GstElement * element, gpointer user_data)
{
  CallAsyncData *data;

  fail_unless (GST_IS_ELEMENT (element));

  data = (CallAsyncData *) user_data;

  fail_unless (g_thread_self () != data->caller_thread);

  g_mutex_lock (&data->lock);
  assert_equals_int (data->called, 0);
  assert_equals_int (data->notified, 0);
  data->called++;
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->lock);
}

static void
call_async_notify (gpointer user_data)
{
  CallAsyncData *data = (CallAsyncData *) user_data;

  /* notify should be called from another thread */
  fail_unless (g_thread_self () != data->caller_thread);

  g_mutex_lock (&data->lock);
  assert_equals_int (data->called, 1);
  assert_equals_int (data->notified, 0);
  data->notified++;
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->lock);
}

GST_START_TEST (test_call_async)
{
  GstElement *element;
  CallAsyncData *data;

  element = gst_element_factory_make ("fakesrc", NULL);
  fail_unless (element);

  data = g_new0 (CallAsyncData, 1);
  g_mutex_init (&data->lock);
  g_cond_init (&data->cond);
  data->caller_thread = g_thread_self ();

  gst_element_call_async (element,
      call_async_func, data, (GDestroyNotify) call_async_notify);
  g_mutex_lock (&data->lock);
  while (!data->called || !data->notified)
    g_cond_wait (&data->cond, &data->lock);
  g_mutex_unlock (&data->lock);

  assert_equals_int (data->called, 1);
  assert_equals_int (data->notified, 1);

  g_mutex_clear (&data->lock);
  g_cond_clear (&data->cond);
  g_free (data);

  gst_object_unref (element);
}

GST_END_TEST;

static Suite *
gst_element_suite (void)
{
  Suite *s = suite_create ("GstElement");
  TCase *tc_chain = tcase_create ("element tests");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_add_remove_pad);
  tcase_add_test (tc_chain, test_add_pad_unref_element);
  tcase_add_test (tc_chain, test_add_pad_while_paused);
  tcase_add_test (tc_chain, test_error_no_bus);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_link_no_pads);
  tcase_add_test (tc_chain, test_pad_templates);
  tcase_add_test (tc_chain, test_property_notify_message);
  tcase_add_test (tc_chain, test_request_pad_templates);
  tcase_add_test (tc_chain, test_forbidden_pad_template_names);
  tcase_add_test (tc_chain, test_foreach_pad);
  tcase_add_test (tc_chain, test_call_async);

  return s;
}

GST_CHECK_MAIN (gst_element);
