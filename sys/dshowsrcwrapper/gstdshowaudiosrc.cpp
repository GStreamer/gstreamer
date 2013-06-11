/* GStreamer
 * Copyright (C)  2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowaudiosrc.c:
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

#include "gstdshowaudiosrc.h"

GST_DEBUG_CATEGORY_STATIC (dshowaudiosrc_debug);
#define GST_CAT_DEFAULT dshowaudiosrc_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw-int, "
        "endianness = (int) { " G_STRINGIFY (G_BYTE_ORDER) " }, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 16, "
        "depth = (int) 16, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]; "
        "audio/x-raw-int, "
        "signed = (boolean) { TRUE, FALSE }, "
        "width = (int) 8, "
        "depth = (int) 8, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, 2 ]")
    );

static void gst_dshowaudiosrc_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstDshowAudioSrc, gst_dshowaudiosrc, GstAudioSrc,
    GST_TYPE_AUDIO_SRC, gst_dshowaudiosrc_init_interfaces);

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

static void gst_dshowaudiosrc_probe_interface_init (GstPropertyProbeInterface *
    iface);
static const GList *gst_dshowaudiosrc_probe_get_properties (GstPropertyProbe *
    probe);
static GValueArray *gst_dshowaudiosrc_probe_get_values (GstPropertyProbe *
    probe, guint prop_id, const GParamSpec * pspec);
static GValueArray *gst_dshowaudiosrc_get_device_name_values (GstDshowAudioSrc *
    src);


static void gst_dshowaudiosrc_dispose (GObject * gobject);
static void gst_dshowaudiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dshowaudiosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_dshowaudiosrc_get_caps (GstBaseSrc * src);
static GstStateChangeReturn gst_dshowaudiosrc_change_state (GstElement *
    element, GstStateChange transition);

static gboolean gst_dshowaudiosrc_open (GstAudioSrc * asrc);
static gboolean gst_dshowaudiosrc_prepare (GstAudioSrc * asrc,
    GstRingBufferSpec * spec);
static gboolean gst_dshowaudiosrc_unprepare (GstAudioSrc * asrc);
static gboolean gst_dshowaudiosrc_close (GstAudioSrc * asrc);
static guint gst_dshowaudiosrc_read (GstAudioSrc * asrc, gpointer data,
    guint length);
static guint gst_dshowaudiosrc_delay (GstAudioSrc * asrc);
static void gst_dshowaudiosrc_reset (GstAudioSrc * asrc);

/* utils */
static GstCaps *gst_dshowaudiosrc_getcaps_from_streamcaps (GstDshowAudioSrc *
    src, IPin * pin, IAMStreamConfig * streamcaps);
static gboolean gst_dshowaudiosrc_push_buffer (guint8 * buffer, guint size,
    gpointer src_object, GstClockTime duration);

static void
gst_dshowaudiosrc_init_interfaces (GType type)
{
  static const GInterfaceInfo dshowaudiosrc_info = {
    (GInterfaceInitFunc) gst_dshowaudiosrc_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_PROPERTY_PROBE, &dshowaudiosrc_info);
}

static void
gst_dshowaudiosrc_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_dshowaudiosrc_probe_get_properties;
/*  iface->needs_probe    = gst_dshowaudiosrc_probe_needs_probe;
  iface->probe_property = gst_dshowaudiosrc_probe_probe_property;*/
  iface->get_values = gst_dshowaudiosrc_probe_get_values;
}

static void
gst_dshowaudiosrc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_static_metadata (element_class,
      "Directshow audio capture source", "Source/Audio",
      "Receive data from a directshow audio capture graph",
      "Sebastien Moutte <sebastien@moutte.net>");
}

