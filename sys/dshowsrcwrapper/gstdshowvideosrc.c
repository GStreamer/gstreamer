/* GStreamer
 * Copyright (C)  2007 Sebastien Moutte <sebastien@moutte.net>
 *
 * gstdshowvideosrc.c: 
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

#include "gstdshowvideosrc.h"

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

static const GstElementDetails gst_dshowvideosrc_details =
GST_ELEMENT_DETAILS ("DirectShow video capture source",
    "Source/Video",
    "Receive data from a directshow video capture graph",
    "Sebastien Moutte <sebastien@moutte.net>");

GST_DEBUG_CATEGORY_STATIC (dshowvideosrc_debug);
#define GST_CAT_DEFAULT dshowvideosrc_debug

const GUID MEDIASUBTYPE_I420
    = { 0x30323449, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B,
    0x71}
};

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-raw-rgb,"
        "bpp = (int) 24,"
        "depth = (int) 24,"
        "width = (int) [ 1, MAX ],"
        "height = (int) [ 1, MAX ],"
        "framerate = (fraction) [ 0, MAX ];"
        "video/x-dv,"
        "systemstream = (boolean) FALSE,"
        "width = (int) [ 1, MAX ],"
        "height = (int) [ 1, MAX ],"
        "framerate = (fraction) [ 0, MAX ],"
        "format = (fourcc) dvsd;"
        "video/x-dv,"
        "systemstream = (boolean) TRUE;"
        "video/x-raw-yuv,"
        "width = (int) [ 1, MAX ],"
        "height = (int) [ 1, MAX ],"
        "framerate = (fraction) [ 0, MAX ]," "format = (fourcc) I420")
    );

static void gst_dshowvideosrc_init_interfaces (GType type);

GST_BOILERPLATE_FULL (GstDshowVideoSrc, gst_dshowvideosrc, GstPushSrc,
    GST_TYPE_PUSH_SRC, gst_dshowvideosrc_init_interfaces);

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_NAME
};

static void gst_dshowvideosrc_probe_interface_init (GstPropertyProbeInterface *
    iface);
static const GList *gst_dshowvideosrc_probe_get_properties (GstPropertyProbe *
    probe);
static GValueArray *gst_dshowvideosrc_probe_get_values (GstPropertyProbe *
    probe, guint prop_id, const GParamSpec * pspec);
static GValueArray *gst_dshowvideosrc_get_device_name_values (GstDshowVideoSrc *
    src);
static gboolean gst_dshowvideosrc_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec);
static void gst_dshowvideosrc_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec);


static void gst_dshowvideosrc_dispose (GObject * gobject);
static void gst_dshowvideosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_dshowvideosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static GstCaps *gst_dshowvideosrc_get_caps (GstBaseSrc * src);
static GstStateChangeReturn gst_dshowvideosrc_change_state (GstElement *
    element, GstStateChange transition);


static gboolean gst_dshowvideosrc_start (GstBaseSrc * bsrc);
static gboolean gst_dshowvideosrc_stop (GstBaseSrc * bsrc);
static gboolean gst_dshowvideosrc_unlock (GstBaseSrc * bsrc);
static gboolean gst_dshowvideosrc_unlock_stop (GstBaseSrc * bsrc);
static gboolean gst_dshowvideosrc_set_caps (GstBaseSrc * bsrc, GstCaps * caps);
static GstCaps *gst_dshowvideosrc_get_caps (GstBaseSrc * bsrc);
static GstFlowReturn gst_dshowvideosrc_create (GstPushSrc * psrc,
    GstBuffer ** buf);

/*utils*/
static GstCaps *gst_dshowvideosrc_getcaps_from_streamcaps (GstDshowVideoSrc *
    src, IPin * pin, IAMStreamConfig * streamcaps);
static gboolean gst_dshowvideosrc_push_buffer (byte * buffer, long size,
    byte * src_object, UINT64 start, UINT64 stop);

