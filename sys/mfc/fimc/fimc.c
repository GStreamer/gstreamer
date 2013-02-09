/*
 * Copyright (c) 2012 Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * This library is licensed under 2 different licenses and you
 * can choose to use it under the terms of any one of them. The
 * two licenses are the Apache License 2.0 and the LGPL.
 *
 * Apache License 2.0:
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * LGPL:
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
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <linux/videodev2.h>

/* for g_atomic_* */
#include <glib.h>

/* For logging */
#include <gst/gst.h>
GST_DEBUG_CATEGORY (fimc_debug);
#define GST_CAT_DEFAULT fimc_debug

#include "fimc.h"

struct _Fimc
{
  int fd;

  struct v4l2_capability caps;

  int set_src;
  int has_src_buffers;
  int streamon_src;
  FimcColorFormat src_format;
  struct v4l2_format src_fmt;
  struct v4l2_crop src_crop;
  struct v4l2_requestbuffers src_requestbuffers;

  int set_dst;
  int has_dst_buffers;
  int streamon_dst;
  FimcColorFormat dst_format;
  struct v4l2_format dst_fmt;
  struct v4l2_crop dst_crop;
  struct v4l2_requestbuffers dst_requestbuffers;

  struct v4l2_plane dst_planes[3];
  struct v4l2_buffer dst_buffer;
  void *dst_buffer_data[3];
  int dst_buffer_size[3];
};

#define FIMC_PATH "/dev/video4"

static volatile int fimc_in_use;

void
fimc_init_debug (void)
{
  GST_DEBUG_CATEGORY_INIT (fimc_debug, "fimc", 0, "FIMC library");
}

