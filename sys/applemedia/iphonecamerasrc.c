/*
 * Copyright (C) 2010 Ole André Vadla Ravnås <oleavr@gmail.com>
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

#include "iphonecamerasrc.h"

#include "coremediabuffer.h"

#include <gst/video/video.h>

#define DEFAULT_DO_STATS FALSE

#define BUFQUEUE_LOCK(instance) GST_OBJECT_LOCK (instance)
#define BUFQUEUE_UNLOCK(instance) GST_OBJECT_UNLOCK (instance)
#define BUFQUEUE_WAIT(instance) \
    g_cond_wait (instance->cond, GST_OBJECT_GET_LOCK (instance))
#define BUFQUEUE_NOTIFY(instance) g_cond_signal (instance->cond)

GST_DEBUG_CATEGORY (gst_iphone_camera_src_debug);
#define GST_CAT_DEFAULT gst_iphone_camera_src_debug

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("YUY2") ";"
        GST_VIDEO_CAPS_YUV ("I420"))
    );

enum
{
  PROP_0,
  PROP_DO_STATS
};

typedef struct
{
  guint index;

  GstVideoFormat video_format;
  guint32 fourcc;
  gint width;
  gint height;
  gint fps_n;
  gint fps_d;
} GstIPhoneCameraFormat;

static gboolean gst_iphone_camera_src_open_device (GstIPhoneCameraSrc * self);
static void gst_iphone_camera_src_close_device (GstIPhoneCameraSrc * self);
static void gst_iphone_camera_src_ensure_device_caps_and_formats
    (GstIPhoneCameraSrc * self);
static void gst_iphone_camera_src_release_device_caps_and_formats
    (GstIPhoneCameraSrc * self);
static gboolean gst_iphone_camera_src_select_format (GstIPhoneCameraSrc * self,
    GstIPhoneCameraFormat * format);

static gboolean gst_iphone_camera_src_parse_imager_format
    (GstIPhoneCameraSrc * self, guint index, CFDictionaryRef imager_format,
    GstIPhoneCameraFormat * format);
static FigStatus gst_iphone_camera_src_set_device_property_i32
    (GstIPhoneCameraSrc * self, CFStringRef name, SInt32 value);
static FigStatus gst_iphone_camera_src_set_device_property_cstr
    (GstIPhoneCameraSrc * self, const gchar * name, const gchar * value);

static GstPushSrcClass *parent_class;

GST_BOILERPLATE (GstIPhoneCameraSrc, gst_iphone_camera_src, GstPushSrc,
    GST_TYPE_PUSH_SRC);

static void
gst_iphone_camera_src_init (GstIPhoneCameraSrc * self,
    GstIPhoneCameraSrcClass * gclass)
{
  GstBaseSrc *base_src = GST_BASE_SRC_CAST (self);

  gst_base_src_set_live (base_src, TRUE);
  gst_base_src_set_format (base_src, GST_FORMAT_TIME);

  self->cond = g_cond_new ();
}

static void
gst_iphone_camera_src_dispose (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_iphone_camera_src_finalize (GObject * object)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (object);

  g_cond_free (self->cond);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_iphone_camera_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (object);

  switch (prop_id) {
    case PROP_DO_STATS:
      g_value_set_boolean (value, self->do_stats);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_iphone_camera_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (object);

  switch (prop_id) {
    case PROP_DO_STATS:
      self->do_stats = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_iphone_camera_src_change_state (GstElement * element,
    GstStateChange transition)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_iphone_camera_src_open_device (self))
        goto open_failed;
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      gst_iphone_camera_src_close_device (self);
      break;
    default:
      break;
  }

  return ret;

  /* ERRORS */
open_failed:
  {
    return GST_STATE_CHANGE_FAILURE;
  }
}

static GstCaps *
gst_iphone_camera_src_get_caps (GstBaseSrc * basesrc)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (basesrc);
  GstCaps *result;

  if (self->device != NULL) {
    gst_iphone_camera_src_ensure_device_caps_and_formats (self);

    result = gst_caps_ref (self->device_caps);
  } else {
    result = NULL;
  }

  if (result != NULL) {
    gchar *str;

    str = gst_caps_to_string (result);
    GST_DEBUG_OBJECT (self, "returning: %s", str);
    g_free (str);
  }

  return result;
}

