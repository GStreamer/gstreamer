/* GStreamer RTP H.264 unit test
 *
 * Copyright (C) 2017 Centricular Ltd
 *   @author: Tim-Philipp Müller <tim@centricular.com>
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

#include <gst/check/check.h>
#include <gst/app/app.h>

#define ALLOCATOR_CUSTOM_SYSMEM "CustomSysMem"

static GstAllocator *custom_sysmem_allocator;   /* NULL */

/* Custom memory */

typedef struct
{
  GstMemory mem;
  guint8 *data;
  guint8 *allocdata;
} CustomSysmem;

static CustomSysmem *
custom_sysmem_new (GstMemoryFlags flags, gsize maxsize, gsize align,
    gsize offset, gsize size)
{
  gsize aoffset, padding;
  CustomSysmem *mem;

  /* ensure configured alignment */
  align |= gst_memory_alignment;
  /* allocate more to compensate for alignment */
  maxsize += align;

  mem = g_new0 (CustomSysmem, 1);

  mem->allocdata = g_malloc (maxsize);

  mem->data = mem->allocdata;

  /* do alignment */
  if ((aoffset = ((guintptr) mem->data & align))) {
    aoffset = (align + 1) - aoffset;
    mem->data += aoffset;
    maxsize -= aoffset;
  }

  if (offset && (flags & GST_MEMORY_FLAG_ZERO_PREFIXED))
    memset (mem->data, 0, offset);

  padding = maxsize - (offset + size);
  if (padding && (flags & GST_MEMORY_FLAG_ZERO_PADDED))
    memset (mem->data + offset + size, 0, padding);

  gst_memory_init (GST_MEMORY_CAST (mem), flags, custom_sysmem_allocator,
      NULL, maxsize, align, offset, size);

  return mem;
}

static gpointer
custom_sysmem_map (CustomSysmem * mem, gsize maxsize, GstMapFlags flags)
{
  return mem->data;
}

static gboolean
custom_sysmem_unmap (CustomSysmem * mem)
{
  return TRUE;
}

static CustomSysmem *
custom_sysmem_copy (CustomSysmem * mem, gssize offset, gsize size)
{
  g_return_val_if_reached (NULL);
}

static CustomSysmem *
custom_sysmem_share (CustomSysmem * mem, gssize offset, gsize size)
{
  g_return_val_if_reached (NULL);
}

static gboolean
custom_sysmem_is_span (CustomSysmem * mem1, CustomSysmem * mem2, gsize * offset)
{
  g_return_val_if_reached (FALSE);
}

/* Custom allocator */

typedef struct
{
  GstAllocator allocator;
} CustomSysmemAllocator;

typedef struct
{
  GstAllocatorClass allocator_class;
} CustomSysmemAllocatorClass;

GType custom_sysmem_allocator_get_type (void);
G_DEFINE_TYPE (CustomSysmemAllocator, custom_sysmem_allocator,
    GST_TYPE_ALLOCATOR);

static GstMemory *
custom_sysmem_allocator_alloc (GstAllocator * allocator, gsize size,
    GstAllocationParams * params)
{
  gsize maxsize = size + params->prefix + params->padding;

  return (GstMemory *) custom_sysmem_new (params->flags,
      maxsize, params->align, params->prefix, size);
}

static void
custom_sysmem_allocator_free (GstAllocator * allocator, GstMemory * mem)
{
  CustomSysmem *csmem = (CustomSysmem *) mem;

  g_free (csmem->allocdata);
  g_free (csmem);
}

static void
custom_sysmem_allocator_class_init (CustomSysmemAllocatorClass * klass)
{
  GstAllocatorClass *allocator_class = (GstAllocatorClass *) klass;

  allocator_class->alloc = custom_sysmem_allocator_alloc;
  allocator_class->free = custom_sysmem_allocator_free;
}

static void
custom_sysmem_allocator_init (CustomSysmemAllocator * allocator)
{
  GstAllocator *alloc = GST_ALLOCATOR_CAST (allocator);

  alloc->mem_type = ALLOCATOR_CUSTOM_SYSMEM;
  alloc->mem_map = (GstMemoryMapFunction) custom_sysmem_map;
  alloc->mem_unmap = (GstMemoryUnmapFunction) custom_sysmem_unmap;
  alloc->mem_copy = (GstMemoryCopyFunction) custom_sysmem_copy;
  alloc->mem_share = (GstMemoryShareFunction) custom_sysmem_share;
  alloc->mem_is_span = (GstMemoryIsSpanFunction) custom_sysmem_is_span;
}

/* AppSink subclass proposing our custom allocator to upstream */

typedef struct
{
  GstAppSink appsink;
} CMemAppSink;

