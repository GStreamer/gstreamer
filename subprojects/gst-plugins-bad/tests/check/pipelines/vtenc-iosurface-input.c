/* GStreamer
 * Copyright (C) 2026 Dominique Leroux <dominique.p.leroux@gmail.com>
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

#include <gst/check/gstcheck.h>
#include <gst/app/gstappsrc.h>
#include <gst/iosurface/gstiosurface.h>
#include <gst/video/video.h>
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#include <gst/gstmacos.h>
#endif

#include <CoreFoundation/CoreFoundation.h>
#include <CoreVideo/CVPixelBufferIOSurface.h>
#include <IOSurface/IOSurfaceRef.h>
#include <mach/kern_return.h>

#define TEST_WIDTH 64
#define TEST_HEIGHT 64
#define TEST_FPS_N 30
#define TEST_FPS_D 1

typedef struct
{
  const gchar *element;
  const gchar *parser;
} TestEncoder;

typedef struct
{
  GstVideoFormat format;
  OSType cv_format;
} TestInputFormat;

static const TestInputFormat input_format_nv12 = {
  GST_VIDEO_FORMAT_NV12, kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
};

static const TestInputFormat input_format_ayuv64 = {
  GST_VIDEO_FORMAT_AYUV64, kCVPixelFormatType_4444AYpCbCr16
};

static const TestEncoder encoders[] = {
  {"vtenc_h264", "h264parse"},
  {"vtenc_h264_hw", "h264parse"},
  {"vtenc_h265", "h265parse"},
  {"vtenc_h265_hw", "h265parse"},
  {"vtenc_h265a", "h265parse"},
  {"vtenc_h265a_hw", "h265parse"},
};

static const TestEncoder alpha_encoders[] = {
  {"vtenc_h265a", "h265parse"},
  {"vtenc_h265a_hw", "h265parse"},
};

typedef struct _TestIOSurfaceAllocator TestIOSurfaceAllocator;
typedef struct _TestIOSurfaceAllocatorClass TestIOSurfaceAllocatorClass;
typedef struct _TestIOSurfaceMemory TestIOSurfaceMemory;

struct _TestIOSurfaceAllocator
{
  GstAllocator parent;
};

struct _TestIOSurfaceAllocatorClass
{
  GstAllocatorClass parent_class;
};

struct _TestIOSurfaceMemory
{
  GstMemory mem;
  IOSurfaceRef surface;
  guint plane;
  IOSurfaceLockOptions lock_options;
};

GType test_iosurface_allocator_get_type (void);
#define TEST_TYPE_IOSURFACE_ALLOCATOR (test_iosurface_allocator_get_type ())

G_DEFINE_TYPE (TestIOSurfaceAllocator, test_iosurface_allocator,
    GST_TYPE_ALLOCATOR);

static TestIOSurfaceAllocator *test_allocator;

static gpointer
test_iosurface_memory_map (GstMemory * gmem, gsize maxsize, GstMapFlags flags)
{
  TestIOSurfaceMemory *mem = (TestIOSurfaceMemory *) gmem;

  (void) maxsize;

  mem->lock_options = (flags & GST_MAP_WRITE) ? 0 : kIOSurfaceLockReadOnly;
  if (IOSurfaceLock (mem->surface, mem->lock_options, NULL) != KERN_SUCCESS)
    return NULL;

  if (IOSurfaceGetPlaneCount (mem->surface) == 0)
    return IOSurfaceGetBaseAddress (mem->surface);

  return IOSurfaceGetBaseAddressOfPlane (mem->surface, mem->plane);
}

static void
test_iosurface_memory_unmap (GstMemory * gmem)
{
  TestIOSurfaceMemory *mem = (TestIOSurfaceMemory *) gmem;

  IOSurfaceUnlock (mem->surface, mem->lock_options, NULL);
}

static void
test_iosurface_memory_free (GstAllocator * allocator, GstMemory * gmem)
{
  TestIOSurfaceMemory *mem = (TestIOSurfaceMemory *) gmem;

  (void) allocator;

  CFRelease (mem->surface);
  g_free (mem);
}

static gboolean
test_iosurface_memory_query_surface (GstMemory * gmem, IOSurfaceRef * surface,
    guint * plane)
{
  TestIOSurfaceMemory *mem = (TestIOSurfaceMemory *) gmem;

  if (surface)
    *surface = mem->surface;
  if (plane)
    *plane = mem->plane;

  return TRUE;
}

static void
test_iosurface_allocator_class_init (TestIOSurfaceAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = GST_ALLOCATOR_CLASS (klass);

  allocator_class->free = test_iosurface_memory_free;
}

static void
test_iosurface_allocator_init (TestIOSurfaceAllocator * allocator)
{
  GstAllocator *gst_allocator = GST_ALLOCATOR (allocator);

  gst_allocator->mem_type = "TestIOSurfaceMemory";
  gst_allocator->mem_map = test_iosurface_memory_map;
  gst_allocator->mem_unmap = test_iosurface_memory_unmap;
  GST_OBJECT_FLAG_SET (allocator, GST_ALLOCATOR_FLAG_CUSTOM_ALLOC);
}

static void
ensure_test_allocator (void)
{
  static gsize init = 0;

  if (g_once_init_enter (&init)) {
    test_allocator = g_object_new (TEST_TYPE_IOSURFACE_ALLOCATOR, NULL);
    gst_object_ref_sink (test_allocator);
    gst_iosurface_memory_register_query_function (TEST_TYPE_IOSURFACE_ALLOCATOR,
        test_iosurface_memory_query_surface);
    g_once_init_leave (&init, 1);
  }
}

static gboolean
element_available (const gchar * name)
{
  GstElementFactory *factory = gst_element_factory_find (name);

  if (factory) {
    gst_object_unref (factory);
    return TRUE;
  }

  return FALSE;
}

static gboolean
require_elements_or_skip (const gchar * const *elements, gsize n_elements)
{
  gboolean strict = g_getenv ("GST_REQUIRE_TEST_ELEMENTS") != NULL;

  for (gsize i = 0; i < n_elements; i++) {
    if (element_available (elements[i]))
      continue;
    if (strict)
      fail_unless (FALSE, "Missing required element: %s", elements[i]);
    GST_INFO ("Skipping test, missing required element: %s", elements[i]);
    return FALSE;
  }

  return TRUE;
}

static gboolean
encoder_available_or_skip (const TestEncoder * encoder)
{
  const gchar *required[] = {
    encoder->element, encoder->parser, "appsrc", "fakesink"
  };

  return require_elements_or_skip (required, G_N_ELEMENTS (required));
}

static CVPixelBufferRef
create_test_pixel_buffer (const TestInputFormat * format)
{
  CFMutableDictionaryRef attrs =
      CFDictionaryCreateMutable (kCFAllocatorDefault, 1,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionaryRef iosurface_props =
      CFDictionaryCreate (kCFAllocatorDefault, NULL, NULL, 0,
      &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CVPixelBufferRef pbuf = NULL;
  CVReturn cv_ret;

  CFDictionarySetValue (attrs, kCVPixelBufferIOSurfacePropertiesKey,
      iosurface_props);
  CFRelease (iosurface_props);

  cv_ret = CVPixelBufferCreate (kCFAllocatorDefault, TEST_WIDTH, TEST_HEIGHT,
      format->cv_format, attrs, &pbuf);
  CFRelease (attrs);

  if (cv_ret != kCVReturnSuccess) {
    GST_INFO ("Skipping test, CVPixelBufferCreate failed: %d", cv_ret);
    return NULL;
  }

  fail_unless (CVPixelBufferGetIOSurface (pbuf) != NULL);

  return pbuf;
}

static void
fill_test_pixel_buffer (CVPixelBufferRef pbuf)
{
  size_t n_planes = CVPixelBufferGetPlaneCount (pbuf);

  fail_unless_equals_int (CVPixelBufferLockBaseAddress (pbuf, 0),
      kCVReturnSuccess);

  if (n_planes == 0) {
    memset (CVPixelBufferGetBaseAddress (pbuf), 0x80,
        CVPixelBufferGetBytesPerRow (pbuf) * CVPixelBufferGetHeight (pbuf));
  } else {
    memset (CVPixelBufferGetBaseAddressOfPlane (pbuf, 0), 0x10,
        CVPixelBufferGetBytesPerRowOfPlane (pbuf, 0) *
        CVPixelBufferGetHeightOfPlane (pbuf, 0));

    for (size_t i = 1; i < n_planes; i++) {
      memset (CVPixelBufferGetBaseAddressOfPlane (pbuf, i), 0x80,
          CVPixelBufferGetBytesPerRowOfPlane (pbuf, i) *
          CVPixelBufferGetHeightOfPlane (pbuf, i));
    }
  }

  CVPixelBufferUnlockBaseAddress (pbuf, 0);
}

static GstMemory *
create_test_iosurface_memory (IOSurfaceRef surface, guint plane)
{
  TestIOSurfaceMemory *mem;
  gsize size;

  ensure_test_allocator ();

  if (IOSurfaceGetPlaneCount (surface) == 0)
    size = IOSurfaceGetBytesPerRow (surface) * IOSurfaceGetHeight (surface);
  else
    size = IOSurfaceGetBytesPerRowOfPlane (surface, plane) *
        IOSurfaceGetHeightOfPlane (surface, plane);

  mem = g_new0 (TestIOSurfaceMemory, 1);
  mem->surface = (IOSurfaceRef) CFRetain (surface);
  mem->plane = plane;
  gst_memory_init (GST_MEMORY_CAST (mem), 0,
      GST_ALLOCATOR_CAST (test_allocator), NULL, size, 0, 0, size);

  return GST_MEMORY_CAST (mem);
}

static GstBuffer *
create_test_iosurface_buffer (const TestInputFormat * format,
    gboolean split_surfaces, gboolean reverse_memories)
{
  CVPixelBufferRef pbuf0, pbuf1 = NULL;
  IOSurfaceRef surface0, surface1;
  GstBuffer *buffer;
  IOSurfaceRef plane_surface[GST_VIDEO_MAX_PLANES] = { NULL, };
  gsize plane_size[GST_VIDEO_MAX_PLANES] = { 0, };
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, };
  guint n_planes;
  gsize next_offset = 0;

  fail_unless (format->format == GST_VIDEO_FORMAT_NV12 || !split_surfaces);

  pbuf0 = create_test_pixel_buffer (format);
  if (!pbuf0)
    return NULL;
  fill_test_pixel_buffer (pbuf0);

  if (split_surfaces) {
    pbuf1 = create_test_pixel_buffer (format);
    if (!pbuf1) {
      CVPixelBufferRelease (pbuf0);
      return NULL;
    }
    fill_test_pixel_buffer (pbuf1);
  }

  surface0 = CVPixelBufferGetIOSurface (pbuf0);
  surface1 = pbuf1 ? CVPixelBufferGetIOSurface (pbuf1) : surface0;
  n_planes = IOSurfaceGetPlaneCount (surface0);
  if (n_planes == 0)
    n_planes = 1;

  buffer = gst_buffer_new ();

  for (guint plane = 0; plane < n_planes; plane++) {
    IOSurfaceRef surface = (split_surfaces && plane == 1) ? surface1 : surface0;

    plane_surface[plane] = surface;

    if (IOSurfaceGetPlaneCount (surface) == 0) {
      stride[plane] = IOSurfaceGetBytesPerRow (surface);
      plane_size[plane] = IOSurfaceGetBytesPerRow (surface) *
          IOSurfaceGetHeight (surface);
    } else {
      stride[plane] = IOSurfaceGetBytesPerRowOfPlane (surface, plane);
      plane_size[plane] = IOSurfaceGetBytesPerRowOfPlane (surface, plane) *
          IOSurfaceGetHeightOfPlane (surface, plane);
    }
  }

  if (reverse_memories) {
    for (gint plane = n_planes - 1; plane >= 0; plane--) {
      gst_buffer_append_memory (buffer,
          create_test_iosurface_memory (plane_surface[plane], plane));
      offset[plane] = next_offset;
      next_offset += plane_size[plane];
    }
  } else {
    for (guint plane = 0; plane < n_planes; plane++) {
      gst_buffer_append_memory (buffer,
          create_test_iosurface_memory (plane_surface[plane], plane));
      offset[plane] = next_offset;
      next_offset += plane_size[plane];
    }

  }

  gst_buffer_add_video_meta_full (buffer, GST_VIDEO_FRAME_FLAG_NONE,
      format->format, TEST_WIDTH, TEST_HEIGHT, n_planes, offset, stride);

  GST_BUFFER_PTS (buffer) = 0;
  GST_BUFFER_DURATION (buffer) =
      gst_util_uint64_scale_int (GST_SECOND, TEST_FPS_D, TEST_FPS_N);

  CVPixelBufferRelease (pbuf0);
  if (pbuf1)
    CVPixelBufferRelease (pbuf1);

  fail_unless (gst_is_iosurface_buffer (buffer));

  return buffer;
}

static gboolean
wait_pipeline_until_done (GstElement * pipeline, gboolean expect_error)
{
  GstBus *bus = gst_element_get_bus (pipeline);
  GstMessage *msg;
  gboolean ret = FALSE;

  fail_unless (bus != NULL);

  msg = gst_bus_timed_pop_filtered (bus, 10 * GST_SECOND,
      GST_MESSAGE_ERROR | GST_MESSAGE_EOS);

  if (msg == NULL) {
    fail_unless (FALSE, "Pipeline timed out");
  } else if (GST_MESSAGE_TYPE (msg) == GST_MESSAGE_ERROR) {
    GError *err = NULL;
    gchar *debug = NULL;

    gst_message_parse_error (msg, &err, &debug);
    GST_INFO ("Pipeline error: %s", err ? err->message : "unknown");
    if (debug)
      GST_INFO ("Pipeline error details: %s", debug);
    ret = expect_error;
    g_clear_error (&err);
    g_free (debug);
  } else {
    ret = !expect_error;
  }

  gst_clear_message (&msg);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);
  gst_object_unref (bus);

  return ret;
}

static void
run_appsrc_pipeline_with_buffer (GstBuffer * buffer,
    const TestEncoder * encoder, const TestInputFormat * format,
    gboolean expect_error)
{
  GstElement *pipeline, *src;
  GstCaps *caps;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  gchar *pipeline_desc;
  gchar *caps_string;

  pipeline_desc = g_strdup_printf ("appsrc name=src format=time ! "
      "%s allow-frame-reordering=false ! %s ! "
      "fakesink name=sink sync=false", encoder->element, encoder->parser);

  pipeline = gst_parse_launch (pipeline_desc, NULL);
  g_free (pipeline_desc);
  fail_unless (pipeline != NULL);

  src = gst_bin_get_by_name (GST_BIN (pipeline), "src");
  fail_unless (src != NULL);

  caps_string = g_strdup_printf ("video/x-raw(memory:IOSurface),format=%s,"
      "width=%d,height=%d,framerate=%d/%d",
      gst_video_format_to_string (format->format), TEST_WIDTH, TEST_HEIGHT,
      TEST_FPS_N, TEST_FPS_D);
  caps = gst_caps_from_string (caps_string);
  g_free (caps_string);
  g_object_set (src, "caps", caps, "format", GST_FORMAT_TIME, NULL);
  gst_caps_unref (caps);

  fail_unless (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
  g_signal_emit_by_name (src, "push-buffer", buffer, &flow_ret);
  fail_unless (flow_ret == GST_FLOW_OK || expect_error,
      "Unexpected appsrc push flow return: %s", gst_flow_get_name (flow_ret));
  g_signal_emit_by_name (src, "end-of-stream", &flow_ret);
  fail_unless (flow_ret == GST_FLOW_OK || expect_error,
      "Unexpected appsrc EOS flow return: %s", gst_flow_get_name (flow_ret));

  fail_unless_equals_int (wait_pipeline_until_done (pipeline, expect_error),
      TRUE);

  gst_object_unref (src);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_vtenc_memory_iosurface_input)
{
  for (guint i = 0; i < G_N_ELEMENTS (encoders); i++) {
    GstBuffer *buffer;

    if (!encoder_available_or_skip (&encoders[i]))
      continue;

    buffer = create_test_iosurface_buffer (&input_format_nv12, FALSE, FALSE);
    if (!buffer)
      continue;

    GST_INFO ("Testing memory-only IOSurface input with %s",
        encoders[i].element);
    run_appsrc_pipeline_with_buffer (buffer, &encoders[i], &input_format_nv12,
        FALSE);
    gst_buffer_unref (buffer);
  }
}

GST_END_TEST;

GST_START_TEST (test_vtenc_memory_iosurface_input_reordered_memories)
{
  GstBuffer *buffer;

  if (!encoder_available_or_skip (&encoders[0]))
    return;

  buffer = create_test_iosurface_buffer (&input_format_nv12, FALSE, TRUE);
  if (!buffer)
    return;

  run_appsrc_pipeline_with_buffer (buffer, &encoders[0], &input_format_nv12,
      FALSE);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_vtenc_rejects_split_iosurface_input)
{
  for (guint i = 0; i < G_N_ELEMENTS (encoders); i++) {
    GstBuffer *buffer;

    if (!encoder_available_or_skip (&encoders[i]))
      continue;

    buffer = create_test_iosurface_buffer (&input_format_nv12, TRUE, FALSE);
    if (!buffer)
      continue;

    GST_INFO ("Testing split IOSurface rejection with %s", encoders[i].element);
    run_appsrc_pipeline_with_buffer (buffer, &encoders[i], &input_format_nv12,
        TRUE);
    gst_buffer_unref (buffer);
  }
}

GST_END_TEST;

GST_START_TEST (test_vtenc_h265a_ayuv64_iosurface_input)
{
  for (guint i = 0; i < G_N_ELEMENTS (alpha_encoders); i++) {
    GstBuffer *buffer;

    if (!encoder_available_or_skip (&alpha_encoders[i]))
      continue;

    buffer = create_test_iosurface_buffer (&input_format_ayuv64, FALSE, FALSE);
    if (!buffer)
      continue;

    GST_INFO ("Testing AYUV64 IOSurface input with %s",
        alpha_encoders[i].element);
    run_appsrc_pipeline_with_buffer (buffer, &alpha_encoders[i],
        &input_format_ayuv64, FALSE);
    gst_buffer_unref (buffer);
  }
}

GST_END_TEST;

GST_START_TEST (test_vtdec_iosurface_to_vtenc)
{
  static const gchar *required[] = {
    "filesrc", "tsdemux", "h264parse", "vtdec", "fakesink"
  };
  gchar *filepath;
  gchar *resolved_path;

  if (!require_elements_or_skip (required, G_N_ELEMENTS (required)))
    return;

  filepath = g_build_filename (GST_TEST_FILES_PATH, "test.ts", NULL);
  if (!g_file_test (filepath, G_FILE_TEST_EXISTS)) {
    GST_INFO ("Skipping test, missing file: %s", filepath);
    g_free (filepath);
    return;
  }

  resolved_path = g_canonicalize_filename (filepath, NULL);
  g_free (filepath);

  for (guint i = 0; i < G_N_ELEMENTS (encoders); i++) {
    gchar *pipeline_desc;
    GstElement *pipeline;

    if (!element_available (encoders[i].element) ||
        !element_available (encoders[i].parser)) {
      GST_INFO ("Skipping test, missing encoder or parser for: %s",
          encoders[i].element);
      continue;
    }

    pipeline_desc = g_strdup_printf ("filesrc location=\"%s\" ! "
        "tsdemux ! h264parse ! vtdec ! "
        "video/x-raw(memory:IOSurface),format=NV12 ! "
        "%s allow-frame-reordering=false ! %s ! "
        "fakesink name=sink sync=false", resolved_path, encoders[i].element,
        encoders[i].parser);

    pipeline = gst_parse_launch (pipeline_desc, NULL);
    g_free (pipeline_desc);
    fail_unless (pipeline != NULL);

    GST_INFO ("Testing vtdec IOSurface bridge with %s", encoders[i].element);
    fail_unless (gst_element_set_state (pipeline,
            GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE);
    fail_unless_equals_int (wait_pipeline_until_done (pipeline, FALSE), TRUE);
    gst_object_unref (pipeline);
  }

  g_free (resolved_path);
}

GST_END_TEST;

static Suite *
vtenc_iosurface_input_suite (void)
{
  Suite *s = suite_create ("vtenc-iosurface-input");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_vtenc_memory_iosurface_input);
  tcase_add_test (tc_chain,
      test_vtenc_memory_iosurface_input_reordered_memories);
  tcase_add_test (tc_chain, test_vtenc_rejects_split_iosurface_input);
  tcase_add_test (tc_chain, test_vtenc_h265a_ayuv64_iosurface_input);
  tcase_add_test (tc_chain, test_vtdec_iosurface_to_vtenc);

  return s;
}

static int
run_tests (void)
{
  Suite *s = vtenc_iosurface_input_suite ();

  return gst_check_run_suite_nofork (s, "vtenc-iosurface-input", __FILE__);
}

int
main (int argc, char **argv)
{
  gst_check_init (&argc, &argv);
#if TARGET_OS_OSX
  return gst_macos_main_simple ((GstMainFuncSimple) run_tests, NULL);
#else
  return run_tests ();
#endif
}
