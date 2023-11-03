/*
 * GStreamer
 * Copyright (C) 2023 Seungha Yang <seungha@centricular.com>
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

#include "gstdecklink2device.h"
#include "gstdecklink2deviceprovider.h"
#include <stdlib.h>
#include <atomic>
#include <mutex>
#include <vector>
#include <algorithm>
#include <string>

GST_DEBUG_CATEGORY_EXTERN (gst_decklink2_debug);
#define GST_CAT_DEFAULT gst_decklink2_debug

/* *INDENT-OFF* */
static std::vector<GstDeckLink2Device *> device_list;
static std::mutex device_lock;
/* *INDENT-ON* */

struct _GstDeckLink2Device
{
  GstObject parent;

  GstDeckLink2APILevel api_level;

  IDeckLink *device;
  IDeckLinkProfileAttributes *attr;
  IDeckLinkAttributes_v10_11 *attr_10_11;
  IDeckLinkConfiguration *config;
  IDeckLinkConfiguration_v10_11 *config_10_11;
  IDeckLinkProfileManager *profile_manager;

  GstDeckLink2Input *input;
  GstDevice *input_device;

  GstDeckLink2Output *output;
  GstDevice *output_device;

  guint device_number;
  gint64 persistent_id;
  gchar *serial_number;
  gchar *model_name;
  gchar *display_name;

  gboolean input_acquired;
  gboolean output_acquired;
};

static void gst_decklink2_device_dispose (GObject * object);
static void gst_decklink2_device_finalize (GObject * object);

#define gst_decklink2_device_parent_class parent_class
G_DEFINE_TYPE (GstDeckLink2Device, gst_decklink2_device, GST_TYPE_OBJECT);

static void
gst_decklink2_device_class_init (GstDeckLink2DeviceClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = gst_decklink2_device_dispose;
  object_class->finalize = gst_decklink2_device_finalize;
}

static void
gst_decklink2_device_init (GstDeckLink2Device * self)
{
}

