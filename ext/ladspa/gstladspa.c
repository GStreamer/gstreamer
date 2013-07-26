/* GStreamer LADSPA plugin
 * Copyright (C) 1999 Erik Walthinsen <omega@cse.ogi.edu>
 *               2001 Steve Baker <stevebaker_org@yahoo.co.uk>
 *               2003 Andy Wingo <wingo at pobox.com>
 * Copyright (C) 2013 Juan Manuel Borges Caño <juanmabcmail@gmail.com>
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
 * SECTION:element-ladspa
 * @short_description: bridge for LADSPA (Linux Audio Developer's Simple Plugin API)
 * @see_also: #GstAudioConvert #GstAudioResample, #GstAudioTestSrc, #GstAutoAudioSink
 * 
 * The LADSPA (Linux Audio Developer's Simple Plugin API) element is a bridge
 * for plugins using the <ulink url="http://www.ladspa.org/">LADSPA</ulink> API.
 * It scans all installed LADSPA plugins and registers them as gstreamer
 * elements. If available it can also parse LRDF files and use the metadata for
 * element classification. The functionality you get depends on the LADSPA plugins
 * you have installed.
 * 
 * <refsect2>
 * <title>Example LADSPA line without this plugins</title>
 * |[
 * (padsp) listplugins
 * (padsp) analyseplugin cmt.so amp_mono
 * gst-launch -e filesrc location="$myfile" ! decodebin ! audioconvert ! audioresample ! "audio/x-raw,format=S16LE,rate=48000,channels=1" ! wavenc ! filesink location="testin.wav"
 * (padsp) applyplugin testin.wav testout.wav cmt.so amp_mono 2 
 * gst-launch playbin uri=file://"$PWD"/testout.wav
 * ]| Decode any audio file into wav with the format expected for the specific ladspa plugin to be applied, apply the ladspa filter and play it.
 * </refsect2>
 *
 * Now with this plugin:
 *
 * <refsect2>
 * <title>Example LADSPA line with this plugins</title>
 * |[
 * gst-launch autoaudiosrc ! ladspa-cmt-so-amp-mono gain=2 ! ladspa-caps-so-plate ! ladspa-tap-echo-so-tap-stereo-echo l-delay=500 r-haas-delay=500 ! tee name=myT myT. ! queue ! autoaudiosink myT. ! queue ! audioconvert ! goom ! videoconvert ! xvimagesink pixel-aspect-ratio=3/4
 * ]| Get audio input, filter it through CAPS Plate and TAP Stereo Echo, play it and show a visualization (recommended hearphones).
 * </refsect2>
 *
 * In case you wonder the plugin naming scheme, quoting ladspa.h:
 *   "Plugin types should be identified by file and label rather than by
 *   index or plugin name, which may be changed in new plugin versions."
 * This is really the best way then, and so it is less prone to conflicts.
 *
 * Also it is worth noting that LADSPA provides a control in and out interface,
 * on top of the audio in and out one, so some parameters are readable too.
 *
 * You can see the listing of plugins available with:
 * <refsect2>
 * <title>Inspecting the plugins list</title>
 * |[
 * gst-inspect ladspa
 * ]| List available LADSPA plugins on gstreamer.
 * </refsect2>
 *
 * You can see the parameters of any plugin with:
 * <refsect2>
 * <title>Inspecting the plugins</title>
 * |[
 * gst-inspect ladspa-retro-flange-1208-so-retroflange
 * ]| List details of the plugin, parameters, range and defaults included.
 * </refsect2>
 *
 * The elements categorize in: 
 * <itemizedlist>
 * <listitem><para>Filter/Effect/Audio/LADSPA:</para>
 * <refsect2>
 * <title>Example Filter/Effect/Audio/LADSPA line with this plugins</title>
 * |[
 * gst-launch filesrc location="$myfile" ! decodebin ! audioconvert ! audioresample ! ladspa-calf-so-reverb decay-time=15 high-frq-damp=20000 room-size=5 diffusion=1 wet-amount=2 dry-amount=2 pre-delay=50 bass-cut=20000 treble-cut=20000 ! ladspa-tap-echo-so-tap-stereo-echo l-delay=500 r-haas-delay=500 ! autoaudiosink
 * ]| Decode any audio file, filter it through Calf Reverb LADSPA then TAP Stereo Echo, and play it.
 * </refsect2>
 * </listitem>
 * <listitem><para>Source/Audio/LADSPA:</para> 
 * <refsect2>
 * <title>Example Source/Audio/LADSPA line with this plugins</title>
 * |[
 * gst-launch ladspasrc-sine-so-sine-fcac frequency=220 amplitude=100 ! audioconvert ! autoaudiosink
 * ]| Generate a sine wave with Sine Oscillator (Freq:control, Amp:control) and play it.
 * </refsect2>
 * <refsect2>
 * <title>Example Source/Audio/LADSPA line with this plugins</title>
 * |[
 * gst-launch ladspasrc-caps-so-click bpm=240 volume=1 ! autoaudiosink
 * ]| Generate clicks with CAPS Click - Metronome at 240 beats per minute and play it.
 * </refsect2>
 * <refsect2>
 * <title>Example Source/Audio/LADSPA line with this plugins</title>
 * |[
 * gst-launch ladspasrc-random-1661-so-random-fcsc-oa ! ladspa-cmt-so-amp-mono gain=1.5 ! ladspa-caps-so-plate ! tee name=myT myT. ! queue ! autoaudiosink myT. ! queue ! audioconvert ! wavescope ! videoconvert ! autovideosink
 * ]| Generate random wave, filter it trhough Mono Amplifier and Versatile Plate Reverb, and play, while showing, it.
 * </refsect2>
 * </listitem>
 * <listitem><para>Sink/Audio/LADSPA:</para>
 * <refsect2>
 * <title>Example Sink/Audio/LADSPA line with this plugins</title>
 * |[
 * gst-launch autoaudiosrc ! ladspa-cmt-so-amp-mono gain=2 ! ladspa-caps-so-plate ! ladspa-tap-echo-so-tap-stereo-echo l-delay=500 r-haas-delay=500 ! tee name=myT myT. ! audioconvert ! audioresample ! queue ! ladspasink-cmt-so-null-ai myT. ! audioconvert ! audioresample ! queue ! goom ! videoconvert ! xvimagesink pixel-aspect-ratio=3/4
 * ]| Get audio input, filter it trhough Mono Amplifier, CAPS Plate LADSPA and TAP Stereo Echo, explicitily anulate audio with Null (Audio Output), and play a visualization (recommended hearphones).
 * </refsect2>
 * </listitem>
 * </itemizedlist>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstladspautils.h"
#include "gstladspafilter.h"
#include "gstladspasource.h"
#include "gstladspasink.h"
#include <gst/gst-i18n-plugin.h>

#include <string.h>
#include <ladspa.h>
#ifdef HAVE_LRDF
#include <lrdf.h>
#endif

GST_DEBUG_CATEGORY (ladspa_debug);
#define GST_CAT_DEFAULT ladspa_debug

/*
 * 1.0 and the 1.1 preliminary headers don't define a version, but
 * 1.1 finally does
 */
