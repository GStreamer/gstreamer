/* switchbin
 * Copyright (C) 2016  Carlos Rafael Giani
 *
 * gstswitchbin.c: Element for switching between paths based on input caps
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
 * SECTION:element-switchbin
 * @title: switchbin
 *
 * switchbin is a helper element which chooses between a set of
 * processing chains (paths) based on input caps, and changes if new caps
 * arrive. Paths are child objects, which are accessed by the
 * #GstChildProxy interface.
 *
 * Whenever new input caps are encountered at the switchbin's sinkpad,
 * the * first path with matching caps is picked. The paths are looked at
 * in order: path \#0's caps are looked at first, checked against the new
 * input caps with gst_caps_can_intersect(), and if its return value
 * is TRUE, path \#0 is picked. Otherwise, path \#1's caps are looked at etc.
 * If no path matches, an error is reported.
 *
 * <refsect2>
 * <title>Example launch line</title>
 *
 * In this example, if the data is raw PCM audio with 44.1 kHz, a volume
 * element is used for reducing the audio volume to 10%. Otherwise, it is
 * just passed through. So, a 44.1 kHz MP3 will sound quiet, a 48 kHz MP3
 * will be at full volume.
 *
 * |[
 *   gst-launch-1.0 uridecodebin uri=<URI> ! switchbin num-paths=2 \
 *     path0::element="audioconvert ! volume volume=0.1" path0::caps="audio/x-raw, rate=44100" \
 *     path1::element="identity" path1::caps="ANY" ! \
 *     autoaudiosink
 * ]|
 *
 * This example's path \#1 is a fallback "catch-all" path. Its caps are "ANY" caps,
 * so any input caps will match against this. A catch-all path with an identity element
 * is useful for cases where certain kinds of processing should only be done for specific
 * formats, like the example above (it applies volume only to 44.1 kHz PCM audio).
 * </refsect2>
 *
 */

#include <string.h>
#include <stdio.h>
#include "gstswitchbin.h"


GST_DEBUG_CATEGORY (switch_bin_debug);
#define GST_CAT_DEFAULT switch_bin_debug

enum
{
  PROP_0,
  PROP_NUM_PATHS,
  PROP_CURRENT_PATH,
  PROP_LAST
};

#define DEFAULT_NUM_PATHS 0
GParamSpec *switchbin_props[PROP_LAST];

#define PATH_LOCK(obj) g_mutex_lock(&(GST_SWITCH_BIN_CAST (obj)->path_mutex))
#define PATH_UNLOCK(obj) g_mutex_unlock(&(GST_SWITCH_BIN_CAST (obj)->path_mutex))
#define PATH_UNLOCK_AND_CHECK(obj) gst_switch_bin_unlock_paths_and_notify(GST_SWITCH_BIN_CAST (obj));


static GstStaticPadTemplate static_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate static_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);




static void gst_switch_bin_child_proxy_iface_init (gpointer iface,
    gpointer iface_data);
static GObject *gst_switch_bin_child_proxy_get_child_by_index (GstChildProxy *
    child_proxy, guint index);
static guint gst_switch_bin_child_proxy_get_children_count (GstChildProxy *
    child_proxy);


G_DEFINE_TYPE_WITH_CODE (GstSwitchBin,
    gst_switch_bin,
    GST_TYPE_BIN,
    G_IMPLEMENT_INTERFACE (GST_TYPE_CHILD_PROXY,
        gst_switch_bin_child_proxy_iface_init)
    );
GST_ELEMENT_REGISTER_DEFINE (switchbin, "switchbin", GST_RANK_NONE,
    gst_switch_bin_get_type ());

static void gst_switch_bin_unlock_paths_and_notify (GstSwitchBin * switchbin);

static void gst_switch_bin_dispose (GObject * object);
static void gst_switch_bin_finalize (GObject * object);
static void gst_switch_bin_set_property (GObject * object, guint prop_id,
    GValue const *value, GParamSpec * pspec);
static void gst_switch_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_switch_bin_sink_event (GstPad * pad,
    GstObject * parent, GstEvent * event);
static gboolean gst_switch_bin_sink_query (GstPad * pad,
    GstObject * parent, GstQuery * query);
static gboolean gst_switch_bin_src_query (GstPad * pad, GstObject * parent,
    GstQuery * query);

static gboolean gst_switch_bin_set_num_paths (GstSwitchBin * switch_bin,
    guint new_num_paths);
static gboolean gst_switch_bin_select_path_for_caps (GstSwitchBin *
    switch_bin, GstCaps * caps);
static gboolean gst_switch_bin_switch_to_path (GstSwitchBin * switch_bin,
    GstSwitchBinPath * switch_bin_path);
static GstSwitchBinPath *gst_switch_bin_find_matching_path (GstSwitchBin *
    switch_bin, GstCaps const *caps);

static void gst_switch_bin_set_sinkpad_block (GstSwitchBin * switch_bin,
    gboolean do_block);