static gboolean
gst_iphone_camera_src_set_caps (GstBaseSrc * basesrc, GstCaps * caps)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (basesrc);
  GstVideoFormat video_format;
  gint width, height, fps_n, fps_d;
  guint i;
  GstIPhoneCameraFormat *selected_format;

  if (self->device == NULL)
    goto no_device;

  if (!gst_video_format_parse_caps (caps, &video_format, &width, &height))
    goto invalid_format;
  if (!gst_video_parse_caps_framerate (caps, &fps_n, &fps_d))
    goto invalid_format;

  gst_iphone_camera_src_ensure_device_caps_and_formats (self);

  selected_format = NULL;

  for (i = 0; i != self->device_formats->len; i++) {
    GstIPhoneCameraFormat *format;

    format = &g_array_index (self->device_formats, GstIPhoneCameraFormat, i);
    if (format->video_format == video_format &&
        format->width == width && format->height == height &&
        format->fps_n == fps_n && format->fps_d == fps_d) {
      selected_format = format;
      break;
    }
  }

  if (selected_format == NULL)
    goto invalid_format;

  GST_DEBUG_OBJECT (self, "selecting format %u", selected_format->index);

  if (!gst_iphone_camera_src_select_format (self, selected_format))
    goto select_failed;

  gst_iphone_camera_src_release_device_caps_and_formats (self);

  return TRUE;

  /* ERRORS */
no_device:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("no device"), (NULL));
    return FALSE;
  }
invalid_format:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("invalid format"), (NULL));
    return FALSE;
  }
select_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("failed to select format"),
        (NULL));
    return FALSE;
  }
}

static gboolean
gst_iphone_camera_src_start (GstBaseSrc * basesrc)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (basesrc);

  self->running = TRUE;
  self->offset = 0;

  return TRUE;
}

static gboolean
gst_iphone_camera_src_stop (GstBaseSrc * basesrc)
{
  return TRUE;
}

static gboolean
gst_iphone_camera_src_query (GstBaseSrc * basesrc, GstQuery * query)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (basesrc);
  gboolean result = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GstClockTime min_latency, max_latency;

      if (self->device == NULL || !GST_CLOCK_TIME_IS_VALID (self->duration))
        goto beach;

      min_latency = max_latency = self->duration;

      GST_DEBUG_OBJECT (self, "reporting latency of min %" GST_TIME_FORMAT
          " max %" GST_TIME_FORMAT,
          GST_TIME_ARGS (min_latency), GST_TIME_ARGS (max_latency));

      gst_query_set_latency (query, TRUE, min_latency, max_latency);
      result = TRUE;
      break;
    }
    default:
      result = GST_BASE_SRC_CLASS (parent_class)->query (basesrc, query);
      break;
  }

beach:
  return result;
}

static gboolean
gst_iphone_camera_src_unlock (GstBaseSrc * basesrc)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (basesrc);

  BUFQUEUE_LOCK (self);
  self->running = FALSE;
  BUFQUEUE_UNLOCK (self);

  return TRUE;
}

static gboolean
gst_iphone_camera_src_unlock_stop (GstBaseSrc * basesrc)
{
  return TRUE;
}

static Boolean
gst_iphone_camera_src_validate (FigBufferQueueRef queue, FigSampleBuffer * buf,
    void *refCon)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (refCon);

  BUFQUEUE_LOCK (self);
  self->has_pending = TRUE;
  BUFQUEUE_NOTIFY (self);
  BUFQUEUE_UNLOCK (self);

  return FALSE;
}

