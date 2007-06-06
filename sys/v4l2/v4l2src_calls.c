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
    GST_DEBUG_OBJECT (v4l2src, "got format %" GST_FOURCC_FORMAT,
        GST_FOURCC_ARGS (format->pixelformat));

    v4l2src->formats = g_slist_prepend (v4l2src->formats, format);
  }
  v4l2src->formats = g_slist_reverse (v4l2src->formats);
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

/* The frame interval enumeration code first appeared in Linux 2.6.19. */
#ifdef VIDIOC_ENUM_FRAMEINTERVALS
static GstStructure *
gst_v4l2src_probe_caps_for_format_and_size (GstV4l2Src * v4l2src,
    guint32 pixelformat,
    guint32 width, guint32 height, const GstStructure * template)
{
  GstCaps *ret;
  gint fd = v4l2src->v4l2object->video_fd;
  struct v4l2_frmivalenum ival;
  guint32 num, denom;
  GstStructure *s;
  GValue rate = { 0, };

  ret = gst_caps_new_empty ();

  memset (&ival, 0, sizeof (struct v4l2_frmivalenum));
  ival.index = 0;
  ival.pixel_format = pixelformat;
  ival.width = width;
  ival.height = height;

  /* keep in mind that v4l2 gives us frame intervals (durations); we invert the
   * fraction to get framerate */

  if (ioctl (fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) < 0)
    goto enum_frameintervals_failed;

  if (ival.type == V4L2_FRMIVAL_TYPE_DISCRETE) {
    GValue frac = { 0, };

    g_value_init (&rate, GST_TYPE_LIST);

    g_value_init (&frac, GST_TYPE_FRACTION);

    do {
      num = ival.discrete.numerator;
      denom = ival.discrete.denominator;

      if (num > G_MAXINT || denom > G_MAXINT) {
        /* let us hope we don't get here... */
        num >>= 1;
        denom >>= 1;
      }

      gst_value_set_fraction (&frac, denom, num);
      gst_value_list_append_value (&rate, &frac);

      ival.index++;
    } while (ioctl (fd, VIDIOC_ENUM_FRAMEINTERVALS, &ival) >= 0);
  } else if (ival.type == V4L2_FRMIVAL_TYPE_STEPWISE) {
    GValue frac = { 0, };
    GValue step = { 0, };
    GValue max = { 0, };

    g_value_init (&rate, GST_TYPE_LIST);

    g_value_init (&frac, GST_TYPE_FRACTION);
    g_value_init (&step, GST_TYPE_FRACTION);
    g_value_init (&max, GST_TYPE_FRACTION);

    num = ival.stepwise.min.numerator;
    denom = ival.stepwise.min.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }
    gst_value_set_fraction (&frac, num, denom);

    num = ival.stepwise.step.numerator;
    denom = ival.stepwise.step.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }
    /* since we only have gst_value_fraction_subtract and not add, negate the
       numerator */
    gst_value_set_fraction (&step, -num, denom);

    num = ival.stepwise.max.numerator;
    denom = ival.stepwise.max.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }
    gst_value_set_fraction (&max, num, denom);

    while (gst_value_compare (&frac, &max) == GST_VALUE_LESS_THAN) {
      GValue frac = { 0, };
      g_value_init (&frac, GST_TYPE_FRACTION);
      /* invert */
      gst_value_set_fraction (&frac,
          gst_value_get_fraction_denominator (&frac),
          gst_value_get_fraction_numerator (&frac));
      gst_value_list_append_value (&rate, &frac);

      if (!gst_value_fraction_subtract (&frac, &frac, &step)) {
        GST_INFO_OBJECT (v4l2src, "could not step fraction!");
        break;
      }
    }
  } else if (ival.type == V4L2_FRMIVAL_TYPE_CONTINUOUS) {
    guint32 maxnum, maxdenom;

    g_value_init (&rate, GST_TYPE_FRACTION_RANGE);

    num = ival.stepwise.min.numerator;
    denom = ival.stepwise.min.denominator;
    if (num > G_MAXINT || denom > G_MAXINT) {
      num >>= 1;
      denom >>= 1;
    }

    maxnum = ival.stepwise.step.numerator;
    maxdenom = ival.stepwise.step.denominator;
    if (maxnum > G_MAXINT || maxdenom > G_MAXINT) {
      maxnum >>= 1;
      maxdenom >>= 1;
    }

    gst_value_set_fraction_range_full (&rate, maxdenom, maxnum, denom, num);
  } else {
    goto unknown_type;
  }

  s = gst_structure_copy (template);
  gst_structure_set (s, "width", G_TYPE_INT, (gint) width,
      "height", G_TYPE_INT, (gint) height, NULL);
  gst_structure_set_value (s, "framerate", &rate);
  g_value_unset (&rate);
  return s;

