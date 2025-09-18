/* GStreamer command line device monitor testing utility
 * Copyright (C) 2014 Tim-Philipp Müller <tim@centricular.com>
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
#include "config.h"
#endif

#include <locale.h>

#include <gst/gst.h>
#include <glib/gi18n.h>
#include <gst/math-compat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <gst/glib-compat-private.h>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

GST_DEBUG_CATEGORY (devmon_debug);
#define GST_CAT_DEFAULT devmon_debug

typedef struct
{
  GMainLoop *loop;
  GstDeviceMonitor *monitor;
  guint bus_watch_id;
} DevMonApp;

static gboolean bus_msg_handler (GstBus * bus, GstMessage * msg, gpointer data);

typedef enum
{
  SHELL_POSIX,
  SHELL_CMD,
  SHELL_POWERSHELL,
} ShellType;

static inline ShellType
get_shell_type (void)
{
  if (g_getenv ("PROMPT") != NULL)
    return SHELL_CMD;
  if (g_getenv ("PSModulePath") != NULL)
    return SHELL_POWERSHELL;
  return SHELL_POSIX;
}

/*
 * Some quoting rules here:
 * https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/cmd
 *
 * The rest has been deduced from trial-and-error, since command-line parsing
 * is different on Windows compared to UNIX. There is no char* argument array
 * when processes are created. There is a single argument, and the CRT splits
 * it into an array based on its own arcane rules when running a POSIX
 * command-line app with a `main()` instead of `WinMain()`.
 *
 * https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-winmain
 *
 * So for arguments passed to gst-launch, we need to deal with both how cmd.exe
 * handles quoting/escaping and also how the UCRT does argument splitting. The
 * current algorithm is:
 *
 * - Everything is quoted with " except
 * - % needs to be escaped with ^ otherwise it will undergo variable expansion
 * - % must not be quoted with " otherwise the caret-escaping doesn't work
 * - " needs to be escaped as "" when inside " quotes
 * - \ needs to be escaped as \\ due to gst_value_deserialize()
 *
 * So for example `%PATH% bar" wdwd |` becomes `""^%"PATH"^%" bar"" wdwd |"`
 */
static inline char *
cmd_quote (const char *s)
{
  GString *str = g_string_new (s);
  g_string_replace (str, "\"", "\"\"", 0);
  g_string_replace (str, "\\", "\\\\", 0);
  str = g_string_prepend_c (str, '"');
  str = g_string_append_c (str, '"');
  /* Very simple and very ugly: simply terminate the " quoting when we
   * encounter % then escape it and continue the " quoting  */
  g_string_replace (str, "%", "\"^%\"", 0);
  return g_string_free (str, FALSE);
}

/* Verbatim quoting rules:
 * https://learn.microsoft.com/en-us/powershell/module/microsoft.powershell.core/about/about_quoting_rules
 *
 * On top of this, \ needs to be escaped as \\ due to gst_value_deserialize()
 * when parsing launch-lines.
 *
 * The main vs WinMain issue exists here, but the quoting rules are simple
 * enough to cover both.
 */
static inline char *
powershell_quote (const char *s)
{
  GString *str = g_string_new (s);
  g_string_replace (str, "'", "''", 0);
  g_string_replace (str, "‘", "‘‘", 0);
  g_string_replace (str, "’", "’’", 0);
  g_string_replace (str, "\\", "\\\\", 0);
  str = g_string_prepend_c (str, '\'');
  str = g_string_append_c (str, '\'');
  return g_string_free (str, FALSE);
}

static inline char *
do_shell_quote (const char *s)
{
  switch (get_shell_type ()) {
    case SHELL_POSIX:
      return g_shell_quote (s);
    case SHELL_CMD:
      return cmd_quote (s);
    case SHELL_POWERSHELL:
      return powershell_quote (s);
  }
  g_assert_not_reached ();
}

