/* GStreamer base utils library plugin install support for applications
 * Copyright (C) 2007 Tim-Philipp Müller <tim centricular net>
 * Copyright (C) 2006 Ryan Lortie <desrt desrt ca>
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
#include "config.h"
#endif

#include "install-plugins.h"

#include <gst/gstinfo.h>

#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

/* best effort to make things compile and possibly even work on win32 */
#ifndef WEXITSTATUS
# define WEXITSTATUS(status) ((((guint)(status)) & 0xff00) >> 8)
#endif
#ifndef WIFEXITED
# define WIFEXITED(status) ((((guint)(status)) & 0x7f) == 0)
#endif

static gboolean install_in_progress;    /* FALSE */

/* private struct */
struct _GstInstallPluginsContext
{
  guint xid;
};

/**
 * gst_install_plugins_context_set_xid:
 * @ctx: a #GstInstallPluginsContext
 * @xid: the XWindow ID (XID) of the top-level application
 *
 * This function is for X11-based applications (such as most Gtk/Qt
 * applications on linux/unix) only. You can use it to tell the external
 * the XID of your main application window, so the installer can make its
 * own window transient to your application windonw during the installation.
 *
 * If set, the XID will be passed to the installer via a --transient-for=XID
 * command line option.
 *
 * Gtk+/Gnome application should be able to obtain the XID of the top-level
 * window like this:
 * <programlisting>
 * ##include &lt;gtk/gtk.h&gt;
 * ##ifdef GDK_WINDOWING_X11
 * ##include &lt;gdk/gdkx.h&gt;
 * ##endif
 * ...
 * ##ifdef GDK_WINDOWING_X11
 *   xid = GDK_WINDOW_XWINDOW (GTK_WIDGET (application_window)-&gt;window);
 * ##endif
 * ...
 * </programlisting>
 *
 * Since: 0.10.12
 */
void
gst_install_plugins_context_set_xid (GstInstallPluginsContext * ctx, guint xid)
{
  g_return_if_fail (ctx != NULL);

  ctx->xid = xid;
}

/**
 * gst_install_plugins_context_new:
 *
 * Creates a new #GstInstallPluginsContext.
 *
 * Returns: a new #GstInstallPluginsContext. Free with
 * gst_install_plugins_context_free() when no longer needed
 *
 * Since: 0.10.12
 */
GstInstallPluginsContext *
gst_install_plugins_context_new (void)
{
  return g_new0 (GstInstallPluginsContext, 1);
}

/**
 * gst_install_plugins_context_free:
 * @ctx: a #GstInstallPluginsContext
 *
 * Frees a #GstInstallPluginsContext.
 *
 * Since: 0.10.12
 */
void
gst_install_plugins_context_free (GstInstallPluginsContext * ctx)
{
  g_return_if_fail (ctx != NULL);

  g_free (ctx);
}

static const gchar *
gst_install_plugins_get_helper (void)
{
  const gchar *helper;

  helper = g_getenv ("GST_INSTALL_PLUGINS_HELPER");
  if (helper == NULL)
    helper = GST_INSTALL_PLUGINS_HELPER;

  GST_LOG ("Using plugin install helper '%s'", helper);
  return helper;
}