#ifndef LADSPA_VERSION
#define LADSPA_VERSION "1.0"
#endif

#define GST_LADSPA_DEFAULT_PATH \
  "/usr/lib/ladspa" G_SEARCHPATH_SEPARATOR_S \
  "/usr/local/lib/ladspa" G_SEARCHPATH_SEPARATOR_S \
  LIBDIR "/ladspa"

GstStructure *ladspa_meta_all = NULL;

static void
ladspa_plugin_register_element (GstPlugin * plugin, GstStructure * ladspa_meta)
{
  guint audio_in, audio_out;

  gst_structure_get_uint (ladspa_meta, "audio-in", &audio_in);
  gst_structure_get_uint (ladspa_meta, "audio-out", &audio_out);

  if (audio_in == 0) {
    ladspa_register_source_element (plugin, ladspa_meta);
  } else if (audio_out == 0) {
    ladspa_register_sink_element (plugin, ladspa_meta);
  } else {
    ladspa_register_filter_element (plugin, ladspa_meta);
  }
}

static void
ladspa_count_ports (const LADSPA_Descriptor * descriptor,
    guint * audio_in, guint * audio_out, guint * control_in,
    guint * control_out)
{
  guint i;

  *audio_in = *audio_out = *control_in = *control_out = 0;

  for (i = 0; i < descriptor->PortCount; i++) {
    LADSPA_PortDescriptor p = descriptor->PortDescriptors[i];

    if (LADSPA_IS_PORT_AUDIO (p)) {
      if (LADSPA_IS_PORT_INPUT (p))
        (*audio_in)++;
      else
        (*audio_out)++;
    } else if (LADSPA_IS_PORT_CONTROL (p)) {
      if (LADSPA_IS_PORT_INPUT (p))
        (*control_in)++;
      else
        (*control_out)++;
    }
  }
}

