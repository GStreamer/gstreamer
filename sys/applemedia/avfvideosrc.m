/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
 * Copyright (C) 2016 Alessandro Decina <twi@centricular.com>
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
#  include "config.h"
#endif

#include "avfvideosrc.h"
#include "glcontexthelper.h"

#import <AVFoundation/AVFoundation.h>
#if !HAVE_IOS
#import <AppKit/AppKit.h>
#endif
#include <gst/video/video.h>
#include <gst/gl/gstglcontext.h>
#include "coremediabuffer.h"
#include "videotexturecache.h"

#define DEFAULT_DEVICE_INDEX  -1
#define DEFAULT_POSITION      GST_AVF_VIDEO_SOURCE_POSITION_DEFAULT
#define DEFAULT_ORIENTATION   GST_AVF_VIDEO_SOURCE_ORIENTATION_DEFAULT
#define DEFAULT_DEVICE_TYPE   GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_DEFAULT
#define DEFAULT_DO_STATS      FALSE

#define DEVICE_FPS_N          25
#define DEVICE_FPS_D          1

#define BUFFER_QUEUE_SIZE     2

GST_DEBUG_CATEGORY (gst_avf_video_src_debug);
#define GST_CAT_DEFAULT gst_avf_video_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (
#if !HAVE_IOS
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "UYVY") ", "
        "texture-target = " GST_GL_TEXTURE_TARGET_RECTANGLE_STR ";"
#else
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "NV12") ", "
        "texture-target = " GST_GL_TEXTURE_TARGET_2D_STR "; "
#endif
        "video/x-raw, "
        "format = (string) { NV12, UYVY, YUY2 }, "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE "; "

        "video/x-raw, "
        "format = (string) BGRA, "
        "framerate = " GST_VIDEO_FPS_RANGE ", "
        "width = " GST_VIDEO_SIZE_RANGE ", "
        "height = " GST_VIDEO_SIZE_RANGE "; "
));

typedef enum _QueueState {
  NO_BUFFERS = 1,
  HAS_BUFFER_OR_STOP_REQUEST,
} QueueState;

#define gst_avf_video_src_parent_class parent_class
G_DEFINE_TYPE (GstAVFVideoSrc, gst_avf_video_src, GST_TYPE_PUSH_SRC);

#define GST_TYPE_AVF_VIDEO_SOURCE_POSITION (gst_avf_video_source_position_get_type ())
static GType
gst_avf_video_source_position_get_type (void)
{
  static GType avf_video_source_position_type = 0;

  if (!avf_video_source_position_type) {
    static GEnumValue position_types[] = {
      { GST_AVF_VIDEO_SOURCE_POSITION_FRONT, "Front-facing camera", "front" },
      { GST_AVF_VIDEO_SOURCE_POSITION_BACK,  "Back-facing camera", "back"  },
      { GST_AVF_VIDEO_SOURCE_POSITION_DEFAULT,  "Default", "default"  },
      { 0, NULL, NULL },
    };

    avf_video_source_position_type =
    g_enum_register_static ("GstAVFVideoSourcePosition",
                            position_types);
  }

  return avf_video_source_position_type;
}

#define GST_TYPE_AVF_VIDEO_SOURCE_ORIENTATION (gst_avf_video_source_orientation_get_type ())
static GType
gst_avf_video_source_orientation_get_type (void)
{
  static GType avf_video_source_orientation_type = 0;

  if (!avf_video_source_orientation_type) {
    static GEnumValue orientation_types[] = {
      { GST_AVF_VIDEO_SOURCE_ORIENTATION_PORTRAIT, "Indicates that video should be oriented vertically, top at the top.", "portrait" },
      { GST_AVF_VIDEO_SOURCE_ORIENTATION_PORTRAIT_UPSIDE_DOWN, "Indicates that video should be oriented vertically, top at the bottom.", "portrat-upside-down" },
      { GST_AVF_VIDEO_SOURCE_ORIENTATION_LANDSCAPE_RIGHT, "Indicates that video should be oriented horizontally, top on the left.", "landscape-right" },
      { GST_AVF_VIDEO_SOURCE_ORIENTATION_LANDSCAPE_LEFT, "Indicates that video should be oriented horizontally, top on the right.", "landscape-left" },
      { GST_AVF_VIDEO_SOURCE_ORIENTATION_DEFAULT, "Default", "default" },
      { 0, NULL, NULL },
    };

    avf_video_source_orientation_type =
    g_enum_register_static ("GstAVFVideoSourceOrientation",
                            orientation_types);
  }

  return avf_video_source_orientation_type;
}

#define GST_TYPE_AVF_VIDEO_SOURCE_DEVICE_TYPE (gst_avf_video_source_device_type_get_type ())
static GType
gst_avf_video_source_device_type_get_type (void)
{
  static GType avf_video_source_device_type_type = 0;

  if (!avf_video_source_device_type_type) {
    static GEnumValue device_type_types[] = {
      { GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_BUILT_IN_WIDE_ANGLE_CAMERA, "A built-in wide angle camera. These devices are suitable for general purpose use.", "wide-angle" },
      { GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_BUILT_IN_TELEPHOTO_CAMERA, "A built-in camera device with a longer focal length than a wide-angle camera.", "telephoto" },
      { GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_BUILT_IN_DUAL_CAMERA, "A dual camera device, combining built-in wide-angle and telephoto cameras that work together as a single capture device.", "dual" },
      { GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_DEFAULT, "Default", "default" },
      { 0, NULL, NULL },
    };

    avf_video_source_device_type_type =
    g_enum_register_static ("GstAVFVideoSourceDeviceType",
                            device_type_types);
  }

  return avf_video_source_device_type_type;
}

@interface GstAVFVideoSrcImpl : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
  GstElement *element;
  GstBaseSrc *baseSrc;
  GstPushSrc *pushSrc;

  gint deviceIndex;
  GstAVFVideoSourcePosition position;
  GstAVFVideoSourceOrientation orientation;
  GstAVFVideoSourceDeviceType deviceType;
  BOOL doStats;

  AVCaptureSession *session;
  AVCaptureInput *input;
  AVCaptureVideoDataOutput *output;
  AVCaptureDevice *device;
  AVCaptureConnection *connection;
  CMClockRef inputClock;

  dispatch_queue_t mainQueue;
  dispatch_queue_t workerQueue;
  NSConditionLock *bufQueueLock;
  NSMutableArray *bufQueue;
  BOOL stopRequest;

  GstCaps *caps;
  GstVideoFormat format;
  gint width, height;
  GstClockTime latency;
  guint64 offset;

  GstClockTime lastSampling;
  guint count;
  gint fps;
  BOOL captureScreen;
  BOOL captureScreenCursor;
  BOOL captureScreenMouseClicks;

  BOOL useVideoMeta;
  GstGLContextHelper *ctxh;
  GstVideoTextureCache *textureCache;
}

