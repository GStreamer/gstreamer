/* GStreamer
 * Copyright (C) 2020 Seungha Yang <seungha.yang@navercorp.com>
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

#include <gst/gst.h>
#include "gstmftransform.h"
#include "gstmfutils.h"
#include <string.h>
#include <wrl.h>

using namespace Microsoft::WRL;

extern "C" {
GST_DEBUG_CATEGORY_EXTERN (gst_mf_transform_debug);
#define GST_CAT_DEFAULT gst_mf_transform_debug
}

enum
{
  PROP_0,
  PROP_DEVICE_NAME,
  PROP_HARDWARE,
  PROP_ENUM_PARAMS,
};

struct _GstMFTransform
{
  GstObject object;
  gboolean initialized;

  GstMFTransformEnumParams enum_params;

  gchar *device_name;
  gboolean hardware;

  IMFActivate *activate;
  IMFTransform *transform;
  ICodecAPI * codec_api;
  IMFMediaEventGenerator *event_gen;

  GQueue *output_queue;

  DWORD input_id;
  DWORD output_id;

  gboolean need_start;

  gint pending_need_input;
  gint pending_have_output;
};

#define gst_mf_transform_parent_class parent_class
G_DEFINE_TYPE (GstMFTransform, gst_mf_transform, GST_TYPE_OBJECT);

static void gst_mf_transform_constructed (GObject * object);
static void gst_mf_transform_finalize (GObject * object);
static void gst_mf_transform_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);
static void gst_mf_transform_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void
gst_mf_transform_class_init (GstMFTransformClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->constructed = gst_mf_transform_constructed;
  gobject_class->finalize = gst_mf_transform_finalize;
  gobject_class->get_property = gst_mf_transform_get_property;
  gobject_class->set_property = gst_mf_transform_set_property;

  g_object_class_install_property (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "device-name",
          "Device name", NULL,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_HARDWARE,
      g_param_spec_boolean ("hardware", "Hardware",
          "Whether hardware device or not", FALSE,
          (GParamFlags) (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_ENUM_PARAMS,
      g_param_spec_pointer ("enum-params", "Enum Params",
          "GstMFTransformEnumParams for MFTEnumEx",
          (GParamFlags) (G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
          G_PARAM_STATIC_STRINGS)));
}

static void
gst_mf_transform_init (GstMFTransform * self)
{
  self->output_queue = g_queue_new ();

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
}

static void
gst_mf_transform_clear_enum_params (GstMFTransformEnumParams *params)
{
  g_free (params->input_typeinfo);
  params->input_typeinfo = NULL;

  g_free (params->output_typeinfo);
  params->output_typeinfo = NULL;
}

static void
release_mf_sample (IMFSample * sample)
{
  if (sample)
    sample->Release ();
}

static void
gst_mf_transform_finalize (GObject * object)
{
  GstMFTransform *self = GST_MF_TRANSFORM (object);

  gst_mf_transform_close (self);

  if (self->activate)
    self->activate->Release ();

  gst_mf_transform_clear_enum_params (&self->enum_params);
  g_free (self->device_name);

  g_queue_free_full (self->output_queue, (GDestroyNotify) release_mf_sample);

  CoUninitialize ();

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_mf_transform_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstMFTransform *self = GST_MF_TRANSFORM (object);

  switch (prop_id) {
    case PROP_DEVICE_NAME:
      g_value_set_string (value, self->device_name);
      break;
    case PROP_HARDWARE:
      g_value_set_boolean (value, self->hardware);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_transform_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMFTransform *self = GST_MF_TRANSFORM (object);

  switch (prop_id) {
    case PROP_ENUM_PARAMS:
    {
      GstMFTransformEnumParams *params;
      params = (GstMFTransformEnumParams *) g_value_get_pointer (value);

      gst_mf_transform_clear_enum_params (&self->enum_params);
      self->enum_params.category = params->category;
      self->enum_params.enum_flags = params->enum_flags;
      self->enum_params.device_index = params->device_index;
      if (params->input_typeinfo) {
        self->enum_params.input_typeinfo = g_new0 (MFT_REGISTER_TYPE_INFO, 1);
        memcpy (self->enum_params.input_typeinfo, params->input_typeinfo,
            sizeof (MFT_REGISTER_TYPE_INFO));
      }

      if (params->output_typeinfo) {
        self->enum_params.output_typeinfo = g_new0 (MFT_REGISTER_TYPE_INFO, 1);
        memcpy (self->enum_params.output_typeinfo, params->output_typeinfo,
            sizeof (MFT_REGISTER_TYPE_INFO));
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_mf_transform_constructed (GObject * object)
{
  GstMFTransform *self = GST_MF_TRANSFORM (object);
  HRESULT hr;
  IMFActivate **devices = NULL;
  UINT32 num_devices, i;
  LPWSTR name = NULL;

  hr = MFTEnumEx (self->enum_params.category, self->enum_params.enum_flags,
      self->enum_params.input_typeinfo, self->enum_params.output_typeinfo,
      &devices, &num_devices);

  if (!gst_mf_result (hr)) {
    GST_WARNING_OBJECT (self, "MFTEnumEx failure");
    return;
  }

  if (num_devices == 0 || self->enum_params.device_index >= num_devices) {
    GST_WARNING_OBJECT (self, "No available device at index %d",
        self->enum_params.device_index);
    for (i = 0; i < num_devices; i++) {
      devices[i]->Release ();
    }

    CoTaskMemFree (devices);
    return;
  }

  self->activate = devices[self->enum_params.device_index];
  self->activate->AddRef ();

  for (i = 0; i < num_devices; i++)
    devices[i]->Release ();

  hr = self->activate->GetAllocatedString (MFT_FRIENDLY_NAME_Attribute,
    &name, NULL);

  if (gst_mf_result (hr)) {
    self->device_name = g_utf16_to_utf8 ((const gunichar2 *) name,
        -1, NULL, NULL, NULL);

    CoTaskMemFree (name);

    GST_INFO_OBJECT (self, "Open device %s", self->device_name);
  }

done:
  CoTaskMemFree (devices);

  self->hardware = ! !(self->enum_params.enum_flags & MFT_ENUM_FLAG_HARDWARE);
  self->initialized = TRUE;
}

static HRESULT
gst_mf_transform_pop_event (GstMFTransform * self,
    gboolean no_wait, MediaEventType * event_type)
{
  ComPtr<IMFMediaEvent> event;
  MediaEventType type;
  HRESULT hr;
  DWORD flags = 0;

  if (!self->hardware || !self->event_gen)
    return MF_E_NO_EVENTS_AVAILABLE;

  if (no_wait)
    flags = MF_EVENT_FLAG_NO_WAIT;

  hr = self->event_gen->GetEvent (flags, event.GetAddressOf ());

  if (hr == MF_E_NO_EVENTS_AVAILABLE)
    return hr;
  else if (!gst_mf_result (hr))
    return hr;

  hr = event->GetType (&type);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Failed to get event, hr: 0x%x", (guint) hr);

    return hr;
  }

  *event_type = type;
  return S_OK;
}

static void
gst_mf_transform_drain_all_events (GstMFTransform * self)
{
  HRESULT hr;

  if (!self->hardware)
    return;

  do {
    MediaEventType type;

    hr = gst_mf_transform_pop_event (self, TRUE, &type);
    if (hr == MF_E_NO_EVENTS_AVAILABLE || !gst_mf_result (hr))
      return;

    switch (type) {
      case METransformNeedInput:
        self->pending_need_input++;
        break;
      case METransformHaveOutput:
        self->pending_have_output++;
        break;
      default:
        GST_DEBUG_OBJECT (self, "Unhandled event %d", type);
        break;
    }
  } while (SUCCEEDED (hr));
}

static GstFlowReturn
gst_mf_transform_process_output (GstMFTransform * self)
{
  DWORD status;
  HRESULT hr;
  IMFTransform *transform = self->transform;
  DWORD stream_id = self->output_id;
  MFT_OUTPUT_STREAM_INFO out_stream_info = { 0 };
  MFT_OUTPUT_DATA_BUFFER out_data = { 0 };

  hr = transform->GetOutputStreamInfo (stream_id, &out_stream_info);
  if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "Couldn't get output stream info");
    return GST_FLOW_ERROR;
  }

  if ((out_stream_info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
              MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) == 0) {
    ComPtr<IMFMediaBuffer> buffer;
    ComPtr<IMFSample> new_sample;

    hr = MFCreateMemoryBuffer (out_stream_info.cbSize,
        buffer.GetAddressOf ());
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't create memory buffer");
      return GST_FLOW_ERROR;
    }

    hr = MFCreateSample (new_sample.GetAddressOf ());
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't create sample");
      return GST_FLOW_ERROR;
    }

    hr = new_sample->AddBuffer (buffer.Get ());
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't add buffer to sample");
      return GST_FLOW_ERROR;
    }

    out_data.pSample = new_sample.Detach ();
  }

  out_data.dwStreamID = stream_id;

  hr = transform->ProcessOutput (0, 1, &out_data, &status);

  if (self->hardware)
    self->pending_have_output--;

  if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
    GST_LOG_OBJECT (self, "Need more input data");
    return GST_MF_TRANSFORM_FLOW_NEED_DATA;
  } else if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
    ComPtr<IMFMediaType> output_type;

    GST_DEBUG_OBJECT (self, "Stream change, set output type again");

    hr = transform->GetOutputAvailableType (stream_id,
        0, output_type.GetAddressOf ());
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't get available output type");
      return GST_FLOW_ERROR;
    }

    hr = transform->SetOutputType (stream_id, output_type.Get (), 0);
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (self, "Couldn't set output type");
      return GST_FLOW_ERROR;
    }

    return GST_MF_TRANSFORM_FLOW_NEED_DATA;
  } else if (!gst_mf_result (hr)) {
    GST_ERROR_OBJECT (self, "ProcessOutput error");
    if (out_data.pSample)
      out_data.pSample->Release ();
    return GST_FLOW_ERROR;
  }

  if (!out_data.pSample) {
    GST_WARNING_OBJECT (self, "No output sample");
    return GST_FLOW_OK;
  }

  g_queue_push_tail (self->output_queue, out_data.pSample);

  return GST_FLOW_OK;
}

static gboolean
gst_mf_transform_process_input_sync (GstMFTransform * self,
    IMFSample * sample)
{
  HRESULT hr;

  hr = self->transform->ProcessInput (self->output_id, sample, 0);

  if (self->hardware)
    self->pending_need_input--;

  return gst_mf_result (hr);
}

gboolean
gst_mf_transform_process_input (GstMFTransform * object,
    IMFSample * sample)
{
  HRESULT hr;
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);
  g_return_val_if_fail (sample != NULL, FALSE);

  if (!object->transform)
    return FALSE;

  if (object->need_start) {
    hr = object->transform->ProcessMessage (MFT_MESSAGE_NOTIFY_START_OF_STREAM,
        0);
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (object, "Cannot post start-of-stream message");
      return FALSE;
    }

    hr = object->transform->ProcessMessage (MFT_MESSAGE_NOTIFY_BEGIN_STREAMING,
        0);
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (object, "Cannot post begin-stream message");
      return FALSE;
    }

    object->need_start = FALSE;
  }

  gst_mf_transform_drain_all_events (object);

  if (object->hardware) {
    while (object->pending_have_output > 0) {
      ret = gst_mf_transform_process_output (object);
      if (ret != GST_FLOW_OK) {
        if (ret == GST_VIDEO_ENCODER_FLOW_NEED_DATA) {
          ret = GST_FLOW_OK;
          break;
        } else {
          return FALSE;
        }
      }
    }

    while (object->pending_need_input == 0) {
      MediaEventType type;
      HRESULT hr;

      hr = gst_mf_transform_pop_event (object, FALSE, &type);
      if (hr != MF_E_NO_EVENTS_AVAILABLE && !gst_mf_result (hr)) {
        GST_DEBUG_OBJECT (object, "failed to pop event, hr: 0x%x", (guint) hr);
        return FALSE;
      }

      switch (type) {
        case METransformNeedInput:
          object->pending_need_input++;
          break;
        case METransformHaveOutput:
          object->pending_have_output++;
          break;
        default:
          GST_DEBUG_OBJECT (object, "Unhandled event %d", type);
          break;
      }
    }
  }

  return gst_mf_transform_process_input_sync (object, sample);
}

GstFlowReturn
gst_mf_transform_get_output (GstMFTransform * object,
    IMFSample ** sample)
{
  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), GST_FLOW_ERROR);
  g_return_val_if_fail (sample != NULL, GST_FLOW_ERROR);

  if (!object->transform)
    return GST_FLOW_ERROR;

  gst_mf_transform_drain_all_events (object);

  if (!object->hardware || object->pending_have_output)
    gst_mf_transform_process_output (object);

  if (g_queue_is_empty (object->output_queue))
    return GST_MF_TRANSFORM_FLOW_NEED_DATA;

  *sample = (IMFSample *) g_queue_pop_head (object->output_queue);

  return GST_FLOW_OK;
}

gboolean
gst_mf_transform_flush (GstMFTransform * object)
{
  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);

  if (object->transform) {
    if (!object->need_start)
      object->transform->ProcessMessage (MFT_MESSAGE_COMMAND_FLUSH, 0);

    object->pending_have_output = 0;
    object->pending_need_input = 0;
  }

  object->need_start = TRUE;

  while (!g_queue_is_empty (object->output_queue)) {
    IMFSample *sample = (IMFSample *) g_queue_pop_head (object->output_queue);
    sample->Release ();
  }

  return TRUE;
}

gboolean
gst_mf_transform_drain (GstMFTransform * object)
{
  GstFlowReturn ret;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);

  if (!object->transform)
    return TRUE;

  object->need_start = TRUE;
  object->transform->ProcessMessage (MFT_MESSAGE_COMMAND_DRAIN, 0);

  if (object->hardware) {
    MediaEventType type;
    HRESULT hr;

    do {
      hr = gst_mf_transform_pop_event (object, FALSE, &type);
      if (hr != MF_E_NO_EVENTS_AVAILABLE && FAILED (hr)) {
        GST_DEBUG_OBJECT (object, "failed to pop event, hr: 0x%x", (guint) hr);
        break;
      }

      switch (type) {
        case METransformNeedInput:
          GST_DEBUG_OBJECT (object, "Ignore need input during finish");
          break;
        case METransformHaveOutput:
          object->pending_have_output++;
          gst_mf_transform_process_output (object);
          break;
        case METransformDrainComplete:
          GST_DEBUG_OBJECT (object, "Drain complete");
          return TRUE;
        default:
          GST_DEBUG_OBJECT (object, "Unhandled event %d", type);
          break;
      }
    } while (SUCCEEDED (hr));

    /* and drain all the other events if any */
    gst_mf_transform_drain_all_events (object);

    object->pending_have_output = 0;
    object->pending_need_input = 0;
  } else {
    do {
      ret = gst_mf_transform_process_output (object);
    } while (ret == GST_FLOW_OK);
  }

  return TRUE;
}

