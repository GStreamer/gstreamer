/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <gmodule.h>
#include <stdio.h>

#include "gstglwindow.h"

#if GST_GL_HAVE_WINDOW_X11
#include "x11/gstglwindow_x11.h"
#endif
#if GST_GL_HAVE_WINDOW_WIN32
#include "win32/gstglwindow_win32.h"
#endif
#if GST_GL_HAVE_WINDOW_COCOA
#include "cocoa/gstglwindow_cocoa.h"
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
#include "wayland/gstglwindow_wayland_egl.h"
#endif

#include "gstglfeature.h"

#define USING_OPENGL(display) (display->gl_api & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (display->gl_api & GST_GL_API_OPENGL3)
#define USING_GLES(display) (display->gl_api & GST_GL_API_GLES)
#define USING_GLES2(display) (display->gl_api & GST_GL_API_GLES2)
#define USING_GLES3(display) (display->gl_api & GST_GL_API_GLES3)

#define GST_CAT_DEFAULT gst_gl_window_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define gst_gl_window_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstGLWindow, gst_gl_window, G_TYPE_OBJECT);

#define GST_GL_WINDOW_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_WINDOW, GstGLWindowPrivate))

struct _GstGLWindowPrivate
{
  GstGLDisplay *display;

  GThread *gl_thread;

  /* conditions */
  GMutex render_lock;
  GCond cond_create_context;
  GCond cond_destroy_context;

  gboolean context_created;
  gboolean alive;

  guintptr external_gl_context;
  GstGLAPI gl_api;
  GError **error;
};

static gpointer _gst_gl_window_thread_create_context (GstGLWindow * window);
static void gst_gl_window_finalize (GObject * object);

GQuark
gst_gl_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error-quark");
}

static void
gst_gl_window_init (GstGLWindow * window)
{
  window->priv = GST_GL_WINDOW_GET_PRIVATE (window);

  g_mutex_init (&window->lock);
  window->need_lock = TRUE;

  g_mutex_init (&window->priv->render_lock);
  g_cond_init (&window->priv->cond_create_context);
  g_cond_init (&window->priv->cond_destroy_context);
  window->priv->context_created = FALSE;
}

static void
gst_gl_window_class_init (GstGLWindowClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLWindowPrivate));

  klass->get_proc_address =
      GST_DEBUG_FUNCPTR (gst_gl_window_default_get_proc_address);

  G_OBJECT_CLASS (klass)->finalize = gst_gl_window_finalize;
}

GstGLWindow *
gst_gl_window_new (GstGLDisplay * display)
{
  GstGLWindow *window = NULL;
  const gchar *user_choice;
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (gst_gl_window_debug, "glwindow", 0,
        "glwindow element");
    g_once_init_leave (&_init, 1);
  }

  user_choice = g_getenv ("GST_GL_WINDOW");
  GST_INFO ("creating a window, user choice:%s", user_choice);

#if GST_GL_HAVE_WINDOW_X11
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "x11")))
    window = GST_GL_WINDOW (gst_gl_window_x11_new ());
#endif
#if GST_GL_HAVE_WINDOW_WIN32
  if (!window && (!user_choice || g_strstr_len (user_choice, 5, "win32")))
    window = GST_GL_WINDOW (gst_gl_window_win32_new ());
#endif
#if GST_GL_HAVE_WINDOW_COCOA
  if (!window && (!user_choice || g_strstr_len (user_choice, 5, "cocoa"))) {
    window = GST_GL_WINDOW (gst_gl_window_cocoa_new ());
  }
#endif
#if GST_GL_HAVE_WINDOW_WAYLAND
  if (!window && (!user_choice || g_strstr_len (user_choice, 7, "wayland")))
    window = GST_GL_WINDOW (gst_gl_window_wayland_egl_new ());
#endif
  if (!window) {
    /* subclass returned a NULL window */
    GST_WARNING ("Could not create window. user specified %s",
        user_choice ? user_choice : "(null)");

    return NULL;
  }

  window->priv->display = display;

  return window;
}

static void
gst_gl_window_finalize (GObject * object)
{
  GstGLWindow *window = GST_GL_WINDOW (object);
  GstGLWindowClass *window_class = GST_GL_WINDOW_GET_CLASS (window);

  if (window) {
    gst_gl_window_set_resize_callback (window, NULL, NULL);
    gst_gl_window_set_draw_callback (window, NULL, NULL);
    gst_gl_window_set_close_callback (window, NULL, NULL);

    if (window->priv->alive) {
      GST_INFO ("send quit gl window loop");
      gst_gl_window_quit (window, NULL, NULL);
    }
  }

  if (window->priv->gl_thread) {
    gpointer ret = g_thread_join (window->priv->gl_thread);
    GST_INFO ("gl thread joined");
    if (ret != NULL)
      GST_ERROR ("gl thread returned a non-null pointer");
    window->priv->gl_thread = NULL;
  }

  if (window_class->close) {
    window_class->close (window);
  }

  g_mutex_clear (&window->priv->render_lock);

  g_cond_clear (&window->priv->cond_destroy_context);
  g_cond_clear (&window->priv->cond_create_context);

  G_OBJECT_CLASS (gst_gl_window_parent_class)->finalize (object);
}