- (id)init;
- (id)initWithSrc:(GstPushSrc *)src;
- (void)finalize;

@property int deviceIndex;
@property GstAVFVideoSourcePosition position;
@property GstAVFVideoSourceOrientation orientation;
@property GstAVFVideoSourceDeviceType deviceType;
@property BOOL doStats;
@property int fps;
@property BOOL captureScreen;
@property BOOL captureScreenCursor;
@property BOOL captureScreenMouseClicks;

- (BOOL)openScreenInput;
- (BOOL)openDeviceInput;
- (BOOL)openDevice;
- (void)closeDevice;
- (GstVideoFormat)getGstVideoFormat:(NSNumber *)pixel_format;
#if !HAVE_IOS
- (CGDirectDisplayID)getDisplayIdFromDeviceIndex;
- (float)getScaleFactorFromDeviceIndex;
#endif
- (GstCaps *)getDeviceCaps;
- (BOOL)setDeviceCaps:(GstVideoInfo *)info;
- (BOOL)getSessionPresetCaps:(GstCaps *)result;
- (BOOL)setSessionPresetCaps:(GstVideoInfo *)info;
- (GstCaps *)getCaps;
- (BOOL)setCaps:(GstCaps *)new_caps;
- (BOOL)start;
- (BOOL)stop;
- (BOOL)unlock;
- (BOOL)unlockStop;
- (BOOL)query:(GstQuery *)query;
- (void)setContext:(GstContext *)context;
- (GstStateChangeReturn)changeState:(GstStateChange)transition;
- (GstFlowReturn)create:(GstBuffer **)buf;
- (GstCaps *)fixate:(GstCaps *)caps;
- (BOOL)decideAllocation:(GstQuery *)query;
- (void)updateStatistics;
- (void)captureOutput:(AVCaptureOutput *)captureOutput
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection;

@end

#if HAVE_IOS

static AVCaptureDeviceType GstAVFVideoSourceDeviceType2AVCaptureDeviceType(GstAVFVideoSourceDeviceType deviceType) {
  switch (deviceType) {
    case GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_BUILT_IN_WIDE_ANGLE_CAMERA:
      return AVCaptureDeviceTypeBuiltInWideAngleCamera;
    case GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_BUILT_IN_TELEPHOTO_CAMERA:
      return AVCaptureDeviceTypeBuiltInTelephotoCamera;
    case GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_BUILT_IN_DUAL_CAMERA:
      return AVCaptureDeviceTypeBuiltInDuoCamera;
    case GST_AVF_VIDEO_SOURCE_DEVICE_TYPE_DEFAULT:
      g_assert_not_reached();
  }
}

static AVCaptureDevicePosition GstAVFVideoSourcePosition2AVCaptureDevicePosition(GstAVFVideoSourcePosition position) {
  switch (position) {
    case GST_AVF_VIDEO_SOURCE_POSITION_FRONT:
      return AVCaptureDevicePositionFront;
    case GST_AVF_VIDEO_SOURCE_POSITION_BACK:
      return AVCaptureDevicePositionBack;
    case GST_AVF_VIDEO_SOURCE_POSITION_DEFAULT:
      g_assert_not_reached();
  }

}

static AVCaptureVideoOrientation GstAVFVideoSourceOrientation2AVCaptureVideoOrientation(GstAVFVideoSourceOrientation orientation) {
  switch (orientation) {
    case GST_AVF_VIDEO_SOURCE_ORIENTATION_PORTRAIT:
      return AVCaptureVideoOrientationPortrait;
    case GST_AVF_VIDEO_SOURCE_ORIENTATION_PORTRAIT_UPSIDE_DOWN:
      return AVCaptureVideoOrientationPortraitUpsideDown;
    case GST_AVF_VIDEO_SOURCE_ORIENTATION_LANDSCAPE_LEFT:
      return AVCaptureVideoOrientationLandscapeLeft;
    case GST_AVF_VIDEO_SOURCE_ORIENTATION_LANDSCAPE_RIGHT:
      return AVCaptureVideoOrientationLandscapeRight;
    case GST_AVF_VIDEO_SOURCE_ORIENTATION_DEFAULT:
      g_assert_not_reached();
  }
}

#endif

@implementation GstAVFVideoSrcImpl

@synthesize deviceIndex, position, orientation, deviceType, doStats,
    fps, captureScreen, captureScreenCursor, captureScreenMouseClicks;

- (id)init
{
  return [self initWithSrc:NULL];
}

- (id)initWithSrc:(GstPushSrc *)src
{
  if ((self = [super init])) {
    element = GST_ELEMENT_CAST (src);
    baseSrc = GST_BASE_SRC_CAST (src);
    pushSrc = src;

    deviceIndex = DEFAULT_DEVICE_INDEX;
    position = DEFAULT_POSITION;
    orientation = DEFAULT_ORIENTATION;
    deviceType = DEFAULT_DEVICE_TYPE;
    captureScreen = NO;
    captureScreenCursor = NO;
    captureScreenMouseClicks = NO;
    useVideoMeta = NO;
    textureCache = NULL;
    ctxh = gst_gl_context_helper_new (element);
    mainQueue =
        dispatch_queue_create ("org.freedesktop.gstreamer.avfvideosrc.main", NULL);
    workerQueue =
        dispatch_queue_create ("org.freedesktop.gstreamer.avfvideosrc.output", NULL);

    gst_base_src_set_live (baseSrc, TRUE);
    gst_base_src_set_format (baseSrc, GST_FORMAT_TIME);
  }

  return self;
}

- (void)finalize
{
  mainQueue = NULL;
  workerQueue = NULL;
}

- (BOOL)openDeviceInput
{
  NSString *mediaType = AVMediaTypeVideo;
  NSError *err;

  if (deviceIndex == DEFAULT_DEVICE_INDEX) {
#ifdef HAVE_IOS
    if (deviceType != DEFAULT_DEVICE_TYPE && position != DEFAULT_POSITION) {
      device = [AVCaptureDevice
                defaultDeviceWithDeviceType:GstAVFVideoSourceDeviceType2AVCaptureDeviceType(deviceType)
                mediaType:mediaType
                position:GstAVFVideoSourcePosition2AVCaptureDevicePosition(position)];
    } else {
      device = [AVCaptureDevice defaultDeviceWithMediaType:mediaType];
    }
#else
      device = [AVCaptureDevice defaultDeviceWithMediaType:mediaType];
#endif
    if (device == nil) {
      GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
                          ("No video capture devices found"), (NULL));
      return NO;
    }
  } else { // deviceIndex takes priority over position and deviceType
    NSArray *devices = [AVCaptureDevice devicesWithMediaType:mediaType];
    if (deviceIndex >= [devices count]) {
      GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
                          ("Invalid video capture device index"), (NULL));
      return NO;
    }
    device = [devices objectAtIndex:deviceIndex];
  }
  g_assert (device != nil);

  GST_INFO ("Opening '%s'", [[device localizedName] UTF8String]);

  input = [AVCaptureDeviceInput deviceInputWithDevice:device
                                                error:&err];
  if (input == nil) {
    GST_ELEMENT_ERROR (element, RESOURCE, BUSY,
        ("Failed to open device: %s",
        [[err localizedDescription] UTF8String]),
        (NULL));
    device = nil;
    return NO;
  }
  return YES;
}