static GstFlowReturn
gst_iphone_camera_src_create (GstPushSrc * pushsrc, GstBuffer ** buf)
{
  GstIPhoneCameraSrc *self = GST_IPHONE_CAMERA_SRC_CAST (pushsrc);
  GstCMApi *cm = self->ctx->cm;
  FigSampleBuffer *sbuf = NULL;
  GstClock *clock;
  GstClockTime ts;

  BUFQUEUE_LOCK (self);
  while (self->running && !self->has_pending)
    BUFQUEUE_WAIT (self);
  sbuf = cm->FigBufferQueueDequeueAndRetain (self->queue);
  self->has_pending = !cm->FigBufferQueueIsEmpty (self->queue);
  BUFQUEUE_UNLOCK (self);

  if (G_UNLIKELY (!self->running))
    goto shutting_down;

  GST_OBJECT_LOCK (self);
  if ((clock = GST_ELEMENT_CLOCK (self)) != NULL) {
    ts = gst_clock_get_time (clock);

    if (ts > GST_ELEMENT (self)->base_time)
      ts -= GST_ELEMENT (self)->base_time;
    else
      ts = 0;

    if (ts > self->duration)
      ts -= self->duration;
    else
      ts = 0;
  } else {
    ts = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (self);

  *buf = gst_core_media_buffer_new (self->ctx, sbuf);
  GST_BUFFER_OFFSET (*buf) = self->offset;
  GST_BUFFER_OFFSET_END (*buf) = self->offset + 1;
  GST_BUFFER_TIMESTAMP (*buf) = ts;
  GST_BUFFER_DURATION (*buf) = self->duration;

  if (self->offset == 0) {
    GST_BUFFER_FLAG_SET (*buf, GST_BUFFER_FLAG_DISCONT);
  }
  self->offset++;

  cm->FigSampleBufferRelease (sbuf);

  return GST_FLOW_OK;

  /* ERRORS */
shutting_down:
  {
    cm->FigSampleBufferRelease (sbuf);
    return GST_FLOW_WRONG_STATE;
  }
}

static gboolean
gst_iphone_camera_src_open_device (GstIPhoneCameraSrc * self)
{
  GstCoreMediaCtx *ctx = NULL;
  GError *error = NULL;
  GstCMApi *cm = NULL;
  GstMTApi *mt = NULL;
  GstCelApi *cel = NULL;
  FigStatus status;
  FigCaptureDeviceRef device = NULL;
  FigBaseObjectRef device_base;
  FigBaseVTable *device_vt;
  FigCaptureStreamRef stream = NULL;
  FigBaseObjectRef stream_base;
  FigBaseVTable *stream_vt;
  FigCaptureStreamIface *stream_iface;
  FigBufferQueueRef queue = NULL;

  ctx = gst_core_media_ctx_new (GST_API_CORE_VIDEO | GST_API_CORE_MEDIA
      | GST_API_MEDIA_TOOLBOX | GST_API_CELESTIAL, &error);
  if (error != NULL)
    goto api_error;
  cm = ctx->cm;
  mt = ctx->mt;
  cel = ctx->cel;

  status = cel->FigCreateCaptureDevicesAndStreamsForPreset (NULL,
      *(cel->kFigRecorderCapturePreset_VideoRecording), NULL,
      &device, &stream, NULL, NULL);
  if (status == kFigResourceBusy)
    goto device_busy;
  else if (status != kFigSuccess)
    goto unexpected_error;

  device_base = mt->FigCaptureDeviceGetFigBaseObject (device);
  device_vt = cm->FigBaseObjectGetVTable (device_base);

  stream_base = mt->FigCaptureStreamGetFigBaseObject (stream);
  stream_vt = cm->FigBaseObjectGetVTable (stream_base);
  stream_iface = stream_vt->derived;

  status = stream_vt->base->CopyProperty (stream_base,
      *(mt->kFigCaptureStreamProperty_BufferQueue), NULL, &queue);
  if (status != kFigSuccess)
    goto unexpected_error;

  self->has_pending = FALSE;

  cm->FigBufferQueueSetValidationCallback (queue,
      gst_iphone_camera_src_validate, self);

  self->ctx = ctx;

  self->device = device;
  self->device_iface_base = device_vt->base;
  self->stream = stream;
  self->stream_iface_base = stream_vt->base;
  self->stream_iface = stream_iface;
  self->queue = queue;

  self->duration = GST_CLOCK_TIME_NONE;

  return TRUE;

  /* ERRORS */
api_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED, ("API error"),
        ("%s", error->message));
    g_clear_error (&error);
    goto any_error;
  }
device_busy:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, BUSY,
        ("device is already in use"), (NULL));
    goto any_error;
  }
unexpected_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        ("unexpected error while opening device (%d)", status), (NULL));
    goto any_error;
  }
any_error:
  {
    if (stream != NULL)
      CFRelease (stream);
    if (device != NULL)
      CFRelease (device);

    if (ctx != NULL) {
      cm->FigBufferQueueRelease (queue);
      g_object_unref (ctx);
    }

    return FALSE;
  }
}

static void
gst_iphone_camera_src_close_device (GstIPhoneCameraSrc * self)
{
  gst_iphone_camera_src_release_device_caps_and_formats (self);

  self->stream_iface->Stop (self->stream);
  self->stream_iface = NULL;
  self->stream_iface_base->Finalize (self->stream);
  self->stream_iface_base = NULL;
  CFRelease (self->stream);
  self->stream = NULL;

  self->device_iface_base->Finalize (self->device);
  self->device_iface_base = NULL;
  CFRelease (self->device);
  self->device = NULL;

  self->ctx->cm->FigBufferQueueRelease (self->queue);
  self->queue = NULL;

  g_object_unref (self->ctx);
  self->ctx = NULL;
}