static GstPadProbeReturn gst_switch_bin_blocking_pad_probe (GstPad * pad,
    GstPadProbeInfo * info, gpointer user_data);

static GstCaps *gst_switch_bin_get_allowed_caps (GstSwitchBin * switch_bin,
    GstPad * switch_bin_pad, gchar const *pad_name, GstCaps * filter);
static gboolean gst_switch_bin_are_caps_acceptable (GstSwitchBin *
    switch_bin, GstCaps const *caps);

static void
gst_switch_bin_unlock_paths_and_notify (GstSwitchBin * switchbin)
{
  gboolean do_notify = switchbin->path_changed;
  switchbin->path_changed = FALSE;
  PATH_UNLOCK (switchbin);

  if (do_notify)
    g_object_notify_by_pspec (G_OBJECT (switchbin),
        switchbin_props[PROP_CURRENT_PATH]);
}

static void
gst_switch_bin_child_proxy_iface_init (gpointer iface,
    G_GNUC_UNUSED gpointer iface_data)
{
  GstChildProxyInterface *child_proxy_iface = iface;

  child_proxy_iface->get_child_by_index =
      GST_DEBUG_FUNCPTR (gst_switch_bin_child_proxy_get_child_by_index);
  child_proxy_iface->get_children_count =
      GST_DEBUG_FUNCPTR (gst_switch_bin_child_proxy_get_children_count);
}


static GObject *
gst_switch_bin_child_proxy_get_child_by_index (GstChildProxy * child_proxy,
    guint index)
{
  GObject *result;
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (child_proxy);

  PATH_LOCK (switch_bin);

  if (G_UNLIKELY (index >= switch_bin->num_paths))
    result = NULL;
  else
    result = g_object_ref (G_OBJECT (switch_bin->paths[index]));

  PATH_UNLOCK (switch_bin);

  return result;
}


static guint
gst_switch_bin_child_proxy_get_children_count (GstChildProxy * child_proxy)
{
  guint result;
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (child_proxy);

  PATH_LOCK (switch_bin);
  result = switch_bin->num_paths;
  PATH_UNLOCK (switch_bin);

  return result;
}



static void
gst_switch_bin_class_init (GstSwitchBinClass * klass)
{
  GObjectClass *object_class;
  GstElementClass *element_class;

  GST_DEBUG_CATEGORY_INIT (switch_bin_debug, "switchbin", 0, "switch bin");

  object_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&static_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&static_src_template));

  object_class->dispose = GST_DEBUG_FUNCPTR (gst_switch_bin_dispose);
  object_class->finalize = GST_DEBUG_FUNCPTR (gst_switch_bin_finalize);
  object_class->set_property = GST_DEBUG_FUNCPTR (gst_switch_bin_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_switch_bin_get_property);

  /**
   * GstSwitchBin:num-paths
   *
   * Configure how many paths the switchbin will be choosing between. Attempting
   * to configure a path outside the range 0..(n-1) will fail. Reducing the
   * number of paths will release any paths outside the new range, which might
   * trigger activation of a new path by re-assessing the current caps.
   */
  switchbin_props[PROP_NUM_PATHS] = g_param_spec_uint ("num-paths",
      "Number of paths", "Number of paths", 0, G_MAXUINT - 1,
      DEFAULT_NUM_PATHS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_NUM_PATHS, switchbin_props[PROP_NUM_PATHS]);

  /**
   * GstSwitchBin:current-path
   *
   * Returns the currently selected path number. If there is no current
   * path (due to no caps, or unsupported caps), the value is #G_MAXUINT. Read-only.
   */
  switchbin_props[PROP_CURRENT_PATH] =
      g_param_spec_uint ("current-path", "Current Path",
      "Currently selected path", 0, G_MAXUINT,
      0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class,
      PROP_CURRENT_PATH, switchbin_props[PROP_CURRENT_PATH]);

  gst_element_class_set_static_metadata (element_class,
      "switchbin",
      "Generic/Bin",
      "Switch between sub-pipelines (paths) based on input caps",
      "Carlos Rafael Giani <dv@pseudoterminal.org>");
}


