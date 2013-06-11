/*
 * GStreamer
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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

#include <stdio.h>

#include <gst/gst.h>
#include <gst/video/gstvideosink.h>
#include "gstgldownload.h"
#include "gstglmemory.h"
#include "gstglfeature.h"
#include "gstglapi.h"

#include "gstgldisplay.h"

#define USING_OPENGL(display) (display->gl_api & GST_GL_API_OPENGL)
#define USING_OPENGL3(display) (display->gl_api & GST_GL_API_OPENGL3)
#define USING_GLES(display) (display->gl_api & GST_GL_API_GLES)
#define USING_GLES2(display) (display->gl_api & GST_GL_API_GLES2)
#define USING_GLES3(display) (display->gl_api & GST_GL_API_GLES3)

GST_DEBUG_CATEGORY_STATIC (gst_gl_display_debug);
#define GST_CAT_DEFAULT gst_gl_display_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_display_debug, "gldisplay", 0, "opengl display");

G_DEFINE_TYPE_WITH_CODE (GstGLDisplay, gst_gl_display, G_TYPE_OBJECT,
    DEBUG_INIT);

#define GST_GL_DISPLAY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_DISPLAY, GstGLDisplayPrivate))

static void gst_gl_display_finalize (GObject * object);

/* Called in the gl thread, protected by lock and unlock */
gpointer gst_gl_display_thread_create_context (GstGLDisplay * display);
void gst_gl_display_thread_destroy_context (GstGLDisplay * display);
void gst_gl_display_thread_run_generic (GstGLDisplay * display);

struct _GstGLDisplayPrivate
{
  /* conditions */
  GCond cond_create_context;
  GCond cond_destroy_context;

  /* generic gl code */
  GstGLDisplayThreadFunc generic_callback;
  gpointer data;
};

/*------------------------------------------------------------
  --------------------- For klass GstGLDisplay ---------------
  ----------------------------------------------------------*/
static void
gst_gl_display_class_init (GstGLDisplayClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstGLDisplayPrivate));

  G_OBJECT_CLASS (klass)->finalize = gst_gl_display_finalize;
}


static void
gst_gl_display_init (GstGLDisplay * display)
{
  display->priv = GST_GL_DISPLAY_GET_PRIVATE (display);

  /* thread safe */
  g_mutex_init (&display->mutex);

  g_cond_init (&display->priv->cond_create_context);
  g_cond_init (&display->priv->cond_destroy_context);

  display->gl_vtable = g_slice_alloc0 (sizeof (GstGLFuncs));

  display->gl_window = gst_gl_window_new ();

  gst_gl_memory_init ();
}

static void
gst_gl_display_finalize (GObject * object)
{
  GstGLDisplay *display = GST_GL_DISPLAY (object);

  if (display->gl_window) {
    gst_gl_display_lock (display);

    gst_gl_window_set_resize_callback (display->gl_window, NULL, NULL);
    gst_gl_window_set_draw_callback (display->gl_window, NULL, NULL);
    gst_gl_window_set_close_callback (display->gl_window, NULL, NULL);

    if (display->context_created) {
      GST_INFO ("send quit gl window loop");
      gst_gl_window_quit (display->gl_window,
          GST_GL_WINDOW_CB (gst_gl_display_thread_destroy_context), display);

      GST_INFO ("quit sent to gl window loop");

      g_cond_wait (&display->priv->cond_destroy_context, &display->mutex);
      GST_INFO ("quit received from gl window");
    }
    gst_gl_display_unlock (display);
  }

  if (display->gl_thread) {
    gpointer ret = g_thread_join (display->gl_thread);
    GST_INFO ("gl thread joined");
    if (ret != NULL)
      GST_ERROR ("gl thread returned a not null pointer");
    display->gl_thread = NULL;
  }

  g_mutex_clear (&display->mutex);

  g_cond_clear (&display->priv->cond_destroy_context);
  g_cond_clear (&display->priv->cond_create_context);

  if (display->error_message) {
    g_free (display->error_message);
    display->error_message = NULL;
  }

  if (display->gl_vtable) {
    g_slice_free (GstGLFuncs, display->gl_vtable);
    display->gl_vtable = NULL;
  }
}

static gboolean
_create_context_gles2 (GstGLDisplay * display, gint * gl_major, gint * gl_minor)
{
  const GstGLFuncs *gl;
  GLenum gl_err = GL_NO_ERROR;

  gl = display->gl_vtable;

  GST_INFO ("GL_VERSION: %s", gl->GetString (GL_VERSION));
  GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s",
      gl->GetString (GL_SHADING_LANGUAGE_VERSION));
  GST_INFO ("GL_VENDOR: %s", gl->GetString (GL_VENDOR));
  GST_INFO ("GL_RENDERER: %s", gl->GetString (GL_RENDERER));

  gl_err = gl->GetError ();
  if (gl_err != GL_NO_ERROR) {
    gst_gl_display_set_error (display, "glGetString error: 0x%x", gl_err);
    return FALSE;
  }