static inline char *
value_to_string (const GValue * v)
{
  const char *d;
  char *ret, *s;
  gboolean need_quote = FALSE;

  if (G_VALUE_HOLDS_STRING (v)) {
    const char *str = g_value_get_string (v);
    if (!g_utf8_validate (str, -1, NULL)) {
      s = gst_value_serialize (v);
    } else {
      s = g_strdup (str);
    }
  } else {
    s = gst_value_serialize (v);
  }


  d = s;
  while (*++d) {
    if (!g_ascii_isalnum (*d)) {
      need_quote = TRUE;
      break;
    }
  }

  if (need_quote) {
    ret = do_shell_quote (s);
    g_free (s);
    return ret;
  }
  return s;
}

static gchar *
get_launch_line (GstDevice * device)
{
  static const char *const ignored_propnames[] =
      { "name", "parent", "direction", "template", "caps", NULL };
  GString *launch_line;
  GstElement *element;
  GstElement *pureelement;
  GParamSpec **properties, *property;
  GValue value = G_VALUE_INIT;
  GValue pvalue = G_VALUE_INIT;
  guint i, number_of_properties;
  GstElementFactory *factory;

  element = gst_device_create_element (device, NULL);

  if (!element)
    return NULL;

  factory = gst_element_get_factory (element);
  if (!factory) {
    gst_object_unref (element);
    return NULL;
  }

  if (!gst_plugin_feature_get_name (factory)) {
    gst_object_unref (element);
    return NULL;
  }

  launch_line = g_string_new (gst_plugin_feature_get_name (factory));

  pureelement = gst_element_factory_create (factory, NULL);

  /* get paramspecs and show non-default properties */
  properties =
      g_object_class_list_properties (G_OBJECT_GET_CLASS (element),
      &number_of_properties);
  if (properties) {
    for (i = 0; i < number_of_properties; i++) {
      gint j;
      gboolean ignore = FALSE;
      property = properties[i];

      /* skip some properties */
      if ((property->flags & G_PARAM_READWRITE) != G_PARAM_READWRITE)
        continue;

      for (j = 0; ignored_propnames[j]; j++)
        if (!g_strcmp0 (ignored_propnames[j], property->name)) {
          ignore = TRUE;
          break;
        }

      if (ignore)
        continue;

      /* Can't use _param_value_defaults () because sub-classes modify the
       * values already.
       */

      g_value_init (&value, property->value_type);
      g_value_init (&pvalue, property->value_type);
      g_object_get_property (G_OBJECT (element), property->name, &value);
      g_object_get_property (G_OBJECT (pureelement), property->name, &pvalue);
      if (gst_value_compare (&value, &pvalue) != GST_VALUE_EQUAL) {
        char *valuestr = value_to_string (&value);

        if (!valuestr) {
          GST_WARNING ("Could not serialize property %s:%s",
              GST_OBJECT_NAME (element), property->name);
          g_free (valuestr);
          goto next;
        }

        g_string_append_printf (launch_line, " %s=%s",
            property->name, valuestr);
        g_free (valuestr);
      }

    next:
      g_value_unset (&value);
      g_value_unset (&pvalue);
    }
    g_free (properties);
  }

  gst_object_unref (element);
  gst_object_unref (pureelement);

  return g_string_free (launch_line, FALSE);
}


static gboolean
print_structure_field (const GstIdStr * fieldname, const GValue * value,
    gpointer user_data)
{
  gchar *val;

  if (G_VALUE_HOLDS_UINT (value)) {
    val = g_strdup_printf ("%u (0x%08x)", g_value_get_uint (value),
        g_value_get_uint (value));
  } else if (G_VALUE_HOLDS_STRING (value)) {
    val = g_value_dup_string (value);
  } else {
    val = gst_value_serialize (value);
  }

  if (val != NULL)
    gst_print ("\n\t\t%s = %s", gst_id_str_as_str (fieldname), val);
  else
    gst_print ("\n\t\t%s - could not serialise field of type %s",
        gst_id_str_as_str (fieldname), G_VALUE_TYPE_NAME (value));

  g_free (val);

  return TRUE;
}

static gboolean
print_field (const GstIdStr * fieldname, const GValue * value, gpointer unused)
{
  gchar *str = gst_value_serialize (value);

  gst_print (", %s=%s", gst_id_str_as_str (fieldname), str);
  g_free (str);
  return TRUE;
}

