/* G-Streamer Video4linux2 video-capture plugin - system calls
 * Copyright (C) 2002 Ronald Bultje <rbultje@ronald.bitfreak.net>
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
#define MAP_FAILED ( (caddr_t) -1 )
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
    if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_ENUM_FMT,
            format) < 0) {
      if (errno == EINVAL) {
        break;                  /* end of enumeration */
      } else {
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS, (NULL),
            ("failed to get number %d in pixelformat enumeration for %s: %s",
                n, GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
        g_free (format);
        return FALSE;
      }
    }
    GST_LOG_OBJECT (v4l2src, "got format" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (format->pixelformat));
    v4l2src->formats = g_slist_prepend (v4l2src->formats, format);
  }

  return TRUE;
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

  if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_QBUF,
          &v4l2src->pool->buffers[i].buffer) < 0) {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, WRITE,
        (_("Could not write to device \"%s\"."),
            GST_V4L2ELEMENT (v4l2src)->videodev),
        ("Error queueing buffer %u on device %s", i, g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l2src_grab_frame ():
 *   grab a frame for capturing
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gint
gst_v4l2src_grab_frame (GstV4l2Src * v4l2src)
{
  struct v4l2_buffer buffer;

  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  while (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_DQBUF, &buffer) < 0) {
    /* if the sync() got interrupted, we can retry */
    switch (errno) {
      case EAGAIN:
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, SYNC, (NULL),
            ("Non-blocking I/O has been selected using O_NONBLOCK and"
                " no buffer was in the outgoing queue. device %s: %s",
                GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
        break;
      case EINVAL:
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, SYNC, (NULL),
            ("The buffer type is not supported, or the index is out of bounds,"
                " or no buffers have been allocated yet, or the userptr"
                " or length are invalid. device %s: %s",
                GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
        break;
      case ENOMEM:
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, SYNC, (NULL),
            ("isufficient memory to enqueue a user pointer buffer. device %s: %s",
                GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
        break;
      case EIO:
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, SYNC, (NULL),
            ("VIDIOC_DQBUF failed due to an internal error."
                " Can also indicate temporary problems like signal loss."
                " Note the driver might dequeue an (empty) buffer despite"
                " returning an error, or even stop capturing."
                " device %s: %s",
                GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
        break;
      case EINTR:
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, SYNC, (NULL),
            ("could not sync on a buffer on device %s: %s",
                GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
        break;
      default:
        GST_DEBUG_OBJECT (v4l2src, "grab got interrupted");
        break;
    }

    return -1;

  }

  GST_LOG_OBJECT (v4l2src, "grabbed frame %d", buffer.index);

  return buffer.index;
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

  GST_V4L2_CHECK_OPEN (GST_V4L2ELEMENT (v4l2src));

  v4l2src->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_G_FMT,
          &v4l2src->format) < 0) {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS, (NULL),
        ("failed to get pixelformat for device %s: %s",
            GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
    return FALSE;
  }

  return TRUE;
}


/******************************************************
 * gst_v4l2src_set_capture():
 *   set capture parameters
 * return value: TRUE on success, FALSE on error
 ******************************************************/

gboolean
gst_v4l2src_set_capture (GstV4l2Src * v4l2src,
    struct v4l2_fmtdesc * fmt, gint width, gint height)
{
  DEBUG ("Setting capture format to %dx%d, format %s",
      width, height, fmt->description);

  GST_V4L2_CHECK_OPEN (GST_V4L2ELEMENT (v4l2src));
  GST_V4L2_CHECK_NOT_ACTIVE (GST_V4L2ELEMENT (v4l2src));

  memset (&v4l2src->format, 0, sizeof (struct v4l2_format));
  v4l2src->format.fmt.pix.width = width;
  v4l2src->format.fmt.pix.height = height;
  v4l2src->format.fmt.pix.pixelformat = fmt->pixelformat;
  v4l2src->format.fmt.pix.field = V4L2_FIELD_INTERLACED;
  v4l2src->format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_S_FMT,
          &v4l2src->format) < 0) {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS, (NULL),
        ("failed to set pixelformat to %s @ %dx%d for device %s: %s",
            fmt->description, width, height,
            GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
    return FALSE;
  }

  /* update internal info */
  return gst_v4l2src_get_capture (v4l2src);
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

  GST_DEBUG_OBJECT (v4l2src, "initting the capture system");

  GST_V4L2_CHECK_OPEN (GST_V4L2ELEMENT (v4l2src));
  GST_V4L2_CHECK_NOT_ACTIVE (GST_V4L2ELEMENT (v4l2src));

  /* request buffer info */
  buffers = v4l2src->breq.count;
  if (v4l2src->breq.count > GST_V4L2_MAX_BUFFERS) {
    v4l2src->breq.count = GST_V4L2_MAX_BUFFERS;
  }
  if (v4l2src->breq.count < GST_V4L2_MIN_BUFFERS) {
    v4l2src->breq.count = GST_V4L2_MIN_BUFFERS;
  }
  v4l2src->breq.type = v4l2src->format.type;
  if (GST_V4L2ELEMENT (v4l2src)->vcap.capabilities & V4L2_CAP_STREAMING) {
    v4l2src->breq.memory = V4L2_MEMORY_MMAP;
    if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_REQBUFS,
            &v4l2src->breq) < 0) {
      GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
          (_("Could not get buffers from device \"%s\"."),
              GST_V4L2ELEMENT (v4l2src)->videodev),
          ("error requesting %d buffers: %s", v4l2src->breq.count,
              g_strerror (errno)));
      return FALSE;
    }
    GST_LOG_OBJECT (v4l2src, "using default mmap method");
  } else if (GST_V4L2ELEMENT (v4l2src)->vcap.capabilities & V4L2_CAP_READWRITE) {
    v4l2src->breq.memory = 0;
    GST_INFO_OBJECT (v4l2src, "using fallback read method");
  } else {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("the driver of device \"%s\" is broken."),
            GST_V4L2ELEMENT (v4l2src)->videodev),
        ("no supported read capability from %s",
            GST_V4L2ELEMENT (v4l2src)->videodev));
    return FALSE;
  }

  if (v4l2src->breq.memory > 0) {
    if (v4l2src->breq.count < GST_V4L2_MIN_BUFFERS) {
      GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
          (_("Could not get enough buffers from device \"%s\"."),
              GST_V4L2ELEMENT (v4l2src)->videodev),
          ("we received %d, we want at least %d", v4l2src->breq.count,
              GST_V4L2_MIN_BUFFERS));
      v4l2src->breq.count = buffers;
      return FALSE;
    }
    if (v4l2src->breq.count != buffers)
      g_object_notify (G_OBJECT (v4l2src), "num_buffers");

    GST_INFO_OBJECT (v4l2src,
        "Got %d buffers (" GST_FOURCC_FORMAT ") of size %d KB\n",
        v4l2src->breq.count,
        GST_FOURCC_ARGS (v4l2src->format.fmt.pix.pixelformat),
        v4l2src->format.fmt.pix.sizeimage / 1024);

    /* Map the buffers */
    GST_LOG_OBJECT (v4l2src, "initiating buffer pool");

    v4l2src->pool = g_new (GstV4l2BufferPool, 1);
    gst_atomic_int_set (&v4l2src->pool->refcount, 1);
    v4l2src->pool->video_fd = GST_V4L2ELEMENT (v4l2src)->video_fd;
    v4l2src->pool->buffer_count = v4l2src->breq.count;
    v4l2src->pool->buffers = g_new0 (GstV4l2Buffer, v4l2src->breq.count);

    for (n = 0; n < v4l2src->breq.count; n++) {
      GstV4l2Buffer *buffer = &v4l2src->pool->buffers[n];

      gst_atomic_int_set (&buffer->refcount, 1);
      buffer->pool = v4l2src->pool;
      buffer->buffer.index = n;
      buffer->buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_QUERYBUF,
              &buffer->buffer) < 0) {
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ, (NULL),
            ("Could not get buffer properties of buffer %d: %s", n,
                g_strerror (errno)));
        gst_v4l2src_capture_deinit (v4l2src);
        return FALSE;
      }
      buffer->start =
          mmap (0, buffer->buffer.length, PROT_READ | PROT_WRITE, MAP_SHARED,
          GST_V4L2ELEMENT (v4l2src)->video_fd, buffer->buffer.m.offset);
      if (buffer->start == MAP_FAILED) {
        GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ, (NULL),
            ("Could not mmap video buffer %d: %s", n, g_strerror (errno)));
        buffer->start = 0;
        gst_v4l2src_capture_deinit (v4l2src);
        return FALSE;
      }
      buffer->length = buffer->buffer.length;
      if (!gst_v4l2src_queue_frame (v4l2src, n)) {
        gst_v4l2src_capture_deinit (v4l2src);
        return FALSE;
      }
    }
  } else {
    GST_LOG_OBJECT (v4l2src, "no buffer pool used");
    v4l2src->pool = NULL;
  }

  GST_V4L2_SET_ACTIVE (GST_V4L2ELEMENT (v4l2src));

  return TRUE;
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

  GST_V4L2_CHECK_OPEN (GST_V4L2ELEMENT (v4l2src));
  if (!GST_V4L2_IS_ACTIVE (GST_V4L2ELEMENT (v4l2src))) {
    /* gst_pad_renegotiate (v4l2src->srcpad); FIX: is it still required in 0.10 */
  }
  GST_V4L2_CHECK_ACTIVE (GST_V4L2ELEMENT (v4l2src));

  v4l2src->quit = FALSE;

  if (v4l2src->breq.memory != 0) {
    if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_STREAMON, &type) < 0) {
      GST_ELEMENT_ERROR (v4l2src, RESOURCE, OPEN_READ, (NULL),
          ("Error starting streaming capture from device %s: %s",
              GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
      return FALSE;
    }
  }

  v4l2src->is_capturing = TRUE;

  return TRUE;
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
  GST_V4L2_CHECK_OPEN (GST_V4L2ELEMENT (v4l2src));
  GST_V4L2_CHECK_ACTIVE (GST_V4L2ELEMENT (v4l2src));

  if (v4l2src->breq.memory != 0) {
    /* we actually need to sync on all queued buffers but not
     * on the non-queued ones */
    if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_STREAMOFF,
            &type) < 0) {
      GST_ELEMENT_ERROR (v4l2src, RESOURCE, CLOSE, (NULL),
          ("Error stopping streaming capture from device %s: %s",
              GST_V4L2ELEMENT (v4l2src)->videodev, g_strerror (errno)));
      return FALSE;
    }
  }

  /* make an optional pending wait stop */
  v4l2src->quit = TRUE;
  v4l2src->is_capturing = FALSE;

  return TRUE;
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

