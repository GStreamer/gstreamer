/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2013, Fluendo S.A.
 *   Author: Andoni Morales <amorales@fluendo.com>
 * Copyright (C) 2014, Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2014, Collabora Ltd.
 *   Author: Matthieu Bouron <matthieu.bouron@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <pthread.h>
#include <gmodule.h>

#include "gstjniutils.h"

static GModule *java_module;
static jint (*get_created_java_vms) (JavaVM ** vmBuf, jsize bufLen,
    jsize * nVMs);
static jint (*create_java_vm) (JavaVM ** p_vm, JNIEnv ** p_env, void *vm_args);
static JavaVM *java_vm;
static gboolean initialized = FALSE;
static gboolean started_java_vm = FALSE;
static pthread_key_t current_jni_env;

/* getExceptionSummary() and getStackTrace() taken from Android's
 *   platform/libnativehelper/JNIHelp.cpp
 * Modified to work with normal C strings and without C++.
 *
 * Copyright (C) 2006 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Returns a human-readable summary of an exception object. The buffer will
 * be populated with the "binary" class name and, if present, the
 * exception message.
 */
static gchar *
getExceptionSummary (JNIEnv * env, jthrowable exception)
{
  GString *gs = g_string_new ("");
  jclass exceptionClass = NULL, classClass = NULL;
  jmethodID classGetNameMethod, getMessage;
  jstring classNameStr = NULL, messageStr = NULL;
  const char *classNameChars, *messageChars;

  /* get the name of the exception's class */
  exceptionClass = (*env)->GetObjectClass (env, exception);
  classClass = (*env)->GetObjectClass (env, exceptionClass);
  classGetNameMethod =
      (*env)->GetMethodID (env, classClass, "getName", "()Ljava/lang/String;");

  classNameStr =
      (jstring) (*env)->CallObjectMethod (env, exceptionClass,
      classGetNameMethod);

  if (classNameStr == NULL) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    g_string_append (gs, "<error getting class name>");
    goto done;
  }

  classNameChars = (*env)->GetStringUTFChars (env, classNameStr, NULL);
  if (classNameChars == NULL) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    g_string_append (gs, "<error getting class name UTF-8>");
    goto done;
  }

  g_string_append (gs, classNameChars);

  (*env)->ReleaseStringUTFChars (env, classNameStr, classNameChars);

  /* if the exception has a detail message, get that */
  getMessage =
      (*env)->GetMethodID (env, exceptionClass, "getMessage",
      "()Ljava/lang/String;");
  messageStr = (jstring) (*env)->CallObjectMethod (env, exception, getMessage);
  if (messageStr == NULL) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    goto done;
  }
  g_string_append (gs, ": ");

  messageChars = (*env)->GetStringUTFChars (env, messageStr, NULL);
  if (messageChars != NULL) {
    g_string_append (gs, messageChars);
    (*env)->ReleaseStringUTFChars (env, messageStr, messageChars);
  } else {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    g_string_append (gs, "<error getting message>");
  }

done:
  if (exceptionClass)
    (*env)->DeleteLocalRef (env, exceptionClass);
  if (classClass)
    (*env)->DeleteLocalRef (env, classClass);
  if (classNameStr)
    (*env)->DeleteLocalRef (env, classNameStr);
  if (messageStr)
    (*env)->DeleteLocalRef (env, messageStr);

  return g_string_free (gs, FALSE);
}

/*
 * Returns an exception (with stack trace) as a string.
 */
