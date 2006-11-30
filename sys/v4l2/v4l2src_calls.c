/* GStreamer
 *
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
 *               2006 Edgard Lima <edgard.lima@indt.org.br>
 *
 * v4l2src.c - system calls
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

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <string.h>
#include <errno.h>
#include "v4l2src_calls.h"
#include <sys/time.h>
#include <unistd.h>

#include "gstv4l2tuner.h"

GST_DEBUG_CATEGORY_EXTERN (v4l2src_debug);
#define GST_CAT_DEFAULT v4l2src_debug

/* lalala... */
#define GST_V4L2_SET_ACTIVE(element) (element)->buffer = GINT_TO_POINTER (-1)
#define GST_V4L2_SET_INACTIVE(element) (element)->buffer = NULL

#define DEBUG(format, args...) \
	GST_CAT_DEBUG_OBJECT (\
		v4l2src_debug, v4l2src, \
		"V4L2SRC: " format, ##args)

/* On some systems MAP_FAILED seems to be missing */
#ifndef MAP_FAILED
#define MAP_FAILED ((caddr_t) -1)
#endif

/******************************************************
 * gst_v4l2src_fill_format_list():
 *   create list of supported capture formats
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_fill_format_list (GstV4l2Src * v4l2src)
{
  gint n;
  struct v4l2_fmtdesc *format;

  GST_DEBUG_OBJECT (v4l2src, "getting src format enumerations");

  /* format enumeration */
  for (n = 0;; n++) {
    format = g_new (struct v4l2_fmtdesc, 1);

    format->index = n;
    format->type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_ENUM_FMT, format) < 0) {
      if (errno == EINVAL) {
        break;                  /* end of enumeration */
      } else
        goto failed;
    }
    GST_LOG_OBJECT (v4l2src, "got format %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (format->pixelformat));

    v4l2src->formats = g_slist_prepend (v4l2src->formats, format);
  }
  return TRUE;

  /* ERRORS */
failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Failed to enumerate possible video formats device '%s' can work with"), v4l2src->v4l2object->videodev), ("Failed to get number %d in pixelformat enumeration for %s. (%d - %s)", n, v4l2src->v4l2object->videodev, errno, g_strerror (errno)));
    g_free (format);
    return FALSE;
  }
}

/******************************************************
 * gst_v4l2src_clear_format_list():
 *   free list of supported capture formats
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_clear_format_list (GstV4l2Src * v4l2src)
{
  g_slist_foreach (v4l2src->formats, (GFunc) g_free, NULL);
  g_slist_free (v4l2src->formats);
  v4l2src->formats = NULL;

  return TRUE;
}


/******************************************************
 * gst_v4l2src_queue_frame():
 *   queue a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_queue_frame (GstV4l2Src * v4l2src, guint i)
{
  GST_LOG_OBJECT (v4l2src, "queueing frame %u", i);

  if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_QBUF,
          &v4l2src->pool->buffers[i].buffer) < 0)
    goto qbuf_failed;

  return TRUE;

  /* ERRORS */
qbuf_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, WRITE,
        (_("Could not exchange data with device '%s'."),
            v4l2src->v4l2object->videodev),
        ("Error queueing buffer %u on device %s. system error: %s", i,
            v4l2src->v4l2object->videodev, g_strerror (errno)));
    return FALSE;
  }
}

/******************************************************
 * gst_v4l2src_grab_frame ():
 *   grab a frame for capturing
 * return value: The captured frame number or -1 on error.
 ******************************************************/