#if GST_GL_HAVE_GLES2
  if (!GL_ES_VERSION_2_0) {
    gst_gl_display_set_error (display, "OpenGL|ES >= 2.0 is required");
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
_create_context_opengl (GstGLDisplay * display, gint * gl_major,
    gint * gl_minor)
{
  const GstGLFuncs *gl;
  guint maj, min;
  GLenum gl_err = GL_NO_ERROR;
  GString *opengl_version = NULL;

  gl = display->gl_vtable;

  GST_INFO ("GL_VERSION: %s", gl->GetString (GL_VERSION));
  GST_INFO ("GL_SHADING_LANGUAGE_VERSION: %s",
      gl->GetString (GL_SHADING_LANGUAGE_VERSION));
  GST_INFO ("GL_VENDOR: %s", gl->GetString (GL_VENDOR));
  GST_INFO ("GL_RENDERER: %s", gl->GetString (GL_RENDERER));

  gl_err = gl->GetError ();
  if (gl_err != GL_NO_ERROR) {
    gst_gl_display_set_error (display, "glGetString error: 0x%x", gl_err);
    return FALSE;
  }
  opengl_version =
      g_string_truncate (g_string_new ((gchar *) gl->GetString (GL_VERSION)),
      3);

  sscanf (opengl_version->str, "%d.%d", &maj, &min);

  g_string_free (opengl_version, TRUE);

  /* OpenGL > 1.2.0 */
  if ((maj < 1) || (maj < 2 && maj >= 1 && min < 2)) {
    gst_gl_display_set_error (display, "OpenGL >= 1.2.0 required, found %u.%u",
        maj, min);
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

gpointer
gst_gl_display_thread_create_context (GstGLDisplay * display)
{
  GstGLFuncs *gl;
  gint gl_major = 0;
  gboolean ret = FALSE;
  GError *error = NULL;
  GstGLAPI compiled_api, user_api;
  gchar *api_string;
  gchar *compiled_api_s;
  gchar *user_api_string;
  const gchar *user_choice;

  gst_gl_display_lock (display);

  gl = display->gl_vtable;
  compiled_api = _compiled_api ();

  if (!display->gl_window) {
    gst_gl_display_set_error (display, "Failed to create opengl window");
    goto failure;
  }

  user_choice = g_getenv ("GST_GL_API");

  user_api = _parse_gl_api (user_choice);
  user_api_string = gst_gl_api_string (user_api);

  compiled_api_s = gst_gl_api_string (compiled_api);

  GST_INFO ("Attempting to create opengl context. user chosen api(s):%s, "
      "compiled api support:%s", user_api_string, compiled_api_s);

  if (!gst_gl_window_create_context (display->gl_window,
          compiled_api & user_api, display->external_gl_context, &error)) {
    gst_gl_display_set_error (display,
        error ? error->message : "Failed to create gl window");
    g_free (compiled_api_s);
    g_free (user_api_string);
    goto failure;
  }
  GST_INFO ("window created context");

  display->gl_api = gst_gl_window_get_gl_api (display->gl_window);
  g_assert (display->gl_api != GST_GL_API_NONE
      && display->gl_api != GST_GL_API_ANY);

  api_string = gst_gl_api_string (display->gl_api);
  GST_INFO ("available GL APIs: %s", api_string);

  if (((compiled_api & display->gl_api) & user_api) == GST_GL_API_NONE) {
    gst_gl_display_set_error (display, "failed to create context, window "
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

  gl->GetError =
      gst_gl_window_get_proc_address (display->gl_window, "glGetError");
  gl->GetString =
      gst_gl_window_get_proc_address (display->gl_window, "glGetString");

  if (!gl->GetError || !gl->GetString) {
    gst_gl_display_set_error (display,
        "could not GetProcAddress core opengl functions");
    goto failure;
  }

  /* gl api specific code */
  if (!ret && USING_OPENGL (display))
    ret = _create_context_opengl (display, &gl_major, NULL);
  if (!ret && USING_GLES2 (display))
    ret = _create_context_gles2 (display, &gl_major, NULL);

  if (!ret || !gl_major) {
    GST_WARNING ("GL api specific initialization failed");
    goto failure;
  }

  g_cond_signal (&display->priv->cond_create_context);

  display->isAlive = TRUE;
  gst_gl_display_unlock (display);

  gst_gl_window_run (display->gl_window);

  GST_INFO ("loop exited\n");

  gst_gl_display_lock (display);

  display->isAlive = FALSE;

  g_object_unref (G_OBJECT (display->gl_window));

  display->gl_window = NULL;

  g_cond_signal (&display->priv->cond_destroy_context);

  gst_gl_display_unlock (display);

  return NULL;

failure:
  {
    if (display->gl_window) {
      g_object_unref (display->gl_window);
      display->gl_window = NULL;
    }

    g_cond_signal (&display->priv->cond_create_context);
    gst_gl_display_unlock (display);
    return NULL;
  }
}

void
gst_gl_display_thread_destroy_context (GstGLDisplay * display)
{
  GST_INFO ("Context destroyed");
}

//------------------------------------------------------------
//------------------ BEGIN GL THREAD PROCS -------------------
//------------------------------------------------------------

/* Called in the gl thread */

void
gst_gl_display_set_error (GstGLDisplay * display, const char *format, ...)
{
  va_list args;

  if (display->error_message)
    g_free (display->error_message);

  va_start (args, format);
  display->error_message = g_strdup_vprintf (format, args);
  va_end (args);

  GST_WARNING ("%s", display->error_message);

  display->isAlive = FALSE;
}

void
gst_gl_display_thread_run_generic (GstGLDisplay * display)
{
  GST_TRACE ("running function:%p data:%p",
      display->priv->generic_callback, display->priv->data);

  display->priv->generic_callback (display, display->priv->data);
}

/*------------------------------------------------------------
  --------------------- BEGIN PUBLIC -------------------------
  ----------------------------------------------------------*/

void
gst_gl_display_lock (GstGLDisplay * display)
{
  g_mutex_lock (&display->mutex);
}

void
gst_gl_display_unlock (GstGLDisplay * display)
{
  g_mutex_unlock (&display->mutex);
}

/* Called by the first gl element of a video/x-raw-gl flow */
GstGLDisplay *
gst_gl_display_new (void)
{
  return g_object_new (GST_GL_TYPE_DISPLAY, NULL);
}

/* Create an opengl context (one context for one GstGLDisplay) */
gboolean
gst_gl_display_create_context (GstGLDisplay * display,
    gulong external_gl_context)
{
  gboolean isAlive = FALSE;

  gst_gl_display_lock (display);

  if (!display->context_created) {
    display->external_gl_context = external_gl_context;

    display->gl_thread = g_thread_new ("gstglcontext",
        (GThreadFunc) gst_gl_display_thread_create_context, display);

    g_cond_wait (&display->priv->cond_create_context, &display->mutex);

    display->context_created = TRUE;

    GST_INFO ("gl thread created");
  }

  isAlive = display->isAlive;

  gst_gl_display_unlock (display);

  return isAlive;
}

void
gst_gl_display_thread_add (GstGLDisplay * display,
    GstGLDisplayThreadFunc func, gpointer data)
{
  gst_gl_display_lock (display);
  display->priv->data = data;
  display->priv->generic_callback = func;
  gst_gl_window_send_message (display->gl_window,
      GST_GL_WINDOW_CB (gst_gl_display_thread_run_generic), display);
  gst_gl_display_unlock (display);
}

guintptr
gst_gl_display_get_internal_gl_context (GstGLDisplay * display)
{
  gulong external_gl_context = 0;
  gst_gl_display_lock (display);
  external_gl_context = gst_gl_window_get_gl_context (display->gl_window);
  gst_gl_display_unlock (display);
  return external_gl_context;
}

void
gst_gl_display_activate_gl_context (GstGLDisplay * display, gboolean activate)
{
  if (!activate)
    gst_gl_display_lock (display);
  gst_gl_window_activate (display->gl_window, activate);
  if (activate)
    gst_gl_display_unlock (display);
}

GstGLAPI
gst_gl_display_get_gl_api_unlocked (GstGLDisplay * display)
{
  return display->gl_api;
}

GstGLAPI
gst_gl_display_get_gl_api (GstGLDisplay * display)
{
  GstGLAPI api;

  gst_gl_display_lock (display);
  api = gst_gl_display_get_gl_api_unlocked (display);
  gst_gl_display_unlock (display);

  return api;
}

gpointer
gst_gl_display_get_gl_vtable (GstGLDisplay * display)
{
  gpointer gl;

  gst_gl_display_lock (display);
  gl = display->gl_vtable;
  gst_gl_display_unlock (display);

  return gl;
}

//------------------------------------------------------------
//------------------------ END PUBLIC ------------------------
//------------------------------------------------------------