static void
gst_iphone_camera_src_ensure_device_caps_and_formats (GstIPhoneCameraSrc * self)
{
  FigStatus status;
  CFArrayRef iformats = NULL;
  CFIndex format_count, i;

  if (self->device_caps != NULL)
    goto already_probed;

  self->device_caps = gst_caps_new_empty ();
  self->device_formats =
      g_array_new (FALSE, FALSE, sizeof (GstIPhoneCameraFormat));

  status = self->device_iface_base->CopyProperty (self->device,
      *(self->ctx->mt->kFigCaptureDeviceProperty_ImagerSupportedFormatsArray),
      NULL, (CFTypeRef *) & iformats);
  if (status != kFigSuccess)
    goto beach;

  format_count = CFArrayGetCount (iformats);
  GST_DEBUG_OBJECT (self, "device supports %d formats", (gint) format_count);

  for (i = 0; i != format_count; i++) {
    CFDictionaryRef iformat;
    GstIPhoneCameraFormat format;

    iformat = CFArrayGetValueAtIndex (iformats, i);

    if (gst_iphone_camera_src_parse_imager_format (self, i, iformat, &format)) {
      gst_caps_append_structure (self->device_caps,
          gst_structure_new ("video/x-raw-yuv",
              "format", GST_TYPE_FOURCC, format.fourcc,
              "width", G_TYPE_INT, format.width,
              "height", G_TYPE_INT, format.height,
              "framerate", GST_TYPE_FRACTION, format.fps_n, format.fps_d,
              "pixel-aspect-ratio", GST_TYPE_FRACTION, 1, 1, NULL));
      g_array_append_val (self->device_formats, format);
    } else {
      GST_WARNING_OBJECT (self, "ignoring unknown format #%d", (gint) i);
    }
  }

  CFRelease (iformats);

already_probed:
beach:
  return;
}

static void
gst_iphone_camera_src_release_device_caps_and_formats (GstIPhoneCameraSrc *
    self)
{
  if (self->device_caps != NULL) {
    gst_caps_unref (self->device_caps);
    self->device_caps = NULL;
  }

  if (self->device_formats != NULL) {
    g_array_free (self->device_formats, TRUE);
    self->device_formats = NULL;
  }
}

static gboolean
gst_iphone_camera_src_select_format (GstIPhoneCameraSrc * self,
    GstIPhoneCameraFormat * format)
{
  gboolean result = FALSE;
  GstMTApi *mt = self->ctx->mt;
  FigStatus status;
  SInt32 framerate;

  status = gst_iphone_camera_src_set_device_property_i32 (self,
      *(mt->kFigCaptureDeviceProperty_ImagerFormatDescription), format->index);
  if (status != kFigSuccess)
    goto beach;

  framerate = format->fps_n / format->fps_d;

  status = gst_iphone_camera_src_set_device_property_i32 (self,
      *(mt->kFigCaptureDeviceProperty_ImagerFrameRate), framerate);
  if (status != kFigSuccess)
    goto beach;

  status = gst_iphone_camera_src_set_device_property_i32 (self,
      *(mt->kFigCaptureDeviceProperty_ImagerMinimumFrameRate), framerate);
  if (status != kFigSuccess)
    goto beach;

  status = gst_iphone_camera_src_set_device_property_cstr (self,
      "ColorRange", "ColorRangeSDVideo");
  if (status != kFigSuccess)
    goto beach;

  status = self->stream_iface->Start (self->stream);
  if (status != kFigSuccess)
    goto beach;

  GST_DEBUG_OBJECT (self, "configured format %d (%d x %d @ %d Hz)",
      format->index, format->width, format->height, (gint) framerate);

  self->duration =
      gst_util_uint64_scale (GST_SECOND, format->fps_d, format->fps_n);

  result = TRUE;

beach:
  return result;
}

