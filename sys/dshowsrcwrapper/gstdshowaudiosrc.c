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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gstdshowaudiosrc.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

static const GstElementDetails gst_dshowaudiosrc_details =
GST_ELEMENT_DETAILS ("Directshow audio capture source",
    "Source/Audio",
    "Receive data from a directshow audio capture graph",
    "Sebastien Moutte <sebastien@moutte.net>");

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
static gboolean gst_dshowaudiosrc_push_buffer (byte * buffer, long size,
    byte * src_object, UINT64 start, UINT64 stop);

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

  gst_element_class_set_details (element_class, &gst_dshowaudiosrc_details);
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
          "Directshow device reference (classID/name)",
          NULL, G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device-name", "Device name",
          "Human-readable name of the sound device", NULL, G_PARAM_READWRITE));

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
  if (src->audio_cap_filter) {
    IBaseFilter_Release (src->audio_cap_filter);
  }

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
  GValue value = { 0 };
  ICreateDevEnum *devices_enum = NULL;
  IEnumMoniker *moniker_enum = NULL;
  IMoniker *moniker = NULL;
  HRESULT hres = S_FALSE;
  ULONG fetched;

  g_value_init (&value, G_TYPE_STRING);

  hres = CoCreateInstance (&CLSID_SystemDeviceEnum, NULL, CLSCTX_INPROC_SERVER,
      &IID_ICreateDevEnum, (void **) &devices_enum);
  if (hres != S_OK) {
    GST_CAT_ERROR (dshowaudiosrc_debug,
        "Can't create an instance of the system device enumerator (error=%d)",
        hres);
    array = NULL;
    goto clean;
  }

  hres =
      ICreateDevEnum_CreateClassEnumerator (devices_enum,
      &CLSID_AudioInputDeviceCategory, &moniker_enum, 0);
  if (hres != S_OK || !moniker_enum) {
    GST_CAT_ERROR (dshowaudiosrc_debug,
        "Can't get enumeration of audio devices (error=%d)", hres);
    array = NULL;
    goto clean;
  }

  IEnumMoniker_Reset (moniker_enum);

  while (hres = IEnumMoniker_Next (moniker_enum, 1, &moniker, &fetched),
      hres == S_OK) {
    IPropertyBag *property_bag = NULL;

    hres =
        IMoniker_BindToStorage (moniker, NULL, NULL, &IID_IPropertyBag,
        (void **) &property_bag);
    if (SUCCEEDED (hres) && property_bag) {
      VARIANT varFriendlyName;

      VariantInit (&varFriendlyName);
      hres =
          IPropertyBag_Read (property_bag, L"FriendlyName", &varFriendlyName,
          NULL);
      if (hres == S_OK && varFriendlyName.bstrVal) {
        gchar *friendly_name =
            g_utf16_to_utf8 ((const gunichar2 *) varFriendlyName.bstrVal,
            wcslen (varFriendlyName.bstrVal), NULL, NULL, NULL);

        g_value_set_string (&value, friendly_name);
        g_value_array_append (array, &value);
        g_value_unset (&value);
        g_free (friendly_name);
        SysFreeString (varFriendlyName.bstrVal);
      }
      IPropertyBag_Release (property_bag);
    }
    IMoniker_Release (moniker);
  }

