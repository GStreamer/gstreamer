/* GStreamer Editing Services
 * Copyright (C) 2009 Edward Hervey <edward.hervey@collabora.co.uk>
 *               2009 Nokia Corporation
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

/**
 * SECTION: ges.h
 * @title: Initialization
 * @short_description: GStreamer editing services initialization functions
 *
 * GES needs to be initialized after GStreamer itself. This section
 * contains the various functions to do so.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <ges/ges.h>
#include "ges/gstframepositioner.h"
#include "ges/ges-smart-adder.h"
#include "ges/ges-smart-video-mixer.h"
#include "ges-internal.h"

#ifndef DISABLE_XPTV
#include <ges/ges-pitivi-formatter.h>
#endif

#define GES_GNONLIN_VERSION_NEEDED_MAJOR 1
#define GES_GNONLIN_VERSION_NEEDED_MINOR 2
#define GES_GNONLIN_VERSION_NEEDED_MICRO 0

GST_DEBUG_CATEGORY (_ges_debug_category);

G_LOCK_DEFINE_STATIC (init_lock);

/* (without holding ref) thread object for thread_self() validation
 * between init/deinit
 */
static GThread *initialized_thread = NULL;

#ifndef GST_DISABLE_GST_DEBUG
static gpointer
init_debug_category (gpointer data)
{
  /* initialize debugging category */
  GST_DEBUG_CATEGORY_INIT (_ges_debug_category, "ges", GST_DEBUG_FG_YELLOW,
      "GStreamer Editing Services");

  return _ges_debug_category;
}

GstDebugCategory *
_ges_debug (void)
{
  static GOnce my_once = G_ONCE_INIT;

  g_once (&my_once, init_debug_category, NULL);

  return my_once.retval;
}
#else
GstDebugCategory *
_ges_debug (void)
{
  return NULL;
}

#endif

static gboolean
ges_init_pre (GOptionContext * context, GOptionGroup * group, gpointer data,
    GError ** error)
{
  if (initialized_thread) {
    GST_DEBUG ("already initialized");
    return TRUE;
  }

  return TRUE;
}

static gboolean
ges_init_post (GOptionContext * context, GOptionGroup * group, gpointer data,
    GError ** error)
{
  GESUriClipAssetClass *uriasset_klass = NULL;
  GstElementFactory *nlecomposition_factory = NULL;
  static GstValueTable gstvtable = {
    G_TYPE_NONE,
    (GstValueCompareFunc) NULL,
    (GstValueSerializeFunc) ges_marker_list_serialize,
    (GstValueDeserializeFunc) ges_marker_list_deserialize
  };
  static gboolean marker_list_registered = FALSE;

  if (initialized_thread) {
    GST_DEBUG ("already initialized ges");
    return TRUE;
  }

  uriasset_klass = g_type_class_ref (GES_TYPE_URI_CLIP_ASSET);

  _init_formatter_assets ();
  if (!_ges_uri_asset_ensure_setup (uriasset_klass)) {
    GST_ERROR ("cannot setup uri asset");
    if (error)
      *error = g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN,
          "Cannot initialize URI asset class.");
    goto failed;
  }

  nlecomposition_factory = gst_element_factory_find ("nlecomposition");
  if (!nlecomposition_factory) {
    GST_ERROR ("The `nlecomposition` object was not found.");
    if (error)
      *error = g_error_new (GST_CORE_ERROR, GST_CORE_ERROR_MISSING_PLUGIN,
          "The `nle` plugin is missing.");

    goto failed;
  }
  gst_object_unref (nlecomposition_factory);

  /* register clip classes with the system */

  g_type_class_ref (GES_TYPE_TEST_CLIP);
  g_type_class_ref (GES_TYPE_URI_CLIP);
  g_type_class_ref (GES_TYPE_TITLE_CLIP);
  g_type_class_ref (GES_TYPE_TRANSITION_CLIP);
  g_type_class_ref (GES_TYPE_OVERLAY_CLIP);
  g_type_class_ref (GES_TYPE_OVERLAY_TEXT_CLIP);
  g_type_class_ref (GES_TYPE_EFFECT_CLIP);

  g_type_class_ref (GES_TYPE_GROUP);

  /* Register track elements */
  g_type_class_ref (GES_TYPE_EFFECT);

  ges_asset_cache_init ();

  gst_element_register (NULL, "gesaudiomixer", 0, GES_TYPE_SMART_ADDER);
  gst_element_register (NULL, "gescompositor", 0, GES_TYPE_SMART_MIXER);
  gst_element_register (NULL, "framepositioner", 0, GST_TYPE_FRAME_POSITIONNER);
  gst_element_register (NULL, "gespipeline", 0, GES_TYPE_PIPELINE);

  /* TODO: user-defined types? */
  initialized_thread = g_thread_self ();
  g_type_class_unref (uriasset_klass);

  if (!marker_list_registered) {
    gstvtable.type = GES_TYPE_MARKER_LIST;
    gst_value_register (&gstvtable);
    marker_list_registered = TRUE;
  }

  GST_DEBUG ("GStreamer Editing Services initialized");

  return TRUE;