- (BOOL)openScreenInput
{
#if HAVE_IOS
  return NO;
#else
  CGDirectDisplayID displayId;

  GST_DEBUG_OBJECT (element, "Opening screen input");

  displayId = [self getDisplayIdFromDeviceIndex];
  if (displayId == 0)
    return NO;

  AVCaptureScreenInput *screenInput =
      [[AVCaptureScreenInput alloc] initWithDisplayID:displayId];


  @try {
    [screenInput setValue:[NSNumber numberWithBool:captureScreenCursor]
                 forKey:@"capturesCursor"];

  } @catch (NSException *exception) {
    if (![[exception name] isEqualToString:NSUndefinedKeyException]) {
      GST_WARNING ("An unexpected error occured: %s",
                   [[exception reason] UTF8String]);
    }
    GST_WARNING ("Capturing cursor is only supported in OS X >= 10.8");
  }
  screenInput.capturesMouseClicks = captureScreenMouseClicks;
  input = screenInput;
  return YES;
#endif
}

- (BOOL)openDevice
{
  BOOL success = NO, *successPtr = &success;

  GST_DEBUG_OBJECT (element, "Opening device");

  dispatch_sync (mainQueue, ^{
    BOOL ret;

    if (captureScreen)
      ret = [self openScreenInput];
    else
      ret = [self openDeviceInput];

    if (!ret)
      return;

    output = [[AVCaptureVideoDataOutput alloc] init];
    [output setSampleBufferDelegate:self
                              queue:workerQueue];
    output.alwaysDiscardsLateVideoFrames = YES;
    output.videoSettings = nil; /* device native format */

    session = [[AVCaptureSession alloc] init];
    [session addInput:input];
    [session addOutput:output];

    /* retained by session */
    connection = [[output connections] firstObject];
#ifdef HAVE_IOS
    if (orientation != DEFAULT_ORIENTATION)
      connection.videoOrientation = GstAVFVideoSourceOrientation2AVCaptureVideoOrientation(orientation);
#endif
    inputClock = ((AVCaptureInputPort *)connection.inputPorts[0]).clock;

    *successPtr = YES;
  });

  GST_DEBUG_OBJECT (element, "Opening device %s", success ? "succeed" : "failed");

  return success;
}

- (void)closeDevice
{
  GST_DEBUG_OBJECT (element, "Closing device");

  dispatch_sync (mainQueue, ^{
    g_assert (![session isRunning]);

    connection = nil;
    inputClock = nil;

    [session removeInput:input];
    [session removeOutput:output];

    session = nil;

    input = nil;

    output = nil;

    if (!captureScreen) {
      device = nil;
    }

    if (caps)
      gst_caps_unref (caps);
    caps = NULL;
  });
}

#define GST_AVF_CAPS_NEW(format, w, h, fps_n, fps_d)                  \
    (gst_caps_new_simple ("video/x-raw",                              \
        "width", G_TYPE_INT, w,                                       \
        "height", G_TYPE_INT, h,                                      \
        "format", G_TYPE_STRING, gst_video_format_to_string (format), \
        "framerate", GST_TYPE_FRACTION, (fps_n), (fps_d),             \
        NULL))

#define GST_AVF_FPS_RANGE_CAPS_NEW(format, w, h, min_fps_n, min_fps_d, max_fps_n, max_fps_d) \
    (gst_caps_new_simple ("video/x-raw",                              \
        "width", G_TYPE_INT, w,                                       \
        "height", G_TYPE_INT, h,                                      \
        "format", G_TYPE_STRING, gst_video_format_to_string (format), \
        "framerate", GST_TYPE_FRACTION_RANGE, (min_fps_n), (min_fps_d), (max_fps_n), (max_fps_d), \
        NULL))

- (GstVideoFormat)getGstVideoFormat:(NSNumber *)pixel_format
{
  GstVideoFormat gst_format = GST_VIDEO_FORMAT_UNKNOWN;

  switch ([pixel_format integerValue]) {
  case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: /* 420v */
    gst_format = GST_VIDEO_FORMAT_NV12;
    break;
  case kCVPixelFormatType_422YpCbCr8: /* 2vuy */
    gst_format = GST_VIDEO_FORMAT_UYVY;
    break;
  case kCVPixelFormatType_32BGRA: /* BGRA */
    gst_format = GST_VIDEO_FORMAT_BGRA;
    break;
  case kCVPixelFormatType_422YpCbCr8_yuvs: /* yuvs */
    gst_format = GST_VIDEO_FORMAT_YUY2;
    break;
  default:
    GST_LOG_OBJECT (element, "Pixel format %s is not handled by avfvideosrc",
        [[pixel_format stringValue] UTF8String]);
    break;
  }

  return gst_format;
}

#if !HAVE_IOS
- (CGDirectDisplayID)getDisplayIdFromDeviceIndex
{
  NSDictionary *description;
  NSNumber *displayId;
  NSArray *screens = [NSScreen screens];

  if (deviceIndex == DEFAULT_DEVICE_INDEX)
    return kCGDirectMainDisplay;
  if (deviceIndex >= [screens count]) {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
                        ("Invalid screen capture device index"), (NULL));
    return 0;
  }
  description = [[screens objectAtIndex:deviceIndex] deviceDescription];
  displayId = [description objectForKey:@"NSScreenNumber"];
  return [displayId unsignedIntegerValue];
}

- (float)getScaleFactorFromDeviceIndex
{
  NSArray *screens = [NSScreen screens];

  if (deviceIndex == DEFAULT_DEVICE_INDEX)
    return [[NSScreen mainScreen] backingScaleFactor];
  if (deviceIndex >= [screens count]) {
    GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
                        ("Invalid screen capture device index"), (NULL));
    return 1.0;
  }
  return [[screens objectAtIndex:deviceIndex] backingScaleFactor];
}
#endif


- (CMVideoDimensions)orientedDimensions:(CMVideoDimensions)dimensions
{
  CMVideoDimensions orientedDimensions;
  if (orientation == GST_AVF_VIDEO_SOURCE_ORIENTATION_PORTRAIT_UPSIDE_DOWN ||
      orientation == GST_AVF_VIDEO_SOURCE_ORIENTATION_PORTRAIT) {
    orientedDimensions.width = dimensions.height;
    orientedDimensions.height = dimensions.width;
  } else {
    orientedDimensions = dimensions;
  }
  return orientedDimensions;
}