clean:
  if (moniker_enum) {
    IEnumMoniker_Release (moniker_enum);
  }

  if (devices_enum) {
    ICreateDevEnum_Release (devices_enum);
  }

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
    GST_CAT_ERROR (dshowaudiosrc_debug, "No audio device found.");
    return NULL;
  }
  unidevice =
      g_utf8_to_utf16 (src->device, strlen (src->device), NULL, NULL, NULL);

  if (!src->audio_cap_filter) {
    hres = CreateBindCtx (0, &lpbc);
    if (SUCCEEDED (hres)) {
      hres = MkParseDisplayName (lpbc, unidevice, &dwEaten, &audiom);
      if (SUCCEEDED (hres)) {
        hres =
            IMoniker_BindToObject (audiom, lpbc, NULL, &IID_IBaseFilter,
            &src->audio_cap_filter);
        IMoniker_Release (audiom);
      }
      IBindCtx_Release (lpbc);
    }
  }

  if (src->audio_cap_filter && !src->caps) {
    /* get the capture pins supported types */
    IPin *capture_pin = NULL;
    IEnumPins *enumpins = NULL;
    HRESULT hres;

    hres = IBaseFilter_EnumPins (src->audio_cap_filter, &enumpins);
    if (SUCCEEDED (hres)) {
      while (IEnumPins_Next (enumpins, 1, &capture_pin, NULL) == S_OK) {
        IKsPropertySet *pKs = NULL;

        hres =
            IPin_QueryInterface (capture_pin, &IID_IKsPropertySet,
            (void **) &pKs);
        if (SUCCEEDED (hres) && pKs) {
          DWORD cbReturned;
          GUID pin_category;
          RPC_STATUS rpcstatus;

          hres =
              IKsPropertySet_Get (pKs, &AMPROPSETID_Pin,
              AMPROPERTY_PIN_CATEGORY, NULL, 0, &pin_category, sizeof (GUID),
              &cbReturned);

          /* we only want capture pins */
          if (UuidCompare (&pin_category, &PIN_CATEGORY_CAPTURE,
                  &rpcstatus) == 0) {
            IAMStreamConfig *streamcaps = NULL;

            if (SUCCEEDED (IPin_QueryInterface (capture_pin,
                        &IID_IAMStreamConfig, (void **) &streamcaps))) {
              src->caps =
                  gst_dshowaudiosrc_getcaps_from_streamcaps (src, capture_pin,
                  streamcaps);
              IAMStreamConfig_Release (streamcaps);
            }
          }
          IKsPropertySet_Release (pKs);
        }
        IPin_Release (capture_pin);
      }
      IEnumPins_Release (enumpins);
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
        hres = IMediaFilter_Run (src->media_filter, 0);
      if (hres != S_OK) {
        GST_CAT_ERROR (dshowaudiosrc_debug,
            "Can't RUN the directshow capture graph (error=%d)", hres);
        src->is_running = FALSE;
        return GST_STATE_CHANGE_FAILURE;
      } else {
        src->is_running = TRUE;
      }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (src->media_filter)
        hres = IMediaFilter_Stop (src->media_filter);
      if (hres != S_OK) {
        GST_CAT_ERROR (dshowaudiosrc_debug,
            "Can't STOP the directshow capture graph (error=%d)", hres);
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

  hres = CoCreateInstance (&CLSID_FilterGraph, NULL, CLSCTX_INPROC,
      &IID_IFilterGraph, (LPVOID *) & src->filter_graph);
  if (hres != S_OK || !src->filter_graph) {
    GST_CAT_ERROR (dshowaudiosrc_debug,
        "Can't create an instance of the directshow graph manager (error=%d)",
        hres);
    goto error;
  }

  hres = IFilterGraph_QueryInterface (src->filter_graph, &IID_IMediaFilter,
      (void **) &src->media_filter);
  if (hres != S_OK || !src->media_filter) {
    GST_CAT_ERROR (dshowaudiosrc_debug,
        "Can't get IMediacontrol interface from the graph manager (error=%d)",
        hres);
    goto error;
  }

  hres = CoCreateInstance (&CLSID_DshowFakeSink, NULL, CLSCTX_INPROC,
      &IID_IBaseFilter, (LPVOID *) & src->dshow_fakesink);
  if (hres != S_OK || !src->dshow_fakesink) {
    GST_CAT_ERROR (dshowaudiosrc_debug,
        "Can't create an instance of the directshow fakesink (error=%d)", hres);
    goto error;
  }

  hres =
      IFilterGraph_AddFilter (src->filter_graph, src->audio_cap_filter,
      L"capture");
  if (hres != S_OK) {
    GST_CAT_ERROR (dshowaudiosrc_debug,
        "Can't add the directshow capture filter to the graph (error=%d)",
        hres);
    goto error;
  }

  hres =
      IFilterGraph_AddFilter (src->filter_graph, src->dshow_fakesink,
      L"fakesink");
  if (hres != S_OK) {
    GST_CAT_ERROR (dshowaudiosrc_debug,
        "Can't add our fakesink filter to the graph (error=%d)", hres);
    goto error;
  }

  return TRUE;

error:
  if (src->dshow_fakesink) {
    IBaseFilter_Release (src->dshow_fakesink);
    src->dshow_fakesink = NULL;
  }

  if (src->media_filter) {
    IMediaFilter_Release (src->media_filter);
    src->media_filter = NULL;
  }
  if (src->filter_graph) {
    IFilterGraph_Release (src->filter_graph);
    src->filter_graph = NULL;
  }

  return FALSE;
}

static gboolean
gst_dshowaudiosrc_prepare (GstAudioSrc * asrc, GstRingBufferSpec * spec)
{
  HRESULT hres;
  IGstDshowInterface *srcinterface = NULL;
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

        hres =
            IBaseFilter_QueryInterface (src->dshow_fakesink,
            &IID_IGstDshowInterface, (void **) &srcinterface);
        if (hres != S_OK || !srcinterface) {
          GST_CAT_ERROR (dshowaudiosrc_debug,
              "Can't get IGstDshowInterface interface from our dshow fakesink filter (error=%d)",
              hres);
          goto error;
        }

        IGstDshowInterface_gst_set_media_type (srcinterface,
            pin_mediatype->mediatype);
        IGstDshowInterface_gst_set_buffer_callback (srcinterface,
            (byte *) gst_dshowaudiosrc_push_buffer, (byte *) src);

        if (srcinterface) {
          IGstDshowInterface_Release (srcinterface);
        }

        gst_dshow_get_pin_from_filter (src->dshow_fakesink, PINDIR_INPUT,
            &input_pin);
        if (!input_pin) {
          GST_CAT_ERROR (dshowaudiosrc_debug,
              "Can't get input pin from our directshow fakesink filter");
          goto error;
        }

        hres =
            IFilterGraph_ConnectDirect (src->filter_graph,
            pin_mediatype->capture_pin, input_pin, NULL);
        IPin_Release (input_pin);

        if (hres != S_OK) {
          GST_CAT_ERROR (dshowaudiosrc_debug,
              "Can't connect capture filter with fakesink filter (error=%d)",
              hres);
          goto error;
        }

        spec->segsize = spec->rate * spec->channels;
        spec->segtotal = 1;
      }
    }
  }

  return TRUE;