enum_frameintervals_failed:
  {
    GST_DEBUG_OBJECT (v4l2src,
        "Failed to enumerate frame sizes for %" GST_FOURCC_FORMAT "@%ux%u",
        GST_FOURCC_ARGS (pixelformat), width, height);
    return NULL;
  }
unknown_type:
  {
    /* I don't see how this is actually an error */
    GST_WARNING_OBJECT (v4l2src,
        "Unknown frame interval type at %" GST_FOURCC_FORMAT "@%ux%u: %u",
        GST_FOURCC_ARGS (pixelformat), width, height, ival.type);
    return NULL;
  }
}
#endif /* defined VIDIOC_ENUM_FRAMEINTERVALS */

GstCaps *
gst_v4l2src_probe_caps_for_format (GstV4l2Src * v4l2src, guint32 pixelformat,
    const GstStructure * template)
{
#ifdef VIDIOC_ENUM_FRAMESIZES
  GstCaps *ret;
  GstStructure *tmp;
  gint fd = v4l2src->v4l2object->video_fd;
  struct v4l2_frmsizeenum size;
  guint32 w, h;

  ret = gst_caps_new_empty ();

  memset (&size, 0, sizeof (struct v4l2_frmsizeenum));
  size.index = 0;
  size.pixel_format = pixelformat;

  if (ioctl (fd, VIDIOC_ENUM_FRAMESIZES, &size) < 0)
    goto enum_framesizes_failed;

  if (size.type == V4L2_FRMSIZE_TYPE_DISCRETE) {
    do {
      w = MIN (size.discrete.width, G_MAXINT);
      h = MIN (size.discrete.height, G_MAXINT);

      tmp = gst_v4l2src_probe_caps_for_format_and_size (v4l2src, pixelformat,
          w, h, template);
      if (tmp)
        gst_caps_append_structure (ret, tmp);

      size.index++;
    } while (ioctl (fd, VIDIOC_ENUM_FRAMESIZES, &size) >= 0);
  } else if (size.type == V4L2_FRMSIZE_TYPE_STEPWISE) {
    for (w = size.stepwise.min_width, h = size.stepwise.min_height;
        w < size.stepwise.max_width && h < size.stepwise.max_height;
        w += size.stepwise.step_width, h += size.stepwise.step_height) {
      if (w == 0 || h == 0)
        continue;

      tmp = gst_v4l2src_probe_caps_for_format_and_size (v4l2src, pixelformat,
          w, h, template);
      if (tmp)
        gst_caps_append_structure (ret, tmp);
    }
  } else if (size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS) {
    guint32 maxw, maxh;

    w = MAX (size.stepwise.min_width, 1);
    h = MAX (size.stepwise.min_height, 1);
    maxw = MIN (size.stepwise.max_width, G_MAXINT);
    maxh = MIN (size.stepwise.max_height, G_MAXINT);

    tmp = gst_v4l2src_probe_caps_for_format_and_size (v4l2src, pixelformat,
        w, h, template);
    if (tmp) {
      gst_structure_set (tmp, "width", GST_TYPE_INT_RANGE, (gint) w,
          (gint) maxw, "height", GST_TYPE_INT_RANGE, (gint) h, (gint) maxh,
          NULL);
      gst_caps_append_structure (ret, tmp);
    }
  } else {
    goto unknown_type;
  }

  return ret;

  /* ERRORS */
enum_framesizes_failed:
  {
    /* I don't see how this is actually an error */
    GST_DEBUG_OBJECT (v4l2src,
        "Failed to enumerate frame sizes for pixelformat %" GST_FOURCC_FORMAT
        " (%s)", GST_FOURCC_ARGS (pixelformat), g_strerror (errno));
    return NULL;
  }
unknown_type:
  {
    GST_WARNING_OBJECT (v4l2src,
        "Unknown frame sizeenum type for pixelformat %" GST_FOURCC_FORMAT
        ": %u", GST_FOURCC_ARGS (pixelformat), size.type);
    return NULL;
  }
#else /* defined VIDIOC_ENUM_FRAMESIZES */
  GstCaps *ret;
  GstStructure *tmp;

  /* This code is for Linux < 2.6.19 */

  ret = gst_caps_new_empty ();
  tmp = gst_structure_copy (template);
  gst_structure_set (tmp, "width", GST_TYPE_INT_RANGE, (gint) 1,
      (gint) GST_V4L2_MAX_SIZE, "height", GST_TYPE_INT_RANGE,
      (gint) 1, (gint) GST_V4L2_MAX_SIZE,
      "framerate", GST_TYPE_FRACTION_RANGE, (gint) 0,
      (gint) 1, (gint) 100, (gint) 1, NULL);
  gst_caps_append_structure (ret, tmp);
  return ret;
#endif /* defined VIDIOC_ENUM_FRAMESIZES */
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
#define NUM_TRIALS 50
  struct v4l2_buffer buffer;
  gint32 trials = NUM_TRIALS;

  memset (&buffer, 0x00, sizeof (buffer));
  buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  buffer.memory = V4L2_MEMORY_MMAP;
  while (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_DQBUF, &buffer) < 0) {

    GST_LOG_OBJECT (v4l2src,
        "problem grabbing frame %d (ix=%d), trials=%d, pool-ct=%d, buf.flags=%d",
        buffer.sequence, buffer.index, trials, v4l2src->pool->refcount,
        buffer.flags);

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
        goto enomem;
      case EIO:
        GST_INFO_OBJECT (v4l2src,
            "VIDIOC_DQBUF failed due to an internal error."
            " Can also indicate temporary problems like signal loss."
            " Note the driver might dequeue an (empty) buffer despite"
            " returning an error, or even stop capturing."
            " device %s", v4l2src->v4l2object->videodev);
        /* have we de-queued a buffer ? */
        if (!(buffer.flags & (V4L2_BUF_FLAG_QUEUED | V4L2_BUF_FLAG_DONE))) {
          /* this fails
             if ((buffer.index >= 0) && (buffer.index < v4l2src->breq.count)) {
             GST_DEBUG_OBJECT (v4l2src, "reenqueing buffer (ix=%ld)", buffer.index);
             gst_v4l2src_queue_frame (v4l2src, buffer.index);
             }
             else {
           */
          GST_DEBUG_OBJECT (v4l2src, "reenqueing buffer");
          if (ioctl (v4l2src->v4l2object->video_fd, VIDIOC_QBUF, &buffer) < 0) {
            goto qbuf_failed;
          }
          /*} */
        }
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
      memset (&buffer, 0x00, sizeof (buffer));
      buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer.memory = V4L2_MEMORY_MMAP;
    }
  }

  GST_LOG_OBJECT (v4l2src, "grabbed frame %d (ix=%d), pool-ct=%d",
      buffer.sequence, buffer.index, v4l2src->pool->refcount);

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
enomem:
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
 * gst_v4l2src_set_capture():
 *   set capture parameters
 * return value: TRUE on success, FALSE on error
 ******************************************************/
