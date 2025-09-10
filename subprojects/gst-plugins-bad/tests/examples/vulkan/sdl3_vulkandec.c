/*
 * GStreamer
 * Copyright (C) 2025 anonymix007 <48598263+anonymix007@users.noreply.github.com>
 *                    Victor Jaquez <vjaquez@igalia.com>
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
#include <gst/app/app.h>
#include <gst/video/video.h>
#include <gst/vulkan/vulkan.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

typedef struct
{
  GstElement *pipeline;
  GstBus *bus;

  GstVulkanInstance *instance;
  GstVulkanDevice *device;

  SDL_Window *window;
  SDL_Renderer *renderer;

  SDL_Thread *loop_thread;

  SDL_Texture *texture;

  /* operation */
  GstSample *last_sample;
  GMutex lock;
  GCond cond;
  gboolean rendered;
  gboolean quit;
} AppData;

static void
end_stream_cb (GstMessage * msg, AppData * appdata)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_EOS:
      SDL_LogInfo (SDL_LOG_CATEGORY_APPLICATION, "End of stream");
      break;
    case GST_MESSAGE_ERROR:{
      gchar *debug = NULL;
      GError *err = NULL;

      gst_message_parse_error (msg, &err, &debug);

      SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Error: %s", err->message);
      g_error_free (err);

      if (debug) {
        SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "\tDebug details: %s",
            debug);
        g_free (debug);
      }
      break;
    }
    default:
      break;
  }

  appdata->quit = TRUE;
}

static SDL_PixelFormat
sdl_format_from_vk (VkFormat format)
{
  switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM:
      return SDL_PIXELFORMAT_ARGB8888;
    case VK_FORMAT_R8G8B8A8_UNORM:
      return SDL_PIXELFORMAT_ABGR8888;
    case VK_FORMAT_R8_UNORM:
      // This value is probably a GStreamer bug:
      // https://gitlab.freedesktop.org/gstreamer/gstreamer/-/issues/4623
      // "Vulkan native YUV formats" are kinda broken
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return SDL_PIXELFORMAT_NV12;
    default:
      g_assert_not_reached ();
  }
}

static gboolean
create_texture (AppData * appdata, GstBuffer * buffer)
{
  VkImage vkimage;
  VkFormat vkformat;
  guint width, height;

  SDL_PropertiesID props = SDL_CreateProperties ();

  {
    GstMemory *memory;
    GstVulkanImageMemory *vkmem;

    g_assert (gst_buffer_n_memory (buffer) == 1);
    memory = gst_buffer_peek_memory (buffer, 0);
    g_assert (gst_is_vulkan_image_memory (memory));
    vkmem = (GstVulkanImageMemory *) memory;

    g_assert (vkmem->device == appdata->device);

    vkimage = vkmem->image;
    vkformat = vkmem->create_info.format;
    width = vkmem->create_info.extent.width;
    height = vkmem->create_info.extent.height;
  }

  if (appdata->texture)
    SDL_DestroyTexture (appdata->texture);

  SDL_SetNumberProperty (props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, width);
  SDL_SetNumberProperty (props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, height);
  SDL_SetNumberProperty (props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER,
      sdl_format_from_vk (vkformat));
  SDL_SetNumberProperty (props, SDL_PROP_TEXTURE_CREATE_VULKAN_TEXTURE_NUMBER,
      vkimage);

  appdata->texture = SDL_CreateTextureWithProperties (appdata->renderer, props);
  SDL_DestroyProperties (props);

  if (!appdata->texture) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Failed to create texture: %s",
        SDL_GetError ());
    return FALSE;
  }

  return TRUE;
}

