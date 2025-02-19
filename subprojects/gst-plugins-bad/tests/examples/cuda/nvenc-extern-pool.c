/*
 * GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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

#include <gst/gst.h>
#include <gst/cuda/gstcuda.h>
#include <gst/video/video.h>

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * msg, GstCudaContext * cuda_ctx)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_NEED_CONTEXT:
    {
      const gchar *ctx_type;
      gst_message_parse_context_type (msg, &ctx_type);
      gst_println ("Got need-context %s", ctx_type);
      if (g_strcmp0 (ctx_type, GST_CUDA_CONTEXT_TYPE) == 0) {
        GstContext *gst_ctx = gst_context_new_cuda_context (cuda_ctx);
        GstElement *src = GST_ELEMENT (msg->src);
        gst_element_set_context (src, gst_ctx);
        gst_context_unref (gst_ctx);
      }
      break;
    }
    default:
      break;
  }

  return GST_BUS_PASS;
}

static gboolean
bus_handler (GstBus * bus, GstMessage * msg, GMainLoop * loop)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      gst_println ("Got EOS");
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ERROR:
    {
      GError *err = NULL;
      gchar *name, *debug = NULL;

      name = gst_object_get_path_string (msg->src);
      gst_message_parse_error (msg, &err, &debug);

      gst_printerrln ("ERROR: from element %s: %s", name, err->message);
      if (debug != NULL)
        gst_printerrln ("Additional debug info:\n%s", debug);

      g_clear_error (&err);
      g_free (debug);
      g_free (name);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return G_SOURCE_CONTINUE;
}

gint
main (gint argc, gchar ** argv)
{
  GstCudaContext *context;
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  GstVideoInfo info;
  guint i;
  GstBuffer *prealloc[16];
  GstElement *pipeline;
  GstElement *element;
  GstBus *bus;
  GMainLoop *loop;

  /* Set environment to enable stream-ordered-allocation by default */
  g_setenv ("GST_CUDA_ENABLE_STREAM_ORDERED_ALLOC", "1", TRUE);

  loop = g_main_loop_new (NULL, FALSE);

  gst_init (NULL, NULL);

  if (!gst_cuda_load_library ()) {
    gst_println ("Couldn't load cuda library");
    return 0;
  }

  context = gst_cuda_context_new (0);
  if (!context) {
    gst_println ("Couldn't create cuda context");
    return 0;
  }

  /* Prepares bufferpool */
  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_NV12, 1280, 720);
  caps = gst_video_info_to_caps (&info);
  pool = gst_cuda_buffer_pool_new (context);
  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, info.size, 0, 0);
  gst_caps_unref (caps);

  /* Because NVENC does not support stream-ordered allocated CUDA memory,
   * we need to explicitly disable it for this buffer pool */
  gst_buffer_pool_config_set_cuda_stream_ordered_alloc (config, FALSE);

  if (!gst_buffer_pool_set_config (pool, config)) {
    gst_printerrln ("Set config failed");
    return 0;
  }

  if (!gst_buffer_pool_set_active (pool, TRUE)) {
    gst_printerrln ("Set active failed");
    return 0;
  }

  /* Preallocate buffers. Note that GstCudaBufferPool will do pre-allocation
   * by itself even if min_buffers are set */
  for (i = 0; i < G_N_ELEMENTS (prealloc); i++) {
    GstBuffer *buf;
    if (gst_buffer_pool_acquire_buffer (pool, &buf, NULL) != GST_FLOW_OK) {
      gst_printerrln ("Couldn't allocate memory");
      return 0;
    }

    prealloc[i] = buf;
  }

  /* Return buffers to pool */
  for (i = 0; i < G_N_ELEMENTS (prealloc); i++)
    gst_buffer_unref (prealloc[i]);

  /* Constructs pipeline with 2 encoders. Single pool can be shared
   * by multiple encoders if the size of pool is not smaller than encoded stream
   * resolution */
  pipeline = gst_parse_launch ("videotestsrc num-buffers=100 ! "
      "video/x-raw,format=NV12,width=640,height=480 ! cudaupload ! tee name=t ! "
      "queue ! cudascale ! "
      "video/x-raw(memory:CUDAMemory),width=1280,height=720 ! nvh264enc name=enc0 ! "
      "queue ! nvh264dec ! queue ! videoconvert ! autovideosink "
      "t. ! queue ! "
      "video/x-raw(memory:CUDAMemory),width=640,height=480 ! nvh264enc name=enc1 ! "
      "queue ! nvh264dec ! queue ! videoconvert ! autovideosink", NULL);

  if (!pipeline) {
    gst_printerrln ("Couldn't construct pipeline");
    return 0;
  }

  /* Pass our pool to encoders */
  element = gst_bin_get_by_name (GST_BIN (pipeline), "enc0");
  g_object_set (element, "extern-cuda-bufferpool", pool, NULL);
  gst_object_unref (element);

  element = gst_bin_get_by_name (GST_BIN (pipeline), "enc1");
  g_object_set (element, "extern-cuda-bufferpool", pool, NULL);
  gst_object_unref (element);

  /* Configure bus to pass our GstCudaContext to pipeline */
  bus = gst_element_get_bus (pipeline);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler,
      context, NULL);

  /* And bus watch to detect EOS or pipeline error */
  gst_bus_add_watch (bus, (GstBusFunc) bus_handler, loop);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_main_loop_run (loop);

  /* Cleanup pipeline */
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_bus_remove_watch (bus);
  gst_object_unref (bus);
  gst_object_unref (pipeline);

  /* Destroy pool */
  gst_buffer_pool_set_active (pool, FALSE);
  gst_object_unref (pool);

  gst_object_unref (context);

  gst_deinit ();

  return 0;
}
