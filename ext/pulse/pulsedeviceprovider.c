/* GStreamer
 * Copyright (C) 2012 Olivier Crete <olivier.crete@collabora.com>
 *
 * pulsedeviceprovider.c: pulseaudio device probing and monitoring
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

#include "pulsedeviceprovider.h"

#include <string.h>

#include <gst/gst.h>

#include "pulsesrc.h"
#include "pulsesink.h"
#include "pulseutil.h"


GST_DEBUG_CATEGORY_EXTERN (pulse_debug);
#define GST_CAT_DEFAULT pulse_debug


static GstDevice *gst_pulse_device_new (guint id,
    const gchar * device_name, GstCaps * caps, const gchar * internal_name,
    GstPulseDeviceType type, GstStructure * properties);

G_DEFINE_TYPE (GstPulseDeviceProvider, gst_pulse_device_provider,
    GST_TYPE_DEVICE_PROVIDER);

static void gst_pulse_device_provider_finalize (GObject * object);
static void gst_pulse_device_provider_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_pulse_device_provider_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);


static GList *gst_pulse_device_provider_probe (GstDeviceProvider * provider);
static gboolean gst_pulse_device_provider_start (GstDeviceProvider * provider);
static void gst_pulse_device_provider_stop (GstDeviceProvider * provider);

enum
{
  PROP_0,
  PROP_SERVER,
  PROP_CLIENT_NAME,
  PROP_LAST
};


static void
gst_pulse_device_provider_class_init (GstPulseDeviceProviderClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstDeviceProviderClass *dm_class = GST_DEVICE_PROVIDER_CLASS (klass);
  gchar *client_name;

  gobject_class->set_property = gst_pulse_device_provider_set_property;
  gobject_class->get_property = gst_pulse_device_provider_get_property;
  gobject_class->finalize = gst_pulse_device_provider_finalize;

  dm_class->probe = gst_pulse_device_provider_probe;
  dm_class->start = gst_pulse_device_provider_start;
  dm_class->stop = gst_pulse_device_provider_stop;

  g_object_class_install_property (gobject_class,
      PROP_SERVER,
      g_param_spec_string ("server", "Server",
          "The PulseAudio server to connect to", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  client_name = gst_pulse_client_name ();
  g_object_class_install_property (gobject_class,
      PROP_CLIENT_NAME,
      g_param_spec_string ("client-name", "Client Name",
          "The PulseAudio client_name_to_use", client_name,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));
  g_free (client_name);

  gst_device_provider_class_set_static_metadata (dm_class,
      "PulseAudio Device Provider", "Sink/Source/Audio",
      "List and provider PulseAudio source and sink devices",
      "Olivier Crete <olivier.crete@collabora.com>");
}

static void
gst_pulse_device_provider_init (GstPulseDeviceProvider * self)
{
  self->client_name = gst_pulse_client_name ();
}

static void
gst_pulse_device_provider_finalize (GObject * object)
{
  GstPulseDeviceProvider *self = GST_PULSE_DEVICE_PROVIDER (object);

  g_free (self->client_name);
  g_free (self->server);

  G_OBJECT_CLASS (gst_pulse_device_provider_parent_class)->finalize (object);
}


static void
gst_pulse_device_provider_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstPulseDeviceProvider *self = GST_PULSE_DEVICE_PROVIDER (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_free (self->server);
      self->server = g_value_dup_string (value);
      break;
    case PROP_CLIENT_NAME:
      g_free (self->client_name);
      if (!g_value_get_string (value)) {
        GST_WARNING_OBJECT (self,
            "Empty PulseAudio client name not allowed. "
            "Resetting to default value");
        self->client_name = gst_pulse_client_name ();
      } else
        self->client_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_pulse_device_provider_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstPulseDeviceProvider *self = GST_PULSE_DEVICE_PROVIDER (object);

  switch (prop_id) {
    case PROP_SERVER:
      g_value_set_string (value, self->server);
      break;
    case PROP_CLIENT_NAME:
      g_value_set_string (value, self->client_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
context_state_cb (pa_context * c, void *userdata)
{
  GstPulseDeviceProvider *self = userdata;

  switch (pa_context_get_state (c)) {
    case PA_CONTEXT_READY:
    case PA_CONTEXT_TERMINATED:
    case PA_CONTEXT_FAILED:
      pa_threaded_mainloop_signal (self->mainloop, 0);
      break;

    case PA_CONTEXT_UNCONNECTED:
    case PA_CONTEXT_CONNECTING:
    case PA_CONTEXT_AUTHORIZING:
    case PA_CONTEXT_SETTING_NAME:
      break;
  }
}

static GstDevice *
new_source (const pa_source_info * info)
{
  GstCaps *caps;
  GstStructure *props;
  guint i;

  caps = gst_caps_new_empty ();

  for (i = 0; i < info->n_formats; i++)
    gst_caps_append (caps, gst_pulse_format_info_to_caps (info->formats[i]));

  props = gst_pulse_make_structure (info->proplist);

  return gst_pulse_device_new (info->index, info->description,
      caps, info->name, GST_PULSE_DEVICE_TYPE_SOURCE, props);
}

static GstDevice *
new_sink (const pa_sink_info * info)
{
  GstCaps *caps;
  GstStructure *props;
  guint i;

  caps = gst_caps_new_empty ();

  for (i = 0; i < info->n_formats; i++)
    gst_caps_append (caps, gst_pulse_format_info_to_caps (info->formats[i]));

  props = gst_pulse_make_structure (info->proplist);

  return gst_pulse_device_new (info->index, info->description,
      caps, info->name, GST_PULSE_DEVICE_TYPE_SINK, props);
}

static void
get_source_info_cb (pa_context * context,
    const pa_source_info * info, int eol, void *userdata)
{
  GstPulseDeviceProvider *self = userdata;
  GstDevice *dev;

  if (eol) {
    pa_threaded_mainloop_signal (self->mainloop, 0);
    return;
  }

  dev = new_source (info);

  if (dev)
    gst_device_provider_device_add (GST_DEVICE_PROVIDER (self), dev);
}

static void
get_sink_info_cb (pa_context * context,
    const pa_sink_info * info, int eol, void *userdata)
{
  GstPulseDeviceProvider *self = userdata;
  GstDevice *dev;

  if (eol) {
    pa_threaded_mainloop_signal (self->mainloop, 0);
    return;
  }

  dev = new_sink (info);

  if (dev)
    gst_device_provider_device_add (GST_DEVICE_PROVIDER (self), dev);
}

static void
context_subscribe_cb (pa_context * context, pa_subscription_event_type_t type,
    uint32_t idx, void *userdata)
{
  GstPulseDeviceProvider *self = userdata;
  GstDeviceProvider *provider = userdata;
  pa_subscription_event_type_t facility =
      type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
  pa_subscription_event_type_t event_type =
      type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;

  if (facility != PA_SUBSCRIPTION_EVENT_SOURCE &&
      facility != PA_SUBSCRIPTION_EVENT_SINK)
    return;

  if (event_type == PA_SUBSCRIPTION_EVENT_NEW) {
    /* Microphone in the source output has changed */

    if (facility == PA_SUBSCRIPTION_EVENT_SOURCE)
      pa_context_get_source_info_by_index (context, idx, get_source_info_cb,
          self);
    else if (facility == PA_SUBSCRIPTION_EVENT_SINK)
      pa_context_get_sink_info_by_index (context, idx, get_sink_info_cb, self);
  } else if (event_type == PA_SUBSCRIPTION_EVENT_REMOVE) {
    GstPulseDevice *dev = NULL;
    GList *item;

    GST_OBJECT_LOCK (self);
    for (item = provider->devices; item; item = item->next) {
      dev = item->data;

      if (((facility == PA_SUBSCRIPTION_EVENT_SOURCE &&
                  dev->type == GST_PULSE_DEVICE_TYPE_SOURCE) ||
              (facility == PA_SUBSCRIPTION_EVENT_SINK &&
                  dev->type == GST_PULSE_DEVICE_TYPE_SINK)) &&
          dev->device_index == idx) {
        gst_object_ref (dev);
        break;
      }
      dev = NULL;
    }
    GST_OBJECT_UNLOCK (self);

    if (dev) {
      gst_device_provider_device_remove (GST_DEVICE_PROVIDER (self),
          GST_DEVICE (dev));
      gst_object_unref (dev);
    }
  }
}