gboolean
gst_v4l2src_set_capture (GstV4l2Src * v4l2src, guint32 pixelformat,
    guint32 width, guint32 height, guint fps_n, guint fps_d)
{
  gint fd = v4l2src->v4l2object->video_fd;
  struct v4l2_format format;
  struct v4l2_streamparm stream;

  DEBUG ("Setting capture format to %dx%d, format %" GST_FOURCC_FORMAT,
      width, height, GST_FOURCC_ARGS (pixelformat));

  GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2src->v4l2object);

  memset (&format, 0x00, sizeof (struct v4l2_format));
  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

  if (ioctl (fd, VIDIOC_G_FMT, &format) < 0)
    goto get_fmt_failed;

  format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  format.fmt.pix.width = width;
  format.fmt.pix.height = height;
  format.fmt.pix.pixelformat = pixelformat;
  /* request whole frames; change when gstreamer supports interlaced video */
  format.fmt.pix.field = V4L2_FIELD_INTERLACED;

  if (ioctl (fd, VIDIOC_S_FMT, &format) < 0)
    goto set_fmt_failed;

  if (format.fmt.pix.width != width || format.fmt.pix.height != height)
    goto invalid_dimensions;

  if (format.fmt.pix.pixelformat != pixelformat)
    goto invalid_pixelformat;

  memset (&stream, 0x00, sizeof (struct v4l2_streamparm));
  stream.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  if (ioctl (fd, VIDIOC_G_PARM, &stream) < 0)
    goto get_parm_failed;

  /* Note: V4L2 gives us the frame interval, we need the frame rate */
  stream.parm.capture.timeperframe.numerator = fps_d;
  stream.parm.capture.timeperframe.denominator = fps_n;

  if (ioctl (fd, VIDIOC_S_PARM, &stream) < 0)
    goto set_parm_failed;


  /* FIXME: better test for fraction equality */
  if (stream.parm.capture.timeperframe.numerator != fps_d
      || stream.parm.capture.timeperframe.denominator != fps_n)
    goto invalid_framerate;

  return TRUE;

  /* ERRORS */