static void
gst_dshowvideosrc_init_interfaces (GType type)
{
  static const GInterfaceInfo dshowvideosrc_info = {
    (GInterfaceInitFunc) gst_dshowvideosrc_probe_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type,
      GST_TYPE_PROPERTY_PROBE, &dshowvideosrc_info);
}

static void
gst_dshowvideosrc_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_dshowvideosrc_probe_get_properties;
  iface->needs_probe = gst_dshowvideosrc_probe_needs_probe;
  iface->probe_property = gst_dshowvideosrc_probe_probe_property;
  iface->get_values = gst_dshowvideosrc_probe_get_values;
}

static void
gst_dshowvideosrc_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));

  gst_element_class_set_details (element_class, &gst_dshowvideosrc_details);
}

static void
gst_dshowvideosrc_class_init (GstDshowVideoSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_dshowvideosrc_dispose);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_dshowvideosrc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_dshowvideosrc_get_property);

  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_dshowvideosrc_change_state);

  gstbasesrc_class->get_caps = GST_DEBUG_FUNCPTR (gst_dshowvideosrc_get_caps);
  gstbasesrc_class->set_caps = GST_DEBUG_FUNCPTR (gst_dshowvideosrc_set_caps);
  gstbasesrc_class->start = GST_DEBUG_FUNCPTR (gst_dshowvideosrc_start);
  gstbasesrc_class->stop = GST_DEBUG_FUNCPTR (gst_dshowvideosrc_stop);
  gstbasesrc_class->unlock = GST_DEBUG_FUNCPTR (gst_dshowvideosrc_unlock);
  gstbasesrc_class->unlock_stop =
      GST_DEBUG_FUNCPTR (gst_dshowvideosrc_unlock_stop);

  gstpushsrc_class->create = GST_DEBUG_FUNCPTR (gst_dshowvideosrc_create);

  g_object_class_install_property
      (gobject_class, PROP_DEVICE,
      g_param_spec_string ("device", "Device",
          "Directshow device path (@..classID/name)", NULL, G_PARAM_READWRITE));

  g_object_class_install_property
      (gobject_class, PROP_DEVICE_NAME,
      g_param_spec_string ("device_name", "Device name",
          "Human-readable name of the sound device", NULL, G_PARAM_READWRITE));

  GST_DEBUG_CATEGORY_INIT (dshowvideosrc_debug, "dshowvideosrc", 0,
      "Directshow video source");

}

static void
gst_dshowvideosrc_init (GstDshowVideoSrc * src, GstDshowVideoSrcClass * klass)
{
  src->device = NULL;
  src->device_name = NULL;
  src->video_cap_filter = NULL;
  src->dshow_fakesink = NULL;
  src->media_filter = NULL;
  src->filter_graph = NULL;
  src->caps = NULL;
  src->pins_mediatypes = NULL;
  src->is_rgb = FALSE;

  src->buffer_cond = g_cond_new ();
  src->buffer_mutex = g_mutex_new ();
  src->buffer = NULL;
  src->stop_requested = FALSE;

  CoInitializeEx (NULL, COINIT_MULTITHREADED);

  gst_base_src_set_live (GST_BASE_SRC (src), TRUE);
}

static void
gst_dshowvideosrc_dispose (GObject * gobject)
{
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (gobject);

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

  /* clean dshow */
  if (src->video_cap_filter) {
    IBaseFilter_Release (src->video_cap_filter);
    src->video_cap_filter = NULL;
  }

  if (src->buffer_mutex) {
    g_mutex_free (src->buffer_mutex);
    src->buffer_mutex = NULL;
  }

  if (src->buffer_cond) {
    g_cond_free (src->buffer_cond);
    src->buffer_cond = NULL;
  }

  if (src->buffer) {
    gst_buffer_unref (src->buffer);
    src->buffer = NULL;
  }

  CoUninitialize ();

  G_OBJECT_CLASS (parent_class)->dispose (gobject);
}