gint
gst_v4l2src_grab_frame (GstV4l2Src * v4l2src)
{
#define NUM_TRIALS 100
  struct v4l2_buffer buffer;
  gint32 trials = NUM_TRIALS;

  memset (&buffer, 0x00, sizeof (buffer));
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = v4l2src->breq.memory;
  while (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_DQBUF, &buffer) < 0) {
    /* if the sync() got interrupted, we can retry */
    switch (errno) {
      case EAGAIN:
        GST_WARNING_OBJECT (v4l2src,
            "Non-blocking I/O has been selected using O_NONBLOCK and"
            " no buffer was in the outgoing queue. device %s",
            v4l2src->v4l2object->videodev);
        break;
      case EINVAL:
        goto einval;
      case ENOMEM:
        goto nomem;
      case EIO:
        GST_WARNING_OBJECT (v4l2src,
            "VIDIOC_DQBUF failed due to an internal error."
            " Can also indicate temporary problems like signal loss."
            " Note the driver might dequeue an (empty) buffer despite"
            " returning an error, or even stop capturing."
            " device %s", v4l2src->v4l2object->videodev);
        break;
      case EINTR:
        GST_WARNING_OBJECT (v4l2src,
            "could not sync on a buffer on device %s",
            v4l2src->v4l2object->videodev);
        break;
      default:
        GST_WARNING_OBJECT (v4l2src,
            "Grabbing frame got interrupted on %s. No expected reason.",
            v4l2src->v4l2object->videodev);
        break;
    }

    /* check nr. of attempts to capture */
    if (--trials == -1) {
      goto too_many_trials;
    } else {
      if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_QBUF, &buffer) < 0)
        goto qbuf_failed;
      memset (&buffer, 0x00, sizeof (buffer));
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = v4l2src->breq.memory;
    }
  }

  GST_LOG_OBJECT (v4l2src, "grabbed frame %d", buffer.index);

  return buffer.index;

  /* ERRORS */
einval:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'."),
            v4l2src->v4l2object->videodev),
        (_("The buffer type is not supported, or the index is out of bounds,"
                " or no buffers have been allocated yet, or the userptr"
                " or length are invalid. device %s"),
            v4l2src->v4l2object->videodev));
    return -1;
  }
nomem:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'. Not enough memory."), v4l2src->v4l2object->videodev), (_("insufficient memory to enqueue a user pointer buffer. device %s."), v4l2src->v4l2object->videodev));
    return -1;
  }
too_many_trials:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, FAILED,
        (_("Failed trying to get video frames from device '%s'."),
            v4l2src->v4l2object->videodev),
        (_("Failed after %d tries. device %s. system error: %s"),
            NUM_TRIALS, v4l2src->v4l2object->videodev, g_strerror (errno)));
    return -1;
  }
qbuf_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, WRITE,
        (_("Could not exchange data with device '%s'."),
            v4l2src->v4l2object->videodev),
        ("Error queueing buffer on device %s. system error: %s",
            v4l2src->v4l2object->videodev, g_strerror (errno)));
    return -1;
  }
}


/******************************************************
 * gst_v4l2src_get_capture():
 *   get capture parameters
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_get_capture (GstV4l2Src * v4l2src)
{
  DEBUG ("Getting capture format");

  GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);

  memset (&v4l2src->format, 0, sizeof (struct v4l2_format));
  v4l2src->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_G_FMT, &v4l2src->format) < 0)
    goto fmt_failed;

  return TRUE;

  /* ERRORS */
fmt_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Failed querying in which video format device '%s' is working with"),
            v4l2src->v4l2object->videodev),
        ("Failed VIDIOC_G_FMT for %s. (%d - %s)",
            v4l2src->v4l2object->videodev, errno, g_strerror (errno)));
    return FALSE;
  }
}