static void
gst_switch_bin_init (GstSwitchBin * switch_bin)
{
  GstPad *pad;

  switch_bin->num_paths = DEFAULT_NUM_PATHS;
  switch_bin->paths = NULL;
  switch_bin->current_path = NULL;
  switch_bin->last_stream_start = NULL;
  switch_bin->blocking_probe_id = 0;
  switch_bin->drop_probe_id = 0;
  switch_bin->last_caps = NULL;

  switch_bin->sinkpad = gst_ghost_pad_new_no_target_from_template ("sink",
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (switch_bin),
          "sink")
      );
  gst_element_add_pad (GST_ELEMENT (switch_bin), switch_bin->sinkpad);

  switch_bin->srcpad = gst_ghost_pad_new_no_target_from_template ("src",
      gst_element_class_get_pad_template (GST_ELEMENT_GET_CLASS (switch_bin),
          "src")
      );
  gst_element_add_pad (GST_ELEMENT (switch_bin), switch_bin->srcpad);

  gst_pad_set_event_function (switch_bin->sinkpad, gst_switch_bin_sink_event);
  gst_pad_set_query_function (switch_bin->sinkpad, gst_switch_bin_sink_query);
  gst_pad_set_query_function (switch_bin->srcpad, gst_switch_bin_src_query);

  switch_bin->input_identity =
      gst_element_factory_make ("identity", "input-identity");

  gst_bin_add (GST_BIN (switch_bin), switch_bin->input_identity);
  pad = gst_element_get_static_pad (switch_bin->input_identity, "sink");
  gst_ghost_pad_set_target (GST_GHOST_PAD (switch_bin->sinkpad), pad);
  gst_object_unref (GST_OBJECT (pad));
}

static void
gst_switch_bin_dispose (GObject * object)
{
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (object);
  guint i;

  /* Chaining up will release all children of the bin,
   * invalidating any pointer to elements in the paths, so make sure
   * and clear those first */
  PATH_LOCK (switch_bin);
  for (i = 0; i < switch_bin->num_paths; ++i) {
    if (switch_bin->paths[i])
      switch_bin->paths[i]->element = NULL;
  }
  PATH_UNLOCK (switch_bin);
  G_OBJECT_CLASS (gst_switch_bin_parent_class)->dispose (object);
}

static void
gst_switch_bin_finalize (GObject * object)
{
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (object);
  guint i;

  if (switch_bin->last_caps != NULL)
    gst_caps_unref (switch_bin->last_caps);
  if (switch_bin->last_stream_start != NULL)
    gst_event_unref (switch_bin->last_stream_start);

  for (i = 0; i < switch_bin->num_paths; ++i) {
    gst_object_unparent (GST_OBJECT (switch_bin->paths[i]));
  }
  g_free (switch_bin->paths);

  G_OBJECT_CLASS (gst_switch_bin_parent_class)->finalize (object);
}


static void
gst_switch_bin_set_property (GObject * object, guint prop_id,
    GValue const *value, GParamSpec * pspec)
{
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (object);
  switch (prop_id) {
    case PROP_NUM_PATHS:
      PATH_LOCK (switch_bin);
      gst_switch_bin_set_num_paths (switch_bin, g_value_get_uint (value));
      PATH_UNLOCK_AND_CHECK (switch_bin);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_switch_bin_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (object);
  switch (prop_id) {
    case PROP_NUM_PATHS:
      PATH_LOCK (switch_bin);
      g_value_set_uint (value, switch_bin->num_paths);
      PATH_UNLOCK_AND_CHECK (switch_bin);
      break;
    case PROP_CURRENT_PATH:
      PATH_LOCK (switch_bin);
      if (switch_bin->current_path) {
        guint i;
        for (i = 0; i < switch_bin->num_paths; ++i) {
          if (switch_bin->paths[i] == switch_bin->current_path) {
            g_value_set_uint (value, i);
            break;
          }
        }
      } else {
        /* No valid path, return MAXUINT */
        g_value_set_uint (value, G_MAXUINT);
      }
      PATH_UNLOCK (switch_bin);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
gst_switch_bin_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (parent);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_STREAM_START:
    {
      GST_DEBUG_OBJECT (switch_bin,
          "stream-start event observed; copying it for later use");
      gst_event_replace (&(switch_bin->last_stream_start), event);

      return gst_pad_event_default (pad, parent, event);
    }

    case GST_EVENT_CAPS:
    {
      /* Intercept the caps event to switch to an appropriate path, then
       * resume default caps event processing */

      GstCaps *caps;
      gboolean ret;

      gst_event_parse_caps (event, &caps);
      GST_DEBUG_OBJECT (switch_bin,
          "sink pad got caps event with caps %" GST_PTR_FORMAT
          " ; looking for matching path", (gpointer) caps);

      PATH_LOCK (switch_bin);
      ret = gst_switch_bin_select_path_for_caps (switch_bin, caps);
      PATH_UNLOCK_AND_CHECK (switch_bin);

      if (!ret) {
        gst_event_unref (event);
        return FALSE;
      } else
        return gst_pad_event_default (pad, parent, event);
    }

    default:
      GST_DEBUG_OBJECT (switch_bin, "sink event: %s",
          gst_event_type_get_name (GST_EVENT_TYPE (event)));
      return gst_pad_event_default (pad, parent, event);
  }
}


static gboolean
gst_switch_bin_handle_query (GstPad * pad, GstObject * parent, GstQuery * query,
    char const *pad_name)
{
  GstSwitchBin *switch_bin = GST_SWITCH_BIN (parent);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:
    {
      GstCaps *filter, *caps;

      gst_query_parse_caps (query, &filter);

      PATH_LOCK (switch_bin);

      if (switch_bin->num_paths == 0) {
        /* No paths exist - cannot return any caps */
        caps = NULL;
      } else if ((switch_bin->current_path == NULL)
          || (switch_bin->current_path->element == NULL)) {
        /* Paths exist, but there is no current path (or the path is a dropping path,
         * so no element exists) - just return all allowed caps */
        caps =
            gst_switch_bin_get_allowed_caps (switch_bin, pad, pad_name, filter);
      } else {
        /* Paths exist and there is a current path
         * Forward the query to its element */

        GstQuery *caps_query = gst_query_new_caps (NULL);
        GstPad *element_pad =
            gst_element_get_static_pad (switch_bin->current_path->element,
            pad_name);

        caps = NULL;
        if (gst_pad_query (element_pad, caps_query)) {
          GstCaps *query_caps;
          gst_query_parse_caps_result (caps_query, &query_caps);
          caps = gst_caps_copy (query_caps);
        }

        gst_query_unref (caps_query);
        gst_object_unref (GST_OBJECT (element_pad));
      }


      PATH_UNLOCK (switch_bin);

      if (caps != NULL) {
        GST_DEBUG_OBJECT (switch_bin, "%s caps query:  caps: %" GST_PTR_FORMAT,
            pad_name, (gpointer) caps);
        gst_query_set_caps_result (query, caps);
        gst_caps_unref (caps);
        return TRUE;
      } else
        return FALSE;
    }

    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;
      gboolean acceptable;

      gst_query_parse_accept_caps (query, &caps);
      PATH_LOCK (switch_bin);
      acceptable = gst_switch_bin_are_caps_acceptable (switch_bin, caps);
      PATH_UNLOCK (switch_bin);
      GST_DEBUG_OBJECT (switch_bin,
          "%s accept_caps query:  acceptable: %d  caps: %" GST_PTR_FORMAT,
          pad_name, (gint) acceptable, (gpointer) caps);

      gst_query_set_accept_caps_result (query, acceptable);
      return TRUE;
    }

    default:
      return gst_pad_query_default (pad, parent, query);
  }
}


