/* GStreamer
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

/* This example demonstrates how to pass CUDA memory to GStreamer appsrc.
 *
 * Overview:
 * - User selects RGB values via command line arguments
 * - CUDA runtime API is used to allocate memory on the device and set to
 * specified RGB values. This step represents any type of user application that
 * is using the CUDA runtime API to process video data.
 * - Use GStreamer appsrc element (in blocking push mode) to pass the CUDA
 * frames into the pipeline.
 * - Output is written to "output.mp4" file in the current working directory.
 *
 * Additional notes:
 * - This example shows how to use Cuda Streams with GStreamer appsrc.
 * - This example shows how to share the GstCudaContext with the elements in the appsrc pipeline.
 *
 * NOTE: In this example code, GStreamer's dlopen-ed CUDA DRIVER API functions
 * (decleared in cuda-gst.h) will be used instead of ones in decleared
 * in cuda.h. The main diffrence is that the CUDA DRIVER API functions start
 * with "cu" prefix, while the GStreamer loaded ones start with "Cu".
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define GST_USE_UNSTABLE_API

#include <gst/gst.h>

#ifdef G_OS_WIN32
#include <windows.h>
#endif

#include <gst/app/gstappsrc.h>
#include <gst/cuda/gstcuda.h>
#include <gst/video/video.h>

#include <cuda_runtime.h>

#include <glib.h>
#include <stdbool.h>
#include <stdio.h>

#define FPS 15
#define DURATION_S 10
#define NUM_FRAMES (FPS * DURATION_S)
#define WIDTH 1920
#define HEIGHT 1080

#define CUSTOM_META_NAME "GstCudaAppsrcTestMeta"

// Default RGB values
#define DEFAULT_R 0
#define DEFAULT_G 0
#define DEFAULT_B 0

gpointer appsrc_thread_func (gpointer data);
bool check_cuda_result (CUresult result);
bool cuda_check_error (cudaError_t result);
void set_cuda_rbg_color (unsigned char *cuda_mem, size_t width, size_t height,
    unsigned char red, unsigned char green, unsigned char blue);

struct AppSrcData
{
  GstElement *appsrc;
  GstCudaContext *cuda_context;
  unsigned char r, g, b;
};

struct AppData
{
  GstCudaContext *cuda_context;
  GMainLoop *loop;
};

// Helper function to check CUDA DRIVER API results
bool
check_cuda_result (CUresult result)
{
  if (result != CUDA_SUCCESS) {
    const char *errorName;
    const char *errorString;

    CuGetErrorName (result, &errorName);
    CuGetErrorString (result, &errorString);

    gst_printerrln ("CUDA Error: %s (%s)\n", errorName, errorString);
    return false;
  }
  return true;
}

// Helper function to check CUDA runtime API results
bool
cuda_check_error (cudaError_t result)
{
  if (result != cudaSuccess) {
    gst_printerrln ("CUDA Error: %s\n", cudaGetErrorString (result));
    return false;
  }
  return true;
}

// Helper function to set a CUDA memory buffer to a solid color
void
set_cuda_rbg_color (unsigned char *cuda_mem, size_t width, size_t height,
    unsigned char red, unsigned char green, unsigned char blue)
{
  // To avoid a custom CUDA kernel (which would be faster for setting RGB
  // values), we'll just copy the same row of RGB values to all the rows of the
  // frame
  unsigned char *rgb_row = malloc (width * 3);
  if (!rgb_row) {
    gst_printerrln ("Failed to allocate memory for rgb_row");
    return;
  }
  for (int i = 0; i < width; i++) {
    rgb_row[i * 3] = red;
    rgb_row[i * 3 + 1] = green;
    rgb_row[i * 3 + 2] = blue;
  }
  for (int y = 0; y < height; y++) {
    check_cuda_result (cudaMemcpy ((void *) (cuda_mem + y * width * 3), rgb_row,
            width * 3, cudaMemcpyHostToDevice));
  }
  free (rgb_row);
}

// Sync bus handler: share GstCudaContext on need-context, quit main loop on EOS
static GstBusSyncReply
bus_sync_handler (GstBus * bus, GstMessage * msg, gpointer user_data)
{
  struct AppData *app_data = (struct AppData *) user_data;
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_NEED_CONTEXT:{
      const gchar *ctx_type;
      gst_message_parse_context_type (msg, &ctx_type);
      gst_println ("Got need-context %s", ctx_type);
      if (g_strcmp0 (ctx_type, GST_CUDA_CONTEXT_TYPE) == 0) {
        GstContext *gst_ctx =
            gst_context_new_cuda_context (app_data->cuda_context);
        GstElement *src = GST_ELEMENT (msg->src);
        gst_element_set_context (src, gst_ctx);
        gst_context_unref (gst_ctx);
        gst_println ("Set context for %s", GST_ELEMENT_NAME (src));
      }
      break;
    }
    case GST_MESSAGE_EOS:
      gst_println ("Received EOS message, stopping main loop");
      g_main_loop_quit (app_data->loop);
      break;
    default:
      break;
  }
  return GST_BUS_PASS;
}

// Function that runs in a separate thread and sends 3 seconds (15 FPS) of RED
// frames to the output
gpointer
appsrc_thread_func (gpointer data)
{
  struct AppSrcData *app_src_data = (struct AppSrcData *) data;
  GstAppSrc *appsrc = GST_APP_SRC (app_src_data->appsrc);
  GstCudaContext *cuda_context = app_src_data->cuda_context;
  unsigned char r = app_src_data->r;
  unsigned char g = app_src_data->g;
  unsigned char b = app_src_data->b;

  // Clock PTS for the frames
  GstClockTime pts = 0;

  // Allocate some memory for the frame we will push to the appsrc (use cuda
  // runtime API). Memory here is not pitched, just simple contiguous RGB
  unsigned char *cuda_mem;
  guint width = WIDTH;
  guint height = HEIGHT;
  guint cuda_mem_size = width * height * 3;
  if (!cuda_check_error (cudaMalloc ((void **) &cuda_mem, cuda_mem_size))) {
    gst_printerrln ("Failed to allocate CUDA memory");
    return NULL;
  }
  // Set frame to solid color
  set_cuda_rbg_color (cuda_mem, width, height, r, g, b);

  // Setup appsrc caps
  GstVideoInfo output_info;
  gst_video_info_set_format (&output_info, GST_VIDEO_FORMAT_RGB, width, height);
  output_info.fps_n = FPS;
  output_info.fps_d = 1;

  GstCaps *caps = gst_video_info_to_caps (&output_info);
  gst_caps_set_features_simple (caps,
      gst_caps_features_new_static_str (GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY,
          NULL));
  gst_app_src_set_caps (appsrc, caps);
  gst_caps_unref (caps);

  // Allocate cuda stream for pushing buffers to appsrc
  GstCudaStream *stream = gst_cuda_stream_new (cuda_context);

  for (int i = 0; i < NUM_FRAMES; i++) {
    // ALlocate a GStreamer cuda buffer
    GstMemory *mem =
        gst_cuda_allocator_alloc (NULL, cuda_context, stream, &output_info);
    if (!mem) {
      gst_printerrln ("Failed to allocate memory for conversion");
      return NULL;
    }
    GstCudaMemory *cmem = GST_CUDA_MEMORY_CAST (mem);
    GstVideoInfo *info = &cmem->info;

    GstBuffer *output_buf = gst_buffer_new ();
    gst_buffer_append_memory (output_buf, mem);
    gst_buffer_add_video_meta_full (output_buf, GST_VIDEO_FRAME_FLAG_NONE,
        GST_VIDEO_INFO_FORMAT (info), GST_VIDEO_INFO_WIDTH (info),
        GST_VIDEO_INFO_HEIGHT (info), GST_VIDEO_INFO_N_PLANES (info),
        info->offset, info->stride);

    // Map the memory for writing and copy the frame data to the buffer
    GstVideoFrame out_frame;
    if (!gst_video_frame_map (&out_frame, &output_info, output_buf,
            GST_MAP_WRITE_CUDA)) {
      gst_printerrln ("Failed to map output buffer");
      return NULL;
    }
    // Copy the frame data to the buffer
    guint n_planes = GST_VIDEO_FRAME_N_PLANES (&out_frame);
    guint8 *pixels = GST_VIDEO_FRAME_PLANE_DATA (&out_frame, 0);
    guint stride = GST_VIDEO_FRAME_PLANE_STRIDE (&out_frame, 0);
    guint pixel_stride = GST_VIDEO_FRAME_COMP_PSTRIDE (&out_frame, 0);
    guint frame_width = GST_VIDEO_FRAME_COMP_WIDTH (&out_frame, 0);
    guint frame_height = GST_VIDEO_FRAME_COMP_HEIGHT (&out_frame, 0);
    guint src_stride = frame_width * pixel_stride;
    gst_println
        ("pushing frame: %d, stride: %d, pixel_stride: %d, n_planes: %d, "
        "frame_width: %d, frame_height: %d, src_stride: %d", i, stride,
        pixel_stride, n_planes, frame_width, frame_height, src_stride);
    check_cuda_result (cudaMemcpy2DAsync (pixels, stride, cuda_mem, src_stride,
            src_stride, frame_height, cudaMemcpyDeviceToDevice,
            gst_cuda_stream_get_handle (stream)));

    // Set the timestamp (this is not a live source)
    GST_BUFFER_PTS (output_buf) = pts;
    GST_BUFFER_DURATION (output_buf) =
        gst_util_uint64_scale_int (1, GST_SECOND, FPS);
    pts += GST_BUFFER_DURATION (output_buf);

    // Synchronize the stream
    check_cuda_result (cudaStreamSynchronize (gst_cuda_stream_get_handle
            (stream)));

    // Unmap prior to pushing buffer to appsrc
    gst_video_frame_unmap (&out_frame);

    // Push the buffer to the appsrc
    GstFlowReturn push_ret = gst_app_src_push_buffer (appsrc, output_buf);
    if (push_ret != GST_FLOW_OK) {
      gst_printerrln ("Failed to push buffer to appsrc");
      return NULL;
    }
  }

  // Send EOS to the appsrc
  gst_println ("Pushing EOS to the appsrc");
  gst_app_src_end_of_stream (appsrc);

  // Clean up
  gst_cuda_stream_unref (stream);

  return NULL;
}

int
main (int argc, char **argv)
{
  GMainLoop *loop = NULL;
  GstElement *pipeline = NULL;
  const gchar *tags[] = { NULL };

  // GOptionContext for RGB
  gint r = DEFAULT_R, g = DEFAULT_G, b = DEFAULT_B;
  GOptionEntry entries[] = {
    {"red", 'r', 0, G_OPTION_ARG_INT, &r, "Red value (0-255)", NULL},
    {"green", 'g', 0, G_OPTION_ARG_INT, &g, "Green value (0-255)", NULL},
    {"blue", 'b', 0, G_OPTION_ARG_INT, &b, "Blue value (0-255)", NULL},
    {NULL}
  };
  GOptionContext *opt_ctx = g_option_context_new ("CUDA RGB appsrc example");
  g_option_context_add_main_entries (opt_ctx, entries, NULL);
  g_option_context_add_group (opt_ctx, gst_init_get_option_group ());
  GError *opt_err = NULL;
  if (!g_option_context_parse (opt_ctx, &argc, &argv, &opt_err)) {
    fprintf (stderr, "option parsing failed: %s\n", opt_err->message);
    g_clear_error (&opt_err);
    g_option_context_free (opt_ctx);
    return 1;
  }
  g_option_context_free (opt_ctx);
  if (r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) {
    fprintf (stderr, "RGB values must be in 0-255\n");
    return 1;
  }

  gst_println ("RGB value being used: 0x%02X%02X%02X", r, g, b);

  // Initialize GStreamer
  gst_init (&argc, &argv);

  if (!gst_cuda_load_library ()) {
    gst_printerrln ("Unable to initialize GstCUDA library");
    return 1;
  }

  /* Initialize CUDA and create device */
  if (!check_cuda_result (CuInit (0))) {
    gst_printerrln ("cuInit failed");
    return 1;
  }

  int dev_cnt;
  if (!check_cuda_result (CuDeviceGetCount (&dev_cnt))) {
    gst_printerrln ("No availiable CUDA device");
    return 1;
  }
  // Create GStreamer CUDA context
  GstCudaContext *cuda_context = gst_cuda_context_new (0);

  // Create a main loop so we can get messages from the pipeline
  loop = g_main_loop_new (NULL, FALSE);

  // Register the custom meta
  gst_meta_register_custom (CUSTOM_META_NAME, tags, NULL, NULL, NULL);

  // Create the appsrc pipeline using parse_launch
  GError *error = NULL;
  pipeline = gst_parse_launch ("appsrc name=src max-buffers=2 block=true "
      "is-live=false do-timestamp=false ! "
      "queue ! cudaconvert ! nvh264enc ! h264parse ! "
      "mp4mux ! filesink location=output.mp4", &error);

  if (!pipeline) {
    g_printerr ("Failed to create pipeline: %s\n", error->message);
    g_clear_error (&error);
    return 1;
  }
  // Single sync bus handler: share GstCudaContext and handle EOS
  struct AppData app_data = { cuda_context, loop };
  GstBus *bus = gst_element_get_bus (pipeline);
  gst_bus_set_sync_handler (bus, (GstBusSyncHandler) bus_sync_handler,
      &app_data, NULL);
  gst_object_unref (bus);

  // Get the appsrc element
  GstElement *appsrc = gst_bin_get_by_name (GST_BIN (pipeline), "src");

  // Create a thread to push frames to the appsrc
  struct AppSrcData app_src_data = { appsrc, cuda_context, (unsigned char) r,
    (unsigned char) g, (unsigned char) b
  };
  g_thread_new ("appsrc_thread", appsrc_thread_func, &app_src_data);

  // Start the pipeline
  GstStateChangeReturn sret =
      gst_element_set_state (pipeline, GST_STATE_PLAYING);
  if (sret == GST_STATE_CHANGE_FAILURE) {
    g_printerr ("Pipeline did not transition to playing\n");
    return 1;
  }
  // Run the main loop (will stop when the thread sends EOS)
  gst_println ("Starting main loop");
  g_main_loop_run (loop);

  // Set the pipeline to NULL state
  gst_element_set_state (pipeline, GST_STATE_NULL);

  // Clean up
  gst_object_unref (pipeline);
  g_main_loop_unref (loop);
  gst_object_unref (cuda_context);

  return 0;
}
