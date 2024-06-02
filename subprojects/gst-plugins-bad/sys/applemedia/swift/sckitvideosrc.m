/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
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

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <IOKit/graphics/IOGraphicsLib.h>
#import <Cocoa/Cocoa.h>

#include "sckitvideosrc.h"

GST_DEBUG_CATEGORY (gst_sckit_video_src_debug);
#define GST_CAT_DEFAULT gst_sckit_video_src_debug

enum {
  PROP_0,
  PROP_CAPTURE_MODE,
  PROP_DISPLAY_ID,
  PROP_WINDOW_IDS,
  PROP_APPLICATION_IDS,
  PROP_SHOW_CURSOR,
  PROP_ALLOW_TRANSPARENCY,
  PROP_CROP_X,
  PROP_CROP_Y,
  PROP_CROP_WIDTH,
  PROP_CROP_HEIGHT,
};

enum {
  SIGNAL_0,
  RECONFIGURE_CAPTURE_SIGNAL,
  LAST_SIGNAL
};

static guint gst_sckit_video_src_signals[LAST_SIGNAL] = { 0 };

static GstStaticPadTemplate gst_sckitsrc_video_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS(GST_VIDEO_CAPS_MAKE("{ NV12, BGRA }"))
);

GST_ELEMENT_REGISTER_DEFINE (sckitvideosrc, "sckitvideosrc", GST_RANK_PRIMARY, GST_TYPE_SCKIT_VIDEO_SRC);

#define gst_sckit_video_src_parent_class parent_class
G_DEFINE_TYPE (GstSCKitVideoSrc, gst_sckit_video_src, GST_TYPE_BASE_SRC);

#define GST_TYPE_SCKIT_VIDEO_SRC_MODE (gst_sckit_video_src_mode_get_type ())
static GType
gst_sckit_video_src_mode_get_type (void)
{
  static GType sckit_video_src_mode_type = 0;
  static const GEnumValue modes[] = {    
    {GST_SCKIT_VIDEO_SRC_MODE_DISPLAY, "Display capture", "display"},
    {GST_SCKIT_VIDEO_SRC_MODE_WINDOW, "Single window capture", "window"},
    {GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_WINDOWS, "Display capture excluding specified windows", "display-excl-windows"},
    {GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_WINDOWS, "Display capture only including specified windows", "display-incl-windows"},
    {GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_APPLICATIONS_EXCEPT_WINDOWS, "Display capture excluding specified applications, with specified windows as exceptions", "display-excl-apps"},
    {GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_APPLICATIONS_EXCEPT_WINDOWS, "Display capture only including specified applications, with specified windows as exceptions", "display-incl-apps"},
    {0, NULL, NULL},
  };

  if (!sckit_video_src_mode_type)
    sckit_video_src_mode_type = g_enum_register_static ("GstSCKitVideoSrcMode", modes);

  return sckit_video_src_mode_type;
}

static void
gst_sckit_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (object);
  SCKitVideoSrc *impl = self->impl;

  switch (prop_id) {
    case PROP_CAPTURE_MODE:
      g_value_set_enum (value, [impl captureMode]);
      break;
    case PROP_DISPLAY_ID:
      g_value_set_int (value, [impl displayID]);
      break;
    case PROP_WINDOW_IDS: {
      for (NSNumber* num in [impl windowIDs]) {
        GValue val = G_VALUE_INIT;
        g_value_init(&val, G_TYPE_UINT);
        g_value_set_uint(&val, [num unsignedIntValue]);
        gst_value_array_append_value(value, &val);
        g_value_unset(&val);
      }
      break;
    }
    case PROP_APPLICATION_IDS: {
      for (NSNumber* num in [impl applicationIDs]) {
        GValue val = G_VALUE_INIT;
        g_value_init(&val, G_TYPE_INT);
        g_value_set_int(&val, [num intValue]);
        gst_value_array_append_value(value, &val);
        g_value_unset(&val);
      }
      break;
    }
    case PROP_SHOW_CURSOR:
      g_value_set_boolean (value, [impl showCursor]);
      break;
    case PROP_ALLOW_TRANSPARENCY:
      g_value_set_boolean (value, [impl allowTransparency]);
      break;
    case PROP_CROP_X:
      g_value_set_uint (value, (guint) [impl cropRect].origin.x);
      break;
    case PROP_CROP_Y:
      g_value_set_uint (value, (guint) [impl cropRect].origin.y);
      break;
    case PROP_CROP_WIDTH:
      g_value_set_uint (value, (guint) [impl cropRect].size.width);
      break;
    case PROP_CROP_HEIGHT:
      g_value_set_uint (value, (guint) [impl cropRect].size.height);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sckit_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (object);
  SCKitVideoSrc *impl = self->impl;

  switch (prop_id) {
    case PROP_CAPTURE_MODE:
      [impl setCaptureMode:g_value_get_enum (value)];
      break;
    case PROP_DISPLAY_ID:
      [impl setDisplayID:g_value_get_int (value)];
      break;
    case PROP_WINDOW_IDS: {
      NSMutableArray<NSNumber*>* windowIDs = [NSMutableArray array];
      for (guint i = 0; i < gst_value_array_get_size(value); i++) {
        const GValue *val = gst_value_array_get_value(value, i);
        NSNumber *num = [NSNumber numberWithUnsignedInt:g_value_get_uint(val)];
        [windowIDs addObject:num];
      }
      [impl setWindowIDs:windowIDs];
      break;
    }
    case PROP_APPLICATION_IDS: {
      NSMutableArray<NSNumber*>* applicationIDs = [NSMutableArray array];
      for (guint i = 0; i < gst_value_array_get_size(value); i++) {
        const GValue *val = gst_value_array_get_value(value, i);
        NSNumber *num = [NSNumber numberWithInt:g_value_get_int(val)];
        [applicationIDs addObject:num];
      }
      [impl setApplicationIDs:applicationIDs];
      break;
    }
    case PROP_SHOW_CURSOR:
      [impl setShowCursor:g_value_get_boolean (value)];
      break;
    case PROP_ALLOW_TRANSPARENCY:
      [impl setAllowTransparency:g_value_get_boolean (value)];
      break;
    case PROP_CROP_X:
      [impl setCropX:g_value_get_uint (value)];
      break;
    case PROP_CROP_Y:
      [impl setCropY:g_value_get_uint (value)];
      break;
    case PROP_CROP_WIDTH:
      [impl setCropWidth:g_value_get_uint (value)];
      break;
    case PROP_CROP_HEIGHT:
      [impl setCropHeight:g_value_get_uint (value)];
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_sckit_video_src_signal_reconfigure (GstSCKitVideoSrc *self)
{
  [self->impl gstHandleReconfigureSignal];
}

static GstFlowReturn
gst_sckit_video_src_create (GstBaseSrc * src, guint64 offset, guint size, GstBuffer ** buf)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstCreate:offset size:size bufPtr:buf];
}

