/* GStreamer android.hardware.Camera Source
 *
 * Copyright (C) 2012, Cisco Systems, Inc.
 *   Author: Youness Alaoui <youness.alaoui@collabora.co.uk>
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
#  include "config.h"
#endif

#define GST_USE_UNSTABLE_API
#include <gst/video/video.h>
#include <gst/interfaces/propertyprobe.h>
#include <gst/interfaces/photography.h>

#include "gstahcsrc.h"
#include "gst-dvm.h"

/* GObject */
static void gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_ahc_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_ahc_src_dispose (GObject * object);

/* GstElement */
static GstStateChangeReturn gst_ahc_src_change_state (GstElement * element,
    GstStateChange transition);

/* GstBaseSrc */
static GstCaps *gst_ahc_src_getcaps (GstBaseSrc * src);
static gboolean gst_ahc_src_setcaps (GstBaseSrc * src, GstCaps * caps);
static void gst_ahc_src_fixate (GstBaseSrc * basesrc, GstCaps * caps);
static gboolean gst_ahc_src_start (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_stop (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock (GstBaseSrc * bsrc);
static gboolean gst_ahc_src_unlock_stop (GstBaseSrc * bsrc);
static GstFlowReturn gst_ahc_src_create (GstPushSrc * src, GstBuffer ** buffer);
static gboolean gst_ahc_src_query (GstBaseSrc * bsrc, GstQuery * query);

/* GstPropertyProbe */
static void gst_ahc_src_implements_interface_init (GstImplementsInterfaceClass
    * klass);
static void gst_ahc_src_property_probe_interface_init (GstPropertyProbeInterface
    * iface);
static void gst_ahc_src_photography_interface_init (GstPhotographyInterface
    * iface);
static void gst_ahc_src_init_interfaces (GType type);

/* GstPhotography  */
static gboolean gst_ahc_src_get_ev_compensation (GstPhotography * photo,
    gfloat * ev_comp);
static gboolean _white_balance_to_enum (const gchar * white_balance,
    GstWhiteBalanceMode * mode);
static gboolean gst_ahc_src_get_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode * wb_mode);
static gboolean _color_effects_to_enum (const gchar * color_effect,
    GstColourToneMode * mode);
static gboolean gst_ahc_src_get_colour_tone_mode (GstPhotography * photo,
    GstColourToneMode * tone_mode);
static gboolean _scene_modes_to_enum (const gchar * scene, GstSceneMode * mode);
static gboolean gst_ahc_src_get_scene_mode (GstPhotography * photo,
    GstSceneMode * scene_mode);
static gboolean _flash_modes_to_enum (const gchar * flash, GstFlashMode * mode);
static gboolean gst_ahc_src_get_flash_mode (GstPhotography * photo,
    GstFlashMode * flash_mode);
static gboolean gst_ahc_src_get_zoom (GstPhotography * photo, gfloat * zoom);
static gboolean _antibanding_to_enum (const gchar * antibanding,
    GstFlickerReductionMode * mode);
static gboolean gst_ahc_src_get_flicker_mode (GstPhotography * photo,
    GstFlickerReductionMode * flicker_mode);
static gboolean _focus_modes_to_enum (const gchar * focus, GstFocusMode * mode);
static gboolean gst_ahc_src_get_focus_mode (GstPhotography * photo,
    GstFocusMode * focus_mode);

static gboolean gst_ahc_src_set_ev_compensation (GstPhotography * photo,
    gfloat ev_comp);
static gboolean gst_ahc_src_set_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode wb_mode);
static gboolean gst_ahc_src_set_colour_tone_mode (GstPhotography * photo,
    GstColourToneMode tone_mode);
static gboolean gst_ahc_src_set_scene_mode (GstPhotography * photo,
    GstSceneMode scene_mode);
static gboolean gst_ahc_src_set_flash_mode (GstPhotography * photo,
    GstFlashMode flash_mode);
static gboolean gst_ahc_src_set_zoom (GstPhotography * photo, gfloat zoom);
static gboolean gst_ahc_src_set_flicker_mode (GstPhotography * photo,
    GstFlickerReductionMode flicker_mode);
static gboolean gst_ahc_src_set_focus_mode (GstPhotography * photo,
    GstFocusMode focus_mode);

static GstPhotoCaps gst_ahc_src_get_capabilities (GstPhotography * photo);
static void gst_ahc_src_set_autofocus (GstPhotography * photo, gboolean on);

/* GstAHCSrc */
static void gst_ahc_src_close (GstAHCSrc * self);
static void gst_ahc_src_on_preview_frame (jbyteArray data, gpointer user_data);
static void gst_ahc_src_on_error (gint error, gpointer user_data);
static void gst_ahc_src_on_auto_focus (gboolean success, gpointer user_data);

#define NUM_CALLBACK_BUFFERS 5

#define GST_AHC_SRC_CAPS_STR                                    \
  GST_VIDEO_CAPS_YUV (" { YV12 , YUY2 , NV21 , NV16 }") ";"     \
  GST_VIDEO_CAPS_RGB_16

static GstStaticPadTemplate gst_ahc_src_pad_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_AHC_SRC_CAPS_STR));

GST_DEBUG_CATEGORY_STATIC (gst_ahc_src_debug);
#define GST_CAT_DEFAULT gst_ahc_src_debug

enum
{
  PROP_0,
  PROP_DEVICE,
  PROP_DEVICE_FACING,
  PROP_DEVICE_ORIENTATION,
  PROP_WB_MODE,
  PROP_COLOUR_TONE,
  PROP_SCENE_MODE,
  PROP_FLASH_MODE,
  PROP_NOISE_REDUCTION,
  PROP_CAPABILITIES,
  PROP_EV_COMP,
  PROP_ISO_SPEED,
  PROP_APERTURE,
  PROP_EXPOSURE,
  PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
  PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
  PROP_FLICKER_MODE,
  PROP_FOCUS_MODE,
  PROP_ZOOM,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

#define DEFAULT_DEVICE "0"

GST_BOILERPLATE_FULL (GstAHCSrc, gst_ahc_src, GstPushSrc, GST_TYPE_PUSH_SRC,
    gst_ahc_src_init_interfaces);

#define CAMERA_FACING_BACK 0
#define CAMERA_FACING_FRONT 1

static GType
gst_ahc_src_facing_get_type (void)
{
  static GType type = 0;
  static const GEnumValue types[] = {
    {CAMERA_FACING_BACK, "Back", "back"},
    {CAMERA_FACING_FRONT, "Front", "front"},
    {0, NULL, NULL}
  };

  if (!type) {
    type = g_enum_register_static ("GstAHCSrcFacing", types);
  }
  return type;
}

#define GST_AHC_SRC_FACING_TYPE (gst_ahc_src_facing_get_type())

static void
gst_ahc_src_init_interfaces (GType type)
{
  static const GInterfaceInfo ahcsrc_propertyprobe_info = {
    (GInterfaceInitFunc) gst_ahc_src_property_probe_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo ahcsrc_photography_info = {
    (GInterfaceInitFunc) gst_ahc_src_photography_interface_init,
    NULL,
    NULL,
  };
  static const GInterfaceInfo ahcsrc_implements_interface_info = {
    (GInterfaceInitFunc) gst_ahc_src_implements_interface_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_PROPERTY_PROBE,
      &ahcsrc_propertyprobe_info);
  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &ahcsrc_implements_interface_info);
  g_type_add_interface_static (type, GST_TYPE_PHOTOGRAPHY,
      &ahcsrc_photography_info);
}

static void
gst_ahc_src_base_init (gpointer g_class)
{
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (g_class);

  GST_DEBUG_CATEGORY_INIT (gst_ahc_src_debug, "ahcsrc", 0,
      "android.hardware.Camera source element");

  gst_element_class_add_static_pad_template (gstelement_class,
      &gst_ahc_src_pad_template);
  gst_element_class_set_details_simple (gstelement_class,
      "Android Camera Source",
      "Source/Video",
      "Reads frames from android.hardware.Camera class into buffers",
      "Youness Alaoui <youness.alaoui@collabora.co.uk>");
}

static void
gst_ahc_src_class_init (GstAHCSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);

  gobject_class->set_property = gst_ahc_src_set_property;
  gobject_class->get_property = gst_ahc_src_get_property;
  gobject_class->dispose = gst_ahc_src_dispose;