gboolean
gst_mf_transform_open (GstMFTransform * object)
{
  HRESULT hr;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);

  gst_mf_transform_close (object);

  hr = object->activate->ActivateObject (IID_IMFTransform,
      (void **) &object->transform);

  if (!gst_mf_result (hr)) {
    GST_WARNING_OBJECT (object, "Couldn't open MFT");
    return FALSE;
  }

  if (object->hardware) {
    ComPtr<IMFAttributes> attr;

    hr = object->transform->GetAttributes (attr.GetAddressOf ());
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (object, "Couldn't get attribute object");
      goto error;
    }

    hr = attr->SetUINT32 (MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (object, "MF_TRANSFORM_ASYNC_UNLOCK error");
      goto error;
    }

    hr = object->transform->QueryInterface (IID_IMFMediaEventGenerator,
        (void **) &object->event_gen);
    if (!gst_mf_result (hr)) {
      GST_ERROR_OBJECT (object, "IMFMediaEventGenerator unavailable");
      goto error;
    }
  }

  hr = object->transform->GetStreamIDs (1, &object->input_id, 1,
      &object->output_id);
  if (hr == E_NOTIMPL) {
    object->input_id = 0;
    object->output_id = 0;
  }

  hr = object->transform->QueryInterface (IID_ICodecAPI,
      (void **) &object->codec_api);
  if (!gst_mf_result (hr)) {
    GST_WARNING_OBJECT (object, "ICodecAPI is unavailable");
  }

  return TRUE;