static void
gst_dshowaudiosrc_class_init (GstDshowAudioSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstAudioSrcClass *gstaudiosrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstaudiosrc_class = (GstAudioSrcClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_get_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_change_state);

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_get_caps);

  gstaudiosrc_class->open = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_open);
  gstaudiosrc_class->prepare = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_prepare);
  gstaudiosrc_class->unprepare =
      GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_unprepare);
  gstaudiosrc_class->close = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_close);
  gstaudiosrc_class->read = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_read);
  gstaudiosrc_class->delay = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_delay);
  gstaudiosrc_class->reset = GST_DEBUG_FUNCPTR (gst_dshowaudiosrc_reset);

  g_object_class_install_property
      (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "Directshow device reference (classID/name)", NULL,
          static_cast < GParamFlags > (G_PARAM_READWRITE)));

  g_object_class_install_property
      (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", NULL,
          static_cast < GParamFlags > (G_PARAM_READWRITE)));

  GST_DEBUG_CATEGORY_INIT (dshowaudiosrc_debug, "dshowaudiosrc", 0,
      "Directshow audio source");
}

static void
gst_dshowaudiosrc_init (GstDshowAudioSrc * src, GstDshowAudioSrcClass * klass)
{
  src->device = NULL;
  src->device_name = NULL;
  src->audio_cap_filter = NULL;
  src->dshow_fakesink = NULL;
  src->media_filter = NULL;
  src->filter_graph = NULL;
  src->caps = NULL;
  src->pins_mediatypes = NULL;

  src->gbarray = g_byte_array_new ();
  src->gbarray_lock = g_mutex_new ();

  src->is_running = FALSE;

  CoInitializeEx (NULL, COINIT_MULTITHREADED);
}

static void
gst_dshowaudiosrc_dispose (GObject * gobject)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (gobject);

  if (src->device) {
    g_free (src->device);
    src->device = NULL;
  }

  if (src->device_name) {
    g_free (src->device_name);
    src->device_name = NULL;
  }

  if (src->caps) {
    gst_caps_unref (src->caps);
    src->caps = NULL;
  }

  if (src->pins_mediatypes) {
    gst_dshow_free_pins_mediatypes (src->pins_mediatypes);
    src->pins_mediatypes = NULL;
  }

  if (src->gbarray) {
    g_byte_array_free (src->gbarray, TRUE);
    src->gbarray = NULL;
  }

  if (src->gbarray_lock) {
    g_mutex_free (src->gbarray_lock);
    src->gbarray_lock = NULL;
  }

  /* clean dshow */
  if (src->audio_cap_filter)
    src->audio_cap_filter->Release ();

  CoUninitialize ();

  G_OBJECT_CLASS (parent_class)->dispose (gobject);
}


static const GList *
gst_dshowaudiosrc_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *props = NULL;

  if (!props) {
    GParamSpec *pspec;

    pspec = g_object_class_find_property (klass, "device-name");
    props = g_list_append (props, pspec);
  }

  return props;
}

static GValueArray *
gst_dshowaudiosrc_get_device_name_values (GstDshowAudioSrc * src)
{
  GValueArray *array = g_value_array_new (0);
  ICreateDevEnum *devices_enum = NULL;
  IEnumMoniker *moniker_enum = NULL;
  IMoniker *moniker = NULL;
  HRESULT hres = S_FALSE;
  ULONG fetched;

  hres = CoCreateInstance (CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
      IID_ICreateDevEnum, (LPVOID *) & devices_enum);
  if (hres != S_OK) {
    GST_ERROR
        ("Can't create an instance of the system device enumerator (error=0x%x)",
        hres);
    array = NULL;
    goto clean;
  }

  hres = devices_enum->CreateClassEnumerator (CLSID_AudioInputDeviceCategory,
      &moniker_enum, 0);
  if (hres != S_OK || !moniker_enum) {
    GST_ERROR ("Can't get enumeration of audio devices (error=0x%x)", hres);
    array = NULL;
    goto clean;
  }

  moniker_enum->Reset ();

  while (hres = moniker_enum->Next (1, &moniker, &fetched), hres == S_OK) {
    IPropertyBag *property_bag = NULL;

    hres = moniker->BindToStorage (NULL, NULL, IID_IPropertyBag,
        (LPVOID *) & property_bag);
    if (SUCCEEDED (hres) && property_bag) {
      VARIANT varFriendlyName;

      VariantInit (&varFriendlyName);
      hres = property_bag->Read (L"FriendlyName", &varFriendlyName, NULL);
      if (hres == S_OK && varFriendlyName.bstrVal) {
        gchar *friendly_name =
            g_utf16_to_utf8 ((const gunichar2 *) varFriendlyName.bstrVal,
            wcslen (varFriendlyName.bstrVal), NULL, NULL, NULL);

        GValue value = { 0 };
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, friendly_name);
        g_value_array_append (array, &value);
        g_value_unset (&value);
        g_free (friendly_name);
        SysFreeString (varFriendlyName.bstrVal);
      }
      property_bag->Release ();
    }
    moniker->Release ();
  }

