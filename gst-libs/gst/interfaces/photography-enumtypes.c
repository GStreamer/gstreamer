
/* Generated data (by glib-mkenums) */

#include "photography-enumtypes.h"

#include "photography.h"

/* enumerations from "photography.h" */
GType
gst_white_balance_mode_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PHOTOGRAPHY_WB_MODE_AUTO, "GST_PHOTOGRAPHY_WB_MODE_AUTO", "auto"},
      {GST_PHOTOGRAPHY_WB_MODE_DAYLIGHT, "GST_PHOTOGRAPHY_WB_MODE_DAYLIGHT",
          "daylight"},
      {GST_PHOTOGRAPHY_WB_MODE_CLOUDY, "GST_PHOTOGRAPHY_WB_MODE_CLOUDY",
          "cloudy"},
      {GST_PHOTOGRAPHY_WB_MODE_SUNSET, "GST_PHOTOGRAPHY_WB_MODE_SUNSET",
          "sunset"},
      {GST_PHOTOGRAPHY_WB_MODE_TUNGSTEN, "GST_PHOTOGRAPHY_WB_MODE_TUNGSTEN",
          "tungsten"},
      {GST_PHOTOGRAPHY_WB_MODE_FLUORESCENT,
          "GST_PHOTOGRAPHY_WB_MODE_FLUORESCENT", "fluorescent"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstWhiteBalanceMode", values);
  }
  return etype;
}

GType
gst_colour_tone_mode_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NORMAL,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NORMAL", "normal"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SEPIA,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SEPIA", "sepia"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NEGATIVE,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NEGATIVE", "negative"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_GRAYSCALE,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_GRAYSCALE", "grayscale"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NATURAL,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_NATURAL", "natural"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_VIVID,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_VIVID", "vivid"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_COLORSWAP,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_COLORSWAP", "colorswap"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SOLARIZE,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_SOLARIZE", "solarize"},
      {GST_PHOTOGRAPHY_COLOUR_TONE_MODE_OUT_OF_FOCUS,
          "GST_PHOTOGRAPHY_COLOUR_TONE_MODE_OUT_OF_FOCUS", "out-of-focus"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstColourToneMode", values);
  }
  return etype;
}

GType
gst_scene_mode_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PHOTOGRAPHY_SCENE_MODE_MANUAL, "GST_PHOTOGRAPHY_SCENE_MODE_MANUAL",
          "manual"},
      {GST_PHOTOGRAPHY_SCENE_MODE_CLOSEUP, "GST_PHOTOGRAPHY_SCENE_MODE_CLOSEUP",
          "closeup"},
      {GST_PHOTOGRAPHY_SCENE_MODE_PORTRAIT,
          "GST_PHOTOGRAPHY_SCENE_MODE_PORTRAIT", "portrait"},
      {GST_PHOTOGRAPHY_SCENE_MODE_LANDSCAPE,
          "GST_PHOTOGRAPHY_SCENE_MODE_LANDSCAPE", "landscape"},
      {GST_PHOTOGRAPHY_SCENE_MODE_SPORT, "GST_PHOTOGRAPHY_SCENE_MODE_SPORT",
          "sport"},
      {GST_PHOTOGRAPHY_SCENE_MODE_NIGHT, "GST_PHOTOGRAPHY_SCENE_MODE_NIGHT",
          "night"},
      {GST_PHOTOGRAPHY_SCENE_MODE_AUTO, "GST_PHOTOGRAPHY_SCENE_MODE_AUTO",
          "auto"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstSceneMode", values);
  }
  return etype;
}

GType
gst_flash_mode_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PHOTOGRAPHY_FLASH_MODE_AUTO, "GST_PHOTOGRAPHY_FLASH_MODE_AUTO",
          "auto"},
      {GST_PHOTOGRAPHY_FLASH_MODE_OFF, "GST_PHOTOGRAPHY_FLASH_MODE_OFF", "off"},
      {GST_PHOTOGRAPHY_FLASH_MODE_ON, "GST_PHOTOGRAPHY_FLASH_MODE_ON", "on"},
      {GST_PHOTOGRAPHY_FLASH_MODE_FILL_IN, "GST_PHOTOGRAPHY_FLASH_MODE_FILL_IN",
          "fill-in"},
      {GST_PHOTOGRAPHY_FLASH_MODE_RED_EYE, "GST_PHOTOGRAPHY_FLASH_MODE_RED_EYE",
          "red-eye"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstFlashMode", values);
  }
  return etype;
}

