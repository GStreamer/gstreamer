/* This library is free software; you can redistribute it and/or
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

#include "gst_private.h"
#include "gst.h"

#include <jni.h>
#include <android/log.h>
#include <string.h>

/* AMC plugin uses g_module_symbol() to find those symbols */
GST_API jobject gst_android_get_application_context (void);
GST_API jobject gst_android_get_application_class_loader (void);
GST_API JavaVM *gst_android_get_java_vm (void);

/* XXX: Workaround for Android <21 making signal() an inline function
 * around bsd_signal(), and Android >= 21 not having any bsd_signal()
 * symbol but only signal().
 * See https://bugzilla.gnome.org/show_bug.cgi?id=766235
 */
static gpointer
load_real_signal (gpointer data)
{
  GModule *module;
  gpointer ret = NULL;

  module = g_module_open ("libc.so", G_MODULE_BIND_LOCAL);
  g_module_symbol (module, "signal", &ret);

  /* As fallback, let's try bsd_signal */
  if (ret == NULL) {
    g_warning ("Can't find signal(3) in libc.so!");
    g_module_symbol (module, "bsd_signal", &ret);
  }

  g_module_close (module);

  return ret;
}

__sighandler_t bsd_signal (int signum, __sighandler_t handler)
    __attribute__((weak));
__sighandler_t
bsd_signal (int signum, __sighandler_t handler)
{
  static GOnce gonce = G_ONCE_INIT;
  __sighandler_t (*real_signal) (int signum, __sighandler_t handler);

  g_once (&gonce, load_real_signal, NULL);

  real_signal = gonce.retval;
  g_assert (real_signal != NULL);

  return real_signal (signum, handler);
}

static jobject _context = NULL;
static jobject _class_loader = NULL;
static JavaVM *_java_vm = NULL;
static GstClockTime _priv_gst_info_start_time;

static void
glib_print_handler (const gchar * string)
{
  __android_log_print (ANDROID_LOG_INFO, "GLib+stdout", "%s", string);
}

static void
glib_printerr_handler (const gchar * string)
{
  __android_log_print (ANDROID_LOG_ERROR, "GLib+stderr", "%s", string);
}


/* Based on GLib's default handler */
#define CHAR_IS_SAFE(wc) (!((wc < 0x20 && wc != '\t' && wc != '\n' && wc != '\r') || \
			    (wc == 0x7f) || \
			    (wc >= 0x80 && wc < 0xa0)))
#define FORMAT_UNSIGNED_BUFSIZE ((GLIB_SIZEOF_LONG * 3) + 3)
#define	STRING_BUFFER_SIZE	(FORMAT_UNSIGNED_BUFSIZE + 32)
#define	ALERT_LEVELS		(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)
#define DEFAULT_LEVELS (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE)
#define INFO_LEVELS (G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG)

static void
escape_string (GString * string)
{
  const char *p = string->str;
  gunichar wc;

  while (p < string->str + string->len) {
    gboolean safe;

    wc = g_utf8_get_char_validated (p, -1);
    if (wc == (gunichar) - 1 || wc == (gunichar) - 2) {
      gchar *tmp;
      guint pos;

      pos = p - string->str;

      /* Emit invalid UTF-8 as hex escapes
       */
      tmp = g_strdup_printf ("\\x%02x", (guint) (guchar) * p);
      g_string_erase (string, pos, 1);
      g_string_insert (string, pos, tmp);

      p = string->str + (pos + 4);      /* Skip over escape sequence */

      g_free (tmp);
      continue;
    }
    if (wc == '\r') {
      safe = *(p + 1) == '\n';
    } else {
      safe = CHAR_IS_SAFE (wc);
    }

    if (!safe) {
      gchar *tmp;
      guint pos;

      pos = p - string->str;

      /* Largest char we escape is 0x0a, so we don't have to worry
       * about 8-digit \Uxxxxyyyy
       */
      tmp = g_strdup_printf ("\\u%04x", wc);
      g_string_erase (string, pos, g_utf8_next_char (p) - p);
      g_string_insert (string, pos, tmp);
      g_free (tmp);

      p = string->str + (pos + 6);      /* Skip over escape sequence */
    } else
      p = g_utf8_next_char (p);
  }
}