/******************************************************
 * gst_v4l2src_set_capture():
 *   set capture parameters
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_set_capture (GstV4l2Src * v4l2src,
    struct v4l2_fmtdesc * fmt, gint * width, gint * height,
    guint * fps_n, guint * fps_d)
{
  guint new_fps_n = *fps_n;
  guint new_fps_d = *fps_d;

  DEBUG ("Setting capture format to %dx%d, format %s",
      *width, *height, fmt->description);

  GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2src->v4l2object);

  /* error was posted */
  if (!gst_v4l2src_get_capture (v4l2src))
    goto fail;

  v4l2src->format.fmt.pix.width = *width;
  v4l2src->format.fmt.pix.height = *height;
  v4l2src->format.fmt.pix.pixelformat = fmt->pixelformat;
  v4l2src->format.fmt.pix.field = V4L2_FIELD_INTERLACED;
  v4l2src->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_S_FMT, &v4l2src->format) < 0)
    goto fmt_failed;

  if (*width != v4l2src->format.fmt.pix.width ||
      *height != v4l2src->format.fmt.pix.height) {
    GST_ELEMENT_WARNING (v4l2src, STREAM, WRONG_TYPE,
        (_("The closest size from %dx%d is %dx%d, for video format %s on device '%s'"), *width, *height, v4l2src->format.fmt.pix.width, v4l2src->format.fmt.pix.height, fmt->description, v4l2src->v4l2object->videodev), ("Updating size from %dx%d to %dx%d, format %s", *width, *height, v4l2src->format.fmt.pix.width, v4l2src->format.fmt.pix.height, fmt->description));
  }

  /* update internal info, posted error */
  if (!gst_v4l2src_get_capture (v4l2src))
    goto fail;

  if (fmt->pixelformat != v4l2src->format.fmt.pix.pixelformat)
    goto pixfmt_failed;

  if (*fps_n) {
    if (gst_v4l2src_set_fps (v4l2src, &new_fps_n, &new_fps_d)) {
      if (new_fps_n != *fps_n || new_fps_d != *fps_d) {
        GST_ELEMENT_WARNING (v4l2src, STREAM, WRONG_TYPE,
            (_("The closest framerate from %u/%u is %u/%u, on device '%s'"),
                *fps_n, *fps_d, new_fps_n, new_fps_d,
                v4l2src->v4l2object->videodev),
            ("Updating framerate from %u/%u to %u%u", *fps_n, *fps_d, new_fps_n,
                new_fps_d));

        *fps_n = new_fps_n;
        *fps_d = new_fps_d;
      }
    }
  } else {
    if (gst_v4l2src_get_fps (v4l2src, &new_fps_n, &new_fps_d)) {
      DEBUG ("framerate is %u/%u", new_fps_n, new_fps_d);
      *fps_n = new_fps_n;
      *fps_d = new_fps_d;
    }
  }

  *width = v4l2src->format.fmt.pix.width;
  *height = v4l2src->format.fmt.pix.height;

  return TRUE;

  /* ERRORS */
fmt_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Failed setting the video format for device '%s'"),
            v4l2src->v4l2object->videodev),
        ("Failed to set pixelformat to %s @ %dx%d for device %s. (%d - %s)",
            fmt->description, *width, *height,
            v4l2src->v4l2object->videodev, errno, g_strerror (errno)));
    return FALSE;
  }
pixfmt_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Failed setting the video format for device '%s'"),
            v4l2src->v4l2object->videodev),
        ("Failed to set pixelformat to %s @ %dx%d for device %s. (%d - %s)",
            fmt->description, *width, *height,
            v4l2src->v4l2object->videodev, errno, g_strerror (errno)));
    return FALSE;
  }
fail:
  {
    /* ERROR was posted */
    return FALSE;
  }
}