static void
ladspa_describe_plugin (const gchar * file_name, const gchar * entry_name,
    LADSPA_Descriptor_Function descriptor_function)
{
  const LADSPA_Descriptor *desc;
  guint i;

  /* walk through all the plugins in this plugin library */
  for (i = 0; (desc = descriptor_function (i)); i++) {
    GstStructure *ladspa_meta = NULL;
    GValue value = { 0, };
    gchar *tmp;
    gchar *type_name;
    guint audio_in, audio_out, control_in, control_out;

    /* count ports of this plugin */
    ladspa_count_ports (desc, &audio_in, &audio_out, &control_in, &control_out);

    /* categorize  */
    if (audio_in == 0 && audio_out == 0) {
      GST_WARNING ("Skipping control only element (%s:%lu/%s)",
          entry_name, desc->UniqueID, desc->Label);
      continue;
    } else if (audio_in == 0) {
      tmp = g_strdup_printf ("ladspasrc-%s-%s", entry_name, desc->Label);
    } else if (audio_out == 0) {
      tmp = g_strdup_printf ("ladspasink-%s-%s", entry_name, desc->Label);
    } else {
      tmp = g_strdup_printf ("ladspa-%s-%s", entry_name, desc->Label);
    }
    type_name = g_ascii_strdown (tmp, -1);
    g_free (tmp);
    g_strcanon (type_name, G_CSET_A_2_Z G_CSET_a_2_z G_CSET_DIGITS "-+", '-');

    /* check if the type is already registered */
    if (g_type_from_name (type_name)) {
      GST_WARNING ("Plugin identifier collision for %s (%s:%lu/%s)", type_name,
          entry_name, desc->UniqueID, desc->Label);
      g_free (type_name);
      continue;
    }

    ladspa_meta = gst_structure_new_empty ("ladspa");
    gst_structure_set (ladspa_meta,
        "plugin-filename", G_TYPE_STRING, file_name,
        "element-ix", G_TYPE_UINT, i,
        "element-type-name", G_TYPE_STRING, type_name,
        "audio-in", G_TYPE_UINT, audio_in,
        "audio-out", G_TYPE_UINT, audio_out,
        "control-in", G_TYPE_UINT, control_in,
        "control-out", G_TYPE_UINT, control_out, NULL);

    g_value_init (&value, GST_TYPE_STRUCTURE);
    g_value_set_boxed (&value, ladspa_meta);
    gst_structure_set_value (ladspa_meta_all, type_name, &value);
    g_value_unset (&value);
  }
}

#ifdef HAVE_LRDF
static gboolean
ladspa_rdf_directory_search (const char *dir_name)
{
  GDir *dir;
  gchar *file_name, *file_uri;
  const gchar *entry_name;
  gint ok;

  GST_INFO ("scanning directory for rdfs \"%s\"", dir_name);

  dir = g_dir_open (dir_name, 0, NULL);
  if (!dir)
    return FALSE;

  while ((entry_name = g_dir_read_name (dir))) {
    file_name = g_build_filename (dir_name, entry_name, NULL);
    file_uri = g_strconcat ("file://", file_name, NULL);
    ok = lrdf_read_file (file_uri);
    GST_INFO ("read %s : %d", file_uri, ok);
    g_free (file_uri);
    g_free (file_name);
  }
  g_dir_close (dir);

  return TRUE;
}
#endif