guintptr
gst_gl_window_get_gl_context (GstGLWindow * window)
{
  GstGLWindowClass *window_class;
  guintptr result;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), 0);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->get_gl_context != NULL, 0);

  GST_GL_WINDOW_LOCK (window);
  result = window_class->get_gl_context (window);
  GST_GL_WINDOW_UNLOCK (window);

  return result;
}

gboolean
gst_gl_window_activate (GstGLWindow * window, gboolean activate)
{
  GstGLWindowClass *window_class;
  gboolean result;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), FALSE);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->activate != NULL, FALSE);

  GST_GL_WINDOW_LOCK (window);
  result = window_class->activate (window, activate);
  GST_GL_WINDOW_UNLOCK (window);

  return result;
}

void
gst_gl_window_set_window_handle (GstGLWindow * window, guintptr handle)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  g_return_if_fail (handle != 0);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->set_window_handle != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->set_window_handle (window, handle);
  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_draw_unlocked (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->draw_unlocked != NULL);

  window_class->draw_unlocked (window, width, height);
}

void
gst_gl_window_draw (GstGLWindow * window, guint width, guint height)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->draw != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->draw (window, width, height);
  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_run (GstGLWindow * window)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->run != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->run (window);
  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_quit (GstGLWindow * window, GstGLWindowCB callback, gpointer data)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->quit != NULL);

  GST_GL_WINDOW_LOCK (window);

  window->priv->alive = FALSE;

  window->close = callback;
  window->close_data = data;
  window_class->quit (window, callback, data);

  GST_INFO ("quit sent to gl window loop");

  GST_GL_WINDOW_UNLOCK (window);

  g_cond_wait (&window->priv->cond_destroy_context, &window->priv->render_lock);
  GST_INFO ("quit received from gl window");
}

void
gst_gl_window_send_message (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  GstGLWindowClass *window_class;

  g_return_if_fail (GST_GL_IS_WINDOW (window));
  g_return_if_fail (callback != NULL);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_if_fail (window_class->quit != NULL);

  GST_GL_WINDOW_LOCK (window);
  window_class->send_message (window, callback, data);
  GST_GL_WINDOW_UNLOCK (window);
}

/**
 * gst_gl_window_set_need_lock:
 *
 * window: a #GstGLWindow
 * need_lock: whether the @window needs to lock for concurrent access
 *
 * This API is intended only for subclasses of #GstGLWindow in order to ensure
 * correct interaction with the underlying window system.
 */
void
gst_gl_window_set_need_lock (GstGLWindow * window, gboolean need_lock)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  window->need_lock = need_lock;
}

void
gst_gl_window_set_draw_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  window->draw = callback;
  window->draw_data = data;

  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_set_resize_callback (GstGLWindow * window,
    GstGLWindowResizeCB callback, gpointer data)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  window->resize = callback;
  window->resize_data = data;

  GST_GL_WINDOW_UNLOCK (window);
}

void
gst_gl_window_set_close_callback (GstGLWindow * window, GstGLWindowCB callback,
    gpointer data)
{
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  GST_GL_WINDOW_LOCK (window);

  window->close = callback;
  window->close_data = data;

  GST_GL_WINDOW_UNLOCK (window);
}

GstGLAPI
gst_gl_window_get_gl_api (GstGLWindow * window)
{
  GstGLAPI ret;
  GstGLWindowClass *window_class;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), GST_GL_API_NONE);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->get_gl_api != NULL, GST_GL_API_NONE);

  GST_GL_WINDOW_LOCK (window);

  ret = window_class->get_gl_api (window);

  GST_GL_WINDOW_UNLOCK (window);

  return ret;
}

gpointer
gst_gl_window_get_proc_address (GstGLWindow * window, const gchar * name)
{
  gpointer ret;
  GstGLWindowClass *window_class;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), NULL);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->get_proc_address != NULL, NULL);

  GST_GL_WINDOW_LOCK (window);

  ret = window_class->get_proc_address (window, name);

  GST_GL_WINDOW_UNLOCK (window);

  return ret;
}

gpointer
gst_gl_window_default_get_proc_address (GstGLWindow * window,
    const gchar * name)
{
  static GModule *module = NULL;
  gpointer ret = NULL;

  if (!module)
    module = g_module_open (NULL, G_MODULE_BIND_LAZY);

  if (module) {
    if (!g_module_symbol (module, name, &ret))
      return NULL;
  }

  return ret;
}

