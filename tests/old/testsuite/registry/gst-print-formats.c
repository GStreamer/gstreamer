/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *               2000 Wim Taymans <wtay@chello.be>
 *               2004 Thomas Vander Stichele <thomas@apestaart.org>
 *
 * gst-inspect.c: tool to inspect the GStreamer registry
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/gst.h>

#include "gst/gst-i18n-app.h"

#include <string.h>
#include <locale.h>
#include <glib/gprintf.h>

#define static

static void
print_pad_templates_info (GstElement * element, GstElementFactory * factory,
    GstPadDirection dir)
{
  GstElementClass *gstelement_class;
  const GList *pads;
  GstPadTemplate *padtemplate;

  if (!factory->numpadtemplates) {
    return;
  }

  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  pads = factory->padtemplates;
  while (pads) {
    padtemplate = (GstPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    if (padtemplate->direction == dir) {
      if (padtemplate->caps) {
        GstStructure *structure;
        int i;

        for (i = 0; i < gst_caps_get_size (padtemplate->caps); i++) {
          structure = gst_caps_get_structure (padtemplate->caps, i);
          g_print ("    %s\n", gst_structure_get_name (structure));
        }
      }
    }
  }
}

static void
print_element_list (const char *klass, GstPadDirection dir)
{
  GList *plugins;

  g_print ("Elements in %s:\n", klass);
  for (plugins = gst_registry_pool_plugin_list (); plugins;
      plugins = g_list_next (plugins)) {
    GList *features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;
        GstElement *element;

        factory = GST_ELEMENT_FACTORY (feature);
        if (strncmp (factory->details.klass, klass, strlen (klass)) == 0) {
          g_print ("  %s: %s (%d)\n", GST_PLUGIN_FEATURE_NAME (factory),
              factory->details.longname, gst_plugin_feature_get_rank (feature));
          element = gst_element_factory_create (factory, NULL);
          print_pad_templates_info (element, factory, dir);
          gst_object_unref (element);
        }
      }

      features = g_list_next (features);
    }
  }
  g_print ("\n");
}

static void
print_typefind_list (void)
{
  GList *plugins;

  g_print ("Typefind list:\n");
  for (plugins = gst_registry_pool_plugin_list (); plugins;
      plugins = g_list_next (plugins)) {
    GList *features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_TYPE_FIND_FACTORY (feature)) {
        GstTypeFindFactory *factory;
        char *s;

        gst_plugin_load_file (plugin->filename, NULL);

        factory = GST_TYPE_FIND_FACTORY (feature);
        g_print ("  %s: (%d)\n", GST_PLUGIN_FEATURE_NAME (factory),
            gst_plugin_feature_get_rank (feature));
        s = gst_caps_to_string (gst_type_find_factory_get_caps (factory));
        g_print ("    %s\n", s);
        g_free (s);
      }

      features = g_list_next (features);
    }
  }
  g_print ("\n");
}

static int
list_sort_func (gconstpointer a, gconstpointer b)
{
  return strcmp ((const char *) a, (const char *) b);
}

static GList *
get_typefind_mime_list (void)
{
  GList *plugins;
  GList *mime_list = NULL;

  for (plugins = gst_registry_pool_plugin_list (); plugins;
      plugins = g_list_next (plugins)) {
    GList *features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_TYPE_FIND_FACTORY (feature)) {
        GstTypeFindFactory *factory;
        char *s;
        int i;
        const GstCaps *caps;

        factory = GST_TYPE_FIND_FACTORY (feature);
        caps = gst_type_find_factory_get_caps (factory);

        if (gst_plugin_feature_get_rank (feature) > 0 && caps != NULL) {
          for (i = 0; i < gst_caps_get_size (caps); i++) {
            const GstStructure *structure = gst_caps_get_structure (caps, i);

            s = g_strdup (gst_structure_get_name (structure));
            mime_list = g_list_prepend (mime_list, s);
          }
        }
      }

      features = g_list_next (features);
    }
  }

  return mime_list;
}