static gboolean
gst_switch_bin_sink_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  return gst_switch_bin_handle_query (pad, parent, query, "sink");
}


static gboolean
gst_switch_bin_src_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  return gst_switch_bin_handle_query (pad, parent, query, "src");
}


static gboolean
gst_switch_bin_set_num_paths (GstSwitchBin * switch_bin, guint new_num_paths)
{
  guint i;
  gboolean cur_path_removed = FALSE;

  /* must be called with path lock held */

  if (switch_bin->num_paths == new_num_paths) {
    GST_DEBUG_OBJECT (switch_bin,
        "no change in number of paths - ignoring call");
    return TRUE;
  } else if (switch_bin->num_paths < new_num_paths) {
    /* New number of paths is larger -> N new paths need to be created & added,
     * where N = new_num_paths - switch_bin->num_paths. */

    GST_DEBUG_OBJECT (switch_bin, "adding %u new paths",
        new_num_paths - switch_bin->num_paths);

    switch_bin->paths =
        g_realloc (switch_bin->paths, sizeof (GstObject *) * new_num_paths);

    for (i = switch_bin->num_paths; i < new_num_paths; ++i) {
      GstSwitchBinPath *path;
      gchar *path_name;

      /* default names would be something like "switchbinpath0" */
      path_name = g_strdup_printf ("path%u", i);
      switch_bin->paths[i] = path =
          g_object_new (gst_switch_bin_path_get_type (), "name", path_name,
          NULL);
      path->bin = switch_bin;

      gst_object_set_parent (GST_OBJECT (path), GST_OBJECT (switch_bin));
      gst_child_proxy_child_added (GST_CHILD_PROXY (switch_bin),
          G_OBJECT (path), path_name);

      GST_DEBUG_OBJECT (switch_bin, "added path #%u \"%s\" (%p)", i, path_name,
          (gpointer) path);
      g_free (path_name);
    }
  } else {
    /* New number of paths is smaller -> the last N paths need to be removed,
     * where N = switch_bin->num_paths - new_num_paths. If one of the paths
     * that are being removed is the current path, then a new current path
     * is selected. */
    gchar *path_name;

    GST_DEBUG_OBJECT (switch_bin, "removing the last %u paths",
        switch_bin->num_paths - new_num_paths);

    for (i = new_num_paths; i < switch_bin->num_paths; ++i) {
      GstSwitchBinPath *path = switch_bin->paths[i];
      path_name = g_strdup (GST_OBJECT_NAME (path));

      if (path == switch_bin->current_path) {
        cur_path_removed = TRUE;
        gst_switch_bin_switch_to_path (switch_bin, NULL);

        GST_DEBUG_OBJECT (switch_bin,
            "path #%u \"%s\" (%p) is the current path - selecting a new current path will be necessary",
            i, path_name, (gpointer) (switch_bin->paths[i]));
      }

      gst_child_proxy_child_removed (GST_CHILD_PROXY (switch_bin),
          G_OBJECT (path), path_name);
      gst_object_unparent (GST_OBJECT (switch_bin->paths[i]));

      GST_DEBUG_OBJECT (switch_bin, "removed path #%u \"%s\" (%p)", i,
          path_name, (gpointer) (switch_bin->paths[i]));
      g_free (path_name);
    }

    switch_bin->paths =
        g_realloc (switch_bin->paths, sizeof (GstObject *) * new_num_paths);
  }

  switch_bin->num_paths = new_num_paths;

  if (new_num_paths > 0) {
    if (cur_path_removed) {
      /* Select a new current path if the previous one was removed above */

      if (switch_bin->last_caps != NULL) {
        GST_DEBUG_OBJECT (switch_bin,
            "current path was removed earlier - need to select a new one based on the last caps %"
            GST_PTR_FORMAT, (gpointer) (switch_bin->last_caps));
        return gst_switch_bin_select_path_for_caps (switch_bin,
            switch_bin->last_caps);
      } else {
        /* This should not happen. Every time a current path is selected, the
         * caps that were used for the selection are copied as the last_caps.
         * So, if a current path exists, but last_caps is NULL, it indicates
         * a bug. For example, if the current path was selected without calling
         * gst_switch_bin_select_path_for_caps(). */
        g_assert_not_reached ();
        return FALSE;           /* shuts up compiler warning */
      }
    } else
      return TRUE;
  } else
    return gst_switch_bin_switch_to_path (switch_bin, NULL);
}