/* Create an opengl context (one context for one GstGLDisplay) */
gboolean
gst_gl_window_create_context (GstGLWindow * window,
    guintptr external_gl_context, GError ** error)
{
  gboolean alive = FALSE;
  GstGLWindowClass *window_class;

  g_return_val_if_fail (GST_GL_IS_WINDOW (window), FALSE);
  window_class = GST_GL_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (window_class->create_context != NULL, FALSE);

  g_mutex_lock (&window->priv->render_lock);

  if (window_class->open) {
    if (!(alive = window_class->open (window, error)))
      goto out;
  }

  if (!window->priv->context_created) {
    window->priv->external_gl_context = external_gl_context;
    window->priv->error = error;

    window->priv->gl_thread = g_thread_new ("gstglcontext",
        (GThreadFunc) _gst_gl_window_thread_create_context, window);

    g_cond_wait (&window->priv->cond_create_context,
        &window->priv->render_lock);

    window->priv->context_created = TRUE;

    GST_INFO ("gl thread created");
  }

  alive = window->priv->alive;

  g_mutex_unlock (&window->priv->render_lock);

out:
  return alive;
}

gboolean
gst_gl_window_is_running (GstGLWindow * window)
{
  return window->priv->alive;
}

static gboolean
_create_context_gles2 (GstGLWindow * window, gint * gl_major, gint * gl_minor,
    GError ** error)
{
  GstGLDisplay *display;
  const GstGLFuncs *gl;
  GLenum gl_err = GL_NO_ERROR;

  display = window->priv->display;
  gl = display->gl_vtable;

  GST_INFO ("GL_VERSION: %s", gl->GetString (GL_VERSION));
  GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s",
      gl->GetString (GL_SHADING_LANGUAGE_VERSION));
  GST_INFO ("GL_VENDOR: %s", gl->GetString (GL_VENDOR));
  GST_INFO ("GL_RENDERER: %s", gl->GetString (GL_RENDERER));

  gl_err = gl->GetError ();
  if (gl_err != GL_NO_ERROR) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
        "glGetString error: 0x%x", gl_err);
    return FALSE;
  }
#if GST_GL_HAVE_GLES2
  if (!GL_ES_VERSION_2_0) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_OLD_LIBS,
        "OpenGL|ES >= 2.0 is required");
    return FALSE;
  }
#endif

  _gst_gl_feature_check_ext_functions (display, 0, 0,
      (const gchar *) gl->GetString (GL_EXTENSIONS));

  if (gl_major)
    *gl_major = 2;
  if (gl_minor)
    *gl_minor = 0;

  return TRUE;
}

gboolean
_create_context_opengl (GstGLWindow * window, gint * gl_major, gint * gl_minor,
    GError ** error)
{
  GstGLDisplay *display;
  const GstGLFuncs *gl;
  guint maj, min;
  GLenum gl_err = GL_NO_ERROR;
  GString *opengl_version = NULL;

  display = window->priv->display;
  gl = display->gl_vtable;

  GST_INFO ("GL_VERSION: %s", gl->GetString (GL_VERSION));
  GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s",
      gl->GetString (GL_SHADING_LANGUAGE_VERSION));
  GST_INFO ("GL_VENDOR: %s", gl->GetString (GL_VENDOR));
  GST_INFO ("GL_RENDERER: %s", gl->GetString (GL_RENDERER));

  gl_err = gl->GetError ();
  if (gl_err != GL_NO_ERROR) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
        "glGetString error: 0x%x", gl_err);
    return FALSE;
  }
  opengl_version =
      g_string_truncate (g_string_new ((gchar *) gl->GetString (GL_VERSION)),
      3);

  sscanf (opengl_version->str, "%d.%d", &maj, &min);

  g_string_free (opengl_version, TRUE);

  /* OpenGL > 1.2.0 */
  if ((maj < 1) || (maj < 2 && maj >= 1 && min < 2)) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_OLD_LIBS,
        "OpenGL >= 1.2.0 required, found %u.%u", maj, min);
    return FALSE;
  }

  _gst_gl_feature_check_ext_functions (display, maj, min,
      (const gchar *) gl->GetString (GL_EXTENSIONS));

  if (gl_major)
    *gl_major = maj;
  if (gl_minor)
    *gl_minor = min;

  return TRUE;
}

GstGLAPI
_compiled_api (void)
{
  GstGLAPI ret = GST_GL_API_NONE;

#if GST_GL_HAVE_OPENGL
  ret |= GST_GL_API_OPENGL;
#endif
#if GST_GL_HAVE_GLES2
  ret |= GST_GL_API_GLES2;
#endif

  return ret;
}