- (GstCaps *)getDeviceCaps
{
  NSArray *formats = [device valueForKey:@"formats"];
  NSArray *pixel_formats = output.availableVideoCVPixelFormatTypes;
  GstCaps *result_caps, *result_gl_caps;
#if !HAVE_IOS
  GstVideoFormat gl_format = GST_VIDEO_FORMAT_UYVY;
#else
  GstVideoFormat gl_format = GST_VIDEO_FORMAT_NV12;
#endif

  GST_DEBUG_OBJECT (element, "Getting device caps");

  result_caps = gst_caps_new_empty ();
  result_gl_caps = gst_caps_new_empty ();

  /* Do not use AVCaptureDeviceFormat or AVFrameRateRange only
   * available in iOS >= 7.0. We use a dynamic approach with key-value
   * coding or performSelector */
  for (NSObject *f in [formats reverseObjectEnumerator]) {
    /* formatDescription can't be retrieved with valueForKey so use a selector here */
    CMFormatDescriptionRef formatDescription = (__bridge CMFormatDescriptionRef) [f performSelector:@selector(formatDescription)];
    CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);
    dimensions = [self orientedDimensions:dimensions];

    for (NSObject *rate in [f valueForKey:@"videoSupportedFrameRateRanges"]) {
      int min_fps_n, min_fps_d, max_fps_n, max_fps_d;
      gdouble min_fps, max_fps;

      [[rate valueForKey:@"minFrameRate"] getValue:&min_fps];
      gst_util_double_to_fraction (min_fps, &min_fps_n, &min_fps_d);

      [[rate valueForKey:@"maxFrameRate"] getValue:&max_fps];
      gst_util_double_to_fraction (max_fps, &max_fps_n, &max_fps_d);

      for (NSNumber *pixel_format in pixel_formats) {
        GstVideoFormat gst_format = [self getGstVideoFormat:pixel_format];

        if (gst_format != GST_VIDEO_FORMAT_UNKNOWN) {
          if (min_fps != max_fps)
            gst_caps_append (result_caps, GST_AVF_FPS_RANGE_CAPS_NEW (gst_format, dimensions.width, dimensions.height, min_fps_n, min_fps_d, max_fps_n, max_fps_d));
          else
            gst_caps_append (result_caps, GST_AVF_CAPS_NEW (gst_format, dimensions.width, dimensions.height, max_fps_n, max_fps_d));
        }

        if (gst_format == gl_format) {
          GstCaps *gl_caps;
          if (min_fps != max_fps) {
            gl_caps = GST_AVF_FPS_RANGE_CAPS_NEW (gl_format,
                    dimensions.width, dimensions.height,
                    min_fps_n, min_fps_d,
                    max_fps_n, max_fps_d);
          } else {
            gl_caps = GST_AVF_CAPS_NEW (gl_format,
                    dimensions.width, dimensions.height,
                    max_fps_n, max_fps_d);
          }
          gst_caps_set_features (gl_caps, 0,
                  gst_caps_features_new (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
                      NULL));
          gst_caps_set_simple (gl_caps,
                  "texture-target", G_TYPE_STRING,
#if !HAVE_IOS
                  GST_GL_TEXTURE_TARGET_RECTANGLE_STR,
#else
                  GST_GL_TEXTURE_TARGET_2D_STR,
#endif
                  NULL);
          gst_caps_append (result_gl_caps, gl_caps);
        }
      }
    }
  }

  result_gl_caps = gst_caps_simplify (gst_caps_merge (result_gl_caps, result_caps));

  GST_DEBUG_OBJECT (element, "Device returned the following caps %" GST_PTR_FORMAT, result_gl_caps);

  return result_gl_caps;
}

- (BOOL)setDeviceCaps:(GstVideoInfo *)info
{
  double framerate;
  gboolean found_format = FALSE, found_framerate = FALSE;
  NSArray *formats = [device valueForKey:@"formats"];
  gst_util_fraction_to_double (info->fps_n, info->fps_d, &framerate);

  GST_DEBUG_OBJECT (element, "Setting device caps");

  if ([device lockForConfiguration:NULL] == YES) {
    for (NSObject *f in formats) {
      CMFormatDescriptionRef formatDescription = (__bridge CMFormatDescriptionRef) [f performSelector:@selector(formatDescription)];
      CMVideoDimensions dimensions = CMVideoFormatDescriptionGetDimensions(formatDescription);
      dimensions = [self orientedDimensions:dimensions];
      if (dimensions.width == info->width && dimensions.height == info->height) {
        found_format = TRUE;
        [device setValue:f forKey:@"activeFormat"];
        for (NSObject *rate in [f valueForKey:@"videoSupportedFrameRateRanges"]) {
          gdouble min_frame_rate, max_frame_rate;

          [[rate valueForKey:@"minFrameRate"] getValue:&min_frame_rate];
          [[rate valueForKey:@"maxFrameRate"] getValue:&max_frame_rate];
          if ((framerate >= min_frame_rate - 0.00001) &&
              (framerate <= max_frame_rate + 0.00001)) {
            NSValue *frame_duration_value;
            found_framerate = TRUE;
            if (min_frame_rate == max_frame_rate) {
              /* on mac we get tight ranges and an exception is raised if the
               * frame duration doesn't match the one reported in the range to
               * the last decimal point
               */
              frame_duration_value = [rate valueForKey:@"minFrameDuration"];
            } else {
              // Invert fps_n and fps_d to get frame duration value and timescale (or numerator and denominator)
              frame_duration_value = [NSValue valueWithCMTime:CMTimeMake (info->fps_d, info->fps_n)];
            }
            [device setValue:frame_duration_value forKey:@"activeVideoMinFrameDuration"];
            @try {
              /* Only available on OSX >= 10.8 and iOS >= 7.0 */
              [device setValue:frame_duration_value forKey:@"activeVideoMaxFrameDuration"];
            } @catch (NSException *exception) {
              if (![[exception name] isEqualToString:NSUndefinedKeyException]) {
                GST_WARNING ("An unexcepted error occured: %s",
                              [exception.reason UTF8String]);
              }
            }
            break;
          }
        }
      }
    }
    if (!found_format) {
      GST_WARNING ("Unsupported capture dimensions %dx%d", info->width, info->height);
      return NO;
    }
    if (!found_framerate) {
      GST_WARNING ("Unsupported capture framerate %d/%d", info->fps_n, info->fps_d);
      return NO;
    }
  } else {
    GST_WARNING ("Couldn't lock device for configuration");
    return NO;
  }
  return YES;
}

- (BOOL)getSessionPresetCaps:(GstCaps *)result
{
  NSArray *pixel_formats = output.availableVideoCVPixelFormatTypes;
  for (NSNumber *pixel_format in pixel_formats) {
    GstVideoFormat gst_format = [self getGstVideoFormat:pixel_format];
    if (gst_format == GST_VIDEO_FORMAT_UNKNOWN)
      continue;

#if HAVE_IOS
    if ([session canSetSessionPreset:AVCaptureSessionPreset1920x1080])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gst_format, 1920, 1080, DEVICE_FPS_N, DEVICE_FPS_D));