static gboolean
gst_sckit_video_src_start (GstBaseSrc * src)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstStart];
}

static gboolean
gst_sckit_video_src_stop (GstBaseSrc * src)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstStop];
}

static gboolean
gst_sckit_video_src_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstSetCaps:caps];
}

static GstCaps *
gst_sckit_video_src_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstGetCaps:filter];
}

static GstCaps *
gst_sckit_video_src_fixate (GstBaseSrc * src, GstCaps * caps)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstFixateCaps:caps];
}

static gboolean
gst_sckit_video_src_unlock (GstBaseSrc * src)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstUnlock];
}

static gboolean
gst_sckit_video_src_unlock_stop (GstBaseSrc * src)
{
  GstSCKitVideoSrc *self = GST_SCKIT_VIDEO_SRC (src);
  return [self->impl gstUnlockStop];
}

static void
gst_sckit_video_src_init (GstSCKitVideoSrc * self)
{
  GstBaseSrc *base_src = GST_BASE_SRC (self);
  self->impl = [[SCKitVideoSrc alloc] initWithSrc:base_src debugCat:GST_CAT_DEFAULT];
}

static void
gst_sckit_video_src_class_init (GstSCKitVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *basesrc_class = GST_BASE_SRC_CLASS (klass);

  gst_element_class_add_static_pad_template (element_class, &gst_sckitsrc_video_template);

  gst_element_class_set_static_metadata (element_class,
      "ScreenCaptureKit Video Source", "Source/Video",
      "Screen/window capture on macOS", "Piotr Brzeziński <piotr@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_sckit_video_src_debug, "sckitvideosrc", 0, "ScreenCaptureKit video source");

  gobject_class->get_property = gst_sckit_video_src_get_property;
  gobject_class->set_property = gst_sckit_video_src_set_property;

  basesrc_class->create = gst_sckit_video_src_create;
  basesrc_class->start = gst_sckit_video_src_start;
  basesrc_class->stop = gst_sckit_video_src_stop;
  basesrc_class->set_caps = gst_sckit_video_src_set_caps;
  basesrc_class->get_caps = gst_sckit_video_src_get_caps;
  basesrc_class->fixate = gst_sckit_video_src_fixate;
  basesrc_class->unlock = gst_sckit_video_src_unlock;
  basesrc_class->unlock_stop = gst_sckit_video_src_unlock_stop;


  /**
   * GstSCKitVideoSrc:mode:
   *
   * Capture mode/filter type to use. Reflects capabilities of SCContentFilter,
   * please see documentation for details:
   * https://developer.apple.com/documentation/screencapturekit/sccontentfilter
   *
   * Possible modes:
   * - display: captures entire specified display (main display if display-id
   *   unspecified)
   * - window: captures a single window (specified by window-ids) irrespective
   *   of its parent display
   * - display-excl-windows: captures entire specified display excluding
   *   specified windows
   * - display-incl-windows: captures entire specified display only including
   *   specified windows
   * - display-excl-apps: captures entire specified display excluding windows
   *   of specified applications, with specified windows as exceptions
   *   (if a window was excluded by the application filter and is added to
   *   exceptions, it will be captured, and vice versa)
   * - display-incl-apps: captures entire specified display only including
   *   windows of specified applications, with specified windows as exceptions
   *   (if a window was included by the application filter and is added to
   *   exceptions, it will not be captured, and vice versa)
   *
   * In all modes output resolution will be dynamically adjusted to match
   * captured content, e.g. when a window is resized.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_CAPTURE_MODE,
      g_param_spec_enum ("mode", "Capture mode",
          "ScreenCaptureKit capture mode",
          GST_TYPE_SCKIT_VIDEO_SRC_MODE, DEFAULT_CAPTURE_MODE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSCKitVideoSrc:display-id:
   *
   * CGDirectDisplayID of display to capture (-1 to pick default).
   * Used in all modes except for window capture.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_DISPLAY_ID,
      g_param_spec_int ("display-id", "Display ID",
          "CGDirectDisplayID of display to capture (-1 to pick default)",
          -1, G_MAXINT, -1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSCKitVideoSrc:window-ids:
   *
   * A GstArray of CGWindowIDs of windows to capture or exclude, depending on
   * the capture mode. In single window capture mode only one ID should be
   * specified. Specifying windows IDs here will have different effects based
   * on the currently active mode.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_WINDOW_IDS,
      gst_param_spec_array("window-ids", "Window IDs",
          "Array of CGWindowIDs of windows to capture or exclude, depending on mode",
          g_param_spec_uint("window-id",
              "Window ID",
              "CGWindowID of window to capture or exclude",
              0, G_MAXUINT,
              0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSCKitVideoSrc:application-ids:
   *
   * A GstArray of PIDs of applications to capture or exclude, depending on the
   * capture mode. Only used in display-excl-apps and display-incl-apps modes.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_APPLICATION_IDS,
      gst_param_spec_array("application-ids", "Application IDs",
          "Array of PIDs of applications to capture or exclude, depending on mode",
          g_param_spec_int("application-id",
              "Application ID",
              "PID of application to capture or exclude",
              G_MININT, G_MAXINT,
              0,
              G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS),
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSCKitVideoSrc:show-cursor:
   *
   * Whether to show cursor in captured video.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_SHOW_CURSOR,
      g_param_spec_boolean ("show-cursor", "Show cursor",
          "Whether to show cursor in captured video", DEFAULT_SHOW_CURSOR,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
  if (@available(macOS 14.0, *)) {
    /**
     * GstSCKitVideoSrc:allow-transparency:
     *
     * Whether to allow transparency in captured content. If enabled, any
     * semi-transparent content will retain its transparency. Otherwise, it
     * will be backed by a solid white background. Transparency will only work
     * with BGRA output, but depending on the macOS version, this might also
     * cause NV12 output to switch background colour between white and black.
     *
     * Since: 1.26
     */
    g_object_class_install_property (gobject_class, PROP_ALLOW_TRANSPARENCY,
        g_param_spec_boolean ("allow-transparency", "Allow transparency",
            "If enabled, any semi-transparent content will retain its transparency",
            DEFAULT_ALLOW_TRANSPARENCY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  }

  /**
   * GstSCKitVideoSrc:crop-x:
   *
   * X coordinate of the origin of the crop rectangle in screen points (pre
   * Retina scaling). Only has effect in display-based capture modes.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_CROP_X,
      g_param_spec_uint ("crop-x", "Crop X",
          "X coordinate of the top-left corner of the crop rectangle in screen points (pre Retina scaling)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSCKitVideoSrc:crop-y:
   *
   * Y coordinate of the origin of the crop rectangle in screen points (pre
   * Retina scaling). Only has effect in display-based capture modes.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_CROP_Y,
      g_param_spec_uint ("crop-y", "Crop Y",
          "Y coordinate of the top-left corner of the crop rectangle in screen points (pre Retina scaling)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSCKitVideoSrc:crop-width:
   *
   * Width of the crop rectangle in screen points (pre Retina scaling).
   * Only has effect in display-based capture modes.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_CROP_WIDTH,
      g_param_spec_uint ("crop-width", "Crop width",
          "Width of the crop rectangle in screen points (pre Retina scaling)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  
  /**
   * GstSCKitVideoSrc:crop-height:
   *
   * Height of the crop rectangle in screen points (pre Retina scaling).
   * Only has effect in display-based capture modes.
   *
   * Since: 1.26
   */
  g_object_class_install_property (gobject_class, PROP_CROP_HEIGHT,
      g_param_spec_uint ("crop-height", "Crop height",
          "Height of the crop rectangle in screen points (pre Retina scaling)",
          0, G_MAXUINT, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /**
   * GstSCKitVideoSrc::reconfigure-capture:
   *
   * Can be used to trigger reconfiguration of the capture session while it's
   * running (pipeline is in PLAYING). Use after changing all necessary
   * properties (e.g. capture mode + display/window/application IDs).
   */
  gst_sckit_video_src_signals[RECONFIGURE_CAPTURE_SIGNAL] =
      g_signal_new_class_handler ("reconfigure-capture",
          G_TYPE_FROM_CLASS (klass),G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
          G_CALLBACK (gst_sckit_video_src_signal_reconfigure), NULL, NULL,
          NULL, G_TYPE_NONE, 0);
}
