/* GStreamer
 *
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/check/gstharness.h>
#include <gst/vulkan/vulkan.h>

static GstVulkanInstance *instance;
static GstVulkanDevice *device;

static void
setup (void)
{
  instance = gst_vulkan_instance_new ();
  fail_unless (gst_vulkan_instance_open (instance, NULL));
  device = gst_vulkan_device_new_with_index (instance, 0);
  fail_unless (gst_vulkan_device_open (device, NULL));
}

static void
teardown (void)
{
  gst_object_unref (instance);
  gst_object_unref (device);
}

static void
check_size (GstMemory * mem, gsize at_least)
{
  gsize size, maxsize, offset;

  size = gst_memory_get_sizes (mem, &offset, &maxsize);
  fail_unless (size <= maxsize);
  fail_unless (size >= at_least);
}

static GstVulkanImageMemory *
create_image_mem (GstVideoInfo * v_info)
{
  GstVulkanImageMemory *vk_mem;
  VkImageUsageFlags usage;
  VkFormat vk_format;
  GstMemory *mem;

  vk_format = gst_vulkan_format_from_video_info (v_info, 0);

  usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT;
  mem =
      gst_vulkan_image_memory_alloc (device, vk_format,
      GST_VIDEO_INFO_COMP_WIDTH (v_info, 0),
      GST_VIDEO_INFO_COMP_HEIGHT (v_info, 0), VK_IMAGE_TILING_LINEAR,
      usage, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
  fail_unless (gst_is_vulkan_image_memory (mem));
  vk_mem = (GstVulkanImageMemory *) mem;
  fail_unless (vk_mem->usage == usage);
  return vk_mem;
}

GST_START_TEST (test_image_new)
{
  GstVulkanImageMemory *vk_mem;
  GstVideoInfo v_info;
  gsize offset, size;

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 16, 16);
  vk_mem = create_image_mem (&v_info);

  fail_unless (vk_mem->device == device);
  fail_unless (vk_mem->vk_mem != NULL);

  size = gst_memory_get_sizes ((GstMemory *) vk_mem, &offset, NULL);
  fail_unless (offset == 0);
  check_size ((GstMemory *) vk_mem, v_info.size);
  fail_unless (vk_mem->requirements.size >= size);

  size = gst_memory_get_sizes ((GstMemory *) vk_mem->vk_mem, &offset, NULL);
  fail_unless (offset == 0);
  check_size ((GstMemory *) vk_mem->vk_mem, v_info.size);

  gst_memory_unref ((GstMemory *) vk_mem);
}

GST_END_TEST;

GST_START_TEST (test_image_view_new)
{
  GstVulkanImageMemory *vk_mem;
  GstVulkanImageView *view;
  GstVideoInfo v_info;

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 16, 16);
  vk_mem = create_image_mem (&v_info);

  view = gst_vulkan_get_or_create_image_view (vk_mem);

  gst_vulkan_image_view_unref (view);
  gst_memory_unref ((GstMemory *) vk_mem);
}

GST_END_TEST;

GST_START_TEST (test_image_view_get)
{
  GstVulkanImageMemory *vk_mem;
  GstVulkanImageView *view;
  GstVideoInfo v_info;

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 16, 16);
  vk_mem = create_image_mem (&v_info);

  view = gst_vulkan_get_or_create_image_view (vk_mem);
  gst_vulkan_image_view_unref (view);
  view = gst_vulkan_get_or_create_image_view (vk_mem);
  gst_vulkan_image_view_unref (view);

  gst_memory_unref ((GstMemory *) vk_mem);
}

GST_END_TEST;

struct wrapped_image_data
{
  VkImage image;
  gboolean notified;
};

static void
wrapped_image_notify (struct wrapped_image_data *data)
{
  vkDestroyImage (device->device, data->image, NULL);
  data->notified = TRUE;
}

GST_START_TEST (test_image_wrapped_with_image_info)
{
  VkImageCreateInfo image_info = {
    .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = VK_FORMAT_R8G8B8A8_UNORM,
    .extent = {16, 16, 1},
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
    .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
  };
  struct wrapped_image_data data = { 0, };
  GstVulkanImageMemory *vk_mem;
  GstMemory *mem;

  fail_unless (vkCreateImage (device->device, &image_info, NULL,
          &data.image) == VK_SUCCESS);

  mem = gst_vulkan_image_memory_wrapped_with_image_info (device, data.image,
      &image_info, &data, (GDestroyNotify) wrapped_image_notify);
  fail_unless (gst_is_vulkan_image_memory (mem));

  vk_mem = (GstVulkanImageMemory *) mem;
  fail_unless (vk_mem->image == data.image);
  fail_unless (vk_mem->create_info.format == image_info.format);
  fail_unless (vk_mem->create_info.extent.width == image_info.extent.width);
  fail_unless (vk_mem->create_info.extent.height == image_info.extent.height);
  fail_unless (vk_mem->create_info.usage == image_info.usage);
  fail_if (data.notified);

  gst_memory_unref (mem);
  fail_unless (data.notified);
}

GST_END_TEST;

#define N_THREADS 4
#define N_MEMORY 4
#define N_OPS 1024

struct view_stress
{
  GMutex lock;
  GCond cond;
  gint ready;
  int n_ops;
  GQueue *memories;
  GstHarnessThread *threads[N_THREADS];
};

static void
wait_for_ready (GstHarnessThread * thread, struct view_stress *stress)
{
  g_mutex_lock (&stress->lock);
  while (!stress->ready)
    g_cond_wait (&stress->cond, &stress->lock);
  g_mutex_unlock (&stress->lock);
}

static void
get_unref_image_view (GstHarnessThread * thread, struct view_stress *stress)
{
  int rand = g_random_int_range (0, N_MEMORY);
  GstVulkanImageMemory *mem;
  GstVulkanImageView *view;

  mem = g_queue_peek_nth (stress->memories, rand);
  view = gst_vulkan_get_or_create_image_view (mem);
  gst_vulkan_image_view_unref (view);

  g_atomic_int_inc (&stress->n_ops);
  if (g_atomic_int_get (&stress->n_ops) > N_OPS)
    g_usleep (100);
}

GST_START_TEST (test_image_view_stress)
{
  GstHarness *h = gst_harness_new_empty ();
  struct view_stress stress;
  GstVideoInfo v_info;
  int i;

  g_mutex_init (&stress.lock);
  g_cond_init (&stress.cond);
  stress.ready = FALSE;
  g_atomic_int_set (&stress.n_ops, 0);
  stress.memories = g_queue_new ();

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 16, 16);
  for (i = 0; i < N_MEMORY; i++) {
    g_queue_push_head (stress.memories, create_image_mem (&v_info));
  }

  g_mutex_lock (&stress.lock);
  for (i = 0; i < N_THREADS; i++) {
    stress.threads[i] = gst_harness_stress_custom_start (h,
        (GFunc) wait_for_ready, (GFunc) get_unref_image_view, &stress, 10);
  }
  stress.ready = TRUE;
  g_cond_broadcast (&stress.cond);
  g_mutex_unlock (&stress.lock);

  while (g_atomic_int_get (&stress.n_ops) < N_OPS)
    g_usleep (10000);

  for (i = 0; i < N_THREADS; i++) {
    gst_harness_stress_thread_stop (stress.threads[i]);
  }

  g_mutex_clear (&stress.lock);
  g_cond_clear (&stress.cond);
  g_queue_free_full (stress.memories, (GDestroyNotify) gst_memory_unref);
  gst_harness_teardown (h);
}

GST_END_TEST;

#define MEMORY_READY 1
#define VIEW_READY 2

struct simultaneous_destruction
{
  GMutex lock;
  GCond cond;
  gint state;
  GstVulkanImageMemory *image;
  GstVulkanImageView *view;
};

static void
get_memory_and_unref (struct simultaneous_destruction *d)
{
  g_mutex_lock (&d->lock);
  d->state |= MEMORY_READY;
  g_cond_broadcast (&d->cond);
  while (!(d->state & VIEW_READY))
    g_cond_wait (&d->cond, &d->lock);
  GstVulkanImageView *view = gst_vulkan_get_or_create_image_view (d->image);
  GST_INFO ("unref mem %p", d->image);
  g_mutex_unlock (&d->lock);

  gst_memory_unref ((GstMemory *) d->image);
  g_thread_yield ();
  gst_vulkan_image_view_unref (view);
}

static void
get_view_and_unref (struct simultaneous_destruction *d)
{
  g_mutex_lock (&d->lock);

  d->state |= VIEW_READY;
  g_cond_broadcast (&d->cond);
  while (!(d->state & MEMORY_READY))
    g_cond_wait (&d->cond, &d->lock);
  GST_INFO ("unref view %p", d->view);
  g_mutex_unlock (&d->lock);

  g_thread_yield ();
  gst_vulkan_image_view_unref (d->view);
}

static void
simultaneous_destruction_task (GstHarnessThread * t, gint * count)
{
  GstVideoInfo v_info;
  struct simultaneous_destruction d = { 0, };

  g_mutex_init (&d.lock);
  g_cond_init (&d.cond);

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 16, 16);
  d.image = create_image_mem (&v_info);
  d.view = gst_vulkan_get_or_create_image_view (d.image);

  GThread *mem_unref =
      g_thread_new (NULL, (GThreadFunc) get_memory_and_unref, &d);
  GThread *view_unref =
      g_thread_new (NULL, (GThreadFunc) get_view_and_unref, &d);

  g_thread_join (mem_unref);
  g_thread_join (view_unref);
  g_mutex_clear (&d.lock);
  g_cond_clear (&d.cond);

  int progress = g_atomic_int_add (count, 1);
  if (progress % 5000 == 0)
    g_print ("progress: %d\n", progress);
}

#define N_DESTROY_THREADS 8

GST_START_TEST (test_image_view_mem_simultaneous_destruction)
{
  GstHarness *h = gst_harness_new_empty ();
  GstHarnessThread *t[N_DESTROY_THREADS];
  int count = 0;

  for (int i = 0; i < N_DESTROY_THREADS; i++) {
    t[i] = gst_harness_stress_custom_start (h,
        NULL, (GFunc) simultaneous_destruction_task, &count, 0);
  }

  while (g_atomic_int_get (&count) < 10240)
    g_usleep (1000);

  for (int i = 0; i < N_DESTROY_THREADS; i++) {
    gst_harness_stress_thread_stop (t[i]);
  }

  GST_INFO ("performed %d", g_atomic_int_get (&count));
  gst_harness_teardown (h);
}

GST_END_TEST;

GST_START_TEST (test_image_barrier_new)
{
  GstVulkanImageMemory *vk_mem;
  GstVulkanBarrierImageInfo barrier = { 0, }, new_barrier =
      { 0, }, set_barrier = { 0, };
  GstVideoInfo v_info;
  GstVulkanQueue *queue = NULL;

  gst_video_info_set_format (&v_info, GST_VIDEO_FORMAT_RGBA, 16, 16);
  vk_mem = create_image_mem (&v_info);

  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (queue);
  vk_mem->barrier.parent.queue = queue;

  gst_vulkan_image_memory_lock (vk_mem);
  gst_vulkan_image_memory_peek_barrier_unlocked (vk_mem, &barrier);

  // update the barrier with new information
  gst_vulkan_barrier_image_info_copy_into (&barrier, &new_barrier);
  new_barrier.image_layout = VK_IMAGE_LAYOUT_GENERAL;
  fail_unless (!gst_vulkan_barrier_image_info_is_equal (&barrier,
          &new_barrier));
  gst_vulkan_barrier_image_info_copy_into (&new_barrier, &set_barrier);

  // update the memory with the set barrier
  fail_unless (gst_vulkan_image_memory_compare_exchange_barrier_unlocked
      (vk_mem, &barrier, &new_barrier));

  // the memory barrier is different from the old barrier so this will fail.
  fail_if (gst_vulkan_image_memory_compare_exchange_barrier_unlocked (vk_mem,
          &barrier, &new_barrier));

  gst_vulkan_barrier_image_info_clear (&barrier);
  gst_vulkan_barrier_image_info_clear (&new_barrier);

  // check the set barrier is the same
  gst_vulkan_image_memory_peek_barrier_unlocked (vk_mem, &barrier);
  fail_unless (gst_vulkan_barrier_image_info_is_equal (&barrier, &set_barrier));
  gst_vulkan_image_memory_unlock (vk_mem);

  gst_vulkan_barrier_image_info_clear (&barrier);
  gst_vulkan_barrier_image_info_clear (&set_barrier);

  gst_memory_unref ((GstMemory *) vk_mem);
}

GST_END_TEST;

struct copy_format
{
  VkFormat format;
  guint n_planes;
  guint bpp[2];
  guint sub[2];
};

static const struct copy_format rgba_format = {
  VK_FORMAT_R8G8B8A8_UNORM, 1, {4, 0}, {0, 0}
};

static const struct copy_format nv12_format = {
  VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 2, {1, 2}, {0, 1}
};

static const struct copy_format r8_format = {
  VK_FORMAT_R8_UNORM, 1, {1, 0}, {0, 0}
};

static const struct copy_format rg88_format = {
  VK_FORMAT_R8G8_UNORM, 1, {2, 0}, {0, 0}
};

static GstMemory *
create_copy_image (const struct copy_format *fmt, guint width, guint height,
    VkImageUsageFlags usage)
{
  GstMemory *mem;

  mem = gst_vulkan_image_memory_alloc (device, fmt->format, width, height,
      VK_IMAGE_TILING_OPTIMAL, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  fail_unless (gst_is_vulkan_image_memory (mem));

  return mem;
}

static guint
fill_regions (const struct copy_format *fmt, GstVulkanImageMemory * img,
    VkBufferImageCopy * regions)
{
  guint width = gst_vulkan_image_memory_get_width (img);
  guint height = gst_vulkan_image_memory_get_height (img);
  gsize offset = 0;
  guint i;

  for (i = 0; i < fmt->n_planes; i++) {
    guint w = width >> fmt->sub[i], h = height >> fmt->sub[i];
    VkBufferImageCopy region = {
      .bufferOffset = offset,
      .imageSubresource = {
            .aspectMask = fmt->n_planes == 1 ? VK_IMAGE_ASPECT_COLOR_BIT :
            VK_IMAGE_ASPECT_PLANE_0_BIT << i,
            .layerCount = 1,
          },
      .imageExtent = {w, h, 1},
    };

    regions[i] = region;
    offset += (gsize) fmt->bpp[i] * w * h;
  }

  return offset;
}

static GstVulkanOperation *
new_operation (GstVulkanQueue * queue)
{
  GstVulkanCommandPool *cmd_pool;
  GstVulkanOperation *exec;

  cmd_pool = gst_vulkan_queue_create_command_pool (queue, NULL);
  fail_unless (cmd_pool);
  exec = gst_vulkan_operation_new (cmd_pool);
  gst_object_unref (cmd_pool);

  return exec;
}

static GstMemory *
upload_image (GstVulkanOperation * exec, const struct copy_format *fmt,
    GstMemory * mem, guint8 seed)
{
  GstVulkanImageMemory *img = (GstVulkanImageMemory *) mem;
  VkBufferImageCopy regions[2];
  GstMemory *staging;
  GstBuffer *frame;
  GstMapInfo info;
  gsize size, i;

  size = fill_regions (fmt, img, regions);
  staging = gst_vulkan_buffer_memory_alloc (device, size,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  fail_unless (staging);
  fail_unless (gst_memory_map (staging, &info, GST_MAP_WRITE));
  for (i = 0; i < size; i++)
    info.data[i] = (i * 7 + seed) & 0xff;
  gst_memory_unmap (staging, &info);

  frame = gst_buffer_new ();
  gst_buffer_append_memory (frame, gst_memory_ref (mem));

  fail_unless (gst_vulkan_operation_begin (exec, NULL));
  fail_unless (gst_vulkan_operation_add_dependency_frame (exec, frame,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT));
  fail_unless (gst_vulkan_operation_add_frame_barrier (exec, frame,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
          NULL));
  gst_vulkan_barrier_state_pipeline_barrier (gst_vulkan_operation_get_barriers
      (exec), exec->cmd_buf, 0);
  gst_vulkan_command_buffer_lock (exec->cmd_buf);
  vkCmdCopyBufferToImage (exec->cmd_buf->cmd,
      ((GstVulkanBufferMemory *) staging)->buffer, img->image,
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, fmt->n_planes, regions);
  gst_vulkan_command_buffer_unlock (exec->cmd_buf);
  fail_unless (gst_vulkan_operation_end (exec, NULL));

  gst_buffer_unref (frame);

  return staging;
}

static void
check_image (const struct copy_format *fmt, GstMemory * mem, guint8 seed)
{
  GstVulkanImageMemory *img = (GstVulkanImageMemory *) mem;
  VkBufferImageCopy regions[2];
  GstVulkanOperation *exec;
  GstVulkanQueue *queue;
  GstMemory *staging;
  GstBuffer *frame;
  GstMapInfo info;
  gsize size, i;

  size = fill_regions (fmt, img, regions);
  staging = gst_vulkan_buffer_memory_alloc (device, size,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
      | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  fail_unless (staging);

  frame = gst_buffer_new ();
  gst_buffer_append_memory (frame, gst_memory_ref (mem));

  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (queue);
  exec = new_operation (queue);
  fail_unless (gst_vulkan_operation_begin (exec, NULL));
  fail_unless (gst_vulkan_operation_add_dependency_frame (exec, frame,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT));
  fail_unless (gst_vulkan_operation_add_frame_barrier (exec, frame,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
          VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
          NULL));
  gst_vulkan_barrier_state_pipeline_barrier (gst_vulkan_operation_get_barriers
      (exec), exec->cmd_buf, 0);
  gst_vulkan_command_buffer_lock (exec->cmd_buf);
  vkCmdCopyImageToBuffer (exec->cmd_buf->cmd, img->image,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      ((GstVulkanBufferMemory *) staging)->buffer, fmt->n_planes, regions);
  gst_vulkan_command_buffer_unlock (exec->cmd_buf);
  fail_unless (gst_vulkan_operation_end (exec, NULL));
  fail_unless (gst_vulkan_operation_wait (exec));

  fail_unless (gst_memory_map (staging, &info, GST_MAP_READ));
  for (i = 0; i < size; i++) {
    if (info.data[i] != ((i * 7 + seed) & 0xff))
      break;
  }
  gst_memory_unmap (staging, &info);
  fail_unless (i == size, "image data differs at byte %" G_GSIZE_FORMAT, i);

  gst_object_unref (exec);
  gst_object_unref (queue);
  gst_buffer_unref (frame);
  gst_memory_unref (staging);
}

static void
check_image_copy (GstMemory * mem, GstMemory * copy)
{
  GstVulkanImageMemory *img = (GstVulkanImageMemory *) mem;
  GstVulkanImageMemory *img_copy = (GstVulkanImageMemory *) copy;

  fail_unless (gst_is_vulkan_image_memory (copy));
  fail_unless (img_copy->image != img->image);
  fail_unless (img_copy->device == img->device);
  fail_unless_equals_int (img_copy->create_info.format,
      img->create_info.format);
  fail_unless_equals_int (img_copy->create_info.extent.width,
      img->create_info.extent.width);
  fail_unless_equals_int (img_copy->create_info.extent.height,
      img->create_info.extent.height);
  fail_unless_equals_int (img_copy->create_info.tiling,
      img->create_info.tiling);
  fail_unless_equals_int (img_copy->usage, img->usage
      | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
  fail_unless_equals_int (img_copy->vk_mem->properties,
      img->vk_mem->properties);
}

static void
check_copy_format (const struct copy_format *fmt, guint width, guint height)
{
  GstVulkanOperation *exec;
  GstVulkanQueue *queue;
  GstMemory *mem, *copy, *staging;

  mem = create_copy_image (fmt, width, height,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT);

  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (queue);
  exec = new_operation (queue);
  staging = upload_image (exec, fmt, mem, 17);
  fail_unless (gst_vulkan_operation_wait (exec));

  copy = gst_memory_copy (mem, 0, -1);
  fail_unless (copy);
  check_image_copy (mem, copy);

  check_image (fmt, mem, 17);
  check_image (fmt, copy, 17);

  gst_memory_unref (staging);
  staging = upload_image (exec, fmt, mem, 23);
  fail_unless (gst_vulkan_operation_wait (exec));
  check_image (fmt, mem, 23);
  check_image (fmt, copy, 17);

  gst_object_unref (exec);
  gst_object_unref (queue);
  gst_memory_unref (staging);
  gst_memory_unref (copy);
  gst_memory_unref (mem);
}

GST_START_TEST (test_image_copy_rgba)
{
  check_copy_format (&rgba_format, 64, 32);
}

GST_END_TEST;

GST_START_TEST (test_image_copy_nv12)
{
  check_copy_format (&nv12_format, 64, 32);
}

GST_END_TEST;

static gboolean
find_other_queue (GstVulkanDevice * device, GstVulkanQueue * queue,
    GstVulkanQueue ** other)
{
  VkQueueFlags flags =
      device->physical_device->queue_family_props[queue->family].queueFlags;

  if ((queue->family != (*other)->family || queue->index != (*other)->index)
      && (flags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT
              | VK_QUEUE_TRANSFER_BIT))) {
    gst_object_unref (*other);
    *other = gst_object_ref (queue);
    return FALSE;
  }

  return TRUE;
}

GST_START_TEST (test_image_copy_after_write)
{
  GstVulkanImageMemory *img;
  GstVulkanOperation *exec;
  GstVulkanQueue *queue;
  GstMemory *mem, *copy, *staging;

  mem = create_copy_image (&nv12_format, 1920, 1088,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT);
  img = (GstVulkanImageMemory *) mem;

  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (queue);
  gst_vulkan_device_foreach_queue (device,
      (GstVulkanDeviceForEachQueueFunc) find_other_queue, &queue);
  GST_INFO ("writing on queue family %u index %u", queue->family, queue->index);

  exec = new_operation (queue);
  staging = upload_image (exec, &nv12_format, mem, 29);

  copy = gst_memory_copy (mem, 0, -1);
  fail_unless (copy);
  check_image_copy (mem, copy);
  if (img->timeline_semaphore) {
    gst_vulkan_timeline_semaphore_lock (img->timeline_semaphore);
    fail_unless_equals_int (gst_vulkan_timeline_semaphore_peek_unlocked
        (img->timeline_semaphore), 2);
    gst_vulkan_timeline_semaphore_unlock (img->timeline_semaphore);
  }

  check_image (&nv12_format, copy, 29);
  check_image (&nv12_format, mem, 29);

  fail_unless (gst_vulkan_operation_wait (exec));
  gst_object_unref (exec);
  gst_object_unref (queue);
  gst_memory_unref (staging);
  gst_memory_unref (copy);
  gst_memory_unref (mem);
}

GST_END_TEST;

#if defined(VK_KHR_synchronization2) && defined(VK_KHR_video_decode_queue)
GST_START_TEST (test_image_copy_video_access)
{
  GstVulkanBarrierImageInfo old_info = { 0, }, new_info = { 0, };
  GstVulkanImageMemory *img;
  GstVulkanOperation *exec;
  GstVulkanQueue *queue;
  GstMemory *mem, *copy, *staging;

  mem = create_copy_image (&nv12_format, 64, 32,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT);
  img = (GstVulkanImageMemory *) mem;

  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (queue);
  exec = new_operation (queue);
  staging = upload_image (exec, &nv12_format, mem, 41);
  fail_unless (gst_vulkan_operation_wait (exec));

  gst_vulkan_image_memory_lock (img);
  gst_vulkan_image_memory_peek_barrier_unlocked (img, &old_info);
  gst_vulkan_barrier_image_info_copy_into (&old_info, &new_info);
  new_info.parent.access_flags = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
  fail_unless (gst_vulkan_image_memory_compare_exchange_barrier_unlocked (img,
          &old_info, &new_info));
  gst_vulkan_image_memory_unlock (img);
  gst_vulkan_barrier_image_info_clear (&old_info);
  gst_vulkan_barrier_image_info_clear (&new_info);

  copy = gst_memory_copy (mem, 0, -1);
  fail_unless (copy);
  check_image_copy (mem, copy);

  gst_vulkan_image_memory_lock (img);
  gst_vulkan_image_memory_peek_barrier_unlocked (img, &old_info);
  gst_vulkan_image_memory_unlock (img);
  fail_unless_equals_int (old_info.image_layout,
      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  fail_unless_equals_int (old_info.parent.access_flags,
      VK_ACCESS_TRANSFER_READ_BIT);
  gst_vulkan_barrier_image_info_clear (&old_info);

  check_image (&nv12_format, copy, 41);

  gst_object_unref (exec);
  gst_object_unref (queue);
  gst_memory_unref (staging);
  gst_memory_unref (copy);
  gst_memory_unref (mem);
}

GST_END_TEST;
#endif

GST_START_TEST (test_image_buffer_copy_deep)
{
  GstVulkanOperation *exec;
  GstVulkanQueue *queue;
  GstMemory *planes[2], *staging[2];
  GstBuffer *buffer, *copy;
  guint i;

  planes[0] = create_copy_image (&r8_format, 64, 32,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT);
  planes[1] = create_copy_image (&rg88_format, 32, 16,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT
      | VK_IMAGE_USAGE_SAMPLED_BIT);

  queue = gst_vulkan_device_select_queue (device, VK_QUEUE_GRAPHICS_BIT);
  fail_unless (queue);
  exec = new_operation (queue);
  staging[0] = upload_image (exec, &r8_format, planes[0], 31);
  staging[1] = upload_image (exec, &rg88_format, planes[1], 37);

  buffer = gst_buffer_new ();
  gst_buffer_append_memory (buffer, planes[0]);
  gst_buffer_append_memory (buffer, planes[1]);

  copy = gst_buffer_copy_deep (buffer);
  fail_unless (copy);
  fail_unless_equals_int (gst_buffer_n_memory (copy), 2);
  for (i = 0; i < 2; i++) {
    check_image_copy (gst_buffer_peek_memory (buffer, i),
        gst_buffer_peek_memory (copy, i));
  }

  check_image (&r8_format, gst_buffer_peek_memory (copy, 0), 31);
  check_image (&rg88_format, gst_buffer_peek_memory (copy, 1), 37);

  fail_unless (gst_vulkan_operation_wait (exec));
  gst_object_unref (exec);
  gst_object_unref (queue);
  gst_memory_unref (staging[0]);
  gst_memory_unref (staging[1]);
  gst_buffer_unref (copy);
  gst_buffer_unref (buffer);
}

GST_END_TEST;

GST_START_TEST (test_image_copy_unsupported)
{
  GstMemory *mem;

  mem = create_copy_image (&rgba_format, 64, 32,
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  fail_unless (gst_memory_copy (mem, 1, -1) == NULL);
  fail_unless (gst_memory_copy (mem, 0, 16) == NULL);
  gst_memory_unref (mem);

  mem = create_copy_image (&rgba_format, 64, 32,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
  fail_unless (gst_memory_copy (mem, 0, -1) == NULL);
  gst_memory_unref (mem);
}

GST_END_TEST;

static Suite *
vkimage_suite (void)
{
  Suite *s = suite_create ("vkimage");
  TCase *tc_basic = tcase_create ("general");
  gboolean have_instance;

  suite_add_tcase (s, tc_basic);
  tcase_add_checked_fixture (tc_basic, setup, teardown);

  instance = gst_vulkan_instance_new ();
  have_instance = gst_vulkan_instance_open (instance, NULL);
  gst_object_unref (instance);
  if (have_instance) {
    tcase_add_test (tc_basic, test_image_new);
    tcase_add_test (tc_basic, test_image_wrapped_with_image_info);
    tcase_add_test (tc_basic, test_image_barrier_new);
    tcase_add_test (tc_basic, test_image_view_new);
    tcase_add_test (tc_basic, test_image_view_get);
    tcase_add_test (tc_basic, test_image_view_stress);
    tcase_add_test (tc_basic, test_image_view_mem_simultaneous_destruction);
    tcase_add_test (tc_basic, test_image_copy_rgba);
    tcase_add_test (tc_basic, test_image_copy_nv12);
    tcase_add_test (tc_basic, test_image_copy_after_write);
#if defined(VK_KHR_synchronization2) && defined(VK_KHR_video_decode_queue)
    tcase_add_test (tc_basic, test_image_copy_video_access);
#endif
    tcase_add_test (tc_basic, test_image_buffer_copy_deep);
    tcase_add_test (tc_basic, test_image_copy_unsupported);
  }

  return s;
}


#ifdef __APPLE__
GST_CHECK_MAIN_NOFORK (vkimage);
#else
GST_CHECK_MAIN (vkimage);
#endif
