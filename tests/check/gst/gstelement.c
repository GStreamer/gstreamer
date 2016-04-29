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

  g_object_unref (test);
  g_object_unref (test2);
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
  GSList *padname_blacklists = NULL, *item;
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

  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%u%u");
  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%u_%d");
  padname_blacklists =
      g_slist_append (padname_blacklists, (gpointer) "src_%u_%u_");
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

  test = g_object_new (gst_test_element3_get_type (), NULL);

  /* check available request pad names */
  g_hash_table_iter_init (&iter, padnames);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    pad_name = (const gchar *) key;
    templ_name = (const gchar *) value;

    pad = gst_element_get_request_pad (GST_ELEMENT (test), pad_name);
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

  item = padname_blacklists;

  /* check invalid request pad name */
  while (item) {
    pad_name = (const gchar *) (item->data);
    item = g_slist_next (item);
    pad = gst_element_get_request_pad (GST_ELEMENT (test), pad_name);
    fail_unless (pad == NULL);
  }

  /* check it working with some APIs
   * gst_element_link/link_pads */
  sink = gst_element_factory_make ("fakesink", "sink");

  fail_unless (gst_element_link (GST_ELEMENT (test), sink));
  gst_element_unlink (GST_ELEMENT (test), sink);

  fail_unless (gst_element_link_pads (GST_ELEMENT (test), "src_%u_%u", sink,
          "sink"));
  gst_element_unlink (GST_ELEMENT (test), sink);

  g_object_unref (test);
  g_object_unref (sink);

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
  g_slist_free (padname_blacklists);
  g_hash_table_unref (padnames);
  gst_object_unref (pipeline);
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
  tcase_add_test (tc_chain, test_error_no_bus);
  tcase_add_test (tc_chain, test_link);
  tcase_add_test (tc_chain, test_link_no_pads);
  tcase_add_test (tc_chain, test_pad_templates);
  tcase_add_test (tc_chain, test_property_notify_message);
  tcase_add_test (tc_chain, test_request_pad_templates);

  return s;
}

GST_CHECK_MAIN (gst_element);
