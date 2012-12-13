/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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

#include "gst-dvm.h"

#include <pthread.h>

GST_DEBUG_CATEGORY (gst_dvm_debug);
#define GST_CAT_DEFAULT gst_dvm_debug

static GModule *java_module;
static jint (*get_created_java_vms) (JavaVM ** vmBuf, jsize bufLen,
    jsize * nVMs);
static jint (*create_java_vm) (JavaVM ** p_vm, JNIEnv ** p_env, void *vm_args);
static JavaVM *java_vm = NULL;
static gboolean started_java_vm = FALSE;

static pthread_key_t current_jni_env;

static JNIEnv *
gst_dvm_attach_current_thread (void)
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
gst_dvm_detach_current_thread (void *env)
{
  GST_DEBUG ("Detaching thread %p", g_thread_self ());
  (*java_vm)->DetachCurrentThread (java_vm);
}

JNIEnv *
gst_dvm_get_env (void)
{
  JNIEnv *env;

  if ((env = pthread_getspecific (current_jni_env)) == NULL) {
    env = gst_dvm_attach_current_thread ();
    pthread_setspecific (current_jni_env, env);
  }

  return env;
}

gboolean
gst_dvm_init (void)
{
  jsize n_vms;

  GST_DEBUG_CATEGORY_INIT (gst_dvm_debug, "dvm", 0, "DVM");

  pthread_key_create (&current_jni_env, gst_dvm_detach_current_thread);

  java_module = g_module_open ("libdvm", G_MODULE_BIND_LOCAL);
  if (!java_module)
    goto load_failed;

  if (!g_module_symbol (java_module, "JNI_CreateJavaVM",
          (gpointer *) & create_java_vm))
    goto symbol_error;

  if (!g_module_symbol (java_module, "JNI_GetCreatedJavaVMs",
          (gpointer *) & get_created_java_vms))
    goto symbol_error;

  n_vms = 0;
  if (get_created_java_vms (&java_vm, 1, &n_vms) < 0)
    goto get_created_failed;

  if (n_vms > 0) {
    GST_DEBUG ("Successfully got existing Java VM %p", java_vm);
  } else {
    JNIEnv *env;
    JavaVMInitArgs vm_args;
    JavaVMOption options[4];

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

load_failed:
  {
    GST_ERROR ("Failed to load libdvm: %s", g_module_error ());
    return FALSE;
  }
symbol_error:
  {
    GST_ERROR ("Failed to locate required JNI symbols in libdvm: %s",
        g_module_error ());
    g_module_close (java_module);
    java_module = NULL;
    return FALSE;
  }
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

gboolean
gst_dvm_is_own_vm (void)
{
  return started_java_vm;
}
