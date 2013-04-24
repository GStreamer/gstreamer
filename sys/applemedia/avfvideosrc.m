/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@soundrop.com>
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

#include "avfvideosrc.h"

#import <AVFoundation/AVFoundation.h>
#include <gst/video/video.h>
#include "coremediabuffer.h"

#define DEFAULT_DEVICE_INDEX  -1
#define DEFAULT_DO_STATS      FALSE

#define DEVICE_FPS_N          25
#define DEVICE_FPS_D          1

#define BUFFER_QUEUE_SIZE     2

GST_DEBUG_CATEGORY (gst_avf_video_src_debug);
#define GST_CAT_DEFAULT gst_avf_video_src_debug

#define VIDEO_CAPS_YUV(width, height) "video/x-raw-yuv, "       \
    "format = (fourcc) { NV12, UYVY, YUY2 }, "                  \
    "framerate = " GST_VIDEO_FPS_RANGE ", "                     \
    "width = (int) " G_STRINGIFY (width) ", height = (int) " G_STRINGIFY (height)

#define VIDEO_CAPS_BGRA(width, height) "video/x-raw-rgb, "      \
    "bpp = (int) 32, "                                          \
    "depth = (int) 32, "                                        \
    "endianness = (int) BIG_ENDIAN, "                           \
    "red_mask = (int) " GST_VIDEO_BYTE3_MASK_32 ", "            \
    "green_mask = (int) " GST_VIDEO_BYTE2_MASK_32 ", "          \
    "blue_mask = (int) " GST_VIDEO_BYTE1_MASK_32 ", "           \
    "alpha_mask = (int) " GST_VIDEO_BYTE4_MASK_32 ", "          \
    "width = (int) " G_STRINGIFY (width) ", height = (int) " G_STRINGIFY (height)

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (VIDEO_CAPS_YUV (192, 144) ";"
        VIDEO_CAPS_YUV (480, 360) ";"
        VIDEO_CAPS_YUV (352, 288) ";"
        VIDEO_CAPS_YUV (640, 480) ";"
        VIDEO_CAPS_YUV (1280, 720) ";"
        VIDEO_CAPS_YUV (1920, 1280) ";"
        VIDEO_CAPS_BGRA (192, 144) ";"
        VIDEO_CAPS_BGRA (480, 360) ";"
        VIDEO_CAPS_BGRA (352, 288) ";"
        VIDEO_CAPS_BGRA (640, 480) ";"
        VIDEO_CAPS_BGRA (1280, 720) ";"
        VIDEO_CAPS_BGRA (1920, 1280))
);

typedef enum _QueueState {
  NO_BUFFERS = 1,
  HAS_BUFFER_OR_STOP_REQUEST,
} QueueState;

static GstPushSrcClass * parent_class;

@interface GstAVFVideoSrcImpl : NSObject <AVCaptureVideoDataOutputSampleBufferDelegate> {
  GstElement *element;
  GstBaseSrc *baseSrc;
  GstPushSrc *pushSrc;

  gint deviceIndex;
  BOOL doStats;

  AVCaptureSession *session;
  AVCaptureDeviceInput *input;
  AVCaptureVideoDataOutput *output;
  AVCaptureDevice *device;

  dispatch_queue_t mainQueue;
  dispatch_queue_t workerQueue;
  NSConditionLock *bufQueueLock;
  NSMutableArray *bufQueue;
  BOOL stopRequest;

  GstVideoFormat format;
  gint width, height;
  GstClockTime duration;
  guint64 offset;

  GstClockTime lastSampling;
  guint count;
  gint fps;
}

- (id)init;
- (id)initWithSrc:(GstPushSrc *)src;
- (void)finalize;

@property int deviceIndex;
@property BOOL doStats;
@property int fps;

