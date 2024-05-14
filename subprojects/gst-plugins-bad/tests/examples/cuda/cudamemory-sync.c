/*
 * GStreamer
 * Copyright (C) 2024 Seungha Yang <seungha@centricular.com>
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

/* This example demonstrates how to share application's CUDA context with
 * GStreamer, and CUDA synchronization.
 *
 * In case that application wants to read CUDA device memory produced by
 * GStreamer directly, buffer/memory map with GST_MAP_CUDA flag will return
 * CUDA device memory instead of staging system memory. Also, GStreamer will not
 * wait for pending CUDA operation associated with the device memory when
 * GST_MAP_CUDA is specified. Thus, synchronization is user's responsibility.
 * For the synchronization, app needs to use GStreamer's CUDA stream, or
 * waits for possibly pending GPU operations queued by GStreamer.
 * 1) Executes operations with GStreamer's CUDA stream:
 *   GstCudaMemory will hold associated CUDA stream. User can access the
 *   CUDA stream via gst_cuda_memory_get_stream() which returns GstCudaStream
 *   object. The GstCudaStream is a wrapper of CUstream, so that the native
 *   handle can be used as a refcounted manner. To get native CUstream handle,
 *   use gst_cuda_stream_get_handle(). Since GPU commands are serialized in
 *   the CUDA stream already, user-side CUDA operation using the shared
 *   CUDA stream will be automatically serialized.
 * 2) Executes CUDA operation without GStreamer's CUDA stream:
 *   Since queued GPU commands may or may not be finished at the moment
 *   when application executes any CUDA operation using application's own
 *   CUDA stream, application should wait for GStreamer side CUDA operation.
 *   gst_cuda_memory_sync() will execute synchronization operation if needed
 *   and will block the calling CPU thread.
 *
 * This example consists of following steps
 * - Prepares CUDA resources (context, memory, etc)
 * - Launches GStreamer pipeline with shared CUDA context.
 *   The pipeline will produce GstCudaMemory rendered by cudaconvert element.
 * - Exectues scale CUDA kernel function and downloads scaled frame to host memory
 * - Encodes downloaded host memory to JPEG, write to a file.
 *
 * NOTE: In this example code, GStreamer's dlopen-ed CUDA functions
 * (decleared in cuda-gst.h) will be used instead of ones in decleared
 * in cuda.h.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <gst/video/video.h>
#include <gst/cuda/gstcuda.h>
#include <cuda.h>
#include <string.h>
#include <stdio.h>

#define RENDER_TARGET_WIDTH 640
#define RENDER_TARGET_HEIGHT 480

typedef struct
{
  GMutex lock;
  GCond cond;
  GstCudaContext *cuda_ctx;
  GstBuffer *buffer;
} AppData;

static void
on_handoff_cb (GstElement * sink, GstBuffer * buf, GstPad * pad, AppData * data)
{
  g_mutex_lock (&data->lock);
  data->buffer = gst_buffer_ref (buf);
  g_cond_signal (&data->cond);
  g_mutex_unlock (&data->lock);
}

static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * msg, AppData * data)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_NEED_CONTEXT:
    {
      const gchar *ctx_type;
      gst_message_parse_context_type (msg, &ctx_type);
      gst_println ("Got need-context %s", ctx_type);
      if (g_strcmp0 (ctx_type, GST_CUDA_CONTEXT_TYPE) == 0) {
        GstContext *gst_ctx = gst_context_new_cuda_context (data->cuda_ctx);
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

/* *INDENT-OFF* */
static const gchar kernel_func_str[] =
"extern \"C\" {\n"
"__device__ inline unsigned char\n"
"scale_to_uchar (float val)\n"
"{\n"
"  return (unsigned char) __float2int_rz (val * 255.0);\n"
"}\n"
"__global__ void\n"
"scale_func (cudaTextureObject_t tex, unsigned char * dst, size_t stride)\n"
"{\n"
"  int x_pos = blockIdx.x * blockDim.x + threadIdx.x;\n"
"  int y_pos = blockIdx.y * blockDim.y + threadIdx.y;\n"
"  if (x_pos >= 640 || y_pos >= 480)"
"    return;\n"
"  float x = (float) x_pos / 640.0f;\n"
"  float y = (float) y_pos / 480.0f;\n"
"  float4 sample = tex2D<float4>(tex, x, y);\n"
"  int dst_pos = (x_pos * 4) + (y_pos * stride);\n"
"  dst[dst_pos] = scale_to_uchar (sample.x);\n"
"  dst[dst_pos + 1] = scale_to_uchar (sample.y);\n"
"  dst[dst_pos + 2] = scale_to_uchar (sample.z);\n"
"  dst[dst_pos + 3] = scale_to_uchar (sample.w);\n"
"}\n"
"}\n";
/* *INDENT-ON* */