clean:
  if (moniker_enum)
    moniker_enum->Release ();

  if (devices_enum)
    devices_enum->Release ();

  return array;
}

static GValueArray *
gst_dshowaudiosrc_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case PROP_DEVICE_NAME:
      array = gst_dshowaudiosrc_get_device_name_values (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_dshowaudiosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (object);

  switch (prop_id) {
    case PROP_DEVICE:
    {
      if (src->device) {
        g_free (src->device);
        src->device = NULL;
      }
      if (g_value_get_string (value)) {
        src->device = g_strdup (g_value_get_string (value));
      }
      break;
    }
    case PROP_DEVICE_NAME:
    {
      if (src->device_name) {
        g_free (src->device_name);
        src->device_name = NULL;
      }
      if (g_value_get_string (value)) {
        src->device_name = g_strdup (g_value_get_string (value));
      }
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_dshowaudiosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{

}

static GstCaps *
gst_dshowaudiosrc_get_caps (GstBaseSrc * basesrc)
{
  HRESULT hres = S_OK;
  IBindCtx *lpbc = NULL;
  IMoniker *audiom = NULL;
  DWORD dwEaten;
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (basesrc);
  gunichar2 *unidevice = NULL;

  if (src->device) {
    g_free (src->device);
    src->device = NULL;
  }

  src->device =
      gst_dshow_getdevice_from_devicename (&CLSID_AudioInputDeviceCategory,
      &src->device_name);
  if (!src->device) {
    GST_ERROR ("No audio device found.");
    return NULL;
  }
  unidevice =
      g_utf8_to_utf16 (src->device, strlen (src->device), NULL, NULL, NULL);

  if (!src->audio_cap_filter) {
    hres = CreateBindCtx (0, &lpbc);
    if (SUCCEEDED (hres)) {
      hres =
          MkParseDisplayName (lpbc, (LPCOLESTR) unidevice, &dwEaten, &audiom);
      if (SUCCEEDED (hres)) {
        hres = audiom->BindToObject (lpbc, NULL, IID_IBaseFilter,
            (LPVOID *) & src->audio_cap_filter);
        audiom->Release ();
      }
      lpbc->Release ();
    }
  }

  if (src->audio_cap_filter && !src->caps) {
    /* get the capture pins supported types */
    IPin *capture_pin = NULL;
    IEnumPins *enumpins = NULL;
    HRESULT hres;

    hres = src->audio_cap_filter->EnumPins (&enumpins);
    if (SUCCEEDED (hres)) {
      while (enumpins->Next (1, &capture_pin, NULL) == S_OK) {
        IKsPropertySet *pKs = NULL;

        hres =
            capture_pin->QueryInterface (IID_IKsPropertySet, (LPVOID *) & pKs);
        if (SUCCEEDED (hres) && pKs) {
          DWORD cbReturned;
          GUID pin_category;
          RPC_STATUS rpcstatus;

          hres =
              pKs->Get (AMPROPSETID_Pin,
              AMPROPERTY_PIN_CATEGORY, NULL, 0, &pin_category, sizeof (GUID),
              &cbReturned);

          /* we only want capture pins */
          if (UuidCompare (&pin_category, (UUID *) & PIN_CATEGORY_CAPTURE,
                  &rpcstatus) == 0) {
            IAMStreamConfig *streamcaps = NULL;

            if (SUCCEEDED (capture_pin->QueryInterface (IID_IAMStreamConfig,
                        (LPVOID *) & streamcaps))) {
              src->caps =
                  gst_dshowaudiosrc_getcaps_from_streamcaps (src, capture_pin,
                  streamcaps);
              streamcaps->Release ();
            }
          }
          pKs->Release ();
        }
        capture_pin->Release ();
      }
      enumpins->Release ();
    }
  }

  if (unidevice) {
    g_free (unidevice);
  }

  if (src->caps) {
    return gst_caps_ref (src->caps);
  }

  return NULL;
}

static GstStateChangeReturn
gst_dshowaudiosrc_change_state (GstElement * element, GstStateChange transition)
{
  HRESULT hres = S_FALSE;
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (src->media_filter)
        hres = src->media_filter->Run (0);
      if (hres != S_OK) {
        GST_ERROR ("Can't RUN the directshow capture graph (error=0x%x)", hres);
        src->is_running = FALSE;
        return GST_STATE_CHANGE_FAILURE;
      } else {
        src->is_running = TRUE;
      }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (src->media_filter)
        hres = src->media_filter->Stop ();
      if (hres != S_OK) {
        GST_ERROR ("Can't STOP the directshow capture graph (error=0x%x)",
            hres);
        return GST_STATE_CHANGE_FAILURE;
      }
      src->is_running = FALSE;

      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_dshowaudiosrc_open (GstAudioSrc * asrc)
{
  HRESULT hres = S_FALSE;
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (asrc);

  hres = CoCreateInstance (CLSID_FilterGraph, NULL, CLSCTX_INPROC,
      IID_IFilterGraph, (LPVOID *) & src->filter_graph);
  if (hres != S_OK || !src->filter_graph) {
    GST_ERROR
        ("Can't create an instance of the directshow graph manager (error=0x%x)",
        hres);
    goto error;
  }

  hres =
      src->filter_graph->QueryInterface (IID_IMediaFilter,
      (LPVOID *) & src->media_filter);
  if (hres != S_OK || !src->media_filter) {
    GST_ERROR
        ("Can't get IMediacontrol interface from the graph manager (error=0x%x)",
        hres);
    goto error;
  }

  src->dshow_fakesink = new CDshowFakeSink;
  src->dshow_fakesink->AddRef ();

  hres = src->filter_graph->AddFilter (src->audio_cap_filter, L"capture");
  if (hres != S_OK) {
    GST_ERROR
        ("Can't add the directshow capture filter to the graph (error=0x%x)",
        hres);
    goto error;
  }

  hres = src->filter_graph->AddFilter (src->dshow_fakesink, L"fakesink");
  if (hres != S_OK) {
    GST_ERROR ("Can't add our fakesink filter to the graph (error=0x%x)", hres);
    goto error;
  }

  return TRUE;

error:
  if (src->dshow_fakesink) {
    src->dshow_fakesink->Release ();
    src->dshow_fakesink = NULL;
  }

  if (src->media_filter) {
    src->media_filter->Release ();
    src->media_filter = NULL;
  }
  if (src->filter_graph) {
    src->filter_graph->Release ();
    src->filter_graph = NULL;
  }

  return FALSE;
}

static gboolean
gst_dshowaudiosrc_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{
  HRESULT hres;
  IPin *input_pin = NULL;
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (asrc);

  /* search the negociated caps in our caps list to get its index and the corresponding mediatype */
  if (gst_caps_is_subset (spec->caps, src->caps)) {
    guint i = 0;
    gint res = -1;

    for (; i < gst_caps_get_size (src->caps) && res == -1; i++) {
      GstCaps *capstmp = gst_caps_copy_nth (src->caps, i);

      if (gst_caps_is_subset (spec->caps, capstmp)) {
        res = i;
      }
      gst_caps_unref (capstmp);
    }

    if (res != -1 && src->pins_mediatypes) {
      /*get the corresponding media type and build the dshow graph */
      GstCapturePinMediaType *pin_mediatype = NULL;
      GList *type = g_list_nth (src->pins_mediatypes, res);

      if (type) {
        pin_mediatype = (GstCapturePinMediaType *) type->data;

        src->dshow_fakesink->gst_set_media_type (pin_mediatype->mediatype);
        src->dshow_fakesink->gst_set_buffer_callback (
            (push_buffer_func) gst_dshowaudiosrc_push_buffer, src);

        gst_dshow_get_pin_from_filter (src->dshow_fakesink, PINDIR_INPUT,
            &input_pin);
        if (!input_pin) {
          GST_ERROR ("Can't get input pin from our directshow fakesink filter");
          goto error;
        }

        spec->segsize = (gint) (spec->bytes_per_sample * spec->rate * spec->latency_time /
            GST_MSECOND);
        spec->segtotal = (gint) ((gfloat) spec->buffer_time /
            (gfloat) spec->latency_time + 0.5);
        if (!gst_dshow_configure_latency (pin_mediatype->capture_pin,
            spec->segsize))
        {
          GST_WARNING ("Could not change capture latency");
          spec->segsize = spec->rate * spec->channels;
          spec->segtotal = 2;
        };
        GST_INFO ("Configuring with segsize:%d segtotal:%d", spec->segsize, spec->segtotal);
        hres = src->filter_graph->ConnectDirect (pin_mediatype->capture_pin,
            input_pin, NULL);
        input_pin->Release ();

        if (hres != S_OK) {
          GST_ERROR
              ("Can't connect capture filter with fakesink filter (error=0x%x)",
              hres);
          goto error;
        }

      }
    }
  }

  return TRUE;

error:
  return FALSE;
}

static gboolean
gst_dshowaudiosrc_unprepare (GstAudioSrc * asrc)
{
  IPin *input_pin = NULL, *output_pin = NULL;
  HRESULT hres = S_FALSE;
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (asrc);

  /* disconnect filters */
  gst_dshow_get_pin_from_filter (src->audio_cap_filter, PINDIR_OUTPUT,
      &output_pin);
  if (output_pin) {
    hres = src->filter_graph->Disconnect (output_pin);
    output_pin->Release ();
  }

  gst_dshow_get_pin_from_filter (src->dshow_fakesink, PINDIR_INPUT, &input_pin);
  if (input_pin) {
    hres = src->filter_graph->Disconnect (input_pin);
    input_pin->Release ();
  }

  return TRUE;
}

static gboolean
gst_dshowaudiosrc_close (GstAudioSrc * asrc)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (asrc);

  if (!src->filter_graph)
    return TRUE;

  /*remove filters from the graph */
  src->filter_graph->RemoveFilter (src->audio_cap_filter);
  src->filter_graph->RemoveFilter (src->dshow_fakesink);

  /*release our gstreamer dshow sink */
  src->dshow_fakesink->Release ();
  src->dshow_fakesink = NULL;

  /*release media filter interface */
  src->media_filter->Release ();
  src->media_filter = NULL;

  /*release the filter graph manager */
  src->filter_graph->Release ();
  src->filter_graph = NULL;

  return TRUE;
}

static guint
gst_dshowaudiosrc_read (GstAudioSrc * asrc, gpointer data, guint length)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (asrc);
  guint ret = 0;

  if (!src->is_running)
    return -1;

  if (src->gbarray) {
  test:
    if (src->gbarray->len >= length) {
      g_mutex_lock (src->gbarray_lock);
      memcpy (data, src->gbarray->data + (src->gbarray->len - length), length);
      g_byte_array_remove_range (src->gbarray, src->gbarray->len - length,
          length);
      ret = length;
      g_mutex_unlock (src->gbarray_lock);
    } else {
      if (src->is_running) {
        Sleep (GST_BASE_AUDIO_SRC(src)->ringbuffer->spec.latency_time /
            GST_MSECOND / 10);
        goto test;
      }
    }
  }

  return ret;
}

static guint
gst_dshowaudiosrc_delay (GstAudioSrc * asrc)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (asrc);
  guint ret = 0;

  if (src->gbarray) {
    g_mutex_lock (src->gbarray_lock);
    if (src->gbarray->len) {
      ret = src->gbarray->len / 4;
    }
    g_mutex_unlock (src->gbarray_lock);
  }

  return ret;
}

static void
gst_dshowaudiosrc_reset (GstAudioSrc * asrc)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (asrc);

  g_mutex_lock (src->gbarray_lock);
  GST_DEBUG ("byte array size= %d", src->gbarray->len);
  if (src->gbarray->len > 0)
    g_byte_array_remove_range (src->gbarray, 0, src->gbarray->len);
  g_mutex_unlock (src->gbarray_lock);
}