static gboolean
gst_dshowvideosrc_probe_needs_probe (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  static gboolean init = FALSE;
  gboolean ret = FALSE;

  if (!init) {
    ret = TRUE;
    init = TRUE;
  }

  return ret;
}

static void
gst_dshowvideosrc_probe_probe_property (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);

  switch (prop_id) {
    case PROP_DEVICE_NAME:
      //gst_v4l_class_probe_devices (klass, FALSE);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }
}

static const GList *
gst_dshowvideosrc_probe_get_properties (GstPropertyProbe * probe)
{
  GObjectClass *klass = G_OBJECT_GET_CLASS (probe);
  static GList *props = NULL;

  if (!props) {
    GParamSpec *pspec;

    pspec = g_object_class_find_property (klass, "device_name");
    props = g_list_append (props, pspec);
  }

  return props;
}

static GValueArray *
gst_dshowvideosrc_get_device_name_values (GstDshowVideoSrc * src)
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
    GST_CAT_ERROR (dshowvideosrc_debug,
        "Can't create an instance of the system device enumerator (error=%d)",
        hres);
    array = NULL;
    goto clean;
  }

  hres =
      ICreateDevEnum_CreateClassEnumerator (devices_enum,
      &CLSID_VideoInputDeviceCategory, &moniker_enum, 0);
  if (hres != S_OK || !moniker_enum) {
    GST_CAT_ERROR (dshowvideosrc_debug,
        "Can't get enumeration of video devices (error=%d)", hres);
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
gst_dshowvideosrc_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (probe);
  GValueArray *array = NULL;

  switch (prop_id) {
    case PROP_DEVICE_NAME:
      array = gst_dshowvideosrc_get_device_name_values (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
      break;
  }

  return array;
}

static void
gst_dshowvideosrc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (object);

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
gst_dshowvideosrc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{

}

static GstCaps *
gst_dshowvideosrc_get_caps (GstBaseSrc * basesrc)
{
  HRESULT hres = S_OK;
  IBindCtx *lpbc = NULL;
  IMoniker *videom;
  DWORD dwEaten;
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (basesrc);
  gunichar2 *unidevice = NULL;

  if (src->device) {
    g_free (src->device);
    src->device = NULL;
  }

  src->device =
      gst_dshow_getdevice_from_devicename (&CLSID_VideoInputDeviceCategory,
      &src->device_name);
  if (!src->device) {
    GST_CAT_ERROR (dshowvideosrc_debug, "No video device found.");
    return NULL;
  }
  unidevice =
      g_utf8_to_utf16 (src->device, strlen (src->device), NULL, NULL, NULL);

  if (!src->video_cap_filter) {
    hres = CreateBindCtx (0, &lpbc);
    if (SUCCEEDED (hres)) {
      hres = MkParseDisplayName (lpbc, unidevice, &dwEaten, &videom);
      if (SUCCEEDED (hres)) {
        hres =
            IMoniker_BindToObject (videom, lpbc, NULL, &IID_IBaseFilter,
            &src->video_cap_filter);
        IMoniker_Release (videom);
      }
      IBindCtx_Release (lpbc);
    }
  }

  if (!src->caps) {
    src->caps = gst_caps_new_empty ();
  }

  if (src->video_cap_filter && gst_caps_is_empty (src->caps)) {
    /* get the capture pins supported types */
    IPin *capture_pin = NULL;
    IEnumPins *enumpins = NULL;
    HRESULT hres;

    hres = IBaseFilter_EnumPins (src->video_cap_filter, &enumpins);
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
              GstCaps *caps =
                  gst_dshowvideosrc_getcaps_from_streamcaps (src, capture_pin,
                  streamcaps);

              if (caps) {
                gst_caps_append (src->caps, caps);
              }
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
    GST_CAT_LOG (dshowvideosrc_debug, "getcaps returned %s",
        gst_caps_to_string (src->caps));
    return gst_caps_ref (src->caps);
  }

  return NULL;
}

static GstStateChangeReturn
gst_dshowvideosrc_change_state (GstElement * element, GstStateChange transition)
{
  HRESULT hres = S_FALSE;
  IAMVfwCaptureDialogs *dialog = NULL;
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      if (src->media_filter)
        hres = IMediaFilter_Run (src->media_filter, 0);
      if (hres != S_OK) {
        GST_CAT_ERROR (dshowvideosrc_debug,
            "Can't RUN the directshow capture graph (error=%d)", hres);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      if (src->media_filter)
        hres = IMediaFilter_Stop (src->media_filter);
      if (hres != S_OK) {
        GST_CAT_ERROR (dshowvideosrc_debug,
            "Can't STOP the directshow capture graph (error=%d)", hres);
        return GST_STATE_CHANGE_FAILURE;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static gboolean
gst_dshowvideosrc_start (GstBaseSrc * bsrc)
{
  HRESULT hres = S_FALSE;
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (bsrc);

  hres = CoCreateInstance (&CLSID_FilterGraph, NULL, CLSCTX_INPROC,
      &IID_IFilterGraph, (LPVOID *) & src->filter_graph);
  if (hres != S_OK || !src->filter_graph) {
    GST_CAT_ERROR (dshowvideosrc_debug,
        "Can't create an instance of the dshow graph manager (error=%d)", hres);
    goto error;
  }

  hres = IFilterGraph_QueryInterface (src->filter_graph, &IID_IMediaFilter,
      (void **) &src->media_filter);
  if (hres != S_OK || !src->media_filter) {
    GST_CAT_ERROR (dshowvideosrc_debug,
        "Can't get IMediacontrol interface from the graph manager (error=%d)",
        hres);
    goto error;
  }

  hres = CoCreateInstance (&CLSID_DshowFakeSink, NULL, CLSCTX_INPROC,
      &IID_IBaseFilter, (LPVOID *) & src->dshow_fakesink);
  if (hres != S_OK || !src->dshow_fakesink) {
    GST_CAT_ERROR (dshowvideosrc_debug,
        "Can't create an instance of our dshow fakesink filter (error=%d)",
        hres);
    goto error;
  }

  hres =
      IFilterGraph_AddFilter (src->filter_graph, src->video_cap_filter,
      L"capture");
  if (hres != S_OK) {
    GST_CAT_ERROR (dshowvideosrc_debug,
        "Can't add video capture filter to the graph (error=%d)", hres);
    goto error;
  }

  hres =
      IFilterGraph_AddFilter (src->filter_graph, src->dshow_fakesink, L"sink");
  if (hres != S_OK) {
    GST_CAT_ERROR (dshowvideosrc_debug,
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
gst_dshowvideosrc_set_caps (GstBaseSrc * bsrc, GstCaps * caps)
{
  HRESULT hres;
  IGstDshowInterface *srcinterface = NULL;
  IPin *input_pin = NULL;
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (bsrc);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  /* search the negociated caps in our caps list to get its index and the corresponding mediatype */
  if (gst_caps_is_subset (caps, src->caps)) {
    guint i = 0;
    gint res = -1;

    for (; i < gst_caps_get_size (src->caps) && res == -1; i++) {
      GstCaps *capstmp = gst_caps_copy_nth (src->caps, i);

      if (gst_caps_is_subset (caps, capstmp)) {
        res = i;
      }
      gst_caps_unref (capstmp);
    }

    if (res != -1 && src->pins_mediatypes) {
      /* get the corresponding media type and build the dshow graph */
      GstCapturePinMediaType *pin_mediatype = NULL;
      gchar *caps_string = NULL;
      GList *type = g_list_nth (src->pins_mediatypes, res);

      if (type) {
        pin_mediatype = (GstCapturePinMediaType *) type->data;

        hres =
            IBaseFilter_QueryInterface (src->dshow_fakesink,
            &IID_IGstDshowInterface, (void **) &srcinterface);

        if (hres != S_OK || !srcinterface) {
          GST_CAT_ERROR (dshowvideosrc_debug,
              "Can't get IGstDshowInterface interface from our dshow fakesink filter (error=%d)",
              hres);
          goto error;
        }

        IGstDshowInterface_gst_set_media_type (srcinterface,
            pin_mediatype->mediatype);
        IGstDshowInterface_gst_set_buffer_callback (srcinterface,
            (byte *) gst_dshowvideosrc_push_buffer, (byte *) src);

        if (srcinterface) {
          IGstDshowInterface_Release (srcinterface);
        }

        gst_dshow_get_pin_from_filter (src->dshow_fakesink, PINDIR_INPUT,
            &input_pin);
        if (!input_pin) {
          GST_CAT_ERROR (dshowvideosrc_debug,
              "Can't get input pin from our dshow fakesink");
          goto error;
        }

        hres =
            IFilterGraph_ConnectDirect (src->filter_graph,
            pin_mediatype->capture_pin, input_pin, NULL);
        IPin_Release (input_pin);

        if (hres != S_OK) {
          GST_CAT_ERROR (dshowvideosrc_debug,
              "Can't connect capture filter with fakesink filter (error=%d)",
              hres);
          goto error;
        }

        /* save width and height negociated */
        gst_structure_get_int (s, "width", &src->width);
        gst_structure_get_int (s, "height", &src->height);

        src->is_rgb = FALSE;
        caps_string = gst_caps_to_string (caps);
        if (caps_string) {
          if (strstr (caps_string, "video/x-raw-rgb")) {
            src->is_rgb = TRUE;
          } else {
            src->is_rgb = FALSE;
          }
          g_free (caps_string);
        }
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
gst_dshowvideosrc_stop (GstBaseSrc * bsrc)
{
  IPin *input_pin = NULL, *output_pin = NULL;
  HRESULT hres = S_FALSE;
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (bsrc);

  if (!src->filter_graph)
    return TRUE;

  /* disconnect filters */
  gst_dshow_get_pin_from_filter (src->video_cap_filter, PINDIR_OUTPUT,
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

  /*remove filters from the graph */
  IFilterGraph_RemoveFilter (src->filter_graph, src->video_cap_filter);
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

static gboolean
gst_dshowvideosrc_unlock (GstBaseSrc * bsrc)
{
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (bsrc);

  g_mutex_lock (src->buffer_mutex);
  src->stop_requested = TRUE;
  g_cond_signal (src->buffer_cond);
  g_mutex_unlock (src->buffer_mutex);

  return TRUE;
}

static gboolean
gst_dshowvideosrc_unlock_stop (GstBaseSrc * bsrc)
{
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (bsrc);

  src->stop_requested = FALSE;

  return TRUE;
}

static GstFlowReturn
gst_dshowvideosrc_create (GstPushSrc * psrc, GstBuffer ** buf)
{
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (psrc);

  g_mutex_lock (src->buffer_mutex);
  while (src->buffer == NULL && !src->stop_requested)
    g_cond_wait (src->buffer_cond, src->buffer_mutex);
  *buf = src->buffer;
  src->buffer = NULL;
  g_mutex_unlock (src->buffer_mutex);

  if (src->stop_requested) {
    if (*buf != NULL) {
      gst_buffer_unref (*buf);
      *buf = NULL;
    }
    return GST_FLOW_WRONG_STATE;
  }

  GST_CAT_DEBUG (dshowvideosrc_debug,
      "dshowvideosrc_create => pts %" GST_TIME_FORMAT " duration %"
      GST_TIME_FORMAT, GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (*buf)),
      GST_TIME_ARGS (GST_BUFFER_DURATION (*buf)));

  return GST_FLOW_OK;
}

static GstCaps *
gst_dshowvideosrc_getcaps_from_streamcaps (GstDshowVideoSrc * src, IPin * pin,
    IAMStreamConfig * streamcaps)
{
  GstCaps *caps = NULL;
  HRESULT hres = S_OK;
  RPC_STATUS rpcstatus;
  int icount = 0;
  int isize = 0;
  VIDEO_STREAM_CONFIG_CAPS vscc;
  int i = 0;

  if (!streamcaps)
    return NULL;

  IAMStreamConfig_GetNumberOfCapabilities (streamcaps, &icount, &isize);

  if (isize != sizeof (vscc))
    return NULL;

  for (; i < icount; i++) {
    GstCapturePinMediaType *pin_mediatype = g_new0 (GstCapturePinMediaType, 1);

    IPin_AddRef (pin);
    pin_mediatype->capture_pin = pin;

    hres =
        IAMStreamConfig_GetStreamCaps (streamcaps, i, &pin_mediatype->mediatype,
        (BYTE *) & vscc);
    if (hres == S_OK && pin_mediatype->mediatype) {
      VIDEOINFOHEADER *video_info;
      GstCaps *mediacaps = NULL;

      if (!caps)
        caps = gst_caps_new_empty ();

      /* I420 */
      if ((UuidCompare (&pin_mediatype->mediatype->subtype, &MEDIASUBTYPE_I420,
                  &rpcstatus) == 0 && rpcstatus == RPC_S_OK)
          && (UuidCompare (&pin_mediatype->mediatype->formattype,
                  &FORMAT_VideoInfo, &rpcstatus) == 0
              && rpcstatus == RPC_S_OK)) {
        video_info = (VIDEOINFOHEADER *) pin_mediatype->mediatype->pbFormat;

        mediacaps = gst_caps_new_simple ("video/x-raw-yuv",
            "width", G_TYPE_INT, video_info->bmiHeader.biWidth,
            "height", G_TYPE_INT, video_info->bmiHeader.biHeight,
            "framerate", GST_TYPE_FRACTION,
            (int) (10000000 / video_info->AvgTimePerFrame), 1, "format",
            GST_TYPE_FOURCC, MAKEFOURCC ('I', '4', '2', '0'), NULL);

        if (mediacaps) {
          src->pins_mediatypes =
              g_list_append (src->pins_mediatypes, pin_mediatype);
          gst_caps_append (caps, mediacaps);
        } else {
          gst_dshow_free_pin_mediatype (pin_mediatype);
        }
        continue;
      }

      /* RGB24 */
      if ((UuidCompare (&pin_mediatype->mediatype->subtype, &MEDIASUBTYPE_RGB24,
                  &rpcstatus) == 0 && rpcstatus == RPC_S_OK)
          && (UuidCompare (&pin_mediatype->mediatype->formattype,
                  &FORMAT_VideoInfo, &rpcstatus) == 0
              && rpcstatus == RPC_S_OK)) {
        video_info = (VIDEOINFOHEADER *) pin_mediatype->mediatype->pbFormat;

        /* ffmpegcolorspace handles RGB24 in BIG_ENDIAN */
        mediacaps = gst_caps_new_simple ("video/x-raw-rgb",
            "bpp", G_TYPE_INT, 24,
            "depth", G_TYPE_INT, 24,
            "width", G_TYPE_INT, video_info->bmiHeader.biWidth,
            "height", G_TYPE_INT, video_info->bmiHeader.biHeight,
            "framerate", GST_TYPE_FRACTION,
            (int) (10000000 / video_info->AvgTimePerFrame), 1, "endianness",
            G_TYPE_INT, G_BIG_ENDIAN, "red_mask", G_TYPE_INT, 255, "green_mask",
            G_TYPE_INT, 65280, "blue_mask", G_TYPE_INT, 16711680, NULL);

        if (mediacaps) {
          src->pins_mediatypes =
              g_list_append (src->pins_mediatypes, pin_mediatype);
          gst_caps_append (caps, mediacaps);
        } else {
          gst_dshow_free_pin_mediatype (pin_mediatype);
        }
        continue;
      }

      /* DVSD */
      if ((UuidCompare (&pin_mediatype->mediatype->subtype, &MEDIASUBTYPE_dvsd,
                  &rpcstatus) == 0 && rpcstatus == RPC_S_OK)
          && (UuidCompare (&pin_mediatype->mediatype->formattype,
                  &FORMAT_VideoInfo, &rpcstatus) == 0
              && rpcstatus == RPC_S_OK)) {
        video_info = (VIDEOINFOHEADER *) pin_mediatype->mediatype->pbFormat;

        mediacaps = gst_caps_new_simple ("video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, FALSE,
            "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('d', 'v', 's', 'd'),
            "framerate", GST_TYPE_FRACTION,
            (int) (10000000 / video_info->AvgTimePerFrame), 1, "width",
            G_TYPE_INT, video_info->bmiHeader.biWidth, "height", G_TYPE_INT,
            video_info->bmiHeader.biHeight, NULL);

        if (mediacaps) {
          src->pins_mediatypes =
              g_list_append (src->pins_mediatypes, pin_mediatype);
          gst_caps_append (caps, mediacaps);
        } else {
          gst_dshow_free_pin_mediatype (pin_mediatype);
        }
        continue;
      }

      /* DV stream */
      if ((UuidCompare (&pin_mediatype->mediatype->subtype, &MEDIASUBTYPE_dvsd,
                  &rpcstatus) == 0 && rpcstatus == RPC_S_OK)
          && (UuidCompare (&pin_mediatype->mediatype->formattype,
                  &FORMAT_DvInfo, &rpcstatus) == 0 && rpcstatus == RPC_S_OK)) {

        mediacaps = gst_caps_new_simple ("video/x-dv",
            "systemstream", G_TYPE_BOOLEAN, TRUE, NULL);

        if (mediacaps) {
          src->pins_mediatypes =
              g_list_append (src->pins_mediatypes, pin_mediatype);
          gst_caps_append (caps, mediacaps);
        } else {
          gst_dshow_free_pin_mediatype (pin_mediatype);
        }
        continue;
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
gst_dshowvideosrc_push_buffer (byte * buffer, long size, byte * src_object,
    UINT64 start, UINT64 stop)
{
  GstDshowVideoSrc *src = GST_DSHOWVIDEOSRC (src_object);
  GstBuffer *buf;
  IPin *pPin = NULL;
  HRESULT hres = S_FALSE;
  AM_MEDIA_TYPE *pMediaType = NULL;

  if (!buffer || size == 0 || !src) {
    return FALSE;
  }

  /* create a new buffer assign to it the clock time as timestamp */
  buf = gst_buffer_new_and_alloc (size);

  GST_BUFFER_SIZE (buf) = size;
  GST_BUFFER_TIMESTAMP (buf) = gst_clock_get_time (GST_ELEMENT (src)->clock);
  GST_BUFFER_TIMESTAMP (buf) -= GST_ELEMENT (src)->base_time;
  GST_BUFFER_DURATION (buf) = stop - start;

  if (src->is_rgb) {
    /* FOR RGB directshow decoder will return bottom-up BITMAP 
     * There is probably a way to get top-bottom video frames from
     * the decoder...
     */
    gint line = 0;
    gint stride = size / src->height;

    for (; line < src->height; line++) {
      memcpy (GST_BUFFER_DATA (buf) + (line * stride),
          buffer + (size - ((line + 1) * (stride))), stride);
    }
  } else {
    memcpy (GST_BUFFER_DATA (buf), buffer, size);
  }

  GST_CAT_DEBUG (dshowvideosrc_debug,
      "push_buffer => pts %" GST_TIME_FORMAT "duration %" GST_TIME_FORMAT,
      GST_TIME_ARGS (GST_BUFFER_TIMESTAMP (buf)), GST_TIME_ARGS (stop - start));

  /* the negotiate() method already set caps on the source pad */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (src)));

  g_mutex_lock (src->buffer_mutex);
  if (src->buffer != NULL)
    gst_buffer_unref (src->buffer);
  src->buffer = buf;
  g_cond_signal (src->buffer_cond);
  g_mutex_unlock (src->buffer_mutex);

  return TRUE;
}
