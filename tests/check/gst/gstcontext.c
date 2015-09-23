/* GStreamer
 * Copyright (C) 2013 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2013 Sebastian Dröge <slomo@circular-chaos.org>
 *
 * gstcontext.c: Unit test for GstContext
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

#include <gst/check/gstcheck.h>

GST_START_TEST (test_basic)
{
  GstContext *c1, *c2;
  GstStructure *s1;
  const GstStructure *s2;

  c1 = gst_context_new ("test", FALSE);
  fail_unless (c1 != NULL);
  fail_unless (GST_IS_CONTEXT (c1));
  s1 = gst_context_writable_structure (c1);
  fail_unless (s1 != NULL);
  gst_structure_set (s1, "foobar", G_TYPE_INT, 1, NULL);

  c2 = gst_context_copy (c1);
  fail_unless (c2 != NULL);
  fail_unless (GST_IS_CONTEXT (c2));
  fail_unless (strcmp (gst_context_get_context_type (c1),
          gst_context_get_context_type (c2)) == 0);
  s2 = gst_context_get_structure (c2);
  fail_unless (s2 != NULL);
  fail_unless (gst_structure_is_equal (s1, s2));

  gst_context_unref (c1);
  gst_context_unref (c2);
}

GST_END_TEST;

typedef struct
{
  GstElement parent;
  gboolean set_before_ready;
  gboolean set_from_need_context;
  gboolean create_self;

  gboolean have_foobar;
} GstContextElement;

typedef struct
{
  GstElementClass parent_class;
} GstContextElementClass;

GType gst_context_element_get_type (void);

G_DEFINE_TYPE (GstContextElement, gst_context_element, GST_TYPE_ELEMENT);

static void
gst_context_element_set_context (GstElement * element, GstContext * context)
{
  if (strcmp (gst_context_get_context_type (context), "foobar") == 0)
    ((GstContextElement *) element)->have_foobar = TRUE;

  GST_ELEMENT_CLASS (gst_context_element_parent_class)->set_context (element,
      context);
}

static GstStateChangeReturn
gst_context_element_change_state (GstElement * element,
    GstStateChange transition)
{
  GstContextElement *celement = (GstContextElement *) element;

  if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
    GstContext *context;
    GstMessage *msg;
    gboolean have_foobar = celement->have_foobar;

    if (celement->set_before_ready && !have_foobar)
      return GST_STATE_CHANGE_FAILURE;
    else if (celement->set_before_ready)
      goto chain_up;

    if (celement->set_from_need_context && have_foobar)
      return GST_STATE_CHANGE_FAILURE;

    if (!have_foobar) {
      /* Here we would first query downstream for a context but we have no pads */
      msg = gst_message_new_need_context (GST_OBJECT (element), "foobar");
      gst_element_post_message (element, msg);

      have_foobar = celement->have_foobar;
    }

    if (celement->set_from_need_context && !have_foobar)
      return GST_STATE_CHANGE_FAILURE;
    else if (celement->set_from_need_context)
      goto chain_up;

    if (celement->create_self && have_foobar)
      return GST_STATE_CHANGE_FAILURE;

    if (!have_foobar) {
      context = gst_context_new ("foobar", FALSE);
      gst_element_set_context (element, context);
      msg =
          gst_message_new_have_context (GST_OBJECT (element),
          gst_context_ref (context));
      gst_element_post_message (element, msg);
      gst_context_unref (context);
    }
  }

chain_up:
  return
      GST_ELEMENT_CLASS (gst_context_element_parent_class)->change_state
      (element, transition);
}

static void
gst_context_element_class_init (GstContextElementClass * klass)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);

  gstelement_class->set_context = gst_context_element_set_context;
  gstelement_class->change_state = gst_context_element_change_state;
}

static void
gst_context_element_init (GstContextElement * self)
{

}