GstGLAPI
_parse_gl_api (const gchar * apis_s)
{
  GstGLAPI ret = GST_GL_API_NONE;
  gchar *apis = (gchar *) apis_s;

  while (apis) {
    if (apis[0] == '\0') {
      break;
    } else if (apis[0] == ' ' || apis[0] == ',') {
      apis = &apis[1];
    } else if (g_strstr_len (apis, 7, "opengl3")) {
      ret |= GST_GL_API_OPENGL3;
      apis = &apis[7];
    } else if (g_strstr_len (apis, 6, "opengl")) {
      ret |= GST_GL_API_OPENGL;
      apis = &apis[6];
    } else if (g_strstr_len (apis, 5, "gles1")) {
      ret |= GST_GL_API_GLES;
      apis = &apis[5];
    } else if (g_strstr_len (apis, 5, "gles2")) {
      ret |= GST_GL_API_GLES2;
      apis = &apis[5];
    } else if (g_strstr_len (apis, 5, "gles3")) {
      ret |= GST_GL_API_GLES3;
      apis = &apis[5];
    } else {
      break;
    }
  }

  if (ret == GST_GL_API_NONE)
    ret = GST_GL_API_ANY;

  return ret;
}

static gpointer
_gst_gl_window_thread_create_context (GstGLWindow * window)
{
  GstGLWindowClass *window_class;
  GstGLDisplay *display;
  GstGLFuncs *gl;
  gint gl_major = 0;
  gboolean ret = FALSE;
  GstGLAPI compiled_api, user_api;
  gchar *api_string;
  gchar *compiled_api_s;
  gchar *user_api_string;
  const gchar *user_choice;
  GError **error;

  window_class = GST_GL_WINDOW_GET_CLASS (window);
  error = window->priv->error;
  display = window->priv->display;

  g_mutex_lock (&window->priv->render_lock);

  gl = display->gl_vtable;
  compiled_api = _compiled_api ();

  user_choice = g_getenv ("GST_GL_API");

  user_api = _parse_gl_api (user_choice);
  user_api_string = gst_gl_api_string (user_api);

  compiled_api_s = gst_gl_api_string (compiled_api);

  GST_INFO ("Attempting to create opengl context. user chosen api(s):%s, "
      "compiled api support:%s", user_api_string, compiled_api_s);

  if (!window_class->create_context (window, compiled_api & user_api,
          window->priv->external_gl_context, error)) {
    g_assert (error == NULL || *error != NULL);
    g_free (compiled_api_s);
    g_free (user_api_string);
    goto failure;
  }
  GST_INFO ("window created context");

  display->gl_api = gst_gl_window_get_gl_api (window);
  g_assert (display->gl_api != GST_GL_API_NONE
      && display->gl_api != GST_GL_API_ANY);

  api_string = gst_gl_api_string (display->gl_api);
  GST_INFO ("available GL APIs: %s", api_string);

  if (((compiled_api & display->gl_api) & user_api) == GST_GL_API_NONE) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_WRONG_API,
        "failed to create context, window "
        "could not provide correct api. user:%s, compiled:%s, window:%s",
        user_api_string, compiled_api_s, api_string);
    g_free (api_string);
    g_free (compiled_api_s);
    g_free (user_api_string);
    goto failure;
  }

  g_free (api_string);
  g_free (compiled_api_s);
  g_free (user_api_string);

  gl->GetError = gst_gl_window_get_proc_address (window, "glGetError");
  gl->GetString = gst_gl_window_get_proc_address (window, "glGetString");

  if (!gl->GetError || !gl->GetString) {
    g_set_error (error, GST_GL_WINDOW_ERROR, GST_GL_WINDOW_ERROR_FAILED,
        "could not GetProcAddress core opengl functions");
    goto failure;
  }

  /* gl api specific code */
  if (!ret && USING_OPENGL (display))
    ret = _create_context_opengl (window, &gl_major, NULL, error);
  if (!ret && USING_GLES2 (display))
    ret = _create_context_gles2 (window, &gl_major, NULL, error);

  if (!ret)
    goto failure;

  g_cond_signal (&window->priv->cond_create_context);

  window->priv->alive = TRUE;
  g_mutex_unlock (&window->priv->render_lock);

  gst_gl_window_run (window);

  GST_INFO ("loop exited\n");

  g_mutex_lock (&window->priv->render_lock);

  window->priv->alive = FALSE;

  g_cond_signal (&window->priv->cond_destroy_context);

  g_mutex_unlock (&window->priv->render_lock);

  return NULL;

failure:
  {
    g_cond_signal (&window->priv->cond_create_context);
    g_mutex_unlock (&window->priv->render_lock);
    return NULL;
  }
}