#endif
    if ([session canSetSessionPreset:AVCaptureSessionPreset1280x720])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gst_format, 1280, 720, DEVICE_FPS_N, DEVICE_FPS_D));
    if ([session canSetSessionPreset:AVCaptureSessionPreset640x480])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gst_format, 640, 480, DEVICE_FPS_N, DEVICE_FPS_D));
    if ([session canSetSessionPreset:AVCaptureSessionPresetMedium])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gst_format, 480, 360, DEVICE_FPS_N, DEVICE_FPS_D));
    if ([session canSetSessionPreset:AVCaptureSessionPreset352x288])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gst_format, 352, 288, DEVICE_FPS_N, DEVICE_FPS_D));
    if ([session canSetSessionPreset:AVCaptureSessionPresetLow])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gst_format, 192, 144, DEVICE_FPS_N, DEVICE_FPS_D));
  }

  GST_LOG_OBJECT (element, "Session presets returned the following caps %" GST_PTR_FORMAT, result);

  return YES;
}

- (BOOL)setSessionPresetCaps:(GstVideoInfo *)info;
{
  GST_DEBUG_OBJECT (element, "Setting session presset caps");

  if ([device lockForConfiguration:NULL] != YES) {
    GST_WARNING ("Couldn't lock device for configuration");
    return NO;
  }

  switch (info->width) {
  case 192:
    session.sessionPreset = AVCaptureSessionPresetLow;
    break;
  case 352:
    session.sessionPreset = AVCaptureSessionPreset352x288;
    break;
  case 480:
    session.sessionPreset = AVCaptureSessionPresetMedium;
    break;
  case 640:
    session.sessionPreset = AVCaptureSessionPreset640x480;
    break;
  case 1280:
    session.sessionPreset = AVCaptureSessionPreset1280x720;
    break;
#if HAVE_IOS
  case 1920:
    session.sessionPreset = AVCaptureSessionPreset1920x1080;
    break;
#endif
  default:
    GST_WARNING ("Unsupported capture dimensions %dx%d", info->width, info->height);
    return NO;
  }
  return YES;
}

- (GstCaps *)getCaps
{
  GstCaps *result;
  NSArray *pixel_formats;

  if (session == nil)
    return NULL; /* BaseSrc will return template caps */

  result = gst_caps_new_empty ();
  pixel_formats = output.availableVideoCVPixelFormatTypes;

  if (captureScreen) {
#if !HAVE_IOS
    CGRect rect = CGDisplayBounds ([self getDisplayIdFromDeviceIndex]);
    float scale = [self getScaleFactorFromDeviceIndex];
    for (NSNumber *pixel_format in pixel_formats) {
      GstVideoFormat gst_format = [self getGstVideoFormat:pixel_format];
      if (gst_format != GST_VIDEO_FORMAT_UNKNOWN)
        gst_caps_append (result, gst_caps_new_simple ("video/x-raw",
            "width", G_TYPE_INT, (int)(rect.size.width * scale),
            "height", G_TYPE_INT, (int)(rect.size.height * scale),
            "format", G_TYPE_STRING, gst_video_format_to_string (gst_format),
            NULL));
    }
#else
    GST_WARNING ("Screen capture is not supported by iOS");
#endif
    return result;
  }

  @try {
    result = gst_caps_merge (result, [self getDeviceCaps]);
  } @catch (NSException *exception) {
    if (![[exception name] isEqualToString:NSUndefinedKeyException]) {
      GST_WARNING ("An unexcepted error occured: %s", [exception.reason UTF8String]);
      return result;
    }

    /* Fallback on session presets API for iOS < 7.0 */
    [self getSessionPresetCaps:result];
  }

  return result;
}

- (BOOL)setCaps:(GstCaps *)new_caps
{
  GstVideoInfo info;
  BOOL success = YES, *successPtr = &success;

  gst_video_info_init (&info);
  gst_video_info_from_caps (&info, new_caps);

  width = info.width;
  height = info.height;
  format = info.finfo->format;
  latency = gst_util_uint64_scale (GST_SECOND, info.fps_d, info.fps_n);

  dispatch_sync (mainQueue, ^{
    int newformat;

    if (captureScreen) {
#if !HAVE_IOS
      AVCaptureScreenInput *screenInput = (AVCaptureScreenInput *)input;
      screenInput.minFrameDuration = CMTimeMake(info.fps_d, info.fps_n);
#else
      GST_WARNING ("Screen capture is not supported by iOS");
      *successPtr = NO;
      return;
#endif
    } else {
      @try {

        /* formats and activeFormat keys are only available on OSX >= 10.7 and iOS >= 7.0 */
        *successPtr = [self setDeviceCaps:(GstVideoInfo *)&info];
        if (*successPtr != YES)
          return;

      } @catch (NSException *exception) {

        if (![[exception name] isEqualToString:NSUndefinedKeyException]) {
          GST_WARNING ("An unexcepted error occured: %s", [exception.reason UTF8String]);
          *successPtr = NO;
          return;
        }

        /* Fallback on session presets API for iOS < 7.0 */
        *successPtr = [self setSessionPresetCaps:(GstVideoInfo *)&info];
        if (*successPtr != YES)
          return;
      }
    }

    switch (format) {
      case GST_VIDEO_FORMAT_NV12:
        newformat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange;
        break;
      case GST_VIDEO_FORMAT_UYVY:
        newformat = kCVPixelFormatType_422YpCbCr8;
        break;
      case GST_VIDEO_FORMAT_YUY2:
        newformat = kCVPixelFormatType_422YpCbCr8_yuvs;
        break;
      case GST_VIDEO_FORMAT_BGRA:
        newformat = kCVPixelFormatType_32BGRA;
        break;
      default:
        *successPtr = NO;
        GST_WARNING ("Unsupported output format %s",
            gst_video_format_to_string (format));
        return;
    }

    GST_INFO_OBJECT (element,
        "width: %d height: %d format: %s", width, height,
        gst_video_format_to_string (format));

    output.videoSettings = [NSDictionary
        dictionaryWithObject:[NSNumber numberWithInt:newformat]
        forKey:(NSString*)kCVPixelBufferPixelFormatTypeKey];

    gst_caps_replace (&caps, new_caps);
    GST_INFO_OBJECT (element, "configured caps %"GST_PTR_FORMAT, caps);

    if (![session isRunning])
      [session startRunning];

    /* Unlock device configuration only after session is started so the session
     * won't reset the capture formats */
    [device unlockForConfiguration];
  });

  return success;
}