GST_START_TEST (test_element_set_before_ready)
{
  GstBus *bus;
  GstElement *element;
  GstContext *context;
  GstMessage *msg;

  element = g_object_new (gst_context_element_get_type (), NULL);
  bus = gst_bus_new ();
  gst_element_set_bus (element, bus);

  ((GstContextElement *) element)->set_before_ready = TRUE;

  fail_if (gst_element_set_state (element,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);
  fail_if (gst_bus_pop (bus) != NULL);

  context = gst_context_new ("foobar", FALSE);
  gst_element_set_context (element, context);
  fail_unless (gst_element_set_state (element,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);
  fail_unless ((msg =
          gst_bus_pop_filtered (bus, GST_MESSAGE_STATE_CHANGED)) != NULL);
  gst_message_unref (msg);
  fail_if (gst_bus_pop (bus) != NULL);

  fail_unless (((GstContextElement *) element)->have_foobar);

  gst_context_unref (context);

  gst_element_set_bus (element, NULL);
  fail_unless (gst_element_set_state (element,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (element);
}

GST_END_TEST;

static GstBusSyncReply
sync_handler (GstBus * bus, GstMessage * message, gpointer user_data)
{
  if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_NEED_CONTEXT) {
    const gchar *type;
    GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (message));
    GstContext *context;

    fail_unless (gst_message_parse_context_type (message, &type));
    fail_unless_equals_string (type, "foobar");
    context = gst_context_new ("foobar", FALSE);
    gst_element_set_context (element, context);
    gst_context_unref (context);
  }

  return GST_BUS_PASS;
}

GST_START_TEST (test_element_set_from_need_context)
{
  GstBus *bus;
  GstElement *element;
  GstMessage *msg;

  element = g_object_new (gst_context_element_get_type (), NULL);
  bus = gst_bus_new ();
  gst_bus_set_sync_handler (bus, sync_handler, NULL, NULL);
  gst_element_set_bus (element, bus);

  ((GstContextElement *) element)->set_from_need_context = TRUE;

  fail_unless (gst_element_set_state (element,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);
  fail_unless ((msg =
          gst_bus_pop_filtered (bus, GST_MESSAGE_NEED_CONTEXT)) != NULL);
  gst_message_unref (msg);
  fail_unless ((msg =
          gst_bus_pop_filtered (bus, GST_MESSAGE_STATE_CHANGED)) != NULL);
  gst_message_unref (msg);
  fail_if (gst_bus_pop (bus) != NULL);

  fail_unless (((GstContextElement *) element)->have_foobar);

  gst_element_set_bus (element, NULL);
  fail_unless (gst_element_set_state (element,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (element);
}

GST_END_TEST;

GST_START_TEST (test_element_create_self)
{
  GstBus *bus;
  GstElement *element;
  GstContext *context;
  GstMessage *msg;

  element = g_object_new (gst_context_element_get_type (), NULL);
  bus = gst_bus_new ();
  gst_element_set_bus (element, bus);

  ((GstContextElement *) element)->create_self = TRUE;

  fail_unless (gst_element_set_state (element,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);
  fail_unless ((msg =
          gst_bus_pop_filtered (bus, GST_MESSAGE_NEED_CONTEXT)) != NULL);
  gst_message_unref (msg);
  fail_unless ((msg =
          gst_bus_pop_filtered (bus, GST_MESSAGE_HAVE_CONTEXT)) != NULL);
  gst_message_parse_have_context (msg, &context);
  fail_unless (GST_IS_CONTEXT (context));
  fail_unless_equals_string (gst_context_get_context_type (context), "foobar");
  gst_context_unref (context);
  gst_message_unref (msg);
  fail_unless ((msg =
          gst_bus_pop_filtered (bus, GST_MESSAGE_STATE_CHANGED)) != NULL);
  gst_message_unref (msg);
  fail_if (gst_bus_pop (bus) != NULL);

  fail_unless (((GstContextElement *) element)->have_foobar);

  gst_element_set_bus (element, NULL);
  fail_unless (gst_element_set_state (element,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (element);
}

GST_END_TEST;

GST_START_TEST (test_element_bin_caching)
{
  GstBus *bus;
  GstElement *bin;
  GstElement *element, *element2;

  bin = gst_bin_new (NULL);
  element = g_object_new (gst_context_element_get_type (), NULL);
  element2 = g_object_new (gst_context_element_get_type (), NULL);
  gst_bin_add_many (GST_BIN (bin), element, element2, NULL);

  /* FIXME: This assumes (as currently is true) the GstBin activates
   * the last added element first if none of them is a sink or has pads
   */
  ((GstContextElement *) element2)->create_self = TRUE;
  ((GstContextElement *) element)->set_from_need_context = TRUE;

  bus = gst_bus_new ();
  gst_element_set_bus (bin, bus);

  fail_unless (gst_element_set_state (bin,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);

  fail_unless (((GstContextElement *) element)->have_foobar);
  fail_unless (((GstContextElement *) element2)->have_foobar);

  gst_element_set_bus (bin, NULL);
  fail_unless (gst_element_set_state (bin,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_add_element_to_bin)
{
  GstBus *bus;
  GstElement *bin;
  GstElement *element;
  GList *contexts, *contexts2, *l;

  /* Start with an element not inside a bin requesting a context. Add the
   * element to a bin and check the context propagation. */
  element = g_object_new (gst_context_element_get_type (), NULL);

  ((GstContextElement *) element)->create_self = TRUE;

  fail_unless (gst_element_set_state (element,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);

  fail_unless (((GstContextElement *) element)->have_foobar);

  bin = gst_bin_new (NULL);

  bus = gst_bus_new ();
  gst_element_set_bus (bin, bus);

  fail_unless (gst_element_set_state (bin,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);

  gst_bin_add (GST_BIN (bin), element);

  /* check the contexts are the same */
  contexts = gst_element_get_contexts (element);
  contexts2 = gst_element_get_contexts (bin);
  for (l = contexts; l; l = l->next)
    fail_unless (g_list_find (contexts2, l->data));
  g_list_free_full (contexts, (GDestroyNotify) gst_context_unref);
  g_list_free_full (contexts2, (GDestroyNotify) gst_context_unref);

  gst_element_set_bus (bin, NULL);
  fail_unless (gst_element_set_state (bin,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

GST_START_TEST (test_add_element_to_bin_collision)
{
  GstBus *bus;
  GstElement *bin;
  GstElement *element, *element2;
  GList *contexts, *contexts2, *l;

  /* Start with a bin containing an element that requests a context and then add
   * another element to the bin that has already requested the same context. */

  bin = gst_bin_new (NULL);
  element = g_object_new (gst_context_element_get_type (), NULL);
  gst_bin_add (GST_BIN (bin), element);

  ((GstContextElement *) element)->create_self = TRUE;

  bus = gst_bus_new ();
  gst_element_set_bus (bin, bus);

  fail_unless (gst_element_set_state (bin,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);

  fail_unless (((GstContextElement *) element)->have_foobar);

  /* propagate a context without a parent bin */
  element2 = g_object_new (gst_context_element_get_type (), NULL);
  ((GstContextElement *) element2)->create_self = TRUE;

  fail_unless (gst_element_set_state (element2,
          GST_STATE_READY) == GST_STATE_CHANGE_SUCCESS);

  fail_unless (((GstContextElement *) element2)->have_foobar);

  ((GstContextElement *) element)->have_foobar = FALSE;
  ((GstContextElement *) element2)->have_foobar = FALSE;

  /* add element to bin should result in the propagation of contexts to the
   * added element */
  gst_bin_add (GST_BIN (bin), element2);

  fail_unless (((GstContextElement *) element)->have_foobar == FALSE);
  fail_unless (((GstContextElement *) element2)->have_foobar);

  /* check the contexts are the same */
  contexts = gst_element_get_contexts (element);
  contexts2 = gst_element_get_contexts (element2);
  for (l = contexts; l; l = l->next)
    fail_unless (g_list_find (contexts2, l->data));
  g_list_free_full (contexts, (GDestroyNotify) gst_context_unref);
  contexts = gst_element_get_contexts (bin);
  for (l = contexts; l; l = l->next)
    fail_unless (g_list_find (contexts2, l->data));
  g_list_free_full (contexts, (GDestroyNotify) gst_context_unref);
  g_list_free_full (contexts2, (GDestroyNotify) gst_context_unref);

  gst_element_set_bus (bin, NULL);
  fail_unless (gst_element_set_state (bin,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);

  gst_object_unref (bus);
  gst_object_unref (bin);
}

GST_END_TEST;

static Suite *
gst_context_suite (void)
{
  Suite *s = suite_create ("GstContext");
  TCase *tc_chain = tcase_create ("context tests");

  tcase_set_timeout (tc_chain, 0);

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_basic);
  tcase_add_test (tc_chain, test_element_set_before_ready);
  tcase_add_test (tc_chain, test_element_set_from_need_context);
  tcase_add_test (tc_chain, test_element_create_self);
  tcase_add_test (tc_chain, test_element_bin_caching);
  tcase_add_test (tc_chain, test_add_element_to_bin);
  tcase_add_test (tc_chain, test_add_element_to_bin_collision);

  return s;
}

GST_CHECK_MAIN (gst_context);