gint
main (gint argc, gchar ** argv)
{
  gchar *location = NULL;
  gboolean shared_stream = FALSE;
  GOptionEntry options[] = {
    {"location", 'l', 0, G_OPTION_ARG_STRING, &location,
        "Output jpeg file location", NULL},
    {"shared-stream", 's', 0, G_OPTION_ARG_NONE, &shared_stream,
        "Use GStreamer's CUDA stream", NULL},
    {NULL}
  };
  GOptionContext *option_ctx;
  gboolean ret;
  GError *err = NULL;
  CUresult cuda_ret;
  CUcontext cuda_ctx;
  CUdevice cuda_dev;
  int dev_cnt = 0;
  GstElement *pipeline;
  CUdeviceptr render_target;
  void *host_mem;
  gsize mem_size;
  size_t pitch;
  GstElement *sink;
  GstBus *bus;
  AppData app_data;
  gchar *cubin;
  CUmodule module;
  CUfunction kernel_func;
  GstBuffer *converted_buf;
  GstVideoInfo info;
  GstCaps *caps;
  GstSample *sample;
  gsize offset[GST_VIDEO_MAX_PLANES] = { 0, };
  gint stride[GST_VIDEO_MAX_PLANES] = { 0, };
  GstSample *jpeg_sample;
  GstCaps *jpeg_caps;
  CUstream app_stream = NULL;

  option_ctx = g_option_context_new ("CUDA memory sync example");
  g_option_context_add_main_entries (option_ctx, options, NULL);
  g_option_context_add_group (option_ctx, gst_init_get_option_group ());
  ret = g_option_context_parse (option_ctx, &argc, &argv, &err);
  g_option_context_free (option_ctx);

  if (!ret) {
    gst_printerrln ("option parsing failed: %s", err->message);
    g_clear_error (&err);
    return 1;
  }

  if (!location) {
    gst_printerrln ("File location must be specified");
    return 1;
  }

  if (!gst_cuda_load_library ()) {
    gst_printerrln ("Unable to initialize GstCUDA library");
    return 1;
  }

  if (!gst_cuda_nvrtc_load_library ()) {
    gst_printerrln ("Unable to load CUDA runtime compiler library");
    return 1;
  }

  /* Initialize CUDA and create device */
  cuda_ret = CuInit (0);
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("cuInit failed");
    return 1;
  }

  cuda_ret = CuDeviceGetCount (&dev_cnt);
  if (cuda_ret != CUDA_SUCCESS || dev_cnt == 0) {
    gst_printerrln ("No availiable CUDA device");
    return 1;
  }

  cuda_ret = CuDeviceGet (&cuda_dev, 0);
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("Couldn't get CUDA device");
    return 1;
  }

  cuda_ret = CuCtxCreate (&cuda_ctx, 0, cuda_dev);
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("Couldn't create CUDA context");
    return 1;
  }

  if (!shared_stream) {
    cuda_ret = CuStreamCreate (&app_stream, CU_STREAM_DEFAULT);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("Couldn't create CUDA stream");
      return 1;
    }
  }

  /* Allocate render target device memory */
  cuda_ret = CuMemAllocPitch (&render_target,
      &pitch, RENDER_TARGET_WIDTH * 4, RENDER_TARGET_HEIGHT, 16);
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("cuMemAllocPitch failed");
    return 1;
  }

  mem_size = pitch * RENDER_TARGET_HEIGHT;
  cuda_ret = CuMemAllocHost (&host_mem, mem_size);
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("cuMemAllocHost failed");
    return 1;
  }

  /* We will download converted CUDA device memory to this system memory */
  converted_buf = gst_buffer_new_wrapped_full (0,
      host_mem, mem_size, 0, mem_size, NULL, NULL);

  gst_video_info_set_format (&info, GST_VIDEO_FORMAT_RGBA,
      RENDER_TARGET_WIDTH, RENDER_TARGET_HEIGHT);
  stride[0] = pitch;

  /* Since we allocated system memory with the same size of CUDA device
   * memory, need to attach video meta to signal memory layout. The pitch
   * can be different from default stride */
  gst_buffer_add_video_meta_full (converted_buf, GST_VIDEO_FRAME_FLAG_NONE,
      GST_VIDEO_FORMAT_RGBA, RENDER_TARGET_WIDTH, RENDER_TARGET_HEIGHT,
      1, offset, stride);

  cubin = gst_cuda_nvrtc_compile_cubin (kernel_func_str, (gint) cuda_dev);
  if (!cubin) {
    gst_printerrln ("Couldn't compile cubin");
    return 1;
  }

  cuda_ret = CuModuleLoadData (&module, cubin);
  g_free (cubin);
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("cuModuleLoadData failed");
    return 1;
  }

  cuda_ret = CuModuleGetFunction (&kernel_func, module, "scale_func");
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("cuModuleGetFunction failed");
    return 1;
  }

  cuda_ret = CuCtxPopCurrent (NULL);
  if (cuda_ret != CUDA_SUCCESS) {
    gst_printerrln ("cuCtxPopCurrent failed");
    return 1;
  }

  /* Create GstCudaContext wrapping our context */
  app_data.cuda_ctx = gst_cuda_context_new_wrapped (cuda_ctx, cuda_dev);
  if (!app_data.cuda_ctx) {
    gst_printerrln ("Couldn't create wrapped context");
    return 1;
  }

  pipeline = gst_parse_launch ("videotestsrc num-buffers=1 ! "
      "video/x-raw,format=NV12 ! cudaupload ! cudaconvert ! "
      "video/x-raw(memory:CUDAMemory),format=RGBA ! "
      "fakesink signal-handoffs=true name=sink", NULL);
  if (!pipeline) {
    gst_printerrln ("Couldn't create pipeline");
    return 1;
  }

  g_mutex_init (&app_data.lock);
  g_cond_init (&app_data.cond);
  app_data.buffer = NULL;

  sink = gst_bin_get_by_name (GST_BIN (pipeline), "sink");
  g_assert (sink);

  /* Install handoff signal to get GstCudaMemory processed by cudaconvert */
  g_signal_connect (sink, "handoff", G_CALLBACK (on_handoff_cb), &app_data);
  gst_object_unref (sink);

  /* Setup **SYNC** bus handler. In case that an application wants to
   * shader its own CUDA context with GStreamer pipeline, GstContext
   * should be configured using sync bus handler */
  bus = gst_element_get_bus (pipeline);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler,
      &app_data, NULL);
  gst_object_unref (bus);

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    gst_printerrln ("State change failed");
    return 1;
  }

  /* Wait for processed buffer */
  g_mutex_lock (&app_data.lock);
  while (!app_data.buffer)
    g_cond_wait (&app_data.cond, &app_data.lock);
  g_mutex_unlock (&app_data.lock);
  gst_element_set_state (pipeline, GST_STATE_NULL);

  /* Launch image scale kernel func and download to host memory */
  {
    CUtexObject texture;
    GstMemory *mem;
    GstCudaMemory *cmem;
    GstCudaStream *gst_stream;
    CUstream stream;
    CUDA_MEMCPY2D copy_params = { 0, };
    CUDA_RESOURCE_DESC resource_desc;
    CUDA_TEXTURE_DESC texture_desc;
    GstMapInfo src_map;
    void *kernel_args[] = { &texture, &render_target, &pitch };

    mem = gst_buffer_peek_memory (app_data.buffer, 0);
    g_assert (gst_is_cuda_memory (mem));

    if (!gst_memory_map (mem, &src_map, GST_MAP_READ | GST_MAP_CUDA)) {
      gst_printerrln ("gst_memory_map failed");
      return 1;
    }

    cmem = GST_CUDA_MEMORY_CAST (mem);

    /* In case of GST_MAP_CUDA, GStreamer will not wait for CUDA sync.
     * Application can use CUDA stream attached in GstCudaMemory
     * or need to call gst_cuda_memory_sync() to ensure synchronization */
    if (shared_stream) {
      gst_stream = gst_cuda_memory_get_stream (cmem);
      stream = gst_cuda_stream_get_handle (gst_stream);
    } else {
      gst_cuda_memory_sync (cmem);
      stream = app_stream;
    }

    /* Prepare texture resource */
    memset (&resource_desc, 0, sizeof (CUDA_RESOURCE_DESC));
    memset (&texture_desc, 0, sizeof (CUDA_TEXTURE_DESC));
    resource_desc.resType = CU_RESOURCE_TYPE_PITCH2D;
    resource_desc.res.pitch2D.format = CU_AD_FORMAT_UNSIGNED_INT8;
    resource_desc.res.pitch2D.numChannels = 4;
    resource_desc.res.pitch2D.width = cmem->info.width;
    resource_desc.res.pitch2D.height = cmem->info.height;
    resource_desc.res.pitch2D.pitchInBytes = cmem->info.stride[0];
    resource_desc.res.pitch2D.devPtr = (CUdeviceptr) src_map.data;

    texture_desc.filterMode = CU_TR_FILTER_MODE_LINEAR;
    texture_desc.flags = CU_TRSF_NORMALIZED_COORDINATES;
    texture_desc.addressMode[0] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_desc.addressMode[1] = CU_TR_ADDRESS_MODE_CLAMP;
    texture_desc.addressMode[2] = CU_TR_ADDRESS_MODE_CLAMP;

    cuda_ret = CuCtxPushCurrent (cuda_ctx);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("cuCtxPopCurrent failed");
      return 1;
    }

    /* Create texture for sampling */
    cuda_ret = CuTexObjectCreate (&texture,
        &resource_desc, &texture_desc, NULL);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("cuTexObjectCreate failed");
      return 1;
    }

    cuda_ret = CuLaunchKernel (kernel_func,
        GST_ROUND_UP_16 (RENDER_TARGET_WIDTH) / 16,
        GST_ROUND_UP_16 (RENDER_TARGET_HEIGHT) / 16, 1, 16, 16, 1, 0,
        stream, kernel_args, NULL);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("cuLaunchKernel failed");
      return 1;
    }

    /* Download to system memory */
    copy_params.srcMemoryType = CU_MEMORYTYPE_DEVICE;
    copy_params.srcDevice = render_target;
    copy_params.srcPitch = pitch;

    copy_params.dstMemoryType = CU_MEMORYTYPE_HOST;
    copy_params.dstHost = host_mem;
    copy_params.dstPitch = pitch;

    copy_params.WidthInBytes = RENDER_TARGET_WIDTH * 4;
    copy_params.Height = RENDER_TARGET_HEIGHT;

    cuda_ret = CuMemcpy2DAsync (&copy_params, stream);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("cuMemcpy2DAsync failed");
      return 1;
    }

    /* Wait for conversion and memory download */
    cuda_ret = CuStreamSynchronize (stream);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("cuStreamSynchronize failed");
      return 1;
    }

    cuda_ret = CuTexObjectDestroy (texture);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("cuTexObjectDestroy failed");
      return 1;
    }

    cuda_ret = CuCtxPopCurrent (NULL);
    if (cuda_ret != CUDA_SUCCESS) {
      gst_printerrln ("cuCtxPopCurrent failed");
      return 1;
    }

    gst_memory_unmap (mem, &src_map);
  }

  /* Create sample and convert it to jpeg image */
  caps = gst_video_info_to_caps (&info);
  sample = gst_sample_new (converted_buf, caps, NULL, NULL);

  jpeg_caps = gst_caps_new_empty_simple ("image/jpeg");

  jpeg_sample = gst_video_convert_sample (sample,
      jpeg_caps, GST_CLOCK_TIME_NONE, NULL);
  if (!jpeg_sample) {
    gst_printerrln ("gst_video_convert_sample failed");
    return 1;
  }

  {
    GstBuffer *jpeg_buf = gst_sample_get_buffer (jpeg_sample);
    GstMapInfo map;

    if (!gst_buffer_map (jpeg_buf, &map, GST_MAP_READ)) {
      gst_printerrln ("gst_buffer_map failed");
      return 1;
    }

    FILE *fp;
    fp = fopen (location, "wb");
    if (!fp) {
      gst_printerrln ("fopen failed");
      return 1;
    }

    if (map.size != fwrite (map.data, 1, map.size, fp)) {
      gst_printerrln ("fwrite failed");
      return 1;
    }

    fclose (fp);

    gst_buffer_unmap (jpeg_buf, &map);
  }

  gst_println ("JPEG file is written to \"%s\"", location);

  /* Cleanup */
  g_free (location);
  gst_buffer_unref (app_data.buffer);
  gst_object_unref (app_data.cuda_ctx);
  g_mutex_clear (&app_data.lock);
  g_cond_clear (&app_data.cond);
  gst_object_unref (pipeline);
  gst_buffer_unref (converted_buf);
  gst_sample_unref (jpeg_sample);
  gst_sample_unref (sample);
  gst_caps_unref (caps);
  gst_caps_unref (jpeg_caps);

  /* Release CUDA resources */
  CuCtxPushCurrent (cuda_ctx);
  CuModuleUnload (module);
  CuMemFree (render_target);
  CuMemFreeHost (host_mem);
  if (app_stream)
    CuStreamDestroy (app_stream);
  CuCtxPopCurrent (NULL);
  CuCtxDestroy (cuda_ctx);

  gst_deinit ();

  return 0;
}