static gboolean
gst_switch_bin_select_path_for_caps (GstSwitchBin * switch_bin, GstCaps * caps)
{
  /* must be called with path lock held */

  gboolean ret;
  GstSwitchBinPath *path;

  path = gst_switch_bin_find_matching_path (switch_bin, caps);
  if (path == NULL) {
    /* No matching path found, the caps are incompatible. Report this and exit. */

    GST_ELEMENT_ERROR (switch_bin, STREAM, WRONG_TYPE,
        ("could not find compatible path"), ("sink caps: %" GST_PTR_FORMAT,
            (gpointer) caps));
    ret = FALSE;
  } else {
    /* Matching path found. Try to switch to it. */

    GST_DEBUG_OBJECT (switch_bin, "found matching path \"%s\" (%p) - switching",
        GST_OBJECT_NAME (path), (gpointer) path);
    ret = gst_switch_bin_switch_to_path (switch_bin, path);
  }

  if (ret && (caps != switch_bin->last_caps))
    gst_caps_replace (&(switch_bin->last_caps), caps);

  return ret;
}


static gboolean
gst_switch_bin_switch_to_path (GstSwitchBin * switch_bin,
    GstSwitchBinPath * switch_bin_path)
{
  /* must be called with path lock held */

  gboolean ret = TRUE;

  if (switch_bin_path != NULL)
    GST_DEBUG_OBJECT (switch_bin, "switching to path \"%s\" (%p)",
        GST_OBJECT_NAME (switch_bin_path), (gpointer) switch_bin_path);
  else
    GST_DEBUG_OBJECT (switch_bin,
        "switching to NULL path (= disabling current path)");

  /* No current path set and no path is to be set -> nothing to do */
  if ((switch_bin_path == NULL) && (switch_bin->current_path == NULL))
    return TRUE;

  /* If this path is already the current one, do nothing */
  if (switch_bin->current_path == switch_bin_path)
    return TRUE;

  /* Block incoming data to be able to safely switch */
  gst_switch_bin_set_sinkpad_block (switch_bin, TRUE);

  /* Unlink the current path's element (if there is a current path) */
  if (switch_bin->current_path != NULL) {
    GstSwitchBinPath *cur_path = switch_bin->current_path;

    if (cur_path->element != NULL) {
      gst_element_set_state (cur_path->element, GST_STATE_NULL);
      gst_element_unlink (switch_bin->input_identity, cur_path->element);
    }

    gst_ghost_pad_set_target (GST_GHOST_PAD (switch_bin->srcpad), NULL);

    switch_bin->current_path = NULL;
    switch_bin->path_changed = TRUE;
  }

  /* Link the new path's element (if a new path is specified) */
  if (switch_bin_path != NULL) {
    if (switch_bin_path->element != NULL) {
      GstPad *pad;

      /* There is a path element. Link it into the pipeline. Data passes through
       * it now, since its associated path just became the current one. */

      /* TODO: currently, only elements with one "src" "sink" always-pad are supported;
       * add support for request and sometimes pads */

      pad = gst_element_get_static_pad (switch_bin_path->element, "src");
      if (pad == NULL) {
        GST_ERROR_OBJECT (switch_bin,
            "path element has no static srcpad - cannot link");
        ret = FALSE;
        goto finish;
      }

      if (!gst_ghost_pad_set_target (GST_GHOST_PAD (switch_bin->srcpad), pad)) {
        GST_ERROR_OBJECT (switch_bin,
            "could not set the path element's srcpad as the ghost srcpad's target");
        ret = FALSE;
      }

      gst_object_unref (GST_OBJECT (pad));

      if (!ret)
        goto finish;

      if (!gst_element_link (switch_bin->input_identity,
              switch_bin_path->element)) {
        GST_ERROR_OBJECT (switch_bin,
            "linking the path element's sinkpad failed ; check if the path element's sink caps and the upstream elements connected to the switchbin's sinkpad match");
        ret = FALSE;
        goto finish;
      }

      /* Unlock the element's state in case it was locked earlier
       * so its state can be synced to the switchbin's */
      gst_element_set_locked_state (switch_bin_path->element, FALSE);
      if (!gst_element_sync_state_with_parent (switch_bin_path->element)) {
        GST_ERROR_OBJECT (switch_bin,
            "could not sync the path element's state with that of the switchbin");
        ret = FALSE;
        goto finish;
      }
    } else {
      GstPad *srcpad;

      /* There is no path element. This will probably yield an error
       * into the pipeline unless we're shutting down */
      GST_DEBUG_OBJECT (switch_bin, "path has no element ; will drop data");

      srcpad = gst_element_get_static_pad (switch_bin->input_identity, "src");

      g_assert (srcpad != NULL);

      if (!gst_ghost_pad_set_target (GST_GHOST_PAD (switch_bin->srcpad),
              srcpad)) {
        GST_ERROR_OBJECT (switch_bin,
            "could not set the path element's srcpad as the ghost srcpad's target");
        ret = FALSE;
      }

      GST_DEBUG_OBJECT (switch_bin,
          "pushing stream-start downstream before disabling");
      gst_element_send_event (switch_bin->input_identity,
          gst_event_ref (switch_bin->last_stream_start));

      gst_object_unref (GST_OBJECT (srcpad));
    }
  }

  switch_bin->current_path = switch_bin_path;
  switch_bin->path_changed = TRUE;

  /* If there is a new path to use, unblock the input */
  if (switch_bin_path != NULL)
    gst_switch_bin_set_sinkpad_block (switch_bin, FALSE);

finish:
  return ret;
}