get_fmt_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Device '%s' does not support video capture"),
            v4l2src->v4l2object->videodev),
        ("Call to G_FMT failed: (%s)", g_strerror (errno)));
    return FALSE;
  }
set_fmt_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Device '%s' cannot capture at %dx%d"),
            v4l2src->v4l2object->videodev, width, height),
        ("Call to S_FMT failed for %" GST_FOURCC_FORMAT " @ %dx%d: %s",
            GST_FOURCC_ARGS (pixelformat), width, height, g_strerror (errno)));
    return FALSE;
  }
invalid_dimensions:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Device '%s' cannot capture at %dx%d"),
            v4l2src->v4l2object->videodev, width, height),
        ("Tried to capture at %dx%d, but device returned size %dx%d",
            width, height, format.fmt.pix.width, format.fmt.pix.height));
    return FALSE;
  }
invalid_pixelformat:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Device '%s' cannot capture in the specified format"),
            v4l2src->v4l2object->videodev),
        ("Tried to capture in %" GST_FOURCC_FORMAT
            ", but device returned format" " %" GST_FOURCC_FORMAT,
            GST_FOURCC_ARGS (pixelformat),
            GST_FOURCC_ARGS (format.fmt.pix.pixelformat)));
    return FALSE;
  }
get_parm_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Could not get parameters on device '%s'"),
            v4l2src->v4l2object->videodev), GST_ERROR_SYSTEM);
    return FALSE;
  }
set_parm_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Could not set parameters on device '%s'"),
            v4l2src->v4l2object->videodev), GST_ERROR_SYSTEM);
    return FALSE;
  }
invalid_framerate:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, SETTINGS,
        (_("Device '%s' cannot capture at %d/%d frames per second"),
            v4l2src->v4l2object->videodev, fps_n, fps_d),
        ("Tried to capture at %d/%d fps, but device returned %d/%d fps",
            fps_n, fps_d, stream.parm.capture.timeperframe.denominator,
            stream.parm.capture.timeperframe.numerator));
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
  gint fd = v4l2src->v4l2object->video_fd;
  struct v4l2_requestbuffers breq;

  memset (&breq, 0, sizeof (struct v4l2_requestbuffers));

  GST_DEBUG_OBJECT (v4l2src, "initializing the capture system");

  GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);
  GST_V4L2_CHECK_NOT_ACTIVE (v4l2src->v4l2object);

  if (v4l2src->v4l2object->vcap.capabilities & V4L2_CAP_STREAMING) {
    breq.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    breq.count = v4l2src->num_buffers;

    breq.memory = V4L2_MEMORY_MMAP;
    if (ioctl (fd, VIDIOC_REQBUFS, &breq) < 0)
      goto reqbufs_failed;

    if (breq.count < GST_V4L2_MIN_BUFFERS)
      goto no_buffers;

    if (v4l2src->num_buffers != breq.count) {
      v4l2src->num_buffers = breq.count;
      g_object_notify (G_OBJECT (v4l2src), "queue-size");
    }

    GST_INFO_OBJECT (v4l2src, "Got %d buffers of size %d kB",
        breq.count, v4l2src->frame_byte_size / 1024);

    /* Map the buffers */
    GST_LOG_OBJECT (v4l2src, "initiating buffer pool");

    v4l2src->pool = g_new (GstV4l2BufferPool, 1);
    gst_atomic_int_set (&v4l2src->pool->refcount, 1);
    v4l2src->pool->video_fd = fd;
    v4l2src->pool->buffer_count = v4l2src->num_buffers;
    v4l2src->pool->buffers = g_new0 (GstV4l2Buffer, v4l2src->num_buffers);

    for (n = 0; n < v4l2src->num_buffers; n++) {
      GstV4l2Buffer *buffer = &v4l2src->pool->buffers[n];

      gst_atomic_int_set (&buffer->refcount, 1);
      buffer->pool = v4l2src->pool;
      memset (&buffer->buffer, 0x00, sizeof (buffer->buffer));
      buffer->buffer.index = n;
      buffer->buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer->buffer.memory = V4L2_MEMORY_MMAP;

      if (ioctl (fd, VIDIOC_QUERYBUF, &buffer->buffer) < 0)
        goto querybuf_failed;

      buffer->start = mmap (0, buffer->buffer.length, PROT_READ | PROT_WRITE,
          MAP_SHARED, fd, buffer->buffer.m.offset);

      if (buffer->start == MAP_FAILED)
        goto mmap_failed;

      buffer->length = buffer->buffer.length;
    }

    GST_INFO_OBJECT (v4l2src, "capturing buffers via mmap()");
    v4l2src->use_mmap = TRUE;
  } else if (v4l2src->v4l2object->vcap.capabilities & V4L2_CAP_READWRITE) {
    GST_INFO_OBJECT (v4l2src, "capturing buffers via read()");
    v4l2src->use_mmap = FALSE;
    v4l2src->pool = NULL;
  } else {
    goto no_supported_capture_method;
  }

  GST_V4L2_SET_ACTIVE (v4l2src->v4l2object);

  return TRUE;

  /* ERRORS */