static gboolean
gst_iphone_camera_src_parse_imager_format (GstIPhoneCameraSrc * self,
    guint index, CFDictionaryRef imager_format, GstIPhoneCameraFormat * format)
{
  GstCMApi *cm = self->ctx->cm;
  GstMTApi *mt = self->ctx->mt;
  const FigFormatDescription *desc;
  FigVideoDimensions dim;
  UInt32 subtype;
  CFNumberRef framerate_value;
  SInt32 fps_n;

  format->index = index;

  desc = CFDictionaryGetValue (imager_format,
      *(mt->kFigImagerSupportedFormat_FormatDescription));

  dim = cm->FigVideoFormatDescriptionGetDimensions (desc);
  format->width = dim.width;
  format->height = dim.height;

  subtype = cm->FigFormatDescriptionGetMediaSubType (desc);

  switch (subtype) {
    case kComponentVideoUnsigned:
      format->video_format = GST_VIDEO_FORMAT_YUY2;
      format->fourcc = GST_MAKE_FOURCC ('Y', 'U', 'Y', '2');
      break;
    case kYUV420vCodecType:
      format->video_format = GST_VIDEO_FORMAT_I420;
      format->fourcc = GST_MAKE_FOURCC ('I', '4', '2', '0');
      break;
    default:
      goto unsupported_format;
  }

  framerate_value = CFDictionaryGetValue (imager_format,
      *(mt->kFigImagerSupportedFormat_MaxFrameRate));
  CFNumberGetValue (framerate_value, kCFNumberSInt32Type, &fps_n);
  format->fps_n = fps_n;
  format->fps_d = 1;

  return TRUE;

unsupported_format:
  return FALSE;
}

static FigStatus
gst_iphone_camera_src_set_device_property_i32 (GstIPhoneCameraSrc * self,
    CFStringRef name, SInt32 value)
{
  FigStatus status;
  CFNumberRef number;

  number = CFNumberCreate (NULL, kCFNumberSInt32Type, &value);
  status = self->device_iface_base->SetProperty (self->device, name, number);
  CFRelease (number);

  return status;
}

static FigStatus
gst_iphone_camera_src_set_device_property_cstr (GstIPhoneCameraSrc * self,
    const gchar * name, const gchar * value)
{
  FigStatus status;
  CFStringRef name_str, value_str;

  name_str = CFStringCreateWithCStringNoCopy (NULL, name,
      kCFStringEncodingUTF8, kCFAllocatorNull);
  value_str = CFStringCreateWithCStringNoCopy (NULL, value,
      kCFStringEncodingUTF8, kCFAllocatorNull);
  status = self->device_iface_base->SetProperty (self->device,
      name_str, value_str);
  CFRelease (value_str);
  CFRelease (name_str);

  return status;
}

static void
gst_iphone_camera_src_base_init (gpointer gclass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (gclass);

  gst_element_class_set_details_simple (element_class,
      "iPhone camera source",
      "Source/Video",
      "Stream data from iPhone camera sensor",
      "Ole André Vadla Ravnås <oleavr@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_template));
}

static void
gst_iphone_camera_src_class_init (GstIPhoneCameraSrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSrcClass *gstbasesrc_class = GST_BASE_SRC_CLASS (klass);
  GstPushSrcClass *gstpushsrc_class = GST_PUSH_SRC_CLASS (klass);

  gobject_class->dispose = gst_iphone_camera_src_dispose;
  gobject_class->finalize = gst_iphone_camera_src_finalize;
  gobject_class->get_property = gst_iphone_camera_src_get_property;
  gobject_class->set_property = gst_iphone_camera_src_set_property;

  gstelement_class->change_state = gst_iphone_camera_src_change_state;

  gstbasesrc_class->get_caps = gst_iphone_camera_src_get_caps;
  gstbasesrc_class->set_caps = gst_iphone_camera_src_set_caps;
  gstbasesrc_class->start = gst_iphone_camera_src_start;
  gstbasesrc_class->stop = gst_iphone_camera_src_stop;
  gstbasesrc_class->query = gst_iphone_camera_src_query;
  gstbasesrc_class->unlock = gst_iphone_camera_src_unlock;
  gstbasesrc_class->unlock_stop = gst_iphone_camera_src_unlock_stop;

  gstpushsrc_class->create = gst_iphone_camera_src_create;

  g_object_class_install_property (gobject_class, PROP_DO_STATS,
      g_param_spec_boolean ("do-stats", "Enable statistics",
          "Enable logging of statistics", DEFAULT_DO_STATS,
          G_PARAM_CONSTRUCT | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  GST_DEBUG_CATEGORY_INIT (gst_iphone_camera_src_debug, "iphonecamerasrc",
      0, "iPhone video source");
}