  element_class->change_state = gst_ahc_src_change_state;

  gstbasesrc_class->get_caps = gst_ahc_src_getcaps;
  gstbasesrc_class->set_caps = gst_ahc_src_setcaps;
  gstbasesrc_class->fixate = gst_ahc_src_fixate;
  gstbasesrc_class->start = gst_ahc_src_start;
  gstbasesrc_class->stop = gst_ahc_src_stop;
  gstbasesrc_class->unlock = gst_ahc_src_unlock;
  gstbasesrc_class->unlock_stop = gst_ahc_src_unlock_stop;
  gstbasesrc_class->query = gst_ahc_src_query;

  gstpushsrc_class->create = gst_ahc_src_create;

  properties[PROP_DEVICE] = g_param_spec_string ("device",
      "device", "Device ID", DEFAULT_DEVICE,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_DEVICE,
      properties[PROP_DEVICE]);

  properties[PROP_DEVICE_ORIENTATION] = g_param_spec_int ("device-orientation",
      "Device orientation", "The orientation of the camera image",
      0, 360, 0, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_DEVICE_ORIENTATION,
      properties[PROP_DEVICE_ORIENTATION]);

  properties[PROP_DEVICE_FACING] = g_param_spec_enum ("device-facing",
      "Device facing", "The direction that the camera faces",
      GST_AHC_SRC_FACING_TYPE, CAMERA_FACING_BACK,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (gobject_class, PROP_DEVICE_FACING,
      properties[PROP_DEVICE_FACING]);

  /* Override GstPhotography properties */
  g_object_class_override_property (gobject_class, PROP_WB_MODE,
      GST_PHOTOGRAPHY_PROP_WB_MODE);
  properties[PROP_WB_MODE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_WB_MODE);

  g_object_class_override_property (gobject_class, PROP_COLOUR_TONE,
      GST_PHOTOGRAPHY_PROP_COLOUR_TONE);
  properties[PROP_COLOUR_TONE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_COLOUR_TONE);

  g_object_class_override_property (gobject_class, PROP_SCENE_MODE,
      GST_PHOTOGRAPHY_PROP_SCENE_MODE);
  properties[PROP_SCENE_MODE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_SCENE_MODE);

  g_object_class_override_property (gobject_class, PROP_FLASH_MODE,
      GST_PHOTOGRAPHY_PROP_FLASH_MODE);
  properties[PROP_FLASH_MODE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_FLASH_MODE);

  g_object_class_override_property (gobject_class, PROP_NOISE_REDUCTION,
      GST_PHOTOGRAPHY_PROP_NOISE_REDUCTION);
  properties[PROP_NOISE_REDUCTION] =
      g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_NOISE_REDUCTION);

  g_object_class_override_property (gobject_class, PROP_CAPABILITIES,
      GST_PHOTOGRAPHY_PROP_CAPABILITIES);
  properties[PROP_CAPABILITIES] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_CAPABILITIES);

  g_object_class_override_property (gobject_class, PROP_EV_COMP,
      GST_PHOTOGRAPHY_PROP_EV_COMP);
  properties[PROP_EV_COMP] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_EV_COMP);

  g_object_class_override_property (gobject_class, PROP_ISO_SPEED,
      GST_PHOTOGRAPHY_PROP_ISO_SPEED);
  properties[PROP_ISO_SPEED] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_ISO_SPEED);

  g_object_class_override_property (gobject_class, PROP_APERTURE,
      GST_PHOTOGRAPHY_PROP_APERTURE);
  properties[PROP_APERTURE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_APERTURE);

  g_object_class_override_property (gobject_class, PROP_EXPOSURE,
      GST_PHOTOGRAPHY_PROP_EXPOSURE);
  properties[PROP_EXPOSURE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_EXPOSURE);

  g_object_class_override_property (gobject_class,
      PROP_IMAGE_CAPTURE_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_CAPTURE_SUPPORTED_CAPS);
  properties[PROP_IMAGE_CAPTURE_SUPPORTED_CAPS] =
      g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_IMAGE_CAPTURE_SUPPORTED_CAPS);

  g_object_class_override_property (gobject_class,
      PROP_IMAGE_PREVIEW_SUPPORTED_CAPS,
      GST_PHOTOGRAPHY_PROP_IMAGE_PREVIEW_SUPPORTED_CAPS);
  properties[PROP_IMAGE_PREVIEW_SUPPORTED_CAPS] =
      g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_IMAGE_PREVIEW_SUPPORTED_CAPS);

  g_object_class_override_property (gobject_class, PROP_FLICKER_MODE,
      GST_PHOTOGRAPHY_PROP_FLICKER_MODE);
  properties[PROP_FLICKER_MODE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_FLICKER_MODE);

  g_object_class_override_property (gobject_class, PROP_FOCUS_MODE,
      GST_PHOTOGRAPHY_PROP_FOCUS_MODE);
  properties[PROP_FOCUS_MODE] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_FOCUS_MODE);

  g_object_class_override_property (gobject_class, PROP_ZOOM,
      GST_PHOTOGRAPHY_PROP_ZOOM);
  properties[PROP_ZOOM] = g_object_class_find_property (gobject_class,
      GST_PHOTOGRAPHY_PROP_ZOOM);

  klass->probe_properties = NULL;
}

static gboolean
_data_queue_check_full (GstDataQueue * queue, guint visible,
    guint bytes, guint64 time, gpointer checkdata)
{
  return FALSE;
}

static void
gst_ahc_src_init (GstAHCSrc * self, GstAHCSrcClass * klass)
{
  gst_base_src_set_live (GST_BASE_SRC (self), TRUE);
  gst_base_src_set_format (GST_BASE_SRC (self), GST_FORMAT_TIME);
  gst_base_src_set_do_timestamp (GST_BASE_SRC (self), FALSE);

  self->camera = NULL;
  self->texture = NULL;
  self->data = NULL;
  self->queue = gst_data_queue_new (_data_queue_check_full, NULL);
  self->start = FALSE;
  self->previous_ts = GST_CLOCK_TIME_NONE;
}