static void
glib_log_handler (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  gchar *string;
  GString *gstring;
  const gchar *domains;
  gint android_log_level;
  gchar *tag;

  if ((log_level & DEFAULT_LEVELS) || (log_level >> G_LOG_LEVEL_USER_SHIFT))
    goto emit;

  domains = g_getenv ("G_MESSAGES_DEBUG");
  if (((log_level & INFO_LEVELS) == 0) ||
      domains == NULL ||
      (strcmp (domains, "all") != 0 && (!log_domain
              || !strstr (domains, log_domain))))
    return;

emit:

  if (log_domain)
    tag = g_strdup_printf ("GLib+%s", log_domain);
  else
    tag = g_strdup ("GLib");

  switch (log_level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_ERROR:
    case G_LOG_LEVEL_CRITICAL:
      android_log_level = ANDROID_LOG_ERROR;
      break;
    case G_LOG_LEVEL_WARNING:
      android_log_level = ANDROID_LOG_WARN;
      break;
    case G_LOG_LEVEL_MESSAGE:
    case G_LOG_LEVEL_INFO:
      android_log_level = ANDROID_LOG_INFO;
      break;
    case G_LOG_LEVEL_DEBUG:
      android_log_level = ANDROID_LOG_DEBUG;
      break;
    default:
      android_log_level = ANDROID_LOG_VERBOSE;
      break;
  }

  gstring = g_string_new (NULL);
  if (!message) {
    g_string_append (gstring, "(NULL) message");
  } else {
    GString *msg = g_string_new (message);
    escape_string (msg);
    g_string_append (gstring, msg->str);
    g_string_free (msg, TRUE);
  }
  string = g_string_free (gstring, FALSE);

  __android_log_print (android_log_level, tag, "%s", string);

  g_free (string);
  g_free (tag);
}

static void
gst_debug_logcat (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line,
    GObject * object, GstDebugMessage * message, gpointer unused)
{
  GstClockTime elapsed;
  gint android_log_level;
  gchar *tag;

  if (level > gst_debug_category_get_threshold (category))
    return;

  elapsed = GST_CLOCK_DIFF (_priv_gst_info_start_time,
      gst_util_get_timestamp ());

  switch (level) {
    case GST_LEVEL_ERROR:
      android_log_level = ANDROID_LOG_ERROR;
      break;
    case GST_LEVEL_WARNING:
      android_log_level = ANDROID_LOG_WARN;
      break;
    case GST_LEVEL_FIXME:
    case GST_LEVEL_INFO:
      android_log_level = ANDROID_LOG_INFO;
      break;
    case GST_LEVEL_DEBUG:
      android_log_level = ANDROID_LOG_DEBUG;
      break;
    default:
      android_log_level = ANDROID_LOG_VERBOSE;
      break;
  }

  tag = g_strdup_printf ("GStreamer+%s",
      gst_debug_category_get_name (category));

  if (object) {
    gchar *obj;

    if (GST_IS_PAD (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s:%s>", GST_DEBUG_PAD_NAME (object));
    } else if (GST_IS_OBJECT (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s>", GST_OBJECT_NAME (object));
    } else if (G_IS_OBJECT (object)) {
      obj = g_strdup_printf ("<%s@%p>", G_OBJECT_TYPE_NAME (object), object);
    } else {
      obj = g_strdup_printf ("<%p>", object);
    }

    __android_log_print (android_log_level, tag,
        "%" GST_TIME_FORMAT " %p %s:%d:%s:%s %s\n",
        GST_TIME_ARGS (elapsed), g_thread_self (),
        file, line, function, obj, gst_debug_message_get (message));

    g_free (obj);
  } else {
    __android_log_print (android_log_level, tag,
        "%" GST_TIME_FORMAT " %p %s:%d:%s %s\n",
        GST_TIME_ARGS (elapsed), g_thread_self (),
        file, line, function, gst_debug_message_get (message));
  }
  g_free (tag);
}

static gboolean
get_application_dirs (JNIEnv * env, jobject context, gchar ** cache_dir,
    gchar ** files_dir)
{
  jclass context_class;
  jmethodID get_cache_dir_id, get_files_dir_id;
  jclass file_class;
  jmethodID get_absolute_path_id;
  jobject dir;
  jstring abs_path;
  const gchar *abs_path_str;

  *cache_dir = *files_dir = NULL;

  context_class = (*env)->GetObjectClass (env, context);
  if (!context_class) {
    return FALSE;
  }
  get_cache_dir_id =
      (*env)->GetMethodID (env, context_class, "getCacheDir",
      "()Ljava/io/File;");
  get_files_dir_id =
      (*env)->GetMethodID (env, context_class, "getFilesDir",
      "()Ljava/io/File;");
  if (!get_cache_dir_id || !get_files_dir_id) {
    (*env)->DeleteLocalRef (env, context_class);
    return FALSE;
  }

  file_class = (*env)->FindClass (env, "java/io/File");
  if (!file_class) {
    (*env)->DeleteLocalRef (env, context_class);
    return FALSE;
  }
  get_absolute_path_id =
      (*env)->GetMethodID (env, file_class, "getAbsolutePath",
      "()Ljava/lang/String;");
  if (!get_absolute_path_id) {
    (*env)->DeleteLocalRef (env, context_class);
    (*env)->DeleteLocalRef (env, file_class);
    return FALSE;
  }

  dir = (*env)->CallObjectMethod (env, context, get_cache_dir_id);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
    (*env)->DeleteLocalRef (env, context_class);
    (*env)->DeleteLocalRef (env, file_class);
    return FALSE;
  }

  if (dir) {
    abs_path = (*env)->CallObjectMethod (env, dir, get_absolute_path_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
      (*env)->DeleteLocalRef (env, dir);
      (*env)->DeleteLocalRef (env, context_class);
      (*env)->DeleteLocalRef (env, file_class);
      return FALSE;
    }
    abs_path_str = (*env)->GetStringUTFChars (env, abs_path, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
      (*env)->DeleteLocalRef (env, abs_path);
      (*env)->DeleteLocalRef (env, dir);
      (*env)->DeleteLocalRef (env, context_class);
      (*env)->DeleteLocalRef (env, file_class);
      return FALSE;
    }
    *cache_dir = abs_path ? g_strdup (abs_path_str) : NULL;

    (*env)->ReleaseStringUTFChars (env, abs_path, abs_path_str);
    (*env)->DeleteLocalRef (env, abs_path);
    (*env)->DeleteLocalRef (env, dir);
  }

  dir = (*env)->CallObjectMethod (env, context, get_files_dir_id);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
    (*env)->DeleteLocalRef (env, context_class);
    (*env)->DeleteLocalRef (env, file_class);
    return FALSE;
  }
  if (dir) {
    abs_path = (*env)->CallObjectMethod (env, dir, get_absolute_path_id);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
      (*env)->DeleteLocalRef (env, dir);
      (*env)->DeleteLocalRef (env, context_class);
      (*env)->DeleteLocalRef (env, file_class);
      return FALSE;
    }
    abs_path_str = (*env)->GetStringUTFChars (env, abs_path, NULL);
    if ((*env)->ExceptionCheck (env)) {
      (*env)->ExceptionDescribe (env);
      (*env)->ExceptionClear (env);
      (*env)->DeleteLocalRef (env, abs_path);
      (*env)->DeleteLocalRef (env, dir);
      (*env)->DeleteLocalRef (env, context_class);
      (*env)->DeleteLocalRef (env, file_class);
      return FALSE;
    }
    *files_dir = files_dir ? g_strdup (abs_path_str) : NULL;

    (*env)->ReleaseStringUTFChars (env, abs_path, abs_path_str);
    (*env)->DeleteLocalRef (env, abs_path);
    (*env)->DeleteLocalRef (env, dir);
  }

  (*env)->DeleteLocalRef (env, file_class);
  (*env)->DeleteLocalRef (env, context_class);

  return TRUE;
}