static GstSwitchBinPath *
gst_switch_bin_find_matching_path (GstSwitchBin * switch_bin,
    GstCaps const *caps)
{
  /* must be called with path lock held */

  guint i;

  for (i = 0; i < switch_bin->num_paths; ++i) {
    GstSwitchBinPath *path = switch_bin->paths[i];
    if (gst_caps_can_intersect (caps, path->caps))
      return path;
  }

  return NULL;
}


static GstCaps *
gst_switch_bin_get_allowed_caps (GstSwitchBin * switch_bin,
    GstPad * switch_bin_pad, gchar const *pad_name, GstCaps * filter)
{
  /* must be called with path lock held */

  guint i;
  GstCaps *total_path_caps;
  gboolean is_sink_pad =
      (gst_pad_get_direction (switch_bin_pad) == GST_PAD_SINK);

  /* The allowed caps are a combination of the caps of all paths, the
   * filter caps, and the allowed caps as indicated by the result
   * of the CAPS query on the current path's element.
   * Since the CAPS query result can be influenced by an element's
   * current state and link to other elements, the non-current
   * path elements are not queried.
   *
   * In theory, it would be enough to just append all path caps. However,
   * to refine this a bit further, in case of the current path, the
   * path caps are first intersected with the result of the CAPS query.
   * This narrows down the acceptable caps for this current path,
   * hopefully providing better quality caps. */

  if (switch_bin->num_paths == 0) {
    /* No paths exist, so nothing can be returned */
    GST_ELEMENT_ERROR (switch_bin, STREAM, FAILED, ("no paths defined"),
        ("there must be at least one path in order for switchbin to do anything"));
    return NULL;
  }

  total_path_caps = gst_caps_new_empty ();

  for (i = 0; i < switch_bin->num_paths; ++i) {
    GstSwitchBinPath *path = switch_bin->paths[i];

    if (path->element != NULL) {
      GstPad *pad;
      GstCaps *caps, *intersected_caps;
      GstQuery *caps_query = NULL;

      pad = gst_element_get_static_pad (path->element, pad_name);
      caps_query = gst_query_new_caps (NULL);

      /* Query the path element for allowed caps. If this is
       * successful, intersect the returned caps with the path caps for the sink pad,
       * and append the result of the intersection to the total_path_caps,
       * or just append the result to the total_path_caps if collecting srcpad caps. */
      if (gst_pad_query (pad, caps_query)) {
        gst_query_parse_caps_result (caps_query, &caps);
        if (is_sink_pad) {
          intersected_caps = gst_caps_intersect (caps, path->caps);
        } else {
          intersected_caps = gst_caps_copy (caps);
        }
        gst_caps_append (total_path_caps, intersected_caps);
      } else if (is_sink_pad) {
        /* Just assume the sink pad has the path caps if the query failed */
        gst_caps_append (total_path_caps, gst_caps_ref (path->caps));
      }

      gst_object_unref (GST_OBJECT (pad));
      gst_query_unref (caps_query);
    } else {
      /* This is a path with no element (= is a dropping path),
       * If querying the sink caps, append the path
       * input caps, otherwise the output caps can be ANY */
      if (is_sink_pad)
        gst_caps_append (total_path_caps, gst_caps_ref (path->caps));
      else
        gst_caps_append (total_path_caps, gst_caps_new_any ());
    }
  }

  /* Apply filter caps if present */
  if (filter != NULL) {
    GstCaps *tmp_caps = total_path_caps;
    total_path_caps = gst_caps_intersect (tmp_caps, filter);
    gst_caps_unref (tmp_caps);
  }

  return total_path_caps;
}