static void
gst_ahc_src_dispose (GObject * object)
{
  GstAHCSrc *self = GST_AHC_SRC (object);

  if (self->queue)
    g_object_unref (self->queue);
  self->queue = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_ahc_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (object);
  (void) self;

  switch (prop_id) {
    case PROP_DEVICE:{
      const gchar *dev = g_value_get_string (value);
      gchar *endptr = NULL;
      guint64 device;

      device = g_ascii_strtoll (dev, &endptr, 10);
      if (endptr != dev && endptr[0] == 0 && device < G_MAXINT)
        self->device = (gint) device;
    }
      break;
    case PROP_WB_MODE:{
      GstWhiteBalanceMode wb = g_value_get_enum (value);

      gst_ahc_src_set_white_balance_mode (GST_PHOTOGRAPHY (self), wb);
    }
      break;
    case PROP_COLOUR_TONE:{
      GstColourToneMode tone = g_value_get_enum (value);

      gst_ahc_src_set_colour_tone_mode (GST_PHOTOGRAPHY (self), tone);
    }
      break;
    case PROP_SCENE_MODE:{
      GstSceneMode scene = g_value_get_enum (value);

      gst_ahc_src_set_scene_mode (GST_PHOTOGRAPHY (self), scene);
    }
      break;
    case PROP_FLASH_MODE:{
      GstFlashMode flash = g_value_get_enum (value);

      gst_ahc_src_set_flash_mode (GST_PHOTOGRAPHY (self), flash);
    }
      break;
    case PROP_EV_COMP:{
      gfloat ev = g_value_get_float (value);

      gst_ahc_src_set_ev_compensation (GST_PHOTOGRAPHY (self), ev);
    }
      break;
    case PROP_FLICKER_MODE:{
      GstFlickerReductionMode flicker = g_value_get_enum (value);

      gst_ahc_src_set_flicker_mode (GST_PHOTOGRAPHY (self), flicker);
    }
      break;
    case PROP_FOCUS_MODE:{
      GstFocusMode focus = g_value_get_enum (value);

      gst_ahc_src_set_focus_mode (GST_PHOTOGRAPHY (self), focus);
    }
      break;
    case PROP_ZOOM:{
      gfloat zoom = g_value_get_float (value);

      gst_ahc_src_set_zoom (GST_PHOTOGRAPHY (self), zoom);
    }
      break;
    case PROP_NOISE_REDUCTION:
    case PROP_ISO_SPEED:
    case PROP_APERTURE:
    case PROP_EXPOSURE:
    case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
    case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_ahc_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (object);
  (void) self;

  switch (prop_id) {
    case PROP_DEVICE:{
      gchar *dev = g_strdup_printf ("%d", self->device);

      g_value_take_string (value, dev);
    }
      break;
    case PROP_DEVICE_FACING:{
      GstAHCCameraInfo info;

      if (gst_ah_camera_get_camera_info (self->device, &info))
        g_value_set_enum (value, info.facing == CameraInfo_CAMERA_FACING_BACK ?
            CAMERA_FACING_BACK : CAMERA_FACING_FRONT);
      else
        g_value_set_enum (value, CAMERA_FACING_BACK);
    }
      break;
    case PROP_DEVICE_ORIENTATION:{
      GstAHCCameraInfo info;

      if (gst_ah_camera_get_camera_info (self->device, &info))
        g_value_set_int (value, info.orientation);
      else
        g_value_set_int (value, 0);
    }
      break;
    case PROP_WB_MODE:{
      GstWhiteBalanceMode wb;

      if (gst_ahc_src_get_white_balance_mode (GST_PHOTOGRAPHY (self), &wb))
        g_value_set_enum (value, wb);
    }
      break;
    case PROP_COLOUR_TONE:{
      GstColourToneMode tone;

      if (gst_ahc_src_get_colour_tone_mode (GST_PHOTOGRAPHY (self), &tone))
        g_value_set_enum (value, tone);
    }
      break;
    case PROP_SCENE_MODE:{
      GstSceneMode scene;

      if (gst_ahc_src_get_scene_mode (GST_PHOTOGRAPHY (self), &scene))
        g_value_set_enum (value, scene);
    }
      break;
    case PROP_FLASH_MODE:{
      GstFlashMode flash;

      if (gst_ahc_src_get_flash_mode (GST_PHOTOGRAPHY (self), &flash))
        g_value_set_enum (value, flash);
    }
      break;
    case PROP_CAPABILITIES:{
      GstPhotoCaps caps;

      caps = gst_ahc_src_get_capabilities (GST_PHOTOGRAPHY (self));
      g_value_set_ulong (value, caps);
    }
      break;
    case PROP_EV_COMP:{
      gfloat ev;

      if (gst_ahc_src_get_ev_compensation (GST_PHOTOGRAPHY (self), &ev))
        g_value_set_float (value, ev);
    }
      break;
    case PROP_FLICKER_MODE:{
      GstFlickerReductionMode flicker;

      if (gst_ahc_src_get_flicker_mode (GST_PHOTOGRAPHY (self), &flicker))
        g_value_set_enum (value, flicker);
    }
      break;
    case PROP_FOCUS_MODE:{
      GstFocusMode focus;

      if (gst_ahc_src_get_focus_mode (GST_PHOTOGRAPHY (self), &focus))
        g_value_set_enum (value, focus);
    }
      break;
    case PROP_ZOOM:{
      gfloat zoom;

      if (gst_ahc_src_get_zoom (GST_PHOTOGRAPHY (self), &zoom))
        g_value_set_float (value, zoom);
    }
      break;
    case PROP_IMAGE_CAPTURE_SUPPORTED_CAPS:
    case PROP_IMAGE_PREVIEW_SUPPORTED_CAPS:
    case PROP_NOISE_REDUCTION:
    case PROP_ISO_SPEED:
    case PROP_APERTURE:
    case PROP_EXPOSURE:
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_ahc_src_interface_supported (GstImplementsInterface * iface,
    GType iface_type)
{
  if (iface_type == GST_TYPE_PHOTOGRAPHY ||
      iface_type == GST_TYPE_PROPERTY_PROBE)
    return TRUE;
  else
    return FALSE;
}

static void
gst_ahc_src_implements_interface_init (GstImplementsInterfaceClass * klass)
{
  klass->supported = gst_ahc_src_interface_supported;
}

static const GList *
gst_ahc_src_probe_get_properties (GstPropertyProbe * probe)
{
  GstAHCSrcClass *ahc_class = GST_AHC_SRC_CLASS (probe);
  GList **list = &ahc_class->probe_properties;

  if (!*list) {
    *list = g_list_append (*list, properties[PROP_DEVICE]);
    *list = g_list_append (*list, properties[PROP_EV_COMP]);
    *list = g_list_append (*list, properties[PROP_ZOOM]);
  }

  return *list;
}

static GValueArray *
gst_ahc_src_probe_get_values (GstPropertyProbe * probe,
    guint prop_id, const GParamSpec * pspec)
{
  GstAHCSrc *self = GST_AHC_SRC (probe);
  GValueArray *array = NULL;

  /* g_object_class_find_property returns overriden property with
   * param_id == 0, so we can't switch/case the prop_id and
   * we need to check the pspec instead */
  if (pspec == properties[PROP_DEVICE]) {
    GValue value = { 0 };
    gint num_cams = gst_ah_camera_get_number_of_cameras ();
    gint i;

    array = g_value_array_new (num_cams);
    g_value_init (&value, G_TYPE_STRING);
    for (i = 0; i < num_cams; i++) {
      g_value_take_string (&value, g_strdup_printf ("%d", i));
      g_value_array_append (array, &value);
    }
    g_value_unset (&value);
  } else if (pspec == properties[PROP_EV_COMP]) {
    if (self->camera) {
      GstAHCParameters *params;

      params = gst_ah_camera_get_parameters (self->camera);
      if (params) {
        gint min, max;
        gfloat step;

        min = gst_ahc_parameters_get_min_exposure_compensation (params);
        max = gst_ahc_parameters_get_max_exposure_compensation (params);
        step = gst_ahc_parameters_get_exposure_compensation_step (params);

        if (step != 0.0 && min != max) {
          GValue value = { 0 };
          gint i;

          /* Min and Max are inclusive */
          array = g_value_array_new (max - min + 1);
          g_value_init (&value, G_TYPE_FLOAT);
          for (i = min; i <= max; i++) {
            g_value_set_float (&value, step * i);
            g_value_array_append (array, &value);
          }
          g_value_unset (&value);
        }

        gst_ahc_parameters_free (params);
      }
    }
  } else if (pspec == properties[PROP_ZOOM]) {
    if (self->camera) {
      GstAHCParameters *params;

      params = gst_ah_camera_get_parameters (self->camera);
      if (params) {
        GList *zoom_ratios = gst_ahc_parameters_get_zoom_ratios (params);
        gint max_zoom = gst_ahc_parameters_get_max_zoom (params);

        if (zoom_ratios && g_list_length (zoom_ratios) == (max_zoom + 1)) {
          GValue value = { 0 };
          GList *i;

          array = g_value_array_new (max_zoom + 1);
          g_value_init (&value, G_TYPE_FLOAT);
          for (i = zoom_ratios; i; i = i->next) {
            gint zoom_value = GPOINTER_TO_INT (i->data);
            gfloat zoom = (gfloat) zoom_value / 100.0;

            g_value_set_float (&value, zoom);
            g_value_array_append (array, &value);
          }
          g_value_unset (&value);
        }

        gst_ahc_parameters_zoom_ratios_free (zoom_ratios);
        gst_ahc_parameters_free (params);
      }
    }
  } else {
    G_OBJECT_WARN_INVALID_PROPERTY_ID (probe, prop_id, pspec);
  }

  return array;
}

static void
gst_ahc_src_property_probe_interface_init (GstPropertyProbeInterface * iface)
{
  iface->get_properties = gst_ahc_src_probe_get_properties;
  iface->get_values = gst_ahc_src_probe_get_values;
}


static gboolean
_antibanding_to_enum (const gchar * antibanding, GstFlickerReductionMode * mode)
{
  if (antibanding == Parameters_ANTIBANDING_AUTO)
    *mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_AUTO;
  else if (antibanding == Parameters_ANTIBANDING_50HZ)
    *mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_50HZ;
  else if (antibanding == Parameters_ANTIBANDING_60HZ)
    *mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_60HZ;
  else if (antibanding == Parameters_ANTIBANDING_OFF)
    *mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_OFF;
  else
    return FALSE;

  return TRUE;
}

static gboolean
_white_balance_to_enum (const gchar * white_balance, GstWhiteBalanceMode * mode)
{
  if (white_balance == Parameters_WHITE_BALANCE_AUTO)
    *mode = GST_PHOTOGRAPHY_WB_MODE_AUTO;
  else if (white_balance == Parameters_WHITE_BALANCE_INCANDESCENT)
    *mode = GST_PHOTOGRAPHY_WB_MODE_TUNGSTEN;
  else if (white_balance == Parameters_WHITE_BALANCE_FLUORESCENT)
    *mode = GST_PHOTOGRAPHY_WB_MODE_FLUORESCENT;
  else if (white_balance == Parameters_WHITE_BALANCE_WARM_FLUORESCENT)
    *mode = GST_PHOTOGRAPHY_WB_MODE_WARM_FLUORESCENT;
  else if (white_balance == Parameters_WHITE_BALANCE_DAYLIGHT)
    *mode = GST_PHOTOGRAPHY_WB_MODE_DAYLIGHT;
  else if (white_balance == Parameters_WHITE_BALANCE_CLOUDY_DAYLIGHT)
    *mode = GST_PHOTOGRAPHY_WB_MODE_CLOUDY;
  else if (white_balance == Parameters_WHITE_BALANCE_TWILIGHT)
    *mode = GST_PHOTOGRAPHY_WB_MODE_SUNSET;
  else if (white_balance == Parameters_WHITE_BALANCE_SHADE)
    *mode = GST_PHOTOGRAPHY_WB_MODE_SHADE;
  else
    return FALSE;

  return TRUE;
}

static gboolean
_color_effects_to_enum (const gchar * color_effect, GstColourToneMode * mode)
{
  if (color_effect == Parameters_EFFECT_NONE)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NORMAL;
  else if (color_effect == Parameters_EFFECT_MONO)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_GRAYSCALE;
  else if (color_effect == Parameters_EFFECT_NEGATIVE)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NEGATIVE;
  else if (color_effect == Parameters_EFFECT_SOLARIZE)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SOLARIZE;
  else if (color_effect == Parameters_EFFECT_SEPIA)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SEPIA;
  else if (color_effect == Parameters_EFFECT_POSTERIZE)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_POSTERIZE;
  else if (color_effect == Parameters_EFFECT_WHITEBOARD)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_WHITEBOARD;
  else if (color_effect == Parameters_EFFECT_BLACKBOARD)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_BLACKBOARD;
  else if (color_effect == Parameters_EFFECT_AQUA)
    *mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_AQUA;
  else
    return FALSE;

  return TRUE;
}

static gboolean
_scene_modes_to_enum (const gchar * scene, GstSceneMode * mode)
{
  if (scene == Parameters_SCENE_MODE_AUTO)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_AUTO;
  else if (scene == Parameters_SCENE_MODE_ACTION)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_ACTION;
  else if (scene == Parameters_SCENE_MODE_PORTRAIT)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_PORTRAIT;
  else if (scene == Parameters_SCENE_MODE_LANDSCAPE)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_LANDSCAPE;
  else if (scene == Parameters_SCENE_MODE_NIGHT)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_NIGHT;
  else if (scene == Parameters_SCENE_MODE_NIGHT_PORTRAIT)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_NIGHT_PORTRAIT;
  else if (scene == Parameters_SCENE_MODE_THEATRE)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_THEATRE;
  else if (scene == Parameters_SCENE_MODE_BEACH)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_BEACH;
  else if (scene == Parameters_SCENE_MODE_SNOW)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_SNOW;
  else if (scene == Parameters_SCENE_MODE_SUNSET)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_SUNSET;
  else if (scene == Parameters_SCENE_MODE_STEADYPHOTO)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_STEADY_PHOTO;
  else if (scene == Parameters_SCENE_MODE_FIREWORKS)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_FIREWORKS;
  else if (scene == Parameters_SCENE_MODE_SPORTS)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_SPORT;
  else if (scene == Parameters_SCENE_MODE_PARTY)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_PARTY;
  else if (scene == Parameters_SCENE_MODE_CANDLELIGHT)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_CANDLELIGHT;
  else if (scene == Parameters_SCENE_MODE_BARCODE)
    *mode = GST_PHOTOGRAPHY_SCENE_MODE_BARCODE;
  else
    return FALSE;

  return TRUE;
}

static gboolean
_flash_modes_to_enum (const gchar * flash, GstFlashMode * mode)
{
  if (flash == Parameters_FLASH_MODE_OFF)
    *mode = GST_PHOTOGRAPHY_FLASH_MODE_OFF;
  else if (flash == Parameters_FLASH_MODE_AUTO)
    *mode = GST_PHOTOGRAPHY_FLASH_MODE_AUTO;
  else if (flash == Parameters_FLASH_MODE_ON)
    *mode = GST_PHOTOGRAPHY_FLASH_MODE_ON;
  else if (flash == Parameters_FLASH_MODE_RED_EYE)
    *mode = GST_PHOTOGRAPHY_FLASH_MODE_RED_EYE;
  else if (flash == Parameters_FLASH_MODE_TORCH)
    *mode = GST_PHOTOGRAPHY_FLASH_MODE_FILL_IN;
  else
    return FALSE;

  return TRUE;
}

static gboolean
_focus_modes_to_enum (const gchar * focus, GstFocusMode * mode)
{
  if (focus == Parameters_FOCUS_MODE_AUTO)
    *mode = GST_PHOTOGRAPHY_FOCUS_MODE_AUTO;
  else if (focus == Parameters_FOCUS_MODE_INFINITY)
    *mode = GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY;
  else if (focus == Parameters_FOCUS_MODE_MACRO)
    *mode = GST_PHOTOGRAPHY_FOCUS_MODE_MACRO;
  else if (focus == Parameters_FOCUS_MODE_FIXED)
    *mode = GST_PHOTOGRAPHY_FOCUS_MODE_HYPERFOCAL;
  else if (focus == Parameters_FOCUS_MODE_EDOF)
    *mode = GST_PHOTOGRAPHY_FOCUS_MODE_EXTENDED;
  else if (focus == Parameters_FOCUS_MODE_CONTINUOUS_VIDEO)
    *mode = GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_EXTENDED;
  else if (focus == Parameters_FOCUS_MODE_CONTINUOUS_PICTURE)
    *mode = GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL;
  else
    return FALSE;

  return TRUE;
}

static gboolean
gst_ahc_src_get_ev_compensation (GstPhotography * photo, gfloat * ev_comp)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      gint ev, min, max;
      gfloat step;

      ev = gst_ahc_parameters_get_exposure_compensation (params);
      min = gst_ahc_parameters_get_min_exposure_compensation (params);
      max = gst_ahc_parameters_get_max_exposure_compensation (params);
      step = gst_ahc_parameters_get_exposure_compensation_step (params);

      if (step != 0.0 && min != max && min <= ev && ev <= max) {
        if (ev_comp)
          *ev_comp = ev * step;
        ret = TRUE;
      }
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_get_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode * wb_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *wb = gst_ahc_parameters_get_white_balance (params);
      GstWhiteBalanceMode mode = GST_PHOTOGRAPHY_WB_MODE_AUTO;

      if (_white_balance_to_enum (wb, &mode)) {
        ret = TRUE;

        if (wb_mode)
          *wb_mode = mode;
      }

      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_get_colour_tone_mode (GstPhotography * photo,
    GstColourToneMode * tone_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *effect = gst_ahc_parameters_get_color_effect (params);
      GstColourToneMode mode = GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NORMAL;

      if (_color_effects_to_enum (effect, &mode)) {
        ret = TRUE;

        if (tone_mode)
          *tone_mode = mode;
      }

      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_get_scene_mode (GstPhotography * photo, GstSceneMode * scene_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (scene_mode && self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *scene = gst_ahc_parameters_get_scene_mode (params);
      GstSceneMode mode = GST_PHOTOGRAPHY_SCENE_MODE_AUTO;

      if (_scene_modes_to_enum (scene, &mode)) {
        ret = TRUE;

        if (scene_mode)
          *scene_mode = mode;
      }

      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_get_flash_mode (GstPhotography * photo, GstFlashMode * flash_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *flash = gst_ahc_parameters_get_flash_mode (params);
      GstFlashMode mode = GST_PHOTOGRAPHY_FLASH_MODE_OFF;

      if (_flash_modes_to_enum (flash, &mode)) {
        ret = TRUE;

        if (flash_mode)
          *flash_mode = mode;
      }

      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_get_zoom (GstPhotography * photo, gfloat * zoom)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      GList *zoom_ratios = gst_ahc_parameters_get_zoom_ratios (params);
      gint zoom_idx = gst_ahc_parameters_get_zoom (params);
      gint max_zoom = gst_ahc_parameters_get_max_zoom (params);

      if (zoom_ratios && g_list_length (zoom_ratios) == (max_zoom + 1) &&
          zoom_idx >= 0 && zoom_idx < max_zoom) {
        gint zoom_value;

        zoom_value = GPOINTER_TO_INT (g_list_nth_data (zoom_ratios, zoom_idx));
        if (zoom)
          *zoom = (gfloat) zoom_value / 100.0;

        ret = TRUE;
      }

      gst_ahc_parameters_zoom_ratios_free (zoom_ratios);
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_get_flicker_mode (GstPhotography * photo,
    GstFlickerReductionMode * flicker_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *antibanding = gst_ahc_parameters_get_antibanding (params);
      GstFlickerReductionMode mode = GST_PHOTOGRAPHY_FLICKER_REDUCTION_AUTO;

      if (_antibanding_to_enum (antibanding, &mode)) {
        ret = TRUE;

        if (flicker_mode)
          *flicker_mode = mode;
      }

      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_get_focus_mode (GstPhotography * photo, GstFocusMode * focus_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *focus = gst_ahc_parameters_get_focus_mode (params);
      GstFocusMode mode = GST_PHOTOGRAPHY_FOCUS_MODE_AUTO;

      if (_focus_modes_to_enum (focus, &mode)) {
        ret = TRUE;

        if (focus_mode)
          *focus_mode = mode;
      }

      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}


static gboolean
gst_ahc_src_set_ev_compensation (GstPhotography * photo, gfloat ev_comp)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      gint ev, min, max;
      gfloat step;

      ev = gst_ahc_parameters_get_exposure_compensation (params);
      min = gst_ahc_parameters_get_min_exposure_compensation (params);
      max = gst_ahc_parameters_get_max_exposure_compensation (params);
      step = gst_ahc_parameters_get_exposure_compensation_step (params);
      if (step != 0.0 && min != max &&
          (min * step) <= ev_comp && ev_comp <= (max * step)) {
        ev = ev_comp / step;
        if ((ev * step) == ev_comp) {
          gst_ahc_parameters_set_exposure_compensation (params, ev);
          ret = gst_ah_camera_set_parameters (self->camera, params);
        }
      }
    }
    gst_ahc_parameters_free (params);
  }

  return ret;
}

static gboolean
gst_ahc_src_set_white_balance_mode (GstPhotography * photo,
    GstWhiteBalanceMode wb_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *white_balance = NULL;

      switch (wb_mode) {
        case GST_PHOTOGRAPHY_WB_MODE_AUTO:
          white_balance = Parameters_WHITE_BALANCE_AUTO;
          break;
        case GST_PHOTOGRAPHY_WB_MODE_DAYLIGHT:
          white_balance = Parameters_WHITE_BALANCE_DAYLIGHT;
          break;
        case GST_PHOTOGRAPHY_WB_MODE_CLOUDY:
          white_balance = Parameters_WHITE_BALANCE_CLOUDY_DAYLIGHT;
          break;
        case GST_PHOTOGRAPHY_WB_MODE_SUNSET:
          white_balance = Parameters_WHITE_BALANCE_TWILIGHT;
          break;
        case GST_PHOTOGRAPHY_WB_MODE_TUNGSTEN:
          white_balance = Parameters_WHITE_BALANCE_INCANDESCENT;
          break;
        case GST_PHOTOGRAPHY_WB_MODE_FLUORESCENT:
          white_balance = Parameters_WHITE_BALANCE_FLUORESCENT;
          break;
        case GST_PHOTOGRAPHY_WB_MODE_WARM_FLUORESCENT:
          white_balance = Parameters_WHITE_BALANCE_WARM_FLUORESCENT;
          break;
        case GST_PHOTOGRAPHY_WB_MODE_SHADE:
          white_balance = Parameters_WHITE_BALANCE_SHADE;
          break;
        default:
          white_balance = NULL;
          break;
      }

      if (white_balance) {
        gst_ahc_parameters_set_white_balance (params, white_balance);
        ret = gst_ah_camera_set_parameters (self->camera, params);
      }
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_set_colour_tone_mode (GstPhotography * photo,
    GstColourToneMode tone_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *color_effect = NULL;

      switch (tone_mode) {
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NORMAL:
          color_effect = Parameters_EFFECT_NONE;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SEPIA:
          color_effect = Parameters_EFFECT_SEPIA;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NEGATIVE:
          color_effect = Parameters_EFFECT_NEGATIVE;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_GRAYSCALE:
          color_effect = Parameters_EFFECT_MONO;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SOLARIZE:
          color_effect = Parameters_EFFECT_SOLARIZE;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_POSTERIZE:
          color_effect = Parameters_EFFECT_POSTERIZE;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_WHITEBOARD:
          color_effect = Parameters_EFFECT_WHITEBOARD;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_BLACKBOARD:
          color_effect = Parameters_EFFECT_BLACKBOARD;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_AQUA:
          color_effect = Parameters_EFFECT_AQUA;
          break;
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NATURAL:
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_VIVID:
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_COLORSWAP:
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_OUT_OF_FOCUS:
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SKY_BLUE:
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_GRASS_GREEN:
        case GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SKIN_WHITEN:
        default:
          color_effect = NULL;
          break;
      }

      if (color_effect) {
        gst_ahc_parameters_set_color_effect (params, color_effect);
        ret = gst_ah_camera_set_parameters (self->camera, params);
      }
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_set_scene_mode (GstPhotography * photo, GstSceneMode scene_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *scene = NULL;

      switch (scene_mode) {
        case GST_PHOTOGRAPHY_SCENE_MODE_PORTRAIT:
          scene = Parameters_SCENE_MODE_PORTRAIT;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_LANDSCAPE:
          scene = Parameters_SCENE_MODE_LANDSCAPE;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_SPORT:
          scene = Parameters_SCENE_MODE_SPORTS;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_NIGHT:
          scene = Parameters_SCENE_MODE_NIGHT;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_AUTO:
          scene = Parameters_SCENE_MODE_AUTO;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_ACTION:
          scene = Parameters_SCENE_MODE_ACTION;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_NIGHT_PORTRAIT:
          scene = Parameters_SCENE_MODE_NIGHT_PORTRAIT;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_THEATRE:
          scene = Parameters_SCENE_MODE_THEATRE;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_BEACH:
          scene = Parameters_SCENE_MODE_BEACH;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_SNOW:
          scene = Parameters_SCENE_MODE_SNOW;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_SUNSET:
          scene = Parameters_SCENE_MODE_SUNSET;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_STEADY_PHOTO:
          scene = Parameters_SCENE_MODE_STEADYPHOTO;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_FIREWORKS:
          scene = Parameters_SCENE_MODE_FIREWORKS;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_PARTY:
          scene = Parameters_SCENE_MODE_PARTY;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_CANDLELIGHT:
          scene = Parameters_SCENE_MODE_CANDLELIGHT;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_BARCODE:
          scene = Parameters_SCENE_MODE_BARCODE;
          break;
        case GST_PHOTOGRAPHY_SCENE_MODE_MANUAL:
        case GST_PHOTOGRAPHY_SCENE_MODE_CLOSEUP:
        default:
          scene = NULL;
          break;
      }

      if (scene) {
        gst_ahc_parameters_set_scene_mode (params, scene);
        ret = gst_ah_camera_set_parameters (self->camera, params);
      }
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_set_flash_mode (GstPhotography * photo, GstFlashMode flash_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *flash = NULL;

      switch (flash_mode) {
        case GST_PHOTOGRAPHY_FLASH_MODE_AUTO:
          flash = Parameters_FLASH_MODE_AUTO;
          break;
        case GST_PHOTOGRAPHY_FLASH_MODE_OFF:
          flash = Parameters_FLASH_MODE_OFF;
          break;
        case GST_PHOTOGRAPHY_FLASH_MODE_ON:
          flash = Parameters_FLASH_MODE_ON;
          break;
        case GST_PHOTOGRAPHY_FLASH_MODE_FILL_IN:
          flash = Parameters_FLASH_MODE_TORCH;
          break;
        case GST_PHOTOGRAPHY_FLASH_MODE_RED_EYE:
          flash = Parameters_FLASH_MODE_RED_EYE;
          break;
        default:
          flash = NULL;
          break;
      }

      if (flash) {
        gst_ahc_parameters_set_flash_mode (params, flash);
        ret = gst_ah_camera_set_parameters (self->camera, params);
      }
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_set_zoom (GstPhotography * photo, gfloat zoom)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      GList *zoom_ratios = gst_ahc_parameters_get_zoom_ratios (params);
      gint max_zoom = gst_ahc_parameters_get_max_zoom (params);
      gint zoom_idx = -1;

      if (zoom_ratios && g_list_length (zoom_ratios) == (max_zoom + 1)) {
        gint i;
        gint value = zoom * 100;

        for (i = 0; i < max_zoom + 1; i++) {
          gint zoom_value = GPOINTER_TO_INT (g_list_nth_data (zoom_ratios, i));

          if (value == zoom_value)
            zoom_idx = i;
        }
      }

      if (zoom_idx != -1) {
        gst_ahc_parameters_set_zoom (params, zoom_idx);
        ret = gst_ah_camera_set_parameters (self->camera, params);
      }

      gst_ahc_parameters_zoom_ratios_free (zoom_ratios);
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_set_flicker_mode (GstPhotography * photo,
    GstFlickerReductionMode flicker_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *antibanding = NULL;

      switch (flicker_mode) {
        case GST_PHOTOGRAPHY_FLICKER_REDUCTION_OFF:
          antibanding = Parameters_ANTIBANDING_OFF;
          break;
        case GST_PHOTOGRAPHY_FLICKER_REDUCTION_50HZ:
          antibanding = Parameters_ANTIBANDING_50HZ;
          break;
        case GST_PHOTOGRAPHY_FLICKER_REDUCTION_60HZ:
          antibanding = Parameters_ANTIBANDING_60HZ;
          break;
        case GST_PHOTOGRAPHY_FLICKER_REDUCTION_AUTO:
          antibanding = Parameters_ANTIBANDING_AUTO;
          break;
        default:
          antibanding = NULL;
          break;
      }

      if (antibanding) {
        gst_ahc_parameters_set_antibanding (params, antibanding);
        ret = gst_ah_camera_set_parameters (self->camera, params);
      }
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static gboolean
gst_ahc_src_set_focus_mode (GstPhotography * photo, GstFocusMode focus_mode)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);
  gboolean ret = FALSE;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      const gchar *focus = NULL;

      switch (focus_mode) {
        case GST_PHOTOGRAPHY_FOCUS_MODE_AUTO:
          focus = Parameters_FOCUS_MODE_AUTO;
          break;
        case GST_PHOTOGRAPHY_FOCUS_MODE_MACRO:
          focus = Parameters_FOCUS_MODE_MACRO;
          break;
        case GST_PHOTOGRAPHY_FOCUS_MODE_INFINITY:
          focus = Parameters_FOCUS_MODE_INFINITY;
          break;
        case GST_PHOTOGRAPHY_FOCUS_MODE_HYPERFOCAL:
          focus = Parameters_FOCUS_MODE_FIXED;
          break;
        case GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_NORMAL:
          focus = Parameters_FOCUS_MODE_CONTINUOUS_PICTURE;
          break;
        case GST_PHOTOGRAPHY_FOCUS_MODE_CONTINUOUS_EXTENDED:
          focus = Parameters_FOCUS_MODE_CONTINUOUS_VIDEO;
          break;
        case GST_PHOTOGRAPHY_FOCUS_MODE_EXTENDED:
          focus = Parameters_FOCUS_MODE_EDOF;
          break;
        case GST_PHOTOGRAPHY_FOCUS_MODE_PORTRAIT:
        default:
          focus = NULL;
          break;
      }

      if (focus) {
        gst_ahc_parameters_set_focus_mode (params, focus);
        ret = gst_ah_camera_set_parameters (self->camera, params);
      }
      gst_ahc_parameters_free (params);
    }
  }

  return ret;
}

static GstPhotoCaps
gst_ahc_src_get_capabilities (GstPhotography * photo)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);

  GstPhotoCaps caps = GST_PHOTOGRAPHY_CAPS_EV_COMP |
      GST_PHOTOGRAPHY_CAPS_WB_MODE | GST_PHOTOGRAPHY_CAPS_TONE |
      GST_PHOTOGRAPHY_CAPS_SCENE | GST_PHOTOGRAPHY_CAPS_FLASH |
      GST_PHOTOGRAPHY_CAPS_FOCUS | GST_PHOTOGRAPHY_CAPS_ZOOM;

  if (self->camera) {
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (!gst_ahc_parameters_is_zoom_supported (params))
      caps &= ~GST_PHOTOGRAPHY_CAPS_ZOOM;

    gst_ahc_parameters_free (params);
  }

  return caps;
}

static void
gst_ahc_src_on_auto_focus (gboolean success, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);

  GST_WARNING_OBJECT (self, "Auto focus completed : %d", success);
  gst_element_post_message (GST_ELEMENT (self),
      gst_message_new_custom (GST_MESSAGE_ELEMENT, GST_OBJECT (self),
          gst_structure_new (GST_PHOTOGRAPHY_AUTOFOCUS_DONE, NULL)));
}

static void
gst_ahc_src_set_autofocus (GstPhotography * photo, gboolean on)
{
  GstAHCSrc *self = GST_AHC_SRC (photo);

  if (self->camera) {
    if (on)
      gst_ah_camera_auto_focus (self->camera, gst_ahc_src_on_auto_focus, self);
    else
      gst_ah_camera_cancel_auto_focus (self->camera);
  }

}

static void
gst_ahc_src_photography_interface_init (GstPhotographyInterface * iface)
{
  iface->get_ev_compensation = gst_ahc_src_get_ev_compensation;
  iface->get_white_balance_mode = gst_ahc_src_get_white_balance_mode;
  iface->get_colour_tone_mode = gst_ahc_src_get_colour_tone_mode;
  iface->get_scene_mode = gst_ahc_src_get_scene_mode;
  iface->get_flash_mode = gst_ahc_src_get_flash_mode;
  iface->get_zoom = gst_ahc_src_get_zoom;
  iface->get_flicker_mode = gst_ahc_src_get_flicker_mode;
  iface->get_focus_mode = gst_ahc_src_get_focus_mode;

  iface->set_ev_compensation = gst_ahc_src_set_ev_compensation;
  iface->set_white_balance_mode = gst_ahc_src_set_white_balance_mode;
  iface->set_colour_tone_mode = gst_ahc_src_set_colour_tone_mode;
  iface->set_scene_mode = gst_ahc_src_set_scene_mode;
  iface->set_flash_mode = gst_ahc_src_set_flash_mode;
  iface->set_zoom = gst_ahc_src_set_zoom;
  iface->set_flicker_mode = gst_ahc_src_set_flicker_mode;
  iface->set_focus_mode = gst_ahc_src_set_focus_mode;

  iface->get_capabilities = gst_ahc_src_get_capabilities;
  iface->set_autofocus = gst_ahc_src_set_autofocus;
}

static gint
_compare_formats (int f1, int f2)
{
  if (f1 == f2)
    return 0;
  /* YV12 has priority */
  if (f1 == ImageFormat_YV12)
    return -1;
  if (f2 == ImageFormat_YV12)
    return 1;
  /* Then NV21 */
  if (f1 == ImageFormat_NV21)
    return -1;
  if (f2 == ImageFormat_NV21)
    return 1;
  /* Then we don't care */
  return f2 - f1;
}

static gint
_compare_sizes (GstAHCSize * s1, GstAHCSize * s2)
{
  return ((s2->width * s2->height) - (s1->width * s1->height));
}


static gint
_compare_ranges (int *r1, int *r2)
{
  if (r1[1] == r2[1])
    /* Smallest range */
    return (r1[1] - r1[0]) - (r2[1] - r2[0]);
  else
    /* Highest fps */
    return r2[1] - r1[1];
}

static GstCaps *
gst_ahc_src_getcaps (GstBaseSrc * src)
{
  GstAHCSrc *self = GST_AHC_SRC (src);

  if (self->camera) {
    GstCaps *ret = gst_caps_new_empty ();
    GstAHCParameters *params;

    params = gst_ah_camera_get_parameters (self->camera);
    if (params) {
      GList *formats, *sizes, *ranges;
      GList *i, *j, *k;
      int previous_format = ImageFormat_UNKNOWN;

      formats = gst_ahc_parameters_get_supported_preview_formats (params);
      formats = g_list_sort (formats, (GCompareFunc) _compare_formats);
      sizes = gst_ahc_parameters_get_supported_preview_sizes (params);
      sizes = g_list_sort (sizes, (GCompareFunc) _compare_sizes);
      ranges = gst_ahc_parameters_get_supported_preview_fps_range (params);
      ranges = g_list_sort (ranges, (GCompareFunc) _compare_ranges);
      GST_DEBUG_OBJECT (self, "Supported preview formats:");

      for (i = formats; i; i = i->next) {
        int f = GPOINTER_TO_INT (i->data);
        GstStructure *format = NULL;

        /* Ignore duplicates */
        if (f == previous_format)
          continue;

        /* Can't use switch/case because the values are not constants */
        if (f == ImageFormat_NV16) {
          GST_DEBUG_OBJECT (self, "    NV16 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '1', '6'),
              NULL);
        } else if (f == ImageFormat_NV21) {
          GST_DEBUG_OBJECT (self, "    NV21 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('N', 'V', '2', '1'),
              NULL);
        } else if (f == ImageFormat_RGB_565) {
          GST_DEBUG_OBJECT (self, "    RGB565 (%d)", f);
          format = gst_structure_new ("video/x-raw-rgb",
              "bpp", G_TYPE_INT, 16,
              "depth", G_TYPE_INT, 16,
              "red_mask", G_TYPE_INT, 0xf800,
              "green_mask", G_TYPE_INT, 0x07e0,
              "blue_mask", G_TYPE_INT, 0x001f,
              "endianness", G_TYPE_INT, G_LITTLE_ENDIAN, NULL);
        } else if (f == ImageFormat_YUY2) {
          GST_DEBUG_OBJECT (self, "    YUY2 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'U', 'Y', '2'),
              NULL);
        } else if (f == ImageFormat_YV12) {
          GST_DEBUG_OBJECT (self, "    YV12 (%d)", f);
          format = gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, GST_MAKE_FOURCC ('Y', 'V', '1', '2'),
              NULL);
        }
        previous_format = f;

        if (format) {
          for (j = sizes; j; j = j->next) {
            GstAHCSize *s = j->data;
            GstStructure *size;

            size = gst_structure_copy (format);
            gst_structure_set (size, "width", G_TYPE_INT, s->width,
                "height", G_TYPE_INT, s->height,
                "interlaced", G_TYPE_BOOLEAN, FALSE,
                "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL);

            for (k = ranges; k; k = k->next) {
              int *range = k->data;
              GstStructure *s;

              s = gst_structure_copy (size);
              if (range[0] == range[1]) {
                gst_structure_set (s, "framerate", GST_TYPE_FRACTION,
                    range[0], 1000, NULL);
              } else {
                gst_structure_set (s, "framerate", GST_TYPE_FRACTION_RANGE,
                    range[0], 1000, range[1], 1000, NULL);
              }
              gst_caps_append_structure (ret, s);
            }
            gst_structure_free (size);
          }
          gst_structure_free (format);
        }
      }
      GST_DEBUG_OBJECT (self, "Supported preview sizes:");
      for (i = sizes; i; i = i->next) {
        GstAHCSize *s = i->data;

        GST_DEBUG_OBJECT (self, "    %dx%d", s->width, s->height);
      }
      GST_DEBUG_OBJECT (self, "Supported preview fps range:");
      for (i = ranges; i; i = i->next) {
        int *range = i->data;

        GST_DEBUG_OBJECT (self, "    [%d, %d]", range[0], range[1]);
      }

      gst_ahc_parameters_supported_preview_formats_free (formats);
      gst_ahc_parameters_supported_preview_sizes_free (sizes);
      gst_ahc_parameters_supported_preview_fps_range_free (ranges);
      gst_ahc_parameters_free (params);
    }

    return ret;
  } else {
    return NULL;
  }
}

static void
gst_ahc_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstAHCSrc *self = GST_AHC_SRC (src);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (self, "Fixating : %" GST_PTR_FORMAT, caps);

  /* Width/height will be fixed already here, format will
   * be left for fixation by the default handler.
   * We only have to fixate framerate here, to the
   * highest possible framerate.
   */
  gst_structure_fixate_field_nearest_fraction (s, "framerate", G_MAXINT, 1);
}

static gboolean
gst_ahc_src_setcaps (GstBaseSrc * src, GstCaps * caps)
{
  GstAHCSrc *self = GST_AHC_SRC (src);
  gboolean ret = FALSE;
  GstAHCParameters *params = NULL;

  if (!self->camera) {
    GST_WARNING_OBJECT (self, "setcaps called without a camera available");
    goto end;
  }

  params = gst_ah_camera_get_parameters (self->camera);
  if (params) {
    GstVideoFormat format;
    gint fmt;
    gint width, height, fps_n, fps_d, buffer_size;
    GList *ranges, *l;
    gint range_size = G_MAXINT;

    if (!gst_video_format_parse_caps (caps, &format, &width, &height) ||
        !gst_video_parse_caps_framerate (caps, &fps_n, &fps_d)) {
      GST_WARNING_OBJECT (self, "unable to parse video caps");
      goto end;
    }
    fps_n *= 1000 / fps_d;

    /* Select the best range that contains our framerate.
     * We *must* set a range of those returned by the camera
     * according to the API docs and can't use a subset of any
     * of those ranges.
     * We chose the smallest range that contains the target
     * framerate.
     */
    self->fps_max = self->fps_min = 0;
    ranges = gst_ahc_parameters_get_supported_preview_fps_range (params);
    ranges = g_list_sort (ranges, (GCompareFunc) _compare_ranges);
    for (l = ranges; l; l = l->next) {
      int *range = l->data;

      if (fps_n >= range[0] && fps_n <= range[1] &&
          range_size > (range[1] - range[0])) {
        self->fps_min = range[0];
        self->fps_max = range[1];
        range_size = range[1] - range[0];
      }
    }
    gst_ahc_parameters_supported_preview_fps_range_free (ranges);
    if (self->fps_max == 0) {
      GST_ERROR_OBJECT (self, "Couldn't find an applicable FPS range");
      goto end;
    }

    switch (format) {
      case GST_VIDEO_FORMAT_YV12:
        fmt = ImageFormat_YV12;
        break;
      case GST_VIDEO_FORMAT_NV21:
        fmt = ImageFormat_NV21;
        break;
      case GST_VIDEO_FORMAT_YUY2:
        fmt = ImageFormat_YUY2;
        break;
      case GST_VIDEO_FORMAT_RGB16:
        fmt = ImageFormat_RGB_565;
        break;
        /* GST_VIDEO_FORMAT_NV16 doesn't exist */
        //case GST_VIDEO_FORMAT_NV16:
        //fmt = ImageFormat_NV16;
        //break;
      default:
        fmt = ImageFormat_UNKNOWN;
        break;
    }

    if (fmt == ImageFormat_UNKNOWN) {
      GST_WARNING_OBJECT (self, "unsupported video format");
      goto end;
    }

    gst_ahc_parameters_set_preview_size (params, width, height);
    gst_ahc_parameters_set_preview_format (params, fmt);
    gst_ahc_parameters_set_preview_fps_range (params, self->fps_min,
        self->fps_max);

    GST_DEBUG_OBJECT (self, "Setting camera parameters : %d %dx%d @ [%f, %f]",
        fmt, width, height, self->fps_min / 1000.0, self->fps_max / 1000.0);

    if (!gst_ah_camera_set_parameters (self->camera, params)) {
      GST_WARNING_OBJECT (self, "Unable to set video parameters");
      goto end;
    }

    self->width = width;
    self->height = height;
    self->format = fmt;
    buffer_size = width * height *
        ((double) gst_ag_imageformat_get_bits_per_pixel (fmt) / 8);
    if (buffer_size > self->buffer_size) {
      JNIEnv *env = gst_dvm_get_env ();
      gint i;

      for (i = 0; i < NUM_CALLBACK_BUFFERS; i++) {
        jbyteArray array = (*env)->NewByteArray (env, buffer_size);

        if (array) {
          gst_ah_camera_add_callback_buffer (self->camera, array);
          (*env)->DeleteLocalRef (env, array);
        }
      }
    }
    self->buffer_size = buffer_size;
    ret = TRUE;
  }

end:
  if (params)
    gst_ahc_parameters_free (params);

  if (ret && self->start) {
    GST_DEBUG_OBJECT (self, "Starting preview");
    ret = gst_ah_camera_start_preview (self->camera);
    if (ret) {
      /* Need to reset callbacks after every startPreview */
      gst_ah_camera_set_preview_callback_with_buffer (self->camera,
          gst_ahc_src_on_preview_frame, self);
      gst_ah_camera_set_error_callback (self->camera, gst_ahc_src_on_error,
          self);
      self->start = FALSE;
    }
  }
  return ret;
}

typedef struct
{
  GstAHCSrc *self;
  jbyteArray array;
  jbyte *data;
} FreeFuncBuffer;

static void
gst_ahc_src_buffer_free_func (gpointer priv)
{
  FreeFuncBuffer *data = (FreeFuncBuffer *) priv;
  GstAHCSrc *self = data->self;
  JNIEnv *env = gst_dvm_get_env ();

  (*env)->ReleaseByteArrayElements (env, data->array, data->data, JNI_ABORT);
  if (self->camera)
    gst_ah_camera_add_callback_buffer (self->camera, data->array);
  (*env)->DeleteGlobalRef (env, data->array);
  gst_object_unref (self);

  g_slice_free (FreeFuncBuffer, data);
}

static void
_data_queue_item_free (GstDataQueueItem * item)
{
  gst_buffer_unref (GST_BUFFER (item->object));
  g_slice_free (GstDataQueueItem, item);
}

static void
gst_ahc_src_on_preview_frame (jbyteArray array, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);
  JNIEnv *env = gst_dvm_get_env ();
  GstBuffer *buffer;
  GstDataQueueItem *item = NULL;
  FreeFuncBuffer *malloc_data = NULL;
  GstClockTime timestamp = GST_CLOCK_TIME_NONE;
  GstClockTime duration = 0;
  GstClock *clock;

  if (array == NULL) {
    GST_DEBUG_OBJECT (self, "Size of array in queue is too small, dropping it");
    return;
  }

  if ((clock = GST_ELEMENT_CLOCK (self))) {
    GstClockTime base_time = GST_ELEMENT_CAST (self)->base_time;
    GstClockTime current_ts;

    gst_object_ref (clock);
    current_ts = gst_clock_get_time (clock) - base_time;
    gst_object_unref (clock);
    if (GST_CLOCK_TIME_IS_VALID (self->previous_ts)) {
      timestamp = self->previous_ts;
      duration = current_ts - self->previous_ts;
      self->previous_ts = current_ts;
    } else {
      /* Drop the first buffer */
      self->previous_ts = current_ts;
      gst_ah_camera_add_callback_buffer (self->camera, array);
      return;
    }
  } else {
    gst_ah_camera_add_callback_buffer (self->camera, array);
    return;
  }
  //GST_WARNING_OBJECT (self, "Received data buffer %p", data);
  malloc_data = g_slice_new0 (FreeFuncBuffer);
  malloc_data->self = gst_object_ref (self);
  malloc_data->array = (*env)->NewGlobalRef (env, array);
  malloc_data->data = (*env)->GetByteArrayElements (env, array, NULL);

  buffer = gst_buffer_new ();
  GST_BUFFER_DATA (buffer) = (guint8 *) malloc_data->data;
  GST_BUFFER_SIZE (buffer) = self->buffer_size;
  GST_BUFFER_MALLOCDATA (buffer) = (gpointer) malloc_data;
  GST_BUFFER_FREE_FUNC (buffer) = gst_ahc_src_buffer_free_func;
  GST_BUFFER_DURATION (buffer) = duration;
  GST_BUFFER_TIMESTAMP (buffer) = timestamp;

  item = g_slice_new0 (GstDataQueueItem);
  item->object = GST_MINI_OBJECT (buffer);
  item->size = GST_BUFFER_SIZE (buffer);
  item->duration = GST_BUFFER_DURATION (buffer);
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) _data_queue_item_free;

  if (!gst_data_queue_push (self->queue, item)) {
    /* Can't add buffer to queue. Must be flushing. */
    _data_queue_item_free (item);
  }
}