GList *
g_list_uniqify (GList * list)
{
  GList *item;

  for (item = g_list_first (list); item; item = g_list_next (item)) {
    GList *next_item = g_list_next (item);

    while (next_item && strcmp (item->data, next_item->data) == 0) {
      g_free (next_item->data);
      list = g_list_delete_link (list, next_item);
      next_item = g_list_next (item);
    }
  }

  return list;
}

static GList *
get_pad_templates_info (GstElement * element, GstElementFactory * factory,
    GstPadDirection dir)
{
  GstElementClass *gstelement_class;
  const GList *pads;
  GstPadTemplate *padtemplate;
  GList *mime_list = NULL;

  if (!factory->numpadtemplates) {
    return NULL;
  }

  gstelement_class = GST_ELEMENT_CLASS (G_OBJECT_GET_CLASS (element));

  pads = factory->padtemplates;
  while (pads) {
    padtemplate = (GstPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    if (padtemplate->direction == dir) {
      if (padtemplate->caps) {
        GstStructure *structure;
        int i;

        for (i = 0; i < gst_caps_get_size (padtemplate->caps); i++) {
          structure = gst_caps_get_structure (padtemplate->caps, i);
          mime_list = g_list_prepend (mime_list,
              g_strdup (gst_structure_get_name (structure)));
        }
      }
    }
  }
  return mime_list;
}

static GList *
get_element_mime_list (const char *klass, GstPadDirection dir)
{
  GList *mime_list = NULL;
  GList *plugins;

  for (plugins = gst_registry_pool_plugin_list (); plugins;
      plugins = g_list_next (plugins)) {
    GList *features;
    GstPlugin *plugin;

    plugin = (GstPlugin *) (plugins->data);

    features = gst_plugin_get_feature_list (plugin);
    while (features) {
      GstPluginFeature *feature;

      feature = GST_PLUGIN_FEATURE (features->data);

      if (GST_IS_ELEMENT_FACTORY (feature)) {
        GstElementFactory *factory;
        GstElement *element;

        factory = GST_ELEMENT_FACTORY (feature);
        if (strncmp (factory->details.klass, klass, strlen (klass)) == 0) {
          if (gst_plugin_feature_get_rank (feature) > 0) {
            GList *list;

            element = gst_element_factory_create (factory, NULL);
            list = get_pad_templates_info (element, factory, dir);
            mime_list = g_list_concat (mime_list, list);
            gst_object_unref (element);
          }
        }
      }

      features = g_list_next (features);
    }
  }

  return mime_list;
}

static void
print_mime_list (void)
{
  GList *list;
  GList *typefind_list;
  GList *item;
  GList *item2;

  typefind_list = get_typefind_mime_list ();
  typefind_list = g_list_sort (typefind_list, list_sort_func);
  typefind_list = g_list_uniqify (typefind_list);

  list = get_element_mime_list ("Codec/Demuxer", GST_PAD_SINK);
  list = g_list_concat (list, get_element_mime_list ("Codec/Decoder",
          GST_PAD_SINK));
  list = g_list_sort (list, list_sort_func);
  list = g_list_uniqify (list);

  g_print ("MIME media type list:\n");
  for (item = g_list_first (list); item; item = g_list_next (item)) {
    for (item2 = g_list_first (typefind_list); item2;
        item2 = g_list_next (item2)) {
      if (strcmp ((char *) item->data, (char *) item2->data) == 0) {
        g_print ("  %s\n", (char *) item->data);
      }
    }
  }
}


int
main (int argc, char *argv[])
{

#ifdef GETTEXT_PACKAGE
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  gst_init (&argc, &argv);

  print_element_list ("Codec/Demuxer", GST_PAD_SINK);
  print_element_list ("Codec/Decoder", GST_PAD_SINK);
  print_element_list ("Codec/Muxer", GST_PAD_SRC);
  print_element_list ("Codec/Encoder", GST_PAD_SRC);
  print_typefind_list ();
  print_mime_list ();

  return 0;
}