/******************************************************
 * gst_v4l2src_capture_init():
 *   initialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_capture_init (GstV4l2Src * v4l2src)
{
  gint n;
  guint buffers;
  GstV4l2Buffer *buffer;

  GST_DEBUG_OBJECT (v4l2src, "initting the capture system");

  GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2src->v4l2object);

  /* request buffer info */
  buffers = v4l2src->breq.count;

  if (v4l2src->breq.count > GST_V4L2_MAX_BUFFERS)
    v4l2src->breq.count = GST_V4L2_MAX_BUFFERS;
  else if (v4l2src->breq.count < GST_V4L2_MIN_BUFFERS)
    v4l2src->breq.count = GST_V4L2_MIN_BUFFERS;

  v4l2src->breq.type = v4l2src->format.type;
  if (v4l2src->v4l2object->vcap.capabilities & V4L2_CAP_STREAMING) {
    v4l2src->breq.memory = V4L2_MEMORY_MMAP;
    if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_REQBUFS,
            &v4l2src->breq) < 0)
      goto reqbufs_failed;

    GST_LOG_OBJECT (v4l2src, "using default mmap method");
  } else if (v4l2src->v4l2object->vcap.capabilities & V4L2_CAP_READWRITE) {
    v4l2src->breq.memory = 0;
    GST_INFO_OBJECT (v4l2src, "using fallback read method");
  } else
    goto broken_driver;

  /* Determine the device's framerate */
  if (!gst_v4l2src_update_fps (v4l2src->v4l2object)) {
    GST_DEBUG_OBJECT (v4l2src, "frame rate is unknown.");
    v4l2src->fps_d = 1;
    v4l2src->fps_n = 0;
  }

  if (v4l2src->breq.memory > 0) {
    if (v4l2src->breq.count < GST_V4L2_MIN_BUFFERS)
      goto no_buffers;

    if (v4l2src->breq.count != buffers)
      g_object_notify (G_OBJECT (v4l2src), "num_buffers");

    GST_INFO_OBJECT (v4l2src,
        "Got %d buffers (%" GST_FOURCC_FORMAT ") of size %d KB",
        v4l2src->breq.count,
        GST_FOURCC_ARGS (v4l2src->format.fmt.pix.pixelformat),
        v4l2src->format.fmt.pix.sizeimage / 1024);

    /* Map the buffers */
    GST_LOG_OBJECT (v4l2src, "initiating buffer pool");

    v4l2src->pool = g_new (GstV4l2BufferPool, 1);
    gst_atomic_int_set (&v4l2src->pool->refcount, 1);
    v4l2src->pool->video_fd = v4l2src->v4l2object->video_fd;
    v4l2src->pool->buffer_count = v4l2src->breq.count;
    v4l2src->pool->buffers = g_new0 (GstV4l2Buffer, v4l2src->breq.count);

    for (n = 0; n < v4l2src->breq.count; n++) {
      buffer = &v4l2src->pool->buffers[n];

      gst_atomic_int_set (&buffer->refcount, 1);
      buffer->pool = v4l2src->pool;
      memset (&buffer->buffer, 0x00, sizeof (buffer->buffer));
      buffer->buffer.index = n;
      buffer->buffer.type = v4l2src->breq.type;
      buffer->buffer.memory = v4l2src->breq.memory;

      if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_QUERYBUF,
              &buffer->buffer) < 0)
        goto querybuf_failed;

      buffer->start =
          mmap (0, buffer->buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
          v4l2src->v4l2object->video_fd, buffer->buffer.m.offset);

      if (buffer->start == MAP_FAILED)
        goto mmap_failed;

      buffer->length = buffer->buffer.length;
      if (!gst_v4l2src_queue_frame (v4l2src, n))
        goto queue_failed;
    }
  } else {
    GST_LOG_OBJECT (v4l2src, "no buffer pool used");
    v4l2src->pool = NULL;
  }

  GST_V4L2_SET_ACTIVE (v4l2src->v4l2object);

  return TRUE;

  /* ERRORS */
reqbufs_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not get buffers from device '%s'."),
            v4l2src->v4l2object->videodev),
        ("error requesting %d buffers. (%d - %s)",
            v4l2src->breq.count, errno, g_strerror (errno)));
    return FALSE;
  }
broken_driver:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("The driver of device '%s' is broken."),
            v4l2src->v4l2object->videodev),
        ("no supported read capability from %s. (%d - %s)",
            v4l2src->v4l2object->videodev, errno, g_strerror (errno)));
    return FALSE;
  }
no_buffers:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not get enough buffers from device '%s'."),
            v4l2src->v4l2object->videodev),
        ("we received %d from device '%s', we want at least %d. (%d - %s))",
            v4l2src->breq.count, v4l2src->v4l2object->videodev,
            GST_V4L2_MIN_BUFFERS, errno, g_strerror (errno)));
    v4l2src->breq.count = buffers;
    return FALSE;
  }