- (BOOL)openDevice;
- (void)closeDevice;
- (GstCaps *)getCaps;
- (BOOL)setCaps:(GstCaps *)caps;
- (BOOL)start;
- (BOOL)stop;
- (BOOL)unlock;
- (BOOL)unlockStop;
- (BOOL)query:(GstQuery *)query;
- (GstStateChangeReturn)changeState:(GstStateChange)transition;
- (GstFlowReturn)create:(GstBuffer **)buf;
- (void)timestampBuffer:(GstBuffer *)buf;
- (void)updateStatistics;
- (void)captureOutput:(AVCaptureOutput *)captureOutput
didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer
       fromConnection:(AVCaptureConnection *)connection;

- (void)waitForMainQueueToDrain;
- (void)waitForWorkerQueueToDrain;
- (void)waitForQueueToDrain:(dispatch_queue_t)dispatchQueue;

@end

@implementation GstAVFVideoSrcImpl

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

    mainQueue = dispatch_get_main_queue ();
    workerQueue =
        dispatch_queue_create ("org.freedesktop.gstreamer.avfvideosrc", NULL);

    gst_base_src_set_live (baseSrc, TRUE);
    gst_base_src_set_format (baseSrc, GST_FORMAT_TIME);
  }

  return self;
}

- (void)finalize
{
  mainQueue = NULL;
  dispatch_release (workerQueue);
  workerQueue = NULL;

  [super finalize];
}

@synthesize deviceIndex, doStats, fps;

- (BOOL)openDevice
{
  BOOL success = NO, *successPtr = &success;

  dispatch_async (mainQueue, ^{
    NSString *mediaType = AVMediaTypeVideo;
    NSError *err;

    if (deviceIndex == -1) {
      device = [AVCaptureDevice defaultDeviceWithMediaType:mediaType];
      if (device == nil) {
        GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
                           ("No video capture devices found"), (NULL));
        return;
      }
    } else {
      NSArray *devices = [AVCaptureDevice devicesWithMediaType:mediaType];
      if (deviceIndex >= [devices count]) {
        GST_ELEMENT_ERROR (element, RESOURCE, NOT_FOUND,
                           ("Invalid video capture device index"), (NULL));
        return;
      }
      device = [devices objectAtIndex:deviceIndex];
    }
    g_assert (device != nil);
    [device retain];

    GST_INFO ("Opening '%s'", [[device localizedName] UTF8String]);

    input = [AVCaptureDeviceInput deviceInputWithDevice:device
                                                  error:&err];
    if (input == nil) {
      GST_ELEMENT_ERROR (element, RESOURCE, BUSY,
          ("Failed to open device: %s",
           [[err localizedDescription] UTF8String]),
          (NULL));
      [device release];
      device = nil;
      return;
    }
    [input retain];

    output = [[AVCaptureVideoDataOutput alloc] init];
    [output setSampleBufferDelegate:self
                              queue:workerQueue];
    output.alwaysDiscardsLateVideoFrames = YES;
    output.minFrameDuration = kCMTimeZero; /* unlimited */
    output.videoSettings = nil; /* device native format */

    session = [[AVCaptureSession alloc] init];
    [session addInput:input];
    [session addOutput:output];

    *successPtr = YES;
  });
  [self waitForMainQueueToDrain];

  return success;
}

- (void)closeDevice
{
  dispatch_async (mainQueue, ^{
    g_assert (![session isRunning]);

    [session removeInput:input];
    [session removeOutput:output];

    [session release];
    session = nil;

    [input release];
    input = nil;

    [output release];
    output = nil;

    [device release];
    device = nil;
  });
  [self waitForMainQueueToDrain];
}

#define GST_AVF_CAPS_NEW(format, w, h)                    \
    (gst_video_format_new_caps (format, w, h, \
                                DEVICE_FPS_N, DEVICE_FPS_D, 1, 1))