static void
print_device (GstDevice * device, gboolean modified)
{
  gchar *device_class, *str, *name;
  GstCaps *caps;
  GstStructure *props;
  guint i, size = 0;

  caps = gst_device_get_caps (device);
  if (caps != NULL)
    size = gst_caps_get_size (caps);

  name = gst_device_get_display_name (device);
  device_class = gst_device_get_device_class (device);
  props = gst_device_get_properties (device);

  gst_print ("\nDevice %s:\n\n", modified ? "modified" : "found");
  gst_print ("\tname  : %s\n", name);
  gst_print ("\tclass : %s\n", device_class);
  for (i = 0; i < size; ++i) {
    GstStructure *s = gst_caps_get_structure (caps, i);
    GstCapsFeatures *features = gst_caps_get_features (caps, i);

    gst_print ("\t%s %s", (i == 0) ? "caps  :" : "       ",
        gst_structure_get_name (s));
    if (features && (gst_caps_features_is_any (features) ||
            !gst_caps_features_is_equal (features,
                GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))) {
      gchar *features_string = gst_caps_features_to_string (features);

      gst_print ("(%s)", features_string);
      g_free (features_string);
    }
    gst_structure_foreach_id_str (s, print_field, NULL);
    gst_print ("\n");
  }
  if (props) {
    gst_print ("\tproperties:");
    gst_structure_foreach_id_str (props, print_structure_field, NULL);
    gst_structure_free (props);
    gst_print ("\n");
  }
  str = get_launch_line (device);
  if (gst_device_has_classes (device, "Source"))
    gst_print ("\tgst-launch-1.0 %s ! ...\n", str);
  else if (gst_device_has_classes (device, "Sink"))
    gst_print ("\tgst-launch-1.0 ... ! %s\n", str);
  else if (gst_device_has_classes (device, "CameraSource")) {
    gst_print ("\tgst-launch-1.0 %s.vfsrc name=camerasrc ! ... "
        "camerasrc.vidsrc ! [video/x-h264] ... \n", str);
  }

  g_free (str);
  gst_print ("\n");

  g_free (name);
  g_free (device_class);

  if (caps != NULL)
    gst_caps_unref (caps);
}

static void
device_removed (GstDevice * device)
{
  gchar *name;

  name = gst_device_get_display_name (device);

  gst_print ("Device removed:\n");
  gst_print ("\tname  : %s\n", name);

  g_free (name);
}

static gboolean
bus_msg_handler (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  GstDevice *device;

  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_DEVICE_ADDED:
      gst_message_parse_device_added (msg, &device);
      print_device (device, FALSE);
      gst_object_unref (device);
      break;
    case GST_MESSAGE_DEVICE_REMOVED:
      gst_message_parse_device_removed (msg, &device);
      device_removed (device);
      gst_object_unref (device);
      break;
    case GST_MESSAGE_DEVICE_CHANGED:
      gst_message_parse_device_changed (msg, &device, NULL);
      print_device (device, TRUE);
      gst_object_unref (device);
      break;
    default:
      gst_print ("%s message\n", GST_MESSAGE_TYPE_NAME (msg));
      break;
  }

  return TRUE;
}

static gboolean
quit_loop (GMainLoop * loop)
{
  g_main_loop_quit (loop);
  return G_SOURCE_REMOVE;
}

static int
real_main (int argc, char **argv)
{
  gboolean print_version = FALSE;
  GError *err = NULL;
  gchar **arg, **args = NULL;
  gboolean follow = FALSE;
  gboolean include_hidden = FALSE;
  GOptionContext *ctx;
  GOptionEntry options[] = {
    {"version", 0, 0, G_OPTION_ARG_NONE, &print_version,
        N_("Print version information and exit"), NULL},
    {"follow", 'f', 0, G_OPTION_ARG_NONE, &follow,
        N_("Don't exit after showing the initial device list, but wait "
              "for devices to added/removed."), NULL},
    {"include-hidden", 'i', 0, G_OPTION_ARG_NONE, &include_hidden,
        N_("Include devices from hidden device providers."), NULL},
    {G_OPTION_REMAINING, 0, 0, G_OPTION_ARG_STRING_ARRAY, &args, NULL},
    {NULL}
  };
  GTimer *timer;
  DevMonApp app;
  GstBus *bus;

  setlocale (LC_ALL, "");

#ifdef ENABLE_NLS
  bindtextdomain (GETTEXT_PACKAGE, LOCALEDIR);
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);
#endif

  g_set_prgname ("gst-device-monitor-" GST_API_VERSION);

  ctx = g_option_context_new ("[DEVICE_CLASSES[:FILTER_CAPS]] "
      "[DEVICE_CLASSES[:FILTER_CAPS]] …");
  g_option_context_add_main_entries (ctx, options, GETTEXT_PACKAGE);
  g_option_context_add_group (ctx, gst_init_get_option_group ());