querybuf_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not get properties of data comming from device '%s'"),
            v4l2src->v4l2object->videodev),
        ("Failed querying buffer properties. (%d - %s)",
            errno, g_strerror (errno)));
    gst_v4l2src_capture_deinit (v4l2src);
    return FALSE;
  }
mmap_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not map memory in device '%s'."),
            v4l2src->v4l2object->videodev),
        ("mmap failed. (%d - %s)", errno, g_strerror (errno)));
    gst_v4l2src_capture_deinit (v4l2src);
    buffer->start = 0;
    return FALSE;
  }
queue_failed:
  {
    gst_v4l2src_capture_deinit (v4l2src);
    return FALSE;
  }
}


/******************************************************
 * gst_v4l2src_capture_start():
 *   start streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_capture_start (GstV4l2Src * v4l2src)
{
  gint type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  GST_DEBUG_OBJECT (v4l2src, "starting the capturing");
  GST_V4L2_CHECK_ACTIVE (v4l2src->v4l2object);

  v4l2src->quit = FALSE;

  if (v4l2src->breq.memory != 0) {
    if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_STREAMON, &type) < 0)
      goto streamon_failed;
  }

  v4l2src->is_capturing = TRUE;

  return TRUE;

  /* ERRORS */
streamon_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, OPEN_READ,
        (_("Error starting streaming capture from device '%s'."),
            v4l2src->v4l2object->videodev), GST_ERROR_SYSTEM);
    return FALSE;
  }
}

/******************************************************
 * gst_v4l2src_capture_stop():
 *   stop streaming capture
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_capture_stop (GstV4l2Src * v4l2src)
{
  gint type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  GST_DEBUG_OBJECT (v4l2src, "stopping capturing");

  if (!GST_V4L2_IS_OPEN (v4l2src->v4l2object)) {
    goto done;
  }
  if (!GST_V4L2_IS_ACTIVE (v4l2src->v4l2object)) {
    goto done;
  }

  if (v4l2src->breq.memory != 0) {
    /* we actually need to sync on all queued buffers but not
     * on the non-queued ones */
    if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_STREAMOFF, &type) < 0)
      goto streamoff_failed;
  }

done:

  /* make an optional pending wait stop */
  v4l2src->quit = TRUE;
  v4l2src->is_capturing = FALSE;

  return TRUE;

  /* ERRORS */
streamoff_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, CLOSE,
        (_("Error stopping streaming capture from device '%s'."),
            v4l2src->v4l2object->videodev), GST_ERROR_SYSTEM);
    return FALSE;
  }
}

static void
gst_v4l2src_buffer_pool_free (GstV4l2BufferPool * pool, gboolean do_close)
{
  guint i;

  for (i = 0; i < pool->buffer_count; i++) {
    gst_atomic_int_set (&pool->buffers[i].refcount, 0);
    munmap (pool->buffers[i].start, pool->buffers[i].length);
  }
  g_free (pool->buffers);
  gst_atomic_int_set (&pool->refcount, 0);
  if (do_close)
    close (pool->video_fd);
  g_free (pool);
}

/******************************************************
 * gst_v4l2src_capture_deinit():
 *   deinitialize the capture system
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_capture_deinit (GstV4l2Src * v4l2src)
{
  gint i;
  gboolean try_reinit = FALSE;

  GST_DEBUG_OBJECT (v4l2src, "deinitting capture system");

  if (!GST_V4L2_IS_OPEN (v4l2src->v4l2object)) {
    return TRUE;
  }
  if (!GST_V4L2_IS_ACTIVE (v4l2src->v4l2object)) {
    return TRUE;
  }

  if (v4l2src->pool) {
    /* free the buffers */
    for (i = 0; i < v4l2src->breq.count; i++) {
      if (g_atomic_int_dec_and_test (&v4l2src->pool->buffers[i].refcount)) {
        if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_DQBUF,
                &v4l2src->pool->buffers[i].buffer) < 0) {
          GST_DEBUG_OBJECT (v4l2src,
              "Could not dequeue buffer on uninitialization."
              "system error: %s. Will try reinit instead", g_strerror (errno));
          try_reinit = TRUE;
        }
      }
    }
    if (g_atomic_int_dec_and_test (&v4l2src->pool->refcount)) {
      /* we're last thing that used all this */
      gst_v4l2src_buffer_pool_free (v4l2src->pool, FALSE);
    }
    v4l2src->pool = NULL;
    /* This is our second try to get the buffers dequeued.
     * Since buffers are normally dequeued automatically when capturing is
     * stopped, but may be enqueued before capturing has started, you get
     * a problem when you abort before capturing started but have enqueued
     * the buffers. We avoid that by starting/stopping capturing once so
     * they get auto-dequeued.
     */
    if (try_reinit) {
      gst_v4l2src_capture_start (v4l2src);
      if (!gst_v4l2src_capture_stop (v4l2src)) {
        GST_DEBUG_OBJECT (v4l2src, "failed reinit device");
        return FALSE;
      }
    }
  }

  GST_V4L2_SET_INACTIVE (v4l2src->v4l2object);

  return TRUE;
}