#if 0

void
gst_v4l2src_free_buffer (GstBuffer * buffer)
{
  GstV4l2Buffer *buf = (GstV4l2Buffer *) GST_BUFFER_PRIVATE (buffer);

  GST_LOG ("freeing buffer %p (nr. %d)", buffer, buf->buffer.index);

  if (!g_atomic_int_dec_and_test (&buf->refcount)) {
    /* we're still in use, add to queue again 
       note: this might fail because the device is already stopped (race) */
    if (ioctl (buf->pool->video_fd, VIDIOC_QBUF, &buf->buffer) < 0)
      GST_INFO ("readding to queue failed, assuming video device is stopped");
  }
  if (g_atomic_int_dec_and_test (&buf->pool->refcount)) {
    /* we're last thing that used all this */
    gst_v4l2src_buffer_pool_free (buf->pool, TRUE);
  }
}


#endif

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

  GST_V4L2_CHECK_OPEN (GST_V4L2ELEMENT (v4l2src));
  GST_V4L2_CHECK_ACTIVE (GST_V4L2ELEMENT (v4l2src));

  if (v4l2src->pool) {
    /* free the buffers */
    for (i = 0; i < v4l2src->breq.count; i++) {
      if (g_atomic_int_dec_and_test (&v4l2src->pool->buffers[i].refcount)) {
        if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_DQBUF,
                &v4l2src->pool->buffers[i].buffer) < 0)
          GST_WARNING_OBJECT (v4l2src,
              "Could not dequeue buffer on uninitialization: %s - will try reinit instead",
              g_strerror (errno));
        try_reinit = TRUE;
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
      if (!gst_v4l2src_capture_start (v4l2src) ||
          !gst_v4l2src_capture_stop (v4l2src))
        return FALSE;
    }
  }

  GST_V4L2_SET_INACTIVE (GST_V4L2ELEMENT (v4l2src));
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

  GST_LOG_OBJECT (v4l2src, "getting size limits with format " GST_FOURCC_FORMAT,
      GST_FOURCC_ARGS (format->pixelformat));

  /* get size delimiters */
  memset (&fmt, 0, sizeof (fmt));
  fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  fmt.fmt.pix.width = 0;
  fmt.fmt.pix.height = 0;
  fmt.fmt.pix.pixelformat = format->pixelformat;
  fmt.fmt.pix.field = V4L2_FIELD_INTERLACED;
  if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_TRY_FMT, &fmt) < 0) {
    return FALSE;
  }

  if (min_w)
    *min_w = fmt.fmt.pix.width;
  if (min_h)
    *min_h = fmt.fmt.pix.height;
  GST_LOG_OBJECT (v4l2src, "got min size %dx%d", fmt.fmt.pix.width,
      fmt.fmt.pix.height);

  fmt.fmt.pix.width = G_MAXINT;
  fmt.fmt.pix.height = 576;
  if (ioctl (GST_V4L2ELEMENT (v4l2src)->video_fd, VIDIOC_TRY_FMT, &fmt) < 0) {
    return FALSE;
  }

  if (max_w)
    *max_w = fmt.fmt.pix.width;
  if (max_h)
    *max_h = fmt.fmt.pix.height;
  GST_LOG_OBJECT (v4l2src, "got max size %dx%d", fmt.fmt.pix.width,
      fmt.fmt.pix.height);

  return TRUE;
}