reqbufs_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not get buffers from device '%s'."),
            v4l2src->v4l2object->videodev),
        ("error requesting %d buffers: %s",
            v4l2src->num_buffers, g_strerror (errno)));
    return FALSE;
  }
no_buffers:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not get enough buffers from device '%s'."),
            v4l2src->v4l2object->videodev),
        ("we received %d from device '%s', we want at least %d",
            breq.count, v4l2src->v4l2object->videodev, GST_V4L2_MIN_BUFFERS));
    return FALSE;
  }
querybuf_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not retrieve buffer from device '%s'"),
            v4l2src->v4l2object->videodev),
        ("Failed QUERYBUF: %s", g_strerror (errno)));
    gst_v4l2src_capture_deinit (v4l2src);
    return FALSE;
  }
mmap_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not map memory in device '%s'."),
            v4l2src->v4l2object->videodev),
        ("mmap failed: %s", g_strerror (errno)));
    gst_v4l2src_capture_deinit (v4l2src);
    return FALSE;
  }
no_supported_capture_method:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("The driver of device '%s' does not support any known capture "
                "method."), v4l2src->v4l2object->videodev), (NULL));
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
  gint n;
  gint fd = v4l2src->v4l2object->video_fd;

  GST_DEBUG_OBJECT (v4l2src, "starting the capturing");
  //GST_V4L2_CHECK_OPEN (v4l2src->v4l2object);
  GST_V4L2_CHECK_ACTIVE (v4l2src->v4l2object);

  v4l2src->quit = FALSE;

  if (v4l2src->use_mmap) {
    for (n = 0; n < v4l2src->num_buffers; n++)
      if (!gst_v4l2src_queue_frame (v4l2src, n))
        goto queue_failed;

    if (ioctl (fd, VIDIOC_STREAMON, &type) < 0)
      goto streamon_failed;
  }

  v4l2src->is_capturing = TRUE;

  return TRUE;

  /* ERRORS */
queue_failed:
  {
    GST_ELEMENT_ERROR (v4l2src, RESOURCE, READ,
        (_("Could not enqueue buffers in device '%s'."),
            v4l2src->v4l2object->videodev),
        ("enqueing buffer %d/%d failed: %s",
            n, v4l2src->num_buffers, g_strerror (errno)));
    return FALSE;
  }
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

  if (v4l2src->use_mmap) {
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
  GST_DEBUG_OBJECT (v4l2src, "deinitting capture system");

  if (!GST_V4L2_IS_OPEN (v4l2src->v4l2object)) {
    return TRUE;
  }
  if (!GST_V4L2_IS_ACTIVE (v4l2src->v4l2object)) {
    return TRUE;
  }

  if (v4l2src->pool) {
    if (g_atomic_int_dec_and_test (&v4l2src->pool->refcount)) {
      /* FIXME: this is crack, should either use user pointer io method or some
         strategy similar to what filesrc uses with automatic freeing */
      gst_v4l2src_buffer_pool_free (v4l2src->pool, FALSE);
    }
    v4l2src->pool = NULL;
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
  GstClockTime timestamp;
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

  /* FIXME: use the timestamp from the buffer itself! */
  GST_BUFFER_TIMESTAMP (buf) = timestamp;

  /* offsets */
  GST_BUFFER_OFFSET (buf) = v4l2src->offset++;
  GST_BUFFER_OFFSET_END (buf) = v4l2src->offset;

  /* the negotiate() method already set caps on the source pad */
  gst_buffer_set_caps (buf, GST_PAD_CAPS (GST_BASE_SRC_PAD (v4l2src)));

  return buf;
}