static GstCaps *
gst_dshowaudiosrc_getcaps_from_streamcaps (GstDshowAudioSrc * src, IPin * pin,
    IAMStreamConfig * streamcaps)
{
  GstCaps *caps = NULL;
  HRESULT hres = S_OK;
  int icount = 0;
  int isize = 0;
  AUDIO_STREAM_CONFIG_CAPS ascc;
  int i = 0;

  if (!streamcaps)
    return NULL;

  streamcaps->GetNumberOfCapabilities (&icount, &isize);

  if (isize != sizeof (ascc))
    return NULL;

  for (; i < icount; i++) {
    GstCapturePinMediaType *pin_mediatype = g_new0 (GstCapturePinMediaType, 1);

    pin->AddRef ();
    pin_mediatype->capture_pin = pin;

    hres = streamcaps->GetStreamCaps (i, &pin_mediatype->mediatype,
        (BYTE *) & ascc);
    if (hres == S_OK && pin_mediatype->mediatype) {
      GstCaps *mediacaps = NULL;

      if (!caps)
        caps = gst_caps_new_empty ();

      if (gst_dshow_check_mediatype (pin_mediatype->mediatype, MEDIASUBTYPE_PCM,
              FORMAT_WaveFormatEx)) {
        WAVEFORMATEX *wavformat =
            (WAVEFORMATEX *) pin_mediatype->mediatype->pbFormat;
        mediacaps =
            gst_caps_new_simple ("audio/x-raw-int", "width", G_TYPE_INT,
            wavformat->wBitsPerSample, "depth", G_TYPE_INT,
            wavformat->wBitsPerSample, "endianness", G_TYPE_INT, G_BYTE_ORDER,
            "signed", G_TYPE_BOOLEAN, TRUE, "channels", G_TYPE_INT,
            wavformat->nChannels, "rate", G_TYPE_INT, wavformat->nSamplesPerSec,
            NULL);

        if (mediacaps) {
          src->pins_mediatypes =
              g_list_append (src->pins_mediatypes, pin_mediatype);
          gst_caps_append (caps, mediacaps);
        } else {
          gst_dshow_free_pin_mediatype (pin_mediatype);
        }
      } else {
        gst_dshow_free_pin_mediatype (pin_mediatype);
      }
    } else {
      gst_dshow_free_pin_mediatype (pin_mediatype);
    }
  }

  if (caps && gst_caps_is_empty (caps)) {
    gst_caps_unref (caps);
    caps = NULL;
  }

  return caps;
}

static gboolean
gst_dshowaudiosrc_push_buffer (guint8 * buffer, guint size, gpointer src_object,
    GstClockTime duration)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (src_object);

  if (!buffer || size == 0 || !src) {
    return FALSE;
  }

  g_mutex_lock (src->gbarray_lock);
  g_byte_array_prepend (src->gbarray, buffer, size);
  g_mutex_unlock (src->gbarray_lock);

  return TRUE;
}