static gchar *
getStackTrace (JNIEnv * env, jthrowable exception)
{
  GString *gs = g_string_new ("");
  jclass stringWriterClass = NULL, printWriterClass = NULL;
  jclass exceptionClass = NULL;
  jmethodID stringWriterCtor, stringWriterToStringMethod;
  jmethodID printWriterCtor, printStackTraceMethod;
  jobject stringWriter = NULL, printWriter = NULL;
  jstring messageStr = NULL;
  const char *utfChars;

  stringWriterClass = (*env)->FindClass (env, "java/io/StringWriter");

  if (stringWriterClass == NULL) {
    g_string_append (gs, "<error getting java.io.StringWriter class>");
    goto done;
  }

  stringWriterCtor =
      (*env)->GetMethodID (env, stringWriterClass, "<init>", "()V");
  stringWriterToStringMethod =
      (*env)->GetMethodID (env, stringWriterClass, "toString",
      "()Ljava/lang/String;");

  printWriterClass = (*env)->FindClass (env, "java/io/PrintWriter");
  if (printWriterClass == NULL) {
    g_string_append (gs, "<error getting java.io.PrintWriter class>");
    goto done;
  }

  printWriterCtor =
      (*env)->GetMethodID (env, printWriterClass, "<init>",
      "(Ljava/io/Writer;)V");
  stringWriter = (*env)->NewObject (env, stringWriterClass, stringWriterCtor);
  if (stringWriter == NULL) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    g_string_append (gs, "<error creating new StringWriter instance>");
    goto done;
  }

  printWriter =
      (*env)->NewObject (env, printWriterClass, printWriterCtor, stringWriter);
  if (printWriter == NULL) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    g_string_append (gs, "<error creating new PrintWriter instance>");
    goto done;
  }

  exceptionClass = (*env)->GetObjectClass (env, exception);
  printStackTraceMethod =
      (*env)->GetMethodID (env, exceptionClass, "printStackTrace",
      "(Ljava/io/PrintWriter;)V");
  (*env)->CallVoidMethod (env, exception, printStackTraceMethod, printWriter);
  if ((*env)->ExceptionCheck (env)) {
    (*env)->ExceptionClear (env);
    g_string_append (gs, "<exception while printing stack trace>");
    goto done;
  }

  messageStr = (jstring) (*env)->CallObjectMethod (env, stringWriter,
      stringWriterToStringMethod);
  if (messageStr == NULL) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    g_string_append (gs, "<failed to call StringWriter.toString()>");
    goto done;
  }

  utfChars = (*env)->GetStringUTFChars (env, messageStr, NULL);
  if (utfChars == NULL) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    g_string_append (gs, "<failed to get UTF chars for message>");
    goto done;
  }

  g_string_append (gs, utfChars);

  (*env)->ReleaseStringUTFChars (env, messageStr, utfChars);

done:
  if (stringWriterClass)
    (*env)->DeleteLocalRef (env, stringWriterClass);
  if (printWriterClass)
    (*env)->DeleteLocalRef (env, printWriterClass);
  if (exceptionClass)
    (*env)->DeleteLocalRef (env, exceptionClass);
  if (stringWriter)
    (*env)->DeleteLocalRef (env, stringWriter);
  if (printWriter)
    (*env)->DeleteLocalRef (env, printWriter);
  if (messageStr)
    (*env)->DeleteLocalRef (env, messageStr);

  return g_string_free (gs, FALSE);
}

static JNIEnv *
gst_amc_jni_attach_current_thread (void)
{
  JNIEnv *env;
  JavaVMAttachArgs args;

  GST_DEBUG ("Attaching thread %p", g_thread_self ());
  args.version = JNI_VERSION_1_6;
  args.name = NULL;
  args.group = NULL;

  if ((*java_vm)->AttachCurrentThread (java_vm, &env, &args) < 0) {
    GST_ERROR ("Failed to attach current thread");
    return NULL;
  }

  return env;
}