static gboolean
draw (AppData * appdata)
{
  if (appdata->last_sample) {
    GstBuffer *buffer = gst_sample_get_buffer (appdata->last_sample);
    if (!create_texture (appdata, buffer))
      return FALSE;
  } else if (!appdata->texture) {
    SDL_LogInfo (SDL_LOG_CATEGORY_APPLICATION,
        "Neither a sample nor a texture is available yet");
  }

  if (!appdata->texture) {
    if (!SDL_SetRenderDrawColor (appdata->renderer, 0xFF, 0x18, 0x18, 0xFF)) {
      SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Failed to set color: %s",
          SDL_GetError ());
      return FALSE;
    }
    if (!SDL_RenderClear (appdata->renderer)) {
      SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
          "Failed to clear with color: %s", SDL_GetError ());
      return FALSE;
    }
  } else {
    if (!SDL_RenderTexture (appdata->renderer, appdata->texture, NULL, NULL)) {
      SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
          "Failed to render texture: %s", SDL_GetError ());
      return FALSE;
    }
  }

  if (!SDL_RenderPresent (appdata->renderer)) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Failed to present: %s",
        SDL_GetError ());
    return FALSE;
  }

  return TRUE;
}

static void
sdl_send_expose (void *userdata)
{
  SDL_Event event;
  SDL_zero (event);
  event.type = SDL_EVENT_WINDOW_EXPOSED;
  SDL_PushEvent (&event);
}

static GstFlowReturn
get_sample (AppData * appdata, GstSample * sample)
{
  if (!sample)
    return GST_FLOW_EOS;

  g_mutex_lock (&appdata->lock);

  gst_clear_sample (&appdata->last_sample);
  appdata->last_sample = sample;
  appdata->rendered = false;

  SDL_RunOnMainThread (sdl_send_expose, NULL, false);

  while (!appdata->rendered && !appdata->quit)
    g_cond_wait (&appdata->cond, &appdata->lock);

  g_mutex_unlock (&appdata->lock);

  return appdata->rendered ? GST_FLOW_OK : GST_FLOW_ERROR;
}

static GstFlowReturn
new_sample_cb (GstAppSink * sink, gpointer data)
{
  return get_sample (data, gst_app_sink_pull_sample (sink));;
}

static GstFlowReturn
new_preroll_cb (GstAppSink * sink, gpointer data)
{
  return get_sample (data, gst_app_sink_pull_preroll (sink));
}

static GstPadProbeReturn
pad_query_cb (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  AppData *appdata = data;

  if (GST_PAD_PROBE_INFO_TYPE (info) & GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM) {
    GstQuery *query = GST_PAD_PROBE_INFO_QUERY (info);

    switch (GST_QUERY_TYPE (query)) {
      case GST_QUERY_CONTEXT:
        if (gst_vulkan_handle_context_query (GST_PAD_PARENT (pad), query, NULL,
                appdata->instance, appdata->device)) {
          return GST_PAD_PROBE_HANDLED;
        }
      default:
        break;
    }
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
sdl_renderer_init (AppData * app)
{
  SDL_PropertiesID props = SDL_CreateProperties ();

  SDL_SetStringProperty (props, SDL_PROP_RENDERER_CREATE_NAME_STRING, "vulkan");
  SDL_SetPointerProperty (props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER,
      app->window);
  SDL_SetPointerProperty (props,
      SDL_PROP_RENDERER_CREATE_VULKAN_INSTANCE_POINTER,
      app->instance->instance);
  SDL_SetPointerProperty (props,
      SDL_PROP_RENDERER_CREATE_VULKAN_PHYSICAL_DEVICE_POINTER,
      app->device->physical_device->device);
  SDL_SetPointerProperty (props, SDL_PROP_RENDERER_CREATE_VULKAN_DEVICE_POINTER,
      app->device->device);

  app->renderer = SDL_CreateRendererWithProperties (props);

  SDL_DestroyProperties (props);

  if (!app->renderer) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Failed to create renderer: %s",
        SDL_GetError ());
  } else {
    SDL_LogInfo (SDL_LOG_CATEGORY_APPLICATION, "Renderer name: %s",
        SDL_GetRendererName (app->renderer));
  }

  return (app->renderer != NULL);
}

static void
sdl_deinit (AppData * app)
{
  if (app->loop_thread)
    SDL_WaitThread (app->loop_thread, NULL);
  if (app->texture)
    SDL_DestroyTexture (app->texture);
  if (app->renderer)
    SDL_DestroyRenderer (app->renderer);
  if (app->window)
    SDL_DestroyWindow (app->window);
}

struct DevData
{
  gboolean graphics_queue;
  gboolean video_queue;
  VkVideoCodecOperationFlagsKHR codecs;
};

