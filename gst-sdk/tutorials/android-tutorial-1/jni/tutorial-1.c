/*
 * Copyright (C) 2009 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */
#include <string.h>
#include <jni.h>
#include <gst/gst.h>

jstring
Java_com_gst_1sdk_1tutorials_tutorial_11_Tutorial1_gstVersion (JNIEnv* env,
                                                  jobject thiz )
{
  jstring ret;
  char *buffer, *tmp;
  GList *original_plugin_list = gst_registry_get_plugin_list (gst_registry_get_default());
  GList *plugin_list = original_plugin_list;

  buffer = g_strdup_printf ("Version: %s\n", gst_version_string());
  while (plugin_list) {
    GstPlugin *plugin = (GstPlugin *)plugin_list->data;
    GList *original_features_list, *features_list;
    plugin_list = plugin_list->next;

    tmp = g_strdup_printf ("%sPlugin: %s\n", buffer, gst_plugin_get_name (plugin));
    g_free (buffer);
    buffer = tmp;
    original_features_list = features_list = gst_registry_get_feature_list_by_plugin (gst_registry_get_default(), plugin->desc.name);

    while (features_list) {
      GstPluginFeature *feature = (GstPluginFeature *)features_list->data;
      features_list = features_list->next;

      tmp = g_strdup_printf ("%s    %s\n", buffer, gst_plugin_feature_get_name (feature));
      g_free (buffer);
      buffer = tmp;
    }
    gst_plugin_feature_list_free (original_features_list);
  }
  gst_plugin_list_free (original_plugin_list);
  ret = (*env)->NewStringUTF(env, buffer);
  g_free (buffer);
  return ret;
}