jobject
gst_android_get_application_context (void)
{
  return _context;
}

jobject
gst_android_get_application_class_loader (void)
{
  return _class_loader;
}

JavaVM *
gst_android_get_java_vm (void)
{
  return _java_vm;
}

static gboolean
init (JNIEnv * env, jobject context)
{
  jclass context_cls = NULL;
  jmethodID get_class_loader_id = 0;

  jobject class_loader = NULL;

  context_cls = (*env)->GetObjectClass (env, context);
  if (!context_cls) {
    return FALSE;
  }

  get_class_loader_id = (*env)->GetMethodID (env, context_cls,
      "getClassLoader", "()Ljava/lang/ClassLoader;");
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  class_loader = (*env)->CallObjectMethod (env, context, get_class_loader_id);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionDescribe (env);
    (*env)->ExceptionClear (env);
    return FALSE;
  }

  if (_context) {
    (*env)->DeleteGlobalRef (env, _context);
  }
  _context = (*env)->NewGlobalRef (env, context);

  if (_class_loader) {
    (*env)->DeleteGlobalRef (env, _class_loader);
  }
  _class_loader = (*env)->NewGlobalRef (env, class_loader);

  return TRUE;
}

static void
gst_android_init (JNIEnv * env, jobject context)
{
  gchar *cache_dir;
  gchar *files_dir;
  gchar *registry;
  GError *error = NULL;

  if (!init (env, context)) {
    __android_log_print (ANDROID_LOG_INFO, "GStreamer",
        "GStreamer failed to initialize");
  }

  if (gst_is_initialized ()) {
    __android_log_print (ANDROID_LOG_INFO, "GStreamer",
        "GStreamer already initialized");
    return;
  }

  if (!get_application_dirs (env, context, &cache_dir, &files_dir)) {
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer",
        "Failed to get application dirs");
  }

  if (cache_dir) {
    g_setenv ("TMP", cache_dir, TRUE);
    g_setenv ("TEMP", cache_dir, TRUE);
    g_setenv ("TMPDIR", cache_dir, TRUE);
    g_setenv ("XDG_RUNTIME_DIR", cache_dir, TRUE);
    g_setenv ("XDG_CACHE_HOME", cache_dir, TRUE);
    registry = g_build_filename (cache_dir, "registry.bin", NULL);
    g_setenv ("GST_REGISTRY", registry, TRUE);
    g_free (registry);
    g_setenv ("GST_REGISTRY_REUSE_PLUGIN_SCANNER", "no", TRUE);
    /* TODO: Should probably also set GST_PLUGIN_SCANNER and GST_PLUGIN_SYSTEM_PATH */
  }
  if (files_dir) {
    gchar *fontconfig, *certs;

    g_setenv ("HOME", files_dir, TRUE);
    g_setenv ("XDG_DATA_DIRS", files_dir, TRUE);
    g_setenv ("XDG_CONFIG_DIRS", files_dir, TRUE);
    g_setenv ("XDG_CONFIG_HOME", files_dir, TRUE);
    g_setenv ("XDG_DATA_HOME", files_dir, TRUE);

    fontconfig = g_build_filename (files_dir, "fontconfig", NULL);
    g_setenv ("FONTCONFIG_PATH", fontconfig, TRUE);
    g_free (fontconfig);

    certs =
        g_build_filename (files_dir, "ssl", "certs", "ca-certificates.crt",
        NULL);
    g_setenv ("CA_CERTIFICATES", certs, TRUE);
    g_free (certs);
  }
  g_free (cache_dir);
  g_free (files_dir);

  /* Set GLib print handlers */
  g_set_print_handler (glib_print_handler);
  g_set_printerr_handler (glib_printerr_handler);
  g_log_set_default_handler (glib_log_handler, NULL);

  /* Set GStreamer log handlers */
  gst_debug_remove_log_function (NULL);
  gst_debug_set_default_threshold (GST_LEVEL_WARNING);
  gst_debug_add_log_function ((GstLogFunction) gst_debug_logcat, NULL, NULL);

  /* get time we started for debugging messages */
  _priv_gst_info_start_time = gst_util_get_timestamp ();

  if (!gst_init_check (NULL, NULL, &error)) {
    gchar *message = g_strdup_printf ("GStreamer initialization failed: %s",
        error && error->message ? error->message : "(no message)");
    jclass exception_class = (*env)->FindClass (env, "java/lang/Exception");
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer", "%s", message);
    (*env)->ThrowNew (env, exception_class, message);
    g_free (message);
    return;
  }
  __android_log_print (ANDROID_LOG_INFO, "GStreamer",
      "GStreamer initialization complete");
}