/*
 */
gboolean
gst_v4l2src_get_size_limits (GstV4l2Src * v4l2src,
    struct v4l2_fmtdesc * format,
    gint * min_w, gint * max_w, gint * min_h, gint * max_h)
{

  struct v4l2_format fmt;

  GST_LOG_OBJECT (v4l2src,
      "getting size limits with format %" GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (format->pixelformat));

  /* get size delimiters */
  memset (&fmt, 0, sizeof (fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = 0;
  fmt.fmt.pix.height = 0;
  fmt.fmt.pix.pixelformat = format->pixelformat;
  fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
  if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_TRY_FMT, &fmt) < 0) {
    GST_DEBUG_OBJECT (v4l2src, "failed to get min size: %s",
        g_strerror (errno));
    return FALSE;
  }

  if (min_w)
    *min_w = fmt.fmt.pix.width;
  if (min_h)
    *min_h = fmt.fmt.pix.height;

  GST_LOG_OBJECT (v4l2src,
      "got min size %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);

  fmt.fmt.pix.width = GST_V4L2_MAX_SIZE;
  fmt.fmt.pix.height = GST_V4L2_MAX_SIZE;
  if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_TRY_FMT, &fmt) < 0) {
    GST_DEBUG_OBJECT (v4l2src, "failed to get max size: %s",
        g_strerror (errno));
    return FALSE;
  }

  if (max_w)
    *max_w = fmt.fmt.pix.width;
  if (max_h)
    *max_h = fmt.fmt.pix.height;

  GST_LOG_OBJECT (v4l2src,
      "got max size %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);

  return TRUE;
}

gboolean
gst_v4l2src_update_fps (GstV4l2Object * v4l2object)
{
  GstV4l2Src *v4l2src = GST_V4L2SRC (v4l2object->element);

  return gst_v4l2src_get_fps (v4l2src, &v4l2src->fps_n, &v4l2src->fps_d);
}


gboolean
gst_v4l2src_set_fps (GstV4l2Src * v4l2src, guint * fps_n, guint * fps_d)
{
  GstV4l2Object *v4l2object = v4l2src->v4l2object;
  struct v4l2_streamparm stream;

  GST_LOG_OBJECT (v4l2src, "setting fps %d / %d", *fps_n, *fps_d);

  memset (&stream, 0x00, sizeof (struct v4l2_streamparm));
  stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl (v4l2object->video_fd, VIDIOC_G_PARM, &stream) < 0)
    goto gparm_failed;

  if (!(stream.parm.capture.capability & V4L2_CAP_TIMEPERFRAME))
    goto no_timeperframe;

  /* Note: V4L2 needs the frame interval, we have the frame rate */
  stream.parm.capture.timeperframe.numerator = *fps_d;
  stream.parm.capture.timeperframe.denominator = *fps_n;

  if (ioctl (v4l2object->video_fd, VIDIOC_S_PARM, &stream) < 0)
    goto sparm_failed;

  /* Note: V4L2 gives us the frame interval, we need the frame rate */
  *fps_d = stream.parm.capture.timeperframe.numerator;
  *fps_n = stream.parm.capture.timeperframe.denominator;

  GST_LOG_OBJECT (v4l2src, "fps set to %d / %d", *fps_n, *fps_d);

  return TRUE;

  /* ERRORS */