gboolean
gst_v4l2src_get_fps (GstV4l2Src * v4l2src, gint * fps_n, gint * fps_d)
{
  v4l2_std_id std;
  const GList *item;

  if (!GST_V4L2_IS_OPEN (GST_V4L2ELEMENT (v4l2src)))
    return FALSE;

  if (!gst_v4l2_get_norm (GST_V4L2ELEMENT (v4l2src), &std))
    return FALSE;
  for (item = GST_V4L2ELEMENT (v4l2src)->stds; item != NULL; item = item->next) {
    GstV4l2TunerNorm *v4l2norm = item->data;

    if (v4l2norm->index == std) {
      *fps_n =
          gst_value_get_fraction_numerator (&GST_TUNER_NORM (v4l2norm)->
          framerate);
      *fps_d =
          gst_value_get_fraction_denominator (&GST_TUNER_NORM (v4l2norm)->
          framerate);
      return TRUE;
    }
  }

  return FALSE;

}

#if 0

/* get a list of possible framerates
 * this is only done for webcams;
 * other devices return NULL here.
 * this function takes a LONG time to execute.
 */
GValue *
gst_v4l2src_get_fps_list (GstV4l2Src * v4l2src)
{
  gint fps_index;
  struct video_window *vwin = &GST_V4L2ELEMENT (v4l2src)->vwin;
  GstV4l2Element *v4l2element = GST_V4L2ELEMENT (v4l2src);

  /* check if we have vwin window properties giving a framerate,
   * as is done for webcams
   * See http://www.smcc.demon.nl/webcam/api.html
   * which is used for the Philips and qce-ga drivers */
  fps_index = (vwin->flags >> 16) & 0x3F;       /* 6 bit index for framerate */

  /* webcams have a non-zero fps_index */
  if (fps_index == 0) {
    GST_DEBUG_OBJECT (v4l2src, "fps_index is 0, no webcam");
    return NULL;
  }
  GST_DEBUG_OBJECT (v4l2src, "fps_index is %d, so webcam", fps_index);

  {
    int i;
    GValue *list = NULL;
    GValue value = { 0 };

    /* webcam detected, so try all framerates and return a list */

    list = g_new0 (GValue, 1);
    g_value_init (list, GST_TYPE_LIST);

    /* index of 16 corresponds to 15 fps */
    GST_DEBUG_OBJECT (v4l2src, "device reports fps of %d/%d (%.4f)",
        fps_index * 15, 16, fps_index * 15.0 / 16);
    for (i = 0; i < 63; ++i) {
      /* set bits 16 to 21 to 0 */
      vwin->flags &= (0x3F00 - 1);
      /* set bits 16 to 21 to the index */
      vwin->flags |= i << 16;
      if (gst_v4l2_set_window_properties (v4l2element)) {
        /* setting it succeeded.  FIXME: get it and check. */
        g_value_init (&value, GST_TYPE_FRACTION);
        gst_value_set_fraction (&value, i * 15, 16);
        gst_value_list_append_value (list, &value);
        g_value_unset (&value);
      }
    }
    /* FIXME: set back the original fps_index */
    vwin->flags &= (0x3F00 - 1);
    vwin->flags |= fps_index << 16;
    gst_v4l2_set_window_properties (v4l2element);
    return list;
  }
  return NULL;
}