error:
  gst_mf_transform_close (object);
  return FALSE;
}

gboolean
gst_mf_transform_close (GstMFTransform * object)
{
  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);

  gst_mf_transform_flush (object);

  if (object->event_gen) {
    object->event_gen->Release ();
    object->event_gen = NULL;
  }

  if (object->codec_api) {
    object->codec_api->Release ();
    object->codec_api = NULL;
  }

  if (object->transform) {
    object->transform->Release ();
    object->transform = NULL;
  }

  return TRUE;
}

IMFActivate *
gst_mf_transform_get_activate_handle (GstMFTransform * object)
{
  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), NULL);

  return object->activate;
}

IMFTransform *
gst_mf_transform_get_transform_handle (GstMFTransform * object)
{
  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), NULL);

  if (!object->transform) {
    GST_WARNING_OBJECT (object,
        "IMFTransform is not configured, open MFT first");
    return NULL;
  }

  return object->transform;
}

ICodecAPI *
gst_mf_transform_get_codec_api_handle (GstMFTransform * object)
{
  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), NULL);

  if (!object->codec_api) {
    GST_WARNING_OBJECT (object,
        "ICodecAPI is not configured, open MFT first");
    return NULL;
  }

  return object->codec_api;
}

gboolean
gst_mf_transform_get_input_available_types (GstMFTransform * object,
    GList ** input_types)
{
  IMFTransform *transform;
  HRESULT hr;
  DWORD index = 0;
  GList *list = NULL;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);
  g_return_val_if_fail (input_types != NULL, FALSE);

  transform = object->transform;

  if (!transform) {
    GST_ERROR_OBJECT (object, "Should open first");
    return FALSE;
  }

  do {
    IMFMediaType *type = NULL;

    hr = transform->GetInputAvailableType (object->input_id, index, &type);
    if (SUCCEEDED (hr))
      list = g_list_append (list, type);

    index++;
  } while (SUCCEEDED (hr));

  *input_types = list;

  return !!list;
}