static void
gst_amc_jni_detach_current_thread (void *env)
{
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

static gboolean
check_nativehelper (void)
{
  GModule *module;
  void **jni_invocation = NULL;
  gboolean ret = FALSE;

  module = g_module_open (NULL, G_MODULE_BIND_LOCAL);
  if (!module)
    return ret;

  /* Check if libnativehelper is loaded in the process and if
   * it has these awful wrappers for JNI_CreateJavaVM and
   * JNI_GetCreatedJavaVMs that crash the app if you don't
   * create a JniInvocation instance first. If it isn't we
   * just fail here and don't initialize anything.
   * See this code for reference:
   * https://android.googlesource.com/platform/libnativehelper/+/master/JniInvocation.cpp
   */
  if (!g_module_symbol (module, "_ZN13JniInvocation15jni_invocation_E",
          (gpointer *) & jni_invocation)) {
    ret = TRUE;
  } else {
    ret = (jni_invocation != NULL && *jni_invocation != NULL);
  }

  g_module_close (module);

  return ret;
}

static gboolean
load_java_module (const gchar * name)
{
  java_module = g_module_open (name, G_MODULE_BIND_LOCAL);
  if (!java_module)
    goto load_failed;

  if (!g_module_symbol (java_module, "JNI_CreateJavaVM",
          (gpointer *) & create_java_vm))
    goto symbol_error;

  if (!g_module_symbol (java_module, "JNI_GetCreatedJavaVMs",
          (gpointer *) & get_created_java_vms))
    goto symbol_error;

  return TRUE;

load_failed:
  {
    GST_ERROR ("Failed to load Java module '%s': %s", GST_STR_NULL (name),
        g_module_error ());
    return FALSE;
  }
symbol_error:
  {
    GST_ERROR ("Failed to locate required JNI symbols in '%s': %s",
        GST_STR_NULL (name), g_module_error ());
    g_module_close (java_module);
    java_module = NULL;
    return FALSE;
  }
}

static gboolean
gst_amc_jni_initialize_java_vm (void)
{
  jsize n_vms;

  /* Returns TRUE if we can safely
   * a) get the current VMs and
   * b) start a VM if none is started yet
   *
   * FIXME: On Android >= 4.4 we won't be able to safely start a
   * VM on our own without using private C++ API!
   */
  if (!check_nativehelper ()) {
    GST_ERROR ("Can't safely check for VMs or start a VM");
    return FALSE;
  }

  if (!load_java_module (NULL)) {
    if (!load_java_module ("libdvm"))
      return FALSE;
  }

  n_vms = 0;
  if (get_created_java_vms (&java_vm, 1, &n_vms) < 0)
    goto get_created_failed;

  if (n_vms > 0) {
    GST_DEBUG ("Successfully got existing Java VM %p", java_vm);
  } else {
    JNIEnv *env;
    JavaVMInitArgs vm_args;
    JavaVMOption options[4];

    GST_DEBUG ("Found no existing Java VM, trying to start one");

    options[0].optionString = "-verbose:jni";
    options[1].optionString = "-verbose:gc";
    options[2].optionString = "-Xcheck:jni";
    options[3].optionString = "-Xdebug";

    vm_args.version = JNI_VERSION_1_4;
    vm_args.options = options;
    vm_args.nOptions = 4;
    vm_args.ignoreUnrecognized = JNI_TRUE;
    if (create_java_vm (&java_vm, &env, &vm_args) < 0)
      goto create_failed;
    GST_DEBUG ("Successfully created Java VM %p", java_vm);

    started_java_vm = TRUE;
  }

  return java_vm != NULL;

get_created_failed:
  {
    GST_ERROR ("Failed to get already created VMs");
    g_module_close (java_module);
    java_module = NULL;
    return FALSE;
  }
create_failed:
  {
    GST_ERROR ("Failed to create a Java VM");
    g_module_close (java_module);
    java_module = NULL;
    return FALSE;
  }
}

static void
gst_amc_jni_set_error_string (JNIEnv * env, GQuark domain, gint code,
    GError ** err, const gchar * message)
{
  jthrowable exception;

  if (!err) {
    if ((*env)->ExceptionCheck (env))
      (*env)->ExceptionClear (env);
    return;
  }

  if ((*env)->ExceptionCheck (env)) {
    if ((exception = (*env)->ExceptionOccurred (env))) {
      gchar *exception_description, *exception_stacktrace;

      /* Clear exception so that we can call Java methods again */
      (*env)->ExceptionClear (env);

      exception_description = getExceptionSummary (env, exception);
      exception_stacktrace = getStackTrace (env, exception);
      g_set_error (err, domain, code, "%s: %s\n%s", message,
          exception_description, exception_stacktrace);
      g_free (exception_description);
      g_free (exception_stacktrace);

      (*env)->DeleteLocalRef (env, exception);
    } else {
      (*env)->ExceptionClear (env);
      g_set_error (err, domain, code, "%s", message);
    }
  } else {
    g_set_error (err, domain, code, "%s", message);
  }
}

G_GNUC_PRINTF (5, 6)
     void gst_amc_jni_set_error (JNIEnv * env, GQuark domain, gint code,
    GError ** err, const gchar * format, ...)
{
  gchar *message;
  va_list var_args;

  va_start (var_args, format);
  message = g_strdup_vprintf (format, var_args);
  va_end (var_args);

  gst_amc_jni_set_error_string (env, domain, code, err, message);

  g_free (message);
}


gboolean
gst_amc_jni_initialize (void)
{
  if (!initialized) {
    pthread_key_create (&current_jni_env, gst_amc_jni_detach_current_thread);
    initialized = gst_amc_jni_initialize_java_vm ();
  }
  return initialized;
}

JNIEnv *
gst_amc_jni_get_env (void)
{
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = gst_amc_jni_attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

gboolean
gst_amc_jni_is_vm_started (void)
{
  return started_java_vm;
}