static gboolean
vulkan_pick_queues (GstVulkanDevice * device, GstVulkanQueue * queue,
    gpointer data)
{
  struct DevData *dev = data;
  guint flags =
      device->physical_device->queue_family_props[queue->family].queueFlags;
  guint32 codecs =
      device->physical_device->queue_family_ops[queue->family].video;


  dev->graphics_queue |=
      ((flags & VK_QUEUE_TRANSFER_BIT) == VK_QUEUE_TRANSFER_BIT);
  dev->video_queue |=
      (((flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) ==
          VK_QUEUE_VIDEO_DECODE_BIT_KHR)
      && ((codecs & dev->codecs) == dev->codecs));

  return !(dev->graphics_queue && dev->video_queue);
}

static gboolean
vulkan_init (AppData * app, VkVideoCodecOperationFlagsKHR codecs)
{
  struct DevData dev = { FALSE, FALSE, codecs };
  GError *error = NULL;

  app->instance = gst_vulkan_instance_new ();
  if (!app->instance)
    return FALSE;

  if (!gst_vulkan_instance_fill_info (app->instance, &error)) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
        "Failed to populate Vulkan instance: %s", error->message);
    goto bail;
  }

  /* SDL stupidity: if instance supports both xlib and xcb, SDL chooses xlib;
   * while GStreamer only enables xcb */
  {
    GstVulkanDisplayType display_type;

    display_type = gst_vulkan_display_choose_type (app->instance);
    if (display_type == GST_VULKAN_DISPLAY_TYPE_XCB)
      gst_vulkan_instance_enable_extension (app->instance,
          "VK_KHR_xlib_surface");
  }


  if (!gst_vulkan_instance_open (app->instance, &error)) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
        "Failed to open Vulkan instance: %s", error->message);
    goto bail;
  }

  for (int i = 0; i < app->instance->n_physical_devices; i++) {
    app->device = gst_vulkan_device_new_with_index (app->instance, i);
    if (!app->device)
      continue;

    if (!gst_vulkan_device_open (app->device, &error)) {
      SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
          "Failed to open Vulkan devices: %s", error->message);
      g_clear_error (&error);
      gst_clear_object (&app->device);
      continue;
    }

    gst_vulkan_device_foreach_queue (app->device, vulkan_pick_queues, &dev);
    if (dev.graphics_queue && dev.video_queue)
      break;

    dev.graphics_queue = dev.video_queue = FALSE;

    gst_clear_object (&app->device);
  }

  /* TODO: check device can render too */

  if (!app->device) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
        "No usable Vulkan device found");
    goto bail;
  } else {
    SDL_LogInfo (SDL_LOG_CATEGORY_APPLICATION, "Using device %s",
        app->device->physical_device->properties.deviceName);
  }

  return TRUE;

bail:
  g_clear_error (&error);
  gst_clear_object (&app->instance);
  return FALSE;
}

static void
vulkan_deinit (AppData * app)
{
  gst_clear_object (&app->device);
  gst_clear_object (&app->instance);
}

static int
bus_thread (void *data)
{
  AppData *appdata = data;
  GstMessage *msg;

  msg = gst_bus_timed_pop_filtered (appdata->bus, GST_CLOCK_TIME_NONE,
      GST_MESSAGE_EOS | GST_MESSAGE_ERROR);
  end_stream_cb (msg, appdata);
  gst_message_unref (msg);

  return 0;
}