static void
get_source_info_list_cb (pa_context * context, const pa_source_info * info,
    int eol, void *userdata)
{
  GList **devices = userdata;

  if (eol)
    return;

  *devices = g_list_prepend (*devices, gst_object_ref_sink (new_source (info)));
}

static void
get_sink_info_list_cb (pa_context * context, const pa_sink_info * info,
    int eol, void *userdata)
{
  GList **devices = userdata;

  if (eol)
    return;

  *devices = g_list_prepend (*devices, gst_object_ref_sink (new_sink (info)));
}

static GList *
gst_pulse_device_provider_probe (GstDeviceProvider * provider)
{
  GstPulseDeviceProvider *self = GST_PULSE_DEVICE_PROVIDER (provider);
  GList *devices = NULL;
  pa_mainloop *m = NULL;
  pa_context *c = NULL;
  pa_operation *o;

  if (!(m = pa_mainloop_new ()))
    return NULL;

  if (!(c = pa_context_new (pa_mainloop_get_api (m), self->client_name))) {
    GST_ERROR_OBJECT (self, "Failed to create context");
    goto failed;
  }

  if (pa_context_connect (c, self->server, 0, NULL) < 0) {
    GST_ERROR_OBJECT (self, "Failed to connect: %s",
        pa_strerror (pa_context_errno (self->context)));
    goto failed;
  }

  for (;;) {
    pa_context_state_t state;

    state = pa_context_get_state (c);

    if (!PA_CONTEXT_IS_GOOD (state)) {
      GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("Failed to connect: %s",
              pa_strerror (pa_context_errno (c))), (NULL));
      goto failed;
    }

    if (state == PA_CONTEXT_READY)
      break;

    /* Wait until the context is ready */
    if (pa_mainloop_iterate (m, TRUE, NULL) < 0)
      goto failed;

  }
  GST_DEBUG_OBJECT (self, "connected");

  o = pa_context_get_sink_info_list (c, get_sink_info_list_cb, &devices);
  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING &&
      pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    if (pa_mainloop_iterate (m, TRUE, NULL) < 0)
      break;
  }
  pa_operation_unref (o);

  o = pa_context_get_source_info_list (c, get_source_info_list_cb, &devices);
  while (pa_operation_get_state (o) == PA_OPERATION_RUNNING &&
      pa_operation_get_state (o) == PA_OPERATION_RUNNING) {
    if (pa_mainloop_iterate (m, TRUE, NULL) < 0)
      break;
  }
  pa_operation_unref (o);

  pa_context_disconnect (c);
  pa_mainloop_free (m);

  return devices;