failed:
  if (uriasset_klass)
    g_type_class_unref (uriasset_klass);

  GST_ERROR ("Could not initialize GES.");

  return FALSE;
}

/**
 * ges_init:
 *
 * Initialize the GStreamer Editing Service. Call this before any usage of
 * GES. You should take care of initilizing GStreamer before calling this
 * function.
 *
 * MT safety.
 * GStreamer Editing Services do not guarantee MT safety.
 * An application is required to use GES APIs (including ges_deinit())
 * in the thread where ges_init() was called.
 */

gboolean
ges_init (void)
{
  gboolean ret;

  G_LOCK (init_lock);
  ges_init_pre (NULL, NULL, NULL, NULL);

  ret = ges_init_post (NULL, NULL, NULL, NULL);
  G_UNLOCK (init_lock);

  return ret;
}

/**
 * ges_deinit:
 *
 * Clean up any resources created by GES in ges_init().
 *
 * It is normally not needed to call this function in a normal application as the
 * resources will automatically be freed when the program terminates.
 * This function is therefore mostly used by testsuites and other memory profiling tools.
 * This function should be called from the thread where ges_init() was called.
 *
 * After this call GES should not be used until another ges_init() call.
 */
void
ges_deinit (void)
{
  G_LOCK (init_lock);

  GST_INFO ("deinitializing GES");

  if (!initialized_thread) {
    GST_DEBUG ("nothing to deinitialize");
    G_UNLOCK (init_lock);
    return;
  }

  /* Allow deinit only from a thread where ges_init() was called */
  g_assert (initialized_thread == g_thread_self ());

  _ges_uri_asset_cleanup ();

  g_type_class_unref (g_type_class_peek (GES_TYPE_TEST_CLIP));
  g_type_class_unref (g_type_class_peek (GES_TYPE_URI_CLIP));
  g_type_class_unref (g_type_class_peek (GES_TYPE_TITLE_CLIP));
  g_type_class_unref (g_type_class_peek (GES_TYPE_TRANSITION_CLIP));
  g_type_class_unref (g_type_class_peek (GES_TYPE_OVERLAY_CLIP));
  g_type_class_unref (g_type_class_peek (GES_TYPE_OVERLAY_TEXT_CLIP));
  g_type_class_unref (g_type_class_peek (GES_TYPE_EFFECT_CLIP));

  g_type_class_unref (g_type_class_peek (GES_TYPE_GROUP));
  /* Register track elements */
  g_type_class_unref (g_type_class_peek (GES_TYPE_EFFECT));

  ges_asset_cache_deinit ();
  ges_xml_formatter_deinit ();

  initialized_thread = NULL;
  G_UNLOCK (init_lock);

  GST_INFO ("deinitialized GES");

  return;
}

#ifndef GST_DISABLE_OPTION_PARSING
static gboolean
parse_goption_arg (const gchar * s_opt,
    const gchar * arg, gpointer data, GError ** err)
{
  if (g_strcmp0 (s_opt, "--ges-version") == 0) {
    gst_print ("GStreamer Editing Services version %s\n", PACKAGE_VERSION);
    exit (0);
  } else if (g_strcmp0 (s_opt, "--ges-sample-paths") == 0) {
    ges_add_missing_uri_relocation_uri (arg, FALSE);
  } else if (g_strcmp0 (s_opt, "--ges-sample-path-recurse") == 0) {
    ges_add_missing_uri_relocation_uri (arg, TRUE);
  }

  return TRUE;
}
#endif