typedef struct
{
  GstAppSinkClass appsink;
} CMemAppSinkClass;

GType c_mem_app_sink_get_type (void);

G_DEFINE_TYPE (CMemAppSink, c_mem_app_sink, GST_TYPE_APP_SINK);

static void
c_mem_app_sink_init (CMemAppSink * cmemsink)
{
}

static gboolean
c_mem_app_sink_propose_allocation (GstBaseSink * sink, GstQuery * query)
{
  gst_query_add_allocation_param (query, custom_sysmem_allocator, NULL);
  return TRUE;
}

static void
c_mem_app_sink_class_init (CMemAppSinkClass * klass)
{
  GstBaseSinkClass *basesink_class = (GstBaseSinkClass *) klass;

  basesink_class->propose_allocation = c_mem_app_sink_propose_allocation;
}

#define RTP_H264_FILE GST_TEST_FILES_PATH G_DIR_SEPARATOR_S "h264.rtp"

GST_START_TEST (test_rtph264depay_with_downstream_allocator)
{
  GstElement *pipeline, *src, *depay, *sink;
  GstMemory *mem;
  GstSample *sample;
  GstBuffer *buf;
  GstCaps *caps;

  custom_sysmem_allocator =
      g_object_new (custom_sysmem_allocator_get_type (), NULL);

  pipeline = gst_pipeline_new ("pipeline");

  src = gst_element_factory_make ("appsrc", NULL);

  caps = gst_caps_new_simple ("application/x-rtp",
      "media", G_TYPE_STRING, "video",
      "payload", G_TYPE_INT, 96,
      "clock-rate", G_TYPE_INT, 90000,
      "encoding-name", G_TYPE_STRING, "H264",
      "ssrc", G_TYPE_UINT, 1990683810,
      "timestamp-offset", G_TYPE_UINT, 3697583446,
      "seqnum-offset", G_TYPE_UINT, 15568,
      "a-framerate", G_TYPE_STRING, "30", NULL);
  g_object_set (src, "format", GST_FORMAT_TIME, "caps", caps, NULL);
  gst_bin_add (GST_BIN (pipeline), src);
  gst_caps_unref (caps);

  depay = gst_element_factory_make ("rtph264depay", NULL);
  gst_bin_add (GST_BIN (pipeline), depay);

  sink = g_object_new (c_mem_app_sink_get_type (), NULL);
  gst_bin_add (GST_BIN (pipeline), sink);

  gst_element_link_many (src, depay, sink, NULL);

  gst_element_set_state (pipeline, GST_STATE_PAUSED);

  {
    gchar *data, *pdata;
    gsize len;

    fail_unless (g_file_get_contents (RTP_H264_FILE, &data, &len, NULL));
    fail_unless (len > 2);

    pdata = data;
    while (len > 2) {
      GstFlowReturn flow;
      guint16 packet_len;

      packet_len = GST_READ_UINT16_BE (pdata);
      GST_INFO ("rtp packet length: %u (bytes left: %u)", packet_len,
          (guint) len);
      fail_unless (len >= 2 + packet_len);

      flow = gst_app_src_push_buffer (GST_APP_SRC (src),
          gst_buffer_new_wrapped (g_memdup (pdata + 2, packet_len),
              packet_len));

      fail_unless_equals_int (flow, GST_FLOW_OK);

      pdata += 2 + packet_len;
      len -= 2 + packet_len;
    }

    g_free (data);
  }

  gst_app_src_end_of_stream (GST_APP_SRC (src));

  sample = gst_app_sink_pull_preroll (GST_APP_SINK (sink));
  fail_unless (sample != NULL);

  buf = gst_sample_get_buffer (sample);

  GST_LOG ("buffer has %u memories", gst_buffer_n_memory (buf));
  GST_LOG ("buffer size: %u", (guint) gst_buffer_get_size (buf));

  fail_unless (gst_buffer_n_memory (buf) > 0);
  mem = gst_buffer_peek_memory (buf, 0);
  fail_unless (mem != NULL);

  GST_LOG ("buffer memory type: %s", mem->allocator->mem_type);
  fail_unless (gst_memory_is_type (mem, ALLOCATOR_CUSTOM_SYSMEM));

  gst_sample_unref (sample);

  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);

  g_object_unref (custom_sysmem_allocator);
  custom_sysmem_allocator = NULL;
}

GST_END_TEST;

static Suite *
rtph264_suite (void)
{
  Suite *s = suite_create ("rtph264");
  TCase *tc_chain;

  tc_chain = tcase_create ("rtph264depay");
  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_rtph264depay_with_downstream_allocator);

  return s;
}

GST_CHECK_MAIN (rtph264);