gboolean
gst_mf_transform_get_output_available_types (GstMFTransform * object,
    GList ** output_types)
{
  IMFTransform *transform;
  HRESULT hr;
  DWORD index = 0;
  GList *list = NULL;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);
  g_return_val_if_fail (output_types != NULL, FALSE);

  transform = object->transform;

  if (!transform) {
    GST_ERROR_OBJECT (object, "Should open first");
    return FALSE;
  }

  do {
    IMFMediaType *type;

    hr = transform->GetOutputAvailableType (object->input_id, index, &type);
    if (SUCCEEDED (hr))
      list = g_list_append (list, type);

    index++;
  } while (SUCCEEDED (hr));

  *output_types = list;

  return !!list;
}

gboolean
gst_mf_transform_set_input_type (GstMFTransform * object,
    IMFMediaType * input_type)
{
  IMFTransform *transform;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);

  transform = object->transform;

  if (!transform) {
    GST_ERROR_OBJECT (object, "Should open first");
    return FALSE;
  }

  hr = transform->SetInputType (object->input_id, input_type, 0);
  if (!gst_mf_result (hr))
    return FALSE;

  return TRUE;
}

gboolean
gst_mf_transform_set_output_type (GstMFTransform * object,
    IMFMediaType * output_type)
{
  IMFTransform *transform;
  HRESULT hr;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);

  transform = object->transform;

  if (!transform) {
    GST_ERROR_OBJECT (object, "Should open first");
    return FALSE;
  }

  hr = transform->SetOutputType (object->output_id, output_type, 0);
  if (!gst_mf_result (hr)) {
    return FALSE;
  }

  return TRUE;
}