static gboolean
gst_switch_bin_are_caps_acceptable (GstSwitchBin * switch_bin,
    GstCaps const *caps)
{
  /* must be called with path lock held */

  return (gst_switch_bin_find_matching_path (switch_bin, caps) != NULL);
}


static void
gst_switch_bin_set_sinkpad_block (GstSwitchBin * switch_bin, gboolean do_block)
{
  GstPad *pad;

  if ((do_block && (switch_bin->blocking_probe_id != 0)) || (!do_block
          && (switch_bin->blocking_probe_id == 0)))
    return;

  pad = gst_element_get_static_pad (switch_bin->input_identity, "sink");

  if (do_block) {
    switch_bin->blocking_probe_id =
        gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM,
        gst_switch_bin_blocking_pad_probe, NULL, NULL);
  } else {
    gst_pad_remove_probe (pad, switch_bin->blocking_probe_id);
    switch_bin->blocking_probe_id = 0;
  }

  GST_DEBUG_OBJECT (switch_bin, "sinkpad block enabled: %d", do_block);

  gst_object_unref (GST_OBJECT (pad));
}


static GstPadProbeReturn
gst_switch_bin_blocking_pad_probe (G_GNUC_UNUSED GstPad * pad,
    GstPadProbeInfo * info, G_GNUC_UNUSED gpointer user_data)
{
  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT (info);
    switch (GST_EVENT_TYPE (event)) {
      case GST_EVENT_CAPS:
      case GST_EVENT_STREAM_START:
        return GST_PAD_PROBE_PASS;
      default:
        break;
    }
  }

  return GST_PAD_PROBE_OK;
}

/************ GstSwitchBinPath ************/


typedef struct _GstSwitchBinPathClass GstSwitchBinPathClass;


#define GST_TYPE_SWITCH_BIN_PATH             (gst_switch_bin_path_get_type())
#define GST_SWITCH_BIN_PATH(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SWITCH_BIN_PATH, GstSwitchBinPath))
#define GST_SWITCH_BIN_PATH_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SWITCH_BIN_PATH, GstSwitchBinPathClass))
#define GST_SWITCH_BIN_PATH_CAST(obj)        ((GstSwitchBinPath *)(obj))
#define GST_IS_SWITCH_BIN_PATH(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SWITCH_BIN_PATH))
#define GST_IS_SWITCH_BIN_PATH_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SWITCH_BIN_PATH))


struct _GstSwitchBinPathClass
{
  GstObjectClass parent_class;
};


enum
{
  PROP_PATH_0,
  PROP_ELEMENT,
  PROP_PATH_CAPS
};


G_DEFINE_TYPE (GstSwitchBinPath, gst_switch_bin_path, GST_TYPE_OBJECT);


static void gst_switch_bin_path_dispose (GObject * object);
static void gst_switch_bin_path_set_property (GObject * object,
    guint prop_id, GValue const *value, GParamSpec * pspec);
static void gst_switch_bin_path_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_switch_bin_path_use_new_element (GstSwitchBinPath *
    switch_bin_path, GstElement * new_element);