gparm_failed:
  {
    GST_DEBUG_OBJECT (v4l2src, "failed to get PARM: %s", g_strerror (errno));
    return FALSE;
  }
no_timeperframe:
  {
    GST_DEBUG_OBJECT (v4l2src, "no V4L2_CAP_TIMEPERFRAME");
    return FALSE;
  }
sparm_failed:
  {
    GST_DEBUG_OBJECT (v4l2src, "failed to set PARM: %s", g_strerror (errno));
    return FALSE;
  }
}

gboolean
gst_v4l2src_get_fps (GstV4l2Src * v4l2src, guint * fps_n, guint * fps_d)
{
  GstV4l2Object *v4l2object = v4l2src->v4l2object;
  v4l2_std_id norm;
  struct v4l2_streamparm stream;
  const GList *item;
  gboolean found;

  if (!GST_V4L2_IS_OPEN (v4l2object))
    return FALSE;

  /* Try to get the frame rate directly from the device using VIDIOC_G_PARM */
  memset (&stream, 0x00, sizeof (struct v4l2_streamparm));
  stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl (v4l2object->video_fd, VIDIOC_G_PARM, &stream) < 0) {
    GST_DEBUG_OBJECT (v4l2src, "failed to get PARM: %s", g_strerror (errno));
    goto try_stds;
  }

  if (!(stream.parm.capture.capability & V4L2_CAP_TIMEPERFRAME)) {
    GST_DEBUG_OBJECT (v4l2src, "no V4L2_CAP_TIMEPERFRAME");
    goto try_stds;
  }

  /* Note: V4L2 gives us the frame interval, we need the frame rate */
  *fps_d = stream.parm.capture.timeperframe.numerator;
  *fps_n = stream.parm.capture.timeperframe.denominator;

  GST_DEBUG_OBJECT (v4l2src,
      "frame rate returned by G_PARM: %d/%d fps", *fps_n, *fps_d);
  /* and we are done now */
  goto done;

try_stds:
  /* If G_PARM failed, try to get the same information from the video standard */
  if (!gst_v4l2_get_norm (v4l2object, &norm))
    return FALSE;

  found = FALSE;
  for (item = v4l2object->norms; item != NULL; item = item->next) {
    GstV4l2TunerNorm *v4l2norm = item->data;

    if (v4l2norm->index == norm) {
      GValue *framerate = &GST_TUNER_NORM (v4l2norm)->framerate;

      *fps_n = gst_value_get_fraction_numerator (framerate);
      *fps_d = gst_value_get_fraction_denominator (framerate);

      GST_DEBUG_OBJECT (v4l2src,
          "frame rate returned by get_norm: %d/%d fps", *fps_n, *fps_d);
      found = TRUE;
      break;
    }
  }
  /* nothing found, that's an error */
  if (!found)
    goto failed;

done:
  return TRUE;

  /* ERRORS */
failed:
  {
    GST_DEBUG_OBJECT (v4l2src, "failed to get framerate");
    return FALSE;
  }
}

#define GST_TYPE_V4L2SRC_BUFFER (gst_v4l2src_buffer_get_type())
#define GST_IS_V4L2SRC_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_V4L2SRC_BUFFER))
#define GST_V4L2SRC_BUFFER(obj) (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_V4L2SRC_BUFFER, GstV4l2SrcBuffer))

typedef struct _GstV4l2SrcBuffer
{
  GstBuffer buffer;

  GstV4l2Buffer *buf;
} GstV4l2SrcBuffer;

static void gst_v4l2src_buffer_class_init (gpointer g_class,
    gpointer class_data);
static void gst_v4l2src_buffer_init (GTypeInstance * instance,
    gpointer g_class);
static void gst_v4l2src_buffer_finalize (GstV4l2SrcBuffer * v4l2src_buffer);