/**
 * ges_init_get_option_group: (skip)
 *
 * Returns a #GOptionGroup with GES's argument specifications. The
 * group is set up to use standard GOption callbacks, so when using this
 * group in combination with GOption parsing methods, all argument parsing
 * and initialization is automated.
 *
 * This function is useful if you want to integrate GES with other
 * libraries that use GOption (see g_option_context_add_group() ).
 *
 * If you use this function, you should make sure you initialise the GStreamer
 * as one of the very first things in your program. That means you need to
 * use gst_init_get_option_group() and add it to the option context before
 * using the ges_init_get_option_group() result.
 *
 * Returns: (transfer full): a pointer to GES's option group.
 */
GOptionGroup *
ges_init_get_option_group (void)
{
#ifndef GST_DISABLE_OPTION_PARSING

  GOptionGroup *group;
  static const GOptionEntry ges_args[] = {
    {"ges-version", 0, G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
          (gpointer) parse_goption_arg,
          "Print the GStreamer Editing Services version",
        NULL},
    {"ges-sample-paths", 0, 0, G_OPTION_ARG_CALLBACK,
          (gpointer) parse_goption_arg,
        "List of pathes to look assets in if they were moved"},
    {"ges-sample-path-recurse", 0, 0, G_OPTION_ARG_CALLBACK,
          (gpointer) parse_goption_arg,
        "Same as above, but recursing into the folder"},
    {NULL}
  };

  group = g_option_group_new ("GES", "GStreamer Editing Services Options",
      "Show GES Options", NULL, NULL);
  g_option_group_set_parse_hooks (group, (GOptionParseFunc) ges_init_pre,
      (GOptionParseFunc) ges_init_post);
  g_option_group_add_entries (group, ges_args);

  return group;

#else
  return NULL;
#endif
}

/**
 * ges_version:
 * @major: (out): pointer to a guint to store the major version number
 * @minor: (out): pointer to a guint to store the minor version number
 * @micro: (out): pointer to a guint to store the micro version number
 * @nano:  (out): pointer to a guint to store the nano version number
 *
 * Gets the version number of the GStreamer Editing Services library.
 */
void
ges_version (guint * major, guint * minor, guint * micro, guint * nano)
{
  g_return_if_fail (major);
  g_return_if_fail (minor);
  g_return_if_fail (micro);
  g_return_if_fail (nano);

  *major = GES_VERSION_MAJOR;
  *minor = GES_VERSION_MINOR;
  *micro = GES_VERSION_MICRO;
  *nano = GES_VERSION_NANO;
}

/**
 * ges_init_check:
 * @argc: (inout) (allow-none): pointer to application's argc
 * @argv: (inout) (array length=argc) (allow-none): pointer to application's argv
 * @err: pointer to a #GError to which a message will be posted on error
 *
 * Initializes the GStreamer Editing Services library, setting up internal path lists,
 * and loading evrything needed.
 *
 * This function will return %FALSE if GES could not be initialized
 * for some reason.
 *
 * Returns: %TRUE if GES could be initialized.
 */
gboolean
ges_init_check (int *argc, char **argv[], GError ** err)
{
#ifndef GST_DISABLE_OPTION_PARSING
  GOptionGroup *group;
  GOptionContext *ctx;
#endif
  gboolean res;

  G_LOCK (init_lock);

  if (initialized_thread) {
    GST_DEBUG ("already initialized ges");
    G_UNLOCK (init_lock);
    return TRUE;
  }
#ifndef GST_DISABLE_OPTION_PARSING
  ctx = g_option_context_new ("- GStreamer Editing Services initialization");
  g_option_context_set_ignore_unknown_options (ctx, TRUE);
  g_option_context_set_help_enabled (ctx, FALSE);
  group = ges_init_get_option_group ();
  g_option_context_add_group (ctx, group);
  res = g_option_context_parse (ctx, argc, argv, err);
  g_option_context_free (ctx);
#endif

  if (!res) {
    G_UNLOCK (init_lock);
    return res;
  }

  ges_init_pre (NULL, NULL, NULL, NULL);
  res = ges_init_post (NULL, NULL, NULL, NULL);

  G_UNLOCK (init_lock);

  return res;
}

/**
 * ges_is_initialized:
 *
 * Use this function to check if GES has been initialized with ges_init()
 * or ges_init_check().
 *
 * Returns: %TRUE if initialization has been done, %FALSE otherwise.
 *
 * Since: 1.16
 */
gboolean
ges_is_initialized (void)
{
  return initialized_thread ? TRUE : FALSE;
}