- (BOOL)start
{
  bufQueueLock = [[NSConditionLock alloc] initWithCondition:NO_BUFFERS];
  bufQueue = [[NSMutableArray alloc] initWithCapacity:BUFFER_QUEUE_SIZE];
  stopRequest = NO;

  offset = 0;
  latency = GST_CLOCK_TIME_NONE;

  lastSampling = GST_CLOCK_TIME_NONE;
  count = 0;
  fps = -1;

  return YES;
}

- (BOOL)stop
{
  dispatch_sync (mainQueue, ^{ [session stopRunning]; });
  dispatch_sync (workerQueue, ^{});

  bufQueueLock = nil;
  bufQueue = nil;

  if (textureCache)
    gst_video_texture_cache_free (textureCache);
  textureCache = NULL;

  if (ctxh)
    gst_gl_context_helper_free (ctxh);
  ctxh = NULL;

  return YES;
}

- (BOOL)query:(GstQuery *)query
{
  BOOL result = NO;

  if (GST_QUERY_TYPE (query) == GST_QUERY_LATENCY) {
    if (device != nil && caps != NULL) {
      GstClockTime min_latency, max_latency;

      min_latency = max_latency = latency;
      result = YES;

      GST_DEBUG_OBJECT (element, "reporting latency of min %" GST_TIME_FORMAT
          " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));
      gst_query_set_latency (query, TRUE, min_latency, max_latency);
    }
  } else {
    result = GST_BASE_SRC_CLASS (parent_class)->query (baseSrc, query);
  }

  return result;
}

- (BOOL)unlock
{
  [bufQueueLock lock];
  stopRequest = YES;
  [bufQueueLock unlockWithCondition:HAS_BUFFER_OR_STOP_REQUEST];

  return YES;
}

- (BOOL)unlockStop
{
  [bufQueueLock lock];
  stopRequest = NO;
  [bufQueueLock unlockWithCondition:([bufQueue count] == 0) ? NO_BUFFERS : HAS_BUFFER_OR_STOP_REQUEST];

  return YES;
}

- (GstStateChangeReturn)changeState:(GstStateChange)transition
{
  GstStateChangeReturn ret;

  if (transition == GST_STATE_CHANGE_NULL_TO_READY) {
    if (![self openDevice])
      return GST_STATE_CHANGE_FAILURE;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (transition == GST_STATE_CHANGE_READY_TO_NULL)
    [self closeDevice];

  return ret;
}

- (void)captureOutput:(AVCaptureOutput *)captureOutput
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)aConnection
{
  GstClockTime timestamp, duration;

  [bufQueueLock lock];

  if (stopRequest) {
    [bufQueueLock unlock];
    return;
  }

  [self getSampleBuffer:sampleBuffer timestamp:&timestamp duration:&duration];

  if (timestamp == GST_CLOCK_TIME_NONE) {
    [bufQueueLock unlockWithCondition:([bufQueue count] == 0) ? NO_BUFFERS : HAS_BUFFER_OR_STOP_REQUEST];
    return;
  }

  if ([bufQueue count] == BUFFER_QUEUE_SIZE)
    [bufQueue removeLastObject];

  [bufQueue insertObject:@{@"sbuf": (__bridge id)sampleBuffer,
                           @"timestamp": @(timestamp),
                           @"duration": @(duration)}
                 atIndex:0];

  [bufQueueLock unlockWithCondition:HAS_BUFFER_OR_STOP_REQUEST];
}

- (GstFlowReturn)create:(GstBuffer **)buf
{
  CMSampleBufferRef sbuf;
  CVImageBufferRef image_buf;
  CVPixelBufferRef pixel_buf;
  size_t cur_width, cur_height;
  GstClockTime timestamp, duration;

  [bufQueueLock lockWhenCondition:HAS_BUFFER_OR_STOP_REQUEST];
  if (stopRequest) {
    [bufQueueLock unlock];
    return GST_FLOW_FLUSHING;
  }

  NSDictionary *dic = (NSDictionary *) [bufQueue lastObject];
  sbuf = (__bridge CMSampleBufferRef) dic[@"sbuf"];
  timestamp = (GstClockTime) [dic[@"timestamp"] longLongValue];
  duration = (GstClockTime) [dic[@"duration"] longLongValue];
  CFRetain (sbuf);
  [bufQueue removeLastObject];
  [bufQueueLock unlockWithCondition:
      ([bufQueue count] == 0) ? NO_BUFFERS : HAS_BUFFER_OR_STOP_REQUEST];

  /* Check output frame size dimensions */
  image_buf = CMSampleBufferGetImageBuffer (sbuf);
  if (image_buf) {
    pixel_buf = (CVPixelBufferRef) image_buf;
    cur_width = CVPixelBufferGetWidth (pixel_buf);
    cur_height = CVPixelBufferGetHeight (pixel_buf);

    if (width != cur_width || height != cur_height) {
      /* Set new caps according to current frame dimensions */
      GST_WARNING ("Output frame size has changed %dx%d -> %dx%d, updating caps",
          width, height, (int)cur_width, (int)cur_height);
      width = cur_width;
      height = cur_height;
      gst_caps_set_simple (caps,
        "width", G_TYPE_INT, width,
        "height", G_TYPE_INT, height,
        NULL);
      gst_pad_push_event (GST_BASE_SINK_PAD (baseSrc), gst_event_new_caps (caps));
    }
  }

  *buf = gst_core_media_buffer_new (sbuf, useVideoMeta, textureCache);
  if (*buf == NULL) {
    CFRelease (sbuf);
    return GST_FLOW_ERROR;
  }
  CFRelease (sbuf);

  GST_BUFFER_OFFSET (*buf) = offset++;
  GST_BUFFER_OFFSET_END (*buf) = GST_BUFFER_OFFSET (*buf) + 1;
  GST_BUFFER_TIMESTAMP (*buf) = timestamp;
  GST_BUFFER_DURATION (*buf) = duration;

  if (doStats)
    [self updateStatistics];

  return GST_FLOW_OK;
}

- (GstCaps *)fixate:(GstCaps *)new_caps
{
  GstStructure *structure;

  new_caps = gst_caps_make_writable (new_caps);
  new_caps = gst_caps_truncate (new_caps);
  structure = gst_caps_get_structure (new_caps, 0);
  /* crank up to 11. This is what the presets do, but we don't use the presets
   * in ios >= 7.0 */
  gst_structure_fixate_field_nearest_int (structure, "height", G_MAXINT);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", G_MAXINT, 1);

  return gst_caps_fixate (new_caps);
}

- (BOOL)decideAllocation:(GstQuery *)query
{
  GstCaps *alloc_caps;
  GstCapsFeatures *features;
  gboolean ret;

  ret = GST_BASE_SRC_CLASS (parent_class)->decide_allocation (baseSrc, query);
  if (!ret)
    return ret;

  gst_query_parse_allocation (query, &alloc_caps, NULL);
  features = gst_caps_get_features (alloc_caps, 0);
  if (gst_caps_features_contains (features, GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
    gst_gl_context_helper_ensure_context (ctxh);
    GST_INFO_OBJECT (element, "pushing textures, context %p old context %p",
        ctxh->context, textureCache ? textureCache->ctx : NULL);
    if (textureCache && textureCache->ctx != ctxh->context) {
      gst_video_texture_cache_free (textureCache);
      textureCache = NULL;
    }
    textureCache = gst_video_texture_cache_new (ctxh->context);
    gst_video_texture_cache_set_format (textureCache, format, alloc_caps);
  }

  return TRUE;
}

- (void)setContext:(GstContext *)context
{
  GST_INFO_OBJECT (element, "setting context %s",
          gst_context_get_context_type (context));
  gst_gl_handle_set_context (element, context,
          &ctxh->display, &ctxh->other_context);
  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

- (void)getSampleBuffer:(CMSampleBufferRef)sbuf
              timestamp:(GstClockTime *)outTimestamp
               duration:(GstClockTime *)outDuration
{
  CMSampleTimingInfo time_info;
  GstClockTime timestamp, avf_timestamp, duration, input_clock_now, input_clock_diff, running_time;
  CMItemCount num_timings;
  GstClock *clock;
  CMTime now;

  timestamp = GST_CLOCK_TIME_NONE;
  duration = GST_CLOCK_TIME_NONE;
  if (CMSampleBufferGetOutputSampleTimingInfoArray(sbuf, 1, &time_info, &num_timings) == noErr) {
    avf_timestamp = gst_util_uint64_scale (GST_SECOND,
            time_info.presentationTimeStamp.value, time_info.presentationTimeStamp.timescale);

    if (CMTIME_IS_VALID (time_info.duration) && time_info.duration.timescale != 0)
      duration = gst_util_uint64_scale (GST_SECOND,
          time_info.duration.value, time_info.duration.timescale);

    now = CMClockGetTime(inputClock);
    input_clock_now = gst_util_uint64_scale (GST_SECOND,
        now.value, now.timescale);
    input_clock_diff = input_clock_now - avf_timestamp;

    GST_OBJECT_LOCK (element);
    clock = GST_ELEMENT_CLOCK (element);
    if (clock) {
      running_time = gst_clock_get_time (clock) - element->base_time;
      /* We use presentationTimeStamp to determine how much time it took
       * between capturing and receiving the frame in our delegate
       * (e.g. how long it spent in AVF queues), then we subtract that time
       * from our running time to get the actual timestamp.
       */
      if (running_time >= input_clock_diff)
        timestamp = running_time - input_clock_diff;
      else
        timestamp = running_time;

      GST_DEBUG_OBJECT (element, "AVF clock: %"GST_TIME_FORMAT ", AVF PTS: %"GST_TIME_FORMAT
          ", AVF clock diff: %"GST_TIME_FORMAT
          ", running time: %"GST_TIME_FORMAT ", out PTS: %"GST_TIME_FORMAT,
          GST_TIME_ARGS (input_clock_now), GST_TIME_ARGS (avf_timestamp),
          GST_TIME_ARGS (input_clock_diff),
          GST_TIME_ARGS (running_time), GST_TIME_ARGS (timestamp));
    } else {
      /* no clock, can't set timestamps */
      timestamp = GST_CLOCK_TIME_NONE;
    }
    GST_OBJECT_UNLOCK (element);
  }

  *outTimestamp = timestamp;
  *outDuration = duration;
}

- (void)updateStatistics
{
  GstClock *clock;

  GST_OBJECT_LOCK (element);
  clock = GST_ELEMENT_CLOCK (element);
  if (clock != NULL)
    gst_object_ref (clock);
  GST_OBJECT_UNLOCK (element);

  if (clock != NULL) {
    GstClockTime now = gst_clock_get_time (clock);
    gst_object_unref (clock);

    count++;

    if (GST_CLOCK_TIME_IS_VALID (lastSampling)) {
      if (now - lastSampling >= GST_SECOND) {
        GST_OBJECT_LOCK (element);
        fps = count;
        GST_OBJECT_UNLOCK (element);

        g_object_notify (G_OBJECT (element), "fps");

        lastSampling = now;
        count = 0;
      }
    } else {
      lastSampling = now;
    }
  }
}

@end

/*
 * Glue code
 */

enum
{
  PROP_0,
  PROP_DEVICE_INDEX,
  PROP_POSITION,
  PROP_ORIENTATION,
  PROP_DEVICE_TYPE,
  PROP_DO_STATS,
  PROP_FPS,
#if !HAVE_IOS
  PROP_CAPTURE_SCREEN,
  PROP_CAPTURE_SCREEN_CURSOR,
  PROP_CAPTURE_SCREEN_MOUSE_CLICKS,
#endif
};


static void gst_avf_video_src_finalize (GObject * obj);
static void gst_avf_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_avf_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_avf_video_src_change_state (
    GstElement * element, GstStateChange transition);
static GstCaps * gst_avf_video_src_get_caps (GstBaseSrc * basesrc,
    GstCaps * filter);
static gboolean gst_avf_video_src_set_caps (GstBaseSrc * basesrc,
    GstCaps * caps);
static gboolean gst_avf_video_src_start (GstBaseSrc * basesrc);
static gboolean gst_avf_video_src_stop (GstBaseSrc * basesrc);
static gboolean gst_avf_video_src_query (GstBaseSrc * basesrc,
    GstQuery * query);
static gboolean gst_avf_video_src_unlock (GstBaseSrc * basesrc);
static gboolean gst_avf_video_src_unlock_stop (GstBaseSrc * basesrc);
static GstFlowReturn gst_avf_video_src_create (GstPushSrc * pushsrc,
    GstBuffer ** buf);
static GstCaps * gst_avf_video_src_fixate (GstBaseSrc * bsrc,
    GstCaps * caps);
static gboolean gst_avf_video_src_decide_allocation (GstBaseSrc * bsrc,
    GstQuery * query);
static void gst_avf_video_src_set_context (GstElement * element,
        GstContext * context);

static void
gst_avf_video_src_class_init (GstAVFVideoSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->finalize = gst_avf_video_src_finalize;
  gobject_class->get_property = gst_avf_video_src_get_property;
  gobject_class->set_property = gst_avf_video_src_set_property;

  gstelement_class->change_state = gst_avf_video_src_change_state;
  gstelement_class->set_context = gst_avf_video_src_set_context;

  gstbasesrc_class->get_caps = gst_avf_video_src_get_caps;
  gstbasesrc_class->set_caps = gst_avf_video_src_set_caps;
  gstbasesrc_class->start = gst_avf_video_src_start;
  gstbasesrc_class->stop = gst_avf_video_src_stop;
  gstbasesrc_class->query = gst_avf_video_src_query;
  gstbasesrc_class->unlock = gst_avf_video_src_unlock;
  gstbasesrc_class->unlock_stop = gst_avf_video_src_unlock_stop;
  gstbasesrc_class->fixate = gst_avf_video_src_fixate;
  gstbasesrc_class->decide_allocation = gst_avf_video_src_decide_allocation;

  gstpushsrc_class->create = gst_avf_video_src_create;

  gst_element_class_set_metadata (gstelement_class,
      "Video Source (AVFoundation)", "Source/Video",
      "Reads frames from an iOS AVFoundation device",
      "Ole André Vadla Ravnås <oleavr@soundrop.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &src_template);

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "Device Index",
          "The zero-based device index",
          -1, G_MAXINT, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_POSITION,
                                   g_param_spec_enum ("position", "Position",
                                                      "The position of the capture device (front or back-facing)",
                                                      GST_TYPE_AVF_VIDEO_SOURCE_POSITION, DEFAULT_POSITION,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_ORIENTATION,
                                   g_param_spec_enum ("orientation", "Orientation",
                                                      "The orientation of the video",
                                                      GST_TYPE_AVF_VIDEO_SOURCE_ORIENTATION, DEFAULT_ORIENTATION,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEVICE_TYPE,
                                   g_param_spec_enum ("device-type", "Device Type",
                                                      "The general type of a video capture device",
                                                      GST_TYPE_AVF_VIDEO_SOURCE_DEVICE_TYPE, DEFAULT_DEVICE_TYPE,
                                                      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DO_STATS,
      g_param_spec_boolean ("do-stats", "Enable statistics",
          "Enable logging of statistics", DEFAULT_DO_STATS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FPS,
      g_param_spec_int ("fps", "Frames per second",
          "Last measured framerate, if statistics are enabled",
          -1, G_MAXINT, -1, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
#if !HAVE_IOS
  g_object_class_install_property (gobject_class, PROP_CAPTURE_SCREEN,
      g_param_spec_boolean ("capture-screen", "Enable screen capture",
          "Enable screen capture functionality", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CAPTURE_SCREEN_CURSOR,
      g_param_spec_boolean ("capture-screen-cursor", "Capture screen cursor",
          "Enable cursor capture while capturing screen", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CAPTURE_SCREEN_MOUSE_CLICKS,
      g_param_spec_boolean ("capture-screen-mouse-clicks", "Enable mouse clicks capture",
          "Enable mouse clicks capture while capturing screen", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
#endif

  GST_DEBUG_CATEGORY_INIT (gst_avf_video_src_debug, "avfvideosrc",
      0, "iOS AVFoundation video source");
}

static void
gst_avf_video_src_init (GstAVFVideoSrc * src)
{
  src->impl = (__bridge_retained gpointer)[[GstAVFVideoSrcImpl alloc] initWithSrc:GST_PUSH_SRC (src)];
}

static void
gst_avf_video_src_finalize (GObject * obj)
{
  CFBridgingRelease(GST_AVF_VIDEO_SRC_CAST(obj)->impl);

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_avf_video_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAVFVideoSrcImpl *impl = GST_AVF_VIDEO_SRC_IMPL (object);

  switch (prop_id) {
#if !HAVE_IOS
    case PROP_CAPTURE_SCREEN:
      g_value_set_boolean (value, impl.captureScreen);
      break;
    case PROP_CAPTURE_SCREEN_CURSOR:
      g_value_set_boolean (value, impl.captureScreenCursor);
      break;
    case PROP_CAPTURE_SCREEN_MOUSE_CLICKS:
      g_value_set_boolean (value, impl.captureScreenMouseClicks);
      break;
#endif
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, impl.deviceIndex);
      break;
    case PROP_POSITION:
      g_value_set_enum(value, impl.position);
      break;
    case PROP_ORIENTATION:
      g_value_set_enum(value, impl.orientation);
      break;
    case PROP_DEVICE_TYPE:
      g_value_set_enum(value, impl.deviceType);
      break;
    case PROP_DO_STATS:
      g_value_set_boolean (value, impl.doStats);
      break;
    case PROP_FPS:
      GST_OBJECT_LOCK (object);
      g_value_set_int (value, impl.fps);
      GST_OBJECT_UNLOCK (object);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_avf_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstAVFVideoSrcImpl *impl = GST_AVF_VIDEO_SRC_IMPL (object);

  switch (prop_id) {
#if !HAVE_IOS
    case PROP_CAPTURE_SCREEN:
      impl.captureScreen = g_value_get_boolean (value);
      break;
    case PROP_CAPTURE_SCREEN_CURSOR:
      impl.captureScreenCursor = g_value_get_boolean (value);
      break;
    case PROP_CAPTURE_SCREEN_MOUSE_CLICKS:
      impl.captureScreenMouseClicks = g_value_get_boolean (value);
      break;
#endif
    case PROP_DEVICE_INDEX:
      impl.deviceIndex = g_value_get_int (value);
      break;
    case PROP_POSITION:
      impl.position = g_value_get_enum(value);
      break;
    case PROP_ORIENTATION:
      impl.orientation = g_value_get_enum(value);
      break;
    case PROP_DEVICE_TYPE:
      impl.deviceType = g_value_get_enum(value);
      break;
    case PROP_DO_STATS:
      impl.doStats = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_avf_video_src_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (element) changeState: transition];

  return ret;
}

static GstCaps *
gst_avf_video_src_get_caps (GstBaseSrc * basesrc, GstCaps * filter)
{
  GstCaps *ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) getCaps];

  return ret;
}

static gboolean
gst_avf_video_src_set_caps (GstBaseSrc * basesrc, GstCaps * caps)
{
  gboolean ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) setCaps:caps];

  return ret;
}

static gboolean
gst_avf_video_src_start (GstBaseSrc * basesrc)
{
  gboolean ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) start];

  return ret;
}

static gboolean
gst_avf_video_src_stop (GstBaseSrc * basesrc)
{
  gboolean ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) stop];

  return ret;
}

static gboolean
gst_avf_video_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  gboolean ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) query:query];

  return ret;
}

static gboolean
gst_avf_video_src_unlock (GstBaseSrc * basesrc)
{
  gboolean ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) unlock];

  return ret;
}

static gboolean
gst_avf_video_src_unlock_stop (GstBaseSrc * basesrc)
{
  gboolean ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) unlockStop];

  return ret;
}

static GstFlowReturn
gst_avf_video_src_create (GstPushSrc * pushsrc, GstBuffer ** buf)
{
  GstFlowReturn ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (pushsrc) create: buf];

  return ret;
}


static GstCaps *
gst_avf_video_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstCaps *ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (bsrc) fixate:caps];

  return ret;
}

static gboolean
gst_avf_video_src_decide_allocation (GstBaseSrc * bsrc,
    GstQuery * query)
{
  gboolean ret;

  ret = [GST_AVF_VIDEO_SRC_IMPL (bsrc) decideAllocation:query];

  return ret;
}

static void
gst_avf_video_src_set_context (GstElement * element, GstContext * context)
{
  [GST_AVF_VIDEO_SRC_IMPL (element) setContext:context];
}