static void
gst_ahc_src_on_error (gint error, gpointer user_data)
{
  GstAHCSrc *self = GST_AHC_SRC (user_data);

  GST_WARNING_OBJECT (self, "Received error code : %d", error);
}

static gboolean
gst_ahc_src_open (GstAHCSrc * self)
{
  GST_DEBUG_OBJECT (self, "Openning camera");

  self->camera = gst_ah_camera_open (self->device);

  if (self->camera) {
    GST_DEBUG_OBJECT (self, "Opened camera");

    self->texture = gst_ag_surfacetexture_new (0);
    gst_ah_camera_set_preview_texture (self->camera, self->texture);
    self->buffer_size = 0;
  } else {
    gint num_cams = gst_ah_camera_get_number_of_cameras ();
    if (num_cams > 0 && self->device < num_cams) {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("Unable to open device '%d'.", self->device), GST_ERROR_SYSTEM);
    } else if (num_cams > 0) {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("Device '%d' does not exist.", self->device), GST_ERROR_SYSTEM);
    } else {
      GST_ELEMENT_ERROR (self, RESOURCE, NOT_FOUND,
          ("There are no cameras available on this device."), GST_ERROR_SYSTEM);
    }
  }

  return (self->camera != NULL);
}

static void
gst_ahc_src_close (GstAHCSrc * self)
{
  if (self->camera) {
    gst_ah_camera_set_error_callback (self->camera, NULL, NULL);
    gst_ah_camera_set_preview_callback_with_buffer (self->camera, NULL, NULL);
    gst_ah_camera_release (self->camera);
  }
  self->camera = NULL;

  if (self->texture)
    gst_ag_surfacetexture_release (self->texture);
  self->texture = NULL;
}