static gboolean
gst_install_plugins_spawn_child (gchar ** details,
    GstInstallPluginsContext * ctx, GPid * child_pid, gint * exit_status)
{
  GPtrArray *arr;
  gboolean ret;
  GError *err = NULL;
  gchar **argv, xid_str[64] = { 0, };

  arr = g_ptr_array_new ();

  /* argv[0] = helper path */
  g_ptr_array_add (arr, (gchar *) gst_install_plugins_get_helper ());

  /* add any additional command line args from the context */
  if (ctx != NULL && ctx->xid != 0) {
    g_snprintf (xid_str, sizeof (xid_str), "--transient-for=%u", ctx->xid);
    g_ptr_array_add (arr, xid_str);
  }

  /* finally, add the detail strings */
  while (details != NULL && details[0] != NULL) {
    g_ptr_array_add (arr, details[0]);
    ++details;
  }

  /* and NULL-terminate */
  g_ptr_array_add (arr, NULL);

  argv = (gchar **) arr->pdata;

  if (child_pid == NULL && exit_status != NULL) {
    install_in_progress = TRUE;
    ret = g_spawn_sync (NULL, argv, NULL, (GSpawnFlags) 0, NULL, NULL,
        NULL, NULL, exit_status, &err);
    install_in_progress = FALSE;
  } else if (child_pid != NULL && exit_status == NULL) {
    install_in_progress = TRUE;
    ret = g_spawn_async (NULL, argv, NULL, G_SPAWN_DO_NOT_REAP_CHILD, NULL,
        NULL, child_pid, &err);
  } else {
    g_assert_not_reached ();
  }

  if (!ret) {
    GST_ERROR ("Error spawning plugin install helper: %s", err->message);
    g_error_free (err);
  }

  g_ptr_array_free (arr, TRUE);
  return ret;
}

static GstInstallPluginsReturn
gst_install_plugins_return_from_status (gint status)
{
  GstInstallPluginsReturn ret;

  /* did we exit cleanly? */
  if (!WIFEXITED (status)) {
    ret = GST_INSTALL_PLUGINS_CRASHED;
  } else {
    ret = (GstInstallPluginsReturn) WEXITSTATUS (status);

    /* did the helper return an invalid status code? */
    if ((ret < 0 || ret >= GST_INSTALL_PLUGINS_STARTED_OK) &&
        ret != GST_INSTALL_PLUGINS_INTERNAL_FAILURE) {
      ret = GST_INSTALL_PLUGINS_INVALID;
    }
  }

  GST_LOG ("plugin installer exited with status 0x%04x = %s", status,
      gst_install_plugins_return_get_name (ret));

  return ret;
}

typedef struct
{
  GstInstallPluginsResultFunc func;
  gpointer user_data;
} GstInstallPluginsAsyncHelper;

static void
gst_install_plugins_installer_exited (GPid pid, gint status, gpointer data)
{
  GstInstallPluginsAsyncHelper *helper;
  GstInstallPluginsReturn ret;

  install_in_progress = FALSE;

  helper = (GstInstallPluginsAsyncHelper *) data;
  ret = gst_install_plugins_return_from_status (status);

  GST_LOG ("calling plugin install result function %p", helper->func);
  helper->func (ret, helper->user_data);

  g_free (helper);
}

/**
 * gst_install_plugins_async:
 * @details: NULL-terminated array of installer string details (see below)
 * @ctx: a #GstInstallPluginsContext, or NULL
 * @func: the function to call when the installer program returns
 * @user_data: the user data to pass to @func when called, or NULL
 * 
 * Requests plugin installation without blocking. Once the plugins have been
 * installed or installation has failed, @func will be called with the result
 * of the installation and your provided @user_data pointer.
 *
 * This function requires a running GLib/Gtk main loop. If you are not
 * running a GLib/Gtk main loop, make sure to regularly call
 * g_main_context_iteration(NULL,FALSE).
 *
 * The installer strings that make up @detail are typically obtained by
 * calling gst_missing_plugin_message_get_installer_detail() on missing-plugin
 * messages that have been caught on a pipeline's bus or created by the
 * application via the provided API, such as gst_missing_element_message_new().
 *
 * Returns: result code whether an external installer could be started
 *
 * Since: 0.10.12
 */

GstInstallPluginsReturn
gst_install_plugins_async (gchar ** details, GstInstallPluginsContext * ctx,
    GstInstallPluginsResultFunc func, gpointer user_data)
{
  GstInstallPluginsAsyncHelper *helper;
  GPid pid;

  g_return_val_if_fail (details != NULL, GST_INSTALL_PLUGINS_INTERNAL_FAILURE);
  g_return_val_if_fail (func != NULL, GST_INSTALL_PLUGINS_INTERNAL_FAILURE);

  if (install_in_progress)
    return GST_INSTALL_PLUGINS_INSTALL_IN_PROGRESS;

  /* if we can't access our helper, don't bother */
  if (!g_file_test (gst_install_plugins_get_helper (),
          G_FILE_TEST_IS_EXECUTABLE))
    return GST_INSTALL_PLUGINS_HELPER_MISSING;

  if (!gst_install_plugins_spawn_child (details, ctx, &pid, NULL))
    return GST_INSTALL_PLUGINS_INTERNAL_FAILURE;

  helper = g_new (GstInstallPluginsAsyncHelper, 1);
  helper->func = func;
  helper->user_data = user_data;

  g_child_watch_add (pid, gst_install_plugins_installer_exited, helper);

  return GST_INSTALL_PLUGINS_STARTED_OK;
}