/* search just the one directory */
static gboolean
ladspa_plugin_directory_search (GstPlugin * ladspa_plugin, const char *dir_name)
{
  GDir *dir;
  gchar *file_name;
  const gchar *entry_name;
  LADSPA_Descriptor_Function descriptor_function;
  GModule *plugin;
  gboolean ok = FALSE;

  GST_INFO ("scanning directory for plugins \"%s\"", dir_name);

  dir = g_dir_open (dir_name, 0, NULL);
  if (!dir)
    return FALSE;

  while ((entry_name = g_dir_read_name (dir))) {
    file_name = g_build_filename (dir_name, entry_name, NULL);
    plugin =
        g_module_open (file_name, G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (plugin) {
      /* the file is a shared library */
      if (g_module_symbol (plugin, "ladspa_descriptor",
              (gpointer *) & descriptor_function)) {
        /* we've found a ladspa_descriptor function, now introspect it. */
        GST_INFO ("describe %s", file_name);
        ladspa_describe_plugin (file_name, entry_name, descriptor_function);
        ok = TRUE;
      } else {
        /* it was a library, but not a LADSPA one. Unload it. */
        g_module_close (plugin);
      }
    }
    g_free (file_name);
  }
  g_dir_close (dir);

  return ok;
}

/* search the plugin path */
static gboolean
ladspa_plugin_path_search (GstPlugin * plugin)
{
  const gchar *search_path;
  gchar *ladspa_path;
  gchar **paths;
  gint i, j, path_entries;
  gboolean res = FALSE, skip;
#ifdef HAVE_LRDF
  gchar *pos, *prefix, *rdf_path;
#endif

  search_path = g_getenv ("LADSPA_PATH");
  if (search_path) {
    ladspa_path =
        g_strdup_printf ("%s" G_SEARCHPATH_SEPARATOR_S GST_LADSPA_DEFAULT_PATH,
        search_path);
  } else {
    ladspa_path = g_strdup (GST_LADSPA_DEFAULT_PATH);
  }

  paths = g_strsplit (ladspa_path, G_SEARCHPATH_SEPARATOR_S, 0);
  path_entries = g_strv_length (paths);
  GST_INFO ("%d dirs in search paths \"%s\"", path_entries, ladspa_path);

#ifdef HAVE_LRDF
  for (i = 0; i < path_entries; i++) {
    skip = FALSE;
    for (j = 0; j < i; j++) {
      if (!strcmp (paths[i], paths[j])) {
        skip = TRUE;
        break;
      }
    }
    if (skip)
      break;
    /* 
     * transform path: /usr/lib/ladspa -> /usr/share/ladspa/rdf/
     * yes, this is ugly, but lrdf has not searchpath
     */
    if ((pos = strstr (paths[i], "/lib/ladspa"))) {
      prefix = g_strndup (paths[i], (pos - paths[i]));
      rdf_path = g_build_filename (prefix, "share", "ladspa", "rdf", NULL);
      ladspa_rdf_directory_search (rdf_path);
      g_free (rdf_path);
      g_free (prefix);
    }
  }
#endif

  for (i = 0; i < path_entries; i++) {
    skip = FALSE;
    for (j = 0; j < i; j++) {
      if (!strcmp (paths[i], paths[j])) {
        skip = TRUE;
        break;
      }
    }
    if (skip)
      break;
    res |= ladspa_plugin_directory_search (plugin, paths[i]);
  }
  g_strfreev (paths);

  g_free (ladspa_path);

  return res;
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  gboolean res = FALSE;
  gint n = 0;

  GST_DEBUG_CATEGORY_INIT (ladspa_debug, "ladspa", 0, "LADSPA plugins");

#ifdef ENABLE_NLS
  GST_DEBUG ("binding text domain %s to locale dir %s", GETTEXT_PACKAGE,
      LOCALEDIR);
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
#endif

  gst_plugin_add_dependency_simple (plugin,
      "LADSPA_PATH",
      GST_LADSPA_DEFAULT_PATH, NULL, GST_PLUGIN_DEPENDENCY_FLAG_NONE);

#ifdef HAVE_LRDF
  lrdf_init ();
#endif

  ladspa_meta_all = (GstStructure *) gst_plugin_get_cache_data (plugin);
  if (ladspa_meta_all) {
    n = gst_structure_n_fields (ladspa_meta_all);
  }
  GST_INFO ("%d entries in cache", n);
  if (!n) {
    ladspa_meta_all = gst_structure_new_empty ("ladspa");
    res = ladspa_plugin_path_search (plugin);
    if (res) {
      n = gst_structure_n_fields (ladspa_meta_all);
      GST_INFO ("%d entries after scanning", n);
      gst_plugin_set_cache_data (plugin, ladspa_meta_all);
    }
  } else {
    res = TRUE;
  }

  if (n) {
    gint i;
    const gchar *name;
    const GValue *value;

    GST_INFO ("register types");

    for (i = 0; i < n; i++) {
      name = gst_structure_nth_field_name (ladspa_meta_all, i);
      value = gst_structure_get_value (ladspa_meta_all, name);
      if (G_VALUE_TYPE (value) == GST_TYPE_STRUCTURE) {
        GstStructure *ladspa_meta = g_value_get_boxed (value);

        ladspa_plugin_register_element (plugin, ladspa_meta);
      }
    }
  }

  if (!res) {
    GST_WARNING ("no LADSPA plugins found, check LADSPA_PATH");
  }

  /* we don't want to fail, even if there are no elements registered */
  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    ladspa,
    "LADSPA plugin",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
