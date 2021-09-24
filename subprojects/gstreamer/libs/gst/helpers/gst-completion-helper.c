/* GStreamer
 * Copyright (C) 2015 Mathieu Duponchelle <mathieu.duponchelle@opencreed.com>
 *
 * gst-completion-helper.c: tool to let other tools enjoy fast and powerful
 * gstreamer-aware completion
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

#include <gst/gst.h>
#include <glib.h>
#include <stdlib.h>
#include <string.h>

static GList *
get_pad_templates_info (GstElement * element, GstElementFactory * factory,
    GstPadDirection direction)
{
  const GList *pads;
  GstStaticPadTemplate *padtemplate;
  GList *caps_list = NULL;

  if (gst_element_factory_get_num_pad_templates (factory) == 0) {
    g_print ("  none\n");
    return NULL;
  }

  pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    padtemplate = (GstStaticPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    if (padtemplate->direction != direction)
      continue;

    if (padtemplate->static_caps.string) {
      caps_list =
          g_list_append (caps_list,
          gst_static_caps_get (&padtemplate->static_caps));
    }

  }

  return caps_list;
}

static GList *
_get_pad_caps (const gchar * factory_name, GstPadDirection direction)
{
  GstElementFactory *factory = gst_element_factory_find (factory_name);
  GstElement *element = gst_element_factory_make (factory_name, NULL);

  if (!element)
    return NULL;
  if (!factory)
    return NULL;
  factory =
      GST_ELEMENT_FACTORY (gst_plugin_feature_load (GST_PLUGIN_FEATURE
          (factory)));
  if (!factory)
    return NULL;
  return get_pad_templates_info (element, factory, direction);
}

static gboolean
_are_linkable (GstPluginFeature * feature, GList * caps_list)
{
  gboolean print = FALSE;
  GstElementFactory *factory = GST_ELEMENT_FACTORY (feature);

  GList *tmp;
  print = FALSE;
  for (tmp = caps_list; tmp; tmp = tmp->next) {
    if (gst_element_factory_can_sink_any_caps (factory, tmp->data)) {
      print = TRUE;
      break;
    }
  }

  return print;
}

static gboolean
_belongs_to_klass (GstElementFactory * factory, const gchar * klass)
{
  const gchar *factory_klass;


  factory_klass =
      gst_element_factory_get_metadata (factory, GST_ELEMENT_METADATA_KLASS);
  if (strstr (factory_klass, klass))
    return TRUE;
  return FALSE;
}

static void
_list_features (const gchar * compatible_with, const gchar * klass,
    GstCaps * sinkcaps)
{
  GList *plugins, *orig_plugins;
  GList *caps_list = NULL;

  if (compatible_with) {
    caps_list = _get_pad_caps (compatible_with, GST_PAD_SRC);
  }

  orig_plugins = plugins = gst_registry_get_plugin_list (gst_registry_get ());
  while (plugins) {
    GList *features, *orig_features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);
    plugins = g_list_next (plugins);

    if (GST_OBJECT_FLAG_IS_SET (plugin, GST_PLUGIN_FLAG_BLACKLISTED)) {
      continue;
    }

    orig_features = features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get (),
        gst_plugin_get_name (plugin));
    while (features) {
      GstPluginFeature *feature;

      if (G_UNLIKELY (features->data == NULL))
        goto next;
      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        gboolean print = TRUE;
        if (caps_list)
          print = _are_linkable (feature, caps_list);
        if (print && klass)
          print = _belongs_to_klass (GST_ELEMENT_FACTORY (feature), klass);
        if (print && sinkcaps)
          print =
              gst_element_factory_can_sink_any_caps (GST_ELEMENT_FACTORY
              (feature), sinkcaps);

        if (print)
          g_print ("%s ", gst_plugin_feature_get_name (feature));
      }

    next:
      features = g_list_next (features);
    }

    gst_plugin_feature_list_free (orig_features);
  }

  g_list_free (caps_list);
  g_print ("\n");
  gst_plugin_list_free (orig_plugins);
}

static void
_print_element_properties_info (GstElement * element)
{
  GParamSpec **property_specs;
  guint num_properties, i;

  property_specs = g_object_class_list_properties
      (G_OBJECT_GET_CLASS (element), &num_properties);

  for (i = 0; i < num_properties; i++) {
    GParamSpec *param = property_specs[i];

    if (param->flags & G_PARAM_WRITABLE) {
      g_print ("%s= ", g_param_spec_get_name (param));
    }
  }

  g_free (property_specs);
}

static void
_list_element_properties (const gchar * factory_name)
{
  GstElement *element = gst_element_factory_make (factory_name, NULL);

  _print_element_properties_info (element);
}

int
main (int argc, char *argv[])
{
  gboolean list_features = FALSE;
  gchar *compatible_with = NULL;
  gchar *element = NULL;
  gchar *klass = NULL;
  gchar *caps_str = NULL;
  GstCaps *sinkcaps = NULL;
  gint exit_code = EXIT_SUCCESS;

  GOptionEntry options[] = {
    {"list-features", 'l', 0, G_OPTION_ARG_NONE, &list_features,
        "list all the available features", NULL},
    {"compatible-with", '\0', 0, G_OPTION_ARG_STRING, &compatible_with,
          "Only print the elements that could be queued after this feature name",
        NULL},
    {"element-properties", '\0', 0, G_OPTION_ARG_STRING, &element,
        "The element to list properties on", NULL},
    {"klass", '\0', 0, G_OPTION_ARG_STRING, &klass,
        "Only print the elements belonging to that klass", NULL},
    {"sinkcaps", '\0', 0, G_OPTION_ARG_STRING, &caps_str,
        "Only print the elements that can sink these caps", NULL},
    {NULL}
  };

  GOptionContext *ctx;
  GError *err = NULL;

  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    if (err)
      g_printerr ("Error initializing: %s\n", GST_STR_NULL (err->message));
    else
      g_printerr ("Error initializing: Unknown error!\n");
    g_clear_error (&err);
    g_option_context_free (ctx);
    exit (1);
  }
  g_option_context_free (ctx);

  if (caps_str) {
    sinkcaps = gst_caps_from_string (caps_str);
    if (!sinkcaps) {
      exit_code = EXIT_FAILURE;
      goto done;
    }
  }

  if (compatible_with || klass || sinkcaps) {
    _list_features (compatible_with, klass, sinkcaps);
    goto done;
  }

  if (element) {
    _list_element_properties (element);
    goto done;
  }

  if (list_features) {
    _list_features (NULL, NULL, NULL);
    goto done;
  }

done:
  if (sinkcaps)
    gst_caps_unref (sinkcaps);
  exit (exit_code);
}