failed:

  return NULL;
}

static gboolean
gst_pulse_device_provider_start (GstDeviceProvider * provider)
{
  GstPulseDeviceProvider *self = GST_PULSE_DEVICE_PROVIDER (provider);
  pa_operation *initial_operation;

  if (!(self->mainloop = pa_threaded_mainloop_new ())) {
    GST_ERROR_OBJECT (self, "Could not create pulseaudio mainloop");
    goto mainloop_failed;
  }
  if (pa_threaded_mainloop_start (self->mainloop) < 0) {
    GST_ERROR_OBJECT (self, "Could not start pulseaudio mainloop");
    pa_threaded_mainloop_free (self->mainloop);
    self->mainloop = NULL;
    goto mainloop_failed;
  }

  pa_threaded_mainloop_lock (self->mainloop);

  if (!(self->context =
          pa_context_new (pa_threaded_mainloop_get_api (self->mainloop),
              self->client_name))) {
    GST_ERROR_OBJECT (self, "Failed to create context");
    goto unlock_and_fail;
  }

  pa_context_set_state_callback (self->context, context_state_cb, self);
  pa_context_set_subscribe_callback (self->context, context_subscribe_cb, self);


  GST_DEBUG_OBJECT (self, "connect to server %s", GST_STR_NULL (self->server));

  if (pa_context_connect (self->context, self->server, 0, NULL) < 0) {
    GST_ERROR_OBJECT (self, "Failed to connect: %s",
        pa_strerror (pa_context_errno (self->context)));
    goto unlock_and_fail;
  }

  for (;;) {
    pa_context_state_t state;

    state = pa_context_get_state (self->context);

    if (!PA_CONTEXT_IS_GOOD (state)) {
      GST_ERROR_OBJECT (self, "Failed to connect: %s",
          pa_strerror (pa_context_errno (self->context)));
      goto unlock_and_fail;
    }

    if (state == PA_CONTEXT_READY)
      break;

    /* Wait until the context is ready */
    pa_threaded_mainloop_wait (self->mainloop);
  }
  GST_DEBUG_OBJECT (self, "connected");

  pa_context_subscribe (self->context,
      PA_SUBSCRIPTION_MASK_SOURCE | PA_SUBSCRIPTION_MASK_SINK, NULL, NULL);

  initial_operation = pa_context_get_source_info_list (self->context,
      get_source_info_cb, self);
  while (pa_operation_get_state (initial_operation) == PA_OPERATION_RUNNING) {
    if (!PA_CONTEXT_IS_GOOD (pa_context_get_state ((self->context))))
      goto cancel_and_fail;

    pa_threaded_mainloop_wait (self->mainloop);
  }
  pa_operation_unref (initial_operation);

  initial_operation = pa_context_get_sink_info_list (self->context,
      get_sink_info_cb, self);
  if (!initial_operation)
    goto unlock_and_fail;
  while (pa_operation_get_state (initial_operation) == PA_OPERATION_RUNNING) {
    if (!PA_CONTEXT_IS_GOOD (pa_context_get_state ((self->context))))
      goto cancel_and_fail;

    pa_threaded_mainloop_wait (self->mainloop);
  }
  pa_operation_unref (initial_operation);

  pa_threaded_mainloop_unlock (self->mainloop);

  return TRUE;

unlock_and_fail:
  pa_threaded_mainloop_unlock (self->mainloop);
  gst_pulse_device_provider_stop (provider);
  return FALSE;

mainloop_failed:
  return FALSE;

cancel_and_fail:
  pa_operation_cancel (initial_operation);
  pa_operation_unref (initial_operation);
  goto unlock_and_fail;
}