#endif

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
         note: this might fail because the device is already stopped (race) */
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
  gint fps_n, fps_d;

  GST_DEBUG_OBJECT (v4l2src, "creating buffer %d");

  g_return_val_if_fail (gst_v4l2src_get_fps (v4l2src, &fps_n, &fps_d), NULL);

  buf = (GstBuffer *) gst_mini_object_new (GST_TYPE_V4L2SRC_BUFFER);

  GST_V4L2SRC_BUFFER (buf)->buf = srcbuf;

  if (data == NULL) {
    GST_BUFFER_DATA (buf) = g_malloc (size);
  } else {
    GST_BUFFER_DATA (buf) = data;
  }
  GST_BUFFER_SIZE (buf) = size;

  GST_BUFFER_TIMESTAMP (buf) =
      gst_clock_get_time (GST_ELEMENT (v4l2src)->clock);
  GST_BUFFER_TIMESTAMP (buf) -= GST_ELEMENT (v4l2src)->base_time;

  GST_BUFFER_FLAG_SET (buf, GST_BUFFER_FLAG_READONLY);
  GST_BUFFER_DURATION (buf) = gst_util_uint64_scale_int (GST_SECOND,
      fps_n, fps_d);

  /* the negotiate() method already set caps on the source pad */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (v4l2src)));

  return buf;
}
