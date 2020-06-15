/* GStreamer
 * Copyright (C) 2019 Seungha Yang <seungha.yang@navercorp.com>
 * Copyright (C) 2020 Seungha Yang <seungha@centricular.com>
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

#include "gstmfconfig.h"

#include "gstmfsourceobject.h"

GST_DEBUG_CATEGORY_EXTERN (gst_mf_source_object_debug);
#define GST_CAT_DEFAULT gst_mf_source_object_debug

enum
{
  PROP_0,
  PROP_DEVICE_PATH,
  PROP_DEVICE_NAME,
  PROP_DEVICE_INDEX,
  PROP_SOURCE_TYPE,
};

#define DEFAULT_DEVICE_PATH         NULL
#define DEFAULT_DEVICE_NAME         NULL
#define DEFAULT_DEVICE_INDEX        -1
#define DEFAULT_SOURCE_TYPE        GST_MF_SOURCE_TYPE_VIDEO

GType
gst_mf_source_type_get_type (void)
{
  static GType source_type = 0;

  static const GEnumValue source_types[] = {
    {GST_MF_SOURCE_TYPE_VIDEO, "Video", "video"},
    {0, NULL, NULL}
  };

  if (!source_type) {
    source_type = g_enum_register_static ("GstMFSourceMode", source_types);
  }

  return source_type;
}

struct _GstMFSourceObjectPrivate
{
  GstMFSourceType source_type;

  gchar *device_path;
  gchar *device_name;
  gint device_index;

  GThread *thread;
  GMutex lock;
  GCond cond;
  GMainContext *context;
  GMainLoop *loop;
};

static void gst_mf_source_object_constructed (GObject * object);
static void gst_mf_source_object_dispose (GObject * object);
static void gst_mf_source_object_finalize (GObject * object);
static void gst_mf_source_object_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_mf_source_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);

#if !(GST_MF_WINAPI_ONLY_APP)
static gpointer gst_mf_source_object_thread_func (GstMFSourceObject * self);
static gboolean gst_mf_source_enum_device_activate (GstMFSourceObject * self,
    GstMFSourceType source_type, GList ** device_activates);
static void gst_mf_device_activate_free (GstMFDeviceActivate * activate);
#endif

#define gst_mf_source_object_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (GstMFSourceObject, gst_mf_source_object,
    GST_TYPE_OBJECT);

static void
gst_mf_source_object_class_init (GstMFSourceObjectClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_mf_source_object_constructed;
  gobject_class->dispose = gst_mf_source_object_dispose;
  gobject_class->finalize = gst_mf_source_object_finalize;
  gobject_class->get_property = gst_mf_source_object_get_property;
  gobject_class->set_property = gst_mf_source_object_set_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE_PATH,
      g_param_spec_string ("device-path", "Device Path",
          "The device path", DEFAULT_DEVICE_PATH,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device Name",
          "The human-readable device name", DEFAULT_DEVICE_NAME,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "Device Index",
          "The zero-based device index", -1, G_MAXINT, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_SOURCE_TYPE,
      g_param_spec_enum ("source-type", "Source Type",
          "Source Type", GST_TYPE_MF_SOURCE_TYPE,
          DEFAULT_SOURCE_TYPE,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
}

static void
gst_mf_source_object_init (GstMFSourceObject * self)
{
  GstMFSourceObjectPrivate *priv;

  self->priv = priv = gst_mf_source_object_get_instance_private (self);

  self->device_index = DEFAULT_DEVICE_INDEX;
  priv->source_type = DEFAULT_SOURCE_TYPE;

  g_mutex_init (&priv->lock);
  g_cond_init (&priv->cond);
}

static void
gst_mf_source_object_constructed (GObject * object)
{
#if !(GST_MF_WINAPI_ONLY_APP)
  GstMFSourceObject *self = GST_MF_SOURCE_OBJECT (object);
  GstMFSourceObjectPrivate *priv = self->priv;

  priv->context = g_main_context_new ();
  priv->loop = g_main_loop_new (priv->context, FALSE);

  /* Create a new thread to ensure that COM thread can be MTA thread */
  g_mutex_lock (&priv->lock);
  priv->thread = g_thread_new ("GstMFSourceObject",
      (GThreadFunc) gst_mf_source_object_thread_func, self);
  while (!g_main_loop_is_running (priv->loop))
    g_cond_wait (&priv->cond, &priv->lock);
  g_mutex_unlock (&priv->lock);

