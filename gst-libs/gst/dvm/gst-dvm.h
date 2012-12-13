/* Dalvik Virtual Machine helper functions
 *
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

#ifndef __GST_DVM_H__
#define __GST_DVM_H__

#include <gst/gst.h>
#include <jni.h>

#define GST_DVM_GET_CLASS(k, name) {                                    \
    jclass tmp;                                                         \
                                                                        \
    tmp = (*env)->FindClass (env, name);                                \
    if (!tmp) {                                                         \
      (*env)->ExceptionClear (env);                                     \
      GST_ERROR ("Failed to get class %s", name);                       \
      return FALSE;                                                     \
    }                                                                   \
                                                                        \
    k.klass = (*env)->NewGlobalRef (env, tmp);                          \
    if (!k.klass) {                                                     \
      (*env)->ExceptionClear (env);                                     \
      (*env)->DeleteLocalRef (env, tmp);                                \
      GST_ERROR ("Failed to get %s class global reference", name);      \
      return FALSE;                                                     \
    }                                                                   \
    (*env)->DeleteLocalRef (env, tmp);                                  \
  }
#define GST_DVM_GET_STATIC_METHOD(k, method, signature)                 \
  k.method = (*env)->GetStaticMethodID (env, k.klass, #method,          \
      signature);                                                       \
  if (!k.method) {                                                      \
    (*env)->ExceptionClear (env);                                       \
    GST_ERROR ("Failed to get static method %s for %s", #method, #k);   \
    return FALSE;                                                       \
  }

#define GST_DVM_GET_METHOD(k, method, signature)                        \
  k.method = (*env)->GetMethodID (env, k.klass, #method, signature);    \
  if (!k.method) {                                                      \
    (*env)->ExceptionClear (env);                                       \
    GST_ERROR ("Failed to get method %s for %s", #method, #k);          \
    return FALSE;                                                       \
  }

#define GST_DVM_GET_CONSTRUCTOR(k, field, signature)                    \
  k.field = (*env)->GetMethodID (env, k.klass, "<init>",  signature);   \
  if (!k.field) {                                                       \
    (*env)->ExceptionClear (env);                                       \
    GST_ERROR ("Failed to get constructor %s for %s", #field, #k);      \
    return FALSE;                                                       \
  }

#define GST_DVM_GET_STATIC_FIELD(k, field, signature)                   \
  k.field = (*env)->GetStaticFieldID (env, k.klass, #field, signature); \
  if (!k.field) {                                                       \
    (*env)->ExceptionClear (env);                                       \
    GST_ERROR ("Failed to get static field %s for %s", #field, #k);     \
    return FALSE;                                                       \
  }

#define GST_DVM_GET_FIELD(k, field, signature)                          \
  k.field = (*env)->GetFieldID (env, k.klass, #field, signature);       \
  if (!k.field) {                                                       \
    (*env)->ExceptionClear (env);                                       \
    GST_ERROR ("Failed to get field %s for %s", #field, #k);            \
    return FALSE;                                                       \
  }

#define GST_DVM_GET_CONSTANT(k, field, type, signature) {               \
    jfieldID id;                                                        \
                                                                        \
    id = (*env)->GetStaticFieldID (env, k.klass, #field, signature);    \
    if (!id) {                                                          \
      (*env)->ExceptionClear (env);                                     \
      GST_ERROR ("Failed to get static field %s for %s", #field, #k);   \
      return FALSE;                                                     \
    }                                                                   \
    k.field = (*env)->GetStatic##type##Field (env, k.klass, id);        \
    if ((*env)->ExceptionCheck (env)) {                                 \
      (*env)->ExceptionClear (env);                                     \
      GST_ERROR ("Failed to get " #type " constant %s", #field);        \
      return FALSE;                                                     \
    }                                                                   \
  }

#define GST_DVM_STATIC_CALL(error_statement, type, k, method, ...)      \
  (*env)->CallStatic##type##Method (env, k.klass, k.method, ## __VA_ARGS__); \
  if ((*env)->ExceptionCheck (env)) {                                   \
    GST_ERROR ("Failed to call Java method");                           \
    (*env)->ExceptionDescribe (env);                                    \
    (*env)->ExceptionClear (env);                                       \
    error_statement;                                                    \
  }

#define GST_DVM_CALL(error_statement, obj, type, k, method, ...)        \
  (*env)->Call##type##Method (env, obj, k.method, ## __VA_ARGS__);      \
  if ((*env)->ExceptionCheck (env)) {                                   \
    GST_ERROR ("Failed to call Java method");                           \
    (*env)->ExceptionDescribe (env);                                    \
    (*env)->ExceptionClear (env);                                       \
    error_statement;                                                    \
  }

#define GST_DVM_FIELD(error_statement, obj, type, k, field)             \
  (*env)->Get##type##Field (env, obj, k.field);                         \
  if ((*env)->ExceptionCheck (env)) {                                   \
    GST_ERROR ("Failed to get Java field");                             \
    (*env)->ExceptionDescribe (env);                                    \
    (*env)->ExceptionClear (env);                                       \
    error_statement;                                                    \
  }

#define GST_DVM_STATIC_FIELD(error_statement, type, k, field)           \
  (*env)->Get##type##Field (env, k.klass, k.field);                     \
  if ((*env)->ExceptionCheck (env)) {                                   \
    GST_ERROR ("Failed to get Java static field");                      \
    (*env)->ExceptionDescribe (env);                                    \
    (*env)->ExceptionClear (env);                                       \
    error_statement;                                                    \
  }

JNIEnv *gst_dvm_get_env (void);
gboolean gst_dvm_init (void);

#endif /* __GST_DVM_H__ */