error:
  if (srcinterface) {
    IGstDshowInterface_Release (srcinterface);
  }

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
    hres = IFilterGraph_Disconnect (src->filter_graph, output_pin);
    IPin_Release (output_pin);
  }

  gst_dshow_get_pin_from_filter (src->dshow_fakesink, PINDIR_INPUT, &input_pin);
  if (input_pin) {
    hres = IFilterGraph_Disconnect (src->filter_graph, input_pin);
    IPin_Release (input_pin);
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
  IFilterGraph_RemoveFilter (src->filter_graph, src->audio_cap_filter);
  IFilterGraph_RemoveFilter (src->filter_graph, src->dshow_fakesink);

  /*release our gstreamer dshow sink */
  IBaseFilter_Release (src->dshow_fakesink);
  src->dshow_fakesink = NULL;

  /*release media filter interface */
  IMediaFilter_Release (src->media_filter);
  src->media_filter = NULL;

  /*release the filter graph manager */
  IFilterGraph_Release (src->filter_graph);
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
        Sleep (100);
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
  g_byte_array_remove_range (src->gbarray, 0, src->gbarray->len);
  g_mutex_unlock (src->gbarray_lock);
}

static GstCaps *
gst_dshowaudiosrc_getcaps_from_streamcaps (GstDshowAudioSrc * src, IPin * pin,
    IAMStreamConfig * streamcaps)
{
  GstCaps *caps = NULL;
  HRESULT hres = S_OK;
  RPC_STATUS rpcstatus;
  int icount = 0;
  int isize = 0;
  AUDIO_STREAM_CONFIG_CAPS ascc;
  int i = 0;

  if (!streamcaps)
    return NULL;

  IAMStreamConfig_GetNumberOfCapabilities (streamcaps, &icount, &isize);

  if (isize != sizeof (ascc))
    return NULL;

  for (; i < icount; i++) {
    GstCapturePinMediaType *pin_mediatype = g_new0 (GstCapturePinMediaType, 1);

    IPin_AddRef (pin);
    pin_mediatype->capture_pin = pin;

    hres =
        IAMStreamConfig_GetStreamCaps (streamcaps, i, &pin_mediatype->mediatype,
        (BYTE *) & ascc);
    if (hres == S_OK && pin_mediatype->mediatype) {
      GstCaps *mediacaps = NULL;

      if (!caps)
        caps = gst_caps_new_empty ();

      if ((UuidCompare (&pin_mediatype->mediatype->subtype, &MEDIASUBTYPE_PCM,
                  &rpcstatus) == 0 && rpcstatus == RPC_S_OK)
          && (UuidCompare (&pin_mediatype->mediatype->formattype,
                  &FORMAT_WaveFormatEx, &rpcstatus) == 0
              && rpcstatus == RPC_S_OK)) {
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
gst_dshowaudiosrc_push_buffer (byte * buffer, long size, byte * src_object,
    UINT64 start, UINT64 stop)
{
  GstDshowAudioSrc *src = GST_DSHOWAUDIOSRC (src_object);

  if (!buffer || size == 0 || !src) {
    return FALSE;
  }

  g_mutex_lock (src->gbarray_lock);
  g_byte_array_prepend (src->gbarray, (guint8 *) buffer, size);
  g_mutex_unlock (src->gbarray_lock);

  return TRUE;
}