#endif
  G_OBJECT_CLASS (parent_class)->constructed (object);
}

static void
gst_mf_source_object_dispose (GObject * object)
{
  GstMFSourceObject *self = GST_MF_SOURCE_OBJECT (object);
  GstMFSourceObjectPrivate *priv = self->priv;

  if (priv->loop) {
    g_main_loop_quit (priv->loop);
    g_thread_join (priv->thread);
    g_main_loop_unref (priv->loop);
    g_main_context_unref (priv->context);
    priv->loop = NULL;
  }

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_mf_source_object_finalize (GObject * object)
{
  GstMFSourceObject *self = GST_MF_SOURCE_OBJECT (object);
  GstMFSourceObjectPrivate *priv = self->priv;

  g_mutex_clear (&priv->lock);
  g_cond_clear (&priv->cond);

  g_free (self->device_path);
  g_free (self->device_name);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mf_source_object_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMFSourceObject *self = GST_MF_SOURCE_OBJECT (object);
  GstMFSourceObjectPrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_DEVICE_PATH:
      g_value_set_string (value, self->device_path);
      break;
    case PROP_DEVICE_NAME:
      g_value_set_string (value, self->device_name);
      break;
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, self->device_index);
      break;
    case PROP_SOURCE_TYPE:
      g_value_set_enum (value, priv->source_type);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_source_object_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMFSourceObject *self = GST_MF_SOURCE_OBJECT (object);
  GstMFSourceObjectPrivate *priv = self->priv;

  switch (prop_id) {
    case PROP_DEVICE_PATH:
      g_free (self->device_path);
      self->device_path = g_value_dup_string (value);
      break;
    case PROP_DEVICE_NAME:
      g_free (self->device_name);
      self->device_name = g_value_dup_string (value);
      break;
    case PROP_DEVICE_INDEX:
      self->device_index = g_value_get_int (value);
      break;
    case PROP_SOURCE_TYPE:
      priv->source_type = g_value_get_enum (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_mf_source_object_main_loop_running_cb (GstMFSourceObject * self)
{
  GstMFSourceObjectPrivate *priv = self->priv;

  GST_TRACE_OBJECT (self, "Main loop running now");

  g_mutex_lock (&priv->lock);
  g_cond_signal (&priv->cond);
  g_mutex_unlock (&priv->lock);

  return G_SOURCE_REMOVE;
}

gboolean
gst_mf_source_object_start (GstMFSourceObject * object)
{
  GstMFSourceObjectClass *klass;

  g_return_val_if_fail (GST_IS_MF_SOURCE_OBJECT (object), FALSE);

  klass = GST_MF_SOURCE_OBJECT_GET_CLASS (object);
  g_assert (klass->start != NULL);

  return klass->start (object);
}

gboolean
gst_mf_source_object_stop (GstMFSourceObject * object)
{
  GstMFSourceObjectClass *klass;

  g_return_val_if_fail (GST_IS_MF_SOURCE_OBJECT (object), FALSE);

  klass = GST_MF_SOURCE_OBJECT_GET_CLASS (object);
  g_assert (klass->stop != NULL);

  return klass->stop (object);
}

GstFlowReturn
gst_mf_source_object_fill (GstMFSourceObject * object, GstBuffer * buffer)
{
  GstMFSourceObjectClass *klass;

  g_return_val_if_fail (GST_IS_MF_SOURCE_OBJECT (object), GST_FLOW_ERROR);
  g_return_val_if_fail (GST_IS_BUFFER (buffer), GST_FLOW_ERROR);

  klass = GST_MF_SOURCE_OBJECT_GET_CLASS (object);
  g_assert (klass->fill != NULL);

  return klass->fill (object, buffer);
}

GstFlowReturn
gst_mf_source_object_create (GstMFSourceObject * object, GstBuffer ** buffer)
{
  GstMFSourceObjectClass *klass;

  g_return_val_if_fail (GST_IS_MF_SOURCE_OBJECT (object), GST_FLOW_ERROR);
  g_return_val_if_fail (buffer != NULL, GST_FLOW_ERROR);

  klass = GST_MF_SOURCE_OBJECT_GET_CLASS (object);
  g_assert (klass->create != NULL);

  return klass->create (object, buffer);
}

void
gst_mf_source_object_set_flushing (GstMFSourceObject * object,
    gboolean flushing)
{
  GstMFSourceObjectClass *klass;

  g_return_if_fail (GST_IS_MF_SOURCE_OBJECT (object));

  klass = GST_MF_SOURCE_OBJECT_GET_CLASS (object);

  if (flushing) {
    if (klass->unlock)
      klass->unlock (object);
  } else {
    if (klass->unlock_stop)
      klass->unlock_stop (object);
  }
}

gboolean
gst_mf_source_object_set_caps (GstMFSourceObject * object, GstCaps * caps)
{
  GstMFSourceObjectClass *klass;

  g_return_val_if_fail (GST_IS_MF_SOURCE_OBJECT (object), FALSE);

  klass = GST_MF_SOURCE_OBJECT_GET_CLASS (object);
  g_assert (klass->set_caps != NULL);

  return klass->set_caps (object, caps);
}

GstCaps *
gst_mf_source_object_get_caps (GstMFSourceObject * object)
{
  GstMFSourceObjectClass *klass;

  g_return_val_if_fail (GST_IS_MF_SOURCE_OBJECT (object), NULL);

  klass = GST_MF_SOURCE_OBJECT_GET_CLASS (object);
  g_assert (klass->get_caps != NULL);

  return klass->get_caps (object);
}

#if !(GST_MF_WINAPI_ONLY_APP)
static gpointer
gst_mf_source_object_thread_func (GstMFSourceObject * self)
{
  GstMFSourceObjectPrivate *priv = self->priv;
  GstMFSourceObjectClass *klass = GST_MF_SOURCE_OBJECT_GET_CLASS (self);
  GSource *source;
  GList *activate_list = NULL;
  GstMFDeviceActivate *target = NULL;
  GList *iter;

  g_assert (klass->open != NULL);
  g_assert (klass->close != NULL);

  CoInitializeEx (NULL, COINIT_MULTITHREADED);

  g_main_context_push_thread_default (priv->context);

  source = g_idle_source_new ();
  g_source_set_callback (source,
      (GSourceFunc) gst_mf_source_object_main_loop_running_cb, self, NULL);
  g_source_attach (source, priv->context);
  g_source_unref (source);

  if (!gst_mf_source_enum_device_activate (self,
          priv->source_type, &activate_list)) {
    GST_WARNING_OBJECT (self, "No available video capture device");
    goto run_loop;
  }
#ifndef GST_DISABLE_GST_DEBUG
  for (iter = activate_list; iter; iter = g_list_next (iter)) {
    GstMFDeviceActivate *activate = (GstMFDeviceActivate *) iter->data;

    GST_DEBUG_OBJECT (self, "device %d, name: \"%s\", path: \"%s\"",
        activate->index, GST_STR_NULL (activate->name),
        GST_STR_NULL (activate->path));
  }
#endif

  GST_DEBUG_OBJECT (self,
      "Requested device index: %d, name: \"%s\", path \"%s\"",
      self->device_index, GST_STR_NULL (self->device_name),
      GST_STR_NULL (self->device_path));

  for (iter = activate_list; iter; iter = g_list_next (iter)) {
    GstMFDeviceActivate *activate = (GstMFDeviceActivate *) iter->data;
    gboolean match;

    if (self->device_path) {
      match = g_ascii_strcasecmp (activate->path, self->device_path) == 0;
    } else if (self->device_name) {
      match = g_ascii_strcasecmp (activate->name, self->device_name) == 0;
    } else if (self->device_index >= 0) {
      match = activate->index == self->device_index;
    } else {
      /* pick the first entry */
      match = TRUE;
    }

    if (match) {
      target = activate;
      break;
    }
  }

  if (target) {
    self->opened = klass->open (self, target->handle);

    g_free (self->device_path);
    self->device_path = g_strdup (target->path);

    g_free (self->device_name);
    self->device_name = g_strdup (target->name);

    self->device_index = target->index;
  }

  if (activate_list)
    g_list_free_full (activate_list,
        (GDestroyNotify) gst_mf_device_activate_free);

run_loop:
  GST_TRACE_OBJECT (self, "Starting main loop");
  g_main_loop_run (priv->loop);
  GST_TRACE_OBJECT (self, "Stopped main loop");

  klass->stop (self);
  klass->close (self);

  g_main_context_pop_thread_default (priv->context);

  CoUninitialize ();

  return NULL;
}

static gboolean
gst_mf_source_enum_device_activate (GstMFSourceObject * self,
    GstMFSourceType source_type, GList ** device_sources)
{
  HRESULT hr;
  GList *ret = NULL;
  IMFAttributes *attr = NULL;
  IMFActivate **devices = NULL;
  UINT32 i, count = 0;

  hr = MFCreateAttributes (&attr, 1);
  if (!gst_mf_result (hr)) {
    return FALSE;
  }

  switch (source_type) {
    case GST_MF_SOURCE_TYPE_VIDEO:
      hr = IMFAttributes_SetGUID (attr, &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
          &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
      break;
    default:
      GST_ERROR_OBJECT (self, "Unknown source type %d", source_type);
      return FALSE;
  }

  if (!gst_mf_result (hr))
    return FALSE;

  hr = MFEnumDeviceSources (attr, &devices, &count);
  if (!gst_mf_result (hr)) {
    IMFAttributes_Release (attr);
    return FALSE;
  }

  for (i = 0; i < count; i++) {
    GstMFDeviceActivate *entry;
    LPWSTR name;
    UINT32 name_len;
    IMFActivate *activate = devices[i];

    switch (source_type) {
      case GST_MF_SOURCE_TYPE_VIDEO:
        hr = IMFActivate_GetAllocatedString (activate,
            &MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
            &name, &name_len);
        break;
      default:
        g_assert_not_reached ();
        goto done;
    }

    entry = g_new0 (GstMFDeviceActivate, 1);
    entry->index = i;
    entry->handle = activate;

    if (gst_mf_result (hr)) {
      entry->path = g_utf16_to_utf8 ((const gunichar2 *) name,
          -1, NULL, NULL, NULL);
      CoTaskMemFree (name);
    }

    hr = IMFActivate_GetAllocatedString (activate,
        &MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &name_len);
    if (gst_mf_result (hr)) {
      entry->name = g_utf16_to_utf8 ((const gunichar2 *) name,
          -1, NULL, NULL, NULL);
      CoTaskMemFree (name);
    }

    ret = g_list_prepend (ret, entry);
  }

done:
  ret = g_list_reverse (ret);
  CoTaskMemFree (devices);

  *device_sources = ret;

  return ! !ret;
}

static void
gst_mf_device_activate_free (GstMFDeviceActivate * activate)
{
  g_return_if_fail (activate != NULL);

  if (activate->handle)
    IMFActivate_Release (activate->handle);

  g_free (activate->name);
  g_free (activate->path);
  g_free (activate);
}
#endif