GstMFTransform *
gst_mf_transform_new (GstMFTransformEnumParams * params)
{
  GstMFTransform *self;

  g_return_val_if_fail (params != NULL, NULL);

  self = (GstMFTransform *) g_object_new (GST_TYPE_MF_TRANSFORM_OBJECT,
      "enum-params", params, NULL);

  if (!self->initialized) {
    gst_object_unref (self);
    return NULL;
  }

  gst_object_ref_sink (self);

  return self;
}

gboolean
gst_mf_transform_set_codec_api_uint32 (GstMFTransform * object,
    const GUID * api, guint32 value)
{
  HRESULT hr;
  VARIANT var;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);
  g_return_val_if_fail (api != NULL, FALSE);

  if (!object->codec_api) {
    GST_WARNING_OBJECT (object, "codec api unavailable");
    return FALSE;
  }

  VariantInit (&var);
  var.vt = VT_UI4;
  var.ulVal = value;

  hr = object->codec_api->SetValue (api, &var);
  VariantClear (&var);

  return gst_mf_result (hr);
}

gboolean
gst_mf_transform_set_codec_api_uint64 (GstMFTransform * object,
    const GUID * api, guint64 value)
{
  HRESULT hr;
  VARIANT var;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);
  g_return_val_if_fail (api != NULL, FALSE);

  if (!object->codec_api) {
    GST_WARNING_OBJECT (object, "codec api unavailable");
    return FALSE;
  }

  VariantInit (&var);
  var.vt = VT_UI8;
  var.ullVal = value;

  hr = object->codec_api->SetValue (api, &var);
  VariantClear (&var);

  return gst_mf_result (hr);
}

gboolean
gst_mf_transform_set_codec_api_boolean (GstMFTransform * object,
    const GUID * api, gboolean value)
{
  HRESULT hr;
  VARIANT var;

  g_return_val_if_fail (GST_IS_MF_TRANSFORM (object), FALSE);
  g_return_val_if_fail (api != NULL, FALSE);

  if (!object->codec_api) {
    GST_WARNING_OBJECT (object, "codec api unavailable");
    return FALSE;
  }

  VariantInit (&var);
  var.vt = VT_BOOL;
  var.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;

  hr = object->codec_api->SetValue (api, &var);
  VariantClear (&var);

  return gst_mf_result (hr);
}