Fimc *
fimc_new (void)
{
  Fimc *fimc;

  if (!g_atomic_int_compare_and_exchange (&fimc_in_use, FALSE, TRUE)) {
    GST_ERROR ("Rejected because FIMC is already in use");
    return NULL;
  }

  fimc = calloc (1, sizeof (Fimc));

  fimc->fd = open (FIMC_PATH, O_RDWR, 0);
  if (fimc->fd == -1) {
    GST_ERROR ("Unable to open FIMC device node: %d", errno);
    fimc_free (fimc);
    return NULL;
  }

  /* check capabilities */

  if (ioctl (fimc->fd, VIDIOC_QUERYCAP, &fimc->caps) < 0) {
    GST_ERROR ("Unable to query capabilities: %d", errno);
    fimc_free (fimc);
    return NULL;
  }

  if ((fimc->caps.capabilities & V4L2_CAP_STREAMING) == 0 ||
      (fimc->caps.capabilities & V4L2_CAP_VIDEO_OUTPUT_MPLANE) == 0 ||
      (fimc->caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) {
    GST_ERROR ("Required capabilities not available");
    fimc_free (fimc);
    return NULL;
  }

  GST_DEBUG ("Created new FIMC context");

  return fimc;
}

void
fimc_free (Fimc * fimc)
{
  fimc_release_src_buffers (fimc);
  fimc_release_dst_buffers (fimc);

  if (fimc->fd != -1)
    close (fimc->fd);

  g_atomic_int_set (&fimc_in_use, FALSE);
  free (fimc);
}

static int
fimc_color_format_to_v4l2 (FimcColorFormat format)
{
  switch (format) {
    case FIMC_COLOR_FORMAT_YUV420SPT:
      return V4L2_PIX_FMT_NV12MT;
    case FIMC_COLOR_FORMAT_YUV420SP:
      return V4L2_PIX_FMT_NV12M;
    case FIMC_COLOR_FORMAT_YUV420P:
      return V4L2_PIX_FMT_YUV420M;
    case FIMC_COLOR_FORMAT_RGB32:
      return V4L2_PIX_FMT_RGB32;
    default:
      break;
  }

  return -1;
}

static int
fimc_color_format_get_nplanes (FimcColorFormat format)
{
  switch (format) {
    case FIMC_COLOR_FORMAT_RGB32:
      return 1;
    case FIMC_COLOR_FORMAT_YUV420SPT:
    case FIMC_COLOR_FORMAT_YUV420SP:
      return 2;
    case FIMC_COLOR_FORMAT_YUV420P:
      return 3;
    default:
      break;
  }

  return -1;
}

static int
fimc_color_format_get_component_height (FimcColorFormat format, int c,
    int height)
{
  switch (format) {
    case FIMC_COLOR_FORMAT_RGB32:
      return height;
    case FIMC_COLOR_FORMAT_YUV420SPT:
    case FIMC_COLOR_FORMAT_YUV420SP:
    case FIMC_COLOR_FORMAT_YUV420P:
      if (c == 0)
        return height;
      else
        return (height + 1) / 2;
    default:
      break;
  }

  return -1;
}

int
fimc_set_src_format (Fimc * fimc, FimcColorFormat format, int width, int height,
    int stride[3], int crop_left, int crop_top, int crop_width, int crop_height)
{
  struct v4l2_format fmt;
  struct v4l2_crop crop;
  struct v4l2_control control;
  int i;

  /* Check if something changed */
  if (fimc->set_src &&
      fimc->src_fmt.fmt.pix_mp.width == width &&
      fimc->src_fmt.fmt.pix_mp.height == height &&
      fimc->src_fmt.fmt.pix_mp.pixelformat == fimc_color_format_to_v4l2 (format)
      && fimc->src_crop.c.left == crop_left && fimc->src_crop.c.top == crop_top
      && fimc->src_crop.c.width == crop_width
      && fimc->src_crop.c.height == crop_height) {
    if (fimc->src_fmt.fmt.pix_mp.plane_fmt[0].bytesperline == stride[0] &&
        fimc->src_fmt.fmt.pix_mp.plane_fmt[1].bytesperline == stride[1] &&
        fimc->src_fmt.fmt.pix_mp.plane_fmt[2].bytesperline == stride[2]) {
      GST_DEBUG ("Nothing has changed");
      return 0;
    }
  }

  /* Something has changed */
  fimc->set_src = 0;

  memset (&fmt, 0, sizeof (fmt));
  memset (&crop, 0, sizeof (crop));
  memset (&control, 0, sizeof (control));

  fimc->src_format = format;

  fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  fmt.fmt.pix_mp.width = width;
  fmt.fmt.pix_mp.height = height;
  fmt.fmt.pix_mp.pixelformat = fimc_color_format_to_v4l2 (format);
  fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
  fmt.fmt.pix_mp.num_planes = fimc_color_format_get_nplanes (format);

  for (i = 0; i < fmt.fmt.pix_mp.num_planes; i++) {
    fmt.fmt.pix_mp.plane_fmt[i].bytesperline = stride[i];
    fmt.fmt.pix_mp.plane_fmt[i].sizeimage =
        fimc_color_format_get_component_height (format, i, height) * stride[i];
  }

  if (ioctl (fimc->fd, VIDIOC_S_FMT, &fmt) < 0) {
    GST_ERROR ("Failed to set src format: %d", errno);
    return -1;
  }

  fimc->src_fmt = fmt;

  crop.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  crop.c.left = crop_left;
  crop.c.top = crop_top;
  crop.c.width = crop_width;
  crop.c.height = crop_height;

  if (ioctl (fimc->fd, VIDIOC_S_CROP, &crop) < 0) {
    GST_ERROR ("Failed to set src crop: %d", errno);
    return -1;
  }

  fimc->src_crop = crop;

  control.id = V4L2_CID_ROTATE;
  control.value = 0;

  if (ioctl (fimc->fd, VIDIOC_S_CTRL, &control) < 0) {
    GST_ERROR ("Failed to set rotation to 0: %d", errno);
    return -1;
  }

  fimc->set_src = 1;

  return 0;
}

int
fimc_request_src_buffers (Fimc * fimc)
{
  struct v4l2_requestbuffers requestbuffers;

  if (fimc->has_dst_buffers) {
    GST_ERROR ("Already have dst buffers");
    return -1;
  }

  fimc->has_src_buffers = 0;

  memset (&requestbuffers, 0, sizeof (requestbuffers));

  requestbuffers.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
  requestbuffers.memory = V4L2_MEMORY_USERPTR;
  requestbuffers.count = 1;

  if (ioctl (fimc->fd, VIDIOC_REQBUFS, &requestbuffers) < 0) {
    GST_ERROR ("Failed to request src buffers: %d", errno);
    return -1;
  }

  fimc->src_requestbuffers = requestbuffers;

  if (requestbuffers.count < 1) {
    GST_ERROR ("Got %d buffers instead of %d", requestbuffers.count, 1);
    return -1;
  }

  fimc->has_src_buffers = 1;

  return 0;
}

int
fimc_release_src_buffers (Fimc * fimc)
{
  enum v4l2_buf_type type;

  if (fimc->streamon_src) {
    type = fimc->src_requestbuffers.type;
    if (ioctl (fimc->fd, VIDIOC_STREAMOFF, &type) < 0) {
      GST_ERROR ("Deactivating input stream failed: %d", errno);
      return -1;
    }
    fimc->streamon_src = 0;
  }

  /* Nothing to do here now */
  fimc->has_src_buffers = 0;

  return 0;
}

int
fimc_set_dst_format (Fimc * fimc, FimcColorFormat format, int width, int height,
    int stride[3], int crop_left, int crop_top, int crop_width, int crop_height)
{
  struct v4l2_format fmt;
  struct v4l2_crop crop;
  struct v4l2_control control;
  int i;

  /* Check if something changed */
  if (fimc->set_dst &&
      fimc->dst_fmt.fmt.pix_mp.width == width &&
      fimc->dst_fmt.fmt.pix_mp.height == height &&
      fimc->dst_fmt.fmt.pix_mp.pixelformat == fimc_color_format_to_v4l2 (format)
      && fimc->dst_crop.c.left == crop_left && fimc->dst_crop.c.top == crop_top
      && fimc->dst_crop.c.width == crop_width
      && fimc->dst_crop.c.height == crop_height) {
    if (fimc->dst_fmt.fmt.pix_mp.plane_fmt[0].bytesperline == stride[0] &&
        fimc->dst_fmt.fmt.pix_mp.plane_fmt[1].bytesperline == stride[1] &&
        fimc->dst_fmt.fmt.pix_mp.plane_fmt[2].bytesperline == stride[2]) {
      GST_DEBUG ("Nothing has changed");
      return 0;
    }
  }

  /* Something has changed */
  fimc->set_dst = 0;

  memset (&fmt, 0, sizeof (fmt));
  memset (&crop, 0, sizeof (crop));
  memset (&control, 0, sizeof (control));

  fimc->dst_format = format;

  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  fmt.fmt.pix_mp.width = width;
  fmt.fmt.pix_mp.height = height;
  fmt.fmt.pix_mp.pixelformat = fimc_color_format_to_v4l2 (format);
  fmt.fmt.pix_mp.field = V4L2_FIELD_ANY;
  fmt.fmt.pix_mp.num_planes = fimc_color_format_get_nplanes (format);

  for (i = 0; i < fmt.fmt.pix_mp.num_planes; i++) {
    fmt.fmt.pix_mp.plane_fmt[i].bytesperline = stride[i];
    fmt.fmt.pix_mp.plane_fmt[i].sizeimage =
        fimc_color_format_get_component_height (format, i, height) * stride[i];
  }

  if (ioctl (fimc->fd, VIDIOC_S_FMT, &fmt) < 0) {
    GST_ERROR ("Failed to set dst format: %d", errno);
    return -1;
  }

  fimc->dst_fmt = fmt;

  crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  crop.c.left = crop_left;
  crop.c.top = crop_top;
  crop.c.width = crop_width;
  crop.c.height = crop_height;

  if (ioctl (fimc->fd, VIDIOC_S_CROP, &crop) < 0) {
    GST_ERROR ("Failed to set dst crop: %d", errno);
    return -1;
  }

  fimc->dst_crop = crop;

  control.id = V4L2_CID_ROTATE;
  control.value = 0;

  if (ioctl (fimc->fd, VIDIOC_S_CTRL, &control) < 0) {
    GST_ERROR ("Failed to set rotation to 0: %d", errno);
    return -1;
  }

  fimc->set_dst = 1;

  return 0;
}

int
fimc_request_dst_buffers (Fimc * fimc)
{
  struct v4l2_requestbuffers requestbuffers;

  if (fimc->has_dst_buffers) {
    GST_ERROR ("Already have dst buffers");
    return -1;
  }

  fimc->has_dst_buffers = 0;

  memset (&requestbuffers, 0, sizeof (requestbuffers));

  requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  requestbuffers.memory = V4L2_MEMORY_USERPTR;
  requestbuffers.count = 1;

  if (ioctl (fimc->fd, VIDIOC_REQBUFS, &requestbuffers) < 0) {
    GST_ERROR ("Failed to request dst buffers: %d", errno);
    return -1;
  }

  fimc->dst_requestbuffers = requestbuffers;

  if (requestbuffers.count < 1) {
    GST_ERROR ("Got %d buffers instead of %d", requestbuffers.count, 1);
    return -1;
  }

  fimc->has_dst_buffers = 1;

  return 0;
}

int
fimc_request_dst_buffers_mmap (Fimc * fimc, void *dst[3], int stride[3])
{
  struct v4l2_requestbuffers requestbuffers;
  struct v4l2_plane planes[3];
  struct v4l2_buffer buffer;
  int i;

  if (fimc->has_dst_buffers) {
    GST_ERROR ("Already have dst buffers");
    return -1;
  }

  fimc->has_dst_buffers = 0;

  memset (&requestbuffers, 0, sizeof (requestbuffers));
  memset (planes, 0, sizeof (planes));
  memset (&buffer, 0, sizeof (buffer));

  requestbuffers.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
  requestbuffers.memory = V4L2_MEMORY_MMAP;
  requestbuffers.count = 1;

  if (ioctl (fimc->fd, VIDIOC_REQBUFS, &requestbuffers) < 0) {
    GST_ERROR ("Failed to request dst buffers: %d", errno);
    return -1;
  }

  fimc->dst_requestbuffers = requestbuffers;

  if (requestbuffers.count < 1) {
    GST_ERROR ("Got %d buffers instead of %d", requestbuffers.count, 1);
    return -1;
  }

  buffer.type = fimc->dst_requestbuffers.type;
  buffer.memory = fimc->dst_requestbuffers.memory;
  buffer.index = 0;
  buffer.length = fimc_color_format_get_nplanes (fimc->dst_format);
  buffer.m.planes = planes;

  if (ioctl (fimc->fd, VIDIOC_QUERYBUF, &buffer) < 0) {
    GST_ERROR ("Query of output buffers failed: %d", errno);
    return -1;
  }

  fimc->dst_planes[0] = planes[0];
  fimc->dst_planes[1] = planes[1];
  fimc->dst_planes[2] = planes[2];
  fimc->dst_buffer = buffer;
  fimc->dst_buffer.m.planes = fimc->dst_planes;

  for (i = 0; i < buffer.length; i++) {
    fimc->dst_buffer_data[i] =
        mmap (NULL, buffer.m.planes[i].length, PROT_READ | PROT_WRITE,
        MAP_SHARED, fimc->fd, buffer.m.planes[i].m.mem_offset);

    if (fimc->dst_buffer_data[i] == MAP_FAILED) {
      GST_ERROR ("Failed to map output buffer %d", i);
      return -1;
    }
    fimc->dst_buffer_size[i] = buffer.m.planes[i].length;

    dst[i] = fimc->dst_buffer_data[i];
    stride[i] = fimc->dst_fmt.fmt.pix_mp.plane_fmt[i].bytesperline;
  }

  /* FIXME: The device reports wrong strides */
  if (fimc->dst_format == FIMC_COLOR_FORMAT_YUV420P) {
    stride[1] /= 2;
    stride[2] /= 2;
  }

  fimc->has_dst_buffers = 1;

  return 0;
}

int
fimc_release_dst_buffers (Fimc * fimc)
{
  enum v4l2_buf_type type;
  int i;

  if (fimc->streamon_dst) {
    type = fimc->dst_requestbuffers.type;
    if (ioctl (fimc->fd, VIDIOC_STREAMOFF, &type) < 0) {
      GST_ERROR ("Deactivating output stream failed: %d", errno);
      return -1;
    }
    fimc->streamon_dst = 0;
  }

  fimc->has_dst_buffers = 0;

  for (i = 0; i < 3; i++) {
    if (fimc->dst_buffer_data[i])
      munmap (fimc->dst_buffer_data[i], fimc->dst_buffer_size[i]);
  }

  return 0;
}

int
fimc_convert (Fimc * fimc, void *src[3], void *dst[3])
{
  struct v4l2_plane planes[3];
  struct v4l2_buffer buffer;
  enum v4l2_buf_type type;
  int i;

  if (!fimc->set_src || !fimc->set_dst ||
      !fimc->has_src_buffers || !fimc->has_dst_buffers) {
    GST_ERROR ("Not configured yet");
    return -1;
  }

  memset (planes, 0, sizeof (planes));
  memset (&buffer, 0, sizeof (buffer));

  buffer.type = fimc->src_requestbuffers.type;
  buffer.memory = fimc->src_requestbuffers.memory;
  buffer.length = fimc->src_fmt.fmt.pix_mp.num_planes;
  buffer.index = 0;
  buffer.m.planes = planes;

  for (i = 0; i < buffer.length; i++) {
    buffer.m.planes[i].length = fimc->src_fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
    buffer.m.planes[i].m.userptr = (unsigned long) src[i];
  }

  if (ioctl (fimc->fd, VIDIOC_QBUF, &buffer) < 0) {
    GST_ERROR ("Failed to queue input buffer: %d", errno);
    return -1;
  }

  memset (planes, 0, sizeof (planes));
  memset (&buffer, 0, sizeof (buffer));

  buffer.type = fimc->dst_requestbuffers.type;
  buffer.memory = fimc->dst_requestbuffers.memory;
  buffer.length = fimc->dst_fmt.fmt.pix_mp.num_planes;
  buffer.index = 0;
  buffer.m.planes = planes;

  for (i = 0; i < buffer.length; i++) {
    buffer.m.planes[i].length = fimc->dst_fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
    if (fimc->dst_requestbuffers.memory == V4L2_MEMORY_MMAP)
      buffer.m.planes[i].m.mem_offset = fimc->dst_planes[i].m.mem_offset;
    else
      buffer.m.planes[i].m.userptr = (unsigned long) dst[i];
  }

  if (ioctl (fimc->fd, VIDIOC_QBUF, &buffer) < 0) {
    GST_ERROR ("Failed to queue output buffer: %d", errno);
    return -1;
  }

  if (!fimc->streamon_src) {
    type = fimc->src_requestbuffers.type;
    if (ioctl (fimc->fd, VIDIOC_STREAMON, &type) < 0) {
      GST_ERROR ("Activating input stream failed: %d", errno);
      return -1;
    }
    fimc->streamon_src = 1;
  }

  if (!fimc->streamon_dst) {
    type = fimc->dst_requestbuffers.type;
    if (ioctl (fimc->fd, VIDIOC_STREAMON, &type) < 0) {
      GST_ERROR ("Activating output stream failed: %d", errno);
      return -1;
    }
    fimc->streamon_dst = 1;
  }

  memset (planes, 0, sizeof (planes));
  memset (&buffer, 0, sizeof (buffer));

  buffer.type = fimc->src_requestbuffers.type;
  buffer.memory = fimc->src_requestbuffers.memory;
  buffer.length = fimc->src_fmt.fmt.pix_mp.num_planes;
  buffer.m.planes = planes;

  if (ioctl (fimc->fd, VIDIOC_DQBUF, &buffer) < 0) {
    GST_ERROR ("Failed to dequeue input buffer: %d", errno);
    return -1;
  }

  memset (planes, 0, sizeof (planes));
  memset (&buffer, 0, sizeof (buffer));

  buffer.type = fimc->dst_requestbuffers.type;
  buffer.memory = fimc->dst_requestbuffers.memory;
  buffer.length = fimc->dst_fmt.fmt.pix_mp.num_planes;
  buffer.m.planes = planes;

  if (ioctl (fimc->fd, VIDIOC_DQBUF, &buffer) < 0) {
    GST_ERROR ("Failed to dequeue output buffer: %d", errno);
    return -1;
  }

  return 0;
}