- (GstCaps *)getCaps
{
  GstCaps *result;
  NSArray *formats;

  if (session == nil)
    return NULL; /* BaseSrc will return template caps */
 
  result = gst_caps_new_empty ();

  formats = output.availableVideoCVPixelFormatTypes;
  for (id object in formats) {
    NSNumber *nsformat = object;
    GstVideoFormat gstformat = GST_VIDEO_FORMAT_UNKNOWN;

    switch ([nsformat integerValue]) {
    case kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange: /* 420v */
      gstformat = GST_VIDEO_FORMAT_NV12;
      break;
    case kCVPixelFormatType_422YpCbCr8: /* 2vuy */
      gstformat = GST_VIDEO_FORMAT_UYVY;
      break;
    case kCVPixelFormatType_32BGRA: /* BGRA */
      gstformat = GST_VIDEO_FORMAT_BGRA;
      break;
    case kCVPixelFormatType_422YpCbCr8_yuvs: /* yuvs */
      gstformat = GST_VIDEO_FORMAT_YUY2;
      break;
    default:
      continue;
    }

    gst_caps_append (result, GST_AVF_CAPS_NEW (gstformat, 192, 144));
    if ([session canSetSessionPreset:AVCaptureSessionPreset352x288])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gstformat, 352, 288));
    if ([session canSetSessionPreset:AVCaptureSessionPresetMedium])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gstformat, 480, 360));
    if ([session canSetSessionPreset:AVCaptureSessionPreset640x480]) 
      gst_caps_append (result, GST_AVF_CAPS_NEW (gstformat, 640, 480));
    if ([session canSetSessionPreset:AVCaptureSessionPreset1280x720])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gstformat, 1280, 720));
    if ([session canSetSessionPreset:AVCaptureSessionPreset1920x1080])
      gst_caps_append (result, GST_AVF_CAPS_NEW (gstformat, 1920, 1080));
  }

  return result;
}