GType
gst_focus_status_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PHOTOGRAPHY_FOCUS_STATUS_NONE, "GST_PHOTOGRAPHY_FOCUS_STATUS_NONE",
          "none"},
      {GST_PHOTOGRAPHY_FOCUS_STATUS_RUNNING,
          "GST_PHOTOGRAPHY_FOCUS_STATUS_RUNNING", "running"},
      {GST_PHOTOGRAPHY_FOCUS_STATUS_FAIL, "GST_PHOTOGRAPHY_FOCUS_STATUS_FAIL",
          "fail"},
      {GST_PHOTOGRAPHY_FOCUS_STATUS_SUCCESS,
          "GST_PHOTOGRAPHY_FOCUS_STATUS_SUCCESS", "success"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstFocusStatus", values);
  }
  return etype;
}

GType
gst_photo_caps_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GFlagsValue values[] = {
      {GST_PHOTOGRAPHY_CAPS_NONE, "GST_PHOTOGRAPHY_CAPS_NONE", "none"},
      {GST_PHOTOGRAPHY_CAPS_EV_COMP, "GST_PHOTOGRAPHY_CAPS_EV_COMP", "ev-comp"},
      {GST_PHOTOGRAPHY_CAPS_ISO_SPEED, "GST_PHOTOGRAPHY_CAPS_ISO_SPEED",
          "iso-speed"},
      {GST_PHOTOGRAPHY_CAPS_WB_MODE, "GST_PHOTOGRAPHY_CAPS_WB_MODE", "wb-mode"},
      {GST_PHOTOGRAPHY_CAPS_TONE, "GST_PHOTOGRAPHY_CAPS_TONE", "tone"},
      {GST_PHOTOGRAPHY_CAPS_SCENE, "GST_PHOTOGRAPHY_CAPS_SCENE", "scene"},
      {GST_PHOTOGRAPHY_CAPS_FLASH, "GST_PHOTOGRAPHY_CAPS_FLASH", "flash"},
      {GST_PHOTOGRAPHY_CAPS_ZOOM, "GST_PHOTOGRAPHY_CAPS_ZOOM", "zoom"},
      {GST_PHOTOGRAPHY_CAPS_FOCUS, "GST_PHOTOGRAPHY_CAPS_FOCUS", "focus"},
      {GST_PHOTOGRAPHY_CAPS_APERTURE, "GST_PHOTOGRAPHY_CAPS_APERTURE",
          "aperture"},
      {GST_PHOTOGRAPHY_CAPS_EXPOSURE, "GST_PHOTOGRAPHY_CAPS_EXPOSURE",
          "exposure"},
      {GST_PHOTOGRAPHY_CAPS_SHAKE, "GST_PHOTOGRAPHY_CAPS_SHAKE", "shake"},
      {0, NULL, NULL}
    };
    etype = g_flags_register_static ("GstPhotoCaps", values);
  }
  return etype;
}

GType
gst_photo_shake_risk_get_type (void)
{
  static GType etype = 0;
  if (etype == 0) {
    static const GEnumValue values[] = {
      {GST_PHOTOGRAPHY_SHAKE_RISK_LOW, "GST_PHOTOGRAPHY_SHAKE_RISK_LOW", "low"},
      {GST_PHOTOGRAPHY_SHAKE_RISK_MEDIUM, "GST_PHOTOGRAPHY_SHAKE_RISK_MEDIUM",
          "medium"},
      {GST_PHOTOGRAPHY_SHAKE_RISK_HIGH, "GST_PHOTOGRAPHY_SHAKE_RISK_HIGH",
          "high"},
      {0, NULL, NULL}
    };
    etype = g_enum_register_static ("GstPhotoShakeRisk", values);
  }
  return etype;
}

/* Generated data ends here */