static void
gst_pulse_device_provider_stop (GstDeviceProvider * provider)
{
  GstPulseDeviceProvider *self = GST_PULSE_DEVICE_PROVIDER (provider);

  pa_threaded_mainloop_stop (self->mainloop);

  if (self->context) {
    pa_context_disconnect (self->context);

    /* Make sure we don't get any further callbacks */
    pa_context_set_state_callback (self->context, NULL, NULL);
    pa_context_set_subscribe_callback (self->context, NULL, NULL);

    pa_context_unref (self->context);
    self->context = NULL;
  }

  pa_threaded_mainloop_free (self->mainloop);
  self->mainloop = NULL;
}

enum
{
  PROP_INTERNAL_NAME = 1,
};

G_DEFINE_TYPE (GstPulseDevice, gst_pulse_device, GST_TYPE_DEVICE);

static void gst_pulse_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_pulse_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_pulse_device_finalize (GObject * object);
static GstElement *gst_pulse_device_create_element (GstDevice * device,
    const gchar * name);
static gboolean gst_pulse_device_reconfigure_element (GstDevice * device,
    GstElement * element);

static void
gst_pulse_device_class_init (GstPulseDeviceClass * klass)
{
  GstDeviceClass *dev_class = GST_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  dev_class->create_element = gst_pulse_device_create_element;
  dev_class->reconfigure_element = gst_pulse_device_reconfigure_element;

  object_class->get_property = gst_pulse_device_get_property;
  object_class->set_property = gst_pulse_device_set_property;
  object_class->finalize = gst_pulse_device_finalize;

  g_object_class_install_property (object_class, PROP_INTERNAL_NAME,
      g_param_spec_string ("internal-name", "Internal PulseAudio device name",
          "The internal name of the PulseAudio device", "",
          G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_pulse_device_init (GstPulseDevice * device)
{
}

static void
gst_pulse_device_finalize (GObject * object)
{
  GstPulseDevice *device = GST_PULSE_DEVICE (object);

  g_free (device->internal_name);

  G_OBJECT_CLASS (gst_pulse_device_parent_class)->finalize (object);
}

static GstElement *
gst_pulse_device_create_element (GstDevice * device, const gchar * name)
{
  GstPulseDevice *pulse_dev = GST_PULSE_DEVICE (device);
  GstElement *elem;

  elem = gst_element_factory_make (pulse_dev->element, name);
  g_object_set (elem, "device", pulse_dev->internal_name, NULL);

  return elem;
}

static gboolean
gst_pulse_device_reconfigure_element (GstDevice * device, GstElement * element)
{
  GstPulseDevice *pulse_dev = GST_PULSE_DEVICE (device);

  if (!strcmp (pulse_dev->element, "pulsesrc")) {
    if (!GST_IS_PULSESRC (element))
      return FALSE;
  } else if (!strcmp (pulse_dev->element, "pulsesink")) {
    if (!GST_IS_PULSESINK (element))
      return FALSE;
  } else {
    g_assert_not_reached ();
  }

  g_object_set (element, "device", pulse_dev->internal_name, NULL);

  return TRUE;
}

/* Takes ownership of @caps and @props */
static GstDevice *
gst_pulse_device_new (guint device_index, const gchar * device_name,
    GstCaps * caps, const gchar * internal_name, GstPulseDeviceType type,
    GstStructure * props)
{
  GstPulseDevice *gstdev;
  const gchar *element = NULL;
  const gchar *klass = NULL;

  g_return_val_if_fail (device_name, NULL);
  g_return_val_if_fail (internal_name, NULL);
  g_return_val_if_fail (caps, NULL);


  switch (type) {
    case GST_PULSE_DEVICE_TYPE_SOURCE:
      element = "pulsesrc";
      klass = "Audio/Source";
      break;
    case GST_PULSE_DEVICE_TYPE_SINK:
      element = "pulsesink";
      klass = "Audio/Sink";
      break;
    default:
      g_assert_not_reached ();
      break;
  }


  gstdev = g_object_new (GST_TYPE_PULSE_DEVICE,
      "display-name", device_name, "caps", caps, "device-class", klass,
      "internal-name", internal_name, "properties", props, NULL);

  gstdev->type = type;
  gstdev->device_index = device_index;
  gstdev->element = element;

  gst_structure_free (props);
  gst_caps_unref (caps);

  return GST_DEVICE (gstdev);
}


static void
gst_pulse_device_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstPulseDevice *device;

  device = GST_PULSE_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_INTERNAL_NAME:
      g_value_set_string (value, device->internal_name);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}


static void
gst_pulse_device_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPulseDevice *device;

  device = GST_PULSE_DEVICE_CAST (object);

  switch (prop_id) {
    case PROP_INTERNAL_NAME:
      device->internal_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}