SDL_AppResult
SDL_AppInit (void **data, int argc, char **argv)
{
  GError *error = NULL;
  AppData *appdata = g_new0 (AppData, 1);

  if (!SDL_SetHint (SDL_HINT_MAIN_CALLBACK_RATE, "120")) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Failed to set FPS: %s",
        SDL_GetError ());
    return SDL_APP_FAILURE;
  }

  if (!SDL_Init (SDL_INIT_VIDEO)) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Failed to initialize SDL: %s",
        SDL_GetError ());
    return SDL_APP_FAILURE;
  }

  gst_init (&argc, &argv);
  if (argc != 2) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION, "Missing H.264 file to render");
    return SDL_APP_FAILURE;
  }

  if (!vulkan_init (appdata, VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR))
    return SDL_APP_FAILURE;

  appdata->pipeline = gst_parse_launch ("filesrc name=src ! parsebin ! "
      "vulkanh264dec ! appsink name=vksink", &error);
  if (!appdata->pipeline) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
        "Failed to parse GStreamer pipeline: %s", error->message);
    g_clear_error (&error);
    return SDL_APP_FAILURE;
  } else if (error) {
    SDL_LogInfo (SDL_LOG_CATEGORY_APPLICATION, "Pipeline parsing warning: %s",
        error->message);
    g_clear_error (&error);
  }

  appdata->window = SDL_CreateWindow ("SDL GStreamer Vulkan Demo", 1280, 800,
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_VULKAN);
  if (!appdata->window) {
    SDL_LogError (SDL_LOG_CATEGORY_APPLICATION,
        "Failed to create SDL Vulkan window: %s", SDL_GetError ());
    vulkan_deinit (appdata);
    gst_clear_object (&appdata->pipeline);
    return SDL_APP_FAILURE;
  }

  if (!sdl_renderer_init (appdata)) {
    vulkan_deinit (appdata);
    gst_clear_object (&appdata->pipeline);
    sdl_deinit (appdata);
    return SDL_APP_FAILURE;
  }

  g_mutex_init (&appdata->lock);

  {
    GstElement *vksink =
        gst_bin_get_by_name (GST_BIN (appdata->pipeline), "vksink");
    GstPad *pad = gst_element_get_static_pad (vksink, "sink");
    GstCaps *caps = gst_caps_from_string ("video/x-raw(memory:VulkanImage)");
    GstAppSinkCallbacks callbacks = {
      .new_sample = new_sample_cb,
      .new_preroll = new_preroll_cb,
    };

    g_assert (pad != NULL);

    gst_app_sink_set_callbacks (GST_APP_SINK (vksink), &callbacks, appdata,
        NULL);
    g_object_set (vksink, "caps", caps, NULL);
    gst_caps_unref (caps);

    gst_pad_add_probe (pad, GST_PAD_PROBE_TYPE_QUERY_DOWNSTREAM, pad_query_cb,
        appdata, NULL);
    gst_object_unref (pad);
    gst_object_unref (vksink);
  }

  {
    GstElement *src = gst_bin_get_by_name (GST_BIN (appdata->pipeline), "src");
    g_object_set (src, "location", argv[1], NULL);
    gst_object_unref (src);
  }

  appdata->bus = gst_pipeline_get_bus (GST_PIPELINE (appdata->pipeline));

  gst_element_set_state (appdata->pipeline, GST_STATE_PLAYING);

  appdata->loop_thread = SDL_CreateThread (bus_thread, "gst-bus-thread",
      appdata);

  *data = appdata;

  return SDL_APP_CONTINUE;
}

SDL_AppResult
SDL_AppEvent (void *data, SDL_Event * event)
{
  AppData *appdata = data;

  switch (event->type) {
    case SDL_EVENT_KEY_DOWN:
      if (event->key.key != SDLK_ESCAPE)
        break;
      /* fallthrough */
    case SDL_EVENT_QUIT:
      SDL_LogInfo (SDL_LOG_CATEGORY_APPLICATION, "SDL_EVENT_QUIT");
      gst_element_send_event (appdata->pipeline, gst_event_new_eos ());
      break;
    default:
      break;
  }

  return SDL_APP_CONTINUE;
}

SDL_AppResult
SDL_AppIterate (void *data)
{
  AppData *appdata = data;

  g_mutex_lock (&appdata->lock);
  if (!appdata->quit)
    appdata->rendered = draw (appdata);
  g_mutex_unlock (&appdata->lock);

  g_cond_signal (&appdata->cond);

  return appdata->quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

void
SDL_AppQuit (void *data, SDL_AppResult result)
{
  AppData *appdata = data;

  if (appdata) {
    gst_element_set_state (appdata->pipeline, GST_STATE_NULL);
    gst_object_unref (appdata->pipeline);
    gst_object_unref (appdata->bus);

    gst_clear_sample (&appdata->last_sample);
    g_mutex_clear (&appdata->lock);

    sdl_deinit (appdata);

    vulkan_deinit (appdata);
  }

  g_free (data);

  SDL_Quit ();
}