/**
 * gst_install_plugins_sync:
 * @details: NULL-terminated array of installer string details
 * @ctx: a #GstInstallPluginsContext, or NULL
 * 
 * Requests plugin installation and block until the plugins have been
 * installed or installation has failed.
 *
 * This function should almost never be used, it only exists for cases where
 * a non-GLib main loop is running and the user wants to run it in a separate
 * thread and marshal the result back asynchronously into the main thread
 * using the other non-GLib main loop. You should almost always use
 * gst_install_plugins_async() instead of this function.
 *
 * Returns: the result of the installation.
 *
 * Since: 0.10.12
 */
GstInstallPluginsReturn
gst_install_plugins_sync (gchar ** details, GstInstallPluginsContext * ctx)
{
  gint status;

  g_return_val_if_fail (details != NULL, GST_INSTALL_PLUGINS_INTERNAL_FAILURE);

  if (install_in_progress)
    return GST_INSTALL_PLUGINS_INSTALL_IN_PROGRESS;

  /* if we can't access our helper, don't bother */
  if (!g_file_test (gst_install_plugins_get_helper (),
          G_FILE_TEST_IS_EXECUTABLE))
    return GST_INSTALL_PLUGINS_HELPER_MISSING;

  if (!gst_install_plugins_spawn_child (details, ctx, NULL, &status))
    return GST_INSTALL_PLUGINS_INTERNAL_FAILURE;

  return gst_install_plugins_return_from_status (status);
}

/**
 * gst_install_plugins_return_get_name:
 * @ret: the return status code
 * 
 * Convenience function to return the descriptive string associated
 * with a status code.  This function returns English strings and
 * should not be used for user messages. It is here only to assist
 * in debugging.
 *
 * Returns: a descriptive string for the status code in @ret
 *
 * Since: 0.10.12
 */
const gchar *
gst_install_plugins_return_get_name (GstInstallPluginsReturn ret)
{
  switch (ret) {
    case GST_INSTALL_PLUGINS_SUCCESS:
      return "success";
    case GST_INSTALL_PLUGINS_NOT_FOUND:
      return "not-found";
    case GST_INSTALL_PLUGINS_ERROR:
      return "install-error";
    case GST_INSTALL_PLUGINS_CRASHED:
      return "installer-exit-unclean";
    case GST_INSTALL_PLUGINS_PARTIAL_SUCCESS:
      return "partial-success";
    case GST_INSTALL_PLUGINS_USER_ABORT:
      return "user-abort";
    case GST_INSTALL_PLUGINS_STARTED_OK:
      return "started-ok";
    case GST_INSTALL_PLUGINS_INTERNAL_FAILURE:
      return "internal-failure";
    case GST_INSTALL_PLUGINS_HELPER_MISSING:
      return "helper-missing";
    case GST_INSTALL_PLUGINS_INSTALL_IN_PROGRESS:
      return "install-in-progress";
    case GST_INSTALL_PLUGINS_INVALID:
      return "invalid";
    default:
      break;
  }
  return "(UNKNOWN)";
}

/**
 * gst_install_plugins_installation_in_progress:
 * 
 * Checks whether plugin installation (initiated by this application only)
 * is currently in progress.
 *
 * Returns: TRUE if plugin installation is in progress, otherwise FALSE
 *
 * Since: 0.10.12
 */
gboolean
gst_install_plugins_installation_in_progress (void)
{
  return install_in_progress;
}