- (BOOL)setCaps:(GstCaps *)caps
{
  gst_video_format_parse_caps (caps, &format, &width, &height);

  dispatch_async (mainQueue, ^{
    int newformat;

    g_assert (![session isRunning]);

    switch (width) {
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
      case 1920:
        session.sessionPreset = AVCaptureSessionPreset1920x1080;
        break;
      default:
        g_assert_not_reached ();
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
        g_assert_not_reached ();
    }

    GST_DEBUG_OBJECT(element,
       "Width: %d Height: %d Format: %" GST_FOURCC_FORMAT,
       width, height,
       GST_FOURCC_ARGS (gst_video_format_to_fourc(format)));


    output.videoSettings = [NSDictionary dictionaryWithObject:[NSNumber numberWithInt:newformat] forKey:(NSString*)kCVPixelBu

    [session startRunning];
  });
  [self waitForMainQueueToDrain];

  return YES;
}

- (BOOL)start
{
  bufQueueLock = [[NSConditionLock alloc] initWithCondition:NO_BUFFERS];
  bufQueue = [[NSMutableArray alloc] initWithCapacity:BUFFER_QUEUE_SIZE];
  stopRequest = NO;

  duration = gst_util_uint64_scale (GST_SECOND, DEVICE_FPS_D, DEVICE_FPS_N);
  offset = 0;

  lastSampling = GST_CLOCK_TIME_NONE;
  count = 0;
  fps = -1;

  return YES;
}

- (BOOL)stop
{
  dispatch_async (mainQueue, ^{ [session stopRunning]; });
  [self waitForMainQueueToDrain];
  [self waitForWorkerQueueToDrain];

  [bufQueueLock release];
  bufQueueLock = nil;
  [bufQueue release];
  bufQueue = nil;

  return YES;
}

- (BOOL)query:(GstQuery *)query
{
  BOOL result = NO;

  if (GST_QUERY_TYPE (query) == GST_QUERY_LATENCY) {
    if (device != nil) {
      GstClockTime min_latency, max_latency;

      min_latency = max_latency = duration; /* for now */
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
  [bufQueueLock unlock];

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
       fromConnection:(AVCaptureConnection *)connection
{
  [bufQueueLock lock];

  if (stopRequest) {
    [bufQueueLock unlock];
    return;
  }

  if ([bufQueue count] == BUFFER_QUEUE_SIZE)
    [bufQueue removeLastObject];

  [bufQueue insertObject:(id)sampleBuffer
                 atIndex:0];

  [bufQueueLock unlockWithCondition:HAS_BUFFER_OR_STOP_REQUEST];
}

- (GstFlowReturn)create:(GstBuffer **)buf
{
  CMSampleBufferRef sbuf;

  [bufQueueLock lockWhenCondition:HAS_BUFFER_OR_STOP_REQUEST];
  if (stopRequest) {
    [bufQueueLock unlock];
    return GST_FLOW_FLUSHING;
  }

  sbuf = (CMSampleBufferRef) [bufQueue lastObject];
  CFRetain (sbuf);
  [bufQueue removeLastObject];
  [bufQueueLock unlockWithCondition:
      ([bufQueue count] == 0) ? NO_BUFFERS : HAS_BUFFER_OR_STOP_REQUEST];

  *buf = gst_core_media_buffer_new (sbuf);
  CFRelease (sbuf);

  [self timestampBuffer:*buf];

  if (doStats)
    [self updateStatistics];

  return GST_FLOW_OK;
}

- (void)timestampBuffer:(GstBuffer *)buf
{
  GstClock *clock;
  GstClockTime timestamp;

  GST_OBJECT_LOCK (element);
  clock = GST_ELEMENT_CLOCK (element);
  if (clock != NULL) {
    gst_object_ref (clock);
    timestamp = element->base_time;
  } else {
    timestamp = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (element);

  if (clock != NULL) {
    timestamp = gst_clock_get_time (clock) - timestamp;
    if (timestamp > duration)
      timestamp -= duration;
    else
      timestamp = 0;

    gst_object_unref (clock);
    clock = NULL;
  }

  GST_BUFFER_OFFSET (buf) = offset++;
  GST_BUFFER_OFFSET_END (buf) = GST_BUFFER_OFFSET (buf) + 1;
  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;
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

- (void)waitForMainQueueToDrain
{
  [self waitForQueueToDrain:mainQueue];
}

- (void)waitForWorkerQueueToDrain
{
  [self waitForQueueToDrain:workerQueue];
}

- (void)waitForQueueToDrain:(dispatch_queue_t)dispatchQueue
{
  if (dispatchQueue != dispatch_get_current_queue())
      dispatch_sync (dispatchQueue, ^{});
}

@end

/*
 * Glue code
 */

enum
{
  PROP_0,
  PROP_DEVICE_INDEX,
  PROP_DO_STATS,
  PROP_FPS
};

GST_BOILERPLATE (GstAVFVideoSrc, gst_avf_video_src, GstPushSrc,
    GST_TYPE_PUSH_SRC);

static void gst_avf_video_src_finalize (GObject * obj);
static void gst_avf_video_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_avf_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_avf_video_src_change_state (
    GstElement * element, GstStateChange transition);
static GstCaps * gst_avf_video_src_get_caps (GstBaseSrc * basesrc);
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

static void
gst_avf_video_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_metadata (element_class,
      "Video Source (AVFoundation)", "Source/Video",
      "Reads frames from an iOS AVFoundation device",
      "Ole André Vadla Ravnås <oleavr@soundrop.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

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

  gstbasesrc_class->get_caps = gst_avf_video_src_get_caps;
  gstbasesrc_class->set_caps = gst_avf_video_src_set_caps;
  gstbasesrc_class->start = gst_avf_video_src_start;
  gstbasesrc_class->stop = gst_avf_video_src_stop;
  gstbasesrc_class->query = gst_avf_video_src_query;
  gstbasesrc_class->unlock = gst_avf_video_src_unlock;
  gstbasesrc_class->unlock_stop = gst_avf_video_src_unlock_stop;

  gstpushsrc_class->create = gst_avf_video_src_create;

  g_object_class_install_property (gobject_class, PROP_DEVICE_INDEX,
      g_param_spec_int ("device-index", "Device Index",
          "The zero-based device index",
          -1, G_MAXINT, DEFAULT_DEVICE_INDEX,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DO_STATS,
      g_param_spec_boolean ("do-stats", "Enable statistics",
          "Enable logging of statistics", DEFAULT_DO_STATS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FPS,
      g_param_spec_int ("fps", "Frames per second",
          "Last measured framerate, if statistics are enabled",
          -1, G_MAXINT, -1, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_avf_video_src_debug, "avfvideosrc",
      0, "iOS AVFoundation video source");
}

#define OBJC_CALLOUT_BEGIN() \
  NSAutoreleasePool *pool; \
  \
  pool = [[NSAutoreleasePool alloc] init]
#define OBJC_CALLOUT_END() \
  [pool release]

static void
gst_avf_video_src_init (GstAVFVideoSrc * src, GstAVFVideoSrcClass * gclass)
{
  OBJC_CALLOUT_BEGIN ();
  src->impl = [[GstAVFVideoSrcImpl alloc] initWithSrc:GST_PUSH_SRC (src)];
  OBJC_CALLOUT_END ();
}

static void
gst_avf_video_src_finalize (GObject * obj)
{
  OBJC_CALLOUT_BEGIN ();
  [GST_AVF_VIDEO_SRC_IMPL (obj) release];
  OBJC_CALLOUT_END ();

  G_OBJECT_CLASS (parent_class)->finalize (obj);
}

static void
gst_avf_video_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstAVFVideoSrcImpl *impl = GST_AVF_VIDEO_SRC_IMPL (object);

  switch (prop_id) {
    case PROP_DEVICE_INDEX:
      g_value_set_int (value, impl.deviceIndex);
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
    case PROP_DEVICE_INDEX:
      impl.deviceIndex = g_value_get_int (value);
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

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (element) changeState: transition];
  OBJC_CALLOUT_END ();

  return ret;
}

static GstCaps *
gst_avf_video_src_get_caps (GstBaseSrc * basesrc)
{
  GstCaps *ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) getCaps];
  OBJC_CALLOUT_END ();

  return ret;
}

static gboolean
gst_avf_video_src_set_caps (GstBaseSrc * basesrc, GstCaps * caps)
{
  gboolean ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) setCaps:caps];
  OBJC_CALLOUT_END ();

  return ret;
}

static gboolean
gst_avf_video_src_start (GstBaseSrc * basesrc)
{
  gboolean ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) start];
  OBJC_CALLOUT_END ();

  return ret;
}

static gboolean
gst_avf_video_src_stop (GstBaseSrc * basesrc)
{
  gboolean ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) stop];
  OBJC_CALLOUT_END ();

  return ret;
}

static gboolean
gst_avf_video_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  gboolean ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) query:query];
  OBJC_CALLOUT_END ();

  return ret;
}

static gboolean
gst_avf_video_src_unlock (GstBaseSrc * basesrc)
{
  gboolean ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) unlock];
  OBJC_CALLOUT_END ();

  return ret;
}

static gboolean
gst_avf_video_src_unlock_stop (GstBaseSrc * basesrc)
{
  gboolean ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (basesrc) unlockStop];
  OBJC_CALLOUT_END ();

  return ret;
}

static GstFlowReturn
gst_avf_video_src_create (GstPushSrc * pushsrc, GstBuffer ** buf)
{
  GstFlowReturn ret;

  OBJC_CALLOUT_BEGIN ();
  ret = [GST_AVF_VIDEO_SRC_IMPL (pushsrc) create: buf];
  OBJC_CALLOUT_END ();

  return ret;
}