static void
gst_android_init_jni (JNIEnv * env, jobject gstreamer, jobject context)
{
  gst_android_init (env, context);
}

static JNINativeMethod native_methods[] = {
  {"nativeInit", "(Landroid/content/Context;)V", (void *) gst_android_init_jni}
};

jint
JNI_OnLoad (JavaVM * vm, void *reserved)
{
  JNIEnv *env = NULL;

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer",
        "Could not retrieve JNIEnv");
    return 0;
  }
  jclass klass = (*env)->FindClass (env, "org/freedesktop/gstreamer/GStreamer");
  if (!klass) {
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer",
        "Could not retrieve class org.freedesktop.gstreamer.GStreamer. "
        "Please copy GStreamer.java file into your project: "
        "https://gitlab.freedesktop.org/gstreamer/gstreamer/-/tree/master/data/android/GStreamer.java");
    return 0;
  }
  if ((*env)->RegisterNatives (env, klass, native_methods,
          G_N_ELEMENTS (native_methods))) {
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer",
        "Could not register native methods for org.freedesktop.gstreamer.GStreamer");
    return 0;
  }

  /* Remember Java VM */
  _java_vm = vm;

  return JNI_VERSION_1_4;
}

void
JNI_OnUnload (JavaVM * vm, void *reversed)
{
  JNIEnv *env = NULL;

  if ((*vm)->GetEnv (vm, (void **) &env, JNI_VERSION_1_4) != JNI_OK) {
    __android_log_print (ANDROID_LOG_ERROR, "GStreamer",
        "Could not retrieve JNIEnv");
    return;
  }

  if (_context) {
    (*env)->DeleteGlobalRef (env, _context);
    _context = NULL;
  }

  if (_class_loader) {
    (*env)->DeleteGlobalRef (env, _class_loader);
    _class_loader = NULL;
  }

  _java_vm = NULL;
}