static void
gst_decklink2_device_dispose (GObject * object)
{
  GstDeckLink2Device *self = GST_DECKLINK2_DEVICE (object);

  if (self->input) {
    gst_object_unparent (GST_OBJECT (self->input));
    self->input = NULL;
  }

  if (self->output) {
    gst_object_unparent (GST_OBJECT (self->output));
    self->output = NULL;
  }

  gst_clear_object (&self->input_device);
  gst_clear_object (&self->output_device);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_decklink2_device_finalize (GObject * object)
{
  GstDeckLink2Device *self = GST_DECKLINK2_DEVICE (object);

  GST_DECKLINK2_CLEAR_COM (self->attr);
  GST_DECKLINK2_CLEAR_COM (self->attr_10_11);
  GST_DECKLINK2_CLEAR_COM (self->config);
  GST_DECKLINK2_CLEAR_COM (self->config_10_11);
  GST_DECKLINK2_CLEAR_COM (self->profile_manager);
  GST_DECKLINK2_CLEAR_COM (self->device);

  g_free (self->serial_number);
  g_free (self->model_name);
  g_free (self->display_name);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

const gchar *
gst_decklink2_device_get_serial_number (GstDeckLink2Device * device)
{
  return device->serial_number;
}

static GstDeckLink2Device *
gst_decklink2_device_new (IDeckLink * device, guint index,
    GstDeckLink2APILevel api_level, const gchar * api_ver_str,
    const gchar * driver_ver_str)
{
  HRESULT hr;
  GstDeckLink2Input *input;
  GstDeckLink2Output *output;
  GstDeckLink2Device *self;
  dlstring_t str;
  gint64 max_num_audio_channels = 0;
  GstCaps *caps = NULL;

  input = gst_decklink2_input_new (device, api_level);
  output = gst_decklink2_output_new (device, api_level);

  if (!output && !input)
    return NULL;

  self = (GstDeckLink2Device *)
      g_object_new (GST_TYPE_DECKLINK2_DEVICE, NULL);
  gst_object_ref_sink (self);

  self->api_level = api_level;

  if (input)
    gst_object_set_parent (GST_OBJECT (input), GST_OBJECT (self));

  if (output)
    gst_object_set_parent (GST_OBJECT (output), GST_OBJECT (self));

  self->input = input;
  self->output = output;
  self->device_number = index;
  self->device = device;
  device->AddRef ();

  if (api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    IDeckLinkConfiguration_v10_11 *config_10_11 = NULL;
    hr = device->QueryInterface (IID_IDeckLinkConfiguration_v10_11,
        (void **) &config_10_11);
    if (!gst_decklink2_result (hr)) {
      GST_WARNING_OBJECT (self, "Couldn't get config object");
      gst_object_unref (self);
      return NULL;
    }

    hr = config_10_11->GetString
        (bmdDeckLinkConfigDeviceInformationSerialNumber, &str);

    if (gst_decklink2_result (hr)) {
      std::string serial_number = DlToStdString (str);
      DeleteString (str);

      self->serial_number = g_strdup (serial_number.c_str ());
      GST_DEBUG_OBJECT (self, "device %d has serial number %s", index,
          GST_STR_NULL (self->serial_number));
    }

    self->config_10_11 = config_10_11;
  } else {
    IDeckLinkConfiguration *config = NULL;
    hr = device->QueryInterface (IID_IDeckLinkConfiguration, (void **) &config);
    if (!gst_decklink2_result (hr)) {
      GST_WARNING_OBJECT (self, "Couldn't get config object");
      gst_object_unref (self);
      return NULL;
    }

    hr = config->GetString (bmdDeckLinkConfigDeviceInformationSerialNumber,
        &str);
    if (gst_decklink2_result (hr)) {
      std::string serial_number = DlToStdString (str);
      DeleteString (str);

      self->serial_number = g_strdup (serial_number.c_str ());
      GST_DEBUG_OBJECT (self, "device %d has serial number %s", index,
          GST_STR_NULL (self->serial_number));
    }

    self->config = config;
    hr = device->QueryInterface (IID_IDeckLinkProfileManager,
        (void **) &self->profile_manager);
    if (!gst_decklink2_result (hr)) {
      GST_DEBUG_OBJECT (self,
          "IDeckLinkProfileManager interface is not available");
    }
  }

  if (api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    hr = device->QueryInterface (IID_IDeckLinkAttributes_v10_11,
        (void **) &self->attr_10_11);
  } else {
    hr = device->QueryInterface (IID_IDeckLinkProfileAttributes,
        (void **) &self->attr);
  }

  if (!gst_decklink2_result (hr)) {
    GST_WARNING_OBJECT (self,
        "IDeckLinkProfileAttributes interface is not available");
    self->persistent_id = self->device_number;
  } else {
    hr = E_FAIL;
    if (self->attr) {
      hr = self->attr->GetInt (BMDDeckLinkPersistentID, &self->persistent_id);
      if (!gst_decklink2_result (hr))
        self->persistent_id = self->device_number;
      hr = self->attr->GetInt (BMDDeckLinkMaximumAudioChannels,
          &max_num_audio_channels);
    } else if (self->attr_10_11) {
      hr = self->attr_10_11->GetInt (BMDDeckLinkPersistentID,
          &self->persistent_id);
      if (!gst_decklink2_result (hr))
        self->persistent_id = self->device_number;
      hr = self->attr_10_11->GetInt (BMDDeckLinkMaximumAudioChannels,
          &max_num_audio_channels);
    }

    if (!gst_decklink2_result (hr)) {
      GST_WARNING_OBJECT (self, "Couldn't query max audio channels");
      max_num_audio_channels = 0;
    }
  }

  hr = device->GetModelName (&str);
  if (gst_decklink2_result (hr)) {
    std::string model_name = DlToStdString (str);
    DeleteString (str);

    self->model_name = g_strdup (model_name.c_str ());
  }

  hr = device->GetDisplayName (&str);
  if (gst_decklink2_result (hr)) {
    std::string display_name = DlToStdString (str);
    DeleteString (str);

    self->display_name = g_strdup (display_name.c_str ());
  }

  GstDeckLink2ColorCapabilities color_caps;
  gst_decklink2_get_color_capabilities (device, &color_caps);

  if (self->input) {
    caps = gst_decklink2_input_get_caps (self->input,
        bmdModeUnknown, bmdFormatUnspecified);
    self->input_device =
        gst_decklink2_provider_device_new (TRUE, self->model_name,
        self->display_name, self->serial_number, caps, self->persistent_id,
        self->device_number, (guint) max_num_audio_channels, driver_ver_str,
        api_ver_str, &color_caps);
    gst_clear_caps (&caps);
    gst_object_ref_sink (self->input_device);
  }

  if (self->output) {
    caps = gst_decklink2_output_get_caps (self->output,
        bmdModeUnknown, bmdFormatUnspecified);
    self->output_device =
        gst_decklink2_provider_device_new (FALSE, self->model_name,
        self->display_name, self->serial_number, caps, self->persistent_id,
        self->device_number, (guint) max_num_audio_channels, driver_ver_str,
        api_ver_str, &color_caps);
    gst_clear_caps (&caps);
    gst_object_ref_sink (self->output_device);
  }

  return self;
}

#ifdef G_OS_WIN32
static IDeckLinkIterator *
CreateDeckLinkIteratorInstance (void)
{
  IDeckLinkIterator *iter = NULL;
  CoCreateInstance (CLSID_CDeckLinkIterator, NULL, CLSCTX_ALL,
      IID_IDeckLinkIterator, (void **) &iter);

  return iter;
}

static IDeckLinkIterator *
CreateDeckLinkIteratorInstance_v10_11 (void)
{
  IDeckLinkIterator *iter = NULL;
  CoCreateInstance (CLSID_CDeckLinkIterator_v10_11, NULL, CLSCTX_ALL,
      IID_IDeckLinkIterator, (void **) &iter);

  return iter;
}
#endif

static void
gst_decklink2_device_discover (void)
{
  GstDeckLink2APILevel api_level = gst_decklink2_get_api_level ();
  IDeckLinkIterator *iter = NULL;
  HRESULT hr = S_OK;
  guint major, minor, sub, extra;
  std::string driver_version;
  std::string api_version;

  if (api_level == GST_DECKLINK2_API_LEVEL_UNKNOWN)
    return;

  api_version = gst_decklink2_api_level_to_string (api_level);

  gst_decklink2_get_api_version (&major, &minor, &sub, &extra);
  driver_version = std::to_string (major) + "." + std::to_string (minor)
      + "." + std::to_string (sub) + "." + std::to_string (extra);

  if (api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    iter = CreateDeckLinkIteratorInstance_v10_11 ();
  } else {
    iter = CreateDeckLinkIteratorInstance ();
  }

  if (!iter) {
    GST_DEBUG ("Couldn't create device iterator");
    return;
  }

  guint i = 0;
  do {
    IDeckLink *decklink = NULL;
    GstDeckLink2Device *device;
    hr = iter->Next (&decklink);
    if (!gst_decklink2_result (hr))
      break;

    device = gst_decklink2_device_new (decklink, i, api_level,
        api_version.c_str (), driver_version.c_str ());
    decklink->Release ();

    if (device)
      device_list.push_back (device);

    i++;
  } while (gst_decklink2_result (hr));

  iter->Release ();

  std::sort (device_list.begin (), device_list.end (),
      [](const GstDeckLink2Device * a, const GstDeckLink2Device * b)->bool
      {
        {
          return a->persistent_id < b->persistent_id;
        }
      }
  );

  GST_DEBUG ("Found %u devices", (guint) device_list.size ());
}

static void
gst_decklink2_device_init_once (void)
{
  GST_DECKLINK2_CALL_ONCE_BEGIN {
    std::lock_guard < std::mutex > lk (device_lock);
    gst_decklink2_device_discover ();
  } GST_DECKLINK2_CALL_ONCE_END;
}

GstDeckLink2Input *
gst_decklink2_acquire_input (guint device_number, gint64 persistent_id)
{
  GstDeckLink2Device *target = NULL;

  gst_decklink2_device_init_once ();

  std::lock_guard < std::mutex > lk (device_lock);

  /* *INDENT-OFF* */
  if (persistent_id != -1) {
    auto device = std::find_if (device_list.begin (), device_list.end (),
      [&](const GstDeckLink2Device * obj) {
        return obj->persistent_id == persistent_id;
      });

    if (device == device_list.end ()) {
      GST_WARNING ("Couldn't find object for persistent id %" G_GINT64_FORMAT,
          persistent_id);
      return NULL;
    }

    target = *device;
  }

  if (!target) {
    auto device = std::find_if (device_list.begin (), device_list.end (),
        [&](const GstDeckLink2Device * obj) {
          return obj->device_number == device_number;
        });

    if (device == device_list.end ()) {
      GST_WARNING ("Couldn't find object for device number %u", device_number);
      return NULL;
    }

    target = *device;
  }
  /* *INDENT-ON* */

  if (!target->input) {
    GST_WARNING_OBJECT (target, "Device does not support input");
    return NULL;
  }

  if (target->input_acquired) {
    GST_WARNING_OBJECT (target, "Input was already acquired");
    return NULL;
  }

  target->input_acquired = TRUE;
  return (GstDeckLink2Input *) gst_object_ref (target->input);
}

GstDeckLink2Output *
gst_decklink2_acquire_output (guint device_number, gint64 persistent_id)
{
  GstDeckLink2Device *target = NULL;

  gst_decklink2_device_init_once ();

  std::lock_guard < std::mutex > lk (device_lock);

  /* *INDENT-OFF* */
  if (persistent_id != -1) {
    auto device = std::find_if (device_list.begin (), device_list.end (),
      [&](const GstDeckLink2Device * obj) {
        return obj->persistent_id == persistent_id;
      });

    if (device == device_list.end ()) {
      GST_WARNING ("Couldn't find object for persistent id %" G_GINT64_FORMAT,
          persistent_id);
      return NULL;
    }

    target = *device;
  }

  if (!target) {
    auto device = std::find_if (device_list.begin (), device_list.end (),
        [&](const GstDeckLink2Device * obj) {
          return obj->device_number == device_number;
        });

    if (device == device_list.end ()) {
      GST_WARNING ("Couldn't find object for device number %u", device_number);
      return NULL;
    }

    target = *device;
  }
  /* *INDENT-ON* */

  if (!target->output) {
    GST_WARNING_OBJECT (target, "Device does not support output");
    return NULL;
  }

  if (target->output_acquired) {
    GST_WARNING_OBJECT (target, "Output was already acquired");
    return NULL;
  }

  target->output_acquired = TRUE;
  return (GstDeckLink2Output *) gst_object_ref (target->output);
}

void
gst_decklink2_release_input (GstDeckLink2Input * input)
{
  std::unique_lock < std::mutex > lk (device_lock);
  auto device = std::find_if (device_list.begin (), device_list.end (),
      [&](const GstDeckLink2Device * obj) {
        return obj->input == input;
      }
  );

  if (device == device_list.end ()) {
    GST_ERROR_OBJECT (input, "Couldn't find parent object");
  } else {
    (*device)->input_acquired = FALSE;
  }
  lk.unlock ();

  gst_object_unref (input);
}

void
gst_decklink2_release_output (GstDeckLink2Output * output)
{
  std::unique_lock < std::mutex > lk (device_lock);
  auto device = std::find_if (device_list.begin (), device_list.end (),
      [&](const GstDeckLink2Device * obj) {
        return obj->output == output;
      }
  );

  if (device == device_list.end ()) {
    GST_ERROR_OBJECT (output, "Couldn't find parent object");
  } else {
    (*device)->output_acquired = FALSE;
  }
  lk.unlock ();

  gst_object_unref (output);
}

void
gst_decklink2_device_deinit (void)
{
  std::lock_guard < std::mutex > lk (device_lock);

  /* *INDENT-OFF* */
  for (auto iter : device_list)
    gst_object_unref (iter);
  /* *INDENT-ON* */

  device_list.clear ();
}

GList *
gst_decklink2_get_devices (void)
{
  GQueue queue = G_QUEUE_INIT;

  gst_decklink2_device_init_once ();

  std::lock_guard < std::mutex > lk (device_lock);

  /* *INDENT-OFF* */
  for (auto iter : device_list) {
    if (iter->input_device)
      g_queue_push_tail (&queue, gst_object_ref (iter->input_device));

    if (iter->output_device)
      g_queue_push_tail (&queue, gst_object_ref (iter->output_device));
  }
  /* *INDENT-ON* */

  return queue.head;
}

static HRESULT
gst_decklink2_set_duplex_mode (gint64 persistent_id, BMDDuplexMode_v10_11 mode)
{
  GstDeckLink2Device *device = NULL;
  HRESULT hr = E_FAIL;
  dlbool_t duplex_supported = FALSE;

  std::lock_guard < std::mutex > lk (device_lock);

  /* *INDENT-OFF* */
  for (auto iter : device_list) {
    if (iter->persistent_id == persistent_id) {
      device = iter;
      break;
    }
  }
  /* *INDENT-ON* */

  if (!device) {
    GST_ERROR ("Couldn't find device for persistent id %" G_GINT64_FORMAT,
        persistent_id);
    return E_FAIL;
  }

  if (!device->attr_10_11 || !device->config_10_11) {
    GST_WARNING_OBJECT (device,
        "Couldn't set duplex mode, missing required interface");
    return E_FAIL;
  }

  hr = device->attr_10_11->GetFlag ((BMDDeckLinkAttributeID)
      BMDDeckLinkSupportsDuplexModeConfiguration_v10_11, &duplex_supported);
  if (!gst_decklink2_result (hr)) {
    GST_WARNING_OBJECT (device, "Couldn't query duplex mode support");
    return hr;
  }

  if (!duplex_supported) {
    GST_WARNING_OBJECT (device, "Duplex mode is not supported");
    return E_FAIL;
  }

  return device->config_10_11->SetInt ((BMDDeckLinkConfigurationID)
      bmdDeckLinkConfigDuplexMode_v10_11, mode);
}

HRESULT
gst_decklink2_device_set_profile_id (GstDeckLink2Device * device,
    BMDProfileID profile_id)
{
  gchar *profile_id_str = NULL;
  HRESULT hr = E_FAIL;

  g_return_val_if_fail (GST_IS_DECKLINK2_DEVICE (device), E_INVALIDARG);

  if (profile_id == bmdProfileDefault)
    return S_OK;

  profile_id_str = g_enum_to_string (GST_TYPE_DECKLINK2_PROFILE_ID, profile_id);

  GST_DEBUG_OBJECT (device, "Setting profile id \"%s\"", profile_id_str);

  if (device->api_level == GST_DECKLINK2_API_LEVEL_10_11) {
    dlbool_t duplex_supported = FALSE;
    BMDDuplexMode_v10_11 duplex_mode = bmdDuplexModeHalf_v10_11;

    if (!device->attr_10_11 || !device->config_10_11) {
      GST_DEBUG_OBJECT (device, "Profile configuration is not supported");
      hr = S_OK;
      goto out;
    }

    switch (profile_id) {
      case bmdProfileOneSubDeviceHalfDuplex:
      case bmdProfileTwoSubDevicesHalfDuplex:
      case bmdProfileFourSubDevicesHalfDuplex:
        duplex_mode = bmdDuplexModeHalf_v10_11;
        GST_DEBUG_OBJECT (device, "Mapping \"%s\" to bmdDuplexModeHalf",
            profile_id_str);
        break;
      default:
        GST_DEBUG_OBJECT (device, "Mapping \"%s\" to bmdDuplexModeFull",
            profile_id_str);
        duplex_mode = bmdDuplexModeFull_v10_11;
        break;
    }

    hr = device->attr_10_11->GetFlag ((BMDDeckLinkAttributeID)
        BMDDeckLinkSupportsDuplexModeConfiguration_v10_11, &duplex_supported);

    if (!gst_decklink2_result (hr)) {
      GST_DEBUG_OBJECT (device, "Profile configuration is not supported");
      hr = S_OK;
      goto out;
    }

    if (!duplex_supported) {
      gint64 paired_device_id = 0;

      if (duplex_mode == bmdDuplexModeFull_v10_11) {
        GST_DEBUG_OBJECT (device, "Profile configuration is not supported");
        hr = S_OK;
        goto out;
      }

      hr = device->attr_10_11->GetInt ((BMDDeckLinkAttributeID)
          BMDDeckLinkPairedDevicePersistentID_v10_11, &paired_device_id);
      if (!gst_decklink2_result (hr)) {
        GST_DEBUG_OBJECT (device, "Profile configuration is not supported");
        hr = S_OK;
        goto out;
      }

      GST_DEBUG_OBJECT (device,
          "Device has paired device, setting duplex mode to paired device");
      hr = gst_decklink2_set_duplex_mode (paired_device_id, duplex_mode);
    } else {
      hr = device->config_10_11->SetInt ((BMDDeckLinkConfigurationID)
          bmdDeckLinkConfigDuplexMode_v10_11, duplex_mode);
    }
  } else {
    IDeckLinkProfile *profile = NULL;

    if (!device->profile_manager) {
      GST_DEBUG_OBJECT (device,
          "Profile \"%s\" is requested but profile manager is not available",
          profile_id_str);
      hr = S_OK;
      goto out;
    }

    hr = device->profile_manager->GetProfile (profile_id, &profile);
    if (gst_decklink2_result (hr)) {
      hr = profile->SetActive ();
      profile->Release ();
    }
  }

  if (!gst_decklink2_result (hr)) {
    GST_WARNING_OBJECT (device, "Couldn't set profile \"%s\"", profile_id_str);
  } else {
    GST_DEBUG_OBJECT (device, "Profile \"%s\" is configured", profile_id_str);
  }

out:
  g_free (profile_id_str);

  return hr;
}