static GstStateChangeReturn
gst_ahc_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstAHCSrc *self = GST_AHC_SRC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_ahc_src_open (self))
        return GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_ahc_src_close (self);
      break;
    default:
      break;
  }

  return ret;
}

static gboolean
gst_ahc_src_start (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Starting preview");
  if (self->camera) {
    self->previous_ts = GST_CLOCK_TIME_NONE;
    self->fps_min = self->fps_max = self->width = self->height = 0;
    self->format = ImageFormat_UNKNOWN;
    self->start = TRUE;

    return TRUE;
  } else {
    return FALSE;
  }
}

static gboolean
gst_ahc_src_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Stopping preview");
  if (self->camera) {
    gst_data_queue_flush (self->queue);
    self->start = FALSE;
    gst_ah_camera_set_error_callback (self->camera, NULL, NULL);
    return gst_ah_camera_stop_preview (self->camera);
  }
  return TRUE;
}

static gboolean
gst_ahc_src_unlock (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Unlocking create");
  gst_data_queue_set_flushing (self->queue, TRUE);

  return TRUE;
}

static gboolean
gst_ahc_src_unlock_stop (GstBaseSrc * bsrc)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  GST_DEBUG_OBJECT (self, "Stopping unlock");
  gst_data_queue_set_flushing (self->queue, FALSE);

  return TRUE;
}

static GstFlowReturn
gst_ahc_src_create (GstPushSrc * src, GstBuffer ** buffer)
{
  GstAHCSrc *self = GST_AHC_SRC (src);
  GstDataQueueItem *item;

  if (!gst_data_queue_pop (self->queue, &item))
    return GST_FLOW_WRONG_STATE;

  *buffer = GST_BUFFER (item->object);
  g_slice_free (GstDataQueueItem, item);

  return GST_FLOW_OK;
}

static gboolean
gst_ahc_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  GstAHCSrc *self = GST_AHC_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min, max;

      gst_query_parse_latency (query, NULL, &min, &max);
      min = gst_util_uint64_scale (GST_SECOND, 1000, self->fps_max);
      max = gst_util_uint64_scale (GST_SECOND, 1000, self->fps_min);
      GST_DEBUG_OBJECT (self,
          "Reporting latency min: %" GST_TIME_FORMAT " max: %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min), GST_TIME_ARGS (max));
      gst_query_set_latency (query, TRUE, min, max);

      return TRUE;
      break;
    }
    default:
      return GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
      break;
  }

  g_assert_not_reached ();
}