#ifdef G_OS_WIN32
  if (!g_option_context_parse_strv (ctx, &argv, &err))
#else
  if (!g_option_context_parse (ctx, &argc, &argv, &err))
#endif
  {
    gst_print ("Error initializing: %s\n", GST_STR_NULL (err->message));
    g_option_context_free (ctx);
    g_clear_error (&err);
    return 1;
  }
  g_option_context_free (ctx);

#ifdef G_OS_WIN32
  argc = g_strv_length (argv);
#endif

  GST_DEBUG_CATEGORY_INIT (devmon_debug, "device-monitor", 0,
      "gst-device-monitor");

  if (print_version) {
    gchar *version_str;

    version_str = gst_version_string ();
    gst_print ("%s version %s\n", g_get_prgname (), PACKAGE_VERSION);
    gst_print ("%s\n", version_str);
    gst_print ("%s\n", GST_PACKAGE_ORIGIN);
    g_free (version_str);

    return 0;
  }

  app.loop = g_main_loop_new (NULL, FALSE);
  app.monitor = gst_device_monitor_new ();
  gst_device_monitor_set_show_all_devices (app.monitor, include_hidden);

  bus = gst_device_monitor_get_bus (app.monitor);
  app.bus_watch_id = gst_bus_add_watch (bus, bus_msg_handler, &app);
  gst_object_unref (bus);

  /* process optional remaining arguments in the form
   * DEVICE_CLASSES or DEVICE_CLASSES:FILTER_CAPS */
  for (arg = args; arg != NULL && *arg != NULL; ++arg) {
    gchar **filters = g_strsplit (*arg, ":", 2);
    if (filters != NULL && filters[0] != NULL) {
      GstCaps *caps = NULL;

      if (filters[1] != NULL) {
        caps = gst_caps_from_string (filters[1]);
        if (caps == NULL)
          g_warning ("Couldn't parse device filter caps '%s'", filters[1]);
      }
      gst_device_monitor_add_filter (app.monitor, filters[0], caps);
      if (caps)
        gst_caps_unref (caps);
      g_strfreev (filters);
    }
  }
  g_strfreev (args);

  gst_print ("Probing devices...\n\n");

  timer = g_timer_new ();

  if (!gst_device_monitor_start (app.monitor)) {
    gst_printerr ("Failed to start device monitor!\n");
    return -1;
  }

  GST_INFO ("Took %.2f seconds", g_timer_elapsed (timer, NULL));

  if (!follow) {
    /* Consume all the messages pending on the bus and exit */
    g_idle_add ((GSourceFunc) quit_loop, app.loop);
  } else {
    gst_print ("Monitoring devices, waiting for devices to be removed or "
        "new devices to be added...\n");
  }

  g_main_loop_run (app.loop);

  gst_device_monitor_stop (app.monitor);
  gst_object_unref (app.monitor);

  g_source_remove (app.bus_watch_id);
  g_main_loop_unref (app.loop);
  g_timer_destroy (timer);

  gst_deinit ();
  return 0;
}

int
main (int argc, char *argv[])
{
  int ret;

#ifdef G_OS_WIN32
  argv = g_win32_get_command_line ();
#endif

#if defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE
  ret = gst_macos_main ((GstMainFunc) real_main, argc, argv, NULL);
#else
  ret = real_main (argc, argv);
#endif

#ifdef G_OS_WIN32
  g_strfreev (argv);
#endif

  return ret;
}