GType
gst_v4l2src_buffer_get_type (void)
{
  static GType _gst_v4l2src_buffer_type;

  if (G_UNLIKELY (_gst_v4l2src_buffer_type == 0)) {
    static const GTypeInfo v4l2src_buffer_info = {
      sizeof (GstBufferClass),
      NULL,
      NULL,
      gst_v4l2src_buffer_class_init,
      NULL,
      NULL,
      sizeof (GstV4l2SrcBuffer),
      0,
      gst_v4l2src_buffer_init,
      NULL
    };
    _gst_v4l2src_buffer_type = g_type_register_static (GST_TYPE_BUFFER,
        "GstV4l2SrcBuffer", &v4l2src_buffer_info, 0);
  }
  return _gst_v4l2src_buffer_type;
}

static void
gst_v4l2src_buffer_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_v4l2src_buffer_finalize;
}

static void
gst_v4l2src_buffer_init (GTypeInstance * instance, gpointer g_class)
{

}

static void
gst_v4l2src_buffer_finalize (GstV4l2SrcBuffer * v4l2src_buffer)
{
  GstV4l2Buffer *buf = v4l2src_buffer->buf;

  if (buf) {
    GST_LOG ("freeing buffer %p (nr. %d)", buf, buf->buffer.index);

    if (!g_atomic_int_dec_and_test (&buf->refcount)) {
      /* we're still in use, add to queue again
       * note: this might fail because the device is already stopped (race)
       */
      if (ioctl (buf->pool->video_fd, VIDIOC_QBUF, &buf->buffer) < 0)
        GST_INFO ("readding to queue failed, assuming video device is stopped");
    }
    if (g_atomic_int_dec_and_test (&buf->pool->refcount)) {
      /* we're last thing that used all this */
      gst_v4l2src_buffer_pool_free (buf->pool, TRUE);
    }
  }
}

/* Create a V4l2Src buffer from our mmap'd data area */
GstBuffer *
gst_v4l2src_buffer_new (GstV4l2Src * v4l2src, guint size, guint8 * data,
    GstV4l2Buffer * srcbuf)
{
  GstBuffer *buf;
  GstClockTime timestamp, duration;
  GstClock *clock;

  if (data == NULL) {
    buf = gst_buffer_new_and_alloc (size);
  } else {
    buf = (GstBuffer *) gst_mini_object_new (GST_TYPE_V4L2SRC_BUFFER);
    GST_BUFFER_DATA (buf) = data;
    GST_V4L2SRC_BUFFER (buf)->buf = srcbuf;
    GST_LOG_OBJECT (v4l2src,
        "creating buffer  %p (nr. %d)", srcbuf, srcbuf->buffer.index);
  }
  GST_BUFFER_SIZE (buf) = size;

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);

  /* timestamps, LOCK to get clock and base time. */
  GST_OBJECT_LOCK (v4l2src);
  if ((clock = GST_ELEMENT_CLOCK (v4l2src))) {
    /* we have a clock, get base time and ref clock */
    timestamp = GST_ELEMENT (v4l2src)->base_time;
    gst_object_ref (clock);
  } else {
    /* no clock, can't set timestamps */
    timestamp = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (v4l2src);

  if (clock) {
    /* the time now is the time of the clock minus the base time */
    timestamp = gst_clock_get_time (clock) - timestamp;
    gst_object_unref (clock);
  }

  if (v4l2src->fps_n > 0) {
    duration =
        gst_util_uint64_scale_int (GST_SECOND, v4l2src->fps_d, v4l2src->fps_n);
  } else {
    duration = GST_CLOCK_TIME_NONE;
  }
  GST_BUFFER_TIMESTAMP (buf) = timestamp;
  GST_BUFFER_DURATION (buf) = duration;

  /* offsets */
  GST_BUFFER_OFFSET (buf) = v4l2src->offset++;
  GST_BUFFER_OFFSET_END (buf) = v4l2src->offset;

  /* the negotiate() method already set caps on the source pad */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (v4l2src)));

  return buf;
}