static void
gst_switch_bin_path_class_init (GstSwitchBinPathClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = GST_DEBUG_FUNCPTR (gst_switch_bin_path_dispose);
  object_class->set_property =
      GST_DEBUG_FUNCPTR (gst_switch_bin_path_set_property);
  object_class->get_property =
      GST_DEBUG_FUNCPTR (gst_switch_bin_path_get_property);

  g_object_class_install_property (object_class,
      PROP_ELEMENT,
      g_param_spec_object ("element",
          "Element",
          "The path's element (if set to NULL, this path will drop any incoming data)",
          GST_TYPE_ELEMENT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
  g_object_class_install_property (object_class,
      PROP_PATH_CAPS,
      g_param_spec_boxed ("caps",
          "Caps",
          "Caps which, if they are a subset of the input caps, select this path as the active one",
          GST_TYPE_CAPS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)
      );
}


static void
gst_switch_bin_path_init (GstSwitchBinPath * switch_bin_path)
{
  switch_bin_path->caps = gst_caps_new_any ();
  switch_bin_path->element = NULL;
  switch_bin_path->bin = NULL;
}


static void
gst_switch_bin_path_dispose (GObject * object)
{
  GstSwitchBinPath *switch_bin_path = GST_SWITCH_BIN_PATH (object);

  if (switch_bin_path->caps != NULL) {
    gst_caps_unref (switch_bin_path->caps);
    switch_bin_path->caps = NULL;
  }

  if (switch_bin_path->element != NULL) {
    gst_switch_bin_path_use_new_element (switch_bin_path, NULL);
  }

  /* element is managed by the bin itself */

  G_OBJECT_CLASS (gst_switch_bin_path_parent_class)->dispose (object);
}


static void
gst_switch_bin_path_set_property (GObject * object, guint prop_id,
    GValue const *value, GParamSpec * pspec)
{
  GstSwitchBinPath *switch_bin_path = GST_SWITCH_BIN_PATH (object);
  switch (prop_id) {
    case PROP_ELEMENT:
    {
      /* Get the object without modifying the refcount */
      GstElement *new_element = GST_ELEMENT (g_value_get_object (value));

      GST_OBJECT_LOCK (switch_bin_path);
      PATH_LOCK (switch_bin_path->bin);
      gst_switch_bin_path_use_new_element (switch_bin_path, new_element);
      PATH_UNLOCK_AND_CHECK (switch_bin_path->bin);
      GST_OBJECT_UNLOCK (switch_bin_path);

      break;
    }

    case PROP_PATH_CAPS:
    {
      GstCaps *old_caps;
      GstCaps const *new_caps = gst_value_get_caps (value);

      GST_OBJECT_LOCK (switch_bin_path);
      old_caps = switch_bin_path->caps;
      if (new_caps == NULL) {
        /* NULL caps are interpreted as ANY */
        switch_bin_path->caps = gst_caps_new_any ();
      } else
        switch_bin_path->caps = gst_caps_copy (new_caps);
      GST_OBJECT_UNLOCK (switch_bin_path);

      if (old_caps != NULL)
        gst_caps_unref (old_caps);

      /* the new caps do not get applied right away
       * they only start to be used with the next stream */

      break;
    }

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_switch_bin_path_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSwitchBinPath *switch_bin_path = GST_SWITCH_BIN_PATH (object);
  switch (prop_id) {
    case PROP_ELEMENT:
      /* If a path element exists, increase its refcount first. This is
       * necessary because the code that called g_object_get() to fetch this
       * element will also unref it when it is finished with it. */
      if (switch_bin_path->element != NULL)
        gst_object_ref (GST_OBJECT (switch_bin_path->element));

      /* Use g_value_take_object() instead of g_value_set_object() as the latter
       * increases the element's refcount for the duration of the GValue's lifetime */
      g_value_take_object (value, switch_bin_path->element);

      break;

    case PROP_PATH_CAPS:
      GST_OBJECT_LOCK (switch_bin_path);
      gst_value_set_caps (value, switch_bin_path->caps);
      GST_OBJECT_UNLOCK (switch_bin_path);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static gboolean
gst_switch_bin_path_use_new_element (GstSwitchBinPath * switch_bin_path,
    GstElement * new_element)
{
  /* Must be called with lock */

  GstSwitchBinPath *current_path = switch_bin_path->bin->current_path;
  gboolean is_current_path = (current_path == switch_bin_path);

  /* Before switching the element, make sure it is not linked,
   * which is the case if this is the current path. */
  if (is_current_path)
    gst_switch_bin_switch_to_path (switch_bin_path->bin, NULL);

  /* Remove any present path element prior to using the new one */
  if (switch_bin_path->element != NULL) {
    gst_element_set_state (switch_bin_path->element, GST_STATE_NULL);
    /* gst_bin_remove automatically unrefs the path element */
    gst_bin_remove (GST_BIN (switch_bin_path->bin), switch_bin_path->element);
    switch_bin_path->element = NULL;
  }

  /* If there *is* a new element, use it. new_element == NULL is a valid case;
   * a NULL element is used in dropping paths, which will just use the drop probe
   * to drop buffers if they become the current path. */
  if (new_element != NULL) {
    gst_bin_add (GST_BIN (switch_bin_path->bin), new_element);
    switch_bin_path->element = new_element;

    /* Lock the element's state in case. This prevents freezes, which can happen
     * when an element from a not-current path tries to follow a state change,
     * but is unable to do so as long as it isn't linked. By locking the state,
     * it won't follow state changes, so the freeze does not happen. */
    gst_element_set_locked_state (new_element, TRUE);
  }

  /* We are done. Switch back to the path if it is the current one,
   * since we switched away from it earlier. */
  if (is_current_path)
    return gst_switch_bin_switch_to_path (switch_bin_path->bin, current_path);
  else
    return TRUE;
}
